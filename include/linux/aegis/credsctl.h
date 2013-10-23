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

#ifndef _LINUX_AEGIS_CREDSCTL_H_
#define _LINUX_AEGIS_CREDSCTL_H_
/*
 * This defines the low level interface to the kernel
 * credentials retrieval module. This header is used in
 * implementations of the wrapper library.
 */

#include <linux/sockios.h>
#include <linux/aegis/creds.h>

#define CREDS_SECURITY_DIR "creds"
#define CREDS_SECURITY_FILE "read"
#define CREDS_STR_MAX 256

enum {
	SIOCCREDS_GET = SIOCDEVPRIVATE,
	SIOCCREDS_CREDS2STR,
	SIOCCREDS_STR2CREDS,
	SIOCCREDS_GETPEER,
};


/**
 * struct creds_list - credentials list argument
 * @pid: The process to query
 * @length: The length of the array in user space to receive the data
 * @items: The start of the array in user space
 */
struct creds_list {
	int id;
	size_t length;
	__u32 __user *items;
};

/**
 * struct creds_string - credential string conversion argument
 * @type: Credential type code
 * @length: Length of the credential string value
 * @value: Credential number
 * @name: Credential name
 */
struct creds_string {
	__u16 type;
	__u16 length;
	long __user *value;
	char __user *name;
};

union creds_ioc_arg {
	struct creds_string str;
	struct creds_list list;
};

#endif
