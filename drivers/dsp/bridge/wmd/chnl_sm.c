/*
 * chnl_sm.c
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
 *  ======== chnl_sm.c ========
 *  Description:
 *      Implements upper edge functions for WMD channel module.
 *
 *  Public Functions:
 *      WMD_CHNL_AddIOReq
 *      WMD_CHNL_CancelIO
 *      WMD_CHNL_Close
 *      WMD_CHNL_Create
 *      WMD_CHNL_Destroy
 *      WMD_CHNL_FlushIO
 *      WMD_CHNL_GetInfo
 *      WMD_CHNL_GetIOC
 *      WMD_CHNL_GetMgrInfo
 *      WMD_CHNL_Idle
 *      WMD_CHNL_Open
 *
 *  Notes:
 *      The lower edge functions must be implemented by the WMD writer, and
 *      are declared in chnl_sm.h.
 *
 *      Care is taken in this code to prevent simulataneous access to channel
 *      queues from
 *      1. Threads.
 *      2. IO_DPC(), scheduled from the IO_ISR() as an event.
 *
 *      This is done primarily by:
 *      - Semaphores.
 *      - state flags in the channel object; and
 *      - ensuring the IO_Dispatch() routine, which is called from both
 *        CHNL_AddIOReq() and the DPC(if implemented), is not re-entered.
 *
 *  Channel Invariant:
 *      There is an important invariant condition which must be maintained per
 *      channel outside of WMD_CHNL_GetIOC() and IO_Dispatch(), violation of
 *      which may cause timeouts and/or failure offunction SYNC_WaitOnEvent.
 *      This invariant condition is:
 *
 *          LST_Empty(pChnl->pIOCompletions) ==> pChnl->hSyncEvent is reset
 *      and
 *          !LST_Empty(pChnl->pIOCompletions) ==> pChnl->hSyncEvent is set.
 *
 *! Revision History:
 *! ================
 *! 10-Feb-2004 sb: Consolidated the MAILBOX_IRQ macro at the top of the file.
 *! 05-Jan-2004 vp: Updated for 2.6 kernel on 24xx platform.
 *! 23-Apr-2003 sb: Fixed mailbox deadlock
 *! 24-Feb-2003 vp: Code Review Updates.
 *! 18-Oct-2002 vp: Ported to Linux platform
 *! 29-Aug-2002 rr  Changed the SYNC error code return to DSP error code return
 *            in WMD_CHNL_GetIOC.
 *! 22-Jan-2002 ag  Zero-copy support added.
 *!                 CMM_CallocBuf() used for SM allocations.
 *! 04-Feb-2001 ag  DSP-DMA support added.
 *! 22-Nov-2000 kc: Updated usage of PERF_RegisterStat.
 *! 06-Nov-2000 jeh Move ISR_Install, DPC_Create from CHNL_Create to IO_Create.
 *! 13-Oct-2000 jeh Added dwArg parameter to WMD_CHNL_AddIOReq(), added
 *!                 WMD_CHNL_Idle and WMD_CHNL_RegisterNotify for DSPStream.
 *!                 Remove #ifdef DEBUG from around channel cIOCs field.
 *! 21-Sep-2000 rr: PreOMAP chnl class library acts like a IO class library.
 *! 25-Sep-2000 ag: MEM_[Unmap]LinearAddress added for #ifdef CHNL_PREOMAP.
 *! 07-Sep-2000 rr: Added new channel class for PreOMAP.
 *! 11-Jul-2000 jeh Allow NULL user event in WMD_CHNL_Open().
 *! 06-Jul-2000 rr: Changed prefix PROC to PRCS for process module calls.
 *! 20-Jan-2000 ag: Incorporated code review comments.
 *! 05-Jan-2000 ag: Text format cleanup.
 *! 07-Dec-1999 ag: Now setting ChnlMgr fSharedIRQ flag before ISR_Install().
 *! 01-Dec-1999 ag: WMD_CHNL_Open() now accepts named sync event.
 *! 14-Nov-1999 ag: DPC_Schedule() uncommented.
 *! 28-Oct-1999 ag: CHNL Attrs userEvent not supported.
 *!                 SM addrs taken from COFF(IO) or host resource(SM).
 *! 25-May-1999 jg: CHNL_IOCLASS boards now get their shared memory buffer
 *!                 address and length from symbols defined in the currently
 *!                 loaded COFF file. See _chn_sm.h.
 *! 18-Jun-1997 gp: Moved waiting back to ring 0 to improve performance.
 *! 22-Jan-1998 gp: Update User's pIOC struct in GetIOC at lower IRQL (NT).
 *! 16-Jan-1998 gp: Commented out PERF stuff, since it is not all there in NT.
 *! 13-Jan-1998 gp: Protect IOCTLs from IO_DPC by raising IRQL to DIRQL (NT).
 *! 22-Oct-1997 gp: Call SYNC_OpenEvent in CHNL_Open, for NT support.
 *! 18-Jun-1997 gp: Moved waiting back to ring 0 to improve performance.
 *! 16-Jun-1997 gp: Added call into lower edge CHNL function to allow override
 *!                 of the SHM window length reported by Windows CM.
 *! 05-Jun-1997 gp: Removed unnecessary critical sections.
 *! 18-Mar-1997 gp: Ensured CHNL_FlushIO on input leaves channel in READY state.
 *! 06-Jan-1997 gp: ifdefed to support the IO variant of SHM channel class lib.
 *! 21-Jan-1997 gp: CHNL_Close: set pChnl = NULL for DBC_Ensure().
 *! 14-Jan-1997 gp: Updated based on code review feedback.
 *! 03-Jan-1997 gp: Added CHNL_E_WAITTIMEOUT error return code to CHNL_FlushIO()
 *! 23-Oct-1996 gp: Tag channel with ring 0 process handle.
 *! 13-Sep-1996 gp: Added performance statistics for channel.
 *! 09-Sep-1996 gp: Added WMD_CHNL_GetMgrInfo().
 *! 04-Sep-1996 gp: Removed shared memory control struct offset: made zero.
 *! 01-Aug-1996 gp: Implemented basic channel manager and channel create/delete.
 *! 17-Jul-1996 gp: Started pseudo coding.
 *! 11-Jul-1996 gp: Stubbed out.
 */

/*  ----------------------------------- OS */
#include <dspbridge/host_os.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>
#include <dspbridge/dbdefs.h>
#include <dspbridge/errbase.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>
#include <dspbridge/dbg.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/mem.h>
#include <dspbridge/cfg.h>
#include <dspbridge/csl.h>
#include <dspbridge/sync.h>

/*  ----------------------------------- Mini-Driver */
#include <dspbridge/wmd.h>
#include <dspbridge/wmdchnl.h>

/*  ----------------------------------- Platform Manager */
#include <dspbridge/dev.h>

/*  ----------------------------------- Others */
#include <dspbridge/io_sm.h>

/*  ----------------------------------- Define for This */
#define USERMODE_ADDR   PAGE_OFFSET

#define MAILBOX_IRQ INT_MAIL_MPU_IRQ

/*  ----------------------------------- Function Prototypes */
static struct LST_LIST *CreateChirpList(u32 uChirps);

static void FreeChirpList(struct LST_LIST *pList);

static struct CHNL_IRP *MakeNewChirp(void);

static DSP_STATUS SearchFreeChannel(struct CHNL_MGR *pChnlMgr,
				   OUT u32 *pdwChnl);

/*
 *  ======== WMD_CHNL_AddIOReq ========
 *      Enqueue an I/O request for data transfer on a channel to the DSP.
 *      The direction (mode) is specified in the channel object. Note the DSP
 *      address is specified for channels opened in direct I/O mode.
 */
DSP_STATUS WMD_CHNL_AddIOReq(struct CHNL_OBJECT *hChnl, void *pHostBuf,
			    u32 cBytes, u32 cBufSize,
			    OPTIONAL u32 dwDspAddr, u32 dwArg)
{
	DSP_STATUS status = DSP_SOK;
	struct CHNL_OBJECT *pChnl = (struct CHNL_OBJECT *)hChnl;
	struct CHNL_IRP *pChirp = NULL;
	u32 dwState;
	bool fIsEOS;
	struct CHNL_MGR *pChnlMgr = pChnl->pChnlMgr;
	u8 *pHostSysBuf = NULL;
	bool fSchedDPC = false;
	u16 wMbVal = 0;

	DBG_Trace(DBG_ENTER,
		  "> WMD_CHNL_AddIOReq pChnl %p CHNL_IsOutput %x uChnlType "
		  "%x Id %d\n", pChnl, CHNL_IsOutput(pChnl->uMode),
		  pChnl->uChnlType, pChnl->uId);

	fIsEOS = (cBytes == 0) ? true : false;

	if (pChnl->uChnlType == CHNL_PCPY && pChnl->uId > 1 && pHostBuf) {
		if (!(pHostBuf < (void *)USERMODE_ADDR)) {
			pHostSysBuf = pHostBuf;
			goto func_cont;
		}
		/* if addr in user mode, then copy to kernel space */
		pHostSysBuf = MEM_Alloc(cBufSize, MEM_NONPAGED);
		if (pHostSysBuf == NULL) {
			status = DSP_EMEMORY;
			DBG_Trace(DBG_LEVEL7,
				 "No memory to allocate kernel buffer\n");
			goto func_cont;
		}
		if (CHNL_IsOutput(pChnl->uMode)) {
			status = copy_from_user(pHostSysBuf, pHostBuf,
						cBufSize);
			if (status) {
				DBG_Trace(DBG_LEVEL7,
					 "Error copying user buffer to "
					 "kernel, %d bytes remaining.\n",
					 status);
				MEM_Free(pHostSysBuf);
				pHostSysBuf = NULL;
				status = DSP_EPOINTER;
			}
		}
	}
func_cont:
	/* Validate args:  */
	if (pHostBuf == NULL) {
		status = DSP_EPOINTER;
	} else if (!MEM_IsValidHandle(pChnl, CHNL_SIGNATURE)) {
		status = DSP_EHANDLE;
	} else if (fIsEOS && CHNL_IsInput(pChnl->uMode)) {
		status = CHNL_E_NOEOS;
	} else {
		/* Check the channel state: only queue chirp if channel state
		 * allows */
		dwState = pChnl->dwState;
		if (dwState != CHNL_STATEREADY) {
			if (dwState & CHNL_STATECANCEL) {
				status = CHNL_E_CANCELLED;
			} else if ((dwState & CHNL_STATEEOS)
				   && CHNL_IsOutput(pChnl->uMode)) {
				status = CHNL_E_EOS;
			} else {
				/* No other possible states left: */
				DBC_Assert(0);
			}
		}
	}
	/* Mailbox IRQ is disabled to avoid race condition with DMA/ZCPY
	 * channels. DPCCS is held to avoid race conditions with PCPY channels.
	 * If DPC is scheduled in process context (IO_Schedule) and any
	 * non-mailbox interrupt occurs, that DPC will run and break CS. Hence
	 * we disable ALL DPCs. We will try to disable ONLY IO DPC later.  */
	SYNC_EnterCS(pChnlMgr->hCSObj);
	disable_irq(MAILBOX_IRQ);
	if (pChnl->uChnlType == CHNL_PCPY) {
		/* This is a processor-copy channel. */
		if (DSP_SUCCEEDED(status) && CHNL_IsOutput(pChnl->uMode)) {
			/* Check buffer size on output channels for fit. */
			if (cBytes > IO_BufSize(pChnl->pChnlMgr->hIOMgr))
				status = CHNL_E_BUFSIZE;

		}
	}
	if (DSP_SUCCEEDED(status)) {
		/* Get a free chirp: */
		pChirp = (struct CHNL_IRP *)LST_GetHead(pChnl->pFreeList);
		if (pChirp == NULL)
			status = CHNL_E_NOIORPS;

	}
	if (DSP_SUCCEEDED(status)) {
		/* Enqueue the chirp on the chnl's IORequest queue: */
		pChirp->pHostUserBuf = pChirp->pHostSysBuf = pHostBuf;
		if (pChnl->uChnlType == CHNL_PCPY && pChnl->uId > 1)
			pChirp->pHostSysBuf = pHostSysBuf;

		if (DSP_SUCCEEDED(status)) {
			/* Note: for dma chans dwDspAddr contains dsp address
			 * of SM buffer.*/
			DBC_Assert(pChnlMgr->uWordSize != 0);
			/* DSP address */
			pChirp->uDspAddr = dwDspAddr / pChnlMgr->uWordSize;
			pChirp->cBytes = cBytes;
			pChirp->cBufSize = cBufSize;
			/* Only valid for output channel */
			pChirp->dwArg = dwArg;
			pChirp->status = (fIsEOS ? CHNL_IOCSTATEOS :
					 CHNL_IOCSTATCOMPLETE);
			LST_PutTail(pChnl->pIORequests, (struct LST_ELEM *)
				   pChirp);
			pChnl->cIOReqs++;
			DBC_Assert(pChnl->cIOReqs <= pChnl->cChirps);
			/* If end of stream, update the channel state to prevent
			 * more IOR's: */
			if (fIsEOS)
				pChnl->dwState |= CHNL_STATEEOS;

			{
				/* Legacy DSM Processor-Copy */
				DBC_Assert(pChnl->uChnlType == CHNL_PCPY);
				/* Request IO from the DSP */
				IO_RequestChnl(pChnlMgr->hIOMgr, pChnl,
					(CHNL_IsInput(pChnl->uMode) ?
					IO_INPUT : IO_OUTPUT), &wMbVal);
				fSchedDPC = true;
			}
		}
	}
	enable_irq(MAILBOX_IRQ);
	SYNC_LeaveCS(pChnlMgr->hCSObj);
	if (wMbVal != 0)
		IO_IntrDSP2(pChnlMgr->hIOMgr, wMbVal);

	if (fSchedDPC == true) {
		/* Schedule a DPC, to do the actual data transfer: */
		IO_Schedule(pChnlMgr->hIOMgr);
	}
	DBG_Trace(DBG_ENTER, "< WMD_CHNL_AddIOReq pChnl %p\n", pChnl);
	return status;
}

/*
 *  ======== WMD_CHNL_CancelIO ========
 *      Return all I/O requests to the client which have not yet been
 *      transferred.  The channel's I/O completion object is
 *      signalled, and all the I/O requests are queued as IOC's, with the
 *      status field set to CHNL_IOCSTATCANCEL.
 *      This call is typically used in abort situations, and is a prelude to
 *      CHNL_Close();
 */
DSP_STATUS WMD_CHNL_CancelIO(struct CHNL_OBJECT *hChnl)
{
	DSP_STATUS status = DSP_SOK;
	struct CHNL_OBJECT *pChnl = (struct CHNL_OBJECT *)hChnl;
	u32 iChnl = -1;
	CHNL_MODE uMode;
	struct CHNL_IRP *pChirp;
	struct CHNL_MGR *pChnlMgr = NULL;

	/* Check args: */
	if (MEM_IsValidHandle(pChnl, CHNL_SIGNATURE)) {
		iChnl = pChnl->uId;
		uMode = pChnl->uMode;
		pChnlMgr = pChnl->pChnlMgr;
	} else {
		status = DSP_EHANDLE;
	}
	if (DSP_FAILED(status))
		goto func_end;

	 /*  Mark this channel as cancelled, to prevent further IORequests or
	 *  IORequests or dispatching.  */
	SYNC_EnterCS(pChnlMgr->hCSObj);
	pChnl->dwState |= CHNL_STATECANCEL;
	if (LST_IsEmpty(pChnl->pIORequests))
		goto func_cont;

	if (pChnl->uChnlType == CHNL_PCPY) {
		/* Indicate we have no more buffers available for transfer: */
		if (CHNL_IsInput(pChnl->uMode)) {
			IO_CancelChnl(pChnlMgr->hIOMgr, iChnl);
		} else {
			/* Record that we no longer have output buffers
			 * available: */
			pChnlMgr->dwOutputMask &= ~(1 << iChnl);
		}
	}
	/* Move all IOR's to IOC queue:  */
	while (!LST_IsEmpty(pChnl->pIORequests)) {
		pChirp = (struct CHNL_IRP *)LST_GetHead(pChnl->pIORequests);
		if (pChirp) {
			pChirp->cBytes = 0;
			pChirp->status |= CHNL_IOCSTATCANCEL;
			LST_PutTail(pChnl->pIOCompletions,
				   (struct LST_ELEM *)pChirp);
			pChnl->cIOCs++;
			pChnl->cIOReqs--;
			DBC_Assert(pChnl->cIOReqs >= 0);
		}
	}
func_cont:
		SYNC_LeaveCS(pChnlMgr->hCSObj);
func_end:
	return status;
}

/*
 *  ======== WMD_CHNL_Close ========
 *  Purpose:
 *      Ensures all pending I/O on this channel is cancelled, discards all
 *      queued I/O completion notifications, then frees the resources allocated
 *      for this channel, and makes the corresponding logical channel id
 *      available for subsequent use.
 */
DSP_STATUS WMD_CHNL_Close(struct CHNL_OBJECT *hChnl)
{
	DSP_STATUS status;
	struct CHNL_OBJECT *pChnl = (struct CHNL_OBJECT *)hChnl;

	/* Check args: */
	if (!MEM_IsValidHandle(pChnl, CHNL_SIGNATURE)) {
		status = DSP_EHANDLE;
		goto func_cont;
	}
	{
		/* Cancel IO: this ensures no further IO requests or
		 * notifications.*/
		status = WMD_CHNL_CancelIO(hChnl);
	}
func_cont:
	if (DSP_SUCCEEDED(status)) {
		/* Assert I/O on this channel is now cancelled: Protects
		 * from IO_DPC. */
		DBC_Assert((pChnl->dwState & CHNL_STATECANCEL));
		/* Invalidate channel object: Protects from
		 * CHNL_GetIOCompletion(). */
		pChnl->dwSignature = 0x0000;
		/* Free the slot in the channel manager: */
		pChnl->pChnlMgr->apChannel[pChnl->uId] = NULL;
		pChnl->pChnlMgr->cOpenChannels -= 1;
		if (pChnl->hNtfy) {
			NTFY_Delete(pChnl->hNtfy);
			pChnl->hNtfy = NULL;
		}
		/* Reset channel event: (NOTE: hUserEvent freed in user
		 * context.). */
		if (pChnl->hSyncEvent) {
			SYNC_ResetEvent(pChnl->hSyncEvent);
			SYNC_CloseEvent(pChnl->hSyncEvent);
			pChnl->hSyncEvent = NULL;
		}
		/* Free I/O request and I/O completion queues:  */
		if (pChnl->pIOCompletions) {
			FreeChirpList(pChnl->pIOCompletions);
			pChnl->pIOCompletions = NULL;
			pChnl->cIOCs = 0;
		}
		if (pChnl->pIORequests) {
			FreeChirpList(pChnl->pIORequests);
			pChnl->pIORequests = NULL;
			pChnl->cIOReqs = 0;
		}
		if (pChnl->pFreeList) {
			FreeChirpList(pChnl->pFreeList);
			pChnl->pFreeList = NULL;
		}
		/* Release channel object. */
		MEM_FreeObject(pChnl);
		pChnl = NULL;
	}
	DBC_Ensure(DSP_FAILED(status) ||
		  !MEM_IsValidHandle(pChnl, CHNL_SIGNATURE));
	return status;
}

/*
 *  ======== WMD_CHNL_Create ========
 *      Create a channel manager object, responsible for opening new channels
 *      and closing old ones for a given board.
 */
DSP_STATUS WMD_CHNL_Create(OUT struct CHNL_MGR **phChnlMgr,
			  struct DEV_OBJECT *hDevObject,
			  IN CONST struct CHNL_MGRATTRS *pMgrAttrs)
{
	DSP_STATUS status = DSP_SOK;
	struct CHNL_MGR *pChnlMgr = NULL;
	s32 cChannels;
#ifdef DEBUG
	struct CHNL_MGR *hChnlMgr;
#endif
	/* Check DBC requirements:  */
	DBC_Require(phChnlMgr != NULL);
	DBC_Require(pMgrAttrs != NULL);
	DBC_Require(pMgrAttrs->cChannels > 0);
	DBC_Require(pMgrAttrs->cChannels <= CHNL_MAXCHANNELS);
	DBC_Require(pMgrAttrs->uWordSize != 0);
#ifdef DEBUG
	/* This for the purposes of DBC_Require: */
	status = DEV_GetChnlMgr(hDevObject, &hChnlMgr);
	DBC_Require(status != DSP_EHANDLE);
	DBC_Require(hChnlMgr == NULL);
#endif
	if (DSP_SUCCEEDED(status)) {
		/* Allocate channel manager object: */
		MEM_AllocObject(pChnlMgr, struct CHNL_MGR, CHNL_MGRSIGNATURE);
		if (pChnlMgr) {
			/* The cChannels attr must equal the # of supported
			 * chnls for each transport(# chnls for PCPY = DDMA =
			 * ZCPY): i.e. pMgrAttrs->cChannels = CHNL_MAXCHANNELS =
			 * DDMA_MAXDDMACHNLS = DDMA_MAXZCPYCHNLS.  */
			DBC_Assert(pMgrAttrs->cChannels == CHNL_MAXCHANNELS);
			cChannels = (CHNL_MAXCHANNELS + (CHNL_MAXCHANNELS *
				    CHNL_PCPY));
			/* Create array of channels: */
			pChnlMgr->apChannel = MEM_Calloc(
						sizeof(struct CHNL_OBJECT *) *
						cChannels, MEM_NONPAGED);
			if (pChnlMgr->apChannel) {
				/* Initialize CHNL_MGR object: */
				/* Shared memory driver. */
				pChnlMgr->dwType = CHNL_TYPESM;
				pChnlMgr->uWordSize = pMgrAttrs->uWordSize;
				/* total # chnls supported */
				pChnlMgr->cChannels = cChannels;
				pChnlMgr->cOpenChannels = 0;
				pChnlMgr->dwOutputMask = 0;
				pChnlMgr->dwLastOutput = 0;
				pChnlMgr->hDevObject = hDevObject;
				if (DSP_SUCCEEDED(status)) {
					status = SYNC_InitializeDPCCS
						(&pChnlMgr->hCSObj);
				}
			} else {
				status = DSP_EMEMORY;
			}
		} else {
			status = DSP_EMEMORY;
		}
	}
	if (DSP_FAILED(status)) {
		WMD_CHNL_Destroy(pChnlMgr);
		*phChnlMgr = NULL;
	} else {
		/* Return channel manager object to caller... */
		*phChnlMgr = pChnlMgr;
	}
	return status;
}

/*
 *  ======== WMD_CHNL_Destroy ========
 *  Purpose:
 *      Close all open channels, and destroy the channel manager.
 */
DSP_STATUS WMD_CHNL_Destroy(struct CHNL_MGR *hChnlMgr)
{
	DSP_STATUS status = DSP_SOK;
	struct CHNL_MGR *pChnlMgr = hChnlMgr;
	u32 iChnl;

	if (MEM_IsValidHandle(hChnlMgr, CHNL_MGRSIGNATURE)) {
		/* Close all open channels: */
		for (iChnl = 0; iChnl < pChnlMgr->cChannels; iChnl++) {
			if (DSP_SUCCEEDED
			    (WMD_CHNL_Close(pChnlMgr->apChannel[iChnl]))) {
				DBC_Assert(pChnlMgr->apChannel[iChnl] == NULL);
			}
		}
		/* release critical section */
		if (pChnlMgr->hCSObj)
			SYNC_DeleteCS(pChnlMgr->hCSObj);

		/* Free channel manager object: */
		if (pChnlMgr->apChannel)
			MEM_Free(pChnlMgr->apChannel);

		/* Set hChnlMgr to NULL in device object. */
		DEV_SetChnlMgr(pChnlMgr->hDevObject, NULL);
		/* Free this Chnl Mgr object: */
		MEM_FreeObject(hChnlMgr);
	} else {
		status = DSP_EHANDLE;
	}
	return status;
}

/*
 *  ======== WMD_CHNL_FlushIO ========
 *  purpose:
 *      Flushes all the outstanding data requests on a channel.
 */
DSP_STATUS WMD_CHNL_FlushIO(struct CHNL_OBJECT *hChnl, u32 dwTimeOut)
{
	DSP_STATUS status = DSP_SOK;
	struct CHNL_OBJECT *pChnl = (struct CHNL_OBJECT *)hChnl;
	CHNL_MODE uMode = -1;
	struct CHNL_MGR *pChnlMgr;
	struct CHNL_IOC chnlIOC;
	/* Check args:  */
	if (MEM_IsValidHandle(pChnl, CHNL_SIGNATURE)) {
		if ((dwTimeOut == CHNL_IOCNOWAIT)
		    && CHNL_IsOutput(pChnl->uMode)) {
			status = DSP_EINVALIDARG;
		} else {
			uMode = pChnl->uMode;
			pChnlMgr = pChnl->pChnlMgr;
		}
	} else {
		status = DSP_EHANDLE;
	}
	if (DSP_SUCCEEDED(status)) {
		/* Note: Currently, if another thread continues to add IO
		 * requests to this channel, this function will continue to
		 * flush all such queued IO requests.  */
		if (CHNL_IsOutput(uMode) && (pChnl->uChnlType == CHNL_PCPY)) {
			/* Wait for IO completions, up to the specified
			 * timeout: */
			while (!LST_IsEmpty(pChnl->pIORequests) &&
			      DSP_SUCCEEDED(status)) {
				status = WMD_CHNL_GetIOC(hChnl, dwTimeOut,
							 &chnlIOC);
				if (DSP_FAILED(status))
					continue;

				if (chnlIOC.status & CHNL_IOCSTATTIMEOUT)
					status = CHNL_E_WAITTIMEOUT;

			}
		} else {
			status = WMD_CHNL_CancelIO(hChnl);
			/* Now, leave the channel in the ready state: */
			pChnl->dwState &= ~CHNL_STATECANCEL;
		}
	}
	DBC_Ensure(DSP_FAILED(status) || LST_IsEmpty(pChnl->pIORequests));
	return status;
}

/*
 *  ======== WMD_CHNL_GetInfo ========
 *  Purpose:
 *      Retrieve information related to a channel.
 */
DSP_STATUS WMD_CHNL_GetInfo(struct CHNL_OBJECT *hChnl,
			   OUT struct CHNL_INFO *pInfo)
{
	DSP_STATUS status = DSP_SOK;
	struct CHNL_OBJECT *pChnl = (struct CHNL_OBJECT *)hChnl;
	if (pInfo != NULL) {
		if (MEM_IsValidHandle(pChnl, CHNL_SIGNATURE)) {
			/* Return the requested information:  */
			pInfo->hChnlMgr = pChnl->pChnlMgr;
			pInfo->hEvent = pChnl->hUserEvent;
			pInfo->dwID = pChnl->uId;
			pInfo->dwMode = pChnl->uMode;
			pInfo->cPosition = pChnl->cBytesMoved;
			pInfo->hProcess = pChnl->hProcess;
			pInfo->hSyncEvent = pChnl->hSyncEvent;
			pInfo->cIOCs = pChnl->cIOCs;
			pInfo->cIOReqs = pChnl->cIOReqs;
			pInfo->dwState = pChnl->dwState;
		} else {
			status = DSP_EHANDLE;
		}
	} else {
		status = DSP_EPOINTER;
	}
	return status;
}

/*
 *  ======== WMD_CHNL_GetIOC ========
 *      Optionally wait for I/O completion on a channel.  Dequeue an I/O
 *      completion record, which contains information about the completed
 *      I/O request.
 *      Note: Ensures Channel Invariant (see notes above).
 */
DSP_STATUS WMD_CHNL_GetIOC(struct CHNL_OBJECT *hChnl, u32 dwTimeOut,
			  OUT struct CHNL_IOC *pIOC)
{
	DSP_STATUS status = DSP_SOK;
	struct CHNL_OBJECT *pChnl = (struct CHNL_OBJECT *)hChnl;
	struct CHNL_IRP *pChirp;
	DSP_STATUS statSync;
	bool fDequeueIOC = true;
	struct CHNL_IOC ioc = { NULL, 0, 0, 0, 0 };
	u8 *pHostSysBuf = NULL;

	DBG_Trace(DBG_ENTER, "> WMD_CHNL_GetIOC pChnl %p CHNL_IsOutput %x "
		 "uChnlType %x\n", pChnl, CHNL_IsOutput(pChnl->uMode),
		 pChnl->uChnlType);
	/* Check args: */
	if (pIOC == NULL) {
		status = DSP_EPOINTER;
	} else if (!MEM_IsValidHandle(pChnl, CHNL_SIGNATURE)) {
		status = DSP_EHANDLE;
	} else if (dwTimeOut == CHNL_IOCNOWAIT) {
		if (LST_IsEmpty(pChnl->pIOCompletions))
			status = CHNL_E_NOIOC;

	}
	if (DSP_FAILED(status))
		goto func_end;

	ioc.status = CHNL_IOCSTATCOMPLETE;
	if (dwTimeOut != CHNL_IOCNOWAIT && LST_IsEmpty(pChnl->pIOCompletions)) {
		if (dwTimeOut == CHNL_IOCINFINITE)
			dwTimeOut = SYNC_INFINITE;

		statSync = SYNC_WaitOnEvent(pChnl->hSyncEvent, dwTimeOut);
		if (statSync == DSP_ETIMEOUT) {
			/* No response from DSP */
			ioc.status |= CHNL_IOCSTATTIMEOUT;
			fDequeueIOC = false;
		} else if (statSync == DSP_EFAIL) {
			/* This can occur when the user mode thread is
			 * aborted (^C), or when _VWIN32_WaitSingleObject()
			 * fails due to unkown causes.  */
			/* Even though Wait failed, there may be something in
			 * the Q: */
			if (LST_IsEmpty(pChnl->pIOCompletions)) {
				ioc.status |= CHNL_IOCSTATCANCEL;
				fDequeueIOC = false;
			}
		}
	}
	/* See comment in AddIOReq */
	SYNC_EnterCS(pChnl->pChnlMgr->hCSObj);
	disable_irq(MAILBOX_IRQ);
	if (fDequeueIOC) {
		/* Dequeue IOC and set pIOC; */
		DBC_Assert(!LST_IsEmpty(pChnl->pIOCompletions));
		pChirp = (struct CHNL_IRP *)LST_GetHead(pChnl->pIOCompletions);
		/* Update pIOC from channel state and chirp: */
		if (pChirp) {
			pChnl->cIOCs--;
			/*  If this is a zero-copy channel, then set IOC's pBuf
			 *  to the DSP's address. This DSP address will get
			 *  translated to user's virtual addr later.  */
			{
				pHostSysBuf = pChirp->pHostSysBuf;
				ioc.pBuf = pChirp->pHostUserBuf;
			}
			ioc.cBytes = pChirp->cBytes;
			ioc.cBufSize = pChirp->cBufSize;
			ioc.dwArg = pChirp->dwArg;
			ioc.status |= pChirp->status;
			/* Place the used chirp on the free list: */
			LST_PutTail(pChnl->pFreeList, (struct LST_ELEM *)
				   pChirp);
		} else {
			ioc.pBuf = NULL;
			ioc.cBytes = 0;
		}
	} else {
		ioc.pBuf = NULL;
		ioc.cBytes = 0;
		ioc.dwArg = 0;
		ioc.cBufSize = 0;
	}
	/* Ensure invariant: If any IOC's are queued for this channel... */
	if (!LST_IsEmpty(pChnl->pIOCompletions)) {
		/*  Since DSPStream_Reclaim() does not take a timeout
		 *  parameter, we pass the stream's timeout value to
		 *  WMD_CHNL_GetIOC. We cannot determine whether or not
		 *  we have waited in User mode. Since the stream's timeout
		 *  value may be non-zero, we still have to set the event.
		 *  Therefore, this optimization is taken out.
		 *
		 *  if (dwTimeOut == CHNL_IOCNOWAIT) {
		 *    ... ensure event is set..
		 *      SYNC_SetEvent(pChnl->hSyncEvent);
		 *  } */
		SYNC_SetEvent(pChnl->hSyncEvent);
	} else {
		/* else, if list is empty, ensure event is reset. */
		SYNC_ResetEvent(pChnl->hSyncEvent);
	}
	enable_irq(MAILBOX_IRQ);
	SYNC_LeaveCS(pChnl->pChnlMgr->hCSObj);
	if (fDequeueIOC && (pChnl->uChnlType == CHNL_PCPY && pChnl->uId > 1)) {
		if (!(ioc.pBuf < (void *) USERMODE_ADDR))
			goto func_cont;

		/* If the addr is in user mode, then copy it */
		if (!pHostSysBuf || !ioc.pBuf) {
			status = DSP_EPOINTER;
			DBG_Trace(DBG_LEVEL7,
				 "System buffer NULL in IO completion.\n");
			goto func_cont;
		}
		if (!CHNL_IsInput(pChnl->uMode))
			goto func_cont1;

		/*pHostUserBuf */
		status = copy_to_user(ioc.pBuf, pHostSysBuf, ioc.cBytes);
#ifndef RES_CLEANUP_DISABLE
		if (status) {
			if (current->flags & PF_EXITING) {
				DBG_Trace(DBG_LEVEL7,
					 "\n2current->flags ==  PF_EXITING, "
					 " current->flags;0x%x\n",
					 current->flags);
				status = 0;
			} else {
				DBG_Trace(DBG_LEVEL7,
					 "\n2current->flags != PF_EXITING, "
					 " current->flags;0x%x\n",
					 current->flags);
			}
		}
#endif
		if (status) {
			DBG_Trace(DBG_LEVEL7,
				 "Error copying kernel buffer to user, %d"
				 " bytes remaining.  in_interupt %d\n",
				 status, in_interrupt());
			status = DSP_EPOINTER;
		}
func_cont1:
		MEM_Free(pHostSysBuf);
	}
func_cont:
	/* Update User's IOC block: */
	*pIOC = ioc;
func_end:
	DBG_Trace(DBG_ENTER, "< WMD_CHNL_GetIOC pChnl %p\n", pChnl);
	return status;
}

/*
 *  ======== WMD_CHNL_GetMgrInfo ========
 *      Retrieve information related to the channel manager.
 */
DSP_STATUS WMD_CHNL_GetMgrInfo(struct CHNL_MGR *hChnlMgr, u32 uChnlID,
			      OUT struct CHNL_MGRINFO *pMgrInfo)
{
	DSP_STATUS status = DSP_SOK;
	struct CHNL_MGR *pChnlMgr = (struct CHNL_MGR *)hChnlMgr;

	if (pMgrInfo != NULL) {
		if (uChnlID <= CHNL_MAXCHANNELS) {
			if (MEM_IsValidHandle(hChnlMgr, CHNL_MGRSIGNATURE)) {
				/* Return the requested information:  */
				pMgrInfo->hChnl = pChnlMgr->apChannel[uChnlID];
				pMgrInfo->cOpenChannels = pChnlMgr->
							  cOpenChannels;
				pMgrInfo->dwType = pChnlMgr->dwType;
				/* total # of chnls */
				pMgrInfo->cChannels = pChnlMgr->cChannels;
			} else {
				status = DSP_EHANDLE;
			}
		} else {
			status = CHNL_E_BADCHANID;
		}
	} else {
		status = DSP_EPOINTER;
	}

	return status;
}

/*
 *  ======== WMD_CHNL_Idle ========
 *      Idles a particular channel.
 */
DSP_STATUS WMD_CHNL_Idle(struct CHNL_OBJECT *hChnl, u32 dwTimeOut,
			 bool fFlush)
{
	CHNL_MODE uMode;
	struct CHNL_MGR *pChnlMgr;
	DSP_STATUS status = DSP_SOK;

	DBC_Require(MEM_IsValidHandle(hChnl, CHNL_SIGNATURE));

	uMode = hChnl->uMode;
	pChnlMgr = hChnl->pChnlMgr;

	if (CHNL_IsOutput(uMode) && !fFlush) {
		/* Wait for IO completions, up to the specified timeout: */
		status = WMD_CHNL_FlushIO(hChnl, dwTimeOut);
	} else {
		status = WMD_CHNL_CancelIO(hChnl);

		/* Reset the byte count and put channel back in ready state. */
		hChnl->cBytesMoved = 0;
		hChnl->dwState &= ~CHNL_STATECANCEL;
	}

	return status;
}

/*
 *  ======== WMD_CHNL_Open ========
 *      Open a new half-duplex channel to the DSP board.
 */
DSP_STATUS WMD_CHNL_Open(OUT struct CHNL_OBJECT **phChnl,
			 struct CHNL_MGR *hChnlMgr, CHNL_MODE uMode,
			 u32 uChnlId, CONST IN struct CHNL_ATTRS *pAttrs)
{
	DSP_STATUS status = DSP_SOK;
	struct CHNL_MGR *pChnlMgr = hChnlMgr;
	struct CHNL_OBJECT *pChnl = NULL;
	struct SYNC_ATTRS *pSyncAttrs = NULL;
	struct SYNC_OBJECT *hSyncEvent = NULL;
	/* Ensure DBC requirements:  */
	DBC_Require(phChnl != NULL);
	DBC_Require(pAttrs != NULL);
	*phChnl = NULL;
	/* Validate Args:  */
	if (pAttrs->uIOReqs == 0) {
		status = DSP_EINVALIDARG;
	} else {
		if (!MEM_IsValidHandle(hChnlMgr, CHNL_MGRSIGNATURE)) {
			status = DSP_EHANDLE;
		} else {
			if (uChnlId != CHNL_PICKFREE) {
				if (uChnlId >= pChnlMgr->cChannels) {
					status = CHNL_E_BADCHANID;
				} else if (pChnlMgr->apChannel[uChnlId] !=
					  NULL) {
					status = CHNL_E_CHANBUSY;
				}
			} else {
				/* Check for free channel */
				status = SearchFreeChannel(pChnlMgr, &uChnlId);
			}
		}
	}
	if (DSP_FAILED(status))
		goto func_end;

	DBC_Assert(uChnlId < pChnlMgr->cChannels);
	/* Create channel object:  */
	MEM_AllocObject(pChnl, struct CHNL_OBJECT, 0x0000);
	if (!pChnl) {
		status = DSP_EMEMORY;
		goto func_cont;
	}
	/* Protect queues from IO_DPC: */
	pChnl->dwState = CHNL_STATECANCEL;
	/* Allocate initial IOR and IOC queues: */
	pChnl->pFreeList = CreateChirpList(pAttrs->uIOReqs);
	pChnl->pIORequests = CreateChirpList(0);
	pChnl->pIOCompletions = CreateChirpList(0);
	pChnl->cChirps = pAttrs->uIOReqs;
	pChnl->cIOCs = 0;
	pChnl->cIOReqs = 0;
	status = SYNC_OpenEvent(&hSyncEvent, pSyncAttrs);
	if (DSP_SUCCEEDED(status)) {
		status = NTFY_Create(&pChnl->hNtfy);
		if (DSP_FAILED(status)) {
			/* The only failure that could have occurred */
			status = DSP_EMEMORY;
		}
	}
	if (DSP_SUCCEEDED(status)) {
		if (pChnl->pIOCompletions && pChnl->pIORequests &&
		   pChnl->pFreeList) {
			/* Initialize CHNL object fields:    */
			pChnl->pChnlMgr = pChnlMgr;
			pChnl->uId = uChnlId;
			pChnl->uMode = uMode;
			pChnl->hUserEvent = hSyncEvent;	/* for Linux */
			pChnl->hSyncEvent = hSyncEvent;
                       /* get the process handle */
                       pChnl->hProcess = current->pid;
			pChnl->pCBArg = 0;
			pChnl->cBytesMoved = 0;
			/* Default to proc-copy */
			pChnl->uChnlType = CHNL_PCPY;
		} else {
			status = DSP_EMEMORY;
		}
	} else {
		status = DSP_EINVALIDARG;
	}
	if (DSP_FAILED(status)) {
		/* Free memory */
		if (pChnl->pIOCompletions) {
			FreeChirpList(pChnl->pIOCompletions);
			pChnl->pIOCompletions = NULL;
			pChnl->cIOCs = 0;
		}
		if (pChnl->pIORequests) {
			FreeChirpList(pChnl->pIORequests);
			pChnl->pIORequests = NULL;
		}
		if (pChnl->pFreeList) {
			FreeChirpList(pChnl->pFreeList);
			pChnl->pFreeList = NULL;
		}
		if (hSyncEvent) {
			SYNC_CloseEvent(hSyncEvent);
			hSyncEvent = NULL;
		}
		if (pChnl->hNtfy) {
			NTFY_Delete(pChnl->hNtfy);
			pChnl->hNtfy = NULL;
		}
		MEM_FreeObject(pChnl);
	}
func_cont:
	if (DSP_SUCCEEDED(status)) {
		/* Insert channel object in channel manager: */
		pChnlMgr->apChannel[pChnl->uId] = pChnl;
		SYNC_EnterCS(pChnlMgr->hCSObj);
		pChnlMgr->cOpenChannels++;
		SYNC_LeaveCS(pChnlMgr->hCSObj);
		/* Return result... */
		pChnl->dwSignature = CHNL_SIGNATURE;
		pChnl->dwState = CHNL_STATEREADY;
		*phChnl = pChnl;
	}
func_end:
	DBC_Ensure((DSP_SUCCEEDED(status) &&
		  MEM_IsValidHandle(pChnl, CHNL_SIGNATURE)) ||
		  (*phChnl == NULL));
	return status;
}

/*
 *  ======== WMD_CHNL_RegisterNotify ========
 *      Registers for events on a particular channel.
 */
DSP_STATUS WMD_CHNL_RegisterNotify(struct CHNL_OBJECT *hChnl, u32 uEventMask,
				  u32 uNotifyType,
				  struct DSP_NOTIFICATION *hNotification)
{
	DSP_STATUS status = DSP_SOK;

	DBC_Assert(!(uEventMask & ~(DSP_STREAMDONE | DSP_STREAMIOCOMPLETION)));

	status = NTFY_Register(hChnl->hNtfy, hNotification, uEventMask,
			      uNotifyType);

	return status;
}

/*
 *  ======== CreateChirpList ========
 *  Purpose:
 *      Initialize a queue of channel I/O Request/Completion packets.
 *  Parameters:
 *      uChirps:    Number of Chirps to allocate.
 *  Returns:
 *      Pointer to queue of IRPs, or NULL.
 *  Requires:
 *  Ensures:
 */
static struct LST_LIST *CreateChirpList(u32 uChirps)
{
	struct LST_LIST *pChirpList;
	struct CHNL_IRP *pChirp;
	u32 i;

	pChirpList = LST_Create();

	if (pChirpList) {
		/* Make N chirps and place on queue. */
		for (i = 0; (i < uChirps) && ((pChirp = MakeNewChirp()) !=
		    NULL); i++) {
			LST_PutTail(pChirpList, (struct LST_ELEM *)pChirp);
		}

		/* If we couldn't allocate all chirps, free those allocated: */
		if (i != uChirps) {
			FreeChirpList(pChirpList);
			pChirpList = NULL;
		}
	}

	return pChirpList;
}

/*
 *  ======== FreeChirpList ========
 *  Purpose:
 *      Free the queue of Chirps.
 */
static void FreeChirpList(struct LST_LIST *pChirpList)
{
	DBC_Require(pChirpList != NULL);

	while (!LST_IsEmpty(pChirpList))
		MEM_Free(LST_GetHead(pChirpList));

	LST_Delete(pChirpList);
}

/*
 *  ======== MakeNewChirp ========
 *      Allocate the memory for a new channel IRP.
 */
static struct CHNL_IRP *MakeNewChirp(void)
{
	struct CHNL_IRP *pChirp;

	pChirp = (struct CHNL_IRP *)MEM_Calloc(
		 sizeof(struct CHNL_IRP), MEM_NONPAGED);
	if (pChirp != NULL) {
		/* LST_InitElem only resets the list's member values. */
		LST_InitElem(&pChirp->link);
	}

	return pChirp;
}

/*
 *  ======== SearchFreeChannel ========
 *      Search for a free channel slot in the array of channel pointers.
 */
static DSP_STATUS SearchFreeChannel(struct CHNL_MGR *pChnlMgr,
				   OUT u32 *pdwChnl)
{
	DSP_STATUS status = CHNL_E_OUTOFSTREAMS;
	u32 i;

	DBC_Require(MEM_IsValidHandle(pChnlMgr, CHNL_MGRSIGNATURE));

	for (i = 0; i < pChnlMgr->cChannels; i++) {
		if (pChnlMgr->apChannel[i] == NULL) {
			status = DSP_SOK;
			*pdwChnl = i;
			break;
		}
	}

	return status;
}
