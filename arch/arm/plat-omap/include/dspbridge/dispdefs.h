/*
 * dispdefs.h
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
 *  ======== dispdefs.h ========
 *  Description:
 *      Global DISP constants and types, shared by PROCESSOR, NODE, and DISP.
 *
 *! Revision History
 *! ================
 *! 08-Aug-2000 jeh     Added fields to DISP_ATTRS.
 *! 06-Jul-2000 jeh     Created.
 */

#ifndef DISPDEFS_
#define DISPDEFS_

	struct DISP_OBJECT;

/* Node Dispatcher attributes */
	struct DISP_ATTRS {
		u32 ulChnlOffset; /* Offset of channel ids reserved for RMS */
		/* Size of buffer for sending data to RMS */
		u32 ulChnlBufSize;
		DSP_PROCFAMILY procFamily;	/* eg, 5000 */
		DSP_PROCTYPE procType;	/* eg, 5510 */
		HANDLE hReserved1;	/* Reserved for future use. */
		u32 hReserved2;	/* Reserved for future use. */
	} ;

#endif				/* DISPDEFS_ */
