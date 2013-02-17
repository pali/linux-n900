/*
 * arch/arm/mach-omap2/board-rx51-camera.c
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

#include <linux/i2c.h>
#include <linux/i2c/twl.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/platform_device.h>
#include <linux/videodev2.h>

#include <asm/gpio.h>

#include "../../../drivers/media/platform/omap3isp/isp.h"
#include "../../../drivers/media/platform/omap3isp/ispreg.h"

#include <media/et8ek8.h>
#include <media/smiapp.h>
#include <media/ad5820.h>
#include <media/adp1653.h>

#include "control.h"
#include "devices.h"
#include "omap34xx.h"

#define ADP1653_GPIO_ENABLE	88	/* Used for resetting ADP1653 */
#define ADP1653_GPIO_INT	167	/* Fault interrupt */
#define ADP1653_GPIO_STROBE	126	/* Pin used in cam_strobe mode ->
					 * control using ISP drivers */

#define STINGRAY_RESET_GPIO	102
#define ACMELITE_RESET_GPIO	97	/* Used also to MUX between cameras */

#define RX51_CAMERA_STINGRAY	(1 << 0)
#define RX51_CAMERA_LENS	(1 << 1)
#define RX51_CAMERA_ACMELITE	(1 << 2)

#define RX51_CAMERA_PRIMARY	(RX51_CAMERA_STINGRAY | RX51_CAMERA_LENS)
#define RX51_CAMERA_SECONDARY	RX51_CAMERA_ACMELITE

static DEFINE_MUTEX(rx51_camera_mutex);
static unsigned int rx51_camera_xshutdown;

static int rx51_camera_set_xshutdown(unsigned int which, int set)
{
	unsigned int new = rx51_camera_xshutdown;
	int ret = 0;

	mutex_lock(&rx51_camera_mutex);

	if (set)
		new |= which;
	else
		new &= ~which;

	/* The primary and secondary cameras can't be powered on at the same
	 * time.
	 */
	if ((new & RX51_CAMERA_PRIMARY) && (new & RX51_CAMERA_SECONDARY)) {
		ret = -EBUSY;
		goto out;
	}

	if ((rx51_camera_xshutdown & RX51_CAMERA_PRIMARY) !=
	    (new & RX51_CAMERA_PRIMARY))
		gpio_set_value(STINGRAY_RESET_GPIO,
			       new & RX51_CAMERA_PRIMARY ? 1 : 0);

	if ((rx51_camera_xshutdown & RX51_CAMERA_SECONDARY) !=
	    (new & RX51_CAMERA_SECONDARY))
		gpio_set_value(ACMELITE_RESET_GPIO,
			       new & RX51_CAMERA_SECONDARY ? 1 : 0);

	rx51_camera_xshutdown = new;

out:
	mutex_unlock(&rx51_camera_mutex);
	return ret;
}

static void __init rx51_stingray_init(void)
{
	if (gpio_request(STINGRAY_RESET_GPIO, "stingray reset") != 0) {
		printk(KERN_INFO "%s: unable to acquire Stingray reset gpio\n",
		       __FUNCTION__);
		return;
	}

	/* XSHUTDOWN off, reset  */
	gpio_direction_output(STINGRAY_RESET_GPIO, 0);
	gpio_set_value(STINGRAY_RESET_GPIO, 0);
}

static void __init rx51_acmelite_init(void)
{
	if (gpio_request(ACMELITE_RESET_GPIO, "acmelite reset") != 0) {
		printk(KERN_INFO "%s: unable to acquire Acme Lite reset gpio\n",
		       __FUNCTION__);
		return;
	}

	/* XSHUTDOWN off, reset  */
	gpio_direction_output(ACMELITE_RESET_GPIO, 0);
	gpio_set_value(ACMELITE_RESET_GPIO, 0);

	gpio_free(ACMELITE_RESET_GPIO);
}

static int __init rx51_adp1653_init(void)
{
	int err;

	err = gpio_request(ADP1653_GPIO_ENABLE, "adp1653 enable");
	if (err) {
		printk(KERN_ERR ADP1653_NAME
		       " Failed to request EN gpio\n");
		err = -ENODEV;
		goto err_omap_request_gpio;
	}

	err = gpio_request(ADP1653_GPIO_INT, "adp1653 interrupt");
	if (err) {
		printk(KERN_ERR ADP1653_NAME " Failed to request IRQ gpio\n");
		err = -ENODEV;
		goto err_omap_request_gpio_2;
	}

	err = gpio_request(ADP1653_GPIO_STROBE, "adp1653 strobe");
	if (err) {
		printk(KERN_ERR ADP1653_NAME
		       " Failed to request STROBE gpio\n");
		err = -ENODEV;
		goto err_omap_request_gpio_3;
	}

	gpio_direction_output(ADP1653_GPIO_ENABLE, 0);
	gpio_direction_input(ADP1653_GPIO_INT);
	gpio_direction_output(ADP1653_GPIO_STROBE, 0);

	return 0;

err_omap_request_gpio_3:
	gpio_free(ADP1653_GPIO_INT);

err_omap_request_gpio_2:
	gpio_free(ADP1653_GPIO_ENABLE);

err_omap_request_gpio:
	return err;
}

static int __init rx51_camera_hw_init(void)
{
	int rval;

	mutex_init(&rx51_camera_mutex);

	rval = rx51_adp1653_init();
	if (rval)
		return rval;

	rx51_stingray_init();
	rx51_acmelite_init();

	return 0;
}

/*
 *
 * Stingray
 *
 */

#define STINGRAY_XCLK		ISP_XCLK_A

static int rx51_stingray_set_xclk(struct v4l2_subdev *subdev, int hz)
{
	struct isp_device *isp = v4l2_dev_to_isp_device(subdev->v4l2_dev);

	if (!isp)
		return 1;

	isp->platform_cb.set_xclk(isp, hz, STINGRAY_XCLK);

	return 0;
}

static int rx51_stingray_set_xshutdown(struct v4l2_subdev *subdev, int set)
{
	int ret;

	ret = rx51_camera_set_xshutdown(RX51_CAMERA_STINGRAY, set);
	if (ret == 0 && set) {
		/* CONTROL_CSIRXFE
		 * Data/strobe, enable transceiver, disable reset
		 */
		omap_ctrl_writel(OMAP343X_CSIB_RESET | OMAP343X_CSIB_PWRDNZ |
			    OMAP343X_CSIB_SELFORM,
			    OMAP343X_CONTROL_CSIRXFE);
	}

	return ret;
}

static struct et8ek8_platform_data rx51_et8ek8_platform_data = {
	.set_xclk		= rx51_stingray_set_xclk,
	.set_xshutdown		= rx51_stingray_set_xshutdown,
};

/*
 *
 * AD5820
 *
 */

static int rx51_ad5820_set_xshutdown(struct v4l2_subdev *subdev, int set)
{
	return rx51_camera_set_xshutdown(RX51_CAMERA_LENS, set);
}

static struct ad5820_platform_data rx51_ad5820_platform_data = {
	.set_xshutdown		= rx51_ad5820_set_xshutdown,
};

/*
 *
 * ADP1653
 *
 */

static int rx51_adp1653_power(struct v4l2_subdev *subdev, int on)
{
	gpio_set_value(ADP1653_GPIO_ENABLE, on);
	if (on) {
		/* Some delay is apparently required. */
		udelay(20);
	}

	return 0;
}

static struct adp1653_platform_data rx51_adp1653_platform_data = {
	.power			 = rx51_adp1653_power,
	/* Must be limited to 500 ms in RX-51 */
	.max_flash_timeout	 = 500000,		/* us */
	/* Must be limited to 320 mA in RX-51 B3 and newer hardware */
	.max_flash_intensity	 = ADP1653_FLASH_INTENSITY_REG_TO_mA(19),
	/* Must be limited to 50 mA in RX-51 */
	.max_torch_intensity	 = ADP1653_FLASH_INTENSITY_REG_TO_mA(1),
	.max_indicator_intensity = ADP1653_INDICATOR_INTENSITY_REG_TO_uA(
		ADP1653_REG_OUT_SEL_ILED_MAX),
};

/*
 *
 * Acmelite
 *
 */

#define ACMELITE_XCLK		ISP_XCLK_A

static int rx51_acmelite_set_xclk(struct v4l2_subdev *subdev, int hz)
{
	struct isp_device *isp = v4l2_dev_to_isp_device(subdev->v4l2_dev);

	if (!isp)
		return 1;

	isp->platform_cb.set_xclk(isp, hz, ACMELITE_XCLK);

	return 0;
}

static struct smiapp_platform_data rx51_smiapp_sensor_platform_data = {
	.ext_clk		= 9.6 * 1000 * 1000,
	.lanes			= 1,
	/* bit rate / ddr */
	.op_sys_clock		= (s64 []){ 12000000 * 10 / 2, 0 },
	.csi_signalling_mode	= SMIAPP_CSI_SIGNALLING_MODE_CCP2_DATA_CLOCK,
	.set_xclk		= rx51_acmelite_set_xclk,
	.xshutdown		= ACMELITE_RESET_GPIO,
};

/*
 *
 * Init it all
 *
 */

#define ET8EK8_I2C_BUS_NUM		3
#define AD5820_I2C_BUS_NUM		3
#define ADP1653_I2C_BUS_NUM		2
#define SMIAPP_I2C_BUS_NUM		2

static struct i2c_board_info rx51_camera_i2c_devices[] = {
	{
		I2C_BOARD_INFO(ET8EK8_NAME, ET8EK8_I2C_ADDR),
		.platform_data = &rx51_et8ek8_platform_data,
	},
	{
		I2C_BOARD_INFO(AD5820_NAME, AD5820_I2C_ADDR),
		.platform_data = &rx51_ad5820_platform_data,
	},
	{
		I2C_BOARD_INFO(ADP1653_NAME, ADP1653_I2C_ADDR),
		.platform_data = &rx51_adp1653_platform_data,
	},
	{
		I2C_BOARD_INFO(SMIAPP_NAME, SMIAPP_DFL_I2C_ADDR),
		.platform_data = &rx51_smiapp_sensor_platform_data,
	},
};

static struct isp_subdev_i2c_board_info rx51_camera_primary_subdevs[] = {
	{
		.board_info = &rx51_camera_i2c_devices[0],
		.i2c_adapter_id = ET8EK8_I2C_BUS_NUM,
	},
	{
		.board_info = &rx51_camera_i2c_devices[1],
		.i2c_adapter_id = AD5820_I2C_BUS_NUM,
	},
	{
		.board_info = &rx51_camera_i2c_devices[2],
		.i2c_adapter_id = ADP1653_I2C_BUS_NUM,
	},
	{ NULL, 0, },
};

static struct isp_subdev_i2c_board_info rx51_camera_secondary_subdevs[] = {
	{
		.board_info = &rx51_camera_i2c_devices[3],
		.i2c_adapter_id = SMIAPP_I2C_BUS_NUM,
	},
	{ NULL, 0, },
};

static struct isp_v4l2_subdevs_group rx51_camera_subdevs[] = {
	{
		.subdevs = rx51_camera_primary_subdevs,
		.interface = ISP_INTERFACE_CCP2B_PHY1,
		.bus = { .ccp2 = {
			.strobe_clk_pol		= 0,
			.crc			= 1,
			.ccp2_mode		= 1,
			.phy_layer		= 1,
			.vpclk_div		= 1,
		} },
	},
	{
		.subdevs = rx51_camera_secondary_subdevs,
		.interface = ISP_INTERFACE_CCP2B_PHY1,
		.bus = { .ccp2 = {
			.strobe_clk_pol		= 0,
			.crc			= 1,
			.ccp2_mode		= 1,
			.phy_layer		= 1,
			.vpclk_div		= 1,
		} },
	},
	{ NULL, 0, },
};

static struct isp_platform_data rx51_isp_platform_data = {
	.subdevs = rx51_camera_subdevs,
};

void __init rx51_camera_init(void)
{
	if (rx51_camera_hw_init()) {
		printk(KERN_WARNING "%s: Unable to initialize camera\n",
		       __func__);
		return;
	}

	if (omap3_init_camera(&rx51_isp_platform_data) < 0)
		printk(KERN_WARNING "%s: Unable to register camera platform "
		       "device\n", __func__);
}
