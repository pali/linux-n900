/*
 * include/media/adp1653.h
 *
 * Copyright (C) 2008 Nokia Corporation
 *
 * Contact: Sakari Ailus <sakari.ailus@nokia.com>
 *          Tuukka Toivonen <tuukka.o.toivonen@nokia.com>
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

#ifndef ADP1653_H
#define ADP1653_H

#include <linux/videodev2.h>
#include <linux/i2c.h>
#include <media/v4l2-int-device.h>
#include <media/smiaregs.h>

#define ADP1653_NAME				"adp1653"
#define ADP1653_I2C_ADDR			(0x60 >> 1)

/* Register definitions */
#define ADP1653_REG_OUT_SEL			0x00
#define ADP1653_REG_OUT_SEL_HPLED_MAX		0x1f
#define ADP1653_REG_OUT_SEL_HPLED_SHIFT		3
#define ADP1653_REG_OUT_SEL_ILED_MAX		0x07
#define ADP1653_REG_OUT_SEL_ILED_SHIFT		0

#define ADP1653_REG_CONFIG			0x01
#define ADP1653_REG_CONFIG_TMR_CFG		(1 << 4)
#define ADP1653_REG_CONFIG_TMR_SET_MAX		0x0f
#define ADP1653_REG_CONFIG_TMR_SET_SHIFT	0

#define ADP1653_REG_SW_STROBE			0x02
#define ADP1653_REG_SW_STROBE_SW_STROBE		(1 << 0)

#define ADP1653_REG_FAULT			0x03
#define ADP1653_REG_FAULT_FLT_SCP		(1 << 3)
#define ADP1653_REG_FAULT_FLT_OT		(1 << 2)
#define ADP1653_REG_FAULT_FLT_TMR		(1 << 1)
#define ADP1653_REG_FAULT_FLT_OV		(1 << 0)

#define ADP1653_TORCH_INTENSITY_MAX		11

struct adp1653_platform_data {
	int (*g_priv)(struct v4l2_int_device *s, void *priv);
	int (*power_on)(struct v4l2_int_device *s);
	int (*power_off)(struct v4l2_int_device *s);
	int (*strobe)(struct v4l2_int_device *s);  /* If NULL, use SW strobe */
	u32 max_flash_timeout;		/* flash light timeout in us */
	u32 max_flash_intensity;	/* led intensity, flash mode */
	u32 max_torch_intensity; 	/* led intensity, torch mode */
	u32 max_indicator_intensity;	/* indicator led intensity */
};

struct adp1653_flash {
	struct i2c_client *i2c_client;
	struct i2c_driver driver;

	u32 flash_timeout;
	u32 flash_intensity;
	u32 torch_intensity;
	u32 indicator_intensity;

	struct v4l2_int_device *v4l2_int_device;

	struct adp1653_platform_data *platform_data;

	enum v4l2_power power;		/* Requested power state */
};

#endif /* ADP1653_H */
