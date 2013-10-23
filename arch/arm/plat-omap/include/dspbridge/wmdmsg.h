/*
 * wmdmsg.h
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
 *  ======== wmdmsg.h ========
 *  Description:
 *      Declares the upper edge message class library functions required by
 *      all WMD / WCD driver interface tables.  These functions are
 *      implemented by every class of WMD channel library.
 *
 *  Public Functions:
 *
 *  Notes:
 *      Function comment headers reside with the function typedefs in wmd.h.
 *
 *! Revision History:
 *! ================
 *! 06-Dec-2000 jeh     Added uEventMask to WMD_MSG_RegisterNotify(). Added
 *!                     WMD_MSG_SetQueueId().
 *! 17-Nov-2000 jeh     Created.
 */

#ifndef WMDMSG_
#define WMDMSG_

#include <dspbridge/msgdefs.h>

	extern DSP_STATUS WMD_MSG_Create(OUT struct MSG_MGR **phMsgMgr,
					 struct DEV_OBJECT *hDevObject,
					 MSG_ONEXIT msgCallback);

	extern DSP_STATUS WMD_MSG_CreateQueue(struct MSG_MGR *hMsgMgr,
					      OUT struct MSG_QUEUE **phMsgQueue,
					      u32 dwId, u32 uMaxMsgs,
					      HANDLE h);

	extern void WMD_MSG_Delete(struct MSG_MGR *hMsgMgr);

	extern void WMD_MSG_DeleteQueue(struct MSG_QUEUE *hMsgQueue);

	extern DSP_STATUS WMD_MSG_Get(struct MSG_QUEUE *hMsgQueue,
				      struct DSP_MSG *pMsg, u32 uTimeout);

	extern DSP_STATUS WMD_MSG_Put(struct MSG_QUEUE *hMsgQueue,
				      IN CONST struct DSP_MSG *pMsg,
				      u32 uTimeout);

	extern DSP_STATUS WMD_MSG_RegisterNotify(struct MSG_QUEUE *hMsgQueue,
						 u32 uEventMask,
						 u32 uNotifyType,
						 struct DSP_NOTIFICATION
						 *hNotification);

	extern void WMD_MSG_SetQueueId(struct MSG_QUEUE *hMsgQueue, u32 dwId);

#endif				/* WMDMSG_ */
