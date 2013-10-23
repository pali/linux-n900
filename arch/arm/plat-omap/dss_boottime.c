/*
 * File: arch/arm/plat-omap/dss.c
 *
 * OMAP Display Subsystem helper functions
 *
 * Copyright (C) 2008 Nokia Corporation
 * Author: Imre Deak <imre.deak@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/omapfb.h>
#include <linux/delay.h>

#include <asm/io.h>

#include <mach/dss_boottime.h>

#define DSS_BASE		0x48050000
#define DSS_SYSCONFIG		0x0010
#define DSS_SYSSTATUS		0x0014

#define DISPC_BASE		0x48050400

#define DISPC_SYSCONFIG		0x0010
#define DISPC_SYSSTATUS		0x0014
#define DISPC_CONTROL		0x0040

/* Base address offsets into DISPC_BASE for the 3 planes */
#define DISPC_GFX_BASE		0x0000
#define DISPC_VID1_BASE		0x00BC
#define DISPC_VID2_BASE		0x014C

/* Register offsets into GFX / VIDx plane base addresses */
#define DISPC_GFX_BA0		0x0080
#define DISPC_GFX_SIZE		0x008C
#define DISPC_GFX_ATTRIBUTES	0x00A0
#define DISPC_VID_BA0		0x0000
#define DISPC_VID_ATTRIBUTES	0x0010
#define DISPC_VID_PICTURE_SIZE	0x0028

#define OMAP3430_DSS_ICK_REG	0x48004e10
#define OMAP3430_DSS_ICK_BIT	(1 << 0)
#define OMAP3430_DSS_FCK_REG	0x48004e00
#define OMAP3430_DSS_FCK_BIT	(1 << 0)

static struct clk *dss_fclk;
static struct clk *dss_iclk;
static struct clk *digit_fclk;

static const u32 at_reg[3] = {
	DISPC_GFX_BASE + DISPC_GFX_ATTRIBUTES,
	DISPC_VID1_BASE + DISPC_VID_ATTRIBUTES,
	DISPC_VID2_BASE + DISPC_VID_ATTRIBUTES
};

static const u32 ba0_reg[3] = {
	DISPC_GFX_BASE + DISPC_GFX_BA0,
	DISPC_VID1_BASE + DISPC_VID_BA0,
	DISPC_VID2_BASE + DISPC_VID_BA0,
};

static const u32 siz_reg[3] = {
	DISPC_GFX_BASE + DISPC_GFX_SIZE,
	DISPC_VID1_BASE + DISPC_VID_PICTURE_SIZE,
	DISPC_VID2_BASE + DISPC_VID_PICTURE_SIZE,
};

int dss_boottime_get_clocks(void)
{
	static const char *dss_ick = "dss_ick";
	static const char *dss1_fck = cpu_is_omap34xx() ?
				"dss1_alwon_fck" : "dss1_fck";
	static const char *digit_fck = cpu_is_omap34xx() ?
				"dss_tv_fck" : "dss_54m_fck";

	BUG_ON(dss_iclk || dss_fclk || digit_fclk);

	dss_iclk = clk_get(NULL, dss_ick);
	if (IS_ERR(dss_iclk))
		return PTR_ERR(dss_iclk);

	dss_fclk = clk_get(NULL, dss1_fck);
	if (IS_ERR(dss_fclk)) {
		clk_put(dss_iclk);
		dss_iclk = NULL;
		return PTR_ERR(dss_fclk);
	}

	digit_fclk = clk_get(NULL, digit_fck);
	if (IS_ERR(digit_fclk)) {
		clk_put(dss_iclk);
		clk_put(dss_fclk);
		dss_iclk = NULL;
		dss_fclk = NULL;
		return PTR_ERR(digit_fclk);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(dss_boottime_get_clocks);

void dss_boottime_put_clocks(void)
{
	clk_put(digit_fclk);
	clk_put(dss_iclk);
	clk_put(dss_fclk);
	digit_fclk = NULL;
	dss_iclk = NULL;
	dss_fclk = NULL;
}
EXPORT_SYMBOL_GPL(dss_boottime_put_clocks);

int dss_boottime_enable_clocks(void)
{
	int r;

	BUG_ON(!dss_fclk || !dss_iclk);

	if ((r = clk_enable(dss_iclk)) < 0)
		goto err1;

	if ((r = clk_enable(dss_fclk)) < 0)
		goto err2;

	return 0;

err2:
	clk_disable(dss_iclk);
err1:
	return r;
}
EXPORT_SYMBOL_GPL(dss_boottime_enable_clocks);

void dss_boottime_disable_clocks(void)
{
	BUG_ON(!dss_fclk || !dss_iclk);

	clk_disable(dss_iclk);
	clk_disable(dss_fclk);
}
EXPORT_SYMBOL_GPL(dss_boottime_disable_clocks);

static int __init enable_digit_clocks(void)
{
	BUG_ON(!digit_fclk);

	return clk_enable(digit_fclk);
}

static void __init disable_digit_clocks(void)
{
	BUG_ON(!digit_fclk);

	clk_disable(digit_fclk);
}

/**
 * dispc_read_reg          Read a DISPC register
 * @reg - DISPC register to read
 *
 * Assumes that clocks are on.
 */
static u32 __init dispc_read_reg(int reg)
{
	return omap_readl(DISPC_BASE + reg);
}

/**
 * dispc_write_reg          Write a DISPC register
 * @reg - DISPC register to write
 * @val - value to write
 *
 * Assumes that clocks are on.
 */
static void __init dispc_write_reg(int reg, u32 val)
{
	omap_writel(val, DISPC_BASE + reg);
}

/**
 * dss_read_reg          Read a DSS register
 * @reg - DSS register to read
 *
 * Assumes that clocks are on.
 */
static u32 __init dss_read_reg(int reg)
{
	return omap_readl(DSS_BASE + reg);
}

/**
 * dss_write_reg          Write a DSS register
 * @reg - DSS register to write
 * @val - value to write
 *
 * Assumes that clocks are on.
 */
static void __init dss_write_reg(int reg, u32 val)
{
	omap_writel(val, DSS_BASE + reg);
}

/**
 * dss_boottime_plane_is_enabled  Determines whether the plane is enabled
 * @plane_idx - index of the plane to do the check for
 *
 * Return 1 if plane is enabled 0 otherwise.
 *
 * Since clock FW might not be initialized yet we can't use the clk_*
 * interface.
 */
int __init dss_boottime_plane_is_enabled(int plane_idx)
{
	if (cpu_is_omap3430()) {
		u32 l;

		if (plane_idx >= 3)
			return 0;

		if (!(omap_readl(OMAP3430_DSS_ICK_REG) & OMAP3430_DSS_ICK_BIT))
			return 0;

		if (!(omap_readl(OMAP3430_DSS_FCK_REG) & OMAP3430_DSS_FCK_BIT))
			return 0;

		l = dispc_read_reg(DISPC_CONTROL);
		/* LCD out enabled ? */
		if (!(l & 1))
			/* No, don't take over memory */
			return 0;

		l = dispc_read_reg(at_reg[plane_idx]);
		/* Plane enabled ? */
		if (!(l & 1))
			/* No, don't take over memory */
			return 0;

		return 1;
	}

	return 0;
}

/**
 * dss_boottime_get_plane_base - get base address for a plane's FB
 * @plane_idx - plane index
 *
 * Return physical base address if plane is enabled, otherwise -1.
 */
u32 __init dss_boottime_get_plane_base(int plane_idx)
{
	if (dss_boottime_plane_is_enabled(plane_idx))
		return dispc_read_reg(ba0_reg[plane_idx]);
	else
		return -1UL;
}

static const struct {
	enum omapfb_color_format format;
	int bpp;
} mode_info[] = {
	{ OMAPFB_COLOR_CLUT_1BPP,	1, },
	{ OMAPFB_COLOR_CLUT_2BPP,	2, },
	{ OMAPFB_COLOR_CLUT_4BPP,	4, },
	{ OMAPFB_COLOR_CLUT_8BPP,	8, },
	{ OMAPFB_COLOR_RGB444,		16, },
	{ OMAPFB_COLOR_ARGB16,		16, },
	{ OMAPFB_COLOR_RGB565,		16, },
	{ 0,				0,  },	/* id=0x07, reserved */
	{ OMAPFB_COLOR_RGB24U,		32, },
	{ OMAPFB_COLOR_RGB24P,		24, },
	{ OMAPFB_COLOR_YUY422,		16, },
	{ OMAPFB_COLOR_YUV422,		16, },
	{ OMAPFB_COLOR_ARGB32,		32, },
	{ OMAPFB_COLOR_RGBA32,		32, },
	{ OMAPFB_COLOR_RGBX32,		32, },
};

static unsigned __init get_plane_mode(int plane_idx)
{
	u32 l;

	l = dispc_read_reg(at_reg[plane_idx]);
	l = (l >> 1) & 0x0f;
	if (l == 0x07 || l >= ARRAY_SIZE(mode_info))
		BUG();
	/* For the GFX plane YUV2 and UYVY modes are not defined. */
	if (plane_idx == 0 && (l == 0x0a || l == 0x0b))
		BUG();
	return l;
}

/**
 * dss_boottime_get_plane_format - get color format for a plane's FB
 * @plane_idx - plane index
 *
 * Return plane color format if plane is enabled, otherwise -1.
 */
enum omapfb_color_format __init dss_boottime_get_plane_format(int plane_idx)
{
	unsigned mode;

	if (!dss_boottime_plane_is_enabled(plane_idx))
		return -1;

	mode = get_plane_mode(plane_idx);

	return mode_info[mode].format;
}

int __init dss_boottime_get_plane_bpp(int plane_idx)
{
	unsigned mode;
	unsigned bpp;

	if (!dss_boottime_plane_is_enabled(plane_idx))
		return -1;

	mode = get_plane_mode(plane_idx);
	bpp = mode_info[mode].bpp;

	return bpp;
}

/**
 * dss_boottime_get_plane_size - get size of a plane's FB
 * @plane_idx - plane index
 *
 * Return the size of a plane's FB based on it's color format and width/height
 * if the plane is enabled, otherwise -1.
 */
size_t __init dss_boottime_get_plane_size(int plane_idx)
{
	u32 l;
	unsigned bpp;
	unsigned x, y;
	size_t size;

	if (!dss_boottime_plane_is_enabled(plane_idx))
		return -1;

	bpp = dss_boottime_get_plane_bpp(plane_idx);

	l = dispc_read_reg(siz_reg[plane_idx]);
	x = l & ((1 << 11) - 1);
	x++;
	l >>= 16;
	y = l & ((1 << 11) - 1);
	y++;

	size = x * y * bpp / 8;

	size = PAGE_ALIGN(size);

	return size;
}

/**
 * dss_boottime_reset - reset DSS
 *
 * This can only be called after dss_boottime_get_clocks has been called.
 */
int __init dss_boottime_reset(void)
{
	int tmo = 100000;
	u32 l;

	BUG_ON(!dss_fclk || !dss_iclk || !digit_fclk);

	/* Reset monitoring works only w/ the 54M clk */
	if (dss_boottime_enable_clocks() < 0)
		goto err1;

	if (enable_digit_clocks() < 0)
		goto err2;

	/* Resetting DSS right after enabling clocks, or if
	 * bootloader has enabled the display, seems to put
	 * DSS sometimes in an invalid state. Disabling output
	 * and waiting after enabling clocks seem to fix this */

	/* disable LCD & DIGIT output */
	dispc_write_reg(DISPC_CONTROL, dispc_read_reg(DISPC_CONTROL) & ~0x3);
	msleep(50);

	/* Soft reset */
	l = dss_read_reg(DSS_SYSCONFIG);
	l |= 1 << 1;
	dss_write_reg(DSS_SYSCONFIG, l);

	while (!(dss_read_reg(DSS_SYSSTATUS) & 1)) {
		if (!--tmo)
			goto err3;
	}

	disable_digit_clocks();
	dss_boottime_disable_clocks();

	return 0;

err3:
	disable_digit_clocks();
err2:
	dss_boottime_disable_clocks();
err1:
	return -ENODEV;
}

