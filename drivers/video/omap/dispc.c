/*
 * OMAP2 display controller support
 *
 * Copyright (C) 2005 Nokia Corporation
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
#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/omapfb.h>

#include <mach/sram.h>
#include <mach/board.h>
#include <mach/clock.h>

#include "dispc.h"

#define MODULE_NAME			"dispc"

#define DSS_BASE			0x48050000

#define DSS_SYSCONFIG			0x0010
#define DSS_CONTROL			0x0040
#define DSS_SDI_CONTROL			0x0044
#define DSS_PLL_CONTROL			0x0048
#define DSS_SDI_STATUS			0x005c


static struct {
	int	reg;
	u32	val;
} dss_context[] = {
	{ DSS_SYSCONFIG, },
	{ DSS_CONTROL, },
	{ DSS_SDI_CONTROL, },
	{ DSS_PLL_CONTROL, },
};

#define DISPC_BASE			0x48050400

/* DISPC common */
#define DISPC_REVISION			0x0000
#define DISPC_SYSCONFIG			0x0010
#define DISPC_SYSSTATUS			0x0014
#define DISPC_IRQSTATUS			0x0018
#define DISPC_IRQENABLE			0x001C
#define DISPC_CONTROL			0x0040
#define DISPC_CONFIG			0x0044
#define DISPC_CAPABLE			0x0048
#define DISPC_DEFAULT_COLOR0		0x004C
#define DISPC_DEFAULT_COLOR1		0x0050
#define DISPC_TRANS_COLOR0		0x0054
#define DISPC_TRANS_COLOR1		0x0058
#define DISPC_LINE_STATUS		0x005C
#define DISPC_LINE_NUMBER		0x0060
#define DISPC_TIMING_H			0x0064
#define DISPC_TIMING_V			0x0068
#define DISPC_POL_FREQ			0x006C
#define DISPC_DIVISOR			0x0070
#define DISPC_SIZE_DIG			0x0078
#define DISPC_SIZE_LCD			0x007C

#define DISPC_DATA_CYCLE1		0x01D4
#define DISPC_DATA_CYCLE2		0x01D8
#define DISPC_DATA_CYCLE3		0x01DC

static struct {
	int	reg;
	u32	val;
} dispc_context[] = {
	{ DISPC_SYSCONFIG, },
	{ DISPC_IRQENABLE, },
	{ DISPC_CONTROL, },

	{ DISPC_CONFIG, },
	{ DISPC_DEFAULT_COLOR0, },
	{ DISPC_DEFAULT_COLOR1, },
	{ DISPC_TRANS_COLOR0, },
	{ DISPC_TRANS_COLOR1, },
	{ DISPC_LINE_NUMBER, },
	{ DISPC_TIMING_H, },
	{ DISPC_TIMING_V, },
	{ DISPC_POL_FREQ, },
	{ DISPC_DIVISOR, },
	{ DISPC_SIZE_DIG, },
	{ DISPC_SIZE_LCD, },
	{ DISPC_DATA_CYCLE1, },
	{ DISPC_DATA_CYCLE2, },
	{ DISPC_DATA_CYCLE3, },
};

/* DISPC GFX plane */
#define DISPC_GFX_BA0			0x0080
#define DISPC_GFX_BA1			0x0084
#define DISPC_GFX_POSITION		0x0088
#define DISPC_GFX_SIZE			0x008C
#define DISPC_GFX_ATTRIBUTES		0x00A0
#define DISPC_GFX_FIFO_THRESHOLD	0x00A4
#define DISPC_GFX_FIFO_SIZE_STATUS	0x00A8
#define DISPC_GFX_ROW_INC		0x00AC
#define DISPC_GFX_PIXEL_INC		0x00B0
#define DISPC_GFX_WINDOW_SKIP		0x00B4
#define DISPC_GFX_TABLE_BA		0x00B8

static struct {
	int	reg;
	u32	val;
} gfx_context[] = {
	{ DISPC_GFX_BA0, },
	{ DISPC_GFX_BA1, },
	{ DISPC_GFX_POSITION, },
	{ DISPC_GFX_SIZE, },
	{ DISPC_GFX_ATTRIBUTES, },
	{ DISPC_GFX_FIFO_THRESHOLD, },
	{ DISPC_GFX_ROW_INC, },
	{ DISPC_GFX_PIXEL_INC, },
	{ DISPC_GFX_WINDOW_SKIP, },
	{ DISPC_GFX_TABLE_BA, },
};

/* DISPC Video plane 1/2 */
#define DISPC_VID1_BASE			0x00BC
#define DISPC_VID2_BASE			0x014C

/* Offsets into DISPC_VID1/2_BASE */
#define DISPC_VID_BA0			0x0000
#define DISPC_VID_BA1			0x0004
#define DISPC_VID_POSITION		0x0008
#define DISPC_VID_SIZE			0x000C
#define DISPC_VID_ATTRIBUTES		0x0010
#define DISPC_VID_FIFO_THRESHOLD	0x0014
#define DISPC_VID_FIFO_SIZE_STATUS	0x0018
#define DISPC_VID_ROW_INC		0x001C
#define DISPC_VID_PIXEL_INC		0x0020
#define DISPC_VID_FIR			0x0024
#define DISPC_VID_PICTURE_SIZE		0x0028
#define DISPC_VID_ACCU0			0x002C
#define DISPC_VID_ACCU1			0x0030

/* 8 elements in 8 byte increments */
#define DISPC_VID_FIR_COEF_H0		0x0034
/* 8 elements in 8 byte increments */
#define DISPC_VID_FIR_COEF_HV0		0x0038
/* 5 elements in 4 byte increments */
#define DISPC_VID_CONV_COEF0		0x0074

static struct {
	int	reg;
	u32	val;
} vid_context[] = {
	{ DISPC_VID_BA0, },
	{ DISPC_VID_BA1, },
	{ DISPC_VID_POSITION, },
	{ DISPC_VID_SIZE, },
	{ DISPC_VID_ATTRIBUTES, },
	{ DISPC_VID_FIFO_THRESHOLD, },
	{ DISPC_VID_ROW_INC, },
	{ DISPC_VID_PIXEL_INC, },
	{ DISPC_VID_FIR, },
	{ DISPC_VID_PICTURE_SIZE, },
	{ DISPC_VID_ACCU0, },
	{ DISPC_VID_ACCU1, },
};


static struct {
	int	reg;
	u32	val;
} vid_fir_context[2 * 2 * 8] = {	/* 2 planes * 2 coef * 8 instance */
	{ DISPC_VID_FIR_COEF_H0, },
	{ DISPC_VID_FIR_COEF_HV0, },
};

static struct {
	int	reg;
	u32	val;
} vid_conv_context[2 * 1 * 5] = {	/* 2 planes * 1 coef * 5 instance */
	{ DISPC_VID_CONV_COEF0, },
};


#define DISPC_IRQ_FRAMEMASK		0x0001
#define DISPC_IRQ_VSYNC			0x0002
#define DISPC_IRQ_EVSYNC_EVEN		0x0004
#define DISPC_IRQ_EVSYNC_ODD		0x0008
#define DISPC_IRQ_ACBIAS_COUNT_STAT	0x0010
#define DISPC_IRQ_PROG_LINE_NUM		0x0020
#define DISPC_IRQ_GFX_FIFO_UNDERFLOW	0x0040
#define DISPC_IRQ_GFX_END_WIN		0x0080
#define DISPC_IRQ_PAL_GAMMA_MASK	0x0100
#define DISPC_IRQ_OCP_ERR		0x0200
#define DISPC_IRQ_VID1_FIFO_UNDERFLOW	0x0400
#define DISPC_IRQ_VID1_END_WIN		0x0800
#define DISPC_IRQ_VID2_FIFO_UNDERFLOW	0x1000
#define DISPC_IRQ_VID2_END_WIN		0x2000
#define DISPC_IRQ_SYNC_LOST		0x4000
#define DISPC_IRQ_SYNC_LOST_DIGITAL	0x8000

#define DISPC_IRQ_MASK_ALL		0xffff

/* coef index i = {0, 1, 2, 3, 4, 5, 6, 7} */
#define DISPC_VID_FIR_COEF_H(n, i)	(0x00F0 + (n)*0x90 + (i)*0x8)
/* coef index i = {0, 1, 2, 3, 4, 5, 6, 7} */
#define DISPC_VID_FIR_COEF_HV(n, i)	(0x00F4 + (n)*0x90 + (i)*0x8)
/* coef index i = {0, 1, 2, 3, 4, 5, 6, 7} */
#define DISPC_VID_FIR_COEF_V(n, i)	(0x01E0 + (n)*0x20 + (i)*0x4)
/* coef index i = {0, 1, 2, 3, 4} */
#define DISPC_VID_CONV_COEF(n, i)	(0x0130 + (n)*0x90 + (i)*0x4)

#define DISPC_IRQ_MASK_ERROR		(DISPC_IRQ_GFX_FIFO_UNDERFLOW |	\
					     DISPC_IRQ_OCP_ERR | \
					     DISPC_IRQ_VID1_FIFO_UNDERFLOW | \
					     DISPC_IRQ_VID2_FIFO_UNDERFLOW | \
					     DISPC_IRQ_SYNC_LOST | \
					     DISPC_IRQ_SYNC_LOST_DIGITAL)

#define RFBI_CONTROL			0x48050040

#define MAX_PALETTE_SIZE		(256 * 16)

#define FLD_MASK(pos, len)	(((1 << len) - 1) << pos)

#define MOD_REG_FLD(reg, mask, val) \
	dispc_write_reg((reg), (dispc_read_reg(reg) & ~(mask)) | (val));

#define OMAP2_SRAM_START		0x40200000
/* Maximum size, in reality this is smaller if SRAM is partially locked. */
#define OMAP2_SRAM_SIZE			0xa0000		/* 640k */

/* We support the SDRAM / SRAM types. See OMAPFB_PLANE_MEMTYPE_* in omapfb.h */
#define DISPC_MEMTYPE_NUM		2

#define RESMAP_SIZE(_page_cnt)						\
	((_page_cnt + (sizeof(unsigned long) * 8) - 1) / 8)
#define RESMAP_PTR(_res_map, _page_nr)					\
	(((_res_map)->map) + (_page_nr) / (sizeof(unsigned long) * 8))
#define RESMAP_MASK(_page_nr)						\
	(1 << ((_page_nr) & (sizeof(unsigned long) * 8 - 1)))

struct resmap {
	unsigned long	start;
	unsigned	page_cnt;
	unsigned long	*map;
};

#define MAX_IRQ_HANDLERS            4

static struct {
	void __iomem	*base;

	struct omapfb_mem_desc	mem_desc;
	struct resmap		*res_map[DISPC_MEMTYPE_NUM];
	atomic_t		map_count[OMAPFB_PLANE_NUM];

	dma_addr_t	palette_paddr;
	void		*palette_vaddr;

	int		ext_mode;

	struct {
		u32	irq_mask;
		void	(*callback)(void *);
		void	*data;
	} irq_handlers[MAX_IRQ_HANDLERS];
	spinlock_t		lock;
	struct completion	vsync_done;
	struct completion	frame_done;

	int		fir_hinc[OMAPFB_PLANE_NUM];
	int		fir_vinc[OMAPFB_PLANE_NUM];

	struct clk	*dss_ick, *dss1_fck;
	struct clk	*dss_54m_fck;

	enum omapfb_update_mode	update_mode;
	struct omapfb_device	*fbdev;

	struct omapfb_color_key	color_key;
} dispc;

static void enable_lcd_clocks(int enable);

static void inline dss_write_reg(int idx, u32 val)
{
	omap_writel(val, DSS_BASE + idx);
}

static u32 inline dss_read_reg(int idx)
{
	return omap_readl(DSS_BASE + idx);
}

static void inline dispc_write_reg(int idx, u32 val)
{
	__raw_writel(val, dispc.base + idx);
}

static u32 inline dispc_read_reg(int idx)
{
	u32 l = __raw_readl(dispc.base + idx);
	return l;
}

static void save_dss_context(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dss_context); i++)
		dss_context[i].val = dss_read_reg(dss_context[i].reg);
}

static void restore_dss_context(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dss_context); i++)
		dss_write_reg(dss_context[i].reg, dss_context[i].val);
}

static void save_dispc_context(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dispc_context); i++)
		dispc_context[i].val = dispc_read_reg(dispc_context[i].reg);
}

static void restore_dispc_context(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dispc_context); i++)
		dispc_write_reg(dispc_context[i].reg, dispc_context[i].val);
}

static void save_gfx_context(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(gfx_context); i++)
		gfx_context[i].val = dispc_read_reg(gfx_context[i].reg);
}

static void restore_gfx_context(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(gfx_context); i++)
		dispc_write_reg(gfx_context[i].reg, gfx_context[i].val);
}

static void save_vid_context(int video_plane)
{
	int i;
	u32 base;

	if (video_plane == 1)
		base = DISPC_VID1_BASE;
	else
		base = DISPC_VID2_BASE;
	for (i = 0; i < ARRAY_SIZE(vid_context); i++)
		vid_context[i].val = dispc_read_reg(base + vid_context[i].reg);
}

static void restore_vid_context(int video_plane)
{
	u32 base;
	int i;

	if (video_plane == 1)
		base = DISPC_VID1_BASE;
	else
		base = DISPC_VID2_BASE;

	for (i = 0; i < ARRAY_SIZE(vid_context); i++)
		dispc_write_reg(base + vid_context[i].reg, vid_context[i].val);
}

static void save_vid_fir_context(int video_plane)
{
	int i;
	u32 base;

	if (video_plane == 1)
		base = DISPC_VID1_BASE;
	else
		base = DISPC_VID2_BASE;

	for (i = 0; i < 8; i++) {
		vid_fir_context[i * 2].val = dispc_read_reg(base +
				vid_fir_context[0].reg + i * 8);
		vid_fir_context[i * 2 + 1].val = dispc_read_reg(base +
				vid_fir_context[1].reg + i * 8);
	}
}

static void restore_vid_fir_context(int video_plane)
{
	int i;
	u32 base;

	if (video_plane == 1)
		base = DISPC_VID1_BASE;
	else
		base = DISPC_VID2_BASE;


	for (i = 0; i < 8; i++) {
		dispc_write_reg(base + vid_fir_context[0].reg + i * 8,
				vid_fir_context[i * 2].val);
		dispc_write_reg(base + vid_fir_context[1].reg + i * 8,
				vid_fir_context[i * 2 + 1].val);
	}
}

static void save_vid_conv_context(int video_plane)
{
	int i;
	u32 base;

	if (video_plane == 1)
		base = DISPC_VID1_BASE;
	else
		base = DISPC_VID2_BASE;


	for (i = 0; i < 5; i++) {
		vid_conv_context[i].val = dispc_read_reg(base +
				vid_conv_context[0].reg + i * 4);
	}
}

static void restore_vid_conv_context(int video_plane)
{
	int i;
	u32 base;

	if (video_plane == 1)
		base = DISPC_VID1_BASE;
	else
		base = DISPC_VID2_BASE;


	for (i = 0; i < 5; i++) {
		dispc_write_reg(base + vid_conv_context[0].reg + i * 4,
				vid_conv_context[i].val);
	}
}

static void save_all_context(void)
{
	save_dss_context();
	save_dispc_context();
	save_gfx_context();
	save_vid_context(1);
	save_vid_context(2);
	save_vid_fir_context(1);
	save_vid_fir_context(2);
	save_vid_conv_context(1);
	save_vid_conv_context(2);
}

static void restore_all_context(void)
{
	restore_dss_context();
	restore_dispc_context();
	restore_gfx_context();
	restore_vid_context(1);
	restore_vid_context(2);
	restore_vid_fir_context(1);
	restore_vid_fir_context(2);
	restore_vid_conv_context(1);
	restore_vid_conv_context(2);
}

/* Select RFBI or bypass mode */
static void enable_rfbi_mode(int enable)
{
	u32 l;

	l = dispc_read_reg(DISPC_CONTROL);
	/* Enable RFBI, GPIO0/1 */
	l &= ~((1 << 11) | (1 << 15) | (1 << 16));
	l |= enable ? (1 << 11) : 0;
	/* RFBI En: GPIO0/1=10  RFBI Dis: GPIO0/1=11 */
	l |= 1 << 15;
	l |= enable ? 0 : (1 << 16);
	dispc_write_reg(DISPC_CONTROL, l);

	/* Set bypass mode in RFBI module */
	l = __raw_readl(IO_ADDRESS(RFBI_CONTROL));
	l |= enable ? 0 : (1 << 1);
	__raw_writel(l, IO_ADDRESS(RFBI_CONTROL));
}

static void set_lcd_data_lines(int data_lines)
{
	u32 l;
	int code = 0;

	switch (data_lines) {
	case 12:
		code = 0;
		break;
	case 16:
		code = 1;
		break;
	case 18:
		code = 2;
		break;
	case 24:
		code = 3;
		break;
	default:
		BUG();
	}

	l = dispc_read_reg(DISPC_CONTROL);
	l &= ~(0x03 << 8);
	l |= code << 8;
	dispc_write_reg(DISPC_CONTROL, l);
}

static void omap_dispc_go(enum omapfb_channel_out channel_out)
{
	int bit;

	bit = channel_out == OMAPFB_CHANNEL_OUT_LCD ? (1 << 5) : (1 << 6);

	MOD_REG_FLD(DISPC_CONTROL, bit, bit);
}

static void omap_dispc_vsync_done(void *data)
{
	complete(&dispc.vsync_done);
}

/**
 * omap_dispc_sync - wait for the vsync signal
 * @channel_out: specifies whether to wait for the LCD or DIGIT out vsync
 *		 signal
 *
 * Sleeps until receiving the appropriate vsync signal. If the output is not
 * enabled return immediately.
 */
static int omap_dispc_sync(enum omapfb_channel_out channel_out)
{
	u32 irq_mask;
	u32 l;

	l = dispc_read_reg(DISPC_CONTROL);

	switch (channel_out) {
	case OMAPFB_CHANNEL_OUT_LCD:
		irq_mask = DISPC_IRQ_VSYNC;
		if (!(l & 1))
			return 0;
		break;
	case OMAPFB_CHANNEL_OUT_DIGIT:
		irq_mask = DISPC_IRQ_EVSYNC_EVEN | DISPC_IRQ_EVSYNC_ODD;
		if (!(l & (1 << 1)))
			return 0;
		break;
	default:
		return -ENODEV;
	}

	init_completion(&dispc.vsync_done);
	if (omap_dispc_request_irq(irq_mask, omap_dispc_vsync_done, NULL) < 0)
		BUG();
	if (!wait_for_completion_timeout(&dispc.vsync_done,
			msecs_to_jiffies(100))) {
		if (printk_ratelimit()) {
			dev_err(dispc.fbdev->dev,
				"timeout waiting for VSYNC\n");
		}
	}
	omap_dispc_free_irq(irq_mask, omap_dispc_vsync_done, NULL);

	return 0;
}

/**
 * omap_dispc_wait_update - wait for a pending shadow->internal register update
 * @channel_out: specifies whether to wait for LCD or DIGIT out updates to
 *		 finish.
 *
 * If there is a pending update sleep until it finishes. If output is not
 * enabled or GO bit is not set (no pending update) return imediately.
 */
static void omap_dispc_wait_update(enum omapfb_channel_out channel_out)
{
	int enable_bit;
	int go_bit;
	u32 l;
	int tmo = 100000;

	if (channel_out == OMAPFB_CHANNEL_OUT_LCD) {
		enable_bit = 1 << 0;
		go_bit = 1 << 5;
	} else {
		enable_bit = 1 << 1;
		go_bit = 1 << 6;
	}

	l = dispc_read_reg(DISPC_CONTROL);
	if (!(l & enable_bit) || !(l & go_bit))
		/* Output is disabled or GO bit is not set, so no pending
		 * updates */
		return;
	/* GO bit is set, the update will happen at the next vsync time. */
	omap_dispc_sync(channel_out);
	while (l & go_bit) {
		cpu_relax();
		if (!tmo--) {
			dev_err(dispc.fbdev->dev,
					"timeout waiting for %s\n",
					channel_out == OMAPFB_CHANNEL_OUT_LCD ?
					   "GOLCD" : "GODIGIT");
			break;
		}
		l = dispc_read_reg(DISPC_CONTROL);
	}
}

static int omap_dispc_sync_wrapper(enum omapfb_channel_out channel_out)
{
	int r;
	enable_lcd_clocks(1);
	r = omap_dispc_sync(channel_out);
	enable_lcd_clocks(0);
	return r;
}

static void set_load_mode(int mode)
{
	BUG_ON(mode & ~(DISPC_LOAD_CLUT_ONLY | DISPC_LOAD_FRAME_ONLY |
			DISPC_LOAD_CLUT_ONCE_FRAME));
	MOD_REG_FLD(DISPC_CONFIG, 0x03 << 1, mode << 1);
}

void omap_dispc_set_lcd_size(int x, int y)
{
	BUG_ON((x > (1 << 11)) || (y > (1 << 11)));
	enable_lcd_clocks(1);
	MOD_REG_FLD(DISPC_SIZE_LCD, FLD_MASK(16, 11) | FLD_MASK(0, 11),
			((y - 1) << 16) | (x - 1));
	enable_lcd_clocks(0);
}
EXPORT_SYMBOL(omap_dispc_set_lcd_size);

void omap_dispc_set_digit_size(int x, int y)
{
	BUG_ON((x > (1 << 11)) || (y > (1 << 11)));
	enable_lcd_clocks(1);
	MOD_REG_FLD(DISPC_SIZE_DIG, FLD_MASK(16, 11) | FLD_MASK(0, 11),
			((y - 1) << 16) | (x - 1));
	enable_lcd_clocks(0);
}
EXPORT_SYMBOL(omap_dispc_set_digit_size);

static void setup_plane_fifo(int plane, int ext_mode)
{
	const u32 ftrs_reg[] = { DISPC_GFX_FIFO_THRESHOLD,
				DISPC_VID1_BASE + DISPC_VID_FIFO_THRESHOLD,
			        DISPC_VID2_BASE + DISPC_VID_FIFO_THRESHOLD };
	const u32 fsz_reg[] = { DISPC_GFX_FIFO_SIZE_STATUS,
				DISPC_VID1_BASE + DISPC_VID_FIFO_SIZE_STATUS,
				DISPC_VID2_BASE + DISPC_VID_FIFO_SIZE_STATUS };
	int low, high;
	u32 l;

	BUG_ON(plane > 2);

	l = dispc_read_reg(fsz_reg[plane]);
	l &= FLD_MASK(0, 11);
	low = l * 3 / 4;
	high = l - 1;
	MOD_REG_FLD(ftrs_reg[plane], FLD_MASK(16, 12) | FLD_MASK(0, 12),
			(high << 16) | low);
}

void omap_dispc_enable_lcd_out(int enable)
{
	enable_lcd_clocks(1);
	MOD_REG_FLD(DISPC_CONTROL, 1, enable ? 1 : 0);
	enable_lcd_clocks(0);
}
EXPORT_SYMBOL(omap_dispc_enable_lcd_out);

void omap_dispc_enable_digit_out(int enable)
{
	enable_lcd_clocks(1);
	MOD_REG_FLD(DISPC_CONTROL, 1 << 1, enable ? 1 << 1 : 0);
	enable_lcd_clocks(0);
}
EXPORT_SYMBOL(omap_dispc_enable_digit_out);

extern void omap_dispc_set_plane_base(int plane, u32 paddr)
{
	u32 reg;

	enable_lcd_clocks(1);

	switch (plane) {
	case 0:
		reg = DISPC_GFX_BA0;
		break;
	case 1:
		reg = DISPC_VID1_BASE + DISPC_VID_BA0;
		break;
	case 2:
		reg = DISPC_VID2_BASE + DISPC_VID_BA0;
		break;
	default:
		BUG();
		return;
	}

	dispc_write_reg(reg, paddr);

	omap_dispc_go(OMAPFB_CHANNEL_OUT_LCD);
	enable_lcd_clocks(0);
}
EXPORT_SYMBOL(omap_dispc_set_plane_base);

static int omap_dispc_set_scale(int plane,
				int orig_width, int orig_height,
				int out_width, int out_height, int ilace);
static void setup_color_conv_coef(void);

static inline int _setup_plane(int plane, int enabled, int channel_out,
				  u32 paddr, int screen_width,
				  int pos_x, int pos_y, int width, int height,
				  int out_width, int out_height,
				  int color_mode, int rotate)
{
	const u32 at_reg[] = { DISPC_GFX_ATTRIBUTES,
				DISPC_VID1_BASE + DISPC_VID_ATTRIBUTES,
			        DISPC_VID2_BASE + DISPC_VID_ATTRIBUTES };
	const u32 ba0_reg[] = { DISPC_GFX_BA0, DISPC_VID1_BASE + DISPC_VID_BA0,
				DISPC_VID2_BASE + DISPC_VID_BA0 };
	const u32 ba1_reg[] = { DISPC_GFX_BA1, DISPC_VID1_BASE + DISPC_VID_BA1,
				DISPC_VID2_BASE + DISPC_VID_BA1 };
	const u32 ps_reg[] = { DISPC_GFX_POSITION,
				DISPC_VID1_BASE + DISPC_VID_POSITION,
				DISPC_VID2_BASE + DISPC_VID_POSITION };
	const u32 sz_reg[] = { DISPC_GFX_SIZE,
				DISPC_VID1_BASE + DISPC_VID_PICTURE_SIZE,
				DISPC_VID2_BASE + DISPC_VID_PICTURE_SIZE };
	const u32 ri_reg[] = { DISPC_GFX_ROW_INC,
				DISPC_VID1_BASE + DISPC_VID_ROW_INC,
			        DISPC_VID2_BASE + DISPC_VID_ROW_INC };
	const u32 vs_reg[] = { 0, DISPC_VID1_BASE + DISPC_VID_SIZE,
				DISPC_VID2_BASE + DISPC_VID_SIZE };

	int chout_shift, burst_shift;
	int chout_val;
	int color_code;
	int bpp;
	int cconv_en;
	u32 l;
	int ilace = channel_out == OMAPFB_CHANNEL_OUT_DIGIT;
	int fieldmode = 0;

	/*
	 * Some definitions:
	 *
	 * ilace == the actual output is interlaced, image is draw as
	 * two separate fields with other containing odd horizontal
	 * lines and the other even horizontal lines.
	 *
	 * fieldmode == the input data from framebuffer is also fed to
	 * venc as two separate fields.
	 *
	 * Why fieldmode can be disabled with interlacing?
	 *
	 * When scaling up, we must not skip any lines from the
	 * framebuffer, otherwise the scaling unit cannot interpolate
	 * missing lines properly. Furthermore, since the venc is in
	 * interlaced mode, each output field has only half of the
	 * vertical resolution, thus we may end up actually
	 * downsampling even though the original image has less
	 * physical lines than the output.
	 *
	 */

#ifdef VERBOSE
	dev_dbg(dispc.fbdev->dev, "plane %d channel %d paddr %#08x scr_width %d"
		" pos_x %d pos_y %d width %d height %d color_mode %d "
		" out_width %d out height %d "
		"interlaced %d\n",
		plane, channel_out, paddr, screen_width, pos_x, pos_y,
		width, height, color_mode, out_width, out_height, ilace);
#endif

	switch (plane) {
	case OMAPFB_PLANE_GFX:
		burst_shift = 6;
		chout_shift = 8;
		break;
	case OMAPFB_PLANE_VID1:
	case OMAPFB_PLANE_VID2:
		burst_shift = 14;
		chout_shift = 16;
		break;
	default:
		return -EINVAL;
	}

	if (!enabled) {
		/* just disable it, without configuring the rest */
		l = dispc_read_reg(at_reg[plane]);
		l &= ~1;
		dispc_write_reg(at_reg[plane], l);
		goto out;
	}

	switch (channel_out) {
	case OMAPFB_CHANNEL_OUT_LCD:
		chout_val = 0;
		break;
	case OMAPFB_CHANNEL_OUT_DIGIT:
		chout_val = 1;
		break;
	default:
		return -EINVAL;
	}

	cconv_en = 0;
	switch (color_mode) {
	case OMAPFB_COLOR_RGB565:
		color_code = DISPC_RGB_16_BPP;
		bpp = 16;
		break;
	case OMAPFB_COLOR_YUV422:
		if (plane == 0)
			return -EINVAL;
		color_code = DISPC_UYVY_422;
		cconv_en = 1;
		bpp = 16;
		break;
	case OMAPFB_COLOR_YUY422:
		if (plane == 0)
			return -EINVAL;
		color_code = DISPC_YUV2_422;
		cconv_en = 1;
		bpp = 16;
		break;
	default:
		return -EINVAL;
	}

	if (plane == OMAPFB_PLANE_GFX) {
		if (width != out_width || height != out_height)
			return -EINVAL;
	}

	if (ilace) {
		/*
		 * FIXME the downscaling ratio really isn't a good
		 * indicator whether fieldmode should be used.
		 * fieldmode should be user controllable.
		 *
		 * In general the downscaling ratio could be reduced
		 * by simply skipping some of the source lines
		 * regardless of fieldmode.
		 */
		if (height >= (out_height << (cpu_is_omap34xx() ? 1 : 0)))
			fieldmode = 1;
	}

	if (fieldmode)
		height /= 2;
	if (ilace) {
		pos_y /= 2;
		out_height /= 2;
	}

	if (plane != OMAPFB_PLANE_GFX) {
		l = omap_dispc_set_scale(plane, width, height,
					 out_width, out_height, ilace);
		if (l)
			return l;
	}

	l = dispc_read_reg(at_reg[plane]);

	l &= ~(0x0f << 1);
	l |= color_code << 1;
	l &= ~(1 << 9);
	l |= cconv_en << 9;

	l &= ~(0x03 << burst_shift);
	l |= DISPC_BURST_8x32 << burst_shift;

	l &= ~(1 << chout_shift);
	l |= chout_val << chout_shift;

	l |= 1;		/* Enable plane */

	dispc_write_reg(at_reg[plane], l);

	if (cconv_en)
		setup_color_conv_coef();

	dispc_write_reg(ba0_reg[plane], paddr);

	if (fieldmode)
		dispc_write_reg(ba1_reg[plane],
				paddr + (screen_width) * bpp / 8);
	else
		dispc_write_reg(ba1_reg[plane], paddr);

	MOD_REG_FLD(ps_reg[plane],
		    FLD_MASK(16, 11) | FLD_MASK(0, 11), (pos_y << 16) | pos_x);

	MOD_REG_FLD(sz_reg[plane], FLD_MASK(16, 11) | FLD_MASK(0, 11),
			((height - 1) << 16) | (width - 1));

	MOD_REG_FLD(vs_reg[plane], FLD_MASK(16, 11) | FLD_MASK(0, 11),
			((out_height - 1) << 16) | (out_width - 1));

	dispc_write_reg(ri_reg[plane], (screen_width - width) * bpp / 8 +
			(fieldmode ? screen_width * bpp / 8 : 0) + 1);

out:
	omap_dispc_go(channel_out);

	return height * screen_width * bpp / 8;
}

static int omap_dispc_setup_plane(int plane, int enable, int channel_out,
				  unsigned long paddr,
				  int screen_width,
				  int pos_x, int pos_y, int width, int height,
				  int out_width, int out_height,
				  int color_mode, int rotate)
{
	int r;

	if ((unsigned)plane > dispc.mem_desc.region_cnt)
		return -EINVAL;
	enable_lcd_clocks(1);
	omap_dispc_wait_update(channel_out);
	r = _setup_plane(plane, enable, channel_out, paddr,
			screen_width,
			pos_x, pos_y, width, height,
			out_width, out_height, color_mode, rotate);
	omap_dispc_wait_update(channel_out);
	enable_lcd_clocks(0);
	return r;
}

static void write_firh_reg(int plane, int reg, u32 value)
{
	dispc_write_reg(DISPC_VID_FIR_COEF_H(plane-1, reg), value);
}

static void write_firhv_reg(int plane, int reg, u32 value)
{
	dispc_write_reg(DISPC_VID_FIR_COEF_HV(plane-1, reg), value);
}

static void write_firv_reg(int plane, int reg, u32 value)
{
	dispc_write_reg(DISPC_VID_FIR_COEF_V(plane-1, reg), value);
}

static void set_sampling_coef_tables(int plane, int hscaleup, int vscaleup, int five_taps)
{
	/* Coefficients for horizontal up-sampling */
	static const u32 coef_hup[8] = {
		0x00800000,
		0x0D7CF800,
		0x1E70F5FF,
		0x335FF5FE,
		0xF74949F7,
		0xF55F33FB,
		0xF5701EFE,
		0xF87C0DFF,
	};

	/* Coefficients for horizontal down-sampling */
	static const u32 coef_hdown[8] = {
		0x24382400,
		0x28371FFE,
		0x2C361BFB,
		0x303516F9,
		0x11343311,
		0x1635300C,
		0x1B362C08,
		0x1F372804,
	};

	/* Coefficients for horizontal and vertical up-sampling */
	static const u32 coef_hvup[2][8] = {
		{
		0x00800000,
		0x037B02FF,
		0x0C6F05FE,
		0x205907FB,
		0x00404000,
		0x075920FE,
		0x056F0CFF,
		0x027B0300,
		},
		{
		0x00800000,
		0x0D7CF8FF,
		0x1E70F5FE,
		0x335FF5FB,
		0xF7404000,
		0xF55F33FE,
		0xF5701EFF,
		0xF87C0D00,
		},
	};

	/* Coefficients for horizontal and vertical down-sampling */
	static const u32 coef_hvdown[2][8] = {
		{
		0x24382400,
		0x28391F04,
		0x2D381B08,
		0x3237170C,
		0x123737F7,
		0x173732F9,
		0x1B382DFB,
		0x1F3928FE,
		},
		{
		0x24382400,
		0x28371F04,
		0x2C361B08,
		0x3035160C,
		0x113433F7,
		0x163530F9,
		0x1B362CFB,
		0x1F3728FE,
		},
	};

	/* Coefficients for vertical up-sampling */
	static const u32 coef_vup[8] = {
		0x00000000,
		0x0000FF00,
		0x0000FEFF,
		0x0000FBFE,
		0x000000F7,
		0x0000FEFB,
		0x0000FFFE,
		0x000000FF,
	};

	/* Coefficients for vertical down-sampling */
	static const u32 coef_vdown[8] = {
		0x00000000,
		0x000004FE,
		0x000008FB,
		0x00000CF9,
		0x0000F711,
		0x0000F90C,
		0x0000FB08,
		0x0000FE04,
	};

	const u32 *h_coef;
	const u32 *hv_coef;
	const u32 *hv_coef_mod;
	const u32 *v_coef;
	int i;

	if (hscaleup)
		h_coef = coef_hup;
	else
		h_coef = coef_hdown;

	if (vscaleup) {
		hv_coef = coef_hvup[five_taps];
		v_coef = coef_vup;

		if (hscaleup)
			hv_coef_mod = NULL;
		else
			hv_coef_mod = coef_hvdown[five_taps];
	} else {
		hv_coef = coef_hvdown[five_taps];
		v_coef = coef_vdown;

		if (hscaleup)
			hv_coef_mod = coef_hvup[five_taps];
		else
			hv_coef_mod = NULL;
	}

	for (i = 0; i < 8; i++) {
		u32 h, hv;

		h = h_coef[i];

		hv = hv_coef[i];

		if (hv_coef_mod) {
			hv &= 0xffffff00;
			hv |= (hv_coef_mod[i] & 0xff);
		}

		write_firh_reg(plane, i, h);
		write_firhv_reg(plane, i, hv);
	}

	if (!five_taps)
		return;

	for (i = 0; i < 8; i++) {
		u32 v;

		v = v_coef[i];

		write_firv_reg(plane, i, v);
	}
}

static int omap_dispc_set_scale(int plane,
				int orig_width, int orig_height,
				int out_width, int out_height, int ilace)
{
	const u32 at_reg[]  = { 0, DISPC_VID1_BASE + DISPC_VID_ATTRIBUTES,
				DISPC_VID2_BASE + DISPC_VID_ATTRIBUTES };
	const u32 fir_reg[] = { 0, DISPC_VID1_BASE + DISPC_VID_FIR,
				DISPC_VID2_BASE + DISPC_VID_FIR };
	const u32 accu0_reg[]  = { 0, DISPC_VID1_BASE + DISPC_VID_ACCU0,
				   DISPC_VID2_BASE + DISPC_VID_ACCU0 };
	const u32 accu1_reg[]  = { 0, DISPC_VID1_BASE + DISPC_VID_ACCU1,
				   DISPC_VID2_BASE + DISPC_VID_ACCU1 };
	u32 l;
	int fir_hinc;
	int fir_vinc;
	int hscaleup, vscaleup, five_taps;
	int accu0 = 0;
	int accu1 = 0;

	hscaleup = orig_width <= out_width;
	vscaleup = orig_height <= out_height;
	five_taps = orig_height > out_height * 2;

	if ((unsigned)plane >= OMAPFB_PLANE_NUM)
		return -ENODEV;

	if (plane == OMAPFB_PLANE_GFX)
		return 0;

	if (orig_width > (2048 >> five_taps))
		return -EINVAL;

	if (hscaleup) {
		if (orig_width * 8 < out_width)
			return -EINVAL;
	} else {
		if (!cpu_is_omap34xx() && orig_width > out_width * 2)
			return -EINVAL;
		if (orig_width > out_width * 4)
			return -EINVAL;
	}

	if (vscaleup) {
		if (orig_height * 8 < out_height)
			return -EINVAL;
	} else {
		if (!cpu_is_omap34xx() && orig_height > out_height * 2)
			return -EINVAL;
		if (orig_height > out_height * 4)
			return -EINVAL;
	}

	if (!orig_width || orig_width == out_width)
		fir_hinc = 0;
	else
		fir_hinc = 1024 * orig_width / out_width;

	if (!orig_height || orig_height == out_height)
		fir_vinc = 0;
	else
		fir_vinc = 1024 * orig_height / out_height;

	if (ilace) {
		accu0 = 0;
		accu1 = fir_vinc / 2;
		if (accu1 >= 1024 / 2) {
			accu0 = 1024 / 2;
			accu1 -= accu0;
		}
	}

	enable_lcd_clocks(1);

	set_sampling_coef_tables(plane, hscaleup, vscaleup, five_taps);

	dispc_write_reg(accu0_reg[plane], FLD_MASK(16, 26) || (accu0 << 16));
	dispc_write_reg(accu1_reg[plane], FLD_MASK(16, 26) || (accu1 << 16));

	dispc.fir_hinc[plane] = fir_hinc;
	dispc.fir_vinc[plane] = fir_vinc;

	MOD_REG_FLD(fir_reg[plane],
		    FLD_MASK(16, 12) | FLD_MASK(0, 12),
		    ((fir_vinc & 4095) << 16) |
		    (fir_hinc & 4095));

	dev_dbg(dispc.fbdev->dev, "out_width %d out_height %d orig_width %d "
		"orig_height %d fir_hinc  %d fir_vinc %d\n",
		out_width, out_height, orig_width, orig_height,
		fir_hinc, fir_vinc);

	l = dispc_read_reg(at_reg[plane]);
	l &= ~(0x03 << 5);
	l |= fir_hinc ? (1 << 5) : 0;
	l |= fir_vinc ? (1 << 6) : 0;
	l &= ~(0x3 << 7);
	l |= hscaleup ? 0 : (1 << 7);
	l |= vscaleup ? 0 : (1 << 8);
	l &= ~(0x3 << 21);
	l |= five_taps ? (1 << 21) : 0;
	l |= five_taps ? (1 << 22) : 0;
	dispc_write_reg(at_reg[plane], l);

	enable_lcd_clocks(0);
	return 0;
}

static int omap_dispc_set_color_key(struct omapfb_color_key *ck)
{
	u32 df_reg, tr_reg;
	int shift, val;

	switch (ck->channel_out) {
	case OMAPFB_CHANNEL_OUT_LCD:
		df_reg = DISPC_DEFAULT_COLOR0;
		tr_reg = DISPC_TRANS_COLOR0;
		shift = 10;
		break;
	case OMAPFB_CHANNEL_OUT_DIGIT:
		df_reg = DISPC_DEFAULT_COLOR1;
		tr_reg = DISPC_TRANS_COLOR1;
		shift = 12;
		break;
	default:
		return -EINVAL;
	}
	switch (ck->key_type) {
	case OMAPFB_COLOR_KEY_DISABLED:
		val = 0;
		break;
	case OMAPFB_COLOR_KEY_GFX_DST:
		val = 1;
		break;
	case OMAPFB_COLOR_KEY_VID_SRC:
		val = 3;
		break;
	default:
		return -EINVAL;
	}
	enable_lcd_clocks(1);
	MOD_REG_FLD(DISPC_CONFIG, FLD_MASK(shift, 2), val << shift);

	if (val != 0)
		dispc_write_reg(tr_reg, ck->trans_key);
	dispc_write_reg(df_reg, ck->background);

	omap_dispc_go(ck->channel_out);
	enable_lcd_clocks(0);

	dispc.color_key = *ck;

	return 0;
}

static int omap_dispc_get_color_key(struct omapfb_color_key *ck)
{
	*ck = dispc.color_key;
	return 0;
}

static void load_palette(void)
{
}

static void omap_dispc_frame_done(void *data)
{
	complete(&dispc.frame_done);
}

static int omap_dispc_set_update_mode(enum omapfb_update_mode mode)
{
	int r = 0;

	if (mode != dispc.update_mode) {
		switch (mode) {
		case OMAPFB_AUTO_UPDATE:
		case OMAPFB_MANUAL_UPDATE:
			enable_lcd_clocks(1);
			omap_dispc_enable_lcd_out(1);
			dispc.update_mode = mode;
			break;
		case OMAPFB_UPDATE_DISABLED:
			init_completion(&dispc.frame_done);
			if (omap_dispc_request_irq(DISPC_IRQ_FRAMEMASK,
					omap_dispc_frame_done, NULL) < 0)
				BUG();
			omap_dispc_enable_lcd_out(0);
			if (!wait_for_completion_timeout(&dispc.frame_done,
					msecs_to_jiffies(500))) {
				dev_err(dispc.fbdev->dev,
					 "timeout waiting for FRAME DONE\n");
			}
			omap_dispc_free_irq(DISPC_IRQ_FRAMEMASK,
					omap_dispc_frame_done, NULL);
			dispc.update_mode = mode;
			enable_lcd_clocks(0);
			break;
		default:
			r = -EINVAL;
		}
	}

	return r;
}

static void omap_dispc_get_caps(int plane, struct omapfb_caps *caps)
{
	caps->ctrl |= OMAPFB_CAPS_PLANE_RELOCATE_MEM;
	if (plane > 0)
		caps->ctrl |= OMAPFB_CAPS_PLANE_SCALE;
	caps->plane_color |= (1 << OMAPFB_COLOR_RGB565) |
			     (1 << OMAPFB_COLOR_YUV422) |
			     (1 << OMAPFB_COLOR_YUY422);
	if (plane == 0)
		caps->plane_color |= (1 << OMAPFB_COLOR_CLUT_8BPP) |
				     (1 << OMAPFB_COLOR_CLUT_4BPP) |
				     (1 << OMAPFB_COLOR_CLUT_2BPP) |
				     (1 << OMAPFB_COLOR_CLUT_1BPP) |
				     (1 << OMAPFB_COLOR_RGB444);
}

static enum omapfb_update_mode omap_dispc_get_update_mode(void)
{
	return dispc.update_mode;
}

static void setup_color_conv_coef(void)
{
	u32 mask = FLD_MASK(16, 11) | FLD_MASK(0, 11);
	int cf1_reg = DISPC_VID1_BASE + DISPC_VID_CONV_COEF0;
	int cf2_reg = DISPC_VID2_BASE + DISPC_VID_CONV_COEF0;
	int at1_reg = DISPC_VID1_BASE + DISPC_VID_ATTRIBUTES;
	int at2_reg = DISPC_VID2_BASE + DISPC_VID_ATTRIBUTES;
	const struct color_conv_coef {
		int  ry,  rcr,  rcb,   gy,  gcr,  gcb,   by,  bcr,  bcb;
		int  full_range;
	}  ctbl_bt601_5 = {
		    298,  409,    0,  298, -208, -100,  298,    0,  517, 0,
	};
	const struct color_conv_coef *ct;
#define CVAL(x, y)	(((x & 2047) << 16) | (y & 2047))

	ct = &ctbl_bt601_5;

	MOD_REG_FLD(cf1_reg,		mask,	CVAL(ct->rcr, ct->ry));
	MOD_REG_FLD(cf1_reg + 4,	mask,	CVAL(ct->gy,  ct->rcb));
	MOD_REG_FLD(cf1_reg + 8,	mask,	CVAL(ct->gcb, ct->gcr));
	MOD_REG_FLD(cf1_reg + 12,	mask,	CVAL(ct->bcr, ct->by));
	MOD_REG_FLD(cf1_reg + 16,	mask,	CVAL(0,	      ct->bcb));

	MOD_REG_FLD(cf2_reg,		mask,	CVAL(ct->rcr, ct->ry));
	MOD_REG_FLD(cf2_reg + 4,	mask,	CVAL(ct->gy,  ct->rcb));
	MOD_REG_FLD(cf2_reg + 8,	mask,	CVAL(ct->gcb, ct->gcr));
	MOD_REG_FLD(cf2_reg + 12,	mask,	CVAL(ct->bcr, ct->by));
	MOD_REG_FLD(cf2_reg + 16,	mask,	CVAL(0,	      ct->bcb));
#undef CVAL

	MOD_REG_FLD(at1_reg, (1 << 11), ct->full_range);
	MOD_REG_FLD(at2_reg, (1 << 11), ct->full_range);
}

static void calc_ck_div(int is_tft, int pck, int *lck_div, int *pck_div)
{
	unsigned long fck, lck;

	*lck_div = 1;
	pck = max(1, pck);
	fck = clk_get_rate(dispc.dss1_fck);
	lck = fck;
	*pck_div = (lck + pck - 1) / pck;
	if (is_tft)
		*pck_div = max(2, *pck_div);
	else
		*pck_div = max(3, *pck_div);
	if (*pck_div > 255) {
		*pck_div = 255;
		lck = pck * *pck_div;
		*lck_div = fck / lck;
		BUG_ON(*lck_div < 1);
		if (*lck_div > 255) {
			*lck_div = 255;
			dev_warn(dispc.fbdev->dev, "pixclock %d kHz too low.\n",
				 pck / 1000);
		}
	}
}

static void set_lcd_tft_mode(int enable)
{
	u32 mask;

	mask = 1 << 3;
	MOD_REG_FLD(DISPC_CONTROL, mask, enable ? mask : 0);
}

static void set_lcd_timings(void)
{
	u32 l;
	int lck_div, pck_div;
	struct lcd_panel *panel = dispc.fbdev->lcd_panel;
	int is_tft = panel->config & OMAP_LCDC_PANEL_TFT;
	unsigned long fck;

	l = dispc_read_reg(DISPC_TIMING_H);
	l &= ~(FLD_MASK(0, 6) | FLD_MASK(8, 8) | FLD_MASK(20, 8));
	l |= ( max(1, (min(64,  panel->hsw))) - 1 ) << 0;
	l |= ( max(1, (min(256, panel->hfp))) - 1 ) << 8;
	l |= ( max(1, (min(256, panel->hbp))) - 1 ) << 20;
	dispc_write_reg(DISPC_TIMING_H, l);

	l = dispc_read_reg(DISPC_TIMING_V);
	l &= ~(FLD_MASK(0, 6) | FLD_MASK(8, 8) | FLD_MASK(20, 8));
	l |= ( max(1, (min(64,  panel->vsw))) - 1 ) << 0;
	l |= ( max(0, (min(255, panel->vfp))) - 0 ) << 8;
	l |= ( max(0, (min(255, panel->vbp))) - 0 ) << 20;
	dispc_write_reg(DISPC_TIMING_V, l);

	l = dispc_read_reg(DISPC_POL_FREQ);
	l &= ~FLD_MASK(12, 6);
	l |= (panel->config & OMAP_LCDC_SIGNAL_MASK) << 12;
	l |= panel->acb & 0xff;
	dispc_write_reg(DISPC_POL_FREQ, l);

	calc_ck_div(is_tft, panel->pixel_clock * 1000, &lck_div, &pck_div);

	l = dispc_read_reg(DISPC_DIVISOR);
	l &= ~(FLD_MASK(16, 8) | FLD_MASK(0, 8));
	l |= (lck_div << 16) | (pck_div << 0);
	dispc_write_reg(DISPC_DIVISOR, l);

	/* update panel info with the exact clock */
	fck = clk_get_rate(dispc.dss1_fck);
	panel->pixel_clock = fck / lck_div / pck_div / 1000;
}

/**
 * _recalc_irq_mask - calculate the new set of enabled IRQs
 *
 * Calculate the new set of enabled IRQs which is a combination of all
 * handlers' IRQs.
 *
 * dispc.lock must be held.
 */
static void _recalc_irq_mask(void)
{
	int i;
	unsigned long irq_mask = DISPC_IRQ_MASK_ERROR;

	for (i = 0; i < MAX_IRQ_HANDLERS; i++) {
		if (!dispc.irq_handlers[i].callback)
			continue;

		irq_mask |= dispc.irq_handlers[i].irq_mask;
	}

	enable_lcd_clocks(1);
	MOD_REG_FLD(DISPC_IRQENABLE, DISPC_IRQ_MASK_ALL, irq_mask);
	enable_lcd_clocks(0);
}

static void recalc_irq_mask(void)
{
	unsigned long flags;

	spin_lock_irqsave(&dispc.lock, flags);
	_recalc_irq_mask();
	spin_unlock_irqrestore(&dispc.lock, flags);
}

static inline void _clear_irq(u32 irq_mask)
{
	dispc_write_reg(DISPC_IRQSTATUS, irq_mask);
}

static inline void clear_irq(u32 irq_mask)
{
	enable_lcd_clocks(1);
	_clear_irq(irq_mask);
	enable_lcd_clocks(0);
}

int omap_dispc_request_irq(unsigned long irq_mask, void (*callback)(void *data),
			   void *data)
{
	unsigned long flags;
	int i;

	BUG_ON(callback == NULL);

	spin_lock_irqsave(&dispc.lock, flags);
	for (i = 0; i < MAX_IRQ_HANDLERS; i++) {
		if (dispc.irq_handlers[i].callback)
			continue;

		dispc.irq_handlers[i].irq_mask = irq_mask;
		dispc.irq_handlers[i].callback = callback;
		dispc.irq_handlers[i].data = data;
		clear_irq(irq_mask);
		_recalc_irq_mask();

		spin_unlock_irqrestore(&dispc.lock, flags);
		return 0;
	}

	spin_unlock_irqrestore(&dispc.lock, flags);
	return -EBUSY;
}
EXPORT_SYMBOL(omap_dispc_request_irq);

void omap_dispc_free_irq(unsigned long irq_mask, void (*callback)(void *data),
			 void *data)
{
	int i;
	unsigned long flags;

	spin_lock_irqsave(&dispc.lock, flags);
	for (i = 0; i < MAX_IRQ_HANDLERS; i++) {
		if (dispc.irq_handlers[i].callback == callback &&
		    dispc.irq_handlers[i].data == data) {
			dispc.irq_handlers[i].irq_mask = 0;
			dispc.irq_handlers[i].callback = NULL;
			dispc.irq_handlers[i].data = NULL;
			_recalc_irq_mask();

			spin_unlock_irqrestore(&dispc.lock, flags);
			return;
		}
	}
	spin_unlock_irqrestore(&dispc.lock, flags);

	BUG();
}
EXPORT_SYMBOL(omap_dispc_free_irq);

static irqreturn_t omap_dispc_irq_handler(int irq, void *dev)
{
	u32 stat;
	int i = 0;

	enable_lcd_clocks(1);

	stat = dispc_read_reg(DISPC_IRQSTATUS);

	if (stat & DISPC_IRQ_MASK_ERROR) {
		if (printk_ratelimit()) {
			dev_err(dispc.fbdev->dev, "irq error status %04x\n",
				stat & DISPC_IRQ_MASK_ALL);
		}
	}

	spin_lock(&dispc.lock);
	for (i = 0; i < MAX_IRQ_HANDLERS; i++) {
		void (*cb)(void *) = dispc.irq_handlers[i].callback;
		unsigned long cb_irqs = dispc.irq_handlers[i].irq_mask;
		void *cb_data = dispc.irq_handlers[i].data;

		spin_unlock(&dispc.lock);
		if (unlikely(cb != NULL && (stat & cb_irqs)))
			cb(cb_data);
		spin_lock(&dispc.lock);
	}
	spin_unlock(&dispc.lock);

	dispc_write_reg(DISPC_IRQSTATUS, stat);

	enable_lcd_clocks(0);

	return IRQ_HANDLED;
}

static int get_dss_clocks(void)
{
	char *dss_ick = "dss_ick";
	char *dss1_fck = cpu_is_omap34xx() ? "dss1_alwon_fck" : "dss1_fck";
	char *tv_fck = cpu_is_omap34xx() ? "dss_tv_fck" : "dss_54m_fck";

	if (IS_ERR((dispc.dss_ick = clk_get(dispc.fbdev->dev, dss_ick)))) {
		dev_err(dispc.fbdev->dev, "can't get %s", dss_ick);
		return PTR_ERR(dispc.dss_ick);
	}

	if (IS_ERR((dispc.dss1_fck = clk_get(dispc.fbdev->dev, dss1_fck)))) {
		dev_err(dispc.fbdev->dev, "can't get %s", dss1_fck);
		clk_put(dispc.dss_ick);
		return PTR_ERR(dispc.dss1_fck);
	}

	if (IS_ERR((dispc.dss_54m_fck =
				clk_get(dispc.fbdev->dev, tv_fck)))) {
		dev_err(dispc.fbdev->dev, "can't get %s", tv_fck);
		clk_put(dispc.dss_ick);
		clk_put(dispc.dss1_fck);
		return PTR_ERR(dispc.dss_54m_fck);
	}

	return 0;
}

static void put_dss_clocks(void)
{
	clk_put(dispc.dss_54m_fck);
	clk_put(dispc.dss1_fck);
	clk_put(dispc.dss_ick);
}

static void enable_lcd_clocks(int enable)
{
	if (enable) {
		clk_enable(dispc.dss_ick);
		clk_enable(dispc.dss1_fck);
		if (dispc.dss1_fck->usecount == 1)
			restore_all_context();
	} else {
		if (dispc.dss1_fck->usecount == 1)
			save_all_context();
		clk_disable(dispc.dss1_fck);
		clk_disable(dispc.dss_ick);
	}
}

static void enable_digit_clocks(int enable)
{
	if (enable)
		clk_enable(dispc.dss_54m_fck);
	else
		clk_disable(dispc.dss_54m_fck);
}

#ifdef DEBUG
static void omap_dispc_dump(void)
{
	int i;

	static const struct {
		const char	*name;
		int		idx;
	} dss_regs[] = {
		{ "DSS_SYSCONFIG", 			0x0010},
		{ "DSS_CONTROL", 				0x0040},
		{ "DSS_SDI_CONTROL", 				0x0044},
		{ "DSS_PLL_CONTROL", 				0x0048},
		{ "DSS_SDI_STATUS", 				0x005c},
	};

	static const struct {
		const char	*name;
		int		idx;
	} dispc_regs[] = {
		{ "DISPC_REVISION", 				0x0000},
		{ "DISPC_SYSCONFIG", 				0x0010},
		{ "DISPC_SYSSTATUS", 				0x0014},
		{ "DISPC_IRQSTATUS", 				0x0018},
		{ "DISPC_IRQENABLE", 				0x001C},
		{ "DISPC_CONTROL", 				0x0040},
		{ "DISPC_CONFIG", 				0x0044},
		{ "DISPC_CAPABLE", 				0x0048},
		{ "DISPC_DEFAULT_COLOR0", 			0x004C},
		{ "DISPC_DEFAULT_COLOR1", 			0x0050},
		{ "DISPC_TRANS_COLOR0", 			0x0054},
		{ "DISPC_TRANS_COLOR1", 			0x0058},
		{ "DISPC_LINE_STATUS", 			0x005C},
		{ "DISPC_LINE_NUMBER", 			0x0060},
		{ "DISPC_TIMING_H", 				0x0064},
		{ "DISPC_TIMING_V", 				0x0068},
		{ "DISPC_POL_FREQ", 				0x006C},
		{ "DISPC_DIVISOR", 				0x0070},
		{ "DISPC_SIZE_DIG", 				0x0078},
		{ "DISPC_SIZE_LCD", 				0x007C},
		{ "DISPC_DATA_CYCLE1", 			0x01D4},
		{ "DISPC_DATA_CYCLE2", 			0x01D8},
		{ "DISPC_DATA_CYCLE3", 			0x01DC},
		{ "DISPC_GFX_BA0", 				0x0080},
		{ "DISPC_GFX_BA1", 				0x0084},
		{ "DISPC_GFX_POSITION", 			0x0088},
		{ "DISPC_GFX_SIZE", 				0x008C},
		{ "DISPC_GFX_ATTRIBUTES", 			0x00A0},
		{ "DISPC_GFX_FIFO_THRESHOLD", 		0x00A4},
		{ "DISPC_GFX_FIFO_SIZE_STATUS", 		0x00A8},
		{ "DISPC_GFX_ROW_INC", 			0x00AC},
		{ "DISPC_GFX_PIXEL_INC", 			0x00B0},
		{ "DISPC_GFX_WINDOW_SKIP", 			0x00B4},
		{ "DISPC_GFX_TABLE_BA", 			0x00B8},
	};
	for (i = 0; i < ARRAY_SIZE(dss_regs); i++) {
		printk(KERN_DEBUG "%-20s: %08x\n",
			dss_regs[i].name, dss_read_reg(dss_regs[i].idx));
	}

	for (i = 0; i < ARRAY_SIZE(dispc_regs); i++) {
		printk(KERN_DEBUG "%-20s: %08x\n",
			dispc_regs[i].name, dispc_read_reg(dispc_regs[i].idx));
	}
}
#else
static inline void omap_dispc_dump(void)
{
}
#endif

static void omap_dispc_suspend(void)
{
	if (dispc.update_mode == OMAPFB_AUTO_UPDATE) {
		omap_dispc_dump();
		init_completion(&dispc.frame_done);
		if (omap_dispc_request_irq(DISPC_IRQ_FRAMEMASK,
					omap_dispc_frame_done, NULL) < 0)
			BUG();
		omap_dispc_enable_lcd_out(0);

		if (!wait_for_completion_timeout(&dispc.frame_done,
				msecs_to_jiffies(500))) {
			dev_err(dispc.fbdev->dev,
				"timeout waiting for FRAME DONE\n");
		}
		omap_dispc_free_irq(DISPC_IRQ_FRAMEMASK,
					omap_dispc_frame_done, NULL);
		enable_lcd_clocks(0);
	}
}

static void sdi_enable(void);

static void omap_dispc_resume(void)
{
	if (dispc.update_mode == OMAPFB_AUTO_UPDATE) {
		enable_lcd_clocks(1);
		if (!dispc.ext_mode) {
			set_lcd_timings();
			load_palette();
		}
		sdi_enable();
		omap_dispc_enable_lcd_out(1);
		omap_dispc_dump();
	}
}

static int omap_dispc_update_window(struct fb_info *fbi,
				 struct omapfb_update_window *win,
				 void (*complete_callback)(void *arg),
				 void *complete_callback_data)
{
	return dispc.update_mode == OMAPFB_UPDATE_DISABLED ? -ENODEV : 0;
}

static int mmap_kern(struct omapfb_mem_region *region)
{
	struct vm_struct	*kvma;
	struct vm_area_struct	vma;
	pgprot_t		pgprot;
	unsigned long		vaddr;

	kvma = get_vm_area(region->size, VM_IOREMAP);
	if (kvma == NULL) {
		dev_err(dispc.fbdev->dev, "can't get kernel vm area\n");
		return -ENOMEM;
	}
	vma.vm_mm = &init_mm;

	vaddr = (unsigned long)kvma->addr;

	pgprot = pgprot_writecombine(pgprot_kernel);
	vma.vm_start = vaddr;
	vma.vm_end = vaddr + region->size;
	if (io_remap_pfn_range(&vma, vaddr, region->paddr >> PAGE_SHIFT,
			   region->size, pgprot) < 0) {
		dev_err(dispc.fbdev->dev, "kernel mmap for FBMEM failed\n");
		return -EAGAIN;
	}
	region->vaddr = (void *)vaddr;

	return 0;
}

static void mmap_user_open(struct vm_area_struct *vma)
{
	int plane = (int)vma->vm_private_data;

	atomic_inc(&dispc.map_count[plane]);
}

static void mmap_user_close(struct vm_area_struct *vma)
{
	int plane = (int)vma->vm_private_data;

	atomic_dec(&dispc.map_count[plane]);
}

static struct vm_operations_struct mmap_user_ops = {
	.open = mmap_user_open,
	.close = mmap_user_close,
};

static int omap_dispc_mmap_user(struct fb_info *info,
				struct vm_area_struct *vma)
{
	struct omapfb_plane_struct *plane = info->par;
	unsigned long off;
	unsigned long start;
	u32 len;

	if (vma->vm_end - vma->vm_start == 0)
		return 0;
	if (vma->vm_pgoff > (~0UL >> PAGE_SHIFT))
		return -EINVAL;
	off = vma->vm_pgoff << PAGE_SHIFT;

	start = info->fix.smem_start;
	len = info->fix.smem_len;
	if (off >= len)
		return -EINVAL;
	if ((vma->vm_end - vma->vm_start + off) > len)
		return -EINVAL;
	off += start;
	vma->vm_pgoff = off >> PAGE_SHIFT;
	vma->vm_flags |= VM_IO | VM_RESERVED;
	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	vma->vm_ops = &mmap_user_ops;
	vma->vm_private_data = (void *)plane->idx;
	if (io_remap_pfn_range(vma, vma->vm_start, off >> PAGE_SHIFT,
			     vma->vm_end - vma->vm_start, vma->vm_page_prot))
		return -EAGAIN;
	/* vm_ops.open won't be called for mmap itself. */
	atomic_inc(&dispc.map_count[plane->idx]);
	return 0;
}

static void unmap_kern(struct omapfb_mem_region *region)
{
	vunmap(region->vaddr);
}

static int alloc_palette_ram(void)
{
	dispc.palette_vaddr = dma_alloc_writecombine(dispc.fbdev->dev,
		MAX_PALETTE_SIZE, &dispc.palette_paddr, GFP_KERNEL);
	if (dispc.palette_vaddr == NULL) {
		dev_err(dispc.fbdev->dev, "failed to alloc palette memory\n");
		return -ENOMEM;
	}

	return 0;
}

static void free_palette_ram(void)
{
	dma_free_writecombine(dispc.fbdev->dev, MAX_PALETTE_SIZE,
			dispc.palette_vaddr, dispc.palette_paddr);
}

static int alloc_fbmem(struct omapfb_mem_region *region)
{
	region->vaddr = dma_alloc_writecombine(dispc.fbdev->dev,
			region->size, &region->paddr, GFP_KERNEL);

	if (region->vaddr == NULL) {
		dev_err(dispc.fbdev->dev, "unable to allocate FB DMA memory\n");
		return -ENOMEM;
	}

	return 0;
}

static void free_fbmem(struct omapfb_mem_region *region)
{
	dma_free_writecombine(dispc.fbdev->dev, region->size,
			      region->vaddr, region->paddr);
}

static struct resmap *init_resmap(unsigned long start, size_t size)
{
	unsigned page_cnt;
	struct resmap *res_map;

	page_cnt = PAGE_ALIGN(size) / PAGE_SIZE;
	res_map =
	    kzalloc(sizeof(struct resmap) + RESMAP_SIZE(page_cnt), GFP_KERNEL);
	if (res_map == NULL)
		return NULL;
	res_map->start = start;
	res_map->page_cnt = page_cnt;
	res_map->map = (unsigned long *)(res_map + 1);
	return res_map;
}

static void cleanup_resmap(struct resmap *res_map)
{
	kfree(res_map);
}

static inline int resmap_mem_type(unsigned long start)
{
	if (start >= OMAP2_SRAM_START &&
	    start < OMAP2_SRAM_START + OMAP2_SRAM_SIZE)
		return OMAPFB_MEMTYPE_SRAM;
	else
		return OMAPFB_MEMTYPE_SDRAM;
}

static inline int resmap_page_reserved(struct resmap *res_map, unsigned page_nr)
{
	return *RESMAP_PTR(res_map, page_nr) & RESMAP_MASK(page_nr) ? 1 : 0;
}

static inline void resmap_reserve_page(struct resmap *res_map, unsigned page_nr)
{
	BUG_ON(resmap_page_reserved(res_map, page_nr));
	*RESMAP_PTR(res_map, page_nr) |= RESMAP_MASK(page_nr);
}

static inline void resmap_free_page(struct resmap *res_map, unsigned page_nr)
{
	BUG_ON(!resmap_page_reserved(res_map, page_nr));
	*RESMAP_PTR(res_map, page_nr) &= ~RESMAP_MASK(page_nr);
}

static void resmap_reserve_region(unsigned long start, size_t size)
{

	struct resmap	*res_map;
	unsigned	start_page;
	unsigned	end_page;
	int		mtype;
	unsigned	i;

	mtype = resmap_mem_type(start);
	res_map = dispc.res_map[mtype];
	dev_dbg(dispc.fbdev->dev, "reserve mem type %d start %08lx size %d\n",
		mtype, start, size);
	start_page = (start - res_map->start) / PAGE_SIZE;
	end_page = start_page + PAGE_ALIGN(size) / PAGE_SIZE;
	for (i = start_page; i < end_page; i++)
		resmap_reserve_page(res_map, i);
}

static void resmap_free_region(unsigned long start, size_t size)
{
	struct resmap	*res_map;
	unsigned	start_page;
	unsigned	end_page;
	unsigned	i;
	int		mtype;

	mtype = resmap_mem_type(start);
	res_map = dispc.res_map[mtype];
	dev_dbg(dispc.fbdev->dev, "free mem type %d start %08lx size %d\n",
		mtype, start, size);
	start_page = (start - res_map->start) / PAGE_SIZE;
	end_page = start_page + PAGE_ALIGN(size) / PAGE_SIZE;
	for (i = start_page; i < end_page; i++)
		resmap_free_page(res_map, i);
}

static unsigned long resmap_alloc_region(int mtype, size_t size)
{
	unsigned i;
	unsigned total;
	unsigned start_page;
	unsigned long start;
	struct resmap *res_map = dispc.res_map[mtype];

	BUG_ON(mtype >= DISPC_MEMTYPE_NUM || res_map == NULL || !size);

	size = PAGE_ALIGN(size) / PAGE_SIZE;
	start_page = 0;
	total = 0;
	for (i = 0; i < res_map->page_cnt; i++) {
		if (resmap_page_reserved(res_map, i)) {
			start_page = i + 1;
			total = 0;
		} else if (++total == size)
			break;
	}
	if (total < size)
		return 0;

	start = res_map->start + start_page * PAGE_SIZE;
	resmap_reserve_region(start, size * PAGE_SIZE);

	return start;
}

/* Note that this will only work for user mappings, we don't deal with
 * kernel mappings here, so fbcon will keep using the old region.
 */
static int omap_dispc_setup_mem(int plane, size_t size, int mem_type,
				unsigned long *paddr)
{
	struct omapfb_mem_region *rg;
	unsigned long new_addr = 0;

	if ((unsigned)plane > dispc.mem_desc.region_cnt)
		return -EINVAL;
	if (mem_type >= DISPC_MEMTYPE_NUM)
		return -EINVAL;
	if (dispc.res_map[mem_type] == NULL)
		return -ENOMEM;
	rg = &dispc.mem_desc.region[plane];
	if (size == rg->size && mem_type == rg->type)
		return 0;
	if (atomic_read(&dispc.map_count[plane]))
		return -EBUSY;
	if (rg->size != 0)
		resmap_free_region(rg->paddr, rg->size);
	if (size != 0) {
		new_addr = resmap_alloc_region(mem_type, size);
		if (!new_addr) {
			/* Reallocate old region. */
			resmap_reserve_region(rg->paddr, rg->size);
			return -ENOMEM;
		}
	}
	rg->paddr = new_addr;
	rg->size = size;
	rg->type = mem_type;

	*paddr = new_addr;

	return 0;
}

static int setup_fbmem(struct omapfb_mem_desc *req_md)
{
	struct omapfb_mem_region	*rg;
	int i;
	int r;
	unsigned long			mem_start[DISPC_MEMTYPE_NUM];
	unsigned long			mem_end[DISPC_MEMTYPE_NUM];

	if (!req_md->region_cnt) {
		dev_err(dispc.fbdev->dev, "no memory regions defined\n");
		return -ENOENT;
	}

	rg = &req_md->region[0];
	memset(mem_start, 0xff, sizeof(mem_start));
	memset(mem_end, 0, sizeof(mem_end));

	for (i = 0; i < req_md->region_cnt; i++, rg++) {
		int mtype;
		if (rg->paddr) {
			rg->alloc = 0;
			if (rg->vaddr == NULL) {
				rg->map = 1;
				if ((r = mmap_kern(rg)) < 0)
					return r;
			}
		} else {
			if (rg->type != OMAPFB_MEMTYPE_SDRAM) {
				dev_err(dispc.fbdev->dev,
					"unsupported memory type\n");
				return -EINVAL;
			}
			rg->alloc = rg->map = 1;
			if ((r = alloc_fbmem(rg)) < 0)
				return r;
		}
		mtype = rg->type;

		if (rg->paddr < mem_start[mtype])
			mem_start[mtype] = rg->paddr;
		if (rg->paddr + rg->size > mem_end[mtype])
			mem_end[mtype] = rg->paddr + rg->size;
	}

	for (i = 0; i < DISPC_MEMTYPE_NUM; i++) {
		unsigned long start;
		size_t size;
		if (mem_end[i] == 0)
			continue;
		start = mem_start[i];
		size = mem_end[i] - start;
		dispc.res_map[i] = init_resmap(start, size);
		r = -ENOMEM;
		if (dispc.res_map[i] == NULL)
			goto fail;
		/* Initial state is that everything is reserved. This
		 * includes possible holes as well, which will never be
		 * freed.
		 */
		resmap_reserve_region(start, size);
	}

	dispc.mem_desc = *req_md;

	return 0;
fail:
	for (i = 0; i < DISPC_MEMTYPE_NUM; i++) {
		if (dispc.res_map[i] != NULL)
			cleanup_resmap(dispc.res_map[i]);
	}
	return r;
}

static void cleanup_fbmem(void)
{
	struct omapfb_mem_region *rg;
	int i;

	for (i = 0; i < DISPC_MEMTYPE_NUM; i++) {
		if (dispc.res_map[i] != NULL)
			cleanup_resmap(dispc.res_map[i]);
	}
	rg = &dispc.mem_desc.region[0];
	for (i = 0; i < dispc.mem_desc.region_cnt; i++, rg++) {
		if (rg->alloc)
			free_fbmem(rg);
		else {
			if (rg->map)
				unmap_kern(rg);
		}
	}
}


#ifdef CONFIG_FB_OMAP_VENC
void omap_dispc_set_venc_clocks(void)
{
	unsigned int l;
	enable_lcd_clocks(1);

	l = dss_read_reg(DSS_CONTROL);
	l |= 1 << 4;	/* venc dac demen */
	l |= 1 << 3;	/* venc clock 4x enable */
	l &= ~3;	/* venc clock mode */
	dss_write_reg(DSS_CONTROL, l);

	enable_lcd_clocks(0);
}

void omap_dispc_set_venc_output(enum omap_dispc_venc_type type)
{
	unsigned int l;

	/* venc out selection. 0 = comp, 1 = svideo */
	if (type == OMAP_DISPC_VENC_TYPE_COMPOSITE)
		l = 0;
	else if (type == OMAP_DISPC_VENC_TYPE_SVIDEO)
		l = 1 << 6;
	else
		BUG();

	enable_lcd_clocks(1);
	l = dss_read_reg(DSS_CONTROL);
	l = (l & ~(1 << 6)) | l;
	dss_write_reg(DSS_CONTROL, l);
	enable_lcd_clocks(0);
}

void omap_dispc_set_dac_pwrdn_bgz(int enable)
{
	int l;

	enable_lcd_clocks(1);
	/* DAC Power-Down Control */
	l = dss_read_reg(DSS_CONTROL);
	l = (l & ~(1 << 5)) | (enable ? 1 << 5 : 0);
	dss_write_reg(DSS_CONTROL, l);
	enable_lcd_clocks(0);
}
#endif

static void sdi_init(void)
{
	u32 l;

	l = dss_read_reg(DSS_SDI_CONTROL);
	l |= (0xF << 15) | (0x1 << 2) | (0x2 << 0);
	dss_write_reg(DSS_SDI_CONTROL, l);

	l = dss_read_reg(DSS_PLL_CONTROL);
	/* FSEL | NDIV | MDIV */
	l |= (0x7 << 22) | (0xB << 11) | (0xB4 << 1);
	dss_write_reg(DSS_PLL_CONTROL, l);

	/* Reset SDI PLL */
	l |= (1 << 18);
	dss_write_reg(DSS_PLL_CONTROL, l);
	udelay(1);

	/* Lock SDI PLL */
	l |= (1 << 28);
	dss_write_reg(DSS_PLL_CONTROL, l);

	/* Waiting for PLL lock request to complete */
	while(dss_read_reg(DSS_SDI_STATUS) & (1 << 6));

	/* Clearing PLL_GO bit */
	l &= ~(1 << 28);
	dss_write_reg(DSS_PLL_CONTROL, l);

	/* Waiting for PLL to lock */
	while(!(dss_read_reg(DSS_SDI_STATUS) & (1 << 5)));
}

static void sdi_enable(void)
{
	u32 l;

	l = dispc_read_reg(DISPC_CONTROL);
	l |= (1 << 29) | (1 << 27) | (1 << 3);
	dispc_write_reg(DISPC_CONTROL, l);

	sdi_init();

	/* Enable SDI */
	l |= (1 << 28);
	dispc_write_reg(DISPC_CONTROL, l);
	mdelay(2);
}

static int omap_dispc_init(struct omapfb_device *fbdev, int ext_mode,
			   struct omapfb_mem_desc *req_vram)
{
	int r;
	u32 l;
	struct lcd_panel *panel = fbdev->lcd_panel;
	int tmo = 10000;
	int skip_init = 0;
	int i;

	memset(&dispc, 0, sizeof(dispc));

	dispc.base = ioremap(DISPC_BASE, SZ_1K);
	if (!dispc.base) {
		dev_err(fbdev->dev, "can't ioremap DISPC\n");
		return -ENOMEM;
	}

	dispc.fbdev = fbdev;
	dispc.ext_mode = ext_mode;

	init_completion(&dispc.frame_done);
	spin_lock_init(&dispc.lock);

	if ((r = get_dss_clocks()) < 0)
		goto fail0;

	enable_lcd_clocks(1);

/* If built as module we _have_ to reset, since boot time arch code
 * disabled the clocks, thus the configuration done by the bootloader
 * is lost.
 */
#ifndef CONFIG_FB_OMAP_MODULE
#ifdef CONFIG_FB_OMAP_BOOTLOADER_INIT
	l = dispc_read_reg(DISPC_CONTROL);
	/* LCD enabled ? */
	if (l & 1) {
		pr_info("omapfb: skipping hardware initialization\n");
		skip_init = 1;
	}
#endif
#endif

	if (!skip_init) {
		/* Reset monitoring works only w/ the 54M clk */
		enable_digit_clocks(1);

		/* Soft reset */
		MOD_REG_FLD(DISPC_SYSCONFIG, 1 << 1, 1 << 1);

		while (!(dispc_read_reg(DISPC_SYSSTATUS) & 1)) {
			if (!--tmo) {
				dev_err(dispc.fbdev->dev, "soft reset failed\n");
				r = -ENODEV;
				enable_digit_clocks(0);
				goto fail1;
			}
		}

		enable_digit_clocks(0);
	}

	/* Enable smart standby/idle, autoidle and wakeup */
	l = dispc_read_reg(DISPC_SYSCONFIG);
	l &= ~((3 << 12) | (3 << 3));
	l |= (2 << 12) | (2 << 3) | (1 << 2) | (1 << 0);
	dispc_write_reg(DISPC_SYSCONFIG, l);
	omap_writel(1 << 0, DSS_BASE + DSS_SYSCONFIG);

	/* Set functional clock autogating */
	l = dispc_read_reg(DISPC_CONFIG);
	l |= 1 << 9;
	dispc_write_reg(DISPC_CONFIG, l);

	l = dispc_read_reg(DISPC_IRQSTATUS);
	dispc_write_reg(DISPC_IRQSTATUS, l);

	recalc_irq_mask();

	if ((r = request_irq(INT_24XX_DSS_IRQ, omap_dispc_irq_handler,
			   0, MODULE_NAME, fbdev)) < 0) {
		dev_err(dispc.fbdev->dev, "can't get DSS IRQ\n");
		goto fail1;
	}

	/* L3 firewall setting: enable access to OCM RAM */
	__raw_writel(0x402000b0, IO_ADDRESS(0x680050a0));

	if ((r = alloc_palette_ram()) < 0)
		goto fail2;

	if ((r = setup_fbmem(req_vram)) < 0)
		goto fail3;

	if (!skip_init) {
		for (i = 0; i < dispc.mem_desc.region_cnt; i++) {
			memset(dispc.mem_desc.region[i].vaddr, 0,
				dispc.mem_desc.region[i].size);
		}

		/* Set logic clock to fck, pixel clock to fck/2 for now */
		MOD_REG_FLD(DISPC_DIVISOR, FLD_MASK(16, 8), 1 << 16);
		MOD_REG_FLD(DISPC_DIVISOR, FLD_MASK(0, 8), 2 << 0);

		setup_plane_fifo(0, ext_mode);
		setup_plane_fifo(1, ext_mode);
		setup_plane_fifo(2, ext_mode);

		set_lcd_tft_mode(panel->config & OMAP_LCDC_PANEL_TFT);
		set_load_mode(DISPC_LOAD_FRAME_ONLY);

		if (!ext_mode) {
			set_lcd_data_lines(panel->data_lines);
			omap_dispc_set_lcd_size(panel->x_res, panel->y_res);
			set_lcd_timings();
		} else
			set_lcd_data_lines(panel->bpp);

		enable_rfbi_mode(ext_mode);

		sdi_enable();
	}

	l = dispc_read_reg(DISPC_REVISION);
	pr_info("omapfb: DISPC version %d.%d initialized\n",
		 l >> 4 & 0x0f, l & 0x0f);
	if (skip_init && !ext_mode) {
		/* Since the bootloader already enabled the display, and the
		 * clocks are enabled by the arch FB code, we can set the
		 * update mode already here.
		 */
		dispc.update_mode = OMAPFB_AUTO_UPDATE;
	}
	enable_lcd_clocks(0);

	return 0;
fail3:
	free_palette_ram();
fail2:
	free_irq(INT_24XX_DSS_IRQ, fbdev);
fail1:
	enable_lcd_clocks(0);
	put_dss_clocks();
fail0:
	iounmap(dispc.base);
	return r;
}

static void omap_dispc_cleanup(void)
{
	omap_dispc_set_update_mode(OMAPFB_UPDATE_DISABLED);
	/* This will also disable clocks that are on */
	cleanup_fbmem();
	free_palette_ram();
	free_irq(INT_24XX_DSS_IRQ, dispc.fbdev);
	put_dss_clocks();
	iounmap(dispc.base);
}

const struct lcd_ctrl omap2_int_ctrl = {
	.name			= "internal",
	.init			= omap_dispc_init,
	.cleanup		= omap_dispc_cleanup,
	.get_caps		= omap_dispc_get_caps,
	.set_update_mode	= omap_dispc_set_update_mode,
	.get_update_mode	= omap_dispc_get_update_mode,
	.update_window		= omap_dispc_update_window,
	.sync			= omap_dispc_sync_wrapper,
	.suspend		= omap_dispc_suspend,
	.resume			= omap_dispc_resume,
	.setup_plane		= omap_dispc_setup_plane,
	.setup_mem		= omap_dispc_setup_mem,
	.set_color_key		= omap_dispc_set_color_key,
	.get_color_key		= omap_dispc_get_color_key,
	.mmap			= omap_dispc_mmap_user,
};
