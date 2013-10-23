/*
 * cfgdefs.h
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
 *  ======== cfgdefs.h ========
 *  Purpose:
 *      Global CFG constants and types, shared between class and mini driver.
 *
 *! Revision History:
 *! ================
 *! 24-Feb-2003 kc  Removed wIOPort* in CFG_HOSTRES.
 *! 06-Sep-2000 jeh Added channel info to CFG_HOSTRES.
 *! 09-May-2000 rr: CFG_HOSTRES now support multiple windows for PCI support.
 *! 31-Jan-2000 rr: Comments changed after code review.
 *! 06-Jan-2000 rr: Bus Type included in CFG_HOSTRES.
 *! 12-Nov-1999 rr: CFG_HOSTRES member names changed.
 *! 25-Oct-1999 rr: Modified the CFG_HOSTRES Structure
 *!                 PCMCIA ISR Register/Unregister fxn removed..
 *!                 New flag PCCARD introduced during compile time.
 *! 10-Sep-1999 ww: Added PCMCIA ISR Register/Unregister fxn.
 *! 01-Sep-1999 ag: Removed NT/95 specific fields in CFG_HOSTRES
 *! 27-Oct-1997 cr: Updated CFG_HOSTRES struct to support 1+ IRQs per board.
 *! 17-Sep-1997 gp: Tacked some NT config info to end of CFG_HOSTRES structure.
 *! 12-Dec-1996 cr: Cleaned up after code review.
 *! 14-Nov-1996 gp: Renamed from wsxcfg.h
 *! 19-Jun-1996 cr: Created.
 */

#ifndef CFGDEFS_
#define CFGDEFS_

/* Maximum length of module search path. */
#define CFG_MAXSEARCHPATHLEN    255

/* Maximum length of general paths. */
#define CFG_MAXPATH             255

/* Host Resources:  */
#define CFG_MAXMEMREGISTERS     9
#define CFG_MAXIOPORTS          20
#define CFG_MAXIRQS             7
#define CFG_MAXDMACHANNELS      7

/* IRQ flag */
#define CFG_IRQSHARED           0x01	/* IRQ can be shared */

/* DSP Resources: */
#define CFG_DSPMAXMEMTYPES      10
#define CFG_DEFAULT_NUM_WINDOWS 1	/* We support only one window. */

/* A platform-related device handle: */
	struct CFG_DEVNODE;

/*
 *  Host resource structure.
 */
	struct CFG_HOSTRES {
		u32 wNumMemWindows;	/* Set to default */
		/* This is the base.memory */
		u32 dwMemBase[CFG_MAXMEMREGISTERS];  /* SHM virtual address */
		u32 dwMemLength[CFG_MAXMEMREGISTERS]; /* Length of the Base */
		u32 dwMemPhys[CFG_MAXMEMREGISTERS]; /* SHM Physical address */
		u8 bIRQRegisters;	/* IRQ Number */
		u8 bIRQAttrib;	/* IRQ Attribute */
		u32 dwOffsetForMonitor;	/* The Shared memory starts from
					 * dwMemBase + this offset */
	/*
	 *  Info needed by NODE for allocating channels to communicate with RMS:
	 *      dwChnlOffset:       Offset of RMS channels. Lower channels are
	 *                          reserved.
	 *      dwChnlBufSize:      Size of channel buffer to send to RMS
	 *      dwNumChnls:       Total number of channels (including reserved).
	 */
		u32 dwChnlOffset;
		u32 dwChnlBufSize;
		u32 dwNumChnls;
		void __iomem *dwPrmBase;
		void __iomem *dwCmBase;
		void __iomem *dwPerBase;
		u32 dwPerPmBase;
		u32 dwCorePmBase;
		void __iomem *dwWdTimerDspBase;
		void __iomem *dwMboxBase;
		void __iomem *dwDmmuBase;
		void __iomem *dwSysCtrlBase;
	} ;

	struct CFG_DSPMEMDESC {
		u32 uMemType;	/* Type of memory.                        */
		u32 ulMin;	/* Minimum amount of memory of this type. */
		u32 ulMax;	/* Maximum amount of memory of this type. */
	} ;

	struct CFG_DSPRES {
		u32 uChipType;	/* DSP chip type.               */
		u32 uWordSize;	/* Number of bytes in a word    */
		u32 cChips;	/* Number of chips.             */
		u32 cMemTypes;	/* Types of memory.             */
		struct CFG_DSPMEMDESC aMemDesc[CFG_DSPMAXMEMTYPES];
		/* DSP Memory types */
	} ;

#endif				/* CFGDEFS_ */
