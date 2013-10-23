/*
 * msg.h
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
 *  ======== msg.h ========
 *  Description:
 *      DSP/BIOS Bridge MSG Module.
 *
 *  Public Functions:
 *      MSG_Create
 *      MSG_Delete
 *      MSG_Exit
 *      MSG_Init
 *
 *  Notes:
 *
 *! Revision History:
 *! =================
 *! 17-Nov-2000 jeh     Removed MSG_Get, MSG_Put, MSG_CreateQueue,
 *!                     MSG_DeleteQueue, and MSG_RegisterNotify, since these
 *!                     are now part of mini-driver.
 *! 12-Sep-2000 jeh     Created.
 */

#ifndef MSG_
#define MSG_

#include <dspbridge/devdefs.h>
#include <dspbridge/msgdefs.h>

/*
 *  ======== MSG_Create ========
 *  Purpose:
 *      Create an object to manage message queues. Only one of these objects
 *      can exist per device object. The MSG manager must be created before
 *      the IO Manager.
 *  Parameters:
 *      phMsgMgr:           Location to store MSG manager handle on output.
 *      hDevObject:         The device object.
 *      msgCallback:        Called whenever an RMS_EXIT message is received.
 *  Returns:
 *  Requires:
 *      MSG_Init(void) called.
 *      phMsgMgr != NULL.
 *      hDevObject != NULL.
 *      msgCallback != NULL.
 *  Ensures:
 */
	extern DSP_STATUS MSG_Create(OUT struct MSG_MGR **phMsgMgr,
				     struct DEV_OBJECT *hDevObject,
				     MSG_ONEXIT msgCallback);

/*
 *  ======== MSG_Delete ========
 *  Purpose:
 *      Delete a MSG manager allocated in MSG_Create().
 *  Parameters:
 *      hMsgMgr:            Handle returned from MSG_Create().
 *  Returns:
 *  Requires:
 *      MSG_Init(void) called.
 *      Valid hMsgMgr.
 *  Ensures:
 */
	extern void MSG_Delete(struct MSG_MGR *hMsgMgr);

/*
 *  ======== MSG_Exit ========
 *  Purpose:
 *      Discontinue usage of MSG module.
 *  Parameters:
 *  Returns:
 *  Requires:
 *      MSG_Init(void) successfully called before.
 *  Ensures:
 *      Any resources acquired in MSG_Init(void) will be freed when last MSG
 *      client calls MSG_Exit(void).
 */
	extern void MSG_Exit(void);

/*
 *  ======== MSG_Init ========
 *  Purpose:
 *      Initialize the MSG module.
 *  Parameters:
 *  Returns:
 *      TRUE if initialization succeeded, FALSE otherwise.
 *  Ensures:
 */
	extern bool MSG_Init(void);

#endif				/* MSG_ */
