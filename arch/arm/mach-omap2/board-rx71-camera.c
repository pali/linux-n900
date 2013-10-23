/**
 * arch/arm/mach-omap2/board-rx71-camera.c
 *
 * Copyright (C) 2008 Nokia Corporation
 * Contact: Vimarsh Zutshi <vimarsh.zutshi@nokia.com>
 *
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
#include <linux/i2c.h>
#include <linux/i2c/twl4030.h>

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

#define RX71_PRI_SENSOR		1
#define RX71_PRI_LENS		2
#define RX71_SEC_SENSOR		3
#define MAIN_CAMERA_XCLK	ISP_XCLK_A
#define SEC_CAMERA_XCLK		ISP_XCLK_B

/* Macro 4.1 or Devlon 4.x in RM-696 configuration */
static int board_is_devlon4_696(void)
{
	return system_rev == 0x4000 || system_rev == 0x4005 ||
		system_rev == 0x4100 || system_rev == 0x4110 ||
		system_rev == 0x4120 || system_rev == 0x4130 ||
		system_rev == 0x4140;
}

/* Macro 4.1 or Devlon 4.x in RM-680 configuration or Macro 3.x */
static int board_is_devlon4_680(void)
{
	static u32 revs[] = {
		/* Devlon 4.0 HWID in RM-680 configuration */
		0x3225,

		/* Macro 3.1 HWIDs */
		0x3102, 0x3104, 0x3106, 0x3108, 0x3112, 0x3114,
		0x3116, 0x3122, 0x3132, 0x3142, 0x3152,

		/* Macro 3.2 HWIDs */
		0x3202, 0x3204, 0x3206, 0x3208, 0x3210, 0x3212, 0x3214, 0x3216,

		/* Macro 3.3 HWIDs */
		0x3310, 0x3314, 0x3318,

		/* Macro 4.1 HWIDs */
		0x3500,
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(revs); ++i)
		if (system_rev == revs[i])
			return 1;

	return 0;
}

static int board_is_devlon3(void)
{
	return system_rev >= 0x3000 && system_rev < 0x4000 &&
	       !board_is_devlon4_680();
}

static inline int board_is_known(void)
{
	return board_is_devlon3() ||
	       board_is_devlon4_680() ||
	       board_is_devlon4_696();
}

static int __init rx71_main_camera_init(void)
{
	if (board_is_devlon4_696())
		return 0; /* RESET_GPIO is not allocated any more */

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

static int __init rx71_sec_camera_init(void)
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

static int __init rx71_camera_hw_init(void)
{
	int rval;

	rval = rx71_main_camera_init();
	if (rval)
		return -ENODEV;

	rval = rx71_sec_camera_init();
	if (rval) {
		gpio_free(MAIN_CAMERA_RESET_GPIO);
		return -ENODEV;
	}

	return 0;
}

/**
 *
 * Main Camera Module EXTCLK, XSHUTDOWN
 * Used by the sensor and the actuator driver.
 *
 */
static struct camera_xclk {
	u32 hz;
	u32 lock;
	u8 xclksel;
} cameras_xclk;

static DEFINE_MUTEX(lock_xclk);

static int rx71_update_xclk(struct v4l2_subdev *subdev, u32 hz, u32 which,
			    u8 xclksel)
{
	struct isp_device *isp = v4l2_dev_to_isp_device(subdev->v4l2_dev);
	int ret;

	mutex_lock(&lock_xclk);

	if (which == RX71_SEC_SENSOR) {
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

/**
 *
 * Main Camera Sensor
 *
 */

static struct isp_csiphy_lanes_cfg rx71_main_camera_csi2_lanecfg_s3 = {
	.clk = {
		.pol = 0,
		.pos = 1,
	},
	.data[0] = {
		.pol = 0,
		.pos = 2,
	},
	.data[1] = {
		.pol = 0,
		.pos = 3,
	},
};

static struct isp_csiphy_lanes_cfg rx71_main_camera_csi2_lanecfg_s4 = {
	.clk = {
		.pol = 0,
		.pos = 1,
	},
	.data[0] = {
		.pol = 0,
		.pos = 2,
	},
	.data[1] = {
		.pol = 0,
		.pos = 3,
	},
};

static struct isp_csiphy_lanes_cfg rx71_main_camera_csi2_lanecfg_macro = {
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

static unsigned int rx71_calc_pixelclk(struct smia_mode *mode)
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

/**
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

/**
 * TCLK values are OK at their reset values
 */
#define TCLK_TERM	0
#define TCLK_MISS	1
#define TCLK_SETTLE	14

static void rx71_main_camera_csi2_configure(struct v4l2_subdev *subdev,
					    struct smia_mode *mode)
{
	struct isp_device *isp = v4l2_dev_to_isp_device(subdev->v4l2_dev);
	struct isp_csiphy_dphy_cfg csi2phy;
	static struct isp_csiphy_lanes_cfg *lanecfg;
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

	if (board_is_devlon4_680())
		lanecfg = &rx71_main_camera_csi2_lanecfg_macro;
	else if (board_is_devlon4_696())
		lanecfg = &rx71_main_camera_csi2_lanecfg_s4;
	else	/* devlon3 */
		lanecfg = &rx71_main_camera_csi2_lanecfg_s3;

	isp->platform_cb.csiphy_config(&isp->isp_csiphy2, &csi2phy, lanecfg);

	pixelclk = rx71_calc_pixelclk(mode);

	isp->platform_cb.set_pixel_clock(isp, pixelclk);
}

static int rx71_main_camera_set_xclk(struct v4l2_subdev *sd, int hz)
{
	return rx71_update_xclk(sd, hz, RX71_PRI_SENSOR, MAIN_CAMERA_XCLK);
}

/*
 * Lada TS samples don't have their own sensors in there yet.
 * Lada TS Toshiba has Toshiba Gambino Sensor
 * Lada TS Fujinon has Sony Gambino Sensor
 *
 * Moreover the SMIA++ registers of model_id and manufacturer_id are
 * not present on the modules yet. Instead we have to use sensor model id
 * and sensor manufacturer id for identification. This should hopefully
 * get resolved with the ES samples.
 *
 * For now provide the translation to the driver from sensor idents to
 * module idents
 *
 * Lada ES1.0 Fujinon has Fujinon Lada Sensor but the SMIA++ registers
 * are still missing.
 *
 * Lada ES1.0 Toshiba has Toshiba Lada sensor and SMIA++ registers are
 * programmed as well
 */
static const struct smiapp_module_ident rx71_main_camera_idents[] = {
	{
		.sensor_manu_id 	= 0x0b,
		.sensor_model_id	= 0x0088,
		.name			= "smiapp-001",
	},  /* Sony Gambino - imx088es */
	{
		.sensor_manu_id		= 0x0c,
		.sensor_model_id	= 0x218e,
		.name			= "smiapp-002",
	}, /* Toshiba Gambino - tcm8596md */
	{
		.sensor_manu_id		= 0x0b,
		.sensor_model_id	= 0x0125,
		.name			= "smiapp-003",
	}, /* Fujinon Lada - imx125 */
	{
		.manu_id		= 0x0c,
		.model_id		= 0x560f,
		.name			= "smiapp-004",
	}, /* Toshiba Lada - jt8ew9 */
};

static struct smiapp_module_ident rx71_main_camera;

static const struct smiapp_module_ident *
rx71_main_camera_identify_module(const struct smiapp_module_ident *ident_in)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rx71_main_camera_idents); i++) {
		if (rx71_main_camera_idents[i].manu_id
		    && rx71_main_camera_idents[i].manu_id
		    == ident_in->manu_id
		    && rx71_main_camera_idents[i].model_id
		    == ident_in->model_id)
			break;
		else if (rx71_main_camera_idents[i].sensor_manu_id
			 == ident_in->sensor_manu_id
			 && rx71_main_camera_idents[i].sensor_model_id
			 == ident_in->sensor_model_id)
				break;
	}

	if (i >= ARRAY_SIZE(rx71_main_camera_idents))
		return NULL;

	/*
	 * if manu_id and model_id are missing, copy over the corresponding
	 * sensor_manu_id and sensor_model_id to keep the smiapp driver going
	 */
	rx71_main_camera = rx71_main_camera_idents[i];
	if (!rx71_main_camera.manu_id) {
		rx71_main_camera.manu_id = rx71_main_camera.sensor_manu_id;
		rx71_main_camera.model_id = rx71_main_camera.sensor_model_id;
	}

	return &rx71_main_camera;
}

static struct smiapp_platform_data rx71_main_camera_platform_data = {
	.i2c_addr_dfl		= SMIAPP_DFL_I2C_ADDR,
	.i2c_addr_alt		= SMIAPP_ALT_I2C_ADDR,
	.nvm_size		= 16 * 64,
	.ext_clk		= (9.6 * 1000 * 1000),
	.module_board_orient	= SMIAPP_MODULE_BOARD_ORIENT_180,
	.csi_configure		= rx71_main_camera_csi2_configure,
	.set_xclk		= rx71_main_camera_set_xclk,
};

/**
 *
 * Main Camera Actuator Driver
 *
 */

static int rx71_lens_set_xclk(struct v4l2_subdev *sd, u32 hz)
{
	return rx71_update_xclk(sd, hz, RX71_PRI_LENS, MAIN_CAMERA_XCLK);
}

/* When no activity on EXTCLK, the AD5836 enters power-down mode */
static struct ad58xx_platform_data rx71_ad5836_platform_data = {
	.ext_clk		= (9.6 * 1000 * 1000),
	.set_xclk		= rx71_lens_set_xclk
};

static struct as3645a_flash_torch_parms rx71_main_camera_flash_setup = {
	.flash_min_current	= 200,
	.flash_max_current	= 320,
	.torch_min_current	= 20,
	.torch_max_current	= 60,
	.timeout_min		= 100000,
	.timeout_max		= 150000,
};

static struct as3645a_platform_data rx71_as3645a_platform_data = {
	.num_leds		= 2,
	.flash_torch_limits	= &rx71_main_camera_flash_setup,
};

/**
 *
 * SECONDARY CAMERA Sensor
 *
 */
static struct isp_csiphy_lanes_cfg rx71_sec_camera_csiphy_lanecfg = {
	.clk = {
		.pol = 0,
		.pos = 1,
	},
	.data[0] = {
		.pol = 0,
		.pos = 2,
	},
};

static void rx71_sec_camera_configure_interface(struct v4l2_subdev *subdev,
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
				       &rx71_sec_camera_csiphy_lanecfg);

	pixelclk = rx71_calc_pixelclk(mode);

	isp->platform_cb.set_pixel_clock(isp, pixelclk);
}

static int rx71_sec_camera_set_xclk(struct v4l2_subdev *sd, int hz)
{
	return rx71_update_xclk(sd, hz, RX71_SEC_SENSOR, SEC_CAMERA_XCLK);
}

static int rx71_sec_camera_set_xshutdown(struct v4l2_subdev *subdev, u8 set)
{
	gpio_set_value(SEC_CAMERA_RESET_GPIO, !!set);
	return 0;
}

static struct smiapp_platform_data rx71_sec_camera_platform_data = {
	.ext_clk		= (10.8 * 1000 * 1000),
	.csi_configure		= rx71_sec_camera_configure_interface,
	.set_xclk		= rx71_sec_camera_set_xclk,
	.set_xshutdown		= rx71_sec_camera_set_xshutdown,
};

/**
 *
 * Init all the modules
 *
 */

#define CAMERA_I2C_BUS_NUM		2
#define AD5836_I2C_BUS_NUM		2
#define AS3645A_I2C_BUS_NUM		2

static struct i2c_board_info rx71_s3_camera_i2c_devices[] = {
	{
		I2C_BOARD_INFO(SMIAPP_NAME, SMIAPP_ALT_I2C_ADDR),
		.platform_data = &rx71_main_camera_platform_data,
	},
	{
		I2C_BOARD_INFO(AD58XX_NAME, AD58XX_I2C_ADDR),
		.platform_data = &rx71_ad5836_platform_data,
	},
	{
		I2C_BOARD_INFO(AS3645A_NAME, AS3645A_I2C_ADDR),
		.platform_data = &rx71_as3645a_platform_data,
	},
	{
		I2C_BOARD_INFO(SMIAPP_NAME, SMIAPP_DFL_I2C_ADDR),
		.platform_data = &rx71_sec_camera_platform_data,
	},
};

static struct isp_subdev_i2c_board_info rx71_s3_camera_primary_subdevs[] = {
	{
		.board_info = &rx71_s3_camera_i2c_devices[0],
		.i2c_adapter_id = CAMERA_I2C_BUS_NUM,
	},
	{
		.board_info = &rx71_s3_camera_i2c_devices[1],
		.i2c_adapter_id = AD5836_I2C_BUS_NUM,
	},
	{
		.board_info = &rx71_s3_camera_i2c_devices[2],
		.i2c_adapter_id = AS3645A_I2C_BUS_NUM,
	},
	{ NULL, 0, },
};

static struct isp_subdev_i2c_board_info rx71_s3_camera_secondary_subdevs[] = {
	{
		.board_info = &rx71_s3_camera_i2c_devices[3],
		.i2c_adapter_id = CAMERA_I2C_BUS_NUM,
	},
	{ NULL, 0, },
};

static struct isp_v4l2_subdevs_group rx71_s3_camera_subdevs[] = {
	{
		.subdevs = rx71_s3_camera_primary_subdevs,
		.interface = ISP_INTERFACE_CSI2A_PHY2,
		.bus = { .csi2 = {
			.crc			= 1,
			.vpclk_div		= 1,
		} },
	},
	{
		.subdevs = rx71_s3_camera_secondary_subdevs,
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

static struct isp_platform_data rx71_isp_platform_data = {
	.subdevs = rx71_s3_camera_subdevs,
};

void __init rx71_camera_init(void)
{
	int rval;

	if (!board_is_known()) {
		printk(KERN_WARNING
		       "%s: unknown board --- not initialising camera\n",
		       __func__);
		return;
	}

	rval = rx71_camera_hw_init();
	if (rval) {
		printk(KERN_WARNING "%s: unable to initialise camera\n",
		       __func__);
		return;
	}

	if (board_is_devlon4_696()) {
		rx71_main_camera_platform_data.identify_module =
					rx71_main_camera_identify_module;
		rx71_main_camera_platform_data.module_board_orient =
					SMIAPP_MODULE_BOARD_ORIENT_0;
	}

	if (omap3_init_camera(&rx71_isp_platform_data) < 0)
		printk(KERN_WARNING
		       "%s: unable to register camera platform device\n",
		       __func__);
}
