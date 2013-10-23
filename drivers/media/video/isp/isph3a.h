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
 * struct isph3a_aewb_xtrastats - Structure with extra statistics sent by cam.
 * @field_count: Sequence number of returned framestats.
 * @isph3a_aewb_xtrastats: Pointer to next buffer with extra stats.
 */
struct isph3a_aewb_xtrastats {
	unsigned long field_count;
	struct isph3a_aewb_xtrastats *next;
};

/**
 * struct isph3a_aewb_buffer - AE, AWB frame stats buffer.
 * @virt_addr: Virtual address to mmap the buffer.
 * @phy_addr: Physical address of the buffer.
 * @addr_align: Virtual Address 32 bytes aligned.
 * @ispmmu_addr: Address of the buffer mapped by the ISPMMU.
 * @mmap_addr: Mapped memory area of buffer. For userspace access.
 * @locked: 1 - Buffer locked from write. 0 - Buffer can be overwritten.
 * @frame_num: Frame number from which the statistics are taken.
 * @next: Pointer to link next buffer.
 */
struct isph3a_aewb_buffer {
	unsigned long virt_addr;
	unsigned long phy_addr;
	unsigned long addr_align;
	unsigned long ispmmu_addr;
	unsigned long mmap_addr;	/* For userspace */
	struct timeval ts;
	u32 config_counter;

	u8 locked;
	u16 frame_num;
	struct isph3a_aewb_buffer *next;
};

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

/**
 * struct isp_h3a_device - AE, AWB status.
 * @initialized: 1 - Buffers initialized.
 * @update: 1 - Update registers.
 * @stats_req: 1 - Future stats requested.
 * @stats_done: 1 - Stats ready for user.
 * @frame_req: Number of frame requested for statistics.
 * @h3a_buff: Array of statistics buffers to access.
 * @stats_buf_size: Statistics buffer size.
 * @min_buf_size: Minimum statisitics buffer size.
 * @win_count: Window Count.
 * @frame_count: Frame Count.
 * @stats_wait: Wait primitive for locking/unlocking the stats request.
 * @buffer_lock: Spinlock for statistics buffers access.
 */
struct isp_h3a_device {
	u8 initialized;
	u8 update;
	u8 stats_req;
	u8 stats_done;
	u16 frame_req;
	int pm_state;
	int camnotify;
	int wb_update;

	struct isph3a_aewb_buffer buff[H3A_MAX_BUFF];
	unsigned int stats_buf_size;
	unsigned int min_buf_size;
	unsigned int curr_cfg_buf_size;
	struct isph3a_aewb_buffer *active_buff;

	atomic_t config_counter;
	struct isph3a_aewb_regs regs;
	struct ispprev_wbal h3awb_update;
	struct isph3a_aewb_xtrastats xtrastats[H3A_MAX_BUFF];
	struct isph3a_aewb_config aewb_config_local;
	u16 win_count;
	u32 frame_count;
	wait_queue_head_t stats_wait;
	spinlock_t buffer_lock;		/* For stats buffers read/write sync */

	struct device *dev;
};

void isph3a_aewb_setxtrastats(struct isp_h3a_device *isp_h3a,
			      struct isph3a_aewb_xtrastats *xtrastats);

int isph3a_aewb_configure(struct isp_h3a_device *isp_h3a,
			  struct isph3a_aewb_config *aewbcfg);

int isph3a_aewb_request_statistics(struct isp_h3a_device *isp_h3a,
				   struct isph3a_aewb_data *aewbdata);

void isph3a_save_context(struct device *dev);

void isph3a_restore_context(struct device *dev);

void isph3a_aewb_enable(struct isp_h3a_device *isp_h3a, u8 enable);

int isph3a_aewb_busy(struct isp_h3a_device *isp_h3a);

void isph3a_aewb_suspend(struct isp_h3a_device *isp_h3a);

void isph3a_aewb_resume(struct isp_h3a_device *isp_h3a);

void isph3a_update_wb(struct isp_h3a_device *isp_h3a);

void isph3a_notify(struct isp_h3a_device *isp_h3a, int notify);
#endif		/* OMAP_ISP_H3A_H */
