/*
 * isp.h
 *
 * Top level public header file for ISP Control module in
 * TI's OMAP3 Camera ISP
 *
 * Copyright (C) 2009 Texas Instruments.
 * Copyright (C) 2009 Nokia.
 *
 * Contributors:
 * 	Sameer Venkatraman <sameerv@ti.com>
 * 	Mohit Jalori <mjalori@ti.com>
 * 	Sergio Aguirre <saaguirre@ti.com>
 * 	Sakari Ailus <sakari.ailus@nokia.com>
 * 	Tuukka Toivonen <tuukka.o.toivonen@nokia.com>
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef OMAP_ISP_TOP_H
#define OMAP_ISP_TOP_H
#include <mach/cpu.h>
#include <media/videobuf-dma-sg.h>
#include <linux/videodev2.h>

#include <asm/io.h>

#include <mach/iommu.h>
#include <mach/iovmm.h>

struct isp_pipeline;

#include "ispstat.h"
#include "isp_af.h"
#include "isphist.h"
#include "ispccdc.h"
#include "ispreg.h"
#include "isph3a.h"
#include "ispresizer.h"
#include "isppreview.h"

#define IOMMU_FLAG (IOVMF_ENDIAN_LITTLE | IOVMF_ELSZ_8)

#define OMAP_ISP_CCDC		(1 << 0)
#define OMAP_ISP_PREVIEW	(1 << 1)
#define OMAP_ISP_RESIZER	(1 << 2)
#define OMAP_ISP_AEWB		(1 << 3)
#define OMAP_ISP_AF		(1 << 4)
#define OMAP_ISP_HIST		(1 << 5)

#define ISP_TOK_TERM		0xFFFFFFFF	/*
						 * terminating token for ISP
						 * modules reg list
						 */
#define NUM_BUFS		VIDEO_MAX_FRAME

#ifndef CONFIG_ARCH_OMAP3410
#define USE_ISP_PREVIEW
#define USE_ISP_RESZ
#define is_isppreview_enabled()		1
#define is_ispresizer_enabled()		1
#else
#define is_isppreview_enabled()		0
#define is_ispresizer_enabled()		0
#endif

#define ISP_BYTES_PER_PIXEL		2
#define NUM_ISP_CAPTURE_FORMATS 	(sizeof(isp_formats) /		\
					 sizeof(isp_formats[0]))

#define to_isp_device(ptr_module)				\
	container_of(ptr_module, struct isp_device, ptr_module)
#define to_device(ptr_module)						\
	(to_isp_device(ptr_module)->dev)

typedef int (*isp_vbq_callback_ptr) (struct videobuf_buffer *vb);
typedef void (*isp_callback_t) (unsigned long status,
				isp_vbq_callback_ptr arg1, void *arg2);

enum isp_mem_resources {
	OMAP3_ISP_IOMEM_MAIN,
	OMAP3_ISP_IOMEM_CBUFF,
	OMAP3_ISP_IOMEM_CCP2,
	OMAP3_ISP_IOMEM_CCDC,
	OMAP3_ISP_IOMEM_HIST,
	OMAP3_ISP_IOMEM_H3A,
	OMAP3_ISP_IOMEM_PREV,
	OMAP3_ISP_IOMEM_RESZ,
	OMAP3_ISP_IOMEM_SBL,
	OMAP3_ISP_IOMEM_CSI2A,
	OMAP3_ISP_IOMEM_CSI2PHY
};

enum isp_interface_type {
	ISP_PARLL = 1,
	ISP_CSIA = 2,
	ISP_CSIB = 4,
	ISP_NONE = 8 /* memory input to preview / resizer */
};

enum isp_irqevents {
	CSIA = 0x01,
	CSIB = 0x10,
	CCDC_VD0 = 0x100,
	CCDC_VD1 = 0x200,
	CCDC_VD2 = 0x400,
	CCDC_ERR = 0x800,
	H3A_AWB_DONE = 0x2000,
	H3A_AF_DONE = 0x1000,
	HIST_DONE = 0x10000,
	PREV_DONE = 0x100000,
	LSC_DONE = 0x20000,
	LSC_PRE_COMP = 0x40000,
	LSC_PRE_ERR = 0x80000,
	RESZ_DONE = 0x1000000,
	SBL_OVF = 0x2000000,
	MMU_ERR = 0x10000000,
	OCP_ERR = 0x20000000,
	HS_VS = 0x80000000
};

enum isp_callback_type {
	CBK_CCDC_VD0,
	CBK_CCDC_VD1,
	CBK_PREV_DONE,
	CBK_RESZ_DONE,
	CBK_MMU_ERR,
	CBK_HIST_DONE,
	CBK_HS_VS,
	CBK_LSC_ISR,
	CBK_CATCHALL,
	CBK_CSIA,
	CBK_CSIB,
	CBK_END,
};

enum isp_running {
	ISP_STOPPED,
	ISP_RUNNING,
	ISP_STOPPING,
};

/**
 * struct isp_reg - Structure for ISP register values.
 * @reg: 32-bit Register address.
 * @val: 32-bit Register value.
 */
struct isp_reg {
	enum isp_mem_resources mmio_range;
	u32 reg;
	u32 val;
};

/**
 * struct isp_interface_config - ISP interface configuration.
 * @ccdc_par_ser: ISP interface type. 0 - Parallel, 1 - CSIA, 2 - CSIB to CCDC.
 * @par_bridge: CCDC Bridge input control. Parallel interface.
 *                  0 - Disable, 1 - Enable, first byte->cam_d(bits 7 to 0)
 *                  2 - Enable, first byte -> cam_d(bits 15 to 8)
 * @par_clk_pol: Pixel clock polarity on the parallel interface.
 *                    0 - Non Inverted, 1 - Inverted
 * @dataline_shift: Data lane shifter.
 *                      0 - No Shift, 1 - CAMEXT[13 to 2]->CAM[11 to 0]
 *                      2 - CAMEXT[13 to 4]->CAM[9 to 0]
 *                      3 - CAMEXT[13 to 6]->CAM[7 to 0]
 * @hsvs_syncdetect: HS or VS synchronization signal detection.
 *                       0 - HS Falling, 1 - HS rising
 *                       2 - VS falling, 3 - VS rising
 * @strobe: Strobe related parameter.
 * @prestrobe: PreStrobe related parameter.
 * @shutter: Shutter related parameter.
 * @hskip: Horizontal Start Pixel performed in Preview module.
 * @vskip: Vertical Start Line performed in Preview module.
 * @wenlog: Store the value for the sensor specific wenlog field.
 * @wait_hs_vs: Wait for this many hs_vs before anything else in the beginning.
 * @pixelclk: Pixel data rate from sensor.
 */
struct isp_interface_config {
	enum isp_interface_type ccdc_par_ser;
	u8 dataline_shift;
	u32 hsvs_syncdetect;
	int strobe;
	int prestrobe;
	int shutter;
	u32 prev_sph;
	u32 prev_slv;
	u32 wenlog;
	int wait_hs_vs;
	unsigned int pixelclk;
	union {
		struct par {
			unsigned par_bridge:2;
			unsigned par_clk_pol:1;
		} par;
		struct csi {
			unsigned crc:1;
			unsigned mode:1;
			unsigned edge:1;
			unsigned signalling:1;
			unsigned strobe_clock_inv:1;
			unsigned vs_edge:1;
			unsigned channel:3;
			unsigned vpclk:2;	/* Video port output clock */
			unsigned int data_start;
			unsigned int data_size;
			u32 format;		/* V4L2_PIX_FMT_* */
		} csi;
	} u;
};

struct isp_buf {
	dma_addr_t isp_addr;
	void (*complete)(struct videobuf_buffer *vb, void *priv);
	struct videobuf_buffer *vb;
	void *priv;
	u32 vb_state;
};

#define ISP_BUFS_IS_FULL(bufs)					\
	(((bufs)->queue + 1) % NUM_BUFS == (bufs)->done)
#define ISP_BUFS_IS_EMPTY(bufs)		((bufs)->queue == (bufs)->done)
#define ISP_BUFS_IS_LAST(bufs)					\
	((bufs)->queue == ((bufs)->done + 1) % NUM_BUFS)
#define ISP_BUFS_QUEUED(bufs)						\
	((((bufs)->done - (bufs)->queue + NUM_BUFS)) % NUM_BUFS)
#define ISP_BUF_DONE(bufs)		((bufs)->buf + (bufs)->done)
#define ISP_BUF_NEXT_DONE(bufs)				\
	((bufs)->buf + ((bufs)->done + 1) % NUM_BUFS)
#define ISP_BUF_QUEUE(bufs)		((bufs)->buf + (bufs)->queue)
#define ISP_BUF_MARK_DONE(bufs)				\
	(bufs)->done = ((bufs)->done + 1) % NUM_BUFS;
#define ISP_BUF_MARK_QUEUED(bufs)			\
	(bufs)->queue = ((bufs)->queue + 1) % NUM_BUFS;

struct isp_bufs {
	dma_addr_t isp_addr_capture[VIDEO_MAX_FRAME];
	/* queue full: (ispsg.queue + 1) % NUM_BUFS == ispsg.done
	   queue empty: ispsg.queue == ispsg.done */
	struct isp_buf buf[NUM_BUFS];
	/* Next slot to queue a buffer. */
	int queue;
	/* Buffer that is being processed. */
	int done;
	/* Wait for this many hs_vs before anything else. */
	int wait_hs_vs;
	/* Ignore statistic's interrupts until first good hs_vs. */
	int wait_stats;
};

/**
 * struct ispirq - Structure for containing callbacks to be called in ISP ISR.
 * @isp_callbk: Array which stores callback functions, indexed by the type of
 *              callback (8 possible types).
 * @isp_callbk_arg1: Pointer to array containing pointers to the first argument
 *                   to be passed to the requested callback function.
 * @isp_callbk_arg2: Pointer to array containing pointers to the second
 *                   argument to be passed to the requested callback function.
 *
 * This structure is used to contain all the callback functions related for
 * each callback type (CBK_CCDC_VD0, CBK_CCDC_VD1, CBK_PREV_DONE,
 * CBK_RESZ_DONE, CBK_MMU_ERR, CBK_H3A_AWB_DONE, CBK_HIST_DONE, CBK_HS_VS,
 * CBK_LSC_ISR).
 */
struct isp_irq {
	isp_callback_t isp_callbk[CBK_END];
	isp_vbq_callback_ptr isp_callbk_arg1[CBK_END];
	void *isp_callbk_arg2[CBK_END];
};

struct isp_pipeline {
	unsigned int modules;		/* modules in use */
	struct v4l2_pix_format pix;	/* output pix */
	unsigned int ccdc_in_w;
	unsigned int ccdc_in_h;
	unsigned int ccdc_out_w;	/* ccdc output data width (pixels) */
	unsigned int ccdc_out_h;	/* ccdc output data height */
	unsigned int ccdc_out_w_img;	/* ccdc output visible image width */
	enum ccdc_input ccdc_in;
	enum ccdc_output ccdc_out;
	unsigned int prv_out_w;
	unsigned int prv_out_h;
	unsigned int prv_out_w_img;
	unsigned int prv_out_h_img;
	unsigned int prv_fmt_avg;
	enum preview_input prv_in;
	enum preview_output prv_out;
	struct v4l2_rect rsz_crop;
	unsigned int rsz_out_w;
	unsigned int rsz_out_h;
	unsigned int rsz_out_w_img;
	enum resizer_input rsz_in;
};

#define RAW_CAPTURE(isp)					\
	(!((isp)->pipeline.modules & OMAP_ISP_PREVIEW))

/**
 * struct isp - Structure for storing ISP Control module information
 * @lock: Spinlock to sync between isr and processes.
 * @isp_mutex: Semaphore used to get access to the ISP.
 * @ref_count: Reference counter.
 * @cam_ick: Pointer to ISP Interface clock.
 * @cam_fck: Pointer to ISP Functional clock.
 *
 * This structure is used to store the OMAP ISP Information.
 */
struct isp_device {
	struct device *dev;
	struct isp *isp_obj;

	/*** platform HW resources ***/
	unsigned int irq_num;

#define mmio_base_main mmio_base[OMAP3_ISP_IOMEM_MAIN]
#define mmio_cbuff_main mmio_base[OMAP3_ISP_IOMEM_CBUFF]
#define mmio_ccp2_main mmio_base[OMAP3_ISP_IOMEM_CCP2]
#define mmio_ccdc_main mmio_base[OMAP3_ISP_IOMEM_CCDC]
#define mmio_hist_main mmio_base[OMAP3_ISP_IOMEM_HIST]
#define mmio_h3a_main mmio_base[OMAP3_ISP_IOMEM_H3A]
#define mmio_prev_main mmio_base[OMAP3_ISP_IOMEM_PREV]
#define mmio_resz_main mmio_base[OMAP3_ISP_IOMEM_RESZ]
#define mmio_sbl_main mmio_base[OMAP3_ISP_IOMEM_SBL]
#define mmio_csi2_main mmio_base[OMAP3_ISP_IOMEM_CSI2A]
#define mmio_csi2phy_main mmio_base[OMAP3_ISP_IOMEM_CSI2PHY]
	unsigned long mmio_base[OMAP3_ISP_IOMEM_CSI2PHY + 1];
	unsigned long mmio_base_phys[OMAP3_ISP_IOMEM_CSI2PHY + 1];
	unsigned long mmio_size[OMAP3_ISP_IOMEM_CSI2PHY + 1];

	/* ISP Obj */
	spinlock_t lock;	/* For handling registered ISP callbacks */
	spinlock_t h3a_lock;
	struct mutex isp_mutex;	/* For handling ref_count field */
	int ref_count;
	struct clk *cam_ick;
	struct clk *cam_mclk;
	struct clk *dpll4_m5_ck;
	struct clk *csi2_fck;
	struct clk *l3_ick;
	struct isp_interface_config *config;
	dma_addr_t tmp_buf;
	size_t tmp_buf_size;
	unsigned long tmp_buf_offset;
	struct isp_bufs bufs;
	struct isp_irq irq;
	struct isp_pipeline pipeline;
	u32 interrupts;
	enum isp_running running;

	/* ISP modules */
	struct isp_af_device isp_af;
	struct isp_hist_device isp_hist;
	struct isp_h3a_device isp_h3a;
	struct isp_res_device isp_res;
	struct isp_prev_device isp_prev;
	struct isp_ccdc_device isp_ccdc;

	struct iommu *iommu;
};

void isp_hist_dma_done(struct device *dev);

void isp_flush(struct device *dev);

void isp_start(struct device *dev);

void isp_stop(struct device *dev);

int isp_buf_queue(struct device *dev, struct videobuf_buffer *vb,
		  void (*complete)(struct videobuf_buffer *vb, void *priv),
		  void *priv);

int isp_vbq_setup(struct device *dev, struct videobuf_queue *vbq,
		  unsigned int *cnt, unsigned int *size);

int isp_vbq_prepare(struct device *dev, struct videobuf_queue *vbq,
		    struct videobuf_buffer *vb, enum v4l2_field field);

void isp_vbq_release(struct device *dev, struct videobuf_queue *vbq,
		    struct videobuf_buffer *vb);

int isp_set_callback(struct device *dev, enum isp_callback_type type,
		     isp_callback_t callback, isp_vbq_callback_ptr arg1,
		     void *arg2);

int isp_unset_callback(struct device *dev, enum isp_callback_type type);

u32 isp_set_xclk(struct device *dev, u32 xclk, u8 xclksel);

int isp_configure_interface(struct device *dev,
			    struct isp_interface_config *config);

struct device *isp_get(void);

int isp_put(void);

int isp_queryctrl(struct v4l2_queryctrl *a);

int isp_querymenu(struct v4l2_querymenu *a);

int isp_g_ctrl(struct device *dev, struct v4l2_control *a);

int isp_s_ctrl(struct device *dev, struct v4l2_control *a);

int isp_enum_fmt_cap(struct v4l2_fmtdesc *f);

int isp_try_fmt_cap(struct device *dev, struct v4l2_pix_format *pix_input,
		    struct v4l2_pix_format *pix_output);

void isp_g_fmt_cap(struct device *dev, struct v4l2_pix_format *pix);

int isp_s_fmt_cap(struct device *dev, struct v4l2_pix_format *pix_input,
		  struct v4l2_pix_format *pix_output);

int isp_g_crop(struct device *dev, struct v4l2_crop *a);

int isp_s_crop(struct device *dev, struct v4l2_crop *a);

int isp_try_fmt(struct device *dev, struct v4l2_pix_format *pix_input,
		struct v4l2_pix_format *pix_output);

int isp_handle_private(struct device *dev, int cmd, void *arg);

void isp_save_context(struct device *dev, struct isp_reg *);

void isp_restore_context(struct device *dev, struct isp_reg *);

void isp_print_status(struct device *dev);

int __init isp_ccdc_init(struct device *dev);
int __init isp_hist_init(struct device *dev);
int __init isph3a_aewb_init(struct device *dev);
int __init isp_preview_init(struct device *dev);
int __init isp_resizer_init(struct device *dev);
int __init isp_af_init(struct device *dev);
int __init isp_csi2_init(struct device *dev);

void isp_ccdc_cleanup(struct device *dev);
void isp_hist_cleanup(struct device *dev);
void isph3a_aewb_cleanup(struct device *dev);
void isp_resizer_cleanup(struct device *dev);
void isp_af_exit(struct device *dev);
void isp_csi2_cleanup(struct device *dev);

/* FIXME: Remove these when iommu supports these directly. */
dma_addr_t ispmmu_vmap(struct device *dev, const struct scatterlist *sglist,
		       int sglen);
void ispmmu_vunmap(struct device *dev, dma_addr_t da);

static inline
u32 isp_reg_readl(struct device *dev, enum isp_mem_resources isp_mmio_range,
		  u32 reg_offset)
{
	struct isp_device *isp = dev_get_drvdata(dev);

	return __raw_readl(isp->mmio_base[isp_mmio_range] + reg_offset);
}

static inline
void isp_reg_writel(struct device *dev, u32 reg_value,
		    enum isp_mem_resources isp_mmio_range, u32 reg_offset)
{
	struct isp_device *isp = dev_get_drvdata(dev);

	__raw_writel(reg_value, isp->mmio_base[isp_mmio_range] + reg_offset);
}

static inline
void isp_reg_and(struct device *dev, enum isp_mem_resources mmio_range, u32 reg,
		 u32 and_bits)
{
	u32 v = isp_reg_readl(dev, mmio_range, reg);

	isp_reg_writel(dev, v & and_bits, mmio_range, reg);
}

static inline
void isp_reg_or(struct device *dev, enum isp_mem_resources mmio_range, u32 reg,
		u32 or_bits)
{
	u32 v = isp_reg_readl(dev, mmio_range, reg);

	isp_reg_writel(dev, v | or_bits, mmio_range, reg);
}

static inline
void isp_reg_and_or(struct device *dev, enum isp_mem_resources mmio_range,
		    u32 reg, u32 and_bits, u32 or_bits)
{
	u32 v = isp_reg_readl(dev, mmio_range, reg);

	isp_reg_writel(dev, (v & and_bits) | or_bits, mmio_range, reg);
}

#endif	/* OMAP_ISP_TOP_H */
