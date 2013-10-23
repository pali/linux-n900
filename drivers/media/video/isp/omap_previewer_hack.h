/*
 * drivers/media/video/isp/omap_previewer.h
 *
 * Include file for Preview module wrapper in TI's OMAP3430 ISP
 *
 * Copyright (C) 2008 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include "isppreview.h"

#ifndef OMAP_ISP_PREVIEW_WRAP_H
#define OMAP_ISP_PREVIEW_WRAP_H

#define PREV_IOC_BASE			'P'
#define PREV_REQBUF			_IOWR(PREV_IOC_BASE, 1,\
						struct v4l2_requestbuffers)
#define PREV_QUERYBUF			_IOWR(PREV_IOC_BASE, 2,\
							struct v4l2_buffer)
#define PREV_SET_PARAM			_IOW(PREV_IOC_BASE, 3,\
							struct prev_params)
#define PREV_GET_PARAM			_IOWR(PREV_IOC_BASE, 4,\
							struct prev_params)
#define PREV_PREVIEW			_IOR(PREV_IOC_BASE, 5, int)
#define PREV_GET_STATUS			_IOR(PREV_IOC_BASE, 6, char)
#define PREV_GET_CROPSIZE		_IOR(PREV_IOC_BASE, 7,\
							struct prev_cropsize)
#define PREV_QUEUEBUF			_IOWR(PREV_IOC_BASE, 8,\
							struct v4l2_buffer)
#define PREV_IOC_MAXNR    8

#define LUMA_TABLE_SIZE			128
#define GAMMA_TABLE_SIZE		1024
#define CFA_COEFF_TABLE_SIZE		576
#define NOISE_FILTER_TABLE_SIZE		256

#define MAX_IMAGE_WIDTH			3300

#define PREV_INWIDTH_8BIT		0	/* pixel width of 8 bitS */
#define PREV_INWIDTH_10BIT		1	/* pixel width of 10 bits */

#define PREV_32BYTES_ALIGN_MASK		0xFFFFFFE0
#define PREV_16PIX_ALIGN_MASK		0xFFFFFFF0

/* list of structures */

/* structure for RGB2RGB blending parameters */
struct prev_rgbblending {
	short blending[RGB_MAX][RGB_MAX];	/* color correlation 3x3
						 * matrix.
						 */
	short offset[RGB_MAX];			/* color correlation offsets */
};

/* structure for CFA coefficients */
struct prev_cfa_coeffs {
	char hthreshold, vthreshold;		/* horizontal an vertical
						 * threshold.
						 */
	int coeffs[CFA_COEFF_TABLE_SIZE];	/* cfa coefficients */
};
/* structure for Gamma Coefficients */
struct prev_gamma_coeffs {
	unsigned char red[GAMMA_TABLE_SIZE];	/* table of gamma correction
						 * values for red color.
						 */
	unsigned char green[GAMMA_TABLE_SIZE];	/* table of gamma correction
						 * values for green color.
						 */
	unsigned char blue[GAMMA_TABLE_SIZE];	/* table of gamma correction
						 * values for blue color.
						 */
};
/* Structure for Noise Filter Coefficients */
struct prev_noiseflt_coeffs {
	unsigned char noise[NOISE_FILTER_TABLE_SIZE];	/* noise filter
							 * table.
							 */
	unsigned char strength;				/* to find out
							 * weighted average.
							 */
};

/* Structure for Chroma Suppression */
struct prev_chroma_spr {
	unsigned char hpfy;			/* whether to use high passed
						 * version of Y or normal Y
						 */
	char threshold;				/* threshold for chroma
						 * suppress.
						 */
	unsigned char gain;			/* chroma suppression gain */
};

/* structure to know status of the hardware */
struct prev_status {
	char hw_busy;
};
/* structure to knwo crop size */
struct prev_cropsize {
	int hcrop;
	int vcrop;
};


/* device structure keeps track of global information */
struct prev_device {
	struct prev_params *params;
	unsigned char opened;			/* state of the device */
	struct completion wfc;
	struct mutex prevwrap_mutex;
	/* spinlock for in/out videbuf queue */
	spinlock_t inout_vbq_lock;
	/* spinlock for lsc videobuf queues */
	spinlock_t lsc_vbq_lock;
	struct videobuf_queue_ops vbq_ops;	/* videobuf queue operations */
	dma_addr_t isp_addr_read;		/* Input/Output address */
	dma_addr_t isp_addr_lsc;  /* lsc address */
	struct device *isp;
};

/* per-filehandle data structure */
struct prev_fh {
	/* in/out videobuf queue */
	enum v4l2_buf_type inout_type;
	struct videobuf_queue inout_vbq;
	/* lsc videobuf queue */
	enum v4l2_buf_type lsc_type;
	struct videobuf_queue lsc_vbq;
	/* device structure */
	struct prev_device *device;
};

#endif

