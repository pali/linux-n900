/*
 * LCD panel driver for Samsung LTE430WQ-F0C
 *
 * Author: Steve Sakoman <steve@sakoman.com>
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

static int samsung_lte_panel_init(struct omap_display *display)
{
	return 0;
}

static void samsung_lte_panel_cleanup(struct omap_display *display)
{
}

static int samsung_lte_panel_enable(struct omap_display *display)
{
	int r = 0;

	/* wait couple of vsyncs until enabling the LCD */
	msleep(50);

	if (display->hw_config.panel_enable)
		r = display->hw_config.panel_enable(display);

	return r;
}

static void samsung_lte_panel_disable(struct omap_display *display)
{
	if (display->hw_config.panel_disable)
		display->hw_config.panel_disable(display);

	/* wait at least 5 vsyncs after disabling the LCD */
	msleep(100);
}

static int samsung_lte_panel_suspend(struct omap_display *display)
{
	samsung_lte_panel_disable(display);
	return 0;
}

static int samsung_lte_panel_resume(struct omap_display *display)
{
	return samsung_lte_panel_enable(display);
}

static struct omap_panel samsung_lte_panel = {
	.owner		= THIS_MODULE,
	.name		= "samsung-lte430wq-f0c",
	.init		= samsung_lte_panel_init,
	.cleanup	= samsung_lte_panel_cleanup,
	.enable		= samsung_lte_panel_enable,
	.disable	= samsung_lte_panel_disable,
	.suspend	= samsung_lte_panel_suspend,
	.resume		= samsung_lte_panel_resume,

	.timings = {
		.x_res = 480,
		.y_res = 272,

		.pixel_clock	= 9200,

		.hsw		= 41,
		.hfp		= 8,
		.hbp		= 45-41,

		.vsw		= 10,
		.vfp		= 4,
		.vbp		= 12-10,
	},

	.config = OMAP_DSS_LCD_TFT | OMAP_DSS_LCD_IHS | OMAP_DSS_LCD_IVS,
};


static int __init samsung_lte_panel_drv_init(void)
{
	omap_dss_register_panel(&samsung_lte_panel);
	return 0;
}

static void __exit samsung_lte_panel_drv_exit(void)
{
	omap_dss_unregister_panel(&samsung_lte_panel);
}

module_init(samsung_lte_panel_drv_init);
module_exit(samsung_lte_panel_drv_exit);
MODULE_LICENSE("GPL");
