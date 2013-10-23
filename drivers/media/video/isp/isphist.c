/*
 * isphist.c
 *
 * HISTOGRAM module for TI's OMAP3 Camera ISP
 *
 * Copyright (C) 2009 Texas Instruments, Inc.
 *
 * Contributors:
 *	Sergio Aguirre <saaguirre@ti.com>
 *	Troy Laramy
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <asm/cacheflush.h>

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/uaccess.h>

#include "isp.h"
#include "ispreg.h"
#include "isphist.h"
#include "ispmmu.h"

/* Structure for saving/restoring histogram module registers */
struct isp_reg isphist_reg_list[] = {
	{OMAP3_ISP_IOMEM_HIST, ISPHIST_CNT, 0},
	{OMAP3_ISP_IOMEM_HIST, ISPHIST_WB_GAIN, 0},
	{OMAP3_ISP_IOMEM_HIST, ISPHIST_R0_HORZ, 0},
	{OMAP3_ISP_IOMEM_HIST, ISPHIST_R0_VERT, 0},
	{OMAP3_ISP_IOMEM_HIST, ISPHIST_R1_HORZ, 0},
	{OMAP3_ISP_IOMEM_HIST, ISPHIST_R1_VERT, 0},
	{OMAP3_ISP_IOMEM_HIST, ISPHIST_R2_HORZ, 0},
	{OMAP3_ISP_IOMEM_HIST, ISPHIST_R2_VERT, 0},
	{OMAP3_ISP_IOMEM_HIST, ISPHIST_R3_HORZ, 0},
	{OMAP3_ISP_IOMEM_HIST, ISPHIST_R3_VERT, 0},
	{OMAP3_ISP_IOMEM_HIST, ISPHIST_ADDR, 0},
	{OMAP3_ISP_IOMEM_HIST, ISPHIST_RADD, 0},
	{OMAP3_ISP_IOMEM_HIST, ISPHIST_RADD_OFF, 0},
	{OMAP3_ISP_IOMEM_HIST, ISPHIST_H_V_INFO, 0},
	{0, ISP_TOK_TERM, 0}
};

static void isp_hist_print_status(struct isp_hist_device *isp_hist);

void __isp_hist_enable(struct isp_hist_device *isp_hist, u8 enable)
{
	if (enable)
		DPRINTK_ISPHIST("   histogram enabled \n");
	else
		DPRINTK_ISPHIST("   histogram disabled \n");

	isp_reg_and_or(isp_hist->dev, OMAP3_ISP_IOMEM_HIST, ISPHIST_PCR,
		       ~ISPHIST_PCR_EN,	(enable ? ISPHIST_PCR_EN : 0));
	isp_hist->hist_enable = enable;
}

/**
 * isp_hist_enable - Enables ISP Histogram submodule operation.
 * @enable: 1 - Enables the histogram submodule.
 *
 * Client should configure all the Histogram registers before calling this
 * function.
 **/
void isp_hist_enable(struct isp_hist_device *isp_hist, u8 enable)
{
	__isp_hist_enable(isp_hist, enable);
	isp_hist->pm_state = enable;
}

/**
 * isp_hist_suspend - Suspend ISP Histogram submodule.
 **/
void isp_hist_suspend(struct isp_hist_device *isp_hist)
{
	if (isp_hist->pm_state)
		__isp_hist_enable(isp_hist, 0);
}

/**
 * isp_hist_resume - Resume ISP Histogram submodule.
 **/
void isp_hist_resume(struct isp_hist_device *isp_hist)
{
	if (isp_hist->pm_state)
		__isp_hist_enable(isp_hist, 1);
}

int isp_hist_busy(struct isp_hist_device *isp_hist)
{
	return isp_reg_readl(isp_hist->dev, OMAP3_ISP_IOMEM_HIST, ISPHIST_PCR)
			     & ISPHIST_PCR_BUSY;
}


/**
 * isp_hist_update_regs - Helper function to update Histogram registers.
 **/
static void isp_hist_update_regs(struct isp_hist_device *isp_hist)
{
	isp_reg_writel(isp_hist->dev, isp_hist->regs.pcr, OMAP3_ISP_IOMEM_HIST,
		       ISPHIST_PCR);
	isp_reg_writel(isp_hist->dev, isp_hist->regs.cnt, OMAP3_ISP_IOMEM_HIST,
		       ISPHIST_CNT);
	isp_reg_writel(isp_hist->dev, isp_hist->regs.wb_gain,
		       OMAP3_ISP_IOMEM_HIST, ISPHIST_WB_GAIN);
	isp_reg_writel(isp_hist->dev, isp_hist->regs.r0_h, OMAP3_ISP_IOMEM_HIST,
		       ISPHIST_R0_HORZ);
	isp_reg_writel(isp_hist->dev, isp_hist->regs.r0_v, OMAP3_ISP_IOMEM_HIST,
		       ISPHIST_R0_VERT);
	isp_reg_writel(isp_hist->dev, isp_hist->regs.r1_h, OMAP3_ISP_IOMEM_HIST,
		       ISPHIST_R1_HORZ);
	isp_reg_writel(isp_hist->dev, isp_hist->regs.r1_v, OMAP3_ISP_IOMEM_HIST,
		       ISPHIST_R1_VERT);
	isp_reg_writel(isp_hist->dev, isp_hist->regs.r2_h, OMAP3_ISP_IOMEM_HIST,
		       ISPHIST_R2_HORZ);
	isp_reg_writel(isp_hist->dev, isp_hist->regs.r2_v, OMAP3_ISP_IOMEM_HIST,
		       ISPHIST_R2_VERT);
	isp_reg_writel(isp_hist->dev, isp_hist->regs.r3_h, OMAP3_ISP_IOMEM_HIST,
		       ISPHIST_R3_HORZ);
	isp_reg_writel(isp_hist->dev, isp_hist->regs.r3_v, OMAP3_ISP_IOMEM_HIST,
		       ISPHIST_R3_VERT);
	isp_reg_writel(isp_hist->dev, isp_hist->regs.hist_addr,
		       OMAP3_ISP_IOMEM_HIST, ISPHIST_ADDR);
	isp_reg_writel(isp_hist->dev, isp_hist->regs.hist_data,
		       OMAP3_ISP_IOMEM_HIST, ISPHIST_DATA);
	isp_reg_writel(isp_hist->dev, isp_hist->regs.hist_radd,
		       OMAP3_ISP_IOMEM_HIST, ISPHIST_RADD);
	isp_reg_writel(isp_hist->dev, isp_hist->regs.hist_radd_off,
		       OMAP3_ISP_IOMEM_HIST, ISPHIST_RADD_OFF);
	isp_reg_writel(isp_hist->dev, isp_hist->regs.h_v_info,
		       OMAP3_ISP_IOMEM_HIST, ISPHIST_H_V_INFO);
}

/**
 * isp_hist_isr - Callback from ISP driver for HIST interrupt.
 * @status: IRQ0STATUS in case of MMU error, 0 for hist interrupt.
 *          arg1 and arg2 Not used as of now.
 **/
static void isp_hist_isr(unsigned long status, isp_vbq_callback_ptr arg1,
			 void *arg2)
{
	struct isp_hist_device *isp_hist = arg2;

	isp_hist_enable(isp_hist, 0);

	if (!(status & HIST_DONE))
		return;

	if (!isp_hist->completed) {
		if (isp_hist->frame_req == isp_hist->frame_cnt) {
			isp_hist->frame_cnt = 0;
			isp_hist->frame_req = 0;
			isp_hist->completed = 1;
		} else {
			isp_hist_enable(isp_hist, 1);
			isp_hist->frame_cnt++;
		}
	}
}

/**
 * isp_hist_reset_mem - clear Histogram memory before start stats engine.
 *
 * Returns 0 after histogram memory was cleared.
 **/
static int isp_hist_reset_mem(struct isp_hist_device *isp_hist)
{
	int i;

	isp_reg_or(isp_hist->dev, OMAP3_ISP_IOMEM_HIST, ISPHIST_CNT,
		   ISPHIST_CNT_CLR_EN);

	for (i = 0; i < HIST_MEM_SIZE; i++)
		isp_reg_readl(isp_hist->dev, OMAP3_ISP_IOMEM_HIST,
			      ISPHIST_DATA);

	isp_reg_and(isp_hist->dev, OMAP3_ISP_IOMEM_HIST, ISPHIST_CNT,
		    ~ISPHIST_CNT_CLR_EN);

	return 0;
}

/**
 * isp_hist_set_params - Helper function to check and store user given params.
 * @user_cfg: Pointer to user configuration structure.
 *
 * Returns 0 on success configuration.
 **/
static int isp_hist_set_params(struct isp_hist_device *isp_hist,
			       struct isp_hist_config *user_cfg)
{

	int reg_num = 0;
	int bit_shift = 0;


	if (isp_hist_busy(isp_hist))
		return -EINVAL;

	if (user_cfg->input_bit_width > MIN_BIT_WIDTH)
		WRITE_DATA_SIZE(isp_hist->regs.cnt, 0);
	else
		WRITE_DATA_SIZE(isp_hist->regs.cnt, 1);

	WRITE_SOURCE(isp_hist->regs.cnt, user_cfg->hist_source);

	if (user_cfg->hist_source) {
		WRITE_HV_INFO(isp_hist->regs.h_v_info, user_cfg->hist_h_v_info);

		if ((user_cfg->hist_radd & ISP_32B_BOUNDARY_BUF) ==
		    user_cfg->hist_radd) {
			WRITE_RADD(isp_hist->regs.hist_radd,
				   user_cfg->hist_radd);
		} else {
			dev_err(isp_hist->dev,
				"hist: Address should be in 32 byte boundary"
				"\n");
			return -EINVAL;
		}

		if ((user_cfg->hist_radd_off & ISP_32B_BOUNDARY_OFFSET) ==
		    user_cfg->hist_radd_off) {
			WRITE_RADD_OFF(isp_hist->regs.hist_radd_off,
				       user_cfg->hist_radd_off);
		} else {
			dev_err(isp_hist->dev,
				"hist: Offset should be in 32 byte boundary\n");
			return -EINVAL;
		}

	}

	isp_hist_reset_mem(isp_hist);
	DPRINTK_ISPHIST("ISPHIST: Memory Cleared\n");
	isp_hist->frame_req = user_cfg->hist_frames;

	if (unlikely(user_cfg->wb_gain_R > MAX_WB_GAIN ||
		     user_cfg->wb_gain_RG > MAX_WB_GAIN ||
		     user_cfg->wb_gain_B > MAX_WB_GAIN ||
		     user_cfg->wb_gain_BG > MAX_WB_GAIN)) {
		dev_err(isp_hist->dev, "hist: Invalid WB gain\n");
		return -EINVAL;
	} else {
		WRITE_WB_R(isp_hist->regs.wb_gain, user_cfg->wb_gain_R);
		WRITE_WB_RG(isp_hist->regs.wb_gain, user_cfg->wb_gain_RG);
		WRITE_WB_B(isp_hist->regs.wb_gain, user_cfg->wb_gain_B);
		WRITE_WB_BG(isp_hist->regs.wb_gain, user_cfg->wb_gain_BG);
	}

	/* Regions size and position */

	if (user_cfg->num_regions > MAX_REGIONS)
		return -EINVAL;

	if (likely((user_cfg->reg0_hor & ISPHIST_REGHORIZ_HEND_MASK) -
		   ((user_cfg->reg0_hor & ISPHIST_REGHORIZ_HSTART_MASK) >>
		    ISPHIST_REGHORIZ_HSTART_SHIFT))) {
		WRITE_REG_HORIZ(isp_hist->regs.r0_h, user_cfg->reg0_hor);
		reg_num++;
	} else {
		dev_err(isp_hist->dev, "hist: Invalid Region parameters\n");
		return -EINVAL;
	}

	if (likely((user_cfg->reg0_ver & ISPHIST_REGVERT_VEND_MASK) -
		   ((user_cfg->reg0_ver & ISPHIST_REGVERT_VSTART_MASK) >>
		    ISPHIST_REGVERT_VSTART_SHIFT))) {
		WRITE_REG_VERT(isp_hist->regs.r0_v, user_cfg->reg0_ver);
	} else {
		dev_err(isp_hist->dev, "hist: Invalid Region parameters\n");
		return -EINVAL;
	}

	if (user_cfg->num_regions >= 1) {
		if (likely((user_cfg->reg1_hor & ISPHIST_REGHORIZ_HEND_MASK) -
			   ((user_cfg->reg1_hor &
			     ISPHIST_REGHORIZ_HSTART_MASK) >>
			    ISPHIST_REGHORIZ_HSTART_SHIFT))) {
			WRITE_REG_HORIZ(isp_hist->regs.r1_h,
					user_cfg->reg1_hor);
		} else {
			dev_err(isp_hist->dev,
				"hist: Invalid Region parameters\n");
			return -EINVAL;
		}

		if (likely((user_cfg->reg1_ver & ISPHIST_REGVERT_VEND_MASK) -
			   ((user_cfg->reg1_ver &
			     ISPHIST_REGVERT_VSTART_MASK) >>
			    ISPHIST_REGVERT_VSTART_SHIFT))) {
			WRITE_REG_VERT(isp_hist->regs.r1_v,
				       user_cfg->reg1_ver);
		} else {
			dev_err(isp_hist->dev,
				"hist: Invalid Region parameters\n");
			return -EINVAL;
		}
	}

	if (user_cfg->num_regions >= 2) {
		if (likely((user_cfg->reg2_hor & ISPHIST_REGHORIZ_HEND_MASK) -
			   ((user_cfg->reg2_hor &
			     ISPHIST_REGHORIZ_HSTART_MASK) >>
			    ISPHIST_REGHORIZ_HSTART_SHIFT))) {
			WRITE_REG_HORIZ(isp_hist->regs.r2_h,
					user_cfg->reg2_hor);
		} else {
			dev_err(isp_hist->dev,
				"hist: Invalid Region parameters\n");
			return -EINVAL;
		}

		if (likely((user_cfg->reg2_ver & ISPHIST_REGVERT_VEND_MASK) -
			   ((user_cfg->reg2_ver &
			     ISPHIST_REGVERT_VSTART_MASK) >>
			    ISPHIST_REGVERT_VSTART_SHIFT))) {
			WRITE_REG_VERT(isp_hist->regs.r2_v,
				       user_cfg->reg2_ver);
		} else {
			dev_err(isp_hist->dev,
				"hist: Invalid Region parameters\n");
			return -EINVAL;
		}
	}

	if (user_cfg->num_regions >= 3) {
		if (likely((user_cfg->reg3_hor & ISPHIST_REGHORIZ_HEND_MASK) -
			   ((user_cfg->reg3_hor &
			     ISPHIST_REGHORIZ_HSTART_MASK) >>
			    ISPHIST_REGHORIZ_HSTART_SHIFT))) {
			WRITE_REG_HORIZ(isp_hist->regs.r3_h,
					user_cfg->reg3_hor);
		} else {
			dev_err(isp_hist->dev,
				"hist: Invalid Region parameters\n");
			return -EINVAL;
		}

		if (likely((user_cfg->reg3_ver & ISPHIST_REGVERT_VEND_MASK) -
			   ((user_cfg->reg3_ver &
			     ISPHIST_REGVERT_VSTART_MASK) >>
			    ISPHIST_REGVERT_VSTART_SHIFT))) {
			WRITE_REG_VERT(isp_hist->regs.r3_v,
				       user_cfg->reg3_ver);
		} else {
			dev_err(isp_hist->dev,
				"hist: Invalid Region parameters\n");
			return -EINVAL;
		}
	}
	reg_num = user_cfg->num_regions;
	if (unlikely(((user_cfg->hist_bins > BINS_256) &&
		      (user_cfg->hist_bins != BINS_32)) ||
		     ((user_cfg->hist_bins == BINS_256) &&
		      reg_num != 0) || ((user_cfg->hist_bins ==
					 BINS_128) && reg_num >= 2))) {
		dev_err(isp_hist->dev, "hist: Invalid Bins Number: %d\n",
		       user_cfg->hist_bins);
		return -EINVAL;
	} else {
		WRITE_NUM_BINS(isp_hist->regs.cnt, user_cfg->hist_bins);
	}

	if (user_cfg->input_bit_width > MAX_BIT_WIDTH ||
	    user_cfg->input_bit_width < MIN_BIT_WIDTH) {
		dev_err(isp_hist->dev, "hist: Invalid Bit Width: %d\n",
		       user_cfg->input_bit_width);
		return -EINVAL;
	} else {
		switch (user_cfg->hist_bins) {
		case BINS_256:
			bit_shift = user_cfg->input_bit_width - 8;
			break;
		case BINS_128:
			bit_shift = user_cfg->input_bit_width - 7;
			break;
		case BINS_64:
			bit_shift = user_cfg->input_bit_width - 6;
			break;
		case BINS_32:
			bit_shift = user_cfg->input_bit_width - 5;
			break;
		default:
			return -EINVAL;
		}
		WRITE_BIT_SHIFT(isp_hist->regs.cnt, bit_shift);
	}

	isp_hist_update_regs(isp_hist);
	isp_hist->initialized = 1;

	return 0;
}

/**
 * isp_hist_configure - API to configure HIST registers.
 * @histcfg: Pointer to user configuration structure.
 *
 * Returns 0 on success configuration.
 **/
int isp_hist_configure(struct isp_hist_device *isp_hist,
		       struct isp_hist_config *histcfg)
{

	int ret = 0;

	if (NULL == histcfg) {
		dev_err(isp_hist->dev,
			"hist: Null argument in configuration. \n");
		return -EINVAL;
	}

	if (!isp_hist->initialized) {
		DPRINTK_ISPHIST("Setting callback for HISTOGRAM\n");
		ret = isp_set_callback(isp_hist->dev, CBK_HIST_DONE,
				       isp_hist_isr, (void *)NULL,
				       isp_hist);
		if (ret) {
			dev_err(isp_hist->dev, "hist: No callback for HIST\n");
			return ret;
		}
	}

	ret = isp_hist_set_params(isp_hist, histcfg);
	if (ret) {
		dev_err(isp_hist->dev, "hist: Invalid parameters! \n");
		return ret;
	}

	isp_hist->frame_cnt = 0;
	isp_hist->completed = 0;
	isp_hist_enable(isp_hist, 1);
	isp_hist_print_status(isp_hist);

	return 0;
}
EXPORT_SYMBOL(isp_hist_configure);

/**
 * isp_hist_request_statistics - Request statistics in Histogram.
 * @histdata: Pointer to data structure.
 *
 * This API allows the user to request for histogram statistics.
 *
 * Returns 0 on successful request.
 **/
int isp_hist_request_statistics(struct isp_hist_device *isp_hist,
				struct isp_hist_data *histdata)
{
	int i, ret;
	u32 curr;

	if (isp_hist_busy(isp_hist))
		return -EBUSY;

	if (!isp_hist->completed && isp_hist->initialized)
		return -EINVAL;

	isp_reg_or(isp_hist->dev, OMAP3_ISP_IOMEM_HIST, ISPHIST_CNT,
		   ISPHIST_CNT_CLR_EN);

	for (i = 0; i < HIST_MEM_SIZE; i++) {
		curr = isp_reg_readl(isp_hist->dev, OMAP3_ISP_IOMEM_HIST,
				     ISPHIST_DATA);
		ret = put_user(curr, histdata->hist_statistics_buf + i);
		if (ret) {
			dev_err(isp_hist->dev, "hist: Failed copy_to_user for "
			       "HIST stats buff, %d\n", ret);
		}
	}

	isp_reg_and(isp_hist->dev, OMAP3_ISP_IOMEM_HIST, ISPHIST_CNT,
		    ~ISPHIST_CNT_CLR_EN);
	isp_hist->completed = 0;
	return 0;
}
EXPORT_SYMBOL(isp_hist_request_statistics);

/**
 * isp_hist_init - Module Initialization.
 *
 * Returns 0 if successful.
 **/
int __init isp_hist_init(struct device *dev)
{
	struct isp_device *isp = dev_get_drvdata(dev);

	isp->isp_hist.dev = dev;

	return 0;
}

/**
 * isp_hist_cleanup - Module cleanup.
 **/
void isp_hist_cleanup(struct device *dev)
{
}

/**
 * isphist_save_context - Saves the values of the histogram module registers.
 **/
void isphist_save_context(struct device *dev)
{
	DPRINTK_ISPHIST(" Saving context\n");
	isp_save_context(dev, isphist_reg_list);
}
EXPORT_SYMBOL(isphist_save_context);

/**
 * isphist_restore_context - Restores the values of the histogram module regs.
 **/
void isphist_restore_context(struct device *dev)
{
	DPRINTK_ISPHIST(" Restoring context\n");
	isp_restore_context(dev, isphist_reg_list);
}
EXPORT_SYMBOL(isphist_restore_context);

/**
 * isp_hist_print_status - Debug print
 **/
static void isp_hist_print_status(struct isp_hist_device *isp_hist)
{
	DPRINTK_ISPHIST("ISPHIST_PCR = 0x%08x\n",
			isp_reg_readl(isp_hist->dev, OMAP3_ISP_IOMEM_HIST,
				      ISPHIST_PCR));
	DPRINTK_ISPHIST("ISPHIST_CNT = 0x%08x\n",
			isp_reg_readl(isp_hist->dev, OMAP3_ISP_IOMEM_HIST,
				      ISPHIST_CNT));
	DPRINTK_ISPHIST("ISPHIST_WB_GAIN = 0x%08x\n",
			isp_reg_readl(isp_hist->dev, OMAP3_ISP_IOMEM_HIST,
				      ISPHIST_WB_GAIN));
	DPRINTK_ISPHIST("ISPHIST_R0_HORZ = 0x%08x\n",
			isp_reg_readl(isp_hist->dev, OMAP3_ISP_IOMEM_HIST,
				      ISPHIST_R0_HORZ));
	DPRINTK_ISPHIST("ISPHIST_R0_VERT = 0x%08x\n",
			isp_reg_readl(isp_hist->dev, OMAP3_ISP_IOMEM_HIST,
				      ISPHIST_R0_VERT));
	DPRINTK_ISPHIST("ISPHIST_R1_HORZ = 0x%08x\n",
			isp_reg_readl(isp_hist->dev, OMAP3_ISP_IOMEM_HIST,
				      ISPHIST_R1_HORZ));
	DPRINTK_ISPHIST("ISPHIST_R1_VERT = 0x%08x\n",
			isp_reg_readl(isp_hist->dev, OMAP3_ISP_IOMEM_HIST,
				      ISPHIST_R1_VERT));
	DPRINTK_ISPHIST("ISPHIST_R2_HORZ = 0x%08x\n",
			isp_reg_readl(isp_hist->dev, OMAP3_ISP_IOMEM_HIST,
				      ISPHIST_R2_HORZ));
	DPRINTK_ISPHIST("ISPHIST_R2_VERT = 0x%08x\n",
			isp_reg_readl(isp_hist->dev, OMAP3_ISP_IOMEM_HIST,
				      ISPHIST_R2_VERT));
	DPRINTK_ISPHIST("ISPHIST_R3_HORZ = 0x%08x\n",
			isp_reg_readl(isp_hist->dev, OMAP3_ISP_IOMEM_HIST,
				      ISPHIST_R3_HORZ));
	DPRINTK_ISPHIST("ISPHIST_R3_VERT = 0x%08x\n",
			isp_reg_readl(isp_hist->dev, OMAP3_ISP_IOMEM_HIST,
				      ISPHIST_R3_VERT));
	DPRINTK_ISPHIST("ISPHIST_ADDR = 0x%08x\n",
			isp_reg_readl(isp_hist->dev, OMAP3_ISP_IOMEM_HIST,
				      ISPHIST_ADDR));
	DPRINTK_ISPHIST("ISPHIST_RADD = 0x%08x\n",
			isp_reg_readl(isp_hist->dev, OMAP3_ISP_IOMEM_HIST,
				      ISPHIST_RADD));
	DPRINTK_ISPHIST("ISPHIST_RADD_OFF = 0x%08x\n",
			isp_reg_readl(isp_hist->dev, OMAP3_ISP_IOMEM_HIST,
				      ISPHIST_RADD_OFF));
	DPRINTK_ISPHIST("ISPHIST_H_V_INFO = 0x%08x\n",
			isp_reg_readl(isp_hist->dev, OMAP3_ISP_IOMEM_HIST,
				      ISPHIST_H_V_INFO));
}
