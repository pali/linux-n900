/*
 * Driver for ak8975 (Asahi Kasei EMD Corporation) magnetometer driver
 *
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
 *
 * Contact: Samu Onkalo <samu.p.onkalo@nokia.com>
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
 */
#ifndef __LINUX_I2C_AK8975_H
#define __LINUX_I2C_AK8975_H

#define AK8975_NO_MAP		  0
#define AK8975_DEV_X		  1
#define AK8975_DEV_Y		  2
#define AK8975_DEV_Z		  3
#define AK8975_INV_DEV_X	 -1
#define AK8975_INV_DEV_Y	 -2
#define AK8975_INV_DEV_Z	 -3

struct ak8975_platform_data {
	s8 axis_x;
	s8 axis_y;
	s8 axis_z;
};

/* Device name: /dev/ak8975n, where n is a running number */
struct ak8975_data {
	__s16 x;
	__s16 y;
	__s16 z;
	__u16 valid;
} __attribute__((packed));

#endif
