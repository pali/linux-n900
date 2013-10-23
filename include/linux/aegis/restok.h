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

#ifndef _LINUX_AEGIS_RESTOK_H_
#define _LINUX_AEGIS_RESTOK_H_

/*
 * Define the range for virtual group identifiers
 */
#define RESTOK_GROUP_MIN 9990000
#define RESTOK_GROUP_MAX 9999999

/*
 * The name of the default root.
 */
#define RESTOK_DEFAULT_ROOT ""

#ifdef __KERNEL__
extern int restok_has_tcb(void);
#endif

extern long restok_locate(const char *name);
extern long restok_define(const char *name, long owner_id);
extern int restok_string(long id, char __user *str, size_t str_len);
extern int restok_setlock(long id);

#endif
