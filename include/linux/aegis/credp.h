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

#ifndef _LINUX_AEGIS_CREDP_H_
#define _LINUX_AEGIS_CREDP_H_
#include <linux/types.h>
#include <linux/aegis/creds.h>

/**
 * enum credp_policy_type - Type and modifiers for the policy
 *
 * This defines the API for the credentials policy module in kernel,
 * both in kernel and user space. In user space this is the API
 * for the wrapper library, which passes the information to the
 * kernel side.
 */
enum credp_policy_type {
	CREDP_TYPE_NONE = 0,	/* Unspecified */
	CREDP_TYPE_SET,		/* Set credentials */
	CREDP_TYPE_ADD,		/* Add to current */
	CREDP_TYPE_INHERIT,	/* Inherit from current */

	CREDP_TYPE_MASK = 7,

	CREDP_TYPE_INHERITABLE = 1 << 3,
	CREDP_TYPE_SETUID = 1 << 4,
};

long credp_kload(int flags, const char *path,
		 const __u32 __user *list, size_t list_length);
long credp_kset(int flags, const __u32 __user *list, size_t list_length);
long credp_kunload(const char *path);
long credp_kconfine(const char *path);
long credp_kconfine2(const char *path, int flags,
		     const __u32 __user *list, size_t list_length);

#ifdef __KERNEL__
int credp_apply(struct linux_binprm *bprm, bool *effective);
int credp_check(long srcid, const struct cred *cred);
int credp_task_setgroups(struct group_info *group_info);
int credp_task_setgid(gid_t id0, gid_t id1, gid_t id2, int flags);
int credp_ptrace_access_check(struct task_struct *child, unsigned int mode);
int credp_ptrace_traceme(struct task_struct *parent);
#endif

#endif
