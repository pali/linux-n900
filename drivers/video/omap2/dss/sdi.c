/*
 * linux/drivers/video/omap2/dss/sdi.c
 *
 * Copyright (C) 2009 Nokia Corporation
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

#define DSS_SUBSYS_NAME "SDI"

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>

#include <mach/board.h>
#include <mach/display.h>
#include <mach/dss_boottime.h>

#include "dss.h"


static struct {
	bool skip_init;
	bool update_enabled;
} sdi;

static void sdi_basic_init(void)
{
	dispc_set_parallel_interface_mode(OMAP_DSS_PARALLELMODE_BYPASS);

	dispc_set_lcd_display_type(OMAP_DSS_LCD_DISPLAY_TFT);
	dispc_set_tft_data_lines(24);
	dispc_lcd_enable_signal_polarity(1);
}

static int sdi_display_enable(struct omap_display *display)
{
	struct dispc_clock_info cinfo;
	u16 lck_div, pck_div;
	unsigned long fck;
	struct omap_panel *panel = display->panel;
	unsigned long pck;
	int r;

	if (display->state != OMAP_DSS_DISPLAY_DISABLED) {
		DSSERR("display already enabled\n");
		return -EINVAL;
	}

	/* In case of skip_init sdi_init has already enabled the clocks */
	if (!sdi.skip_init)
		dss_clk_enable(DSS_CLK_ICK | DSS_CLK_FCK1);

	sdi_basic_init();

	/* 15.5.9.1.2 */
	panel->config |= OMAP_DSS_LCD_RF | OMAP_DSS_LCD_ONOFF;

	dispc_set_pol_freq(panel);

	if (!sdi.skip_init)
		r = dispc_calc_clock_div(1, panel->timings.pixel_clock * 1000,
				&cinfo);
	else
		r = dispc_get_clock_div(&cinfo);

	if (r)
		goto err0;

	fck = cinfo.fck;
	lck_div = cinfo.lck_div;
	pck_div = cinfo.pck_div;

	pck = fck / lck_div / pck_div / 1000;

	if (pck != panel->timings.pixel_clock) {
		DSSWARN("Could not find exact pixel clock. Requested %d kHz, "
				"got %lu kHz\n",
				panel->timings.pixel_clock, pck);

		panel->timings.pixel_clock = pck;
	}


	dispc_set_lcd_timings(&panel->timings);

	r = dispc_set_clock_div(&cinfo);
	if (r)
		goto err1;

	if (!sdi.skip_init) {
		dss_sdi_init(display->hw_config.u.sdi.datapairs);
		dss_sdi_enable();
		mdelay(2);
	}

	dispc_enable_lcd_out(1);

	r = panel->enable(display);
	if (r)
		goto err2;

	display->state = OMAP_DSS_DISPLAY_ACTIVE;

	sdi.skip_init = 0;

	return 0;
err2:
	dispc_enable_lcd_out(0);
err1:
err0:
	dss_clk_disable(DSS_CLK_ICK | DSS_CLK_FCK1);
	return r;
}

static int sdi_display_resume(struct omap_display *display);

static void sdi_display_disable(struct omap_display *display)
{
	if (display->state == OMAP_DSS_DISPLAY_DISABLED)
		return;

	if (display->state == OMAP_DSS_DISPLAY_SUSPENDED)
		sdi_display_resume(display);

	display->panel->disable(display);

	dispc_enable_lcd_out(0);

	dss_sdi_disable();

	dss_clk_disable(DSS_CLK_ICK | DSS_CLK_FCK1);

	display->state = OMAP_DSS_DISPLAY_DISABLED;
}

static int sdi_display_suspend(struct omap_display *display)
{
	if (display->state != OMAP_DSS_DISPLAY_ACTIVE)
		return -EINVAL;

	if (display->panel->suspend)
		display->panel->suspend(display);

	dispc_enable_lcd_out(0);

	dss_sdi_disable();

	dss_clk_disable(DSS_CLK_ICK | DSS_CLK_FCK1);

	display->state = OMAP_DSS_DISPLAY_SUSPENDED;

	return 0;
}

static int sdi_display_resume(struct omap_display *display)
{
	if (display->state != OMAP_DSS_DISPLAY_SUSPENDED)
		return -EINVAL;

	dss_clk_enable(DSS_CLK_ICK | DSS_CLK_FCK1);

	dss_sdi_enable();
	mdelay(2);

	dispc_enable_lcd_out(1);

	if (display->panel->resume)
		display->panel->resume(display);

	display->state = OMAP_DSS_DISPLAY_ACTIVE;

	return 0;
}

static int sdi_display_set_update_mode(struct omap_display *display,
		enum omap_dss_update_mode mode)
{
	if (mode == OMAP_DSS_UPDATE_MANUAL)
		return -EINVAL;

	if (mode == OMAP_DSS_UPDATE_DISABLED) {
		dispc_enable_lcd_out(0);
		sdi.update_enabled = 0;
	} else {
		dispc_enable_lcd_out(1);
		sdi.update_enabled = 1;
	}

	return 0;
}

static enum omap_dss_update_mode sdi_display_get_update_mode(
		struct omap_display *display)
{
	return sdi.update_enabled ? OMAP_DSS_UPDATE_AUTO :
		OMAP_DSS_UPDATE_DISABLED;
}

static void sdi_get_timings(struct omap_display *display,
			struct omap_video_timings *timings)
{
	*timings = display->panel->timings;
}

void sdi_init_display(struct omap_display *display)
{
	DSSDBG("SDI init\n");

	display->enable = sdi_display_enable;
	display->disable = sdi_display_disable;
	display->suspend = sdi_display_suspend;
	display->resume = sdi_display_resume;
	display->set_update_mode = sdi_display_set_update_mode;
	display->get_update_mode = sdi_display_get_update_mode;
	display->get_timings = sdi_get_timings;
}

int sdi_init(bool skip_init)
{
	/* we store this for first display enable, then clear it */
	sdi.skip_init = skip_init;

	/*
	 * Enable clocks already here, otherwise there would be a toggle
	 * of them until sdi_display_enable is called.
	 */
	if (skip_init) {
		dss_clk_enable(DSS_CLK_ICK | DSS_CLK_FCK1);
		dss_boottime_disable_clocks();
		dss_boottime_put_clocks();
	}
	return 0;
}

void sdi_exit(void)
{
}
