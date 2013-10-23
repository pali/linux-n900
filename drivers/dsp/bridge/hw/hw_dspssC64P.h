/*
 * hw_dspssC64P.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
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


/*
 *  ======== hw_dspss.h ========
 *  Description:
 *      DSP Subsystem API declarations
 *
 *! Revision History:
 *! ================
 *! 19-Apr-2004 sb: Removed redundant argument from HW_DSPSS_IPIEndianismSet
 *!		    Moved endianness and element size to generic hw_defs.h
 *! 16 Feb 2003 sb: Initial version
 */

#ifndef __HW_DSPSS_H
#define __HW_DSPSS_H
#include <linux/types.h>

	enum HW_DSPSYSC_BootMode_t {
		HW_DSPSYSC_DIRECTBOOT = 0x0,
		HW_DSPSYSC_IDLEBOOT = 0x1,
		HW_DSPSYSC_SELFLOOPBOOT = 0x2,
		HW_DSPSYSC_USRBOOTSTRAP = 0x3,
		HW_DSPSYSC_DEFAULTRESTORE = 0x4
	} ;

#define HW_DSP_IDLEBOOT_ADDR   0x007E0000

	extern HW_STATUS HW_DSPSS_BootModeSet(const void __iomem *baseAddress,
					enum HW_DSPSYSC_BootMode_t bootMode,
					const u32 bootAddress);

#endif				/* __HW_DSPSS_H */
