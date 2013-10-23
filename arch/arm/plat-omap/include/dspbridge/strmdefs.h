/*
 * strmdefs.h
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
 *  ======== strmdefs.h ========
 *  Purpose:
 *      Global STRM constants and types.
 *
 *! Revision History
 *! ================
 *! 19-Nov-2001 ag      Added STRM_INFO..
 *! 25-Sep-2000 jeh     Created.
 */

#ifndef STRMDEFS_
#define STRMDEFS_

#define STRM_MAXEVTNAMELEN      32

	struct STRM_MGR;

	struct STRM_OBJECT;

	struct STRM_ATTR {
		HANDLE hUserEvent;
		char *pstrEventName;
		void *pVirtBase;	/* Process virtual base address of
					 * mapped SM */
		u32 ulVirtSize;	/* Size of virtual space in bytes */
		struct DSP_STREAMATTRIN *pStreamAttrIn;
	} ;

	struct STRM_INFO {
		enum DSP_STRMMODE lMode;	/* transport mode of
					 * stream(DMA, ZEROCOPY..) */
		u32 uSegment;	/* Segment strm allocs from. 0 is local mem */
		void *pVirtBase;	/* "      " Stream'process virt base */
		struct DSP_STREAMINFO *pUser;	/* User's stream information
						 * returned */
	} ;

#endif				/* STRMDEFS_ */

