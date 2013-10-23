/**
 * arch/arm/mach-omap2/board-rm680-camera.c
 *
 * Copyright (C) 2010 Nokia Corporation
 * Contact: Vimarsh Zutshi <vimarsh.zutshi@nokia.com>
 *
 * Based on board-rx71-camera.c by Vimarsh Zutshi
 * Based on board-rx51-camera.c by Sakari Ailus
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

#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/platform_device.h>
#include <linux/videodev2.h>

#include <plat/gpio.h>
#include <plat/control.h>
#include <plat/omap-pm.h>

#include "../../../drivers/media/video/omap3isp/isp.h"
#include "../../../drivers/media/video/omap3isp/ispreg.h"
#include "../../../drivers/media/video/omap3isp/ispcsi2.h"

#include <media/smiapp.h>
#include <media/ad58xx.h>
#include <media/as3645a.h>	/* Senna Flash driver */
#include <asm/mach-types.h>

#include "devices.h"

#define MAIN_CAMERA_RESET_GPIO	102
#define SEC_CAMERA_RESET_GPIO	97

#define RM680_PRI_SENSOR	1
#define RM680_PRI_LENS		2
#define RM680_SEC_SENSOR	3
#define MAIN_CAMERA_XCLK	ISP_XCLK_A
#define SEC_CAMERA_XCLK		ISP_XCLK_B

/*
 *
 * RM-680 board revisions
 * It is important to identify board revisions s05_or_lower and
 * s06_or_higher because polarity of the CSI2 lanes has been changed
 * in s06 and onwards.
 *
 */
static inline int board_in_revs(const u32 *revs, int num_revs)
{
	int i;

	for (i = 0; i < num_revs; ++i)
		if (system_rev == revs[i])
			return 1;

	return 0;
}

static inline int board_is_s05_or_lower(void)
{
	static u32 revs[] = {
		0x0103, 0x0105, 0x0107,		/* S0.1 */
		0x0201, 0x0203, 0x0205, 	/* S0.2 */
		0x0301, 0x0303,			/* S0.3 wing */
		0x0305, 0x0307, 0x0309, 	/* S0.4 wing */
		0x0120, 0x0122, 0x0124, 	/* S0.5 */
	};

	return board_in_revs(revs, ARRAY_SIZE(revs));;
}

static inline int board_is_s06_or_higher(void)
{
	/*
	 * 0x0220, 0x0221, 0x0224, 0x0225, 0x0226, - S0.6
	 */

	return !board_is_s05_or_lower();
}

/*
 *
 * HW initialization
 *
 *
 */
static int __init rm680_main_camera_init(void)
{
	/*
	 * Main camera reset gpio (GPIO 102) is allocated on the OMAP side but
	 * not connected to batray since it does not have a reset signal. NOLO
	 * still configures this pin as GPIO and not in Safe Mode. So we still
	 * acquire it and configure it as output low.
	 *
	 */
	if (gpio_request(MAIN_CAMERA_RESET_GPIO, "cam reset (unused)") != 0) {
		printk(KERN_INFO "%s: unable to acquire main camera "
		      "reset gpio\n", __func__);
		return -ENODEV;
	}

	gpio_direction_output(MAIN_CAMERA_RESET_GPIO, 0);
	gpio_set_value(MAIN_CAMERA_RESET_GPIO, 0);

	return 0;
}

static int __init rm680_sec_camera_init(void)
{
	if (gpio_request(SEC_CAMERA_RESET_GPIO, "sec_camera reset") != 0) {
		printk(KERN_INFO "%s: unable to acquire secondary "
		       "camera reset gpio\n", __func__);
		return -ENODEV;
	}

	/* XSHUTDOWN off, reset  */
	gpio_direction_output(SEC_CAMERA_RESET_GPIO, 0);
	gpio_set_value(SEC_CAMERA_RESET_GPIO, 0);

	return 0;
}

static int __init rm680_camera_hw_init(void)
{
	int rval;

	rval = rm680_main_camera_init();
	if (rval)
		return -ENODEV;

	rval = rm680_sec_camera_init();
	if (rval) {
		gpio_free(MAIN_CAMERA_RESET_GPIO);
		return -ENODEV;
	}

	return 0;
}

/*
 *
 * Main Camera Module EXTCLK
 * Used by the sensor and the actuator driver.
 *
 */
static struct camera_xclk {
	u32 hz;
	u32 lock;
	u8 xclksel;
} cameras_xclk;

static DEFINE_MUTEX(lock_xclk);

static int rm680_update_xclk(struct v4l2_subdev *subdev, u32 hz, u32 which,
			     u8 xclksel)
{
	struct isp_device *isp = v4l2_dev_to_isp_device(subdev->v4l2_dev);
	int ret;

	mutex_lock(&lock_xclk);

	if (which == RM680_SEC_SENSOR) {
		if (cameras_xclk.xclksel == MAIN_CAMERA_XCLK) {
			ret = -EBUSY;
			goto done;
		}
	} else {
		if (cameras_xclk.xclksel == SEC_CAMERA_XCLK) {
			ret = -EBUSY;
			goto done;
		}
	}

	if (hz) {	/* Turn on */
		cameras_xclk.lock |= which;
		if (cameras_xclk.hz == 0) {
			isp->platform_cb.set_xclk(isp, hz, xclksel);
			cameras_xclk.hz = hz;
			cameras_xclk.xclksel = xclksel;
		}
	} else {	/* Turn off */
		cameras_xclk.lock &= ~which;
		if (cameras_xclk.lock == 0) {
			isp->platform_cb.set_xclk(isp, 0, xclksel);
			cameras_xclk.hz = 0;
			cameras_xclk.xclksel = 0;
		}
	}

	ret = cameras_xclk.hz;

done:
	mutex_unlock(&lock_xclk);
	return ret;
}

/*
 *
 * Main Camera Sensor
 *
 */

static struct isp_csiphy_lanes_cfg rm680_main_camera_csi2_lanecfg_s05 = {
	.clk = {
		.pol = 0,
		.pos = 2,
	},
	.data[0] = {
		.pol = 0,
		.pos = 3,
	},
	.data[1] = {
		.pol = 0,
		.pos = 1,
	},
};

static struct isp_csiphy_lanes_cfg rm680_main_camera_csi2_lanecfg_s06 = {
	.clk = {
		.pol = 1,
		.pos = 2,
	},
	.data[0] = {
		.pol = 1,
		.pos = 3,
	},
	.data[1] = {
		.pol = 1,
		.pos = 1,
	},
};

static unsigned int rm680_calc_pixelclk(struct smia_mode *mode)
{
	static const int S = 8;
	unsigned int pixelclk;

	/*
	 * Calculate average pixel clock per line. Assume buffers can spread
	 * the data over horizontal blanking time. Rounding upwards.
	 * Tuukka Toivonen: board-rx51-camera.c
	 */
	pixelclk = mode->window_width
		 * (((mode->pixel_clock + (1 << S) - 1) >> S) + mode->width - 1)
		 / mode->width;
	pixelclk <<= S;

	return pixelclk;
}

/*
 * THS_TERM: Programmed value = ceil(12.5 ns/DDRClk period) - 1.
 * THS_SETTLE: Programmed value = ceil(90 ns/DDRClk period) + 3.
 */
#define THS_TERM_D 2000000
#define THS_TERM(ddrclk_khz)					\
(								\
	((25 * (ddrclk_khz)) % THS_TERM_D) ? 			\
		((25 * (ddrclk_khz)) / THS_TERM_D) :		\
		((25 * (ddrclk_khz)) / THS_TERM_D) - 1		\
)

#define THS_SETTLE_D 1000000
#define THS_SETTLE(ddrclk_khz)					\
(								\
	((90 * (ddrclk_khz)) % THS_SETTLE_D) ? 			\
		((90 * (ddrclk_khz)) / THS_SETTLE_D) + 4 :	\
		((90 * (ddrclk_khz)) / THS_SETTLE_D) + 3	\
)

/*
 * TCLK values are OK at their reset values
 */
#define TCLK_TERM	0
#define TCLK_MISS	1
#define TCLK_SETTLE	14

static void rm680_main_camera_csi2_configure(struct v4l2_subdev *subdev,
					     struct smia_mode *mode)
{
	struct isp_device *isp = v4l2_dev_to_isp_device(subdev->v4l2_dev);
	struct isp_csiphy_lanes_cfg *lanecfg;
	struct isp_csiphy_dphy_cfg csi2phy;
	unsigned int pixelclk;
	int csi2_ddrclk_khz;

	/*
	 * SCM.CONTROL_CAMERA_PHY_CTRL
	 * - bit[4]    : 0  CSIPHY1 data sent to CSIB
	 * - bit [3:2] : 10 CSIPHY1 in CCP2 Data/Clock Mode
	 * - bit [1:0] : 00 CSIPHY2 in D-PHY Mode
	 */
	omap_writel(0x08,
		    OMAP343X_CTRL_BASE + OMAP3630_CONTROL_CAMERA_PHY_CTRL);

	csi2_ddrclk_khz = ((mode->opsys_clock / 1000) /
				(2 * isp->isp_csiphy2.num_data_lanes));
	csi2phy.ths_term = THS_TERM(csi2_ddrclk_khz);
	csi2phy.ths_settle = THS_SETTLE(csi2_ddrclk_khz);
	csi2phy.tclk_term = TCLK_TERM;
	csi2phy.tclk_miss = TCLK_MISS;
	csi2phy.tclk_settle = TCLK_SETTLE;

	if (board_is_s05_or_lower())
		lanecfg = &rm680_main_camera_csi2_lanecfg_s05;
	else if (board_is_s06_or_higher())
		lanecfg = &rm680_main_camera_csi2_lanecfg_s06;
	else
		BUG();

	isp->platform_cb.csiphy_config(&isp->isp_csiphy2, &csi2phy, lanecfg);

	pixelclk = rm680_calc_pixelclk(mode);

	isp->platform_cb.set_pixel_clock(isp, pixelclk);
}

static int rm680_main_camera_set_xclk(struct v4l2_subdev *sd, int hz)
{
	return rm680_update_xclk(sd, hz, RM680_PRI_SENSOR, MAIN_CAMERA_XCLK);
}

static struct smiapp_flash_strobe_parms rm680_main_camera_strobe_setup = {
	.mode			= 0x0c,
	.strobe_width_high_us	= 100000,
	.strobe_delay		= 0,
	.stobe_start_point	= 0,
	.trigger		= 0,
};

static struct smiapp_platform_data rm680_main_camera_platform_data = {
	.i2c_addr_dfl		= SMIAPP_DFL_I2C_ADDR,
	.i2c_addr_alt		= SMIAPP_ALT_I2C_ADDR,
	.nvm_size		= 16 * 64,
	.ext_clk		= (9.6 * 1000 * 1000),
	.module_board_orient	= SMIAPP_MODULE_BOARD_ORIENT_180,
	.strobe_setup		= &rm680_main_camera_strobe_setup,
	.csi_configure		= rm680_main_camera_csi2_configure,
	.set_xclk		= rm680_main_camera_set_xclk,
};

/*
 *
 * Main Camera Actuator Driver
 *
 */

static int rm680_lens_set_xclk(struct v4l2_subdev *sd, u32 hz)
{
	return rm680_update_xclk(sd, hz, RM680_PRI_LENS, MAIN_CAMERA_XCLK);
}

/* When no activity on EXTCLK, the AD5836 enters power-down mode */
static struct ad58xx_platform_data rm680_ad5836_platform_data = {
	.ext_clk		= (9.6 * 1000 * 1000),
	.set_xclk		= rm680_lens_set_xclk
};

/*
 * Main Camera Flash
 */

static void rm680_as3645a_setup_ext_strobe(int enable)
{
	if (enable)
		rm680_main_camera_platform_data.strobe_setup->trigger = 1;
	else
		rm680_main_camera_platform_data.strobe_setup->trigger = 0;
}

static void rm680_as3645a_set_strobe_width(u32 width_in_us)
{
	rm680_main_camera_platform_data.strobe_setup->strobe_width_high_us =
								width_in_us;
}

static struct as3645a_flash_torch_parms rm680_main_camera_flash_setup = {
	.flash_min_current	= 200,
	.flash_max_current	= 320,
	.torch_min_current	= 20,
	.torch_max_current	= 60,
	.timeout_min		= 100000,
	.timeout_max		= 150000,
};

static struct as3645a_platform_data rm680_as3645a_platform_data = {
	.num_leds		= 2,
	.use_ext_flash_strobe	= 1,
	.setup_ext_strobe	= rm680_as3645a_setup_ext_strobe,
	.set_strobe_width	= rm680_as3645a_set_strobe_width,
	.flash_torch_limits	= &rm680_main_camera_flash_setup,
};

/*
 *
 * SECONDARY CAMERA Sensor
 *
 */

static struct isp_csiphy_lanes_cfg rm680_sec_camera_csiphy_lanecfg = {
	.clk = {
		.pol = 0,
		.pos = 1,
	},
	.data[0] = {
		.pol = 0,
		.pos = 2,
	},
};

static void rm680_sec_camera_configure_interface(struct v4l2_subdev *subdev,
						 struct smia_mode *mode)
{
	struct isp_device *isp = v4l2_dev_to_isp_device(subdev->v4l2_dev);
	struct isp_csiphy_dphy_cfg dummy_phy;
	unsigned int pixelclk;

	/*
	 * SCM.CONTROL_CAMERA_PHY_CTRL
	 * - bit[4]    : 0  CSIPHY1 data sent to CSIB
	 * - bit [3:2] : 10 CSIPHY1 in CCP2 Data/Clock Mode
	 * - bit [1:0] : 00 CSIPHY2 in D-PHY Mode
	 */
	omap_writel(0x08,
		    OMAP343X_CTRL_BASE + OMAP3630_CONTROL_CAMERA_PHY_CTRL);

	memset(&dummy_phy, 0, sizeof(dummy_phy));
	isp->platform_cb.csiphy_config(&isp->isp_csiphy1, &dummy_phy,
				       &rm680_sec_camera_csiphy_lanecfg);

	pixelclk = rm680_calc_pixelclk(mode);

	isp->platform_cb.set_pixel_clock(isp, pixelclk);
}

static int rm680_sec_camera_set_xclk(struct v4l2_subdev *subdev, int hz)
{
	return rm680_update_xclk(subdev, hz, RM680_SEC_SENSOR, SEC_CAMERA_XCLK);
}

static int rm680_sec_camera_set_xshutdown(struct v4l2_subdev *subdev, u8 set)
{
	gpio_set_value(SEC_CAMERA_RESET_GPIO, !!set);
	return 0;
}

static struct smiapp_platform_data rm680_sec_camera_platform_data = {
	.ext_clk		= (10.8 * 1000 * 1000),
	.csi_configure		= rm680_sec_camera_configure_interface,
	.set_xclk		= rm680_sec_camera_set_xclk,
	.set_xshutdown		= rm680_sec_camera_set_xshutdown,
};

/*
 *
 * Init all the modules
 *
 */

#define CAMERA_I2C_BUS_NUM		2
#define AD5836_I2C_BUS_NUM		2
#define AS3645A_I2C_BUS_NUM		2

static struct i2c_board_info rm680_camera_i2c_devices[] = {
	{
		I2C_BOARD_INFO(SMIAPP_NAME, SMIAPP_ALT_I2C_ADDR),
		.platform_data = &rm680_main_camera_platform_data,
	},
	{
		I2C_BOARD_INFO(AD58XX_NAME, AD58XX_I2C_ADDR),
		.platform_data = &rm680_ad5836_platform_data,
	},
	{
		I2C_BOARD_INFO(AS3645A_NAME, AS3645A_I2C_ADDR),
		.platform_data = &rm680_as3645a_platform_data,
	},
	{
		I2C_BOARD_INFO(SMIAPP_NAME, SMIAPP_DFL_I2C_ADDR),
		.platform_data = &rm680_sec_camera_platform_data,
	},
};

static struct isp_subdev_i2c_board_info rm680_camera_primary_subdevs[] = {
	{
		.board_info = &rm680_camera_i2c_devices[0],
		.i2c_adapter_id = CAMERA_I2C_BUS_NUM,
	},
	{
		.board_info = &rm680_camera_i2c_devices[1],
		.i2c_adapter_id = AD5836_I2C_BUS_NUM,
	},
	{
		.board_info = &rm680_camera_i2c_devices[2],
		.i2c_adapter_id = AS3645A_I2C_BUS_NUM,
	},
	{ NULL, 0, },
};

static struct isp_subdev_i2c_board_info rm680_camera_secondary_subdevs[] = {
	{
		.board_info = &rm680_camera_i2c_devices[3],
		.i2c_adapter_id = CAMERA_I2C_BUS_NUM,
	},
	{ NULL, 0, },
};

static struct isp_v4l2_subdevs_group rm680_camera_subdevs[] = {
	{
		.subdevs = rm680_camera_primary_subdevs,
		.interface = ISP_INTERFACE_CSI2A_PHY2,
		.bus = { .csi2 = {
			.crc			= 1,
			.vpclk_div		= 1,
		} },
	},
	{
		.subdevs = rm680_camera_secondary_subdevs,
		.interface = ISP_INTERFACE_CCP2B_PHY1,
		.bus = { .ccp2 = {
			.strobe_clk_pol		= 0,
			.crc			= 0,
			.ccp2_mode		= 0,
			.phy_layer		= 0,
			.vpclk_div		= 2,
		} },
	},
	{ NULL, 0, },
};

static struct isp_platform_data rm680_isp_platform_data = {
	.subdevs = rm680_camera_subdevs,
};

void __init rm680_camera_init(void)
{
	int rval;

	rval = rm680_camera_hw_init();
	if (rval) {
		printk(KERN_WARNING "%s: unable to initialise camera\n",
		       __func__);
		return;
	}

	if (omap3_init_camera(&rm680_isp_platform_data) < 0)
		printk(KERN_WARNING
		       "%s: unable to register camera platform device\n",
		       __func__);
}
