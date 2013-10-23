/*
 * linux/drivers/video/omap2/dss/dpi.c
 *
 * Copyright (C) 2009 Nokia Corporation
 * Author: Tomi Valkeinen <tomi.valkeinen@nokia.com>
 *
 * Some code and ideas taken from drivers/video/omap/ driver
 * by Imre Deak.
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

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/errno.h>

#include <mach/board.h>
#include <mach/display.h>
#include <mach/cpu.h>

#include "dss.h"

static struct {
	int update_enabled;
} dpi;

#ifdef CONFIG_OMAP2_DSS_USE_DSI_PLL
static int dpi_set_dsi_clk(bool is_tft, unsigned long pck_req,
		unsigned long *fck, int *lck_div, int *pck_div)
{
	struct dsi_clock_info cinfo;
	int r;

	r = dsi_pll_calc_pck(is_tft, pck_req, &cinfo);
	if (r)
		return r;

	r = dsi_pll_program(&cinfo);
	if (r)
		return r;

	dss_select_clk_source(0, 1);

	dispc_set_lcd_divisor(cinfo.lck_div, cinfo.pck_div);

	*fck = cinfo.dsi1_pll_fclk;
	*lck_div = cinfo.lck_div;
	*pck_div = cinfo.pck_div;

	return 0;
}
#else
static int dpi_set_dispc_clk(bool is_tft, unsigned long pck_req,
		unsigned long *fck, int *lck_div, int *pck_div)
{
	struct dispc_clock_info cinfo;
	int r;

	r = dispc_calc_clock_div(is_tft, pck_req, &cinfo);
	if (r)
		return r;

	r = dispc_set_clock_div(&cinfo);
	if (r)
		return r;

	*fck = cinfo.fck;
	*lck_div = cinfo.lck_div;
	*pck_div = cinfo.pck_div;

	return 0;
}
#endif

static int dpi_set_mode(struct omap_display *display)
{
	struct omap_panel *panel = display->panel;
	int lck_div, pck_div;
	unsigned long fck;
	unsigned long pck;
	bool is_tft;
	int r = 0;

	dss_clk_enable(DSS_CLK_ICK | DSS_CLK_FCK1);

	dispc_set_pol_freq(panel);

	is_tft = (display->panel->config & OMAP_DSS_LCD_TFT) != 0;

#ifdef CONFIG_OMAP2_DSS_USE_DSI_PLL
	r = dpi_set_dsi_clk(is_tft, panel->timings.pixel_clock * 1000,
			&fck, &lck_div, &pck_div);
#else
	r = dpi_set_dispc_clk(is_tft, panel->timings.pixel_clock * 1000,
			&fck, &lck_div, &pck_div);
#endif
	if (r)
		goto err0;

	pck = fck / lck_div / pck_div / 1000;

	if (pck != panel->timings.pixel_clock) {
		DSSWARN("Could not find exact pixel clock. "
				"Requested %d kHz, got %lu kHz\n",
				panel->timings.pixel_clock, pck);

		panel->timings.pixel_clock = pck;
	}

	dispc_set_lcd_timings(&panel->timings);

err0:
	dss_clk_disable(DSS_CLK_ICK | DSS_CLK_FCK1);
	return r;
}

static int dpi_basic_init(struct omap_display *display)
{
	bool is_tft;

	is_tft = (display->panel->config & OMAP_DSS_LCD_TFT) != 0;

	dispc_set_parallel_interface_mode(OMAP_DSS_PARALLELMODE_BYPASS);
	dispc_set_lcd_display_type(is_tft ? OMAP_DSS_LCD_DISPLAY_TFT :
			OMAP_DSS_LCD_DISPLAY_STN);
	dispc_set_tft_data_lines(display->hw_config.u.dpi.data_lines);

	return 0;
}

static int dpi_display_enable(struct omap_display *display)
{
	struct omap_panel *panel = display->panel;
	int r;

	if (display->state != OMAP_DSS_DISPLAY_DISABLED) {
		DSSERR("display already enabled\n");
		return -EINVAL;
	}

	dss_clk_enable(DSS_CLK_ICK | DSS_CLK_FCK1);

	r = dpi_basic_init(display);
	if (r)
		goto err0;

#ifdef CONFIG_OMAP2_DSS_USE_DSI_PLL
	dss_clk_enable(DSS_CLK_FCK2);
	r = dsi_pll_init(0, 1);
	if (r)
		goto err1;
#endif
	r = dpi_set_mode(display);
	if (r)
		goto err2;

	mdelay(2);

	dispc_enable_lcd_out(1);

	r = panel->enable(display);
	if (r)
		goto err3;

	display->state = OMAP_DSS_DISPLAY_ACTIVE;

	return 0;

err3:
	dispc_enable_lcd_out(0);
err2:
#ifdef CONFIG_OMAP2_DSS_USE_DSI_PLL
	dsi_pll_uninit();
err1:
	dss_clk_disable(DSS_CLK_FCK2);
#endif
err0:
	dss_clk_disable(DSS_CLK_ICK | DSS_CLK_FCK1);
	return r;
}

static int dpi_display_resume(struct omap_display *display);

static void dpi_display_disable(struct omap_display *display)
{
	if (display->state == OMAP_DSS_DISPLAY_DISABLED)
		return;

	if (display->state == OMAP_DSS_DISPLAY_SUSPENDED)
		dpi_display_resume(display);

	display->panel->disable(display);

	dispc_enable_lcd_out(0);

#ifdef CONFIG_OMAP2_DSS_USE_DSI_PLL
	dss_select_clk_source(0, 0);
	dsi_pll_uninit();
	dss_clk_disable(DSS_CLK_FCK2);
#endif

	dss_clk_disable(DSS_CLK_ICK | DSS_CLK_FCK1);

	display->state = OMAP_DSS_DISPLAY_DISABLED;
}

static int dpi_display_suspend(struct omap_display *display)
{
	if (display->state != OMAP_DSS_DISPLAY_ACTIVE)
		return -EINVAL;

	DSSDBG("dpi_display_suspend\n");

	if (display->panel->suspend)
		display->panel->suspend(display);

	dispc_enable_lcd_out(0);

	dss_clk_disable(DSS_CLK_ICK | DSS_CLK_FCK1);

	display->state = OMAP_DSS_DISPLAY_SUSPENDED;

	return 0;
}

static int dpi_display_resume(struct omap_display *display)
{
	if (display->state != OMAP_DSS_DISPLAY_SUSPENDED)
		return -EINVAL;

	DSSDBG("dpi_display_resume\n");

	dss_clk_enable(DSS_CLK_ICK | DSS_CLK_FCK1);

	dispc_enable_lcd_out(1);

	if (display->panel->resume)
		display->panel->resume(display);

	display->state = OMAP_DSS_DISPLAY_ACTIVE;

	return 0;
}

static void dpi_set_timings(struct omap_display *display,
			struct omap_video_timings *timings)
{
	DSSDBG("dpi_set_timings\n");
	display->panel->timings = *timings;
	if (display->state == OMAP_DSS_DISPLAY_ACTIVE) {
		dpi_set_mode(display);
		dispc_go(OMAP_DSS_CHANNEL_LCD);
	}
}

static int dpi_check_timings(struct omap_display *display,
			struct omap_video_timings *timings)
{
	bool is_tft;
	int r;
	int lck_div, pck_div;
	unsigned long fck;
	unsigned long pck;

	if (cpu_is_omap24xx() || omap_rev() < OMAP3430_REV_ES3_0) {
		if (timings->hsw < 1 || timings->hsw > 64 ||
				timings->hfp < 1 || timings->hfp > 256 ||
				timings->hbp < 1 || timings->hbp > 256) {
			return -EINVAL;
		}

		if (timings->vsw < 1 || timings->vsw > 64 ||
				timings->vfp > 255 || timings->vbp > 255) {
			return -EINVAL;
		}
	} else {
		if (timings->hsw < 1 || timings->hsw > 256 ||
				timings->hfp < 1 || timings->hfp > 4096 ||
				timings->hbp < 1 || timings->hbp > 4096) {
			return -EINVAL;
		}

		if (timings->vsw < 1 || timings->vsw > 64 ||
				timings->vfp > 4095 || timings->vbp > 4095) {
			return -EINVAL;
		}
	}

	if (timings->pixel_clock == 0)
		return -EINVAL;

	is_tft = (display->panel->config & OMAP_DSS_LCD_TFT) != 0;

#ifdef CONFIG_OMAP2_DSS_USE_DSI_PLL
	{
		struct dsi_clock_info cinfo;
		r = dsi_pll_calc_pck(is_tft, timings->pixel_clock * 1000,
				&cinfo);

		if (r)
			return r;

		fck = cinfo.dsi1_pll_fclk;
		lck_div = cinfo.lck_div;
		pck_div = cinfo.pck_div;
	}
#else
	{
		struct dispc_clock_info cinfo;
		r = dispc_calc_clock_div(is_tft, timings->pixel_clock * 1000,
				&cinfo);

		if (r)
			return r;

		fck = cinfo.fck;
		lck_div = cinfo.lck_div;
		pck_div = cinfo.pck_div;
	}
#endif

	pck = fck / lck_div / pck_div / 1000;

	timings->pixel_clock = pck;

	return 0;
}

static void dpi_get_timings(struct omap_display *display,
			struct omap_video_timings *timings)
{
	*timings = display->panel->timings;
}

static int dpi_display_set_update_mode(struct omap_display *display,
		enum omap_dss_update_mode mode)
{
	if (mode == OMAP_DSS_UPDATE_MANUAL)
		return -EINVAL;

	if (mode == OMAP_DSS_UPDATE_DISABLED) {
		dispc_enable_lcd_out(0);
		dpi.update_enabled = 0;
	} else {
		dispc_enable_lcd_out(1);
		dpi.update_enabled = 1;
	}

	return 0;
}

static enum omap_dss_update_mode dpi_display_get_update_mode(
		struct omap_display *display)
{
	return dpi.update_enabled ? OMAP_DSS_UPDATE_AUTO :
		OMAP_DSS_UPDATE_DISABLED;
}

int dpi_init_display(struct omap_display *display)
{
	DSSDBG("DPI init_display\n");

	display->enable = dpi_display_enable;
	display->disable = dpi_display_disable;
	display->suspend = dpi_display_suspend;
	display->resume = dpi_display_resume;
	display->set_timings = dpi_set_timings;
	display->check_timings = dpi_check_timings;
	display->get_timings = dpi_get_timings;
	display->set_update_mode = dpi_display_set_update_mode;
	display->get_update_mode = dpi_display_get_update_mode;

	return 0;
}

int dpi_init(void)
{
	return 0;
}

void dpi_exit(void)
{
}

