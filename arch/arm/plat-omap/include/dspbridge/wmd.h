/*
 * wmd.h
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
 *  ======== wmd.h ========
 *  Purpose:
 *      'Bridge mini driver entry point and interface function declarations.
 *
 *  Public Functions:
 *      WMD_DRV_Entry
 *
 *  Notes:
 *      The 'Bridge class driver obtains it's function interface to
 *      the 'Bridge mini driver via a call to WMD_DRV_Entry().
 *
 *      'Bridge Class Driver services exported to WMD's are initialized by the
 *      WCD on behalf of the WMD.
 *
 *      WMD function DBC Requires and Ensures are also made by the WCD on
 *      behalf of the WMD, to simplify the WMD code.
 *
 *! Revision History:
 *! ================
 *! 19-Apr-2004 sb  Aligned DMM definitions with Symbian
 *! 08-Mar-2004 sb  Added the Dynamic Memory Mapping APIs - WMD_BRD_MemMap/UnMap
 *! 01-Mar-2004 vp  Added filename argument to WMD_DRV_Entry function.
 *! 29-Aug-2002 map Added WMD_BRD_MemWrite()
 *! 26-Aug-2002 map Added WMD_BRD_MemCopy()
 *! 07-Jan-2002 ag  Added cBufSize to WMD_CHNL_AddIOReq().
 *! 05-Nov-2001 kc: Added error handling DEH functions.
 *! 06-Dec-2000 jeh Added uEventMask to WMD_MSG_RegisterNotify().
 *! 17-Nov-2000 jeh Added WMD_MSG and WMD_IO definitions.
 *! 01-Nov-2000 jeh Added more error codes to WMD_CHNL_RegisterNotify().
 *! 13-Oct-2000 jeh Added dwArg to WMD_CHNL_AddIOReq(), added WMD_CHNL_IDLE
 *!                 and WMD_CHNL_RegisterNotify for DSPStream support.
 *! 17-Jan-2000 rr: WMD_BRD_SETSTATE Added.
 *! 30-Jul-1997 gp: Split wmd IOCTL space into reserved and private.
 *! 07-Nov-1996 gp: Updated for code review.
 *! 18-Oct-1996 gp: Added WMD_E_HARDWARE return code from WMD_BRD_Monitor.
 *! 09-Sep-1996 gp: Subtly altered the semantics of WMD_CHNL_GetInfo().
 *! 02-Aug-1996 gp: Ensured on BRD_Start that interrupts to the PC are enabled.
 *! 11-Jul-1996 gp: Added CHNL interface. Note stronger DBC_Require conditions.
 *! 29-May-1996 gp: Removed WCD_ prefix from functions imported from WCD.LIB.
 *! 29-May-1996 gp: Made OUT param first in WMD_DEV_Create().
 *! 09-May-1996 gp: Created.
 */

#ifndef WMD_
#define WMD_

#include <dspbridge/brddefs.h>
#include <dspbridge/cfgdefs.h>
#include <dspbridge/chnlpriv.h>
#include <dspbridge/dehdefs.h>
#include <dspbridge/devdefs.h>
#include <dspbridge/iodefs.h>
#include <dspbridge/msgdefs.h>

/*
 *  Any IOCTLS at or above this value are reserved for standard WMD
 *  interfaces.
 */
#define WMD_RESERVEDIOCTLBASE   0x8000

/* Handle to mini-driver's private device context.  */
	struct WMD_DEV_CONTEXT;

/*---------------------------------------------------------------------------*/
/* 'Bridge MINI DRIVER FUNCTION TYPES                                        */
/*---------------------------------------------------------------------------*/

/*
 *  ======== WMD_BRD_Monitor ========
 *  Purpose:
 *      Bring the board to the BRD_IDLE (monitor) state.
 *  Parameters:
 *      hDevContext:    Handle to mini-driver defined device context.
 *  Returns:
 *      DSP_SOK:        Success.
 *      WMD_E_HARDWARE: A test of hardware assumptions/integrity failed.
 *      WMD_E_TIMEOUT:  Timeout occured waiting for a response from hardware.
 *      DSP_EFAIL:      Other, unspecified error.
 *  Requires:
 *      hDevContext != NULL
 *  Ensures:
 *      DSP_SOK:        Board is in BRD_IDLE state;
 *      else:           Board state is indeterminate.
 */
       typedef DSP_STATUS(
			   *WMD_BRD_MONITOR) (struct WMD_DEV_CONTEXT
			   *hDevContext);

/*
 *  ======== WMD_BRD_SETSTATE ========
 *  Purpose:
 *      Sets the Mini driver state
 *  Parameters:
 *      hDevContext:    Handle to mini-driver defined device info.
 *      ulBrdState:     Board state
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EFAIL:      Other, unspecified error.
 *  Requires:
 *      hDevContext != NULL;
 *      ulBrdState  <= BRD_LASTSTATE.
 *  Ensures:
 *      ulBrdState  <= BRD_LASTSTATE.
 *  Update the Board state to the specified state.
 */
       typedef DSP_STATUS(
			   *WMD_BRD_SETSTATE) (struct WMD_DEV_CONTEXT
			   *hDevContext, u32 ulBrdState);

/*
 *  ======== WMD_BRD_Start ========
 *  Purpose:
 *      Bring board to the BRD_RUNNING (start) state.
 *  Parameters:
 *      hDevContext:    Handle to mini-driver defined device context.
 *      dwDSPAddr:      DSP address at which to start execution.
 *  Returns:
 *      DSP_SOK:        Success.
 *      WMD_E_TIMEOUT:  Timeout occured waiting for a response from hardware.
 *      DSP_EFAIL:      Other, unspecified error.
 *  Requires:
 *      hDevContext != NULL
 *      Board is in monitor (BRD_IDLE) state.
 *  Ensures:
 *      DSP_SOK:        Board is in BRD_RUNNING state.
 *                      Interrupts to the PC are enabled.
 *      else:           Board state is indeterminate.
 */
       typedef DSP_STATUS(*WMD_BRD_START) (struct WMD_DEV_CONTEXT
						*hDevContext, u32 dwDSPAddr);

/*
 *  ======== WMD_BRD_MemCopy ========
 *  Purpose:
 *  Copy memory from one DSP address to another
 *  Parameters:
 *      pDevContext:    Pointer to context handle
 *  ulDspDestAddr:  DSP address to copy to
 *  ulDspSrcAddr:   DSP address to copy from
 *  ulNumBytes: Number of bytes to copy
 *  ulMemType:  What section of memory to copy to
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EFAIL:      Other, unspecified error.
 *  Requires:
 *      pDevContext != NULL
 *  Ensures:
 *      DSP_SOK:        Board is in BRD_RUNNING state.
 *                      Interrupts to the PC are enabled.
 *      else:           Board state is indeterminate.
 */
       typedef DSP_STATUS(*WMD_BRD_MEMCOPY) (struct WMD_DEV_CONTEXT
					     *hDevContext,
					     u32 ulDspDestAddr,
					     u32 ulDspSrcAddr,
					     u32 ulNumBytes, u32 ulMemType);
/*
 *  ======== WMD_BRD_MemWrite ========
 *  Purpose:
 *      Write a block of host memory into a DSP address, into a given memory
 *      space.  Unlike WMD_BRD_Write, this API does reset the DSP
 *  Parameters:
 *      hDevContext:    Handle to mini-driver defined device info.
 *      dwDSPAddr:      Address on DSP board (Destination).
 *      pHostBuf:       Pointer to host buffer (Source).
 *      ulNumBytes:     Number of bytes to transfer.
 *      ulMemType:      Memory space on DSP to which to transfer.
 *  Returns:
 *      DSP_SOK:        Success.
 *      WMD_E_TIMEOUT:  Timeout occured waiting for a response from hardware.
 *      DSP_EFAIL:      Other, unspecified error.
 *  Requires:
 *      hDevContext != NULL;
 *      pHostBuf != NULL.
 *  Ensures:
 */
       typedef DSP_STATUS(*WMD_BRD_MEMWRITE) (struct WMD_DEV_CONTEXT
					*hDevContext,
					IN u8 *pHostBuf,
					u32 dwDSPAddr, u32 ulNumBytes,
					u32 ulMemType);

/*
 *  ======== WMD_BRD_MemMap ========
 *  Purpose:
 *      Map a MPU memory region to a DSP/IVA memory space
 *  Parameters:
 *      hDevContext:    Handle to mini-driver defined device info.
 *      ulMpuAddr:      MPU memory region start address.
 *      ulVirtAddr:     DSP/IVA memory region u8 address.
 *      ulNumBytes:     Number of bytes to map.
 *      mapAttrs:       Mapping attributes (e.g. endianness).
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EFAIL:      Other, unspecified error.
 *  Requires:
 *      hDevContext != NULL;
 *  Ensures:
 */
       typedef DSP_STATUS(*WMD_BRD_MEMMAP) (struct WMD_DEV_CONTEXT
					*hDevContext, u32 ulMpuAddr,
					u32 ulVirtAddr, u32 ulNumBytes,
					u32 ulMapAttrs);

/*
 *  ======== WMD_BRD_MemUnMap ========
 *  Purpose:
 *      UnMap an MPU memory region from DSP/IVA memory space
 *  Parameters:
 *      hDevContext:    Handle to mini-driver defined device info.
 *      ulVirtAddr:     DSP/IVA memory region u8 address.
 *      ulNumBytes:     Number of bytes to unmap.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EFAIL:      Other, unspecified error.
 *  Requires:
 *      hDevContext != NULL;
 *  Ensures:
 */
       typedef DSP_STATUS(*WMD_BRD_MEMUNMAP) (struct WMD_DEV_CONTEXT
					*hDevContext,
					u32 ulVirtAddr,
					u32 ulNumBytes);

/*
 *  ======== WMD_BRD_Stop ========
 *  Purpose:
 *      Bring board to the BRD_STOPPED state.
 *  Parameters:
 *      hDevContext:    Handle to mini-driver defined device context.
 *  Returns:
 *      DSP_SOK:        Success.
 *      WMD_E_TIMEOUT:  Timeout occured waiting for a response from hardware.
 *      DSP_EFAIL:      Other, unspecified error.
 *  Requires:
 *      hDevContext != NULL
 *  Ensures:
 *      DSP_SOK:        Board is in BRD_STOPPED (stop) state;
 *                      Interrupts to the PC are disabled.
 *      else:           Board state is indeterminate.
 */
       typedef DSP_STATUS(*WMD_BRD_STOP) (struct WMD_DEV_CONTEXT
					*hDevContext);

/*
 *  ======== WMD_BRD_Status ========
 *  Purpose:
 *      Report the current state of the board.
 *  Parameters:
 *      hDevContext:    Handle to mini-driver defined device context.
 *      pdwState:       Ptr to BRD status variable.
 *  Returns:
 *      DSP_SOK:
 *  Requires:
 *      pdwState != NULL;
 *      hDevContext != NULL
 *  Ensures:
 *      *pdwState is one of {BRD_STOPPED, BRD_IDLE, BRD_RUNNING, BRD_UNKNOWN};
 */
       typedef DSP_STATUS(*
			   WMD_BRD_STATUS) (struct WMD_DEV_CONTEXT *hDevContext,
					    OUT BRD_STATUS * pdwState);

/*
 *  ======== WMD_BRD_Read ========
 *  Purpose:
 *      Read a block of DSP memory, from a given memory space, into a host
 *      buffer.
 *  Parameters:
 *      hDevContext:    Handle to mini-driver defined device info.
 *      pHostBuf:       Pointer to host buffer (Destination).
 *      dwDSPAddr:      Address on DSP board (Source).
 *      ulNumBytes:     Number of bytes to transfer.
 *      ulMemType:      Memory space on DSP from which to transfer.
 *  Returns:
 *      DSP_SOK:        Success.
 *      WMD_E_TIMEOUT:  Timeout occured waiting for a response from hardware.
 *      DSP_EFAIL:      Other, unspecified error.
 *  Requires:
 *      hDevContext != NULL;
 *      pHostBuf != NULL.
 *  Ensures:
 *  Will not write more than ulNumBytes bytes into pHostBuf.
 */
typedef DSP_STATUS(*WMD_BRD_READ) (struct WMD_DEV_CONTEXT *hDevContext,
						  OUT u8 *pHostBuf,
						  u32 dwDSPAddr,
						  u32 ulNumBytes,
						  u32 ulMemType);

/*
 *  ======== WMD_BRD_Write ========
 *  Purpose:
 *      Write a block of host memory into a DSP address, into a given memory
 *      space.
 *  Parameters:
 *      hDevContext:    Handle to mini-driver defined device info.
 *      dwDSPAddr:      Address on DSP board (Destination).
 *      pHostBuf:       Pointer to host buffer (Source).
 *      ulNumBytes:     Number of bytes to transfer.
 *      ulMemType:      Memory space on DSP to which to transfer.
 *  Returns:
 *      DSP_SOK:        Success.
 *      WMD_E_TIMEOUT:  Timeout occured waiting for a response from hardware.
 *      DSP_EFAIL:      Other, unspecified error.
 *  Requires:
 *      hDevContext != NULL;
 *      pHostBuf != NULL.
 *  Ensures:
 */
typedef DSP_STATUS(*WMD_BRD_WRITE)(struct WMD_DEV_CONTEXT *hDevContext,
						   IN u8 *pHostBuf,
						   u32 dwDSPAddr,
						   u32 ulNumBytes,
						   u32 ulMemType);

/*
 *  ======== WMD_CHNL_Create ========
 *  Purpose:
 *      Create a channel manager object, responsible for opening new channels
 *      and closing old ones for a given 'Bridge board.
 *  Parameters:
 *      phChnlMgr:      Location to store a channel manager object on output.
 *      hDevObject:     Handle to a device object.
 *      pMgrAttrs:      Channel manager attributes.
 *      pMgrAttrs->cChannels: Max channels
 *      pMgrAttrs->bIRQ:      Channel's I/O IRQ number.
 *      pMgrAttrs->fShared:   TRUE if the IRQ is shareable.
 *      pMgrAttrs->uWordSize: DSP Word size in equivalent PC bytes..
 *      pMgrAttrs->dwSMBase:  Base physical address of shared memory, if any.
 *      pMgrAttrs->uSMLength: Bytes of shared memory block.
 *  Returns:
 *      DSP_SOK:            Success;
 *      DSP_EMEMORY:        Insufficient memory for requested resources.
 *      CHNL_E_ISR:         Unable to plug ISR for given IRQ.
 *      CHNL_E_NOMEMMAP:    Couldn't map physical address to a virtual one.
 *  Requires:
 *      phChnlMgr != NULL.
 *      pMgrAttrs != NULL
 *      pMgrAttrs field are all valid:
 *          0 < cChannels <= CHNL_MAXCHANNELS.
 *          bIRQ <= 15.
 *          uWordSize > 0.
 *      IsValidHandle(hDevObject)
 *      No channel manager exists for this board.
 *  Ensures:
 */
       typedef DSP_STATUS(*WMD_CHNL_CREATE)(OUT struct CHNL_MGR
						    **phChnlMgr,
						    struct DEV_OBJECT
						    *hDevObject,
						    IN CONST struct
						    CHNL_MGRATTRS *pMgrAttrs);

/*
 *  ======== WMD_CHNL_Destroy ========
 *  Purpose:
 *      Close all open channels, and destroy the channel manager.
 *  Parameters:
 *      hChnlMgr:       Channel manager object.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    hChnlMgr was invalid.
 *  Requires:
 *  Ensures:
 *      DSP_SOK: Cancels I/O on each open channel. Closes each open channel.
 *          CHNL_Create may subsequently be called for the same device.
 */
       typedef DSP_STATUS(*WMD_CHNL_DESTROY) (struct CHNL_MGR
						      *hChnlMgr);
/*
 *  ======== WMD_DEH_Notify ========
 *  Purpose:
 *      When notified of DSP error, take appropriate action.
 *  Parameters:
 *      hDehMgr:        Handle to DEH manager object.
 *      ulEventMask:  Indicate the type of exception
 *      dwErrInfo:     Error information
 *  Returns:
 *
 *  Requires:
 *      hDehMgr != NULL;
 *     ulEventMask with a valid exception
 *  Ensures:
 */
       typedef void (*WMD_DEH_NOTIFY)(struct DEH_MGR *hDehMgr,
					u32 ulEventMask, u32 dwErrInfo);


/*
 *  ======== WMD_CHNL_Open ========
 *  Purpose:
 *      Open a new half-duplex channel to the DSP board.
 *  Parameters:
 *      phChnl:         Location to store a channel object handle.
 *      hChnlMgr:       Handle to channel manager, as returned by CHNL_GetMgr().
 *      uMode:          One of {CHNL_MODETODSP, CHNL_MODEFROMDSP} specifies
 *                      direction of data transfer.
 *      uChnlId:        If CHNL_PICKFREE is specified, the channel manager will
 *                      select a free channel id (default);
 *                      otherwise this field specifies the id of the channel.
 *      pAttrs:         Channel attributes.  Attribute fields are as follows:
 *      pAttrs->uIOReqs: Specifies the maximum number of I/O requests which can
 *                      be pending at any given time. All request packets are
 *                      preallocated when the channel is opened.
 *      pAttrs->hEvent: This field allows the user to supply an auto reset
 *                      event object for channel I/O completion notifications.
 *                      It is the responsibility of the user to destroy this
 *                      object AFTER closing the channel.
 *                      This channel event object can be retrieved using
 *                      CHNL_GetEventHandle().
 *      pAttrs->hReserved: The kernel mode handle of this event object.
 *
 *  Returns:
 *      DSP_SOK:                Success.
 *      DSP_EHANDLE:            hChnlMgr is invalid.
 *      DSP_EMEMORY:            Insufficient memory for requested resources.
 *      DSP_EINVALIDARG:        Invalid number of IOReqs.
 *      CHNL_E_OUTOFSTREAMS:    No free channels available.
 *      CHNL_E_BADCHANID:       Channel ID is out of range.
 *      CHNL_E_CHANBUSY:        Channel is in use.
 *      CHNL_E_NOIORPS:         No free IO request packets available for
 *                              queuing.
 *  Requires:
 *      phChnl != NULL.
 *      pAttrs != NULL.
 *      pAttrs->hEvent is a valid event handle.
 *      pAttrs->hReserved is the kernel mode handle for pAttrs->hEvent.
 *  Ensures:
 *      DSP_SOK:                *phChnl is a valid channel.
 *      else:                   *phChnl is set to NULL if (phChnl != NULL);
 */
       typedef DSP_STATUS(*WMD_CHNL_OPEN) (OUT struct CHNL_OBJECT
						   **phChnl,
						   struct CHNL_MGR *hChnlMgr,
						   CHNL_MODE uMode,
						   u32 uChnlId,
						   CONST IN OPTIONAL struct
						   CHNL_ATTRS *pAttrs);

/*
 *  ======== WMD_CHNL_Close ========
 *  Purpose:
 *      Ensures all pending I/O on this channel is cancelled, discards all
 *      queued I/O completion notifications, then frees the resources allocated
 *      for this channel, and makes the corresponding logical channel id
 *      available for subsequent use.
 *  Parameters:
 *      hChnl:          Handle to a channel object.
 *  Returns:
 *      DSP_SOK:        Success;
 *      DSP_EHANDLE:    Invalid hChnl.
 *  Requires:
 *      No thread must be blocked on this channel's I/O completion event.
 *  Ensures:
 *      DSP_SOK:        hChnl is no longer valid.
 */
       typedef DSP_STATUS(*WMD_CHNL_CLOSE) (struct CHNL_OBJECT *hChnl);

/*
 *  ======== WMD_CHNL_AddIOReq ========
 *  Purpose:
 *      Enqueue an I/O request for data transfer on a channel to the DSP.
 *      The direction (mode) is specified in the channel object. Note the DSP
 *      address is specified for channels opened in direct I/O mode.
 *  Parameters:
 *      hChnl:          Channel object handle.
 *      pHostBuf:       Host buffer address source.
 *      cBytes:         Number of PC bytes to transfer. A zero value indicates
 *                      that this buffer is the last in the output channel.
 *                      A zero value is invalid for an input channel.
 *!     cBufSize:       Actual buffer size in host bytes.
 *      dwDspAddr:      DSP address for transfer.  (Currently ignored).
 *      dwArg:          A user argument that travels with the buffer.
 *  Returns:
 *      DSP_SOK:        Success;
 *      DSP_EHANDLE:    Invalid hChnl.
 *      DSP_EPOINTER:   pHostBuf is invalid.
 *      CHNL_E_NOEOS:   User cannot mark EOS on an input channel.
 *      CHNL_E_CANCELLED: I/O has been cancelled on this channel.  No further
 *                      I/O is allowed.
 *      CHNL_E_EOS:     End of stream was already marked on a previous
 *                      IORequest on this channel.  No further I/O is expected.
 *      CHNL_E_BUFSIZE: Buffer submitted to this output channel is larger than
 *                      the size of the physical shared memory output window.
 *  Requires:
 *  Ensures:
 *      DSP_SOK: The buffer will be transferred if the channel is ready;
 *          otherwise, will be queued for transfer when the channel becomes
 *          ready.  In any case, notifications of I/O completion are
 *          asynchronous.
 *          If cBytes is 0 for an output channel, subsequent CHNL_AddIOReq's
 *          on this channel will fail with error code CHNL_E_EOS.  The
 *          corresponding IOC for this I/O request will have its status flag
 *          set to CHNL_IOCSTATEOS.
 */
       typedef DSP_STATUS(*WMD_CHNL_ADDIOREQ) (struct CHNL_OBJECT
						       *hChnl,
						       void *pHostBuf,
						       u32 cBytes,
						       u32 cBufSize,
						       OPTIONAL u32 dwDspAddr,
						       u32 dwArg);

/*
 *  ======== WMD_CHNL_GetIOC ========
 *  Purpose:
 *      Dequeue an I/O completion record, which contains information about the
 *      completed I/O request.
 *  Parameters:
 *      hChnl:          Channel object handle.
 *      dwTimeOut:      A value of CHNL_IOCNOWAIT will simply dequeue the
 *                      first available IOC.
 *      pIOC:           On output, contains host buffer address, bytes
 *                      transferred, and status of I/O completion.
 *      pIOC->status:   See chnldefs.h.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hChnl.
 *      DSP_EPOINTER:   pIOC is invalid.
 *      CHNL_E_NOIOC:   CHNL_IOCNOWAIT was specified as the dwTimeOut parameter
 *                      yet no I/O completions were queued.
 *  Requires:
 *      dwTimeOut == CHNL_IOCNOWAIT.
 *  Ensures:
 *      DSP_SOK: if there are any remaining IOC's queued before this call
 *          returns, the channel event object will be left in a signalled
 *          state.
 */
       typedef DSP_STATUS(*WMD_CHNL_GETIOC) (struct CHNL_OBJECT *hChnl,
						     u32 dwTimeOut,
						     OUT struct CHNL_IOC *pIOC);

/*
 *  ======== WMD_CHNL_CancelIO ========
 *  Purpose:
 *      Return all I/O requests to the client which have not yet been
 *      transferred.  The channel's I/O completion object is
 *      signalled, and all the I/O requests are queued as IOC's, with the
 *      status field set to CHNL_IOCSTATCANCEL.
 *      This call is typically used in abort situations, and is a prelude to
 *      CHNL_Close();
 *  Parameters:
 *      hChnl:          Channel object handle.
 *  Returns:
 *      DSP_SOK:        Success;
 *      DSP_EHANDLE:    Invalid hChnl.
 *  Requires:
 *  Ensures:
 *      Subsequent I/O requests to this channel will not be accepted.
 */
       typedef DSP_STATUS(*WMD_CHNL_CANCELIO) (struct CHNL_OBJECT
						       *hChnl);

/*
 *  ======== WMD_CHNL_FlushIO ========
 *  Purpose:
 *      For an output stream (to the DSP), indicates if any IO requests are in
 *      the output request queue.  For input streams (from the DSP), will
 *      cancel all pending IO requests.
 *  Parameters:
 *      hChnl:              Channel object handle.
 *      dwTimeOut:          Timeout value for flush operation.
 *  Returns:
 *      DSP_SOK:            Success;
 *      S_CHNLIOREQUEST:    Returned if any IORequests are in the output queue.
 *      DSP_EHANDLE:        Invalid hChnl.
 *  Requires:
 *  Ensures:
 *      DSP_SOK:            No I/O requests will be pending on this channel.
 */
       typedef DSP_STATUS(*WMD_CHNL_FLUSHIO) (struct CHNL_OBJECT *hChnl,
						      u32 dwTimeOut);

/*
 *  ======== WMD_CHNL_GetInfo ========
 *  Purpose:
 *      Retrieve information related to a channel.
 *  Parameters:
 *      hChnl:          Handle to a valid channel object, or NULL.
 *      pInfo:          Location to store channel info.
 *  Returns:
 *      DSP_SOK:        Success;
 *      DSP_EHANDLE:    Invalid hChnl.
 *      DSP_EPOINTER:   pInfo == NULL.
 *  Requires:
 *  Ensures:
 *      DSP_SOK:        pInfo points to a filled in CHNL_INFO struct,
 *                      if (pInfo != NULL).
 */
       typedef DSP_STATUS(*WMD_CHNL_GETINFO) (struct CHNL_OBJECT *hChnl,
						      OUT struct CHNL_INFO
						      *pChnlInfo);

/*
 *  ======== WMD_CHNL_GetMgrInfo ========
 *  Purpose:
 *      Retrieve information related to the channel manager.
 *  Parameters:
 *      hChnlMgr:           Handle to a valid channel manager, or NULL.
 *      uChnlID:            Channel ID.
 *      pMgrInfo:           Location to store channel manager info.
 *  Returns:
 *      DSP_SOK:            Success;
 *      DSP_EHANDLE:        Invalid hChnlMgr.
 *      DSP_EPOINTER:       pMgrInfo == NULL.
 *      CHNL_E_BADCHANID:   Invalid channel ID.
 *  Requires:
 *  Ensures:
 *      DSP_SOK:            pMgrInfo points to a filled in CHNL_MGRINFO
 *                          struct, if (pMgrInfo != NULL).
 */
       typedef DSP_STATUS(*WMD_CHNL_GETMGRINFO) (struct CHNL_MGR
							 *hChnlMgr,
							 u32 uChnlID,
							 OUT struct CHNL_MGRINFO
							 *pMgrInfo);

/*
 *  ======== WMD_CHNL_Idle ========
 *  Purpose:
 *      Idle a channel. If this is an input channel, or if this is an output
 *      channel and fFlush is TRUE, all currently enqueued buffers will be
 *      dequeued (data discarded for output channel).
 *      If this is an output channel and fFlush is FALSE, this function
 *      will block until all currently buffered data is output, or the timeout
 *      specified has been reached.
 *
 *  Parameters:
 *      hChnl:          Channel object handle.
 *      dwTimeOut:      If output channel and fFlush is FALSE, timeout value
 *                      to wait for buffers to be output. (Not used for
 *                      input channel).
 *      fFlush:         If output channel and fFlush is TRUE, discard any
 *                      currently buffered data. If FALSE, wait for currently
 *                      buffered data to be output, or timeout, whichever
 *                      occurs first. fFlush is ignored for input channel.
 *  Returns:
 *      DSP_SOK:            Success;
 *      DSP_EHANDLE:        Invalid hChnl.
 *      CHNL_E_WAITTIMEOUT: Timeout occured before channel could be idled.
 *  Requires:
 *  Ensures:
 */
       typedef DSP_STATUS(*WMD_CHNL_IDLE) (struct CHNL_OBJECT *hChnl,
						   u32 dwTimeOut,
						   bool fFlush);

/*
 *  ======== WMD_CHNL_RegisterNotify ========
 *  Purpose:
 *      Register for notification of events on a channel.
 *  Parameters:
 *      hChnl:          Channel object handle.
 *      uEventMask:     Type of events to be notified about: IO completion
 *                      (DSP_STREAMIOCOMPLETION) or end of stream
 *                      (DSP_STREAMDONE).
 *      uNotifyType:    DSP_SIGNALEVENT.
 *      hNotification:  Handle of a DSP_NOTIFICATION object.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EMEMORY:    Insufficient memory.
 *      DSP_EVALUE:     uEventMask is 0 and hNotification was not
 *                      previously registered.
 *      DSP_EHANDLE:    NULL hNotification, hNotification event name
 *                      too long, or hNotification event name NULL.
 *  Requires:
 *      Valid hChnl.
 *      hNotification != NULL.
 *      (uEventMask & ~(DSP_STREAMIOCOMPLETION | DSP_STREAMDONE)) == 0.
 *      uNotifyType == DSP_SIGNALEVENT.
 *  Ensures:
 */
       typedef DSP_STATUS(*WMD_CHNL_REGISTERNOTIFY)
				(struct CHNL_OBJECT *hChnl,
				u32 uEventMask,
				u32 uNotifyType,
				struct DSP_NOTIFICATION *hNotification);

/*
 *  ======== WMD_DEV_Create ========
 *  Purpose:
 *      Complete creation of the device object for this board.
 *  Parameters:
 *      phDevContext:   Ptr to location to store a WMD device context.
 *      hDevObject:     Handle to a Device Object, created and managed by WCD.
 *      pConfig:        Ptr to configuration parameters provided by the Windows
 *                      Configuration Manager during device loading.
 *      pDspConfig:     DSP resources, as specified in the registry key for this
 *                      device.
 *  Returns:
 *      DSP_SOK:            Success.
 *      DSP_EMEMORY:        Unable to allocate memory for device context.
 *      WMD_E_BADCONFIG:    One or more of the host or DSP configuration
 *                          parameters did not satisfy hardware assumptions
 *                          made by this WMD.
 *  Requires:
 *      phDevContext != NULL;
 *      hDevObject != NULL;
 *      pConfig != NULL;
 *      pDspConfig != NULL;
 *      Fields in pConfig and pDspConfig contain valid values.
 *  Ensures:
 *      DSP_SOK:        All mini-driver specific DSP resource and other
 *                      board context has been allocated.
 *      DSP_EMEMORY:    WMD failed to allocate resources.
 *                      Any acquired resources have been freed.  The WCD will
 *                      not call WMD_DEV_Destroy() if WMD_DEV_Create() fails.
 *  Details:
 *      Called during the CONFIGMG's Device_Init phase. Based on host and
 *      DSP configuration information, create a board context, a handle to
 *      which is passed into other WMD BRD and CHNL functions.  The
 *      board context contains state information for the device. Since the
 *      addresses of all IN pointer parameters may be invalid when this
 *      function returns, they must not be stored into the device context
 *      structure.
 */
       typedef DSP_STATUS(*WMD_DEV_CREATE) (OUT struct WMD_DEV_CONTEXT
						    **phDevContext,
						    struct DEV_OBJECT
						    *hDevObject,
						    IN CONST struct CFG_HOSTRES
						    *pConfig,
						    IN CONST struct CFG_DSPRES
						    *pDspConfig);

/*
 *  ======== WMD_DEV_Ctrl ========
 *  Purpose:
 *      Mini-driver specific interface.
 *  Parameters:
 *      hDevContext:    Handle to mini-driver defined device info.
 *      dwCmd:          WMD defined command code.
 *      pArgs:          Pointer to an arbitrary argument structure.
 *  Returns:
 *      DSP_SOK or DSP_EFAIL. Actual command error codes should be passed back
 *      in the pArgs structure, and are defined by the WMD implementor.
 *  Requires:
 *      All calls are currently assumed to be synchronous.  There are no
 *      IOCTL completion routines provided.
 *  Ensures:
 */
typedef DSP_STATUS(*WMD_DEV_CTRL)(struct WMD_DEV_CONTEXT *hDevContext,
					u32 dwCmd,
					IN OUT void *pArgs);

/*
 *  ======== WMD_DEV_Destroy ========
 *  Purpose:
 *      Deallocate WMD device extension structures and all other resources
 *      acquired by the mini-driver.
 *      No calls to other mini driver functions may subsequently
 *      occur, except for WMD_DEV_Create().
 *  Parameters:
 *      hDevContext:    Handle to mini-driver defined device information.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EFAIL:      Failed to release a resource previously acquired.
 *  Requires:
 *      hDevContext != NULL;
 *  Ensures:
 *      DSP_SOK: Device context is freed.
 */
       typedef DSP_STATUS(*WMD_DEV_DESTROY) (struct WMD_DEV_CONTEXT
					     *hDevContext);

/*
 *  ======== WMD_DEH_Create ========
 *  Purpose:
 *      Create an object that manages DSP exceptions from the GPP.
 *  Parameters:
 *      phDehMgr:       Location to store DEH manager on output.
 *      hDevObject:     Handle to DEV object.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EMEMORY:    Memory allocation failure.
 *      DSP_EFAIL:      Creation failed.
 *  Requires:
 *      hDevObject != NULL;
 *      phDehMgr != NULL;
 *  Ensures:
 */
       typedef DSP_STATUS(*WMD_DEH_CREATE) (OUT struct DEH_MGR
						    **phDehMgr,
						    struct DEV_OBJECT
						    *hDevObject);

/*
 *  ======== WMD_DEH_Destroy ========
 *  Purpose:
 *      Destroy the DEH object.
 *  Parameters:
 *      hDehMgr:        Handle to DEH manager object.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EFAIL:      Destroy failed.
 *  Requires:
 *      hDehMgr != NULL;
 *  Ensures:
 */
       typedef DSP_STATUS(*WMD_DEH_DESTROY) (struct DEH_MGR *hDehMgr);

/*
 *  ======== WMD_DEH_RegisterNotify ========
 *  Purpose:
 *      Register for DEH event notification.
 *  Parameters:
 *      hDehMgr:        Handle to DEH manager object.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EFAIL:      Destroy failed.
 *  Requires:
 *      hDehMgr != NULL;
 *  Ensures:
 */
       typedef DSP_STATUS(*WMD_DEH_REGISTERNOTIFY)
				(struct DEH_MGR *hDehMgr,
				u32 uEventMask, u32 uNotifyType,
				struct DSP_NOTIFICATION *hNotification);

/*
 *  ======== WMD_DEH_GetInfo ========
 *  Purpose:
 *      Get DSP exception info.
 *  Parameters:
 *      phDehMgr:       Location to store DEH manager on output.
 *      pErrInfo:       Ptr to error info structure.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EFAIL:      Creation failed.
 *  Requires:
 *      phDehMgr != NULL;
 *      pErrorInfo != NULL;
 *  Ensures:
 */
       typedef DSP_STATUS(*WMD_DEH_GETINFO) (struct DEH_MGR *phDehMgr,
					struct DSP_ERRORINFO *pErrInfo);

/*
 *  ======== WMD_IO_Create ========
 *  Purpose:
 *      Create an object that manages I/O between CHNL and MSG.
 *  Parameters:
 *      phIOMgr:        Location to store IO manager on output.
 *      hChnlMgr:       Handle to channel manager.
 *      hMsgMgr:        Handle to message manager.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EMEMORY:    Memory allocation failure.
 *      DSP_EFAIL:      Creation failed.
 *  Requires:
 *      hDevObject != NULL;
 *      Channel manager already created;
 *      Message manager already created;
 *      pMgrAttrs != NULL;
 *      phIOMgr != NULL;
 *  Ensures:
 */
       typedef DSP_STATUS(*WMD_IO_CREATE) (OUT struct IO_MGR **phIOMgr,
					struct DEV_OBJECT *hDevObject,
					IN CONST struct IO_ATTRS *pMgrAttrs);

/*
 *  ======== WMD_IO_Destroy ========
 *  Purpose:
 *      Destroy object created in WMD_IO_Create.
 *  Parameters:
 *      hIOMgr:         IO Manager.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EMEMORY:    Memory allocation failure.
 *      DSP_EFAIL:      Creation failed.
 *  Requires:
 *      Valid hIOMgr;
 *  Ensures:
 */
       typedef DSP_STATUS(*WMD_IO_DESTROY) (struct IO_MGR *hIOMgr);

/*
 *  ======== WMD_IO_OnLoaded ========
 *  Purpose:
 *      Called whenever a program is loaded to update internal data. For
 *      example, if shared memory is used, this function would update the
 *      shared memory location and address.
 *  Parameters:
 *      hIOMgr:     IO Manager.
 *  Returns:
 *      DSP_SOK:    Success.
 *      DSP_EFAIL:  Internal failure occurred.
 *  Requires:
 *      Valid hIOMgr;
 *  Ensures:
 */
       typedef DSP_STATUS(*WMD_IO_ONLOADED) (struct IO_MGR *hIOMgr);

/*
 *  ======== WMD_IO_GETPROCLOAD ========
 *  Purpose:
 *      Called to get the Processor's current and predicted load
 *  Parameters:
 *      hIOMgr:     IO Manager.
 *      pProcLoadStat   Processor Load statistics
 *  Returns:
 *      DSP_SOK:    Success.
 *      DSP_EFAIL:  Internal failure occurred.
 *  Requires:
 *      Valid hIOMgr;
 *  Ensures:
 */
       typedef DSP_STATUS(*WMD_IO_GETPROCLOAD)(struct IO_MGR *hIOMgr,
			   struct DSP_PROCLOADSTAT *pProcLoadStat);

/*
 *  ======== WMD_MSG_Create ========
 *  Purpose:
 *      Create an object to manage message queues. Only one of these objects
 *      can exist per device object.
 *  Parameters:
 *      phMsgMgr:           Location to store MSG manager on output.
 *      hDevObject:         Handle to a device object.
 *      msgCallback:        Called whenever an RMS_EXIT message is received.
 *  Returns:
 *      DSP_SOK:            Success.
 *      DSP_EMEMORY:        Insufficient memory.
 *  Requires:
 *      phMsgMgr != NULL.
 *      msgCallback != NULL.
 *      hDevObject != NULL.
 *  Ensures:
 */
       typedef DSP_STATUS(*WMD_MSG_CREATE)
				(OUT struct MSG_MGR **phMsgMgr,
				struct DEV_OBJECT *hDevObject,
				MSG_ONEXIT msgCallback);

/*
 *  ======== WMD_MSG_CreateQueue ========
 *  Purpose:
 *      Create a MSG queue for sending or receiving messages from a Message
 *      node on the DSP.
 *  Parameters:
 *      hMsgMgr:            MSG queue manager handle returned from
 *                          WMD_MSG_Create.
 *      phMsgQueue:         Location to store MSG queue on output.
 *      dwId:               Identifier for messages (node environment pointer).
 *      uMaxMsgs:           Max number of simultaneous messages for the node.
 *      h:                  Handle passed to hMsgMgr->msgCallback().
 *  Returns:
 *      DSP_SOK:            Success.
 *      DSP_EMEMORY:        Insufficient memory.
 *  Requires:
 *      phMsgQueue != NULL.
 *      h != NULL.
 *      uMaxMsgs > 0.
 *  Ensures:
 *      phMsgQueue !=NULL <==> DSP_SOK.
 */
       typedef DSP_STATUS(*WMD_MSG_CREATEQUEUE)
				(struct MSG_MGR *hMsgMgr,
				OUT struct MSG_QUEUE **phMsgQueue,
				u32 dwId, u32 uMaxMsgs, HANDLE h);

/*
 *  ======== WMD_MSG_Delete ========
 *  Purpose:
 *      Delete a MSG manager allocated in WMD_MSG_Create().
 *  Parameters:
 *      hMsgMgr:    Handle returned from WMD_MSG_Create().
 *  Returns:
 *  Requires:
 *      Valid hMsgMgr.
 *  Ensures:
 */
       typedef void(*WMD_MSG_DELETE) (struct MSG_MGR *hMsgMgr);

/*
 *  ======== WMD_MSG_DeleteQueue ========
 *  Purpose:
 *      Delete a MSG queue allocated in WMD_MSG_CreateQueue.
 *  Parameters:
 *      hMsgQueue:  Handle to MSG queue returned from
 *                  WMD_MSG_CreateQueue.
 *  Returns:
 *  Requires:
 *      Valid hMsgQueue.
 *  Ensures:
 */
       typedef void(*WMD_MSG_DELETEQUEUE) (struct MSG_QUEUE *hMsgQueue);

/*
 *  ======== WMD_MSG_Get ========
 *  Purpose:
 *      Get a message from a MSG queue.
 *  Parameters:
 *      hMsgQueue:     Handle to MSG queue returned from
 *                     WMD_MSG_CreateQueue.
 *      pMsg:          Location to copy message into.
 *      uTimeout:      Timeout to wait for a message.
 *  Returns:
 *      DSP_SOK:       Success.
 *      DSP_ETIMEOUT:  Timeout occurred.
 *      DSP_EFAIL:     No frames available for message (uMaxMessages too
 *                     small).
 *  Requires:
 *      Valid hMsgQueue.
 *      pMsg != NULL.
 *  Ensures:
 */
       typedef DSP_STATUS(*WMD_MSG_GET) (struct MSG_QUEUE *hMsgQueue,
						 struct DSP_MSG *pMsg,
						 u32 uTimeout);

/*
 *  ======== WMD_MSG_Put ========
 *  Purpose:
 *      Put a message onto a MSG queue.
 *  Parameters:
 *      hMsgQueue:      Handle to MSG queue returned from
 *                      WMD_MSG_CreateQueue.
 *      pMsg:           Pointer to message.
 *      uTimeout:       Timeout to wait for a message.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_ETIMEOUT:   Timeout occurred.
 *      DSP_EFAIL:      No frames available for message (uMaxMessages too
 *                      small).
 *  Requires:
 *      Valid hMsgQueue.
 *      pMsg != NULL.
 *  Ensures:
 */
       typedef DSP_STATUS(*WMD_MSG_PUT) (struct MSG_QUEUE *hMsgQueue,
						 IN CONST struct DSP_MSG *pMsg,
						 u32 uTimeout);

/*
 *  ======== WMD_MSG_RegisterNotify ========
 *  Purpose:
 *      Register notification for when a message is ready.
 *  Parameters:
 *      hMsgQueue:      Handle to MSG queue returned from
 *                      WMD_MSG_CreateQueue.
 *      uEventMask:     Type of events to be notified about: Must be
 *                      DSP_NODEMESSAGEREADY, or 0 to unregister.
 *      uNotifyType:    DSP_SIGNALEVENT.
 *      hNotification:  Handle of notification object.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EMEMORY:    Insufficient memory.
 *  Requires:
 *      Valid hMsgQueue.
 *      hNotification != NULL.
 *      uNotifyType == DSP_SIGNALEVENT.
 *      uEventMask == DSP_NODEMESSAGEREADY || uEventMask == 0.
 *  Ensures:
 */
       typedef DSP_STATUS(*WMD_MSG_REGISTERNOTIFY)
				(struct MSG_QUEUE *hMsgQueue,
				u32 uEventMask, u32 uNotifyType,
				struct DSP_NOTIFICATION *hNotification);

/*
 *  ======== WMD_MSG_SetQueueId ========
 *  Purpose:
 *      Set message queue id to node environment. Allows WMD_MSG_CreateQueue
 *      to be called in NODE_Allocate, before the node environment is known.
 *  Parameters:
 *      hMsgQueue:  Handle to MSG queue returned from
 *                  WMD_MSG_CreateQueue.
 *      dwId:       Node environment pointer.
 *  Returns:
 *  Requires:
 *      Valid hMsgQueue.
 *      dwId != 0.
 *  Ensures:
 */
       typedef void(*WMD_MSG_SETQUEUEID) (struct MSG_QUEUE *hMsgQueue,
						  u32 dwId);

/*
 *  'Bridge Mini Driver (WMD) interface function table.
 *
 *  The information in this table is filled in by the specific mini-driver,
 *  and copied into the 'Bridge class driver's own space.  If any interface
 *  function field is set to a value of NULL, then the class driver will
 *  consider that function not implemented, and return the error code
 *  DSP_ENOTIMPL when a WMD client attempts to call that function.
 *
 *  This function table contains WCD version numbers, which are used by the
 *  WMD loader to help ensure backwards compatility between older WMD's and a
 *  newer 'Bridge Class Driver.  These must be set to WCD_MAJOR_VERSION
 *  and WCD_MINOR_VERSION, respectively.
 *
 *  A mini-driver need not export a CHNL interface.  In this case, *all* of
 *  the WMD_CHNL_* entries must be set to NULL.
 */
	struct WMD_DRV_INTERFACE {
		u32 dwWCDMajorVersion;	/* Set to WCD_MAJOR_VERSION. */
		u32 dwWCDMinorVersion;	/* Set to WCD_MINOR_VERSION. */
		WMD_DEV_CREATE pfnDevCreate;	/* Create device context     */
		WMD_DEV_DESTROY pfnDevDestroy;	/* Destroy device context    */
		WMD_DEV_CTRL pfnDevCntrl;	/* Optional vendor interface */
		WMD_BRD_MONITOR pfnBrdMonitor;	/* Load and/or start monitor */
		WMD_BRD_START pfnBrdStart;	/* Start DSP program.        */
		WMD_BRD_STOP pfnBrdStop;	/* Stop/reset board.         */
		WMD_BRD_STATUS pfnBrdStatus;	/* Get current board status. */
		WMD_BRD_READ pfnBrdRead;	/* Read board memory         */
		WMD_BRD_WRITE pfnBrdWrite;	/* Write board memory.       */
		WMD_BRD_SETSTATE pfnBrdSetState;  /* Sets the Board State */
		WMD_BRD_MEMCOPY pfnBrdMemCopy;	 /* Copies DSP Memory         */
		WMD_BRD_MEMWRITE pfnBrdMemWrite; /* Write DSP Memory w/o halt */
		WMD_BRD_MEMMAP pfnBrdMemMap;	 /* Maps MPU mem to DSP mem   */
		WMD_BRD_MEMUNMAP pfnBrdMemUnMap; /* Unmaps MPU mem to DSP mem */
		WMD_CHNL_CREATE pfnChnlCreate;	 /* Create channel manager.   */
		WMD_CHNL_DESTROY pfnChnlDestroy; /* Destroy channel manager.  */
		WMD_CHNL_OPEN pfnChnlOpen;	 /* Create a new channel.     */
		WMD_CHNL_CLOSE pfnChnlClose;	 /* Close a channel.          */
		WMD_CHNL_ADDIOREQ pfnChnlAddIOReq; /* Req I/O on a channel. */
		WMD_CHNL_GETIOC pfnChnlGetIOC;	 /* Wait for I/O completion.  */
		WMD_CHNL_CANCELIO pfnChnlCancelIO; /* Cancl I/O on a channel. */
		WMD_CHNL_FLUSHIO pfnChnlFlushIO;	/* Flush I/O.         */
		WMD_CHNL_GETINFO pfnChnlGetInfo; /* Get channel specific info */
		/* Get channel manager info. */
		WMD_CHNL_GETMGRINFO pfnChnlGetMgrInfo;
		WMD_CHNL_IDLE pfnChnlIdle;	/* Idle the channel */
		/* Register for notif. */
		WMD_CHNL_REGISTERNOTIFY pfnChnlRegisterNotify;
		WMD_DEH_CREATE pfnDehCreate;	/* Create DEH manager */
		WMD_DEH_DESTROY pfnDehDestroy;	/* Destroy DEH manager */
		WMD_DEH_NOTIFY pfnDehNotify;    /* Notify of DSP error */
		/* register for deh notif. */
		WMD_DEH_REGISTERNOTIFY pfnDehRegisterNotify;
		WMD_DEH_GETINFO pfnDehGetInfo;	/* register for deh notif. */
		WMD_IO_CREATE pfnIOCreate;	/* Create IO manager */
		WMD_IO_DESTROY pfnIODestroy;	/* Destroy IO manager */
		WMD_IO_ONLOADED pfnIOOnLoaded;	/* Notify of program loaded */
		/* Get Processor's current and predicted load */
		WMD_IO_GETPROCLOAD pfnIOGetProcLoad;
		WMD_MSG_CREATE pfnMsgCreate;	/* Create message manager */
		/* Create message queue */
		WMD_MSG_CREATEQUEUE pfnMsgCreateQueue;
		WMD_MSG_DELETE pfnMsgDelete;	/* Delete message manager */
		/* Delete message queue */
		WMD_MSG_DELETEQUEUE pfnMsgDeleteQueue;
		WMD_MSG_GET pfnMsgGet;	/* Get a message */
		WMD_MSG_PUT pfnMsgPut;	/* Send a message */
		/* Register for notif. */
		WMD_MSG_REGISTERNOTIFY pfnMsgRegisterNotify;
		/* Set message queue id */
		WMD_MSG_SETQUEUEID pfnMsgSetQueueId;
	} ;

/*
 *  ======== WMD_DRV_Entry ========
 *  Purpose:
 *      Registers WMD functions with the class driver. Called only once
 *      by the WCD.  The caller will first check WCD version compatibility, and
 *      then copy the interface functions into its own memory space.
 *  Parameters:
 *      ppDrvInterface  Pointer to a location to receive a pointer to the
 *                      mini driver interface.
 *  Returns:
 *  Requires:
 *      The code segment this function resides in must expect to be discarded
 *      after completion.
 *  Ensures:
 *      ppDrvInterface pointer initialized to WMD's function interface.
 *      No system resources are acquired by this function.
 *  Details:
 *      Win95: Called during the Device_Init phase.
 */
       void WMD_DRV_Entry(OUT struct WMD_DRV_INTERFACE **ppDrvInterface,
				 IN CONST char *pstrWMDFileName);

#endif				/* WMD_ */
