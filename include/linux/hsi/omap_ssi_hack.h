/*
 * omap_ssi_hack.h
 *
 * OMAP SSI HACK header file.
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

#ifndef __LINUX_OMAP_SSI_HACK_H__
#define __LINUX_OMAP_SSI_HACK_H__

/*
 * FIXME: This file is to be removed asap.
 * This is only use to implement a horrible hack to support the useless
 * wakeline test until is removed in the CMT
 */
#include <linux/hsi/hsi.h>

void ssi_waketest(struct hsi_client *cl, unsigned int enable);

#endif /* __OMAP_SSI_HACK_H__ */
