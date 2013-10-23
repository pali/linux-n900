/*
 * hw_dspss64P.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * API definitions to configure DSP Subsystem modules like IPI
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

/* PROJECT SPECIFIC INCLUDE FILES */
#include <GlobalTypes.h>
#include <linux/io.h>
#include <hw_defs.h>
#include <hw_dspssC64P.h>
#include <IVA2RegAcM.h>
#include <IPIAccInt.h>

/* HW FUNCTIONS */
hw_status hw_dspss_boot_mode_set(const void __iomem *baseAddress,
				 enum hw_dspsysc_boot_mode_t bootMode,
				 const u32 bootAddress)
{
	hw_status status = RET_OK;
	u32 offset = SYSC_IVA2BOOTMOD_OFFSET;
	u32 aligned_boot_addr;

	/* if Boot mode it DIRECT BOOT, check that the bootAddress is
	 * aligned to atleast 1K :: TODO */
	__raw_writel(bootMode, (baseAddress) + offset);

	offset = SYSC_IVA2BOOTADDR_OFFSET;

	aligned_boot_addr = bootAddress & SYSC_IVA2BOOTADDR_MASK;

	__raw_writel(aligned_boot_addr, (baseAddress) + offset);

	return status;
}
