/*
 * chnl.h
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
 *  ======== chnl.h ========
 *  Description:
 *      WCD channel interface: multiplexes data streams through the single
 *      physical link managed by a mini-driver.
 *
 *  Public Functions:
 *      CHNL_AddIOReq
 *      CHNL_AllocBuffer
 *      CHNL_CancelIO
 *      CHNL_Close
 *      CHNL_CloseOrphans
 *      CHNL_Create
 *      CHNL_Destroy
 *      CHNL_Exit
 *      CHNL_FlushIO
 *      CHNL_FreeBuffer
 *      CHNL_GetEventHandle
 *      CHNL_GetHandle
 *      CHNL_GetIOCompletion
 *      CHNL_GetId
 *      CHNL_GetMgr
 *      CHNL_GetMode
 *      CHNL_GetPosition
 *      CHNL_GetProcessHandle
 *      CHNL_Init
 *      CHNL_Open
 *
 *  Notes:
 *      See DSP API chnl.h for more details.
 *
 *! Revision History:
 *! ================
 *! 14-Jan-1997 gp: Updated based on code review feedback.
 *! 24-Oct-1996 gp: Move CloseOrphans into here from dspsys.
 *! 09-Sep-1996 gp: Added CHNL_GetProcessID() and CHNL_GetHandle().
 *! 10-Jul-1996 gp: Created.
 */

#ifndef CHNL_
#define CHNL_

#include <dspbridge/chnlpriv.h>

/*
 *  ======== CHNL_Close ========
 *  Purpose:
 *      Ensures all pending I/O on this channel is cancelled, discards all
 *      queued I/O completion notifications, then frees the resources allocated
 *      for this channel, and makes the corresponding logical channel id
 *      available for subsequent use.
 *  Parameters:
 *      hChnl:          Channel object handle.
 *  Returns:
 *      DSP_SOK:        Success;
 *      DSP_EHANDLE:    Invalid hChnl.
 *  Requires:
 *      CHNL_Init(void) called.
 *      No thread must be blocked on this channel's I/O completion event.
 *  Ensures:
 *      DSP_SOK:        The I/O completion event for this channel is freed.
 *                      hChnl is no longer valid.
 */
	extern DSP_STATUS CHNL_Close(struct CHNL_OBJECT *hChnl);


/*
 *  ======== CHNL_Create ========
 *  Purpose:
 *      Create a channel manager object, responsible for opening new channels
 *      and closing old ones for a given board.
 *  Parameters:
 *      phChnlMgr:      Location to store a channel manager object on output.
 *      hDevObject:     Handle to a device object.
 *      pMgrAttrs:      Channel manager attributes.
 *      pMgrAttrs->cChannels:   Max channels
 *      pMgrAttrs->bIRQ:        Channel's I/O IRQ number.
 *      pMgrAttrs->fShared:     TRUE if the IRQ is shareable.
 *      pMgrAttrs->uWordSize:   DSP Word size in equivalent PC bytes..
 *  Returns:
 *      DSP_SOK:                Success;
 *      DSP_EHANDLE:            hDevObject is invalid.
 *      DSP_EINVALIDARG:        cChannels is 0.
 *      DSP_EMEMORY:            Insufficient memory for requested resources.
 *      CHNL_E_ISR:             Unable to plug channel ISR for configured IRQ.
 *      CHNL_E_MAXCHANNELS:     This manager cannot handle this many channels.
 *      CHNL_E_INVALIDIRQ:      Invalid IRQ number. Must be 0 <= bIRQ <= 15.
 *      CHNL_E_INVALIDWORDSIZE: Invalid DSP word size.  Must be > 0.
 *      CHNL_E_INVALIDMEMBASE:  Invalid base address for DSP communications.
 *      CHNL_E_MGREXISTS:       Channel manager already exists for this device.
 *  Requires:
 *      CHNL_Init(void) called.
 *      phChnlMgr != NULL.
 *      pMgrAttrs != NULL.
 *  Ensures:
 *      DSP_SOK:                Subsequent calls to CHNL_Create() for the same
 *                              board without an intervening call to
 *                              CHNL_Destroy() will fail.
 */
	extern DSP_STATUS CHNL_Create(OUT struct CHNL_MGR **phChnlMgr,
				      struct DEV_OBJECT *hDevObject,
				      IN CONST struct CHNL_MGRATTRS *pMgrAttrs);

/*
 *  ======== CHNL_Destroy ========
 *  Purpose:
 *      Close all open channels, and destroy the channel manager.
 *  Parameters:
 *      hChnlMgr:           Channel manager object.
 *  Returns:
 *      DSP_SOK:            Success.
 *      DSP_EHANDLE:        hChnlMgr was invalid.
 *  Requires:
 *      CHNL_Init(void) called.
 *  Ensures:
 *      DSP_SOK:            Cancels I/O on each open channel.
 *                          Closes each open channel.
 *                          CHNL_Create may subsequently be called for the
 *                          same board.
 */
	extern DSP_STATUS CHNL_Destroy(struct CHNL_MGR *hChnlMgr);

/*
 *  ======== CHNL_Exit ========
 *  Purpose:
 *      Discontinue usage of the CHNL module.
 *  Parameters:
 *  Returns:
 *  Requires:
 *      CHNL_Init(void) previously called.
 *  Ensures:
 *      Resources, if any acquired in CHNL_Init(void), are freed when the last
 *      client of CHNL calls CHNL_Exit(void).
 */
	extern void CHNL_Exit(void);


/*
 *  ======== CHNL_Init ========
 *  Purpose:
 *      Initialize the CHNL module's private state.
 *  Parameters:
 *  Returns:
 *      TRUE if initialized; FALSE if error occurred.
 *  Requires:
 *  Ensures:
 *      A requirement for each of the other public CHNL functions.
 */
	extern bool CHNL_Init(void);



#endif				/* CHNL_ */
