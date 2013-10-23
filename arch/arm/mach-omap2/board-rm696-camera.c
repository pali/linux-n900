/**
 * arch/arm/mach-omap2/board-rm696-camera.c
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

#define SEC_CAMERA_RESET_GPIO	97

#define RM696_PRI_SENSOR	1
#define RM696_PRI_LENS		2
#define RM696_SEC_SENSOR	3
#define MAIN_CAMERA_XCLK	ISP_XCLK_A
#define SEC_CAMERA_XCLK		ISP_XCLK_B

/*
 *
 * HW initialization
 *
 *
 */
static int __init rm696_sec_camera_init(void)
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

static int __init rm696_camera_hw_init(void)
{
	return rm696_sec_camera_init();
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

static int rm696_update_xclk(struct v4l2_subdev *subdev, u32 hz, u32 which,
			     u8 xclksel)
{
	struct isp_device *isp = v4l2_dev_to_isp_device(subdev->v4l2_dev);
	int ret;

	mutex_lock(&lock_xclk);

	if (which == RM696_SEC_SENSOR) {
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

static struct isp_csiphy_lanes_cfg rm696_main_camera_csi2_lanecfg = {
	.clk = {
		.pol = 1,
		.pos = 2,
	},
	.data[0] = {
		.pol = 1,
		.pos = 1,
	},
	.data[1] = {
		.pol = 1,
		.pos = 3,
	},
};

static unsigned int rm696_calc_pixelclk(struct smia_mode *mode)
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

static void rm696_main_camera_csi2_configure(struct v4l2_subdev *subdev,
					     struct smia_mode *mode)
{
	struct isp_device *isp = v4l2_dev_to_isp_device(subdev->v4l2_dev);
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

	isp->platform_cb.csiphy_config(&isp->isp_csiphy2, &csi2phy,
				       &rm696_main_camera_csi2_lanecfg);

	pixelclk = rm696_calc_pixelclk(mode);

	isp->platform_cb.set_pixel_clock(isp, pixelclk);
}

static int rm696_main_camera_set_xclk(struct v4l2_subdev *sd, int hz)
{
	return rm696_update_xclk(sd, hz, RM696_PRI_SENSOR, MAIN_CAMERA_XCLK);
}

static struct smiapp_flash_strobe_parms rm696_main_camera_strobe_setup = {
	.mode			= 0x0c,
	.strobe_width_high_us	= 100000,
	.strobe_delay		= 0,
	.stobe_start_point	= 0,
	.trigger		= 0,
};

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
static const struct smiapp_module_ident rm696_main_camera_idents[] = {
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

static struct smiapp_module_ident rm696_main_camera;

static const struct smiapp_module_ident *
rm696_main_camera_identify_module(const struct smiapp_module_ident *ident_in)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rm696_main_camera_idents); i++) {
		if (rm696_main_camera_idents[i].manu_id
		    && rm696_main_camera_idents[i].manu_id
		    == ident_in->manu_id
		    && rm696_main_camera_idents[i].model_id
		    == ident_in->model_id)
			break;
		else if (rm696_main_camera_idents[i].sensor_manu_id
			 == ident_in->sensor_manu_id
			 && rm696_main_camera_idents[i].sensor_model_id
			 == ident_in->sensor_model_id)
				break;
	}

	if (i >= ARRAY_SIZE(rm696_main_camera_idents))
		return NULL;

	/*
	 * if manu_id and model_id are missing, copy over the corresponding
	 * sensor_manu_id and sensor_model_id to keep the smiapp driver going
	 */
	rm696_main_camera = rm696_main_camera_idents[i];
	if (!rm696_main_camera.manu_id) {
		rm696_main_camera.manu_id = rm696_main_camera.sensor_manu_id;
		rm696_main_camera.model_id = rm696_main_camera.sensor_model_id;
	}

	return &rm696_main_camera;
}

static int rm696_get_analog_gain_limits(const struct smiapp_module_ident *ident,
					u32 *min, u32 *max, u32 *step)
{
	if (ident->manu_id == 0x0c && ident->model_id == 0x218e) {
		/* Toshiba Gambino - tcm8596md */
		*min = 69;	/* Below 27 gain doesn't have effect at all, */
		*max = 1858;	/* but ~69 is needed for full dynamic range */
		*step = 1;
	} else if (ident->manu_id == 0x0c && ident->model_id == 0x560f) {
		/* Toshiba Lada - jt8ew9 */
		*min = 59;	/* Below 24 gain doesn't have effect at all, */
		*max = 6000;	/* but ~59 is needed for full dynamic range */
		*step = 1;
	} else
		return -ENOSYS;

	return 0;
}

static struct smiapp_platform_data rm696_main_camera_platform_data = {
	.i2c_addr_dfl		= SMIAPP_DFL_I2C_ADDR,
	.i2c_addr_alt		= SMIAPP_ALT_I2C_ADDR,
	.nvm_size		= 16 * 64,
	.ext_clk		= (9.6 * 1000 * 1000),
	.identify_module	= rm696_main_camera_identify_module,
	.get_analog_gain_limits	= rm696_get_analog_gain_limits,
	.strobe_setup		= &rm696_main_camera_strobe_setup,
	.csi_configure		= rm696_main_camera_csi2_configure,
	.set_xclk		= rm696_main_camera_set_xclk,
};

/*
 *
 * Main Camera Actuator Driver
 *
 */

static int rm696_lens_set_xclk(struct v4l2_subdev *sd, u32 hz)
{
	return rm696_update_xclk(sd, hz, RM696_PRI_LENS, MAIN_CAMERA_XCLK);
}

/* When no activity on EXTCLK, the AD5836 enters power-down mode */
static struct ad58xx_platform_data rm696_ad5836_platform_data = {
	.ext_clk		= (9.6 * 1000 * 1000),
	.set_xclk		= rm696_lens_set_xclk
};

/*
 * Main Camera Flash
 */

static void rm696_as3645a_setup_ext_strobe(int enable)
{
	if (enable)
		rm696_main_camera_platform_data.strobe_setup->trigger = 1;
	else
		rm696_main_camera_platform_data.strobe_setup->trigger = 0;
}

static void rm696_as3645a_set_strobe_width(u32 width_in_us)
{
	rm696_main_camera_platform_data.strobe_setup->strobe_width_high_us =
								width_in_us;
}

static struct as3645a_flash_torch_parms rm696_main_camera_flash_setup = {
	.flash_min_current	= 200,
	.flash_max_current	= 320,
	.torch_min_current	= 20,
	.torch_max_current	= 60,
	.timeout_min		= 100000,
	.timeout_max		= 150000,
};

static struct as3645a_platform_data rm696_as3645a_platform_data = {
	.num_leds		= 2,
	.use_ext_flash_strobe	= 1,
	.setup_ext_strobe	= rm696_as3645a_setup_ext_strobe,
	.set_strobe_width       = rm696_as3645a_set_strobe_width,
	.flash_torch_limits	= &rm696_main_camera_flash_setup,
};

/*
 *
 * SECONDARY CAMERA Sensor
 *
 */

#define SEC_CAMERA_XCLK		ISP_XCLK_B

static struct isp_csiphy_lanes_cfg rm696_sec_camera_csiphy_lanecfg = {
	.clk = {
		.pol = 0,
		.pos = 1,
	},
	.data[0] = {
		.pol = 0,
		.pos = 2,
	},
};

static void rm696_sec_camera_configure_interface(struct v4l2_subdev *subdev,
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
				       &rm696_sec_camera_csiphy_lanecfg);

	pixelclk = rm696_calc_pixelclk(mode);

	isp->platform_cb.set_pixel_clock(isp, pixelclk);
}

static int rm696_sec_camera_set_xclk(struct v4l2_subdev *sd, int hz)
{
	return rm696_update_xclk(sd, hz, RM696_SEC_SENSOR, SEC_CAMERA_XCLK);
}

static int rm696_sec_camera_set_xshutdown(struct v4l2_subdev *subdev, u8 set)
{
	gpio_set_value(SEC_CAMERA_RESET_GPIO, !!set);
	return 0;
}

static struct smiapp_platform_data rm696_sec_camera_platform_data = {
	.ext_clk		= (10.8 * 1000 * 1000),
	.module_board_orient	= SMIAPP_MODULE_BOARD_ORIENT_180,
	.csi_configure		= rm696_sec_camera_configure_interface,
	.set_xclk		= rm696_sec_camera_set_xclk,
	.set_xshutdown		= rm696_sec_camera_set_xshutdown,
};

/*
 *
 * Init all the modules
 *
 */

#define CAMERA_I2C_BUS_NUM		2
#define AD5836_I2C_BUS_NUM		2
#define AS3645A_I2C_BUS_NUM		2

static struct i2c_board_info rm696_camera_i2c_devices[] = {
	{
		I2C_BOARD_INFO(SMIAPP_NAME, SMIAPP_ALT_I2C_ADDR),
		.platform_data = &rm696_main_camera_platform_data,
	},
	{
		I2C_BOARD_INFO(AD58XX_NAME, AD58XX_I2C_ADDR),
		.platform_data = &rm696_ad5836_platform_data,
	},
	{
		I2C_BOARD_INFO(AS3645A_NAME, AS3645A_I2C_ADDR),
		.platform_data = &rm696_as3645a_platform_data,
	},
	{
		I2C_BOARD_INFO(SMIAPP_NAME, SMIAPP_DFL_I2C_ADDR),
		.platform_data = &rm696_sec_camera_platform_data,
	},
};

static struct isp_subdev_i2c_board_info rm696_camera_primary_subdevs[] = {
	{
		.board_info = &rm696_camera_i2c_devices[0],
		.i2c_adapter_id = CAMERA_I2C_BUS_NUM,
	},
	{
		.board_info = &rm696_camera_i2c_devices[1],
		.i2c_adapter_id = AD5836_I2C_BUS_NUM,
	},
	{
		.board_info = &rm696_camera_i2c_devices[2],
		.i2c_adapter_id = AS3645A_I2C_BUS_NUM,
	},
	{ NULL, 0, },
};

static struct isp_subdev_i2c_board_info rm696_camera_secondary_subdevs[] = {
	{
		.board_info = &rm696_camera_i2c_devices[3],
		.i2c_adapter_id = CAMERA_I2C_BUS_NUM,
	},
	{ NULL, 0, },
};

static struct isp_v4l2_subdevs_group rm696_camera_subdevs[] = {
	{
		.subdevs = rm696_camera_primary_subdevs,
		.interface = ISP_INTERFACE_CSI2A_PHY2,
		.bus = { .csi2 = {
			.crc			= 1,
			.vpclk_div		= 1,
		} },
	},
	{
		.subdevs = rm696_camera_secondary_subdevs,
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

static struct isp_platform_data rm696_isp_platform_data = {
	.subdevs = rm696_camera_subdevs,
};

void __init rm696_camera_init(void)
{
	int rval;

	rval = rm696_camera_hw_init();
	if (rval) {
		printk(KERN_WARNING "%s: unable to initialise camera\n",
		       __func__);
		return;
	}

	if (omap3_init_camera(&rm696_isp_platform_data) < 0)
		printk(KERN_WARNING
		       "%s: unable to register camera platform device\n",
		       __func__);
}
