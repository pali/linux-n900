/*
 * hw_dspssC64P.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * DSP Subsystem API declarations
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef _HW_DSPSS_H
#define _HW_DSPSS_H
#include <linux/types.h>

enum hw_dspsysc_boot_mode_t {
	HW_DSPSYSC_DIRECTBOOT = 0x0,
	HW_DSPSYSC_IDLEBOOT = 0x1,
	HW_DSPSYSC_SELFLOOPBOOT = 0x2,
	HW_DSPSYSC_USRBOOTSTRAP = 0x3,
	HW_DSPSYSC_DEFAULTRESTORE = 0x4
};

#define HW_DSP_IDLEBOOT_ADDR   0x007E0000

extern hw_status hw_dspss_boot_mode_set(const void __iomem *baseAddress,
					enum hw_dspsysc_boot_mode_t bootMode,
					const u32 bootAddress);

#endif /* _HW_DSPSS_H */
