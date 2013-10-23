/*
 * ispccdc.h
 *
 * Driver header file for CCDC module in TI's OMAP3 Camera ISP
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

#ifndef OMAP_ISP_CCDC_H
#define OMAP_ISP_CCDC_H

#include <mach/isp_user.h>

/* Enumeration constants for CCDC input output format */
enum ccdc_input {
	CCDC_RAW,
	CCDC_YUV_SYNC,
	CCDC_YUV_BT,
	CCDC_OTHERS
};

enum ccdc_output {
	CCDC_YUV_RSZ,
	CCDC_YUV_MEM_RSZ,
	CCDC_OTHERS_VP,
	CCDC_OTHERS_MEM,
	CCDC_OTHERS_VP_MEM
};

/* Enumeration constants for the sync interface parameters */
enum inpmode {
	RAW,
	YUV16,
	YUV8
};
enum datasize {
	DAT8,
	DAT10,
	DAT11,
	DAT12
};


/**
 * struct ispccdc_syncif - Structure for Sync Interface between sensor and CCDC
 * @ccdc_mastermode: Master mode. 1 - Master, 0 - Slave.
 * @fldstat: Field state. 0 - Odd Field, 1 - Even Field.
 * @ipmod: Input mode.
 * @datsz: Data size.
 * @fldmode: 0 - Progressive, 1 - Interlaced.
 * @datapol: 0 - Positive, 1 - Negative.
 * @fldpol: 0 - Positive, 1 - Negative.
 * @hdpol: 0 - Positive, 1 - Negative.
 * @vdpol: 0 - Positive, 1 - Negative.
 * @fldout: 0 - Input, 1 - Output.
 * @hs_width: Width of the Horizontal Sync pulse, used for HS/VS Output.
 * @vs_width: Width of the Vertical Sync pulse, used for HS/VS Output.
 * @ppln: Number of pixels per line, used for HS/VS Output.
 * @hlprf: Number of half lines per frame, used for HS/VS Output.
 * @bt_r656_en: 1 - Enable ITU-R BT656 mode, 0 - Sync mode.
 */
struct ispccdc_syncif {
	u8 ccdc_mastermode;
	u8 fldstat;
	enum inpmode ipmod;
	enum datasize datsz;
	u8 fldmode;
	u8 datapol;
	u8 fldpol;
	u8 hdpol;
	u8 vdpol;
	u8 fldout;
	u8 hs_width;
	u8 vs_width;
	u8 ppln;
	u8 hlprf;
	u8 bt_r656_en;
};

/**
 * ispccdc_refmt - Structure for Reformatter parameters
 * @lnalt: Line alternating mode enable. 0 - Enable, 1 - Disable.
 * @lnum: Number of output lines from 1 input line. 1 to 4 lines.
 * @plen_even: Number of program entries in even line minus 1.
 * @plen_odd: Number of program entries in odd line minus 1.
 * @prgeven0: Program entries 0-7 for even lines register
 * @prgeven1: Program entries 8-15 for even lines register
 * @prgodd0: Program entries 0-7 for odd lines register
 * @prgodd1: Program entries 8-15 for odd lines register
 * @fmtaddr0: Output line in which the original pixel is to be placed
 * @fmtaddr1: Output line in which the original pixel is to be placed
 * @fmtaddr2: Output line in which the original pixel is to be placed
 * @fmtaddr3: Output line in which the original pixel is to be placed
 * @fmtaddr4: Output line in which the original pixel is to be placed
 * @fmtaddr5: Output line in which the original pixel is to be placed
 * @fmtaddr6: Output line in which the original pixel is to be placed
 * @fmtaddr7: Output line in which the original pixel is to be placed
 */
struct ispccdc_refmt {
	u8 lnalt;
	u8 lnum;
	u8 plen_even;
	u8 plen_odd;
	u32 prgeven0;
	u32 prgeven1;
	u32 prgodd0;
	u32 prgodd1;
	u32 fmtaddr0;
	u32 fmtaddr1;
	u32 fmtaddr2;
	u32 fmtaddr3;
	u32 fmtaddr4;
	u32 fmtaddr5;
	u32 fmtaddr6;
	u32 fmtaddr7;
};

/**
 * struct isp_ccdc_device - Structure for the CCDC module to store its own
			    information
 * @ccdc_inuse: Flag to determine if CCDC has been reserved or not (0 or 1).
 * @ccdcout_w: CCDC output width.
 * @ccdcout_h: CCDC output height.
 * @ccdcin_w: CCDC input width.
 * @ccdcin_h: CCDC input height.
 * @ccdcin_woffset: CCDC input horizontal offset.
 * @ccdcin_hoffset: CCDC input vertical offset.
 * @crop_w: Crop width.
 * @crop_h: Crop weight.
 * @ccdc_inpfmt: CCDC input format.
 * @ccdc_outfmt: CCDC output format.
 * @vpout_en: Video port output enable.
 * @wen: Data write enable.
 * @exwen: External data write enable.
 * @refmt_en: Reformatter enable.
 * @ccdcslave: CCDC slave mode enable.
 * @syncif_ipmod: Image
 * @obclamp_en: Data input format.
 * @mutexlock: Mutex used to get access to the CCDC.
 * @update_lsc_config: Set when user changes lsc_config
 * @lsc_request_enable: Whether LSC is requested to be enabled
 * @lsc_config: LSC config set by user
 * @update_lsc_table: Set when user provides a new LSC table to lsc_table_new
 * @lsc_table_new: LSC table set by user, ISP address
 * @lsc_table_inuse: LSC table currently in use, ISP address
 * @shadow_update: non-zero when user is updating CCDC configuration
 * @lock: serializes shadow_update with interrupt handler
 */
struct isp_ccdc_device {
	u8 ccdc_inuse;
	u32 ccdcin_woffset;
	u32 ccdcin_hoffset;
	u32 crop_w;
	u32 crop_h;
	u8 vpout_en;
	u8 wen;
	u8 exwen;
	u8 refmt_en;
	u8 ccdcslave;
	u8 syncif_ipmod;
	u8 obclamp_en;
	struct mutex mutexlock; /* For checking/modifying ccdc_inuse */
	u32 wenlog;
	unsigned long fpc_table_add_m;
	u32 *fpc_table_add;

	/* LSC related fields */
	u8 update_lsc_config;
	u8 lsc_request_enable;
	struct ispccdc_lsc_config lsc_config;
	u8 update_lsc_table;
	u32 lsc_table_new;
	u32 lsc_table_inuse;

	int shadow_update;
	spinlock_t lock;
};

void ispccdc_lsc_error_handler(struct isp_ccdc_device *isp_ccdc);
int ispccdc_set_outaddr(struct isp_ccdc_device *isp_ccdc, u32 addr);
void ispccdc_set_wenlog(struct isp_ccdc_device *isp_ccdc, u32 wenlog);
int ispccdc_try_pipeline(struct isp_ccdc_device *isp_ccdc,
			 struct isp_pipeline *pipe);
int ispccdc_s_pipeline(struct isp_ccdc_device *isp_ccdc,
		       struct isp_pipeline *pipe);
void ispccdc_enable(struct isp_ccdc_device *isp_ccdc, u8 enable);
int ispccdc_sbl_busy(void *_isp_ccdc);
int ispccdc_busy(struct isp_ccdc_device *isp_ccdc);
void ispccdc_config_shadow_registers(struct isp_ccdc_device *isp_ccdc);
int ispccdc_config(struct isp_ccdc_device *isp_ccdc,
			     void *userspace_add);
int ispccdc_request(struct isp_ccdc_device *isp_ccdc);
int ispccdc_free(struct isp_ccdc_device *isp_ccdc);
void ispccdc_save_context(struct device *dev);
void ispccdc_restore_context(struct device *dev);

#endif		/* OMAP_ISP_CCDC_H */
