/*
 * msg.c
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
 *  ======== msg.c ========
 *  Description:
 *      DSP/BIOS Bridge MSG Module.
 *
 *  Public Functions:
 *      MSG_Create
 *      MSG_Delete
 *      MSG_Exit
 *      MSG_Init
 *
 *! Revision History:
 *! =================
 *! 24-Feb-2003 swa 	PMGR Code review comments incorporated.
 *! 15-May-2001 ag      Changed SUCCEEDED to DSP_SUCCEEDED.
 *! 16-Feb-2001 jeh     Fixed some comments.
 *! 15-Dec-2000 rr      MSG_Create returns DSP_EFAIL if pfnMsgCreate fails.
 *! 12-Sep-2000 jeh     Created.
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
#include <dspbridge/list.h>
#include <dspbridge/mem.h>

/*  ----------------------------------- Mini Driver */
#include <dspbridge/wmd.h>

/*  ----------------------------------- Platform Manager */
#include <dspbridge/dev.h>

/*  ----------------------------------- This */
#include <msgobj.h>
#include <dspbridge/msg.h>

/*  ----------------------------------- Globals */
#if GT_TRACE
static struct GT_Mask MSG_debugMask = { NULL, NULL };	/* GT trace variable */
#endif
static u32 cRefs;		/* module reference count */

/*
 *  ======== MSG_Create ========
 *  Purpose:
 *      Create an object to manage message queues. Only one of these objects
 *      can exist per device object.
 */
DSP_STATUS MSG_Create(OUT struct MSG_MGR **phMsgMgr,
		      struct DEV_OBJECT *hDevObject, MSG_ONEXIT msgCallback)
{
	struct WMD_DRV_INTERFACE *pIntfFxns;
	struct MSG_MGR_ *pMsgMgr;
	struct MSG_MGR *hMsgMgr;
	DSP_STATUS status = DSP_SOK;

	DBC_Require(cRefs > 0);
	DBC_Require(phMsgMgr != NULL);
	DBC_Require(msgCallback != NULL);
	DBC_Require(hDevObject != NULL);

	GT_3trace(MSG_debugMask, GT_ENTER, "MSG_Create: phMsgMgr: 0x%x\t"
		 "hDevObject: 0x%x\tmsgCallback: 0x%x\n",
		 phMsgMgr, hDevObject, msgCallback);

	*phMsgMgr = NULL;

	DEV_GetIntfFxns(hDevObject, &pIntfFxns);

	/* Let WMD message module finish the create: */
	status = (*pIntfFxns->pfnMsgCreate)(&hMsgMgr, hDevObject, msgCallback);

	if (DSP_SUCCEEDED(status)) {
		/* Fill in WCD message module's fields of the MSG_MGR
		 * structure */
		pMsgMgr = (struct MSG_MGR_ *)hMsgMgr;
		pMsgMgr->pIntfFxns = pIntfFxns;

		/* Finally, return the new message manager handle: */
		*phMsgMgr = hMsgMgr;
		GT_1trace(MSG_debugMask, GT_1CLASS,
			 "MSG_Create: Success pMsgMgr: 0x%x\n",	pMsgMgr);
	} else {
		status = DSP_EFAIL;
	}
	return status;
}

/*
 *  ======== MSG_Delete ========
 *  Purpose:
 *      Delete a MSG manager allocated in MSG_Create().
 */
void MSG_Delete(struct MSG_MGR *hMsgMgr)
{
	struct MSG_MGR_ *pMsgMgr = (struct MSG_MGR_ *)hMsgMgr;
	struct WMD_DRV_INTERFACE *pIntfFxns;

	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(pMsgMgr, MSGMGR_SIGNATURE));

	GT_1trace(MSG_debugMask, GT_ENTER, "MSG_Delete: hMsgMgr: 0x%x\n",
		 hMsgMgr);

	pIntfFxns = pMsgMgr->pIntfFxns;

	/* Let WMD message module destroy the MSG_MGR: */
	(*pIntfFxns->pfnMsgDelete)(hMsgMgr);

	DBC_Ensure(!MEM_IsValidHandle(pMsgMgr, MSGMGR_SIGNATURE));
}

/*
 *  ======== MSG_Exit ========
 */
void MSG_Exit(void)
{
	DBC_Require(cRefs > 0);
	cRefs--;
	GT_1trace(MSG_debugMask, GT_5CLASS,
		 "Entered MSG_Exit, ref count: 0x%x\n",	cRefs);
	DBC_Ensure(cRefs >= 0);
}

/*
 *  ======== MSG_Init ========
 */
bool MSG_Init(void)
{
	DBC_Require(cRefs >= 0);

	if (cRefs == 0) {
		DBC_Assert(!MSG_debugMask.flags);
		GT_create(&MSG_debugMask, "MS");	/* "MS" for MSg */
	}

	cRefs++;

	GT_1trace(MSG_debugMask, GT_5CLASS, "MSG_Init(), ref count:  0x%x\n",
		 cRefs);

	DBC_Ensure(cRefs >= 0);

	return true;
}

