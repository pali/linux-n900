/*
 * chnldefs.h
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
 *  ======== chnldefs.h ========
 *  Purpose:
 *      System-wide channel objects and constants.
 *
 *! Revision History:
 *! ================
 *! 19-Jan-2002 ag  Added cBufSize to IOC.
 *! 05-Jan-2000 ag: Text format cleanup.
 *! 02-Dec-1999 ag: Added new chnl attribute pstrEventName.
 *! 12-Nov-1999 kc: Enabled hEvent attribute for tests.
 *! 01-Nov-1999 ag: hEvent attribute not supported(yet).
 *! 16-Jan-1997 gp: Moved private stuff into chnlpriv.h
 *! 14-Jan-1997 gp: Updated based on code review feedback:
 *!                 Removed CHNL_MODENOWAIT, CHNL_MODEDIRECT,
 *! 03-Jan-1997 gp: Added channel class library types.
 *! 14-Dec-1996 gp: Moved uChnlId field from CHNL_ATTRS to CHNL_Open().
 *! 10-Dec-1996 gp: Added CHNL_IsTimedOut() macro.
 *! 14-Nov-1996 gp: Renamed from wsxchnl.h.
 *! 09-Sep-1996 gp: Added hReserved2 field to CHNL_ATTRS. Updated CHNL_INFO.
 *! 10-Jul-1996 gp: Created from channel.h.
 */

#ifndef CHNLDEFS_
#define CHNLDEFS_

/* Channel id option. */
#define CHNL_PICKFREE       (~0UL)	/* Let manager pick a free channel. */

/* Channel manager limits: */
#define CHNL_INITIOREQS      4	/* Default # of I/O requests.       */

/* Channel modes */
#define CHNL_MODETODSP       0x0000	/* Data streaming to the DSP.      */
#define CHNL_MODEFROMDSP     0x0001	/* Data streaming from the DSP.    */

/* GetIOCompletion flags */
#define CHNL_IOCINFINITE     0xffffffff	/* Wait forever for IO completion. */
#define CHNL_IOCNOWAIT       0x0	/* Dequeue an IOC, if available.   */

/* IO Completion Record status: */
#define CHNL_IOCSTATCOMPLETE 0x0000	/* IO Completed.             */
#define CHNL_IOCSTATCANCEL   0x0002	/* IO was cancelled          */
#define CHNL_IOCSTATTIMEOUT  0x0008	/* Wait for IOC timed out.   */
#define CHNL_IOCSTATEOS      0x8000	/* End Of Stream reached.    */

/* Macros for checking I/O Completion status: */
#define CHNL_IsEOS(ioc)         (ioc.status & CHNL_IOCSTATEOS)
#define CHNL_IsIOComplete(ioc)  (!(ioc.status & ~CHNL_IOCSTATEOS))
#define CHNL_IsIOCancelled(ioc) (ioc.status & CHNL_IOCSTATCANCEL)
#define CHNL_IsTimedOut(ioc)    (ioc.status & CHNL_IOCSTATTIMEOUT)

/* CHNL types: */
	typedef u32 CHNL_MODE;	/* Channel transfer mode.         */

/* Channel attributes: */
	struct CHNL_ATTRS {
		u32 uIOReqs;	/* Max # of preallocated I/O requests.    */
		HANDLE hEvent;	/* User supplied auto-reset event object. */
		char *pstrEventName;	/* Ptr to name of user event object.  */
		HANDLE hReserved1;	/* Reserved for future use.         */
		u32 hReserved2;	/* Reserved for future use.        */

	};

/* I/O completion record: */
	struct CHNL_IOC {
		void *pBuf;	/* Buffer to be filled/emptied.           */
		u32 cBytes;	/* Bytes transferred.                     */
		u32 cBufSize;	/* Actual buffer size in bytes            */
		u32 status;	/* Status of IO completion.               */
		u32 dwArg;	/* User argument associated with pBuf.    */
	} ;

#endif				/* CHNLDEFS_ */
