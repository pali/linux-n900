/*
 * utildefs.h
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
 *  ======== utildefs.h ========
 *  Purpose:
 *      Global UTIL constants and types, shared between WCD and DSPSYS.
 *
 *! Revision History:
 *! ================
 *! 24-Feb-2003 kc  Removed wIOPort* entries from UTIL_HOSTCONFIG.
 *! 12-Aug-2000 ag  Added UTIL_SYSINFO typedef.
 *! 08-Oct-1999 rr  Adopted for WinCE where test fxns will be added in util.h
 *! 26-Dec-1996 cr  Created.
 */

#ifndef UTILDEFS_
#define UTILDEFS_

/* constants taken from configmg.h */
#define UTIL_MAXMEMREGS     9
#define UTIL_MAXIOPORTS     20
#define UTIL_MAXIRQS        7
#define UTIL_MAXDMACHNLS    7

/* misc. constants */
#define UTIL_MAXARGVS       10

/* Platform specific important info */
	struct UTIL_SYSINFO {
		/* Granularity of page protection; usually 1k or 4k */
		u32 dwPageSize;
		u32 dwAllocationGranularity; /* VM granularity, usually 64K */
		u32 dwNumberOfProcessors;	/* Used as sanity check */
	} ;

#endif				/* UTILDEFS_ */
