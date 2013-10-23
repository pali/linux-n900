/*
 * drivers/media/video/et8ek8.h
 *
 * Copyright (C) 2008 Nokia Corporation
 *
 * Contact: Sakari Ailus <sakari.ailus@nokia.com>
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

#ifndef ET8EK8_H
#define ET8EK8_H

#include <linux/i2c.h>
#include <media/smiaregs.h>
#include <media/v4l2-int-device.h>

#define ET8EK8_NAME		"et8ek8"
#define ET8EK8_I2C_ADDR		(0x7C >> 1)

#define ET8EK8_PRIV_MEM_SIZE	128
#define ET8EK8_NCTRLS		3

struct et8ek8_platform_data {
	int (*g_priv)(struct v4l2_int_device *s, void *priv);
	int (*configure_interface)(struct v4l2_int_device *s,
				   struct smia_mode *mode);
	int (*set_xclk)(struct v4l2_int_device *s, int hz);
	int (*power_on)(struct v4l2_int_device *s);
	int (*power_off)(struct v4l2_int_device *s);
};

struct et8ek8_sensor;

/* Current values for V4L2 controls */
struct et8ek8_control {
	s32 minimum;
	s32 maximum;
	s32 step;
	s32 default_value;
	s32 value;
	int (*set)(struct et8ek8_sensor *sensor, s32 value);
};

struct et8ek8_sensor {
	struct i2c_client *i2c_client;
	struct i2c_driver driver;

	u16 version;

	struct et8ek8_control controls[ET8EK8_NCTRLS];

	struct smia_reglist *current_reglist;
	struct v4l2_int_device *v4l2_int_device;
	struct v4l2_fract timeperframe;

	struct et8ek8_platform_data *platform_data;

	const struct firmware *fw;
	struct smia_meta_reglist *meta_reglist;
	u8 priv_mem[ET8EK8_PRIV_MEM_SIZE];

	enum v4l2_power power;
};

#endif /* ET8EK8_H */
