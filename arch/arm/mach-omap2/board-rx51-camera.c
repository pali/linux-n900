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
#include <linux/i2c/twl4030.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/videodev2.h>
#include <linux/device.h>

#include <asm/gpio.h>
#include <mach/control.h>
#include <mach/omap-pm.h>

#include "../../../drivers/media/video/omap34xxcam.h"
#include "../../../drivers/media/video/isp/ispreg.h"
#include "../../../drivers/media/video/et8ek8.h"
#include "../../../drivers/media/video/smia-sensor.h"

#include <media/ad5820.h>
#include <media/adp1653.h>
#include <media/smiaregs.h>

#include "board-rx51-camera.h"

#define ADP1653_GPIO_ENABLE	88	/* Used for resetting ADP1653 */
#define ADP1653_GPIO_INT	167	/* Fault interrupt */
#define ADP1653_GPIO_STROBE	126	/* Pin used in cam_strobe mode ->
					 * control using ISP drivers */

#define STINGRAY_RESET_GPIO	102
#define ACMELITE_RESET_GPIO	97	/* Used also to MUX between cameras */

#define RX51_CAMERA_STINGRAY	0
#define RX51_CAMERA_ACMELITE	1

#define RX51_SENSOR		1
#define RX51_LENS		2

#define GPIO_DIR_OUTPUT		0

/*
 *
 * Power control
 *
 */

/* Assign camera to peripheral power group P3 */
#define CAMERA_DEV_GRP		(0x4 << 5)
#define VAUX2_1V8		0x05
#define VAUX3_1V8		0x01
#define VAUX4_2V8		0x09

/* Earlier rx51 builds require VAUX3. */
#define NEEDS_VAUX3		(system_rev >= 0x100 && system_rev < 0x900)

static struct rx51_camera {
	int okay;
	int inuse;
} rx51_camera[2];

static DEFINE_MUTEX(rx51_camera_mutex);

/* Acquires the given slave `which' for camera if possible.
 * Returns the bitmask containing previously acquired slaves for the device.
 */
static int rx51_camera_acquire(int camera, int which)
{
	int other = 1 - camera;
	int old_which;

	if (!rx51_camera[camera].okay)
		return -EINVAL;

	if (rx51_camera[other].inuse)
		return -EBUSY;

	old_which = rx51_camera[camera].inuse;
	rx51_camera[camera].inuse |= which;

	return old_which;
}

/* Releases the given slave `which' for camera.
 * Returns the bitmask containing still acquired slaves for the device.
 */
static int rx51_camera_release(int camera, int which)
{
	rx51_camera[camera].inuse &= ~which;

	return rx51_camera[camera].inuse;
}

static int rx51_camera_power_on_nolock(int camera)
{
	int rval;

	/* Reset Stingray */
	gpio_set_value(STINGRAY_RESET_GPIO, 0);

	/* Mux to Stingray and reset Acme Lite */
	gpio_set_value(ACMELITE_RESET_GPIO, 0);

	/* VAUX2=1.8 V (muxer voltage) */
	rval = twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
				    VAUX2_1V8, TWL4030_VAUX2_DEDICATED);
	if (rval)
		goto out;
	rval = twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
				    CAMERA_DEV_GRP, TWL4030_VAUX2_DEV_GRP);
	if (rval)
		goto out;

	/* Off & sleep -> Active state */
	rval = twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
				    0xEE, TWL4030_VAUX2_REMAP);
	if (rval)
		goto out;

	/* VAUX4=2.8 V (camera VANA) */
	rval = twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
				    VAUX4_2V8, TWL4030_VAUX4_DEDICATED);
	if (rval)
		goto out;
	rval = twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
				    CAMERA_DEV_GRP, TWL4030_VAUX4_DEV_GRP);
	if (rval)
		goto out;
	rval = twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
				    0xEE, TWL4030_VAUX4_REMAP);
	if (rval)
		goto out;

	if (NEEDS_VAUX3) {
		/* VAUX3=1.8 V (camera VDIG) */
		printk(KERN_INFO "%s: VAUX3 on for old board\n", __func__);
		rval = twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
					    VAUX3_1V8,
					    TWL4030_VAUX3_DEDICATED);
		if (rval)
			goto out;
		rval = twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
					    CAMERA_DEV_GRP,
					    TWL4030_VAUX3_DEV_GRP);
		if (rval)
			goto out;
		rval = twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
					    0xEE, TWL4030_VAUX3_REMAP);
		if (rval)
			goto out;
	}

	/* Let the voltages stabilize */
	udelay(15);

	/* XSHUTDOWN on, enable camera and set muxer */
	gpio_set_value(camera == RX51_CAMERA_STINGRAY ?
		       STINGRAY_RESET_GPIO : ACMELITE_RESET_GPIO, 1);

	/* CONTROL_CSIRXFE */
	omap_writel(
		/*
		 * CSIb receiver data/clock or data/strobe mode
		 *
		 * Stingray uses data/strobe.
		 */
		((camera ? 0 : 1) << 10)
		| BIT(12)       /* Enable differential transceiver */
		| BIT(13)       /* Disable reset */
		, OMAP343X_CTRL_BASE + OMAP343X_CONTROL_CSIRXFE);

	/* Let the voltages stabilize */
	udelay(15);

	return 0;

out:
	printk(KERN_ALERT "%s: Error %d in writing to TWL4030!\n", __func__,
	       rval);

	return rval;
}

static int rx51_camera_power_on(int camera, int which)
{
	int rval;

	mutex_lock(&rx51_camera_mutex);

	rval = rx51_camera_acquire(camera, which);

	if (!rval)
		rval = rx51_camera_power_on_nolock(camera);
	else if (rval > 0)
		rval = 0;

	mutex_unlock(&rx51_camera_mutex);

	if (rval < 0)
		printk(KERN_INFO "%s: power_on camera %d which %d failed\n",
		       __func__, camera, which);

	return rval;
}

static void rx51_camera_power_off_nolock(int camera)
{
	int rval;

	/* Reset cameras */
	gpio_set_value(STINGRAY_RESET_GPIO, 0);
	gpio_set_value(ACMELITE_RESET_GPIO, 0);

	/* VAUX2 (muxer voltage) off */
	rval = twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
				    0, TWL4030_VAUX2_DEV_GRP);
	if (rval)
		goto out;
	/* Off & sleep -> Off state */
	rval = twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
				    0x00, TWL4030_VAUX2_REMAP);
	if (rval)
		goto out;

	/* VAUX4 (camera VANA) off */
	rval = twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
				    0, TWL4030_VAUX4_DEV_GRP);
	if (rval)
		goto out;
	rval = twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
				    0x00, TWL4030_VAUX4_REMAP);
	if (rval)
		goto out;

	if (NEEDS_VAUX3) {
		printk(KERN_INFO "%s: VAUX3 off for old board\n", __func__);
		/* VAUX3 (camera VDIG) off */
		rval = twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
					    0, TWL4030_VAUX3_DEV_GRP);
		if (rval)
			goto out;
		rval = twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
					    0x00, TWL4030_VAUX3_REMAP);
		if (rval)
			goto out;
	}

	return;

out:
	printk(KERN_ALERT "%s: Error %d in writing to TWL4030!\n", __func__,
	       rval);
}

static void rx51_camera_power_off(int camera, int which)
{
	int rval;

	mutex_lock(&rx51_camera_mutex);

	rval = rx51_camera_release(camera, which);
	if (!rval)
		rx51_camera_power_off_nolock(camera);

	mutex_unlock(&rx51_camera_mutex);
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
	rx51_camera_power_off_nolock(RX51_CAMERA_STINGRAY);
	rx51_camera[RX51_CAMERA_STINGRAY].okay = 1;
	rx51_camera[RX51_CAMERA_STINGRAY].inuse = 0;
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
	rx51_camera_power_off_nolock(RX51_CAMERA_ACMELITE);
	rx51_camera[RX51_CAMERA_ACMELITE].okay = 1;
	rx51_camera[RX51_CAMERA_ACMELITE].inuse = 0;
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

	rval = rx51_adp1653_init();
	if (rval)
		return rval;

	mutex_init(&rx51_camera_mutex);
	rx51_stingray_init();
	rx51_acmelite_init();

	return 0;
}

/*
 *
 * Stingray
 *
 */

#define STINGRAY_XCLK		OMAP34XXCAM_XCLK_A

static struct isp_interface_config rx51_stingray_config = {
	.ccdc_par_ser = ISP_CSIB,
	.dataline_shift = 0,
	.hsvs_syncdetect = ISPCTRL_SYNC_DETECT_VSRISE,
	.strobe = 0,
	.prestrobe = 0,
	.shutter = 0,
	.wait_hs_vs = 1,
	.u = {
		.csi = {
			 .crc		=	1,
			 .mode		=	0,
			 .edge		=	1,
			 .signalling	=	1,
			 .strobe_clock_inv =	0,
			 .vs_edge	=	0,
			 .channel	=	0,
			 .vpclk		=	1,
			 .data_start	=	0,
		 },
	},
};

static struct omap34xxcam_hw_config rx51_stingray_omap34xxcam_hw_config = {
	.dev_index	= 0,
	.dev_minor	= 0,
	.dev_type	= OMAP34XXCAM_SLAVE_SENSOR,
	.u		= {
		.sensor		= {
			 .sensor_isp	= 0,
			 .capture_mem	= PAGE_ALIGN(2608 * 1966 * 2) * 2,
			 .ival_default	= { 1, 30 },
		 },
	},
};

static int rx51_stingray_configure_interface(struct v4l2_int_device *s,
					     struct smia_mode *mode)
{
	struct omap34xxcam_videodev *vdev = s->u.slave->master->priv;
	static const int S = 8;

	/* Configure sensor interface. */
	rx51_stingray_config.u.csi.format = mode->pixel_format;
	rx51_stingray_config.u.csi.data_size = mode->window_height;
	/* Calculate average pixel clock per line. Assume buffers can spread
	 * the data over horizontal blanking time. Rounding upwards. */
	rx51_stingray_config.pixelclk =
		mode->window_width
		* (((mode->pixel_clock + (1<<S) - 1) >> S) + mode->width - 1)
		/ mode->width;
	rx51_stingray_config.pixelclk <<= S;
	return isp_configure_interface(vdev->cam->isp, &rx51_stingray_config);
}

static int rx51_stingray_set_xclk(struct v4l2_int_device *s, int hz)
{
	struct omap34xxcam_videodev *vdev = s->u.slave->master->priv;

	isp_set_xclk(vdev->cam->isp, hz, STINGRAY_XCLK);

	return 0;
}

static int rx51_stingray_power_on(struct v4l2_int_device *s)
{
	struct omap34xxcam_videodev *vdev = s->u.slave->master->priv;
	struct device *dev = vdev->cam->isp;
	int rval;

	omap_pm_set_min_bus_tput(dev, OCP_INITIATOR_AGENT, 400000);

	rval = rx51_camera_power_on(RX51_CAMERA_STINGRAY, RX51_SENSOR);

	if (rval)
		omap_pm_set_min_bus_tput(dev, 2, 0);

	return rval;
}

static int rx51_stingray_power_off(struct v4l2_int_device *s)
{
	struct omap34xxcam_videodev *vdev = s->u.slave->master->priv;
	struct device *dev = vdev->cam->isp;

	rx51_camera_power_off(RX51_CAMERA_STINGRAY, RX51_SENSOR);

	omap_pm_set_min_bus_tput(dev, OCP_INITIATOR_AGENT, 0);

	return 0;
}

static int rx51_stingray_g_priv(struct v4l2_int_device *s, void *priv)
{
	*(struct omap34xxcam_hw_config *)priv =
		rx51_stingray_omap34xxcam_hw_config;

	return 0;
}

static struct et8ek8_platform_data et8ek8_tmp_platform_data;

static struct et8ek8_platform_data et8ek8_my_platform_data = {
	.g_priv			= rx51_stingray_g_priv,
	.configure_interface	= rx51_stingray_configure_interface,
	.set_xclk		= rx51_stingray_set_xclk,
	.power_on		= rx51_stingray_power_on,
	.power_off		= rx51_stingray_power_off,
};

/*
 *
 * AD5820
 *
 */

static struct omap34xxcam_hw_config ad5820_omap34xxcam_hw_config = {
	.dev_index	= 0,
	.dev_minor	= 0,
	.dev_type	= OMAP34XXCAM_SLAVE_LENS,
	.u		= {
		.lens		= {
		 },
	},
};

static int ad5820_g_priv(struct v4l2_int_device *s, void *priv)
{
	*(struct omap34xxcam_hw_config *)priv = ad5820_omap34xxcam_hw_config;

	return 0;
}

static int ad5820_s_power(struct v4l2_int_device *s, enum v4l2_power state)
{
	if (state == V4L2_POWER_ON)
		return rx51_camera_power_on(RX51_CAMERA_STINGRAY, RX51_LENS);
	else
		rx51_camera_power_off(RX51_CAMERA_STINGRAY, RX51_LENS);

	return 0;
}

static struct ad5820_platform_data ad5820_tmp_platform_data;

static struct ad5820_platform_data ad5820_my_platform_data = {
	.g_priv		= ad5820_g_priv,
	.s_power	= ad5820_s_power,

};

/*
 *
 * ADP1653
 *
 */

static struct omap34xxcam_hw_config adp1653_omap34xxcam_hw_config = {
	.dev_index	= 0,
	.dev_minor	= 0,
	.dev_type	= OMAP34XXCAM_SLAVE_FLASH,
	.u		= {
		.flash		= {
		 },
	},
};

static int rx51_adp1653_g_priv(struct v4l2_int_device *s, void *priv)
{
	*(struct omap34xxcam_hw_config *)priv = adp1653_omap34xxcam_hw_config;

	return 0;
}

static int rx51_adp1653_power_on(struct v4l2_int_device *s)
{
	gpio_set_value(ADP1653_GPIO_ENABLE, 1);

	/* Some delay is apparently required. */
	udelay(400);

	return 0;
}

static int rx51_adp1653_power_off(struct v4l2_int_device *s)
{
	gpio_set_value(ADP1653_GPIO_ENABLE, 0);

	return 0;
}

static struct adp1653_platform_data adp1653_tmp_platform_data;

static struct adp1653_platform_data adp1653_my_platform_data = {
	.g_priv			 = rx51_adp1653_g_priv,
	.power_on		 = rx51_adp1653_power_on,
	.power_off		 = rx51_adp1653_power_off,
	/* Must be limited to 500 ms in RX-51 */
	.max_flash_timeout	 = 500000,		/* us */
	/* Must be limited to 320 mA in RX-51 B3 and newer hardware */
	.max_flash_intensity	 = 19,
	/* Must be limited to 50 mA in RX-51 */
	.max_torch_intensity	 = 1,
	.max_indicator_intensity = ADP1653_REG_OUT_SEL_ILED_MAX,
};

/*
 *
 * Acmelite
 *
 */

#define ACMELITE_XCLK		OMAP34XXCAM_XCLK_A

static struct isp_interface_config rx51_acmelite_config = {
	.ccdc_par_ser = ISP_CSIB,
	.dataline_shift = 0,
	.hsvs_syncdetect = ISPCTRL_SYNC_DETECT_VSRISE,
	.strobe = 0,
	.prestrobe = 0,
	.shutter = 0,
	.wait_hs_vs = 1,
	.u = {
		.csi = {
			 .crc		=	0,
			 .mode		=	0,
			 .edge		=	1,
			 .signalling	=	0,
			 .strobe_clock_inv =	0,
			 .vs_edge	=	0,
			 .channel	=	0,
			 .vpclk		=	2,
			 .data_start	=	4,
			 .format	=	V4L2_PIX_FMT_SGRBG10,
		 },
	},
};

static struct omap34xxcam_hw_config rx51_acmelite_omap34xxcam_hw_config = {
	.dev_index	= 1,
	.dev_minor	= 1,
	.dev_type	= OMAP34XXCAM_SLAVE_SENSOR,
	.u		= {
		.sensor		= {
			 .sensor_isp	= 0,
			 .capture_mem	= PAGE_ALIGN(648 * 488 * 2) * 4,
			 .ival_default	= { 1, 30 },
		 },
	},
};

static int rx51_acmelite_configure_interface(struct v4l2_int_device *s,
					     int width, int height)
{
	struct omap34xxcam_videodev *vdev = s->u.slave->master->priv;

	/* Configure sensor interface. */
	rx51_acmelite_config.u.csi.data_size = height;

	return isp_configure_interface(vdev->cam->isp, &rx51_acmelite_config);
}

static int rx51_acmelite_set_xclk(struct v4l2_int_device *s, int hz)
{
	struct omap34xxcam_videodev *vdev = s->u.slave->master->priv;

	isp_set_xclk(vdev->cam->isp, hz, ACMELITE_XCLK);

	return 0;
}

static int rx51_acmelite_power_on(struct v4l2_int_device *s)
{
	struct omap34xxcam_videodev *vdev = s->u.slave->master->priv;
	struct device *dev = vdev->cam->isp;
	int rval;

	omap_pm_set_min_bus_tput(dev, OCP_INITIATOR_AGENT, 400000);

	rval = rx51_camera_power_on(RX51_CAMERA_ACMELITE, RX51_SENSOR);

	if (rval)
		omap_pm_set_min_bus_tput(dev, 2, 0);

	return rval;
}

static int rx51_acmelite_power_off(struct v4l2_int_device *s)
{
	struct omap34xxcam_videodev *vdev = s->u.slave->master->priv;
	struct device *dev = vdev->cam->isp;

	rx51_camera_power_off(RX51_CAMERA_ACMELITE, RX51_SENSOR);

	omap_pm_set_min_bus_tput(dev, OCP_INITIATOR_AGENT, 0);

	return 0;
}

static int rx51_acmelite_g_priv(struct v4l2_int_device *s, void *priv)
{
	*(struct omap34xxcam_hw_config *)priv =
		rx51_acmelite_omap34xxcam_hw_config;

	return 0;
}

static struct smia_sensor_platform_data smia_sensor_tmp_platform_data;

static struct smia_sensor_platform_data smia_sensor_my_platform_data = {
	.g_priv			= rx51_acmelite_g_priv,
	.configure_interface	= rx51_acmelite_configure_interface,
	.set_xclk		= rx51_acmelite_set_xclk,
	.power_on		= rx51_acmelite_power_on,
	.power_off		= rx51_acmelite_power_off,
};

/*
 *
 * Init it all
 *
 */

static int __init rx51_camera_init(void) {
	int err;

	err = rx51_camera_hw_init();
	if (err)
		return err;

	/* ADP1653 */
	adp1653_tmp_platform_data = rx51_adp1653_platform_data;
	rx51_adp1653_platform_data = adp1653_my_platform_data;

	/* Stingray */
	et8ek8_tmp_platform_data = rx51_et8ek8_platform_data;
	rx51_et8ek8_platform_data = et8ek8_my_platform_data;

	/* AD5820 */
	ad5820_tmp_platform_data = rx51_ad5820_platform_data;
	rx51_ad5820_platform_data = ad5820_my_platform_data;

	/* Acmelite */
	smia_sensor_tmp_platform_data = rx51_smia_sensor_platform_data;
	rx51_smia_sensor_platform_data = smia_sensor_my_platform_data;

	/* FIXME: can't unload yet. */
	__module_get(THIS_MODULE);

	/* FIXME: somehow initiate device init. */
	v4l2_int_device_try_attach_all();

	return 0;
}

static void __exit rx51_camera_exit(void) {
	rx51_et8ek8_platform_data = et8ek8_tmp_platform_data;
	rx51_ad5820_platform_data = ad5820_tmp_platform_data;
	rx51_adp1653_platform_data = adp1653_tmp_platform_data;
	rx51_smia_sensor_platform_data = smia_sensor_tmp_platform_data;

	gpio_free(ADP1653_GPIO_ENABLE);
	gpio_free(ADP1653_GPIO_INT);
	gpio_free(ADP1653_GPIO_STROBE);
}

module_init(rx51_camera_init);
module_exit(rx51_camera_exit);

MODULE_LICENSE("GPL");
