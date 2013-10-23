/*
 * io.c
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
 *  ======== io.c ========
 *  Description:
 *      IO manager interface: Manages IO between CHNL and MSG.
 *
 *  Public Functions:
 *      IO_Create
 *      IO_Destroy
 *      IO_Exit
 *      IO_Init
 *      IO_OnLoaded
 *
 *  Notes:
 *      This interface is basically a pass through to the WMD IO functions.
 *
 *! Revision History:
 *! ================
 *! 24-Feb-2003 swa 	PMGR Code review comments incorporated.
 *! 04-Apr-2001 rr      WSX_STATUS initialized in IO_Create.
 *! 07-Nov-2000 jeh     Created.
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
#include <dspbridge/mem.h>

/*  ----------------------------------- Platform Manager */
#include <dspbridge/dev.h>

/*  ----------------------------------- This */
#include <ioobj.h>
#include <dspbridge/iodefs.h>
#include <dspbridge/io.h>

/*  ----------------------------------- Globals */
static u32 cRefs;

#if GT_TRACE
static struct GT_Mask IO_DebugMask = { NULL, NULL };	/* WCD IO Mask */
#endif

/*
 *  ======== IO_Create ========
 *  Purpose:
 *      Create an IO manager object, responsible for managing IO between
 *      CHNL and MSG
 */
DSP_STATUS IO_Create(OUT struct IO_MGR **phIOMgr, struct DEV_OBJECT *hDevObject,
		    IN CONST struct IO_ATTRS *pMgrAttrs)
{
	struct WMD_DRV_INTERFACE *pIntfFxns;
	struct IO_MGR *hIOMgr = NULL;
	struct IO_MGR_ *pIOMgr = NULL;
	DSP_STATUS status = DSP_SOK;

	DBC_Require(cRefs > 0);
	DBC_Require(phIOMgr != NULL);
	DBC_Require(pMgrAttrs != NULL);

	GT_3trace(IO_DebugMask, GT_ENTER, "Entered IO_Create: phIOMgr: 0x%x\t "
		 "hDevObject: 0x%x\tpMgrAttrs: 0x%x\n",
		 phIOMgr, hDevObject, pMgrAttrs);

	*phIOMgr = NULL;

	/* A memory base of 0 implies no memory base:  */
	if ((pMgrAttrs->dwSMBase != 0) && (pMgrAttrs->uSMLength == 0)) {
		status = CHNL_E_INVALIDMEMBASE;
		GT_0trace(IO_DebugMask, GT_7CLASS,
			 "IO_Create:Invalid Mem Base\n");
	}

	if (pMgrAttrs->uWordSize == 0) {
		status = CHNL_E_INVALIDWORDSIZE;
		GT_0trace(IO_DebugMask, GT_7CLASS,
			 "IO_Create:Invalid Word size\n");
	}

	if (DSP_SUCCEEDED(status)) {
		DEV_GetIntfFxns(hDevObject, &pIntfFxns);

		/* Let WMD channel module finish the create: */
		status = (*pIntfFxns->pfnIOCreate)(&hIOMgr, hDevObject,
			 pMgrAttrs);

		if (DSP_SUCCEEDED(status)) {
			pIOMgr = (struct IO_MGR_ *) hIOMgr;
			pIOMgr->pIntfFxns = pIntfFxns;
			pIOMgr->hDevObject = hDevObject;

			/* Return the new channel manager handle: */
			*phIOMgr = hIOMgr;
			GT_1trace(IO_DebugMask, GT_1CLASS,
				 "IO_Create: Success hIOMgr: 0x%x\n",
				 hIOMgr);
		}
	}

	GT_2trace(IO_DebugMask, GT_ENTER,
		 "Exiting IO_Create: hIOMgr: 0x%x, status:"
		 " 0x%x\n", hIOMgr, status);

	return status;
}

/*
 *  ======== IO_Destroy ========
 *  Purpose:
 *      Delete IO manager.
 */
DSP_STATUS IO_Destroy(struct IO_MGR *hIOMgr)
{
	struct WMD_DRV_INTERFACE *pIntfFxns;
	struct IO_MGR_ *pIOMgr = (struct IO_MGR_ *)hIOMgr;
	DSP_STATUS status;

	DBC_Require(cRefs > 0);

	GT_1trace(IO_DebugMask, GT_ENTER, "Entered IO_Destroy: hIOMgr: 0x%x\n",
		  hIOMgr);

	pIntfFxns = pIOMgr->pIntfFxns;

	/* Let WMD channel module destroy the IO_MGR: */
	status = (*pIntfFxns->pfnIODestroy) (hIOMgr);

	GT_2trace(IO_DebugMask, GT_ENTER,
		 "Exiting IO_Destroy: pIOMgr: 0x%x, status:"
		 " 0x%x\n", pIOMgr, status);
	return status;
}

/*
 *  ======== IO_Exit ========
 *  Purpose:
 *      Discontinue usage of the IO module.
 */
void IO_Exit(void)
{
	DBC_Require(cRefs > 0);

	cRefs--;

	GT_1trace(IO_DebugMask, GT_5CLASS,
		 "Entered IO_Exit, ref count: 0x%x\n", cRefs);

	DBC_Ensure(cRefs >= 0);
}

/*
 *  ======== IO_Init ========
 *  Purpose:
 *      Initialize the IO module's private state.
 */
bool IO_Init(void)
{
	bool fRetval = true;

	DBC_Require(cRefs >= 0);

	if (cRefs == 0) {
		DBC_Assert(!IO_DebugMask.flags);
		GT_create(&IO_DebugMask, "IO");	/* "IO" for IO */
	}

	if (fRetval)
		cRefs++;


	GT_1trace(IO_DebugMask, GT_5CLASS,
		 "Entered IO_Init, ref count: 0x%x\n", cRefs);

	DBC_Ensure((fRetval && (cRefs > 0)) || (!fRetval && (cRefs >= 0)));

	return fRetval;
}
