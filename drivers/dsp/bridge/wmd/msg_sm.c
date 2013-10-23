/*
 * msg_sm.c
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
 *  ======== msg_sm.c ========
 *  Description:
 *      Implements upper edge functions for WMD message module.
 *
 *  Public Functions:
 *      WMD_MSG_Create
 *      WMD_MSG_CreateQueue
 *      WMD_MSG_Delete
 *      WMD_MSG_DeleteQueue
 *      WMD_MSG_Get
 *      WMD_MSG_Put
 *      WMD_MSG_RegisterNotify
 *      WMD_MSG_SetQueueId
 *
 *! Revision History:
 *! =================
 *! 24-Jul-2002 jeh     Release critical section in WMD_MSG_Put() before
 *!                     scheduling DPC.
 *! 09-May-2001 jeh     Free MSG queue NTFY object, remove unnecessary set/
 *!                     reset of events.
 *! 10-Jan-2001 jeh     Set/Reset message manager and message queue events
 *!                     correctly.
 *! 04-Dec-2000 jeh     Bug fixes.
 *! 12-Sep-2000 jeh     Created.
 */

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>
#include <dspbridge/dbdefs.h>
#include <dspbridge/errbase.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/list.h>
#include <dspbridge/mem.h>
#include <dspbridge/sync.h>

/*  ----------------------------------- Platform Manager */
#include <dspbridge/dev.h>

/*  ----------------------------------- Others */
#include <dspbridge/io_sm.h>

/*  ----------------------------------- This */
#include <_msg_sm.h>
#include <dspbridge/wmdmsg.h>

/*  ----------------------------------- Defines, Data Structures, Typedefs */
#define MSGQ_SIGNATURE      0x5147534d	/* "QGSM" */

/*  ----------------------------------- Function Prototypes */
static DSP_STATUS AddNewMsg(struct LST_LIST *msgList);
static void DeleteMsgMgr(struct MSG_MGR *hMsgMgr);
static void DeleteMsgQueue(struct MSG_QUEUE *hMsgQueue, u32 uNumToDSP);
static void FreeMsgList(struct LST_LIST *msgList);

/*
 *  ======== WMD_MSG_Create ========
 *      Create an object to manage message queues. Only one of these objects
 *      can exist per device object.
 */
DSP_STATUS WMD_MSG_Create(OUT struct MSG_MGR **phMsgMgr,
			 struct DEV_OBJECT *hDevObject, MSG_ONEXIT msgCallback)
{
	struct MSG_MGR *pMsgMgr;
	struct IO_MGR *hIOMgr;
	DSP_STATUS status = DSP_SOK;

	DBC_Require(phMsgMgr != NULL);
	DBC_Require(msgCallback != NULL);
	DBC_Require(hDevObject != NULL);
	DEV_GetIOMgr(hDevObject, &hIOMgr);
	DBC_Assert(hIOMgr != NULL);
	*phMsgMgr = NULL;
	/* Allocate MSG manager object */
	MEM_AllocObject(pMsgMgr, struct MSG_MGR, MSGMGR_SIGNATURE);

	if (pMsgMgr) {
		pMsgMgr->onExit = msgCallback;
		pMsgMgr->hIOMgr = hIOMgr;
		/* List of MSG_QUEUEs */
		pMsgMgr->queueList = LST_Create();
		 /*  Queues of message frames for messages to the DSP. Message
		  * frames will only be added to the free queue when a
		  * MSG_QUEUE object is created.  */
		pMsgMgr->msgFreeList = LST_Create();
		pMsgMgr->msgUsedList = LST_Create();
		if (pMsgMgr->queueList == NULL ||
		    pMsgMgr->msgFreeList == NULL ||
		    pMsgMgr->msgUsedList == NULL)
			status = DSP_EMEMORY;
		if (DSP_SUCCEEDED(status))
			status = SYNC_InitializeDPCCS(&pMsgMgr->hSyncCS);

		 /*  Create an event to be used by WMD_MSG_Put() in waiting
		 *  for an available free frame from the message manager.  */
		if (DSP_SUCCEEDED(status))
			status = SYNC_OpenEvent(&pMsgMgr->hSyncEvent, NULL);

		if (DSP_SUCCEEDED(status))
			*phMsgMgr = pMsgMgr;
		else
			DeleteMsgMgr(pMsgMgr);

	} else {
		status = DSP_EMEMORY;
	}
	return status;
}

/*
 *  ======== WMD_MSG_CreateQueue ========
 *      Create a MSG_QUEUE for sending/receiving messages to/from a node
 *      on the DSP.
 */
DSP_STATUS WMD_MSG_CreateQueue(struct MSG_MGR *hMsgMgr,
			      OUT struct MSG_QUEUE **phMsgQueue,
			      u32 dwId, u32 uMaxMsgs, HANDLE hArg)
{
	u32 i;
	u32 uNumAllocated = 0;
	struct MSG_QUEUE *pMsgQ;
	DSP_STATUS status = DSP_SOK;

	DBC_Require(MEM_IsValidHandle(hMsgMgr, MSGMGR_SIGNATURE));
	DBC_Require(phMsgQueue != NULL);

	*phMsgQueue = NULL;
	/* Allocate MSG_QUEUE object */
	MEM_AllocObject(pMsgQ, struct MSG_QUEUE, MSGQ_SIGNATURE);
	if (!pMsgQ) {
		status = DSP_EMEMORY;
		goto func_end;
	}
	LST_InitElem((struct LST_ELEM *) pMsgQ);
	pMsgQ->uMaxMsgs = uMaxMsgs;
	pMsgQ->hMsgMgr = hMsgMgr;
	pMsgQ->hArg = hArg;	/* Node handle */
	pMsgQ->dwId = dwId;	/* Node env (not valid yet) */
	/* Queues of Message frames for messages from the DSP */
	pMsgQ->msgFreeList = LST_Create();
	pMsgQ->msgUsedList = LST_Create();
	if (pMsgQ->msgFreeList == NULL || pMsgQ->msgUsedList == NULL)
		status = DSP_EMEMORY;

	 /*  Create event that will be signalled when a message from
	 *  the DSP is available.  */
	if (DSP_SUCCEEDED(status))
		status = SYNC_OpenEvent(&pMsgQ->hSyncEvent, NULL);

	/* Create a notification list for message ready notification. */
	if (DSP_SUCCEEDED(status))
		status = NTFY_Create(&pMsgQ->hNtfy);

	 /*  Create events that will be used to synchronize cleanup
	 *  when the object is deleted. hSyncDone will be set to
	 *  unblock threads in MSG_Put() or MSG_Get(). hSyncDoneAck
	 *  will be set by the unblocked thread to signal that it
	 *  is unblocked and will no longer reference the object.  */
	if (DSP_SUCCEEDED(status))
		status = SYNC_OpenEvent(&pMsgQ->hSyncDone, NULL);

	if (DSP_SUCCEEDED(status))
		status = SYNC_OpenEvent(&pMsgQ->hSyncDoneAck, NULL);

	if (DSP_SUCCEEDED(status)) {
               if (!hMsgMgr->msgFreeList) {
                       status = DSP_EHANDLE;
                       goto func_end;
               }
		/* Enter critical section */
		(void)SYNC_EnterCS(hMsgMgr->hSyncCS);
		/* Initialize message frames and put in appropriate queues */
		for (i = 0; i < uMaxMsgs && DSP_SUCCEEDED(status); i++) {
			status = AddNewMsg(hMsgMgr->msgFreeList);
			if (DSP_SUCCEEDED(status)) {
				uNumAllocated++;
				status = AddNewMsg(pMsgQ->msgFreeList);
			}
		}
		if (DSP_FAILED(status)) {
			/*  Stay inside CS to prevent others from taking any
			 *  of the newly allocated message frames.  */
			DeleteMsgQueue(pMsgQ, uNumAllocated);
		} else {
			LST_PutTail(hMsgMgr->queueList,
				   (struct LST_ELEM *)pMsgQ);
			*phMsgQueue = pMsgQ;
			/* Signal that free frames are now available */
			if (!LST_IsEmpty(hMsgMgr->msgFreeList))
				SYNC_SetEvent(hMsgMgr->hSyncEvent);

		}
		/* Exit critical section */
		(void)SYNC_LeaveCS(hMsgMgr->hSyncCS);
	} else {
		DeleteMsgQueue(pMsgQ, 0);
	}
func_end:
	return status;
}

/*
 *  ======== WMD_MSG_Delete ========
 *      Delete a MSG manager allocated in WMD_MSG_Create().
 */
void WMD_MSG_Delete(struct MSG_MGR *hMsgMgr)
{
	DBC_Require(MEM_IsValidHandle(hMsgMgr, MSGMGR_SIGNATURE));

	DeleteMsgMgr(hMsgMgr);
}

/*
 *  ======== WMD_MSG_DeleteQueue ========
 *      Delete a MSG queue allocated in WMD_MSG_CreateQueue.
 */
void WMD_MSG_DeleteQueue(struct MSG_QUEUE *hMsgQueue)
{
	struct MSG_MGR *hMsgMgr = hMsgQueue->hMsgMgr;
	u32 refCount;

	DBC_Require(MEM_IsValidHandle(hMsgQueue, MSGQ_SIGNATURE));
	hMsgQueue->fDone = true;
	 /*  Unblock all threads blocked in MSG_Get() or MSG_Put().  */
	refCount = hMsgQueue->refCount;
	while (refCount) {
		/* Unblock thread */
		SYNC_SetEvent(hMsgQueue->hSyncDone);
		/* Wait for acknowledgement */
		SYNC_WaitOnEvent(hMsgQueue->hSyncDoneAck, SYNC_INFINITE);
		refCount = hMsgQueue->refCount;
	}
	/* Remove message queue from hMsgMgr->queueList */
	(void)SYNC_EnterCS(hMsgMgr->hSyncCS);
	LST_RemoveElem(hMsgMgr->queueList, (struct LST_ELEM *)hMsgQueue);
	/* Free the message queue object */
	DeleteMsgQueue(hMsgQueue, hMsgQueue->uMaxMsgs);
       if (!hMsgMgr->msgFreeList)
               goto func_cont;
	if (LST_IsEmpty(hMsgMgr->msgFreeList))
		SYNC_ResetEvent(hMsgMgr->hSyncEvent);
func_cont:
	(void)SYNC_LeaveCS(hMsgMgr->hSyncCS);
}

/*
 *  ======== WMD_MSG_Get ========
 *      Get a message from a MSG queue.
 */
DSP_STATUS WMD_MSG_Get(struct MSG_QUEUE *hMsgQueue,
		      struct DSP_MSG *pMsg, u32 uTimeout)
{
	struct MSG_FRAME *pMsgFrame;
	struct MSG_MGR *hMsgMgr;
	bool fGotMsg = false;
	struct SYNC_OBJECT *hSyncs[2];
	u32 uIndex;
	DSP_STATUS status = DSP_SOK;

	DBC_Require(MEM_IsValidHandle(hMsgQueue, MSGQ_SIGNATURE));
	DBC_Require(pMsg != NULL);

	hMsgMgr = hMsgQueue->hMsgMgr;
       if (!hMsgQueue->msgUsedList) {
               status = DSP_EHANDLE;
               goto func_end;
       }

	/* Enter critical section */
	(void)SYNC_EnterCS(hMsgMgr->hSyncCS);
	/* If a message is already there, get it */
	if (!LST_IsEmpty(hMsgQueue->msgUsedList)) {
		pMsgFrame = (struct MSG_FRAME *)LST_GetHead(hMsgQueue->
			    msgUsedList);
		if (pMsgFrame != NULL) {
			*pMsg = pMsgFrame->msgData.msg;
			LST_PutTail(hMsgQueue->msgFreeList,
				   (struct LST_ELEM *)pMsgFrame);
			if (LST_IsEmpty(hMsgQueue->msgUsedList))
				SYNC_ResetEvent(hMsgQueue->hSyncEvent);

			fGotMsg = true;
		}
	} else {
		if (hMsgQueue->fDone)
			status = DSP_EFAIL;
		else
			hMsgQueue->refCount++;

	}
	/* Exit critical section */
	(void)SYNC_LeaveCS(hMsgMgr->hSyncCS);
	if (DSP_SUCCEEDED(status) && !fGotMsg) {
		/*  Wait til message is available, timeout, or done. We don't
		 *  have to schedule the DPC, since the DSP will send messages
		 *  when they are available.  */
		hSyncs[0] = hMsgQueue->hSyncEvent;
		hSyncs[1] = hMsgQueue->hSyncDone;
		status = SYNC_WaitOnMultipleEvents(hSyncs, 2, uTimeout,
			 &uIndex);
		/* Enter critical section */
		(void)SYNC_EnterCS(hMsgMgr->hSyncCS);
		if (hMsgQueue->fDone) {
			hMsgQueue->refCount--;
			/* Exit critical section */
			(void)SYNC_LeaveCS(hMsgMgr->hSyncCS);
			 /*  Signal that we're not going to access hMsgQueue
			  *  anymore, so it can be deleted.  */
			(void)SYNC_SetEvent(hMsgQueue->hSyncDoneAck);
			status = DSP_EFAIL;
		} else {
			if (DSP_SUCCEEDED(status)) {
				DBC_Assert(!LST_IsEmpty(hMsgQueue->
					  msgUsedList));
				/* Get msg from used list */
				pMsgFrame = (struct MSG_FRAME *)
					   LST_GetHead(hMsgQueue->msgUsedList);
				/* Copy message into pMsg and put frame on the
				 * free list */
				if (pMsgFrame != NULL) {
					*pMsg = pMsgFrame->msgData.msg;
					LST_PutTail(hMsgQueue->msgFreeList,
					(struct LST_ELEM *)pMsgFrame);
				}
			}
			hMsgQueue->refCount--;
			/* Reset the event if there are still queued messages */
			if (!LST_IsEmpty(hMsgQueue->msgUsedList))
				SYNC_SetEvent(hMsgQueue->hSyncEvent);

			/* Exit critical section */
			(void)SYNC_LeaveCS(hMsgMgr->hSyncCS);
		}
	}
func_end:
	return status;
}

/*
 *  ======== WMD_MSG_Put ========
 *      Put a message onto a MSG queue.
 */
DSP_STATUS WMD_MSG_Put(struct MSG_QUEUE *hMsgQueue,
		      IN CONST struct DSP_MSG *pMsg, u32 uTimeout)
{
	struct MSG_FRAME *pMsgFrame;
	struct MSG_MGR *hMsgMgr;
	bool fPutMsg = false;
	struct SYNC_OBJECT *hSyncs[2];
	u32 uIndex;
	DSP_STATUS status = DSP_SOK;

	DBC_Require(MEM_IsValidHandle(hMsgQueue, MSGQ_SIGNATURE));
	DBC_Require(pMsg != NULL);

	hMsgMgr = hMsgQueue->hMsgMgr;

       if (!hMsgMgr->msgFreeList) {
               status = DSP_EHANDLE;
               goto func_end;
       }


	(void) SYNC_EnterCS(hMsgMgr->hSyncCS);

	/* If a message frame is available, use it */
	if (!LST_IsEmpty(hMsgMgr->msgFreeList)) {
		pMsgFrame = (struct MSG_FRAME *)LST_GetHead(hMsgMgr->
			    msgFreeList);
		if (pMsgFrame != NULL) {
			pMsgFrame->msgData.msg = *pMsg;
			pMsgFrame->msgData.dwId = hMsgQueue->dwId;
			LST_PutTail(hMsgMgr->msgUsedList, (struct LST_ELEM *)
				   pMsgFrame);
			hMsgMgr->uMsgsPending++;
			fPutMsg = true;
		}
		if (LST_IsEmpty(hMsgMgr->msgFreeList))
			SYNC_ResetEvent(hMsgMgr->hSyncEvent);

		/* Release critical section before scheduling DPC */
		(void)SYNC_LeaveCS(hMsgMgr->hSyncCS);
		/* Schedule a DPC, to do the actual data transfer: */
		IO_Schedule(hMsgMgr->hIOMgr);
	} else {
		if (hMsgQueue->fDone)
			status = DSP_EFAIL;
		else
			hMsgQueue->refCount++;

		(void)SYNC_LeaveCS(hMsgMgr->hSyncCS);
	}
	if (DSP_SUCCEEDED(status) && !fPutMsg) {
		/* Wait til a free message frame is available, timeout,
		 * or done */
		hSyncs[0] = hMsgMgr->hSyncEvent;
		hSyncs[1] = hMsgQueue->hSyncDone;
		status = SYNC_WaitOnMultipleEvents(hSyncs, 2, uTimeout,
			 &uIndex);
		/* Enter critical section */
		(void)SYNC_EnterCS(hMsgMgr->hSyncCS);
		if (hMsgQueue->fDone) {
			hMsgQueue->refCount--;
			/* Exit critical section */
			(void)SYNC_LeaveCS(hMsgMgr->hSyncCS);
			 /*  Signal that we're not going to access hMsgQueue
			  *  anymore, so it can be deleted.  */
			(void)SYNC_SetEvent(hMsgQueue->hSyncDoneAck);
			status = DSP_EFAIL;
		} else {
			if (DSP_SUCCEEDED(status)) {
                               if (LST_IsEmpty(hMsgMgr->msgFreeList)) {
                                       status = DSP_EPOINTER;
                                       goto func_cont;
                               }
				/* Get msg from free list */
				pMsgFrame = (struct MSG_FRAME *)
					    LST_GetHead(hMsgMgr->msgFreeList);
				/* Copy message into pMsg and put frame on the
				 * used list */
				if (pMsgFrame != NULL) {
					pMsgFrame->msgData.msg = *pMsg;
					pMsgFrame->msgData.dwId =
						hMsgQueue->dwId;
					LST_PutTail(hMsgMgr->msgUsedList,
						   (struct LST_ELEM *)
						   pMsgFrame);
					hMsgMgr->uMsgsPending++;
					/* Schedule a DPC, to do the actual
					 * data transfer: */
					IO_Schedule(hMsgMgr->hIOMgr);
				}
			}
			hMsgQueue->refCount--;
			/* Reset event if there are still frames available */
			if (!LST_IsEmpty(hMsgMgr->msgFreeList))
				SYNC_SetEvent(hMsgMgr->hSyncEvent);
func_cont:
			/* Exit critical section */
			(void) SYNC_LeaveCS(hMsgMgr->hSyncCS);
		}
	}
func_end:
	return status;
}

/*
 *  ======== WMD_MSG_RegisterNotify ========
 */
DSP_STATUS WMD_MSG_RegisterNotify(struct MSG_QUEUE *hMsgQueue, u32 uEventMask,
				  u32 uNotifyType,
				  struct DSP_NOTIFICATION *hNotification)
{
	DSP_STATUS status = DSP_SOK;

	DBC_Require(MEM_IsValidHandle(hMsgQueue, MSGQ_SIGNATURE));
	DBC_Require(hNotification != NULL);
	DBC_Require(uEventMask == DSP_NODEMESSAGEREADY || uEventMask == 0);
	DBC_Require(uNotifyType == DSP_SIGNALEVENT);

	status = NTFY_Register(hMsgQueue->hNtfy, hNotification, uEventMask,
			      uNotifyType);

	if (status == DSP_EVALUE) {
		/*  Not registered. Ok, since we couldn't have known. Node
		 *  notifications are split between node state change handled
		 *  by NODE, and message ready handled by MSG.  */
		status = DSP_SOK;
	}

	return status;
}

/*
 *  ======== WMD_MSG_SetQueueId ========
 */
void WMD_MSG_SetQueueId(struct MSG_QUEUE *hMsgQueue, u32 dwId)
{
	DBC_Require(MEM_IsValidHandle(hMsgQueue, MSGQ_SIGNATURE));
	/* DBC_Require(dwId != 0); */

	/*
	 *  A message queue must be created when a node is allocated,
	 *  so that NODE_RegisterNotify() can be called before the node
	 *  is created. Since we don't know the node environment until the
	 *  node is created, we need this function to set hMsgQueue->dwId
	 *  to the node environment, after the node is created.
	 */
	hMsgQueue->dwId = dwId;
}

/*
 *  ======== AddNewMsg ========
 *      Must be called in message manager critical section.
 */
static DSP_STATUS AddNewMsg(struct LST_LIST *msgList)
{
	struct MSG_FRAME *pMsg;
	DSP_STATUS status = DSP_SOK;

	pMsg = (struct MSG_FRAME *)MEM_Calloc(sizeof(struct MSG_FRAME),
		MEM_PAGED);
	if (pMsg != NULL) {
		LST_InitElem((struct LST_ELEM *) pMsg);
		LST_PutTail(msgList, (struct LST_ELEM *) pMsg);
	} else {
		status = DSP_EMEMORY;
	}

	return status;
}

/*
 *  ======== DeleteMsgMgr ========
 */
static void DeleteMsgMgr(struct MSG_MGR *hMsgMgr)
{
	DBC_Require(MEM_IsValidHandle(hMsgMgr, MSGMGR_SIGNATURE));

	if (hMsgMgr->queueList) {
               if (LST_IsEmpty(hMsgMgr->queueList)) {
                       LST_Delete(hMsgMgr->queueList);
                       hMsgMgr->queueList = NULL;
               }
	}

       if (hMsgMgr->msgFreeList) {
		FreeMsgList(hMsgMgr->msgFreeList);
               hMsgMgr->msgFreeList = NULL;
       }

       if (hMsgMgr->msgUsedList) {
		FreeMsgList(hMsgMgr->msgUsedList);
               hMsgMgr->msgUsedList = NULL;
       }

	if (hMsgMgr->hSyncEvent)
		SYNC_CloseEvent(hMsgMgr->hSyncEvent);

	if (hMsgMgr->hSyncCS)
		SYNC_DeleteCS(hMsgMgr->hSyncCS);

	MEM_FreeObject(hMsgMgr);
}

/*
 *  ======== DeleteMsgQueue ========
 */
static void DeleteMsgQueue(struct MSG_QUEUE *hMsgQueue, u32 uNumToDSP)
{
       struct MSG_MGR *hMsgMgr;
	struct MSG_FRAME *pMsg;
	u32 i;

       if (!MEM_IsValidHandle(hMsgQueue, MSGQ_SIGNATURE)
               || !hMsgQueue->hMsgMgr || !hMsgQueue->hMsgMgr->msgFreeList)
               goto func_end;
       hMsgMgr = hMsgQueue->hMsgMgr;


	/* Pull off uNumToDSP message frames from Msg manager and free */
	for (i = 0; i < uNumToDSP; i++) {

		if (!LST_IsEmpty(hMsgMgr->msgFreeList)) {
			pMsg = (struct MSG_FRAME *)LST_GetHead(hMsgMgr->
				msgFreeList);
			MEM_Free(pMsg);
		} else {
			/* Cannot free all of the message frames */
			break;
		}
	}

       if (hMsgQueue->msgFreeList) {
		FreeMsgList(hMsgQueue->msgFreeList);
               hMsgQueue->msgFreeList = NULL;
       }

       if (hMsgQueue->msgUsedList) {
		FreeMsgList(hMsgQueue->msgUsedList);
               hMsgQueue->msgUsedList = NULL;
       }


	if (hMsgQueue->hNtfy)
		NTFY_Delete(hMsgQueue->hNtfy);

	if (hMsgQueue->hSyncEvent)
		SYNC_CloseEvent(hMsgQueue->hSyncEvent);

	if (hMsgQueue->hSyncDone)
		SYNC_CloseEvent(hMsgQueue->hSyncDone);

	if (hMsgQueue->hSyncDoneAck)
		SYNC_CloseEvent(hMsgQueue->hSyncDoneAck);

	MEM_FreeObject(hMsgQueue);
func_end:
       return;

}

/*
 *  ======== FreeMsgList ========
 */
static void FreeMsgList(struct LST_LIST *msgList)
{
	struct MSG_FRAME *pMsg;

       if (!msgList)
               goto func_end;

	while ((pMsg = (struct MSG_FRAME *)LST_GetHead(msgList)) != NULL)
		MEM_Free(pMsg);

	DBC_Assert(LST_IsEmpty(msgList));

	LST_Delete(msgList);
func_end:
       return;
}

