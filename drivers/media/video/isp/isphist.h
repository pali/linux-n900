/*
 * isphist.h
 *
 * Header file for HISTOGRAM module in TI's OMAP3 Camera ISP
 *
 * Copyright (C) 2009 Texas Instruments, Inc.
 *
 * Contributors:
 * 	David Cohen <david.cohen@nokia.com>
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

#ifndef OMAP_ISP_HIST_H
#define OMAP_ISP_HIST_H

#include <mach/isp_user.h>
#include <mach/dma.h>

#include "ispstat.h"

#define ISPHIST_PCR_EN		(1 << 0)
#define ISPHIST_CNT_CLR_EN	(1 << 7)

#define WRITE_SOURCE(reg, source)			\
	(reg = (reg & ~(ISPHIST_CNT_SOURCE_MASK))	\
	 | (source << ISPHIST_CNT_SOURCE_SHIFT))

#define WRITE_HV_INFO(reg, hv_info)			\
	(reg = ((reg & ~(ISPHIST_HV_INFO_MASK))		\
		| (hv_info & ISPHIST_HV_INFO_MASK)))

#define WRITE_RADD(reg, radd)			\
	(reg = (reg & ~(ISPHIST_RADD_MASK))	\
	 | (radd << ISPHIST_RADD_SHIFT))

#define WRITE_RADD_OFF(reg, radd_off)			\
	(reg = (reg & ~(ISPHIST_RADD_OFF_MASK))		\
	 | (radd_off << ISPHIST_RADD_OFF_SHIFT))

#define WRITE_BIT_SHIFT(reg, bit_shift)			\
	(reg = (reg & ~(ISPHIST_CNT_SHIFT_MASK))	\
	 | (bit_shift << ISPHIST_CNT_SHIFT_SHIFT))

#define WRITE_DATA_SIZE(reg, data_size)			\
	(reg = (reg & ~(ISPHIST_CNT_DATASIZE_MASK))	\
	 | (data_size << ISPHIST_CNT_DATASIZE_SHIFT))

#define WRITE_NUM_BINS(reg, num_bins)			\
	(reg = (reg & ~(ISPHIST_CNT_BINS_MASK))		\
	 | (num_bins << ISPHIST_CNT_BINS_SHIFT))

#define WRITE_CFA(reg, cfa)				\
	(reg = (reg & ~(ISPHIST_CNT_CFA_MASK))		\
	 | (cfa << ISPHIST_CNT_CFA_SHIFT))

#define WRITE_WG0(reg, reg_wb_gain)				\
	reg = ((reg & ~(ISPHIST_WB_GAIN_WG00_MASK))		\
	       | (reg_wb_gain << ISPHIST_WB_GAIN_WG00_SHIFT))

#define WRITE_WG1(reg, reg_wb_gain)			\
	(reg = (reg & ~(ISPHIST_WB_GAIN_WG01_MASK))	\
	 | (reg_wb_gain << ISPHIST_WB_GAIN_WG01_SHIFT))

#define WRITE_WG2(reg, reg_wb_gain)			\
	(reg = (reg & ~(ISPHIST_WB_GAIN_WG02_MASK))	\
	 | (reg_wb_gain << ISPHIST_WB_GAIN_WG02_SHIFT))

#define WRITE_WG3(reg, reg_wb_gain)			\
	(reg = (reg & ~(ISPHIST_WB_GAIN_WG03_MASK))	\
	 | (reg_wb_gain << ISPHIST_WB_GAIN_WG03_SHIFT))

#define WRITE_REG_HORIZ(reg, reg_n_hor)			\
	(reg = ((reg & ~ISPHIST_REGHORIZ_MASK)		\
		| (reg_n_hor & ISPHIST_REGHORIZ_MASK)))

#define WRITE_REG_VERT(reg, reg_n_vert)			\
	(reg = ((reg & ~ISPHIST_REGVERT_MASK)		\
		| (reg_n_vert & ISPHIST_REGVERT_MASK)))

/**
 * struct isp_hist_regs - Current value of Histogram configuration registers.
 * @pcr: Peripheral control register.
 * @cnt: Histogram control register.
 * @wb_gain: Histogram white balance gain register.
 * @reg_hor[]: Region N horizontal register.
 * @reg_ver[]: Region N vertical register.
 * @hist_radd: Address register. When input data comes from mem.
 * @hist_radd_off: Address offset register. When input data comes from mem.
 * @h_v_info: Image size register. When input data comes from mem.
 */
struct isp_hist_regs {
	u32 pcr;
	u32 cnt;
	u32 wb_gain;
	u32 reg_hor[HIST_MAX_REGIONS];
	u32 reg_ver[HIST_MAX_REGIONS];
	u32 hist_radd;
	u32 hist_radd_off;
	u32 h_v_info;
};

/**
 * struct isp_hist_status - Histogram status.
 * @hist_enable: Enables the histogram module.
 * @initialized: Flag to indicate that the module is correctly initialized.
 * @frame_cnt: Actual frame count.
 * @num_acc_frames: Num accumulated image frames per hist frame
 * @completed: Flag to indicate if a frame request is completed.
 */
struct isp_hist_device {
	u8 enabled;
	u8 update;
	u8 num_acc_frames;
	u8 waiting_dma;
	u8 invalid_buf;
	u8 use_dma;
	int dma_ch;
	struct timeval ts;

	struct omap_dma_channel_params dma_config;
	struct isp_hist_regs regs;
	struct isp_hist_config config;
	struct ispstat_buffer *active_buf;
	unsigned int buf_size;
	struct ispstat stat;

	spinlock_t lock;	/* serialize access to hist device's fields */
};

#define HIST_BUF_DONE		0
#define HIST_NO_BUF		1
#define HIST_BUF_WAITING_DMA	2

int isp_hist_busy(struct isp_hist_device *isp_hist);
void isp_hist_enable(struct isp_hist_device *isp_hist, u8 enable);
void isp_hist_try_enable(struct isp_hist_device *isp_hist);
int isp_hist_busy(struct isp_hist_device *isp_hist);
int isp_hist_buf_process(struct isp_hist_device *isp_hist);
void isp_hist_mark_invalid_buf(struct isp_hist_device *isp_hist);
void isp_hist_config_registers(struct isp_hist_device *isp_hist);
void isp_hist_suspend(struct isp_hist_device *isp_hist);
void isp_hist_resume(struct isp_hist_device *isp_hist);
void isp_hist_save_context(struct device *dev);
void isp_hist_restore_context(struct device *dev);
int isp_hist_config(struct isp_hist_device *isp_hist,
		    struct isp_hist_config *histcfg);
int isp_hist_request_statistics(struct isp_hist_device *isp_hist,
				struct isp_hist_data *histdata);

#endif				/* OMAP_ISP_HIST */
