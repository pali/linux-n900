/*
 * ispresizer.c
 *
 * Driver Library for Resizer module in TI's OMAP3 Camera ISP
 *
 * Copyright (C)2009 Texas Instruments, Inc.
 *
 * Contributors:
 * 	Sameer Venkatraman <sameerv@ti.com>
 * 	Mohit Jalori
 *	Sergio Aguirre <saaguirre@ti.com>
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <linux/module.h>
#include <linux/device.h>

#include "isp.h"
#include "ispreg.h"
#include "ispresizer.h"

/* Default configuration of resizer,filter coefficients,yenh for camera isp */
static struct isprsz_coef ispreszdefcoef = {
	{
		0x0000, 0x0100, 0x0000, 0x0000,
		0x03FA, 0x00F6, 0x0010, 0x0000,
		0x03F9, 0x00DB, 0x002C, 0x0000,
		0x03FB, 0x00B3, 0x0053, 0x03FF,
		0x03FD, 0x0082, 0x0084, 0x03FD,
		0x03FF, 0x0053, 0x00B3, 0x03FB,
		0x0000, 0x002C, 0x00DB, 0x03F9,
		0x0000, 0x0010, 0x00F6, 0x03FA
	},
	{
		0x0000, 0x0100, 0x0000, 0x0000,
		0x03FA, 0x00F6, 0x0010, 0x0000,
		0x03F9, 0x00DB, 0x002C, 0x0000,
		0x03FB, 0x00B3, 0x0053, 0x03FF,
		0x03FD, 0x0082, 0x0084, 0x03FD,
		0x03FF, 0x0053, 0x00B3, 0x03FB,
		0x0000, 0x002C, 0x00DB, 0x03F9,
		0x0000, 0x0010, 0x00F6, 0x03FA
	},
	{
		0x0004, 0x0023, 0x005A, 0x0058,
		0x0023, 0x0004, 0x0000, 0x0002,
		0x0018, 0x004d, 0x0060, 0x0031,
		0x0008, 0x0000, 0x0001, 0x000f,
		0x003f, 0x0062, 0x003f, 0x000f,
		0x0001, 0x0000, 0x0008, 0x0031,
		0x0060, 0x004d, 0x0018, 0x0002
	},
	{
		0x0004, 0x0023, 0x005A, 0x0058,
		0x0023, 0x0004, 0x0000, 0x0002,
		0x0018, 0x004d, 0x0060, 0x0031,
		0x0008, 0x0000, 0x0001, 0x000f,
		0x003f, 0x0062, 0x003f, 0x000f,
		0x0001, 0x0000, 0x0008, 0x0031,
		0x0060, 0x004d, 0x0018, 0x0002
	}
};

/* Structure for saving/restoring resizer module registers */
static struct isp_reg isprsz_reg_list[] = {
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_CNT, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_OUT_SIZE, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_IN_START, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_IN_SIZE, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_SDR_INADD, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_SDR_INOFF, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_SDR_OUTADD, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_SDR_OUTOFF, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_HFILT10, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_HFILT32, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_HFILT54, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_HFILT76, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_HFILT98, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_HFILT1110, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_HFILT1312, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_HFILT1514, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_HFILT1716, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_HFILT1918, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_HFILT2120, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_HFILT2322, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_HFILT2524, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_HFILT2726, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_HFILT2928, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_HFILT3130, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_VFILT10, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_VFILT32, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_VFILT54, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_VFILT76, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_VFILT98, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_VFILT1110, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_VFILT1312, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_VFILT1514, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_VFILT1716, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_VFILT1918, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_VFILT2120, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_VFILT2322, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_VFILT2524, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_VFILT2726, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_VFILT2928, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_VFILT3130, 0x0000},
	{OMAP3_ISP_IOMEM_RESZ, ISPRSZ_YENH, 0x0000},
	{0, ISP_TOK_TERM, 0x0000}
};

/**
 * ispresizer_applycrop - Apply crop to input image.
 **/
void ispresizer_applycrop(struct isp_res_device *isp_res)
{
	struct isp_device *isp = to_isp_device(isp_res);

	if (!isp_res->applycrop)
		return;

	ispresizer_s_pipeline(isp_res, &isp->pipeline);

	isp_res->applycrop = 0;

	return;
}

/**
 * ispresizer_config_shadow_registers - Configure shadow registers.
 **/
void ispresizer_config_shadow_registers(struct isp_res_device *isp_res)
{
	ispresizer_applycrop(isp_res);

	return;
}

int ispresizer_config_crop(struct isp_res_device *isp_res,
			   struct v4l2_crop *a)
{
	struct isp_device *isp = to_isp_device(isp_res);
	struct v4l2_crop *crop = a;
	int rval;

	if (crop->c.left < 0)
		crop->c.left = 0;
	if (crop->c.width < 0)
		crop->c.width = 0;
	if (crop->c.top < 0)
		crop->c.top = 0;
	if (crop->c.height < 0)
		crop->c.height = 0;

	if (crop->c.left >= isp->pipeline.prv_out_w_img)
		crop->c.left = isp->pipeline.prv_out_w_img - 1;
	if (crop->c.top >= isp->pipeline.rsz_out_h)
		crop->c.top = isp->pipeline.rsz_out_h - 1;

	/* Make sure the crop rectangle is never smaller than width
	 * and height divided by 4, since the resizer cannot upscale it
	 * by more than 4x. */

	if (crop->c.width < (isp->pipeline.rsz_out_w + 3) / 4)
		crop->c.width = (isp->pipeline.rsz_out_w + 3) / 4;
	if (crop->c.height < (isp->pipeline.rsz_out_h + 3) / 4)
		crop->c.height = (isp->pipeline.rsz_out_h + 3) / 4;

	if (crop->c.left + crop->c.width > isp->pipeline.prv_out_w_img)
		crop->c.width = isp->pipeline.prv_out_w_img - crop->c.left;
	if (crop->c.top + crop->c.height > isp->pipeline.prv_out_h_img)
		crop->c.height = isp->pipeline.prv_out_h_img - crop->c.top;

	isp->pipeline.rsz_crop = crop->c;

	rval = ispresizer_try_pipeline(isp_res, &isp->pipeline);
	if (rval)
		return rval;

	isp_res->applycrop = 1;

	if (isp->running == ISP_STOPPED)
		ispresizer_applycrop(isp_res);

	return 0;
}

/**
 * ispresizer_request - Reserves the Resizer module.
 *
 * Allows only one user at a time.
 *
 * Returns 0 if successful, or -EBUSY if resizer module was already requested.
 **/
int ispresizer_request(struct isp_res_device *isp_res)
{
	struct device *dev = to_device(isp_res);

	mutex_lock(&isp_res->ispres_mutex);
	if (!isp_res->res_inuse) {
		isp_res->res_inuse = 1;
		mutex_unlock(&isp_res->ispres_mutex);
		isp_reg_writel(dev,
			       isp_reg_readl(dev,
					     OMAP3_ISP_IOMEM_MAIN, ISP_CTRL) |
			       ISPCTRL_SBL_WR0_RAM_EN |
			       ISPCTRL_RSZ_CLK_EN,
			       OMAP3_ISP_IOMEM_MAIN, ISP_CTRL);
		return 0;
	} else {
		mutex_unlock(&isp_res->ispres_mutex);
		dev_err(dev, "resizer: Module Busy\n");
		return -EBUSY;
	}
}

/**
 * ispresizer_free - Makes Resizer module free.
 *
 * Returns 0 if successful, or -EINVAL if resizer module was already freed.
 **/
int ispresizer_free(struct isp_res_device *isp_res)
{
	struct device *dev = to_device(isp_res);

	mutex_lock(&isp_res->ispres_mutex);
	if (isp_res->res_inuse) {
		isp_res->res_inuse = 0;
		mutex_unlock(&isp_res->ispres_mutex);
		isp_reg_and(dev, OMAP3_ISP_IOMEM_MAIN, ISP_CTRL,
			    ~(ISPCTRL_RSZ_CLK_EN | ISPCTRL_SBL_WR0_RAM_EN));
		return 0;
	} else {
		mutex_unlock(&isp_res->ispres_mutex);
		DPRINTK_ISPRESZ("ISP_ERR : Resizer Module already freed\n");
		return -EINVAL;
	}
}

/**
 * ispresizer_config_datapath - Specifies which input to use in resizer module
 * @input: Indicates the module that gives the image to resizer.
 *
 * Sets up the default resizer configuration according to the arguments.
 *
 * Returns 0 if successful, or -EINVAL if an unsupported input was requested.
 **/
int ispresizer_config_datapath(struct isp_res_device *isp_res,
			       struct isp_pipeline *pipe)
{
	struct device *dev = to_device(isp_res);
	u32 cnt = 0;

	DPRINTK_ISPRESZ("ispresizer_config_datapath()+\n");

	switch (pipe->rsz_in) {
	case RSZ_OTFLY_YUV:
		cnt &= ~ISPRSZ_CNT_INPTYP;
		cnt &= ~ISPRSZ_CNT_INPSRC;
		ispresizer_set_inaddr(isp_res, 0);
		ispresizer_config_inlineoffset(isp_res, 0);
		break;
	case RSZ_MEM_YUV:
		cnt |= ISPRSZ_CNT_INPSRC;
		cnt &= ~ISPRSZ_CNT_INPTYP;
		break;
	case RSZ_MEM_COL8:
		cnt |= ISPRSZ_CNT_INPSRC;
		cnt |= ISPRSZ_CNT_INPTYP;
		break;
	default:
		dev_err(dev, "resizer: Wrong Input\n");
		return -EINVAL;
	}
	isp_reg_or(dev, OMAP3_ISP_IOMEM_RESZ, ISPRSZ_CNT, cnt);
	ispresizer_config_ycpos(isp_res, 0);
	ispresizer_config_filter_coef(isp_res, &ispreszdefcoef);
	ispresizer_enable_cbilin(isp_res, 0);
	ispresizer_config_luma_enhance(isp_res, &isp_res->defaultyenh);
	DPRINTK_ISPRESZ("ispresizer_config_datapath()-\n");
	return 0;
}

/**
 * ispresizer_adjust_bandwidth - Reduces read bandwidth when scaling up.
 * Otherwise there will be SBL overflows.
 *
 * The ISP read speed is 256.0 / max(256, 1024 * ISPSBL_SDR_REQ_EXP). This
 * formula is correct, no matter what the TRM says. Thus, the first
 * step to use is 0.25 (REQ_EXP=1).
 *
 * Ratios:
 * 0 = 1.0
 * 1 = 0.25
 * 2 = 0.125
 * 3 = 0.083333...
 * 4 = 0.0625
 * 5 = 0.05 and so on...
 *
 * TRM says that read bandwidth should be no more than 83MB/s, half
 * of the maximum of 166MB/s.
 *
 * HOWEVER, the read speed must be chosen so that the resizer always
 * has time to process the frame before the next frame comes in.
 * Failure to do so will result in a pile-up and endless "resizer busy!"
 * messages.
 *
 * Zoom ratio must not exceed 4.0. This is checked in
 * ispresizer_check_crop_boundaries().
 **/
static void ispresizer_adjust_bandwidth(struct isp_res_device *isp_res,
					struct isp_pipeline *pipe)
{
	struct device *dev = to_device(isp_res);

	/* Table for dividers. This allows hand tuning. */
	static const unsigned char area_to_divider[] = {
		0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 4, 4, 5, 5, 5
	     /* 1........2...........3.......................4 Zoom level */
	};
	unsigned int input_area = pipe->rsz_crop.width * pipe->rsz_crop.height;
	unsigned int output_area = pipe->rsz_out_w * pipe->rsz_out_h;

	if (input_area < output_area && input_area > 0) {
		u32 val = area_to_divider[output_area / input_area - 1];
		DPRINTK_ISPRESZ("%s: area factor = %i, val = %i\n",
				__func__, output_area / input_area, val);
		isp_reg_writel(dev, val << ISPSBL_SDR_REQ_RSZ_EXP_SHIFT,
			       OMAP3_ISP_IOMEM_SBL, ISPSBL_SDR_REQ_EXP);
	} else {
		/* Required input bandwidth greater than output, no limit. */
		DPRINTK_ISPRESZ("%s: resetting\n", __func__);
		isp_reg_writel(dev, 0, OMAP3_ISP_IOMEM_SBL,
			       ISPSBL_SDR_REQ_EXP);
	}
}

/**
 * ispresizer_try_size - Validates input and output images size.
 * @input_w: input width for the resizer in number of pixels per line
 * @input_h: input height for the resizer in number of lines
 * @output_w: output width from the resizer in number of pixels per line
 *            resizer when writing to memory needs this to be multiple of 16.
 * @pipe->rsz_out_h: output height for the resizer in number of lines, must be
 *		     even.
 *
 * Calculates the horizontal and vertical resize ratio, number of pixels to
 * be cropped in the resizer module and checks the validity of various
 * parameters. Formula used for calculation is:-
 *
 * 8-phase 4-tap mode :-
 * inputwidth = (32 * sph + (ow - 1) * hrsz + 16) >> 8 + 7
 * inputheight = (32 * spv + (oh - 1) * vrsz + 16) >> 8 + 4
 * endpahse for width = ((32 * sph + (ow - 1) * hrsz + 16) >> 5) % 8
 * endphase for height = ((32 * sph + (oh - 1) * hrsz + 16) >> 5) % 8
 *
 * 4-phase 7-tap mode :-
 * inputwidth = (64 * sph + (ow - 1) * hrsz + 32) >> 8 + 7
 * inputheight = (64 * spv + (oh - 1) * vrsz + 32) >> 8 + 7
 * endpahse for width = ((64 * sph + (ow - 1) * hrsz + 32) >> 6) % 4
 * endphase for height = ((64 * sph + (oh - 1) * hrsz + 32) >> 6) % 4
 *
 * Where:
 * sph = Start phase horizontal
 * spv = Start phase vertical
 * ow = Output width
 * oh = Output height
 * hrsz = Horizontal resize value
 * vrsz = Vertical resize value
 *
 * Fills up the output/input widht/height, horizontal/vertical resize ratio,
 * horizontal/vertical crop variables in the isp_res structure.
 **/
int ispresizer_try_pipeline(struct isp_res_device *isp_res,
			    struct isp_pipeline *pipe)
{
	u32 rsz, rsz_7, rsz_4;
	u32 sph;
	int max_in_otf, max_out_7tap;

	if (pipe->rsz_crop.width < 32 || pipe->rsz_crop.height < 32) {
		DPRINTK_ISPCCDC("ISP_ERR: RESIZER cannot handle input width"
				" less than 32 pixels or height less than"
				" 32\n");
		return -EINVAL;
	}

	if (pipe->rsz_crop.height > MAX_IN_HEIGHT)
		return -EINVAL;

	if (pipe->rsz_out_w < 16)
		pipe->rsz_out_w = 16;

	if (pipe->rsz_out_h < 2)
		pipe->rsz_out_h = 2;

	if (omap_rev() == OMAP3430_REV_ES1_0) {
		max_in_otf = MAX_IN_WIDTH_ONTHEFLY_MODE;
		max_out_7tap = MAX_7TAP_VRSZ_OUTWIDTH;
	} else {
		max_in_otf = MAX_IN_WIDTH_ONTHEFLY_MODE_ES2;
		max_out_7tap = MAX_7TAP_VRSZ_OUTWIDTH_ES2;
	}

	if (pipe->rsz_in == RSZ_OTFLY_YUV) {
		if (pipe->rsz_crop.width > max_in_otf)
			return -EINVAL;
	} else {
		if (pipe->rsz_crop.width > MAX_IN_WIDTH_MEMORY_MODE)
			return -EINVAL;
	}

	pipe->rsz_out_h &= 0xfffffffe;
	sph = DEFAULTSTPHASE;

	rsz_7 = ((pipe->rsz_crop.height - 7) * 256) / (pipe->rsz_out_h - 1);
	rsz_4 = ((pipe->rsz_crop.height - 4) * 256) / (pipe->rsz_out_h - 1);

	rsz = (pipe->rsz_crop.height * 256) / pipe->rsz_out_h;

	if (rsz <= MID_RESIZE_VALUE) {
		rsz = rsz_4;
		if (rsz < MINIMUM_RESIZE_VALUE) {
			rsz = MINIMUM_RESIZE_VALUE;
			pipe->rsz_out_h =
				(((pipe->rsz_crop.height - 4) * 256) / rsz) + 1;
		}
	} else {
		rsz = rsz_7;
		if (pipe->rsz_out_w > max_out_7tap)
			pipe->rsz_out_w = max_out_7tap;
		if (rsz > MAXIMUM_RESIZE_VALUE) {
			rsz = MAXIMUM_RESIZE_VALUE;
			pipe->rsz_out_h =
				(((pipe->rsz_crop.height - 7) * 256) / rsz) + 1;
		}
	}

	if (rsz > MID_RESIZE_VALUE) {
		pipe->rsz_crop.height =
			(((64 * sph) + ((pipe->rsz_out_h - 1) * rsz) + 32)
			 / 256) + 7;
	} else {
		pipe->rsz_crop.height =
			(((32 * sph) + ((pipe->rsz_out_h - 1) * rsz) + 16)
			 / 256) + 4;
	}

	isp_res->v_resz = rsz;
	/* FIXME: pipe->rsz_crop.height here is the real input height! */
	isp_res->v_startphase = sph;

	pipe->rsz_out_w &= 0xfffffff0;
	sph = DEFAULTSTPHASE;

	rsz_7 = ((pipe->rsz_crop.width - 7) * 256) / (pipe->rsz_out_w - 1);
	rsz_4 = ((pipe->rsz_crop.width - 4) * 256) / (pipe->rsz_out_w - 1);

	rsz = (pipe->rsz_crop.width * 256) / pipe->rsz_out_w;
	if (rsz > MID_RESIZE_VALUE) {
		rsz = rsz_7;
		if (rsz > MAXIMUM_RESIZE_VALUE) {
			rsz = MAXIMUM_RESIZE_VALUE;
			pipe->rsz_out_w =
				(((pipe->rsz_crop.width - 7) * 256) / rsz) + 1;
			pipe->rsz_out_w = (pipe->rsz_out_w + 0xf) & 0xfffffff0;
		}
	} else {
		rsz = rsz_4;
		if (rsz < MINIMUM_RESIZE_VALUE) {
			rsz = MINIMUM_RESIZE_VALUE;
			pipe->rsz_out_w =
				(((pipe->rsz_crop.width - 4) * 256) / rsz) + 1;
			pipe->rsz_out_w = (pipe->rsz_out_w + 0xf) & 0xfffffff0;
		}
	}

	/* Recalculate input based on TRM equations */
	if (rsz > MID_RESIZE_VALUE) {
		pipe->rsz_crop.width =
			(((64 * sph) + ((pipe->rsz_out_w - 1) * rsz) + 32)
			 / 256) + 7;
	} else {
		pipe->rsz_crop.width =
			(((32 * sph) + ((pipe->rsz_out_w - 1) * rsz) + 16)
			 / 256) + 7;
	}

	isp_res->h_resz = rsz;
	/* FIXME: pipe->rsz_crop.width here is the real input width! */
	isp_res->h_startphase = sph;

	pipe->rsz_out_w_img = pipe->rsz_out_w;

	return 0;
}

/**
 * ispresizer_config_size - Configures input and output image size.
 * @pipe->rsz_crop.width: input width for the resizer in number of pixels per
 *			  line.
 * @pipe->rsz_crop.height: input height for the resizer in number of lines.
 * @pipe->rsz_out_w: output width from the resizer in number of pixels per line.
 * @pipe->rsz_out_h: output height for the resizer in number of lines.
 *
 * Configures the appropriate values stored in the isp_res structure in the
 * resizer registers.
 *
 * Returns 0 if successful, or -EINVAL if passed values haven't been verified
 * with ispresizer_try_size() previously.
 **/
int ispresizer_s_pipeline(struct isp_res_device *isp_res,
			  struct isp_pipeline *pipe)
{
	struct device *dev = to_device(isp_res);
	int i, j;
	u32 res;
	int rval;

	rval = ispresizer_config_datapath(isp_res, pipe);
	if (rval)
		return rval;

	/* Set read bandwidth */
	ispresizer_adjust_bandwidth(isp_res, pipe);

	/* Set Resizer input address and offset adderss */
	ispresizer_config_inlineoffset(isp_res,
				       pipe->prv_out_w * ISP_BYTES_PER_PIXEL);

	res = isp_reg_readl(dev, OMAP3_ISP_IOMEM_RESZ, ISPRSZ_CNT) &
		~(ISPRSZ_CNT_HSTPH_MASK | ISPRSZ_CNT_VSTPH_MASK);
	isp_reg_writel(dev, res |
		       (isp_res->h_startphase << ISPRSZ_CNT_HSTPH_SHIFT) |
		       (isp_res->v_startphase << ISPRSZ_CNT_VSTPH_SHIFT),
		       OMAP3_ISP_IOMEM_RESZ,
		       ISPRSZ_CNT);
	/* Set start address for cropping */
	ispresizer_set_inaddr(isp_res, isp_res->tmp_buf);

	isp_reg_writel(dev,
		       (pipe->rsz_crop.width << ISPRSZ_IN_SIZE_HORZ_SHIFT) |
		       (pipe->rsz_crop.height <<
			ISPRSZ_IN_SIZE_VERT_SHIFT),
		       OMAP3_ISP_IOMEM_RESZ,
		       ISPRSZ_IN_SIZE);
	if (!isp_res->algo) {
		isp_reg_writel(dev,
			       (pipe->rsz_out_w << ISPRSZ_OUT_SIZE_HORZ_SHIFT) |
			       (pipe->rsz_out_h << ISPRSZ_OUT_SIZE_VERT_SHIFT),
			       OMAP3_ISP_IOMEM_RESZ,
			       ISPRSZ_OUT_SIZE);
	} else {
		isp_reg_writel(dev,
			       ((pipe->rsz_out_w - 4)
				<< ISPRSZ_OUT_SIZE_HORZ_SHIFT) |
			       (pipe->rsz_out_h << ISPRSZ_OUT_SIZE_VERT_SHIFT),
			       OMAP3_ISP_IOMEM_RESZ,
			       ISPRSZ_OUT_SIZE);
	}

	res = isp_reg_readl(dev, OMAP3_ISP_IOMEM_RESZ, ISPRSZ_CNT) &
		~(ISPRSZ_CNT_HRSZ_MASK | ISPRSZ_CNT_VRSZ_MASK);
	isp_reg_writel(dev, res |
		       ((isp_res->h_resz - 1) << ISPRSZ_CNT_HRSZ_SHIFT) |
		       ((isp_res->v_resz - 1) << ISPRSZ_CNT_VRSZ_SHIFT),
		       OMAP3_ISP_IOMEM_RESZ,
		       ISPRSZ_CNT);
	if (isp_res->h_resz <= MID_RESIZE_VALUE) {
		j = 0;
		for (i = 0; i < 16; i++) {
			isp_reg_writel(dev,
				(isp_res->coeflist.h_filter_coef_4tap[j]
				 << ISPRSZ_HFILT10_COEF0_SHIFT) |
				(isp_res->coeflist.h_filter_coef_4tap[j + 1]
				 << ISPRSZ_HFILT10_COEF1_SHIFT),
				OMAP3_ISP_IOMEM_RESZ,
				ISPRSZ_HFILT10 + (i * 0x04));
			j += 2;
		}
	} else {
		j = 0;
		for (i = 0; i < 16; i++) {
			if ((i + 1) % 4 == 0) {
				isp_reg_writel(dev,
					       (isp_res->coeflist.
						h_filter_coef_7tap[j] <<
						ISPRSZ_HFILT10_COEF0_SHIFT),
					       OMAP3_ISP_IOMEM_RESZ,
					       ISPRSZ_HFILT10 + (i * 0x04));
				j += 1;
			} else {
				isp_reg_writel(dev,
					       (isp_res->coeflist.
						h_filter_coef_7tap[j] <<
						ISPRSZ_HFILT10_COEF0_SHIFT) |
					       (isp_res->coeflist.
						h_filter_coef_7tap[j+1] <<
						ISPRSZ_HFILT10_COEF1_SHIFT),
					       OMAP3_ISP_IOMEM_RESZ,
					       ISPRSZ_HFILT10 + (i * 0x04));
				j += 2;
			}
		}
	}
	if (isp_res->v_resz <= MID_RESIZE_VALUE) {
		j = 0;
		for (i = 0; i < 16; i++) {
			isp_reg_writel(dev, (isp_res->coeflist.
					v_filter_coef_4tap[j] <<
					ISPRSZ_VFILT10_COEF0_SHIFT) |
				       (isp_res->coeflist.
					v_filter_coef_4tap[j + 1] <<
					ISPRSZ_VFILT10_COEF1_SHIFT),
				       OMAP3_ISP_IOMEM_RESZ,
				       ISPRSZ_VFILT10 + (i * 0x04));
			j += 2;
		}
	} else {
		j = 0;
		for (i = 0; i < 16; i++) {
			if ((i + 1) % 4 == 0) {
				isp_reg_writel(dev,
					       (isp_res->coeflist.
						v_filter_coef_7tap[j] <<
						ISPRSZ_VFILT10_COEF0_SHIFT),
					       OMAP3_ISP_IOMEM_RESZ,
					       ISPRSZ_VFILT10 + (i * 0x04));
				j += 1;
			} else {
				isp_reg_writel(dev,
					       (isp_res->coeflist.
						v_filter_coef_7tap[j] <<
						ISPRSZ_VFILT10_COEF0_SHIFT) |
					       (isp_res->coeflist.
						v_filter_coef_7tap[j+1] <<
						ISPRSZ_VFILT10_COEF1_SHIFT),
					       OMAP3_ISP_IOMEM_RESZ,
					       ISPRSZ_VFILT10 + (i * 0x04));
				j += 2;
			}
		}
	}

	ispresizer_config_outlineoffset(isp_res, pipe->rsz_out_w*2);

	if (pipe->pix.pixelformat == V4L2_PIX_FMT_UYVY)
		ispresizer_config_ycpos(isp_res, 0);
	else
		ispresizer_config_ycpos(isp_res, 1);

	DPRINTK_ISPRESZ("ispresizer_config_size()-\n");
	return 0;
}

/**
 * ispresizer_enable - Enables the resizer module.
 * @enable: 1 - Enable, 0 - Disable
 *
 * Client should configure all the sub modules in resizer before this.
 **/
void ispresizer_enable(struct isp_res_device *isp_res, int enable)
{
	struct device *dev = to_device(isp_res);
	int val;

	DPRINTK_ISPRESZ("+ispresizer_enable()+\n");
	if (enable) {
		val = (isp_reg_readl(dev,
				     OMAP3_ISP_IOMEM_RESZ, ISPRSZ_PCR) & 0x2) |
			ISPRSZ_PCR_ENABLE;
	} else {
		val = isp_reg_readl(dev,
				    OMAP3_ISP_IOMEM_RESZ, ISPRSZ_PCR) &
			~ISPRSZ_PCR_ENABLE;
	}
	isp_reg_writel(dev, val, OMAP3_ISP_IOMEM_RESZ, ISPRSZ_PCR);
	DPRINTK_ISPRESZ("+ispresizer_enable()-\n");
}

/**
 * ispresizer_busy - Checks if ISP resizer is busy.
 *
 * Returns busy field from ISPRSZ_PCR register.
 **/
int ispresizer_busy(struct isp_res_device *isp_res)
{
	struct device *dev = to_device(isp_res);

	return isp_reg_readl(dev, OMAP3_ISP_IOMEM_RESZ, ISPRSZ_PCR) &
		ISPPRV_PCR_BUSY;
}

/**
 * ispresizer_config_startphase - Sets the horizontal and vertical start phase.
 * @hstartphase: horizontal start phase (0 - 7).
 * @vstartphase: vertical startphase (0 - 7).
 *
 * This API just updates the isp_res struct. Actual register write happens in
 * ispresizer_config_size.
 **/
void ispresizer_config_startphase(struct isp_res_device *isp_res,
				  u8 hstartphase, u8 vstartphase)
{
	DPRINTK_ISPRESZ("ispresizer_config_startphase()+\n");
	isp_res->h_startphase = hstartphase;
	isp_res->v_startphase = vstartphase;
	DPRINTK_ISPRESZ("ispresizer_config_startphase()-\n");
}

/**
 * ispresizer_config_ycpos - Specifies if output should be in YC or CY format.
 * @yc: 0 - YC format, 1 - CY format
 **/
void ispresizer_config_ycpos(struct isp_res_device *isp_res, u8 yc)
{
	struct device *dev = to_device(isp_res);

	DPRINTK_ISPRESZ("ispresizer_config_ycpos()+\n");
	isp_reg_and_or(dev, OMAP3_ISP_IOMEM_RESZ, ISPRSZ_CNT,
		       ~ISPRSZ_CNT_YCPOS, (yc ? ISPRSZ_CNT_YCPOS : 0));
	DPRINTK_ISPRESZ("ispresizer_config_ycpos()-\n");
}

/**
 * Sets the chrominance algorithm
 * @cbilin: 0 - chrominance uses same processing as luminance,
 *          1 - bilinear interpolation processing
 **/
void ispresizer_enable_cbilin(struct isp_res_device *isp_res, u8 enable)
{
	struct device *dev = to_device(isp_res);

	DPRINTK_ISPRESZ("ispresizer_enable_cbilin()+\n");
	isp_reg_and_or(dev, OMAP3_ISP_IOMEM_RESZ, ISPRSZ_CNT,
		       ~ISPRSZ_CNT_CBILIN, (enable ? ISPRSZ_CNT_CBILIN : 0));
	DPRINTK_ISPRESZ("ispresizer_enable_cbilin()-\n");
}

/**
 * ispresizer_config_luma_enhance - Configures luminance enhancer parameters.
 * @yenh: Pointer to structure containing desired values for core, slope, gain
 *        and algo parameters.
 **/
void ispresizer_config_luma_enhance(struct isp_res_device *isp_res,
				    struct isprsz_yenh *yenh)
{
	struct device *dev = to_device(isp_res);

	DPRINTK_ISPRESZ("ispresizer_config_luma_enhance()+\n");
	isp_res->algo = yenh->algo;
	isp_reg_writel(dev, (yenh->algo << ISPRSZ_YENH_ALGO_SHIFT) |
		       (yenh->gain << ISPRSZ_YENH_GAIN_SHIFT) |
		       (yenh->slope << ISPRSZ_YENH_SLOP_SHIFT) |
		       (yenh->coreoffset << ISPRSZ_YENH_CORE_SHIFT),
		       OMAP3_ISP_IOMEM_RESZ,
		       ISPRSZ_YENH);
	DPRINTK_ISPRESZ("ispresizer_config_luma_enhance()-\n");
}

/**
 * ispresizer_config_filter_coef - Sets filter coefficients for 4 & 7-tap mode.
 * This API just updates the isp_res struct.Actual register write happens in
 * ispresizer_config_size.
 * @coef: Structure containing horizontal and vertical filter coefficients for
 *        both 4-tap and 7-tap mode.
 **/
void ispresizer_config_filter_coef(struct isp_res_device *isp_res,
				   struct isprsz_coef *coef)
{
	int i;
	DPRINTK_ISPRESZ("ispresizer_config_filter_coef()+\n");
	for (i = 0; i < 32; i++) {
		isp_res->coeflist.h_filter_coef_4tap[i] =
			coef->h_filter_coef_4tap[i];
		isp_res->coeflist.v_filter_coef_4tap[i] =
			coef->v_filter_coef_4tap[i];
	}
	for (i = 0; i < 28; i++) {
		isp_res->coeflist.h_filter_coef_7tap[i] =
			coef->h_filter_coef_7tap[i];
		isp_res->coeflist.v_filter_coef_7tap[i] =
			coef->v_filter_coef_7tap[i];
	}
	DPRINTK_ISPRESZ("ispresizer_config_filter_coef()-\n");
}

/**
 * ispresizer_config_inlineoffset - Configures the read address line offset.
 * @offset: Line Offset for the input image.
 *
 * Returns 0 if successful, or -EINVAL if offset is not 32 bits aligned.
 **/
int ispresizer_config_inlineoffset(struct isp_res_device *isp_res, u32 offset)
{
	struct device *dev = to_device(isp_res);

	DPRINTK_ISPRESZ("ispresizer_config_inlineoffset()+\n");
	if (offset % 32)
		return -EINVAL;
	isp_reg_writel(dev, offset << ISPRSZ_SDR_INOFF_OFFSET_SHIFT,
		       OMAP3_ISP_IOMEM_RESZ, ISPRSZ_SDR_INOFF);
	DPRINTK_ISPRESZ("ispresizer_config_inlineoffset()-\n");
	return 0;
}

/**
 * ispresizer_set_inaddr - Sets the memory address of the input frame.
 * @addr: 32bit memory address aligned on 32byte boundary.
 *
 * Returns 0 if successful, or -EINVAL if address is not 32 bits aligned.
 **/
int ispresizer_set_inaddr(struct isp_res_device *isp_res, u32 addr)
{
	struct device *dev = to_device(isp_res);
	struct isp_device *isp = to_isp_device(isp_res);

	DPRINTK_ISPRESZ("ispresizer_set_inaddr()+\n");

	if (addr % 32)
		return -EINVAL;
	isp_res->tmp_buf = addr;
	/* FIXME: is this the right place to put crop-related junk? */
	isp_reg_writel(dev,
		       isp_res->tmp_buf + ISP_BYTES_PER_PIXEL
		       * ((isp->pipeline.rsz_crop.left & ~0xf) +
			  isp->pipeline.prv_out_w
			  * isp->pipeline.rsz_crop.top),
		       OMAP3_ISP_IOMEM_RESZ, ISPRSZ_SDR_INADD);
	/* Set the fractional part of the starting address. Needed for crop */
	isp_reg_writel(dev, ((isp->pipeline.rsz_crop.left & 0xf) <<
		       ISPRSZ_IN_START_HORZ_ST_SHIFT) |
		       (0x00 << ISPRSZ_IN_START_VERT_ST_SHIFT),
		       OMAP3_ISP_IOMEM_RESZ, ISPRSZ_IN_START);

	DPRINTK_ISPRESZ("ispresizer_set_inaddr()-\n");
	return 0;
}

/**
 * ispresizer_config_outlineoffset - Configures the write address line offset.
 * @offset: Line offset for the preview output.
 *
 * Returns 0 if successful, or -EINVAL if address is not 32 bits aligned.
 **/
int ispresizer_config_outlineoffset(struct isp_res_device *isp_res, u32 offset)
{
	struct device *dev = to_device(isp_res);

	DPRINTK_ISPRESZ("ispresizer_config_outlineoffset()+\n");
	if (offset % 32)
		return -EINVAL;
	isp_reg_writel(dev, offset << ISPRSZ_SDR_OUTOFF_OFFSET_SHIFT,
		       OMAP3_ISP_IOMEM_RESZ, ISPRSZ_SDR_OUTOFF);
	DPRINTK_ISPRESZ("ispresizer_config_outlineoffset()-\n");
	return 0;
}

/**
 * Configures the memory address to which the output frame is written.
 * @addr: 32bit memory address aligned on 32byte boundary.
 **/
int ispresizer_set_outaddr(struct isp_res_device *isp_res, u32 addr)
{
	struct device *dev = to_device(isp_res);

	DPRINTK_ISPRESZ("ispresizer_set_outaddr()+\n");
	if (addr % 32)
		return -EINVAL;
	isp_reg_writel(dev, addr << ISPRSZ_SDR_OUTADD_ADDR_SHIFT,
		       OMAP3_ISP_IOMEM_RESZ, ISPRSZ_SDR_OUTADD);
	DPRINTK_ISPRESZ("ispresizer_set_outaddr()-\n");
	return 0;
}

/**
 * ispresizer_save_context - Saves the values of the resizer module registers.
 **/
void ispresizer_save_context(struct device *dev)
{
	DPRINTK_ISPRESZ("Saving context\n");
	isp_save_context(dev, isprsz_reg_list);
}

/**
 * ispresizer_restore_context - Restores resizer module register values.
 **/
void ispresizer_restore_context(struct device *dev)
{
	DPRINTK_ISPRESZ("Restoring context\n");
	isp_restore_context(dev, isprsz_reg_list);
}

/**
 * ispresizer_print_status - Prints the values of the resizer module registers.
 **/
void ispresizer_print_status(struct isp_res_device *isp_res)
{
#ifdef OMAP_ISPRESZ_DEBUG
	struct device *dev = to_device(isp_res);
#endif

	if (!is_ispresz_debug_enabled())
		return;
	DPRINTK_ISPRESZ("###ISP_CTRL inresizer =0x%x\n",
			isp_reg_readl(dev,
				      OMAP3_ISP_IOMEM_MAIN, ISP_CTRL));
	DPRINTK_ISPRESZ("###ISP_IRQ0ENABLE in resizer =0x%x\n",
			isp_reg_readl(dev,
				      OMAP3_ISP_IOMEM_MAIN, ISP_IRQ0ENABLE));
	DPRINTK_ISPRESZ("###ISP_IRQ0STATUS in resizer =0x%x\n",
			isp_reg_readl(dev,
				      OMAP3_ISP_IOMEM_MAIN, ISP_IRQ0STATUS));
	DPRINTK_ISPRESZ("###RSZ PCR =0x%x\n",
			isp_reg_readl(dev,
				      OMAP3_ISP_IOMEM_RESZ, ISPRSZ_PCR));
	DPRINTK_ISPRESZ("###RSZ CNT =0x%x\n",
			isp_reg_readl(dev,
				      OMAP3_ISP_IOMEM_RESZ, ISPRSZ_CNT));
	DPRINTK_ISPRESZ("###RSZ OUT SIZE =0x%x\n",
			isp_reg_readl(dev,
				      OMAP3_ISP_IOMEM_RESZ, ISPRSZ_OUT_SIZE));
	DPRINTK_ISPRESZ("###RSZ IN START =0x%x\n",
			isp_reg_readl(dev,
				      OMAP3_ISP_IOMEM_RESZ, ISPRSZ_IN_START));
	DPRINTK_ISPRESZ("###RSZ IN SIZE =0x%x\n",
			isp_reg_readl(dev,
				      OMAP3_ISP_IOMEM_RESZ, ISPRSZ_IN_SIZE));
	DPRINTK_ISPRESZ("###RSZ SDR INADD =0x%x\n",
			isp_reg_readl(dev,
				      OMAP3_ISP_IOMEM_RESZ, ISPRSZ_SDR_INADD));
	DPRINTK_ISPRESZ("###RSZ SDR INOFF =0x%x\n",
			isp_reg_readl(dev,
				      OMAP3_ISP_IOMEM_RESZ, ISPRSZ_SDR_INOFF));
	DPRINTK_ISPRESZ("###RSZ SDR OUTADD =0x%x\n",
			isp_reg_readl(dev,
				      OMAP3_ISP_IOMEM_RESZ, ISPRSZ_SDR_OUTADD));
	DPRINTK_ISPRESZ("###RSZ SDR OTOFF =0x%x\n",
			isp_reg_readl(dev,
				      OMAP3_ISP_IOMEM_RESZ, ISPRSZ_SDR_OUTOFF));
	DPRINTK_ISPRESZ("###RSZ YENH =0x%x\n",
			isp_reg_readl(dev,
				      OMAP3_ISP_IOMEM_RESZ, ISPRSZ_YENH));
}

/**
 * isp_resizer_init - Module Initialisation.
 *
 * Always returns 0.
 **/
int __init isp_resizer_init(struct device *dev)
{
	struct isp_device *isp = dev_get_drvdata(dev);
	struct isp_res_device *isp_res = &isp->isp_res;

	mutex_init(&isp_res->ispres_mutex);

	return 0;
}

/**
 * isp_resizer_cleanup - Module Cleanup.
 **/
void isp_resizer_cleanup(struct device *dev)
{
}
