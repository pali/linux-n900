/*
 * include/media/ad5820.h
 *
 * Copyright (C) 2008 Nokia Corporation
 * Copyright (C) 2007 Texas Instruments
 *
 * Contact: Tuukka Toivonen <tuukka.o.toivonen@nokia.com>
 *          Sakari Ailus <sakari.ailus@nokia.com>
 *
 * Based on af_d88.c by Texas Instruments.
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

#ifndef AD5820_H
#define AD5820_H

#include <linux/videodev2.h>

#include <linux/i2c.h>

#include <media/v4l2-int-device.h>

#define AD5820_NAME		"ad5820"
#define AD5820_I2C_ADDR		(0x18 >> 1)

struct ad5820_platform_data {
	int (*g_priv)(struct v4l2_int_device *s, void *priv);
	int (*s_power)(struct v4l2_int_device *s, enum v4l2_power state);
};

struct ad5820_device {
	/* client->adapter is non-NULL if driver is registered to
	 * I2C subsystem and omap camera driver, otherwise NULL */
	struct i2c_client *i2c_client;
	s32 focus_absolute;		/* Current values of V4L2 controls */
	s32 focus_ramp_time;
	s32 focus_ramp_mode;
	enum v4l2_power power;
	struct ad5820_platform_data *platform_data;
	struct v4l2_int_device *v4l2_int_device;
};

#endif /* AD5820_H */
