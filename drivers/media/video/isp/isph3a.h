/*
 * isph3a.h
 *
 * Include file for H3A module in TI's OMAP3 Camera ISP
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

#ifndef OMAP_ISP_H3A_H
#define OMAP_ISP_H3A_H

#include <mach/isp_user.h>

#define AEWB_PACKET_SIZE	16
#define H3A_MAX_BUFF		5
#define AEWB_SATURATION_LIMIT	0x3FF

/* Flags for changed registers */
#define PCR_CHNG		(1 << 0)
#define AEWWIN1_CHNG		(1 << 1)
#define AEWINSTART_CHNG		(1 << 2)
#define AEWINBLK_CHNG		(1 << 3)
#define AEWSUBWIN_CHNG		(1 << 4)
#define PRV_WBDGAIN_CHNG	(1 << 5)
#define PRV_WBGAIN_CHNG		(1 << 6)

/* ISPH3A REGISTERS bits */
#define ISPH3A_PCR_AF_EN	(1 << 0)
#define ISPH3A_PCR_AF_ALAW_EN	(1 << 1)
#define ISPH3A_PCR_AF_MED_EN	(1 << 2)
#define ISPH3A_PCR_AF_BUSY	(1 << 15)
#define ISPH3A_PCR_AEW_EN	(1 << 16)
#define ISPH3A_PCR_AEW_ALAW_EN	(1 << 17)
#define ISPH3A_PCR_AEW_BUSY	(1 << 18)
#define ISPH3A_PCR_AEW_MASK 	(ISPH3A_PCR_AEW_ALAW_EN | \
				 ISPH3A_PCR_AEW_AVE2LMT_MASK)

#define WRITE_SAT_LIM(reg, sat_limit)			\
	(reg = (reg & (~(ISPH3A_PCR_AEW_AVE2LMT_MASK))) \
	 | (sat_limit << ISPH3A_PCR_AEW_AVE2LMT_SHIFT))

#define WRITE_ALAW(reg, alaw_en)			\
	(reg = (reg & (~(ISPH3A_PCR_AEW_ALAW_EN)))	\
	 | ((alaw_en & ISPH3A_PCR_AF_ALAW_EN)		\
	    << ISPH3A_PCR_AEW_ALAW_EN_SHIFT))

#define WRITE_WIN_H(reg, height)				\
	(reg = (reg & (~(ISPH3A_AEWWIN1_WINH_MASK)))		\
	 | (((height >> 1) - 1) << ISPH3A_AEWWIN1_WINH_SHIFT))

#define WRITE_WIN_W(reg, width)					\
	(reg = (reg & (~(ISPH3A_AEWWIN1_WINW_MASK)))		\
	 | (((width >> 1) - 1) << ISPH3A_AEWWIN1_WINW_SHIFT))

#define WRITE_VER_C(reg, ver_count)				\
	(reg = (reg & ~(ISPH3A_AEWWIN1_WINVC_MASK))		\
	 | ((ver_count - 1) << ISPH3A_AEWWIN1_WINVC_SHIFT))

#define WRITE_HOR_C(reg, hor_count)				\
	(reg = (reg & ~(ISPH3A_AEWWIN1_WINHC_MASK))		\
	 | ((hor_count - 1) << ISPH3A_AEWWIN1_WINHC_SHIFT))

#define WRITE_VER_WIN_ST(reg, ver_win_st)			\
	(reg = (reg & ~(ISPH3A_AEWINSTART_WINSV_MASK))		\
	 | (ver_win_st << ISPH3A_AEWINSTART_WINSV_SHIFT))

#define WRITE_HOR_WIN_ST(reg, hor_win_st)			\
	(reg = (reg & ~(ISPH3A_AEWINSTART_WINSH_MASK))		\
	 | (hor_win_st << ISPH3A_AEWINSTART_WINSH_SHIFT))

#define WRITE_BLK_VER_WIN_ST(reg, blk_win_st)		\
	(reg = (reg & ~(ISPH3A_AEWINBLK_WINSV_MASK))	\
	 | (blk_win_st << ISPH3A_AEWINBLK_WINSV_SHIFT))

#define WRITE_BLK_WIN_H(reg, height)				\
	(reg = (reg & ~(ISPH3A_AEWINBLK_WINH_MASK))		\
	 | (((height >> 1) - 1) << ISPH3A_AEWINBLK_WINH_SHIFT))

#define WRITE_SUB_VER_INC(reg, sub_ver_inc)				\
	(reg = (reg & ~(ISPH3A_AEWSUBWIN_AEWINCV_MASK))			\
	 | (((sub_ver_inc >> 1) - 1) << ISPH3A_AEWSUBWIN_AEWINCV_SHIFT))

#define WRITE_SUB_HOR_INC(reg, sub_hor_inc)				\
	(reg = (reg & ~(ISPH3A_AEWSUBWIN_AEWINCH_MASK))			\
	 | (((sub_hor_inc >> 1) - 1) << ISPH3A_AEWSUBWIN_AEWINCH_SHIFT))

/**
 * struct isph3a_aewb_regs - Current value of AE, AWB configuration registers.
 * pcr: Peripheral control register.
 * win1: Control register.
 * start: Start position register.
 * blk: Black line register.
 * subwin: Configuration register.
 */
struct isph3a_aewb_regs {
	u32 pcr;
	u32 win1;
	u32 start;
	u32 blk;
	u32 subwin;
};

struct isp_h3a_device {
	spinlock_t *lock;		/* Lock for this struct */

	u8 update;
	u8 buf_err;
	int enabled;
	int wb_update;

	struct isph3a_aewb_regs regs;
	struct ispprev_wbal h3awb_update;
	struct isph3a_aewb_config aewb_config_local;
	struct ispstat_buffer *buf_next;
	u16 win_count;
	unsigned int buf_size;

	struct ispstat stat;
};

int isph3a_aewb_config(struct isp_h3a_device *isp_h3a,
		       struct isph3a_aewb_config *aewbcfg);

int isph3a_aewb_request_statistics(struct isp_h3a_device *isp_h3a,
				   struct isph3a_aewb_data *aewbdata);

void isph3a_save_context(struct device *dev);

void isph3a_restore_context(struct device *dev);

void isph3a_aewb_enable(struct isp_h3a_device *isp_h3a, u8 enable);

void isph3a_aewb_try_enable(struct isp_h3a_device *isp_h3a);

int isph3a_aewb_busy(struct isp_h3a_device *isp_h3a);

void isph3a_aewb_suspend(struct isp_h3a_device *isp_h3a);

void isph3a_aewb_resume(struct isp_h3a_device *isp_h3a);

void isph3a_update_wb(struct isp_h3a_device *isp_h3a);

int isph3a_aewb_buf_process(struct isp_h3a_device *isp_h3a);

void isph3a_aewb_config_registers(struct isp_h3a_device *isp_h3a);

#endif		/* OMAP_ISP_H3A_H */
