/*
 * isphist.h
 *
 * Header file for HISTOGRAM module in TI's OMAP3 Camera ISP
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

#ifndef OMAP_ISP_HIST_H
#define OMAP_ISP_HIST_H

#include <mach/isp_user.h>

#define MAX_REGIONS		0x4
#define MAX_WB_GAIN		255
#define MIN_WB_GAIN		0x0
#define MAX_BIT_WIDTH		14
#define MIN_BIT_WIDTH		8

#define ISPHIST_PCR_EN		(1 << 0)
#define HIST_MEM_SIZE		1024
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

#define WRITE_WB_R(reg, reg_wb_gain)				\
	reg = ((reg & ~(ISPHIST_WB_GAIN_WG00_MASK))		\
	       | (reg_wb_gain << ISPHIST_WB_GAIN_WG00_SHIFT))

#define WRITE_WB_RG(reg, reg_wb_gain)			\
	(reg = (reg & ~(ISPHIST_WB_GAIN_WG01_MASK))	\
	 | (reg_wb_gain << ISPHIST_WB_GAIN_WG01_SHIFT))

#define WRITE_WB_B(reg, reg_wb_gain)			\
	(reg = (reg & ~(ISPHIST_WB_GAIN_WG02_MASK))	\
	 | (reg_wb_gain << ISPHIST_WB_GAIN_WG02_SHIFT))

#define WRITE_WB_BG(reg, reg_wb_gain)			\
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
 * @r0_h: Region 0 horizontal register.
 * @r0_v: Region 0 vertical register.
 * @r1_h: Region 1 horizontal register.
 * @r1_v: Region 1 vertical register.
 * @r2_h: Region 2 horizontal register.
 * @r2_v: Region 2 vertical register.
 * @r3_h: Region 3 horizontal register.
 * @r3_v: Region 3 vertical register.
 * @hist_addr: Histogram address register.
 * @hist_data: Histogram data.
 * @hist_radd: Address register. When input data comes from mem.
 * @hist_radd_off: Address offset register. When input data comes from mem.
 * @h_v_info: Image size register. When input data comes from mem.
 */
struct isp_hist_regs {
	u32 pcr;
	u32 cnt;
	u32 wb_gain;
	u32 r0_h;
	u32 r0_v;
	u32 r1_h;
	u32 r1_v;
	u32 r2_h;
	u32 r2_v;
	u32 r3_h;
	u32 r3_v;
	u32 hist_addr;
	u32 hist_data;
	u32 hist_radd;
	u32 hist_radd_off;
	u32 h_v_info;
};

/**
 * struct isp_hist_status - Histogram status.
 * @hist_enable: Enables the histogram module.
 * @initialized: Flag to indicate that the module is correctly initializated.
 * @frame_cnt: Actual frame count.
 * @frame_req: Frame requested by user.
 * @completed: Flag to indicate if a frame request is completed.
 */
struct isp_hist_device {
	u8 hist_enable;
	u8 pm_state;
	u8 initialized;
	u8 frame_cnt;
	u8 frame_req;
	u8 completed;
	struct isp_hist_regs regs;
	struct device *dev;
};

void isp_hist_enable(struct isp_hist_device *isp_hist, u8 enable);

int isp_hist_busy(struct isp_hist_device *isp_hist);

int isp_hist_configure(struct isp_hist_device *isp_hist,
		       struct isp_hist_config *histcfg);

int isp_hist_request_statistics(struct isp_hist_device *isp_hist,
				struct isp_hist_data *histdata);

void isphist_save_context(struct device *dev);

void isp_hist_suspend(struct isp_hist_device *isp_hist);

void isp_hist_resume(struct isp_hist_device *isp_hist);

void isphist_restore_context(struct device *dev);

#endif				/* OMAP_ISP_HIST */
