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
#include <linux/io.h>
#include <linux/i2c/twl4030.h>

#include <mach/board.h>
#include <mach/display.h>
#include <mach/dss_boottime.h>

#include "dss.h"

#define CONTROL_PADCONF_BASE	0x48002000

#define OMAP_SDI_PAD_DIS(pe,pu)	((7 << 0)		| /* MODE 7 = safe */ \
				 (((pe) ? 1 : 0) << 3)	| /* PULL_ENA */      \
				 (((pu) ? 1 : 0) << 4)	| /* PULL_UP  */      \
				 (1 << 8))		  /* INPUT_EN */

#define OMAP_SDI_PAD_EN		 (1 << 0)		  /* MODE 1 = SDI_xx */

#define OMAP_SDI_PAD_MASK	OMAP_SDI_PAD_DIS(1, 1)

static struct {
	bool skip_init;
	bool update_enabled;
} sdi;

/* CONTROL_PADCONF_DSS_DATAXX */
static const u16 sdi_pads[] =
{
	0x0f0,		/* 10[ 7..0]:SDI_DAT1N */
	0x0f2,		/* 10[15..0]:SDI_DAT1P */
	0x0f4,		/* 12[ 7..0]:SDI_DAT2N */
	0x0f6,		/* 12[15..0]:SDI_DAT2P */
	0x0f8,		/* 14[ 7..0]:SDI_DAT3N */
	0x0fa,		/* 14[15..0]:SDI_DAT3P */
	0x108,		/* 22[ 7..0]:SDI_CLKN */
	0x10a,		/* 22[15..0]:SDI_CLKP */
};

/*
 * Check if bootloader / platform code has configured the SDI pads properly.
 * This means it either configured all required pads for SDI mode, or that it
 * left all the required pads unconfigured.
 */
static int sdi_pad_init(struct omap_display *display)
{
	unsigned req_map;
	bool configured = false;
	bool unconfigured = false;
	int data_pairs;
	int i;

	data_pairs = display->hw_config.u.sdi.datapairs;
	req_map = (1 << (data_pairs * 2)) - 1;		/* data lanes */
	req_map |= 3 << 6;				/* clk lane */
	for (i = 0; i < ARRAY_SIZE(sdi_pads); i++) {
		u32 reg;
		u32 val;

		if (!((1 << i) & req_map))
			/* Ignore unneded pads. */
			continue;
		reg = CONTROL_PADCONF_BASE + sdi_pads[i];
		val = omap_readw(reg);
		switch (val & 0x07) {	/* pad mode */
		case 1:
			if (unconfigured)
				break;
			/* Is the pull configuration ok for SDI mode? */
			if ((val & OMAP_SDI_PAD_MASK) != OMAP_SDI_PAD_EN)
				break;
			configured = true;
			break;
		case 0:
		case 7:
			if (configured)
				break;
			unconfigured = true;
			break;
		default:
			break;
		}
	}
	if (i != ARRAY_SIZE(sdi_pads)) {
		DSSERR("SDI: invalid pad configuration\n");
		return -1;
	}

	return 0;
}

static void sdi_pad_config(struct omap_display *display, bool enable)
{
	int data_pairs;
	bool pad_off_pe, pad_off_pu;
	unsigned req_map;
	int i;

	data_pairs = display->hw_config.u.sdi.datapairs;
	pad_off_pe = display->hw_config.u.sdi.pad_off_pe;
	pad_off_pu = display->hw_config.u.sdi.pad_off_pu;
	req_map = (1 << (data_pairs * 2)) - 1;		/* data lanes */
	req_map |= 3 << 6;				/* clk lane */
	for (i = 0; i < ARRAY_SIZE(sdi_pads); i++) {
		u32 reg;
		u16 val;

		if (!((1 << i) & req_map))
			continue;
		if (enable)
			val = OMAP_SDI_PAD_EN;
		else
			val = OMAP_SDI_PAD_DIS(pad_off_pe, pad_off_pu);
		reg = CONTROL_PADCONF_BASE + sdi_pads[i];
		omap_writew(val, reg);
	}
}

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

	twl4030_enable_regulator(RES_VAUX1);

	sdi_pad_config(display, 1);

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
		r = dss_sdi_enable();
		if (r)
			goto err1;
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
	twl4030_disable_regulator(RES_VAUX1);

	return r;
}

static int sdi_display_resume(struct omap_display *display);

static void sdi_display_disable(struct omap_display *display)
{
	if (display->state == OMAP_DSS_DISPLAY_DISABLED)
		return;

	if (display->state == OMAP_DSS_DISPLAY_SUSPENDED) {
		if (sdi_display_resume(display))
			return;
	}

	display->panel->disable(display);

	dispc_enable_lcd_out(0);

	dss_sdi_disable();

	dss_clk_disable(DSS_CLK_ICK | DSS_CLK_FCK1);
	sdi_pad_config(display, 0);

	twl4030_disable_regulator(RES_VAUX1);

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
	sdi_pad_config(display, 0);

	twl4030_disable_regulator(RES_VAUX1);

	display->state = OMAP_DSS_DISPLAY_SUSPENDED;

	return 0;
}

static int sdi_display_resume(struct omap_display *display)
{
	int r;

	if (display->state != OMAP_DSS_DISPLAY_SUSPENDED)
		return -EINVAL;

	twl4030_enable_regulator(RES_VAUX1);

	sdi_pad_config(display, 1);
	dss_clk_enable(DSS_CLK_ICK | DSS_CLK_FCK1);

	r = dss_sdi_enable();
	if (r)
		goto err;
	mdelay(2);

	dispc_enable_lcd_out(1);

	if (display->panel->resume)
		display->panel->resume(display);

	display->state = OMAP_DSS_DISPLAY_ACTIVE;

	return 0;

 err:
	dss_clk_disable(DSS_CLK_ICK | DSS_CLK_FCK1);
	sdi_pad_config(display, 0);

	twl4030_disable_regulator(RES_VAUX1);

	return r;
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

int sdi_init_display(struct omap_display *display)
{
	DSSDBG("SDI init\n");

	display->enable = sdi_display_enable;
	display->disable = sdi_display_disable;
	display->suspend = sdi_display_suspend;
	display->resume = sdi_display_resume;
	display->set_update_mode = sdi_display_set_update_mode;
	display->get_update_mode = sdi_display_get_update_mode;
	display->get_timings = sdi_get_timings;

	return sdi_pad_init(display);
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
