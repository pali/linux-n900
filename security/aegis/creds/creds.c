/*
 * This file is part of AEGIS
 *
 * Copyright (C) 2009-2010 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * Author: Markku Savela
 */

/**
 * DOC: Provide access to credential information.
 *
 * This service attemps to give a tool for user space access control
 * in client/server architecture. When a client issues a request to a
 * server, the server may wish to check whether the client is
 * authorized for the requested operation.
 *
 * This service gives the server a way to read the credentials of the
 * client process and it can permform the desired credential checks
 * (via a companion user space library called 'libcreds', which is a
 * user of this service).
 *
 * Because this is targeted for access control, the returned
 * credentials are the *effective* credentials.
 *
 * The interface is designed to be stable, even if new credential
 * types are added or removed. It is insentive to addition or removal
 * of capability bits.
 *
 * Without this service, getting information about the credentials of
 * another process, is only possible by parsing the
 * "/proc/<pid>/status" content. In addition to being somewhat
 * fragile, if the output format changes, it only provides maximum of
 * 32 supplementary groups.
 *
 * In addition to credentials retrieval, this also provides
 * translations between string and numeric values of
 * credentials. Currently only capabilities names need to be provided
 * and handled by the kernel.
 *
 * If a companion component 'restok' is available, this provides a
 * gateway for translations of symbols defined there. The restok
 * defined symbols are currently mapped into credentials via use of
 * supplementary groups. Other mappings, like defining a totally new
 * credential type for those, are possible in future.
 */
#ifdef CONFIG_SECURITY_AEGIS_RESTOK
#define HAVE_RESTOK /* If restok is built in, it's available */
#else
#ifdef CONFIG_SECURITY_AEGIS_RESTOK_MODULE
#ifdef MODULE
#define HAVE_RESTOK /* If restok is module, it's available if creds is also */
#endif /* MODULE */
#endif /* CONFIG_SECURITY_AEGIS_RESTOK_MODULE */
#endif /* CONFIG_SECURITY_AEGIS_RESTOK */

#include <linux/file.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/pid.h>
#include <linux/net.h>
#include <net/af_unix.h>
#include <linux/capability.h>
#include <linux/security.h>
#include <linux/namei.h>
#include <linux/sockios.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>
#include <linux/user_namespace.h>
#include <linux/cred.h>
#include <linux/aegis/creds.h>
#include <linux/aegis/credsctl.h>

#ifdef HAVE_RESTOK
#include <linux/aegis/restok.h>
#endif

#define CREDS_NAME "creds"

#undef INFO
#undef ERR
#define INFO(args...) pr_info(CREDS_NAME ": " args)
#define ERR(args...) pr_err(CREDS_NAME ": " args)

/**
 * struct creds_str: Holds constant string and length
 * @str: The string
 * @len: The length
 */
struct creds_str {
	const char *const str;
	size_t len;
};
#define CREDS_STR(s) {s, sizeof(s)-1}

/*
 * Static mapping of capability names <-> values
 *
 * Other kernel may require similar things. The array of capability
 * names should be provided globally by commoncap.c or capability.c.
 */
static const struct creds_str cap_names[] = {
	[CAP_CHOWN]		= CREDS_STR("chown"),
	[CAP_DAC_OVERRIDE]	= CREDS_STR("dac_override"),
	[CAP_DAC_READ_SEARCH]	= CREDS_STR("dac_read_search"),
	[CAP_FOWNER]		= CREDS_STR("fowner"),
	[CAP_FSETID]		= CREDS_STR("fsetid"),
	[CAP_KILL]		= CREDS_STR("kill"),
	[CAP_SETGID]		= CREDS_STR("setgid"),
	[CAP_SETUID]		= CREDS_STR("setuid"),
	[CAP_SETPCAP]		= CREDS_STR("setpcap"),
	[CAP_LINUX_IMMUTABLE]	= CREDS_STR("linux_immutable"),
	[CAP_NET_BIND_SERVICE]	= CREDS_STR("net_bind_service"),
	[CAP_NET_BROADCAST]	= CREDS_STR("net_broadcast"),
	[CAP_NET_ADMIN]		= CREDS_STR("net_admin"),
	[CAP_NET_RAW]		= CREDS_STR("net_raw"),
	[CAP_IPC_LOCK]		= CREDS_STR("ipc_lock"),
	[CAP_IPC_OWNER]		= CREDS_STR("ipc_owner"),
	[CAP_SYS_MODULE]	= CREDS_STR("sys_module"),
	[CAP_SYS_RAWIO]		= CREDS_STR("sys_rawio"),
	[CAP_SYS_CHROOT]	= CREDS_STR("sys_chroot"),
	[CAP_SYS_PTRACE]	= CREDS_STR("sys_ptrace"),
	[CAP_SYS_PACCT]		= CREDS_STR("sys_pacct"),
	[CAP_SYS_ADMIN]		= CREDS_STR("sys_admin"),
	[CAP_SYS_BOOT]		= CREDS_STR("sys_boot"),
	[CAP_SYS_NICE]		= CREDS_STR("sys_nice"),
	[CAP_SYS_RESOURCE]	= CREDS_STR("sys_resource"),
	[CAP_SYS_TIME]		= CREDS_STR("sys_time"),
	[CAP_SYS_TTY_CONFIG]	= CREDS_STR("sys_tty_config"),
	[CAP_MKNOD]		= CREDS_STR("mknod"),
	[CAP_LEASE]		= CREDS_STR("lease"),
	[CAP_AUDIT_WRITE]	= CREDS_STR("audit_write"),
	[CAP_AUDIT_CONTROL]	= CREDS_STR("audit_control"),
	[CAP_SETFCAP]		= CREDS_STR("setfcap"),
	[CAP_MAC_OVERRIDE]	= CREDS_STR("mac_override"),
	[CAP_MAC_ADMIN]		= CREDS_STR("mac_admin"),
};
#define CAP_NAMES_SIZE ARRAY_SIZE(cap_names)

static const struct creds_str creds_map[CREDS_MAX] = {
	[CREDS_CAP] = CREDS_STR("CAP::"),
	[CREDS_UID] = CREDS_STR("UID::"),
	[CREDS_GID] = CREDS_STR("GID::"),
	[CREDS_GRP] = CREDS_STR("GRP::"),
};

/**
 * put_string - Copy a string into user space
 * @src: The string to copy (creds_str)
 * @dst: Destination address in user space
 * @dst_len: Room at destination
 *
 * Copy a string to user space, truncate as needed and make it NUL
 * terminated (if possible).
 *
 * Return negative error, or the actual length of the creds_str.
 */
static int creds_put_string(const struct creds_str *src,
			    char __user *dst, size_t dst_len)
{
	size_t copy_len = min(src->len+1, dst_len);

	if (!copy_len)
		return src->len;
	copy_len -= 1;
	if (copy_to_user(dst, src->str, copy_len))
		return -EFAULT;
	if (put_user(0, dst+copy_len))
		return -EFAULT;
	return src->len;
}

/**
 * creds_cap2str - Convert capability number to capability name
 *
 * @cap: The capability number
 * @str: The return buffer (in user space)
 * @str_len: The length of the return buffer.
 *
 * Return the length of the capability (not including terminating
 * NUL byte). If the return value is larger than str_len, then
 * the name could not be returned fully.
 */
static long creds_cap2str(long cap, __user char *str, size_t str_len)
{
	if (cap < 0 || cap >= CAP_NAMES_SIZE)
		return -EINVAL;
	return creds_put_string(&cap_names[cap], str, str_len);
}

/**
 * creds_str2cap - Convert capability name to capability number
 *
 * @str: The name of the capability name (kernel space)
 * @str_len: The length of the capability name.
 * @value: Holds capability number on successful conversion
 *
 * Return the capability type CREDS_CAP, or -EIVAL, if conversion
 * fails.
 */
static long creds_str2cap(const char *str, size_t str_len, long *value)
{
	int cap;
	for (cap = 0; cap < CAP_NAMES_SIZE; ++cap) {
		if (str_len == cap_names[cap].len &&
		    memcmp(cap_names[cap].str, str, str_len) == 0) {
			*value = cap;
			return CREDS_CAP;
		}
	}
	return -EINVAL;
}

/*
 * The pos = [0..CAP_NAMES_SIZE-1] returns capability. The
 * CAP_NAMES_SIZE <= pos < CAP_NAMES_SIZE+CREDS_MAX returns known
 * typenames
 */
static void *creds_seq_start(struct seq_file *m, loff_t *pos)
{
	const unsigned index = *pos;
	if (index >= CAP_NAMES_SIZE + CREDS_MAX)
		return NULL;
	return (void *)(index+1);
}

static void creds_seq_stop(struct seq_file *m, void *v)
{
}

static void *creds_seq_next(struct seq_file *m, void *v, loff_t *pos)
{
	const unsigned index = (unsigned)v;

	if (index > CAP_NAMES_SIZE + CREDS_MAX)
		return NULL;
	++*pos;
	return (void *)(index+1);
}

static int creds_seq_show(struct seq_file *m, void *v)
{
	unsigned index = (unsigned)v - 1;

	if (index < CAP_NAMES_SIZE) {
		seq_printf(m, "%d::%d\tCAP::", CREDS_CAP, index);
		if (cap_names[index].str)
			seq_printf(m, "%s\n", cap_names[index].str);
		else
			seq_printf(m, "%d\n", index);
		return 0;
	}
	index -= CAP_NAMES_SIZE;
	if (index < CREDS_MAX) {
		seq_printf(m, "%d::-1\t", index);
		if (creds_map[index].str)
			seq_printf(m, "%s\n", creds_map[index].str);
		else
			seq_printf(m, "%d::\n", index);
		return 0;
	}
	return -EFAULT;
}

static const struct seq_operations creds_seq_ops = {
	.start = creds_seq_start,
	.next = creds_seq_next,
	.stop = creds_seq_stop,
	.show = creds_seq_show
};

/**
 * creds_kstr2creds - Convert credential string to binary
 * @str: The credential string to convert (NUL terminated)
 * @value: Converted binary value
 *
 * Return the type of the credential value, if conversion was
 * possible; otherwise returns -1.
 *
 * This only provides full translation for capabilities. For
 * persistent UID and GID/GRP credentials this only provides the
 * translation for the credentials type. The full translation is left
 * for the user space library, which will access the /etc/passwd and
 * /etc/group for translating those translations.
 *
 * The request to translate credentials type is a bare credential
 * prefix ending with "::". This returns successful conversion with
 * correct type, but value as -1.
 *
 * Restok component, when available, can provide additional
 * translations for transient values mapped to credential
 * type GRP by default.
 */
long creds_kstr2creds(const char *str, long *value)
{
	int type;
	size_t len;

	if (!value || !str)
		return -EINVAL;
	len =  strlen(str);
	*value = -1;
	for (type = 0; type < CREDS_MAX; ++type) {
		const size_t cmp_len = creds_map[type].len;

		if (!cmp_len || cmp_len > len ||
		    memcmp(creds_map[type].str, str, cmp_len))
			continue;
		if (len == cmp_len)
			return type;
		if (type != CREDS_CAP)
			return -EINVAL;
		return creds_str2cap(str + cmp_len, len - cmp_len, value);
	}
#ifdef HAVE_RESTOK
	*value = restok_locate(str);
	if (*value > 0)
		return CREDS_GRP;
#endif
	return -EINVAL;
}
EXPORT_SYMBOL(creds_kstr2creds);

/**
 * creds_kcreds2str - convert binary credential into string
 *
 * @type: Type of the credential to convert
 * @value: Value of the credential to convert
 * @str: Address of the user space buffer for the result
 * @str_len: Size of the user space buffer
 *
 * The return value follows the 'snprintf' logic, e.g. it gives
 * the true length of the converted string, even if truncation
 * happens. Hard error conditions are indicated by -1 return.
 *
 * This only provides full translation for capabilities. For UID and
 * GID/GRP credentials this only provides the translation for the
 * credentials type (give the value as -1 in such case). The full
 * translation is left for the user space library, which will access
 * the /etc/passwd and /etc/group for translating those translations.
 *
 * Restok component, when available, can provide additional mappings
 * into credential type GRP.
 */
long creds_kcreds2str(int type, long value, __user char *str, size_t str_len)
{
	if (type < 0 || type >= CREDS_MAX)
		return -EINVAL;

	if (value == -1 || type == CREDS_CAP) {
		int ret, prf_ret;
		size_t off;

		prf_ret = creds_put_string(&creds_map[type], str, str_len);
		if (prf_ret < 0 || value == -1)
			return prf_ret;
		off = min(str_len, (size_t)prf_ret);
		ret = creds_cap2str(value, str + off, str_len - off);
		if (ret >= 0)
			ret += prf_ret;
		return ret;
	}
#ifdef HAVE_RESTOK
	if (type == CREDS_GRP)
		return restok_string(value, str, str_len);
#endif
	return -EINVAL;
}

/**
 * creds_khave_p - Test if current task has a credential
 * @type: The type of the credential to test
 * @value: The specific value to test
 *
 * Return 1 if current task has the credential
 *
 * Return 0 if current task does not have the credential.
 */
int creds_khave_p(int type, long value)
{
	if (value < 0)
		return 0;

	switch (type) {
	case CREDS_CAP:
		return capable(value);
	case CREDS_UID:
		return current_euid() == value;
	case CREDS_GID:
		return current_egid() == value;
	case CREDS_GRP:
		return in_egroup_p(value);
	default:
		return 0;
	}
}
EXPORT_SYMBOL(creds_khave_p);

static long put_user_creds(const struct cred *cred, __u32 __user *list,
							size_t list_length)
{
	int ret = -EFAULT;
	size_t count, actual;

	if (!access_ok(VERIFY_WRITE, list, sizeof(*list) * list_length))
		return -EINVAL;

	count = 0;
	actual = 2;
	if (list_length >= actual) {
		const u32 tl = CREDS_TL(CREDS_UID, 1);
		if (__put_user(tl, &list[count++]) ||
		    __put_user(cred->euid, &list[count++]))
			goto out;
	}
	actual += 2;
	if (list_length >= actual) {
		const u32 tl = CREDS_TL(CREDS_GID, 1);
		if (__put_user(tl, &list[count++]) ||
		    __put_user(cred->egid, &list[count++]))
			goto out;
	}
	actual += 1 + _KERNEL_CAPABILITY_U32S;
	if (list_length >= actual) {
		const u32 tl = CREDS_TL(CREDS_CAP, _KERNEL_CAPABILITY_U32S);
		int i;
		if (__put_user(tl, &list[count++]))
			goto out;
		/* REVISIT: Following assumes that the kernel
		 * capability bit numbering and ordering within each
		 * u32 are same in the API specification for the TLV
		 * content.
		 */
		CAP_FOR_EACH_U32(i)
			if (__put_user(cred->cap_effective.cap[i],
				       &list[count++]))
				goto out;
	}
	actual += 1 + cred->group_info->ngroups;
	if (list_length >= actual) {
		const u32 tl = CREDS_TL(CREDS_GRP, cred->group_info->ngroups);
		int j;
		if (__put_user(tl, &list[count++]))
			goto out;
		for (j = 0; j < cred->group_info->ngroups; ++j)
			if (__put_user(GROUP_AT(cred->group_info, j),
				       &list[count++]))
				goto out;
	}

	/* Generate a "filler TLV" to account the remaining unused
	 * space of the user space buffer. Especially important, if
	 * only partial result was returned: for example, allows the
	 * user space to retrieve UID, if that could be returned but
	 * not supplementary groups. If user space only wanted UID,
	 * it does not need reissue the request with larger buffer.
	 */
	if (count < list_length) {
		const u32 tl = CREDS_TL(CREDS_MAX, list_length-count-1);
		if (__put_user(tl, &list[count]))
			goto out;
	}
	ret = actual;
 out:
	return ret;
}

/**
 * creds_kget - Return effective credentials of the identified process.
 * @pid: The PID
 * @list: The user space buffer to receive the credentials
 * @list_length: The number of __u32 elements in list array
 *
 * Return the length of the actual credentials information,
 * if positive. Negative returns indicate an error.
 *
 * If this is larger than the list_length, only at most
 * list_length is copied into user space. Only complete
 * TLV's are copied.
 */
long creds_kget(pid_t pid, __u32 __user *list, size_t list_length)
{
	const struct cred *cred = NULL;
	int ret = -EFAULT;

	if (pid) {
		struct task_struct *tsk;
		/*
		 * REVISIT: Is 'rcu_read_lock()' enough here, or can
		 * tsk go away between or during pid_task() and
		 * get_task_cred()? For modules, cannot use
		 * tasklist_lock, which seems to be only alteranative
		 * rcu_read_lock is not enough!
		 */
		rcu_read_lock();
		tsk = pid_task(find_vpid(pid), PIDTYPE_PID);
		if (tsk)
			cred = get_task_cred(tsk);
		rcu_read_unlock();
	} else {
		cred = get_current_cred();
	}

	if (!cred)
		return -ESRCH;

	ret = put_user_creds(cred, list, list_length);
	put_cred(cred);
	return ret;
}

/**
 * creds_kpeer - Return effective credentials of the identified socket.
 * @fd: The socket file descriptor
 * @list: The user space buffer to receive the credentials
 * @list_length: The number of __u32 elements in list array
 *
 * Return the length of the actual credentials information,
 * if positive. Negative returns indicate an error.
 *
 * If this is larger than the list_length, only at most
 * list_length is copied into user space. Only complete
 * TLV's are copied.
 */
long creds_kpeer(int fd, __u32 __user *list, size_t list_length)
{
	struct socket *sock;
	struct sock *sk;
	struct sock *other;
	const struct cred *cred = NULL;

	int ret = -EFAULT;

	if (fd < 0)
		return -EINVAL;

	sock = sockfd_lookup(fd, &ret);
	if (sock == NULL)
		return ret;

	ret = -ESRCH;

	sk = sock->sk;
	if (sk == NULL || sk->sk_family != AF_UNIX)
		goto out;

	unix_state_lock(sk);
	other = unix_sk(sk)->peer;
	if (other)
		sock_hold(other);
	unix_state_unlock(sk);
	if (other == NULL)
		goto out;

	unix_state_lock(other);
	if (!test_bit(SOCK_DEAD, &other->sk_flags) &&
	    other->sk_socket &&
	    other->sk_socket->file &&
	    other->sk_socket->file->f_cred)
		cred = get_cred(other->sk_socket->file->f_cred);
	unix_state_unlock(other);
	sock_put(other);

	if (cred) {
		if (cred->user->user_ns == current_user_ns())
			ret = put_user_creds(cred, list, list_length);
		put_cred(cred);
	}
out:
	if (sock && sock->file)
		sockfd_put(sock);

	return ret;
}


static struct dentry *creds_fs;
static struct dentry *creds_fs_file;

/**
 * creds_ioctl - The interface to the credentials retrieval from user space
 * @filp: The file
 * @cmd: The operation do do
 * @arg: Must hold the user space pointer to 'union creds_ioc_arg'.
 *
 * The ioctl way is only a placeholder implementation. This should be
 * defined as a syscall.
 */
static long creds_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	union creds_ioc_arg par;
	long retval = -EFAULT;
	char *str;
	long value;

	if (copy_from_user(&par, (const void __user *)arg, sizeof(par)))
		return -EFAULT;

	switch (cmd) {
	case SIOCCREDS_GET:
		retval = creds_kget(par.list.id,
				    par.list.items, par.list.length);
		break;
	case SIOCCREDS_GETPEER:
		retval = creds_kpeer(par.list.id,
					par.list.items, par.list.length);
		break;
	case SIOCCREDS_CREDS2STR:
		retval = get_user(value, par.str.value);
		if (retval)
			break;
		retval = creds_kcreds2str(par.str.type, value,
					  par.str.name, par.str.length);
		break;
	case SIOCCREDS_STR2CREDS:
		if (par.str.length > CREDS_STR_MAX)
			return -EINVAL;
		str = kmalloc(par.str.length + 1, GFP_KERNEL);
		if (!str)
			return -ENOMEM;
		if (copy_from_user(str, par.str.name, par.str.length))
			goto outstr;
		str[par.str.length] = 0;
		retval = creds_kstr2creds(str, &value);
		if (retval < 0)
			goto outstr;
		value = put_user(value, par.str.value);
		if (value)
			retval = value;
		goto outstr;
	default:
		retval = -EINVAL;
		break;
	}
	return retval;
outstr:
	kfree(str);
	return retval;
}

static int creds_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &creds_seq_ops);
}

static const struct file_operations creds_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl = creds_ioctl,
	.open		= creds_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release
};

static inline void cleanup(void)
{
	if (creds_fs_file && !IS_ERR(creds_fs_file))
		securityfs_remove(creds_fs_file);
	if (creds_fs && !IS_ERR(creds_fs))
		securityfs_remove(creds_fs);
}

static int __init creds_init(void)
{
	int error;

	creds_fs = securityfs_create_dir(CREDS_SECURITY_DIR, NULL);
	if (IS_ERR(creds_fs)) {
		ERR("Creds FS create failed\n");
		error = PTR_ERR(creds_fs);
		goto out;
		}
	creds_fs_file = securityfs_create_file(CREDS_SECURITY_FILE,
					       S_IFREG | 0644, creds_fs,
					       NULL, &creds_fops);
	if (IS_ERR(creds_fs_file)) {
		ERR("Failed creating securityfs entry '"
		    CREDS_SECURITY_DIR "/" CREDS_SECURITY_FILE "'\n");
		error = PTR_ERR(creds_fs_file);
		goto out;
		}

	INFO("ready\n");
	return 0;
out:
	cleanup();
	return error;
}

module_init(creds_init);

#ifdef MODULE
static void __exit creds_exit(void)
{
	cleanup();
	INFO("unloaded\n");
}

module_exit(creds_exit);

#endif

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Markku Savela");
MODULE_DESCRIPTION("Credentials retrieval module");
