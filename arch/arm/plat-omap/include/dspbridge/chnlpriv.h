/*
 * chnlpriv.h
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
 *  ======== chnlpriv.h ========
 *  Description:
 *      Private channel header shared between DSPSYS, WCD and WMD modules.
 *
 *  Public Functions:
 *      None.
 *
 *  Notes:
 *
 *! Revision History:
 *! ================
 *! 05-Jan-2002 ag  Added cChannels(total # of chnls) to CHNL_MGRINFO struct.
 *!                 Added private CHNL_[PCPY][ZCPY][DDMA].
 *! 17-Nov-2000 jeh Removed IRQ, shared memory from CHNL_MGRATTRS, since these
 *!                 now belong to IO_ATTRS.
 *! 21-Jan-2000 ag: Code review comments added.
 *! 05-Jan-2000 ag: Text format cleanup.
 *! 11-Dec-1999 ag: Added CHNL_MAXLOCKPAGES for CHNL_PrepareBuffer().
 *! 04-Dec-1999 ag: Added CHNL_MAXEVTNAMELEN for i/o compl named event support.
 *! 01-Nov-1999 ag: CHNL_MAXCHANNELS set to 16 for 16-bit DSPs.
 *! 27-Oct-1997 cr: Expanded CHNL_MAXIRQ from 0x0f to 0xff.
 *! 16-Jan-1997 gp: Moved symbols into here from chnldefs.h.
 *! 03-Jan-1997 gp: Added CHNL_MAXIRQ define.
 *! 09-Dec-1996 gp: Removed CHNL_STATEIDLE.
 *! 15-Jul-1996 gp: Created.
 */

#ifndef CHNLPRIV_
#define CHNLPRIV_

#include <dspbridge/chnldefs.h>
#include <dspbridge/devdefs.h>
#include <dspbridge/sync.h>

/* CHNL Object validation signatures: */
#define CHNL_MGRSIGNATURE   0x52474D43	/* "CMGR" (in reverse). */
#define CHNL_SIGNATURE      0x4C4E4843	/* "CHNL" (in reverse). */

/* Channel manager limits: */
#define CHNL_MAXCHANNELS    32	/* Max channels available per transport */


/*
 *  Trans port channel Id definitions:(must match dsp-side).
 *
 *  For CHNL_MAXCHANNELS = 16:
 *
 *  ChnlIds:
 *      0-15  (PCPY) - transport 0)
 *      16-31 (DDMA) - transport 1)
 *      32-47 (ZCPY) - transport 2)
 */
#define CHNL_PCPY       0	/* Proc-copy transport 0 */

#define CHNL_MAXIRQ     0xff	/* Arbitrarily large number. */

/* The following modes are private: */
#define CHNL_MODEUSEREVENT  0x1000	/* User provided the channel event. */
#define CHNL_MODEMASK       0x1001

/* Higher level channel states: */
#define CHNL_STATEREADY     0x0000	/* Channel ready for I/O.    */
#define CHNL_STATECANCEL    0x0001	/* I/O was cancelled.        */
#define CHNL_STATEEOS       0x0002	/* End Of Stream reached.    */

/* Determine if user supplied an event for this channel:  */
#define CHNL_IsUserEvent(mode)  (mode & CHNL_MODEUSEREVENT)

/* Macros for checking mode: */
#define CHNL_IsInput(mode)      (mode & CHNL_MODEFROMDSP)
#define CHNL_IsOutput(mode)     (!CHNL_IsInput(mode))

/* Types of channel class libraries: */
#define CHNL_TYPESM         1	/* Shared memory driver. */
#define CHNL_TYPEBM         2	/* Bus Mastering driver. */

/* Max string length of channel I/O completion event name - change if needed */
#define CHNL_MAXEVTNAMELEN  32

/* Max memory pages lockable in CHNL_PrepareBuffer() - change if needed */
#define CHNL_MAXLOCKPAGES   64

/* Channel info.  */
	 struct CHNL_INFO {
		struct CHNL_MGR *hChnlMgr;	/* Owning channel manager.   */
		u32 dwID;	/* Channel ID.                            */
		HANDLE hEvent;	/* Channel I/O completion event.          */
		/*Abstraction of I/O completion event.*/
		struct SYNC_OBJECT *hSyncEvent;
		u32 dwMode;	/* Channel mode.                          */
		u32 dwState;	/* Current channel state.                 */
		u32 cPosition;	/* Total bytes transferred.        */
		u32 cIOCs;	/* Number of IOCs in queue.               */
		u32 cIOReqs;	/* Number of IO Requests in queue.        */
               u32 hProcess;   /* Process owning this channel.     */
		/*
		 * Name of channel I/O completion event. Not required in Linux
		 */
		char szEventName[CHNL_MAXEVTNAMELEN + 1];
	} ;

/* Channel manager info: */
	struct CHNL_MGRINFO {
		u32 dwType;	/* Type of channel class library.         */
		/* Channel handle, given the channel id. */
		struct CHNL_OBJECT *hChnl;
		u32 cOpenChannels;	/* Number of open channels.     */
		u32 cChannels;	/* total # of chnls supported */
	} ;

/* Channel Manager Attrs: */
	struct CHNL_MGRATTRS {
		/* Max number of channels this manager can use. */
		u32 cChannels;
		u32 uWordSize;	/* DSP Word size.                       */
	} ;

#endif				/* CHNLPRIV_ */
