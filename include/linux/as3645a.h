/**
 * include/linux/as3645a.h
 *
 * Copyright (C) 2010 Nokia Corporation
 *
 * Contact: Ivan T. Ivanov <iivanov@mm-sol.com>
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

#ifndef __AS3645A_USER_H_
#define __AS3645A_USER_H_

#include <linux/videodev2.h>

#define V4L2_EVENT_AS3645A_CLASS	(V4L2_EVENT_PRIVATE_START | 0x200)
#define V4L2_EVENT_STATUS_CHANGED	(V4L2_EVENT_AS3645A_CLASS | 1)

enum v4l2_flash_events {
	V4L2_FLASH_READY = 0,
	V4L2_FLASH_COOLDOWN = 1,
};

#endif /* __AS3645A_USER_H_ */
