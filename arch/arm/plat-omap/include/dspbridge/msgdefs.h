/*
 * msgdefs.h
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
 *  ======== msgdefs.h ========
 *  Description:
 *      Global MSG constants and types.
 *
 *! Revision History
 *! ================
 *! 09-May-2001 jeh Removed MSG_TODSP, MSG_FROMDSP.
 *! 17-Nov-2000 jeh Added MSGMGR_SIGNATURE.
 *! 12-Sep-2000 jeh Created.
 */

#ifndef MSGDEFS_
#define MSGDEFS_

#define MSGMGR_SIGNATURE    0x4d47534d	/* "MGSM" */

/* MSG Objects: */
	struct MSG_MGR;
	struct MSG_QUEUE;

/* Function prototype for callback to be called on RMS_EXIT message received */
       typedef void(*MSG_ONEXIT) (HANDLE h, s32 nStatus);

#endif				/* MSGDEFS_ */

