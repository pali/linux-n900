/*
 * cmt.h
 *
 * CMT support header
 *
 * Copyright (C) 2009 Nokia Corporation. All rights reserved.
 *
 * Contact: Carlos Chinea <carlos.chinea@nokia.com>
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
 */

#ifndef __NOKIA_CMT_H__
#define __NOKIA_CMT_H__

#include <linux/notifier.h>

/*
 * NOKIA CMT notifier events
 */
enum {
	CMT_RESET,
};

struct cmt_device;

/*
 * struct cmt_platform_data - CMT platform data
 * @ape_rst_rq_gpio: GPIO line number for the CMT reset line
 */
struct cmt_platform_data {
	unsigned int cmt_rst_ind_gpio;
};

struct cmt_device *cmt_get(const char *name);
void cmt_put(struct cmt_device *cmt);
int cmt_notifier_register(struct cmt_device *cmtdev,
						struct notifier_block *nb);
int cmt_notifier_unregister(struct cmt_device *cmtdev,
						struct notifier_block *nb);
#endif /* __NOKIA_CMT_H__ */
