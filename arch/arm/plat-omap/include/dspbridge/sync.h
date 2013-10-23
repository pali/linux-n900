/*
 * sync.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Provide synchronization services.
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

#ifndef _SYNC_H
#define _SYNC_H

#define SIGNATURECS     0x53435953	/* "SYCS" (in reverse) */
#define SIGNATUREDPCCS  0x53445953	/* "SYDS" (in reverse) */

/* Special timeout value indicating an infinite wait: */
#define SYNC_INFINITE  0xffffffff

/* Maximum string length of a named event */
#define SYNC_MAXNAMELENGTH 32

/* Generic SYNC object: */
struct sync_object;

/* Generic SYNC CS object: */
struct sync_csobject {
	u32 dw_signature;	/* used for object validation */
	struct semaphore sem;
};

/* SYNC object attributes: */
struct sync_attrs {
	bhandle user_event;	/* Platform's User Mode synch. object. */
	bhandle kernel_event;	/* Platform's Kernel Mode sync. object. */
	u32 dw_reserved1;	/* For future expansion. */
	u32 dw_reserved2;	/* For future expansion. */
};

/*
 *  ======== sync_close_event ========
 *  Purpose:
 *      Close this event handle, freeing resources allocated in sync_open_event
 *      if necessary.
 *  Parameters:
 *      event_obj: Handle to a synchronization event, created/opened in
 *              sync_open_event.
 *  Returns:
 *      DSP_SOK:        Success;
 *      DSP_EFAIL:      Failed to close event handle.
 *      DSP_EHANDLE:    Invalid handle.
 *  Requires:
 *      SYNC initialized.
 *  Ensures:
 *      Any subsequent usage of event_obj would be invalid.
 */
extern dsp_status sync_close_event(IN struct sync_object *event_obj);

/*
 *  ======== sync_delete_cs ========
 *  Purpose:
 *      Delete a critical section.
 *  Parameters:
 *      hcs_obj: critical section handle.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid handle.
 *  Requires:
 *  Ensures:
 */
extern dsp_status sync_delete_cs(IN struct sync_csobject *hcs_obj);

/*
 *  ======== sync_enter_cs ========
 *  Purpose:
 *      Enter the critical section.
 *  Parameters:
 *      hcs_obj: critical section handle.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid handle.
 *  Requires:
 *  Ensures:
 */
extern dsp_status sync_enter_cs(IN struct sync_csobject *hcs_obj);

/*
 *  ======== sync_exit ========
 *  Purpose:
 *      Discontinue usage of module; free resources when reference count
 *      reaches 0.
 *  Parameters:
 *  Returns:
 *  Requires:
 *      SYNC initialized.
 *  Ensures:
 *      Resources used by module are freed when cRef reaches zero.
 */
extern void sync_exit(void);

/*
 *  ======== sync_init ========
 *  Purpose:
 *      Initializes private state of SYNC module.
 *  Parameters:
 *  Returns:
 *      TRUE if initialized; FALSE if error occured.
 *  Requires:
 *  Ensures:
 *      SYNC initialized.
 */
extern bool sync_init(void);

/*
 *  ======== sync_initialize_cs ========
 *  Purpose:
 *      Initialize the critical section.
 *  Parameters:
 *      hcs_obj: critical section handle.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EMEMORY:    Out of memory.
 *  Requires:
 *  Ensures:
 */
extern dsp_status sync_initialize_cs(OUT struct sync_csobject **phCSObj);

/*
 *  ======== sync_initialize_dpccs ========
 *  Purpose:
 *      Initialize the critical section between process context and DPC.
 *  Parameters:
 *      hcs_obj: critical section handle.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EMEMORY:    Out of memory.
 *  Requires:
 *  Ensures:
 */
extern dsp_status sync_initialize_dpccs(OUT struct sync_csobject
					**phCSObj);

/*
 *  ======== sync_leave_cs ========
 *  Purpose:
 *      Leave the critical section.
 *  Parameters:
 *      hcs_obj: critical section handle.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid handle.
 *  Requires:
 *  Ensures:
 */
extern dsp_status sync_leave_cs(IN struct sync_csobject *hcs_obj);

/*
 *  ======== sync_open_event ========
 *  Purpose:
 *      Create/open and initialize an event object for thread synchronization,
 *      which is initially in the non-signalled state.
 *  Parameters:
 *      ph_event:    Pointer to location to receive the event object handle.
 *      pattrs:     Pointer to sync_attrs object containing initial SYNC
 *                  sync_object attributes.  If this pointer is NULL, then
 *                  sync_open_event will create and manage an OS specific
 *                  syncronization object.
 *          pattrs->user_event:  Platform's User Mode synchronization object.
 *
 *      The behaviour of the SYNC methods depend on the value of
 *      the user_event attr:
 *
 *      1. (user_event == NULL):
 *          A user mode event is created.
 *      2. (user_event != NULL):
 *          A user mode event is supplied by the caller of sync_open_event().
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EFAIL:      Unable to create user mode event.
 *      DSP_EMEMORY:    Insufficient memory.
 *      DSP_EINVALIDARG sync_attrs values are invalid.
 *  Requires:
 *      - SYNC initialized.
 *      - ph_event != NULL.
 *  Ensures:
 *      If function succeeded, event->event_obj must be a valid event handle.
 */
extern dsp_status sync_open_event(OUT struct sync_object **ph_event,
				  IN OPTIONAL struct sync_attrs *pattrs);

/*
 * ========= sync_post_message ========
 *  Purpose:
 *      To post a windows message
 *  Parameters:
 *      hWindow:    Handle to the window
 *      uMsg:       Message to be posted
 *  Returns:
 *      DSP_SOK:        Success
 *      DSP_EFAIL:      Post message failed
 *      DSP_EHANDLE:    Invalid Window handle
 *  Requires:
 *      SYNC initialized
 *  Ensures
 */
extern dsp_status sync_post_message(IN bhandle hWindow, IN u32 uMsg);

/*
 *  ======== sync_reset_event ========
 *  Purpose:
 *      Reset a syncronization event object state to non-signalled.
 *  Parameters:
 *      event_obj:         Handle to a sync event.
 *  Returns:
 *      DSP_SOK:        Success;
 *      DSP_EFAIL:      Failed to reset event.
 *      DSP_EHANDLE:    Invalid handle.
 *  Requires:
 *      SYNC initialized.
 *  Ensures:
 */
extern dsp_status sync_reset_event(IN struct sync_object *event_obj);

/*
 *  ======== sync_set_event ========
 *  Purpose:
 *      Signal the event.  Will unblock one waiting thread.
 *  Parameters:
 *      event_obj:         Handle to an event object.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EFAIL:      Failed to signal event.
 *      DSP_EHANDLE:    Invalid handle.
 *  Requires:
 *      SYNC initialized.
 *  Ensures:
 */
extern dsp_status sync_set_event(IN struct sync_object *event_obj);

/*
 *  ======== sync_wait_on_event ========
 *  Purpose:
 *      Wait for an event to be signalled, up to the specified timeout.
 *  Parameters:
 *      event_obj:         Handle to an event object.
 *      dwTimeOut:      The time-out interval, in milliseconds.
 *                      The function returns if the interval elapses, even if
 *                      the object's state is nonsignaled.
 *                      If zero, the function tests the object's state and
 *                      returns immediately.
 *                      If SYNC_INFINITE, the function's time-out interval
 *                      never elapses.
 *  Returns:
 *      DSP_SOK:        The object was signalled.
 *      DSP_EHANDLE:    Invalid handle.
 *      SYNC_E_FAIL:    Wait failed, possibly because the process terminated.
 *      SYNC_E_TIMEOUT: Timeout expired while waiting for event to be signalled.
 *  Requires:
 *  Ensures:
 */
extern dsp_status sync_wait_on_event(IN struct sync_object *event_obj,
				     IN u32 dwTimeOut);

/*
 *  ======== sync_wait_on_multiple_events ========
 *  Purpose:
 *      Wait for any of an array of events to be signalled, up to the
 *      specified timeout.
 *      Note: dwTimeOut must be SYNC_INFINITE to signal infinite wait.
 *  Parameters:
 *      sync_events:    Array of handles to event objects.
 *      count:         Number of event handles.
 *      dwTimeOut:      The time-out interval, in milliseconds.
 *                      The function returns if the interval elapses, even if
 *                      no event is signalled.
 *                      If zero, the function tests the object's state and
 *                      returns immediately.
 *                      If SYNC_INFINITE, the function's time-out interval
 *                      never elapses.
 *      pu_index:        Location to store index of event that was signalled.
 *  Returns:
 *      DSP_SOK:        The object was signalled.
 *      SYNC_E_FAIL:    Wait failed, possibly because the process terminated.
 *      SYNC_E_TIMEOUT: Timeout expired before event was signalled.
 *      DSP_EMEMORY:    Memory allocation failed.
 *  Requires:
 *  Ensures:
 */
extern dsp_status sync_wait_on_multiple_events(IN struct sync_object
					       **sync_events,
					       IN u32 count,
					       IN u32 dwTimeout,
					       OUT u32 *pu_index);

#endif /* _SYNC_H */
