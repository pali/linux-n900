/*
 * Generic panel support
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

static int generic_panel_init(struct omap_display *display)
{
	return 0;
}

static int generic_panel_enable(struct omap_display *display)
{
	int r = 0;

	if (display->hw_config.panel_enable)
		r = display->hw_config.panel_enable(display);

	return r;
}

static void generic_panel_disable(struct omap_display *display)
{
	if (display->hw_config.panel_disable)
		display->hw_config.panel_disable(display);
}

static int generic_panel_suspend(struct omap_display *display)
{
	generic_panel_disable(display);
	return 0;
}

static int generic_panel_resume(struct omap_display *display)
{
	return generic_panel_enable(display);
}

static struct omap_panel generic_panel = {
	.owner		= THIS_MODULE,
	.name		= "panel-generic",
	.init		= generic_panel_init,
	.enable		= generic_panel_enable,
	.disable	= generic_panel_disable,
	.suspend	= generic_panel_suspend,
	.resume		= generic_panel_resume,

	.timings = {
		/* 640 x 480 @ 60 Hz  Reduced blanking VESA CVT 0.31M3-R */
		.x_res		= 640,
		.y_res		= 480,
		.pixel_clock	= 23500,
		.hfp		= 48,
		.hsw		= 32,
		.hbp		= 80,
		.vfp		= 3,
		.vsw		= 4,
		.vbp		= 7,
	},

	.config		= OMAP_DSS_LCD_TFT,
};


static int __init generic_panel_drv_init(void)
{
	omap_dss_register_panel(&generic_panel);
	return 0;
}

static void __exit generic_panel_drv_exit(void)
{
	omap_dss_unregister_panel(&generic_panel);
}

module_init(generic_panel_drv_init);
module_exit(generic_panel_drv_exit);
MODULE_LICENSE("GPL");
