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
		0x0027, 0x00B2, 0x00B2, 0x0027,
		0x0027, 0x00B2, 0x0027, 0x00B2,
		0x0027, 0x00B2, 0x0027, 0x00B2,
		0x0027, 0x00B2, 0x0027, 0x00B2,
		0x0027, 0x00B2, 0x0027, 0x00B2,
		0x0027, 0x00B2, 0x0027, 0x00B2,
		0x0027, 0x00B2, 0x0027, 0x00B2,
		0x0027, 0x00B2, 0x0027, 0x00B2,
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
		0x0004, 0x0023, 0x0023, 0x005A,
		0x005A, 0x0058, 0x0058, 0x0004,
		0x0023, 0x0023, 0x005A, 0x005A,
		0x0058, 0x0058, 0x0004, 0x0023,
		0x0023, 0x005A, 0x005A, 0x0058,
		0x0058, 0x0004, 0x0023, 0x0023,
		0x005A, 0x005A, 0x0058, 0x0058
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
 * ispresizer_config_shadow_registers - Configure shadow registers.
 **/
void ispresizer_config_shadow_registers(struct isp_res_device *isp_res)
{
	return;
}
EXPORT_SYMBOL(ispresizer_config_shadow_registers);

/**
 * ispresizer_trycrop - Validate crop dimensions.
 * @left: Left distance to start position of crop.
 * @top: Top distance to start position of crop.
 * @width: Width of input image.
 * @height: Height of input image.
 * @ow: Width of output image.
 * @oh: Height of output image.
 **/
void ispresizer_trycrop(struct isp_res_device *isp_res, u32 left, u32 top,
			    u32 width, u32 height, u32 ow, u32 oh)
{
	isp_res->cropwidth = width + 6;
	isp_res->cropheight = height + 6;
	ispresizer_try_size(isp_res, &isp_res->cropwidth, &isp_res->cropheight,
			    &ow, &oh);
	isp_res->ipht_crop = top;
	isp_res->ipwd_crop = left;
}
EXPORT_SYMBOL(ispresizer_trycrop);

/**
 * ispresizer_applycrop - Apply crop to input image.
 **/
void ispresizer_applycrop(struct isp_res_device *isp_res)
{
	if (!isp_res->applycrop)
		return;

	ispresizer_config_size(isp_res, isp_res->cropwidth, isp_res->cropheight,
			       isp_res->outputwidth,
			       isp_res->outputheight);

	isp_res->applycrop = 0;

	return;
}
EXPORT_SYMBOL(ispresizer_applycrop);

void ispresizer_config_crop(struct isp_res_device *isp_res,
			    struct v4l2_crop *a)
{
	struct isp_device *isp =
		container_of(isp_res, struct isp_device, isp_res);
	struct v4l2_crop *crop = a;

	crop->c.left &= ~0xf;
	crop->c.width &= ~0xf;

	if (crop->c.left < 0)
		crop->c.left = 0;
	if (crop->c.width < 0)
		crop->c.width = 0;
	if (crop->c.top < 0)
		crop->c.top = 0;
	if (crop->c.height < 0)
		crop->c.height = 0;

	if (crop->c.left >= isp->module.preview_output_width)
		crop->c.left = isp->module.preview_output_width - 1;
	if (crop->c.top >= isp->module.preview_output_height)
		crop->c.top = isp->module.preview_output_height - 1;

	if (crop->c.left + crop->c.width > isp->module.preview_output_width)
		crop->c.width = isp->module.preview_output_width - crop->c.left;
	if (crop->c.top + crop->c.height > isp->module.preview_output_height)
		crop->c.height =
			isp->module.preview_output_height - crop->c.top;

	isp_res->croprect.left = crop->c.left;
	isp_res->croprect.top = crop->c.top;
	isp_res->croprect.width = crop->c.width;
	isp_res->croprect.height = crop->c.height;

	ispresizer_trycrop(isp_res,
			   isp_res->croprect.left, isp_res->croprect.top,
			   isp_res->croprect.width, isp_res->croprect.height,
			   isp->module.resizer_output_width,
			   isp->module.resizer_output_height);

	isp_res->applycrop = 1;

	/* FIXME: ugly hack. */
	if (!ispresizer_busy(isp_res))
		ispresizer_applycrop(isp_res);

	return;
}
EXPORT_SYMBOL(ispresizer_config_crop);

/**
 * ispresizer_request - Reserves the Resizer module.
 *
 * Allows only one user at a time.
 *
 * Returns 0 if successful, or -EBUSY if resizer module was already requested.
 **/
int ispresizer_request(struct isp_res_device *isp_res)
{
	mutex_lock(&isp_res->ispres_mutex);
	if (!isp_res->res_inuse) {
		isp_res->res_inuse = 1;
		mutex_unlock(&isp_res->ispres_mutex);
		isp_reg_writel(isp_res->dev,
			       isp_reg_readl(isp_res->dev,
					     OMAP3_ISP_IOMEM_MAIN, ISP_CTRL) |
			       ISPCTRL_SBL_WR0_RAM_EN |
			       ISPCTRL_RSZ_CLK_EN,
			       OMAP3_ISP_IOMEM_MAIN, ISP_CTRL);
		return 0;
	} else {
		mutex_unlock(&isp_res->ispres_mutex);
		dev_err(isp_res->dev, "resizer: Module Busy\n");
		return -EBUSY;
	}
}
EXPORT_SYMBOL(ispresizer_request);

/**
 * ispresizer_free - Makes Resizer module free.
 *
 * Returns 0 if successful, or -EINVAL if resizer module was already freed.
 **/
int ispresizer_free(struct isp_res_device *isp_res)
{
	mutex_lock(&isp_res->ispres_mutex);
	if (isp_res->res_inuse) {
		isp_res->res_inuse = 0;
		mutex_unlock(&isp_res->ispres_mutex);
		isp_reg_and(isp_res->dev, OMAP3_ISP_IOMEM_MAIN, ISP_CTRL,
			    ~(ISPCTRL_RSZ_CLK_EN | ISPCTRL_SBL_WR0_RAM_EN));
		return 0;
	} else {
		mutex_unlock(&isp_res->ispres_mutex);
		DPRINTK_ISPRESZ("ISP_ERR : Resizer Module already freed\n");
		return -EINVAL;
	}
}
EXPORT_SYMBOL(ispresizer_free);

/**
 * ispresizer_config_datapath - Specifies which input to use in resizer module
 * @input: Indicates the module that gives the image to resizer.
 *
 * Sets up the default resizer configuration according to the arguments.
 *
 * Returns 0 if successful, or -EINVAL if an unsupported input was requested.
 **/
int ispresizer_config_datapath(struct isp_res_device *isp_res,
			       enum ispresizer_input input)
{
	u32 cnt = 0;
	DPRINTK_ISPRESZ("ispresizer_config_datapath()+\n");
	isp_res->resinput = input;
	switch (input) {
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
		dev_err(isp_res->dev, "resizer: Wrong Input\n");
		return -EINVAL;
	}
	isp_reg_or(isp_res->dev, OMAP3_ISP_IOMEM_RESZ, ISPRSZ_CNT, cnt);
	ispresizer_config_ycpos(isp_res, 0);
	ispresizer_config_filter_coef(isp_res, &ispreszdefcoef);
	ispresizer_enable_cbilin(isp_res, 0);
	ispresizer_config_luma_enhance(isp_res, &isp_res->defaultyenh);
	DPRINTK_ISPRESZ("ispresizer_config_datapath()-\n");
	return 0;
}
EXPORT_SYMBOL(ispresizer_config_datapath);

/**
 * ispresizer_try_size - Validates input and output images size.
 * @input_w: input width for the resizer in number of pixels per line
 * @input_h: input height for the resizer in number of lines
 * @output_w: output width from the resizer in number of pixels per line
 *            resizer when writing to memory needs this to be multiple of 16.
 * @output_h: output height for the resizer in number of lines, must be even.
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
int ispresizer_try_size(struct isp_res_device *isp_res, u32 *input_width,
			u32 *input_height, u32 *output_w, u32 *output_h)
{
	u32 rsz, rsz_7, rsz_4;
	u32 sph;
	u32 input_w, input_h;
	int max_in_otf, max_out_7tap;

	input_w = *input_width;
	input_h = *input_height;

	if (input_w < 32 || input_h < 32) {
		DPRINTK_ISPCCDC("ISP_ERR: RESIZER cannot handle input width"
				" less than 32 pixels or height less than"
				" 32\n");
		return -EINVAL;
	}
	input_w -= 6;
	input_h -= 6;

	if (input_h > MAX_IN_HEIGHT)
		return -EINVAL;

	if (*output_w < 16)
		*output_w = 16;

	if (*output_h < 2)
		*output_h = 2;

	if (omap_rev() == OMAP3430_REV_ES1_0) {
		max_in_otf = MAX_IN_WIDTH_ONTHEFLY_MODE;
		max_out_7tap = MAX_7TAP_VRSZ_OUTWIDTH;
	} else {
		max_in_otf = MAX_IN_WIDTH_ONTHEFLY_MODE_ES2;
		max_out_7tap = MAX_7TAP_VRSZ_OUTWIDTH_ES2;
	}

	if (isp_res->resinput == RSZ_OTFLY_YUV) {
		if (input_w > max_in_otf)
			return -EINVAL;
	} else {
		if (input_w > MAX_IN_WIDTH_MEMORY_MODE)
			return -EINVAL;
	}

	*output_h &= 0xfffffffe;
	sph = DEFAULTSTPHASE;

	rsz_7 = ((input_h - 7) * 256) / (*output_h - 1);
	rsz_4 = ((input_h - 4) * 256) / (*output_h - 1);

	rsz = (input_h * 256) / *output_h;

	if (rsz <= MID_RESIZE_VALUE) {
		rsz = rsz_4;
		if (rsz < MINIMUM_RESIZE_VALUE) {
			rsz = MINIMUM_RESIZE_VALUE;
			*output_h = (((input_h - 4) * 256) / rsz) + 1;
			dev_dbg(isp_res->dev,
				"resizer: %s: using output_h %d instead\n",
				__func__, *output_h);
		}
	} else {
		rsz = rsz_7;
		if (*output_w > max_out_7tap)
			*output_w = max_out_7tap;
		if (rsz > MAXIMUM_RESIZE_VALUE) {
			rsz = MAXIMUM_RESIZE_VALUE;
			*output_h = (((input_h - 7) * 256) / rsz) + 1;
			dev_dbg(isp_res->dev,
				"resizer: %s: using output_h %d instead\n",
				__func__, *output_h);
		}
	}

	if (rsz > MID_RESIZE_VALUE) {
		input_h =
			(((64 * sph) + ((*output_h - 1) * rsz) + 32) / 256) + 7;
	} else {
		input_h =
			(((32 * sph) + ((*output_h - 1) * rsz) + 16) / 256) + 4;
	}

	isp_res->outputheight = *output_h;
	isp_res->v_resz = rsz;
	isp_res->inputheight = input_h;
	isp_res->ipht_crop = DEFAULTSTPIXEL;
	isp_res->v_startphase = sph;

	*output_w &= 0xfffffff0;
	sph = DEFAULTSTPHASE;

	rsz_7 = ((input_w - 7) * 256) / (*output_w - 1);
	rsz_4 = ((input_w - 4) * 256) / (*output_w - 1);

	rsz = (input_w * 256) / *output_w;
	if (rsz > MID_RESIZE_VALUE) {
		rsz = rsz_7;
		if (rsz > MAXIMUM_RESIZE_VALUE) {
			rsz = MAXIMUM_RESIZE_VALUE;
			*output_w = (((input_w - 7) * 256) / rsz) + 1;
			*output_w = (*output_w + 0xf) & 0xfffffff0;
			dev_dbg(isp_res->dev,
				"resizer: %s: using output_w %d instead\n",
				__func__, *output_w);
		}
	} else {
		rsz = rsz_4;
		if (rsz < MINIMUM_RESIZE_VALUE) {
			rsz = MINIMUM_RESIZE_VALUE;
			*output_w = (((input_w - 4) * 256) / rsz) + 1;
			*output_w = (*output_w + 0xf) & 0xfffffff0;
			dev_dbg(isp_res->dev,
				"resizer: %s: using output_w %d instead\n",
				__func__, *output_w);
		}
	}

	/* Recalculate input based on TRM equations */
	if (rsz > MID_RESIZE_VALUE) {
		input_w =
			(((64 * sph) + ((*output_w - 1) * rsz) + 32) / 256) + 7;
	} else {
		input_w =
			(((32 * sph) + ((*output_w - 1) * rsz) + 16) / 256) + 7;
	}

	isp_res->outputwidth = *output_w;
	isp_res->h_resz = rsz;
	isp_res->inputwidth = input_w;
	isp_res->ipwd_crop = DEFAULTSTPIXEL;
	isp_res->h_startphase = sph;

	*input_height = input_h;
	*input_width = input_w;

	return 0;
}
EXPORT_SYMBOL(ispresizer_try_size);

/**
 * ispresizer_config_size - Configures input and output image size.
 * @input_w: input width for the resizer in number of pixels per line.
 * @input_h: input height for the resizer in number of lines.
 * @output_w: output width from the resizer in number of pixels per line.
 * @output_h: output height for the resizer in number of lines.
 *
 * Configures the appropriate values stored in the isp_res structure in the
 * resizer registers.
 *
 * Returns 0 if successful, or -EINVAL if passed values haven't been verified
 * with ispresizer_try_size() previously.
 **/
int ispresizer_config_size(struct isp_res_device *isp_res, u32 input_w,
			   u32 input_h, u32 output_w, u32 output_h)
{
	int i, j;
	u32 res;
	DPRINTK_ISPRESZ("ispresizer_config_size()+, input_w = %d,input_h ="
			" %d, output_w = %d, output_h"
			" = %d,hresz = %d,vresz = %d,"
			" hcrop = %d, vcrop = %d,"
			" hstph = %d, vstph = %d\n",
			isp_res->inputwidth,
			isp_res->inputheight,
			isp_res->outputwidth,
			isp_res->outputheight,
			isp_res->h_resz,
			isp_res->v_resz,
			isp_res->ipwd_crop,
			isp_res->ipht_crop,
			isp_res->h_startphase,
			isp_res->v_startphase);
	if ((output_w != isp_res->outputwidth)
	    || (output_h != isp_res->outputheight)) {
		dev_err(isp_res->dev,
			"resizer: Output parameters passed do not match the"
			" values calculated by the trysize passed w %d, h %d"
			" \n", output_w , output_h);
		return -EINVAL;
	}

	/* Set Resizer input address and offset adderss */
	ispresizer_config_inlineoffset(isp_res,
				       isp_reg_readl(isp_res->dev,
						     OMAP3_ISP_IOMEM_PREV,
						     ISPPRV_WADD_OFFSET));

	res = isp_reg_readl(isp_res->dev, OMAP3_ISP_IOMEM_RESZ, ISPRSZ_CNT) &
		~(ISPRSZ_CNT_HSTPH_MASK | ISPRSZ_CNT_VSTPH_MASK);
	isp_reg_writel(isp_res->dev, res |
		       (isp_res->h_startphase << ISPRSZ_CNT_HSTPH_SHIFT) |
		       (isp_res->v_startphase << ISPRSZ_CNT_VSTPH_SHIFT),
		       OMAP3_ISP_IOMEM_RESZ,
		       ISPRSZ_CNT);
	/* Set start address for cropping */
	ispresizer_set_inaddr(isp_res, isp_res->tmp_buf);

	isp_reg_writel(isp_res->dev,
		(0 << ISPRSZ_IN_START_HORZ_ST_SHIFT) |
		(0x00 << ISPRSZ_IN_START_VERT_ST_SHIFT),
		OMAP3_ISP_IOMEM_RESZ, ISPRSZ_IN_START);

	isp_reg_writel(isp_res->dev, (0x00 << ISPRSZ_IN_START_HORZ_ST_SHIFT) |
		       (0x00 << ISPRSZ_IN_START_VERT_ST_SHIFT),
		       OMAP3_ISP_IOMEM_RESZ,
		       ISPRSZ_IN_START);

	isp_reg_writel(isp_res->dev,
		       (isp_res->croprect.width << ISPRSZ_IN_SIZE_HORZ_SHIFT) |
		       (isp_res->croprect.height <<
			ISPRSZ_IN_SIZE_VERT_SHIFT),
		       OMAP3_ISP_IOMEM_RESZ,
		       ISPRSZ_IN_SIZE);
	if (!isp_res->algo) {
		isp_reg_writel(isp_res->dev,
			       (output_w << ISPRSZ_OUT_SIZE_HORZ_SHIFT) |
			       (output_h << ISPRSZ_OUT_SIZE_VERT_SHIFT),
			       OMAP3_ISP_IOMEM_RESZ,
			       ISPRSZ_OUT_SIZE);
	} else {
		isp_reg_writel(isp_res->dev,
			       ((output_w - 4) << ISPRSZ_OUT_SIZE_HORZ_SHIFT) |
			       (output_h << ISPRSZ_OUT_SIZE_VERT_SHIFT),
			       OMAP3_ISP_IOMEM_RESZ,
			       ISPRSZ_OUT_SIZE);
	}

	res = isp_reg_readl(isp_res->dev, OMAP3_ISP_IOMEM_RESZ, ISPRSZ_CNT) &
		~(ISPRSZ_CNT_HRSZ_MASK | ISPRSZ_CNT_VRSZ_MASK);
	isp_reg_writel(isp_res->dev, res |
		       ((isp_res->h_resz - 1) << ISPRSZ_CNT_HRSZ_SHIFT) |
		       ((isp_res->v_resz - 1) << ISPRSZ_CNT_VRSZ_SHIFT),
		       OMAP3_ISP_IOMEM_RESZ,
		       ISPRSZ_CNT);
	if (isp_res->h_resz <= MID_RESIZE_VALUE) {
		j = 0;
		for (i = 0; i < 16; i++) {
			isp_reg_writel(isp_res->dev,
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
				isp_reg_writel(isp_res->dev,
					       (isp_res->coeflist.
						h_filter_coef_7tap[j] <<
						ISPRSZ_HFILT10_COEF0_SHIFT),
					       OMAP3_ISP_IOMEM_RESZ,
					       ISPRSZ_HFILT10 + (i * 0x04));
				j += 1;
			} else {
				isp_reg_writel(isp_res->dev,
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
			isp_reg_writel(isp_res->dev, (isp_res->coeflist.
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
				isp_reg_writel(isp_res->dev,
					       (isp_res->coeflist.
						v_filter_coef_7tap[j] <<
						ISPRSZ_VFILT10_COEF0_SHIFT),
					       OMAP3_ISP_IOMEM_RESZ,
					       ISPRSZ_VFILT10 + (i * 0x04));
				j += 1;
			} else {
				isp_reg_writel(isp_res->dev,
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

	ispresizer_config_outlineoffset(isp_res, output_w*2);
	DPRINTK_ISPRESZ("ispresizer_config_size()-\n");
	return 0;
}
EXPORT_SYMBOL(ispresizer_config_size);

void __ispresizer_enable(struct isp_res_device *isp_res, int enable)
{
	int val;
	DPRINTK_ISPRESZ("+ispresizer_enable()+\n");
	if (enable) {
		val = (isp_reg_readl(isp_res->dev,
				     OMAP3_ISP_IOMEM_RESZ, ISPRSZ_PCR) & 0x2) |
			ISPRSZ_PCR_ENABLE;
	} else {
		val = isp_reg_readl(isp_res->dev,
				    OMAP3_ISP_IOMEM_RESZ, ISPRSZ_PCR) &
			~ISPRSZ_PCR_ENABLE;
	}
	isp_reg_writel(isp_res->dev, val, OMAP3_ISP_IOMEM_RESZ, ISPRSZ_PCR);
	DPRINTK_ISPRESZ("+ispresizer_enable()-\n");
}

/**
 * ispresizer_enable - Enables the resizer module.
 * @enable: 1 - Enable, 0 - Disable
 *
 * Client should configure all the sub modules in resizer before this.
 **/
void ispresizer_enable(struct isp_res_device *isp_res, int enable)
{
	__ispresizer_enable(isp_res, enable);
	isp_res->pm_state = enable;
}
EXPORT_SYMBOL(ispresizer_enable);

/**
 * ispresizer_suspend - Suspend resizer module.
 **/
void ispresizer_suspend(struct isp_res_device *isp_res)
{
	if (isp_res->pm_state)
		__ispresizer_enable(isp_res, 0);
}
EXPORT_SYMBOL(ispresizer_suspend);

/**
 * ispresizer_resume - Resume resizer module.
 **/
void ispresizer_resume(struct isp_res_device *isp_res)
{
	if (isp_res->pm_state)
		__ispresizer_enable(isp_res, 1);
}
EXPORT_SYMBOL(ispresizer_resume);

/**
 * ispresizer_busy - Checks if ISP resizer is busy.
 *
 * Returns busy field from ISPRSZ_PCR register.
 **/
int ispresizer_busy(struct isp_res_device *isp_res)
{
	return isp_reg_readl(isp_res->dev, OMAP3_ISP_IOMEM_RESZ, ISPRSZ_PCR) &
		ISPPRV_PCR_BUSY;
}
EXPORT_SYMBOL(ispresizer_busy);

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
EXPORT_SYMBOL(ispresizer_config_startphase);

/**
 * ispresizer_config_ycpos - Specifies if output should be in YC or CY format.
 * @yc: 0 - YC format, 1 - CY format
 **/
void ispresizer_config_ycpos(struct isp_res_device *isp_res, u8 yc)
{
	DPRINTK_ISPRESZ("ispresizer_config_ycpos()+\n");
	isp_reg_and_or(isp_res->dev, OMAP3_ISP_IOMEM_RESZ, ISPRSZ_CNT,
		       ~ISPRSZ_CNT_YCPOS, (yc ? ISPRSZ_CNT_YCPOS : 0));
	DPRINTK_ISPRESZ("ispresizer_config_ycpos()-\n");
}
EXPORT_SYMBOL(ispresizer_config_ycpos);

/**
 * Sets the chrominance algorithm
 * @cbilin: 0 - chrominance uses same processing as luminance,
 *          1 - bilinear interpolation processing
 **/
void ispresizer_enable_cbilin(struct isp_res_device *isp_res, u8 enable)
{
	DPRINTK_ISPRESZ("ispresizer_enable_cbilin()+\n");
	isp_reg_and_or(isp_res->dev, OMAP3_ISP_IOMEM_RESZ, ISPRSZ_CNT,
		       ~ISPRSZ_CNT_CBILIN, (enable ? ISPRSZ_CNT_CBILIN : 0));
	DPRINTK_ISPRESZ("ispresizer_enable_cbilin()-\n");
}
EXPORT_SYMBOL(ispresizer_enable_cbilin);

/**
 * ispresizer_config_luma_enhance - Configures luminance enhancer parameters.
 * @yenh: Pointer to structure containing desired values for core, slope, gain
 *        and algo parameters.
 **/
void ispresizer_config_luma_enhance(struct isp_res_device *isp_res,
				    struct isprsz_yenh *yenh)
{
	DPRINTK_ISPRESZ("ispresizer_config_luma_enhance()+\n");
	isp_res->algo = yenh->algo;
	isp_reg_writel(isp_res->dev, (yenh->algo << ISPRSZ_YENH_ALGO_SHIFT) |
		       (yenh->gain << ISPRSZ_YENH_GAIN_SHIFT) |
		       (yenh->slope << ISPRSZ_YENH_SLOP_SHIFT) |
		       (yenh->coreoffset << ISPRSZ_YENH_CORE_SHIFT),
		       OMAP3_ISP_IOMEM_RESZ,
		       ISPRSZ_YENH);
	DPRINTK_ISPRESZ("ispresizer_config_luma_enhance()-\n");
}
EXPORT_SYMBOL(ispresizer_config_luma_enhance);

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
EXPORT_SYMBOL(ispresizer_config_filter_coef);

/**
 * ispresizer_config_inlineoffset - Configures the read address line offset.
 * @offset: Line Offset for the input image.
 *
 * Returns 0 if successful, or -EINVAL if offset is not 32 bits aligned.
 **/
int ispresizer_config_inlineoffset(struct isp_res_device *isp_res, u32 offset)
{
	DPRINTK_ISPRESZ("ispresizer_config_inlineoffset()+\n");
	if (offset % 32)
		return -EINVAL;
	isp_reg_writel(isp_res->dev, offset << ISPRSZ_SDR_INOFF_OFFSET_SHIFT,
		       OMAP3_ISP_IOMEM_RESZ, ISPRSZ_SDR_INOFF);
	DPRINTK_ISPRESZ("ispresizer_config_inlineoffset()-\n");
	return 0;
}
EXPORT_SYMBOL(ispresizer_config_inlineoffset);

/**
 * ispresizer_set_inaddr - Sets the memory address of the input frame.
 * @addr: 32bit memory address aligned on 32byte boundary.
 *
 * Returns 0 if successful, or -EINVAL if address is not 32 bits aligned.
 **/
int ispresizer_set_inaddr(struct isp_res_device *isp_res, u32 addr)
{
	struct isp_device *isp =
		container_of(isp_res, struct isp_device, isp_res);

	DPRINTK_ISPRESZ("ispresizer_set_inaddr()+\n");
	if (addr % 32)
		return -EINVAL;
	isp_res->tmp_buf = addr;
	isp_reg_writel(isp_res->dev,
		       isp_res->tmp_buf + ISP_BYTES_PER_PIXEL
		       * (isp_res->croprect.left +
			  isp->module.preview_output_width
			  * isp_res->croprect.top),
		       OMAP3_ISP_IOMEM_RESZ, ISPRSZ_SDR_INADD);
	DPRINTK_ISPRESZ("ispresizer_set_inaddr()-\n");
	return 0;
}
EXPORT_SYMBOL(ispresizer_set_inaddr);

/**
 * ispresizer_config_outlineoffset - Configures the write address line offset.
 * @offset: Line offset for the preview output.
 *
 * Returns 0 if successful, or -EINVAL if address is not 32 bits aligned.
 **/
int ispresizer_config_outlineoffset(struct isp_res_device *isp_res, u32 offset)
{
	DPRINTK_ISPRESZ("ispresizer_config_outlineoffset()+\n");
	if (offset % 32)
		return -EINVAL;
	isp_reg_writel(isp_res->dev, offset << ISPRSZ_SDR_OUTOFF_OFFSET_SHIFT,
		       OMAP3_ISP_IOMEM_RESZ, ISPRSZ_SDR_OUTOFF);
	DPRINTK_ISPRESZ("ispresizer_config_outlineoffset()-\n");
	return 0;
}
EXPORT_SYMBOL(ispresizer_config_outlineoffset);

/**
 * Configures the memory address to which the output frame is written.
 * @addr: 32bit memory address aligned on 32byte boundary.
 **/
int ispresizer_set_outaddr(struct isp_res_device *isp_res, u32 addr)
{
	DPRINTK_ISPRESZ("ispresizer_set_outaddr()+\n");
	if (addr % 32)
		return -EINVAL;
	isp_reg_writel(isp_res->dev, addr << ISPRSZ_SDR_OUTADD_ADDR_SHIFT,
		       OMAP3_ISP_IOMEM_RESZ, ISPRSZ_SDR_OUTADD);
	DPRINTK_ISPRESZ("ispresizer_set_outaddr()-\n");
	return 0;
}
EXPORT_SYMBOL(ispresizer_set_outaddr);

/**
 * ispresizer_save_context - Saves the values of the resizer module registers.
 **/
void ispresizer_save_context(struct device *dev)
{
	DPRINTK_ISPRESZ("Saving context\n");
	isp_save_context(dev, isprsz_reg_list);
}
EXPORT_SYMBOL(ispresizer_save_context);

/**
 * ispresizer_restore_context - Restores resizer module register values.
 **/
void ispresizer_restore_context(struct device *dev)
{
	DPRINTK_ISPRESZ("Restoring context\n");
	isp_restore_context(dev, isprsz_reg_list);
}
EXPORT_SYMBOL(ispresizer_restore_context);

/**
 * ispresizer_print_status - Prints the values of the resizer module registers.
 **/
void ispresizer_print_status(struct isp_res_device *isp_res)
{
	if (!is_ispresz_debug_enabled())
		return;
	DPRINTK_ISPRESZ("###ISP_CTRL inresizer =0x%x\n",
			isp_reg_readl(isp_res->dev,
				      OMAP3_ISP_IOMEM_MAIN, ISP_CTRL));
	DPRINTK_ISPRESZ("###ISP_IRQ0ENABLE in resizer =0x%x\n",
			isp_reg_readl(isp_res->dev,
				      OMAP3_ISP_IOMEM_MAIN, ISP_IRQ0ENABLE));
	DPRINTK_ISPRESZ("###ISP_IRQ0STATUS in resizer =0x%x\n",
			isp_reg_readl(isp_res->dev,
				      OMAP3_ISP_IOMEM_MAIN, ISP_IRQ0STATUS));
	DPRINTK_ISPRESZ("###RSZ PCR =0x%x\n",
			isp_reg_readl(isp_res->dev,
				      OMAP3_ISP_IOMEM_RESZ, ISPRSZ_PCR));
	DPRINTK_ISPRESZ("###RSZ CNT =0x%x\n",
			isp_reg_readl(isp_res->dev,
				      OMAP3_ISP_IOMEM_RESZ, ISPRSZ_CNT));
	DPRINTK_ISPRESZ("###RSZ OUT SIZE =0x%x\n",
			isp_reg_readl(isp_res->dev,
				      OMAP3_ISP_IOMEM_RESZ, ISPRSZ_OUT_SIZE));
	DPRINTK_ISPRESZ("###RSZ IN START =0x%x\n",
			isp_reg_readl(isp_res->dev,
				      OMAP3_ISP_IOMEM_RESZ, ISPRSZ_IN_START));
	DPRINTK_ISPRESZ("###RSZ IN SIZE =0x%x\n",
			isp_reg_readl(isp_res->dev,
				      OMAP3_ISP_IOMEM_RESZ, ISPRSZ_IN_SIZE));
	DPRINTK_ISPRESZ("###RSZ SDR INADD =0x%x\n",
			isp_reg_readl(isp_res->dev,
				      OMAP3_ISP_IOMEM_RESZ, ISPRSZ_SDR_INADD));
	DPRINTK_ISPRESZ("###RSZ SDR INOFF =0x%x\n",
			isp_reg_readl(isp_res->dev,
				      OMAP3_ISP_IOMEM_RESZ, ISPRSZ_SDR_INOFF));
	DPRINTK_ISPRESZ("###RSZ SDR OUTADD =0x%x\n",
			isp_reg_readl(isp_res->dev,
				      OMAP3_ISP_IOMEM_RESZ, ISPRSZ_SDR_OUTADD));
	DPRINTK_ISPRESZ("###RSZ SDR OTOFF =0x%x\n",
			isp_reg_readl(isp_res->dev,
				      OMAP3_ISP_IOMEM_RESZ, ISPRSZ_SDR_OUTOFF));
	DPRINTK_ISPRESZ("###RSZ YENH =0x%x\n",
			isp_reg_readl(isp_res->dev,
				      OMAP3_ISP_IOMEM_RESZ, ISPRSZ_YENH));
}
EXPORT_SYMBOL(ispresizer_print_status);

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
	isp_res->dev = dev;

	return 0;
}

/**
 * isp_resizer_cleanup - Module Cleanup.
 **/
void isp_resizer_cleanup(struct device *dev)
{
}
