/*
 * This file is part of Aegis Validator
 *
 * Copyright (C) 2002-2003 Ericsson, Inc
 * Copyright (C) 2008-2010 Nokia Corporation and/or its subsidiary(-ies).
 *
 * Contact: Markku Kylänpää <ext-markku.kylanpaa@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * Authors: David Gordon, 2003
 *          Serge Hallyn, 2004
 *          Makan Pourzandi, 2003-2005
 *          Vincent Roy, 2003
 *          Chris Wright, 2004
 *          Markku Kylänpää, 2008-2010
 */

/*
 * This file contains Aegis Validator integrity validator for the kernel.
 * The set of function was defined by the LSM interface.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/security.h>
#include <linux/kmod.h>
#include <linux/elf.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/vmalloc.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/mutex.h>
#include <linux/socket.h>
#include <linux/skbuff.h>
#include <net/netlink.h>
#include <net/sock.h>
#include <linux/aegis/creds.h>
#include <linux/aegis/credp.h>
#include "verify.h"
#include "validator.h"
#include "fs.h"
#include "cache.h"
#include "hashlist.h"
#include "enforce.h"
#include "sidcheck.h"
#include "platsec.h"
#include "modlist.h"

/* Validator netlink socket */
#define NETLINK_VALIDATOR 25

/* Netlink socket to be used to notify userspace */
static struct sock *validator_netlink;

/* Aegis Validator state information */
struct validator_info valinfo;

/*
 * Prevent concurrent access to hashlist loading. Using semaphore, because of
 * long lock hold time.
 */
static DECLARE_MUTEX(hashlist_loading);

/* Console log messages for verification failures */
static const char *reason_message[] = {
	"unknown error",
	"source origin check",
	"no reference hash",
	"attribute check",
	"incorrect hash",
	"no reference hashlist",
	"internal error",
	"interrupted syscall",
};

static inline unsigned long get_inode_security(struct inode *ino)
{
	return (unsigned long)(ino->i_security);
}

static inline void set_inode_security(struct inode *ino, unsigned long val)
{
	ino->i_security = (void *)val;
}

static inline unsigned long get_file_security(struct file *file)
{
	return (unsigned long)(file->f_security);
}

static inline void set_file_security(struct file *file, unsigned long val)
{
	file->f_security = (void *)val;
}

/**
 * send_netlink_message() - Send message using netlink socket
 * @data: message
 *
 * Netlink message is sent to userspace listener.
 *
 * Return 0 for success and negative value for an error.
 */
static int send_netlink_message(const char *data)
{
	int r;
	size_t skblen;
	sk_buff_data_t tmp;
	struct sk_buff *skb;
	struct nlmsghdr *nlh;

	skblen = NLMSG_SPACE(strlen(data) + 1);
	skb = alloc_skb(skblen, GFP_USER);
	if (!skb) {
		r = -ENOBUFS;
		goto out1;
	}
	tmp = skb->tail;
	nlh = nlmsg_put(skb, 0, 0, 0, skblen - sizeof(*nlh), 0);
	if (!nlh) {
		r = -ENOBUFS;
		goto out2;
	}
	strcpy(NLMSG_DATA(nlh), data);
	nlh->nlmsg_len = skb->tail - tmp;
	NETLINK_CB(skb).dst_group = 1;
	netlink_broadcast(validator_netlink, skb, 0, 1, GFP_USER);
	return 0;
out2:
	kfree(skb);
out1:
	return r;
}

/**
 * deny_write_access_file() - Deny access while processing the file
 * @file:        Kernel file handle
 * @allow_write: Allow writing after exit if > 0
 *
 * If the file is opened for writing, deny mmap(PROT_EXEC) access. Otherwise,
 * increment the inode->i_security->mapcount, which is our own writecount.
 * When the file is closed, f->f_security will be 1, and so we will decrement
 * the inode->i_security->mapcount.
 *
 * Just to be clear: file->f_security is 1 or 0. inode->i_security->mapcount
 * is the *number* of processes which have this file mmapped(PROT_EXEC), so
 * it can be >1.
 *
 * Return 0 for success and negative value for an error.
 */
static int deny_write_access_file(struct file *file, int *allow_write)
{
	struct inode *inode = file->f_dentry->d_inode;
	unsigned long isec;

	spin_lock(&inode->i_lock);
	if (get_file_security(file) > 0) {
		spin_unlock(&inode->i_lock);
		return 0;
	}
	*allow_write = 1;
	isec = get_inode_security(inode);
	if (atomic_read(&inode->i_writecount) > 0)
		goto errbusy;
	if (WARN_ON(isec == ULONG_MAX))
		goto errbusy;
	else
		set_inode_security(inode, isec + 1);
	set_file_security(file, 1);
	spin_unlock(&inode->i_lock);
	return 0;
errbusy:
	*allow_write = 0;
	spin_unlock(&inode->i_lock);
	pr_info("Aegis: cannot measure file %s (process: %s)\n",
		file->f_dentry->d_name.name, current->comm);
	return -ETXTBSY;
}

/**
 * allow_write_access_file() - Decrement our writer count on the inode.
 * @file: Kernel file handle
 *
 * When the writer count hits 0, we will again allow opening the inode for
 * writing. LSM hook function validator_inode_permission checks i_security
 * before allowing writing.
 */
static void allow_write_access_file(struct file *file)
{
	struct inode *inode = file->f_dentry->d_inode;
	unsigned long isec;

	spin_lock(&inode->i_lock);
	isec = get_inode_security(inode);
	if (!WARN_ON(isec == 0))
		set_inode_security(inode, (isec - 1));
	set_file_security(file, 0);
	spin_unlock(&inode->i_lock);
}

/**
 * delete_from_verification_cache() - Remove an entry from verification cache
 * @inode: Pointer to an inode to be removed from the verification cache
 *
 * If the verification cache has an entry bound to this inide it will be
 * removed.
 */
static void delete_from_verification_cache(struct inode *inode)
{
	long src_id;
	if (!valinfo.g_init)
		return;
	if (validator_cache_contains(inode, &src_id))
		validator_cache_remove(inode);
	return;
}

/**
 * call_init_helper() - calls Aegis Validator helper
 * @path: userspace pathname to look for reference hashlist
 *
 * This is used to request reference hashes from userspace helper application.
 * Mount point of the volume is put as a parameter to invoked application.
 * Return zero for success and negative value for an error.
 */
static int call_init_helper(const char *path)
{
	char *argv[3];
	char *envp[3];
	int rv, i;

	i = 0;
	argv[i++] = CONFIG_SECURITY_AEGIS_VALIDATOR_INIT_PATH;
	argv[i++] = (char *)path;
	argv[i] = NULL;

	i = 0;
	/* minimal command environment */
	envp[i++] = "HOME=/";
	envp[i++] = "PATH=/sbin:/bin:/usr/sbin:/usr/bin";
	envp[i] = NULL;

	pr_info("Aegis: Invoking userspace helper\n");
	rv = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
	if (valinfo.h_init == 0) {
		if (rv >= 0) {
			pr_info("Aegis: enabled (config data protection)\n");
			valinfo.h_init = 1;
		} else {
			/*
			 * If this is non-HS device or R&D certificate is
			 * present disable Validator if userspace helper
			 * invocation failed.
			 */
			if (check_rd_certificate() == 0) {
				pr_info("Aegis: disabled (%d)\n", rv);
				valinfo.g_init = 0;
			}
		}
	}
	return rv;
}

/**
 * pathname_check() - check validator-init pathname
 * @file: file pointer for validator-init
 *
 * Check whether the given file pointer pathname is matching to the pathname
 * of the validator-init userspace helper.
 *
 * Return 0 if there is a filename match to validator-init userspace helper
 * application. Otherwise return negative value.
 */
static int pathname_check(struct file *file)
{
	char *buf = NULL;
	size_t buflen = sizeof(CONFIG_SECURITY_AEGIS_VALIDATOR_INIT_PATH);
	char *p;
	int r;

	buf = kmalloc(buflen, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	p = dentry_path(file->f_dentry, buf, buflen);
	if (IS_ERR(p)) {
		r = -EINVAL;
		goto out;
	}
	r = (strcmp(CONFIG_SECURITY_AEGIS_VALIDATOR_INIT_PATH, p) == 0) ? 0
		: -EINVAL;
out:
	kfree(buf);
	return r;
}

/**
 * initialize_root_hashlist() - Initialize reference hashlist for rootfs
 * @file: File that triggered this call
 *
 * If the reference hashlist for rootfs is not yet loaded try to load it.
 *
 * Return zero if the rootfs hash list has already been loaded or if loading
 * was succesful. Return one if this is a validation request for validator-init
 * executable. Otherwise negative error value is returned.
 */
static int initialize_root_hashlist(struct file *file)
{
	int r;
	long src_id = 0L;
	int allow_write_on_exit = 0;

	if (valinfo.h_init > 0)
		return 0;
	if (validator_cache_contains(file->f_dentry->d_inode, &src_id))
		return 0;
	r = deny_write_access_file(file, &allow_write_on_exit);
	if (r < 0)
		return r;
	if (valinfo.v_init) {
		r = validator_verify_refhash(file, valinfo.vcode);
		if (r == 0) {
			pr_info("Aegis: vhash code matched to %s\n",
				file->f_dentry->d_name.name);
			validator_cache_add(file->f_dentry->d_inode, 0L);
			r = 1;
			goto out;
		}
	}

	/*
	 * If there is R&D certificate present in the device then allow
	 * userspace helper application to run just because the pathname
	 * is matching even if the vcode SHA1 hash did not match. This is
	 * needed in development phase and can be removed.
	 */
	if ((check_rd_certificate() == 0) && (pathname_check(file) == 0)) {
		validator_cache_add(file->f_dentry->d_inode, 0L);
		pr_info("Aegis: R&D certificate found\n");
		r = 1;
		goto out;
	}

	down(&hashlist_loading);
	if (valinfo.h_init == 0) {
		r = call_init_helper("/");
		if (r >= 0) {
			pr_info("Aegis: hashlist initialized %s\n",
				file->f_dentry->d_name.name);
		} else {
			/*
			 * call_init_helper() function has turned off Validator
			 * if userspace invocation failed and if there is BB5
			 * R&D certificate in device or the device is non-HS
			 * device. Otherwise panic should be generated.
			 *
			 * FIXME: This should be "blinking panic".
			 */
			if (valinfo.g_init)
				panic("Aegis: userspace helper failed\n");
			else
				pr_info("Aegis: hashlist init failed (ignored)"
					" %s\n", file->f_dentry->d_name.name);
		}
	}
	up(&hashlist_loading);
out:
	if (allow_write_on_exit)
		allow_write_access_file(file);
	return r;
}

/*
 * The following ipp_* functions are used in Integrity Protection Policy
 * checks in validation() function.
 */

/**
 * ipp_check_cache() - is the file entry already cached?
 * @file: file to be measured
 * @data: measurement context
 *
 * Return 0 for success and negative value if the entry is not found in cache.
 * Set also source identifier if found from cache.
 */
static inline int ipp_check_cache(struct file *file, struct vmetadata *data)
{
	int r = validator_cache_contains(file->f_dentry->d_inode, &data->sid);
	return r ? 0 : -ENOENT;
}

/**
 * ipp_check_sid() - source identifier check
 * @file: file to be measured
 * @data: measurement context
 * @cred: Credentials for the source check
 *
 * Return 0 for success and negative value for fail.
 */
static inline int ipp_check_sid(struct file *file, struct vmetadata *data,
				const struct cred *cred)
{
	int r;
	if (!valinfo.s_init)
		return 0;
	r = validator_sid_check(file->f_dentry->d_name.name, data->sid, cred);
	return r;
}

/**
 * ipp_check_hashlist() - get reference value from hashlist
 * @file: file to be measured
 * @data: measurement context
 *
 * Return 0 for success and negative value for fail.
 */
static inline int ipp_check_hashlist(struct file *file, struct vmetadata *data)
{
	return validator_hashlist_get_data(file->f_dentry->d_inode, data);
}

/**
 * ipp_check_listed() - do we check only files listed in refhashlist?
 * @file: file to be measured
 * @data: measurement context
 *
 * Return 0 for success and negative value for fail.
 */
static inline int ipp_check_listed(struct file *file, struct vmetadata *data)
{
	if (valinfo.listed_only)
		return 0;
	return -EFAULT;
}

/**
 * ipp_check_attrib() - check file attributes
 * @file: file to be measured
 * @data: measurement context
 *
 * Return 0 for success and negative value for fail.
 */
static inline int ipp_check_attrib(struct file *file, struct vmetadata *data)
{
	int r = 0;
	if (valinfo.a_init) {
		struct inode *inode = file->f_dentry->d_inode;
		if (!inode) {
			r = -EFAULT;
		} else {
			if (inode->i_uid != data->uid)
				r = -EFAULT;
			if (inode->i_gid != data->gid)
				r = -EFAULT;
			if (inode->i_mode != data->mode)
				r = -EFAULT;
		}
	}
	return valinfo.attribmode ? r : 0;
}

/**
 * ipp_check_dynamic() - is this file marked as dynamic
 * @file: file to be measured
 * @data: measurement context
 *
 * Return 0 for success and negative value for fail.
 */
static inline int ipp_check_dynamic(struct file *file, struct vmetadata *data)
{
	return (data->nodetype == DYNAMIC_DATA_FILE) ? 0 : -EINVAL;
}

/**
 * ipp_check_hash() - calculate hash and compare agains reference value
 * @file: file to be measured
 * @data: measurement context
 *
 * Return 0 for success and negative value for fail.
 */
static inline int ipp_check_hash(struct file *file, struct vmetadata *data)
{
	return validator_verify_refhash(file, data->refhash);
}

/**
 * ipp_hashlist_load() - try to load more hashes
 * @file: file to be measured
 * @data: measurement context
 *
 * Typically only root filesystem hashes are loaded. If other filesystems
 * are mounted and some files are read/executed try to invoke userspace
 * helper to load new hashes. Return 0 for success and negative value for
 * an error.
 */
static inline int ipp_hashlist_load(struct file *file, struct vmetadata *data)
{
	int r;
	char *buffer;
	char *bufptr;
	const int buflen = PATH_MAX;

	if (valinfo.hashreq == 0)
		return 0;
	buffer = kmalloc(buflen, GFP_KERNEL);
	if (!buffer) {
		pr_err("Aegis: Memory allocation error in hlist_load\n");
		return -ENOMEM;
	}
	bufptr = dentry_path(file->f_vfsmnt->mnt_mountpoint, buffer, buflen);
	if (IS_ERR(bufptr)) {
		pr_err("Aegis: cannot get mount point pathname\n");
		r = -EFAULT;
		goto out;
	}
	if (file->f_vfsmnt->mnt_parent != NULL) {
		bufptr = dentry_path(
			file->f_vfsmnt->mnt_parent->mnt_mountpoint, buffer,
			buflen);
		if (IS_ERR(bufptr)) {
			pr_err("Aegis: cannot get mount parent pathname\n");
			r = -EFAULT;
			goto out;
		}
	}
	r = call_init_helper(bufptr);
out:
	kfree(buffer);
	return (r >= 0) ? 0 : -EFAULT;
}

/**
 * ipp_cache_add() - add this entry to cache
 * @file: file to be measured
 * @data: measurement context
 *
 * Return 0 for success and negative value for an error
 */
static inline int ipp_cache_add(struct file *file, struct vmetadata *data)
{
	return validator_cache_add(file->f_dentry->d_inode, data->sid);
}

/**
 * ipp_immutable() - check whether the file open needs extra checks
 * @file: file to be measured
 * @data: measurement context
 *
 * If the file is in immutable directory we need to process file opening.
 *
 * Return 0 if we should not process this file open. Otherwise negative value.
 */
static inline int ipp_immutable(struct file *file, struct vmetadata *data)
{
	/*
	 * Directory listing should be allowed for directories.
	 */
	if (S_ISDIR(file->f_dentry->d_inode->i_mode))
		return -EINVAL;

	/*
	 * It is now possible to have also directory entries in reference
	 * hashlist. Presence of the entry can be checked using the same
	 * function as files but hash value has no meaning here.
	 *
	 * FIXME: We probably should have a different list of special dirs
	 */
	return validator_hashlist_entry(file->f_dentry->d_parent->d_inode);
}

/**
 * ipp_check_write_perm() - check file modification permission
 * @inode: inode value
 *
 * Check whether there is special constraints for file modification. These
 * can be specified in the reference hashlist.
 *
 * Return 0 if write is allowed and negative value for an error.
 */
static inline int ipp_check_write_perm(struct inode *inode)
{
	int i;
	struct vprotection *v;

	v = validator_hashlist_get_wcreds(inode);
	if (!v)
		return 0;
	for (i = 0; i < v->num; i++)
		if (creds_khave_p(v->credtype[i], v->credvalue[i]) == 1)
			return 0;
	return -EPERM;
}

/**
 * exe_validation() - Validate executable from cache or do measurement
 * @file:   file to be validated
 * @reason: reason for validation failure
 * @cred: Credentials for the source check
 *
 * Validate a file entry first checking from cache and then comparing the
 * calculated SHA1 hash value against the stored one. Source identifier check
 * is also made. Positive verification result adds the entry into verification
 * cache.
 *
 * Inode locking is only for executables now. This is important during boot
 * time to prevent multiple verifications when many applications map large
 * libraries and start new hash verifications.
 *
 * Zero value is returened if validation was succesful and negative value for
 * an error.
 */
static int exe_validation(struct file *file, int *reason,
			  const struct cred *cred)
{
	struct vmetadata data;
	int r;
	struct inode *inode = file->f_dentry->d_inode;

	*reason = R_OK;
	mutex_lock(&inode->i_mutex);
	r = ipp_check_cache(file, &data);
	if (r)
		goto getrefhash;
	r = ipp_check_sid(file, &data, cred);
	if (r)
		*reason = R_SID;
	goto out;
getrefhash:
	r = ipp_check_hashlist(file, &data);
	if (r == 0)
		goto otherchecks;
	r = ipp_check_listed(file, &data);
	if (r == 0) {
		data.sid = valinfo.devorig;
		if (!data.sid) {
			data.sid = validator_sid_define("");
			if (data.sid <= 0) {
				*reason = R_SID;
				goto out;
			}
			valinfo.devorig = data.sid;
		}
		r = ipp_check_sid(file, &data, cred);
		*reason = (r) ? R_SID : R_OK;
		goto out;
	}
	r = ipp_hashlist_load(file, &data);
	if (r) {
		*reason = R_LOAD;
		goto out;
	}
	r = ipp_check_hashlist(file, &data);
	if (r) {
		*reason = R_HLIST;
		goto out;
	}
otherchecks:
	r = ipp_check_sid(file, &data, cred);
	if (r) {
		*reason = R_SID;
		goto out;
	}
	r = ipp_check_attrib(file, &data);
	if (r) {
		*reason = R_ATTRIB;
		goto out;
	}
	r = ipp_check_hash(file, &data);
	if (r) {
		*reason = (r == -EINTR) ? R_EINTR : R_HASH;
		goto out;
	}
	r = ipp_cache_add(file, &data);
	if (r)
		*reason = R_CACHE;
out:
	mutex_unlock(&inode->i_mutex);
	return r;
}

/**
 * data_validation() - Validate data from cache or do measurement
 * @file:   file to be validated
 * @reason: reason for validation failure
 *
 * Validate data file entry by comparing the calculated SHA1 hash value
 * against the stored one. These checks are only made for files that
 * are in a directory that is marked to be "immutable". The directory can
 * also contain files that will not be checked, but the files should be
 * marked to be "dynamic".
 *
 * Zero value is returened if validation was succesful and negative value for
 * an error.
 */
static int data_validation(struct file *file, int *reason)
{
	struct vmetadata data;
	int r;

	*reason = R_OK;
	r = ipp_immutable(file, &data);
	if (r) {
		r = 0;
		goto out;
	}
	r = ipp_check_hashlist(file, &data);
	if (r) {
		*reason = R_HLIST;
		goto out;
	}
	r = ipp_check_dynamic(file, &data);
	if (r == 0)
		goto out;
	r = ipp_check_attrib(file, &data);
	if (r) {
		*reason = R_ATTRIB;
		goto out;
	}
	r = ipp_check_hash(file, &data);
	if (r) {
		*reason = (r == -EINTR) ? R_EINTR : R_HASH;
		goto out;
	}
out:
	return r;
}

/**
 * reasonmsg() - verification failure description string
 * @reason: verification failure code
 *
 * Return description for console log to describe why verification
 * fails.
 */
static const char *reasonmsg(int reason)
{
	if (reason < ARRAY_SIZE(reason_message) && (reason > 0))
		return reason_message[reason];
	return reason_message[0];
}

/**
 * notify_userspace() - generate error message to userspace
 * @file:   a file whose verification failed
 * @hook:   verification hook that triggered the error
 * @reason: verification failure code
 *
 * Return zero for success and negative value for an error.
 */
static int notify_userspace(struct file *file, int hook, int reason)
{
	char *msg;
	int len;
	char *p;
	int r = 0;

	/*
	 * Interrupted system call is not really integrity violation. Do not
	 * report that error to userspace listener.
	 */
	if (reason == R_EINTR)
		return 0;
	msg = (char *)__get_free_page(GFP_KERNEL);
	if (!msg) {
		r = -ENOMEM;
		pr_err("Aegis: allocation for userspace notification failed\n");
		goto out1;
	}
	len = snprintf(msg, PAGE_SIZE, "\nFail: %d (%s)\nMethod: %d\n"
		       "Process: %s\nFile: ", reason, reasonmsg(reason), hook,
		       current->comm);
	p = dentry_path(file->f_dentry, msg + len, PAGE_SIZE - len);
	if (!IS_ERR(p) && (p > msg)) {
		char *end = mangle_path(msg + len, p, "\n\\");
		if (!end) {
			r = -EFAULT;
			pr_err("Aegis: pathname mangling failed\n");
			goto out2;
		}
		if (end < msg + PAGE_SIZE - 2) {
			*end++ = '\n';
			*end = '\0';
		} else {
			r = -EFAULT;
			pr_err("Aegis: failed to terminate mangled path\n");
			goto out2;
		}
	} else {
		pr_err("Aegis: dentry not found\n");
		r = -ENOENT;
		goto out2;
	}
	r = send_netlink_message(msg);
	if (r)
		pr_err("Aegis: validator netlink message sending failed\n");
out2:
	free_page((unsigned long)msg);
out1:
	return r;
}

/**
 * process_measurement() - Validate integrity of the file
 * @file: File to be validated
 * @hook: Hook that triggered measurement mmap/bprm/path_check
 * @cred: Credentials for the source check
 *
 * Validate the file. Return negative value for an error and zero for success.
 */
static int process_measurement(struct file *file, int hook,
			       const struct cred *cred)
{
	int allow_write_on_exit = 0;
	int retval;
	int reason;

	retval = initialize_root_hashlist(file);
	if (retval < 0)
		return valinfo.mode ?
			((hook == PATH_CHECK) ? -EACCES : -EPERM) : 0;
	if (retval > 0)
		return 0;
	if (hook != PATH_CHECK) {
		retval = deny_write_access_file(file, &allow_write_on_exit);
		if (retval < 0)
			return -EPERM;
		retval = exe_validation(file, &reason, cred);
	} else {
		retval = data_validation(file, &reason);
	}
	if (retval < 0) {
		pr_err("Aegis: %s verification failed (%s)\n",
		       file->f_dentry->d_name.name, reasonmsg(reason));
		notify_userspace(file, hook, reason);
	}
	if (allow_write_on_exit)
		allow_write_access_file(file);
	return (valinfo.mode && retval) ?
		((hook == PATH_CHECK) ? -EACCES : -EPERM) : 0;
}

/******************************************************************************
 * LSM HOOK FUNCTIONS
 *****************************************************************************/

/**
 * validator_file_mmap() - LSM hook function for file_mmap
 * @file:      contains the file structure for file to map (may be NULL).
 * @reqprot:   contains the protection requested by the application.
 * @calcprot:  contains the protection that will be applied by the kernel.
 * @flags:     contains the operational flags.
 * @addr:      address attempted to be mapped
 * @addr_only: >0 check only address against mmap minimum address
 *
 * This hook is triggered when executable binary or shared library is loaded.
 * FIXME: Parameters addr and addr_only are not processed and checked. Those
 *        were added to LSM API later to enable further checking on mmap
 *        address.
 *
 * Return 0 if permission is granted.
 */
static int validator_file_mmap(struct file *file, unsigned long reqprot,
			       unsigned long calcprot, unsigned long flags,
			       unsigned long addr, unsigned long addr_only)
{
	unsigned long prot = reqprot;

	if (!valinfo.g_init)
		return 0;
	if (!(prot & VM_EXEC))
		return 0;
	if (!file)
		return 0;
	if (!file->f_dentry)
		return 0;
	if (!file->f_dentry->d_name.name)
		return 0;

	return process_measurement(file, MMAP_CHECK, NULL);
}

/**
 * validator_file_free_security() - LSM hook function for file_free_security
 * @file: contains the file structure being modified.
 *
 * The file is being closed.  If we ever mmaped it for exec, then
 * file->f_security > 0, and we decrement the inode usage count to show that we
 * are done with it.
 */
static void validator_file_free_security(struct file *file)
{
	if (!valinfo.g_init)
		return;
	if (get_file_security(file))
		allow_write_access_file(file);
}

/**
 * validator_inode_permission() - LSM hook function for inode_permission
 * @inode: contains the inode structure to check.
 * @mask:  contains the permission mask.
 *
 * For a file being opened for write, check:
 * 1. whether it is a library currently being dlopen'ed.  If it is, then
 *    inode->i_security > 0.
 * 2. whether the file being opened is an executable or library with a cached
 *    signature validation. If it is, remove the signature validation entry so
 *    that on the next load, the signature will be recomputed.
 *
 * Then we allow the write to happen and will check the reference hash when
 * loading to decide about the validity of the action.
 *
 * Return 0 if permission is granted.
 */
static int validator_inode_permission(struct inode *inode, int mask)
{
	long src_id;

	if (!valinfo.g_init)
		return 0;
	if (inode && mask & MAY_WRITE) {
		unsigned long isec = get_inode_security(inode);
		if (isec > 0)
			return -EPERM;
		if (validator_cache_contains(inode, &src_id))
			validator_cache_remove(inode);
	}
	return 0;
}

/**
 * validator_inode_create() - LSM hook for inode creation
 * @dir:    Directory where new inode will be added
 * @dentry: Dentry for a file to be added to the directory
 * @fmode:  File mode
 *
 * Check if we are trying to add new file to immutable directory.
 *
 * Return 0 if creation is allowed and negative value for an error.
 */
static int validator_inode_create(struct inode *dir, struct dentry *dentry,
				  int fmode)
{
	int r;

	if (!valinfo.g_init)
		return 0;
	if (!valinfo.r_init)
		return 0;
	r = ipp_check_write_perm(dir);
	return r ? -EACCES : 0;
}

/**
 * validator_inode_rename() - LSM hook for inode rename
 * @old_dir:    Directory from where inode will be renamed
 * @old_dentry: Old file dentry
 * @new_dir:    Directory to where inode will be renamed
 * @new_dentry: New file dentry
 *
 * Check if we are trying to rename a file which is in immutable directory or
 * if we are trying to move a file to immutable directory.
 *
 * Return 0 if creation is allowed and negative value for an error.
 */
static int validator_inode_rename(struct inode *old_dir,
				  struct dentry *old_dentry,
				  struct inode *new_dir,
				  struct dentry *new_dentry)
{
	int r;

	if (!valinfo.g_init)
		return 0;
	if (!valinfo.r_init)
		return 0;
	r = ipp_check_write_perm(old_dir);
	if (r)
		return -EACCES;
	r = ipp_check_write_perm(new_dir);
	if (r)
		return -EACCES;
	delete_from_verification_cache(old_dentry->d_inode);
	return 0;
}

/**
 * validator_inode_unlink() - LSM hook function for inode_unlink
 * @dir:    contains the inode structure of parent directory of the file.
 * @dentry: contains the dentry structure for file to be unlinked.
 *
 * If an inode is unlinked, we don't want to hang onto it's signature
 * validation ticket
 *
 * Return 0 if permission is granted.
 */
static int validator_inode_unlink(struct inode *dir, struct dentry *dentry)
{
	int r;

	if (!valinfo.g_init)
		return 0;
	r = validator_hashlist_entry(dentry->d_inode);
	if (r)
		return 0;
	if (valinfo.r_init) {
		r = ipp_check_write_perm(dir);
		if (r)
			return -EACCES;
		r = ipp_check_write_perm(dentry->d_inode);
		if (r)
			return -EACCES;
	}
	delete_from_verification_cache(dentry->d_inode);
	if (dentry->d_inode->i_nlink == 1)
		if (validator_hashlist_delete_item(dentry->d_inode))
			pr_err("Aegis: cannot delete from reference hashlist "
			       "(name=%s ino=%ld)\n", dentry->d_name.name,
			       dentry->d_inode->i_ino);
	return 0;
}

/**
 * validator_inode_delete() - inode is deleted
 * @inode: contains the inode structure
 *
 * Unlink hook is not called if the file was renamed. Remove an inode
 * entry from cache and reference hashlist if the inode is deleted.
 * Note that this LSM hook will disappear in 2.6.35.
 */
static void validator_inode_delete(struct inode *inode)
{
	if (!valinfo.g_init)
		return;
	delete_from_verification_cache(inode);
	if (validator_hashlist_entry(inode) == 0)
		validator_hashlist_delete_item(inode);
}

/**
 * validator_inode_free_security() - LSM hook function for inode_free_security
 * @inode: contains the inode structure.
 *
 * Deallocate the inode security structure.
 */
static void validator_inode_free_security(struct inode *inode)
{
	if (!valinfo.g_init)
		return;
	inode->i_security = NULL;
}

/**
 * validator_bprm_check_security() - LSM hook function for bprm_check_security
 * @bprm: context parameter
 *
 * This LSM hook is always called when an application is started. It is
 * possible to use this to verify scripts, which are started as commands.
 * If the script is started as
 *
 *   $ ./myscript.sh
 *
 * then the script will be verified. However, if the script is started using
 * some other way as
 *
 *   $ sh myscript.sh
 *   $ sh < myscript.sh
 *   $ . myscript.sh
 *   $ cat myscript.sh | sh
 *
 * it will not be verified by this mechanism. If there is a need to protect
 * also in these cases then protection should be built into script interpreter
 * itself.
 *
 * Return 0 if OK and other value if execution should be prevented.
 */
static int validator_bprm_check_security(struct linux_binprm *bprm)
{
	if (!valinfo.g_init)
		return 0;
	return process_measurement(bprm->file, BPRM_CHECK, bprm->cred);
}

/**
 * validator_dentry_open() - check to run when opening a file
 * @file: contains the file structure for file to be opened
 * @cred: process credentials
 *
 * This function is similar to ima_path_check() function which is embedded
 * into fs/namei.c may_open() function to catch file open attempts. This
 * function is implemented as LSM hook and is meant to catch file open
 * attempts from certain special directories.
 *
 * Return 0 for success and negative value for an error.
 */
static int validator_dentry_open(struct file *file, const struct cred *cred)
{
	int r;

	/*
	 * A list of simple checks for cases where we could skip these data
	 * file open checks:
	 *
	 * - validator is not initialized for hash and data file checks
	 * - file is opened for writing but not for reading
	 * - parent inode is NULL
	 * - reading character or block device files
	 * - accessing tmpfs files
	 */
	if (!valinfo.g_init)
		return 0;
	if (valinfo.h_init == 0)
		return 0;
	if (valinfo.r_init == 0)
		return 0;
	if (current->in_execve)
		return 0;
	if (!file->f_dentry->d_parent->d_inode) {
		pr_info("Aegis: No parent entry found\n");
		return -EACCES;
	}
	switch (file->f_dentry->d_parent->d_inode->i_mode & S_IFMT) {
	case S_IFBLK:
	case S_IFCHR:
		return 0;
		break;
	default:
		break;
	}
	r = validator_hashlist_entry(file->f_dentry->d_parent->d_inode);
	if (r)
		return 0;
	if (file->f_mode & FMODE_WRITE) {
		r = ipp_check_write_perm(file->f_dentry->d_parent->d_inode);
		if (r)
			return -EACCES;
		r = ipp_check_write_perm(file->f_dentry->d_inode);
		if (r)
			return -EACCES;
	}
	if (file->f_mode & FMODE_READ)
		r = process_measurement(file, PATH_CHECK, NULL);
	return r ? -EACCES : 0;
}

/**
 * validator_netlink_send() - filter netlink messages
 * @sk: netlink socket
 * @skb: message buffer
 *
 * Block userspace NETLINK_VALIDATOR messages.
 *
 * Return 0 if netlink message OK and negative value to block.
 */
static int validator_netlink_send(struct sock *sk, struct sk_buff *skb)
{
	if (sk && (sk->sk_protocol == NETLINK_VALIDATOR)) {
		pr_err("Aegis: validator netlink message blocked\n");
		return -EPERM;
	}
	return cap_netlink_send(sk, skb);
}

/**
 * validator_sb_free_security() - LSM hook to be called during unmounting.
 * @sb:   Superblock
 *
 * Umount should remove mount-point specific refhashlist
 */
static void validator_sb_free_security(struct super_block *sb)
{
	if (!valinfo.g_init)
		return;
	if (sb->s_security) {
		validator_cache_fsremove(sb->s_dev);
		validator_hashlist_delete(sb->s_security);
		sb->s_security = NULL;
	}
}

/* LSM hooks */
static struct security_operations validator_security_ops = {
	.name                = "aegis",
	.netlink_send        = validator_netlink_send,
	.file_mmap           = validator_file_mmap,
	.file_free_security  = validator_file_free_security,
	.dentry_open         = validator_dentry_open,
	.inode_permission    = validator_inode_permission,
	.inode_unlink        = validator_inode_unlink,
	.inode_delete        = validator_inode_delete,
	.inode_create        = validator_inode_create,
	.inode_rename        = validator_inode_rename,
	.load_module         = validator_kmod_check,
	.bprm_check_security = validator_bprm_check_security,
	.inode_free_security = validator_inode_free_security,
#if CONFIG_SECURITY_AEGIS_CREDP
	.task_setgroups      = credp_task_setgroups,
	.task_setgid         = credp_task_setgid,
	.ptrace_access_check = credp_ptrace_access_check,
	.ptrace_traceme      = credp_ptrace_traceme,
#endif
	.sb_free_security    = validator_sb_free_security
};


/******************************************************************************
 * LINUX MODULE FUNCTIONS
 *****************************************************************************/

/**
 * hexconvert() - Convert hexadecimal character to binary representation
 * @c: Hexadecimal character.
 *
 * Assumes that input values are already checked to be hexadecimal.
 *
 * Returns binary value for the hexadecimal character.
 */
static u8 __init hexconvert(unsigned char c)
{
	return isdigit(c) ? c - '0' : toupper(c) - 'A' + 10;
}

/**
 * hash_string2bin() - Convert hexadecimal SHA1 hash to binary representation
 * @str: Hexadecimal string (length must be 40 characters)
 * @buf: 20 byte buffer for SHA1 value. Result is stored here.
 *
 * Return 0 for success. Return -ERANGE if input string has wrong size.
 * Return -EINVAL if there were non-hexadecimal characters in the string.
 */
static int __init hash_string2bin(char *str, char *buf)
{
	int i;
	int j = 0;

	if (strlen(str) != (2 * SHA1_HASH_LENGTH))
		return -ERANGE;
	for (i = 0; i < 2 * SHA1_HASH_LENGTH; i++) {
		if (!isxdigit(str[i]))
			return -EINVAL;
	}
	for (i = 0; i < SHA1_HASH_LENGTH; i++) {
		buf[i] = hexconvert(str[j++]) << 4;
		buf[i] |= hexconvert(str[j++]);
	}
	return 0;
}

/**
 * init_vhash() - Initialize vhash parameter from kernel command-line
 * @str: Command-line value
 *
 * Bootloader can pass SHA1 hash of the hashlist loading executable as
 * kernel command-line parameter. The hash value is used to authorize
 * execution of the hashlist loading executable when hashlist is not
 * yet loaded.
 *
 * Return 1 for success and -1 for an error.
 */
static int __init init_vhash(char *str)
{
	int r;
	r = hash_string2bin(str, valinfo.vcode);
	if (r == 0)
		valinfo.v_init = 1;
	else
		pr_err("Aegis: Bad vhash parameter (%d)\n", r);
	return r ? r : 1;
}
__setup("vhash=", init_vhash);

/**
 * validator_init_module() - Activate security module.
 *
 * Module initialization code. This should be added to some initcall. Calls
 * various functions to initialize data structures. Because there is nowadays
 * no way to unregister security module, module exit code has been removed.
 *
 * Return zero for success and negative value for an error.
 */
static int __init validator_init_module(void)
{
	if (valinfo.v_init) {
		size_t buflen = 4 * SHA1_HASH_LENGTH;
		char *buffer = kmalloc(buflen, GFP_KERNEL);
		if (!buffer) {
			pr_err("Aegis: Memory allocation error in init\n");
		} else {
			int i;
			char *bufptr = buffer;
			for (i = 0; i < SHA1_HASH_LENGTH; i++) {
				sprintf(bufptr, "%02x", valinfo.vcode[i]);
				bufptr += 2;
			}
			pr_info("Aegis: init vhash=%s\n", buffer);
			kfree(buffer);
		}
	} else {
		pr_info("Aegis: init - vhash is not set\n");
	}
	if (validator_cache_init()) {
		pr_err("Aegis: Error in validator cache initialization\n");
		return -ENOMEM;
	}
	if (!security_module_enable(&validator_security_ops)) {
		pr_notice("Aegis: Validator NOT registered as LSM!\n");
		return 0;
	}
	if (register_security(&validator_security_ops)) {
		pr_err("Aegis: Unable to register validator with kernel.\n");
		return -EFAULT;
	}
	valinfo.g_init = 1;
	return 0;
}

/**
 * validator_netlink_init() - Activate netlink socket
 *
 * Validator creates a netlink socket that is used to notify
 * userspace listener about integrity verification errors.
 * This is done in separate initcall.
 */
static int __init validator_netlink_init(void)
{
	validator_netlink = netlink_kernel_create(&init_net, NETLINK_VALIDATOR,
						  0, NULL, NULL, THIS_MODULE);
	if (validator_netlink == NULL)
		pr_err("Aegis: Cannot create netlink socket\n");
	else
		pr_info("Aegis: Netlink socket created\n");
	netlink_set_nonroot(NETLINK_VALIDATOR, NL_NONROOT_RECV);
	return 0;
}

security_initcall(validator_init_module);
device_initcall(validator_netlink_init);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Aegis Validator OS integrity verification framework");
MODULE_AUTHOR("Markku Kylanpaa (based on DigSig)");

