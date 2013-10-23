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
 * 	David Cohen <david.cohen@nokia.com>
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
#include <linux/device.h>

#include "isp.h"
#include "ispreg.h"
#include "isph3a.h"
#include "isp_af.h"

#define IS_OUT_OF_BOUNDS(value, min, max)		\
	(((value) < (min)) || ((value) > (max)))

/* Function to check paxel parameters */
static int isp_af_check_params(struct isp_af_device *isp_af,
			       struct af_configuration *afconfig)
{
	struct af_paxel *paxel_cfg = &afconfig->paxel_config;
	struct af_iir *iir_cfg = &afconfig->iir_config;
	int index;

	/* Check horizontal Count */
	if (IS_OUT_OF_BOUNDS(paxel_cfg->hz_cnt, AF_PAXEL_HORIZONTAL_COUNT_MIN,
			     AF_PAXEL_HORIZONTAL_COUNT_MAX))
		return -AF_ERR_HZ_COUNT;

	/* Check Vertical Count */
	if (IS_OUT_OF_BOUNDS(paxel_cfg->vt_cnt, AF_PAXEL_VERTICAL_COUNT_MIN,
			     AF_PAXEL_VERTICAL_COUNT_MAX))
		return -AF_ERR_VT_COUNT;

	/* Check Height */
	if (IS_OUT_OF_BOUNDS(paxel_cfg->height, AF_PAXEL_HEIGHT_MIN,
			     AF_PAXEL_HEIGHT_MAX))
		return -AF_ERR_HEIGHT;

	/* Check width */
	if (IS_OUT_OF_BOUNDS(paxel_cfg->width, AF_PAXEL_WIDTH_MIN,
			     AF_PAXEL_WIDTH_MAX))
		return -AF_ERR_WIDTH;

	/* Check Line Increment */
	if (IS_OUT_OF_BOUNDS(paxel_cfg->line_incr, AF_PAXEL_INCREMENT_MIN,
			     AF_PAXEL_INCREMENT_MAX))
		return -AF_ERR_INCR;

	/* Check Horizontal Start */
	if ((paxel_cfg->hz_start % 2 != 0) ||
	    (paxel_cfg->hz_start < (iir_cfg->hz_start_pos + 2)) ||
	    IS_OUT_OF_BOUNDS(paxel_cfg->hz_start,
			     AF_PAXEL_HZSTART_MIN, AF_PAXEL_HZSTART_MAX))
		return -AF_ERR_HZ_START;

	/* Check Vertical Start */
	if (IS_OUT_OF_BOUNDS(paxel_cfg->vt_start, AF_PAXEL_VTSTART_MIN,
			     AF_PAXEL_VTSTART_MAX))
		return -AF_ERR_VT_START;

	/* Check IIR */
	for (index = 0; index < AF_NUMBER_OF_COEF; index++) {
		if ((iir_cfg->coeff_set0[index]) > AF_COEF_MAX)
			return -AF_ERR_IIR_COEF;

		if ((iir_cfg->coeff_set1[index]) > AF_COEF_MAX)
			return -AF_ERR_IIR_COEF;
	}

	if (IS_OUT_OF_BOUNDS(iir_cfg->hz_start_pos, AF_IIRSH_MIN,
			     AF_IIRSH_MAX))
		return -AF_ERR_IIRSH;

	/* Check HMF Threshold Values */
	if (afconfig->hmf_config.threshold > AF_THRESHOLD_MAX)
		return -AF_ERR_THRESHOLD;

	return 0;
}

void isp_af_config_registers(struct isp_af_device *isp_af)
{
	struct device *dev = to_device(isp_af);
	unsigned int pcr = 0, pax1 = 0, pax2 = 0, paxstart = 0;
	unsigned int coef = 0;
	unsigned int base_coef_set0 = 0;
	unsigned int base_coef_set1 = 0;
	int index;
	unsigned long irqflags;

	if (!isp_af->config.af_config)
		return;

	spin_lock_irqsave(isp_af->lock, irqflags);

	isp_reg_writel(dev, isp_af->buf_next->iommu_addr, OMAP3_ISP_IOMEM_H3A,
		       ISPH3A_AFBUFST);

	if (!isp_af->update) {
		spin_unlock_irqrestore(isp_af->lock, irqflags);
		return;
	}

	/* Configure Hardware Registers */
	pax1 |= isp_af->config.paxel_config.width << AF_PAXW_SHIFT;
	/* Set height in AFPAX1 */
	pax1 |= isp_af->config.paxel_config.height;
	isp_reg_writel(dev, pax1, OMAP3_ISP_IOMEM_H3A, ISPH3A_AFPAX1);

	/* Configure AFPAX2 Register */
	/* Set Line Increment in AFPAX2 Register */
	pax2 |= isp_af->config.paxel_config.line_incr << AF_LINE_INCR_SHIFT;
	/* Set Vertical Count */
	pax2 |= isp_af->config.paxel_config.vt_cnt << AF_VT_COUNT_SHIFT;
	/* Set Horizontal Count */
	pax2 |= isp_af->config.paxel_config.hz_cnt;
	isp_reg_writel(dev, pax2, OMAP3_ISP_IOMEM_H3A, ISPH3A_AFPAX2);

	/* Configure PAXSTART Register */
	/*Configure Horizontal Start */
	paxstart |= isp_af->config.paxel_config.hz_start << AF_HZ_START_SHIFT;
	/* Configure Vertical Start */
	paxstart |= isp_af->config.paxel_config.vt_start;
	isp_reg_writel(dev, paxstart, OMAP3_ISP_IOMEM_H3A,
		       ISPH3A_AFPAXSTART);

	/*SetIIRSH Register */
	isp_reg_writel(dev, isp_af->config.iir_config.hz_start_pos,
		       OMAP3_ISP_IOMEM_H3A, ISPH3A_AFIIRSH);

	base_coef_set0 = ISPH3A_AFCOEF010;
	base_coef_set1 = ISPH3A_AFCOEF110;
	for (index = 0; index <= 8; index += 2) {
		/*Set IIR Filter0 Coefficients */
		coef = 0;
		coef |= isp_af->config.iir_config.coeff_set0[index];
		coef |= isp_af->config.iir_config.coeff_set0[index + 1] <<
			AF_COEF_SHIFT;
		isp_reg_writel(dev, coef, OMAP3_ISP_IOMEM_H3A,
			       base_coef_set0);
		base_coef_set0 += AFCOEF_OFFSET;

		/*Set IIR Filter1 Coefficients */
		coef = 0;
		coef |= isp_af->config.iir_config.coeff_set1[index];
		coef |= isp_af->config.iir_config.coeff_set1[index + 1] <<
			AF_COEF_SHIFT;
		isp_reg_writel(dev, coef, OMAP3_ISP_IOMEM_H3A,
			       base_coef_set1);
		base_coef_set1 += AFCOEF_OFFSET;
	}
	/* set AFCOEF0010 Register */
	isp_reg_writel(dev, isp_af->config.iir_config.coeff_set0[10],
		       OMAP3_ISP_IOMEM_H3A, ISPH3A_AFCOEF0010);
	/* set AFCOEF1010 Register */
	isp_reg_writel(dev, isp_af->config.iir_config.coeff_set1[10],
		       OMAP3_ISP_IOMEM_H3A, ISPH3A_AFCOEF1010);

	/* PCR Register */
	/* Set Accumulator Mode */
	if (isp_af->config.mode == ACCUMULATOR_PEAK)
		pcr |= FVMODE;
	/* Set A-law */
	if (isp_af->config.alaw_enable == H3A_AF_ALAW_ENABLE)
		pcr |= AF_ALAW_EN;
	/* Set RGB Position */
	pcr |= isp_af->config.rgb_pos << AF_RGBPOS_SHIFT;
	/* HMF Configurations */
	if (isp_af->config.hmf_config.enable == H3A_AF_HMF_ENABLE) {
		/* Enable HMF */
		pcr |= AF_MED_EN;
		/* Set Median Threshold */
		pcr |= isp_af->config.hmf_config.threshold << AF_MED_TH_SHIFT;
	}
	/* Set PCR Register */
	isp_reg_and_or(dev, OMAP3_ISP_IOMEM_H3A, ISPH3A_PCR,
		       ~AF_PCR_MASK, pcr);

	isp_af->update = 0;
	isp_af->stat.config_counter++;
	ispstat_bufs_set_size(&isp_af->stat, isp_af->buf_size);

	spin_unlock_irqrestore(isp_af->lock, irqflags);
}

/* Update local parameters */
static void isp_af_update_params(struct isp_af_device *isp_af,
				 struct af_configuration *afconfig)
{
	int update = 0;
	int index;

	/* alaw */
	if (isp_af->config.alaw_enable != afconfig->alaw_enable) {
		update = 1;
		goto out;
	}

	/* hmf */
	if (isp_af->config.hmf_config.enable != afconfig->hmf_config.enable) {
		update = 1;
		goto out;
	}
	if (isp_af->config.hmf_config.threshold !=
					afconfig->hmf_config.threshold) {
		update = 1;
		goto out;
	}

	/* rgbpos */
	if (isp_af->config.rgb_pos != afconfig->rgb_pos) {
		update = 1;
		goto out;
	}

	/* iir */
	if (isp_af->config.iir_config.hz_start_pos !=
					afconfig->iir_config.hz_start_pos) {
		update = 1;
		goto out;
	}
	for (index = 0; index < AF_NUMBER_OF_COEF; index++) {
		if (isp_af->config.iir_config.coeff_set0[index] !=
				afconfig->iir_config.coeff_set0[index]) {
			update = 1;
			goto out;
		}
		if (isp_af->config.iir_config.coeff_set1[index] !=
				afconfig->iir_config.coeff_set1[index]) {
			update = 1;
			goto out;
		}
	}

	/* paxel */
	if ((isp_af->config.paxel_config.width !=
				afconfig->paxel_config.width) ||
	    (isp_af->config.paxel_config.height !=
				afconfig->paxel_config.height) ||
	    (isp_af->config.paxel_config.hz_start !=
				afconfig->paxel_config.hz_start) ||
	    (isp_af->config.paxel_config.vt_start !=
				afconfig->paxel_config.vt_start) ||
	    (isp_af->config.paxel_config.hz_cnt !=
				afconfig->paxel_config.hz_cnt) ||
	    (isp_af->config.paxel_config.line_incr !=
				afconfig->paxel_config.line_incr)) {
		update = 1;
		goto out;
	}

	/* af_mode */
	if (isp_af->config.mode != afconfig->mode) {
		update = 1;
		goto out;
	}

	isp_af->config.af_config = afconfig->af_config;

out:
	if (update) {
		memcpy(&isp_af->config, afconfig, sizeof(*afconfig));
		isp_af->update = 1;
	}
}

void isp_af_try_enable(struct isp_af_device *isp_af)
{
	unsigned long irqflags;

	if (!isp_af->config.af_config)
		return;

	spin_lock_irqsave(isp_af->lock, irqflags);
	if (unlikely(!isp_af->enabled && isp_af->config.af_config)) {
		isp_af->update = 1;
		isp_af->buf_next = ispstat_buf_next(&isp_af->stat);
		spin_unlock_irqrestore(isp_af->lock, irqflags);
		isp_af_config_registers(isp_af);
		isp_af_enable(isp_af, 1);
	} else
		spin_unlock_irqrestore(isp_af->lock, irqflags);
}

/* Function to perform hardware set up */
int isp_af_config(struct isp_af_device *isp_af,
		  struct af_configuration *afconfig)
{
	struct device *dev = to_device(isp_af);
	int result;
	int buf_size;
	unsigned long irqflags;

	if (!afconfig) {
		dev_dbg(dev, "af: Null argument in configuration.\n");
		return -EINVAL;
	}

	/* Check Parameters */
	spin_lock_irqsave(isp_af->lock, irqflags);
	result = isp_af_check_params(isp_af, afconfig);
	spin_unlock_irqrestore(isp_af->lock, irqflags);
	if (result) {
		dev_dbg(dev, "af: wrong configure params received.\n");
		return result;
	}

	/* Compute buffer size */
	buf_size = (afconfig->paxel_config.hz_cnt + 1) *
		   (afconfig->paxel_config.vt_cnt + 1) * AF_PAXEL_SIZE;

	result = ispstat_bufs_alloc(&isp_af->stat, buf_size, 0);
	if (result)
		return result;

	spin_lock_irqsave(isp_af->lock, irqflags);
	isp_af->buf_size = buf_size;
	isp_af_update_params(isp_af, afconfig);
	spin_unlock_irqrestore(isp_af->lock, irqflags);

	/* Success */
	return 0;
}
EXPORT_SYMBOL(isp_af_config);

/*
 * This API allows the user to update White Balance gains, as well as
 * exposure time and analog gain. It is also used to request frame
 * statistics.
 */
int isp_af_request_statistics(struct isp_af_device *isp_af,
			      struct isp_af_data *afdata)
{
	struct device *dev = to_device(isp_af);
	struct ispstat_buffer *buf;

	if (!isp_af->config.af_config) {
		dev_dbg(dev, "af: statistics requested while af engine"
			     " is not configured\n");
		return -EINVAL;
	}

	if (afdata->update & REQUEST_STATISTICS) {
		buf = ispstat_buf_get(&isp_af->stat,
			      (void *)afdata->af_statistics_buf,
			      afdata->frame_number);
		if (IS_ERR(buf))
			return PTR_ERR(buf);

		afdata->xtrastats.ts = buf->ts;
		afdata->config_counter = buf->config_counter;
		afdata->frame_number = buf->frame_number;

		ispstat_buf_release(&isp_af->stat);
	}

	afdata->curr_frame = isp_af->stat.frame_number;

	return 0;
}
EXPORT_SYMBOL(isp_af_request_statistics);

/* This function will handle the AF buffer. */
int isp_af_buf_process(struct isp_af_device *isp_af)
{
	if (likely(!isp_af->buf_err && isp_af->config.af_config)) {
		int ret;

		ret = ispstat_buf_queue(&isp_af->stat);
		isp_af->buf_next = ispstat_buf_next(&isp_af->stat);
		return ret;
	} else {
		isp_af->buf_err = 0;
		return -1;
	}
}

static void __isp_af_enable(struct isp_af_device *isp_af, int enable)
{
	struct device *dev = to_device(isp_af);
	unsigned int pcr;

	pcr = isp_reg_readl(dev, OMAP3_ISP_IOMEM_H3A, ISPH3A_PCR);

	/* Set AF_EN bit in PCR Register */
	if (enable)
		pcr |= AF_EN;
	else
		pcr &= ~AF_EN;

	isp_reg_writel(dev, pcr, OMAP3_ISP_IOMEM_H3A, ISPH3A_PCR);
}

/* Function to Enable/Disable AF Engine */
void isp_af_enable(struct isp_af_device *isp_af, int enable)
{
	unsigned long irqflags;

	spin_lock_irqsave(isp_af->lock, irqflags);

	if (!isp_af->config.af_config && enable) {
		spin_unlock_irqrestore(isp_af->lock, irqflags);
		return;
	}

	__isp_af_enable(isp_af, enable);
	isp_af->enabled = enable;

	spin_unlock_irqrestore(isp_af->lock, irqflags);
}

/* Function to Suspend AF Engine */
void isp_af_suspend(struct isp_af_device *isp_af)
{
	unsigned long irqflags;

	spin_lock_irqsave(isp_af->lock, irqflags);
	if (isp_af->enabled)
		__isp_af_enable(isp_af, 0);
	spin_unlock_irqrestore(isp_af->lock, irqflags);
}

/* Function to Resume AF Engine */
void isp_af_resume(struct isp_af_device *isp_af)
{
	unsigned long irqflags;

	spin_lock_irqsave(isp_af->lock, irqflags);
	if (isp_af->enabled)
		__isp_af_enable(isp_af, 1);
	spin_unlock_irqrestore(isp_af->lock, irqflags);
}

int isp_af_busy(struct isp_af_device *isp_af)
{
	struct device *dev = to_device(isp_af);

	return isp_reg_readl(dev, OMAP3_ISP_IOMEM_H3A, ISPH3A_PCR)
		& ISPH3A_PCR_BUSYAF;
}

/* Function to register the AF character device driver. */
int __init isp_af_init(struct device *dev)
{
	struct isp_device *isp = dev_get_drvdata(dev);
	struct isp_af_device *isp_af = &isp->isp_af;

	isp_af->lock = &isp->h3a_lock;
	ispstat_init(dev, "AF", &isp_af->stat, H3A_MAX_BUFF, MAX_FRAME_COUNT);

	return 0;
}

void isp_af_exit(struct device *dev)
{
	struct isp_device *isp = dev_get_drvdata(dev);

	/* Free buffers */
	ispstat_free(&isp->isp_af.stat);
}
