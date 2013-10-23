/*
 * arch/arm/mach-omap2/board-rx51-camera-base.c
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
 */

#include <linux/i2c.h>
#include <linux/i2c/twl4030.h>
#include <linux/delay.h>

#include <mach/gpio.h>
#include <mach/clock.h>

#include <asm/mach-types.h>

#if defined CONFIG_VIDEO_MACH_RX51 || defined CONFIG_VIDEO_MACH_RX51_MODULE

#include "../../../drivers/media/video/et8ek8.h"
#include "../../../drivers/media/video/smia-sensor.h"

#include <media/ad5820.h>
#include <media/adp1653.h>

#include "board-rx51-camera.h"

/* Fake platform data begins here. */

static int fake_rx51_camera_g_priv(struct v4l2_int_device *s, void *priv)
{
	return -EBUSY;
}

struct et8ek8_platform_data rx51_et8ek8_platform_data = {
	.g_priv	= fake_rx51_camera_g_priv,
};
EXPORT_SYMBOL(rx51_et8ek8_platform_data);

struct ad5820_platform_data rx51_ad5820_platform_data = {
	.g_priv	= fake_rx51_camera_g_priv,
};
EXPORT_SYMBOL(rx51_ad5820_platform_data);

struct adp1653_platform_data rx51_adp1653_platform_data = {
	.g_priv	= fake_rx51_camera_g_priv,
};
EXPORT_SYMBOL(rx51_adp1653_platform_data);

struct smia_sensor_platform_data rx51_smia_sensor_platform_data = {
	.g_priv	= fake_rx51_camera_g_priv,
};
EXPORT_SYMBOL(rx51_smia_sensor_platform_data);

static struct i2c_board_info rx51_camera_board_info_2[] __initdata = {
#ifdef CONFIG_VIDEO_MACH_RX51_OLD_I2C
#if defined (CONFIG_VIDEO_ET8EK8) || defined (CONFIG_VIDEO_ET8EK8_MODULE)
	{
		I2C_BOARD_INFO(ET8EK8_NAME, ET8EK8_I2C_ADDR),
		.platform_data = &rx51_et8ek8_platform_data,
	},
#endif
#if defined (CONFIG_VIDEO_AD5820) || defined (CONFIG_VIDEO_AD5820_MODULE)
	{
		I2C_BOARD_INFO(AD5820_NAME, AD5820_I2C_ADDR),
		.platform_data = &rx51_ad5820_platform_data,
	},
#endif
#else /* CONFIG_VIDEO_MACH_RX51_OLD_I2C */
#if defined(CONFIG_VIDEO_ADP1653) || defined(CONFIG_VIDEO_ADP1653_MODULE)
	{
		I2C_BOARD_INFO(ADP1653_NAME, ADP1653_I2C_ADDR),
		.platform_data = &rx51_adp1653_platform_data,
	},
#endif
#endif
#if defined(CONFIG_VIDEO_SMIA_SENSOR) || defined(CONFIG_VIDEO_SMIA_SENSOR_MODULE)
	{
		I2C_BOARD_INFO(SMIA_SENSOR_NAME, SMIA_SENSOR_I2C_ADDR),
		.platform_data = &rx51_smia_sensor_platform_data,
	},
#endif
};

static struct i2c_board_info rx51_camera_board_info_3[] __initdata = {
#ifdef CONFIG_VIDEO_MACH_RX51_OLD_I2C
#if defined (CONFIG_VIDEO_ADP1653) || defined (CONFIG_VIDEO_ADP1653_MODULE)
	{
		I2C_BOARD_INFO(ADP1653_NAME, ADP1653_I2C_ADDR),
		.platform_data = &rx51_adp1653_platform_data,
	},
#endif
#else /* CONFIG_VIDEO_MACH_RX51_OLD_I2C */
#if defined (CONFIG_VIDEO_ET8EK8) || defined (CONFIG_VIDEO_ET8EK8_MODULE)
	{
		I2C_BOARD_INFO(ET8EK8_NAME, ET8EK8_I2C_ADDR),
		.platform_data = &rx51_et8ek8_platform_data,
	},
#endif
#if defined (CONFIG_VIDEO_AD5820) || defined (CONFIG_VIDEO_AD5820_MODULE)
	{
		I2C_BOARD_INFO(AD5820_NAME, AD5820_I2C_ADDR),
		.platform_data = &rx51_ad5820_platform_data,
	},
#endif
#endif
};

static int __init rx51_camera_base_init(void)
{
	int err;

	if (!(machine_is_nokia_rx51() || machine_is_nokia_rx71()))
		return 0;

	/* I2C */
	err = i2c_register_board_info(2, rx51_camera_board_info_2,
				      ARRAY_SIZE(rx51_camera_board_info_2));
	if (err) {
		printk(KERN_ERR
		       "%s: failed to register rx51_camera_board_info_2\n",
		       __func__);
		return err;
	}
	err = i2c_register_board_info(3, rx51_camera_board_info_3,
				      ARRAY_SIZE(rx51_camera_board_info_3));
	if (err) {
		printk(KERN_ERR
		       "%s: failed to register rx51_camera_board_info_3\n",
		       __func__);
		return err;
	}

	return 0;
}

arch_initcall(rx51_camera_base_init);

#endif
