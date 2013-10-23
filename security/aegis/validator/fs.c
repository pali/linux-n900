/*
 * This file is part of Aegis Validator
 *
 * Copyright (C) 2002-2003 Ericsson, Inc
 * Copyright (c) 2003-2004 International Business Machines <serue@us.ibm.com>
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
 * Authors: Vincent Roy, 2002-2003
 *          Serge Hallyn, 2003
 *          Chris Wright, 2004
 *          Markku Kylänpää, 2008-2010
 */

/*
 * This file contains the securityfs interface to userspace.
 */

#include <linux/kernel.h>
#include <linux/security.h>
#include <linux/fs.h>
#include <linux/aegis/restok.h>
#include "validator.h"
#include "verify.h"
#include "cache.h"
#include "hashlist.h"
#include "modlist.h"
#include "enforce.h"
#include "sidcheck.h"
#include "fs.h"

/**
 * struct secfs - Aegis Validator securityfs entries
 * @top:      Aegis Validator securityfs configuration directory
 * @hashlist: an entry file to insert hash entries and view the current list
 * @modlist:  an entry file to insert kernel module hashes
 * @cache:    an entry file to view the cache
 * @flush:    an entry to flush the cache
 * @enforce:  an entry to set validator to enforcing mode and view current mode
 * @enable:   an entry to turn on/off validator and view current mode
 * @devorig:  an entry to specify source origin for developer mode
 *
 * This is a structure for securityfs entries. The entries are in directory
 * /sys/kernel/security/<top>.
 */
struct secfs {
	struct dentry *top;
	struct dentry *hashlist;
	struct dentry *modlist;
	struct dentry *cache;
	struct dentry *flush;
	struct dentry *enforce;
	struct dentry *enable;
	struct dentry *devorig;
};

/* Securityfs entries */
static struct secfs fs;

/* Securityfs entries */
static struct secfs old_fs;

/* Securityfs directory to be created in '/sys/kernel/security' */
static const char secfsdirname[] = "validator";

/* Securityfs directory to be created in '/sys/kernel/security' */
static const char old_secfsdirname[] = "digsig";

/**
 * validator_fsinit() - Initialize securityfs directory entries
 *
 * Create securityfs directory /sys/kernel/security/digsig and also file
 * entries to this directory. This should be renamed to aegis/validator, but
 * this also requires changes to userspace components. Securityfs is not
 * necessarily mounted in all systems. It should be mounte dwith the command:
 *
 * mount securityfs -t securityfs /sys/kernel/security
 *
 * FIXME: Create two set of entries /sys/kernel/security/digsig and
 * /sys/kernel/security/validator. When all userspace tools are updated
 * deprecate /sys/kernel/security/digsig.
 *
 * Return 0 if success, -ENOENT otherwise.
 */
static int __init validator_fsinit(void)
{
	fs.top = securityfs_create_dir(secfsdirname, NULL);
	if (!(fs.top) || IS_ERR(fs.top))
		return -ENOENT;
	old_fs.top = securityfs_create_dir(old_secfsdirname, NULL);
	if (!(old_fs.top) || IS_ERR(old_fs.top))
		return -ENOENT;

	fs.hashlist = validator_hashlist_fsinit(fs.top);
	fs.modlist = validator_modlist_fsinit(fs.top);
	fs.cache = validator_cache_fsinit(fs.top);
	fs.flush = validator_cache_flush_fsinit(fs.top);
	fs.enforce = validator_func_enforce_fsinit(fs.top);
	fs.enable = validator_func_enable_fsinit(fs.top);
	fs.devorig = validator_devorig_fsinit(fs.top);

	old_fs.hashlist = validator_hashlist_fsinit(old_fs.top);
	old_fs.enforce = validator_func_enforce_fsinit(old_fs.top);
	old_fs.enable = validator_func_enable_fsinit(old_fs.top);

	return 0;
}

/**
 * check_restricted_access() - Is client authorized to access securityfs
 *
 * When hashlist is not yet loaded access is allowed for everyone. If
 * the interfaces are not sealed (see struct validator_info field seal)
 * require CAP_MAC_ADMIN. If the interfaces are sealed require "tcb"
 * resource token.
 *
 * Return 0 if acess is OK and negative value if access is denied
 */
static int check_restricted_access(void)
{
	if (!valinfo.h_init)
		return 0;
	if (valinfo.secfs_init)
		return restok_has_tcb() ? 0 : -EPERM;
	if (!capable(CAP_MAC_ADMIN))
		return -EPERM;
	return 0;
}

/**
 * validator_fscleanup() - Cleanup securityfs directory
 *
 * Remove entries from /sys/kernel/security/digsig and then also remove the
 * directory.
 */
void validator_fscleanup(void)
{
	if (fs.enforce)
		securityfs_remove(fs.enforce);
	if (old_fs.enforce)
		securityfs_remove(old_fs.enforce);
	if (fs.enable)
		securityfs_remove(fs.enable);
	if (old_fs.enable)
		securityfs_remove(old_fs.enable);
	if (fs.devorig)
		securityfs_remove(fs.devorig);
	if (fs.flush)
		securityfs_remove(fs.flush);
	if (old_fs.flush)
		securityfs_remove(old_fs.flush);
	if (fs.cache)
		securityfs_remove(fs.cache);
	if (old_fs.cache)
		securityfs_remove(old_fs.cache);
	if (fs.hashlist)
		securityfs_remove(fs.hashlist);
	if (fs.modlist)
		securityfs_remove(fs.modlist);
	if (old_fs.hashlist)
		securityfs_remove(old_fs.hashlist);
	if (fs.top)
		securityfs_remove(fs.top);
	if (old_fs.top)
		securityfs_remove(old_fs.top);
}

/**
 * validator_fsaccess() - Access right check to securityfs entry
 * @op: Access operation identifier defined in fs.h header file
 *
 * Aegis Validator functionality can be configured using various secutrityfs
 * entries. This function contains centralized access right tests for each
 * Aegis Validator securityfs entry operation.
 *
 * Here is an example to test supplementary group membership. Must use
 * restok (from Runtime Policy Framework) for dynamic groups.  This only
 * tests small groups. If there are more than 32 groups another field should
 * be used. It is also possible to check capabilities of the process.
 *
 *	int i;
 *	pid_t pid = current->pid;
 *	uid_t uid = current->uid;
 *	gid_t gid = current->gid;
 *	struct group_info *groups = current->group_info;
 *	pr_info("PID=%d UID=%d GID=%d\n", pid, uid, gid);
 *	for (i = 0; i < groups->ngroups; i++) {
 *		pr_info("GROUP[%d]=%d\n", i, groups->small_block[i]);
 *	}
 *
 * Return 0 if access is allowed and negative value for an error.
 */
int validator_fsaccess(int op)
{
	switch (op) {
	case AEGIS_FS_ENFORCE_READ:
	case AEGIS_FS_ENABLE_READ:
	case AEGIS_FS_CACHE_READ:
	case AEGIS_FS_HASHLIST_READ:
	case AEGIS_FS_DEVORIG_READ:
		break;
	case AEGIS_FS_ENFORCE_WRITE:
	case AEGIS_FS_ENABLE_WRITE:
		if (valinfo.seal)
			return -EPERM;
		/* Fall through */
	case AEGIS_FS_FLUSH_WRITE:
	case AEGIS_FS_HASHLIST_WRITE:
	case AEGIS_FS_DEVORIG_WRITE:
		return check_restricted_access();
		break;
	default:
		break;
	}
	return 0;
}

fs_initcall(validator_fsinit);
