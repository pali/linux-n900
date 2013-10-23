/*
 * brddefs.h
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
 *  ======== brddefs.h ========
 *  Description:
 *      Global BRD constants and types, shared between WSX, WCD, and WMD.
 *
 *! Revision History:
 *! ================
 *! 31-Jan-2000 rr: Comment Exec changed to Monitor
 *! 22-Jul-1999 jeh Added BRD_LOADED state.
 *! 26-Mar-1997 gp: Added BRD_SYNCINIT state.
 *! 11-Dec-1996 cr: Added BRD_LASTSTATE definition.
 *! 11-Jul-1996 gp: Added missing u32 callback argument to BRD_CALLBACK.
 *! 10-Jun-1996 gp: Created from board.h and brd.h.
 */

#ifndef BRDDEFS_
#define BRDDEFS_

/* platform status values */
#define BRD_STOPPED     0x0	/* No Monitor Loaded, Not running. */
#define BRD_IDLE        0x1	/* Monitor Loaded, but suspended.  */
#define BRD_RUNNING     0x2	/* Monitor loaded, and executing.  */
#define BRD_UNKNOWN     0x3	/* Board state is indeterminate. */
#define BRD_SYNCINIT    0x4
#define BRD_LOADED      0x5
#define BRD_LASTSTATE   BRD_LOADED	/* Set to highest legal board state. */
#define BRD_SLEEP_TRANSITION 0x6	/* Sleep transition in progress  */
#define BRD_HIBERNATION 0x7		/* MPU initiated hibernation */
#define BRD_RETENTION     0x8       /* Retention mode */
#define BRD_DSP_HIBERNATION     0x9       /* DSP initiated hibernation */
#define BRD_ERROR		0xA       /* Board state is Error */
	typedef u32 BRD_STATUS;

/* BRD Object */
	struct BRD_OBJECT;

#endif				/* BRDDEFS_ */
