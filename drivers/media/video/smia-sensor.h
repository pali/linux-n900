/*
 * drivers/media/video/smia-sensor.h
 *
 * Copyright (C) 2008,2009 Nokia Corporation
 *
 * Contact: Tuukka Toivonen <tuukka.o.toivonen@nokia.com>
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

#ifndef SMIA_SENSOR_H
#define SMIA_SENSOR_H

#include <linux/i2c.h>
#include <media/v4l2-int-device.h>

#define SMIA_SENSOR_NAME	"smia-sensor"
#define SMIA_SENSOR_I2C_ADDR	(0x20 >> 1)

struct smia_sensor_platform_data {
	int (*g_priv)(struct v4l2_int_device *s, void *priv);
	int (*configure_interface)(struct v4l2_int_device *s,
				   int width, int height);
	int (*set_xclk)(struct v4l2_int_device *s, int hz);
	int (*power_on)(struct v4l2_int_device *s);
	int (*power_off)(struct v4l2_int_device *s);
};

struct smia_sensor {
	struct i2c_client *i2c_client;
	struct i2c_driver driver;

	/* Sensor information */
	u16 model_id;
	u8  revision_number;
	u8  manufacturer_id;
	u8  smia_version;

	/* V4L2 current control values */
	s32 ctrl_exposure;
	s32 ctrl_gain;

	struct smia_reglist *current_reglist;
	struct v4l2_int_device *v4l2_int_device;
	struct v4l2_fract timeperframe;

	struct smia_sensor_platform_data *platform_data;

	const struct firmware *fw;
	struct smia_meta_reglist *meta_reglist;

	enum v4l2_power power;
};

#endif /* SMIA_SENSOR_H */
