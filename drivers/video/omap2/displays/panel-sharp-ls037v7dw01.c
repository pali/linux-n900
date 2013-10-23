/*
 * LCD panel driver for Sharp LS037V7DW01
 *
 * Copyright (C) 2008 Nokia Corporation
 * Author: Tomi Valkeinen <tomi.valkeinen@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/module.h>
#include <linux/delay.h>

#include <mach/display.h>

static int sharp_ls_panel_init(struct omap_display *display)
{
	return 0;
}

static void sharp_ls_panel_cleanup(struct omap_display *display)
{
}

static int sharp_ls_panel_enable(struct omap_display *display)
{
	int r = 0;

	/* wait couple of vsyncs until enabling the LCD */
	msleep(50);

	if (display->hw_config.panel_enable)
		r = display->hw_config.panel_enable(display);

	return r;
}

static void sharp_ls_panel_disable(struct omap_display *display)
{
	if (display->hw_config.panel_disable)
		display->hw_config.panel_disable(display);

	/* wait at least 5 vsyncs after disabling the LCD */

	msleep(100);
}

static int sharp_ls_panel_suspend(struct omap_display *display)
{
	sharp_ls_panel_disable(display);
	return 0;
}

static int sharp_ls_panel_resume(struct omap_display *display)
{
	return sharp_ls_panel_enable(display);
}

static struct omap_panel sharp_ls_panel = {
	.owner		= THIS_MODULE,
	.name		= "sharp-ls037v7dw01",
	.init		= sharp_ls_panel_init,
	.cleanup	= sharp_ls_panel_cleanup,
	.enable		= sharp_ls_panel_enable,
	.disable	= sharp_ls_panel_disable,
	.suspend	= sharp_ls_panel_suspend,
	.resume		= sharp_ls_panel_resume,

	.timings = {
		.x_res = 480,
		.y_res = 640,

		.pixel_clock	= 19200,

		.hsw		= 2,
		.hfp		= 1,
		.hbp		= 28,

		.vsw		= 1,
		.vfp		= 1,
		.vbp		= 1,
	},

	.acb		= 0x28,

	.config	= OMAP_DSS_LCD_TFT | OMAP_DSS_LCD_IVS |	OMAP_DSS_LCD_IHS,
};


static int __init sharp_ls_panel_drv_init(void)
{
	omap_dss_register_panel(&sharp_ls_panel);
	return 0;
}

static void __exit sharp_ls_panel_drv_exit(void)
{
	omap_dss_unregister_panel(&sharp_ls_panel);
}

module_init(sharp_ls_panel_drv_init);
module_exit(sharp_ls_panel_drv_exit);
MODULE_LICENSE("GPL");
