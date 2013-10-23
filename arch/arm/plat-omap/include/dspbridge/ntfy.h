/*
 * ntfy.h
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
 *  ======== ntfy.h ========
 *  Purpose:
 *      Manage lists of notification events.
 *
 *  Public Functions:
 *      NTFY_Create
 *      NTFY_Delete
 *      NTFY_Exit
 *      NTFY_Init
 *      NTFY_Notify
 *      NTFY_Register
 *
 *! Revision History:
 *! =================
 *! 05-Nov-2001 kc: Updated NTFY_Register.
 *! 07-Sep-2000 jeh Created.
 */

#ifndef NTFY_
#define NTFY_

	struct NTFY_OBJECT;

/*
 *  ======== NTFY_Create ========
 *  Purpose:
 *      Create an empty list of notifications.
 *  Parameters:
 *      phNtfy:         Location to store handle on output.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EMEMORY:    Memory allocation failure.
 *  Requires:
 *      NTFY_Init(void) called.
 *      phNtfy != NULL.
 *  Ensures:
 *      DSP_SUCCEEDED(status) <==>  IsValid(*phNtfy).
 */
	extern DSP_STATUS NTFY_Create(OUT struct NTFY_OBJECT **phNtfy);

/*
 *  ======== NTFY_Delete ========
 *  Purpose:
 *      Free resources allocated in NTFY_Create.
 *  Parameters:
 *      hNtfy:  Handle returned from NTFY_Create().
 *  Returns:
 *  Requires:
 *      NTFY_Init(void) called.
 *      IsValid(hNtfy).
 *  Ensures:
 */
	extern void NTFY_Delete(IN struct NTFY_OBJECT *hNtfy);

/*
 *  ======== NTFY_Exit ========
 *  Purpose:
 *      Discontinue usage of NTFY module.
 *  Parameters:
 *  Returns:
 *  Requires:
 *      NTFY_Init(void) successfully called before.
 *  Ensures:
 */
	extern void NTFY_Exit(void);

/*
 *  ======== NTFY_Init ========
 *  Purpose:
 *      Initialize the NTFY module.
 *  Parameters:
 *  Returns:
 *      TRUE if initialization succeeded, FALSE otherwise.
 *  Ensures:
 */
	extern bool NTFY_Init(void);

/*
 *  ======== NTFY_Notify ========
 *  Purpose:
 *      Execute notify function (signal event or post message) for every
 *      element in the notification list that is to be notified about the
 *      event specified in uEventMask.
 *  Parameters:
 *      hNtfy:      Handle returned from NTFY_Create().
 *      uEventMask: The type of event that has occurred.
 *  Returns:
 *  Requires:
 *      NTFY_Init(void) called.
 *      IsValid(hNtfy).
 *  Ensures:
 */
	extern void NTFY_Notify(IN struct NTFY_OBJECT *hNtfy,
				IN u32 uEventMask);

/*
 *  ======== NTFY_Register ========
 *  Purpose:
 *      Add a notification element to the list. If the notification is already
 *      registered, and uEventMask != 0, the notification will get posted for
 *      events specified in the new event mask. If the notification is already
 *      registered and uEventMask == 0, the notification will be unregistered.
 *  Parameters:
 *      hNtfy:              Handle returned from NTFY_Create().
 *      hNotification:      Handle to a DSP_NOTIFICATION object.
 *      uEventMask:         Events to be notified about.
 *      uNotifyType:        Type of notification: DSP_SIGNALEVENT.
 *  Returns:
 *      DSP_SOK:            Success.
 *      DSP_EMEMORY:        Insufficient memory.
 *      DSP_EVALUE:         uEventMask is 0 and hNotification was not
 *                          previously registered.
 *      DSP_EHANDLE:        NULL hNotification, hNotification event name
 *                          too long, or hNotification event name NULL.
 *  Requires:
 *      NTFY_Init(void) called.
 *      IsValid(hNtfy).
 *      hNotification != NULL.
 *      uNotifyType is DSP_SIGNALEVENT
 *  Ensures:
 */
	extern DSP_STATUS NTFY_Register(IN struct NTFY_OBJECT *hNtfy,
					IN struct DSP_NOTIFICATION
					*hNotification,
					IN u32 uEventMask,
					IN u32 uNotifyType);

#endif				/* NTFY_ */
