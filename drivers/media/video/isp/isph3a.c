/*
 * isph3a.c
 *
 * H3A module for TI's OMAP3 Camera ISP
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

#include <linux/dma-mapping.h>
#include <linux/uaccess.h>

#include "isp.h"
#include "ispreg.h"
#include "isph3a.h"
#include "ispmmu.h"
#include "isppreview.h"

/* Structure for saving/restoring h3a module registers */
static struct isp_reg isph3a_reg_list[] = {
	{OMAP3_ISP_IOMEM_H3A, ISPH3A_PCR, 0}, /* Should be the first one */
	{OMAP3_ISP_IOMEM_H3A, ISPH3A_AEWWIN1, 0},
	{OMAP3_ISP_IOMEM_H3A, ISPH3A_AEWINSTART, 0},
	{OMAP3_ISP_IOMEM_H3A, ISPH3A_AEWINBLK, 0},
	{OMAP3_ISP_IOMEM_H3A, ISPH3A_AEWSUBWIN, 0},
	{OMAP3_ISP_IOMEM_H3A, ISPH3A_AEWBUFST, 0},
	{OMAP3_ISP_IOMEM_H3A, ISPH3A_AFPAX1, 0},
	{OMAP3_ISP_IOMEM_H3A, ISPH3A_AFPAX2, 0},
	{OMAP3_ISP_IOMEM_H3A, ISPH3A_AFPAXSTART, 0},
	{OMAP3_ISP_IOMEM_H3A, ISPH3A_AFIIRSH, 0},
	{OMAP3_ISP_IOMEM_H3A, ISPH3A_AFBUFST, 0},
	{OMAP3_ISP_IOMEM_H3A, ISPH3A_AFCOEF010, 0},
	{OMAP3_ISP_IOMEM_H3A, ISPH3A_AFCOEF032, 0},
	{OMAP3_ISP_IOMEM_H3A, ISPH3A_AFCOEF054, 0},
	{OMAP3_ISP_IOMEM_H3A, ISPH3A_AFCOEF076, 0},
	{OMAP3_ISP_IOMEM_H3A, ISPH3A_AFCOEF098, 0},
	{OMAP3_ISP_IOMEM_H3A, ISPH3A_AFCOEF0010, 0},
	{OMAP3_ISP_IOMEM_H3A, ISPH3A_AFCOEF110, 0},
	{OMAP3_ISP_IOMEM_H3A, ISPH3A_AFCOEF132, 0},
	{OMAP3_ISP_IOMEM_H3A, ISPH3A_AFCOEF154, 0},
	{OMAP3_ISP_IOMEM_H3A, ISPH3A_AFCOEF176, 0},
	{OMAP3_ISP_IOMEM_H3A, ISPH3A_AFCOEF198, 0},
	{OMAP3_ISP_IOMEM_H3A, ISPH3A_AFCOEF1010, 0},
	{0, ISP_TOK_TERM, 0}
};

static void isph3a_print_status(struct isp_h3a_device *isp_h3a);

/**
 * isph3a_aewb_setxtrastats - Receives extra statistics from prior frames.
 * @xtrastats: Pointer to structure containing extra statistics fields like
 *             field count and timestamp of frame.
 *
 * Called from update_vbq in camera driver
 **/
void isph3a_aewb_setxtrastats(struct isp_h3a_device *isp_h3a,
			      struct isph3a_aewb_xtrastats *xtrastats)
{
	int i;

	if (isp_h3a->active_buff == NULL)
		return;

	for (i = 0; i < H3A_MAX_BUFF; i++) {
		if (isp_h3a->buff[i].frame_num !=
					isp_h3a->active_buff->frame_num)
			continue;

		if (i == 0) {
			if (isp_h3a->buff[H3A_MAX_BUFF - 1].locked == 0) {
				isp_h3a->xtrastats[H3A_MAX_BUFF - 1] =
					*xtrastats;
			} else {
				isp_h3a->xtrastats[H3A_MAX_BUFF - 2] =
					*xtrastats;
			}
		} else if (i == 1) {
			if (isp_h3a->buff[0].locked == 0)
				isp_h3a->xtrastats[0] = *xtrastats;
			else {
				isp_h3a->xtrastats[H3A_MAX_BUFF - 1] =
					*xtrastats;
			}
		} else {
			if (isp_h3a->buff[i - 1].locked == 0)
				isp_h3a->xtrastats[i - 1] = *xtrastats;
			else
				isp_h3a->xtrastats[i - 2] = *xtrastats;
		}
		return;
	}
}
EXPORT_SYMBOL(isph3a_aewb_setxtrastats);

void __isph3a_aewb_enable(struct isp_h3a_device *isp_h3a, u8 enable)
{
	isp_reg_writel(isp_h3a->dev, IRQ0STATUS_H3A_AWB_DONE_IRQ,
		       OMAP3_ISP_IOMEM_MAIN, ISP_IRQ0STATUS);

	if (enable) {
		isp_h3a->regs.pcr |= ISPH3A_PCR_AEW_EN;
		DPRINTK_ISPH3A("    H3A enabled \n");
	} else {
		isp_h3a->regs.pcr &= ~ISPH3A_PCR_AEW_EN;
		DPRINTK_ISPH3A("    H3A disabled \n");
	}
	isp_reg_and_or(isp_h3a->dev, OMAP3_ISP_IOMEM_H3A, ISPH3A_PCR,
		       ~ISPH3A_PCR_AEW_EN, (enable ? ISPH3A_PCR_AEW_EN : 0));
	isp_h3a->aewb_config_local.aewb_enable = enable;
}

/**
 * isph3a_aewb_enable - Enables AE, AWB engine in the H3A module.
 * @enable: 1 - Enables the AE & AWB engine.
 *
 * Client should configure all the AE & AWB registers in H3A before this.
 **/
void isph3a_aewb_enable(struct isp_h3a_device *isp_h3a, u8 enable)
{
	__isph3a_aewb_enable(isp_h3a, enable);
	isp_h3a->pm_state = enable;
}

/**
 * isph3a_aewb_suspend - Suspend AE, AWB engine in the H3A module.
 **/
void isph3a_aewb_suspend(struct isp_h3a_device *isp_h3a)
{
	if (isp_h3a->pm_state)
		__isph3a_aewb_enable(isp_h3a, 0);
}

/**
 * isph3a_aewb_resume - Resume AE, AWB engine in the H3A module.
 **/
void isph3a_aewb_resume(struct isp_h3a_device *isp_h3a)
{
	if (isp_h3a->pm_state)
		__isph3a_aewb_enable(isp_h3a, 1);
}

int isph3a_aewb_busy(struct isp_h3a_device *isp_h3a)
{
	return isp_reg_readl(isp_h3a->dev, OMAP3_ISP_IOMEM_H3A, ISPH3A_PCR)
		& ISPH3A_PCR_BUSYAEAWB;
}

/**
 * isph3a_update_wb - Updates WB parameters.
 *
 * Needs to be called when no ISP Preview processing is taking place.
 **/
void isph3a_update_wb(struct isp_h3a_device *isp_h3a)
{
	struct isp_device *isp = dev_get_drvdata(isp_h3a->dev);

	if (isp_h3a->wb_update) {
		isppreview_config_whitebalance(&isp->isp_prev,
					       isp_h3a->h3awb_update);
		isp_h3a->wb_update = 0;
	}
	return;
}
EXPORT_SYMBOL(isph3a_update_wb);

/**
 * isph3a_aewb_update_regs - Helper function to update h3a registers.
 **/
static void isph3a_aewb_update_regs(struct isp_h3a_device *isp_h3a)
{
	isp_reg_writel(isp_h3a->dev, isp_h3a->regs.pcr, OMAP3_ISP_IOMEM_H3A,
		       ISPH3A_PCR);
	isp_reg_writel(isp_h3a->dev, isp_h3a->regs.win1, OMAP3_ISP_IOMEM_H3A,
		       ISPH3A_AEWWIN1);
	isp_reg_writel(isp_h3a->dev, isp_h3a->regs.start, OMAP3_ISP_IOMEM_H3A,
		       ISPH3A_AEWINSTART);
	isp_reg_writel(isp_h3a->dev, isp_h3a->regs.blk, OMAP3_ISP_IOMEM_H3A,
		       ISPH3A_AEWINBLK);
	isp_reg_writel(isp_h3a->dev, isp_h3a->regs.subwin, OMAP3_ISP_IOMEM_H3A,
		       ISPH3A_AEWSUBWIN);

	isp_h3a->update = 0;
	isp_h3a->frame_count = 1;
}

/**
 * isph3a_aewb_update_req_buffer - Helper function to update buffer cache pages
 * @buffer: Pointer to structure
 **/
static void isph3a_aewb_update_req_buffer(struct isp_h3a_device *isp_h3a,
					  struct isph3a_aewb_buffer *buffer)
{
	int size = isp_h3a->stats_buf_size;

	size = PAGE_ALIGN(size);
	dmac_inv_range((void *)buffer->addr_align,
		       (void *)buffer->addr_align + size);
}

/**
 * isph3a_aewb_stats_available - Check for stats available of specified frame.
 * @aewbdata: Pointer to return AE AWB statistics data
 *
 * Returns 0 if successful, or -1 if statistics are unavailable.
 **/
static int isph3a_aewb_stats_available(struct isp_h3a_device *isp_h3a,
				       struct isph3a_aewb_data *aewbdata)
{
	int i, ret;
	unsigned long irqflags;

	spin_lock_irqsave(&isp_h3a->buffer_lock, irqflags);
	for (i = 0; i < H3A_MAX_BUFF; i++) {
		DPRINTK_ISPH3A("Checking Stats buff[%d] (%d) for %d\n",
			       i, isp_h3a->buff[i].frame_num,
			       aewbdata->frame_number);
		if ((aewbdata->frame_number !=
		     isp_h3a->buff[i].frame_num) ||
		    (isp_h3a->buff[i].frame_num ==
		     isp_h3a->active_buff->frame_num))
			continue;
		isp_h3a->buff[i].locked = 1;
		spin_unlock_irqrestore(&isp_h3a->buffer_lock, irqflags);
		isph3a_aewb_update_req_buffer(isp_h3a, &isp_h3a->buff[i]);
		isp_h3a->buff[i].frame_num = 0;
		ret = copy_to_user((void *)aewbdata->h3a_aewb_statistics_buf,
				   (void *)isp_h3a->buff[i].virt_addr,
				   isp_h3a->curr_cfg_buf_size);
		if (ret) {
			dev_err(isp_h3a->dev, "h3a: Failed copy_to_user for "
			       "H3A stats buff, %d\n", ret);
		}
		aewbdata->ts = isp_h3a->buff[i].ts;
		aewbdata->config_counter = isp_h3a->buff[i].config_counter;
		aewbdata->field_count = isp_h3a->xtrastats[i].field_count;
		return 0;
	}
	spin_unlock_irqrestore(&isp_h3a->buffer_lock, irqflags);

	return -1;
}

/**
 * isph3a_aewb_link_buffers - Helper function to link allocated buffers.
 **/
static void isph3a_aewb_link_buffers(struct isp_h3a_device *isp_h3a)
{
	int i;

	for (i = 0; i < H3A_MAX_BUFF; i++) {
		if ((i + 1) < H3A_MAX_BUFF) {
			isp_h3a->buff[i].next = &isp_h3a->buff[i + 1];
			isp_h3a->xtrastats[i].next = &isp_h3a->xtrastats[i + 1];
		} else {
			isp_h3a->buff[i].next = &isp_h3a->buff[0];
			isp_h3a->xtrastats[i].next = &isp_h3a->xtrastats[0];
		}
	}
}

/**
 * isph3a_aewb_unlock_buffers - Helper function to unlock all buffers.
 **/
static void isph3a_aewb_unlock_buffers(struct isp_h3a_device *isp_h3a)
{
	int i;
	unsigned long irqflags;

	spin_lock_irqsave(&isp_h3a->buffer_lock, irqflags);
	for (i = 0; i < H3A_MAX_BUFF; i++)
		isp_h3a->buff[i].locked = 0;

	spin_unlock_irqrestore(&isp_h3a->buffer_lock, irqflags);
}

/**
 * isph3a_aewb_isr - Callback from ISP driver for H3A AEWB interrupt.
 * @status: IRQ0STATUS in case of MMU error, 0 for H3A interrupt.
 * @arg1: Not used as of now.
 * @arg2: Not used as of now.
 */
static void isph3a_aewb_isr(unsigned long status, isp_vbq_callback_ptr arg1,
			    void *arg2)
{
	struct isp_h3a_device *isp_h3a = arg2;
	u16 frame_align;

	if ((H3A_AWB_DONE & status) != H3A_AWB_DONE)
		return;

	do_gettimeofday(&isp_h3a->active_buff->ts);
	isp_h3a->active_buff->config_counter =
					atomic_read(&isp_h3a->config_counter);
	isp_h3a->active_buff = isp_h3a->active_buff->next;
	if (isp_h3a->active_buff->locked == 1)
		isp_h3a->active_buff = isp_h3a->active_buff->next;
	isp_reg_writel(isp_h3a->dev, isp_h3a->active_buff->ispmmu_addr,
		       OMAP3_ISP_IOMEM_H3A, ISPH3A_AEWBUFST);

	isp_h3a->frame_count++;
	frame_align = isp_h3a->frame_count;
	if (isp_h3a->frame_count > MAX_FRAME_COUNT) {
		isp_h3a->frame_count = 1;
		frame_align++;
	}
	isp_h3a->active_buff->frame_num = isp_h3a->frame_count;

	if (isp_h3a->stats_req) {
		DPRINTK_ISPH3A("waiting for frame %d\n", isp_h3a->frame_req);
		if (frame_align >= isp_h3a->frame_req + 1) {
			isp_h3a->stats_req = 0;
			isp_h3a->stats_done = 1;
			wake_up_interruptible(&isp_h3a->stats_wait);
		}
	}

	if (isp_h3a->update)
		isph3a_aewb_update_regs(isp_h3a);
}

/**
 * isph3a_aewb_set_params - Helper function to check & store user given params.
 * @user_cfg: Pointer to AE and AWB parameters struct.
 *
 * As most of them are busy-lock registers, need to wait until AEW_BUSY = 0 to
 * program them during ISR.
 *
 * Returns 0 if successful, or -EINVAL if any of the parameters are invalid.
 **/
static int isph3a_aewb_set_params(struct isp_h3a_device *isp_h3a,
				  struct isph3a_aewb_config *user_cfg)
{
	if (unlikely(user_cfg->saturation_limit > MAX_SATURATION_LIM)) {
		dev_err(isp_h3a->dev, "h3a: Invalid Saturation_limit: %d\n",
		       user_cfg->saturation_limit);
		return -EINVAL;
	}
	if (isp_h3a->aewb_config_local.saturation_limit !=
						user_cfg->saturation_limit) {
		WRITE_SAT_LIM(isp_h3a->regs.pcr, user_cfg->saturation_limit);
		isp_h3a->aewb_config_local.saturation_limit =
			user_cfg->saturation_limit;
		isp_h3a->update = 1;
	}

	if (isp_h3a->aewb_config_local.alaw_enable != user_cfg->alaw_enable) {
		WRITE_ALAW(isp_h3a->regs.pcr, user_cfg->alaw_enable);
		isp_h3a->aewb_config_local.alaw_enable = user_cfg->alaw_enable;
		isp_h3a->update = 1;
	}

	if (unlikely(user_cfg->win_height < MIN_WIN_H ||
		     user_cfg->win_height > MAX_WIN_H ||
		     user_cfg->win_height & 0x01)) {
		dev_err(isp_h3a->dev, "h3a: Invalid window height: %d\n",
		       user_cfg->win_height);
		return -EINVAL;
	}
	if (isp_h3a->aewb_config_local.win_height != user_cfg->win_height) {
		WRITE_WIN_H(isp_h3a->regs.win1, user_cfg->win_height);
		isp_h3a->aewb_config_local.win_height = user_cfg->win_height;
		isp_h3a->update = 1;
	}

	if (unlikely(user_cfg->win_width < MIN_WIN_W ||
		     user_cfg->win_width > MAX_WIN_W ||
		     user_cfg->win_width & 0x01)) {
		dev_err(isp_h3a->dev, "h3a: Invalid window width: %d\n",
		       user_cfg->win_width);
		return -EINVAL;
	}
	if (isp_h3a->aewb_config_local.win_width != user_cfg->win_width) {
		WRITE_WIN_W(isp_h3a->regs.win1, user_cfg->win_width);
		isp_h3a->aewb_config_local.win_width = user_cfg->win_width;
		isp_h3a->update = 1;
	}

	if (unlikely(user_cfg->ver_win_count < 1 ||
		     user_cfg->ver_win_count > MAX_WINVC)) {
		dev_err(isp_h3a->dev,
			"h3a: Invalid vertical window count: %d\n",
			user_cfg->ver_win_count);
		return -EINVAL;
	}
	if (isp_h3a->aewb_config_local.ver_win_count !=
						user_cfg->ver_win_count) {
		WRITE_VER_C(isp_h3a->regs.win1, user_cfg->ver_win_count);
		isp_h3a->aewb_config_local.ver_win_count =
						user_cfg->ver_win_count;
		isp_h3a->update = 1;
	}

	if (unlikely(user_cfg->hor_win_count < 1 ||
		     user_cfg->hor_win_count > MAX_WINHC)) {
		dev_err(isp_h3a->dev,
			"h3a: Invalid horizontal window count: %d\n",
			user_cfg->hor_win_count);
		return -EINVAL;
	}
	if (isp_h3a->aewb_config_local.hor_win_count !=
						user_cfg->hor_win_count) {
		WRITE_HOR_C(isp_h3a->regs.win1, user_cfg->hor_win_count);
		isp_h3a->aewb_config_local.hor_win_count =
						user_cfg->hor_win_count;
		isp_h3a->update = 1;
	}

	if (unlikely(user_cfg->ver_win_start > MAX_WINSTART)) {
		dev_err(isp_h3a->dev,
			"h3a: Invalid vertical window start: %d\n",
			user_cfg->ver_win_start);
		return -EINVAL;
	}
	if (isp_h3a->aewb_config_local.ver_win_start !=
						user_cfg->ver_win_start) {
		WRITE_VER_WIN_ST(isp_h3a->regs.start, user_cfg->ver_win_start);
		isp_h3a->aewb_config_local.ver_win_start =
						user_cfg->ver_win_start;
		isp_h3a->update = 1;
	}

	if (unlikely(user_cfg->hor_win_start > MAX_WINSTART)) {
		dev_err(isp_h3a->dev,
			"h3a: Invalid horizontal window start: %d\n",
			user_cfg->hor_win_start);
		return -EINVAL;
	}
	if (isp_h3a->aewb_config_local.hor_win_start !=
						user_cfg->hor_win_start) {
		WRITE_HOR_WIN_ST(isp_h3a->regs.start, user_cfg->hor_win_start);
		isp_h3a->aewb_config_local.hor_win_start =
						user_cfg->hor_win_start;
		isp_h3a->update = 1;
	}

	if (unlikely(user_cfg->blk_ver_win_start > MAX_WINSTART)) {
		dev_err(isp_h3a->dev,
			"h3a: Invalid black vertical window start: %d\n",
			user_cfg->blk_ver_win_start);
		return -EINVAL;
	}
	if (isp_h3a->aewb_config_local.blk_ver_win_start !=
	    user_cfg->blk_ver_win_start) {
		WRITE_BLK_VER_WIN_ST(isp_h3a->regs.blk,
				     user_cfg->blk_ver_win_start);
		isp_h3a->aewb_config_local.blk_ver_win_start =
			user_cfg->blk_ver_win_start;
		isp_h3a->update = 1;
	}

	if (unlikely(user_cfg->blk_win_height < MIN_WIN_H ||
		     user_cfg->blk_win_height > MAX_WIN_H ||
		     user_cfg->blk_win_height & 0x01)) {
		dev_err(isp_h3a->dev, "h3a: Invalid black window height: %d\n",
		       user_cfg->blk_win_height);
		return -EINVAL;
	}
	if (isp_h3a->aewb_config_local.blk_win_height !=
						user_cfg->blk_win_height) {
		WRITE_BLK_WIN_H(isp_h3a->regs.blk, user_cfg->blk_win_height);
		isp_h3a->aewb_config_local.blk_win_height =
						user_cfg->blk_win_height;
		isp_h3a->update = 1;
	}

	if (unlikely(user_cfg->subsample_ver_inc < MIN_SUB_INC ||
		     user_cfg->subsample_ver_inc > MAX_SUB_INC ||
		     user_cfg->subsample_ver_inc & 0x01)) {
		dev_err(isp_h3a->dev,
			"h3a: Invalid vertical subsample increment: %d\n",
			user_cfg->subsample_ver_inc);
		return -EINVAL;
	}
	if (isp_h3a->aewb_config_local.subsample_ver_inc !=
	    user_cfg->subsample_ver_inc) {
		WRITE_SUB_VER_INC(isp_h3a->regs.subwin,
				  user_cfg->subsample_ver_inc);
		isp_h3a->aewb_config_local.subsample_ver_inc =
			user_cfg->subsample_ver_inc;
		isp_h3a->update = 1;
	}

	if (unlikely(user_cfg->subsample_hor_inc < MIN_SUB_INC ||
		     user_cfg->subsample_hor_inc > MAX_SUB_INC ||
		     user_cfg->subsample_hor_inc & 0x01)) {
		dev_err(isp_h3a->dev,
			"h3a: Invalid horizontal subsample increment: %d\n",
			user_cfg->subsample_hor_inc);
		return -EINVAL;
	}
	if (isp_h3a->aewb_config_local.subsample_hor_inc !=
	    user_cfg->subsample_hor_inc) {
		WRITE_SUB_HOR_INC(isp_h3a->regs.subwin,
				  user_cfg->subsample_hor_inc);
		isp_h3a->aewb_config_local.subsample_hor_inc =
			user_cfg->subsample_hor_inc;
		isp_h3a->update = 1;
	}

	if (!isp_h3a->initialized || !isp_h3a->aewb_config_local.aewb_enable) {
		isph3a_aewb_update_regs(isp_h3a);
		isp_h3a->initialized = 1;
	}
	return 0;
}

/**
 * isph3a_aewb_configure - Configure AEWB regs, enable/disable H3A engine.
 * @aewbcfg: Pointer to AEWB config structure.
 *
 * Returns 0 if successful, -EINVAL if aewbcfg pointer is NULL, -ENOMEM if
 * was unable to allocate memory for the buffer, of other errors if H3A
 * callback is not set or the parameters for AEWB are invalid.
 **/
int isph3a_aewb_configure(struct isp_h3a_device *isp_h3a,
			  struct isph3a_aewb_config *aewbcfg)
{
	int ret = 0;
	int i;
	int win_count = 0;

	if (NULL == aewbcfg) {
		dev_err(isp_h3a->dev,
			"h3a: Null argument in configuration. \n");
		return -EINVAL;
	}

	if (!isp_h3a->initialized) {
		DPRINTK_ISPH3A("Setting callback for H3A\n");
		ret = isp_set_callback(isp_h3a->dev, CBK_H3A_AWB_DONE,
				       isph3a_aewb_isr, (void *)NULL,
				       isp_h3a);
		if (ret) {
			dev_err(isp_h3a->dev, "h3a: No callback\n");
			return ret;
		}
	}

	ret = isph3a_aewb_set_params(isp_h3a, aewbcfg);
	if (ret) {
		dev_err(isp_h3a->dev, "h3a: Invalid parameters! \n");
		return ret;
	}

	win_count = aewbcfg->ver_win_count * aewbcfg->hor_win_count;
	win_count += aewbcfg->hor_win_count;
	ret = win_count / 8;
	win_count += win_count % 8 ? 1 : 0;
	win_count += ret;

	isp_h3a->win_count = win_count;
	isp_h3a->curr_cfg_buf_size = win_count * AEWB_PACKET_SIZE;

	if (isp_h3a->stats_buf_size
	    && win_count * AEWB_PACKET_SIZE > isp_h3a->stats_buf_size) {
		DPRINTK_ISPH3A("There was a previous buffer... "
			       "Freeing/unmapping current stat busffs\n");
		isph3a_aewb_enable(isp_h3a, 0);
		for (i = 0; i < H3A_MAX_BUFF; i++) {
			ispmmu_kunmap(isp_h3a->buff[i].ispmmu_addr);
			dma_free_coherent(
				NULL,
				isp_h3a->min_buf_size,
				(void *)isp_h3a->buff[i].virt_addr,
				(dma_addr_t)isp_h3a->buff[i].phy_addr);
			isp_h3a->buff[i].virt_addr = 0;
		}
		isp_h3a->stats_buf_size = 0;
	}

	if (!isp_h3a->buff[0].virt_addr) {
		isp_h3a->stats_buf_size = win_count * AEWB_PACKET_SIZE;
		isp_h3a->min_buf_size = PAGE_ALIGN(isp_h3a->stats_buf_size);

		DPRINTK_ISPH3A("Allocating/mapping new stat buffs\n");
		for (i = 0; i < H3A_MAX_BUFF; i++) {
			isp_h3a->buff[i].virt_addr =
				(unsigned long)dma_alloc_coherent(
					NULL,
					isp_h3a->min_buf_size,
					(dma_addr_t *)
					&isp_h3a->buff[i].phy_addr,
					GFP_KERNEL | GFP_DMA);
			if (isp_h3a->buff[i].virt_addr == 0) {
				dev_err(isp_h3a->dev,
					"h3a: Can't acquire memory for "
					"buffer[%d]\n", i);
				return -ENOMEM;
			}
			isp_h3a->buff[i].addr_align =
				isp_h3a->buff[i].virt_addr;
			while ((isp_h3a->buff[i].addr_align & 0xFFFFFFC0) !=
			       isp_h3a->buff[i].addr_align)
				isp_h3a->buff[i].addr_align++;
			isp_h3a->buff[i].ispmmu_addr =
				ispmmu_kmap(isp_h3a->buff[i].phy_addr,
					    isp_h3a->min_buf_size);
		}
		isph3a_aewb_unlock_buffers(isp_h3a);
		isph3a_aewb_link_buffers(isp_h3a);

		if (isp_h3a->active_buff == NULL)
			isp_h3a->active_buff = &isp_h3a->buff[0];

		isp_reg_writel(isp_h3a->dev, isp_h3a->active_buff->ispmmu_addr,
			       OMAP3_ISP_IOMEM_H3A, ISPH3A_AEWBUFST);
	}
	for (i = 0; i < H3A_MAX_BUFF; i++) {
		DPRINTK_ISPH3A("buff[%d] addr is:\n    virt    0x%lX\n"
			       "    aligned 0x%lX\n"
			       "    phys    0x%lX\n"
			       "    ispmmu  0x%08lX\n"
			       "    mmapped 0x%lX\n"
			       "    frame_num %d\n", i,
			       isp_h3a->buff[i].virt_addr,
			       isp_h3a->buff[i].addr_align,
			       isp_h3a->buff[i].phy_addr,
			       isp_h3a->buff[i].ispmmu_addr,
			       isp_h3a->buff[i].mmap_addr,
			       isp_h3a->buff[i].frame_num);
	}

	isp_h3a->active_buff->frame_num = 1;

	atomic_inc(&isp_h3a->config_counter);
	isph3a_aewb_enable(isp_h3a, aewbcfg->aewb_enable);
	isph3a_print_status(isp_h3a);

	return 0;
}
EXPORT_SYMBOL(isph3a_aewb_configure);

/**
 * isph3a_aewb_request_statistics - REquest statistics and update gains in AEWB
 * @aewbdata: Pointer to return AE AWB statistics data.
 *
 * This API allows the user to update White Balance gains, as well as
 * exposure time and analog gain. It is also used to request frame
 * statistics.
 *
 * Returns 0 if successful, -EINVAL when H3A engine is not enabled, or other
 * errors when setting gains.
 **/
int isph3a_aewb_request_statistics(struct isp_h3a_device *isp_h3a,
				   struct isph3a_aewb_data *aewbdata)
{
	int ret = 0;
	u16 frame_diff = 0;
	u16 frame_cnt = isp_h3a->frame_count;
	wait_queue_t wqt;

	if (!isp_h3a->aewb_config_local.aewb_enable) {
		dev_err(isp_h3a->dev, "h3a: engine not enabled\n");
		return -EINVAL;
	}

	DPRINTK_ISPH3A("isph3a_aewb_request_statistics: Enter "
		       "(frame req. => %d, current frame => %d,"
		       "update => %d)\n",
		       aewbdata->frame_number, frame_cnt, aewbdata->update);
	DPRINTK_ISPH3A("User data received: \n");
	DPRINTK_ISPH3A("Digital gain = 0x%04x\n", aewbdata->dgain);
	DPRINTK_ISPH3A("WB gain b *=   0x%04x\n", aewbdata->wb_gain_b);
	DPRINTK_ISPH3A("WB gain r *=   0x%04x\n", aewbdata->wb_gain_r);
	DPRINTK_ISPH3A("WB gain gb =   0x%04x\n", aewbdata->wb_gain_gb);
	DPRINTK_ISPH3A("WB gain gr =   0x%04x\n", aewbdata->wb_gain_gr);

	if (!aewbdata->update) {
		aewbdata->h3a_aewb_statistics_buf = NULL;
		goto out;
	}
	if (aewbdata->update & SET_DIGITAL_GAIN)
		isp_h3a->h3awb_update.dgain = (u16)aewbdata->dgain;
	if (aewbdata->update & SET_COLOR_GAINS) {
		isp_h3a->h3awb_update.coef0 = (u8)aewbdata->wb_gain_gr;
		isp_h3a->h3awb_update.coef1 = (u8)aewbdata->wb_gain_r;
		isp_h3a->h3awb_update.coef2 = (u8)aewbdata->wb_gain_b;
		isp_h3a->h3awb_update.coef3 = (u8)aewbdata->wb_gain_gb;
	}
	if (aewbdata->update & (SET_COLOR_GAINS | SET_DIGITAL_GAIN))
		isp_h3a->wb_update = 1;

	if (!(aewbdata->update & REQUEST_STATISTICS)) {
		aewbdata->h3a_aewb_statistics_buf = NULL;
		goto out;
	}

	if (aewbdata->frame_number < 1) {
		dev_err(isp_h3a->dev, "h3a: Illeagal frame number "
		       "requested (%d)\n",
		       aewbdata->frame_number);
		return -EINVAL;
	}

	isph3a_aewb_unlock_buffers(isp_h3a);

	DPRINTK_ISPH3A("Stats available?\n");
	ret = isph3a_aewb_stats_available(isp_h3a, aewbdata);
	if (!ret)
		goto out;

	DPRINTK_ISPH3A("Stats in near future?\n");
	if (aewbdata->frame_number > frame_cnt)
		frame_diff = aewbdata->frame_number - frame_cnt;
	else if (aewbdata->frame_number < frame_cnt) {
		if ((frame_cnt > (MAX_FRAME_COUNT - MAX_FUTURE_FRAMES)) &&
		    (aewbdata->frame_number < MAX_FRAME_COUNT)) {
			frame_diff = aewbdata->frame_number + MAX_FRAME_COUNT -
				frame_cnt;
		} else
			frame_diff = MAX_FUTURE_FRAMES + 1;
	}

	if (frame_diff > MAX_FUTURE_FRAMES) {
		dev_err(isp_h3a->dev,
			"h3a: Invalid frame requested, returning current"
			" frame stats\n");
		aewbdata->frame_number = frame_cnt;
	}
	if (isp_h3a->camnotify) {
		DPRINTK_ISPH3A("NOT Waiting on stats IRQ for frame %d "
			       "because camnotify set\n",
			       aewbdata->frame_number);
		aewbdata->h3a_aewb_statistics_buf = NULL;
		goto out;
	}
	DPRINTK_ISPH3A("Waiting on stats IRQ for frame %d\n",
		       aewbdata->frame_number);
	isp_h3a->frame_req = aewbdata->frame_number;
	isp_h3a->stats_req = 1;
	isp_h3a->stats_done = 0;
	init_waitqueue_entry(&wqt, current);
	ret = wait_event_interruptible(isp_h3a->stats_wait,
				       isp_h3a->stats_done == 1);
	if (ret < 0) {
		dev_err(isp_h3a->dev, "h3a: isph3a_aewb_request_statistics"
		       " Error on wait event %d\n", ret);
		aewbdata->h3a_aewb_statistics_buf = NULL;
		return ret;
	}

	DPRINTK_ISPH3A("ISP AEWB request status interrupt raised\n");
	ret = isph3a_aewb_stats_available(isp_h3a, aewbdata);
	if (ret) {
		DPRINTK_ISPH3A("After waiting for stats,"
			       " stats not available!!\n");
		aewbdata->h3a_aewb_statistics_buf = NULL;
	}
out:
	DPRINTK_ISPH3A("isph3a_aewb_request_statistics: "
		       "aewbdata->h3a_aewb_statistics_buf => %p\n",
		       aewbdata->h3a_aewb_statistics_buf);
	aewbdata->curr_frame = isp_h3a->frame_count;

	return 0;
}
EXPORT_SYMBOL(isph3a_aewb_request_statistics);

/**
 * isph3a_aewb_init - Module Initialisation.
 *
 * Always returns 0.
 **/
int __init isph3a_aewb_init(struct device *dev)
{
	struct isp_device *isp = dev_get_drvdata(dev);
	struct isp_h3a_device *isp_h3a = &isp->isp_h3a;

	isp_h3a->dev = dev;
	init_waitqueue_head(&isp_h3a->stats_wait);
	spin_lock_init(&isp_h3a->buffer_lock);

	isp_h3a->aewb_config_local.saturation_limit = AEWB_SATURATION_LIMIT;

	return 0;
}

/**
 * isph3a_aewb_cleanup - Module exit.
 **/
void isph3a_aewb_cleanup(struct device *dev)
{
	struct isp_device *isp = dev_get_drvdata(dev);
	struct isp_h3a_device *isp_h3a = &isp->isp_h3a;
	int i;

	for (i = 0; i < H3A_MAX_BUFF; i++) {
		if (!isp_h3a->buff[i].phy_addr)
			continue;

		ispmmu_kunmap(isp_h3a->buff[i].ispmmu_addr);
		dma_free_coherent(NULL,
				  isp_h3a->min_buf_size,
				  (void *)isp_h3a->buff[i].virt_addr,
				  (dma_addr_t)isp_h3a->buff[i].phy_addr);
	}
}

/**
 * isph3a_print_status - Debug print. Values of H3A related registers.
 **/
static void isph3a_print_status(struct isp_h3a_device *isp_h3a)
{
	DPRINTK_ISPH3A("ISPH3A_PCR = 0x%08x\n",
		       isp_reg_readl(isp_h3a->dev, OMAP3_ISP_IOMEM_H3A,
				     ISPH3A_PCR));
	DPRINTK_ISPH3A("ISPH3A_AEWWIN1 = 0x%08x\n",
		       isp_reg_readl(isp_h3a->dev, OMAP3_ISP_IOMEM_H3A,
				     ISPH3A_AEWWIN1));
	DPRINTK_ISPH3A("ISPH3A_AEWINSTART = 0x%08x\n",
		       isp_reg_readl(isp_h3a->dev, OMAP3_ISP_IOMEM_H3A,
				     ISPH3A_AEWINSTART));
	DPRINTK_ISPH3A("ISPH3A_AEWINBLK = 0x%08x\n",
		       isp_reg_readl(isp_h3a->dev, OMAP3_ISP_IOMEM_H3A,
				     ISPH3A_AEWINBLK));
	DPRINTK_ISPH3A("ISPH3A_AEWSUBWIN = 0x%08x\n",
		       isp_reg_readl(isp_h3a->dev, OMAP3_ISP_IOMEM_H3A,
				     ISPH3A_AEWSUBWIN));
	DPRINTK_ISPH3A("ISPH3A_AEWBUFST = 0x%08x\n",
		       isp_reg_readl(isp_h3a->dev, OMAP3_ISP_IOMEM_H3A,
				     ISPH3A_AEWBUFST));
	DPRINTK_ISPH3A("stats windows = %d\n", isp_h3a->win_count);
	DPRINTK_ISPH3A("stats buff size = %d\n", isp_h3a->stats_buf_size);
	DPRINTK_ISPH3A("currently configured stats buff size = %d\n",
		       isp_h3a->curr_cfg_buf_size);
}

/**
 * isph3a_notify - Unblocks user request for statistics when camera is off
 * @notify: 1 - Camera is turned off
 *
 * Used when the user has requested statistics about a future frame, but the
 * camera is turned off before it happens, and this function unblocks the
 * request so the user can continue in its program.
 **/
void isph3a_notify(struct isp_h3a_device *isp_h3a, int notify)
{
	isp_h3a->camnotify = notify;
	if (isp_h3a->camnotify && isp_h3a->initialized) {
		dev_dbg(isp_h3a->dev, "h3a: Warning Camera Off \n");
		isp_h3a->stats_req = 0;
		isp_h3a->stats_done = 1;
		wake_up_interruptible(&isp_h3a->stats_wait);
	}
}
EXPORT_SYMBOL(isph3a_notify);

/**
 * isph3a_save_context - Saves the values of the h3a module registers.
 **/
void isph3a_save_context(struct device *dev)
{
	DPRINTK_ISPH3A(" Saving context\n");
	isp_save_context(dev, isph3a_reg_list);
	/* Avoid enable during restore ctx */
	isph3a_reg_list[0].val &= ~ISPH3A_PCR_AEW_EN;
}
EXPORT_SYMBOL(isph3a_save_context);

/**
 * isph3a_restore_context - Restores the values of the h3a module registers.
 **/
void isph3a_restore_context(struct device *dev)
{
	DPRINTK_ISPH3A(" Restoring context\n");
	isp_restore_context(dev, isph3a_reg_list);
}
EXPORT_SYMBOL(isph3a_restore_context);
