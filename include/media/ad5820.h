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

#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/videodev2.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

struct regulator;

#define AD5820_NAME		"ad5820"
#define AD5820_I2C_ADDR		(0x18 >> 1)

/* Register definitions */
#define AD5820_POWER_DOWN		(1 << 15)
#define AD5820_DAC_SHIFT		4
#define AD5820_RAMP_MODE_LINEAR		(0 << 3)
#define AD5820_RAMP_MODE_64_16		(1 << 3)

struct ad5820_platform_data {
	int (*set_xshutdown)(struct v4l2_subdev *subdev, int set);
};

#define to_ad5820_device(sd)	container_of(sd, struct ad5820_device, subdev)

struct ad5820_device {
	struct v4l2_subdev subdev;
	struct ad5820_platform_data *platform_data;
	struct regulator *vana;

	struct v4l2_ctrl_handler ctrls;
	u32 focus_absolute;
	u32 focus_ramp_time;
	u32 focus_ramp_mode;

	struct mutex power_lock;
	int power_count;

	int standby : 1;
};

#endif /* AD5820_H */
