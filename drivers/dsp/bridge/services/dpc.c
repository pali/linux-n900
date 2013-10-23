/*
 * dpc.c
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
 *  ======== dpcce.c ========
 *  Purpose:
 *      Deferred Procedure Call(DPC) Services.
 *
 *
 *  Public Functions:
 *      DPC_Create
 *      DPC_Destroy
 *      DPC_Exit
 *      DPC_Init
 *      DPC_Schedule
 *
 *! Revision History:
 *! ================
 *! 28-Mar-2001 ag: Added #ifdef CHNL_NOIPCINTR to set DPC thread priority
 *!                     to THREAD_PRIORITY_IDLE for polling IPC.
 *! 03-Feb-2000 rr: Module init/exit is handled by SERVICES Init/Exit.
 *!		 GT Changes.
 *! 31-Jan-2000 rr: Changes after code review.Terminate thread,handle
 *!                 modified.DPC_Destroy frees the DPC_Object only on
 *!                 Successful termination of the thread and the handle.
 *! 06-Jan-1999 ag: Format cleanup for code review.
 *!                 Removed DPC_[Lower|Raise]IRQL[From|To]DispatchLevel.
 *! 10-Dec-1999 ag: Added SetProcPermissions in DPC_DeferredProcedure().
 *!                 (Needed to access client(s) CHNL buffers).
 *! 19-Sep-1999 a0216266: Stubbed from dpcnt.c.
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
#include <dspbridge/mem.h>

/*  ----------------------------------- This */
#include <dspbridge/dpc.h>

/*  ----------------------------------- Defines, Data Structures, Typedefs */
#define SIGNATURE       0x5f435044	/* "DPC_" (in reverse). */

/* The DPC object, passed to our priority event callback routine: */
struct DPC_OBJECT {
	u32 dwSignature;	/* Used for object validation.   */
	void *pRefData;		/* Argument for client's DPC.    */
	DPC_PROC pfnDPC;	/* Client's DPC.                 */
	u32 numRequested;	/* Number of requested DPC's.      */
	u32 numScheduled;	/* Number of executed DPC's.      */
	struct tasklet_struct dpc_tasklet;

#ifdef DEBUG
	u32 cEntryCount;	/* Number of times DPC reentered. */
	u32 numRequestedMax;	/* Keep track of max pending DPC's. */
#endif

	spinlock_t dpc_lock;
};

/*  ----------------------------------- Globals */
#if GT_TRACE
static struct GT_Mask DPC_DebugMask = { NULL, NULL };	/* DPC Debug Mask */
#endif

/*  ----------------------------------- Function Prototypes */
static void DPC_DeferredProcedure(IN unsigned long pDeferredContext);

/*
 *  ======== DPC_Create ========
 *  Purpose:
 *      Create a DPC object, allowing a client's own DPC procedure to be
 *      scheduled for a call with client reference data.
 */
DSP_STATUS DPC_Create(OUT struct DPC_OBJECT **phDPC, DPC_PROC pfnDPC,
		      void *pRefData)
{
	DSP_STATUS status = DSP_SOK;
	struct DPC_OBJECT *pDPCObject = NULL;

	if ((phDPC != NULL) && (pfnDPC != NULL)) {
		/*
		 *  Allocate a DPC object to store information allowing our DPC
		 *  callback to dispatch to the client's DPC.
		 */
		MEM_AllocObject(pDPCObject, struct DPC_OBJECT, SIGNATURE);
		if (pDPCObject != NULL) {
			tasklet_init(&pDPCObject->dpc_tasklet,
				     DPC_DeferredProcedure,
				     (u32) pDPCObject);
			/* Fill out our DPC Object: */
			pDPCObject->pRefData = pRefData;
			pDPCObject->pfnDPC = pfnDPC;
			pDPCObject->numRequested = 0;
			pDPCObject->numScheduled = 0;
#ifdef DEBUG
			pDPCObject->numRequestedMax = 0;
			pDPCObject->cEntryCount = 0;
#endif
			spin_lock_init(&pDPCObject->dpc_lock);
			*phDPC = pDPCObject;
		} else {
			GT_0trace(DPC_DebugMask, GT_6CLASS,
				  "DPC_Create: DSP_EMEMORY\n");
			status = DSP_EMEMORY;
		}
	} else {
		GT_0trace(DPC_DebugMask, GT_6CLASS,
			  "DPC_Create: DSP_EPOINTER\n");
		status = DSP_EPOINTER;
	}
	DBC_Ensure((DSP_FAILED(status) && (!phDPC || (phDPC && *phDPC == NULL)))
		   || DSP_SUCCEEDED(status));
	return status;
}

/*
 *  ======== DPC_Destroy ========
 *  Purpose:
 *      Cancel the last scheduled DPC, and deallocate a DPC object previously
 *      allocated with DPC_Create(). Frees the Object only if the thread
 *      and the event terminated successfuly.
 */
DSP_STATUS DPC_Destroy(struct DPC_OBJECT *hDPC)
{
	DSP_STATUS status = DSP_SOK;
	struct DPC_OBJECT *pDPCObject = (struct DPC_OBJECT *)hDPC;

	if (MEM_IsValidHandle(hDPC, SIGNATURE)) {

		/* Free our DPC object: */
		if (DSP_SUCCEEDED(status)) {
			tasklet_kill(&pDPCObject->dpc_tasklet);
			MEM_FreeObject(pDPCObject);
			pDPCObject = NULL;
			GT_0trace(DPC_DebugMask, GT_2CLASS,
				  "DPC_Destroy: SUCCESS\n");
		}
	} else {
		GT_0trace(DPC_DebugMask, GT_6CLASS,
			  "DPC_Destroy: DSP_EHANDLE\n");
		status = DSP_EHANDLE;
	}
	DBC_Ensure((DSP_SUCCEEDED(status) && pDPCObject == NULL)
		   || DSP_FAILED(status));
	return status;
}

/*
 *  ======== DPC_Exit ========
 *  Purpose:
 *      Discontinue usage of the DPC module.
 */
void DPC_Exit(void)
{
	GT_0trace(DPC_DebugMask, GT_5CLASS, "Entered DPC_Exit\n");
}

/*
 *  ======== DPC_Init ========
 *  Purpose:
 *      Initialize the DPC module's private state.
 */
bool DPC_Init(void)
{
	GT_create(&DPC_DebugMask, "DP");

	GT_0trace(DPC_DebugMask, GT_5CLASS, "Entered DPC_Init\n");

	return true;
}

/*
 *  ======== DPC_Schedule ========
 *  Purpose:
 *      Schedule a deferred procedure call to be executed at a later time.
 *      Latency and order of DPC execution is platform specific.
 */
DSP_STATUS DPC_Schedule(struct DPC_OBJECT *hDPC)
{
	DSP_STATUS status = DSP_SOK;
	struct DPC_OBJECT *pDPCObject = (struct DPC_OBJECT *)hDPC;
	unsigned long flags;

	GT_1trace(DPC_DebugMask, GT_ENTER, "DPC_Schedule hDPC %x\n", hDPC);
	if (MEM_IsValidHandle(hDPC, SIGNATURE)) {
		/* Increment count of DPC's pending. Needs to be protected
		 * from ISRs since this function is called from process
		 * context also. */
		spin_lock_irqsave(&hDPC->dpc_lock, flags);
		pDPCObject->numRequested++;
		spin_unlock_irqrestore(&hDPC->dpc_lock, flags);
		tasklet_schedule(&(hDPC->dpc_tasklet));
#ifdef DEBUG
		if (pDPCObject->numRequested > pDPCObject->numScheduled +
						pDPCObject->numRequestedMax) {
			pDPCObject->numRequestedMax = pDPCObject->numRequested -
						pDPCObject->numScheduled;
		}
#endif
	/*  If an interrupt occurs between incrementing numRequested and the
	 *  assertion below, then DPC will get executed while returning from
	 *  ISR, which will complete all requests and make numRequested equal
	 * to numScheduled, firing this assertion. This happens only when
	 * DPC is being scheduled in process context */
	} else {
		GT_0trace(DPC_DebugMask, GT_6CLASS,
			  "DPC_Schedule: DSP_EHANDLE\n");
		status = DSP_EHANDLE;
	}
	GT_1trace(DPC_DebugMask, GT_ENTER, "DPC_Schedule status %x\n", status);
	return status;
}

/*
 *  ======== DeferredProcedure ========
 *  Purpose:
 *      Main DPC routine.  This is called by host OS DPC callback
 *      mechanism with interrupts enabled.
 */
static void DPC_DeferredProcedure(IN unsigned long pDeferredContext)
{
	struct DPC_OBJECT *pDPCObject = (struct DPC_OBJECT *)pDeferredContext;
	/* read numRequested in local variable */
	u32 requested;
	u32 serviced;

	DBC_Require(pDPCObject != NULL);
	requested = pDPCObject->numRequested;
	serviced = pDPCObject->numScheduled;

	GT_1trace(DPC_DebugMask, GT_ENTER, "> DPC_DeferredProcedure "
		  "pDeferredContext=%x\n", pDeferredContext);
	/* Rollover taken care of using != instead of < */
	if (serviced != requested) {
		if (pDPCObject->pfnDPC != NULL) {
			/* Process pending DPC's: */
			do {
				/* Call client's DPC: */
				(*(pDPCObject->pfnDPC))(pDPCObject->pRefData);
				serviced++;
			} while (serviced != requested);
		}
		pDPCObject->numScheduled = requested;
	}
	GT_2trace(DPC_DebugMask, GT_ENTER,
		  "< DPC_DeferredProcedure requested %d"
		  " serviced %d\n", requested, serviced);
}

