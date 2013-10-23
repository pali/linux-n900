/*
 * linux/drivers/video/omap2/dss/dispc.c
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

#define DSS_SUBSYS_NAME "DISPC"

#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/vmalloc.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <linux/workqueue.h>

#include <mach/sram.h>
#include <mach/board.h>
#include <mach/clock.h>

#include <mach/display.h>

#include "dss.h"

/* DISPC */
#define DISPC_BASE			0x48050400

#define DISPC_SZ_REGS			SZ_1K

struct dispc_reg { u16 idx; };

#define DISPC_REG(idx)			((const struct dispc_reg) { idx })

/* DISPC common */
#define DISPC_REVISION			DISPC_REG(0x0000)
#define DISPC_SYSCONFIG			DISPC_REG(0x0010)
#define DISPC_SYSSTATUS			DISPC_REG(0x0014)
#define DISPC_IRQSTATUS			DISPC_REG(0x0018)
#define DISPC_IRQENABLE			DISPC_REG(0x001C)
#define DISPC_CONTROL			DISPC_REG(0x0040)
#define DISPC_CONFIG			DISPC_REG(0x0044)
#define DISPC_CAPABLE			DISPC_REG(0x0048)
#define DISPC_DEFAULT_COLOR0		DISPC_REG(0x004C)
#define DISPC_DEFAULT_COLOR1		DISPC_REG(0x0050)
#define DISPC_TRANS_COLOR0		DISPC_REG(0x0054)
#define DISPC_TRANS_COLOR1		DISPC_REG(0x0058)
#define DISPC_LINE_STATUS		DISPC_REG(0x005C)
#define DISPC_LINE_NUMBER		DISPC_REG(0x0060)
#define DISPC_TIMING_H			DISPC_REG(0x0064)
#define DISPC_TIMING_V			DISPC_REG(0x0068)
#define DISPC_POL_FREQ			DISPC_REG(0x006C)
#define DISPC_DIVISOR			DISPC_REG(0x0070)
#define DISPC_GLOBAL_ALPHA		DISPC_REG(0x0074)
#define DISPC_SIZE_DIG			DISPC_REG(0x0078)
#define DISPC_SIZE_LCD			DISPC_REG(0x007C)

/* DISPC GFX plane */
#define DISPC_GFX_BA0			DISPC_REG(0x0080)
#define DISPC_GFX_BA1			DISPC_REG(0x0084)
#define DISPC_GFX_POSITION		DISPC_REG(0x0088)
#define DISPC_GFX_SIZE			DISPC_REG(0x008C)
#define DISPC_GFX_ATTRIBUTES		DISPC_REG(0x00A0)
#define DISPC_GFX_FIFO_THRESHOLD	DISPC_REG(0x00A4)
#define DISPC_GFX_FIFO_SIZE_STATUS	DISPC_REG(0x00A8)
#define DISPC_GFX_ROW_INC		DISPC_REG(0x00AC)
#define DISPC_GFX_PIXEL_INC		DISPC_REG(0x00B0)
#define DISPC_GFX_WINDOW_SKIP		DISPC_REG(0x00B4)
#define DISPC_GFX_TABLE_BA		DISPC_REG(0x00B8)

#define DISPC_DATA_CYCLE1		DISPC_REG(0x01D4)
#define DISPC_DATA_CYCLE2		DISPC_REG(0x01D8)
#define DISPC_DATA_CYCLE3		DISPC_REG(0x01DC)

#define DISPC_CPR_COEF_R		DISPC_REG(0x0220)
#define DISPC_CPR_COEF_G		DISPC_REG(0x0224)
#define DISPC_CPR_COEF_B		DISPC_REG(0x0228)

#define DISPC_GFX_PRELOAD		DISPC_REG(0x022C)

/* DISPC Video plane, n = 0 for VID1 and n = 1 for VID2 */
#define DISPC_VID_REG(n, idx)		DISPC_REG(0x00BC + (n)*0x90 + idx)

#define DISPC_VID_BA0(n)		DISPC_VID_REG(n, 0x0000)
#define DISPC_VID_BA1(n)		DISPC_VID_REG(n, 0x0004)
#define DISPC_VID_POSITION(n)		DISPC_VID_REG(n, 0x0008)
#define DISPC_VID_SIZE(n)		DISPC_VID_REG(n, 0x000C)
#define DISPC_VID_ATTRIBUTES(n)		DISPC_VID_REG(n, 0x0010)
#define DISPC_VID_FIFO_THRESHOLD(n)	DISPC_VID_REG(n, 0x0014)
#define DISPC_VID_FIFO_SIZE_STATUS(n)	DISPC_VID_REG(n, 0x0018)
#define DISPC_VID_ROW_INC(n)		DISPC_VID_REG(n, 0x001C)
#define DISPC_VID_PIXEL_INC(n)		DISPC_VID_REG(n, 0x0020)
#define DISPC_VID_FIR(n)		DISPC_VID_REG(n, 0x0024)
#define DISPC_VID_PICTURE_SIZE(n)	DISPC_VID_REG(n, 0x0028)
#define DISPC_VID_ACCU0(n)		DISPC_VID_REG(n, 0x002C)
#define DISPC_VID_ACCU1(n)		DISPC_VID_REG(n, 0x0030)

/* coef index i = {0, 1, 2, 3, 4, 5, 6, 7} */
#define DISPC_VID_FIR_COEF_H(n, i)	DISPC_REG(0x00F0 + (n)*0x90 + (i)*0x8)
/* coef index i = {0, 1, 2, 3, 4, 5, 6, 7} */
#define DISPC_VID_FIR_COEF_HV(n, i)	DISPC_REG(0x00F4 + (n)*0x90 + (i)*0x8)
/* coef index i = {0, 1, 2, 3, 4} */
#define DISPC_VID_CONV_COEF(n, i)	DISPC_REG(0x0130 + (n)*0x90 + (i)*0x4)
/* coef index i = {0, 1, 2, 3, 4, 5, 6, 7} */
#define DISPC_VID_FIR_COEF_V(n, i)	DISPC_REG(0x01E0 + (n)*0x20 + (i)*0x4)

#define DISPC_VID_PRELOAD(n)		DISPC_REG(0x230 + (n)*0x04)


#define DISPC_IRQ_MASK_ERROR            (DISPC_IRQ_GFX_FIFO_UNDERFLOW | \
					 DISPC_IRQ_OCP_ERR | \
					 DISPC_IRQ_VID1_FIFO_UNDERFLOW | \
					 DISPC_IRQ_VID2_FIFO_UNDERFLOW | \
					 DISPC_IRQ_SYNC_LOST | \
					 DISPC_IRQ_SYNC_LOST_DIGIT)

#define DISPC_MAX_NR_ISRS		8

struct omap_dispc_isr_data {
	omap_dispc_isr_t	isr;
	void			*arg;
	u32			mask;
};

#define REG_GET(idx, start, end) \
	FLD_GET(dispc_read_reg(idx), start, end)

#define REG_FLD_MOD(idx, val, start, end)				\
	dispc_write_reg(idx, FLD_MOD(dispc_read_reg(idx), val, start, end))

static const struct dispc_reg dispc_reg_att[] = { DISPC_GFX_ATTRIBUTES,
	DISPC_VID_ATTRIBUTES(0),
	DISPC_VID_ATTRIBUTES(1) };

static struct {
	void __iomem    *base;

	struct clk	*dpll4_m4_ck;

	unsigned long	cache_req_pck;
	unsigned long	cache_prate;
	struct dispc_clock_info cache_cinfo;

	spinlock_t irq_lock;
	u32 irq_error_mask;
	struct omap_dispc_isr_data registered_isr[DISPC_MAX_NR_ISRS];
	u32 error_irqs;
	struct work_struct error_work;

	u32		ctx[DISPC_SZ_REGS / sizeof(u32)];
} dispc;

static void _omap_dispc_set_irqs(void);

static inline void dispc_write_reg(const struct dispc_reg idx, u32 val)
{
	__raw_writel(val, dispc.base + idx.idx);
}

static inline u32 dispc_read_reg(const struct dispc_reg idx)
{
	return __raw_readl(dispc.base + idx.idx);
}

#define SR(reg) \
	dispc.ctx[(DISPC_##reg).idx / sizeof(u32)] = dispc_read_reg(DISPC_##reg)
#define RR(reg) \
	dispc_write_reg(DISPC_##reg, dispc.ctx[(DISPC_##reg).idx / sizeof(u32)])

void dispc_save_context(void)
{
	if (cpu_is_omap24xx())
		return;

	SR(SYSCONFIG);
	SR(IRQENABLE);
	SR(CONTROL);
	SR(CONFIG);
	SR(DEFAULT_COLOR0);
	SR(DEFAULT_COLOR1);
	SR(TRANS_COLOR0);
	SR(TRANS_COLOR1);
	SR(LINE_NUMBER);
	SR(TIMING_H);
	SR(TIMING_V);
	SR(POL_FREQ);
	SR(DIVISOR);
	SR(GLOBAL_ALPHA);
	SR(SIZE_DIG);
	SR(SIZE_LCD);

	SR(GFX_BA0);
	SR(GFX_BA1);
	SR(GFX_POSITION);
	SR(GFX_SIZE);
	SR(GFX_ATTRIBUTES);
	SR(GFX_FIFO_THRESHOLD);
	SR(GFX_ROW_INC);
	SR(GFX_PIXEL_INC);
	SR(GFX_WINDOW_SKIP);
	SR(GFX_TABLE_BA);

	SR(DATA_CYCLE1);
	SR(DATA_CYCLE2);
	SR(DATA_CYCLE3);

	SR(CPR_COEF_R);
	SR(CPR_COEF_G);
	SR(CPR_COEF_B);

	SR(GFX_PRELOAD);

	/* VID1 */
	SR(VID_BA0(0));
	SR(VID_BA1(0));
	SR(VID_POSITION(0));
	SR(VID_SIZE(0));
	SR(VID_ATTRIBUTES(0));
	SR(VID_FIFO_THRESHOLD(0));
	SR(VID_ROW_INC(0));
	SR(VID_PIXEL_INC(0));
	SR(VID_FIR(0));
	SR(VID_PICTURE_SIZE(0));
	SR(VID_ACCU0(0));
	SR(VID_ACCU1(0));

	SR(VID_FIR_COEF_H(0, 0));
	SR(VID_FIR_COEF_H(0, 1));
	SR(VID_FIR_COEF_H(0, 2));
	SR(VID_FIR_COEF_H(0, 3));
	SR(VID_FIR_COEF_H(0, 4));
	SR(VID_FIR_COEF_H(0, 5));
	SR(VID_FIR_COEF_H(0, 6));
	SR(VID_FIR_COEF_H(0, 7));

	SR(VID_FIR_COEF_HV(0, 0));
	SR(VID_FIR_COEF_HV(0, 1));
	SR(VID_FIR_COEF_HV(0, 2));
	SR(VID_FIR_COEF_HV(0, 3));
	SR(VID_FIR_COEF_HV(0, 4));
	SR(VID_FIR_COEF_HV(0, 5));
	SR(VID_FIR_COEF_HV(0, 6));
	SR(VID_FIR_COEF_HV(0, 7));

	SR(VID_CONV_COEF(0, 0));
	SR(VID_CONV_COEF(0, 1));
	SR(VID_CONV_COEF(0, 2));
	SR(VID_CONV_COEF(0, 3));
	SR(VID_CONV_COEF(0, 4));

	SR(VID_FIR_COEF_V(0, 0));
	SR(VID_FIR_COEF_V(0, 1));
	SR(VID_FIR_COEF_V(0, 2));
	SR(VID_FIR_COEF_V(0, 3));
	SR(VID_FIR_COEF_V(0, 4));
	SR(VID_FIR_COEF_V(0, 5));
	SR(VID_FIR_COEF_V(0, 6));
	SR(VID_FIR_COEF_V(0, 7));

	SR(VID_PRELOAD(0));

	/* VID2 */
	SR(VID_BA0(1));
	SR(VID_BA1(1));
	SR(VID_POSITION(1));
	SR(VID_SIZE(1));
	SR(VID_ATTRIBUTES(1));
	SR(VID_FIFO_THRESHOLD(1));
	SR(VID_ROW_INC(1));
	SR(VID_PIXEL_INC(1));
	SR(VID_FIR(1));
	SR(VID_PICTURE_SIZE(1));
	SR(VID_ACCU0(1));
	SR(VID_ACCU1(1));

	SR(VID_FIR_COEF_H(1, 0));
	SR(VID_FIR_COEF_H(1, 1));
	SR(VID_FIR_COEF_H(1, 2));
	SR(VID_FIR_COEF_H(1, 3));
	SR(VID_FIR_COEF_H(1, 4));
	SR(VID_FIR_COEF_H(1, 5));
	SR(VID_FIR_COEF_H(1, 6));
	SR(VID_FIR_COEF_H(1, 7));

	SR(VID_FIR_COEF_HV(1, 0));
	SR(VID_FIR_COEF_HV(1, 1));
	SR(VID_FIR_COEF_HV(1, 2));
	SR(VID_FIR_COEF_HV(1, 3));
	SR(VID_FIR_COEF_HV(1, 4));
	SR(VID_FIR_COEF_HV(1, 5));
	SR(VID_FIR_COEF_HV(1, 6));
	SR(VID_FIR_COEF_HV(1, 7));

	SR(VID_CONV_COEF(1, 0));
	SR(VID_CONV_COEF(1, 1));
	SR(VID_CONV_COEF(1, 2));
	SR(VID_CONV_COEF(1, 3));
	SR(VID_CONV_COEF(1, 4));

	SR(VID_FIR_COEF_V(1, 0));
	SR(VID_FIR_COEF_V(1, 1));
	SR(VID_FIR_COEF_V(1, 2));
	SR(VID_FIR_COEF_V(1, 3));
	SR(VID_FIR_COEF_V(1, 4));
	SR(VID_FIR_COEF_V(1, 5));
	SR(VID_FIR_COEF_V(1, 6));
	SR(VID_FIR_COEF_V(1, 7));

	SR(VID_PRELOAD(1));
}

void dispc_restore_context(void)
{
	RR(SYSCONFIG);
	/*RR(IRQENABLE);*/
	/*RR(CONTROL);*/
	RR(CONFIG);
	RR(DEFAULT_COLOR0);
	RR(DEFAULT_COLOR1);
	RR(TRANS_COLOR0);
	RR(TRANS_COLOR1);
	RR(LINE_NUMBER);
	RR(TIMING_H);
	RR(TIMING_V);
	RR(POL_FREQ);
	RR(DIVISOR);
	RR(GLOBAL_ALPHA);
	RR(SIZE_DIG);
	RR(SIZE_LCD);

	RR(GFX_BA0);
	RR(GFX_BA1);
	RR(GFX_POSITION);
	RR(GFX_SIZE);
	RR(GFX_ATTRIBUTES);
	RR(GFX_FIFO_THRESHOLD);
	RR(GFX_ROW_INC);
	RR(GFX_PIXEL_INC);
	RR(GFX_WINDOW_SKIP);
	RR(GFX_TABLE_BA);

	RR(DATA_CYCLE1);
	RR(DATA_CYCLE2);
	RR(DATA_CYCLE3);

	RR(CPR_COEF_R);
	RR(CPR_COEF_G);
	RR(CPR_COEF_B);

	RR(GFX_PRELOAD);

	/* VID1 */
	RR(VID_BA0(0));
	RR(VID_BA1(0));
	RR(VID_POSITION(0));
	RR(VID_SIZE(0));
	RR(VID_ATTRIBUTES(0));
	RR(VID_FIFO_THRESHOLD(0));
	RR(VID_ROW_INC(0));
	RR(VID_PIXEL_INC(0));
	RR(VID_FIR(0));
	RR(VID_PICTURE_SIZE(0));
	RR(VID_ACCU0(0));
	RR(VID_ACCU1(0));

	RR(VID_FIR_COEF_H(0, 0));
	RR(VID_FIR_COEF_H(0, 1));
	RR(VID_FIR_COEF_H(0, 2));
	RR(VID_FIR_COEF_H(0, 3));
	RR(VID_FIR_COEF_H(0, 4));
	RR(VID_FIR_COEF_H(0, 5));
	RR(VID_FIR_COEF_H(0, 6));
	RR(VID_FIR_COEF_H(0, 7));

	RR(VID_FIR_COEF_HV(0, 0));
	RR(VID_FIR_COEF_HV(0, 1));
	RR(VID_FIR_COEF_HV(0, 2));
	RR(VID_FIR_COEF_HV(0, 3));
	RR(VID_FIR_COEF_HV(0, 4));
	RR(VID_FIR_COEF_HV(0, 5));
	RR(VID_FIR_COEF_HV(0, 6));
	RR(VID_FIR_COEF_HV(0, 7));

	RR(VID_CONV_COEF(0, 0));
	RR(VID_CONV_COEF(0, 1));
	RR(VID_CONV_COEF(0, 2));
	RR(VID_CONV_COEF(0, 3));
	RR(VID_CONV_COEF(0, 4));

	RR(VID_FIR_COEF_V(0, 0));
	RR(VID_FIR_COEF_V(0, 1));
	RR(VID_FIR_COEF_V(0, 2));
	RR(VID_FIR_COEF_V(0, 3));
	RR(VID_FIR_COEF_V(0, 4));
	RR(VID_FIR_COEF_V(0, 5));
	RR(VID_FIR_COEF_V(0, 6));
	RR(VID_FIR_COEF_V(0, 7));

	RR(VID_PRELOAD(0));

	/* VID2 */
	RR(VID_BA0(1));
	RR(VID_BA1(1));
	RR(VID_POSITION(1));
	RR(VID_SIZE(1));
	RR(VID_ATTRIBUTES(1));
	RR(VID_FIFO_THRESHOLD(1));
	RR(VID_ROW_INC(1));
	RR(VID_PIXEL_INC(1));
	RR(VID_FIR(1));
	RR(VID_PICTURE_SIZE(1));
	RR(VID_ACCU0(1));
	RR(VID_ACCU1(1));

	RR(VID_FIR_COEF_H(1, 0));
	RR(VID_FIR_COEF_H(1, 1));
	RR(VID_FIR_COEF_H(1, 2));
	RR(VID_FIR_COEF_H(1, 3));
	RR(VID_FIR_COEF_H(1, 4));
	RR(VID_FIR_COEF_H(1, 5));
	RR(VID_FIR_COEF_H(1, 6));
	RR(VID_FIR_COEF_H(1, 7));

	RR(VID_FIR_COEF_HV(1, 0));
	RR(VID_FIR_COEF_HV(1, 1));
	RR(VID_FIR_COEF_HV(1, 2));
	RR(VID_FIR_COEF_HV(1, 3));
	RR(VID_FIR_COEF_HV(1, 4));
	RR(VID_FIR_COEF_HV(1, 5));
	RR(VID_FIR_COEF_HV(1, 6));
	RR(VID_FIR_COEF_HV(1, 7));

	RR(VID_CONV_COEF(1, 0));
	RR(VID_CONV_COEF(1, 1));
	RR(VID_CONV_COEF(1, 2));
	RR(VID_CONV_COEF(1, 3));
	RR(VID_CONV_COEF(1, 4));

	RR(VID_FIR_COEF_V(1, 0));
	RR(VID_FIR_COEF_V(1, 1));
	RR(VID_FIR_COEF_V(1, 2));
	RR(VID_FIR_COEF_V(1, 3));
	RR(VID_FIR_COEF_V(1, 4));
	RR(VID_FIR_COEF_V(1, 5));
	RR(VID_FIR_COEF_V(1, 6));
	RR(VID_FIR_COEF_V(1, 7));

	RR(VID_PRELOAD(1));

	/* enable last, because LCD & DIGIT enable are here */
	RR(CONTROL);

	/* clear spurious SYNC_LOST_DIGIT interrupts */
	dispc_write_reg(DISPC_IRQSTATUS, DISPC_IRQ_SYNC_LOST_DIGIT);

	/*
	 * enable last so IRQs won't trigger before
	 * the context is fully restored
	 */
	RR(IRQENABLE);
}

#undef SR
#undef RR

static inline void enable_clocks(bool enable)
{
	if (enable)
		dss_clk_enable(DSS_CLK_ICK | DSS_CLK_FCK1);
	else
		dss_clk_disable(DSS_CLK_ICK | DSS_CLK_FCK1);
}

void dispc_go(enum omap_channel channel)
{
	int bit;
	unsigned long tmo;

	enable_clocks(1);

	if (channel == OMAP_DSS_CHANNEL_LCD)
		bit = 0; /* LCDENABLE */
	else
		bit = 1; /* DIGITALENABLE */

	/* if the channel is not enabled, we don't need GO */
	if (REG_GET(DISPC_CONTROL, bit, bit) == 0)
		goto end;

	if (channel == OMAP_DSS_CHANNEL_LCD)
		bit = 5; /* GOLCD */
	else
		bit = 6; /* GODIGIT */

	tmo = jiffies + msecs_to_jiffies(200);
	while (REG_GET(DISPC_CONTROL, bit, bit) == 1) {
		if (time_after(jiffies, tmo)) {
			DSSERR("timeout waiting GO flag\n");
			goto end;
		}
		cpu_relax();
	}

	DSSDBG("GO %s\n", channel == OMAP_DSS_CHANNEL_LCD ? "LCD" : "DIGIT");

	REG_FLD_MOD(DISPC_CONTROL, 1, bit, bit);
end:
	enable_clocks(0);
}

void dispc_wait_for_go(enum omap_channel channel)
{
	int bit;
	unsigned long tmo;

	enable_clocks(1);

	if (channel == OMAP_DSS_CHANNEL_LCD)
		bit = 0; /* LCDENABLE */
	else
		bit = 1; /* DIGITALENABLE */

	/* if the channel is not enabled, we don't need GO */
	if (REG_GET(DISPC_CONTROL, bit, bit) == 0)
		goto end;

	if (channel == OMAP_DSS_CHANNEL_LCD)
		bit = 5; /* GOLCD */
	else
		bit = 6; /* GODIGIT */

	tmo = jiffies + msecs_to_jiffies(200);
	while (REG_GET(DISPC_CONTROL, bit, bit) == 1) {
		if (time_after(jiffies, tmo)) {
			DSSERR("timeout waiting GO flag\n");
			goto end;
		}
		cpu_relax();
	}

end:
	enable_clocks(0);
}

static void _dispc_write_firh_reg(enum omap_plane plane, int reg, u32 value)
{
	BUG_ON(plane == OMAP_DSS_GFX);

	dispc_write_reg(DISPC_VID_FIR_COEF_H(plane-1, reg), value);
}

static void _dispc_write_firhv_reg(enum omap_plane plane, int reg, u32 value)
{
	BUG_ON(plane == OMAP_DSS_GFX);

	dispc_write_reg(DISPC_VID_FIR_COEF_HV(plane-1, reg), value);
}

static void _dispc_write_firv_reg(enum omap_plane plane, int reg, u32 value)
{
	BUG_ON(plane == OMAP_DSS_GFX);

	dispc_write_reg(DISPC_VID_FIR_COEF_V(plane-1, reg), value);
}

static void _dispc_set_scale_coef(enum omap_plane plane, int hscaleup,
		int vscaleup, int five_taps)
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

		_dispc_write_firh_reg(plane, i, h);
		_dispc_write_firhv_reg(plane, i, hv);
	}

	if (!five_taps)
		return;

	for (i = 0; i < 8; i++) {
		u32 v;
		v = v_coef[i];
		_dispc_write_firv_reg(plane, i, v);
	}
}

static void _dispc_setup_color_conv_coef(void)
{
	const struct color_conv_coef {
		int  ry,  rcr,  rcb,   gy,  gcr,  gcb,   by,  bcr,  bcb;
		int  full_range;
	}  ctbl_bt601_5 = {
		298,  409,    0,  298, -208, -100,  298,    0,  517, 0,
	};

	const struct color_conv_coef *ct;

#define CVAL(x, y) (FLD_VAL(x, 26, 16) | FLD_VAL(y, 10, 0))

	ct = &ctbl_bt601_5;

	dispc_write_reg(DISPC_VID_CONV_COEF(0, 0), CVAL(ct->rcr, ct->ry));
	dispc_write_reg(DISPC_VID_CONV_COEF(0, 1), CVAL(ct->gy,	 ct->rcb));
	dispc_write_reg(DISPC_VID_CONV_COEF(0, 2), CVAL(ct->gcb, ct->gcr));
	dispc_write_reg(DISPC_VID_CONV_COEF(0, 3), CVAL(ct->bcr, ct->by));
	dispc_write_reg(DISPC_VID_CONV_COEF(0, 4), CVAL(0,       ct->bcb));

	dispc_write_reg(DISPC_VID_CONV_COEF(1, 0), CVAL(ct->rcr, ct->ry));
	dispc_write_reg(DISPC_VID_CONV_COEF(1, 1), CVAL(ct->gy,	 ct->rcb));
	dispc_write_reg(DISPC_VID_CONV_COEF(1, 2), CVAL(ct->gcb, ct->gcr));
	dispc_write_reg(DISPC_VID_CONV_COEF(1, 3), CVAL(ct->bcr, ct->by));
	dispc_write_reg(DISPC_VID_CONV_COEF(1, 4), CVAL(0,       ct->bcb));

#undef CVAL

	REG_FLD_MOD(DISPC_VID_ATTRIBUTES(0), ct->full_range, 11, 11);
	REG_FLD_MOD(DISPC_VID_ATTRIBUTES(1), ct->full_range, 11, 11);
}


static void _dispc_set_plane_ba0(enum omap_plane plane, u32 paddr)
{
	const struct dispc_reg ba0_reg[] = { DISPC_GFX_BA0,
		DISPC_VID_BA0(0),
		DISPC_VID_BA0(1) };

	dispc_write_reg(ba0_reg[plane], paddr);
}

void omap_dispc_set_plane_ba0(enum omap_channel channel, enum omap_plane plane,
			      u32 paddr)
{
	enable_clocks(1);
	_dispc_set_plane_ba0(plane, paddr);
	dispc_go(channel);
	enable_clocks(0);
}
EXPORT_SYMBOL_GPL(omap_dispc_set_plane_ba0);

static void _dispc_set_plane_ba1(enum omap_plane plane, u32 paddr)
{
	const struct dispc_reg ba1_reg[] = { DISPC_GFX_BA1,
				      DISPC_VID_BA1(0),
				      DISPC_VID_BA1(1) };

	dispc_write_reg(ba1_reg[plane], paddr);
}

static void _dispc_set_plane_pos(enum omap_plane plane, int x, int y)
{
	const struct dispc_reg pos_reg[] = { DISPC_GFX_POSITION,
				      DISPC_VID_POSITION(0),
				      DISPC_VID_POSITION(1) };

	u32 val = FLD_VAL(y, 26, 16) | FLD_VAL(x, 10, 0);
	dispc_write_reg(pos_reg[plane], val);
}

static void _dispc_set_pic_size(enum omap_plane plane, int width, int height)
{
	const struct dispc_reg siz_reg[] = { DISPC_GFX_SIZE,
				      DISPC_VID_PICTURE_SIZE(0),
				      DISPC_VID_PICTURE_SIZE(1) };
	u32 val = FLD_VAL(height - 1, 26, 16) | FLD_VAL(width - 1, 10, 0);
	dispc_write_reg(siz_reg[plane], val);
}

static void _dispc_set_vid_size(enum omap_plane plane, int width, int height)
{
	u32 val;
	const struct dispc_reg vsi_reg[] = { DISPC_VID_SIZE(0),
				      DISPC_VID_SIZE(1) };

	BUG_ON(plane == OMAP_DSS_GFX);

	val = FLD_VAL(height - 1, 26, 16) | FLD_VAL(width - 1, 10, 0);
	dispc_write_reg(vsi_reg[plane-1], val);
}

static void _dispc_setup_global_alpha(enum omap_plane plane, u8 global_alpha)
{

	BUG_ON(plane == OMAP_DSS_VIDEO1);

	if (cpu_is_omap24xx())
		return;

	if (plane == OMAP_DSS_GFX)
		REG_FLD_MOD(DISPC_GLOBAL_ALPHA, global_alpha, 7, 0);
	else if (plane == OMAP_DSS_VIDEO2)
		REG_FLD_MOD(DISPC_GLOBAL_ALPHA, global_alpha, 23, 16);
}

static void _dispc_set_pix_inc(enum omap_plane plane, s32 inc)
{
	const struct dispc_reg ri_reg[] = { DISPC_GFX_PIXEL_INC,
				     DISPC_VID_PIXEL_INC(0),
				     DISPC_VID_PIXEL_INC(1) };

	dispc_write_reg(ri_reg[plane], inc);
}

static void _dispc_set_row_inc(enum omap_plane plane, s32 inc)
{
	const struct dispc_reg ri_reg[] = { DISPC_GFX_ROW_INC,
				     DISPC_VID_ROW_INC(0),
				     DISPC_VID_ROW_INC(1) };

	dispc_write_reg(ri_reg[plane], inc);
}

static s32 _dispc_get_pix_inc(enum omap_plane plane)
{
	const struct dispc_reg ri_reg[] = { DISPC_GFX_PIXEL_INC,
				     DISPC_VID_PIXEL_INC(0),
				     DISPC_VID_PIXEL_INC(1) };

	return dispc_read_reg(ri_reg[plane]);
}

static s32 _dispc_get_row_inc(enum omap_plane plane)
{
	const struct dispc_reg ri_reg[] = { DISPC_GFX_ROW_INC,
				     DISPC_VID_ROW_INC(0),
				     DISPC_VID_ROW_INC(1) };

	return dispc_read_reg(ri_reg[plane]);
}

static void _dispc_set_color_mode(enum omap_plane plane,
		enum omap_color_mode color_mode)
{
	u32 m = 0;

	switch (color_mode) {
	case OMAP_DSS_COLOR_CLUT1:
		m = 0x0; break;
	case OMAP_DSS_COLOR_CLUT2:
		m = 0x1; break;
	case OMAP_DSS_COLOR_CLUT4:
		m = 0x2; break;
	case OMAP_DSS_COLOR_CLUT8:
		m = 0x3; break;
	case OMAP_DSS_COLOR_RGB12U:
		m = 0x4; break;
	case OMAP_DSS_COLOR_ARGB16:
		m = 0x5; break;
	case OMAP_DSS_COLOR_RGB16:
		m = 0x6; break;
	case OMAP_DSS_COLOR_RGB24U:
		m = 0x8; break;
	case OMAP_DSS_COLOR_RGB24P:
		m = 0x9; break;
	case OMAP_DSS_COLOR_YUV2:
		m = 0xa; break;
	case OMAP_DSS_COLOR_UYVY:
		m = 0xb; break;
	case OMAP_DSS_COLOR_ARGB32:
		m = 0xc; break;
	case OMAP_DSS_COLOR_RGBA32:
		m = 0xd; break;
	case OMAP_DSS_COLOR_RGBX32:
		m = 0xe; break;
	default:
		BUG(); break;
	}

	REG_FLD_MOD(dispc_reg_att[plane], m, 4, 1);
}

static enum omap_color_mode _dispc_get_color_mode(enum omap_plane plane)
{
	u32 m = REG_GET(dispc_reg_att[plane], 4, 1);

	switch (m) {
	case 0x0:
		return OMAP_DSS_COLOR_CLUT1;
	case 0x1:
		return OMAP_DSS_COLOR_CLUT2;
	case 0x2:
		return OMAP_DSS_COLOR_CLUT4;
	case 0x3:
		return OMAP_DSS_COLOR_CLUT8;
	case 0x4:
		return OMAP_DSS_COLOR_RGB12U;
	case 0x5:
		return OMAP_DSS_COLOR_ARGB16;
	case 0x6:
		return OMAP_DSS_COLOR_RGB16;
	case 0x8:
		return OMAP_DSS_COLOR_RGB24U;
	case 0x9:
		return OMAP_DSS_COLOR_RGB24P;
	case 0xa:
		return OMAP_DSS_COLOR_YUV2;
	case 0xb:
		return OMAP_DSS_COLOR_UYVY;
	case 0xc:
		return OMAP_DSS_COLOR_ARGB32;
	case 0xd:
		return OMAP_DSS_COLOR_RGBA32;
	case 0xe:
		return OMAP_DSS_COLOR_RGBX32;
	default:
		BUG();
	}
}

static void _dispc_set_channel_out(enum omap_plane plane,
		enum omap_channel channel)
{
	int shift;
	u32 val;

	switch (plane) {
	case OMAP_DSS_GFX:
		shift = 8;
		break;
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
		shift = 16;
		break;
	default:
		BUG();
		return;
	}

	val = dispc_read_reg(dispc_reg_att[plane]);
	val = FLD_MOD(val, channel, shift, shift);
	dispc_write_reg(dispc_reg_att[plane], val);
}

static enum omap_channel _dispc_get_channel_out(enum omap_plane plane)
{
	int shift;

	switch (plane) {
	case OMAP_DSS_GFX:
		shift = 8;
		break;
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
		shift = 16;
		break;
	default:
		BUG();
		return OMAP_DSS_CHANNEL_LCD;
	}

	return REG_GET(dispc_reg_att[plane], shift, shift);
}

void dispc_set_burst_size(enum omap_plane plane,
		enum omap_burst_size burst_size)
{
	int shift;
	u32 val;

	enable_clocks(1);

	switch (plane) {
	case OMAP_DSS_GFX:
		shift = 6;
		break;
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
		shift = 14;
		break;
	default:
		BUG();
		return;
	}

	val = dispc_read_reg(dispc_reg_att[plane]);
	val = FLD_MOD(val, burst_size, shift+1, shift);
	dispc_write_reg(dispc_reg_att[plane], val);

	enable_clocks(0);
}

static void _dispc_set_vid_color_conv(enum omap_plane plane, bool enable)
{
	u32 val;

	BUG_ON(plane == OMAP_DSS_GFX);

	val = dispc_read_reg(dispc_reg_att[plane]);
	val = FLD_MOD(val, enable, 9, 9);
	dispc_write_reg(dispc_reg_att[plane], val);
}

void dispc_enable_replication(enum omap_plane plane, bool enable)
{
	int bit;

	if (plane == OMAP_DSS_GFX)
		bit = 5;
	else
		bit = 10;

	enable_clocks(1);
	REG_FLD_MOD(dispc_reg_att[plane], enable, bit, bit);
	enable_clocks(0);
}

void dispc_set_lcd_size(u16 width, u16 height)
{
	u32 val;
	BUG_ON((width > (1 << 11)) || (height > (1 << 11)));
	val = FLD_VAL(height - 1, 26, 16) | FLD_VAL(width - 1, 10, 0);
	enable_clocks(1);
	dispc_write_reg(DISPC_SIZE_LCD, val);
	enable_clocks(0);
}

void dispc_set_digit_size(u16 width, u16 height)
{
	u32 val;
	BUG_ON((width > (1 << 11)) || (height > (1 << 11)));
	val = FLD_VAL(height - 1, 26, 16) | FLD_VAL(width - 1, 10, 0);
	enable_clocks(1);
	dispc_write_reg(DISPC_SIZE_DIG, val);
	enable_clocks(0);
}

u32 dispc_get_plane_fifo_size(enum omap_plane plane)
{
	const struct dispc_reg fsz_reg[] = { DISPC_GFX_FIFO_SIZE_STATUS,
				      DISPC_VID_FIFO_SIZE_STATUS(0),
				      DISPC_VID_FIFO_SIZE_STATUS(1) };
	u32 size;

	enable_clocks(1);

	if (cpu_is_omap24xx())
		size = FLD_GET(dispc_read_reg(fsz_reg[plane]), 8, 0);
	else if (cpu_is_omap34xx())
		size = FLD_GET(dispc_read_reg(fsz_reg[plane]), 10, 0);
	else
		BUG();

	if (cpu_is_omap34xx()) {
		/* FIFOMERGE */
		if (REG_GET(DISPC_CONFIG, 14, 14))
			size *= 3;
	}

	enable_clocks(0);

	return size;
}

void dispc_setup_plane_fifo(enum omap_plane plane, u32 low, u32 high)
{
	const struct dispc_reg ftrs_reg[] = { DISPC_GFX_FIFO_THRESHOLD,
				       DISPC_VID_FIFO_THRESHOLD(0),
				       DISPC_VID_FIFO_THRESHOLD(1) };
	u32 size;

	enable_clocks(1);

	size = dispc_get_plane_fifo_size(plane);

	BUG_ON(low > size || high > size);

	DSSDBG("fifo(%d) size %d, low/high old %u/%u, new %u/%u\n",
			plane, size,
			REG_GET(ftrs_reg[plane], 11, 0),
			REG_GET(ftrs_reg[plane], 27, 16),
			low, high);

	if (cpu_is_omap24xx())
		dispc_write_reg(ftrs_reg[plane],
				FLD_VAL(high, 24, 16) | FLD_VAL(low, 8, 0));
	else
		dispc_write_reg(ftrs_reg[plane],
				FLD_VAL(high, 27, 16) | FLD_VAL(low, 11, 0));

	enable_clocks(0);
}

void dispc_enable_fifomerge(bool enable)
{
	enable_clocks(1);

	DSSDBG("FIFO merge %s\n", enable ? "enabled" : "disabled");
	REG_FLD_MOD(DISPC_CONFIG, enable ? 1 : 0, 14, 14);

	enable_clocks(0);
}

bool dispc_fifomerge_enabled(void)
{
	bool enabled;

	enable_clocks(1);

	enabled = REG_GET(DISPC_CONFIG, 14, 14);

	enable_clocks(0);

	return enabled;
}

static bool _dispc_plane_enabled(enum omap_plane plane)
{
	return REG_GET(dispc_reg_att[plane], 0, 0);
}

enum omap_channel dispc_get_enabled_channel(void)
{
	enum omap_channel ch = OMAP_DSS_CHANNEL_LCD;

	enable_clocks(1);

	if (_dispc_plane_enabled(OMAP_DSS_GFX)) {
		ch = _dispc_get_channel_out(OMAP_DSS_GFX);
		goto out;
	}
	if (_dispc_plane_enabled(OMAP_DSS_VIDEO1)) {
		ch = _dispc_get_channel_out(OMAP_DSS_VIDEO1);
		goto out;
	}
	if (_dispc_plane_enabled(OMAP_DSS_VIDEO2)) {
		ch = _dispc_get_channel_out(OMAP_DSS_VIDEO2);
		goto out;
	}

 out:
	enable_clocks(0);

	return ch;
}

static void _dispc_set_fir(enum omap_plane plane, int hinc, int vinc)
{
	u32 val;
	const struct dispc_reg fir_reg[] = { DISPC_VID_FIR(0),
				      DISPC_VID_FIR(1) };

	BUG_ON(plane == OMAP_DSS_GFX);

	if (cpu_is_omap24xx())
		val = FLD_VAL(vinc, 27, 16) | FLD_VAL(hinc, 11, 0);
	else
		val = FLD_VAL(vinc, 28, 16) | FLD_VAL(hinc, 12, 0);
	dispc_write_reg(fir_reg[plane-1], val);
}

static void _dispc_set_vid_accu0(enum omap_plane plane, int haccu, int vaccu)
{
	u32 val;
	const struct dispc_reg ac0_reg[] = { DISPC_VID_ACCU0(0),
				      DISPC_VID_ACCU0(1) };

	BUG_ON(plane == OMAP_DSS_GFX);

	val = FLD_VAL(vaccu, 25, 16) | FLD_VAL(haccu, 9, 0);
	dispc_write_reg(ac0_reg[plane-1], val);
}

static void _dispc_set_vid_accu1(enum omap_plane plane, int haccu, int vaccu)
{
	u32 val;
	const struct dispc_reg ac1_reg[] = { DISPC_VID_ACCU1(0),
				      DISPC_VID_ACCU1(1) };

	BUG_ON(plane == OMAP_DSS_GFX);

	val = FLD_VAL(vaccu, 25, 16) | FLD_VAL(haccu, 9, 0);
	dispc_write_reg(ac1_reg[plane-1], val);
}


static void _dispc_set_scaling(enum omap_plane plane,
		u16 orig_width, u16 orig_height,
		u16 out_width, u16 out_height,
		bool ilace, bool five_taps,
		bool fieldmode)
{
	int fir_hinc;
	int fir_vinc;
	int hscaleup, vscaleup;
	int accu0 = 0;
	int accu1 = 0;
	u32 l;

	BUG_ON(plane == OMAP_DSS_GFX);

	hscaleup = orig_width <= out_width;
	vscaleup = orig_height <= out_height;

	_dispc_set_scale_coef(plane, hscaleup, vscaleup, five_taps);

	if (!orig_width || orig_width == out_width)
		fir_hinc = 0;
	else
		fir_hinc = 1024 * orig_width / out_width;

	if (!orig_height || orig_height == out_height)
		fir_vinc = 0;
	else
		fir_vinc = 1024 * orig_height / out_height;

	_dispc_set_fir(plane, fir_hinc, fir_vinc);

	l = dispc_read_reg(dispc_reg_att[plane]);
	l &= ~((0x0f << 5) | (0x3 << 21));

	l |= fir_hinc ? (1 << 5) : 0;
	l |= fir_vinc ? (1 << 6) : 0;

	l |= hscaleup ? 0 : (1 << 7);
	l |= vscaleup ? 0 : (1 << 8);

	l |= five_taps ? (1 << 21) : 0;
	l |= five_taps ? (1 << 22) : 0;

	dispc_write_reg(dispc_reg_att[plane], l);

	/*
	 * field 0 = even field = bottom field
	 * field 1 = odd field = top field
	 */
	if (ilace && !fieldmode) {
		accu1 = 0;
		accu0 = (fir_vinc / 2) & 0x3ff;
		if (accu0 >= 1024/2) {
			accu1 = 1024/2;
			accu0 -= accu1;
		}
	}

	_dispc_set_vid_accu0(plane, 0, accu0);
	_dispc_set_vid_accu1(plane, 0, accu1);
}

static void _dispc_set_rotation_attrs(enum omap_plane plane, u8 rotation,
		bool mirroring, enum omap_color_mode color_mode)
{
	if (color_mode == OMAP_DSS_COLOR_YUV2 ||
			color_mode == OMAP_DSS_COLOR_UYVY) {
		int vidrot = 0;

		if (mirroring) {
			switch (rotation) {
			case OMAP_DSS_ROT_0:
				vidrot = 2;
				break;
			case OMAP_DSS_ROT_90:
				vidrot = 1;
				break;
			case OMAP_DSS_ROT_180:
				vidrot = 0;
				break;
			case OMAP_DSS_ROT_270:
				vidrot = 3;
				break;
			}
		} else {
			switch (rotation) {
			case OMAP_DSS_ROT_0:
				vidrot = 0;
				break;
			case OMAP_DSS_ROT_90:
				vidrot = 1;
				break;
			case OMAP_DSS_ROT_180:
				vidrot = 2;
				break;
			case OMAP_DSS_ROT_270:
				vidrot = 3;
				break;
			}
		}

		REG_FLD_MOD(dispc_reg_att[plane], vidrot, 13, 12);

		if (rotation == OMAP_DSS_ROT_90 || rotation == OMAP_DSS_ROT_270)
			REG_FLD_MOD(dispc_reg_att[plane], 0x1, 18, 18);
		else
			REG_FLD_MOD(dispc_reg_att[plane], 0x0, 18, 18);
	} else {
		REG_FLD_MOD(dispc_reg_att[plane], 0, 13, 12);
		REG_FLD_MOD(dispc_reg_att[plane], 0, 18, 18);
	}
}

static int color_mode_to_bpp(enum omap_color_mode color_mode)
{
	switch (color_mode) {
	case OMAP_DSS_COLOR_CLUT1:
		return 1;
	case OMAP_DSS_COLOR_CLUT2:
		return 2;
	case OMAP_DSS_COLOR_CLUT4:
		return 4;
	case OMAP_DSS_COLOR_CLUT8:
		return 8;
	case OMAP_DSS_COLOR_RGB12U:
	case OMAP_DSS_COLOR_RGB16:
	case OMAP_DSS_COLOR_ARGB16:
	case OMAP_DSS_COLOR_YUV2:
	case OMAP_DSS_COLOR_UYVY:
		return 16;
	case OMAP_DSS_COLOR_RGB24P:
		return 24;
	case OMAP_DSS_COLOR_RGB24U:
	case OMAP_DSS_COLOR_ARGB32:
	case OMAP_DSS_COLOR_RGBA32:
	case OMAP_DSS_COLOR_RGBX32:
		return 32;
	default:
		BUG();
	}
}

static s32 calc_gfx_window_skip(void)
{
	enum omap_channel channel;
	enum omap_color_mode color_mode;
	unsigned int x0, x1, y0, y1, x, y;
	unsigned int gfxx, gfxy, gfxw, gfxh;
	unsigned int vid1x, vid1y, vid1w, vid1h;
	s32 pix_inc, row_inc, skip;
	u16 ps;

	/* GFXENABLE */
	if (REG_GET(dispc_reg_att[OMAP_DSS_GFX], 0, 0) == 0) {
		DSSDBG("gfx_window_skip: GFX not enabled\n");
		return 0;
	}

	/* VIDENABLE */
	if (REG_GET(dispc_reg_att[OMAP_DSS_VIDEO1], 0, 0) == 0) {
		DSSDBG("gfx_window_skip: VID1 not enabled\n");
		return 0;
	}

	/* GFXCHANNELOUT and VIDCHANNELOUT */
	channel = REG_GET(dispc_reg_att[OMAP_DSS_GFX], 8, 8);
	if (channel != REG_GET(dispc_reg_att[OMAP_DSS_VIDEO1], 16, 16)) {
		DSSDBG("gfx_window_skip: GFX and VID1 on different channels\n");
		return 0;
	}

	if (dispc_trans_key_enabled(channel)) {
		DSSDBG("gfx_window_skip: transparency color key enabled\n");
		return 0;
	}

	if (dispc_alpha_blending_enabled(channel)) {
		DSSDBG("gfx_window_skip: alpha blender enabled\n");
		return 0;
	}

	/* FIXME RGB12U, RGBX32, ARGB16, ARGB32, RGBA32 formats OK? */
	color_mode = _dispc_get_color_mode(OMAP_DSS_GFX);
	switch (color_mode) {
	case OMAP_DSS_COLOR_CLUT8:
	case OMAP_DSS_COLOR_RGB16:
	case OMAP_DSS_COLOR_YUV2:
	case OMAP_DSS_COLOR_UYVY:
	case OMAP_DSS_COLOR_RGB24P:
	case OMAP_DSS_COLOR_RGB24U:
	case OMAP_DSS_COLOR_RGB12U:
	case OMAP_DSS_COLOR_RGBX32:
	case OMAP_DSS_COLOR_ARGB16:
	case OMAP_DSS_COLOR_ARGB32:
	case OMAP_DSS_COLOR_RGBA32:
		break;
	default:
		DSSDBG("gfx_window_skip: unsupported GFX format\n");
		return 0;
	}

	/* FIXME RGB12U, RGBX32 formats OK? */
	switch (_dispc_get_color_mode(OMAP_DSS_VIDEO1)) {
	case OMAP_DSS_COLOR_RGB16:
	case OMAP_DSS_COLOR_YUV2:
	case OMAP_DSS_COLOR_UYVY:
	case OMAP_DSS_COLOR_RGB24P:
	case OMAP_DSS_COLOR_RGB24U:
	case OMAP_DSS_COLOR_RGB12U:
	case OMAP_DSS_COLOR_RGBX32:
		break;
	default:
		DSSDBG("gfx_window_skip: unsupported VID1 format\n");
		return 0;
	}

	gfxx = REG_GET(DISPC_GFX_POSITION, 10, 0);
	gfxy = REG_GET(DISPC_GFX_POSITION, 26, 16);
	gfxw = REG_GET(DISPC_GFX_SIZE, 10, 0) + 1;
	gfxh = REG_GET(DISPC_GFX_SIZE, 26, 16) + 1;

	vid1x = REG_GET(DISPC_VID_POSITION(0), 10, 0);
	vid1y = REG_GET(DISPC_VID_POSITION(0), 26, 16);
	vid1w = REG_GET(DISPC_VID_SIZE(0), 10, 0) + 1;
	vid1h = REG_GET(DISPC_VID_SIZE(0), 26, 16) + 1;

	x0 = max(vid1x, gfxx);
	y0 = max(vid1y, gfxy);
	x1 = min(vid1x + vid1w, gfxx + gfxw);
	y1 = min(vid1y + vid1h, gfxy + gfxh);

	if (x1 <= x0 || y1 <= y0) {
		DSSDBG("gfx_window_skip: GFX and VID1 do not overlap\n");
		return 0;
	}

	pix_inc = _dispc_get_pix_inc(OMAP_DSS_GFX);
	row_inc = _dispc_get_row_inc(OMAP_DSS_GFX);
	ps = color_mode_to_bpp(color_mode) / 8;

	x = x1 - x0;
	y = y1 - y0;

	if (x >= gfxw - 1 && y >= gfxh - 1) {
		/*
		 * If the overlay optimization is enabled with this condition
		 * being true, then disabling the overlay optimization results
		 * in a short burst of visible corruption.
		 *
		 * FIXME Root cause of corruption is unknown,
		 * only	seems to happen when GFX is using VRFB.
		 */
		DSSDBG("gfx_window_skip: Disabling overlay optimization to avoid corruption\n");
		return 0;
	}

	DSSDBG("gfx_window_skip: GFX w=%u, VID1 w=%u, "
	       "x=%u, y=%u, pix_inc=%u, row_inc=%u, ps=%u\n",
	       gfxw, vid1w, x, y, pix_inc, row_inc, ps);

	if (x == gfxw)
		/* Skip full lines */
		skip = y * (x * (pix_inc - 1 + ps) + row_inc - 1);
	else if (gfxx < vid1x && gfxx + gfxw > vid1x + vid1w)
		/* Skip the middle of the line */
		skip = x * (pix_inc - 1 + ps) + 1;
	else
		/* Skip the beginning or the end of the line */
		skip = x * (pix_inc - 1 + ps);

	DSSDBG("gfx_window_skip: skipping %d bytes\n", skip);

	return skip;
}

void dispc_set_overlay_optimization(void)
{
	s32 skip = calc_gfx_window_skip();

	REG_FLD_MOD(DISPC_CONTROL, skip ? 1 : 0, 12, 12);
	dispc_write_reg(DISPC_GFX_WINDOW_SKIP, skip);
}

static s32 pixinc(int pixels, u8 ps)
{
	if (pixels == 1)
		return 1;
	else if (pixels > 1)
		return 1 + (pixels - 1) * ps;
	else if (pixels < 0)
		return 1 - (-pixels + 1) * ps;
	else
		BUG();
}

static void calc_vrfb_rotation_offset(u8 rotation, bool mirror,
		u16 screen_width,
		u16 width, u16 height,
		enum omap_color_mode color_mode, bool fieldmode,
		unsigned int field_offset,
		unsigned *offset0, unsigned *offset1,
		s32 *row_inc, s32 *pix_inc)
{
	u8 ps;

	/* FIXME CLUT formats */
	switch (color_mode) {
	case OMAP_DSS_COLOR_CLUT1:
	case OMAP_DSS_COLOR_CLUT2:
	case OMAP_DSS_COLOR_CLUT4:
	case OMAP_DSS_COLOR_CLUT8:
		BUG();
		return;
	case OMAP_DSS_COLOR_YUV2:
	case OMAP_DSS_COLOR_UYVY:
		ps = 4;
		break;
	default:
		ps = color_mode_to_bpp(color_mode) / 8;
		break;
	}

	DSSDBG("calc_rot(%d): scrw %d, %dx%d\n", rotation, screen_width,
			width, height);

	/*
	 * field 0 = even field = bottom field
	 * field 1 = odd field = top field
	 */
	switch (rotation + mirror * 4) {
	case OMAP_DSS_ROT_0:
	case OMAP_DSS_ROT_180:
		/*
		 * If the pixel format is YUV or UYVY divide the width
		 * of the image by 2 for 0 and 180 degree rotation.
		 */
		if (color_mode == OMAP_DSS_COLOR_YUV2 ||
			color_mode == OMAP_DSS_COLOR_UYVY)
			width = width >> 1;
	case OMAP_DSS_ROT_90:
	case OMAP_DSS_ROT_270:
		*offset1 = 0;
		if (field_offset)
			*offset0 = field_offset * screen_width * ps;
		else
			*offset0 = 0;

		*row_inc = pixinc(1 + (screen_width - width) +
				(fieldmode ? screen_width : 0),
				ps);
		*pix_inc = pixinc(1, ps);
		break;

	case OMAP_DSS_ROT_0 + 4:
	case OMAP_DSS_ROT_180 + 4:
		/* If the pixel format is YUV or UYVY divide the width
		 * of the image by 2  for 0 degree and 180 degree
		 */
		if (color_mode == OMAP_DSS_COLOR_YUV2 ||
			color_mode == OMAP_DSS_COLOR_UYVY)
			width = width >> 1;
	case OMAP_DSS_ROT_90 + 4:
	case OMAP_DSS_ROT_270 + 4:
		*offset1 = 0;
		if (field_offset)
			*offset0 = field_offset * screen_width * ps;
		else
			*offset0 = 0;
		*row_inc = pixinc(1 - (screen_width + width) -
				(fieldmode ? screen_width : 0),
				ps);
		*pix_inc = pixinc(1, ps);
		break;

	default:
		BUG();
	}
}

static void calc_dma_rotation_offset(u8 rotation, bool mirror,
		u16 screen_width,
		u16 width, u16 height,
		enum omap_color_mode color_mode, bool fieldmode,
		unsigned int field_offset,
		unsigned *offset0, unsigned *offset1,
		s32 *row_inc, s32 *pix_inc)
{
	u8 ps;
	u16 fbw, fbh;

	/* FIXME CLUT formats */
	switch (color_mode) {
	case OMAP_DSS_COLOR_CLUT1:
	case OMAP_DSS_COLOR_CLUT2:
	case OMAP_DSS_COLOR_CLUT4:
	case OMAP_DSS_COLOR_CLUT8:
		BUG();
		return;
	default:
		ps = color_mode_to_bpp(color_mode) / 8;
		break;
	}

	DSSDBG("calc_rot(%d): scrw %d, %dx%d\n", rotation, screen_width,
			width, height);

	/* width & height are overlay sizes, convert to fb sizes */

	if (rotation == OMAP_DSS_ROT_0 || rotation == OMAP_DSS_ROT_180) {
		fbw = width;
		fbh = height;
	} else {
		fbw = height;
		fbh = width;
	}

	/*
	 * field 0 = even field = bottom field
	 * field 1 = odd field = top field
	 */
	switch (rotation + mirror * 4) {
	case OMAP_DSS_ROT_0:
		*offset1 = 0;
		if (field_offset)
			*offset0 = *offset1 + field_offset * screen_width * ps;
		else
			*offset0 = *offset1;
		*row_inc = pixinc(1 + (screen_width - fbw) +
				(fieldmode ? screen_width : 0),
				ps);
		*pix_inc = pixinc(1, ps);
		break;
	case OMAP_DSS_ROT_90:
		*offset1 = screen_width * (fbh - 1) * ps;
		if (field_offset)
			*offset0 = *offset1 + field_offset * ps;
		else
			*offset0 = *offset1;
		*row_inc = pixinc(screen_width * (fbh - 1) + 1 +
				(fieldmode ? 1 : 0), ps);
		*pix_inc = pixinc(-screen_width, ps);
		break;
	case OMAP_DSS_ROT_180:
		*offset1 = (screen_width * (fbh - 1) + fbw - 1) * ps;
		if (field_offset)
			*offset0 = *offset1 - field_offset * screen_width * ps;
		else
			*offset0 = *offset1;
		*row_inc = pixinc(-1 -
				(screen_width - fbw) -
				(fieldmode ? screen_width : 0),
				ps);
		*pix_inc = pixinc(-1, ps);
		break;
	case OMAP_DSS_ROT_270:
		*offset1 = (fbw - 1) * ps;
		if (field_offset)
			*offset0 = *offset1 - field_offset * ps;
		else
			*offset0 = *offset1;
		*row_inc = pixinc(-screen_width * (fbh - 1) - 1 -
				(fieldmode ? 1 : 0), ps);
		*pix_inc = pixinc(screen_width, ps);
		break;

	/* mirroring */
	case OMAP_DSS_ROT_0 + 4:
		*offset1 = (fbw - 1) * ps;
		if (field_offset)
			*offset0 = *offset1 + field_offset * screen_width * ps;
		else
			*offset0 = *offset1;
		*row_inc = pixinc(screen_width * 2 - 1 +
				(fieldmode ? screen_width : 0),
				ps);
		*pix_inc = pixinc(-1, ps);
		break;

	case OMAP_DSS_ROT_90 + 4:
		*offset1 = 0;
		if (field_offset)
			*offset0 = *offset1 + field_offset * ps;
		else
			*offset0 = *offset1;
		*row_inc = pixinc(-screen_width * (fbh - 1) + 1 +
				(fieldmode ? 1 : 0),
				ps);
		*pix_inc = pixinc(screen_width, ps);
		break;

	case OMAP_DSS_ROT_180 + 4:
		*offset1 = screen_width * (fbh - 1) * ps;
		if (field_offset)
			*offset0 = *offset1 - field_offset * screen_width * ps;
		else
			*offset0 = *offset1;
		*row_inc = pixinc(1 - screen_width * 2 -
				(fieldmode ? screen_width : 0),
				ps);
		*pix_inc = pixinc(1, ps);
		break;

	case OMAP_DSS_ROT_270 + 4:
		*offset1 = (screen_width * (fbh - 1) + fbw - 1) * ps;
		if (field_offset)
			*offset0 = *offset1 - field_offset * ps;
		else
			*offset0 = *offset1;
		*row_inc = pixinc(screen_width * (fbh - 1) - 1 -
				(fieldmode ? 1 : 0),
				ps);
		*pix_inc = pixinc(-screen_width, ps);
		break;

	default:
		BUG();
	}
}

static struct omap_overlay_manager *
manager_for_channel(enum omap_channel channel_out)
{
	int i;

	for (i = 0; i < omap_dss_get_num_overlay_managers(); ++i) {
		struct omap_overlay_manager *mgr;
		mgr = omap_dss_get_overlay_manager(i);

		if (mgr->id == channel_out)
			return mgr;
	}

	return NULL;
}

static u32 get_pixel_clock(enum omap_channel channel_out)
{
	struct omap_video_timings t;
	struct omap_overlay_manager *mgr = manager_for_channel(channel_out);

	if (!mgr || !mgr->display || !mgr->display->get_timings)
		return 0;

	mgr->display->get_timings(mgr->display, &t);

	DSSDBG("PCLK = %u\n", t.pixel_clock * 1000);

	return t.pixel_clock * 1000;
}

static u32 get_display_width(enum omap_channel channel_out)
{
	struct omap_video_timings t;
	struct omap_overlay_manager *mgr = manager_for_channel(channel_out);

	if (!mgr || !mgr->display || !mgr->display->get_timings)
		return 0;

	mgr->display->get_timings(mgr->display, &t);

	DSSDBG("PPL = %u\n", t.x_res);

	return t.x_res;
}

static void dispc_get_lcd_divisor(int *lck_div, int *pck_div);

static int check_hblank_len(u16 width, u16 height,
		u16 out_width, u16 out_height,
		enum omap_channel channel_out)
{
	static const u8 limits[3] = { 8, 10, 20 };
	int i = 0;
	int lcd, pcd;
	struct omap_video_timings t;
	struct omap_overlay_manager *mgr = manager_for_channel(channel_out);

	if (!mgr || !mgr->display || !mgr->display->get_timings)
		return -ENODEV;

	mgr->display->get_timings(mgr->display, &t);

	enable_clocks(1);

	dispc_get_lcd_divisor(&lcd, &pcd);

	enable_clocks(0);

	if (out_height < height)
		i++;
	if (out_width < width)
		i++;

	DSSDBG("(hbp + hsw + hfp) * pcd = %u (limit = %u)\n",
	       (t.hbp + t.hsw + t.hfp) * pcd, limits[i]);

	if ((t.hbp + t.hsw + t.hfp) * pcd <= limits[i])
		return -EINVAL;

	return 0;
}

static int check_horiz_timing(enum omap_channel channel_out, u16 pos_x,
		u16 width, u16 height, u16 out_width, u16 out_height,
		enum omap_color_mode color_mode, bool five_taps)
{
	unsigned int nonactive;
	int lcd, pcd, ds = DIV_ROUND_UP(height, out_height);
	struct omap_overlay_manager *mgr = manager_for_channel(channel_out);
	struct omap_video_timings t;

	/* FIXME add checks for 3-tap filter once the limitations are known */
	if (!five_taps)
		return 0;

	/* Convert width to 4 byte units */
	width = DIV_ROUND_UP(width * color_mode_to_bpp(color_mode), 32);

	if (!mgr || !mgr->display || !mgr->display->get_timings)
		return -ENODEV;

	mgr->display->get_timings(mgr->display, &t);

	nonactive = t.x_res + t.hfp + t.hsw + t.hbp - out_width;

	enable_clocks(1);
	dispc_get_lcd_divisor(&lcd, &pcd);
	enable_clocks(0);

	DSSDBG("(nonactive - pos_x) * pcd = %u, max(0, ds - 2) * width = %d\n",
	       (nonactive - pos_x) * pcd, max(0, ds - 2) * width);
	DSSDBG("nonactive * pcd = %u, max(0, ds - 1) * width = %d\n",
	       nonactive * pcd, max(0, ds - 1) * width);

	/*
	 * At least ds-2 lines must have already been fetched
	 * before the display active video period starts.
	 */
	if ((nonactive - pos_x) * pcd < max(0, ds - 2) * width)
		return -EINVAL;

	/*
	 * Only one line can be fetched during the overlay active
	 * period, the rest have to be fetched during the inactive
	 * period.
	 */
	if (nonactive * pcd < max(0, ds - 1) * width)
		return -EINVAL;

	return 0;
}

static unsigned long calc_fclk_five_taps(u16 width, u16 height,
		u16 out_width, u16 out_height, enum omap_color_mode color_mode,
		enum omap_channel channel_out)
{
	u32 fclk = 0;
	u64 tmp, pclk = get_pixel_clock(channel_out);

	if (!out_height || !out_height)
		return 0;

	if (height > out_height) {
		unsigned int ppl = get_display_width(channel_out);
		if (!ppl)
			return 0;

		tmp = pclk * height * out_width;
		do_div(tmp, 2 * out_height * ppl);
		fclk = tmp;

		if (height > 2 * out_height) {
			if (ppl == out_width)
				return 0;

			tmp = pclk * (height - 2 * out_height) * out_width;
			do_div(tmp, 2 * out_height * (ppl - out_width));
			fclk = max(fclk, (u32) tmp);
		}
	}

	if (width > out_width) {
		tmp = pclk * width;
		do_div(tmp, out_width);
		fclk = max(fclk, (u32) tmp);

		if (color_mode == OMAP_DSS_COLOR_RGB24U)
			fclk <<= 1;
	}

	return fclk;
}

static unsigned long calc_fclk(u16 width, u16 height,
		u16 out_width, u16 out_height,
		enum omap_channel channel_out)
{
	unsigned int hf, vf;

	/*
	 * FIXME how to determine the 'A' factor
	 * for the no downscaling case ?
	 */

	if (width > 3 * out_width)
		hf = 4;
	else if (width > 2 * out_width)
		hf = 3;
	else if (width > out_width)
		hf = 2;
	else
		hf = 1;

	if (height > out_height)
		vf = 2;
	else
		vf = 1;

	return get_pixel_clock(channel_out) * vf * hf;
}

static int _dispc_setup_plane(enum omap_plane plane,
		enum omap_channel channel_out,
		u32 paddr, u16 screen_width,
		u16 pos_x, u16 pos_y,
		u16 width, u16 height,
		u16 out_width, u16 out_height,
		enum omap_color_mode color_mode,
		bool ilace,
		enum omap_dss_rotation_type rotation_type,
		u8 rotation, int mirror,
		u8 global_alpha)
{
	const int maxdownscale = cpu_is_omap34xx() ? 4 : 2;
	bool five_taps = 0;
	bool fieldmode = 0;
	int cconv = 0;
	unsigned offset0, offset1;
	s32 row_inc;
	s32 pix_inc;
	u16 frame_height = height;
	unsigned int field_offset = 0;

	if (paddr == 0)
		return -EINVAL;

	if (ilace && height == out_height)
		fieldmode = 1;

	if (ilace) {
		if (fieldmode)
			height /= 2;
		pos_y /= 2;
		out_height /= 2;

		DSSDBG("adjusting for ilace: height %d, pos_y %d, "
				"out_height %d\n",
				height, pos_y, out_height);
	}

	if (plane == OMAP_DSS_GFX) {
		if (width != out_width || height != out_height)
			return -EINVAL;

		switch (color_mode) {
		case OMAP_DSS_COLOR_ARGB16:
		case OMAP_DSS_COLOR_ARGB32:
		case OMAP_DSS_COLOR_RGBA32:
		case OMAP_DSS_COLOR_RGBX32:
			if (cpu_is_omap24xx())
				return -EINVAL;
			/* fall through */
		case OMAP_DSS_COLOR_RGB12U:
		case OMAP_DSS_COLOR_RGB16:
		case OMAP_DSS_COLOR_RGB24P:
		case OMAP_DSS_COLOR_RGB24U:
			break;

		default:
			return -EINVAL;
		}
	} else {
		/* video plane */

		unsigned long fclk = 0;

		if (out_width < width / maxdownscale ||
		   out_width > width * 8)
			return -EINVAL;

		if (out_height < height / maxdownscale ||
		   out_height > height * 8)
			return -EINVAL;

		switch (color_mode) {
		case OMAP_DSS_COLOR_RGBX32:
		case OMAP_DSS_COLOR_RGB12U:
			if (cpu_is_omap24xx())
				return -EINVAL;
			/* fall through */
		case OMAP_DSS_COLOR_RGB16:
		case OMAP_DSS_COLOR_RGB24P:
		case OMAP_DSS_COLOR_RGB24U:
			break;

		case OMAP_DSS_COLOR_ARGB16:
		case OMAP_DSS_COLOR_ARGB32:
		case OMAP_DSS_COLOR_RGBA32:
			if (cpu_is_omap24xx())
				return -EINVAL;
			if (plane == OMAP_DSS_VIDEO1)
				return -EINVAL;
			break;

		case OMAP_DSS_COLOR_YUV2:
		case OMAP_DSS_COLOR_UYVY:
			cconv = 1;
			break;

		default:
			return -EINVAL;
		}

		if (check_hblank_len(width, height, out_width, out_height,
				channel_out))
			return -EINVAL;

		/* Must use 5-tap filter? */
		five_taps = height > out_height * 2;

		if (!five_taps) {
			fclk = calc_fclk(width, height,
					out_width, out_height, channel_out);

			/* Try 5-tap filter if 3-tap fclk is too high */
			if (cpu_is_omap34xx() && height > out_height &&
					fclk > dispc_fclk_rate())
				five_taps = true;
		}

		if (width > (2048 >> five_taps))
			return -EINVAL;

		if (check_horiz_timing(channel_out, pos_x, width, height,
				out_width, out_height, color_mode, five_taps)) {
			DSSDBG("horizontal timing too tight\n");
			return -EINVAL;
		}

		if (five_taps)
			fclk = calc_fclk_five_taps(width, height,
					out_width, out_height,
					color_mode, channel_out);

		DSSDBG("required fclk rate = %lu Hz\n", fclk);
		DSSDBG("current fclk rate = %lu Hz\n", dispc_fclk_rate());

		if (!fclk || fclk > dispc_fclk_rate())
			return -EINVAL;
	}

	if (ilace && !fieldmode) {
		/*
		 * when downscaling the bottom field may have to start several
		 * source lines below the top field. Unfortunately ACCUI
		 * registers will only hold the fractional part of the offset
		 * so the integer part must be added to the base address of the
		 * bottom field.
		 */
		if (!height || height == out_height)
			field_offset = 0;
		else
			field_offset = height / out_height / 2;
	}

	/* Fields are independent but interleaved in memory. */
	if (fieldmode)
		field_offset = 1;

	if (rotation_type == OMAP_DSS_ROT_DMA)
		calc_dma_rotation_offset(rotation, mirror,
				screen_width, width, frame_height, color_mode,
				fieldmode, field_offset,
				&offset0, &offset1, &row_inc, &pix_inc);
	else
		calc_vrfb_rotation_offset(rotation, mirror,
				screen_width, width, frame_height, color_mode,
				fieldmode, field_offset,
				&offset0, &offset1, &row_inc, &pix_inc);

	DSSDBG("offset0 %u, offset1 %u, row_inc %d, pix_inc %d\n",
			offset0, offset1, row_inc, pix_inc);

	_dispc_set_channel_out(plane, channel_out);
	_dispc_set_color_mode(plane, color_mode);

	_dispc_set_plane_ba0(plane, paddr + offset0);
	_dispc_set_plane_ba1(plane, paddr + offset1);

	_dispc_set_row_inc(plane, row_inc);
	_dispc_set_pix_inc(plane, pix_inc);

	DSSDBG("%d,%d %dx%d -> %dx%d\n", pos_x, pos_y, width, height,
			out_width, out_height);

	_dispc_set_plane_pos(plane, pos_x, pos_y);

	if (field_offset && !fieldmode)
		_dispc_set_pic_size(plane, width, height - field_offset);
	else
		_dispc_set_pic_size(plane, width, height);

	if (plane != OMAP_DSS_GFX) {
		_dispc_set_scaling(plane, width, height,
				   out_width, out_height,
				   ilace, five_taps, fieldmode);
		_dispc_set_vid_size(plane, out_width, out_height);
		_dispc_set_vid_color_conv(plane, cconv);
	}

	_dispc_set_rotation_attrs(plane, rotation, mirror, color_mode);

	if (plane != OMAP_DSS_VIDEO1)
		_dispc_setup_global_alpha(plane, global_alpha);

	return 0;
}

static void _dispc_enable_plane(enum omap_plane plane, bool enable)
{
	REG_FLD_MOD(dispc_reg_att[plane], enable ? 1 : 0, 0, 0);
}

static void dispc_disable_isr(void *data, u32 mask)
{
	struct completion *compl = data;
	complete(compl);
}

static void _enable_lcd_out(bool enable)
{
	REG_FLD_MOD(DISPC_CONTROL, enable ? 1 : 0, 0, 0);
}

void dispc_enable_lcd_out(bool enable)
{
	struct completion frame_done_completion;
	bool is_on;
	int r;

	enable_clocks(1);

	/* When we disable LCD output, we need to wait until frame is done.
	 * Otherwise the DSS is still working, and turning off the clocks
	 * prevents DSS from going to OFF mode */
	is_on = REG_GET(DISPC_CONTROL, 0, 0);

	if (!enable && is_on) {
		init_completion(&frame_done_completion);

		r = omap_dispc_register_isr(dispc_disable_isr,
				&frame_done_completion,
				DISPC_IRQ_FRAMEDONE);

		if (r)
			DSSERR("failed to register FRAMEDONE isr\n");
	}

	_enable_lcd_out(enable);

	if (!enable && is_on) {
		if (!wait_for_completion_timeout(&frame_done_completion,
					msecs_to_jiffies(100)))
			DSSERR("timeout waiting for FRAME DONE\n");

		r = omap_dispc_unregister_isr(dispc_disable_isr,
				&frame_done_completion,
				DISPC_IRQ_FRAMEDONE);

		if (r)
			DSSERR("failed to unregister FRAMEDONE isr\n");
	}

	enable_clocks(0);
}

static void _enable_digit_out(bool enable)
{
	REG_FLD_MOD(DISPC_CONTROL, enable ? 1 : 0, 1, 1);
}

void dispc_enable_digit_errors(int enable)
{
	unsigned long flags;

	enable_clocks(1);

	spin_lock_irqsave(&dispc.irq_lock, flags);

	if (!enable) {
		/* When we enable digit output, we'll get an extra digit
		 * sync lost interrupt, that we need to ignore */
		dispc.irq_error_mask &= ~DISPC_IRQ_SYNC_LOST_DIGIT;
	} else {
		dispc.irq_error_mask = DISPC_IRQ_MASK_ERROR;
		dispc_write_reg(DISPC_IRQSTATUS, DISPC_IRQ_SYNC_LOST_DIGIT);
	}

	_omap_dispc_set_irqs();
	spin_unlock_irqrestore(&dispc.irq_lock, flags);

	enable_clocks(0);
}

void dispc_enable_digit_out(bool enable)
{
	struct completion frame_done_completion;
	int r;

	enable_clocks(1);

	if (REG_GET(DISPC_CONTROL, 1, 1) == enable) {
		enable_clocks(0);
		return;
	}

	/* When we disable digit output, we need to wait until fields are done.
	 * Otherwise the DSS is still working, and turning off the clocks
	 * prevents DSS from going to OFF mode. And when enabling, we need to
	 * wait for the extra sync losts */
	init_completion(&frame_done_completion);

	r = omap_dispc_register_isr(dispc_disable_isr, &frame_done_completion,
			DISPC_IRQ_EVSYNC_EVEN | DISPC_IRQ_EVSYNC_ODD);
	if (r)
		DSSERR("failed to register EVSYNC isr\n");

	_enable_digit_out(enable);

	/* XXX I understand from TRM that we should only wait for the
	 * current field to complete. But it seems we have to wait
	 * for both fields */
	if (!wait_for_completion_timeout(&frame_done_completion,
				msecs_to_jiffies(100)))
		DSSERR("timeout waiting for EVSYNC\n");

	if (!wait_for_completion_timeout(&frame_done_completion,
				msecs_to_jiffies(100)))
		DSSERR("timeout waiting for EVSYNC\n");

	r = omap_dispc_unregister_isr(dispc_disable_isr,
			&frame_done_completion,
			DISPC_IRQ_EVSYNC_EVEN | DISPC_IRQ_EVSYNC_ODD);
	if (r)
		DSSERR("failed to unregister EVSYNC isr\n");

	enable_clocks(0);
}

void dispc_lcd_enable_signal_polarity(bool act_high)
{
	enable_clocks(1);
	REG_FLD_MOD(DISPC_CONTROL, act_high ? 1 : 0, 29, 29);
	enable_clocks(0);
}

void dispc_lcd_enable_signal(bool enable)
{
	enable_clocks(1);
	REG_FLD_MOD(DISPC_CONTROL, enable ? 1 : 0, 28, 28);
	enable_clocks(0);
}

void dispc_pck_free_enable(bool enable)
{
	enable_clocks(1);
	REG_FLD_MOD(DISPC_CONTROL, enable ? 1 : 0, 27, 27);
	enable_clocks(0);
}

void dispc_enable_fifohandcheck(bool enable)
{
	enable_clocks(1);
	REG_FLD_MOD(DISPC_CONFIG, enable ? 1 : 0, 16, 16);
	enable_clocks(0);
}


void dispc_set_lcd_display_type(enum omap_lcd_display_type type)
{
	int mode;

	switch (type) {
	case OMAP_DSS_LCD_DISPLAY_STN:
		mode = 0;
		break;

	case OMAP_DSS_LCD_DISPLAY_TFT:
		mode = 1;
		break;

	default:
		BUG();
		return;
	}

	enable_clocks(1);
	REG_FLD_MOD(DISPC_CONTROL, mode, 3, 3);
	enable_clocks(0);
}

void dispc_set_loadmode(enum omap_dss_load_mode mode)
{
	enable_clocks(1);
	REG_FLD_MOD(DISPC_CONFIG, mode, 2, 1);
	enable_clocks(0);
}


void dispc_set_default_color(enum omap_channel channel, u32 color)
{
	const struct dispc_reg def_reg[] = { DISPC_DEFAULT_COLOR0,
				DISPC_DEFAULT_COLOR1 };

	enable_clocks(1);
	dispc_write_reg(def_reg[channel], color);
	enable_clocks(0);
}

u32 dispc_get_default_color(enum omap_channel channel)
{
	const struct dispc_reg def_reg[] = { DISPC_DEFAULT_COLOR0,
				DISPC_DEFAULT_COLOR1 };
	u32 l;

	BUG_ON(channel != OMAP_DSS_CHANNEL_DIGIT &&
	       channel != OMAP_DSS_CHANNEL_LCD);

	enable_clocks(1);
	l = dispc_read_reg(def_reg[channel]);
	enable_clocks(0);

	return l;
}

void dispc_set_trans_key(enum omap_channel ch,
		enum omap_dss_color_key_type type,
		u32 trans_key)
{
	const struct dispc_reg tr_reg[] = {
		DISPC_TRANS_COLOR0, DISPC_TRANS_COLOR1 };

	enable_clocks(1);
	if (ch == OMAP_DSS_CHANNEL_LCD)
		REG_FLD_MOD(DISPC_CONFIG, type, 11, 11);
	else /* OMAP_DSS_CHANNEL_DIGIT */
		REG_FLD_MOD(DISPC_CONFIG, type, 13, 13);

	dispc_write_reg(tr_reg[ch], trans_key);
	enable_clocks(0);
}

void dispc_get_trans_key(enum omap_channel ch,
		enum omap_dss_color_key_type *type,
		u32 *trans_key)
{
	const struct dispc_reg tr_reg[] = {
		DISPC_TRANS_COLOR0, DISPC_TRANS_COLOR1 };

	enable_clocks(1);
	if (type) {
		if (ch == OMAP_DSS_CHANNEL_LCD)
			*type = REG_GET(DISPC_CONFIG, 11, 11);
		else if (ch == OMAP_DSS_CHANNEL_DIGIT)
			*type = REG_GET(DISPC_CONFIG, 13, 13);
		else
			BUG();
	}

	if (trans_key)
		*trans_key = dispc_read_reg(tr_reg[ch]);
	enable_clocks(0);
}

void dispc_enable_trans_key(enum omap_channel ch, bool enable)
{
	enable_clocks(1);
	if (ch == OMAP_DSS_CHANNEL_LCD)
		REG_FLD_MOD(DISPC_CONFIG, enable, 10, 10);
	else /* OMAP_DSS_CHANNEL_DIGIT */
		REG_FLD_MOD(DISPC_CONFIG, enable, 12, 12);
	dispc_set_overlay_optimization();
	dispc_go(ch);
	enable_clocks(0);
}
void dispc_enable_alpha_blending(enum omap_channel ch, bool enable)
{
	if (cpu_is_omap24xx())
		return;

	enable_clocks(1);
	if (ch == OMAP_DSS_CHANNEL_LCD)
		REG_FLD_MOD(DISPC_CONFIG, enable, 18, 18);
	else /* OMAP_DSS_CHANNEL_DIGIT */
		REG_FLD_MOD(DISPC_CONFIG, enable, 19, 19);
	dispc_set_overlay_optimization();
	dispc_go(ch);
	enable_clocks(0);
}
bool dispc_alpha_blending_enabled(enum omap_channel ch)
{
	bool enabled;

	if (cpu_is_omap24xx())
		return false;

	enable_clocks(1);
	if (ch == OMAP_DSS_CHANNEL_LCD)
		enabled = REG_GET(DISPC_CONFIG, 18, 18);
	else if (ch == OMAP_DSS_CHANNEL_DIGIT)
		enabled = REG_GET(DISPC_CONFIG, 18, 18);
	else
		BUG();
	enable_clocks(0);

	return enabled;

}


bool dispc_trans_key_enabled(enum omap_channel ch)
{
	bool enabled;

	enable_clocks(1);
	if (ch == OMAP_DSS_CHANNEL_LCD)
		enabled = REG_GET(DISPC_CONFIG, 10, 10);
	else if (ch == OMAP_DSS_CHANNEL_DIGIT)
		enabled = REG_GET(DISPC_CONFIG, 12, 12);
	else BUG();
	enable_clocks(0);

	return enabled;
}


void dispc_set_tft_data_lines(u8 data_lines)
{
	int code;

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
		return;
	}

	enable_clocks(1);
	REG_FLD_MOD(DISPC_CONTROL, code, 9, 8);
	enable_clocks(0);
}

void dispc_set_parallel_interface_mode(enum omap_parallel_interface_mode mode)
{
	u32 l;
	int stallmode;
	int gpout0 = 1;
	int gpout1;

	switch (mode) {
	case OMAP_DSS_PARALLELMODE_BYPASS:
		stallmode = 0;
		gpout1 = 1;
		break;

	case OMAP_DSS_PARALLELMODE_RFBI:
		stallmode = 1;
		gpout1 = 0;
		break;

	case OMAP_DSS_PARALLELMODE_DSI:
		stallmode = 1;
		gpout1 = 1;
		break;

	default:
		BUG();
		return;
	}

	enable_clocks(1);

	l = dispc_read_reg(DISPC_CONTROL);

	l = FLD_MOD(l, stallmode, 11, 11);
	l = FLD_MOD(l, gpout0, 15, 15);
	l = FLD_MOD(l, gpout1, 16, 16);

	dispc_write_reg(DISPC_CONTROL, l);

	enable_clocks(0);
}

static void _dispc_set_lcd_timings(int hsw, int hfp, int hbp,
				   int vsw, int vfp, int vbp)
{
	u32 timing_h, timing_v;

	if (cpu_is_omap24xx() || omap_rev() < OMAP3430_REV_ES3_0) {
		BUG_ON(hsw < 1 || hsw > 64);
		BUG_ON(hfp < 1 || hfp > 256);
		BUG_ON(hbp < 1 || hbp > 256);

		BUG_ON(vsw < 1 || vsw > 64);
		BUG_ON(vfp < 0 || vfp > 255);
		BUG_ON(vbp < 0 || vbp > 255);

		timing_h = FLD_VAL(hsw-1, 5, 0) | FLD_VAL(hfp-1, 15, 8) |
			FLD_VAL(hbp-1, 27, 20);

		timing_v = FLD_VAL(vsw-1, 5, 0) | FLD_VAL(vfp, 15, 8) |
			FLD_VAL(vbp, 27, 20);
	} else {
		BUG_ON(hsw < 1 || hsw > 256);
		BUG_ON(hfp < 1 || hfp > 4096);
		BUG_ON(hbp < 1 || hbp > 4096);

		BUG_ON(vsw < 1 || vsw > 256);
		BUG_ON(vfp < 0 || vfp > 4095);
		BUG_ON(vbp < 0 || vbp > 4095);

		timing_h = FLD_VAL(hsw-1, 7, 0) | FLD_VAL(hfp-1, 19, 8) |
			FLD_VAL(hbp-1, 31, 20);

		timing_v = FLD_VAL(vsw-1, 7, 0) | FLD_VAL(vfp, 19, 8) |
			FLD_VAL(vbp, 31, 20);
	}

	enable_clocks(1);
	dispc_write_reg(DISPC_TIMING_H, timing_h);
	dispc_write_reg(DISPC_TIMING_V, timing_v);
	enable_clocks(0);
}

/* change name to mode? */
void dispc_set_lcd_timings(struct omap_video_timings *timings)
{
	unsigned xtot, ytot;
	unsigned long ht, vt;

	_dispc_set_lcd_timings(timings->hsw, timings->hfp, timings->hbp,
			timings->vsw, timings->vfp, timings->vbp);

	dispc_set_lcd_size(timings->x_res, timings->y_res);

	xtot = timings->x_res + timings->hfp + timings->hsw + timings->hbp;
	ytot = timings->y_res + timings->vfp + timings->vsw + timings->vbp;

	ht = (timings->pixel_clock * 1000) / xtot;
	vt = (timings->pixel_clock * 1000) / xtot / ytot;

	DSSDBG("xres %u yres %u\n", timings->x_res, timings->y_res);
	DSSDBG("pck %u\n", timings->pixel_clock);
	DSSDBG("hsw %d hfp %d hbp %d vsw %d vfp %d vbp %d\n",
			timings->hsw, timings->hfp, timings->hbp,
			timings->vsw, timings->vfp, timings->vbp);

	DSSDBG("hsync %luHz, vsync %luHz\n", ht, vt);
}

void dispc_set_lcd_divisor(u16 lck_div, u16 pck_div)
{
	BUG_ON(lck_div < 1);
	BUG_ON(pck_div < 2);

	enable_clocks(1);
	dispc_write_reg(DISPC_DIVISOR,
			FLD_VAL(lck_div, 23, 16) | FLD_VAL(pck_div, 7, 0));
	enable_clocks(0);
}

static void dispc_get_lcd_divisor(int *lck_div, int *pck_div)
{
	u32 l;
	l = dispc_read_reg(DISPC_DIVISOR);
	*lck_div = FLD_GET(l, 23, 16);
	*pck_div = FLD_GET(l, 7, 0);
}

unsigned long dispc_fclk_rate(void)
{
	unsigned long r = 0;

	if (dss_get_dispc_clk_source() == 0)
		r = dss_clk_get_rate(DSS_CLK_FCK1);
	else
#ifdef CONFIG_OMAP2_DSS_DSI
		r = dsi_get_dsi1_pll_rate();
#else
	BUG();
#endif
	return r;
}

unsigned long dispc_lclk_rate(void)
{
	int lcd;
	unsigned long r;
	u32 l;

	l = dispc_read_reg(DISPC_DIVISOR);

	lcd = FLD_GET(l, 23, 16);

	r = dispc_fclk_rate();

	return r / lcd;
}

unsigned long dispc_pclk_rate(void)
{
	int lcd, pcd;
	unsigned long r;
	u32 l;

	l = dispc_read_reg(DISPC_DIVISOR);

	lcd = FLD_GET(l, 23, 16);
	pcd = FLD_GET(l, 7, 0);

	r = dispc_fclk_rate();

	return r / lcd / pcd;
}

void dispc_dump_clocks(struct seq_file *s)
{
	int lcd, pcd;

	enable_clocks(1);

	dispc_get_lcd_divisor(&lcd, &pcd);

	seq_printf(s, "- dispc -\n");

	seq_printf(s, "dispc fclk source = %s\n",
			dss_get_dispc_clk_source() == 0 ?
			"dss1_alwon_fclk" : "dsi1_pll_fclk");

	seq_printf(s, "pixel clk = %lu / %d / %d = %lu\n",
			dispc_fclk_rate(),
			lcd, pcd,
			dispc_pclk_rate());

	enable_clocks(0);
}

void dispc_dump_regs(struct seq_file *s)
{
#define DUMPREG(r) seq_printf(s, "%-35s %08x\n", #r, dispc_read_reg(r))

	dss_clk_enable(DSS_CLK_ICK | DSS_CLK_FCK1);

	DUMPREG(DISPC_REVISION);
	DUMPREG(DISPC_SYSCONFIG);
	DUMPREG(DISPC_SYSSTATUS);
	DUMPREG(DISPC_IRQSTATUS);
	DUMPREG(DISPC_IRQENABLE);
	DUMPREG(DISPC_CONTROL);
	DUMPREG(DISPC_CONFIG);
	DUMPREG(DISPC_CAPABLE);
	DUMPREG(DISPC_DEFAULT_COLOR0);
	DUMPREG(DISPC_DEFAULT_COLOR1);
	DUMPREG(DISPC_TRANS_COLOR0);
	DUMPREG(DISPC_TRANS_COLOR1);
	DUMPREG(DISPC_LINE_STATUS);
	DUMPREG(DISPC_LINE_NUMBER);
	DUMPREG(DISPC_TIMING_H);
	DUMPREG(DISPC_TIMING_V);
	DUMPREG(DISPC_POL_FREQ);
	DUMPREG(DISPC_DIVISOR);
	DUMPREG(DISPC_GLOBAL_ALPHA);
	DUMPREG(DISPC_SIZE_DIG);
	DUMPREG(DISPC_SIZE_LCD);

	DUMPREG(DISPC_GFX_BA0);
	DUMPREG(DISPC_GFX_BA1);
	DUMPREG(DISPC_GFX_POSITION);
	DUMPREG(DISPC_GFX_SIZE);
	DUMPREG(DISPC_GFX_ATTRIBUTES);
	DUMPREG(DISPC_GFX_FIFO_THRESHOLD);
	DUMPREG(DISPC_GFX_FIFO_SIZE_STATUS);
	DUMPREG(DISPC_GFX_ROW_INC);
	DUMPREG(DISPC_GFX_PIXEL_INC);
	DUMPREG(DISPC_GFX_WINDOW_SKIP);
	DUMPREG(DISPC_GFX_TABLE_BA);

	DUMPREG(DISPC_DATA_CYCLE1);
	DUMPREG(DISPC_DATA_CYCLE2);
	DUMPREG(DISPC_DATA_CYCLE3);

	DUMPREG(DISPC_CPR_COEF_R);
	DUMPREG(DISPC_CPR_COEF_G);
	DUMPREG(DISPC_CPR_COEF_B);

	DUMPREG(DISPC_GFX_PRELOAD);

	DUMPREG(DISPC_VID_BA0(0));
	DUMPREG(DISPC_VID_BA1(0));
	DUMPREG(DISPC_VID_POSITION(0));
	DUMPREG(DISPC_VID_SIZE(0));
	DUMPREG(DISPC_VID_ATTRIBUTES(0));
	DUMPREG(DISPC_VID_FIFO_THRESHOLD(0));
	DUMPREG(DISPC_VID_FIFO_SIZE_STATUS(0));
	DUMPREG(DISPC_VID_ROW_INC(0));
	DUMPREG(DISPC_VID_PIXEL_INC(0));
	DUMPREG(DISPC_VID_FIR(0));
	DUMPREG(DISPC_VID_PICTURE_SIZE(0));
	DUMPREG(DISPC_VID_ACCU0(0));
	DUMPREG(DISPC_VID_ACCU1(0));

	DUMPREG(DISPC_VID_BA0(1));
	DUMPREG(DISPC_VID_BA1(1));
	DUMPREG(DISPC_VID_POSITION(1));
	DUMPREG(DISPC_VID_SIZE(1));
	DUMPREG(DISPC_VID_ATTRIBUTES(1));
	DUMPREG(DISPC_VID_FIFO_THRESHOLD(1));
	DUMPREG(DISPC_VID_FIFO_SIZE_STATUS(1));
	DUMPREG(DISPC_VID_ROW_INC(1));
	DUMPREG(DISPC_VID_PIXEL_INC(1));
	DUMPREG(DISPC_VID_FIR(1));
	DUMPREG(DISPC_VID_PICTURE_SIZE(1));
	DUMPREG(DISPC_VID_ACCU0(1));
	DUMPREG(DISPC_VID_ACCU1(1));

	DUMPREG(DISPC_VID_FIR_COEF_H(0, 0));
	DUMPREG(DISPC_VID_FIR_COEF_H(0, 1));
	DUMPREG(DISPC_VID_FIR_COEF_H(0, 2));
	DUMPREG(DISPC_VID_FIR_COEF_H(0, 3));
	DUMPREG(DISPC_VID_FIR_COEF_H(0, 4));
	DUMPREG(DISPC_VID_FIR_COEF_H(0, 5));
	DUMPREG(DISPC_VID_FIR_COEF_H(0, 6));
	DUMPREG(DISPC_VID_FIR_COEF_H(0, 7));
	DUMPREG(DISPC_VID_FIR_COEF_HV(0, 0));
	DUMPREG(DISPC_VID_FIR_COEF_HV(0, 1));
	DUMPREG(DISPC_VID_FIR_COEF_HV(0, 2));
	DUMPREG(DISPC_VID_FIR_COEF_HV(0, 3));
	DUMPREG(DISPC_VID_FIR_COEF_HV(0, 4));
	DUMPREG(DISPC_VID_FIR_COEF_HV(0, 5));
	DUMPREG(DISPC_VID_FIR_COEF_HV(0, 6));
	DUMPREG(DISPC_VID_FIR_COEF_HV(0, 7));
	DUMPREG(DISPC_VID_CONV_COEF(0, 0));
	DUMPREG(DISPC_VID_CONV_COEF(0, 1));
	DUMPREG(DISPC_VID_CONV_COEF(0, 2));
	DUMPREG(DISPC_VID_CONV_COEF(0, 3));
	DUMPREG(DISPC_VID_CONV_COEF(0, 4));
	DUMPREG(DISPC_VID_FIR_COEF_V(0, 0));
	DUMPREG(DISPC_VID_FIR_COEF_V(0, 1));
	DUMPREG(DISPC_VID_FIR_COEF_V(0, 2));
	DUMPREG(DISPC_VID_FIR_COEF_V(0, 3));
	DUMPREG(DISPC_VID_FIR_COEF_V(0, 4));
	DUMPREG(DISPC_VID_FIR_COEF_V(0, 5));
	DUMPREG(DISPC_VID_FIR_COEF_V(0, 6));
	DUMPREG(DISPC_VID_FIR_COEF_V(0, 7));

	DUMPREG(DISPC_VID_FIR_COEF_H(1, 0));
	DUMPREG(DISPC_VID_FIR_COEF_H(1, 1));
	DUMPREG(DISPC_VID_FIR_COEF_H(1, 2));
	DUMPREG(DISPC_VID_FIR_COEF_H(1, 3));
	DUMPREG(DISPC_VID_FIR_COEF_H(1, 4));
	DUMPREG(DISPC_VID_FIR_COEF_H(1, 5));
	DUMPREG(DISPC_VID_FIR_COEF_H(1, 6));
	DUMPREG(DISPC_VID_FIR_COEF_H(1, 7));
	DUMPREG(DISPC_VID_FIR_COEF_HV(1, 0));
	DUMPREG(DISPC_VID_FIR_COEF_HV(1, 1));
	DUMPREG(DISPC_VID_FIR_COEF_HV(1, 2));
	DUMPREG(DISPC_VID_FIR_COEF_HV(1, 3));
	DUMPREG(DISPC_VID_FIR_COEF_HV(1, 4));
	DUMPREG(DISPC_VID_FIR_COEF_HV(1, 5));
	DUMPREG(DISPC_VID_FIR_COEF_HV(1, 6));
	DUMPREG(DISPC_VID_FIR_COEF_HV(1, 7));
	DUMPREG(DISPC_VID_CONV_COEF(1, 0));
	DUMPREG(DISPC_VID_CONV_COEF(1, 1));
	DUMPREG(DISPC_VID_CONV_COEF(1, 2));
	DUMPREG(DISPC_VID_CONV_COEF(1, 3));
	DUMPREG(DISPC_VID_CONV_COEF(1, 4));
	DUMPREG(DISPC_VID_FIR_COEF_V(1, 0));
	DUMPREG(DISPC_VID_FIR_COEF_V(1, 1));
	DUMPREG(DISPC_VID_FIR_COEF_V(1, 2));
	DUMPREG(DISPC_VID_FIR_COEF_V(1, 3));
	DUMPREG(DISPC_VID_FIR_COEF_V(1, 4));
	DUMPREG(DISPC_VID_FIR_COEF_V(1, 5));
	DUMPREG(DISPC_VID_FIR_COEF_V(1, 6));
	DUMPREG(DISPC_VID_FIR_COEF_V(1, 7));

	DUMPREG(DISPC_VID_PRELOAD(0));
	DUMPREG(DISPC_VID_PRELOAD(1));

	dss_clk_disable(DSS_CLK_ICK | DSS_CLK_FCK1);
#undef DUMPREG
}

static void _dispc_set_pol_freq(bool onoff, bool rf, bool ieo, bool ipc,
				bool ihs, bool ivs, u8 acbi, u8 acb)
{
	u32 l = 0;

	DSSDBG("onoff %d rf %d ieo %d ipc %d ihs %d ivs %d acbi %d acb %d\n",
			onoff, rf, ieo, ipc, ihs, ivs, acbi, acb);

	l |= FLD_VAL(onoff, 17, 17);
	l |= FLD_VAL(rf, 16, 16);
	l |= FLD_VAL(ieo, 15, 15);
	l |= FLD_VAL(ipc, 14, 14);
	l |= FLD_VAL(ihs, 13, 13);
	l |= FLD_VAL(ivs, 12, 12);
	l |= FLD_VAL(acbi, 11, 8);
	l |= FLD_VAL(acb, 7, 0);

	enable_clocks(1);
	dispc_write_reg(DISPC_POL_FREQ, l);
	enable_clocks(0);
}

void dispc_set_pol_freq(struct omap_panel *panel)
{
	_dispc_set_pol_freq((panel->config & OMAP_DSS_LCD_ONOFF) != 0,
				 (panel->config & OMAP_DSS_LCD_RF) != 0,
				 (panel->config & OMAP_DSS_LCD_IEO) != 0,
				 (panel->config & OMAP_DSS_LCD_IPC) != 0,
				 (panel->config & OMAP_DSS_LCD_IHS) != 0,
				 (panel->config & OMAP_DSS_LCD_IVS) != 0,
				 panel->acbi, panel->acb);
}

void find_lck_pck_divs(bool is_tft, unsigned long req_pck, unsigned long fck,
		u16 *lck_div, u16 *pck_div)
{
	u16 pcd_min = is_tft ? 2 : 3;
	unsigned long best_pck;
	u16 best_ld, cur_ld;
	u16 best_pd, cur_pd;

	best_pck = 0;
	best_ld = 0;
	best_pd = 0;

	for (cur_ld = 1; cur_ld <= 255; ++cur_ld) {
		unsigned long lck = fck / cur_ld;

		for (cur_pd = pcd_min; cur_pd <= 255; ++cur_pd) {
			unsigned long pck = lck / cur_pd;
			long old_delta = abs(best_pck - req_pck);
			long new_delta = abs(pck - req_pck);

			if (best_pck == 0 || new_delta < old_delta) {
				best_pck = pck;
				best_ld = cur_ld;
				best_pd = cur_pd;

				if (pck == req_pck)
					goto found;
			}

			if (pck < req_pck)
				break;
		}

		if (lck / pcd_min < req_pck)
			break;
	}

found:
	*lck_div = best_ld;
	*pck_div = best_pd;
}

int dispc_calc_clock_div(bool is_tft, unsigned long req_pck,
		struct dispc_clock_info *cinfo)
{
	unsigned long prate;
	struct dispc_clock_info cur, best;
	int match = 0;
	int min_fck_per_pck;
	unsigned long fck_rate = dss_clk_get_rate(DSS_CLK_FCK1);

	if (cpu_is_omap34xx())
		prate = clk_get_rate(clk_get_parent(dispc.dpll4_m4_ck));
	else
		prate = 0;

	if (req_pck == dispc.cache_req_pck &&
			((cpu_is_omap34xx() && prate == dispc.cache_prate) ||
			 dispc.cache_cinfo.fck == fck_rate)) {
		DSSDBG("dispc clock info found from cache.\n");
		*cinfo = dispc.cache_cinfo;
		return 0;
	}

	min_fck_per_pck = CONFIG_OMAP2_DSS_MIN_FCK_PER_PCK;

	if (min_fck_per_pck &&
		req_pck * min_fck_per_pck > DISPC_MAX_FCK) {
		DSSERR("Requested pixel clock not possible with the current "
				"OMAP2_DSS_MIN_FCK_PER_PCK setting. Turning "
				"the constraint off.\n");
		min_fck_per_pck = 0;
	}

retry:
	memset(&cur, 0, sizeof(cur));
	memset(&best, 0, sizeof(best));

	if (cpu_is_omap24xx()) {
		/* XXX can we change the clock on omap2? */
		cur.fck = dss_clk_get_rate(DSS_CLK_FCK1);
		cur.fck_div = 1;

		match = 1;

		find_lck_pck_divs(is_tft, req_pck, cur.fck,
				&cur.lck_div, &cur.pck_div);

		cur.lck = cur.fck / cur.lck_div;
		cur.pck = cur.lck / cur.pck_div;

		best = cur;

		goto found;
	} else if (cpu_is_omap34xx()) {
		for (cur.fck_div = 16; cur.fck_div > 0; --cur.fck_div) {
			cur.fck = prate / cur.fck_div * 2;

			if (cur.fck > DISPC_MAX_FCK)
				continue;

			if (min_fck_per_pck &&
					cur.fck < req_pck * min_fck_per_pck)
				continue;

			match = 1;

			find_lck_pck_divs(is_tft, req_pck, cur.fck,
					&cur.lck_div, &cur.pck_div);

			cur.lck = cur.fck / cur.lck_div;
			cur.pck = cur.lck / cur.pck_div;

			if (abs(cur.pck - req_pck) < abs(best.pck - req_pck)) {
				best = cur;

				if (cur.pck == req_pck)
					goto found;
			}
		}
	} else {
		BUG();
	}

found:
	if (!match) {
		if (min_fck_per_pck) {
			DSSERR("Could not find suitable clock settings.\n"
					"Turning FCK/PCK constraint off and"
					"trying again.\n");
			min_fck_per_pck = 0;
			goto retry;
		}

		DSSERR("Could not find suitable clock settings.\n");

		return -EINVAL;
	}

	if (cinfo)
		*cinfo = best;

	dispc.cache_req_pck = req_pck;
	dispc.cache_prate = prate;
	dispc.cache_cinfo = best;

	return 0;
}

int dispc_set_clock_div(struct dispc_clock_info *cinfo)
{
	unsigned long prate;
	int r;

	if (cpu_is_omap34xx()) {
		prate = clk_get_rate(clk_get_parent(dispc.dpll4_m4_ck));
		DSSDBG("dpll4_m4 = %ld\n", prate);
	}

	DSSDBG("fck = %ld (%d)\n", cinfo->fck, cinfo->fck_div);
	DSSDBG("lck = %ld (%d)\n", cinfo->lck, cinfo->lck_div);
	DSSDBG("pck = %ld (%d)\n", cinfo->pck, cinfo->pck_div);

	if (cpu_is_omap34xx()) {
		r = clk_set_rate(dispc.dpll4_m4_ck, prate / cinfo->fck_div);
		if (r)
			return r;
	}

	dispc_set_lcd_divisor(cinfo->lck_div, cinfo->pck_div);

	return 0;
}

int dispc_get_clock_div(struct dispc_clock_info *cinfo)
{
	cinfo->fck = dss_clk_get_rate(DSS_CLK_FCK1);

	if (cpu_is_omap34xx()) {
		unsigned long prate;
		prate = clk_get_rate(clk_get_parent(dispc.dpll4_m4_ck));
		cinfo->fck_div = prate / (cinfo->fck / 2);
	} else {
		cinfo->fck_div = 0;
	}

	cinfo->lck_div = REG_GET(DISPC_DIVISOR, 23, 16);
	cinfo->pck_div = REG_GET(DISPC_DIVISOR, 7, 0);

	cinfo->lck = cinfo->fck / cinfo->lck_div;
	cinfo->pck = cinfo->lck / cinfo->pck_div;

	return 0;
}

/* dispc.irq_lock has to be locked by the caller */
static void _omap_dispc_set_irqs(void)
{
	u32 mask;
	u32 old_mask;
	int i;
	struct omap_dispc_isr_data *isr_data;

	mask = dispc.irq_error_mask;

	for (i = 0; i < DISPC_MAX_NR_ISRS; i++) {
		isr_data = &dispc.registered_isr[i];

		if (isr_data->isr == NULL)
			continue;

		mask |= isr_data->mask;
	}

	enable_clocks(1);

	old_mask = dispc_read_reg(DISPC_IRQENABLE);
	/* clear the irqstatus for newly enabled irqs */
	dispc_write_reg(DISPC_IRQSTATUS, (mask ^ old_mask) & mask);

	dispc_write_reg(DISPC_IRQENABLE, mask);

	enable_clocks(0);
}

int omap_dispc_register_isr(omap_dispc_isr_t isr, void *arg, u32 mask)
{
	int i;
	int ret;
	unsigned long flags;
	struct omap_dispc_isr_data *isr_data;

	if (isr == NULL)
		return -EINVAL;

	spin_lock_irqsave(&dispc.irq_lock, flags);

	/* check for duplicate entry */
	for (i = 0; i < DISPC_MAX_NR_ISRS; i++) {
		isr_data = &dispc.registered_isr[i];
		if (isr_data->isr == isr && isr_data->arg == arg &&
				isr_data->mask == mask) {
			ret = -EINVAL;
			goto err;
		}
	}

	isr_data = NULL;
	ret = -EBUSY;

	for (i = 0; i < DISPC_MAX_NR_ISRS; i++) {
		isr_data = &dispc.registered_isr[i];

		if (isr_data->isr != NULL)
			continue;

		isr_data->isr = isr;
		isr_data->arg = arg;
		isr_data->mask = mask;
		ret = 0;

		break;
	}

	_omap_dispc_set_irqs();

	spin_unlock_irqrestore(&dispc.irq_lock, flags);

	return 0;
err:
	spin_unlock_irqrestore(&dispc.irq_lock, flags);

	return ret;
}
EXPORT_SYMBOL(omap_dispc_register_isr);

int omap_dispc_unregister_isr(omap_dispc_isr_t isr, void *arg, u32 mask)
{
	int i;
	unsigned long flags;
	int ret = -EINVAL;
	struct omap_dispc_isr_data *isr_data;

	spin_lock_irqsave(&dispc.irq_lock, flags);

	for (i = 0; i < DISPC_MAX_NR_ISRS; i++) {
		isr_data = &dispc.registered_isr[i];
		if (isr_data->isr != isr || isr_data->arg != arg ||
				isr_data->mask != mask)
			continue;

		/* found the correct isr */

		isr_data->isr = NULL;
		isr_data->arg = NULL;
		isr_data->mask = 0;

		ret = 0;
		break;
	}

	if (ret == 0)
		_omap_dispc_set_irqs();

	spin_unlock_irqrestore(&dispc.irq_lock, flags);

	return ret;
}
EXPORT_SYMBOL(omap_dispc_unregister_isr);

#ifdef DEBUG
static void print_irq_status(u32 status)
{
	if ((status & dispc.irq_error_mask) == 0)
		return;

	printk(KERN_DEBUG "DISPC IRQ: 0x%x: ", status);

#define PIS(x) \
	if (status & DISPC_IRQ_##x) \
		printk(#x " ");
	PIS(GFX_FIFO_UNDERFLOW);
	PIS(OCP_ERR);
	PIS(VID1_FIFO_UNDERFLOW);
	PIS(VID2_FIFO_UNDERFLOW);
	PIS(SYNC_LOST);
	PIS(SYNC_LOST_DIGIT);
#undef PIS

	printk("\n");
}
#endif

/* Called from dss.c. Note that we don't touch clocks here,
 * but we presume they are on because we got an IRQ. However,
 * an irq handler may turn the clocks off, so we may not have
 * clock later in the function. */
void dispc_irq_handler(void)
{
	int i;
	u32 irqstatus;
	u32 handledirqs = 0;
	u32 unhandled_errors;
	struct omap_dispc_isr_data *isr_data;

	spin_lock(&dispc.irq_lock);

	irqstatus = dispc_read_reg(DISPC_IRQSTATUS);

#ifdef DEBUG
	if (dss_debug)
		print_irq_status(irqstatus);
#endif
	/* Ack the interrupt. Do it here before clocks are possibly turned
	 * off */
	dispc_write_reg(DISPC_IRQSTATUS, irqstatus);

	for (i = 0; i < DISPC_MAX_NR_ISRS; i++) {
		isr_data = &dispc.registered_isr[i];

		if (!isr_data->isr)
			continue;

		if (isr_data->mask & irqstatus) {
			isr_data->isr(isr_data->arg, irqstatus);
			handledirqs |= isr_data->mask;
		}
	}

	unhandled_errors = irqstatus & ~handledirqs & dispc.irq_error_mask;

	if (unhandled_errors) {
		dispc.error_irqs |= unhandled_errors;

		dispc.irq_error_mask &= ~unhandled_errors;
		_omap_dispc_set_irqs();

		schedule_work(&dispc.error_work);
	}

	spin_unlock(&dispc.irq_lock);
}

static void dispc_error_worker(struct work_struct *work)
{
	int i;
	u32 errors;
	unsigned long flags;

	spin_lock_irqsave(&dispc.irq_lock, flags);
	errors = dispc.error_irqs;
	dispc.error_irqs = 0;
	spin_unlock_irqrestore(&dispc.irq_lock, flags);

	if (errors & DISPC_IRQ_GFX_FIFO_UNDERFLOW)
		DSSERR("GFX_FIFO_UNDERFLOW\n");

	if (errors & DISPC_IRQ_VID1_FIFO_UNDERFLOW)
		DSSERR("VID1_FIFO_UNDERFLOW\n");

	if (errors & DISPC_IRQ_VID2_FIFO_UNDERFLOW)
		DSSERR("VID2_FIFO_UNDERFLOW\n");

	if (errors & DISPC_IRQ_SYNC_LOST) {
		DSSERR("SYNC_LOST, going to perform a soft reset\n");
		dss_soft_reset();
	}

	if (errors & DISPC_IRQ_SYNC_LOST_DIGIT) {
		DSSERR("SYNC_LOST_DIGIT, going to perform a soft reset\n");
		dss_soft_reset();
	}

	if (errors & DISPC_IRQ_OCP_ERR) {
		DSSERR("OCP_ERR\n");
		for (i = 0; i < omap_dss_get_num_overlay_managers(); ++i) {
			struct omap_overlay_manager *mgr;
			mgr = omap_dss_get_overlay_manager(i);

			if (mgr->caps & OMAP_DSS_OVL_CAP_DISPC)
				mgr->display->disable(mgr->display);
		}
	}

	spin_lock_irqsave(&dispc.irq_lock, flags);
	dispc.irq_error_mask |= errors;
	_omap_dispc_set_irqs();
	spin_unlock_irqrestore(&dispc.irq_lock, flags);
}

int omap_dispc_wait_for_irq_timeout(u32 irqmask, unsigned long timeout)
{
	void dispc_irq_wait_handler(void *data, u32 mask)
	{
		complete((struct completion *)data);
	}

	int r;
	DECLARE_COMPLETION_ONSTACK(completion);

	r = omap_dispc_register_isr(dispc_irq_wait_handler, &completion,
			irqmask);

	if (r)
		return r;

	timeout = wait_for_completion_timeout(&completion, timeout);

	omap_dispc_unregister_isr(dispc_irq_wait_handler, &completion, irqmask);

	if (timeout == 0)
		return -ETIMEDOUT;

	if (timeout == -ERESTARTSYS)
		return -ERESTARTSYS;

	return 0;
}

int omap_dispc_wait_for_irq_interruptible_timeout(u32 irqmask,
		unsigned long timeout)
{
	void dispc_irq_wait_handler(void *data, u32 mask)
	{
		complete((struct completion *)data);
	}

	int r;
	DECLARE_COMPLETION_ONSTACK(completion);

	r = omap_dispc_register_isr(dispc_irq_wait_handler, &completion,
			irqmask);

	if (r)
		return r;

	timeout = wait_for_completion_interruptible_timeout(&completion,
			timeout);

	omap_dispc_unregister_isr(dispc_irq_wait_handler, &completion, irqmask);

	if (timeout == 0)
		return -ETIMEDOUT;

	if (timeout == -ERESTARTSYS)
		return -ERESTARTSYS;

	return 0;
}

#ifdef CONFIG_OMAP2_DSS_FAKE_VSYNC
void dispc_fake_vsync_irq(void)
{
	u32 irqstatus = DISPC_IRQ_VSYNC;
	int i;

	for (i = 0; i < DISPC_MAX_NR_ISRS; i++) {
		struct omap_dispc_isr_data *isr_data;
		isr_data = &dispc.registered_isr[i];

		if (!isr_data->isr)
			continue;

		if (isr_data->mask & irqstatus)
			isr_data->isr(isr_data->arg, irqstatus);
	}
}
#endif

static void _omap_dispc_initialize_irq(void)
{
	unsigned long flags;

	spin_lock_irqsave(&dispc.irq_lock, flags);

	memset(dispc.registered_isr, 0, sizeof(dispc.registered_isr));

	dispc.irq_error_mask = DISPC_IRQ_MASK_ERROR;

	/* there's SYNC_LOST_DIGIT waiting after enabling the DSS,
	 * so clear it */
	dispc_write_reg(DISPC_IRQSTATUS, dispc_read_reg(DISPC_IRQSTATUS));

	_omap_dispc_set_irqs();

	spin_unlock_irqrestore(&dispc.irq_lock, flags);
}

void dispc_enable_sidle(void)
{
	REG_FLD_MOD(DISPC_SYSCONFIG, 2, 4, 3);	/* SIDLEMODE: smart idle */
}

void dispc_disable_sidle(void)
{
	REG_FLD_MOD(DISPC_SYSCONFIG, 1, 4, 3);	/* SIDLEMODE: no idle */
}

static void _omap_dispc_initial_config(void)
{
	u32 l;

	l = dispc_read_reg(DISPC_SYSCONFIG);
	l = FLD_MOD(l, 2, 13, 12);	/* MIDLEMODE: smart standby */
	l = FLD_MOD(l, 2, 4, 3);	/* SIDLEMODE: smart idle */
	l = FLD_MOD(l, 1, 2, 2);	/* ENWAKEUP */
	l = FLD_MOD(l, 1, 0, 0);	/* AUTOIDLE */
	dispc_write_reg(DISPC_SYSCONFIG, l);

	/* FUNCGATED */
	REG_FLD_MOD(DISPC_CONFIG, 1, 9, 9);

	/* L3 firewall setting: enable access to OCM RAM */
	if (cpu_is_omap24xx())
		__raw_writel(0x402000b0, IO_ADDRESS(0x680050a0));

	_dispc_setup_color_conv_coef();

	dispc_set_loadmode(OMAP_DSS_LOAD_FRAME_ONLY);
}

int dispc_init(void)
{
	u32 rev;

	spin_lock_init(&dispc.irq_lock);

	INIT_WORK(&dispc.error_work, dispc_error_worker);

	dispc.base = ioremap(DISPC_BASE, DISPC_SZ_REGS);
	if (!dispc.base) {
		DSSERR("can't ioremap DISPC\n");
		return -ENOMEM;
	}

	if (cpu_is_omap34xx()) {
		dispc.dpll4_m4_ck = clk_get(NULL, "dpll4_m4_ck");
		if (IS_ERR(dispc.dpll4_m4_ck)) {
			DSSERR("Failed to get dpll4_m4_ck\n");
			return -ENODEV;
		}
	}

	enable_clocks(1);

	_omap_dispc_initial_config();

	_omap_dispc_initialize_irq();

	dispc_save_context();

	rev = dispc_read_reg(DISPC_REVISION);
	printk(KERN_INFO "OMAP DISPC rev %d.%d\n",
	       FLD_GET(rev, 7, 4), FLD_GET(rev, 3, 0));

	enable_clocks(0);

	return 0;
}

void dispc_exit(void)
{
	if (cpu_is_omap34xx())
		clk_put(dispc.dpll4_m4_ck);
	iounmap(dispc.base);
}

int dispc_enable_plane(enum omap_plane plane, bool enable)
{
	DSSDBG("dispc_enable_plane %d, %d\n", plane, enable);

	enable_clocks(1);
	_dispc_enable_plane(plane, enable);
	enable_clocks(0);

	return 0;
}

int dispc_setup_plane(enum omap_plane plane, enum omap_channel channel_out,
		       u32 paddr, u16 screen_width,
		       u16 pos_x, u16 pos_y,
		       u16 width, u16 height,
		       u16 out_width, u16 out_height,
		       enum omap_color_mode color_mode,
		       bool ilace,
		       enum omap_dss_rotation_type rotation_type,
		       u8 rotation, bool mirror, u8 global_alpha)
{
	int r = 0;

	DSSDBG("dispc_setup_plane %d, ch %d, pa %x, sw %d, %d,%d, %dx%d -> "
	       "%dx%d, ilace %d, cmode %x, rot %d, mir %d\n",
	       plane, channel_out, paddr, screen_width, pos_x, pos_y,
	       width, height,
	       out_width, out_height,
	       ilace, color_mode,
	       rotation, mirror);

	enable_clocks(1);

	r = _dispc_setup_plane(plane, channel_out,
			   paddr, screen_width,
			   pos_x, pos_y,
			   width, height,
			   out_width, out_height,
			   color_mode, ilace,
			   rotation_type,
			   rotation, mirror,
			   global_alpha);

	enable_clocks(0);

	return r;
}

static int dispc_is_intersecting(int x1, int y1, int w1, int h1,
				 int x2, int y2, int w2, int h2)
{
	if (x1 >= (x2+w2))
		return 0;

	if ((x1+w1) <= x2)
		return 0;

	if (y1 >= (y2+h2))
		return 0;

	if ((y1+h1) <= y2)
		return 0;

	return 1;
}

static int dispc_is_overlay_scaled(struct omap_overlay_info *pi)
{
	if (pi->width != pi->out_width)
		return 1;

	if (pi->height != pi->out_height)
		return 1;

	return 0;
}

/* returns the area that needs updating */
void dispc_setup_partial_planes(struct omap_display *display,
				    u16 *xi, u16 *yi, u16 *wi, u16 *hi)
{
	struct omap_overlay_manager *mgr;
	int i;

	int x, y, w, h;

	x = *xi;
	y = *yi;
	w = *wi;
	h = *hi;

	DSSDBG("dispc_setup_partial_planes %d,%d %dx%d\n",
		*xi, *yi, *wi, *hi);


	mgr = display->manager;

	if (!mgr) {
		DSSDBG("no manager\n");
		return;
	}

	for (i = 0; i < mgr->num_overlays; i++) {
		struct omap_overlay *ovl;
		struct omap_overlay_info *pi;
		ovl = mgr->overlays[i];

		if (ovl->manager != mgr)
			continue;

		if ((ovl->caps & OMAP_DSS_OVL_CAP_SCALE) == 0)
			continue;

		pi = &ovl->info;

		if (!pi->enabled)
			continue;
		/*
		 * If the plane is intersecting and scaled, we
		 * enlarge the update region to accomodate the
		 * whole area
		 */

		if (dispc_is_intersecting(x, y, w, h,
					  pi->pos_x, pi->pos_y,
					  pi->out_width, pi->out_height)) {
			if (dispc_is_overlay_scaled(pi)) {

				int x1, y1, x2, y2;

				if (x > pi->pos_x)
					x1 = pi->pos_x;
				else
					x1 = x;

				if (y > pi->pos_y)
					y1 = pi->pos_y;
				else
					y1 = y;

				if ((x + w) < (pi->pos_x + pi->out_width))
					x2 = pi->pos_x + pi->out_width;
				else
					x2 = x + w;

				if ((y + h) < (pi->pos_y + pi->out_height))
					y2 = pi->pos_y + pi->out_height;
				else
					y2 = y + h;

				x = x1;
				y = y1;
				w = x2 - x1;
				h = y2 - y1;

				DSSDBG("Update area after enlarge due to "
					"scaling %d, %d %dx%d\n",
					x, y, w, h);
			}
		}
	}

	for (i = 0; i < mgr->num_overlays; i++) {
		struct omap_overlay *ovl = mgr->overlays[i];
		struct omap_overlay_info *pi = &ovl->info;

		int px = pi->pos_x;
		int py = pi->pos_y;
		int pw = pi->width;
		int ph = pi->height;
		int pow = pi->out_width;
		int poh = pi->out_height;
		u32 pa = pi->paddr;
		int psw = pi->screen_width;
		int bpp;

		if (ovl->manager != mgr)
			continue;

		/*
		 * If plane is not enabled or the update region
		 * does not intersect with the plane in question,
		 * we really disable the plane from hardware
		 */

		if (!pi->enabled ||
		    !dispc_is_intersecting(x, y, w, h,
					   px, py, pow, poh)) {
			dispc_enable_plane(ovl->id, 0);
			continue;
		}

		/* FIXME CLUT formats */
		switch (pi->color_mode) {
		case OMAP_DSS_COLOR_RGB12U:
		case OMAP_DSS_COLOR_RGB16:
		case OMAP_DSS_COLOR_ARGB16:
		case OMAP_DSS_COLOR_YUV2:
		case OMAP_DSS_COLOR_UYVY:
		case OMAP_DSS_COLOR_RGB24P:
		case OMAP_DSS_COLOR_RGB24U:
		case OMAP_DSS_COLOR_ARGB32:
		case OMAP_DSS_COLOR_RGBA32:
		case OMAP_DSS_COLOR_RGBX32:
			bpp = color_mode_to_bpp(pi->color_mode);
			break;

		default:
			BUG();
			return;
		}

		if (x > pi->pos_x) {
			px = 0;
			pw -= (x - pi->pos_x);
			pa += (x - pi->pos_x) * bpp / 8;
		} else {
			px = pi->pos_x - x;
		}

		if (y > pi->pos_y) {
			py = 0;
			ph -= (y - pi->pos_y);
			pa += (y - pi->pos_y) * psw * bpp / 8;
		} else {
			py = pi->pos_y - y;
		}

		if (w < (px+pw))
			pw -= (px+pw) - (w);

		if (h < (py+ph))
			ph -= (py+ph) - (h);

		/* Can't scale the GFX plane */
		if ((ovl->caps & OMAP_DSS_OVL_CAP_SCALE) == 0 ||
				dispc_is_overlay_scaled(pi) == 0) {
			pow = pw;
			poh = ph;
		}

		DSSDBG("calc  plane %d, %x, sw %d, %d,%d, %dx%d -> %dx%d\n",
				ovl->id, pa, psw, px, py, pw, ph, pow, poh);

		dispc_setup_plane(ovl->id, mgr->id,
				pa, psw,
				px, py,
				pw, ph,
				pow, poh,
				pi->color_mode, 0,
				pi->rotation_type,
				pi->rotation,
				pi->mirror,
				pi->global_alpha);

		if (dss_use_replication(display, ovl->info.color_mode))
			dispc_enable_replication(ovl->id, true);
		else
			dispc_enable_replication(ovl->id, false);

		dispc_enable_plane(ovl->id, 1);
	}

	*xi = x;
	*yi = y;
	*wi = w;
	*hi = h;

}

