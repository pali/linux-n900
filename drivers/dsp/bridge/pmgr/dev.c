/*
 * dev.c
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
 *  ======== dev.c ========
 *  Description:
 *      Implementation of 'Bridge Mini-driver device operations.
 *
 *  Public Functions:
 *      DEV_BrdWriteFxn
 *      DEV_CreateDevice
 *      DEV_Create2
 *      DEV_Destroy2
 *      DEV_DestroyDevice
 *      DEV_GetChnlMgr
 *      DEV_GetCmmMgr
 *      DEV_GetCodMgr
 *      DEV_GetDehMgr
 *      DEV_GetDevNode
 *      DEV_GetDSPWordSize
 *      DEV_GetFirst
 *      DEV_GetIntfFxns
 *      DEV_GetIOMgr
 *      DEV_GetNext
 *      DEV_GetNodeManager
 *      DEV_GetSymbol
 *      DEV_GetWMDContext
 *      DEV_Exit
 *      DEV_Init
 *      DEV_InsertProcObject
 *      DEV_IsLocked
 *      DEV_NotifyClient
 *      DEV_RegisterNotify
 *      DEV_ReleaseCodMgr
 *      DEV_RemoveDevice
 *      DEV_RemoveProcObject
 *      DEV_SetChnlMgr
 *      DEV_SetMsgMgr
 *      DEV_SetLockOwner
 *      DEV_StartDevice
 *
 *  Private Functions:
 *      FxnNotImplemented
 *      InitCodMgr
 *      InsertDevObject
 *      IsValidHandle
 *      RemoveDevObject
 *      StoreInterfaceFxns
 *
 *! Revision History:
 *! ================
 *! 03-Jan-2005 hn  Support for IVA DEH
 *! 08-Mar-2004 sb  Added the Dynamic Memory Mapping feature
 *! 09-Feb-2004 vp	Updated to support IVA.
 *! 24-Feb-2003 swa PMGR Code review comments incorporated.
 *! 29-Nov-2001 jeh Check for DSP_ENOTIMPL status of DEH create function.
 *! 05-Nov-2001 kc  Added support for DEH module.
 *! 05-Aug-2001 ag  Shared memory registration moved to WMD_IO_OnLoaded().
 *! 11-Jul-2001 jeh Moved MGR_Create() from DSP_Init() to DEV_StartDevice().
 *! 11-Apr-2001 rr: Removed CMM_RegisterGPPSMSeg.
 *! 02-Apr-2001 rr: CHNL_Create failure is printed out.
 *! 15-Jan-2001 jeh Removed call to IO_OnLoaded() from DEV_Create2().
 *! 13-Feb-2001 kc: DSP/BIOS Bridge name update.
 *! 15-Dec-2000 rr: Dev_Create2 returns error if NODE_CreateMgr fails.
 *! 05-Dec-2000 jeh Moved IO_OnLoaded() to PROC_Load. Added DEV_SetMsgMgr.
 *! 05-Dev-2000 ag  SM Heap for messaging registered via CMM_RegisterGPPSMSeg().
 *!                 SM heap base and size currently taken from registry.
 *! 29-Nov-2000 rr: Incorporated code review changes.
 *! 17-Nov-2000 jeh Added calls to get IO manager (IO_Create), IO_OnLoaded().
 *! 06-Oct-2000 rr: DEV_Destroy2 and DEV_Create2 added.
 *! 02-Oct-2000 rr: DEV_GetNodeManager added.
 *! 11-Aug-2000 ag: Added DEV_GetCmmMgr(), CMM_Init() & CMM_Exit().
 *!                 Removed <dspbridge/std.h> & <stdwin.h>, added <dspbridge/dbtype.h>
 *! 10-Aug-2000 rr: DEV_InsertProcObject/RemoveProcObject added.
 *!                 DEV_Cleanup calls PROC_Detach if it is a matching process.
 *! 27-Jul-2000 rr: DEV is in new directoy DEV and produces devlib.lib
 *! 17-Jul-2000 rr: DRV Object holds the list of Dev Objects. DEV gets
 *!                 the List and Next devices through DRV.
 *!                 DEV object has a back pointer to DRV Object.
 *! 06-Jun-2000 jeh Added DEV_GetSymbol().
 *! 09-May-2000 rr: dwMemBase has index for multiple windows need.
 *! 28-Feb-2000 rr: New GT Usage implemented.
 *! 03-Feb-2000 rr: GT and Module init/exit Changes.(Done up front from
 *!		    SERVICES)
 *! 31-Jan-2000 rr: Comments changed after code review.
 *! 21-Jan-2000 rr: windows.h, tchar.h, HMODULE removed. FreeLibrary replaced
 *!                 with LDR_FreeModule
 *! 17-Jan-2000 rr: CFG_Get/SetPrivateDword renamed to CFG_Get/SetDevObject.
 *!                 StoreInterfaceFxns stores the new fxn WMD_BRD_SETSTATE.
 *! 20-Nov-1999 ag: Actual uSMLength =  total - monitor offset.
 *! 12-Nov-1999 rr: bIRQ and IRQAttrib taken from the struct CFG_HOSTRES.
 *!		    dMemBase is added with offset for monitor taken from
 *!		    registry.
 *! 31-Oct-1999 ag: Added CHNL support.
 *! 10-Sep-1999 rr: GT Enabled. DEV_Create will Load the Mini Driver and will
 *!                 find its fxn table. Right now lot of things are hardcoded
 *!                 as the REG is not ready.
 *! 10-Jun-1996 rr: Created from WSX
 */

/*  ----------------------------------- Host OS */
#include <dspbridge/host_os.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>
#include <dspbridge/dbdefs.h>
#include <dspbridge/errbase.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>
#include <dspbridge/dbg.h>
#include <dspbridge/gt.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/cfg.h>
#include <dspbridge/ldr.h>
#include <dspbridge/list.h>
#include <dspbridge/mem.h>
#include <dspbridge/util.h>

/*  ----------------------------------- Platform Manager */
#include <dspbridge/cod.h>
#include <dspbridge/drv.h>
#include <dspbridge/proc.h>
#include <dspbridge/dmm.h>

/*  ----------------------------------- Resource Manager */
#include <dspbridge/mgr.h>
#include <dspbridge/node.h>

/*  ----------------------------------- Others */
#include <dspbridge/dbreg.h>
#include <dspbridge/wcd.h>		/* WCD version info. */

#include <dspbridge/chnl.h>
#include <dspbridge/io.h>
#include <dspbridge/msg.h>
#include <dspbridge/cmm.h>

/*  ----------------------------------- This */
#include <dspbridge/dev.h>

/*  ----------------------------------- Defines, Data Structures, Typedefs */

#define SIGNATURE           0x5f564544	/* "DEV_" (in reverse) */
#define MAKEVERSION(major, minor)   (major * 10 + minor)
#define WCDVERSION          MAKEVERSION(WCD_MAJOR_VERSION, WCD_MINOR_VERSION)

/* The WMD device object: */
struct DEV_OBJECT {
	/* LST requires "link" to be first field!                        */
	struct LST_ELEM link;		/* Link to next DEV_OBJECT.      */
	u32 devType;		/* Device Type */
	u32 dwSignature;	/* Used for object validation.   */
	struct CFG_DEVNODE *hDevNode;	/* Platform specific device id   */
	struct WMD_DEV_CONTEXT *hWmdContext;	/* WMD Context Handle        */
	struct WMD_DRV_INTERFACE intfFxns;	/* Function interface to WMD. */
	struct BRD_OBJECT *lockOwner;	/* Client with exclusive access. */
	struct COD_MANAGER *hCodMgr;	/* Code manager handle.          */
	struct CHNL_MGR *hChnlMgr;	/* Channel manager.              */
	struct DEH_MGR *hDehMgr;	/* DEH manager.                  */
	struct MSG_MGR *hMsgMgr;	/* Message manager.              */
	struct IO_MGR *hIOMgr;		/* IO manager (CHNL, MSG)        */
	struct CMM_OBJECT *hCmmMgr;	/* SM memory manager.            */
	struct DMM_OBJECT *hDmmMgr;	/* Dynamic memory manager.       */
	struct LDR_MODULE *hModule;	/* WMD Module handle.            */
	u32 uWordSize;	/* DSP word size: quick access.  */
	struct DRV_OBJECT *hDrvObject;	/* Driver Object                 */
	struct LST_LIST *procList;	/* List of Proceeosr attached to
				 * this device  */
	struct NODE_MGR *hNodeMgr;
} ;

/*  ----------------------------------- Globals */
static u32 cRefs;		/* Module reference count */
#if GT_TRACE
static struct GT_Mask debugMask = { NULL, NULL };	/* For debugging */
#endif

/*  ----------------------------------- Function Prototypes */
static DSP_STATUS FxnNotImplemented(int arg, ...);
static DSP_STATUS InitCodMgr(struct DEV_OBJECT *pDevObject);
static bool IsValidHandle(struct DEV_OBJECT *hObj);
static void StoreInterfaceFxns(struct WMD_DRV_INTERFACE *pDrvFxns,
			       OUT struct WMD_DRV_INTERFACE *pIntfFxns);
/*
 *  ======== DEV_BrdWriteFxn ========
 *  Purpose:
 *      Exported function to be used as the COD write function.  This function
 *      is passed a handle to a DEV_hObject, then calls the
 *      device's WMD_BRD_Write() function.
 */
u32 DEV_BrdWriteFxn(void *pArb, u32 ulDspAddr, void *pHostBuf,
		      u32 ulNumBytes, u32 nMemSpace)
{
	struct DEV_OBJECT *pDevObject = (struct DEV_OBJECT *)pArb;
	u32 ulWritten = 0;
	DSP_STATUS status;

	DBC_Require(cRefs > 0);
	DBC_Require(pHostBuf != NULL);	/* Required of BrdWrite(). */
	GT_5trace(debugMask, GT_ENTER,
		"Entered DEV_BrdWriteFxn, pArb: 0x%x\n\t\t"
		"ulDspAddr: 0x%x\n\t\tpHostBuf: 0x%x\n \t\tulNumBytes:  0x%x\n"
		"\t\tnMemSpace:  0x%x\n", pArb, ulDspAddr, pHostBuf,
		ulNumBytes, nMemSpace);
	if (IsValidHandle(pDevObject)) {
		/* Require of BrdWrite() */
		DBC_Assert(pDevObject->hWmdContext != NULL);
		status = (*pDevObject->intfFxns.pfnBrdWrite)(pDevObject->
			 hWmdContext, pHostBuf, ulDspAddr, ulNumBytes,
			 nMemSpace);
		 /* Special case of getting the address only */
		if (ulNumBytes == 0)
			ulNumBytes = 1;
		if (DSP_SUCCEEDED(status))
			ulWritten = ulNumBytes;

	}
	GT_1trace(debugMask, GT_ENTER, "Exit DEV_BrdWriteFxn ulWritten: 0x%x\n",
		  ulWritten);
	return ulWritten;
}

/*
 *  ======== DEV_CreateDevice ========
 *  Purpose:
 *      Called by the operating system to load the PM Mini Driver for a
 *      PM board (device).
 */
DSP_STATUS DEV_CreateDevice(OUT struct DEV_OBJECT **phDevObject,
			    IN CONST char *pstrWMDFileName,
			    IN CONST struct CFG_HOSTRES *pHostConfig,
			    IN CONST struct CFG_DSPRES *pDspConfig,
			    struct CFG_DEVNODE *hDevNode)
{
	struct LDR_MODULE *hModule = NULL;
	struct WMD_DRV_INTERFACE *pDrvFxns = NULL;
	struct DEV_OBJECT *pDevObject = NULL;
	struct CHNL_MGRATTRS mgrAttrs;
	struct IO_ATTRS ioMgrAttrs;
	u32 uNumWindows;
	struct DRV_OBJECT *hDrvObject = NULL;
	DSP_STATUS status = DSP_SOK;
	DBC_Require(cRefs > 0);
	DBC_Require(phDevObject != NULL);
	DBC_Require(pstrWMDFileName != NULL);
	DBC_Require(pHostConfig != NULL);
	DBC_Require(pDspConfig != NULL);

	GT_5trace(debugMask, GT_ENTER,
		  "Entered DEV_CreateDevice, phDevObject: 0x%x\n"
		  "\t\tpstrWMDFileName:  0x%x\n\t\tpHostConfig:0x%x\n\t\t"
		  "pDspConfig:  0x%x\n\t\tnhDevNode:  0x%x\n", phDevObject,
		  pstrWMDFileName, pHostConfig, pDspConfig, hDevNode);
	/*  Get the WMD interface functions*/
	WMD_DRV_Entry(&pDrvFxns, pstrWMDFileName);
	if (DSP_FAILED(CFG_GetObject((u32 *) &hDrvObject, REG_DRV_OBJECT))) {
		/* don't propogate CFG errors from this PROC function */
		GT_0trace(debugMask, GT_7CLASS,
			  "Failed to get the DRV Object \n");
		status = DSP_EFAIL;
	}
	/* Create the device object, and pass a handle to the WMD for
	 * storage. */
	if (DSP_SUCCEEDED(status)) {
		DBC_Assert(pDrvFxns);
		MEM_AllocObject(pDevObject, struct DEV_OBJECT, SIGNATURE);
		if (pDevObject) {
			/* Fill out the rest of the Dev Object structure: */
			pDevObject->hDevNode = hDevNode;
			pDevObject->hModule = hModule;
			pDevObject->hCodMgr = NULL;
			pDevObject->hChnlMgr = NULL;
			pDevObject->hDehMgr = NULL;
			pDevObject->lockOwner = NULL;
			pDevObject->uWordSize = pDspConfig->uWordSize;
			pDevObject->hDrvObject = hDrvObject;
			pDevObject->devType = DSP_UNIT;
			/* Store this WMD's interface functions, based on its
			 * version. */
			StoreInterfaceFxns(pDrvFxns, &pDevObject->intfFxns);
			/* Call WMD_DEV_CREATE() to get the WMD's device
			 * context handle. */
			status = (pDevObject->intfFxns.pfnDevCreate)
				 (&pDevObject->hWmdContext, pDevObject,
				 pHostConfig, pDspConfig);
			/* Assert WMD_DEV_Create()'s ensure clause: */
			DBC_Assert(DSP_FAILED(status) || (pDevObject->
				   hWmdContext != NULL));
		} else {
			GT_0trace(debugMask, GT_7CLASS,
				  "DEV_Create: Out Of Memory");
			status = DSP_EMEMORY;
		}
	}
	/* Attempt to create the COD manager for this device: */
	if (DSP_SUCCEEDED(status))
		status = InitCodMgr(pDevObject);

	/* Attempt to create the channel manager for this device: */
	if (DSP_SUCCEEDED(status)) {
		mgrAttrs.cChannels = CHNL_MAXCHANNELS;
		ioMgrAttrs.bIRQ = pHostConfig->bIRQRegisters;
		ioMgrAttrs.fShared = (pHostConfig->bIRQAttrib & CFG_IRQSHARED);
		ioMgrAttrs.uWordSize = pDspConfig->uWordSize;
		mgrAttrs.uWordSize = pDspConfig->uWordSize;
		uNumWindows = pHostConfig->wNumMemWindows;
		if (uNumWindows) {
			/* Assume last memory window is for CHNL */
			ioMgrAttrs.dwSMBase = pHostConfig->dwMemBase[1] +
					      pHostConfig->dwOffsetForMonitor;
			ioMgrAttrs.uSMLength = pHostConfig->dwMemLength[1] -
					       pHostConfig->dwOffsetForMonitor;
		} else {
			ioMgrAttrs.dwSMBase = 0;
			ioMgrAttrs.uSMLength = 0;
			GT_0trace(debugMask, GT_7CLASS,
				  "**There is no memory reserved for "
				  "shared structures**\n");
		}
		status = CHNL_Create(&pDevObject->hChnlMgr, pDevObject,
				    &mgrAttrs);
		if (status == DSP_ENOTIMPL) {
			/* It's OK for a device not to have a channel
			 * manager: */
			status = DSP_SOK;
		}
		/* Create CMM mgr even if Msg Mgr not impl.  */
		status = CMM_Create(&pDevObject->hCmmMgr,
				   (struct DEV_OBJECT *)pDevObject, NULL);
		if (DSP_FAILED(status)) {
			GT_0trace(debugMask, GT_7CLASS,
				  "DEV_Create: Failed to Create SM "
				  "Manager\n");
		}
		/* Only create IO manager if we have a channel manager */
		if (DSP_SUCCEEDED(status) && pDevObject->hChnlMgr) {
			status = IO_Create(&pDevObject->hIOMgr, pDevObject,
					   &ioMgrAttrs);
		}
		/* Only create DEH manager if we have an IO manager */
		if (DSP_SUCCEEDED(status)) {
			/* Instantiate the DEH module */
			status = (*pDevObject->intfFxns.pfnDehCreate)
				 (&pDevObject->hDehMgr, 	pDevObject);
		}
		/* Create DMM mgr .  */
		status = DMM_Create(&pDevObject->hDmmMgr,
				   (struct DEV_OBJECT *)pDevObject, NULL);
		if (DSP_FAILED(status)) {
			GT_0trace(debugMask, GT_7CLASS,
				  "DEV_Create: Failed to Create DMM "
				  "Manager\n");
		}
	}
	/* Add the new DEV_Object to the global list: */
	if (DSP_SUCCEEDED(status)) {
		LST_InitElem(&pDevObject->link);
		status = DRV_InsertDevObject(hDrvObject, pDevObject);
	}
	/* Create the Processor List */
	if (DSP_SUCCEEDED(status)) {
		pDevObject->procList = LST_Create();
		if (!(pDevObject->procList)) {
			status = DSP_EFAIL;
			GT_0trace(debugMask, GT_7CLASS, "DEV_Create: "
				 "Failed to Create Proc List");
		}
	}
	 /*  If all went well, return a handle to the dev object;
	 *  else, cleanup and return NULL in the OUT parameter.  */
	if (DSP_SUCCEEDED(status)) {
		*phDevObject = pDevObject;
		GT_1trace(debugMask, GT_1CLASS,
			 "DEV_CreateDevice Succeeded \nDevObject "
			 "0x%x\n", pDevObject);
	} else {
		if (pDevObject && pDevObject->procList)
			LST_Delete(pDevObject->procList);

		if (pDevObject && pDevObject->hCodMgr)
			COD_Delete(pDevObject->hCodMgr);

		if (pDevObject && pDevObject->hDmmMgr)
			DMM_Destroy(pDevObject->hDmmMgr);

		if (pDevObject)
			MEM_FreeObject(pDevObject);

		*phDevObject = NULL;
		GT_0trace(debugMask, GT_7CLASS, "DEV_CreateDevice Failed\n");
	}
	GT_1trace(debugMask, GT_1CLASS, "Exiting DEV_Create: DevObject 0x%x\n",
		 *phDevObject);
	DBC_Ensure((DSP_SUCCEEDED(status) && IsValidHandle(*phDevObject)) ||
		  (DSP_FAILED(status) && !*phDevObject));
	return status;
}

/*
 *  ======== DEV_Create2 ========
 *  Purpose:
 *      After successful loading of the image from WCD_InitComplete2
 *      (PROC Auto_Start) or PROC_Load this fxn is called. This creates
 *      the Node Manager and updates the DEV Object.
 */
DSP_STATUS DEV_Create2(struct DEV_OBJECT *hDevObject)
{
	DSP_STATUS status = DSP_SOK;
	struct DEV_OBJECT *pDevObject = hDevObject;

	DBC_Require(cRefs > 0);
	DBC_Require(IsValidHandle(hDevObject));

	GT_1trace(debugMask, GT_ENTER,
		 "Entered DEV_Create2, hDevObject: 0x%x\n", hDevObject);
	/* There can be only one Node Manager per DEV object */
	DBC_Assert(!pDevObject->hNodeMgr);
	status = NODE_CreateMgr(&pDevObject->hNodeMgr, hDevObject);
	if (DSP_FAILED(status)) {
		GT_1trace(debugMask, GT_7CLASS,
			 "DEV_Create2: NODE_CreateMgr failed, "
			 "0x%x!\n", status);
		pDevObject->hNodeMgr = NULL;
		GT_0trace(debugMask, GT_7CLASS, "DEV_Create2: Failed!!\n");
	}
	DBC_Ensure((DSP_SUCCEEDED(status) && pDevObject->hNodeMgr != NULL)
		   || (DSP_FAILED(status) && pDevObject->hNodeMgr == NULL));
	GT_2trace(debugMask, GT_ENTER,
		  "Exiting DEV_Create2, hNodeMgr:  0x%x, status:"
		  " 0x%x\n", pDevObject->hNodeMgr, status);
	return status;
}

/*
 *  ======== DEV_Destroy2 ========
 *  Purpose:
 *      Destroys the Node manager for this device.
 */
DSP_STATUS DEV_Destroy2(struct DEV_OBJECT *hDevObject)
{
	DSP_STATUS status = DSP_SOK;
	struct DEV_OBJECT *pDevObject = hDevObject;

	DBC_Require(cRefs > 0);
	DBC_Require(IsValidHandle(hDevObject));

	GT_1trace(debugMask, GT_ENTER,
		 "Entered DEV_Destroy2, hDevObject: 0x%x\n",
		 hDevObject);
	if (pDevObject->hNodeMgr) {
		if (DSP_FAILED(NODE_DeleteMgr(pDevObject->hNodeMgr)))
			status = DSP_EFAIL;
		else
			pDevObject->hNodeMgr = NULL;

	}
	if (DSP_FAILED(status))
		GT_0trace(debugMask, GT_7CLASS, "DEV_Destroy2 failed!!\n");

	DBC_Ensure((DSP_SUCCEEDED(status) && pDevObject->hNodeMgr == NULL) ||
		  DSP_FAILED(status));
	GT_2trace(debugMask, GT_ENTER,
		 "Exiting DEV_Destroy2, hNodeMgr: 0x%x, status"
		 " = 0x%x\n", pDevObject->hNodeMgr, status);
	return status;
}

/*
 *  ======== DEV_DestroyDevice ========
 *  Purpose:
 *      Destroys the channel manager for this device, if any, calls
 *      WMD_DEV_Destroy(), and then attempts to unload the WMD module.
 */
DSP_STATUS DEV_DestroyDevice(struct DEV_OBJECT *hDevObject)
{
	DSP_STATUS status = DSP_SOK;
	struct DEV_OBJECT *pDevObject = hDevObject;

	DBC_Require(cRefs > 0);

	GT_1trace(debugMask, GT_ENTER, "Entered DEV_DestroyDevice, hDevObject: "
		 "0x%x\n", hDevObject);
	if (IsValidHandle(hDevObject)) {
		if (pDevObject->hCodMgr)
			COD_Delete(pDevObject->hCodMgr);

		if (pDevObject->hNodeMgr)
			NODE_DeleteMgr(pDevObject->hNodeMgr);

		/* Free the io, channel, and message managers for this board: */
		if (pDevObject->hIOMgr) {
			IO_Destroy(pDevObject->hIOMgr);
			pDevObject->hIOMgr = NULL;
		}
		if (pDevObject->hChnlMgr) {
			CHNL_Destroy(pDevObject->hChnlMgr);
			pDevObject->hChnlMgr = NULL;
		}
		if (pDevObject->hMsgMgr)
			MSG_Delete(pDevObject->hMsgMgr);

		if (pDevObject->hDehMgr) {
			/* Uninitialize DEH module. */
			(*pDevObject->intfFxns.pfnDehDestroy)
			(pDevObject->hDehMgr);
		}
		if (pDevObject->hCmmMgr)
			CMM_Destroy(pDevObject->hCmmMgr, true);

		if (pDevObject->hDmmMgr)
			DMM_Destroy(pDevObject->hDmmMgr);

		/* Call the driver's WMD_DEV_Destroy() function: */
		/* Require of DevDestroy */
		DBC_Assert(pDevObject->hWmdContext != NULL);
		status = (*pDevObject->intfFxns.pfnDevDestroy)
			 (pDevObject->hWmdContext);
		if (DSP_SUCCEEDED(status)) {
			if (pDevObject->procList)
				LST_Delete(pDevObject->procList);

			/* Remove this DEV_Object from the global list: */
			DRV_RemoveDevObject(pDevObject->hDrvObject, pDevObject);
			/* Free The library * LDR_FreeModule
			 * (pDevObject->hModule);*/
			/* Free this dev object: */
			MEM_FreeObject(pDevObject);
		}
	} else {
		GT_0trace(debugMask, GT_7CLASS, "DEV_Destroy: Invlaid handle");
		status = DSP_EHANDLE;
	}
	GT_1trace(debugMask, GT_ENTER, "Exit DEV_destroy: status 0x%x\n",
		 status);
	return status;
}

/*
 *  ======== DEV_GetChnlMgr ========
 *  Purpose:
 *      Retrieve the handle to the channel manager handle created for this
 *      device.
 */
DSP_STATUS DEV_GetChnlMgr(struct DEV_OBJECT *hDevObject,
			 OUT struct CHNL_MGR **phMgr)
{
	DSP_STATUS status = DSP_SOK;
	struct DEV_OBJECT *pDevObject = hDevObject;

	DBC_Require(cRefs > 0);
	DBC_Require(phMgr != NULL);

	GT_2trace(debugMask, GT_ENTER,
		 "Entered DEV_GetChnlMgr, hDevObject: 0x%x\n\t"
		 "\tphMgr: 0x%x\n", hDevObject, phMgr);
	if (IsValidHandle(hDevObject)) {
		*phMgr = pDevObject->hChnlMgr;
	} else {
		*phMgr = NULL;
		status = DSP_EHANDLE;
		GT_0trace(debugMask, GT_7CLASS,
			 "DEV_GetChnlMgr: Invalid handle");
	}
	GT_2trace(debugMask, GT_ENTER,
		 "Exit DEV_GetChnlMgr: status 0x%x\t\n hMgr: "
		 "0x%x\n", status, *phMgr);
	DBC_Ensure(DSP_SUCCEEDED(status) || ((phMgr != NULL) &&
		  (*phMgr == NULL)));
	return status;
}

/*
 *  ======== DEV_GetCmmMgr ========
 *  Purpose:
 *      Retrieve the handle to the shared memory manager created for this
 *      device.
 */
DSP_STATUS DEV_GetCmmMgr(struct DEV_OBJECT *hDevObject,
			OUT struct CMM_OBJECT **phMgr)
{
	DSP_STATUS status = DSP_SOK;
	struct DEV_OBJECT *pDevObject = hDevObject;

	DBC_Require(cRefs > 0);
	DBC_Require(phMgr != NULL);
	GT_2trace(debugMask, GT_ENTER,
		 "Entered DEV_GetCmmMgr, hDevObject:  0x%x\n\t"
		 "\tphMgr:  0x%x\n", hDevObject, phMgr);
	if (IsValidHandle(hDevObject)) {
		*phMgr = pDevObject->hCmmMgr;
	} else {
		*phMgr = NULL;
		status = DSP_EHANDLE;
		GT_0trace(debugMask, GT_7CLASS,
			 "DEV_GetCmmMgr: Invalid handle");
	}
	GT_2trace(debugMask, GT_ENTER,
		 "Exit DEV_GetCmmMgr: status 0x%x\t\nhMgr: "
		 "0x%x\n", status, *phMgr);
	DBC_Ensure(DSP_SUCCEEDED(status) || ((phMgr != NULL) &&
		  (*phMgr == NULL)));
	return status;
}

/*
 *  ======== DEV_GetDmmMgr ========
 *  Purpose:
 *      Retrieve the handle to the dynamic memory manager created for this
 *      device.
 */
DSP_STATUS DEV_GetDmmMgr(struct DEV_OBJECT *hDevObject,
			OUT struct DMM_OBJECT **phMgr)
{
	DSP_STATUS status = DSP_SOK;
	struct DEV_OBJECT *pDevObject = hDevObject;

	DBC_Require(cRefs > 0);
	DBC_Require(phMgr != NULL);

	GT_2trace(debugMask, GT_ENTER, "Entered DEV_GetDmmMgr, hDevObject: "
		 "0x%x\n\t\tphMgr: 0x%x\n", hDevObject, phMgr);
	if (IsValidHandle(hDevObject)) {
		*phMgr = pDevObject->hDmmMgr;
	} else {
		*phMgr = NULL;
		status = DSP_EHANDLE;
		GT_0trace(debugMask, GT_7CLASS,
			 "DEV_GetDmmMgr: Invalid handle");
	}
	GT_2trace(debugMask, GT_ENTER,
		 "Exit DEV_GetDmmMgr: status 0x%x\t\n hMgr: "
		 "0x%x\n", status, *phMgr);
	DBC_Ensure(DSP_SUCCEEDED(status) || ((phMgr != NULL) &&
		  (*phMgr == NULL)));
	return status;
}

/*
 *  ======== DEV_GetCodMgr ========
 *  Purpose:
 *      Retrieve the COD manager create for this device.
 */
DSP_STATUS DEV_GetCodMgr(struct DEV_OBJECT *hDevObject,
			OUT struct COD_MANAGER **phCodMgr)
{
	DSP_STATUS status = DSP_SOK;
	struct DEV_OBJECT *pDevObject = hDevObject;

	DBC_Require(cRefs > 0);
	DBC_Require(phCodMgr != NULL);

	GT_2trace(debugMask, GT_ENTER,
		 "Entered DEV_GetCodMgr, hDevObject: 0x%x\n\t\t"
		 "phCodMgr: 0x%x\n", hDevObject, phCodMgr);
	if (IsValidHandle(hDevObject)) {
		*phCodMgr = pDevObject->hCodMgr;
	} else {
		*phCodMgr = NULL;
		status = DSP_EHANDLE;
		GT_1trace(debugMask, GT_7CLASS,
			 "DEV_GetCodMgr, invalid handle:  0x%x\n",
			 hDevObject);
	}
	GT_2trace(debugMask, GT_ENTER,
		 "Exit DEV_GetCodMgr: status 0x%x\t\n hCodMgr:"
		 " 0x%x\n", status, *phCodMgr);
	DBC_Ensure(DSP_SUCCEEDED(status) || ((phCodMgr != NULL) &&
		  (*phCodMgr == NULL)));
	return status;
}

/*
 *  ========= DEV_GetDehMgr ========
 */
DSP_STATUS DEV_GetDehMgr(struct DEV_OBJECT *hDevObject,
			OUT struct DEH_MGR **phDehMgr)
{
	DSP_STATUS status = DSP_SOK;

	DBC_Require(cRefs > 0);
	DBC_Require(phDehMgr != NULL);
	DBC_Require(MEM_IsValidHandle(hDevObject, SIGNATURE));
	if (IsValidHandle(hDevObject)) {
		*phDehMgr = hDevObject->hDehMgr;
	} else {
		*phDehMgr = NULL;
		status = DSP_EHANDLE;
		GT_0trace(debugMask, GT_7CLASS,
			 "DEV_GetDehMgr: Invalid handle");
	}
	return status;
}

/*
 *  ======== DEV_GetDevNode ========
 *  Purpose:
 *      Retrieve the platform specific device ID for this device.
 */
DSP_STATUS DEV_GetDevNode(struct DEV_OBJECT *hDevObject,
			 OUT struct CFG_DEVNODE **phDevNode)
{
	DSP_STATUS status = DSP_SOK;
	struct DEV_OBJECT *pDevObject = hDevObject;

	DBC_Require(cRefs > 0);
	DBC_Require(phDevNode != NULL);

	GT_2trace(debugMask, GT_ENTER,
		 "Entered DEV_GetDevNode, hDevObject: 0x%x\n\t"
		 "\tphDevNode: 0x%x\n", hDevObject, phDevNode);
	if (IsValidHandle(hDevObject)) {
		*phDevNode = pDevObject->hDevNode;
	} else {
		*phDevNode = NULL;
		status = DSP_EHANDLE;
		GT_0trace(debugMask, GT_7CLASS,
			 "DEV_GetDevNode: Invalid handle");
	}
	GT_2trace(debugMask, GT_ENTER,
		 "Exit DEV_GetDevNode: status 0x%x\t\nhDevNode:"
		 "0x%x\n", status, *phDevNode);
	DBC_Ensure(DSP_SUCCEEDED(status) || ((phDevNode != NULL) &&
		  (*phDevNode == NULL)));
	return status;
}

/*
 *  ======== DEV_GetFirst ========
 *  Purpose:
 *      Retrieve the first Device Object handle from an internal linked list
 *      DEV_OBJECTs maintained by DEV.
 */
struct DEV_OBJECT *DEV_GetFirst(void)
{
	struct DEV_OBJECT *pDevObject = NULL;

	pDevObject = (struct DEV_OBJECT *)DRV_GetFirstDevObject();

	DBC_Ensure((pDevObject == NULL) || IsValidHandle(pDevObject));

	return pDevObject;
}

/*
 *  ======== DEV_GetIntfFxns ========
 *  Purpose:
 *      Retrieve the WMD interface function structure for the loaded WMD.
 *      ppIntfFxns != NULL.
 */
DSP_STATUS DEV_GetIntfFxns(struct DEV_OBJECT *hDevObject,
			  OUT struct WMD_DRV_INTERFACE **ppIntfFxns)
{
	DSP_STATUS status = DSP_SOK;
	struct DEV_OBJECT *pDevObject = hDevObject;

	DBC_Require(cRefs > 0);
	DBC_Require(ppIntfFxns != NULL);

	GT_2trace(debugMask, GT_ENTER,
		 "Entered DEV_GetIntfFxns, hDevObject: 0x%x\n\t"
		 "\tppIntfFxns: 0x%x\n", hDevObject, ppIntfFxns);
	if (IsValidHandle(hDevObject)) {
		*ppIntfFxns = &pDevObject->intfFxns;
	} else {
		*ppIntfFxns = NULL;
		status = DSP_EHANDLE;
		GT_0trace(debugMask, GT_7CLASS,
			  "DEV_GetIntDxns: Invalid handle");
	}
	GT_2trace(debugMask, GT_ENTER, "Exit DEV_GetIntFxns: status 0x%x\t\n"
		 "ppIntFxns: 0x%x\n", status, *ppIntfFxns);
	DBC_Ensure(DSP_SUCCEEDED(status) || ((ppIntfFxns != NULL) &&
		  (*ppIntfFxns == NULL)));
	return status;
}

/*
 *  ========= DEV_GetIOMgr ========
 */
DSP_STATUS DEV_GetIOMgr(struct DEV_OBJECT *hDevObject,
			OUT struct IO_MGR **phIOMgr)
{
	DSP_STATUS status = DSP_SOK;

	DBC_Require(cRefs > 0);
	DBC_Require(phIOMgr != NULL);
	DBC_Require(MEM_IsValidHandle(hDevObject, SIGNATURE));

	if (IsValidHandle(hDevObject)) {
		*phIOMgr = hDevObject->hIOMgr;
	} else {
		*phIOMgr = NULL;
		status = DSP_EHANDLE;
		GT_0trace(debugMask, GT_7CLASS, "DEV_GetIOMgr: Invalid handle");
	}

	return status;
}

/*
 *  ======== DEV_GetNext ========
 *  Purpose:
 *      Retrieve the next Device Object handle from an internal linked list
 *      of DEV_OBJECTs maintained by DEV, after having previously called
 *      DEV_GetFirst() and zero or more DEV_GetNext
 */
struct DEV_OBJECT *DEV_GetNext(struct DEV_OBJECT *hDevObject)
{
	struct DEV_OBJECT *pNextDevObject = NULL;

	if (IsValidHandle(hDevObject)) {
		pNextDevObject = (struct DEV_OBJECT *)
				 DRV_GetNextDevObject((u32)hDevObject);
	}
	DBC_Ensure((pNextDevObject == NULL) || IsValidHandle(pNextDevObject));
	return pNextDevObject;
}

/*
 *  ========= DEV_GetMsgMgr ========
 */
void DEV_GetMsgMgr(struct DEV_OBJECT *hDevObject,
			OUT struct MSG_MGR **phMsgMgr)
{
	DBC_Require(cRefs > 0);
	DBC_Require(phMsgMgr != NULL);
	DBC_Require(MEM_IsValidHandle(hDevObject, SIGNATURE));

	*phMsgMgr = hDevObject->hMsgMgr;
}

/*
 *  ======== DEV_GetNodeManager ========
 *  Purpose:
 *      Retrieve the Node Manager Handle
 */
DSP_STATUS DEV_GetNodeManager(struct DEV_OBJECT *hDevObject,
				   OUT struct NODE_MGR **phNodeMgr)
{
	DSP_STATUS status = DSP_SOK;
	struct DEV_OBJECT *pDevObject = hDevObject;

	DBC_Require(cRefs > 0);
	DBC_Require(phNodeMgr != NULL);

	GT_2trace(debugMask, GT_ENTER,
		 "Entered DEV_GetNodeManager, hDevObject: 0x%x"
		 "\n\t\tphNodeMgr: 0x%x\n", hDevObject, phNodeMgr);
	if (IsValidHandle(hDevObject)) {
		*phNodeMgr = pDevObject->hNodeMgr;
	} else {
		*phNodeMgr = NULL;
		status = DSP_EHANDLE;
		GT_1trace(debugMask, GT_7CLASS,
			 "DEV_GetNodeManager, invalid handle: 0x"
			 "%x\n", hDevObject);
	}
	GT_2trace(debugMask, GT_ENTER,
		 "Exit DEV_GetNodeManager: status 0x%x\t\nhMgr:"
		 " 0x%x\n", status, *phNodeMgr);
	DBC_Ensure(DSP_SUCCEEDED(status) || ((phNodeMgr != NULL) &&
		  (*phNodeMgr == NULL)));
	return status;
}

/*
 *  ======== DEV_GetSymbol ========
 */
DSP_STATUS DEV_GetSymbol(struct DEV_OBJECT *hDevObject,
			      IN CONST char *pstrSym, OUT u32 *pulValue)
{
	DSP_STATUS status = DSP_SOK;
	struct COD_MANAGER *hCodMgr;

	DBC_Require(cRefs > 0);
	DBC_Require(pstrSym != NULL && pulValue != NULL);

	GT_3trace(debugMask, GT_ENTER,
		 "Entered DEV_GetSymbol, hDevObject: 0x%x\n\t\t"
		 "pstrSym: 0x%x\n\t\tpulValue: 0x%x\n", hDevObject, pstrSym,
		 pulValue);
	if (IsValidHandle(hDevObject)) {
		status = DEV_GetCodMgr(hDevObject, &hCodMgr);
		if (DSP_SUCCEEDED(status)) {
			DBC_Assert(hCodMgr != NULL);
			status = COD_GetSymValue(hCodMgr, (char *)pstrSym,
				 pulValue);
		}
	} else {
		status = DSP_EHANDLE;
		GT_0trace(debugMask, GT_7CLASS,
			 "DEV_GetSymbol: Invalid handle");
	}
	GT_2trace(debugMask, GT_ENTER, "Exit DEV_GetSymbol: status 0x%x\t\n"
		 "hWmdContext: 0x%x\n", status, *pulValue);
	return status;
}

/*
 *  ======== DEV_GetWMDContext ========
 *  Purpose:
 *      Retrieve the WMD Context handle, as returned by the WMD_Create fxn.
 */
DSP_STATUS DEV_GetWMDContext(struct DEV_OBJECT *hDevObject,
			    OUT struct WMD_DEV_CONTEXT **phWmdContext)
{
	DSP_STATUS status = DSP_SOK;
	struct DEV_OBJECT *pDevObject = hDevObject;

	DBC_Require(cRefs > 0);
	DBC_Require(phWmdContext != NULL);
	GT_2trace(debugMask, GT_ENTER,
		  "Entered DEV_GetWMDContext, hDevObject: 0x%x\n"
		  "\t\tphWmdContext: 0x%x\n", hDevObject, phWmdContext);
	if (IsValidHandle(hDevObject)) {
		*phWmdContext = pDevObject->hWmdContext;
	} else {
		*phWmdContext = NULL;
		status = DSP_EHANDLE;
		GT_0trace(debugMask, GT_7CLASS,
			 "DEV_GetWMDContext: Invalid handle");
	}

	GT_2trace(debugMask, GT_ENTER,
		 "Exit DEV_GetWMDContext: status 0x%x\t\n"
		 "hWmdContext: 0x%x\n", status, *phWmdContext);
	DBC_Ensure(DSP_SUCCEEDED(status) || ((phWmdContext != NULL) &&
		  (*phWmdContext == NULL)));
	return status;
}

/*
 *  ======== DEV_Exit ========
 *  Purpose:
 *      Decrement reference count, and free resources when reference count is
 *      0.
 */
void DEV_Exit(void)
{
	DBC_Require(cRefs > 0);

	cRefs--;

	if (cRefs == 0) {
		CMM_Exit();
		DMM_Exit();
	}

	GT_1trace(debugMask, GT_5CLASS, "Entered DEV_Exit, ref count: 0x%x\n",
		 cRefs);

	DBC_Ensure(cRefs >= 0);
}

/*
 *  ======== DEV_Init ========
 *  Purpose:
 *      Initialize DEV's private state, keeping a reference count on each call.
 */
bool DEV_Init(void)
{
	bool fCmm, fDmm, fRetval = true;

	DBC_Require(cRefs >= 0);

	if (cRefs == 0) {
		/* Set the Trace mask */
		DBC_Assert(!debugMask.flags);
		GT_create(&debugMask, "DV");	/* "DV" for DeVice */
		fCmm = CMM_Init();
		fDmm = DMM_Init();

		fRetval = fCmm && fDmm;

		if (!fRetval) {
			if (fCmm)
				CMM_Exit();


			if (fDmm)
				DMM_Exit();

		}
	}

	if (fRetval)
		cRefs++;


	GT_1trace(debugMask, GT_5CLASS, "Entered DEV_Init, ref count: 0x%x\n",
		 cRefs);

	DBC_Ensure((fRetval && (cRefs > 0)) || (!fRetval && (cRefs >= 0)));

	return fRetval;
}

/*
 *  ======== DEV_NotifyClients ========
 *  Purpose:
 *      Notify all clients of this device of a change in device status.
 */
DSP_STATUS DEV_NotifyClients(struct DEV_OBJECT *hDevObject, u32 ulStatus)
{
	DSP_STATUS status = DSP_SOK;

	struct DEV_OBJECT *pDevObject = hDevObject;
	DSP_HPROCESSOR hProcObject;

	GT_2trace(debugMask, GT_ENTER,
		 "Entered DEV_NotifyClients, hDevObject: 0x%x\n"
		 "\t\tulStatus: 0x%x\n", hDevObject, ulStatus);
	for (hProcObject = (DSP_HPROCESSOR)LST_First(pDevObject->procList);
		hProcObject != NULL;
		hProcObject = (DSP_HPROCESSOR)LST_Next(pDevObject->procList,
						(struct LST_ELEM *)hProcObject))
		PROC_NotifyClients(hProcObject, (u32) ulStatus);

	return status;
}

/*
 *  ======== DEV_RemoveDevice ========
 */
DSP_STATUS DEV_RemoveDevice(struct CFG_DEVNODE *hDevNode)
{
	struct DEV_OBJECT *hDevObject;	/* handle to device object */
	DSP_STATUS status = DSP_SOK;
	struct DEV_OBJECT *pDevObject;

	GT_1trace(debugMask, GT_ENTER,
		 "Entered DEV_RemoveDevice, hDevNode:  0x%x\n", hDevNode);
	/* Retrieve the device object handle originaly stored with
	 * the DevNode: */
	status = CFG_GetDevObject(hDevNode, (u32 *)&hDevObject);
	if (DSP_SUCCEEDED(status)) {
		/* Remove the Processor List */
		pDevObject = (struct DEV_OBJECT *)hDevObject;
		/* Destroy the device object. */
		status = DEV_DestroyDevice(hDevObject);
		if (DSP_SUCCEEDED(status)) {
			/* Null out the handle stored with the DevNode. */
			GT_0trace(debugMask, GT_1CLASS,
				 "DEV_RemoveDevice, success");
		}
	}
	GT_1trace(debugMask, GT_ENTER, "Exit DEV_RemoveDevice, status: 0x%x\n",
		  status);
	return status;
}

/*
 *  ======== DEV_SetChnlMgr ========
 *  Purpose:
 *      Set the channel manager for this device.
 */
DSP_STATUS DEV_SetChnlMgr(struct DEV_OBJECT *hDevObject, struct CHNL_MGR *hMgr)
{
	DSP_STATUS status = DSP_SOK;
	struct DEV_OBJECT *pDevObject = hDevObject;

	DBC_Require(cRefs > 0);

	GT_2trace(debugMask, GT_ENTER,
		 "Entered DEV_SetChnlMgr, hDevObject: 0x%x\n\t"
		 "\thMgr:0x%x\n", hDevObject, hMgr);
	if (IsValidHandle(hDevObject)) {
		pDevObject->hChnlMgr = hMgr;
	} else {
		status = DSP_EHANDLE;
		GT_0trace(debugMask, GT_7CLASS,
			 "DEV_SetChnlMgr, Invalid handle\n");
	}
	DBC_Ensure(DSP_FAILED(status) || (pDevObject->hChnlMgr == hMgr));
	return status;
}

/*
 *  ======== DEV_SetMsgMgr ========
 *  Purpose:
 *      Set the message manager for this device.
 */
void DEV_SetMsgMgr(struct DEV_OBJECT *hDevObject, struct MSG_MGR *hMgr)
{
	DBC_Require(cRefs > 0);
	DBC_Require(IsValidHandle(hDevObject));
	GT_2trace(debugMask, GT_ENTER,
		 "Entered DEV_SetMsgMgr, hDevObject: 0x%x\n\t\t"
		 "hMgr: 0x%x\n", hDevObject, hMgr);
	hDevObject->hMsgMgr = hMgr;
}

/*
 *  ======== DEV_StartDevice ========
 *  Purpose:
 *      Initializes the new device with the BRIDGE environment.
 */
DSP_STATUS DEV_StartDevice(struct CFG_DEVNODE *hDevNode)
{
	struct DEV_OBJECT *hDevObject = NULL;	/* handle to 'Bridge Device */
	struct CFG_HOSTRES hostRes;	/* resources struct. */
	struct CFG_DSPRES dspRes;	/* DSP resources struct */
	char szWMDFileName[CFG_MAXSEARCHPATHLEN] = "UMA"; /* wmd filename */
	DSP_STATUS status;
	struct MGR_OBJECT *hMgrObject = NULL;

	DBC_Require(cRefs > 0);

	GT_1trace(debugMask, GT_ENTER,
		 "Entered DEV_StartDevice, hDevObject: 0x%x\n", hDevNode);
		status = CFG_GetHostResources(hDevNode, &hostRes);
		if (DSP_SUCCEEDED(status)) {
			/* Get DSP resources of device from Registry: */
			status = CFG_GetDSPResources(hDevNode, &dspRes);
			if (DSP_FAILED(status)) {
				GT_1trace(debugMask, GT_7CLASS,
					 "Failed to get WMD DSP resources"
					 " from registry: 0x%x ", status);
			}
		} else {
			GT_1trace(debugMask, GT_7CLASS,
				 "Failed to get WMD Host resources "
				 "from registry: 0x%x ", status);
		}
	if (DSP_SUCCEEDED(status)) {
		/* Given all resources, create a device object. */
		status = DEV_CreateDevice(&hDevObject, szWMDFileName, &hostRes,
					 &dspRes, hDevNode);
		if (DSP_SUCCEEDED(status)) {
			/* Store away the hDevObject with the DEVNODE */
			status = CFG_SetDevObject(hDevNode, (u32)hDevObject);
			if (DSP_FAILED(status)) {
				/* Clean up */
				GT_1trace(debugMask, GT_7CLASS,
					 "Failed to set DevObject in the "
					 "Registry: 0x%x", status);
				DEV_DestroyDevice(hDevObject);
				hDevObject = NULL;
			}
		} else {
			GT_1trace(debugMask, GT_7CLASS,
				 "Failed to Create Device: 0x%x",
				 status);
		}
	}
	if (DSP_SUCCEEDED(status)) {
		/* Create the Manager Object */
		status = MGR_Create(&hMgrObject, hDevNode);
	}
	if (DSP_FAILED(status)) {
		GT_1trace(debugMask, GT_7CLASS, "Failed to MGR object: 0x%x",
			 status);
		status = DSP_EFAIL;
	}
	if (DSP_FAILED(status)) {
		if (hDevObject)
			DEV_DestroyDevice(hDevObject);

		/* Ensure the device extension is NULL */
		CFG_SetDevObject(hDevNode, 0L);
	}
	GT_1trace(debugMask, GT_ENTER, "Exiting DEV_StartDevice status 0x%x\n",
		 status);
	return status;
}

/*
 *  ======== FxnNotImplemented ========
 *  Purpose:
 *      Takes the place of a WMD Null Function.
 *  Parameters:
 *      Multiple, optional.
 *  Returns:
 *      DSP_ENOTIMPL:   Always.
 */
static DSP_STATUS FxnNotImplemented(int arg, ...)
{
	DBG_Trace(DBG_LEVEL1,
		 "WARNING: Calling a non-implemented WMD function.\n");

	return DSP_ENOTIMPL;
}

/*
 *  ======== IsValidHandle ========
 *  Purpose:
 *      Validate the device object handle.
 *  Parameters:
 *      hDevObject:     Handle to device object created with
 *                      DEV_CreateDevice().
 *  Returns:
 *      true if handle is valid; false otherwise.
 *  Requires:
 *  Ensures:
 */
static bool IsValidHandle(struct DEV_OBJECT *hObj)
{
	bool retVal;

	retVal = (hObj != NULL) && (hObj->dwSignature == SIGNATURE);

	return retVal;
}

/*
 *  ======== InitCodMgr ========
 *  Purpose:
 *      Create a COD manager for this device.
 *  Parameters:
 *      pDevObject:             Pointer to device object created with
 *                              DEV_CreateDevice()
 *  Returns:
 *      DSP_SOK:                Success.
 *      DSP_EHANDLE:            Invalid hDevObject.
 *  Requires:
 *      Should only be called once by DEV_CreateDevice() for a given DevObject.
 *  Ensures:
 */
static DSP_STATUS InitCodMgr(struct DEV_OBJECT *pDevObject)
{
	DSP_STATUS status = DSP_SOK;
	char *szDummyFile = "dummy";

	DBC_Require(cRefs > 0);
	DBC_Require(!IsValidHandle(pDevObject) ||
		   (pDevObject->hCodMgr == NULL));
	GT_1trace(debugMask, GT_ENTER, "Entering InitCodMgr pDevObject: 0x%x",
		 pDevObject);
	status = COD_Create(&pDevObject->hCodMgr, szDummyFile, NULL);
	GT_1trace(debugMask, GT_ENTER, "Exiting InitCodMgr status 0x%x\n ",
		 status);
	return status;
}

/*
 *  ======== DEV_InsertProcObject ========
 *  Purpose:
 *      Insert a ProcObject into the list maintained by DEV.
 *  Parameters:
 *      pProcObject:        Ptr to ProcObject to insert.
 *      pDevObject:         Ptr to Dev Object where the list is.
  *     pbAlreadyAttached:  Ptr to return the bool
 *  Returns:
 *      DSP_SOK:           If successful.
 *  Requires:
 *      List Exists
 *      hDevObject is Valid handle
 *      DEV Initialized
 *      pbAlreadyAttached != NULL
 *      hProcObject != 0
 *  Ensures:
 *      DSP_SOK and List is not Empty.
 */
DSP_STATUS DEV_InsertProcObject(struct DEV_OBJECT *hDevObject,
				     u32 hProcObject,
				     OUT bool *pbAlreadyAttached)
{
	DSP_STATUS status = DSP_SOK;
	struct DEV_OBJECT *pDevObject = (struct DEV_OBJECT *)hDevObject;

	GT_2trace(debugMask, GT_ENTER,
		 "Entering DEV_InsetProcObject pProcObject 0x%x"
		 "pDevObject 0x%x\n", hProcObject, hDevObject);
	DBC_Require(cRefs > 0);
	DBC_Require(IsValidHandle(pDevObject));
	DBC_Require(hProcObject != 0);
	DBC_Require(pDevObject->procList != NULL);
	DBC_Require(pbAlreadyAttached != NULL);
	if (!LST_IsEmpty(pDevObject->procList))
		*pbAlreadyAttached = true;

	/* Add DevObject to tail. */
	LST_PutTail(pDevObject->procList, (struct LST_ELEM *)hProcObject);

	GT_1trace(debugMask, GT_ENTER,
		 "Exiting DEV_InsetProcObject status 0x%x\n", status);
	DBC_Ensure(DSP_SUCCEEDED(status) && !LST_IsEmpty(pDevObject->procList));

	return status;
}

/*
 *  ======== DEV_RemoveProcObject ========
 *  Purpose:
 *      Search for and remove a Proc object from the given list maintained
 *      by the DEV
 *  Parameters:
 *      pProcObject:        Ptr to ProcObject to insert.
 *      pDevObject          Ptr to Dev Object where the list is.
 *  Returns:
 *      DSP_SOK:            If successful.
 *  Requires:
 *      List exists and is not empty
 *      hProcObject != 0
 *      hDevObject is a valid Dev handle.
 *  Ensures:
 *  Details:
 *      List will be deleted when the DEV is destroyed.
 */
DSP_STATUS DEV_RemoveProcObject(struct DEV_OBJECT *hDevObject,
				     u32 hProcObject)
{
	DSP_STATUS status = DSP_EFAIL;
	struct LST_ELEM *pCurElem;
	struct DEV_OBJECT *pDevObject = (struct DEV_OBJECT *)hDevObject;

	DBC_Require(IsValidHandle(pDevObject));
	DBC_Require(hProcObject != 0);
	DBC_Require(pDevObject->procList != NULL);
	DBC_Require(!LST_IsEmpty(pDevObject->procList));

	GT_1trace(debugMask, GT_ENTER,
		 "Entering DEV_RemoveProcObject hDevObject "
		 "0x%x\n", hDevObject);
	/* Search list for pDevObject: */
	for (pCurElem = LST_First(pDevObject->procList); pCurElem != NULL;
	    pCurElem = LST_Next(pDevObject->procList, pCurElem)) {
		/* If found, remove it. */
		if ((u32)pCurElem == hProcObject) {
			LST_RemoveElem(pDevObject->procList, pCurElem);
			status = DSP_SOK;
			break;
		}
	}
	GT_1trace(debugMask, GT_ENTER, "DEV_RemoveProcObject returning 0x%x\n",
		 status);
	return status;
}

DSP_STATUS DEV_GetDevType(struct DEV_OBJECT *hdevObject, u32 *devType)
{
	DSP_STATUS status = DSP_SOK;
	struct DEV_OBJECT *pDevObject = (struct DEV_OBJECT *)hdevObject;

	*devType = pDevObject->devType;

	return status;
}

/*
 *  ======== StoreInterfaceFxns ========
 *  Purpose:
 *      Copy the WMD's interface functions into the device object,
 *      ensuring that FxnNotImplemented() is set for:
 *
 *      1. All WMD function pointers which are NULL; and
 *      2. All function slots in the struct DEV_OBJECT structure which have no
 *         corresponding slots in the the WMD's interface, because the WMD
 *         is of an *older* version.
 *  Parameters:
 *      pIntfFxns:      Interface Fxn Structure of the WCD's Dev Object.
 *      pDrvFxns:       Interface Fxns offered by the WMD during DEV_Create().
 *  Returns:
 *  Requires:
 *      Input pointers are valid.
 *      WMD is *not* written for a newer WCD.
 *  Ensures:
 *      All function pointers in the dev object's Fxn interface are not NULL.
 */
static void StoreInterfaceFxns(struct WMD_DRV_INTERFACE *pDrvFxns,
			      OUT struct WMD_DRV_INTERFACE *pIntfFxns)
{
	u32 dwWMDVersion;

	/* Local helper macro: */
#define  StoreFxn(cast, pfn) \
    (pIntfFxns->pfn = ((pDrvFxns->pfn != NULL) ? pDrvFxns->pfn : \
    (cast)FxnNotImplemented))

	DBC_Require(pIntfFxns != NULL);
	DBC_Require(pDrvFxns != NULL);
	DBC_Require(MAKEVERSION(pDrvFxns->dwWCDMajorVersion,
		   pDrvFxns->dwWCDMinorVersion) <= WCDVERSION);
	dwWMDVersion = MAKEVERSION(pDrvFxns->dwWCDMajorVersion,
				  pDrvFxns->dwWCDMinorVersion);
	pIntfFxns->dwWCDMajorVersion = pDrvFxns->dwWCDMajorVersion;
	pIntfFxns->dwWCDMinorVersion = pDrvFxns->dwWCDMinorVersion;
	/* Install functions up to WCD version .80 (first alpha): */
	if (dwWMDVersion > 0) {
		StoreFxn(WMD_DEV_CREATE, pfnDevCreate);
		StoreFxn(WMD_DEV_DESTROY, pfnDevDestroy);
		StoreFxn(WMD_DEV_CTRL, pfnDevCntrl);
		StoreFxn(WMD_BRD_MONITOR, pfnBrdMonitor);
		StoreFxn(WMD_BRD_START, pfnBrdStart);
		StoreFxn(WMD_BRD_STOP, pfnBrdStop);
		StoreFxn(WMD_BRD_STATUS, pfnBrdStatus);
		StoreFxn(WMD_BRD_READ, pfnBrdRead);
		StoreFxn(WMD_BRD_WRITE, pfnBrdWrite);
		StoreFxn(WMD_BRD_SETSTATE, pfnBrdSetState);
		StoreFxn(WMD_BRD_MEMCOPY, pfnBrdMemCopy);
		StoreFxn(WMD_BRD_MEMWRITE, pfnBrdMemWrite);
		StoreFxn(WMD_BRD_MEMMAP, pfnBrdMemMap);
		StoreFxn(WMD_BRD_MEMUNMAP, pfnBrdMemUnMap);
		StoreFxn(WMD_CHNL_CREATE, pfnChnlCreate);
		StoreFxn(WMD_CHNL_DESTROY, pfnChnlDestroy);
		StoreFxn(WMD_CHNL_OPEN, pfnChnlOpen);
		StoreFxn(WMD_CHNL_CLOSE, pfnChnlClose);
		StoreFxn(WMD_CHNL_ADDIOREQ, pfnChnlAddIOReq);
		StoreFxn(WMD_CHNL_GETIOC, pfnChnlGetIOC);
		StoreFxn(WMD_CHNL_CANCELIO, pfnChnlCancelIO);
		StoreFxn(WMD_CHNL_FLUSHIO, pfnChnlFlushIO);
		StoreFxn(WMD_CHNL_GETINFO, pfnChnlGetInfo);
		StoreFxn(WMD_CHNL_GETMGRINFO, pfnChnlGetMgrInfo);
		StoreFxn(WMD_CHNL_IDLE, pfnChnlIdle);
		StoreFxn(WMD_CHNL_REGISTERNOTIFY, pfnChnlRegisterNotify);
		StoreFxn(WMD_DEH_CREATE, pfnDehCreate);
		StoreFxn(WMD_DEH_DESTROY, pfnDehDestroy);
		StoreFxn(WMD_DEH_NOTIFY, pfnDehNotify);
		StoreFxn(WMD_DEH_REGISTERNOTIFY, pfnDehRegisterNotify);
		StoreFxn(WMD_DEH_GETINFO, pfnDehGetInfo);
		StoreFxn(WMD_IO_CREATE, pfnIOCreate);
		StoreFxn(WMD_IO_DESTROY, pfnIODestroy);
		StoreFxn(WMD_IO_ONLOADED, pfnIOOnLoaded);
		StoreFxn(WMD_IO_GETPROCLOAD, pfnIOGetProcLoad);
		StoreFxn(WMD_MSG_CREATE, pfnMsgCreate);
		StoreFxn(WMD_MSG_CREATEQUEUE, pfnMsgCreateQueue);
		StoreFxn(WMD_MSG_DELETE, pfnMsgDelete);
		StoreFxn(WMD_MSG_DELETEQUEUE, pfnMsgDeleteQueue);
		StoreFxn(WMD_MSG_GET, pfnMsgGet);
		StoreFxn(WMD_MSG_PUT, pfnMsgPut);
		StoreFxn(WMD_MSG_REGISTERNOTIFY, pfnMsgRegisterNotify);
		StoreFxn(WMD_MSG_SETQUEUEID, pfnMsgSetQueueId);
	}
	/* Add code for any additional functions in newer WMD versions here: */
	/* Ensure postcondition: */
	DBC_Ensure(pIntfFxns->pfnDevCreate != NULL);
	DBC_Ensure(pIntfFxns->pfnDevDestroy != NULL);
	DBC_Ensure(pIntfFxns->pfnDevCntrl != NULL);
	DBC_Ensure(pIntfFxns->pfnBrdMonitor != NULL);
	DBC_Ensure(pIntfFxns->pfnBrdStart != NULL);
	DBC_Ensure(pIntfFxns->pfnBrdStop != NULL);
	DBC_Ensure(pIntfFxns->pfnBrdStatus != NULL);
	DBC_Ensure(pIntfFxns->pfnBrdRead != NULL);
	DBC_Ensure(pIntfFxns->pfnBrdWrite != NULL);
	DBC_Ensure(pIntfFxns->pfnChnlCreate != NULL);
	DBC_Ensure(pIntfFxns->pfnChnlDestroy != NULL);
	DBC_Ensure(pIntfFxns->pfnChnlOpen != NULL);
	DBC_Ensure(pIntfFxns->pfnChnlClose != NULL);
	DBC_Ensure(pIntfFxns->pfnChnlAddIOReq != NULL);
	DBC_Ensure(pIntfFxns->pfnChnlGetIOC != NULL);
	DBC_Ensure(pIntfFxns->pfnChnlCancelIO != NULL);
	DBC_Ensure(pIntfFxns->pfnChnlFlushIO != NULL);
	DBC_Ensure(pIntfFxns->pfnChnlGetInfo != NULL);
	DBC_Ensure(pIntfFxns->pfnChnlGetMgrInfo != NULL);
	DBC_Ensure(pIntfFxns->pfnChnlIdle != NULL);
	DBC_Ensure(pIntfFxns->pfnChnlRegisterNotify != NULL);
	DBC_Ensure(pIntfFxns->pfnDehCreate != NULL);
	DBC_Ensure(pIntfFxns->pfnDehDestroy != NULL);
	DBC_Ensure(pIntfFxns->pfnDehNotify != NULL);
	DBC_Ensure(pIntfFxns->pfnDehRegisterNotify != NULL);
	DBC_Ensure(pIntfFxns->pfnDehGetInfo != NULL);
	DBC_Ensure(pIntfFxns->pfnIOCreate != NULL);
	DBC_Ensure(pIntfFxns->pfnIODestroy != NULL);
	DBC_Ensure(pIntfFxns->pfnIOOnLoaded != NULL);
	DBC_Ensure(pIntfFxns->pfnIOGetProcLoad != NULL);
	DBC_Ensure(pIntfFxns->pfnMsgSetQueueId != NULL);

#undef  StoreFxn
}

