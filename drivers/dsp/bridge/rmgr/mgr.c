/*
 * mgr.c
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
 *  ======== mgr.c ========
 *  Description:
 *      Implementation of Manager interface to the device object at the
 *      driver level. This queries the NDB data base and retrieves the
 *      data about Node and Processor.
 *
 *
 *! Revision History:
 *! ================
 *! 12-Feb-2003 vp: Code review updates.
 *! 18-Oct-2002 vp: Ported to Linux platform
 *! 01-Aug-2001 ag: Added extended info for DSP-MMU setup support.
 *! 13-Feb-2001 kc: DSP/BIOS Bridge name updates.
 *! 22-Nov-2000 kc: Added MGR_GetPerfData.
 *! 03-Nov-2000 rr: Updated after code review.
 *! 25-Sep-2000 rr: Updated to Version 0.9
 *! 10-Aug-2000 rr: dwSignature is not specifically inserted in MGR Obj
 *!                 as it is taken care by MEM_AllocObject. stdwin.h added
 *!                 for retail build to succeed.
 *! 07-Aug-2000 rr: MGR_Create does the job of Loading DCD Dll.
 *! 26-Jul-2000 rr: MGR_Destroy releases the hNDBDll.
 *! 20-Jun-2000 rr: Created.
 */

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
#include <dspbridge/sync.h>

/*  ----------------------------------- Others */
#include <dspbridge/dbdcd.h>
#include <dspbridge/dbreg.h>
#include <dspbridge/drv.h>
#include <dspbridge/dev.h>

/*  ----------------------------------- This */
#include <dspbridge/mgr.h>

/*  ----------------------------------- Defines, Data Structures, Typedefs */
#define ZLDLLNAME               ""
#define SIGNATURE               0x5f52474d	/* "MGR_" (in reverse) */

struct MGR_OBJECT {
	u32 dwSignature;
	struct DCD_MANAGER *hDcdMgr;	/* Proc/Node data manager */
};

/*  ----------------------------------- Globals */
#if GT_TRACE
static struct GT_Mask MGR_DebugMask = { NULL, NULL };
#endif

static u32 cRefs;

/*
 *  ========= MGR_Create =========
 *  Purpose:
 *      MGR Object gets created only once during driver Loading.
 */
DSP_STATUS MGR_Create(OUT struct MGR_OBJECT **phMgrObject,
		     struct CFG_DEVNODE *hDevNode)
{
	DSP_STATUS status = DSP_SOK;
	struct MGR_OBJECT *pMgrObject = NULL;

	DBC_Require(phMgrObject != NULL);
	DBC_Require(cRefs > 0);
	GT_1trace(MGR_DebugMask, GT_ENTER,
		 "Entering MGR_Create phMgrObject 0x%x\n ",
		 phMgrObject);
	MEM_AllocObject(pMgrObject, struct MGR_OBJECT, SIGNATURE);
	if (pMgrObject) {
		if (DSP_SUCCEEDED(DCD_CreateManager(ZLDLLNAME,
		   &pMgrObject->hDcdMgr))) {
			/* If succeeded store the handle in the MGR Object */
			if (DSP_SUCCEEDED(CFG_SetObject((u32)pMgrObject,
			   REG_MGR_OBJECT))) {
				*phMgrObject = pMgrObject;
				GT_0trace(MGR_DebugMask, GT_1CLASS,
					 "MGR_Create:MGR Created\r\n");
			} else {
				status = DSP_EFAIL;
				GT_0trace(MGR_DebugMask, GT_7CLASS,
					 "MGR_Create:CFG_SetObject "
					 "Failed\r\n");
				DCD_DestroyManager(pMgrObject->hDcdMgr);
				MEM_FreeObject(pMgrObject);
			}
		} else {
			/* failed to Create DCD Manager */
			status = DSP_EFAIL;
			GT_0trace(MGR_DebugMask, GT_7CLASS,
				 "MGR_Create:DCD_ManagerCreate Failed\r\n");
			MEM_FreeObject(pMgrObject);
		}
	} else {
		status = DSP_EMEMORY;
		GT_0trace(MGR_DebugMask, GT_7CLASS,
			 "MGR_Create DSP_FAILED to allocate memory \n");
	}
	GT_2trace(MGR_DebugMask, GT_ENTER,
		 "Exiting MGR_Create: phMgrObject: 0x%x\t"
		 "status: 0x%x\n", phMgrObject, status);
	DBC_Ensure(DSP_FAILED(status) ||
		  MEM_IsValidHandle(pMgrObject, SIGNATURE));
	return status;
}

/*
 *  ========= MGR_Destroy =========
 *     This function is invoked during bridge driver unloading.Frees MGR object.
 */
DSP_STATUS MGR_Destroy(struct MGR_OBJECT *hMgrObject)
{
	DSP_STATUS status = DSP_SOK;
	struct MGR_OBJECT *pMgrObject = (struct MGR_OBJECT *)hMgrObject;

	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(hMgrObject, SIGNATURE));

	GT_1trace(MGR_DebugMask, GT_ENTER,
		 "Entering MGR_Destroy hMgrObject 0x%x\n", hMgrObject);
	/* Free resources */
	if (hMgrObject->hDcdMgr)
		DCD_DestroyManager(hMgrObject->hDcdMgr);

	MEM_FreeObject(pMgrObject);
	/* Update the Registry with NULL for MGR Object */
	(void)CFG_SetObject(0, REG_MGR_OBJECT);

	GT_2trace(MGR_DebugMask, GT_ENTER,
		 "Exiting MGR_Destroy: hMgrObject: 0x%x\t"
		 "status: 0x%x\n", hMgrObject, status);

	DBC_Ensure(DSP_FAILED(status) ||
		 !MEM_IsValidHandle(hMgrObject, SIGNATURE));

	return status;
}

/*
 *  ======== MGR_EnumNodeInfo ========
 *      Enumerate and get configuration information about nodes configured
 *      in the node database.
 */
DSP_STATUS MGR_EnumNodeInfo(u32 uNode, OUT struct DSP_NDBPROPS *pNDBProps,
			   u32 uNDBPropsSize, OUT u32 *puNumNodes)
{
	DSP_STATUS status = DSP_SOK;
	DSP_STATUS status1 = DSP_SOK;
	struct DSP_UUID Uuid, uTempUuid;
	u32 uTempIndex = 0;
	u32 uNodeIndex = 0;
	struct DCD_GENERICOBJ GenObj;
	struct MGR_OBJECT *pMgrObject = NULL;

	DBC_Require(pNDBProps != NULL);
	DBC_Require(puNumNodes != NULL);
	DBC_Require(uNDBPropsSize >= sizeof(struct DSP_NDBPROPS));
	DBC_Require(cRefs > 0);

	GT_4trace(MGR_DebugMask, GT_ENTER, "Entered Manager_EnumNodeInfo, "
		 "args:\n\t uNode: 0x%x\n\tpNDBProps:  0x%x\n\tuNDBPropsSize:"
		 "0x%x\tpuNumNodes: 0x%x\n", uNode, pNDBProps,
		 uNDBPropsSize, puNumNodes);
	*puNumNodes = 0;
	/* Get The Manager Object from the Registry */
	if (DSP_FAILED(CFG_GetObject((u32 *)&pMgrObject,
	   REG_MGR_OBJECT))) {
		GT_0trace(MGR_DebugMask, GT_7CLASS,
			 "Manager_EnumNodeInfo:Failed To Get"
			 " MGR Object from Registry\r\n");
		goto func_cont;
	}
	DBC_Assert(MEM_IsValidHandle(pMgrObject, SIGNATURE));
	/* Forever loop till we hit failed or no more items in the
	 * Enumeration. We will exit the loop other than DSP_SOK; */
	while (status == DSP_SOK) {
		status = DCD_EnumerateObject(uTempIndex++, DSP_DCDNODETYPE,
					    &uTempUuid);
		if (status == DSP_SOK) {
			uNodeIndex++;
			if (uNode == (uNodeIndex - 1))
				Uuid = uTempUuid;

		}
	}
	if (DSP_SUCCEEDED(status)) {
		if (uNode > (uNodeIndex - 1)) {
			status = DSP_EINVALIDARG;
			GT_0trace(MGR_DebugMask, GT_7CLASS,
				 "Manager_EnumNodeInfo: uNode"
				 " is Invalid \r\n");
		} else {
			status1 = DCD_GetObjectDef(pMgrObject->hDcdMgr,
						(struct DSP_UUID *)&Uuid,
						DSP_DCDNODETYPE, &GenObj);
			if (DSP_SUCCEEDED(status1)) {
				/* Get the Obj def */
				*pNDBProps = GenObj.objData.nodeObj.ndbProps;
				*puNumNodes = uNodeIndex;
				status = DSP_SOK;
			} else {
				GT_0trace(MGR_DebugMask, GT_7CLASS,
					 "Manager_EnumNodeInfo: "
					 "Failed to Get Node Info \r\n");
				status = DSP_EFAIL;
			}
		}
	} else {
		/* This could be changed during enum, EFAIL ... */
		GT_0trace(MGR_DebugMask, GT_7CLASS, "Manager_EnumNodeInfo: "
			 "Enumeration failure\r\n");
		status = DSP_EFAIL;
	}
func_cont:
	GT_4trace(MGR_DebugMask, GT_ENTER,
		 "Exiting Manager_EnumNodeInfo, args:\n\t"
		 "uNode: 0x%x\n\tpNDBProps:  0x%x\n\tuNDBPropsSize:"
		 " 0x%x\tuNumNodes: 0x%x\n", uNode, pNDBProps,
		uNDBPropsSize, *puNumNodes);
	DBC_Ensure((DSP_SUCCEEDED(status) && *puNumNodes > 0) ||
		  (DSP_FAILED(status) && *puNumNodes == 0));

	return status;
}

/*
 *  ======== MGR_EnumProcessorInfo ========
 *      Enumerate and get configuration information about available
 *      DSP processors.
 */
DSP_STATUS MGR_EnumProcessorInfo(u32 uProcessor,
				OUT struct DSP_PROCESSORINFO *pProcessorInfo,
				u32 uProcessorInfoSize, OUT u32 *puNumProcs)
{
	DSP_STATUS status = DSP_SOK;
	DSP_STATUS status1 = DSP_SOK;
	DSP_STATUS status2 = DSP_SOK;
	struct DSP_UUID uTempUuid;
	u32 uTempIndex = 0;
	u32 uProcIndex = 0;
	struct DCD_GENERICOBJ GenObj;
	struct MGR_OBJECT *pMgrObject = NULL;
	struct MGR_PROCESSOREXTINFO *pExtInfo;
	struct DEV_OBJECT *hDevObject;
	struct DRV_OBJECT *hDrvObject;
	s32 devType;
	struct CFG_DEVNODE *devNode;
	struct CFG_DSPRES chipResources;
	bool procDetect = false;

	DBC_Require(pProcessorInfo != NULL);
	DBC_Require(puNumProcs != NULL);
	DBC_Require(uProcessorInfoSize >= sizeof(struct DSP_PROCESSORINFO));
	DBC_Require(cRefs > 0);

	GT_4trace(MGR_DebugMask, GT_ENTER,
		 "Entered Manager_EnumProcessorInfo, "
		 "args:\n\tuProcessor:  0x%x\n\tpProcessorInfo: 0x%x\n\t"
		 "uProcessorInfoSize: 0x%x\tpuNumProcs: 0x%x\n", uProcessor,
		 pProcessorInfo, uProcessorInfoSize, puNumProcs);
	*puNumProcs = 0;
	status = CFG_GetObject((u32 *)&hDrvObject, REG_DRV_OBJECT);
	if (DSP_SUCCEEDED(status)) {
		status = DRV_GetDevObject(uProcessor, hDrvObject, &hDevObject);
		if (DSP_SUCCEEDED(status)) {
			status = DEV_GetDevType(hDevObject, (u32 *) &devType);
			status = DEV_GetDevNode(hDevObject, &devNode);
			if (devType == DSP_UNIT) {
				status = CFG_GetDSPResources(devNode,
							 &chipResources);
			} else {
				status = DSP_EFAIL;
				GT_1trace(MGR_DebugMask, GT_7CLASS,
					 "Unsupported dev type gotten"
					 "from device object %d\n", devType);
			}
			if (DSP_SUCCEEDED(status)) {
				pProcessorInfo->uProcessorType =
						chipResources.uChipType;
			}
		}
	}
	if (DSP_FAILED(status))
		goto func_end;

	/* Get The Manager Object from the Registry */
	if (DSP_FAILED(CFG_GetObject((u32 *)&pMgrObject,
	   REG_MGR_OBJECT))) {
		GT_0trace(MGR_DebugMask, GT_7CLASS,
			 "Manager_EnumProcessorInfo: "
			 "Failed To Get MGR Object from Registry\r\n");
		goto func_end;
	}
	DBC_Assert(MEM_IsValidHandle(pMgrObject, SIGNATURE));
	/* Forever loop till we hit no more items in the
	 * Enumeration. We will exit the loop other than DSP_SOK; */
	while (status1 == DSP_SOK) {
		status1 = DCD_EnumerateObject(uTempIndex++,
					     DSP_DCDPROCESSORTYPE,
					     &uTempUuid);
		if (status1 != DSP_SOK)
			break;

		uProcIndex++;
		/* Get the Object properties to find the Device/Processor
		 * Type */
		if (procDetect != false)
			continue;

		status2 = DCD_GetObjectDef(pMgrObject->hDcdMgr,
					(struct DSP_UUID *)&uTempUuid,
					DSP_DCDPROCESSORTYPE,
					&GenObj);
		if (DSP_SUCCEEDED(status2)) {
			/* Get the Obj def */
			if (uProcessorInfoSize <
					sizeof(struct MGR_PROCESSOREXTINFO)) {
				*pProcessorInfo = GenObj.objData.procObj;
			} else {
				/* extended info */
				pExtInfo = (struct MGR_PROCESSOREXTINFO *)
						pProcessorInfo;
				*pExtInfo = GenObj.objData.extProcObj;
			}
			GT_1trace(MGR_DebugMask, GT_7CLASS,
				 "Manager_EnumProcessorInfo: Got"
				 " Proctype  from DCD %x \r\n",
				 pProcessorInfo->uProcessorType);
			/* See if we got the needed processor */
			if (devType == DSP_UNIT) {
				if (pProcessorInfo->uProcessorType ==
				   DSPPROCTYPE_C64)
					procDetect = true;
			} else if (devType == IVA_UNIT) {
				if (pProcessorInfo->uProcessorType ==
				   IVAPROCTYPE_ARM7)
					procDetect = true;
			}
			/* User applciatiuons aonly check for chip type, so
			 * this clumsy overwrite */
			pProcessorInfo->uProcessorType =
					 chipResources.uChipType;
		} else {
			GT_1trace(MGR_DebugMask, GT_7CLASS,
				 "Manager_EnumProcessorInfo: "
				 "Failed to Get DCD Processor Info %x \r\n",
				 status2);
			status = DSP_EFAIL;
		}
	}
	*puNumProcs = uProcIndex;
	if (procDetect == false) {
		GT_0trace(MGR_DebugMask, GT_7CLASS,
			 "Manager_EnumProcessorInfo: Failed"
			 " to get Proc info from DCD , so use CFG registry\n");
		pProcessorInfo->uProcessorType = chipResources.uChipType;
	}
func_end:
	return status;
}

/*
 *  ======== MGR_Exit ========
 *      Decrement reference count, and free resources when reference count is
 *      0.
 */
void MGR_Exit(void)
{
	DBC_Require(cRefs > 0);
	cRefs--;
	if (cRefs == 0)
		DCD_Exit();

	GT_1trace(MGR_DebugMask, GT_5CLASS,
		 "Entered MGR_Exit, ref count: 0x%x\n", cRefs);
	DBC_Ensure(cRefs >= 0);
}

/*
 *  ======== MGR_GetDCDHandle ========
 *      Retrieves the MGR handle. Accessor Function.
 */
DSP_STATUS MGR_GetDCDHandle(struct MGR_OBJECT *hMGRHandle,
			   OUT u32 *phDCDHandle)
{
	DSP_STATUS status = DSP_EFAIL;
	struct MGR_OBJECT *pMgrObject = (struct MGR_OBJECT *)hMGRHandle;

	DBC_Require(cRefs > 0);
	DBC_Require(phDCDHandle != NULL);

	*phDCDHandle = (u32)NULL;
	if (MEM_IsValidHandle(pMgrObject, SIGNATURE)) {
		*phDCDHandle = (u32) pMgrObject->hDcdMgr;
		status = DSP_SOK;
	}
	DBC_Ensure((DSP_SUCCEEDED(status) && *phDCDHandle != (u32)NULL) ||
		  (DSP_FAILED(status) && *phDCDHandle == (u32)NULL));

	return status;
}

/*
 *  ======== MGR_Init ========
 *      Initialize MGR's private state, keeping a reference count on each call.
 */
bool MGR_Init(void)
{
	bool fRetval = true;
	bool fInitDCD = false;

	DBC_Require(cRefs >= 0);

	if (cRefs == 0) {

		/* Set the Trace mask */
		DBC_Assert(!MGR_DebugMask.flags);

		GT_create(&MGR_DebugMask, "MG");	/* "MG" for Manager */
		fInitDCD = DCD_Init();	/*  DCD Module */

		if (!fInitDCD) {
			fRetval = false;
			GT_0trace(MGR_DebugMask, GT_6CLASS,
				 "MGR_Init failed\n");
		}
	}

	if (fRetval)
		cRefs++;


	GT_1trace(MGR_DebugMask, GT_5CLASS,
		 "Entered MGR_Init, ref count:  0x%x\n", cRefs);
	DBC_Ensure((fRetval && (cRefs > 0)) || (!fRetval && (cRefs >= 0)));

	return fRetval;
}

/*
 *  ======== MGR_WaitForBridgeEvents ========
 *      Block on any Bridge event(s)
 */
DSP_STATUS MGR_WaitForBridgeEvents(struct DSP_NOTIFICATION **aNotifications,
				  u32 uCount, OUT u32 *puIndex, u32 uTimeout)
{
	DSP_STATUS status;
	struct SYNC_OBJECT *hSyncEvents[MAX_EVENTS];
	u32 i;

	DBC_Require(uCount < MAX_EVENTS);

	for (i = 0; i < uCount; i++)
		hSyncEvents[i] = aNotifications[i]->handle;

	status = SYNC_WaitOnMultipleEvents(hSyncEvents, uCount, uTimeout,
					 puIndex);

	return status;

}

