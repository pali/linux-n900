/*
 * sync.c
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
 *  ======== sync.c ========
 *  Purpose:
 *      Synchronization services.
 *
 *  Public Functions:
 *      SYNC_CloseEvent
 *      SYNC_DeleteCS
 *      SYNC_EnterCS
 *      SYNC_Exit
 *      SYNC_Init
 *      SYNC_InitializeCS
 *      SYNC_LeaveCS
 *      SYNC_OpenEvent
 *      SYNC_ResetEvent
 *      SYNC_SetEvent
 *      SYNC_WaitOnEvent
 *      SYNC_WaitOnMultipleEvents
 *
 *! Revision History:
 *! ================
 *! 05-Nov-2001 kc: Minor cosmetic changes.
 *! 05-Oct-2000 jeh Added SYNC_WaitOnMultipleEvents().
 *! 10-Aug-2000 rr: SYNC_PostMessage added.
 *! 10-Jul-2000 jeh Modified SYNC_OpenEvent() to handle NULL attrs.
 *! 03-Feb-2000 rr: Module init/exit is handled by SERVICES Init/Exit.
 *!		 GT Changes.
 *! 01-Dec-1999 ag: Added optional named event creation in SYNC_OpenEvent().
 *! 22-Nov-1999 kc: Added changes from code review.
 *! 22-Sep-1999 kc: Modified from sync95.c.
 *! 05-Aug-1996 gp: Created.
 */

/*  ----------------------------------- Host OS */
#include <dspbridge/host_os.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>
#include <dspbridge/dbdefs.h>
#include <dspbridge/errbase.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>
#include <dspbridge/gt.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/csl.h>
#include <dspbridge/mem.h>

/*  ----------------------------------- This */
#include <dspbridge/sync.h>

/*  ----------------------------------- Defines, Data Structures, Typedefs */
#define SIGNATURE       0x434e5953	/* "SYNC" (in reverse) */

enum wait_state {
	wo_waiting,
	wo_signalled
} ;

enum sync_state {
	so_reset,
	so_signalled
} ;

struct WAIT_OBJECT {
	enum wait_state state;
	struct SYNC_OBJECT *signalling_event;
	struct semaphore sem;
};

/* Generic SYNC object: */
struct SYNC_OBJECT {
	u32 dwSignature;	/* Used for object validation. */
	enum sync_state state;
	spinlock_t sync_lock;
	struct WAIT_OBJECT *pWaitObj;
};

struct SYNC_DPCCSOBJECT {
	u32 dwSignature;	/* used for object validation */
	spinlock_t sync_dpccs_lock;
	s32 count;
} ;

/*  ----------------------------------- Globals */
#if GT_TRACE
static struct GT_Mask SYNC_debugMask = { NULL, NULL };  /* GT trace variable */
#endif

static int test_and_set(volatile void *ptr, int val)
{
	int ret = val;
	asm volatile (" swp %0, %0, [%1]" : "+r" (ret) : "r"(ptr) : "memory");
	return ret;
}

static void timeout_callback(unsigned long hWaitObj);

/*
 *  ======== SYNC_CloseEvent ========
 *  Purpose:
 *      Close an existing SYNC event object.
 */
DSP_STATUS SYNC_CloseEvent(struct SYNC_OBJECT *hEvent)
{
	DSP_STATUS status = DSP_SOK;
	struct SYNC_OBJECT *pEvent = (struct SYNC_OBJECT *)hEvent;

	DBC_Require(pEvent != NULL && pEvent->pWaitObj == NULL);

	GT_1trace(SYNC_debugMask, GT_ENTER, "SYNC_CloseEvent: hEvent 0x%x\n",
		  hEvent);

	if (MEM_IsValidHandle(hEvent, SIGNATURE)) {
		if (pEvent->pWaitObj) {
			status = DSP_EFAIL;
			GT_0trace(SYNC_debugMask, GT_6CLASS,
				  "SYNC_CloseEvent: Wait object not NULL\n");
		}
		MEM_FreeObject(pEvent);

	} else {
		status = DSP_EHANDLE;
		GT_1trace(SYNC_debugMask, GT_6CLASS,
			  "SYNC_CloseEvent: invalid "
			  "hEvent handle 0x%x\n", hEvent);
	}

	return status;
}

/*
 *  ======== SYNC_Exit ========
 *  Purpose:
 *      Cleanup SYNC module.
 */
void SYNC_Exit(void)
{
	GT_0trace(SYNC_debugMask, GT_5CLASS, "SYNC_Exit\n");
}

/*
 *  ======== SYNC_Init ========
 *  Purpose:
 *      Initialize SYNC module.
 */
bool SYNC_Init(void)
{
	GT_create(&SYNC_debugMask, "SY");	/* SY for SYnc */

	GT_0trace(SYNC_debugMask, GT_5CLASS, "SYNC_Init\n");

	return true;
}

/*
 *  ======== SYNC_OpenEvent ========
 *  Purpose:
 *      Open a new synchronization event object.
 */
DSP_STATUS SYNC_OpenEvent(OUT struct SYNC_OBJECT **phEvent,
			  IN OPTIONAL struct SYNC_ATTRS *pAttrs)
{
	struct SYNC_OBJECT *pEvent = NULL;
	DSP_STATUS status = DSP_SOK;

	DBC_Require(phEvent != NULL);

	GT_2trace(SYNC_debugMask, GT_ENTER,
		  "SYNC_OpenEvent: phEvent 0x%x, pAttrs "
		  "0x%x\n", phEvent, pAttrs);

	/* Allocate memory for sync object */
	MEM_AllocObject(pEvent, struct SYNC_OBJECT, SIGNATURE);
	if (pEvent != NULL) {
		pEvent->state = so_reset;
		pEvent->pWaitObj = NULL;
		spin_lock_init(&pEvent->sync_lock);
	} else {
		status = DSP_EMEMORY;
		GT_0trace(SYNC_debugMask, GT_6CLASS,
			  "SYNC_OpenEvent: MEM_AllocObject failed\n");
	}

	*phEvent = pEvent;

	return status;
}

/*
 *  ======== SYNC_ResetEvent ========
 *  Purpose:
 *      Reset an event to non-signalled.
 */
DSP_STATUS SYNC_ResetEvent(struct SYNC_OBJECT *hEvent)
{
	DSP_STATUS status = DSP_SOK;
	struct SYNC_OBJECT *pEvent = (struct SYNC_OBJECT *)hEvent;

	GT_1trace(SYNC_debugMask, GT_ENTER, "SYNC_ResetEvent: hEvent 0x%x\n",
		  hEvent);

	if (MEM_IsValidHandle(hEvent, SIGNATURE)) {
		pEvent->state = so_reset;
		status = DSP_SOK;
	} else {
		status = DSP_EHANDLE;
		GT_1trace(SYNC_debugMask, GT_6CLASS,
			  "SYNC_ResetEvent: invalid hEvent "
			  "handle 0x%x\n", hEvent);
	}

	return status;
}

/*
 *  ======== SYNC_SetEvent ========
 *  Purpose:
 *      Set an event to signaled and unblock one waiting thread.
 *
 *  This function is called from ISR, DPC and user context. Hence interrupts
 *  are disabled to ensure atomicity.
 */

DSP_STATUS SYNC_SetEvent(struct SYNC_OBJECT *hEvent)
{
	DSP_STATUS status = DSP_SOK;
	struct SYNC_OBJECT *pEvent = (struct SYNC_OBJECT *)hEvent;
	unsigned long flags;

	GT_1trace(SYNC_debugMask, GT_6CLASS, "SYNC_SetEvent: hEvent 0x%x\n",
		  hEvent);

	if (MEM_IsValidHandle(hEvent, SIGNATURE)) {
		spin_lock_irqsave(&hEvent->sync_lock, flags);
		GT_1trace(SYNC_debugMask, GT_6CLASS,
			"SYNC_SetEvent: pEvent->pWaitObj "
			"= 0x%x \n", pEvent->pWaitObj);
	if (pEvent->pWaitObj)
		GT_1trace(SYNC_debugMask, GT_6CLASS, "SYNC_SetEvent: "
			"pEvent->pWaitObj->state = 0x%x \n",
			pEvent->pWaitObj->state);
		if (pEvent->pWaitObj != NULL &&
		   test_and_set(&pEvent->pWaitObj->state,
		   wo_signalled) == wo_waiting) {

			pEvent->state = so_reset;
			pEvent->pWaitObj->signalling_event = pEvent;
			up(&pEvent->pWaitObj->sem);
			GT_1trace(SYNC_debugMask, GT_6CLASS,
				  "SYNC_SetEvent: Unlock "
				  "Semaphore for hEvent 0x%x\n", hEvent);
		} else {
			pEvent->state = so_signalled;
		}
		spin_unlock_irqrestore(&hEvent->sync_lock, flags);
	} else {
		status = DSP_EHANDLE;
		GT_1trace(SYNC_debugMask, GT_6CLASS,
			  "SYNC_SetEvent: invalid hEvent "
			  "handle 0x%x\n", hEvent);
	}
	return status;
}

/*
 *  ======== SYNC_WaitOnEvent ========
 *  Purpose:
 *      Wait for an event to be signalled, up to the specified timeout.
 *      Note: dwTimeOut must be 0xffffffff to signal infinite wait.
 */
DSP_STATUS SYNC_WaitOnEvent(struct SYNC_OBJECT *hEvent, u32 dwTimeout)
{
	DSP_STATUS status = DSP_SOK;
	struct SYNC_OBJECT *pEvent = (struct SYNC_OBJECT *)hEvent;
	u32 temp;

	GT_2trace(SYNC_debugMask, GT_6CLASS, "SYNC_WaitOnEvent: hEvent 0x%x\n, "
		  "dwTimeOut 0x%x", hEvent, dwTimeout);
	if (MEM_IsValidHandle(hEvent, SIGNATURE)) {
		status = SYNC_WaitOnMultipleEvents(&pEvent, 1, dwTimeout,
						  &temp);
	} else {
		status = DSP_EHANDLE;
		GT_1trace(SYNC_debugMask, GT_6CLASS,
			  "SYNC_WaitOnEvent: invalid hEvent"
			  "handle 0x%x\n", hEvent);
	}
	return status;
}

/*
 *  ======== SYNC_WaitOnMultipleEvents ========
 *  Purpose:
 *      Wait for any of an array of events to be signalled, up to the
 *      specified timeout.
 */
DSP_STATUS SYNC_WaitOnMultipleEvents(struct SYNC_OBJECT **hSyncEvents,
				     u32 uCount, u32 dwTimeout,
				     OUT u32 *puIndex)
{
	u32 i;
	DSP_STATUS status = DSP_SOK;
	u32 curr;
	struct WAIT_OBJECT *Wp;

	DBC_Require(uCount > 0);
	DBC_Require(hSyncEvents != NULL);
	DBC_Require(puIndex != NULL);

	for (i = 0; i < uCount; i++)
		DBC_Require(MEM_IsValidHandle(hSyncEvents[i], SIGNATURE));

	GT_4trace(SYNC_debugMask, GT_6CLASS,
		  "SYNC_WaitOnMultipleEvents: hSyncEvents:"
		  "0x%x\tuCount: 0x%x" "\tdwTimeout: 0x%x\tpuIndex: 0x%x\n",
		  hSyncEvents, uCount, dwTimeout, puIndex);

	Wp = MEM_Calloc(sizeof(struct WAIT_OBJECT), MEM_NONPAGED);
	if (Wp == NULL)
		return DSP_EMEMORY;

	Wp->state = wo_waiting;
	Wp->signalling_event = NULL;
	init_MUTEX_LOCKED(&(Wp->sem));

	for (curr = 0; curr < uCount; curr++) {
		hSyncEvents[curr]->pWaitObj = Wp;
		if (hSyncEvents[curr]->state == so_signalled) {
			GT_0trace(SYNC_debugMask, GT_6CLASS,
				 "Detected signaled Event !!!\n");
			if (test_and_set(&(Wp->state), wo_signalled) ==
			   wo_waiting) {
				GT_0trace(SYNC_debugMask, GT_6CLASS,
					 "Setting Signal Event!!!\n");
				hSyncEvents[curr]->state = so_reset;
				Wp->signalling_event = hSyncEvents[curr];
			}
		curr++;	/* Will try optimizing later */
		break;
		}
	}

	curr--;			/* Will try optimizing later */
	if (Wp->state != wo_signalled && dwTimeout > 0) {
		struct timer_list timeout;
		if (dwTimeout != SYNC_INFINITE) {
			init_timer(&timeout);
			timeout.function = timeout_callback;
			timeout.data = (unsigned long)Wp;
			timeout.expires = jiffies + dwTimeout * HZ / 1000;
			add_timer(&timeout);
		}
		if (down_interruptible(&(Wp->sem))) {
			GT_0trace(SYNC_debugMask, GT_7CLASS, "SYNC: "
				"WaitOnMultipleEvents Interrupted by signal\n");
			status = DSP_EFAIL;
		}
		if (dwTimeout != SYNC_INFINITE) {
			if (in_interrupt()) {
				if (!del_timer(&timeout)) {
					GT_0trace(SYNC_debugMask, GT_7CLASS,
						  "SYNC: Timer expired\n");
				}
			} else {
				if (!del_timer_sync(&timeout)) {
					GT_0trace(SYNC_debugMask, GT_7CLASS,
						  "SYNC: Timer expired\n");
				}
			}
		}
	}
	for (i = 0; i <= curr; i++) {
		if (MEM_IsValidHandle(hSyncEvents[i], SIGNATURE)) {
			/*  Memory corruption here if hSyncEvents[i] is
			 *  freed before following statememt. */
			hSyncEvents[i]->pWaitObj = NULL;
		}
		if (hSyncEvents[i] == Wp->signalling_event)
			*puIndex = i;

	}
	if (Wp->signalling_event == NULL && DSP_SUCCEEDED(status)) {
		GT_0trace(SYNC_debugMask, GT_7CLASS,
			  "SYNC:Signaling Event NULL!!!(:-\n");
		status = DSP_ETIMEOUT;
	}
	if (Wp)
		MEM_Free(Wp);
	return status;
}

static void timeout_callback(unsigned long hWaitObj)
{
	struct WAIT_OBJECT *pWaitObj = (struct WAIT_OBJECT *)hWaitObj;
	if (test_and_set(&pWaitObj->state, wo_signalled) == wo_waiting)
		up(&pWaitObj->sem);

}

/*
 *  ======== SYNC_DeleteCS ========
 */
DSP_STATUS SYNC_DeleteCS(struct SYNC_CSOBJECT *hCSObj)
{
	DSP_STATUS status = DSP_SOK;
	struct SYNC_CSOBJECT *pCSObj = (struct SYNC_CSOBJECT *)hCSObj;

	GT_0trace(SYNC_debugMask, GT_ENTER, "SYNC_DeleteCS\n");

	if (MEM_IsValidHandle(hCSObj, SIGNATURECS)) {
		if (down_trylock(&pCSObj->sem) != 0) {
			GT_1trace(SYNC_debugMask, GT_7CLASS,
				  "CS in use (locked) while "
				  "deleting! pCSObj=0x%X", pCSObj);
			DBC_Assert(0);
		}
		MEM_FreeObject(hCSObj);
	} else if (MEM_IsValidHandle(hCSObj, SIGNATUREDPCCS)) {
		struct SYNC_DPCCSOBJECT *pDPCCSObj =
					 (struct SYNC_DPCCSOBJECT *)hCSObj;
		if (pDPCCSObj->count != 1) {
			GT_1trace(SYNC_debugMask, GT_7CLASS,
				  "DPC CS in use (locked) while "
				  "deleting! pCSObj=0x%X", pCSObj);
			DBC_Assert(0);
		}
		MEM_FreeObject(pDPCCSObj);
	} else {
		status = DSP_EHANDLE;
		GT_1trace(SYNC_debugMask, GT_6CLASS,
			  "SYNC_DeleteCS: invalid hCSObj "
			  "handle 0x%x\n", hCSObj);
	}

	return status;
}

/*
 *  ======== SYNC_EnterCS ========
 */
DSP_STATUS SYNC_EnterCS(struct SYNC_CSOBJECT *hCSObj)
{
	DSP_STATUS status = DSP_SOK;
	struct SYNC_CSOBJECT *pCSObj = (struct SYNC_CSOBJECT *)hCSObj;

	GT_1trace(SYNC_debugMask, GT_ENTER, "SYNC_EnterCS: hCSObj %p\n",
		 hCSObj);
	if (MEM_IsValidHandle(hCSObj, SIGNATURECS)) {
		if (in_interrupt()) {
			status = DSP_EFAIL;
			GT_0trace(SYNC_debugMask, GT_7CLASS,
				 "SYNC_EnterCS called from "
				 "ISR/DPC or with ISR/DPC disabled!");
			DBC_Assert(0);
		} else if (down_interruptible(&pCSObj->sem)) {
			GT_1trace(SYNC_debugMask, GT_7CLASS,
				 "CS interrupted by signal! "
				 "pCSObj=0x%X", pCSObj);
			status = DSP_EFAIL;
		}
	} else if (MEM_IsValidHandle(hCSObj, SIGNATUREDPCCS)) {
		struct SYNC_DPCCSOBJECT *pDPCCSObj =
					(struct SYNC_DPCCSOBJECT *)hCSObj;
		GT_0trace(SYNC_debugMask, GT_ENTER, "SYNC_EnterCS DPC\n");
		spin_lock_bh(&pDPCCSObj->sync_dpccs_lock);
		pDPCCSObj->count--;
		if (pDPCCSObj->count != 0) {
			/* FATAL ERROR : Failed to acquire DPC CS */
			GT_2trace(SYNC_debugMask, GT_7CLASS,
				  "SYNC_EnterCS DPCCS %x locked,"
				  "count %d", pDPCCSObj, pDPCCSObj->count);
			spin_unlock_bh(&pDPCCSObj->sync_dpccs_lock);
			DBC_Assert(0);
		}
	} else {
		status = DSP_EHANDLE;
		GT_1trace(SYNC_debugMask, GT_6CLASS,
			  "SYNC_EnterCS: invalid hCSObj "
			  "handle 0x%x\n", hCSObj);
	}

	return status;
}

/*
 *  ======== SYNC_InitializeCS ========
 */
DSP_STATUS SYNC_InitializeCS(OUT struct SYNC_CSOBJECT **phCSObj)
{
	DSP_STATUS status = DSP_SOK;
	struct SYNC_CSOBJECT *pCSObj = NULL;

	GT_0trace(SYNC_debugMask, GT_ENTER, "SYNC_InitializeCS\n");

	/* Allocate memory for sync CS object */
	MEM_AllocObject(pCSObj, struct SYNC_CSOBJECT, SIGNATURECS);
	if (pCSObj != NULL) {
		init_MUTEX(&pCSObj->sem);
	} else {
		status = DSP_EMEMORY;
		GT_0trace(SYNC_debugMask, GT_6CLASS,
			  "SYNC_InitializeCS: MEM_AllocObject"
			  "failed\n");
	}
	/* return CS object */
	*phCSObj = pCSObj;
	DBC_Assert(DSP_FAILED(status) || (pCSObj));
	return status;
}

DSP_STATUS SYNC_InitializeDPCCS(OUT struct SYNC_CSOBJECT **phCSObj)
{
	DSP_STATUS status = DSP_SOK;
	struct SYNC_DPCCSOBJECT *pCSObj = NULL;

	DBC_Require(phCSObj);

	GT_0trace(SYNC_debugMask, GT_ENTER, "SYNC_InitializeDPCCS\n");

	if (phCSObj) {
		/* Allocate memory for sync CS object */
		MEM_AllocObject(pCSObj, struct SYNC_DPCCSOBJECT,
				SIGNATUREDPCCS);
		if (pCSObj != NULL) {
			pCSObj->count = 1;
			spin_lock_init(&pCSObj->sync_dpccs_lock);
		} else {
			status = DSP_EMEMORY;
			GT_0trace(SYNC_debugMask, GT_6CLASS,
				  "SYNC_InitializeDPCCS: "
				  "MEM_AllocObject failed\n");
		}

		/* return CS object */
		*phCSObj = (struct SYNC_CSOBJECT *)pCSObj;
	} else {
		status = DSP_EPOINTER;
	}

	GT_1trace(SYNC_debugMask, GT_ENTER, "SYNC_InitializeDPCCS "
		  "pCSObj %p\n", pCSObj);
	DBC_Assert(DSP_FAILED(status) || (pCSObj));

	return status;
}

/*
 *  ======== SYNC_LeaveCS ========
 */
DSP_STATUS SYNC_LeaveCS(struct SYNC_CSOBJECT *hCSObj)
{
	DSP_STATUS status = DSP_SOK;
	struct SYNC_CSOBJECT *pCSObj = (struct SYNC_CSOBJECT *)hCSObj;

	GT_1trace(SYNC_debugMask, GT_ENTER, "SYNC_LeaveCS: hCSObj %p\n",
		  hCSObj);

	if (MEM_IsValidHandle(hCSObj, SIGNATURECS)) {
		up(&pCSObj->sem);
	} else if (MEM_IsValidHandle(hCSObj, SIGNATUREDPCCS)) {
		struct SYNC_DPCCSOBJECT *pDPCCSObj =
					(struct SYNC_DPCCSOBJECT *)hCSObj;
		pDPCCSObj->count++;
		if (pDPCCSObj->count != 1) {
			/* FATAL ERROR : Invalid DPC CS count */
			GT_2trace(SYNC_debugMask, GT_7CLASS,
				  "SYNC_LeaveCS DPCCS %x, "
				  "Invalid count %d", pDPCCSObj,
				  pDPCCSObj->count);
			spin_unlock_bh(&pDPCCSObj->sync_dpccs_lock);
			DBC_Assert(0);
			spin_lock_bh(&pDPCCSObj->sync_dpccs_lock);
		}
		spin_unlock_bh(&pDPCCSObj->sync_dpccs_lock);
		GT_0trace(SYNC_debugMask, GT_ENTER, "SYNC_LeaveCS DPC\n");
	} else {
		status = DSP_EHANDLE;
		GT_1trace(SYNC_debugMask, GT_6CLASS,
			  "SYNC_LeaveCS: invalid hCSObj "
			  "handle 0x%x\n", hCSObj);
	}

	return status;
}
