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

#include <media/smiaregs.h>
#include <media/v4l2-subdev.h>

#define SMIA_SENSOR_NAME	"smia-sensor"
#define SMIA_SENSOR_I2C_ADDR	(0x20 >> 1)

struct smia_sensor_platform_data {
	void (*configure_interface)(struct v4l2_subdev *subdev,
				    struct smia_mode *mode);
	int (*set_xclk)(struct v4l2_subdev *subdev, int hz);
	int (*set_xshutdown)(struct v4l2_subdev *sd, int set);
};

#endif /* SMIA_SENSOR_H */
