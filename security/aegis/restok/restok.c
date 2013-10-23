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
 * DOC: Persistent mapping of strings to unique numbers
 *
 * This module provides a persistent mapping of strings to unique
 * dynamically assigned identifier numbers.
 *
 * The generated identifiers can be used as supplementary group
 * numbers in the task structure. The original intended use of these
 * identifiers is to provide additional, dynamically configured
 * credentials for processes. An access to service can be protected by
 * requiring a presence of specific credential in task context
 * (supplementary groups).
 *
 * Although these numbers can be used as supplementary groups, they
 * should never be used as file system groups in permanent storage.
 *
 * Once the string has been assigned an identifier, this assignment
 * cannot be changed while this module is loaded. If this module is
 * compiled in kernel, the assigments are permanent until the next
 * boot.
 *
 * To provide different name spaces, the strings form a forest of
 * trees. The string correspoding the identifier, is the path from the
 * ground up to the node that defines the identifier. Within the path
 * "::" is used as a separator.
 *
 * The module creates a special default tree with empty string as a
 * name of the root. A string without any "::" is assumed to be a
 * direct child of this default root. For any other identifiers, the
 * string must be a full path from one of the roots to the defining
 * node.
 *
 * The above rule would make it impossible to address any other root
 * nodes. Thus, the module implements a special case, where a string
 * containing "name::name" collapses into "name". Some examples
 *
 * foo		-> "::foo" (symbol under default root)
 * foo::foo	-> "foo" (root level symbol, different from previous)
 * foo::foo::foo-> "foo" (a repeated name is reduced to single instance)
 * ::foo	-> "::foo"
 * ::		-> "" (= default root)
 *
 * The purpose of the "default root" is to provide applications a
 * place to define simple symbols, which do not conflict with the root
 * names, which are used for identifying different name spaces.
 *
 * The string used in resolving an identifier (function
 * 'restok_locate') is always a full path or a string under the
 * default root.
 *
 * Strings are defined one level at time (function
 * 'restok_define'). The identifier of the parent must be supplied. A
 * zero as parent, creates a new root.
 *
 * A malicious application could create a huge number of mapped
 * strings. This is the only reason for limiting the capability of
 * creating new mappings.
 *
 * For debugging purposes, this component can be compiled as a module,
 * but real usage requires that this is built in.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/moduleparam.h>
#include <linux/security.h>
#include <linux/sockios.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/namei.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/aegis/restok.h>
#include <linux/aegis/restokctl.h>

#define RESTOK_NAME "restok"

#undef INFO
#undef ERR

#define INFO(args...) pr_info(RESTOK_NAME ": " args)
#define ERR(args...) pr_err(RESTOK_NAME ": " args)

/**
 * struct token - The token node
 * @list: List of token nodes mapped to same hash bucket
 * @owner: Owner of the token (or NULL, if at root level)
 * @id: Unique token identifier
 * @len: Length of the token name (below)
 * @name: The name of the token (NUL terminated)
 */
struct token {
	struct list_head list;
	const struct token *owner;
	long id;
	__u16 len;
	char name[];
};

/*
 * Mutex for the database access
 */
static DEFINE_MUTEX(mutex);

/*
 * Definition of the hash table for the tokens
 *
 * WARNING: The token_hash must be initialized dynamically in the init
 * code, for this reason, the services of this cannot be used from
 * other init code, before the init of this has been run.
 */
#define TOKEN_HASH_SIZE 509
static struct list_head token_hash[TOKEN_HASH_SIZE];

/*
 * Last allocated resource identifier.
 */
static long resource_last = RESTOK_GROUP_MIN-1;

/*
 * The resource id granting the TCB, if non-zero
 */
static gid_t lock_id;

/*
 * The resource for the system name space
 */
static const struct token *default_root_token;
/*
 * The root of the securityfs "/sys/kernel/security/restok"
 */
static struct dentry *restok_fs;
/*
 * The interface file "/sys/kernel/security/restok/tokens"
 */
static struct dentry *restok_fs_token;

/**
 * find_by_name_owner - Find token by component name and owner
 * @name: Component name start
 * @len: Length of the name part
 * @owner: Owner (parent) of the token to find
 * @bucket: Return index of the hash bucket which applies to this search
 *
 * Find a token that has the given name and owner.
 *
 * Return the token, if found.
 *
 * Return NULL, if not found. The caller can use the returned
 * bucket index, if creating a new token is called for.
 *
 * It owner is the same as the token matching the name, then this
 * completes the find. This implements collapsing of sequences of
 * identical names into single (e.g. "...::name::name::.." =>
 * "...::name::...").
 */
static struct token *find_by_name_owner(const char *name, size_t len,
					const struct token *owner,
					int *bucket)
{
	struct token *p;

	*bucket = full_name_hash(name, len) % TOKEN_HASH_SIZE;
	list_for_each_entry(p, &token_hash[*bucket], list)
		if ((p->owner == owner || p == owner) &&
		    p->len == len && memcmp(name, p->name, len) == 0)
			return p;
	return NULL;
}

/**
 * find_by_id - Find token by it's assigned number
 * @id: The identification number
 *
 * Return found token, or NULL if not found.
 */
static struct token *find_by_id(long id)
{
	int i;
	struct token *p;
	for (i = 0; i < TOKEN_HASH_SIZE; ++i)
		list_for_each_entry(p, &token_hash[i], list)
			if (p->id == id)
				return p;
	return NULL;
}

/**
 * restok_follow_path - Traverse down the tree
 * @path: The path string
 * @tkn: Startpoint and returns the result of traversal
 *
 * If the path contains multiple components (separated by "::"), then
 * for each component follow the path downward. Unpaired ':' is
 * forbidden in the string.
 *
 * Returns updated pointer to the path, or NULL, if the path needed
 * traversing and the intermediate token does not exist.
 */
static const char *restok_follow_path(const char *path,
				      const struct token **tkn)
{
	int dummy;
	const char *sep;

	while ((sep = strchr(path, ':')) != NULL) {
		const size_t nlen = sep - path;
		if (sep[1] != ':')
			return NULL;
		*tkn = find_by_name_owner(path, nlen, *tkn, &dummy);
		if (!*tkn)
			return NULL;
		path += nlen + 2;
	}
	return path;
}

/**
 * restok_has_tcb - Check that current task has 'tcb'
 *
 * Returns 1, if the current task has 'tcb'
 * Returns 0, if the current task does not have 'tcb'
 *
 * Until locked, only CAP_MAC_ADMIN is required
 */
int restok_has_tcb(void)
{
	return lock_id ? in_egroup_p(lock_id) : capable(CAP_MAC_ADMIN);
}
EXPORT_SYMBOL(restok_has_tcb);

/**
 * restok_locked - Set/change the token that allows tcb
 * @id: The token value
 */
int restok_setlock(long id)
{
	if (id < RESTOK_GROUP_MIN || id > RESTOK_GROUP_MAX)
		return -EINVAL;
	if (!restok_has_tcb())
		return -EPERM;
	/*
	 * Note: tokens are curently implemented as group numbers, the
	 * range check above guarantees that id value will fit in the
	 * gid_t.
	 */
	lock_id = id;
	return 0;
}

/**
 * restok_locate - Locate a token and return the mapped value
 * @name: The token name string
 *
 * Returns 0 when token does not have mapping,
 * Returns < 0 on real errors,
 * Returns > 0 (== mapped value) when the token has been defined
 */
long restok_locate(const char *name)
{
	long retval = -EINVAL;
	const struct token *token = NULL;
	int dummy;

	if (!name)
		return -EINVAL;

	mutex_lock(&mutex);
	name = restok_follow_path(name, &token);
	if (!name)
		goto out;
	if (!token)
		token = default_root_token;
	token = find_by_name_owner(name, strlen(name), token, &dummy);
	retval = token ? token->id : 0;
out:
	mutex_unlock(&mutex);
	return retval;
}
EXPORT_SYMBOL(restok_locate);

/**
 * restok_define - Define a new token and return the mapped value
 * @name: The token name string
 * @owner_id: Parent token value
 *
 * If the parent has been defined (owner_id != 0), then the name is
 * relative to that, otherwise the name is relative to the absolute
 * root.
 *
 * If the token has already been mapped, the function just returns
 * that value. For new tokens, this function assigns a new unique
 * positive (> 0) value.
 *
 * Negative return indicates failure.
 */
long restok_define(const char *name, long owner_id)
{
	const struct token *owner = NULL;
	struct token *token;
	long retval = -EINVAL;
	size_t len;
	int bucket;

	if (!name)
		return -EINVAL;

	mutex_lock(&mutex);
	if (owner_id) {
		owner = find_by_id(owner_id);
		if (!owner)
			goto out;
	}
	name = restok_follow_path(name, &owner);
	if (!name)
		goto out;

	len = strlen(name);
	token = find_by_name_owner(name, len, owner, &bucket);
	if (!token) {
		const size_t size = offsetof(struct token, name) + len + 1;

		if (resource_last == RESTOK_GROUP_MAX) {
			ERR("All available resource identifiers in use\n");
			goto out;
		}
		token = kmalloc(size, GFP_KERNEL);
		if (!token) {
			retval = -ENOMEM;
			goto out;
		}
		list_add_tail(&token->list, &token_hash[bucket]);
		token->owner = owner;
		token->len = len;
		memcpy(token->name, name, len);
		token->name[len] = 0;
		token->id = ++resource_last;
	}
	retval = token->id;
out:
	mutex_unlock(&mutex);
	return retval;
}
EXPORT_SYMBOL(restok_define);

/**
 * restok_get_path: Return the path string for token
 * @tkn: The token
 * @len: Returns the total length of the path
 *
 * Allocate memory and construct the path corresponding the 'tkn'.
 *
 * Returns pointer to the allocated NUL-terminated path string. The
 * 'len' contains the length of the constructed path string. The path
 * must be released by the caller with kfree.
 *
 * Returns NULL if token == NULL or kmalloc fails. On NULL return,
 * len is 0.
 */
static char *restok_get_path(const struct token *tkn, size_t *len)
{
	size_t size;
	const struct token *p;
	const struct token *owner;
	char *path;

	*len = 0;
	if (!tkn)
		return NULL;
	owner = tkn->owner;
	/* Return root level tokens (!owner) as "name::name"
	 * (this is to avoid them from getting confused with
	 * the default name space).
	 */
	if (!owner)
		owner = tkn;

	/*
	 * Compute the total length of the required string
	 */
	size = tkn->len;
	/* Note: must test the original tkn->owner, not owner!
	 * Otherwise, the path for default_root_token would be an
	 * empty string, we want it to be "::". Tokens directly under
	 * the default_root_token (the default name space) are
	 * presented by the tkn->name only.
	 */
	if (tkn->owner != default_root_token) {
		p = owner;
		do {
			size += p->len + 2; /* name + 2 for "::" */
			p = p->owner;
		} while (p);
	}
	path = kmalloc(size + 1, GFP_KERNEL);
	if (!path)
		return NULL;
	*len = size;

	/*
	 * Generate the path string (from end to beginning)
	 */
	path[size] = 0;
	size -= tkn->len;
	memcpy(path + size, tkn->name, tkn->len);
	if (tkn->owner != default_root_token) {
		p = owner;
		do {
			path[--size] = ':';
			path[--size] = ':';
			size -= p->len;
			memcpy(path + size, p->name, p->len);
			p = p->owner;
		} while (p);
	}
	BUG_ON(size != 0);
	return path;
}

/**
 * restok_string - Convert the token value to token string
 * @id: The value to convert
 * @str: The buffer (in user space) to store the string
 * @str_len: The length of the buffer (str)
 *
 * Returns the actual length of the string (snprintf logic), or a
 * negative error code on severe error conditions.
 *
 * -ENOENT, if 'id' is not defined
 * -EFAULT, if on any problem of copying data to user space.
 */
int restok_string(long id, __user char *str, size_t str_len)
{
	struct token *token;
	char *path;
	size_t actual, copy_len;

	/* Because once allocated tokens are immutable,
	 * mutex is only required while looking up...
	 */
	mutex_lock(&mutex);
	token = find_by_id(id);
	mutex_unlock(&mutex);

	if (!token)
		/* FIXME: Misusing fs error code, because there does
		 * not seem to be any other "not found" return?
		 */
		return -ENOENT;

	path = restok_get_path(token, &actual);
	if (!path)
		/* Because token != NULL, NULL return from
		 * restok_get_path indicates kmalloc failure.
		 */
		return -ENOMEM;

	copy_len = min(actual+1, str_len);
	if (copy_len) {
		path[copy_len-1] = 0;
		if (copy_to_user(str, path, copy_len))
			actual = -EFAULT;
	}
	kfree(path);
	return actual;
}
EXPORT_SYMBOL(restok_string);

/**
 * restok_ioctl - Gateway from user space to kernel
 * @filp: The file (not really used -- we know it's the tokens)
 * @cmd: The requested operation
 * @arg: The ioctl argument (must be a pointer to struct restok_ioc_arg).
 *
 * The ioctl gateway transfers a call in user space to a function with
 * same name in the kernel space.
 */
static long restok_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct restok_ioc_arg par;
	char *name = NULL;
	long retval = 0;

	if (copy_from_user(&par, (const void __user *)arg, sizeof(par)))
		return -EFAULT;
	if (cmd != SIOCRESTOK_STRING && cmd != SIOCRESTOK_SETLOCK) {
		if (par.len > RESTOKCTL_MAXNAME)
			return -EFAULT;
		name = kmalloc(par.len+1, GFP_KERNEL);
		if (!name)
			return -ENOMEM;
		if (copy_from_user(name, par.name, par.len)) {
			retval = -EFAULT;
			goto out;
		}
		name[par.len] = 0;
	}

	switch (cmd) {
	case SIOCRESTOK_LOCATE:
		retval = restok_locate(name);
		break;
	case SIOCRESTOK_DEFINE:
		if (restok_has_tcb())
			retval = restok_define(name, par.id);
		else
			retval = -EPERM;
		break;
	case SIOCRESTOK_STRING:
		retval = restok_string(par.id, par.name, par.len);
		break;
	case SIOCRESTOK_SETLOCK:
		retval = restok_setlock(par.id);
		break;
	default:
		retval = -EINVAL;
		break;
	}
out:
	kfree(name);
	return retval;
}

static void *restok_seq_start(struct seq_file *m, loff_t *pos)
{
	const int max_pos = resource_last - RESTOK_GROUP_MIN;
	unsigned id = *pos;
	if (id > max_pos)
		return NULL;
	return (void *)(id + RESTOK_GROUP_MIN);
}

static void *restok_seq_next(struct seq_file *m, void *v, loff_t *pos)
{
	unsigned id = (unsigned)v;
	if (id < RESTOK_GROUP_MIN || id >= resource_last)
		return NULL;
	++*pos;
	return (void *) ++id;
}

static void restok_seq_stop(struct seq_file *m, void *v)
{
}

static int restok_seq_show(struct seq_file *m, void *v)
{
	const struct token *tkn;
	char *path;
	size_t len;

	/* Because tokens are never deleted, only the searching needs
	 * to be locked by mutex
	 */
	mutex_lock(&mutex);
	tkn = find_by_id((long)v);
	mutex_unlock(&mutex);

	path = restok_get_path(tkn, &len);

	/* If there is a any problem generating path, we just leave it
	 * out. Any automatic parsing of this output should treat
	 * empty path after token value, as an indication of "broken
	 * token" (or, if debugging this module, it is an indication
	 * of buggy code -- neither kmalloc failure, nor not finding
	 * the token happens in a healthy system...).
	 */
	seq_printf(m, "%d\t", (int)v);
	if (path) {
		seq_write(m, path, len);
		kfree(path);
	}
	seq_putc(m, '\n');
	return 0;
}

static const struct seq_operations restok_seq_ops = {
	.start = restok_seq_start,
	.next = restok_seq_next,
	.stop = restok_seq_stop,
	.show = restok_seq_show
};

static int restok_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &restok_seq_ops);
}

static const struct file_operations restok_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl = restok_ioctl,
	.open		= restok_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release
};

static inline void restok_cleanup(void)
{
	int i;
	if (restok_fs_token && !IS_ERR(restok_fs_token))
		securityfs_remove(restok_fs_token);
	if (restok_fs && !IS_ERR(restok_fs))
		securityfs_remove(restok_fs);

	for (i = 0; i < TOKEN_HASH_SIZE; ++i) {
		struct token *p, *n;
		list_for_each_entry_safe(p, n, &token_hash[i], list) {
			list_del(&p->list);
			kfree(p);
		}
	}
}

static int __init restok_init(void)
{
	int error = -ENOMEM;
	int i;
	long id;

	for (i = 0; i < TOKEN_HASH_SIZE; ++i)
		INIT_LIST_HEAD(&token_hash[i]);

	/* Create the default space, distinct from the real root */
	id = restok_define(RESTOK_DEFAULT_ROOT, 0);
	default_root_token = find_by_id(id);
	if (!default_root_token) {
		ERR("Failed creating default space '" RESTOK_DEFAULT_ROOT "\n");
		goto out;
	}

	/* Enable access/control from user space via securityfs */
	restok_fs = securityfs_create_dir(RESTOK_NAME, NULL);
	if (IS_ERR(restok_fs)) {
		ERR("Restok FS create failed\n");
		error = PTR_ERR(restok_fs);
		goto out;
	}
	restok_fs_token = securityfs_create_file(RESTOKCTL_TOKENS,
						 S_IFREG | 0644, restok_fs,
						 NULL, &restok_fops);
	if (IS_ERR(restok_fs_token)) {
		ERR("Failed creating securityfs entry '"
		    RESTOK_NAME "/" RESTOKCTL_TOKENS "'\n");
		error = PTR_ERR(restok_fs_token);
		goto out;
	}
	INFO("ready\n");
	return 0;
out:
	restok_cleanup();
	return error;
	}

module_init(restok_init);

#ifdef MODULE
static void __exit restok_exit(void)
{
	restok_cleanup();
	INFO("unloaded\n");
}

module_exit(restok_exit);
#endif

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Markku Savela");
MODULE_DESCRIPTION("Maemo Resource Token Manager");
