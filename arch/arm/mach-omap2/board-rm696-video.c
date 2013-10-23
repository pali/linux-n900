/*
 * linux/arch/arm/mach-omap2/board-rm696-video.c
 *
 * Copyright (C) 2010 Nokia
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
#include <linux/pvr.h>

#include <asm/mach-types.h>
#include <mach/gpio.h>

#include <plat/board.h>
#include <plat/common.h>
#include <plat/display.h>
#include <plat/panel-nokia-dsi.h>
#include <plat/vram.h>
#include <plat/ctrl-gf.h>

#include "dss.h"

static bool rm696_board_has_gf(void)
{
	return system_rev < 0x0200;
}

static bool rm696_board_has_pyrenees(void)
{
	return !rm696_board_has_gf();
}

static struct nokia_dsi_panel_data rm696_panel_data = {
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

static struct omap_dss_device rm696_dsi_display_data = {
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
		.ext_te = true,
		.ext_te_gpio = 62,
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

	.data = &rm696_panel_data,
};

static struct ctrl_gf_platform_data rm696_gf_data = {
	.reset_gpio = 170,
	.pdx_gpio = 69,
	.sysclk_name = "sys_clkout1",
	.panel_gpio = 87,
};

static struct omap_dss_device rm696_dsi_display_data_gf = {
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
	},

	.clocks = {
		.dispc = {
			/* LCK 170.88 MHz */
			.lck_div = 1,
			/* PCK 42.72 MHz */
			.pck_div = 4,
		},

		.dsi = {
			/* DDR CLK 256.32 MHz */
			.regn = 10,
			.regm = 267,
			/* DISPC FCLK 170.88 MHz */
			.regm3 = 6,
			/* DSI FCLK 170.88 MHz */
			.regm4 = 6,

			/* LP CLK 9.493 MHz */
			.lp_clk_div = 9,
		},
	},

	.data = &rm696_gf_data,
};

static int rm696_tv_enable(struct omap_dss_device *dssdev)
{
	if (dssdev->reset_gpio != -1)
		gpio_set_value(dssdev->reset_gpio, 1);

	return 0;
}

static void rm696_tv_disable(struct omap_dss_device *dssdev)
{
	if (dssdev->reset_gpio != -1)
		gpio_set_value(dssdev->reset_gpio, 0);
}

static struct omap_dss_device rm696_tv_display_data = {
	.type = OMAP_DISPLAY_TYPE_VENC,
	.name = "tv",
	.driver_name = "venc",
	/* was 40, handled by twl5031-aci */
	.reset_gpio = -1,
	.phy.venc.type = OMAP_DSS_VENC_TYPE_COMPOSITE,
	.platform_enable = rm696_tv_enable,
	.platform_disable = rm696_tv_disable,
};

static struct omap_dss_device *rm696_dss_devices[] = {
	&rm696_dsi_display_data,
	&rm696_tv_display_data,
};

static struct omap_dss_board_info rm696_dss_data = {
	.num_devices = ARRAY_SIZE(rm696_dss_devices),
	.devices = rm696_dss_devices,
	.default_device = &rm696_dsi_display_data,
};

struct platform_device rm696_dss_device = {
	.name          = "omapdss",
	.id            = -1,
	.dev            = {
		.platform_data = &rm696_dss_data,
	},
};

static struct omapfb_platform_data rm696_omapfb_data = {
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

static void rm696_sgx_dev_release(struct device *pdev)
{
	pr_debug("%s: (%p)", __func__, pdev);
}

static struct sgx_platform_data rm696_sgx_platform_data = {
	.fclock_max	= 200000000,
};

static struct platform_device rm696_sgx_device = {
	.name		= "pvrsrvkm",
	.id		= -1,
	.dev		= {
		.platform_data = &rm696_sgx_platform_data,
		.release = rm696_sgx_dev_release,
	}
};


static int __init rm696_video_init(void)
{
	int r;

	if (!machine_is_nokia_rm696())
		return 0;

	omap_setup_dss_device(&rm696_dss_device);

	if (rm696_board_has_pyrenees()) {
		rm696_dss_devices[0] = &rm696_dsi_display_data;

		r = gpio_request(rm696_panel_data.reset_gpio, "pyrenees reset");
		if (r < 0)
			goto err0;

		r = gpio_direction_output(rm696_panel_data.reset_gpio, 1);
		if (r < 0)
			goto err1;
	} else if (rm696_board_has_gf()) {
		rm696_dss_devices[0] = &rm696_dsi_display_data_gf;
	}

	rm696_dss_data.default_device = rm696_dss_devices[0];

	/* TV */
	if (rm696_tv_display_data.reset_gpio != -1) {
		r = gpio_request(rm696_tv_display_data.reset_gpio,
				 "TV-out enable");
		if (r < 0)
			goto err1;

		r = gpio_direction_output(rm696_tv_display_data.reset_gpio, 0);
		if (r < 0)
			goto err2;
	}

	r = platform_device_register(&rm696_dss_device);
	if (r < 0)
		goto err2;

	omapfb_set_platform_data(&rm696_omapfb_data);

	r = platform_device_register(&rm696_sgx_device);
	if (r < 0)
		goto err3;

	return 0;

err3:
	platform_device_unregister(&rm696_dss_device);
err2:
	if (rm696_tv_display_data.reset_gpio != -1) {
		gpio_free(rm696_tv_display_data.reset_gpio);
		rm696_tv_display_data.reset_gpio = -1;
	}
err1:
	if (rm696_board_has_pyrenees()) {
		gpio_free(rm696_panel_data.reset_gpio);
		rm696_panel_data.reset_gpio = -1;
	}
err0:
	pr_err("%s failed (%d)\n", __func__, r);

	return r;
}

subsys_initcall(rm696_video_init);

void __init rm696_video_mem_init(void)
{
	/*
	 * GFX 856x512x16bpp, 3 buffers
	 * VID1/2 1280x720x16bpp, 6 buffers (shared by both)
	 */
	omap_vram_set_sdram_vram(PAGE_ALIGN(856 * 512 * 2 * 3) +
			PAGE_ALIGN(1280 * 720 * 2 * 6), 0);
}
