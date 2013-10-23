/*
 * linux/arch/arm/mach-omap2/board-rx51-video.c
 *
 * Copyright (C) 2008 Nokia
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/spi/spi.h>
#include <linux/i2c/twl4030.h>

#include <asm/mach-types.h>

#include <mach/mcspi.h>
#include <mach/gpio.h>
#include <mach/board.h>
#include <mach/pm.h>

#if defined(CONFIG_FB_OMAP) || defined(CONFIG_FB_OMAP_MODULE)
#include <mach/lcd_mipid.h>
#else
#include <linux/omapfb.h>
#include <mach/display.h>
#include <../drivers/video/omap2/displays/panel-acx565akm.h>
#endif

static struct omap2_mcspi_device_config mipid_mcspi_config = {
	.turbo_mode	= 0,
	.single_channel	= 1,
};

static int twl4030_bklight_level = -1;		/* load from HW at first read */

#define TWL4030_PWM0_ENABLE     (1 << 2)
#define TWL4030_PWM0_CLK_ENABLE (1 << 0)

static const int twl4030_bklight_max = 127;
static int twl4030_bklight_initialized;

static int twl4030_get_bklight_level(void)
{
	if (twl4030_bklight_level == -1) {
		u8 reg;

		twl4030_i2c_read_u8(TWL4030_MODULE_INTBR, &reg, 0x0c);
		if (reg & TWL4030_PWM0_ENABLE) {
			twl4030_i2c_read_u8(TWL4030_MODULE_PWM0, &reg, 0x01);
			twl4030_bklight_level = reg;
		} else {
			twl4030_bklight_level = 0;
		}
	}

	return twl4030_bklight_level;
}

static void twl4030_set_bklight_level(int level)
{
	u8 reg;

	if (!twl4030_bklight_initialized) {
		/* Mux GPIO6 as PWM0 : PMBR1 = xxxx01xx */
		twl4030_i2c_read_u8(TWL4030_MODULE_INTBR, &reg, 0x0d);
		reg &= ~(3 << 2);
		reg |= (1 << 2);
		twl4030_i2c_write_u8(TWL4030_MODULE_INTBR, reg, 0x0d);

		twl4030_bklight_initialized = 1;
	}

	twl4030_i2c_read_u8(TWL4030_MODULE_INTBR, &reg, 0x0c);

	if (level != 0) {
		/* Configure the duty cycle. */
		twl4030_i2c_write_u8(TWL4030_MODULE_PWM0, 0, 0x00);
		twl4030_i2c_write_u8(TWL4030_MODULE_PWM0, level, 0x01);

		/* Enable clock for PWM0 a few microseconds before PWM0 itself.
		   This is not mentioned in TWL4030 spec. but some older boards
		   don't set backlight level properly from time to time
		   without this delay. */
		reg |= TWL4030_PWM0_CLK_ENABLE;
		twl4030_i2c_write_u8(TWL4030_MODULE_INTBR, reg, 0x0c);
		udelay(50);
		reg |= TWL4030_PWM0_ENABLE;
	} else {
		/* Disable PWM0 before disabling its clock, see comment above */
		reg &= ~TWL4030_PWM0_ENABLE;
		twl4030_i2c_write_u8(TWL4030_MODULE_INTBR, reg, 0x0c);
		udelay(50);
		reg &= ~TWL4030_PWM0_CLK_ENABLE;
	}

	twl4030_i2c_write_u8(TWL4030_MODULE_INTBR, reg, 0x0c);

	twl4030_bklight_level = level;
}

#if defined(CONFIG_FB_OMAP) || defined(CONFIG_FB_OMAP_MODULE)


static struct platform_device rx51_lcd_device = {
	.name		= "lcd_mipid",
	.id		= -1,
};

static void mipid_shutdown(struct mipid_platform_data *pdata)
{
	if (pdata->nreset_gpio != -1) {
		pr_info("shutdown LCD\n");
		gpio_set_value(pdata->nreset_gpio, 0);
		msleep(120);
	}
}

static void mipid_set_bklight_level(struct mipid_platform_data *md, int level)
{
	twl4030_set_bklight_level(level);
}

static int mipid_get_bklight_level(struct mipid_platform_data *md)
{
	return twl4030_get_bklight_level();
}

static int mipid_get_bklight_max(struct mipid_platform_data *md)
{
	return twl4030_bklight_max;
}

static struct mipid_platform_data rx51_mipid_platform_data = {
	.bc_connected		= 1,
	.shutdown		= mipid_shutdown,
	.set_bklight_level	= mipid_set_bklight_level,
	.get_bklight_level	= mipid_get_bklight_level,
	.get_bklight_max	= mipid_get_bklight_max,
};

static void __init mipid_dev_init(void)
{
	const struct omap_lcd_config *conf;

	conf = omap_get_config(OMAP_TAG_LCD, struct omap_lcd_config);
	if (conf != NULL) {
		int ret = gpio_request(conf->nreset_gpio, "mipid-reset");
		if (ret) {
			printk(KERN_ERR "Failed to request GPIO %d for "
				"mipid reset\n", conf->nreset_gpio);
		} else {
			gpio_direction_output(conf->nreset_gpio, 1);
			rx51_mipid_platform_data.nreset_gpio =
				conf->nreset_gpio;
		}

		rx51_mipid_platform_data.data_lines = conf->data_lines;
	}
}

static struct spi_board_info rx51_video_spi_board_info[] = {
	[0] = {
		.modalias		= "lcd_mipid",
		.bus_num		= 1,
		.chip_select		= 2,
		.max_speed_hz		= 6000000,
		.controller_data	= &mipid_mcspi_config,
		.platform_data		= &rx51_mipid_platform_data,
	},
};

static struct platform_device *rx51_video_devices[] = {
	&rx51_lcd_device,
};

static int __init rx51_video_init(void)
{
	if (!(machine_is_nokia_rx51() || machine_is_nokia_rx71()))
		return 0;

	platform_add_devices(rx51_video_devices, ARRAY_SIZE(rx51_video_devices));
	spi_register_board_info(rx51_video_spi_board_info,
			ARRAY_SIZE(rx51_video_spi_board_info));
	mipid_dev_init();
	return 0;
}

#else	/* CONFIG_FB_OMAP || CONFIG_FB_OMAP_MODULE */

static struct spi_board_info rx51_video_spi_board_info[] = {
	[0] = {
		.modalias		= "acx565akm",
		.bus_num		= 1,
		.chip_select		= 2,
		.max_speed_hz		= 6000000,
		.controller_data	= &mipid_mcspi_config,
	},
};

/* acx565akm LCD Panel */
static int acx565akm_enable(struct omap_display *display)
{
	if (display->hw_config.panel_reset_gpio != -1) {
		pr_info("Release LCD reset\n");
		gpio_set_value(display->hw_config.panel_reset_gpio, 1);
		msleep(15);
	}

	return 0;
}

static void acx565akm_disable(struct omap_display *display)
{
	if (display->hw_config.panel_reset_gpio != -1) {
		pr_info("Enable LCD reset\n");
		gpio_set_value(display->hw_config.panel_reset_gpio, 0);
		msleep(120);
	}
}

static int rx51_set_backlight_level(struct omap_display *display, int level)
{
	twl4030_set_bklight_level(level);

	return 0;
}

static int rx51_get_backlight_level(struct omap_display *display)
{
	return twl4030_get_bklight_level();
}

static struct acx565akm_panel_data acx565akm_data = {
	.bc_connected = 1,
};

static struct omap_dss_display_config acx565akm_display_data = {
	.type = OMAP_DISPLAY_TYPE_SDI,
	.name = "lcd",
	.panel_name = "panel-acx565akm",
	.panel_enable = acx565akm_enable,
	.panel_disable = acx565akm_disable,
	.panel_reset_gpio = -1, /* set later from tag data */
	.max_backlight_level = 127,
	.set_backlight = rx51_set_backlight_level,
	.get_backlight = rx51_get_backlight_level,
	.panel_data = &acx565akm_data,
	.u.sdi = {
		.datapairs = 2,
	},
};

static void __init acx565akm_dev_init(void)
{
	const struct omap_lcd_config *conf;

	conf = omap_get_config(OMAP_TAG_LCD, struct omap_lcd_config);
	if (conf != NULL) {
		int ret = gpio_request(conf->nreset_gpio, "acx565akm-reset");
		if (ret) {
			printk(KERN_ERR "Failed to request GPIO %d for "
				"acx565akm reset\n", conf->nreset_gpio);
		} else {
			gpio_direction_output(conf->nreset_gpio, 1);
			acx565akm_display_data.panel_reset_gpio =
				conf->nreset_gpio;
		}
	}
}

/* TV-out */

static struct omap_dss_display_config venc_display_data = {
	.type = OMAP_DISPLAY_TYPE_VENC,
	.name = "tv",
	.u.venc.type = OMAP_DSS_VENC_TYPE_COMPOSITE,
};

/* DSS */
static struct omap_dss_board_info rx51_dss_data = {
	.get_last_off_on_transaction_id = get_last_off_on_transaction_id,
	.num_displays = 2,
	.displays = {
		&acx565akm_display_data,
		&venc_display_data,
	}
};

static struct platform_device rx51_dss_device = {
	.name          = "omapdss",
	.id            = -1,
	.dev            = {
		.platform_data = &rx51_dss_data,
	},
};

static struct platform_device *rx51_video_devices[] = {
	&rx51_dss_device,
};

static int __init rx51_video_init(void)
{
	platform_add_devices(rx51_video_devices, ARRAY_SIZE(rx51_video_devices));
	spi_register_board_info(rx51_video_spi_board_info,
			ARRAY_SIZE(rx51_video_spi_board_info));
	acx565akm_dev_init();
	return 0;
}

#endif	/* CONFIG_FB_OMAP || CONFIG_FB_OMAP_MODULE */

subsys_initcall(rx51_video_init);

