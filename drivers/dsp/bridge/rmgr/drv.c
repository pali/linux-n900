/*
 * drv.c
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
 *  ======== drv.c ========
 *  Description:
 *      DSP/BIOS Bridge resource allocation module.
 *
 *  Public Functions:
 *      DRV_Create
 *      DRV_Destroy
 *      DRV_Exit
 *      DRV_GetDevObject
 *      DRV_GetDevExtension
 *      DRV_GetFirstDevObject
 *      DRV_GetNextDevObject
 *      DRV_GetNextDevExtension
 *      DRV_Init
 *      DRV_InsertDevObject
 *      DRV_RemoveDevObject
 *      DRV_RequestResources
 *      DRV_ReleaseResources
 *
 *! Revision History
 *! ======== ========
 *! 19-Apr-2004 sb: Replaced OS specific APIs with MEM_AllocPhysMem and
		    MEM_FreePhysMem. Fixed warnings. Cosmetic updates.
 *! 12-Apr-2004 hp: IVA clean up during bridge-uninstall
 *! 05-Jan-2004 vp: Updated for 24xx platform
 *! 21-Mar-2003 sb: Get SHM size from registry
 *! 10-Feb-2003 vp: Code review updates
 *! 18-Oct-2002 vp: Ported to Linux platform
 *! 30-Oct-2000 kc: Modified usage of REG_SetValue.
 *! 06-Sep-2000 jeh Read channel info into struct CFG_HOSTRES in
 *! 					RequestISAResources()
 *! 21-Sep-2000 rr: numwindows is calculated instead of default value in
 *!		 RequestISAResources.
 *! 07-Aug-2000 rr: static list of dev objects removed.
 *! 27-Jul-2000 rr: RequestResources split into two(Request and Release)
 *!		 Device extension created to hold the DevNodeString.
 *! 17-Jul-2000 rr: Driver Object holds the list of Device Objects.
 *!		 Added DRV_Create, DRV_Destroy, DRV_GetDevObject,
 *!		 DRV_GetFirst/NextDevObject, DRV_Insert/RemoveDevObject.
 *! 09-May-2000 rr: PCI Support is not L301 specific.Use of MEM_Calloc
 *!		 instead of MEM_Alloc.
 *! 28-Mar-2000 rr: PCI Support added. L301 Specific. TBD.
 *! 03-Feb-2000 rr: GT and Module Init/exit Changes. Merged with kc.
 *! 19-Jan-2000 rr: DBC_Ensure in RequestPCMCIA moved within PCCARD ifdef
 *! 29-Dec-1999 rr: PCCard support for any slot.Bus type stored in the
 *!		 struct CFG_HOSTRES Structure.
 *! 17-Dec-1999 rr: if PCCARD_Init fails we return DSP_EFAIL.
 *!		 DBC_Ensure checks for sucess and pDevice != NULL
 *! 11-Dec-1999 ag: #define "Isa" renamed to "IsaBus".
 *! 09-Dec-1999 rr: windows.h included to remove warnings.
 *! 02-Dec-1999 rr: struct GT_Mask is with in if DEBUG. Request resources checks
 *!		 status while making call to Reg functions.
 *! 23-Nov-1999 rr: windows.h included
 *! 19-Nov-1999 rr: DRV_RELEASE bug while setting the registry to zero.
 *!		 fixed.
 *! 12-Nov-1999 rr: RequestResources() reads values from the registry.
 *!		 Hardcoded bIRQRegister define removed.
 *! 05-Nov-1999 rr: Added hardcoded device interrupt.
 *! 25-Oct-1999 rr: Resource structure removed. Now it uses the Host
 *!		 Resource structure directly.
 *! 15-Oct-1999 rr: Resource Structure modified. See drv.h
 *!		 dwBusType taken from the registry.Hard coded
 *!		 registry entries removed.
 *! 05-Oct-1999 rr: Calling DEV_StartDevice moved to wcdce.c. DRV_Register
 *!		 MiniDriver has been renamed to DRV_RequestResources.
 *!		 DRV_UnRegisterMiniDriver fxn removed.
 *! 24-Sep-1999 rr: Significant changes to the RegisterMiniDriver fxns.
 *!		 Now it is simpler. IT stores the dev node in the
 *!		 registry, assign resources and calls the DEV_Start.
 *! 10-Sep-1999 rr: Register Minidriver modified.
 *!		 - Resource structure follows the NT model
 *! 08-Aug-1999 rr: Adopted for WinCE. Exports Fxns removed. Hull Created.
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
#include <dspbridge/list.h>
#include <dspbridge/mem.h>
#include <dspbridge/reg.h>

/*  ----------------------------------- Others */
#include <dspbridge/dbreg.h>

/*  ----------------------------------- This */
#include <dspbridge/drv.h>
#include <dspbridge/dev.h>

#ifndef RES_CLEANUP_DISABLE
#include <dspbridge/node.h>
#include <dspbridge/proc.h>
#include <dspbridge/strm.h>
#include <dspbridge/nodepriv.h>
#include <dspbridge/wmdchnl.h>
#include <dspbridge/resourcecleanup.h>
#endif

/*  ----------------------------------- Defines, Data Structures, Typedefs */
#define SIGNATURE   0x5f52474d	/* "DRV_" (in reverse) */

struct DRV_OBJECT {
	u32 dwSignature;
	struct LST_LIST *devList;
	struct LST_LIST *devNodeString;
#ifndef RES_CLEANUP_DISABLE
	struct PROCESS_CONTEXT  *procCtxtList;
#endif
};

/*
 *  This is the Device Extension. Named with the Prefix
 *  DRV_ since it is living in this module
 */
struct DRV_EXT {
	struct LST_ELEM link;
	char szString[MAXREGPATHLENGTH];
};

/*  ----------------------------------- Globals */
static s32 cRefs;

#if GT_TRACE
extern struct GT_Mask curTrace;
#endif

/*  ----------------------------------- Function Prototypes */
static DSP_STATUS RequestBridgeResources(u32 dwContext, s32 fRequest);
static DSP_STATUS RequestBridgeResourcesDSP(u32 dwContext, s32 fRequest);

#ifndef RES_CLEANUP_DISABLE
/* GPP PROCESS CLEANUP CODE */

static DSP_STATUS PrintProcessInformation(void);
static DSP_STATUS DRV_ProcFreeNodeRes(HANDLE hPCtxt);
static DSP_STATUS  DRV_ProcFreeSTRMRes(HANDLE hPCtxt);
extern enum NODE_STATE NODE_GetState(HANDLE hNode);

/* Get the process context list from driver object */

/* Set the Process ID */
DSP_STATUS DRV_ProcSetPID(HANDLE hPCtxt, s32 hProcess)
{
	struct PROCESS_CONTEXT *pCtxt = (struct PROCESS_CONTEXT *)hPCtxt;
	DSP_STATUS status = DSP_SOK;

	DBC_Assert(hPCtxt != NULL);

	pCtxt->pid = hProcess;
	return status;
}


/* Getting the head of the process context list */
DSP_STATUS DRV_GetProcCtxtList(struct PROCESS_CONTEXT **pPctxt,
				struct DRV_OBJECT *hDrvObject)
{
	DSP_STATUS status = DSP_SOK;
	struct DRV_OBJECT *pDrvObject = (struct DRV_OBJECT *)hDrvObject;

	DBC_Assert(hDrvObject != NULL);
	GT_2trace(curTrace, GT_ENTER,
		"DRV_GetProcCtxtList: 2 *pPctxt:%x, pDrvObject"
		":%x", *pPctxt, pDrvObject);
	*pPctxt = pDrvObject->procCtxtList;
	GT_2trace(curTrace, GT_ENTER,
		"DRV_GetProcCtxtList: 3 *pPctxt:%x, pDrvObject"
		":%x", *pPctxt, pDrvObject);
	return status;
}

/* Add a new process context to process context list */
DSP_STATUS DRV_InsertProcContext(struct DRV_OBJECT *hDrVObject, HANDLE hPCtxt)
{
	struct PROCESS_CONTEXT **pCtxt = (struct PROCESS_CONTEXT **)hPCtxt;
	DSP_STATUS status = DSP_SOK;
	struct PROCESS_CONTEXT *pCtxtList = NULL;
	struct DRV_OBJECT	     *hDRVObject;

	GT_0trace(curTrace, GT_ENTER, "\n In DRV_InsertProcContext\n");

	status = CFG_GetObject((u32 *)&hDRVObject, REG_DRV_OBJECT);
	DBC_Assert(hDRVObject != NULL);

	*pCtxt = MEM_Calloc(1 * sizeof(struct PROCESS_CONTEXT), MEM_PAGED);
	if (!*pCtxt) {
		pr_err("DSP: MEM_Calloc failed in DRV_InsertProcContext\n");
		return DSP_EMEMORY;
	}

	spin_lock_init(&(*pCtxt)->proc_list_lock);
	INIT_LIST_HEAD(&(*pCtxt)->processor_list);

	spin_lock_init(&(*pCtxt)->dmm_list_lock);

	GT_0trace(curTrace, GT_ENTER,
		 "\n In DRV_InsertProcContext Calling "
		 "DRV_GetProcCtxtList\n");
	DRV_GetProcCtxtList(&pCtxtList, hDRVObject);
	GT_0trace(curTrace, GT_ENTER,
		 "\n In DRV_InsertProcContext After Calling "
		 "DRV_GetProcCtxtList\n");
	if (pCtxtList != NULL) {
		GT_0trace(curTrace, GT_ENTER,
			 "\n In DRV_InsertProcContext and pCtxt is "
			 "not Null\n");
		while (pCtxtList->next != NULL)
			pCtxtList = pCtxtList->next;

		pCtxtList->next = *pCtxt;
	} else {
		GT_0trace(curTrace, GT_ENTER,
			 "\n In DRV_InsertProcContext and "
			 "pCtxt is Null\n");
		hDRVObject->procCtxtList = *pCtxt;
	}
	return status;
}

/* Delete a process context from process resource context list */
DSP_STATUS DRV_RemoveProcContext(struct DRV_OBJECT *hDRVObject,
		HANDLE pr_ctxt)
{
	DSP_STATUS status = DSP_SOK;
	struct PROCESS_CONTEXT *pr_ctxt_list = NULL;
	struct PROCESS_CONTEXT *uninitialized_var(ptr_prev);

	DBC_Assert(hDRVObject != NULL);

	GT_0trace(curTrace, GT_ENTER, "DRV_RemoveProcContext: 12");
	DRV_GetProcCtxtList(&pr_ctxt_list, hDRVObject);

	/* Special condition */
	if (pr_ctxt_list == pr_ctxt) {
		hDRVObject->procCtxtList = NULL;
		goto func_cont;
	}

	GT_0trace(curTrace, GT_ENTER, "DRV_RemoveProcContext: 13");
	while (pr_ctxt_list && (pr_ctxt_list != pr_ctxt)) {
		ptr_prev = pr_ctxt_list;
		pr_ctxt_list = pr_ctxt_list->next;
		GT_0trace(curTrace, GT_ENTER,
			 "DRV_RemoveProcContext: 2");
	}

	GT_0trace(curTrace, GT_ENTER, "DRV_RemoveProcContext: 3");

	if (!pr_ctxt_list)
		return DSP_ENOTFOUND;
	else
		ptr_prev->next = pr_ctxt_list->next;

func_cont:
	MEM_Free(pr_ctxt);
	GT_0trace(curTrace, GT_ENTER, "DRV_RemoveProcContext: 7");

	return status;
}

/* Update the state of process context */
DSP_STATUS DRV_ProcUpdatestate(HANDLE hPCtxt, enum GPP_PROC_RES_STATE status)
{
	struct PROCESS_CONTEXT *pCtxt = (struct PROCESS_CONTEXT *)hPCtxt;
	DSP_STATUS status1 = DSP_SOK;
	if (pCtxt != NULL) {
		pCtxt->resState = status;
	} else {
		GT_0trace(curTrace, GT_ENTER,
			 "DRV_ProcUpdatestate: Failed to update "
			 "process state");
	}
	return status1;
}

/* Allocate and add a node resource element
* This function is called from .Node_Allocate.  */
DSP_STATUS DRV_InsertNodeResElement(HANDLE hNode, HANDLE hNodeRes,
					HANDLE hPCtxt)
{
	struct NODE_RES_OBJECT **pNodeRes = (struct NODE_RES_OBJECT **)hNodeRes;
	struct PROCESS_CONTEXT *pCtxt = (struct PROCESS_CONTEXT *)hPCtxt;
	DSP_STATUS status = DSP_SOK;
	struct NODE_RES_OBJECT   *pTempNodeRes = NULL;
	GT_0trace(curTrace, GT_ENTER, "DRV_InsertNodeResElement: 1");
	*pNodeRes = (struct NODE_RES_OBJECT *)MEM_Calloc
		    (1 * sizeof(struct NODE_RES_OBJECT), MEM_PAGED);
	DBC_Assert(hPCtxt != NULL);
	if ((*pNodeRes == NULL) || (hPCtxt == NULL)) {
		GT_0trace(curTrace, GT_ENTER, "DRV_InsertNodeResElement: 12");
		status = DSP_EHANDLE;
	}
	if (DSP_SUCCEEDED(status)) {
		(*pNodeRes)->hNode = hNode;
		if (pCtxt->pNodeList != NULL) {
			pTempNodeRes = pCtxt->pNodeList;
			while (pTempNodeRes->next != NULL)
				pTempNodeRes = pTempNodeRes->next;

			pTempNodeRes->next = *pNodeRes;
			GT_0trace(curTrace, GT_ENTER,
				 "DRV_InsertNodeResElement: 2");
		} else {
			pCtxt->pNodeList = *pNodeRes;
			GT_0trace(curTrace, GT_ENTER,
				 "DRV_InsertNodeResElement: 3");
		}
	}
	GT_0trace(curTrace, GT_ENTER, "DRV_InsertNodeResElement: 4");
	return status;
}

/* Release all Node resources and its context
* This is called from .Node_Delete.  */
DSP_STATUS DRV_RemoveNodeResElement(HANDLE hNodeRes, HANDLE hPCtxt)
{
	struct NODE_RES_OBJECT *pNodeRes = (struct NODE_RES_OBJECT *)hNodeRes;
	struct PROCESS_CONTEXT *pCtxt = (struct PROCESS_CONTEXT *)hPCtxt;
	DSP_STATUS	status = DSP_SOK;
	struct NODE_RES_OBJECT *pTempNode2 = pCtxt->pNodeList;
	struct NODE_RES_OBJECT *pTempNode = pCtxt->pNodeList;

	DBC_Assert(hPCtxt != NULL);
	GT_0trace(curTrace, GT_ENTER, "\nDRV_RemoveNodeResElement: 1\n");
	while ((pTempNode != NULL) && (pTempNode != pNodeRes)) {
		pTempNode2 = pTempNode;
		pTempNode = pTempNode->next;
	}
	if (pCtxt->pNodeList == pNodeRes)
		pCtxt->pNodeList = pNodeRes->next;

	if (pTempNode == NULL)
		return DSP_ENOTFOUND;
	else if (pTempNode2->next != NULL)
		pTempNode2->next = pTempNode2->next->next;

	MEM_Free(pTempNode);
	return status;
}

/* Actual Node De-Allocation */
static DSP_STATUS DRV_ProcFreeNodeRes(HANDLE hPCtxt)
{
	struct PROCESS_CONTEXT *pCtxt = (struct PROCESS_CONTEXT *)hPCtxt;
	DSP_STATUS status = DSP_SOK;
	struct NODE_RES_OBJECT *pNodeList = NULL;
	struct NODE_RES_OBJECT *pNodeRes = NULL;
	u32  nState;

	DBC_Assert(hPCtxt != NULL);
	pNodeList = pCtxt->pNodeList;
	while (pNodeList != NULL) {
		GT_0trace(curTrace, GT_ENTER, "DRV_ProcFreeNodeRes: 1");
		pNodeRes = pNodeList;
		pNodeList = pNodeList->next;
		if (pNodeRes->nodeAllocated) {
			nState = NODE_GetState(pNodeRes->hNode) ;
			GT_1trace(curTrace, GT_5CLASS,
				"DRV_ProcFreeNodeRes: Node state %x\n", nState);
			if (nState <= NODE_DELETING) {
				if ((nState == NODE_RUNNING) ||
					(nState == NODE_PAUSED) ||
					(nState == NODE_TERMINATING)) {
					GT_1trace(curTrace, GT_5CLASS,
					"Calling Node_Terminate for Node:"
					" 0x%x\n", pNodeRes->hNode);
					status = NODE_Terminate
						(pNodeRes->hNode, &status);
					GT_1trace(curTrace, GT_5CLASS,
						 "Calling Node_Delete for Node:"
						 " 0x%x\n", pNodeRes->hNode);
					status = NODE_Delete(pNodeRes->hNode,
							pCtxt);
					GT_1trace(curTrace, GT_5CLASS,
					"the status after the NodeDelete %x\n",
					status);
				} else if ((nState == NODE_ALLOCATED)
					|| (nState == NODE_CREATED))
					status = NODE_Delete(pNodeRes->hNode,
							pCtxt);
			}
		}
	}
	return status;
}

/* Allocate the DMM resource element
* This is called from Proc_Map. after the actual resource is allocated */
DSP_STATUS DRV_InsertDMMResElement(HANDLE hDMMRes, HANDLE hPCtxt)
{
	struct PROCESS_CONTEXT *pCtxt = (struct PROCESS_CONTEXT *)hPCtxt;
	struct DMM_RES_OBJECT **pDMMRes = (struct DMM_RES_OBJECT **)hDMMRes;
	DSP_STATUS	status = DSP_SOK;
	struct DMM_RES_OBJECT *pTempDMMRes = NULL;

	*pDMMRes = (struct DMM_RES_OBJECT *)
		    MEM_Calloc(1 * sizeof(struct DMM_RES_OBJECT), MEM_PAGED);
	DBC_Assert(hPCtxt != NULL);
	GT_0trace(curTrace, GT_ENTER, "DRV_InsertDMMResElement: 1");
	if ((*pDMMRes == NULL) || (hPCtxt == NULL)) {
		GT_0trace(curTrace, GT_5CLASS, "DRV_InsertDMMResElement: 2");
		status = DSP_EHANDLE;
	}
	if (DSP_SUCCEEDED(status)) {
		if (pCtxt->pDMMList != NULL) {
			GT_0trace(curTrace, GT_5CLASS,
				 "DRV_InsertDMMResElement: 3");
			pTempDMMRes = pCtxt->pDMMList;
			while (pTempDMMRes->next != NULL)
				pTempDMMRes = pTempDMMRes->next;

			pTempDMMRes->next = *pDMMRes;
		} else {
			pCtxt->pDMMList = *pDMMRes;
			GT_0trace(curTrace, GT_5CLASS,
				 "DRV_InsertDMMResElement: 4");
		}
	}
	GT_0trace(curTrace, GT_ENTER, "DRV_InsertDMMResElement: 5");
	return status;
}



/* Release DMM resource element context
* This is called from Proc_UnMap. after the actual resource is freed */
DSP_STATUS DRV_RemoveDMMResElement(HANDLE hDMMRes, HANDLE hPCtxt)
{
	struct PROCESS_CONTEXT *pCtxt = (struct PROCESS_CONTEXT *)hPCtxt;
	struct DMM_RES_OBJECT *pDMMRes = (struct DMM_RES_OBJECT *)hDMMRes;
	DSP_STATUS status = DSP_SOK;
	struct DMM_RES_OBJECT *pTempDMMRes2 = NULL;
	struct DMM_RES_OBJECT *pTempDMMRes = NULL;

	DBC_Assert(hPCtxt != NULL);
	pTempDMMRes2 = pCtxt->pDMMList;
	pTempDMMRes = pCtxt->pDMMList;
	GT_0trace(curTrace, GT_ENTER, "DRV_RemoveDMMResElement: 1");
	while ((pTempDMMRes != NULL) && (pTempDMMRes != pDMMRes)) {
		GT_0trace(curTrace, GT_ENTER, "DRV_RemoveDMMResElement: 2");
		pTempDMMRes2 = pTempDMMRes;
		pTempDMMRes = pTempDMMRes->next;
	}
	GT_0trace(curTrace, GT_ENTER, "DRV_RemoveDMMResElement: 3");
	if (pCtxt->pDMMList == pTempDMMRes)
		pCtxt->pDMMList = pTempDMMRes->next;

	if (pTempDMMRes == NULL)
		return DSP_ENOTFOUND;
	else if (pTempDMMRes2->next != NULL)
		pTempDMMRes2->next = pTempDMMRes2->next->next;

	MEM_Free(pDMMRes);
	GT_0trace(curTrace, GT_ENTER, "DRV_RemoveDMMResElement: 4");
	return status;
}

/* Update DMM resource status */
DSP_STATUS DRV_UpdateDMMResElement(HANDLE hDMMRes, u32 pMpuAddr, u32 ulSize,
				  u32 pReqAddr, u32 pMapAddr,
				  HANDLE hProcessor)
{
	struct DMM_RES_OBJECT *pDMMRes = (struct DMM_RES_OBJECT *)hDMMRes;
	DSP_STATUS status = DSP_SOK;

	DBC_Assert(hDMMRes != NULL);
	pDMMRes->ulMpuAddr = pMpuAddr;
	pDMMRes->ulDSPAddr = pMapAddr;
	pDMMRes->ulDSPResAddr = pReqAddr;
	pDMMRes->dmmSize = ulSize;
	pDMMRes->hProcessor = hProcessor;
	pDMMRes->dmmAllocated = 1;

	return status;
}

/* Actual DMM De-Allocation */
DSP_STATUS DRV_ProcFreeDMMRes(HANDLE hPCtxt)
{
	struct PROCESS_CONTEXT *pCtxt = (struct PROCESS_CONTEXT *)hPCtxt;
	DSP_STATUS status = DSP_SOK;
	struct DMM_RES_OBJECT *pDMMList = pCtxt->pDMMList;
	struct DMM_RES_OBJECT *pDMMRes = NULL;

	DBC_Assert(hPCtxt != NULL);
	GT_0trace(curTrace, GT_ENTER, "\nDRV_ProcFreeDMMRes: 1\n");
	while (pDMMList != NULL) {
		pDMMRes = pDMMList;
		pDMMList = pDMMList->next;
		if (pDMMRes->dmmAllocated) {
			status = PROC_UnMap(pDMMRes->hProcessor,
				 (void *)pDMMRes->ulDSPResAddr, pCtxt);
			status = PROC_UnReserveMemory(pDMMRes->hProcessor,
				 (void *)pDMMRes->ulDSPResAddr);
			pDMMRes->dmmAllocated = 0;
		}
	}
	return status;
}


/* Release all DMM resources and its context
* This is called from .bridge_release. */
DSP_STATUS DRV_RemoveAllDMMResElements(HANDLE hPCtxt)
{
	struct PROCESS_CONTEXT *pCtxt = (struct PROCESS_CONTEXT *)hPCtxt;
	DSP_STATUS status = DSP_SOK;
	struct DMM_RES_OBJECT *pTempDMMRes2 = NULL;
	struct DMM_RES_OBJECT *pTempDMMRes = NULL;

	DBC_Assert(pCtxt != NULL);
	DRV_ProcFreeDMMRes(pCtxt);
	pTempDMMRes = pCtxt->pDMMList;
	while (pTempDMMRes != NULL) {
		pTempDMMRes2 = pTempDMMRes;
		pTempDMMRes = pTempDMMRes->next;
		MEM_Free(pTempDMMRes2);
	}
	pCtxt->pDMMList = NULL;
	return status;
}

DSP_STATUS DRV_GetDMMResElement(u32 pMapAddr, HANDLE hDMMRes, HANDLE hPCtxt)
{
	struct PROCESS_CONTEXT *pCtxt = (struct PROCESS_CONTEXT *)hPCtxt;
	struct DMM_RES_OBJECT **pDMMRes = (struct DMM_RES_OBJECT **)hDMMRes;
	DSP_STATUS status = DSP_SOK;
	struct DMM_RES_OBJECT *pTempDMM2 = NULL;
	struct DMM_RES_OBJECT *pTempDMM = NULL;

	DBC_Assert(hPCtxt != NULL);
	pTempDMM = pCtxt->pDMMList;
	while ((pTempDMM != NULL) && (pTempDMM->ulDSPAddr != pMapAddr)) {
		GT_3trace(curTrace, GT_ENTER,
			 "DRV_GetDMMResElement: 2 pTempDMM:%x "
			 "pTempDMM->ulDSPAddr:%x pMapAddr:%x\n", pTempDMM,
			 pTempDMM->ulDSPAddr, pMapAddr);
		pTempDMM2 = pTempDMM;
		pTempDMM = pTempDMM->next;
	}
	if (pTempDMM != NULL) {
		GT_0trace(curTrace, GT_ENTER, "DRV_GetDMMResElement: 3");
		*pDMMRes = pTempDMM;
	} else {
		status = DSP_ENOTFOUND;
	} GT_0trace(curTrace, GT_ENTER, "DRV_GetDMMResElement: 4");
	return status;
}

/* Update Node allocation status */
void DRV_ProcNodeUpdateStatus(HANDLE hNodeRes, s32 status)
{
	struct NODE_RES_OBJECT *pNodeRes = (struct NODE_RES_OBJECT *)hNodeRes;
	DBC_Assert(hNodeRes != NULL);
	pNodeRes->nodeAllocated = status;
}

/* Update Node Heap status */
void DRV_ProcNodeUpdateHeapStatus(HANDLE hNodeRes, s32 status)
{
	struct NODE_RES_OBJECT *pNodeRes = (struct NODE_RES_OBJECT *)hNodeRes;
	DBC_Assert(hNodeRes != NULL);
	pNodeRes->heapAllocated = status;
}

/* Release all Node resources and its context
* This is called from .bridge_release.
*/
DSP_STATUS 	DRV_RemoveAllNodeResElements(HANDLE hPCtxt)
{
	struct PROCESS_CONTEXT *pCtxt = (struct PROCESS_CONTEXT *)hPCtxt;
	DSP_STATUS status = DSP_SOK;
	struct NODE_RES_OBJECT *pTempNode2 = NULL;
	struct NODE_RES_OBJECT *pTempNode = NULL;

	DBC_Assert(hPCtxt != NULL);
	DRV_ProcFreeNodeRes(pCtxt);
	pTempNode = pCtxt->pNodeList;
	while (pTempNode != NULL) {
		pTempNode2 = pTempNode;
		pTempNode = pTempNode->next;
		MEM_Free(pTempNode2);
	}
	pCtxt->pNodeList = NULL;
	return status;
}

/* Getting the node resource element */

DSP_STATUS DRV_GetNodeResElement(HANDLE hNode, HANDLE hNodeRes, HANDLE hPCtxt)
{
	struct NODE_RES_OBJECT **nodeRes = (struct NODE_RES_OBJECT **)hNodeRes;
	struct PROCESS_CONTEXT *pCtxt = (struct PROCESS_CONTEXT *)hPCtxt;
	DSP_STATUS status = DSP_SOK;
	struct NODE_RES_OBJECT *pTempNode2 = NULL;
	struct NODE_RES_OBJECT *pTempNode = NULL;

	DBC_Assert(hPCtxt != NULL);
	pTempNode = pCtxt->pNodeList;
	GT_0trace(curTrace, GT_ENTER, "DRV_GetNodeResElement: 1");
	while ((pTempNode != NULL) && (pTempNode->hNode != hNode)) {
		pTempNode2 = pTempNode;
		pTempNode = pTempNode->next;
	}
	if (pTempNode != NULL)
		*nodeRes = pTempNode;
	else
		status = DSP_ENOTFOUND;

	return status;
}



/* Allocate the STRM resource element
* This is called after the actual resource is allocated
*/
DSP_STATUS DRV_ProcInsertSTRMResElement(HANDLE hStreamHandle, HANDLE hSTRMRes,
					HANDLE hPCtxt)
{
	struct STRM_RES_OBJECT **pSTRMRes = (struct STRM_RES_OBJECT **)hSTRMRes;
	struct PROCESS_CONTEXT *pCtxt = (struct PROCESS_CONTEXT *)hPCtxt;
	DSP_STATUS status = DSP_SOK;
	struct STRM_RES_OBJECT *pTempSTRMRes = NULL;
	DBC_Assert(hPCtxt != NULL);

	*pSTRMRes = (struct STRM_RES_OBJECT *)
		    MEM_Calloc(1 * sizeof(struct STRM_RES_OBJECT), MEM_PAGED);
	if ((*pSTRMRes == NULL) || (hPCtxt == NULL)) {
		GT_0trace(curTrace, GT_ENTER, "DRV_InsertSTRMResElement: 2");
		status = DSP_EHANDLE;
	}
	if (DSP_SUCCEEDED(status)) {
		(*pSTRMRes)->hStream = hStreamHandle;
		if (pCtxt->pSTRMList != NULL) {
			GT_0trace(curTrace, GT_ENTER,
				 "DRV_InsertiSTRMResElement: 3");
			pTempSTRMRes = pCtxt->pSTRMList;
			while (pTempSTRMRes->next != NULL)
				pTempSTRMRes = pTempSTRMRes->next;

			pTempSTRMRes->next = *pSTRMRes;
		} else {
			pCtxt->pSTRMList = *pSTRMRes;
			GT_0trace(curTrace, GT_ENTER,
				 "DRV_InsertSTRMResElement: 4");
		}
	}
	return status;
}



/* Release Stream resource element context
* This function called after the actual resource is freed
*/
DSP_STATUS 	DRV_ProcRemoveSTRMResElement(HANDLE hSTRMRes, HANDLE hPCtxt)
{
	struct STRM_RES_OBJECT *pSTRMRes = (struct STRM_RES_OBJECT *)hSTRMRes;
	struct PROCESS_CONTEXT *pCtxt = (struct PROCESS_CONTEXT *)hPCtxt;
	DSP_STATUS status = DSP_SOK;
	struct STRM_RES_OBJECT *pTempSTRMRes2 = pCtxt->pSTRMList;
	struct STRM_RES_OBJECT *pTempSTRMRes = pCtxt->pSTRMList;

	DBC_Assert(hPCtxt != NULL);
	while ((pTempSTRMRes != NULL) && (pTempSTRMRes != pSTRMRes)) {
		pTempSTRMRes2 = pTempSTRMRes;
		pTempSTRMRes = pTempSTRMRes->next;
	}
	if (pCtxt->pSTRMList == pTempSTRMRes)
		pCtxt->pSTRMList = pTempSTRMRes->next;

	if (pTempSTRMRes == NULL)
		status = DSP_ENOTFOUND;
	else if (pTempSTRMRes2->next != NULL)
		pTempSTRMRes2->next = pTempSTRMRes2->next->next;

	MEM_Free(pSTRMRes);
	return status;
}


/* Actual Stream De-Allocation */
static DSP_STATUS  DRV_ProcFreeSTRMRes(HANDLE hPCtxt)
{
	struct PROCESS_CONTEXT *pCtxt = (struct PROCESS_CONTEXT *)hPCtxt;
	DSP_STATUS status = DSP_SOK;
	DSP_STATUS status1 = DSP_SOK;
	u8 **apBuffer = NULL;
	struct STRM_RES_OBJECT *pSTRMList = NULL;
	struct STRM_RES_OBJECT *pSTRMRes = NULL;
	u8 *pBufPtr;
	u32 ulBytes;
	u32 dwArg;
	s32 ulBufSize;


	DBC_Assert(hPCtxt != NULL);
	pSTRMList = pCtxt->pSTRMList;
	while (pSTRMList != NULL) {
		pSTRMRes = pSTRMList;
		pSTRMList = pSTRMList->next;
		if (pSTRMRes->uNumBufs != 0) {
			apBuffer = MEM_Alloc((pSTRMRes->uNumBufs *
					    sizeof(u8 *)), MEM_NONPAGED);
			status = STRM_FreeBuffer(pSTRMRes->hStream, apBuffer,
						pSTRMRes->uNumBufs, pCtxt);
			MEM_Free(apBuffer);
		}
		status = STRM_Close(pSTRMRes->hStream, pCtxt);
		if (DSP_FAILED(status)) {
			if (status == DSP_EPENDING) {
				status = STRM_Reclaim(pSTRMRes->hStream,
						     &pBufPtr, &ulBytes,
						     (u32 *)&ulBufSize, &dwArg);
				if (DSP_SUCCEEDED(status))
					status = STRM_Close(pSTRMRes->hStream,
							pCtxt);

			}
		}
	}
	return status1;
}

/* Release all Stream resources and its context
* This is called from .bridge_release.
*/
DSP_STATUS	DRV_RemoveAllSTRMResElements(HANDLE hPCtxt)
{
	struct PROCESS_CONTEXT *pCtxt = (struct PROCESS_CONTEXT *)hPCtxt;
	DSP_STATUS status = DSP_SOK;
	struct STRM_RES_OBJECT *pTempSTRMRes2 = NULL;
	struct STRM_RES_OBJECT *pTempSTRMRes = NULL;

	DBC_Assert(hPCtxt != NULL);
	DRV_ProcFreeSTRMRes(pCtxt);
	pTempSTRMRes = pCtxt->pSTRMList;
	while (pTempSTRMRes != NULL) {
		pTempSTRMRes2 = pTempSTRMRes;
		pTempSTRMRes = pTempSTRMRes->next;
		MEM_Free(pTempSTRMRes2);
	}
	pCtxt->pSTRMList = NULL;
	return status;
}


/* Getting the stream resource element */
DSP_STATUS DRV_GetSTRMResElement(HANDLE hStrm, HANDLE hSTRMRes, HANDLE hPCtxt)
{
	struct STRM_RES_OBJECT **STRMRes = (struct STRM_RES_OBJECT **)hSTRMRes;
	struct PROCESS_CONTEXT *pCtxt = (struct PROCESS_CONTEXT *)hPCtxt;
	DSP_STATUS status = DSP_SOK;
	struct STRM_RES_OBJECT *pTempSTRM2 = NULL;
	struct STRM_RES_OBJECT *pTempSTRM = pCtxt->pSTRMList;

	DBC_Assert(hPCtxt != NULL);
	while ((pTempSTRM != NULL) && (pTempSTRM->hStream != hStrm)) {
		GT_0trace(curTrace, GT_ENTER, "DRV_GetSTRMResElement: 2");
		pTempSTRM2 = pTempSTRM;
		pTempSTRM = pTempSTRM->next;
	}
	if (pTempSTRM != NULL) {
		GT_0trace(curTrace, GT_ENTER, "DRV_GetSTRMResElement: 3");
		*STRMRes = pTempSTRM;
	} else {
		GT_0trace(curTrace, GT_ENTER, "DRV_GetSTRMResElement: 4");
		status = DSP_ENOTFOUND;
	}
	GT_0trace(curTrace, GT_ENTER, "DRV_GetSTRMResElement: 5");
	return status;
}

/* Updating the stream resource element */
DSP_STATUS DRV_ProcUpdateSTRMRes(u32 uNumBufs, HANDLE hSTRMRes, HANDLE hPCtxt)
{
	DSP_STATUS status = DSP_SOK;
	struct STRM_RES_OBJECT **STRMRes = (struct STRM_RES_OBJECT **)hSTRMRes;

	DBC_Assert(hPCtxt != NULL);
	(*STRMRes)->uNumBufs = uNumBufs;
	return status;
}

/* Displaying the resources allocated by a process */
DSP_STATUS DRV_ProcDisplayResInfo(u8 *pBuf1, u32 *pSize)
{
	struct PROCESS_CONTEXT *pCtxt = NULL;
	struct NODE_RES_OBJECT *pNodeRes = NULL;
	struct DMM_RES_OBJECT *pDMMRes = NULL;
	struct STRM_RES_OBJECT *pSTRMRes = NULL;
	struct DSPHEAP_RES_OBJECT *pDSPHEAPRes = NULL;
	u32 tempCount = 1;
	HANDLE hDrvObject = NULL;
	void *pBuf = pBuf1;
	u8 pTempBuf[250];
	u32 tempStrLen = 0, tempStrLen2 = 0;
	DSP_STATUS status = DSP_SOK;

	CFG_GetObject((u32 *)&hDrvObject, REG_DRV_OBJECT);
	DRV_GetProcCtxtList(&pCtxt, (struct DRV_OBJECT *)hDrvObject);
	GT_0trace(curTrace, GT_ENTER, "*********************"
		 "DRV_ProcDisplayResourceInfo:*\n");
	while (pCtxt != NULL) {
		tempStrLen2 = sprintf((char *)pTempBuf,
				     "-------------------------------------"
				     "-----------------------------------\n");
		tempStrLen2 += 2;
		memmove(pBuf+tempStrLen, pTempBuf, tempStrLen2);
		tempStrLen += tempStrLen2;
		if (pCtxt->resState == PROC_RES_ALLOCATED) {
			tempStrLen2 = sprintf((char *)pTempBuf,
					"GPP Process Resource State: "
					"pCtxt->resState = PROC_RES_ALLOCATED, "
					" Process ID: %d\n", pCtxt->pid);
			tempStrLen2 += 2;
			memmove(pBuf+tempStrLen, pTempBuf, tempStrLen2);
			tempStrLen += tempStrLen2;
		} else {
			tempStrLen2 = sprintf((char *)pTempBuf,
				"GPP Resource State: pCtxt->resState"
				" = PROC_RES_DEALLOCATED, Process ID:%d\n",
				pCtxt->pid);
			tempStrLen2 += 2;
			memmove(pBuf+tempStrLen, pTempBuf, tempStrLen2);
			tempStrLen += tempStrLen2;
		}
		pNodeRes = pCtxt->pNodeList;
		tempCount = 1;
		while (pNodeRes != NULL) {
			GT_2trace(curTrace, GT_ENTER,
				 "DRV_ProcDisplayResourceInfo: #:%d "
				 "pCtxt->pNodeList->hNode:%x\n",
				 tempCount, pNodeRes->hNode);
			tempStrLen2 = sprintf((char *)pTempBuf,
					"Node Resource Information: Node #"
					" %d Node Handle hNode:0X%x\n",
					tempCount, (u32)pNodeRes->hNode);
			pNodeRes = pNodeRes->next;
			tempStrLen2 += 2;
			memmove(pBuf+tempStrLen, pTempBuf, tempStrLen2);
			tempStrLen += tempStrLen2;
			tempCount++;
		}
		tempCount = 1;
		pDSPHEAPRes = pCtxt->pDSPHEAPList;
		while (pDSPHEAPRes != NULL) {
			GT_2trace(curTrace, GT_ENTER,
				 "DRV_ProcDisplayResourceInfo: #:%d "
				 "pCtxt->pDSPHEAPRList->ulMpuAddr:%x\n",
				 tempCount, pDSPHEAPRes->ulMpuAddr);
			tempStrLen2 = sprintf((char *)pTempBuf,
				 "DSP Heap Resource Info: HEAP # %d"
				 " Mapped GPP Address: 0x%x, size: 0x%x\n",
				 tempCount, (u32)pDSPHEAPRes->ulMpuAddr,
				 (u32)pDSPHEAPRes->heapSize);
			pDSPHEAPRes = pDSPHEAPRes->next;
			tempStrLen2 += 2;
			memmove(pBuf+tempStrLen, pTempBuf, tempStrLen2);
			tempStrLen += tempStrLen2;
			tempCount++;
		}
		tempCount = 1;
		pDMMRes = pCtxt->pDMMList;
		while (pDMMRes != NULL) {
			GT_2trace(curTrace, GT_ENTER,
					"DRV_ProcDisplayResourceInfo: #:%d "
					" pCtxt->pDMMList->ulMpuAddr:%x\n",
					tempCount,
					pDMMRes->ulMpuAddr);
			tempStrLen2 = sprintf((char *)pTempBuf,
					 "DMM Resource Info: DMM # %d Mapped"
					 " GPP Address: 0x%x, size: 0x%x\n",
					 tempCount, (u32)pDMMRes->ulMpuAddr,
					 (u32)pDMMRes->dmmSize);
			pDMMRes = pDMMRes->next;
			tempStrLen2 += 2;
			memmove(pBuf+tempStrLen, pTempBuf, tempStrLen2);
			tempStrLen += tempStrLen2;
			tempCount++;
		}
		tempCount = 1;
		pSTRMRes = pCtxt->pSTRMList;
		while (pSTRMRes != NULL) {
			GT_2trace(curTrace, GT_ENTER,
				 "DRV_ProcDisplayResourceInfo: #:%d "
				 "pCtxt->pSTRMList->hStream:%x\n", tempCount,
				 pSTRMRes->hStream);
			tempStrLen2 = sprintf((char *)pTempBuf,
					     "Stream Resource info: STRM # %d "
					     "Stream Handle: 0x%x \n",
					     tempCount, (u32)pSTRMRes->hStream);
			pSTRMRes = pSTRMRes->next;
			tempStrLen2 += 2;
			memmove(pBuf+tempStrLen, pTempBuf, tempStrLen2);
			tempStrLen += tempStrLen2;
			tempCount++;
		}
		pCtxt = pCtxt->next;
	}
	*pSize = tempStrLen;
	status = PrintProcessInformation();
	GT_0trace(curTrace, GT_ENTER, "*********************"
		"DRV_ProcDisplayResourceInfo:**\n");
	return status;
}

/*
 *  ======== PrintProcessInformation ========
 *  Purpose:
 *      This function prints the Process's information stored in
 *      the process context list. Some of the information that
 *      it displays is Process's state, Node, Stream, DMM, and
 *      Heap information.
 */
static DSP_STATUS PrintProcessInformation(void)
{
	struct DRV_OBJECT *hDrvObject = NULL;
	struct PROCESS_CONTEXT *pCtxtList = NULL;
	struct NODE_RES_OBJECT *pNodeRes = NULL;
	struct DMM_RES_OBJECT *pDMMRes = NULL;
	struct STRM_RES_OBJECT *pSTRMRes = NULL;
	struct DSPHEAP_RES_OBJECT *pDSPHEAPRes = NULL;
	struct PROC_OBJECT *proc_obj_ptr;
	DSP_STATUS status = DSP_SOK;
	u32 tempCount;
	u32  procID;

	/* Get the Process context list */
	CFG_GetObject((u32 *)&hDrvObject, REG_DRV_OBJECT);
	DRV_GetProcCtxtList(&pCtxtList, hDrvObject);
	GT_0trace(curTrace, GT_4CLASS, "\n### Debug information"
			" for DSP bridge ##\n");
	GT_0trace(curTrace, GT_4CLASS, " \n ###The  processes"
			" information is as follows ### \n") ;
	GT_0trace(curTrace, GT_4CLASS, "  ====================="
			"============ \n");
	/* Go through the entries in the Process context list */
	while (pCtxtList  != NULL) {
		GT_1trace(curTrace, GT_4CLASS, "\nThe process"
				" id is %d\n", pCtxtList->pid);
		GT_0trace(curTrace, GT_4CLASS, " -------------------"
				"---------\n");
		if (pCtxtList->resState == PROC_RES_ALLOCATED) {
			GT_0trace(curTrace, GT_4CLASS, " \nThe Process"
					" is in Allocated state\n");
		} else {
			GT_0trace(curTrace, GT_4CLASS, "\nThe Process"
					" is in DeAllocated state\n");
		}

		spin_lock(&pCtxtList->proc_list_lock);
		list_for_each_entry(proc_obj_ptr, &pCtxtList->processor_list,
				proc_object) {
			PROC_GetProcessorId(proc_obj_ptr, &procID);
			if (procID == DSP_UNIT) {
				GT_0trace(curTrace, GT_4CLASS,
					"\nProcess connected to"
					" DSP Processor\n");
			} else if (procID == IVA_UNIT) {
				GT_0trace(curTrace, GT_4CLASS,
					"\nProcess connected to"
					" IVA Processor\n");
			} else {
				GT_0trace(curTrace, GT_7CLASS,
					"\n***ERROR:Invalid Processor Id***\n");
			}
		}
		spin_unlock(&pCtxtList->proc_list_lock);

		pNodeRes = pCtxtList->pNodeList;
		tempCount = 1;
		while (pNodeRes != NULL) {
			if (tempCount == 1)
				GT_0trace(curTrace, GT_4CLASS,
					"\n***The Nodes allocated by"
					" this Process are***\n");
			GT_2trace(curTrace, GT_4CLASS,
					"Node # %d Node Handle hNode:0x%x\n",
					tempCount, (u32)pNodeRes->hNode);
			pNodeRes = pNodeRes->next;
			tempCount++;
		}
		if (tempCount == 1)
			GT_0trace(curTrace, GT_4CLASS,
					"\n ***There are no Nodes"
					" allocated by this Process***\n");
		tempCount = 1;
		pDSPHEAPRes = pCtxtList->pDSPHEAPList;
		while (pDSPHEAPRes != NULL) {
			if (tempCount == 1)
				GT_0trace(curTrace, GT_4CLASS,
						"\n***The Heaps allocated by"
						" this Process are***\n");
			GT_3trace(curTrace, GT_4CLASS,
				"DSP Heap Resource Info: HEAP # %d "
				"Mapped GPP Address:0x%x, Size: 0x%lx\n",
				tempCount, (u32)pDSPHEAPRes->ulMpuAddr,
				pDSPHEAPRes->heapSize);
			pDSPHEAPRes = pDSPHEAPRes->next;
			tempCount++;
		}
		if (tempCount == 1)
			GT_0trace(curTrace, GT_4CLASS,
				"\n ***There are no Heaps allocated"
				" by this Process***\n");
		tempCount = 1;
		pDMMRes = pCtxtList->pDMMList;
		while (pDMMRes != NULL) {
			if (tempCount == 1)
				GT_0trace(curTrace, GT_4CLASS,
					"\n ***The DMM resources allocated by"
					" this Process are***\n");
			GT_3trace(curTrace, GT_4CLASS,
				"DMM Resource Info: DMM # %d "
				"Mapped GPP Address:0X%lx, Size: 0X%lx\n",
				tempCount, pDMMRes->ulMpuAddr,
				pDMMRes->dmmSize);
			pDMMRes = pDMMRes->next;
			tempCount++;
		}
		if (tempCount == 1)
			GT_0trace(curTrace, GT_4CLASS,
				"\n ***There are no DMM resources"
				" allocated by this Process***\n");
		tempCount = 1;
		pSTRMRes = pCtxtList->pSTRMList;
		while (pSTRMRes != NULL) {
			if (tempCount == 1)
				GT_0trace(curTrace, GT_4CLASS,
					"\n***The Stream resources allocated by"
					" this Process are***\n");
			GT_2trace(curTrace, GT_4CLASS,
				"Stream Resource info: STRM # %d"
				"Stream Handle:0X%x\n",	tempCount,
				(u32)pSTRMRes->hStream);
			pSTRMRes = pSTRMRes->next;
			tempCount++;
		}
		if (tempCount == 1)
			GT_0trace(curTrace, GT_4CLASS,
				"\n ***There are no Stream resources"
				"allocated by this Process***\n");
		pCtxtList = pCtxtList->next;
	}
	return status;
}

/* GPP PROCESS CLEANUP CODE END */
#endif

/*
 *  ======== = DRV_Create ======== =
 *  Purpose:
 *      DRV Object gets created only once during Driver Loading.
 */
DSP_STATUS DRV_Create(OUT struct DRV_OBJECT **phDRVObject)
{
	DSP_STATUS status = DSP_SOK;
	struct DRV_OBJECT *pDRVObject = NULL;

	DBC_Require(phDRVObject != NULL);
	DBC_Require(cRefs > 0);
	GT_1trace(curTrace, GT_ENTER, "Entering DRV_Create"
			" phDRVObject 0x%x\n", phDRVObject);
	MEM_AllocObject(pDRVObject, struct DRV_OBJECT, SIGNATURE);
	if (pDRVObject) {
		/* Create and Initialize List of device objects */
		pDRVObject->devList = LST_Create();
		if (pDRVObject->devList) {
			/* Create and Initialize List of device Extension */
			pDRVObject->devNodeString = LST_Create();
			if (!(pDRVObject->devNodeString)) {
				status = DSP_EFAIL;
				GT_0trace(curTrace, GT_7CLASS,
					 "Failed to Create DRV_EXT list ");
				MEM_FreeObject(pDRVObject);
			}
		} else {
			status = DSP_EFAIL;
			GT_0trace(curTrace, GT_7CLASS,
				 "Failed to Create Dev List ");
			MEM_FreeObject(pDRVObject);
		}
	} else {
		status = DSP_EFAIL;
		GT_0trace(curTrace, GT_7CLASS,
			 "Failed to Allocate Memory for DRV Obj");
	}
	if (DSP_SUCCEEDED(status)) {
		/* Store the DRV Object in the Registry */
		if (DSP_SUCCEEDED
		    (CFG_SetObject((u32) pDRVObject, REG_DRV_OBJECT))) {
			GT_1trace(curTrace, GT_1CLASS,
				 "DRV Obj Created pDrvObject 0x%x\n ",
				 pDRVObject);
			*phDRVObject = pDRVObject;
		} else {
			/* Free the DRV Object */
			status = DSP_EFAIL;
			MEM_Free(pDRVObject);
			GT_0trace(curTrace, GT_7CLASS,
				 "Failed to update the Registry with "
				 "DRV Object ");
		}
	}
	GT_2trace(curTrace, GT_ENTER,
		 "Exiting DRV_Create: phDRVObject: 0x%x\tstatus:"
		 "0x%x\n", phDRVObject, status);
	DBC_Ensure(DSP_FAILED(status) ||
		  MEM_IsValidHandle(pDRVObject, SIGNATURE));
	return status;
}

/*
 *  ======== DRV_Exit ========
 *  Purpose:
 *      Discontinue usage of the DRV module.
 */
void DRV_Exit(void)
{
	DBC_Require(cRefs > 0);

	GT_0trace(curTrace, GT_5CLASS, "Entering DRV_Exit \n");

	cRefs--;

	DBC_Ensure(cRefs >= 0);
}

/*
 *  ======== = DRV_Destroy ======== =
 *  purpose:
 *      Invoked during bridge de-initialization
 */
DSP_STATUS DRV_Destroy(struct DRV_OBJECT *hDRVObject)
{
	DSP_STATUS status = DSP_SOK;
	struct DRV_OBJECT *pDRVObject = (struct DRV_OBJECT *)hDRVObject;

	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(pDRVObject, SIGNATURE));

	GT_1trace(curTrace, GT_ENTER, "Entering DRV_Destroy"
			" hDRVObject 0x%x\n", hDRVObject);
	/*
	 *  Delete the List if it exists.Should not come here
	 *  as the DRV_RemoveDevObject and the Last DRV_RequestResources
	 *  removes the list if the lists are empty.
	 */
	if (pDRVObject->devList) {
		/* Could assert if the list is not empty  */
		LST_Delete(pDRVObject->devList);
	}
	if (pDRVObject->devNodeString) {
		/* Could assert if the list is not empty */
		LST_Delete(pDRVObject->devNodeString);
	}
	MEM_FreeObject(pDRVObject);
	/* Update the DRV Object in Registry to be 0 */
	(void)CFG_SetObject(0, REG_DRV_OBJECT);
	GT_2trace(curTrace, GT_ENTER,
		 "Exiting DRV_Destroy: hDRVObject: 0x%x\tstatus:"
		 "0x%x\n", hDRVObject, status);
	DBC_Ensure(!MEM_IsValidHandle(pDRVObject, SIGNATURE));
	return status;
}

/*
 *  ======== DRV_GetDevObject ========
 *  Purpose:
 *      Given a index, returns a handle to DevObject from the list.
 */
DSP_STATUS DRV_GetDevObject(u32 uIndex, struct DRV_OBJECT *hDrvObject,
			   struct DEV_OBJECT **phDevObject)
{
	DSP_STATUS status = DSP_SOK;
#if GT_TRACE	/* pDrvObject is used only for Assertions and debug messages.*/
	struct DRV_OBJECT *pDrvObject = (struct DRV_OBJECT *)hDrvObject;
#endif
	struct DEV_OBJECT *pDevObject;
	u32 i;
	DBC_Require(MEM_IsValidHandle(pDrvObject, SIGNATURE));
	DBC_Require(phDevObject != NULL);
	DBC_Require(uIndex >= 0);
	DBC_Require(cRefs > 0);
	DBC_Assert(!(LST_IsEmpty(pDrvObject->devList)));
	GT_3trace(curTrace, GT_ENTER,
		 "Entered DRV_GetDevObject, args:\n\tuIndex: "
		 "0x%x\n\thDrvObject:  0x%x\n\tphDevObject:  0x%x\n",
		 uIndex, hDrvObject, phDevObject);
	pDevObject = (struct DEV_OBJECT *)DRV_GetFirstDevObject();
	for (i = 0; i < uIndex; i++) {
		pDevObject =
		   (struct DEV_OBJECT *)DRV_GetNextDevObject((u32)pDevObject);
	}
	if (pDevObject) {
		*phDevObject = (struct DEV_OBJECT *) pDevObject;
		status = DSP_SOK;
	} else {
		*phDevObject = NULL;
		status = DSP_EFAIL;
		GT_0trace(curTrace, GT_7CLASS,
			 "DRV: Could not get the DevObject\n");
	}
	GT_2trace(curTrace, GT_ENTER,
		 "Exiting Drv_GetDevObject\n\tstatus: 0x%x\n\t"
		 "hDevObject: 0x%x\n", status, *phDevObject);
	return status;
}

/*
 *  ======== DRV_GetFirstDevObject ========
 *  Purpose:
 *      Retrieve the first Device Object handle from an internal linked list of
 *      of DEV_OBJECTs maintained by DRV.
 */
u32 DRV_GetFirstDevObject(void)
{
	u32 dwDevObject = 0;
	struct DRV_OBJECT *pDrvObject;

	if (DSP_SUCCEEDED
	    (CFG_GetObject((u32 *)&pDrvObject, REG_DRV_OBJECT))) {
		if ((pDrvObject->devList != NULL) &&
		   !LST_IsEmpty(pDrvObject->devList))
			dwDevObject = (u32) LST_First(pDrvObject->devList);
	}

	return dwDevObject;
}

/*
 *  ======== DRV_GetFirstDevNodeString ========
 *  Purpose:
 *      Retrieve the first Device Extension from an internal linked list of
 *      of Pointer to DevNode Strings maintained by DRV.
 */
u32 DRV_GetFirstDevExtension(void)
{
	u32 dwDevExtension = 0;
	struct DRV_OBJECT *pDrvObject;

	if (DSP_SUCCEEDED
	    (CFG_GetObject((u32 *)&pDrvObject, REG_DRV_OBJECT))) {

		if ((pDrvObject->devNodeString != NULL) &&
		   !LST_IsEmpty(pDrvObject->devNodeString)) {
			dwDevExtension = (u32)LST_First(pDrvObject->
							devNodeString);
		}
	}

	return dwDevExtension;
}

/*
 *  ======== DRV_GetNextDevObject ========
 *  Purpose:
 *      Retrieve the next Device Object handle from an internal linked list of
 *      of DEV_OBJECTs maintained by DRV, after having previously called
 *      DRV_GetFirstDevObject() and zero or more DRV_GetNext.
 */
u32 DRV_GetNextDevObject(u32 hDevObject)
{
	u32 dwNextDevObject = 0;
	struct DRV_OBJECT *pDrvObject;

	DBC_Require(hDevObject != 0);

	if (DSP_SUCCEEDED
	    (CFG_GetObject((u32 *)&pDrvObject, REG_DRV_OBJECT))) {

		if ((pDrvObject->devList != NULL) &&
		   !LST_IsEmpty(pDrvObject->devList)) {
			dwNextDevObject = (u32)LST_Next(pDrvObject->devList,
					  (struct LST_ELEM *)hDevObject);
		}
	}
	return dwNextDevObject;
}

/*
 *  ======== DRV_GetNextDevExtension ========
 *  Purpose:
 *      Retrieve the next Device Extension from an internal linked list of
 *      of pointer to DevNodeString maintained by DRV, after having previously
 *      called DRV_GetFirstDevExtension() and zero or more
 *      DRV_GetNextDevExtension().
 */
u32 DRV_GetNextDevExtension(u32 hDevExtension)
{
	u32 dwDevExtension = 0;
	struct DRV_OBJECT *pDrvObject;

	DBC_Require(hDevExtension != 0);

	if (DSP_SUCCEEDED(CFG_GetObject((u32 *)&pDrvObject,
	   REG_DRV_OBJECT))) {
		if ((pDrvObject->devNodeString != NULL) &&
		   !LST_IsEmpty(pDrvObject->devNodeString)) {
			dwDevExtension = (u32)LST_Next(pDrvObject->
				devNodeString,
				(struct LST_ELEM *)hDevExtension);
		}
	}

	return dwDevExtension;
}

/*
 *  ======== DRV_Init ========
 *  Purpose:
 *      Initialize DRV module private state.
 */
DSP_STATUS DRV_Init(void)
{
	s32 fRetval = 1;	/* function return value */

	DBC_Require(cRefs >= 0);

	if (fRetval)
		cRefs++;

	GT_1trace(curTrace, GT_5CLASS, "Entering DRV_Entry  crefs 0x%x \n",
		 cRefs);

	DBC_Ensure((fRetval && (cRefs > 0)) || (!fRetval && (cRefs >= 0)));

	return fRetval;
}

/*
 *  ======== DRV_InsertDevObject ========
 *  Purpose:
 *      Insert a DevObject into the list of Manager object.
 */
DSP_STATUS DRV_InsertDevObject(struct DRV_OBJECT *hDRVObject,
			       struct DEV_OBJECT *hDevObject)
{
	DSP_STATUS status = DSP_SOK;
	struct DRV_OBJECT *pDRVObject = (struct DRV_OBJECT *)hDRVObject;

	DBC_Require(cRefs > 0);
	DBC_Require(hDevObject != NULL);
	DBC_Require(MEM_IsValidHandle(pDRVObject, SIGNATURE));
	DBC_Assert(pDRVObject->devList);

	GT_2trace(curTrace, GT_ENTER,
		 "Entering DRV_InsertProcObject hDRVObject "
		 "0x%x\n, hDevObject 0x%x\n", hDRVObject, hDevObject);

	LST_PutTail(pDRVObject->devList, (struct LST_ELEM *)hDevObject);

	GT_1trace(curTrace, GT_ENTER,
		 "Exiting InsertDevObject status 0x%x\n", status);

	DBC_Ensure(DSP_SUCCEEDED(status) && !LST_IsEmpty(pDRVObject->devList));

	return status;
}

/*
 *  ======== DRV_RemoveDevObject ========
 *  Purpose:
 *      Search for and remove a DeviceObject from the given list of DRV
 *      objects.
 */
DSP_STATUS DRV_RemoveDevObject(struct DRV_OBJECT *hDRVObject,
			       struct DEV_OBJECT *hDevObject)
{
	DSP_STATUS status = DSP_EFAIL;
	struct DRV_OBJECT *pDRVObject = (struct DRV_OBJECT *)hDRVObject;
	struct LST_ELEM *pCurElem;

	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(pDRVObject, SIGNATURE));
	DBC_Require(hDevObject != NULL);

	DBC_Require(pDRVObject->devList != NULL);
	DBC_Require(!LST_IsEmpty(pDRVObject->devList));

	GT_2trace(curTrace, GT_ENTER,
		 "Entering DRV_RemoveDevObject hDevObject "
		 "0x%x\n, hDRVObject 0x%x\n", hDevObject, hDRVObject);
	/* Search list for pProcObject: */
	for (pCurElem = LST_First(pDRVObject->devList); pCurElem != NULL;
	    pCurElem = LST_Next(pDRVObject->devList, pCurElem)) {
		/* If found, remove it. */
		if ((struct DEV_OBJECT *) pCurElem == hDevObject) {
			LST_RemoveElem(pDRVObject->devList, pCurElem);
			status = DSP_SOK;
			break;
		}
	}
	/* Remove list if empty. */
	if (LST_IsEmpty(pDRVObject->devList)) {
		LST_Delete(pDRVObject->devList);
		pDRVObject->devList = NULL;
	}
	DBC_Ensure((pDRVObject->devList == NULL) ||
		  !LST_IsEmpty(pDRVObject->devList));
	GT_1trace(curTrace, GT_ENTER,
		 "DRV_RemoveDevObject returning 0x%x\n", status);
	return status;
}

/*
 *  ======== DRV_RequestResources ========
 *  Purpose:
 *      Requests  resources from the OS.
 */
DSP_STATUS DRV_RequestResources(u32 dwContext, u32 *pDevNodeString)
{
	DSP_STATUS status = DSP_SOK;
	struct DRV_OBJECT *pDRVObject;
	struct DRV_EXT *pszdevNode;

	DBC_Require(dwContext != 0);
	DBC_Require(pDevNodeString != NULL);
	GT_0trace(curTrace, GT_ENTER, "Entering DRV_RequestResources\n");
	/*
	 *  Allocate memory to hold the string. This will live untill
	 *  it is freed in the Release resources. Update the driver object
	 *  list.
	 */
	if (DSP_SUCCEEDED(CFG_GetObject((u32 *)&pDRVObject,
	   REG_DRV_OBJECT))) {
		pszdevNode = MEM_Calloc(sizeof(struct DRV_EXT), MEM_NONPAGED);
		if (pszdevNode) {
			LST_InitElem(&pszdevNode->link);
			strncpy(pszdevNode->szString,
				 (char *)dwContext, MAXREGPATHLENGTH - 1);
			pszdevNode->szString[MAXREGPATHLENGTH - 1] = '\0';
			/* Update the Driver Object List */
			*pDevNodeString = (u32)pszdevNode->szString;
			LST_PutTail(pDRVObject->devNodeString,
				(struct LST_ELEM *)pszdevNode);
		} else {
			GT_0trace(curTrace, GT_7CLASS,
				"Failed to Allocate Memory devNodeString ");
			status = DSP_EFAIL;
			*pDevNodeString = 0;
		}
	} else {
		status = DSP_EFAIL;
		GT_0trace(curTrace, GT_7CLASS,
			 "Failed to get Driver Object from Registry");
		*pDevNodeString = 0;
	}

       if (!(strcmp((char *) dwContext, "TIOMAP1510"))) {
		GT_0trace(curTrace, GT_1CLASS,
			  " Allocating resources for UMA \n");
		status = RequestBridgeResourcesDSP(dwContext, DRV_ASSIGN);
	} else {
		status = DSP_EFAIL;
		GT_0trace(curTrace, GT_7CLASS, "Unknown Device ");
	}

	if (DSP_FAILED(status)) {
		GT_0trace(curTrace, GT_7CLASS,
			 "Failed to reserve bridge resources ");
	}
	DBC_Ensure((DSP_SUCCEEDED(status) && pDevNodeString != NULL &&
		  !LST_IsEmpty(pDRVObject->devNodeString)) ||
		  (DSP_FAILED(status) && *pDevNodeString == 0));

	return status;
}

/*
 *  ======== DRV_ReleaseResources ========
 *  Purpose:
 *      Releases  resources from the OS.
 */
DSP_STATUS DRV_ReleaseResources(u32 dwContext, struct DRV_OBJECT *hDrvObject)
{
	DSP_STATUS status = DSP_SOK;
	struct DRV_OBJECT *pDRVObject = (struct DRV_OBJECT *)hDrvObject;
	struct DRV_EXT *pszdevNode;

	GT_0trace(curTrace, GT_ENTER, "Entering DRV_Release Resources\n");

       if (!(strcmp((char *)((struct DRV_EXT *)dwContext)->szString,
	   "TIOMAP1510"))) {
		GT_0trace(curTrace, GT_1CLASS,
			 " Releasing DSP-Bridge resources \n");
		status = RequestBridgeResources(dwContext, DRV_RELEASE);
	} else {
		GT_0trace(curTrace, GT_1CLASS, " Unknown device\n");
	}

	if (DSP_SUCCEEDED(status)) {
		GT_0trace(curTrace, GT_1CLASS,
			 "Failed to relese bridge resources\n");
	}

	/*
	 *  Irrespective of the status go ahead and clean it
	 *  The following will over write the status.
	 */
	for (pszdevNode = (struct DRV_EXT *)DRV_GetFirstDevExtension();
	    pszdevNode != NULL; pszdevNode = (struct DRV_EXT *)
	    DRV_GetNextDevExtension((u32)pszdevNode)) {
		if ((u32)pszdevNode == dwContext) {
			/* Found it */
			/* Delete from the Driver object list */
			LST_RemoveElem(pDRVObject->devNodeString,
				      (struct LST_ELEM *)pszdevNode);
			MEM_Free((void *) pszdevNode);
			break;
		}
		/* Delete the List if it is empty */
		if (LST_IsEmpty(pDRVObject->devNodeString)) {
			LST_Delete(pDRVObject->devNodeString);
			pDRVObject->devNodeString = NULL;
		}
	}
	return status;
}

/*
 *  ======== RequestBridgeResources ========
 *  Purpose:
 *      Reserves shared memory for bridge.
 */
static DSP_STATUS RequestBridgeResources(u32 dwContext, s32 bRequest)
{
	DSP_STATUS status = DSP_SOK;
	struct CFG_HOSTRES *pResources;
	u32 dwBuffSize;

	struct DRV_EXT *driverExt;
	u32 shm_size;

	DBC_Require(dwContext != 0);

	GT_0trace(curTrace, GT_ENTER, "->RequestBridgeResources \n");

	if (!bRequest) {
		driverExt = (struct DRV_EXT *)dwContext;
		/* Releasing resources by deleting the registry key  */
		dwBuffSize = sizeof(struct CFG_HOSTRES);
		pResources = MEM_Calloc(dwBuffSize, MEM_NONPAGED);
		if (DSP_FAILED(REG_GetValue(NULL, (char *)driverExt->szString,
		   CURRENTCONFIG, (u8 *)pResources, &dwBuffSize))) {
			status = CFG_E_RESOURCENOTAVAIL;
			GT_0trace(curTrace, GT_1CLASS,
				 "REG_GetValue Failed \n");
		} else {
			GT_0trace(curTrace, GT_1CLASS,
				 "REG_GetValue Succeeded \n");
		}

		if (pResources != NULL) {
			dwBuffSize = sizeof(shm_size);
			status = REG_GetValue(NULL, CURRENTCONFIG, SHMSIZE,
				(u8 *)&shm_size, &dwBuffSize);
			if (DSP_SUCCEEDED(status)) {
				if ((pResources->dwMemBase[1]) &&
				   (pResources->dwMemPhys[1])) {
					MEM_FreePhysMem((void *)pResources->
					dwMemBase[1], pResources->dwMemPhys[1],
					shm_size);
				}
			} else {
				GT_1trace(curTrace, GT_7CLASS,
					"Error getting SHM size from registry: "
					"%x. Not calling MEM_FreePhysMem\n",
					status);
			}
			pResources->dwMemBase[1] = 0;
			pResources->dwMemPhys[1] = 0;

			if (pResources->dwPrmBase)
				iounmap(pResources->dwPrmBase);
			if (pResources->dwCmBase)
				iounmap(pResources->dwCmBase);
			if (pResources->dwMboxBase)
				iounmap(pResources->dwMboxBase);
			if (pResources->dwMemBase[0])
				iounmap((void *)pResources->dwMemBase[0]);
			if (pResources->dwMemBase[2])
				iounmap((void *)pResources->dwMemBase[2]);
			if (pResources->dwMemBase[3])
				iounmap((void *)pResources->dwMemBase[3]);
			if (pResources->dwMemBase[4])
				iounmap((void *)pResources->dwMemBase[4]);
			if (pResources->dwWdTimerDspBase)
				iounmap(pResources->dwWdTimerDspBase);
			if (pResources->dwDmmuBase)
				iounmap(pResources->dwDmmuBase);
			if (pResources->dwPerBase)
				iounmap(pResources->dwPerBase);
                       if (pResources->dwPerPmBase)
                               iounmap((void *)pResources->dwPerPmBase);
                       if (pResources->dwCorePmBase)
                               iounmap((void *)pResources->dwCorePmBase);
			if (pResources->dwSysCtrlBase) {
				iounmap(pResources->dwSysCtrlBase);
				/* don't set pResources->dwSysCtrlBase to null
				 * as it is used in BOARD_Stop */
			}
			pResources->dwPrmBase = NULL;
			pResources->dwCmBase = NULL;
			pResources->dwMboxBase = NULL;
			pResources->dwMemBase[0] = (u32) NULL;
			pResources->dwMemBase[2] = (u32) NULL;
			pResources->dwMemBase[3] = (u32) NULL;
			pResources->dwMemBase[4] = (u32) NULL;
			pResources->dwWdTimerDspBase = NULL;
			pResources->dwDmmuBase = NULL;

			dwBuffSize = sizeof(struct CFG_HOSTRES);
			status = REG_SetValue(NULL, (char *)driverExt->szString,
				 CURRENTCONFIG, REG_BINARY, (u8 *)pResources,
				 (u32)dwBuffSize);
			/*  Set all the other entries to NULL */
			MEM_Free(pResources);
		}
		GT_0trace(curTrace, GT_ENTER, " <- RequestBridgeResources \n");
		return status;
	}
	dwBuffSize = sizeof(struct CFG_HOSTRES);
	pResources = MEM_Calloc(dwBuffSize, MEM_NONPAGED);
	if (pResources != NULL) {
		/* wNumMemWindows must not be more than CFG_MAXMEMREGISTERS */
		pResources->wNumMemWindows = 2;
		/* First window is for DSP internal memory */

		pResources->dwPrmBase = ioremap(OMAP_IVA2_PRM_BASE,
							OMAP_IVA2_PRM_SIZE);
		pResources->dwCmBase = ioremap(OMAP_IVA2_CM_BASE,
							OMAP_IVA2_CM_SIZE);
		pResources->dwMboxBase = ioremap(OMAP_MBOX_BASE,
							OMAP_MBOX_SIZE);
		pResources->dwSysCtrlBase = ioremap(OMAP_SYSC_BASE,
							OMAP_SYSC_SIZE);
		GT_1trace(curTrace, GT_2CLASS, "dwMemBase[0] 0x%x\n",
			 pResources->dwMemBase[0]);
		GT_1trace(curTrace, GT_2CLASS, "dwMemBase[3] 0x%x\n",
			 pResources->dwMemBase[3]);
		GT_1trace(curTrace, GT_2CLASS, "dwPrmBase 0x%x\n",
							pResources->dwPrmBase);
		GT_1trace(curTrace, GT_2CLASS, "dwCmBase 0x%x\n",
							pResources->dwCmBase);
		GT_1trace(curTrace, GT_2CLASS, "dwWdTimerDspBase 0x%x\n",
						pResources->dwWdTimerDspBase);
		GT_1trace(curTrace, GT_2CLASS, "dwMboxBase 0x%x\n",
						pResources->dwMboxBase);
		GT_1trace(curTrace, GT_2CLASS, "dwDmmuBase 0x%x\n",
						pResources->dwDmmuBase);

		/* for 24xx base port is not mapping the mamory for DSP
		 * internal memory TODO Do a ioremap here */
		/* Second window is for DSP external memory shared with MPU */
		if (DSP_SUCCEEDED(status)) {
			/* for Linux, these are hard-coded values */
			pResources->bIRQRegisters = 0;
			pResources->bIRQAttrib = 0;
			pResources->dwOffsetForMonitor = 0;
			pResources->dwChnlOffset = 0;
			/* CHNL_MAXCHANNELS */
			pResources->dwNumChnls = CHNL_MAXCHANNELS;
			pResources->dwChnlBufSize = 0x400;
			dwBuffSize = sizeof(struct CFG_HOSTRES);
			status = REG_SetValue(NULL, (char *) dwContext,
					     CURRENTCONFIG, REG_BINARY,
					     (u8 *)pResources,
					     sizeof(struct CFG_HOSTRES));
			if (DSP_SUCCEEDED(status)) {
				GT_0trace(curTrace, GT_1CLASS,
					 " Successfully set the registry "
					 "value for CURRENTCONFIG\n");
			} else {
				GT_0trace(curTrace, GT_7CLASS,
					 " Failed to set the registry "
					 "value for CURRENTCONFIG\n");
			}
		}
		MEM_Free(pResources);
	}
	/* End Mem alloc */
	return status;
}

/*
 *  ======== RequestBridgeResourcesDSP ========
 *  Purpose:
 *      Reserves shared memory for bridge.
 */
static DSP_STATUS RequestBridgeResourcesDSP(u32 dwContext, s32 bRequest)
{
	DSP_STATUS status = DSP_SOK;
	struct CFG_HOSTRES *pResources;
	u32 dwBuffSize;
	u32 dmaAddr;
	u32 shm_size;

	DBC_Require(dwContext != 0);

	GT_0trace(curTrace, GT_ENTER, "->RequestBridgeResourcesDSP \n");

	dwBuffSize = sizeof(struct CFG_HOSTRES);

	pResources = MEM_Calloc(dwBuffSize, MEM_NONPAGED);

	if (pResources != NULL) {
		if (DSP_FAILED(CFG_GetHostResources((struct CFG_DEVNODE *)
		   dwContext, pResources))) {
			/* Call CFG_GetHostResources to get reserve resouces */
			status = RequestBridgeResources(dwContext, bRequest);
			if (DSP_SUCCEEDED(status)) {
				status = CFG_GetHostResources
					((struct CFG_DEVNODE *) dwContext,
					pResources);
			}
		}
		/* wNumMemWindows must not be more than CFG_MAXMEMREGISTERS */
		pResources->wNumMemWindows = 4;

		pResources->dwMemBase[0] = 0;
		pResources->dwMemBase[2] = (u32)ioremap(OMAP_DSP_MEM1_BASE,
							OMAP_DSP_MEM1_SIZE);
		pResources->dwMemBase[3] = (u32)ioremap(OMAP_DSP_MEM2_BASE,
							OMAP_DSP_MEM2_SIZE);
		pResources->dwMemBase[4] = (u32)ioremap(OMAP_DSP_MEM3_BASE,
							OMAP_DSP_MEM3_SIZE);
		pResources->dwPerBase = ioremap(OMAP_PER_CM_BASE,
							OMAP_PER_CM_SIZE);
               pResources->dwPerPmBase = (u32)ioremap(OMAP_PER_PRM_BASE,
                                                       OMAP_PER_PRM_SIZE);
               pResources->dwCorePmBase = (u32)ioremap(OMAP_CORE_PRM_BASE,
                                                       OMAP_CORE_PRM_SIZE);
		pResources->dwDmmuBase = ioremap(OMAP_DMMU_BASE,
							OMAP_DMMU_SIZE);
		pResources->dwWdTimerDspBase = NULL;

		GT_1trace(curTrace, GT_2CLASS, "dwMemBase[0] 0x%x\n",
						pResources->dwMemBase[0]);
		GT_1trace(curTrace, GT_2CLASS, "dwMemBase[1] 0x%x\n",
						pResources->dwMemBase[1]);
		GT_1trace(curTrace, GT_2CLASS, "dwMemBase[2] 0x%x\n",
						pResources->dwMemBase[2]);
		GT_1trace(curTrace, GT_2CLASS, "dwMemBase[3] 0x%x\n",
						pResources->dwMemBase[3]);
		GT_1trace(curTrace, GT_2CLASS, "dwMemBase[4] 0x%x\n",
						pResources->dwMemBase[4]);
		GT_1trace(curTrace, GT_2CLASS, "dwPrmBase 0x%x\n",
						pResources->dwPrmBase);
		GT_1trace(curTrace, GT_2CLASS, "dwCmBase 0x%x\n",
						pResources->dwCmBase);
		GT_1trace(curTrace, GT_2CLASS, "dwWdTimerDspBase 0x%x\n",
						pResources->dwWdTimerDspBase);
		GT_1trace(curTrace, GT_2CLASS, "dwMboxBase 0x%x\n",
						pResources->dwMboxBase);
		GT_1trace(curTrace, GT_2CLASS, "dwDmmuBase 0x%x\n",
						pResources->dwDmmuBase);
		dwBuffSize = sizeof(shm_size);
		status = REG_GetValue(NULL, CURRENTCONFIG, SHMSIZE,
				     (u8 *)&shm_size, &dwBuffSize);
		if (DSP_SUCCEEDED(status)) {
			/* Allocate Physically contiguous,
			 * non-cacheable  memory */
			pResources->dwMemBase[1] =
				(u32)MEM_AllocPhysMem(shm_size, 0x100000,
							&dmaAddr);
			if (pResources->dwMemBase[1] == 0) {
				status = DSP_EMEMORY;
				GT_0trace(curTrace, GT_7CLASS,
					 "SHM reservation Failed\n");
			} else {
				pResources->dwMemLength[1] = shm_size;
				pResources->dwMemPhys[1] = dmaAddr;

				GT_3trace(curTrace, GT_1CLASS,
					 "Bridge SHM address 0x%x dmaAddr"
					 " %x size %x\n",
					 pResources->dwMemBase[1],
					 dmaAddr, shm_size);
			}
		}
		if (DSP_SUCCEEDED(status)) {
			/* for Linux, these are hard-coded values */
			pResources->bIRQRegisters = 0;
			pResources->bIRQAttrib = 0;
			pResources->dwOffsetForMonitor = 0;
			pResources->dwChnlOffset = 0;
			/* CHNL_MAXCHANNELS */
			pResources->dwNumChnls = CHNL_MAXCHANNELS;
			pResources->dwChnlBufSize = 0x400;
			dwBuffSize = sizeof(struct CFG_HOSTRES);
			status = REG_SetValue(NULL, (char *)dwContext,
					     CURRENTCONFIG, REG_BINARY,
					     (u8 *)pResources,
					     sizeof(struct CFG_HOSTRES));
			if (DSP_SUCCEEDED(status)) {
				GT_0trace(curTrace, GT_1CLASS,
					 " Successfully set the registry"
					 " value for CURRENTCONFIG\n");
			} else {
				GT_0trace(curTrace, GT_7CLASS,
					 " Failed to set the registry value"
					 " for CURRENTCONFIG\n");
			}
		}
		MEM_Free(pResources);
	}
	/* End Mem alloc */
	return status;
}
