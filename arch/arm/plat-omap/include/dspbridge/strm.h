/*
 * strm.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * DSPBridge Stream Manager.
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

#ifndef STRM_
#define STRM_

#include <dspbridge/dev.h>

#include <dspbridge/strmdefs.h>
#include <dspbridge/proc.h>

/*
 *  ======== strm_allocate_buffer ========
 *  Purpose:
 *      Allocate data buffer(s) for use with a stream.
 *  Parameter:
 *      hStrm:          Stream handle returned from strm_open().
 *      usize:          Size (GPP bytes) of the buffer(s).
 *      num_bufs:       Number of buffers to allocate.
 *      ap_buffer:       Array to hold buffer addresses.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hStrm.
 *      DSP_EMEMORY:    Insufficient memory.
 *      DSP_EFAIL:      Failure occurred, unable to allocate buffers.
 *      DSP_ESIZE:      usize must be > 0 bytes.
 *  Requires:
 *      strm_init(void) called.
 *      ap_buffer != NULL.
 *  Ensures:
 */
extern dsp_status strm_allocate_buffer(struct strm_object *hStrm,
				       u32 usize,
				       OUT u8 **ap_buffer,
				       u32 num_bufs,
				       struct process_context *pr_ctxt);

/*
 *  ======== strm_close ========
 *  Purpose:
 *      Close a stream opened with strm_open().
 *  Parameter:
 *      hStrm:          Stream handle returned from strm_open().
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hStrm.
 *      DSP_EPENDING:   Some data buffers issued to the stream have not
 *                      been reclaimed.
 *      DSP_EFAIL:      Failure to close stream.
 *  Requires:
 *      strm_init(void) called.
 *  Ensures:
 */
extern dsp_status strm_close(struct strm_object *hStrm,
			     struct process_context *pr_ctxt);

/*
 *  ======== strm_create ========
 *  Purpose:
 *      Create a STRM manager object. This object holds information about the
 *      device needed to open streams.
 *  Parameters:
 *      phStrmMgr:      Location to store handle to STRM manager object on
 *                      output.
 *      dev_obj:           Device for this processor.
 *  Returns:
 *      DSP_SOK:        Success;
 *      DSP_EMEMORY:    Insufficient memory for requested resources.
 *      DSP_EFAIL:      General failure.
 *  Requires:
 *      strm_init(void) called.
 *      phStrmMgr != NULL.
 *      dev_obj != NULL.
 *  Ensures:
 *      DSP_SOK:        Valid *phStrmMgr.
 *      error:          *phStrmMgr == NULL.
 */
extern dsp_status strm_create(OUT struct strm_mgr **phStrmMgr,
			      struct dev_object *dev_obj);

/*
 *  ======== strm_delete ========
 *  Purpose:
 *      Delete the STRM Object.
 *  Parameters:
 *      strm_mgr_obj:       Handle to STRM manager object from strm_create.
 *  Returns:
 *  Requires:
 *      strm_init(void) called.
 *      Valid strm_mgr_obj.
 *  Ensures:
 *      strm_mgr_obj is not valid.
 */
extern void strm_delete(struct strm_mgr *strm_mgr_obj);

/*
 *  ======== strm_exit ========
 *  Purpose:
 *      Discontinue usage of STRM module.
 *  Parameters:
 *  Returns:
 *  Requires:
 *      strm_init(void) successfully called before.
 *  Ensures:
 */
extern void strm_exit(void);

/*
 *  ======== strm_free_buffer ========
 *  Purpose:
 *      Free buffer(s) allocated with strm_allocate_buffer.
 *  Parameter:
 *      hStrm:          Stream handle returned from strm_open().
 *      ap_buffer:       Array containing buffer addresses.
 *      num_bufs:       Number of buffers to be freed.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid stream handle.
 *      DSP_EFAIL:      Failure occurred, unable to free buffers.
 *  Requires:
 *      strm_init(void) called.
 *      ap_buffer != NULL.
 *  Ensures:
 */
extern dsp_status strm_free_buffer(struct strm_object *hStrm,
				   u8 **ap_buffer, u32 num_bufs,
				   struct process_context *pr_ctxt);

/*
 *  ======== strm_get_event_handle ========
 *  Purpose:
 *      Get stream's user event handle. This function is used when closing
 *      a stream, so the event can be closed.
 *  Parameter:
 *      hStrm:          Stream handle returned from strm_open().
 *      ph_event:        Location to store event handle on output.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hStrm.
 *  Requires:
 *      strm_init(void) called.
 *      ph_event != NULL.
 *  Ensures:
 */
extern dsp_status strm_get_event_handle(struct strm_object *hStrm,
					OUT bhandle *ph_event);

/*
 *  ======== strm_get_info ========
 *  Purpose:
 *      Get information about a stream. User's dsp_streaminfo is contained
 *      in stream_info struct. stream_info also contains Bridge private info.
 *  Parameters:
 *      hStrm:              Stream handle returned from strm_open().
 *      stream_info:        Location to store stream info on output.
 *      uSteamInfoSize:     Size of user's dsp_streaminfo structure.
 *  Returns:
 *      DSP_SOK:            Success.
 *      DSP_EHANDLE:        Invalid hStrm.
 *      DSP_ESIZE:          stream_info_size < sizeof(dsp_streaminfo).
 *      DSP_EFAIL:          Unable to get stream info.
 *  Requires:
 *      strm_init(void) called.
 *      stream_info != NULL.
 *  Ensures:
 */
extern dsp_status strm_get_info(struct strm_object *hStrm,
				OUT struct stream_info *stream_info,
				u32 stream_info_size);

/*
 *  ======== strm_idle ========
 *  Purpose:
 *      Idle a stream and optionally flush output data buffers.
 *      If this is an output stream and fFlush is TRUE, all data currently
 *      enqueued will be discarded.
 *      If this is an output stream and fFlush is FALSE, this function
 *      will block until all currently buffered data is output, or the timeout
 *      specified has been reached.
 *      After a successful call to strm_idle(), all buffers can immediately
 *      be reclaimed.
 *  Parameters:
 *      hStrm:          Stream handle returned from strm_open().
 *      fFlush:         If TRUE, discard output buffers.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hStrm.
 *      DSP_ETIMEOUT:   A timeout occurred before the stream could be idled.
 *      DSP_ERESTART:   A critical error occurred, DSP is being restarted.
 *      DSP_EFAIL:      Unable to idle stream.
 *  Requires:
 *      strm_init(void) called.
 *  Ensures:
 */
extern dsp_status strm_idle(struct strm_object *hStrm, bool fFlush);

/*
 *  ======== strm_init ========
 *  Purpose:
 *      Initialize the STRM module.
 *  Parameters:
 *  Returns:
 *      TRUE if initialization succeeded, FALSE otherwise.
 *  Requires:
 *  Ensures:
 */
extern bool strm_init(void);

/*
 *  ======== strm_issue ========
 *  Purpose:
 *      Send a buffer of data to a stream.
 *  Parameters:
 *      hStrm:              Stream handle returned from strm_open().
 *      pbuf:               Pointer to buffer of data to be sent to the stream.
 *      ul_bytes:            Number of bytes of data in the buffer.
 *      ul_buf_size:          Actual buffer size in bytes.
 *      dw_arg:              A user argument that travels with the buffer.
 *  Returns:
 *      DSP_SOK:            Success.
 *      DSP_EHANDLE:        Invalid hStrm.
 *      DSP_ESTREAMFULL:    The stream is full.
 *      DSP_EFAIL:          Failure occurred, unable to issue buffer.
 *  Requires:
 *      strm_init(void) called.
 *      pbuf != NULL.
 *  Ensures:
 */
extern dsp_status strm_issue(struct strm_object *hStrm, IN u8 * pbuf,
			     u32 ul_bytes, u32 ul_buf_size, IN u32 dw_arg);

/*
 *  ======== strm_open ========
 *  Purpose:
 *      Open a stream for sending/receiving data buffers to/from a task of
 *      DAIS socket node on the DSP.
 *  Parameters:
 *      hnode:          Node handle returned from node_allocate().
 *      dir:           DSP_TONODE or DSP_FROMNODE.
 *      index:         Stream index.
 *      pattr:          Pointer to structure containing attributes to be
 *                      applied to stream. Cannot be NULL.
 *      phStrm:         Location to store stream handle on output.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hnode.
 *      DSP_EDIRECTION: Invalid dir.
 *      DSP_EVALUE:     Invalid index.
 *      DSP_ENODETYPE:  hnode is not a task or DAIS socket node.
 *      DSP_EFAIL:      Unable to open stream.
 *  Requires:
 *      strm_init(void) called.
 *      phStrm != NULL.
 *      pattr != NULL.
 *  Ensures:
 *      DSP_SOK:        *phStrm is valid.
 *      error:          *phStrm == NULL.
 */
extern dsp_status strm_open(struct node_object *hnode, u32 dir,
			    u32 index, IN struct strm_attr *pattr,
			    OUT struct strm_object **phStrm,
			    struct process_context *pr_ctxt);

/*
 *  ======== strm_prepare_buffer ========
 *  Purpose:
 *      Prepare a data buffer not allocated by DSPStream_AllocateBuffers()
 *      for use with a stream.
 *  Parameter:
 *      hStrm:          Stream handle returned from strm_open().
 *      usize:          Size (GPP bytes) of the buffer.
 *      pbuffer:        Buffer address.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hStrm.
 *      DSP_EFAIL:      Failure occurred, unable to prepare buffer.
 *  Requires:
 *      strm_init(void) called.
 *      pbuffer != NULL.
 *  Ensures:
 */
extern dsp_status strm_prepare_buffer(struct strm_object *hStrm,
				      u32 usize, u8 *pbuffer);

/*
 *  ======== strm_reclaim ========
 *  Purpose:
 *      Request a buffer back from a stream.
 *  Parameters:
 *      hStrm:          Stream handle returned from strm_open().
 *      buf_ptr:        Location to store pointer to reclaimed buffer.
 *      pulBytes:       Location where number of bytes of data in the
 *                      buffer will be written.
 *      pulBufSize:     Location where actual buffer size will be written.
 *      pdw_arg:         Location where user argument that travels with
 *                      the buffer will be written.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hStrm.
 *      DSP_ETIMEOUT:   A timeout occurred before a buffer could be
 *                      retrieved.
 *      DSP_EFAIL:      Failure occurred, unable to reclaim buffer.
 *  Requires:
 *      strm_init(void) called.
 *      buf_ptr != NULL.
 *      pulBytes != NULL.
 *      pdw_arg != NULL.
 *  Ensures:
 */
extern dsp_status strm_reclaim(struct strm_object *hStrm,
			       OUT u8 **buf_ptr, u32 * pulBytes,
			       u32 *pulBufSize, u32 *pdw_arg);

/*
 *  ======== strm_register_notify ========
 *  Purpose:
 *      Register to be notified on specific events for this stream.
 *  Parameters:
 *      hStrm:          Stream handle returned by strm_open().
 *      event_mask:     Mask of types of events to be notified about.
 *      notify_type:    Type of notification to be sent.
 *      hnotification:  Handle to be used for notification.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hStrm.
 *      DSP_EMEMORY:    Insufficient memory on GPP.
 *      DSP_EVALUE:     event_mask is invalid.
 *      DSP_ENOTIMPL:   Notification type specified by notify_type is not
 *                      supported.
 *  Requires:
 *      strm_init(void) called.
 *      hnotification != NULL.
 *  Ensures:
 */
extern dsp_status strm_register_notify(struct strm_object *hStrm,
				       u32 event_mask, u32 notify_type,
				       struct dsp_notification
				       *hnotification);

/*
 *  ======== strm_select ========
 *  Purpose:
 *      Select a ready stream.
 *  Parameters:
 *      strm_tab:       Array of stream handles returned from strm_open().
 *      nStrms:         Number of stream handles in array.
 *      pmask:          Location to store mask of ready streams on output.
 *      utimeout:       Timeout value (milliseconds).
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_ERANGE:     nStrms out of range.

 *      DSP_EHANDLE:    Invalid stream handle in array.
 *      DSP_ETIMEOUT:   A timeout occurred before a stream became ready.
 *      DSP_EFAIL:      Failure occurred, unable to select a stream.
 *  Requires:
 *      strm_init(void) called.
 *      strm_tab != NULL.
 *      nStrms > 0.
 *      pmask != NULL.
 *  Ensures:
 *      DSP_SOK:        *pmask != 0 || utimeout == 0.
 *      Error:          *pmask == 0.
 */
extern dsp_status strm_select(IN struct strm_object **strm_tab,
			      u32 nStrms, OUT u32 *pmask, u32 utimeout);

/*
 *  ======== strm_unprepare_buffer ========
 *  Purpose:
 *      Unprepare a data buffer that was previously prepared for a stream
 *      with DSPStream_PrepareBuffer(), and that will no longer be used with
 *      the stream.
 *  Parameter:
 *      hStrm:          Stream handle returned from strm_open().
 *      usize:          Size (GPP bytes) of the buffer.
 *      pbuffer:        Buffer address.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hStrm.
 *      DSP_EFAIL:      Failure occurred, unable to unprepare buffer.
 *  Requires:
 *      strm_init(void) called.
 *      pbuffer != NULL.
 *  Ensures:
 */
extern dsp_status strm_unprepare_buffer(struct strm_object *hStrm,
					u32 usize, u8 *pbuffer);

#endif /* STRM_ */
