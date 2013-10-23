/*
 * isp_af.h
 *
 * Include file for AF module in TI's OMAP3 Camera ISP
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

/* Device Constants */
#ifndef OMAP_ISP_AF_H
#define OMAP_ISP_AF_H

#include <mach/isp_user.h>

#include "isph3a.h"

#define AF_MAJOR_NUMBER			0
#define ISPAF_NAME			"OMAPISP_AF"
#define AF_NR_DEVS			1
#define AF_TIMEOUT			((300 * HZ) / 1000)



/* Print Macros */
/*list of error code */
#define AF_ERR_HZ_COUNT			800	/* Invalid Horizontal Count */
#define AF_ERR_VT_COUNT			801	/* Invalid Vertical Count */
#define AF_ERR_HEIGHT			802	/* Invalid Height */
#define AF_ERR_WIDTH			803	/* Invalid width */
#define AF_ERR_INCR			804	/* Invalid Increment */
#define AF_ERR_HZ_START			805	/* Invalid horizontal Start */
#define AF_ERR_VT_START			806	/* Invalud vertical Start */
#define AF_ERR_IIRSH			807	/* Invalid IIRSH value */
#define AF_ERR_IIR_COEF			808	/* Invalid Coefficient */
#define AF_ERR_SETUP			809	/* Setup not done */
#define AF_ERR_THRESHOLD		810	/* Invalid Threshold */
#define AF_ERR_ENGINE_BUSY		811	/* Engine is busy */

#define AFPID				0x0	/* Peripheral Revision
						 * and Class Information
						 */

#define AFCOEF_OFFSET			0x00000004	/* COEFFICIENT BASE
							 * ADDRESS
							 */

/*
 * PCR fields
 */
#define AF_BUSYAF			(1 << 15)
#define FVMODE				(1 << 14)
#define RGBPOS				(0x7 << 11)
#define MED_TH				(0xFF << 3)
#define AF_MED_EN			(1 << 2)
#define AF_ALAW_EN			(1 << 1)
#define AF_EN				(1 << 0)

/*
 * AFPAX1 fields
 */
#define PAXW				(0x7F << 16)
#define PAXH				0x7F

/*
 * AFPAX2 fields
 */
#define AFINCV				(0xF << 13)
#define PAXVC				(0x7F << 6)
#define PAXHC				0x3F

/*
 * AFPAXSTART fields
 */
#define PAXSH				(0xFFF<<16)
#define PAXSV				0xFFF

/*
 * COEFFICIENT MASK
 */

#define COEF_MASK0			0xFFF
#define COEF_MASK1			(0xFFF<<16)

/* BIT SHIFTS */
#define AF_RGBPOS_SHIFT			11
#define AF_MED_TH_SHIFT			3
#define AF_PAXW_SHIFT			16
#define AF_LINE_INCR_SHIFT		13
#define AF_VT_COUNT_SHIFT		6
#define AF_HZ_START_SHIFT		16
#define AF_COEF_SHIFT			16

#define AF_UPDATEXS_TS			(1 << 0)
#define AF_UPDATEXS_FIELDCOUNT	(1 << 1)
#define AF_UPDATEXS_LENSPOS		(1 << 2)

/**
 * struct isp_af_buffer - AF frame stats buffer.
 * @virt_addr: Virtual address to mmap the buffer.
 * @phy_addr: Physical address of the buffer.
 * @addr_align: Virtual Address 32 bytes aligned.
 * @ispmmu_addr: Address of the buffer mapped by the ISPMMU.
 * @mmap_addr: Mapped memory area of buffer. For userspace access.
 * @locked: 1 - Buffer locked from write. 0 - Buffer can be overwritten.
 * @frame_num: Frame number from which the statistics are taken.
 * @lens_position: Lens position currently set in the DW9710 Coil motor driver.
 * @next: Pointer to link next buffer.
 */
struct isp_af_buffer {
	unsigned long virt_addr;
	unsigned long phy_addr;
	unsigned long addr_align;
	unsigned long ispmmu_addr;
	unsigned long mmap_addr;

	u8 locked;
	u16 frame_num;
	u32 config_counter;
	struct isp_af_xtrastats xtrastats;
	struct isp_af_buffer *next;
};

/**
 * struct isp_af_status - AF status.
 * @initialized: 1 - Buffers initialized.
 * @update: 1 - Update registers.
 * @stats_req: 1 - Future stats requested.
 * @stats_done: 1 - Stats ready for user.
 * @frame_req: Number of frame requested for statistics.
 * @af_buff: Array of statistics buffers to access.
 * @stats_buf_size: Statistics buffer size.
 * @curr_cfg_buf_size: Current user configured stats buff size.
 * @min_buf_size: Minimum statisitics buffer size.
 * @frame_count: Frame Count.
 * @stats_wait: Wait primitive for locking/unlocking the stats request.
 * @buffer_lock: Spinlock for statistics buffers access.
 */
struct isp_af_device {
	u8 initialized;
	u8 update;
	u8 stats_req;
	u8 stats_done;
	u16 frame_req;

	struct isp_af_buffer af_buff[H3A_MAX_BUFF];
	unsigned int active_buffer;
	unsigned int stats_buf_size;
	unsigned int min_buf_size;
	unsigned int curr_cfg_buf_size;
	struct isp_af_buffer *active_buff;

	int pm_state;
	u32 frame_count;
	wait_queue_head_t stats_wait;
	atomic_t config_counter;
	spinlock_t buffer_lock;		/* For stats buffers read/write sync */
	struct device *dev;
	int camnotify;

	struct af_configuration config; /*Device configuration structure */
	int size_paxel;         /*Paxel size in bytes */
};

int isp_af_check_paxel(struct isp_af_device *);
int isp_af_check_iir(struct isp_af_device *);
int isp_af_register_setup(struct isp_af_device *);
int isp_af_enable(struct isp_af_device *, int);
void isp_af_suspend(struct isp_af_device *);
void isp_af_resume(struct isp_af_device *);
int isp_af_busy(struct isp_af_device *);
void isp_af_notify(struct isp_af_device *, int notify);
int isp_af_request_statistics(struct isp_af_device *,
			      struct isp_af_data *afdata);
int isp_af_configure(struct isp_af_device *, struct af_configuration *afconfig);
void isp_af_set_address(struct isp_af_device *, unsigned long);
void isp_af_setxtrastats(struct isp_af_device *,
			 struct isp_af_xtrastats *xtrastats, u8 updateflag);
#endif	/* OMAP_ISP_AF_H */
