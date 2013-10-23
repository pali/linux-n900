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
#include "ispstat.h"

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
#define AF_PCR_MASK			(FVMODE | RGBPOS | MED_TH | \
					 AF_MED_EN | AF_ALAW_EN)

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
 * struct isp_af_status - AF status.
 * @update: 1 - Update registers.
 */
struct isp_af_device {
	u8 update;
	u8 buf_err;
	int enabled;
	unsigned int buf_size;
	struct ispstat stat;
	struct af_configuration config; /*Device configuration structure */
	struct ispstat_buffer *buf_next;
	spinlock_t *lock;
};

int isp_af_buf_process(struct isp_af_device *isp_af);
void isp_af_enable(struct isp_af_device *, int);
void isp_af_try_enable(struct isp_af_device *isp_af);
void isp_af_suspend(struct isp_af_device *);
void isp_af_resume(struct isp_af_device *);
int isp_af_busy(struct isp_af_device *);
void isp_af_config_registers(struct isp_af_device *isp_af);
int isp_af_request_statistics(struct isp_af_device *,
			      struct isp_af_data *afdata);
int isp_af_config(struct isp_af_device *, struct af_configuration *afconfig);

#endif	/* OMAP_ISP_AF_H */
