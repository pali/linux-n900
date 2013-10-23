/*
 * strm.h
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
 *  ======== strm.h ========
 *  Description:
 *      DSPBridge Stream Manager.
 *
 *  Public Functions:
 *      STRM_AllocateBuffer
 *      STRM_Close
 *      STRM_Create
 *      STRM_Delete
 *      STRM_Exit
 *      STRM_FreeBuffer
 *      STRM_GetEventHandle
 *      STRM_GetInfo
 *      STRM_Idle
 *      STRM_Init
 *      STRM_Issue
 *      STRM_Open
 *      STRM_PrepareBuffer
 *      STRM_Reclaim
 *      STRM_RegisterNotify
 *      STRM_Select
 *      STRM_UnprepareBuffer
 *
 *  Notes:
 *
 *! Revision History:
 *! =================
 *! 15-Nov-2001 ag  Changed DSP_STREAMINFO to STRM_INFO in STRM_GetInfo().
 *!                 Added DSP_ESIZE error to STRM_AllocateBuffer().
 *! 07-Jun-2001 sg  Made DSPStream_AllocateBuffer fxn name plural.
 *! 10-May-2001 jeh Code review cleanup.
 *! 13-Feb-2001 kc  DSP/BIOS Bridge name updates.
 *! 06-Feb-2001 kc  Updated DBC_Ensure for STRM_Select().
 *! 23-Oct-2000 jeh Allow NULL STRM_ATTRS passed to STRM_Open().
 *! 25-Sep-2000 jeh Created.
 */

#ifndef STRM_
#define STRM_

#include <dspbridge/dev.h>

#include <dspbridge/strmdefs.h>

/*
 *  ======== STRM_AllocateBuffer ========
 *  Purpose:
 *      Allocate data buffer(s) for use with a stream.
 *  Parameter:
 *      hStrm:          Stream handle returned from STRM_Open().
 *      uSize:          Size (GPP bytes) of the buffer(s).
 *      uNumBufs:       Number of buffers to allocate.
 *      apBuffer:       Array to hold buffer addresses.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hStrm.
 *      DSP_EMEMORY:    Insufficient memory.
 *      DSP_EFAIL:      Failure occurred, unable to allocate buffers.
 *      DSP_ESIZE:      uSize must be > 0 bytes.
 *  Requires:
 *      STRM_Init(void) called.
 *      apBuffer != NULL.
 *  Ensures:
 */
	extern DSP_STATUS STRM_AllocateBuffer(struct STRM_OBJECT *hStrm,
					      u32 uSize,
					      OUT u8 **apBuffer,
					      u32 uNumBufs);

/*
 *  ======== STRM_Close ========
 *  Purpose:
 *      Close a stream opened with STRM_Open().
 *  Parameter:
 *      hStrm:          Stream handle returned from STRM_Open().
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hStrm.
 *      DSP_EPENDING:   Some data buffers issued to the stream have not
 *                      been reclaimed.
 *      DSP_EFAIL:      Failure to close stream.
 *  Requires:
 *      STRM_Init(void) called.
 *  Ensures:
 */
	extern DSP_STATUS STRM_Close(struct STRM_OBJECT *hStrm);

/*
 *  ======== STRM_Create ========
 *  Purpose:
 *      Create a STRM manager object. This object holds information about the
 *      device needed to open streams.
 *  Parameters:
 *      phStrmMgr:      Location to store handle to STRM manager object on
 *                      output.
 *      hDev:           Device for this processor.
 *  Returns:
 *      DSP_SOK:        Success;
 *      DSP_EMEMORY:    Insufficient memory for requested resources.
 *      DSP_EFAIL:      General failure.
 *  Requires:
 *      STRM_Init(void) called.
 *      phStrmMgr != NULL.
 *      hDev != NULL.
 *  Ensures:
 *      DSP_SOK:        Valid *phStrmMgr.
 *      error:          *phStrmMgr == NULL.
 */
	extern DSP_STATUS STRM_Create(OUT struct STRM_MGR **phStrmMgr,
				      struct DEV_OBJECT *hDev);

/*
 *  ======== STRM_Delete ========
 *  Purpose:
 *      Delete the STRM Object.
 *  Parameters:
 *      hStrmMgr:       Handle to STRM manager object from STRM_Create.
 *  Returns:
 *  Requires:
 *      STRM_Init(void) called.
 *      Valid hStrmMgr.
 *  Ensures:
 *      hStrmMgr is not valid.
 */
	extern void STRM_Delete(struct STRM_MGR *hStrmMgr);

/*
 *  ======== STRM_Exit ========
 *  Purpose:
 *      Discontinue usage of STRM module.
 *  Parameters:
 *  Returns:
 *  Requires:
 *      STRM_Init(void) successfully called before.
 *  Ensures:
 */
	extern void STRM_Exit(void);

/*
 *  ======== STRM_FreeBuffer ========
 *  Purpose:
 *      Free buffer(s) allocated with STRM_AllocateBuffer.
 *  Parameter:
 *      hStrm:          Stream handle returned from STRM_Open().
 *      apBuffer:       Array containing buffer addresses.
 *      uNumBufs:       Number of buffers to be freed.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid stream handle.
 *      DSP_EFAIL:      Failure occurred, unable to free buffers.
 *  Requires:
 *      STRM_Init(void) called.
 *      apBuffer != NULL.
 *  Ensures:
 */
	extern DSP_STATUS STRM_FreeBuffer(struct STRM_OBJECT *hStrm,
					  u8 **apBuffer, u32 uNumBufs);

/*
 *  ======== STRM_GetEventHandle ========
 *  Purpose:
 *      Get stream's user event handle. This function is used when closing
 *      a stream, so the event can be closed.
 *  Parameter:
 *      hStrm:          Stream handle returned from STRM_Open().
 *      phEvent:        Location to store event handle on output.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hStrm.
 *  Requires:
 *      STRM_Init(void) called.
 *      phEvent != NULL.
 *  Ensures:
 */
	extern DSP_STATUS STRM_GetEventHandle(struct STRM_OBJECT *hStrm,
					      OUT HANDLE *phEvent);

/*
 *  ======== STRM_GetInfo ========
 *  Purpose:
 *      Get information about a stream. User's DSP_STREAMINFO is contained
 *      in STRM_INFO struct. STRM_INFO also contains Bridge private info.
 *  Parameters:
 *      hStrm:              Stream handle returned from STRM_Open().
 *      pStreamInfo:        Location to store stream info on output.
 *      uSteamInfoSize:     Size of user's DSP_STREAMINFO structure.
 *  Returns:
 *      DSP_SOK:            Success.
 *      DSP_EHANDLE:        Invalid hStrm.
 *      DSP_ESIZE:          uStreamInfoSize < sizeof(DSP_STREAMINFO).
 *      DSP_EFAIL:          Unable to get stream info.
 *  Requires:
 *      STRM_Init(void) called.
 *      pStreamInfo != NULL.
 *  Ensures:
 */
	extern DSP_STATUS STRM_GetInfo(struct STRM_OBJECT *hStrm,
				       OUT struct STRM_INFO *pStreamInfo,
				       u32 uStreamInfoSize);

/*
 *  ======== STRM_Idle ========
 *  Purpose:
 *      Idle a stream and optionally flush output data buffers.
 *      If this is an output stream and fFlush is TRUE, all data currently
 *      enqueued will be discarded.
 *      If this is an output stream and fFlush is FALSE, this function
 *      will block until all currently buffered data is output, or the timeout
 *      specified has been reached.
 *      After a successful call to STRM_Idle(), all buffers can immediately
 *      be reclaimed.
 *  Parameters:
 *      hStrm:          Stream handle returned from STRM_Open().
 *      fFlush:         If TRUE, discard output buffers.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hStrm.
 *      DSP_ETIMEOUT:   A timeout occurred before the stream could be idled.
 *      DSP_ERESTART:   A critical error occurred, DSP is being restarted.
 *      DSP_EFAIL:      Unable to idle stream.
 *  Requires:
 *      STRM_Init(void) called.
 *  Ensures:
 */
	extern DSP_STATUS STRM_Idle(struct STRM_OBJECT *hStrm, bool fFlush);

/*
 *  ======== STRM_Init ========
 *  Purpose:
 *      Initialize the STRM module.
 *  Parameters:
 *  Returns:
 *      TRUE if initialization succeeded, FALSE otherwise.
 *  Requires:
 *  Ensures:
 */
	extern bool STRM_Init(void);

/*
 *  ======== STRM_Issue ========
 *  Purpose:
 *      Send a buffer of data to a stream.
 *  Parameters:
 *      hStrm:              Stream handle returned from STRM_Open().
 *      pBuf:               Pointer to buffer of data to be sent to the stream.
 *      ulBytes:            Number of bytes of data in the buffer.
 *      ulBufSize:          Actual buffer size in bytes.
 *      dwArg:              A user argument that travels with the buffer.
 *  Returns:
 *      DSP_SOK:            Success.
 *      DSP_EHANDLE:        Invalid hStrm.
 *      DSP_ESTREAMFULL:    The stream is full.
 *      DSP_EFAIL:          Failure occurred, unable to issue buffer.
 *  Requires:
 *      STRM_Init(void) called.
 *      pBuf != NULL.
 *  Ensures:
 */
	extern DSP_STATUS STRM_Issue(struct STRM_OBJECT *hStrm, IN u8 *pBuf,
				     u32 ulBytes, u32 ulBufSize,
				     IN u32 dwArg);

/*
 *  ======== STRM_Open ========
 *  Purpose:
 *      Open a stream for sending/receiving data buffers to/from a task of
 *      DAIS socket node on the DSP.
 *  Parameters:
 *      hNode:          Node handle returned from NODE_Allocate().
 *      uDir:           DSP_TONODE or DSP_FROMNODE.
 *      uIndex:         Stream index.
 *      pAttr:          Pointer to structure containing attributes to be
 *                      applied to stream. Cannot be NULL.
 *      phStrm:         Location to store stream handle on output.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hNode.
 *      DSP_EDIRECTION: Invalid uDir.
 *      DSP_EVALUE:     Invalid uIndex.
 *      DSP_ENODETYPE:  hNode is not a task or DAIS socket node.
 *      DSP_EFAIL:      Unable to open stream.
 *  Requires:
 *      STRM_Init(void) called.
 *      phStrm != NULL.
 *      pAttr != NULL.
 *  Ensures:
 *      DSP_SOK:        *phStrm is valid.
 *      error:          *phStrm == NULL.
 */
	extern DSP_STATUS STRM_Open(struct NODE_OBJECT *hNode, u32 uDir,
				    u32 uIndex, IN struct STRM_ATTR *pAttr,
				    OUT struct STRM_OBJECT **phStrm);

/*
 *  ======== STRM_PrepareBuffer ========
 *  Purpose:
 *      Prepare a data buffer not allocated by DSPStream_AllocateBuffers()
 *      for use with a stream.
 *  Parameter:
 *      hStrm:          Stream handle returned from STRM_Open().
 *      uSize:          Size (GPP bytes) of the buffer.
 *      pBuffer:        Buffer address.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hStrm.
 *      DSP_EFAIL:      Failure occurred, unable to prepare buffer.
 *  Requires:
 *      STRM_Init(void) called.
 *      pBuffer != NULL.
 *  Ensures:
 */
	extern DSP_STATUS STRM_PrepareBuffer(struct STRM_OBJECT *hStrm,
					     u32 uSize,
					     u8 *pBuffer);

/*
 *  ======== STRM_Reclaim ========
 *  Purpose:
 *      Request a buffer back from a stream.
 *  Parameters:
 *      hStrm:          Stream handle returned from STRM_Open().
 *      pBufPtr:        Location to store pointer to reclaimed buffer.
 *      pulBytes:       Location where number of bytes of data in the
 *                      buffer will be written.
 *      pulBufSize:     Location where actual buffer size will be written.
 *      pdwArg:         Location where user argument that travels with
 *                      the buffer will be written.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hStrm.
 *      DSP_ETIMEOUT:   A timeout occurred before a buffer could be
 *                      retrieved.
 *      DSP_EFAIL:      Failure occurred, unable to reclaim buffer.
 *  Requires:
 *      STRM_Init(void) called.
 *      pBufPtr != NULL.
 *      pulBytes != NULL.
 *      pdwArg != NULL.
 *  Ensures:
 */
	extern DSP_STATUS STRM_Reclaim(struct STRM_OBJECT *hStrm,
				       OUT u8 **pBufPtr, u32 *pulBytes,
				       u32 *pulBufSize, u32 *pdwArg);

/*
 *  ======== STRM_RegisterNotify ========
 *  Purpose:
 *      Register to be notified on specific events for this stream.
 *  Parameters:
 *      hStrm:          Stream handle returned by STRM_Open().
 *      uEventMask:     Mask of types of events to be notified about.
 *      uNotifyType:    Type of notification to be sent.
 *      hNotification:  Handle to be used for notification.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hStrm.
 *      DSP_EMEMORY:    Insufficient memory on GPP.
 *      DSP_EVALUE:     uEventMask is invalid.
 *      DSP_ENOTIMPL:   Notification type specified by uNotifyType is not
 *                      supported.
 *  Requires:
 *      STRM_Init(void) called.
 *      hNotification != NULL.
 *  Ensures:
 */
	extern DSP_STATUS STRM_RegisterNotify(struct STRM_OBJECT *hStrm,
					      u32 uEventMask, u32 uNotifyType,
					      struct DSP_NOTIFICATION
					      *hNotification);

/*
 *  ======== STRM_Select ========
 *  Purpose:
 *      Select a ready stream.
 *  Parameters:
 *      aStrmTab:       Array of stream handles returned from STRM_Open().
 *      nStrms:         Number of stream handles in array.
 *      pMask:          Location to store mask of ready streams on output.
 *      uTimeout:       Timeout value (milliseconds).
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_ERANGE:     nStrms out of range.

 *      DSP_EHANDLE:    Invalid stream handle in array.
 *      DSP_ETIMEOUT:   A timeout occurred before a stream became ready.
 *      DSP_EFAIL:      Failure occurred, unable to select a stream.
 *  Requires:
 *      STRM_Init(void) called.
 *      aStrmTab != NULL.
 *      nStrms > 0.
 *      pMask != NULL.
 *  Ensures:
 *      DSP_SOK:        *pMask != 0 || uTimeout == 0.
 *      Error:          *pMask == 0.
 */
	extern DSP_STATUS STRM_Select(IN struct STRM_OBJECT **aStrmTab,
				      u32 nStrms,
				      OUT u32 *pMask, u32 uTimeout);

/*
 *  ======== STRM_UnprepareBuffer ========
 *  Purpose:
 *      Unprepare a data buffer that was previously prepared for a stream
 *      with DSPStream_PrepareBuffer(), and that will no longer be used with
 *      the stream.
 *  Parameter:
 *      hStrm:          Stream handle returned from STRM_Open().
 *      uSize:          Size (GPP bytes) of the buffer.
 *      pBuffer:        Buffer address.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hStrm.
 *      DSP_EFAIL:      Failure occurred, unable to unprepare buffer.
 *  Requires:
 *      STRM_Init(void) called.
 *      pBuffer != NULL.
 *  Ensures:
 */
	extern DSP_STATUS STRM_UnprepareBuffer(struct STRM_OBJECT *hStrm,
					       u32 uSize,
					       u8 *pBuffer);

#endif				/* STRM_ */
