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

#ifndef _LINUX_AEGIS_RESTOKCTL_H_
#define _LINUX_AEGIS_RESTOKCTL_H_

#define RESTOKCTL_VERSION 1 /* Indicate API version */
#define RESTOKCTL_MAXNAME 500 /* Longest supported name string */
#define RESTOKCTL_TOKENS "tokens"

enum {
	SIOCRESTOK_LOCATE = SIOCDEVPRIVATE,
	SIOCRESTOK_DEFINE,
	SIOCRESTOK_STRING,
	SIOCRESTOK_SETLOCK,
};

/**
 * struct restok_ioc_arg - The ioctl argument block
 * @id: Identifier
 * @len: The Length of the following name area
 * @name: Start of the name area in the user space
 *
 * Detail interpretation of the members depends on the ioctl
 * operation.
 */
struct restok_ioc_arg {
	int id;
	size_t len;
	char __user *name;
};

#endif
