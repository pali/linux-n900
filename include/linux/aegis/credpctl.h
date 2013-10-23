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

#ifndef _LINUX_AEGIS_CREDPCTL_H_
#define _LINUX_AEGIS_CREDPCTL_H_

/*
 * This defines the low level interface to the kernel
 * credentials policy module. This header is used in
 * implementations of the wrapper library.
 */

#include <linux/sockios.h>
#include <linux/aegis/credp.h>

#define CREDP_SECURITY_DIR "credp"
#define CREDP_SECURITY_FILE "policy"
#define CREDP_PATH_MAX 256

enum {
	SIOCCREDP_LOAD = SIOCDEVPRIVATE,
	SIOCCREDP_UNLOAD,
	SIOCCREDP_CONFINE,
	SIOCCREDP_SET,
	SIOCCREDP_CHECK,
	SIOCCREDP_CONFINE2
};


struct credp_list {
	size_t length;
	__u32 __user *items;
};

struct credp_path {
	size_t length;
	const char __user *name;
};

struct credp_ioc_arg {
	int flags;
	struct credp_path path;
	struct credp_list list;
};

#endif
