/*
 * linux/arch/arm/mach-omap2/board-rx71-video.c
 *
 * Copyright (C) 2008 Nokia
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/platform_device.h>
#include <linux/omapfb.h>
#include <linux/regulator/consumer.h>

#include <asm/mach-types.h>

#include <mach/gpio.h>
#include <plat/board.h>
#include <plat/common.h>
#include <plat/display.h>
#include <plat/panel-nokia-dsi.h>
#include <plat/vram.h>
#include <plat/ctrl-gf.h>

#include "dss.h"

static bool rx71_board_has_gf(void)
{
	return
		/* S3.1 */
		system_rev == 0x3100 || system_rev == 0x3110 ||
		/* Macro 3.0 */
		system_rev == 0x3005 || system_rev == 0x3015 ||
		system_rev == 0x3025 || system_rev == 0x3035 ||
		system_rev == 0x3065;
}

static bool rx71_board_has_himalaya(void)
{
	return
		/* pre S3.0 */
		system_rev == 0x3010 || system_rev == 0x3020 ||
		system_rev == 0x3030 ||
		/* S3.0 */
		system_rev == 0x3040 || system_rev == 0x3050 ||
		/* M3.0 */
		system_rev == 0x3006;
}

bool rx71_board_has_himalaya_v2(void)
{
	return
		/* M3.1 */
		system_rev == 0x3102 || system_rev == 0x3104 ||
		system_rev == 0x3106 || system_rev == 0x3108 ||
		system_rev == 0x3112 || system_rev == 0x3114 ||
		system_rev == 0x3116 ||
		/* M3.2 */
		system_rev == 0x3202 || system_rev == 0x3204 ||
		system_rev == 0x3206 || system_rev == 0x3208 ||
		system_rev == 0x3210 || system_rev == 0x3212 ||
		system_rev == 0x3214 || system_rev == 0x3216 ||
		/* S4.0 */
		system_rev == 0x3225 ||
		/* M3.3 */
		system_rev == 0x3310 || system_rev == 0x3312 ||
		system_rev == 0x3314 || system_rev == 0x3316 ||
		system_rev == 0x3318 || system_rev == 0x3320 ||
		system_rev == 0x3322 ||
		/* M4.0 */
		system_rev == 0x3405 || system_rev == 0x3407 ||
		/* M4.1 */
		system_rev == 0x3500;
}

static bool rx71_board_has_broken_himalaya_v2(void)
{
	/* They forgot to connect TE line on M3.2 boards */
	return	system_rev == 0x3202 || system_rev == 0x3204 ||
		system_rev == 0x3206 || system_rev == 0x3208 ||
		system_rev == 0x3210 || system_rev == 0x3212 ||
		system_rev == 0x3214 || system_rev == 0x3216;
}

bool rx71_board_has_pyrenees(void)
{
	return
		/* S4.0 */
		system_rev == 0x4000 ||
		/* M3.3 */
		system_rev == 0x4010 || system_rev == 0x4012 ||
		/* S4.1 */
		system_rev == 0x4005 ||
		/* M4.1 */
		system_rev == 0x4100 || system_rev == 0x4110 ||
		system_rev == 0x4120 || system_rev == 0x4130 ||
		system_rev == 0x4140;
}

static struct nokia_dsi_panel_data rx71_himalaya_data = {
	.name = "himalaya",
	.reset_gpio = 163,
	.use_ext_te = true,
	.ext_te_gpio = 62,
	.esd_timeout = 5000,
	.partial_area = {
		.offset = 5,
		.height = 854,
	},
	.rotate = 3,
};

static struct omap_dss_device rx71_himalaya_device = {
	.type = OMAP_DISPLAY_TYPE_DSI,
	.name = "lcd",
	.driver_name = "panel-nokia-dsi",
	.phy.dsi = {
		.clk_lane = 1,
		.clk_pol = 0,
		.data1_lane = 2,
		.data1_pol = 0,
		.data2_lane = 3,
		.data2_pol = 0,
	},

	.clocks = {
		.dss = {
			.fck_div = 5,
		},

		.dispc = {
			.lck_div = 1,
			.pck_div = 4,

			.fclk_from_dsi_pll = false,
		},

		.dsi = {
			/* DDR CLK 210.24 MHz */
			.regn = 10,
			.regm = 219,
			/* DISPC FCLK 140.16 MHz */
			.regm3 = 6,
			/* DSI FCLK 140.16 MHz */
			.regm4 = 6,

			/* LP CLK 8.760 MHz */
			.lp_clk_div = 8,

			.fclk_from_dsi_pll = false,
		},
	},

	.data = &rx71_himalaya_data,
};

static struct ctrl_gf_platform_data rx71_gf_data = {
	.reset_gpio = 170,
	.pdx_gpio = 69,
	.sysclk_name = "sys_clkout1",
	.panel_gpio = 163,
	.te_gpio = 62,
};

static struct omap_dss_device rx71_gf_device = {
	.type = OMAP_DISPLAY_TYPE_DSI,
	.name = "lcd",
	.driver_name = "gf",
	.reset_gpio = -1,
	.phy.dsi = {
		.clk_lane = 2,
		.clk_pol = 0,
		.data1_lane = 3,
		.data1_pol = 0,
		.data2_lane = 1,
		.data2_pol = 0,

		.ext_te = false,
	},

	.clocks = {
		.dss = {
			.fck_div = 5,
		},

		.dispc = {
			/* LCK 170.88 MHz */
			.lck_div = 1,
			/* PCK 42.72 MHz */
			.pck_div = 4,

			.fclk_from_dsi_pll = false,
		},

		.dsi = {
			/* DDR CLK 256.32 MHz */
			.regn = 10,
			.regm = 267,
			/* DISPC FCLK 170.88 MHz */
			.regm3 = 6,
			/* DSI FCLK 170.88 MHz */
			.regm4 = 6,

			/* LP CLK 7.767 MHz */
			.lp_clk_div = 11,

			.fclk_from_dsi_pll = false,
		},
	},

	.data = &rx71_gf_data,
};

static struct nokia_dsi_panel_data rx71_himalaya_v2_data = {
	.name = "himalaya",
	.reset_gpio = 87,
	.use_ext_te = true,
	.ext_te_gpio = 62,
	.esd_timeout = 5000,
	.ulps_timeout = 500,
	.partial_area = {
		.offset = 5,
		.height = 854,
	},
	.rotate = 3,
};

static struct omap_dss_device rx71_himalaya_v2_device = {
	.type = OMAP_DISPLAY_TYPE_DSI,
	.name = "lcd",
	.driver_name = "panel-nokia-dsi",
	.phy.dsi = {
		.clk_lane = 2,
		.clk_pol = 0,
		.data1_lane = 3,
		.data1_pol = 0,
		.data2_lane = 1,
		.data2_pol = 0,
	},

	.clocks = {
		.dss = {
			.fck_div = 5,
		},

		.dispc = {
			/* LCK 170.88 MHz */
			.lck_div = 1,
			/* PCK 42.72 MHz */
			.pck_div = 4,

			.fclk_from_dsi_pll = false,
		},

		.dsi = {
			/* DDR CLK 256.32 MHz */
			.regn = 10,
			.regm = 267,
			/* DISPC FCLK 170.88 MHz */
			.regm3 = 6,
			/* DSI FCLK 170.88 MHz */
			.regm4 = 6,

			/* LP CLK 7.767 MHz */
			.lp_clk_div = 11,

			.fclk_from_dsi_pll = false,
		},
	},

	.data = &rx71_himalaya_v2_data,
};

static struct nokia_dsi_panel_data rx71_pyrenees_data = {
	.name = "pyrenees",
	.reset_gpio = 87,
	.use_ext_te = true,
	.ext_te_gpio = 62,
	.esd_timeout = 5000,
	.ulps_timeout = 500,
	.partial_area = {
		.offset = 0,
		.height = 854,
	},
	.rotate = 1,
};

static struct omap_dss_device rx71_pyrenees_device = {
	.type = OMAP_DISPLAY_TYPE_DSI,
	.name = "lcd",
	.driver_name = "panel-nokia-dsi",
	.phy.dsi = {
		.clk_lane = 2,
		.clk_pol = 0,
		.data1_lane = 3,
		.data1_pol = 0,
		.data2_lane = 1,
		.data2_pol = 0,
	},

	.clocks = {
		.dss = {
			.fck_div = 5,
		},

		.dispc = {
			/* LCK 170.88 MHz */
			.lck_div = 1,
			/* PCK 42.72 MHz */
			.pck_div = 4,

			.fclk_from_dsi_pll = false,
		},

		.dsi = {
			/* DDR CLK 210.24 MHz */
			.regn = 10,
			.regm = 219,
			/* DISPC FCLK 170.88 MHz */
			.regm3 = 6,
			/* DSI FCLK 170.88 MHz */
			.regm4 = 6,

			/* LP CLK 8.760 MHz */
			.lp_clk_div = 8,

			.fclk_from_dsi_pll = false,
		},
	},

	.data = &rx71_pyrenees_data,
};


static int rx71_tv_enable(struct omap_dss_device *dssdev)
{
	if (dssdev->reset_gpio != -1)
		gpio_set_value(dssdev->reset_gpio, 1);
	return 0;
}

static void rx71_tv_disable(struct omap_dss_device *dssdev)
{
	if (dssdev->reset_gpio != -1)
		gpio_set_value(dssdev->reset_gpio, 0);
}

static struct omap_dss_device rx71_tv_display_data = {
	.type = OMAP_DISPLAY_TYPE_VENC,
	.name = "tv",
	.driver_name = "venc",
	/* was 40, handled by twl5031-aci */
	.reset_gpio = -1,
	.phy.venc = {
		.type = OMAP_DSS_VENC_TYPE_COMPOSITE,
		.invert_polarity = false,
	},
	.platform_enable = rx71_tv_enable,
	.platform_disable = rx71_tv_disable,
};

static struct omap_dss_device *rx71_dss_devices[] = {
	NULL, /* filled later */
	&rx71_tv_display_data,
};

static struct omap_dss_board_info rx71_dss_data = {
	.num_devices = ARRAY_SIZE(rx71_dss_devices),
	.devices = rx71_dss_devices,
	.default_device = NULL, /* filled later */
};

struct platform_device rx71_dss_device = {
	.name          = "omapdss",
	.id            = -1,
	.dev            = {
		.platform_data = &rx71_dss_data,
	},
};


static int __init rx71_display_init(void)
{
	int r;

	if (!rx71_board_has_gf()) {
		struct nokia_dsi_panel_data *panel_data;

		panel_data = rx71_dss_devices[0]->data;

		r = gpio_request(panel_data->reset_gpio, "Panel reset");
		if (r < 0) {
			panel_data->reset_gpio = -1;
			printk(KERN_ERR "Unable to get panel reset GPIO\n");
			return r;
		}

		gpio_direction_output(panel_data->reset_gpio, 1);
	}

	return 0;
}

static void __init rx71_display_cleanup(void)
{
	if (!rx71_board_has_gf())
		gpio_free(rx71_dss_devices[0]->reset_gpio);
}

static int __init rx71_display_init_tv(void)
{
	int r;

	if (rx71_tv_display_data.reset_gpio == -1)
		return 0;

	r = gpio_request(rx71_tv_display_data.reset_gpio,
			"TV-out enable");
	if (r < 0) {
		rx71_tv_display_data.reset_gpio = -1;
		pr_err("Unable to get TV-out GPIO\n");
		return r;
	}

	gpio_direction_output(rx71_tv_display_data.reset_gpio, 0);

	return 0;
}

static void __init rx71_display_cleanup_tv(void)
{
	if (rx71_tv_display_data.reset_gpio == -1)
		return;

	gpio_free(rx71_tv_display_data.reset_gpio);
}

static struct omapfb_platform_data rx71_omapfb_data = {
	.mem_desc = {
		.region_cnt = 1,
		.region[0] = {
			.format_used = true,
			.format = OMAPFB_COLOR_RGB565,
			.size = PAGE_ALIGN(856 * 512 * 2 * 3),
			.xres_virtual = 856,
			.yres_virtual = 512 * 3,
		}
	}
};

static void rx71_sgx_dev_release(struct device *pdev)
{
	pr_debug("%s: (%p)", __func__, pdev);
}

static struct platform_device rx71_sgx_device = {
	.name		= "pvrsrvkm",
	.id		= -1,
	.dev		= {
		.release = rx71_sgx_dev_release,
	}
};

static int __init rx71_video_init(void)
{
	int r;

	if (!machine_is_nokia_rx71())
		return 0;

	omapfb_set_platform_data(&rx71_omapfb_data);

	omap_setup_dss_device(&rx71_dss_device);

	/* TE is not connected on M3.2 boards */
	if (rx71_board_has_broken_himalaya_v2())
		rx71_himalaya_v2_data.use_ext_te = false;

	/* This HWID has DSI lines differently */
	if (system_rev == 0x3225) {
		rx71_himalaya_v2_device.phy.dsi.clk_lane = 3;
		rx71_himalaya_v2_device.phy.dsi.data1_lane = 1;
		rx71_himalaya_v2_device.phy.dsi.data2_lane = 2;
	}

	/* These HWIDs has DSI lines differently */
	if (system_rev == 0x4005 || system_rev == 0x3405 ||
	    system_rev == 0x3407 || system_rev == 0x3500) {
		rx71_pyrenees_device.phy.dsi.data1_lane = 1;
		rx71_pyrenees_device.phy.dsi.data2_lane = 3;
	}

	if (rx71_board_has_gf())
		rx71_dss_devices[0] = &rx71_gf_device;
	else if (rx71_board_has_himalaya())
		rx71_dss_devices[0] = &rx71_himalaya_device;
	else if (rx71_board_has_himalaya_v2())
		rx71_dss_devices[0] = &rx71_himalaya_v2_device;
	else if (rx71_board_has_pyrenees())
		rx71_dss_devices[0] = &rx71_pyrenees_device;
	else {
		r = -ENODEV;
		goto err0;
	}

	r = rx71_display_init();
	if (r < 0)
		goto err0;

	rx71_dss_data.default_device = rx71_dss_devices[0];

	r = rx71_display_init_tv();
	if (r < 0)
		goto err1;

	r = platform_device_register(&rx71_dss_device);
	if (r < 0)
		goto err2;

	r = platform_device_register(&rx71_sgx_device);
	if (r < 0)
		goto err3;

	return 0;

err3:
	platform_device_unregister(&rx71_dss_device);
err2:
	rx71_display_cleanup_tv();
err1:
	rx71_display_cleanup();
err0:
	pr_err("%s failed (%d)\n", __func__, r);

	return r;
}

subsys_initcall(rx71_video_init);

void __init rx71_video_mem_init(void)
{
	/*
	 * GFX 856x512x16bpp, 3 buffers
	 * VID1/2 1280x720x16bpp, 6 buffers (shared by both)
	 */
	omap_vram_set_sdram_vram(PAGE_ALIGN(856 * 512 * 2 * 3) +
			PAGE_ALIGN(1280 * 720 * 2 * 6), 0);
}
