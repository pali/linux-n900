/*
 * isp_af.c
 *
 * AF module for TI's OMAP3 Camera ISP
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

/* Linux specific include files */
#include <asm/cacheflush.h>

#include <linux/uaccess.h>
#include <linux/dma-mapping.h>
#include <asm/atomic.h>

#include "isp.h"
#include "ispreg.h"
#include "isph3a.h"
#include "isp_af.h"
#include "ispmmu.h"

/**
 * isp_af_setxtrastats - Receives extra statistics from prior frames.
 * @xtrastats: Pointer to structure containing extra statistics fields like
 *             field count and timestamp of frame.
 *
 * Called from update_vbq in camera driver
 **/
void isp_af_setxtrastats(struct isp_af_device *isp_af,
			 struct isp_af_xtrastats *xtrastats, u8 updateflag)
{
	int i, past_i;

	if (isp_af->active_buff == NULL)
		return;

	for (i = 0; i < H3A_MAX_BUFF; i++) {
		if (isp_af->af_buff[i].frame_num ==
				isp_af->active_buff->frame_num)
			break;
	}

	if (i == H3A_MAX_BUFF)
		return;

	if (i == 0) {
		if (isp_af->af_buff[H3A_MAX_BUFF - 1].locked == 0)
			past_i = H3A_MAX_BUFF - 1;
		else
			past_i = H3A_MAX_BUFF - 2;
	} else if (i == 1) {
		if (isp_af->af_buff[0].locked == 0)
			past_i = 0;
		else
			past_i = H3A_MAX_BUFF - 1;
	} else {
		if (isp_af->af_buff[i - 1].locked == 0)
			past_i = i - 1;
		else
			past_i = i - 2;
	}

	if (updateflag & AF_UPDATEXS_TS)
		isp_af->af_buff[past_i].xtrastats.ts = xtrastats->ts;

	if (updateflag & AF_UPDATEXS_FIELDCOUNT)
		isp_af->af_buff[past_i].xtrastats.field_count =
			xtrastats->field_count;
}
EXPORT_SYMBOL(isp_af_setxtrastats);

/*
 * Helper function to update buffer cache pages
 */
static void isp_af_update_req_buffer(struct isp_af_device *isp_af,
				     struct isp_af_buffer *buffer)
{
	int size = isp_af->stats_buf_size;

	size = PAGE_ALIGN(size);
	/* Update the kernel pages of the requested buffer */
	dmac_inv_range((void *)buffer->addr_align, (void *)buffer->addr_align +
		       size);
}

#define IS_OUT_OF_BOUNDS(value, min, max)		\
	(((value) < (min)) || ((value) > (max)))

/* Function to check paxel parameters */
int isp_af_check_paxel(struct isp_af_device *isp_af)
{
	struct af_paxel *paxel_cfg = &isp_af->config.paxel_config;
	struct af_iir *iir_cfg = &isp_af->config.iir_config;

	/* Check horizontal Count */
	if (IS_OUT_OF_BOUNDS(paxel_cfg->hz_cnt, AF_PAXEL_HORIZONTAL_COUNT_MIN,
			     AF_PAXEL_HORIZONTAL_COUNT_MAX)) {
		DPRINTK_ISP_AF("Error : Horizontal Count is incorrect");
		return -AF_ERR_HZ_COUNT;
	}

	/*Check Vertical Count */
	if (IS_OUT_OF_BOUNDS(paxel_cfg->vt_cnt, AF_PAXEL_VERTICAL_COUNT_MIN,
			     AF_PAXEL_VERTICAL_COUNT_MAX)) {
		DPRINTK_ISP_AF("Error : Vertical Count is incorrect");
		return -AF_ERR_VT_COUNT;
	}

	/*Check Height */
	if (IS_OUT_OF_BOUNDS(paxel_cfg->height, AF_PAXEL_HEIGHT_MIN,
			     AF_PAXEL_HEIGHT_MAX)) {
		DPRINTK_ISP_AF("Error : Height is incorrect");
		return -AF_ERR_HEIGHT;
	}

	/*Check width */
	if (IS_OUT_OF_BOUNDS(paxel_cfg->width, AF_PAXEL_WIDTH_MIN,
			     AF_PAXEL_WIDTH_MAX)) {
		DPRINTK_ISP_AF("Error : Width is incorrect");
		return -AF_ERR_WIDTH;
	}

	/*Check Line Increment */
	if (IS_OUT_OF_BOUNDS(paxel_cfg->line_incr, AF_PAXEL_INCREMENT_MIN,
			     AF_PAXEL_INCREMENT_MAX)) {
		DPRINTK_ISP_AF("Error : Line Increment is incorrect");
		return -AF_ERR_INCR;
	}

	/*Check Horizontal Start */
	if ((paxel_cfg->hz_start % 2 != 0) ||
	    (paxel_cfg->hz_start < (iir_cfg->hz_start_pos + 2)) ||
	    IS_OUT_OF_BOUNDS(paxel_cfg->hz_start,
			     AF_PAXEL_HZSTART_MIN, AF_PAXEL_HZSTART_MAX)) {
		DPRINTK_ISP_AF("Error : Horizontal Start is incorrect");
		return -AF_ERR_HZ_START;
	}

	/*Check Vertical Start */
	if (IS_OUT_OF_BOUNDS(paxel_cfg->vt_start, AF_PAXEL_VTSTART_MIN,
			     AF_PAXEL_VTSTART_MAX)) {
		DPRINTK_ISP_AF("Error : Vertical Start is incorrect");
		return -AF_ERR_VT_START;
	}
	return 0;
}

/**
 * isp_af_check_iir - Function to check IIR Coefficient.
 **/
int isp_af_check_iir(struct isp_af_device *isp_af)
{
	struct af_iir *iir_cfg = &isp_af->config.iir_config;
	int index;

	for (index = 0; index < AF_NUMBER_OF_COEF; index++) {
		if ((iir_cfg->coeff_set0[index]) > AF_COEF_MAX) {
			DPRINTK_ISP_AF("Error : Coefficient for set 0 is "
				       "incorrect");
			return -AF_ERR_IIR_COEF;
		}

		if ((iir_cfg->coeff_set1[index]) > AF_COEF_MAX) {
			DPRINTK_ISP_AF("Error : Coefficient for set 1 is "
				       "incorrect");
			return -AF_ERR_IIR_COEF;
		}
	}

	if (IS_OUT_OF_BOUNDS(iir_cfg->hz_start_pos, AF_IIRSH_MIN,
			     AF_IIRSH_MAX)) {
		DPRINTK_ISP_AF("Error : IIRSH is incorrect");
		return -AF_ERR_IIRSH;
	}

	return 0;
}
/**
 * isp_af_unlock_buffers - Helper function to unlock all buffers.
 **/
static void isp_af_unlock_buffers(struct isp_af_device *isp_af)
{
	int i;
	unsigned long irqflags;

	spin_lock_irqsave(&isp_af->buffer_lock, irqflags);
	for (i = 0; i < H3A_MAX_BUFF; i++)
		isp_af->af_buff[i].locked = 0;

	spin_unlock_irqrestore(&isp_af->buffer_lock, irqflags);
}

/*
 * Helper function to link allocated buffers
 */
static void isp_af_link_buffers(struct isp_af_device *isp_af)
{
	int i;

	for (i = 0; i < H3A_MAX_BUFF; i++) {
		if ((i + 1) < H3A_MAX_BUFF)
			isp_af->af_buff[i].next = &isp_af->af_buff[i + 1];
		else
			isp_af->af_buff[i].next = &isp_af->af_buff[0];
	}
}

/* Function to perform hardware set up */
int isp_af_configure(struct isp_af_device *isp_af,
		     struct af_configuration *afconfig)
{
	int result;
	int buff_size, i;
	unsigned int busyaf;
	struct af_configuration *af_curr_cfg = &isp_af->config;

	if (NULL == afconfig) {
		dev_err(isp_af->dev, "af: Null argument in configuration. \n");
		return -EINVAL;
	}

	memcpy(af_curr_cfg, afconfig, sizeof(struct af_configuration));
	/* Get the value of PCR register */
	busyaf = isp_reg_readl(isp_af->dev, OMAP3_ISP_IOMEM_H3A, ISPH3A_PCR);

	if ((busyaf & AF_BUSYAF) == AF_BUSYAF) {
		DPRINTK_ISP_AF("AF_register_setup_ERROR : Engine Busy");
		DPRINTK_ISP_AF("\n Configuration cannot be done ");
		return -AF_ERR_ENGINE_BUSY;
	}

	/* Check IIR Coefficient and start Values */
	result = isp_af_check_iir(isp_af);
	if (result < 0)
		return result;

	/* Check Paxel Values */
	result = isp_af_check_paxel(isp_af);
	if (result < 0)
		return result;

	/* Check HMF Threshold Values */
	if (af_curr_cfg->hmf_config.threshold > AF_THRESHOLD_MAX) {
		DPRINTK_ISP_AF("Error : HMF Threshold is incorrect");
		return -AF_ERR_THRESHOLD;
	}

	/* Compute buffer size */
	buff_size = (af_curr_cfg->paxel_config.hz_cnt + 1) *
		(af_curr_cfg->paxel_config.vt_cnt + 1) * AF_PAXEL_SIZE;

	isp_af->curr_cfg_buf_size = buff_size;
	/* Deallocate the previous buffers */
	if (isp_af->stats_buf_size && buff_size > isp_af->stats_buf_size) {
		isp_af_enable(isp_af, 0);
		for (i = 0; i < H3A_MAX_BUFF; i++) {
			ispmmu_kunmap(isp_af->af_buff[i].ispmmu_addr);
			dma_free_coherent(
				NULL, isp_af->min_buf_size,
				(void *)isp_af->af_buff[i].virt_addr,
				(dma_addr_t)isp_af->af_buff[i].phy_addr);
			isp_af->af_buff[i].virt_addr = 0;
		}
		isp_af->stats_buf_size = 0;
	}

	if (!isp_af->af_buff[0].virt_addr) {
		isp_af->stats_buf_size = buff_size;
		isp_af->min_buf_size = PAGE_ALIGN(isp_af->stats_buf_size);

		for (i = 0; i < H3A_MAX_BUFF; i++) {
			isp_af->af_buff[i].virt_addr =
				(unsigned long)dma_alloc_coherent(
					NULL,
					isp_af->min_buf_size,
					(dma_addr_t *)
					&isp_af->af_buff[i].phy_addr,
					GFP_KERNEL | GFP_DMA);
			if (isp_af->af_buff[i].virt_addr == 0) {
				dev_err(isp_af->dev,
					"af: Can't acquire memory for "
					"buffer[%d]\n", i);
				return -ENOMEM;
			}
			isp_af->af_buff[i].addr_align =
				isp_af->af_buff[i].virt_addr;
			while ((isp_af->af_buff[i].addr_align & 0xFFFFFFC0) !=
			       isp_af->af_buff[i].addr_align)
				isp_af->af_buff[i].addr_align++;
			isp_af->af_buff[i].ispmmu_addr =
				ispmmu_kmap(isp_af->af_buff[i].phy_addr,
					    isp_af->min_buf_size);
		}
		isp_af_unlock_buffers(isp_af);
		isp_af_link_buffers(isp_af);

		/* First active buffer */
		if (isp_af->active_buff == NULL)
			isp_af->active_buff = &isp_af->af_buff[0];
		isp_af_set_address(isp_af, isp_af->active_buff->ispmmu_addr);
	}

	result = isp_af_register_setup(isp_af);
	if (result < 0)
		return result;
	isp_af->size_paxel = buff_size;
	atomic_inc(&isp_af->config_counter);
	isp_af->initialized = 1;
	isp_af->frame_count = 1;
	isp_af->active_buff->frame_num = 1;
	/* Set configuration flag to indicate HW setup done */
	if (af_curr_cfg->af_config)
		isp_af_enable(isp_af, 1);
	else
		isp_af_enable(isp_af, 0);

	/* Success */
	return 0;
}
EXPORT_SYMBOL(isp_af_configure);

int isp_af_register_setup(struct isp_af_device *isp_af)
{
	unsigned int pcr = 0, pax1 = 0, pax2 = 0, paxstart = 0;
	unsigned int coef = 0;
	unsigned int base_coef_set0 = 0;
	unsigned int base_coef_set1 = 0;
	int index;

	/* Configure Hardware Registers */
	/* Read PCR Register */
	pcr = isp_reg_readl(isp_af->dev, OMAP3_ISP_IOMEM_H3A, ISPH3A_PCR);

	/* Set Accumulator Mode */
	if (isp_af->config.mode == ACCUMULATOR_PEAK)
		pcr |= FVMODE;
	else
		pcr &= ~FVMODE;

	/* Set A-law */
	if (isp_af->config.alaw_enable == H3A_AF_ALAW_ENABLE)
		pcr |= AF_ALAW_EN;
	else
		pcr &= ~AF_ALAW_EN;

	/* Set RGB Position */
	pcr &= ~RGBPOS;
	pcr |= isp_af->config.rgb_pos << AF_RGBPOS_SHIFT;

	/* HMF Configurations */
	if (isp_af->config.hmf_config.enable == H3A_AF_HMF_ENABLE) {
		pcr &= ~AF_MED_EN;
		/* Enable HMF */
		pcr |= AF_MED_EN;

		/* Set Median Threshold */
		pcr &= ~MED_TH;
		pcr |= isp_af->config.hmf_config.threshold << AF_MED_TH_SHIFT;
	} else
		pcr &= ~AF_MED_EN;

	/* Set PCR Register */
	isp_reg_writel(isp_af->dev, pcr, OMAP3_ISP_IOMEM_H3A, ISPH3A_PCR);

	pax1 &= ~PAXW;
	pax1 |= isp_af->config.paxel_config.width << AF_PAXW_SHIFT;

	/* Set height in AFPAX1 */
	pax1 &= ~PAXH;
	pax1 |= isp_af->config.paxel_config.height;

	isp_reg_writel(isp_af->dev, pax1, OMAP3_ISP_IOMEM_H3A, ISPH3A_AFPAX1);

	/* Configure AFPAX2 Register */
	/* Set Line Increment in AFPAX2 Register */
	pax2 &= ~AFINCV;
	pax2 |= isp_af->config.paxel_config.line_incr << AF_LINE_INCR_SHIFT;
	/* Set Vertical Count */
	pax2 &= ~PAXVC;
	pax2 |= isp_af->config.paxel_config.vt_cnt << AF_VT_COUNT_SHIFT;
	/* Set Horizontal Count */
	pax2 &= ~PAXHC;
	pax2 |= isp_af->config.paxel_config.hz_cnt;
	isp_reg_writel(isp_af->dev, pax2, OMAP3_ISP_IOMEM_H3A, ISPH3A_AFPAX2);

	/* Configure PAXSTART Register */
	/*Configure Horizontal Start */
	paxstart &= ~PAXSH;
	paxstart |= isp_af->config.paxel_config.hz_start << AF_HZ_START_SHIFT;
	/* Configure Vertical Start */
	paxstart &= ~PAXSV;
	paxstart |= isp_af->config.paxel_config.vt_start;
	isp_reg_writel(isp_af->dev, paxstart, OMAP3_ISP_IOMEM_H3A,
		       ISPH3A_AFPAXSTART);

	/*SetIIRSH Register */
	isp_reg_writel(isp_af->dev, isp_af->config.iir_config.hz_start_pos,
		       OMAP3_ISP_IOMEM_H3A, ISPH3A_AFIIRSH);

	/*Set IIR Filter0 Coefficients */
	base_coef_set0 = ISPH3A_AFCOEF010;
	for (index = 0; index <= 8; index += 2) {
		coef &= ~COEF_MASK0;
		coef |= isp_af->config.iir_config.coeff_set0[index];
		coef &= ~COEF_MASK1;
		coef |= isp_af->config.iir_config.coeff_set0[index + 1] <<
			AF_COEF_SHIFT;
		isp_reg_writel(isp_af->dev, coef, OMAP3_ISP_IOMEM_H3A,
			       base_coef_set0);
		base_coef_set0 = base_coef_set0 + AFCOEF_OFFSET;
	}

	/* set AFCOEF0010 Register */
	isp_reg_writel(isp_af->dev, isp_af->config.iir_config.coeff_set0[10],
		       OMAP3_ISP_IOMEM_H3A, ISPH3A_AFCOEF010);

	/*Set IIR Filter1 Coefficients */

	base_coef_set1 = ISPH3A_AFCOEF110;
	for (index = 0; index <= 8; index += 2) {
		coef &= ~COEF_MASK0;
		coef |= isp_af->config.iir_config.coeff_set1[index];
		coef &= ~COEF_MASK1;
		coef |= isp_af->config.iir_config.coeff_set1[index + 1] <<
			AF_COEF_SHIFT;
		isp_reg_writel(isp_af->dev, coef, OMAP3_ISP_IOMEM_H3A,
			       base_coef_set1);

		base_coef_set1 = base_coef_set1 + AFCOEF_OFFSET;
	}
	isp_reg_writel(isp_af->dev, isp_af->config.iir_config.coeff_set1[10],
		       OMAP3_ISP_IOMEM_H3A, ISPH3A_AFCOEF1010);

	return 0;
}

/* Function to set address */
void isp_af_set_address(struct isp_af_device *isp_af, unsigned long address)
{
	isp_reg_writel(isp_af->dev, address, OMAP3_ISP_IOMEM_H3A,
		       ISPH3A_AFBUFST);
}

static int isp_af_stats_available(struct isp_af_device *isp_af,
				  struct isp_af_data *afdata)
{
	int i, ret;
	unsigned long irqflags;

	spin_lock_irqsave(&isp_af->buffer_lock, irqflags);
	for (i = 0; i < H3A_MAX_BUFF; i++) {
		DPRINTK_ISP_AF("Checking Stats buff[%d] (%d) for %d\n",
			       i, isp_af->af_buff[i].frame_num,
			       afdata->frame_number);
		if (afdata->frame_number == isp_af->af_buff[i].frame_num
		    && isp_af->af_buff[i].frame_num !=
					isp_af->active_buff->frame_num) {
			isp_af->af_buff[i].locked = 1;
			spin_unlock_irqrestore(&isp_af->buffer_lock, irqflags);
			isp_af_update_req_buffer(isp_af, &isp_af->af_buff[i]);
			isp_af->af_buff[i].frame_num = 0;
			ret = copy_to_user((void *)afdata->af_statistics_buf,
					   (void *)isp_af->af_buff[i].virt_addr,
					   isp_af->curr_cfg_buf_size);
			if (ret) {
				dev_err(isp_af->dev,
					"af: Failed copy_to_user for "
					"H3A stats buff, %d\n", ret);
			}
			afdata->xtrastats.ts = isp_af->af_buff[i].xtrastats.ts;
			afdata->xtrastats.field_count =
				isp_af->af_buff[i].xtrastats.field_count;
			return 0;
		}
	}
	spin_unlock_irqrestore(&isp_af->buffer_lock, irqflags);
	/* Stats unavailable */

	return -1;
}

void isp_af_notify(struct isp_af_device *isp_af, int notify)
{
	isp_af->camnotify = notify;
	if (isp_af->camnotify && isp_af->initialized) {
		printk(KERN_DEBUG "Warning Camera Off \n");
		isp_af->stats_req = 0;
		isp_af->stats_done = 1;
		wake_up_interruptible(&isp_af->stats_wait);
	}
}
EXPORT_SYMBOL(isp_af_notify);
/*
 * This API allows the user to update White Balance gains, as well as
 * exposure time and analog gain. It is also used to request frame
 * statistics.
 */
int isp_af_request_statistics(struct isp_af_device *isp_af,
			      struct isp_af_data *afdata)
{
	int ret = 0;
	u16 frame_diff = 0;
	u16 frame_cnt = isp_af->frame_count;
	wait_queue_t wqt;

	if (!isp_af->config.af_config) {
		dev_err(isp_af->dev, "af: engine not enabled\n");
		return -EINVAL;
	}

	if (!(afdata->update & REQUEST_STATISTICS)) {
		afdata->af_statistics_buf = NULL;
		goto out;
	}

	isp_af_unlock_buffers(isp_af);
	/* Stats available? */
	DPRINTK_ISP_AF("Stats available?\n");
	ret = isp_af_stats_available(isp_af, afdata);
	if (!ret)
		goto out;

	/* Stats in near future? */
	DPRINTK_ISP_AF("Stats in near future?\n");
	if (afdata->frame_number > frame_cnt)
		frame_diff = afdata->frame_number - frame_cnt;
	else if (afdata->frame_number < frame_cnt) {
		if (frame_cnt > MAX_FRAME_COUNT - MAX_FUTURE_FRAMES
		    && afdata->frame_number < MAX_FRAME_COUNT) {
			frame_diff = afdata->frame_number + MAX_FRAME_COUNT -
				frame_cnt;
		} else {
			/* Frame unavailable */
			frame_diff = MAX_FUTURE_FRAMES + 1;
		}
	}

	if (frame_diff > MAX_FUTURE_FRAMES) {
		dev_err(isp_af->dev,
			"af: Invalid frame requested, returning current"
			" frame stats\n");
		afdata->frame_number = frame_cnt;
	}
	if (!isp_af->camnotify) {
		/* Block until frame in near future completes */
		isp_af->frame_req = afdata->frame_number;
		isp_af->stats_req = 1;
		isp_af->stats_done = 0;
		init_waitqueue_entry(&wqt, current);
		ret = wait_event_interruptible(isp_af->stats_wait,
					       isp_af->stats_done == 1);
		if (ret < 0) {
			afdata->af_statistics_buf = NULL;
			return ret;
		}
		DPRINTK_ISP_AF("ISP AF request status interrupt raised\n");

		/* Stats now available */
		ret = isp_af_stats_available(isp_af, afdata);
		if (ret) {
			dev_err(isp_af->dev,
				"af: After waiting for stats, stats not"
				" available!!\n");
			afdata->af_statistics_buf = NULL;
		}
	}

out:
	afdata->curr_frame = isp_af->frame_count;

	return 0;
}
EXPORT_SYMBOL(isp_af_request_statistics);

/* This function will handle the H3A interrupt. */
static void isp_af_isr(unsigned long status, isp_vbq_callback_ptr arg1,
		       void *arg2)
{
	struct isp_af_device *isp_af = (struct isp_af_device *)arg2;
	u16 frame_align;

	if ((H3A_AF_DONE & status) != H3A_AF_DONE)
		return;

	/* timestamp stats buffer */
	do_gettimeofday(&isp_af->active_buff->xtrastats.ts);
	isp_af->active_buff->config_counter =
				atomic_read(&isp_af->config_counter);

	/* Exchange buffers */
	isp_af->active_buff = isp_af->active_buff->next;
	if (isp_af->active_buff->locked == 1)
		isp_af->active_buff = isp_af->active_buff->next;
	isp_af_set_address(isp_af, isp_af->active_buff->ispmmu_addr);

	/* Update frame counter */
	isp_af->frame_count++;
	frame_align = isp_af->frame_count;
	if (isp_af->frame_count > MAX_FRAME_COUNT) {
		isp_af->frame_count = 1;
		frame_align++;
	}
	isp_af->active_buff->frame_num = isp_af->frame_count;

	/* Future Stats requested? */
	if (isp_af->stats_req) {
		/* Is the frame we want already done? */
		if (frame_align >= isp_af->frame_req + 1) {
			isp_af->stats_req = 0;
			isp_af->stats_done = 1;
			wake_up_interruptible(&isp_af->stats_wait);
		}
	}
}

int __isp_af_enable(struct isp_af_device *isp_af, int enable)
{
	unsigned int pcr;

	pcr = isp_reg_readl(isp_af->dev, OMAP3_ISP_IOMEM_H3A, ISPH3A_PCR);

	/* Set AF_EN bit in PCR Register */
	if (enable) {
		if (isp_set_callback(isp_af->dev, CBK_H3A_AF_DONE, isp_af_isr,
				     (void *)NULL, isp_af)) {
			dev_err(isp_af->dev, "af: No callback for AF\n");
			return -EINVAL;
		}

		pcr |= AF_EN;
	} else {
		isp_unset_callback(isp_af->dev, CBK_H3A_AF_DONE);
		pcr &= ~AF_EN;
	}
	isp_reg_writel(isp_af->dev, pcr, OMAP3_ISP_IOMEM_H3A, ISPH3A_PCR);
	return 0;
}

/* Function to Enable/Disable AF Engine */
int isp_af_enable(struct isp_af_device *isp_af, int enable)
{
	int rval;

	rval = __isp_af_enable(isp_af, enable);

	if (!rval)
		isp_af->pm_state = enable;

	return rval;
}

/* Function to Suspend AF Engine */
void isp_af_suspend(struct isp_af_device *isp_af)
{
	if (isp_af->pm_state)
		__isp_af_enable(isp_af, 0);
}

/* Function to Resume AF Engine */
void isp_af_resume(struct isp_af_device *isp_af)
{
	if (isp_af->pm_state)
		__isp_af_enable(isp_af, 1);
}

int isp_af_busy(struct isp_af_device *isp_af)
{
	return isp_reg_readl(isp_af->dev, OMAP3_ISP_IOMEM_H3A, ISPH3A_PCR)
		& ISPH3A_PCR_BUSYAF;
}

/* Function to register the AF character device driver. */
int __init isp_af_init(struct device *dev)
{
	struct isp_device *isp = dev_get_drvdata(dev);
	struct isp_af_device *isp_af = &isp->isp_af;

	isp_af->active_buff = NULL;
	isp_af->dev = dev;

	init_waitqueue_head(&isp_af->stats_wait);
	spin_lock_init(&isp_af->buffer_lock);

	return 0;
}

void isp_af_exit(struct device *dev)
{
	struct isp_device *isp = dev_get_drvdata(dev);
	struct isp_af_device *isp_af = &isp->isp_af;
	int i;

	/* Free buffers */
	for (i = 0; i < H3A_MAX_BUFF; i++) {
		if (!isp_af->af_buff[i].phy_addr)
			continue;

		ispmmu_kunmap(isp_af->af_buff[i].ispmmu_addr);

		dma_free_coherent(NULL,
				  isp_af->min_buf_size,
				  (void *)isp_af->af_buff[i].virt_addr,
				  (dma_addr_t)isp_af->af_buff[i].phy_addr);
	}
}
