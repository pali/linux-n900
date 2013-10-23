/*
 * chnl.c
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
 *  ======== chnl.c ========
 *  Description:
 *      WCD channel interface: multiplexes data streams through the single
 *      physical link managed by a 'Bridge mini-driver.
 *
 *  Public Functions:
 *      CHNL_Close
 *      CHNL_CloseOrphans
 *      CHNL_Create
 *      CHNL_Destroy
 *      CHNL_Exit
 *      CHNL_GetHandle
 *      CHNL_GetProcessHandle
 *      CHNL_Init
 *      CHNL_Open
 *
 *  Notes:
 *      This interface is basically a pass through to the WMD CHNL functions,
 *      except for the CHNL_Get() accessor functions which call
 *      WMD_CHNL_GetInfo().
 *
 *! Revision History:
 *! ================
 *! 24-Feb-2003 swa PMGR Code review comments incorporated.
 *! 07-Jan-2002 ag  CHNL_CloseOrphans() now closes supported # of channels.
 *! 17-Nov-2000 jeh Removed IRQ, shared memory stuff from CHNL_Create.
 *! 28-Feb-2000 rr: New GT USage Implementation
 *! 03-Feb-2000 rr: GT and Module init/exit Changes.(Done up front from
 *!		    SERVICES)
 *! 21-Jan-2000 ag: Added code review comments.
 *! 13-Jan-2000 rr: CFG_Get/SetPrivateDword renamed to CFG_Get/SetDevObject.
 *! 08-Dec-1999 ag: CHNL_[Alloc|Free]Buffer bufs taken from client process heap.
 *! 02-Dec-1999 ag: Implemented CHNL_GetEventHandle().
 *! 17-Nov-1999 ag: CHNL_AllocBuffer() allocs extra word for process mapping.
 *! 28-Oct-1999 ag: WinCE port. Search for "WinCE" for changes(TBR).
 *! 07-Jan-1998 gp: CHNL_[Alloc|Free]Buffer now call MEM_UMB functions.
 *! 22-Oct-1997 gp: Removed requirement in CHNL_Open that hReserved1 != NULL.
 *! 30-Aug-1997 cr: Renamed cfg.h wbwcd.h b/c of WINNT file name collision.
 *! 10-Mar-1997 gp: Added GT trace.
 *! 14-Jan-1997 gp: Updated based on code review feedback.
 *! 03-Jan-1997 gp: Moved CHNL_AllocBuffer/CHNL_FreeBuffer code from udspsys.
 *! 14-Dec-1996 gp: Added uChnlId parameter to CHNL_Open().
 *! 09-Sep-1996 gp: Added CHNL_GetProcessHandle().
 *! 15-Jul-1996 gp: Created.
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
#include <dspbridge/cfg.h>
#include <dspbridge/csl.h>
#include <dspbridge/dpc.h>
#include <dspbridge/list.h>
#include <dspbridge/mem.h>
#include <dspbridge/sync.h>

/*  ----------------------------------- Platform Manager */
#include <dspbridge/proc.h>
#include <dspbridge/dev.h>

/*  ----------------------------------- Others */
#include <dspbridge/chnlpriv.h>
#include <chnlobj.h>

/*  ----------------------------------- This */
#include <dspbridge/chnl.h>

/*  ----------------------------------- Globals */
static u32 cRefs;
#if GT_TRACE
static struct GT_Mask CHNL_DebugMask = { NULL, NULL };	/* WCD CHNL Mask */
#endif



/*
 *  ======== CHNL_Create ========
 *  Purpose:
 *      Create a channel manager object, responsible for opening new channels
 *      and closing old ones for a given 'Bridge board.
 */
DSP_STATUS CHNL_Create(OUT struct CHNL_MGR **phChnlMgr,
		       struct DEV_OBJECT *hDevObject,
		       IN CONST struct CHNL_MGRATTRS *pMgrAttrs)
{
	DSP_STATUS status;
	struct CHNL_MGR *hChnlMgr;
	struct CHNL_MGR_ *pChnlMgr = NULL;

	DBC_Require(cRefs > 0);
	DBC_Require(phChnlMgr != NULL);
	DBC_Require(pMgrAttrs != NULL);

	GT_3trace(CHNL_DebugMask, GT_ENTER,
		  "Entered CHNL_Create: phChnlMgr: 0x%x\t"
		  "hDevObject: 0x%x\tpMgrAttrs:0x%x\n",
		  phChnlMgr, hDevObject, pMgrAttrs);

	*phChnlMgr = NULL;

	/* Validate args: */
	if ((0 < pMgrAttrs->cChannels) &&
	   (pMgrAttrs->cChannels <= CHNL_MAXCHANNELS)) {
		status = DSP_SOK;
	} else if (pMgrAttrs->cChannels == 0) {
		status = DSP_EINVALIDARG;
		GT_0trace(CHNL_DebugMask, GT_7CLASS,
			  "CHNL_Create:Invalid Args\n");
	} else {
		status = CHNL_E_MAXCHANNELS;
		GT_0trace(CHNL_DebugMask, GT_7CLASS,
			  "CHNL_Create:Error Max Channels\n");
	}
	if (pMgrAttrs->uWordSize == 0) {
		status = CHNL_E_INVALIDWORDSIZE;
		GT_0trace(CHNL_DebugMask, GT_7CLASS,
			  "CHNL_Create:Invalid Word size\n");
	}
	if (DSP_SUCCEEDED(status)) {
		status = DEV_GetChnlMgr(hDevObject, &hChnlMgr);
		if (DSP_SUCCEEDED(status) && hChnlMgr != NULL)
			status = CHNL_E_MGREXISTS;

	}

	if (DSP_SUCCEEDED(status)) {
		struct WMD_DRV_INTERFACE *pIntfFxns;
		DEV_GetIntfFxns(hDevObject, &pIntfFxns);
		/* Let WMD channel module finish the create: */
		status = (*pIntfFxns->pfnChnlCreate)(&hChnlMgr, hDevObject,
			  pMgrAttrs);
		if (DSP_SUCCEEDED(status)) {
			/* Fill in WCD channel module's fields of the
			 * CHNL_MGR structure */
			pChnlMgr = (struct CHNL_MGR_ *)hChnlMgr;
			pChnlMgr->pIntfFxns = pIntfFxns;
			/* Finally, return the new channel manager handle: */
			*phChnlMgr = hChnlMgr;
			GT_1trace(CHNL_DebugMask, GT_1CLASS,
				  "CHNL_Create: Success pChnlMgr:"
				  "0x%x\n", pChnlMgr);
		}
	}

	GT_2trace(CHNL_DebugMask, GT_ENTER,
		  "Exiting CHNL_Create: pChnlMgr: 0x%x,"
		  "status: 0x%x\n", pChnlMgr, status);
	DBC_Ensure(DSP_FAILED(status) || CHNL_IsValidMgr(pChnlMgr));

	return status;
}

/*
 *  ======== CHNL_Destroy ========
 *  Purpose:
 *      Close all open channels, and destroy the channel manager.
 */
DSP_STATUS CHNL_Destroy(struct CHNL_MGR *hChnlMgr)
{
	struct CHNL_MGR_ *pChnlMgr = (struct CHNL_MGR_ *)hChnlMgr;
	struct WMD_DRV_INTERFACE *pIntfFxns;
	DSP_STATUS status;

	DBC_Require(cRefs > 0);

	GT_1trace(CHNL_DebugMask, GT_ENTER,
		  "Entered CHNL_Destroy: hChnlMgr: 0x%x\n", hChnlMgr);
	if (CHNL_IsValidMgr(pChnlMgr)) {
		pIntfFxns = pChnlMgr->pIntfFxns;
		/* Let WMD channel module destroy the CHNL_MGR: */
		status = (*pIntfFxns->pfnChnlDestroy)(hChnlMgr);
	} else {
		GT_0trace(CHNL_DebugMask, GT_7CLASS,
			  "CHNL_Destroy:Invalid Handle\n");
		status = DSP_EHANDLE;
	}

	GT_2trace(CHNL_DebugMask, GT_ENTER,
		  "Exiting CHNL_Destroy: pChnlMgr: 0x%x,"
		  " status:0x%x\n", pChnlMgr, status);
	DBC_Ensure(DSP_FAILED(status) || !CHNL_IsValidMgr(pChnlMgr));

	return status;
}

/*
 *  ======== CHNL_Exit ========
 *  Purpose:
 *      Discontinue usage of the CHNL module.
 */
void CHNL_Exit(void)
{
	DBC_Require(cRefs > 0);

	cRefs--;

	GT_1trace(CHNL_DebugMask, GT_5CLASS,
		  "Entered CHNL_Exit, ref count: 0x%x\n", cRefs);

	DBC_Ensure(cRefs >= 0);
}


/*
 *  ======== CHNL_Init ========
 *  Purpose:
 *      Initialize the CHNL module's private state.
 */
bool CHNL_Init(void)
{
	bool fRetval = true;

	DBC_Require(cRefs >= 0);

	if (cRefs == 0) {
		DBC_Assert(!CHNL_DebugMask.flags);
		GT_create(&CHNL_DebugMask, "CH");   /* "CH" for CHannel */
	}

	if (fRetval)
		cRefs++;

	GT_1trace(CHNL_DebugMask, GT_5CLASS,
		  "Entered CHNL_Init, ref count: 0x%x\n",
		  cRefs);

	DBC_Ensure((fRetval && (cRefs > 0)) || (!fRetval && (cRefs >= 0)));

	return fRetval;
}


