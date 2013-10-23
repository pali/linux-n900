/*
 * isppreview.h
 *
 * Driver header file for Preview module in TI's OMAP3 Camera ISP
 *
 * Copyright (C) 2009 Texas Instruments, Inc.
 *
 * Contributors:
 *	Senthilvadivu Guruswamy <svadivu@ti.com>
 *	Pallavi Kulkarni <p-kulkarni@ti.com>
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

#ifndef OMAP_ISP_PREVIEW_H
#define OMAP_ISP_PREVIEW_H

#include <mach/isp_user.h>
/* Isp query control structure */

#define ISPPRV_BRIGHT_STEP		0x1
#define ISPPRV_BRIGHT_DEF		0x0
#define ISPPRV_BRIGHT_LOW		0x0
#define ISPPRV_BRIGHT_HIGH		0xFF
#define ISPPRV_BRIGHT_UNITS		0x1

#define ISPPRV_CONTRAST_STEP		0x1
#define ISPPRV_CONTRAST_DEF		0x10
#define ISPPRV_CONTRAST_LOW		0x0
#define ISPPRV_CONTRAST_HIGH		0xFF
#define ISPPRV_CONTRAST_UNITS		0x1

#define NO_AVE				0x0
#define AVE_2_PIX			0x1
#define AVE_4_PIX			0x2
#define AVE_8_PIX			0x3
#define AVE_ODD_PIXEL_DIST		(1 << 4) /* For Bayer Sensors */
#define AVE_EVEN_PIXEL_DIST		(1 << 2)

#define WB_GAIN_MAX			4

/* Features list */
#define PREV_AVERAGER			(1 << 0)
#define PREV_INVERSE_ALAW 		(1 << 1)
#define PREV_HORZ_MEDIAN_FILTER		(1 << 2)
#define PREV_NOISE_FILTER 		(1 << 3)
#define PREV_CFA			(1 << 4)
#define PREV_GAMMA_BYPASS		(1 << 5)
#define PREV_LUMA_ENHANCE		(1 << 6)
#define PREV_CHROMA_SUPPRESS		(1 << 7)
#define PREV_DARK_FRAME_SUBTRACT	(1 << 8)
#define PREV_LENS_SHADING		(1 << 9)
#define PREV_DARK_FRAME_CAPTURE		(1 << 10)
#define PREV_DEFECT_COR			(1 << 11)


#define ISP_NF_TABLE_SIZE 		(1 << 10)

#define ISP_GAMMA_TABLE_SIZE 		(1 << 10)

/* Table addresses */
#define ISPPRV_TBL_ADDR_RED_G_START  0x00
#define ISPPRV_TBL_ADDR_BLUE_G_START  0x800
#define ISPPRV_TBL_ADDR_GREEN_G_START  0x400

/*
 *Enumeration Constants for input and output format
 */
enum preview_input {
	PRV_RAW_CCDC,
	PRV_RAW_MEM,
	PRV_RGBBAYERCFA,
	PRV_COMPCFA,
	PRV_CCDC_DRKF,
	PRV_OTHERS
};
enum preview_output {
	PREVIEW_RSZ,
	PREVIEW_MEM
};
/*
 * Configure byte layout of YUV image
 */
enum preview_ycpos_mode {
	YCPOS_YCrYCb = 0,
	YCPOS_YCbYCr = 1,
	YCPOS_CbYCrY = 2,
	YCPOS_CrYCbY = 3
};

/**
 * struct ispprev_gtable - Structure for Gamma Correction.
 * @redtable: Pointer to the red gamma table.
 * @greentable: Pointer to the green gamma table.
 * @bluetable: Pointer to the blue gamma table.
 */
struct ispprev_gtable {
	u32 *redtable;
	u32 *greentable;
	u32 *bluetable;
};

/**
 * struct prev_white_balance - Structure for White Balance 2.
 * @wb_dgain: White balance common gain.
 * @wb_gain: Individual color gains.
 * @wb_coefmatrix: Coefficient matrix
 */
struct prev_white_balance {
	u16 wb_dgain; /* white balance common gain */
	u8 wb_gain[WB_GAIN_MAX]; /* individual color gains */
	u8 wb_coefmatrix[WB_GAIN_MAX][WB_GAIN_MAX];
};

/**
 * struct prev_size_params - Structure for size parameters.
 * @hstart: Starting pixel.
 * @vstart: Starting line.
 * @hsize: Width of input image.
 * @vsize: Height of input image.
 * @pixsize: Pixel size of the image in terms of bits.
 * @in_pitch: Line offset of input image.
 * @out_pitch: Line offset of output image.
 */
struct prev_size_params {
	unsigned int hstart;
	unsigned int vstart;
	unsigned int hsize;
	unsigned int vsize;
	unsigned char pixsize;
	unsigned short in_pitch;
	unsigned short out_pitch;
};

/**
 * struct prev_rgb2ycbcr_coeffs - Structure RGB2YCbCr parameters.
 * @coeff: Color conversion gains in 3x3 matrix.
 * @offset: Color conversion offsets.
 */
struct prev_rgb2ycbcr_coeffs {
	short coeff[RGB_MAX][RGB_MAX];
	short offset[RGB_MAX];
};

/**
 * struct prev_darkfrm_params - Structure for Dark frame suppression.
 * @addr: Memory start address.
 * @offset: Line offset.
 */
struct prev_darkfrm_params {
	u32 addr;
	 u32 offset;
 };

/**
 * struct prev_params - Structure for all configuration
 * @features: Set of features enabled.
 * @cfa: CFA coefficients.
 * @csup: Chroma suppression coefficients.
 * @ytable: Pointer to Luma enhancement coefficients.
 * @nf: Noise filter coefficients.
 * @dcor: Noise filter coefficients.
 * @gtable: Gamma coefficients.
 * @wbal: White Balance parameters.
 * @blk_adj: Black adjustment parameters.
 * @rgb2rgb: RGB blending parameters.
 * @rgb2ycbcr: RGB to ycbcr parameters.
 * @hmf_params: Horizontal median filter.
 * @size_params: Size parameters.
 * @drkf_params: Darkframe parameters.
 * @lens_shading_shift:
 * @average: Downsampling rate for averager.
 * @contrast: Contrast.
 * @brightness: Brightness.
 */
struct prev_params {
	u16 features;
	enum preview_ycpos_mode pix_fmt;
	struct ispprev_cfa cfa;
	struct ispprev_csup csup;
	u32 *ytable;
	struct ispprev_nf nf;
	struct ispprev_dcor dcor;
	struct ispprev_gtable gtable;
	struct ispprev_wbal wbal;
	struct ispprev_blkadj blk_adj;
	struct ispprev_rgbtorgb rgb2rgb;
	struct ispprev_csc rgb2ycbcr;
	struct ispprev_hmed hmf_params;
	struct prev_size_params size_params;
	struct prev_darkfrm_params drkf_params;
	u8 lens_shading_shift;
	u8 average;
	u8 contrast;
	u8 brightness;
};

/**
 * struct isptables_update - Structure for Table Configuration.
 * @update: Specifies which tables should be updated.
 * @flag: Specifies which tables should be enabled.
 * @prev_nf: Pointer to structure for Noise Filter
 * @lsc: Pointer to LSC gain table. (currently not used)
 * @red_gamma: Pointer to red gamma correction table.
 * @green_gamma: Pointer to green gamma correction table.
 * @blue_gamma: Pointer to blue gamma correction table.
 * @prev_cfa: Pointer to color filter array configuration.
 * @prev_wbal: Pointer to colour and digital gain configuration.
 */
struct isptables_update {
	u16 update;
	u16 flag;
	struct ispprev_nf *prev_nf;
	u32 *lsc;
	u32 *red_gamma;
	u32 *green_gamma;
	u32 *blue_gamma;
	struct ispprev_cfa *prev_cfa;
	struct ispprev_wbal *prev_wbal;
};

/**
 * struct isp_prev_device - Structure for storing ISP Preview module information
 * @prevout_w: Preview output width.
 * @prevout_h: Preview output height.
 * @previn_w: Preview input width.
 * @previn_h: Preview input height.
 * @prev_inpfmt: Preview input format.
 * @prev_outfmt: Preview output format.
 * @hmed_en: Horizontal median filter enable.
 * @nf_en: Noise filter enable.
 * @dcor_en: Defect correction enable.
 * @cfa_en: Color Filter Array (CFA) interpolation enable.
 * @csup_en: Chrominance suppression enable.
 * @yenh_en: Luma enhancement enable.
 * @fmtavg: Number of horizontal pixels to average in input formatter. The
 *          input width should be a multiple of this number.
 * @brightness: Brightness in preview module.
 * @contrast: Contrast in preview module.
 * @color: Color effect in preview module.
 * @cfafmt: Color Filter Array (CFA) Format.
 * @wbal_update: Update digital and colour gains in Previewer
 *
 * This structure is used to store the OMAP ISP Preview module Information.
 */
struct isp_prev_device {
	u8 update_color_matrix;
	u8 update_rgb_blending;
	u8 update_rgb_to_ycbcr;
	u8 hmed_en;
	u8 nf_en;
	u8 dcor_en;
	u8 cfa_en;
	u8 csup_en;
	u8 yenh_en;
	u8 rg_update;
	u8 gg_update;
	u8 bg_update;
	u8 cfa_update;
	u8 nf_enable;
	u8 nf_update;
	u8 wbal_update;
	u8 fmtavg;
	u8 brightness;
	u8 contrast;
	enum v4l2_colorfx color;
	enum cfa_fmt cfafmt;
	struct ispprev_nf prev_nf_t;
	struct prev_params params;
	int shadow_update;
	u32 sph;
	u32 slv;
	spinlock_t lock;
};

void isppreview_config_shadow_registers(struct isp_prev_device *isp_prev);

int isppreview_request(struct isp_prev_device *isp_prev);

int isppreview_free(struct isp_prev_device *isp_prev);

int isppreview_config_datapath(struct isp_prev_device *isp_prev,
			       struct isp_pipeline *pipe);

void isppreview_config_ycpos(struct isp_prev_device *isp_prev,
			     enum preview_ycpos_mode mode);

void isppreview_config_averager(struct isp_prev_device *isp_prev, u8 average);

void isppreview_enable_invalaw(struct isp_prev_device *isp_prev, u8 enable);

void isppreview_enable_drkframe(struct isp_prev_device *isp_prev, u8 enable);

void isppreview_enable_shadcomp(struct isp_prev_device *isp_prev, u8 enable);

void isppreview_config_drkf_shadcomp(struct isp_prev_device *isp_prev,
				     u8 scomp_shtval);

void isppreview_enable_gammabypass(struct isp_prev_device *isp_prev, u8 enable);

void isppreview_enable_hmed(struct isp_prev_device *isp_prev, u8 enable);

void isppreview_config_hmed(struct isp_prev_device *isp_prev,
			    struct ispprev_hmed);

void isppreview_enable_noisefilter(struct isp_prev_device *isp_prev, u8 enable);

void isppreview_config_noisefilter(struct isp_prev_device *isp_prev,
				   struct ispprev_nf prev_nf);

void isppreview_enable_dcor(struct isp_prev_device *isp_prev, u8 enable);

void isppreview_config_dcor(struct isp_prev_device *isp_prev,
			    struct ispprev_dcor prev_dcor);


void isppreview_config_cfa(struct isp_prev_device *isp_prev,
			   struct ispprev_cfa *cfa);

void isppreview_config_gammacorrn(struct isp_prev_device *isp_prev,
				  struct ispprev_gtable);

void isppreview_config_chroma_suppression(struct isp_prev_device *isp_prev,
					  struct ispprev_csup csup);

void isppreview_enable_cfa(struct isp_prev_device *isp_prev, u8 enable);

void isppreview_config_luma_enhancement(struct isp_prev_device *isp_prev,
					u32 *ytable);

void isppreview_enable_luma_enhancement(struct isp_prev_device *isp_prev,
					u8 enable);

void isppreview_enable_chroma_suppression(struct isp_prev_device *isp_prev,
					  u8 enable);

void isppreview_config_whitebalance(struct isp_prev_device *isp_prev,
				    struct ispprev_wbal);

void isppreview_config_blkadj(struct isp_prev_device *isp_prev,
			      struct ispprev_blkadj);

void isppreview_config_rgb_blending(struct isp_prev_device *isp_prev,
				    struct ispprev_rgbtorgb);

void isppreview_config_rgb_to_ycbcr(struct isp_prev_device *isp_prev,
				    struct ispprev_csc);

void isppreview_update_contrast(struct isp_prev_device *isp_prev, u8 *contrast);

void isppreview_query_contrast(struct isp_prev_device *isp_prev, u8 *contrast);

void isppreview_config_contrast(struct isp_prev_device *isp_prev, u8 contrast);

void isppreview_get_contrast_range(u8 *min_contrast, u8 *max_contrast);

void isppreview_update_brightness(struct isp_prev_device *isp_prev,
				  u8 *brightness);

void isppreview_config_brightness(struct isp_prev_device *isp_prev,
				  u8 brightness);

void isppreview_get_brightness_range(u8 *min_brightness, u8 *max_brightness);

void isppreview_set_color(struct isp_prev_device *isp_prev, u8 *mode);

void isppreview_get_color(struct isp_prev_device *isp_prev, u8 *mode);

void isppreview_query_brightness(struct isp_prev_device *isp_prev,
				 u8 *brightness);

void isppreview_config_yc_range(struct isp_prev_device *isp_prev,
				struct ispprev_yclimit yclimit);

int isppreview_try_pipeline(struct isp_prev_device *isp_prev,
			    struct isp_pipeline *pipe);

int isppreview_s_pipeline(struct isp_prev_device *isp_prev,
			  struct isp_pipeline *pipe);

int isppreview_config_inlineoffset(struct isp_prev_device *isp_prev,
				   u32 offset);

int isppreview_set_inaddr(struct isp_prev_device *isp_prev, u32 addr);

int isppreview_config_outlineoffset(struct isp_prev_device *isp_prev,
				    u32 offset);

int isppreview_set_outaddr(struct isp_prev_device *isp_prev, u32 addr);

int isppreview_config_darklineoffset(struct isp_prev_device *isp_prev,
				     u32 offset);

int isppreview_set_darkaddr(struct isp_prev_device *isp_prev, u32 addr);

void isppreview_enable(struct isp_prev_device *isp_prev, int enable);

int isppreview_busy(struct isp_prev_device *isp_prev);

void isppreview_print_status(struct isp_prev_device *isp_prev,
			     struct isp_pipeline *pipe);

#ifndef CONFIG_ARCH_OMAP3410
void isppreview_save_context(struct device *dev);
#else
static inline void isppreview_save_context(struct device *dev) {}
#endif

#ifndef CONFIG_ARCH_OMAP3410
void isppreview_restore_context(struct device *dev);
#else
static inline void isppreview_restore_context(struct device *dev) {}
#endif

int isppreview_config(struct isp_prev_device *isp_prev, void *userspace_add);

void isppreview_set_skip(struct isp_prev_device *isp_prev, u32 h, u32 v);

#endif/* OMAP_ISP_PREVIEW_H */
