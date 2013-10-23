/*
 * ispresizer.h
 *
 * Driver header file for Resizer module in TI's OMAP3 Camera ISP
 *
 * Copyright (C) 2009 Texas Instruments, Inc.
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

#ifndef OMAP_ISP_RESIZER_H
#define OMAP_ISP_RESIZER_H

/*
 * Resizer Constants
 */
#define MAX_IN_WIDTH_MEMORY_MODE	4095

#define MAX_IN_WIDTH_ONTHEFLY_MODE	1280
#define MAX_IN_WIDTH_ONTHEFLY_MODE_ES2	4095
#define MAX_IN_HEIGHT			4095
#define MINIMUM_RESIZE_VALUE		64
#define MAXIMUM_RESIZE_VALUE		1024
#define MID_RESIZE_VALUE		512

#define MAX_7TAP_HRSZ_OUTWIDTH		1280
#define MAX_7TAP_VRSZ_OUTWIDTH		640

#define MAX_7TAP_HRSZ_OUTWIDTH_ES2	3300
#define MAX_7TAP_VRSZ_OUTWIDTH_ES2	1650

#define DEFAULTSTPIXEL			0
#define DEFAULTSTPHASE			1
#define DEFAULTHSTPIXEL4TAPMODE		3
#define FOURPHASE			4
#define EIGHTPHASE			8
#define RESIZECONSTANT			256
#define SHIFTER4TAPMODE			0
#define SHIFTER7TAPMODE			1
#define DEFAULTOFFSET			7
#define OFFSETVERT4TAPMODE		4
#define OPWDALIGNCONSTANT		0xfffffff0

/*
 * The client is supposed to call resizer API in the following sequence:
 * 	- request()
 * 	- config_datatpath()
 * 	- optionally config/enable sub modules
 * 	- try/config size
 * 	- setup callback
 * 	- setup in/out memory offsets and ptrs
 * 	- enable()
 * 	...
 * 	- disable()
 * 	- free()
 */

enum resizer_input {
	RSZ_OTFLY_YUV,
	RSZ_MEM_YUV,
	RSZ_MEM_COL8
};

/**
 * struct isprsz_coef - Structure for resizer filter coeffcients.
 * @h_filter_coef_4tap: Horizontal filter coefficients for 8-phase/4-tap
 *			mode (.5x-4x)
 * @v_filter_coef_4tap: Vertical filter coefficients for 8-phase/4-tap
 *			mode (.5x-4x)
 * @h_filter_coef_7tap: Horizontal filter coefficients for 4-phase/7-tap
 *			mode (.25x-.5x)
 * @v_filter_coef_7tap: Vertical filter coefficients for 4-phase/7-tap
 *			mode (.25x-.5x)
 */
struct isprsz_coef {
	u16 h_filter_coef_4tap[32];
	u16 v_filter_coef_4tap[32];
	u16 h_filter_coef_7tap[28];
	u16 v_filter_coef_7tap[28];
};

/**
 * struct isprsz_yenh - Structure for resizer luminance enhancer parameters.
 * @algo: Algorithm select.
 * @gain: Maximum gain.
 * @slope: Slope.
 * @coreoffset: Coring offset.
 */
struct isprsz_yenh {
	u8 algo;
	u8 gain;
	u8 slope;
	u8 coreoffset;
};

/**
 * struct isp_res_device - Structure for the resizer module to store its
 * 			   information.
 * @res_inuse: Indicates if resizer module has been reserved. 1 - Reserved,
 *             0 - Freed.
 * @h_startphase: Horizontal starting phase.
 * @v_startphase: Vertical starting phase.
 * @h_resz: Horizontal resizing value.
 * @v_resz: Vertical resizing value.
 * @outputwidth: Output Image Width in pixels.
 * @outputheight: Output Image Height in pixels.
 * @inputwidth: Input Image Width in pixels.
 * @inputheight: Input Image Height in pixels.
 * @algo: Algorithm select. 0 - Disable, 1 - [-1 2 -1]/2 high-pass filter,
 *        2 - [-1 -2 6 -2 -1]/4 high-pass filter.
 * @ipht_crop: Vertical start line for cropping.
 * @ipwd_crop: Horizontal start pixel for cropping.
 * @cropwidth: Crop Width.
 * @cropheight: Crop Height.
 * @resinput: Resizer input.
 * @coeflist: Register configuration for Resizer.
 * @ispres_mutex: Mutex for isp resizer.
 */
struct isp_res_device {
	u8 res_inuse;
	u8 h_startphase;
	u8 v_startphase;
	u16 h_resz;
	u16 v_resz;
	u8 algo;
	dma_addr_t tmp_buf;
	struct isprsz_coef coeflist;
	struct mutex ispres_mutex; /* For checking/modifying res_inuse */
	struct isprsz_yenh defaultyenh;
	int applycrop;
};

int ispresizer_config_crop(struct isp_res_device *isp_res,
			   struct v4l2_crop *a);
void ispresizer_config_shadow_registers(struct isp_res_device *isp_res);

int ispresizer_request(struct isp_res_device *isp_res);

int ispresizer_free(struct isp_res_device *isp_res);

void ispresizer_enable_cbilin(struct isp_res_device *isp_res, u8 enable);

void ispresizer_config_ycpos(struct isp_res_device *isp_res, u8 yc);

void ispresizer_config_startphase(struct isp_res_device *isp_res,
				  u8 hstartphase, u8 vstartphase);

void ispresizer_config_filter_coef(struct isp_res_device *isp_res,
				   struct isprsz_coef *coef);

void ispresizer_config_luma_enhance(struct isp_res_device *isp_res,
				    struct isprsz_yenh *yenh);

int ispresizer_try_pipeline(struct isp_res_device *isp_res,
			    struct isp_pipeline *pipe);

int ispresizer_s_pipeline(struct isp_res_device *isp_res,
			  struct isp_pipeline *pipe);

int ispresizer_config_inlineoffset(struct isp_res_device *isp_res, u32 offset);

int ispresizer_set_inaddr(struct isp_res_device *isp_res, u32 addr);

int ispresizer_config_outlineoffset(struct isp_res_device *isp_res, u32 offset);

int ispresizer_set_outaddr(struct isp_res_device *isp_res, u32 addr);

void ispresizer_enable(struct isp_res_device *isp_res, int enable);

int ispresizer_busy(struct isp_res_device *isp_res);

void ispresizer_save_context(struct device *dev);

void ispresizer_restore_context(struct device *dev);

void ispresizer_print_status(struct isp_res_device *isp_res);

#endif		/* OMAP_ISP_RESIZER_H */
