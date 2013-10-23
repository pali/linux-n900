/*
 * linux/arch/arm/mach-omap2/board-rm680-video.c
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

#include "board-rm680-types.h"
#include "dss.h"

static bool rm680_board_has_gf(void)
{
	return	board_is_rm680() &&
		(system_rev >= 0x0320 && system_rev != 0x0523 &&
		 system_rev <= 0x0620);
}

static bool rm680_board_has_himalaya(void)
{
	return !rm680_board_has_gf();
}

static struct nokia_dsi_panel_data rm680_panel_data = {
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

static struct omap_dss_device rm680_dsi_display_data = {
	.type = OMAP_DISPLAY_TYPE_DSI,
	.name = "lcd",
	.driver_name = "panel-nokia-dsi",
	.phy.dsi = {
		.clk_lane = 2,
		.clk_pol = 0,
		.data1_lane = 1,
		.data1_pol = 0,
		.data2_lane = 3,
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

	.data = &rm680_panel_data,
};

static struct ctrl_gf_platform_data rm680_gf_data = {
	.reset_gpio = 170,
	.pdx_gpio = 69,
	.sysclk_name = "sys_clkout1",
	.panel_gpio = 87,
	.te_gpio = 62,
};

static struct omap_dss_device rm680_dsi_display_data_gf = {
	.type = OMAP_DISPLAY_TYPE_DSI,
	.name = "lcd",
	.driver_name = "gf",
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

	.data = &rm680_gf_data,
};

static int rm680_tv_enable(struct omap_dss_device *dssdev)
{
	if (dssdev->reset_gpio != -1)
		gpio_set_value(dssdev->reset_gpio, 1);

	return 0;
}

static void rm680_tv_disable(struct omap_dss_device *dssdev)
{
	if (dssdev->reset_gpio != -1)
		gpio_set_value(dssdev->reset_gpio, 0);
}

static struct omap_dss_device rm680_tv_display_data = {
	.type = OMAP_DISPLAY_TYPE_VENC,
	.name = "tv",
	.driver_name = "venc",
	/* was 40, handled by twl5031-aci */
	.reset_gpio = -1,
	.phy.venc.type = OMAP_DSS_VENC_TYPE_COMPOSITE,
	.platform_enable = rm680_tv_enable,
	.platform_disable = rm680_tv_disable,
};

static struct omap_dss_device *rm680_dss_devices[] = {
	&rm680_dsi_display_data,
	&rm680_tv_display_data,
};

static struct omap_dss_board_info rm680_dss_data = {
	.num_devices = ARRAY_SIZE(rm680_dss_devices),
	.devices = rm680_dss_devices,
	.default_device = &rm680_dsi_display_data,
};

struct platform_device rm680_dss_device = {
	.name          = "omapdss",
	.id            = -1,
	.dev            = {
		.platform_data = &rm680_dss_data,
	},
};

static struct omapfb_platform_data rm680_omapfb_data = {
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

static void rm680_sgx_dev_release(struct device *pdev)
{
	pr_debug("%s: (%p)", __func__, pdev);
}

static struct sgx_platform_data rm680_sgx_platform_data = {
	.fclock_max	= 200000000,
};

static struct platform_device rm680_sgx_device = {
	.name		= "pvrsrvkm",
	.id		= -1,
	.dev		= {
		.platform_data = &rm680_sgx_platform_data,
		.release = rm680_sgx_dev_release,
	}
};

static int __init rm680_video_init(void)
{
	int r;

	if (!machine_is_nokia_rm680())
		return 0;

	if (board_is_rm680() && system_rev < 0x0420) {
		pr_err("RM-680 display is supported only on HWID 0420 and " \
				"higher\n");
		r = -ENODEV;
		goto err0;
	}

	omap_setup_dss_device(&rm680_dss_device);

	if (rm680_board_has_himalaya()) {
		rm680_dss_devices[0] = &rm680_dsi_display_data;

		if (board_is_rm680()) {
			if (system_rev < 0x0620 || system_rev == 0x0821 ||
					system_rev == 0x0823) {
				rm680_dsi_display_data.phy.dsi.data1_lane = 3;
				rm680_dsi_display_data.phy.dsi.data2_lane = 1;
			}
		}

		r = gpio_request(rm680_panel_data.reset_gpio, "himalaya reset");
		if (r < 0)
			goto err0;

		r = gpio_direction_output(rm680_panel_data.reset_gpio, 1);
		if (r < 0)
			goto err1;
	} else if (rm680_board_has_gf()) {
		rm680_dss_devices[0] = &rm680_dsi_display_data_gf;
	}

	rm680_dss_data.default_device = rm680_dss_devices[0];

	/* TV */
	if (rm680_tv_display_data.reset_gpio != -1) {
		r = gpio_request(rm680_tv_display_data.reset_gpio,
				 "TV-out enable");
		if (r < 0)
			goto err1;

		r = gpio_direction_output(rm680_tv_display_data.reset_gpio, 0);
		if (r < 0)
			goto err2;
	}

	r = platform_device_register(&rm680_dss_device);
	if (r < 0)
		goto err2;

	omapfb_set_platform_data(&rm680_omapfb_data);

	r = platform_device_register(&rm680_sgx_device);
	if (r < 0)
		goto err3;

	return 0;

err3:
	platform_device_unregister(&rm680_dss_device);
err2:
	if (rm680_tv_display_data.reset_gpio != -1) {
		gpio_free(rm680_tv_display_data.reset_gpio);
		rm680_tv_display_data.reset_gpio = -1;
	}
err1:
	if (rm680_board_has_himalaya()) {
		gpio_free(rm680_panel_data.reset_gpio);
		rm680_panel_data.reset_gpio = -1;
	}
err0:
	pr_err("%s failed (%d)\n", __func__, r);

	return r;
}

subsys_initcall(rm680_video_init);

void __init rm680_video_mem_init(void)
{
	/*
	 * GFX 856x512x16bpp, 3 buffers
	 * VID1/2 1280x720x16bpp, 6 buffers (shared by both)
	 */
	omap_vram_set_sdram_vram(PAGE_ALIGN(856 * 512 * 2 * 3) +
			PAGE_ALIGN(1280 * 720 * 2 * 6), 0);
}
