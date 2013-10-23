/*
 * ntfy.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Manage lists of notification events.
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

#ifndef NTFY_
#define NTFY_

struct ntfy_object;

/*
 *  ======== ntfy_create ========
 *  Purpose:
 *      Create an empty list of notifications.
 *  Parameters:
 *      phNtfy:         Location to store handle on output.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EMEMORY:    Memory allocation failure.
 *  Requires:
 *      ntfy_init(void) called.
 *      phNtfy != NULL.
 *  Ensures:
 *      DSP_SUCCEEDED(status) <==>  IS_VALID(*phNtfy).
 */
extern dsp_status ntfy_create(OUT struct ntfy_object **phNtfy);

/*
 *  ======== ntfy_delete ========
 *  Purpose:
 *      Free resources allocated in ntfy_create.
 *  Parameters:
 *      ntfy_obj:  Handle returned from ntfy_create().
 *  Returns:
 *  Requires:
 *      ntfy_init(void) called.
 *      IS_VALID(ntfy_obj).
 *  Ensures:
 */
extern void ntfy_delete(IN struct ntfy_object *ntfy_obj);

/*
 *  ======== ntfy_exit ========
 *  Purpose:
 *      Discontinue usage of NTFY module.
 *  Parameters:
 *  Returns:
 *  Requires:
 *      ntfy_init(void) successfully called before.
 *  Ensures:
 */
extern void ntfy_exit(void);

/*
 *  ======== ntfy_init ========
 *  Purpose:
 *      Initialize the NTFY module.
 *  Parameters:
 *  Returns:
 *      TRUE if initialization succeeded, FALSE otherwise.
 *  Ensures:
 */
extern bool ntfy_init(void);

/*
 *  ======== ntfy_notify ========
 *  Purpose:
 *      Execute notify function (signal event or post message) for every
 *      element in the notification list that is to be notified about the
 *      event specified in event_mask.
 *  Parameters:
 *      ntfy_obj:      Handle returned from ntfy_create().
 *      event_mask: The type of event that has occurred.
 *  Returns:
 *  Requires:
 *      ntfy_init(void) called.
 *      IS_VALID(ntfy_obj).
 *  Ensures:
 */
extern void ntfy_notify(IN struct ntfy_object *ntfy_obj, IN u32 event_mask);

/*
 *  ======== ntfy_register ========
 *  Purpose:
 *      Add a notification element to the list. If the notification is already
 *      registered, and event_mask != 0, the notification will get posted for
 *      events specified in the new event mask. If the notification is already
 *      registered and event_mask == 0, the notification will be unregistered.
 *  Parameters:
 *      ntfy_obj:              Handle returned from ntfy_create().
 *      hnotification:      Handle to a dsp_notification object.
 *      event_mask:         Events to be notified about.
 *      notify_type:        Type of notification: DSP_SIGNALEVENT.
 *  Returns:
 *      DSP_SOK:            Success.
 *      DSP_EMEMORY:        Insufficient memory.
 *      DSP_EVALUE:         event_mask is 0 and hnotification was not
 *                          previously registered.
 *      DSP_EHANDLE:        NULL hnotification, hnotification event name
 *                          too long, or hnotification event name NULL.
 *  Requires:
 *      ntfy_init(void) called.
 *      IS_VALID(ntfy_obj).
 *      hnotification != NULL.
 *      notify_type is DSP_SIGNALEVENT
 *  Ensures:
 */
extern dsp_status ntfy_register(IN struct ntfy_object *ntfy_obj,
				IN struct dsp_notification *hnotification,
				IN u32 event_mask, IN u32 notify_type);

#endif /* NTFY_ */
