/*
 * proc.c
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
 *  ======== proc.c ========
 *  Description:
 *      Processor interface at the driver level.
 *
 *  Public Functions:
 *      PROC_Attach
 *      PROC_Ctrl
 *      PROC_Detach
 *      PROC_EnumNodes
 *      PROC_GetResourceInfo
 *      PROC_Exit
 *      PROC_FlushMemory
 *      PROC_GetState
 *      PROC_GetProcessorId
 *      PROC_GetTrace
 *      PROC_Init
 *      PROC_Load
 *      PROC_Map
 *      PROC_NotifyClients
 *      PROC_RegisterNotify
 *      PROC_ReserveMemory
 *      PROC_Start
 *      PROC_UnMap
 *      PROC_UnReserveMemory
 *      PROC_InvalidateMemory

 *! Revision History
 *! ======== ========
 *! 04-Apr-2007 sh  Added PROC_InvalidateMemory API
 *! 19-Apr-2004 sb  Aligned DMM definitions with Symbian
 *!		 Used MEM_FlushCache instead of OS specific API
 *!		 Integrated Alan's code review updates
 *! 08-Mar-2004 sb  Added the Dynamic Memory Mapping feature
 *! 08-Mar-2004 vp  Added g_pszLastCoff member to PROC_OBJECT.
 *!		 This is required for multiprocessor environment.
 *! 09-Feb-2004 vp  Added PROC_GetProcessorID function
 *! 22-Apr-2003 vp  Fixed issue with the string that stores coff file name
 *! 03-Apr-2003 sb  Fix DEH deregistering bug
 *! 26-Mar-2003 vp  Commented the call to DSP deep sleep in PROC_Start function.
 *! 18-Feb-2003 vp  Code review updates.
 *! 18-Oct-2002 vp  Ported to Linux platform.
 *! 22-May-2002 sg  Do IOCTL-to-PWR translation before calling PWR_SleepDSP.
 *! 14-May-2002 sg  Use CSL_Atoi() instead of atoi().
 *! 13-May-2002 sg  Propagate PWR return codes upwards.
 *! 07-May-2002 sg  Added check for, and call to PWR functions in PROC_Ctrl.
 *! 02-May-2002 sg  Added "nap" mode: put DSP to sleep once booted.
 *! 01-Apr-2002 jeh Assume word addresses in PROC_GetTrace().
 *! 29-Nov-2001 jeh Don't call DEH function if hDehMgr == NULL.
 *! 05-Nov-2001 kc: Updated PROC_RegisterNotify and PROC_GetState to support
 *!		 DEH module.
 *! 09-Oct-2001 jeh Fix number of bytes calculated in PROC_GetTrace().
 *! 11-Sep-2001 jeh Delete MSG manager in PROC_Monitor() to fix memory leak.
 *! 29-Aug-2001 rr: DCD_AutoRegister and IOOnLoaded moved before COD_LoadBase
 *!		 to facilitate the external loading.
 *! 14-Aug-2001 ag  DCD_AutoRegister() now called before IOOnLoaded() fxn.
 *! 21-Jun-2001 rr: MSG_Create is done only the first time.
 *! 02-May-2001 jeh Return failure in PROC_Load if IOOnLoaded function returns
 *!		 error other than E_NOTIMPL.
 *! 03-Apr-2001 sg: Changed DSP_DCD_ENOAUTOREGISTER to DSP_EDCDNOAUTOREGISTER.
 *! 13-Feb-2001 kc: DSP/BIOS Bridge name updates.
 *! 05-Jan-2001 rr: PROC_LOAD MSG_Create error is checked.
 *! 15-Dec-2000 rr: IoOnLoaded is checked for WSX_STATUS. We fail to load
 *!		 if DEV_Create2 fails; ie, no non-RMS targets can be
 *!		 loaded.
 *! 12-Dec-2000 rr: PROC_Start's DEV_Create2 is checked for WSX_STATUS.
 *! 28-Nov-2000 jeh Added call to IO OnLoaded function to PROC_Load().
 *! 29-Nov-2000 rr: Incorporated code review changes.
 *! 03-Nov-2000 rr: Auto_Register happens after PROC_Load.
 *! 06-Oct-2000 rr: Updated to ver 0.9. PROC_Start calls DEV_Create2 and
 *!		 WMD_BRD_STOP is always followed by DEV_Destroy2.
 *! 05-Sep-2000 rr: PROC_GetTrace calculates the Trace symbol for 55 in a
 *!		 different way.
 *! 10-Aug-2000 rr: PROC_NotifyClients, PROC_GetProcessorHandle Added
 *! 07-Aug-2000 rr: PROC_IDLE/SYNCINIT/UNKNOWN state removed.
 *!		 WMD fxns are checked for WSX_STATUS.
 *!		 PROC_Attach does not alter the state of the BRD.
 *!		 PROC_Run removed.
 *! 04-Aug-2000 rr: All the functions return DSP_EHANDLE if proc handle is
 *!		 invalid
 *! 27-Jul-2000 rr: PROC_GetTrace and PROC_Load implemented. Updated to
 *!		 ver 0.8 API.
 *! 06-Jul-2000 rr: Created.
 */

/* ------------------------------------ Host OS */
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
#include <dspbridge/ntfy.h>
#include <dspbridge/sync.h>
/*  ----------------------------------- Mini Driver */
#include <dspbridge/wmd.h>
#include <dspbridge/wmddeh.h>
/*  ----------------------------------- Platform Manager */
#include <dspbridge/cod.h>
#include <dspbridge/dev.h>
#include <dspbridge/drv.h>
#include <dspbridge/procpriv.h>
#include <dspbridge/dmm.h>

/*  ----------------------------------- Resource Manager */
#include <dspbridge/mgr.h>
#include <dspbridge/node.h>
#include <dspbridge/nldr.h>
#include <dspbridge/rmm.h>

/*  ----------------------------------- Others */
#include <dspbridge/dbdcd.h>
#include <dspbridge/dbreg.h>
#include <dspbridge/msg.h>
#include <dspbridge/wmdioctl.h>
#include <dspbridge/drv.h>

/*  ----------------------------------- This */
#include <dspbridge/proc.h>
#include <dspbridge/pwr.h>
#include <mach-omap2/omap3-opp.h>

#ifndef RES_CLEANUP_DISABLE
#include <dspbridge/resourcecleanup.h>
#endif
/*  ----------------------------------- Defines, Data Structures, Typedefs */
#define PROC_SIGNATURE	   0x434F5250	/* "PROC" (in reverse). */
#define MAXCMDLINELEN       255
#define PROC_ENVPROCID      "PROC_ID=%d"
#define MAXPROCIDLEN	(8 + 5)
#define PROC_DFLT_TIMEOUT   10000	/* Time out in milliseconds  */
#define PWR_TIMEOUT	 500	/* Sleep/wake timout in msec */
#define EXTEND	      "_EXT_END"	/* Extmem end addr in DSP binary */

#define DSP_CACHE_LINE 128

extern char *iva_img;

/*  ----------------------------------- Globals */
#if GT_TRACE
static struct GT_Mask PROC_DebugMask = { NULL, NULL };	/* WCD MGR Mask */
#endif

static u32 cRefs;

struct SYNC_CSOBJECT *hProcLock;	/* For critical sections */

/*  ----------------------------------- Function Prototypes */
static DSP_STATUS PROC_Monitor(struct PROC_OBJECT *hProcessor);
static s32 GetEnvpCount(char **envp);
static char **PrependEnvp(char **newEnvp, char **envp, s32 cEnvp, s32 cNewEnvp,
			 char *szVar);

/*
 *  ======== PROC_CleanupAllResources =====
 *  Purpose:
 *      Funtion to clean the process resources.
 *      This function is intended to be called when the
 *       processor is in error state
 */
DSP_STATUS PROC_CleanupAllResources(void)
{
	DSP_STATUS dsp_status = DSP_SOK;
	HANDLE hDrvObject = NULL;
	struct PROCESS_CONTEXT *pCtxtclosed = NULL;
	struct PROC_OBJECT *proc_obj_ptr, *temp;

	GT_0trace(PROC_DebugMask, GT_ENTER, "PROC_CleanupAllResources\n");

	dsp_status = CFG_GetObject((u32 *)&hDrvObject, REG_DRV_OBJECT);
	if (DSP_FAILED(dsp_status))
		goto func_end;

	DRV_GetProcCtxtList(&pCtxtclosed, (struct DRV_OBJECT *)hDrvObject);

	while (pCtxtclosed != NULL) {
		if (current->tgid != pCtxtclosed->pid) {
			GT_1trace(PROC_DebugMask, GT_5CLASS,
				 "***Cleanup of "
				 "process***%d\n", pCtxtclosed->pid);
			list_for_each_entry_safe(proc_obj_ptr, temp,
					&pCtxtclosed->processor_list,
					proc_object) {
				PROC_Detach(proc_obj_ptr, pCtxtclosed);
			}
		}
		pCtxtclosed = pCtxtclosed->next;
	}

	WMD_DEH_ReleaseDummyMem();
func_end:
	return dsp_status;
}

/*
 *  ======== PROC_Attach ========
 *  Purpose:
 *      Prepare for communication with a particular DSP processor, and return
 *      a handle to the processor object.
 */
DSP_STATUS
PROC_Attach(u32 uProcessor, OPTIONAL CONST struct DSP_PROCESSORATTRIN *pAttrIn,
       OUT DSP_HPROCESSOR *phProcessor, struct PROCESS_CONTEXT *pr_ctxt)
{
	DSP_STATUS status = DSP_SOK;
	struct DEV_OBJECT *hDevObject;
	struct PROC_OBJECT *pProcObject = NULL;
	struct MGR_OBJECT *hMgrObject = NULL;
	struct DRV_OBJECT *hDrvObject = NULL;
	u32 devType;

	DBC_Require(cRefs > 0);
	DBC_Require(phProcessor != NULL);

	GT_3trace(PROC_DebugMask, GT_ENTER, "Entered PROC_Attach, args:\n\t"
		 "uProcessor:  0x%x\n\tpAttrIn:  0x%x\n\tphProcessor:"
		 "0x%x\n", uProcessor, pAttrIn, phProcessor);

	/* Get the Driver and Manager Object Handles */
	status = CFG_GetObject((u32 *)&hDrvObject, REG_DRV_OBJECT);
	if (DSP_SUCCEEDED(status)) {
		status = CFG_GetObject((u32 *)&hMgrObject, REG_MGR_OBJECT);
		if (DSP_FAILED(status)) {
			/* don't propogate CFG errors from this PROC function */
			GT_1trace(PROC_DebugMask, GT_7CLASS,
				 "PROC_Attach: DSP_FAILED to get"
				 "the Manager Object.\n", status);
		}
	} else {
		/* don't propogate CFG errors from this PROC function */
		GT_1trace(PROC_DebugMask, GT_7CLASS,
			 "PROC_Attach: failed to get the"
			 " DriverObject, 0x%x!\n", status);
	}
	if (DSP_SUCCEEDED(status)) {
		/* Get the Device Object */
		status = DRV_GetDevObject(uProcessor, hDrvObject, &hDevObject);
		if (DSP_FAILED(status)) {
			GT_1trace(PROC_DebugMask, GT_7CLASS,
				 "PROC_Attach: failed to get"
				 " DevObject, 0x%x!\n", status);
		}
	}
	if (DSP_SUCCEEDED(status)) {
		status = DEV_GetDevType(hDevObject, &devType);
		if (DSP_FAILED(status)) {
			GT_1trace(PROC_DebugMask, GT_7CLASS,
				 "PROC_Attach: failed to get"
				 " DevType, 0x%x!\n", status);
		}
	}
	if (DSP_FAILED(status))
		goto func_end;

	/* If we made it this far, create the Proceesor object: */
	MEM_AllocObject(pProcObject, struct PROC_OBJECT, PROC_SIGNATURE);
	/* Fill out the Processor Object: */
	if (pProcObject == NULL) {
		GT_0trace(PROC_DebugMask, GT_7CLASS,
			 "PROC_Attach:Out of memeory \n");
		status = DSP_EFAIL;
		goto func_end;
	}
	pProcObject->hDevObject = hDevObject;
	pProcObject->hMgrObject = hMgrObject;
	pProcObject->uProcessor = devType;
	/* Store TGID of Caller Process */
	pProcObject->hProcess = current->tgid;

	INIT_LIST_HEAD(&pProcObject->proc_object);

	if (pAttrIn)
		pProcObject->uTimeout = pAttrIn->uTimeout;
	else
		pProcObject->uTimeout = PROC_DFLT_TIMEOUT;

	status = DEV_GetIntfFxns(hDevObject, &pProcObject->pIntfFxns);
	if (DSP_SUCCEEDED(status)) {
		status = DEV_GetWMDContext(hDevObject,
					 &pProcObject->hWmdContext);
		if (DSP_FAILED(status)) {
			GT_1trace(PROC_DebugMask, GT_7CLASS,
				 "PROC_Attach Could not"
				 " get the WMD Context.\n", status);
			MEM_FreeObject(pProcObject);
		}
	} else {
		GT_1trace(PROC_DebugMask, GT_7CLASS,
			 "PROC_Attach Could not get"
			 " the DEV_ Interface fxns.\n", status);
		MEM_FreeObject(pProcObject);
	}
	if (DSP_FAILED(status))
		goto func_end;

	/* Create the Notification Object */
	/* This is created with no event mask, no notify mask
	 * and no valid handle to the notification. They all get
	 * filled up when PROC_RegisterNotify is called */
	status = NTFY_Create(&pProcObject->hNtfy);
	if (DSP_SUCCEEDED(status)) {
		/* Insert the Processor Object into the DEV List.
		 * Return handle to this Processor Object:
		 * Find out if the Device is already attached to a
		 * Processor. If so, return AlreadyAttached status */
		LST_InitElem(&pProcObject->link);
		status = DEV_InsertProcObject(pProcObject->hDevObject,
					     (u32)pProcObject,
					     &pProcObject->bIsAlreadyAttached);
		if (DSP_SUCCEEDED(status)) {
			if (pProcObject->bIsAlreadyAttached) {
				status = DSP_SALREADYATTACHED;
				GT_0trace(PROC_DebugMask, GT_1CLASS,
					 "PROC_Attach: Processor "
					 "Already Attached!\n");
			}
		} else {
			if (pProcObject->hNtfy)
				NTFY_Delete(pProcObject->hNtfy);

			MEM_FreeObject(pProcObject);
			GT_1trace(PROC_DebugMask, GT_7CLASS,
				 "PROC_Attach: failed to insert "
				 "Proc Object into DEV, 0x%x!\n", status);
		}
		if (DSP_SUCCEEDED(status)) {
			*phProcessor = (DSP_HPROCESSOR)pProcObject;
			(void)PROC_NotifyClients(pProcObject,
						 DSP_PROCESSORATTACH);
			GT_0trace(PROC_DebugMask, GT_1CLASS,
				 "PROC_Attach: Processor "
				 "Attach Success!\n");
		}
	} else {
		/* Don't leak memory if DSP_FAILED */
		GT_0trace(PROC_DebugMask, GT_7CLASS,
			 "PROC_Attach: Could not allocate "
			 "storage for notification \n");
		MEM_FreeObject(pProcObject);
	}
#ifndef RES_CLEANUP_DISABLE
	spin_lock(&pr_ctxt->proc_list_lock);
	list_add(&pProcObject->proc_object, &pr_ctxt->processor_list);
	spin_unlock(&pr_ctxt->proc_list_lock);
#endif
func_end:
	DBC_Ensure((status == DSP_EFAIL && *phProcessor == NULL) ||
		  (DSP_SUCCEEDED(status) &&
		  MEM_IsValidHandle(pProcObject, PROC_SIGNATURE)) ||
		  (status == DSP_SALREADYATTACHED &&
		  MEM_IsValidHandle(pProcObject, PROC_SIGNATURE)));
	GT_2trace(PROC_DebugMask, GT_ENTER, "Exiting PROC_Attach, results:\n\t"
		 "status: 0x%x\n\thProcessor: 0x%x\n", status, *phProcessor);

	return status;
}

static DSP_STATUS GetExecFile(struct CFG_DEVNODE *hDevNode,
			     struct DEV_OBJECT *hDevObject,
			     u32 size, char *execFile)
{
	s32 devType;
	s32 len;

	DEV_GetDevType(hDevObject, (u32 *) &devType);
	if (devType == DSP_UNIT) {
		return CFG_GetExecFile(hDevNode, size, execFile);
	} else if (devType == IVA_UNIT) {
		if (iva_img) {
                       len = strlen(iva_img);
                       strncpy(execFile, iva_img, len + 1);
			return DSP_SOK;
		}
	}
	return DSP_EFILE;
}

/*
 *  ======== PROC_AutoStart ======== =
 *  Purpose:
 *      A Particular device gets loaded with the default image
 *      if the AutoStart flag is set.
 *  Parameters:
 *      hDevObject:     Handle to the Device
 *  Returns:
 *      DSP_SOK:   On Successful Loading
 *      DSP_EFAIL  General Failure
 *  Requires:
 *      hDevObject != NULL
 *  Ensures:
 */
DSP_STATUS PROC_AutoStart(struct CFG_DEVNODE *hDevNode,
			 struct DEV_OBJECT *hDevObject)
{
	DSP_STATUS status = DSP_EFAIL;
	u32 dwAutoStart = 0;	/* autostart flag */
	struct PROC_OBJECT *pProcObject;
	struct PROC_OBJECT *hProcObject;
	char szExecFile[MAXCMDLINELEN];
	char *argv[2];
	struct MGR_OBJECT *hMgrObject = NULL;
	s32 devType;

	DBC_Require(cRefs > 0);
	DBC_Require(hDevNode != NULL);
	DBC_Require(hDevObject != NULL);

	GT_2trace(PROC_DebugMask, GT_ENTER,
		 "Entered PROC_AutoStart, args:\n\t"
		 "hDevNode: 0x%x\thDevObject: 0x%x\n", hDevNode, hDevObject);
	/* Create a Dummy PROC Object */
	if (DSP_FAILED(CFG_GetObject((u32 *)&hMgrObject,
	   REG_MGR_OBJECT))) {
		GT_0trace(PROC_DebugMask, GT_7CLASS,
			 "PROC_AutoStart: DSP_FAILED to "
			 "Get MGR Object\n");
		goto func_end;
	}
	MEM_AllocObject(pProcObject, struct PROC_OBJECT, PROC_SIGNATURE);
	if (pProcObject == NULL) {
		GT_0trace(PROC_DebugMask, GT_7CLASS,
			 "PROC_AutoStart: DSP_FAILED "
			 "to Create a dummy Processor\n");
		goto func_end;
	}
	GT_0trace(PROC_DebugMask, GT_1CLASS, "NTFY Created \n");
	pProcObject->hDevObject = hDevObject;
	pProcObject->hMgrObject = hMgrObject;
	hProcObject = pProcObject;
	if (DSP_SUCCEEDED(DEV_GetIntfFxns(hDevObject,
	   &pProcObject->pIntfFxns))) {
		if (DSP_SUCCEEDED(DEV_GetWMDContext(hDevObject,
				 &pProcObject->hWmdContext))) {
			status = DSP_SOK;
		} else {
			MEM_FreeObject(hProcObject);
			GT_0trace(PROC_DebugMask, GT_7CLASS,
				 "PROC_AutoStart: Failed "
				 "to get WMD Context \n");
		}
	} else {
		MEM_FreeObject(hProcObject);
		GT_0trace(PROC_DebugMask, GT_7CLASS,
			 "PROC_AutoStart: Failed to "
			 "get IntFxns \n");
	}
	if (DSP_FAILED(status))
		goto func_end;

	/* Stop the Device, put it into standby mode */
	status = PROC_Stop(hProcObject);
	if (DSP_FAILED(CFG_GetAutoStart(hDevNode, &dwAutoStart)) ||
			   !dwAutoStart) {
		status = DSP_EFAIL;
		/* DSP_FAILED to Get s32 Fxn or Wmd Context */
		GT_0trace(PROC_DebugMask, GT_1CLASS, "PROC_AutoStart: "
			 "CFG_GetAutoStart DSP_FAILED \n");
		goto func_cont;
	}
	/* Get the default executable for this board... */
	DEV_GetDevType(hDevObject, (u32 *)&devType);
	pProcObject->uProcessor = devType;
	if (DSP_SUCCEEDED(GetExecFile(hDevNode, hDevObject,
			 sizeof(szExecFile), szExecFile))) {
		argv[0] = szExecFile;
		argv[1] = NULL;
		/* ...and try to load it: */
		status = PROC_Load(hProcObject, 1, (CONST char **)argv, NULL);
		if (DSP_SUCCEEDED(status)) {
			status = PROC_Start(hProcObject);
			if (DSP_SUCCEEDED(status)) {
				GT_0trace(PROC_DebugMask, GT_1CLASS,
					  "PROC_AutoStart: Processor started "
					  "running\n");
			} else {
				GT_0trace(PROC_DebugMask, GT_7CLASS,
					  "PROC_AutoStart: DSP_FAILED To "
					  "Start \n");
			}
		} else {
			GT_0trace(PROC_DebugMask, GT_7CLASS,
				  "PROC_AutoStart: DSP_FAILED to Load\n");
		}
	} else {
		status = DSP_EFILE;
		GT_0trace(PROC_DebugMask, GT_7CLASS, "PROC_AutoStart: "
			 "No Exec file found \n");
	}
func_cont:
	MEM_FreeObject(hProcObject);
func_end:
	GT_1trace(PROC_DebugMask, GT_ENTER,
		 "Exiting PROC_AutoStart, status:0x%x\n", status);
	return status;
}

/*
 *  ======== PROC_Ctrl ========
 *  Purpose:
 *      Pass control information to the GPP device driver managing the
 *      DSP processor.
 *
 *      This will be an OEM-only function, and not part of the DSP/BIOS Bridge
 *      application developer's API.
 *      Call the WMD_ICOTL Fxn with the Argument This is a Synchronous
 *      Operation. arg can be null.
 */
DSP_STATUS PROC_Ctrl(DSP_HPROCESSOR hProcessor, u32 dwCmd,
		    IN struct DSP_CBDATA *arg)
{
	DSP_STATUS status = DSP_SOK;
	struct PROC_OBJECT *pProcObject = hProcessor;
	u32 timeout = 0;

	DBC_Require(cRefs > 0);
	GT_3trace(PROC_DebugMask, GT_ENTER,
		 "Entered PROC_Ctrl, args:\n\thProcessor:"
		 " 0x%x\n\tdwCmd: 0x%x\n\targ: 0x%x\n", hProcessor, dwCmd, arg);

	if (MEM_IsValidHandle(pProcObject, PROC_SIGNATURE)) {
		/* intercept PWR deep sleep command */
		if (dwCmd == WMDIOCTL_DEEPSLEEP) {
			timeout = arg->cbData;
			status = PWR_SleepDSP(PWR_DEEPSLEEP, timeout);
		}
		/* intercept PWR emergency sleep command */
		else if (dwCmd == WMDIOCTL_EMERGENCYSLEEP) {
			timeout = arg->cbData;
			status = PWR_SleepDSP(PWR_EMERGENCYDEEPSLEEP, timeout);
		} else if (dwCmd == PWR_DEEPSLEEP) {
			/* timeout = arg->cbData; */
			status = PWR_SleepDSP(PWR_DEEPSLEEP, timeout);
		}
		/* intercept PWR wake commands */
		else if (dwCmd == WMDIOCTL_WAKEUP) {
			timeout = arg->cbData;
			status = PWR_WakeDSP(timeout);
		} else if (dwCmd == PWR_WAKEUP) {
			/* timeout = arg->cbData; */
			status = PWR_WakeDSP(timeout);
		} else
		    if (DSP_SUCCEEDED
			((*pProcObject->pIntfFxns->pfnDevCntrl)
				(pProcObject->hWmdContext, dwCmd, arg))) {
			status = DSP_SOK;
		} else {
			status = DSP_EFAIL;
			GT_0trace(PROC_DebugMask, GT_7CLASS,
				 "PROC_Ctrl: Failed \n");
		}
	} else {
		status = DSP_EHANDLE;
		GT_0trace(PROC_DebugMask, GT_7CLASS,
			 "PROC_Ctrl: InValid Processor Handle \n");
	}
	GT_1trace(PROC_DebugMask, GT_ENTER, "Exiting PROC_Ctrl, 0x%x\n",
		 status);
	return status;
}

/*
 *  ======== PROC_Detach ========
 *  Purpose:
 *      Destroys the  Processor Object. Removes the notification from the Dev
 *      List.
 */
DSP_STATUS PROC_Detach(DSP_HPROCESSOR hProcessor,
		struct PROCESS_CONTEXT *pr_ctxt)
{
	DSP_STATUS status = DSP_SOK;
	struct PROC_OBJECT *pProcObject = (struct PROC_OBJECT *)hProcessor;
	DBC_Require(cRefs > 0);
	GT_1trace(PROC_DebugMask, GT_ENTER, "Entered PROC_Detach, args:\n\t"
		 "hProcessor:  0x%x\n", hProcessor);

	if (MEM_IsValidHandle(pProcObject, PROC_SIGNATURE)) {
#ifndef RES_CLEANUP_DISABLE
		if (pr_ctxt) {
			spin_lock(&pr_ctxt->proc_list_lock);
			list_del(&pProcObject->proc_object);
			spin_unlock(&pr_ctxt->proc_list_lock);
		}
#endif
		/* Notify the Client */
		NTFY_Notify(pProcObject->hNtfy, DSP_PROCESSORDETACH);
		/* Remove the notification memory */
		if (pProcObject->hNtfy)
			NTFY_Delete(pProcObject->hNtfy);

		if (pProcObject->g_pszLastCoff) {
			MEM_Free(pProcObject->g_pszLastCoff);
			pProcObject->g_pszLastCoff = NULL;
		}
		/* Remove the Proc from the DEV List */
		(void)DEV_RemoveProcObject(pProcObject->hDevObject,
			(u32)pProcObject);
		/* Free the Processor Object */
		MEM_FreeObject(pProcObject);
	} else {
		status = DSP_EHANDLE;
		GT_0trace(PROC_DebugMask, GT_7CLASS,
			 "PROC_Detach: InValid Processor Handle \n");
	}
	GT_1trace(PROC_DebugMask, GT_ENTER, "Exiting PROC_Detach, 0x%x\n",
		 status);
	return status;
}

/*
 *  ======== PROC_EnumNodes ========
 *  Purpose:
 *      Enumerate and get configuration information about nodes allocated
 *      on a DSP processor.
 */
DSP_STATUS PROC_EnumNodes(DSP_HPROCESSOR hProcessor, OUT DSP_HNODE *aNodeTab,
               IN u32 uNodeTabSize, OUT u32 *puNumNodes,
               OUT u32 *puAllocated)
{
	DSP_STATUS status = DSP_EFAIL;
	struct PROC_OBJECT *pProcObject = (struct PROC_OBJECT *)hProcessor;
	struct NODE_MGR *hNodeMgr = NULL;

	DBC_Require(cRefs > 0);
	DBC_Require(aNodeTab != NULL || uNodeTabSize == 0);
	DBC_Require(puNumNodes != NULL);
	DBC_Require(puAllocated != NULL);

	GT_5trace(PROC_DebugMask, GT_ENTER, "Entered PROC_EnumNodes, args:\n\t"
			"hProcessor:  0x%x\n\taNodeTab:  0x%x\n\tuNodeTabSize: "
			" 0x%x\n\t puNumNodes 0x%x\n\t puAllocated: 0x%x\n",
			hProcessor, aNodeTab, uNodeTabSize, puNumNodes,
			puAllocated);
	if (MEM_IsValidHandle(pProcObject, PROC_SIGNATURE)) {
		if (DSP_SUCCEEDED(DEV_GetNodeManager(pProcObject->hDevObject,
				 &hNodeMgr))) {
			if (hNodeMgr) {
				status = NODE_EnumNodes(hNodeMgr, aNodeTab,
							uNodeTabSize,
							puNumNodes,
							puAllocated);
			}
		}
	} else {
		status = DSP_EHANDLE;
		GT_0trace(PROC_DebugMask, GT_7CLASS, "PROC_EnumNodes: "
			 "InValid Processor Handle \n");
	}
	GT_6trace(PROC_DebugMask, GT_ENTER, "Exit PROC_EnumNodes, args:\n\t"
			"hProcessor:  0x%x\n\taNodeTab:  0x%x\n\tuNodeTabSize: "
			" 0x%x\n\t puNumNodes 0x%x\n\t puAllocated: 0x%x\n\t "
			"status: 0x%x \n", hProcessor, aNodeTab, uNodeTabSize,
			puNumNodes, puAllocated, status);

	return status;
}

/* Cache operation against kernel address instead of users */
static int memory_sync_page(struct vm_area_struct *vma, unsigned long start,
			    ssize_t len, enum DSP_FLUSHTYPE ftype)
{
	struct page *page;
	void *kaddr;
	unsigned long offset;
	ssize_t rest;

#ifdef CHECK_DSP_CACHE_LINE
	if ((start & DSP_CACHE_LINE) || (len & DSP_CACHE_LINE))
		pr_warning("%s: not aligned: %08lx(%d)\n", __func__,
			   start, len);
#endif
	while (len) {
		page = follow_page(vma, start, FOLL_GET);
		if (!page) {
			pr_err("%s: no page for %08lx\n", __func__, start);
			return -EINVAL;
		} else if (IS_ERR(page)) {
			pr_err("%s: err page for %08lx(%lu)\n", __func__, start,
			       IS_ERR(page));
			return IS_ERR(page);
		}

		offset = start & ~PAGE_MASK;
		kaddr = page_address(page) + offset;
		rest = min_t(ssize_t, PAGE_SIZE - offset, len);

		MEM_FlushCache(kaddr, rest, ftype);

		put_page(page);
		len -= rest;
		start += rest;
	}

	return 0;
}

/* Check if the given area blongs to process virtul memory address space */
static int memory_sync_vma(unsigned long start, u32 len,
			   enum DSP_FLUSHTYPE ftype)
{
	int err = 0;
	unsigned long end;
	struct vm_area_struct *vma;

	end = start + len;
	if (end <= start)
		return -EINVAL;

	while ((vma = find_vma(current->mm, start)) != NULL) {
		ssize_t size;

		if (vma->vm_flags & (VM_IO | VM_PFNMAP))
			return -EINVAL;

		if (vma->vm_start > start)
			return -EINVAL;

		size = min_t(ssize_t, vma->vm_end - start, len);
		err = memory_sync_page(vma, start, size, ftype);
		if (err)
			break;

		if (end <= vma->vm_end)
			break;

		start = vma->vm_end;
	}

	if (!vma)
		err = -EINVAL;

	return err;
}

static DSP_STATUS proc_memory_sync(DSP_HPROCESSOR hProcessor, void *pMpuAddr,
				   u32 ulSize, u32 ulFlags,
				   enum DSP_FLUSHTYPE FlushMemType)
{
	/* Keep STATUS here for future additions to this function */
	DSP_STATUS status = DSP_SOK;
	struct PROC_OBJECT *pProcObject = (struct PROC_OBJECT *)hProcessor;

	DBC_Require(cRefs > 0);
	GT_5trace(PROC_DebugMask, GT_ENTER,
		  "Entered %s, args:\n\t"
		  "hProcessor: 0x%x pMpuAddr: 0x%x ulSize 0x%x, ulFlags 0x%x\n",
		  __func__, hProcessor, pMpuAddr, ulSize, ulFlags);

	if (!MEM_IsValidHandle(pProcObject, PROC_SIGNATURE)) {
		GT_1trace(PROC_DebugMask, GT_7CLASS,
			  "%s: InValid Processor Handle\n", __func__);
		status = DSP_EHANDLE;
		goto err_out;
	}

	down_read(&current->mm->mmap_sem);

	if (memory_sync_vma((u32)pMpuAddr, ulSize, FlushMemType)) {
		pr_err("%s: InValid address parameters %p %x\n",
		       __func__, pMpuAddr, ulSize);
		status = DSP_EHANDLE;
	}

	up_read(&current->mm->mmap_sem);
err_out:
	GT_2trace(PROC_DebugMask, GT_ENTER,
		  "Leaving %s [0x%x]", __func__, status);

	return status;
}

/*
 *  ======== PROC_FlushMemory ========
 *  Purpose:
 *     Flush cache
 */
DSP_STATUS PROC_FlushMemory(DSP_HPROCESSOR hProcessor, void *pMpuAddr,
			    u32 ulSize, u32 ulFlags)
{
	enum DSP_FLUSHTYPE mtype = PROC_WRITEBACK_INVALIDATE_MEM;

	if (ulFlags & 1)
		mtype = PROC_WRITEBACK_MEM;

	return proc_memory_sync(hProcessor, pMpuAddr, ulSize, ulFlags, mtype);
}

/*
 *  ======== PROC_InvalidateMemory ========
 *  Purpose:
 *     Invalidates the memory specified
 */
DSP_STATUS PROC_InvalidateMemory(DSP_HPROCESSOR hProcessor, void *pMpuAddr,
				 u32 ulSize)
{
	enum DSP_FLUSHTYPE mtype = PROC_INVALIDATE_MEM;

	return proc_memory_sync(hProcessor, pMpuAddr, ulSize, 0, mtype);
}

/*
 *  ======== PROC_GetResourceInfo ========
 *  Purpose:
 *      Enumerate the resources currently available on a processor.
 */
DSP_STATUS PROC_GetResourceInfo(DSP_HPROCESSOR hProcessor, u32 uResourceType,
				OUT struct DSP_RESOURCEINFO *pResourceInfo,
				u32 uResourceInfoSize)
{
	DSP_STATUS status = DSP_EFAIL;
	struct PROC_OBJECT *pProcObject = (struct PROC_OBJECT *)hProcessor;
	struct NODE_MGR *hNodeMgr = NULL;
	struct NLDR_OBJECT *hNldr = NULL;
	struct RMM_TargetObj *rmm = NULL;
	struct IO_MGR *hIOMgr = NULL;		/* IO manager handle */

	DBC_Require(cRefs > 0);
	DBC_Require(pResourceInfo != NULL);
	DBC_Require(uResourceInfoSize >= sizeof(struct DSP_RESOURCEINFO));

	GT_4trace(PROC_DebugMask, GT_ENTER, "Entered PROC_GetResourceInfo,\n\t"
		 "hProcessor:  0x%x\n\tuResourceType:  0x%x\n\tpResourceInfo:"
		 " 0x%x\n\t uResourceInfoSize 0x%x\n", hProcessor,
		 uResourceType, pResourceInfo, uResourceInfoSize);
	if (!MEM_IsValidHandle(pProcObject, PROC_SIGNATURE)) {
		status = DSP_EHANDLE;
		GT_0trace(PROC_DebugMask, GT_7CLASS,
			 "PROC_GetResourceInfo: InValid "
			 "Processor Handle \n");
		goto func_end;
	}
	switch (uResourceType) {
	case DSP_RESOURCE_DYNDARAM:
	case DSP_RESOURCE_DYNSARAM:
	case DSP_RESOURCE_DYNEXTERNAL:
	case DSP_RESOURCE_DYNSRAM:
		if (DSP_FAILED(DEV_GetNodeManager(pProcObject->hDevObject,
		   &hNodeMgr)))
			goto func_end;

		if (DSP_SUCCEEDED(NODE_GetNldrObj(hNodeMgr, &hNldr))) {
			if (DSP_SUCCEEDED(NLDR_GetRmmManager(hNldr, &rmm))) {
				DBC_Assert(rmm != NULL);
				status = DSP_EVALUE;
				if (RMM_stat(rmm,
				   (enum DSP_MEMTYPE)uResourceType,
				   (struct DSP_MEMSTAT *)&(pResourceInfo->
				   result.memStat)))
					status = DSP_SOK;
			}
		}
		break;
	case DSP_RESOURCE_PROCLOAD:
		status = DEV_GetIOMgr(pProcObject->hDevObject, &hIOMgr);
		status = pProcObject->pIntfFxns->pfnIOGetProcLoad(hIOMgr,
			 (struct DSP_PROCLOADSTAT *)&(pResourceInfo->
			 result.procLoadStat));
		if (DSP_FAILED(status)) {
			GT_1trace(PROC_DebugMask, GT_7CLASS,
			"Error in procLoadStat function 0x%x\n", status);
		}
		break;
	default:
		status = DSP_EFAIL;
		break;
	}
func_end:
	GT_1trace(PROC_DebugMask, GT_ENTER, "Exiting PROC_GetResourceInfo, "
		 "status 0x%x\n", status);
	return status;
}

/*
 *  ======== PROC_Exit ========
 *  Purpose:
 *      Decrement reference count, and free resources when reference count is
 *      0.
 */
void PROC_Exit(void)
{
	DBC_Require(cRefs > 0);

	if (hProcLock)
		(void)SYNC_DeleteCS(hProcLock);

	cRefs--;

	GT_1trace(PROC_DebugMask, GT_5CLASS,
		 "Entered PROC_Exit, ref count:0x%x\n",	cRefs);
	DBC_Ensure(cRefs >= 0);
}

/*
 *  ======== PROC_GetDevObject ========
 *  Purpose:
 *      Return the Dev Object handle for a given Processor.
 *
 */
DSP_STATUS PROC_GetDevObject(DSP_HPROCESSOR hProcessor,
			     struct DEV_OBJECT **phDevObject)
{
	DSP_STATUS status = DSP_EFAIL;
	struct PROC_OBJECT *pProcObject = (struct PROC_OBJECT *)hProcessor;

	DBC_Require(cRefs > 0);
	DBC_Require(phDevObject != NULL);

	if (MEM_IsValidHandle(pProcObject, PROC_SIGNATURE)) {
		*phDevObject = pProcObject->hDevObject;
		status = DSP_SOK;
	} else {
		*phDevObject = NULL;
		status = DSP_EHANDLE;
	}

	DBC_Ensure((DSP_SUCCEEDED(status) && *phDevObject != NULL) ||
		   (DSP_FAILED(status) && *phDevObject == NULL));

	return status;
}

/*
 *  ======== PROC_GetState ========
 *  Purpose:
 *      Report the state of the specified DSP processor.
 */
DSP_STATUS PROC_GetState(DSP_HPROCESSOR hProcessor,
			OUT struct DSP_PROCESSORSTATE *pProcStatus,
			u32 uStateInfoSize)
{
	DSP_STATUS status = DSP_SOK;
	struct PROC_OBJECT *pProcObject = (struct PROC_OBJECT *)hProcessor;
	BRD_STATUS brdStatus;
	struct DEH_MGR *hDehMgr;

	DBC_Require(cRefs > 0);
	DBC_Require(pProcStatus != NULL);
	DBC_Require(uStateInfoSize >= sizeof(struct DSP_PROCESSORSTATE));

	GT_3trace(PROC_DebugMask, GT_ENTER, "Entering PROC_GetState, args:\n\t"
		 "pProcStatus: 0x%x\n\thProcessor: 0x%x\n\t uStateInfoSize"
		 " 0x%x\n", pProcStatus, hProcessor, uStateInfoSize);
	if (MEM_IsValidHandle(pProcObject, PROC_SIGNATURE)) {
		/* First, retrieve BRD state information */
		if (DSP_SUCCEEDED((*pProcObject->pIntfFxns->pfnBrdStatus)
		   (pProcObject->hWmdContext, &brdStatus))) {
			switch (brdStatus) {
			case BRD_STOPPED:
				pProcStatus->iState = PROC_STOPPED;
				break;
			case BRD_DSP_HIBERNATION:
				/* Fall through */
			case BRD_RUNNING:
				pProcStatus->iState = PROC_RUNNING;
				break;
			case BRD_LOADED:
				pProcStatus->iState = PROC_LOADED;
				break;
			case BRD_ERROR:
				pProcStatus->iState = PROC_ERROR;
				break;
			default:
				pProcStatus->iState = 0xFF;
				status = DSP_EFAIL;
				break;
			}
		} else {
			status = DSP_EFAIL;
			GT_0trace(PROC_DebugMask, GT_7CLASS,
				 "PROC_GetState: General Failure"
				 " to read the PROC Status \n");
		}
		/* Next, retrieve error information, if any */
		status = DEV_GetDehMgr(pProcObject->hDevObject, &hDehMgr);
		if (DSP_SUCCEEDED(status) && hDehMgr) {
			status = (*pProcObject->pIntfFxns->pfnDehGetInfo)
				 (hDehMgr, &(pProcStatus->errInfo));
			if (DSP_FAILED(status)) {
				GT_0trace(PROC_DebugMask, GT_7CLASS,
					 "PROC_GetState: Failed "
					 "retrieve exception info.\n");
			}
		} else {
			status = DSP_EFAIL;
			GT_0trace(PROC_DebugMask, GT_7CLASS,
				 "PROC_GetState: Failed to "
				 "retrieve DEH handle.\n");
		}
	} else {
		status = DSP_EHANDLE;
		GT_0trace(PROC_DebugMask, GT_7CLASS,
			 "PROC_GetState:InValid Processor Handle \n");
	}
	GT_2trace(PROC_DebugMask, GT_ENTER,
		 "Exiting PROC_GetState, results:\n\t"
		 "status:  0x%x\n\tpProcStatus: 0x%x\n", status,
		 pProcStatus->iState);
	return status;
}

/*
 *  ======== PROC_GetTrace ========
 *  Purpose:
 *      Retrieve the current contents of the trace buffer, located on the
 *      Processor.  Predefined symbols for the trace buffer must have been
 *      configured into the DSP executable.
 *  Details:
 *      We support using the symbols SYS_PUTCBEG and SYS_PUTCEND to define a
 *      trace buffer, only.  Treat it as an undocumented feature.
 *      This call is destructive, meaning the processor is placed in the monitor
 *      state as a result of this function.
 */
DSP_STATUS PROC_GetTrace(DSP_HPROCESSOR hProcessor, u8 *pBuf, u32 uMaxSize)
{
	DSP_STATUS status;
	status = DSP_ENOTIMPL;
	return status;
}

/*
 *  ======== PROC_Init ========
 *  Purpose:
 *      Initialize PROC's private state, keeping a reference count on each call
 */
bool PROC_Init(void)
{
	bool fRetval = true;

	DBC_Require(cRefs >= 0);

	if (cRefs == 0) {
		/* Set the Trace mask */
		DBC_Assert(!PROC_DebugMask.flags);
		GT_create(&PROC_DebugMask, "PR");  /* "PR" for Processor */

		(void)SYNC_InitializeCS(&hProcLock);
	}

	if (fRetval)
		cRefs++;

	GT_1trace(PROC_DebugMask, GT_5CLASS,
		 "Entered PROC_Init, ref count:0x%x\n",	cRefs);
	DBC_Ensure((fRetval && (cRefs > 0)) || (!fRetval && (cRefs >= 0)));

	return fRetval;
}

/*
 *  ======== PROC_Load ========
 *  Purpose:
 *      Reset a processor and load a new base program image.
 *      This will be an OEM-only function, and not part of the DSP/BIOS Bridge
 *      application developer's API.
 */
DSP_STATUS PROC_Load(DSP_HPROCESSOR hProcessor, IN CONST s32 iArgc,
		    IN CONST char **aArgv, IN CONST char **aEnvp)
{
	DSP_STATUS status = DSP_SOK;
	struct PROC_OBJECT *pProcObject = (struct PROC_OBJECT *)hProcessor;
	struct IO_MGR *hIOMgr;		/* IO manager handle */
	struct MSG_MGR *hMsgMgr;
	struct COD_MANAGER *hCodMgr;	/* Code manager handle */
	char *pargv0;		/* temp argv[0] ptr */
	char **newEnvp;		/* Updated envp[] array. */
	char szProcID[MAXPROCIDLEN];	/* Size of "PROC_ID=<n>" */
	s32 cEnvp;		/* Num elements in envp[]. */
	s32 cNewEnvp;		/* "  " in newEnvp[]     */
	s32 nProcID = 0;	/* Anticipate MP version. */
	struct DCD_MANAGER *hDCDHandle;
	struct DMM_OBJECT *hDmmMgr;
	u32 dwExtEnd;
	u32 uProcId;
#ifdef DEBUG
	BRD_STATUS uBrdState;
#endif
#ifdef OPT_LOAD_TIME_INSTRUMENTATION
	struct timeval tv1;
	struct timeval tv2;
#endif
	DBC_Require(cRefs > 0);
	DBC_Require(iArgc > 0);
	DBC_Require(aArgv != NULL);
#ifdef OPT_LOAD_TIME_INSTRUMENTATION
	do_gettimeofday(&tv1);
#endif
#if defined(CONFIG_BRIDGE_DVFS) && !defined(CONFIG_CPU_FREQ)
	struct dspbridge_platform_data *pdata =
				omap_dspbridge_dev->dev.platform_data;
#endif
	GT_2trace(PROC_DebugMask, GT_ENTER, "Entered PROC_Load, args:\n\t"
		 "hProcessor:  0x%x\taArgv: 0x%x\n", hProcessor, aArgv[0]);
	/* Call the WMD_BRD_Load Fxn */
	if (!MEM_IsValidHandle(pProcObject, PROC_SIGNATURE)) {
		status = DSP_EHANDLE;
		GT_0trace(PROC_DebugMask, GT_1CLASS,
			 "PROC_Load: Invalid Processor Handle..\n");
		goto func_end;
	}
	if (pProcObject->bIsAlreadyAttached) {
		GT_0trace(PROC_DebugMask, GT_7CLASS,
			 "PROC_Load GPP "
			 "Client is already attached status  \n");
	}
	if (DSP_FAILED(DEV_GetCodMgr(pProcObject->hDevObject, &hCodMgr))) {
		status = DSP_EFAIL;
		GT_1trace(PROC_DebugMask, GT_7CLASS, "PROC_Load: DSP_FAILED in "
			 "DEV_GetCodMgr status 0x%x \n", status);
		goto func_end;
	}
	status = PROC_Stop(hProcessor);
	if (DSP_FAILED(status)) {
		GT_1trace(PROC_DebugMask, GT_7CLASS,
			 "PROC_Load: DSP_FAILED to Place the"
			 " Processor in Stop Mode(PROC_STOP) status 0x%x \n",
			 status);
		goto func_end;
	}
	/* Place the board in the monitor state. */
	status = PROC_Monitor(hProcessor);
	if (DSP_FAILED(status)) {
		GT_1trace(PROC_DebugMask, GT_7CLASS,
			 "PROC_Load: DSP_FAILED to Place the"
			 " Processor in Monitor Mode(PROC_IDLE) status 0x%x\n",
			 status);
		goto func_end;
	}
	/* Save ptr to  original argv[0]. */
	pargv0 = (char *)aArgv[0];
	/*Prepend "PROC_ID=<nProcID>"to envp array for target.*/
	cEnvp = GetEnvpCount((char **)aEnvp);
	cNewEnvp = (cEnvp ? (cEnvp + 1) : (cEnvp + 2));
	newEnvp = MEM_Calloc(cNewEnvp * sizeof(char **), MEM_PAGED);
	if (newEnvp) {
		status = snprintf(szProcID, MAXPROCIDLEN, PROC_ENVPROCID,
				    nProcID);
		if (status == -1) {
			GT_0trace(PROC_DebugMask, GT_7CLASS, "PROC_Load: "
				 "Proc ID string overflow \n");
			status = DSP_EFAIL;
		} else {
			newEnvp = PrependEnvp(newEnvp, (char **)aEnvp, cEnvp,
					     cNewEnvp, szProcID);
			/* Get the DCD Handle */
			status = MGR_GetDCDHandle(pProcObject->hMgrObject,
						 (u32 *)&hDCDHandle);
			if (DSP_SUCCEEDED(status)) {
				/*  Before proceeding with new load,
				 *  check if a previously registered COFF
				 *  exists.
				 *  If yes, unregister nodes in previously
				 *  registered COFF.  If any error occurred,
				 *  set previously registered COFF to NULL.  */
				if (pProcObject->g_pszLastCoff != NULL) {
					status = DCD_AutoUnregister(hDCDHandle,
						 pProcObject->g_pszLastCoff);
					/* Regardless of auto unregister status,
					 *  free previously allocated
					 *  memory.  */
					MEM_Free(pProcObject->g_pszLastCoff);
					pProcObject->g_pszLastCoff = NULL;
				}
			}
			/* On success, do COD_OpenBase() */
			status = COD_OpenBase(hCodMgr, (char *)aArgv[0],
					     COD_SYMB);
			if (DSP_FAILED(status)) {
				GT_1trace(PROC_DebugMask, GT_7CLASS,
					 "PROC_Load: COD_OpenBase "
					 "failed (0x%x)\n", status);
			}
		}
	} else {
		GT_0trace(PROC_DebugMask, GT_7CLASS,
			 " PROC_Load:Out of Memory \n");
		status = DSP_EMEMORY;
	}
	if (DSP_SUCCEEDED(status)) {
		/* Auto-register data base */
		/* Get the DCD Handle */
		status = MGR_GetDCDHandle(pProcObject->hMgrObject,
					 (u32 *)&hDCDHandle);
		if (DSP_SUCCEEDED(status)) {
			/*  Auto register nodes in specified COFF
			 *  file.  If registration did not fail,
			 *  (status = DSP_SOK or DSP_EDCDNOAUTOREGISTER)
			 *  save the name of the COFF file for
			 *  de-registration in the future.  */
			status = DCD_AutoRegister(hDCDHandle, (char *)aArgv[0]);
			if (status == DSP_EDCDNOAUTOREGISTER) {
				GT_0trace(PROC_DebugMask, GT_7CLASS,
					  "PROC_Load: No Auto "
					  "Register section.  Proceeding..\n");
				status = DSP_SOK;
			}
			if (DSP_FAILED(status)) {
				status = DSP_EFAIL;
				GT_0trace(PROC_DebugMask, GT_7CLASS,
					  "PROC_Load: Failed to "
					  "Auto Register..\n");
			} else {
				DBC_Assert(pProcObject->g_pszLastCoff == NULL);
				/* Allocate memory for pszLastCoff */
				pProcObject->g_pszLastCoff = MEM_Calloc(
                                       (strlen((char *)aArgv[0]) + 1),
					MEM_PAGED);
				/* If memory allocated, save COFF file name*/
				if (pProcObject->g_pszLastCoff) {
                                       strncpy(pProcObject->g_pszLastCoff,
						(char *)aArgv[0],
                                       (strlen((char *)aArgv[0]) + 1));
				}
			}
		}
	}
	/* Update shared memory address and size */
	if (DSP_SUCCEEDED(status)) {
		/*  Create the message manager. This must be done
		 *  before calling the IOOnLoaded function.  */
		DEV_GetMsgMgr(pProcObject->hDevObject, &hMsgMgr);
		if (!hMsgMgr) {
			status = MSG_Create(&hMsgMgr, pProcObject->hDevObject,
					   (MSG_ONEXIT)NODE_OnExit);
			DBC_Assert(DSP_SUCCEEDED(status));
			DEV_SetMsgMgr(pProcObject->hDevObject, hMsgMgr);
		}
		if (status == DSP_ENOTIMPL) {
			/* It's OK not to have a message manager */
			status = DSP_SOK;
		}
	}
	if (DSP_SUCCEEDED(status)) {
		/* Set the Device object's message manager */
		status = DEV_GetIOMgr(pProcObject->hDevObject, &hIOMgr);
		DBC_Assert(DSP_SUCCEEDED(status));
		status = (*pProcObject->pIntfFxns->pfnIOOnLoaded)(hIOMgr);
		if (status == DSP_ENOTIMPL) {
			/* Ok not to implement this function */
			status = DSP_SOK;
		} else {
			if (DSP_FAILED(status)) {
				GT_1trace(PROC_DebugMask, GT_7CLASS,
					  "PROC_Load: Failed to get shared "
					  "memory or message buffer address "
					  "from COFF status 0x%x\n", status);
				status = DSP_EFAIL;
			}
		}
	} else {
		status = DSP_EFAIL;
		GT_1trace(PROC_DebugMask, GT_7CLASS,
			  "PROC_Load: DSP_FAILED in "
			  "MSG_Create status 0x%x\n", status);
	}
	if (DSP_SUCCEEDED(status)) {
		/* Now, attempt to load an exec: */

	/* Boost the OPP level to Maximum level supported by baseport*/
#if defined(CONFIG_BRIDGE_DVFS) && !defined(CONFIG_CPU_FREQ)
	if (pdata->cpu_set_freq)
		(*pdata->cpu_set_freq)(pdata->mpu_speed[VDD1_OPP5]);
#endif
		status = COD_LoadBase(hCodMgr, iArgc, (char **)aArgv,
				     DEV_BrdWriteFxn,
				     pProcObject->hDevObject, NULL);
		if (DSP_FAILED(status)) {
			if (status == COD_E_OPENFAILED) {
				GT_0trace(PROC_DebugMask, GT_7CLASS,
					"PROC_Load:Failure to Load the EXE\n");
			}
			if (status == COD_E_SYMBOLNOTFOUND) {
				GT_0trace(PROC_DebugMask, GT_7CLASS,
					"PROC_Load:Could not parse the file\n");
			} else {
				GT_1trace(PROC_DebugMask, GT_7CLASS,
					 "PROC_Load: DSP_FAILED in "
					 "COD_Load  status 0x%x \n", status);
			}
		}
	/* Requesting the lowest opp supported*/
#if defined(CONFIG_BRIDGE_DVFS) && !defined(CONFIG_CPU_FREQ)
	if (pdata->cpu_set_freq)
		(*pdata->cpu_set_freq)(pdata->mpu_speed[VDD1_OPP1]);
#endif

	}
	if (DSP_SUCCEEDED(status)) {
		/* Update the Processor status to loaded */
		status = (*pProcObject->pIntfFxns->pfnBrdSetState)
			 (pProcObject->hWmdContext, BRD_LOADED);
		if (DSP_SUCCEEDED(status)) {
			pProcObject->sState = PROC_LOADED;
			if (pProcObject->hNtfy) {
				PROC_NotifyClients(pProcObject,
						 DSP_PROCESSORSTATECHANGE);
			}
		} else {
			GT_1trace(PROC_DebugMask, GT_7CLASS,
				 "PROC_Load, pfnBrdSetState "
				 "failed: 0x%x\n", status);
			status = DSP_EFAIL;
		}
	}
	if (DSP_SUCCEEDED(status)) {
		status = PROC_GetProcessorId(hProcessor, &uProcId);
		if (uProcId == DSP_UNIT) {
			/* Use all available DSP address space after EXTMEM
			 * for DMM */
			if (DSP_SUCCEEDED(status)) {
				status = COD_GetSymValue(hCodMgr, EXTEND,
								&dwExtEnd);
				if (DSP_FAILED(status)) {
					GT_1trace(PROC_DebugMask, GT_7CLASS,
						 "PROC_Load: Failed on "
						 "COD_GetSymValue %s.\n",
						 EXTEND);
				}
			}
			/* Reset DMM structs and add an initial free chunk*/
			if (DSP_SUCCEEDED(status)) {
				status = DEV_GetDmmMgr(pProcObject->hDevObject,
						      &hDmmMgr);
				if (DSP_SUCCEEDED(status)) {
					/* Set dwExtEnd to DMM START u8
					  * address */
					dwExtEnd = (dwExtEnd + 1) * DSPWORDSIZE;
					 /* DMM memory is from EXT_END */
					status = DMM_CreateTables(hDmmMgr,
						dwExtEnd, DMMPOOLSIZE);
				}
			}
		}
	}
	/* Restore the original argv[0] */
	MEM_Free(newEnvp);
	aArgv[0] = pargv0;
#ifdef DEBUG
	if (DSP_SUCCEEDED(status)) {
		if (DSP_SUCCEEDED((*pProcObject->pIntfFxns->pfnBrdStatus)
		   (pProcObject->hWmdContext, &uBrdState))) {
			GT_0trace(PROC_DebugMask, GT_1CLASS,
				 "PROC_Load: Processor Loaded\n");
			DBC_Assert(uBrdState == BRD_LOADED);
		}
	}
#endif
func_end:
#ifdef DEBUG
	if (DSP_FAILED(status)) {
		GT_0trace(PROC_DebugMask, GT_1CLASS, "PROC_Load: "
			 "Processor Load Failed.\n");

	}
#endif
	GT_1trace(PROC_DebugMask, GT_ENTER,
		 "Exiting PROC_Load, status:  0x%x\n", status);
	DBC_Ensure((DSP_SUCCEEDED(status) && pProcObject->sState == PROC_LOADED)
		   || DSP_FAILED(status));
#ifdef OPT_LOAD_TIME_INSTRUMENTATION
	do_gettimeofday(&tv2);
	if (tv2.tv_usec < tv1.tv_usec) {
		tv2.tv_usec += 1000000;
		tv2.tv_sec--;
	}
	GT_2trace(PROC_DebugMask, GT_1CLASS,
			"Proc_Load: time to load %d sec and %d usec \n",
		    tv2.tv_sec - tv1.tv_sec, tv2.tv_usec - tv1.tv_usec);
#endif
	return status;
}

/*
 *  ======== PROC_Map ========
 *  Purpose:
 *      Maps a MPU buffer to DSP address space.
 */
DSP_STATUS PROC_Map(DSP_HPROCESSOR hProcessor, void *pMpuAddr, u32 ulSize,
		   void *pReqAddr, void **ppMapAddr, u32 ulMapAttr,
		   struct PROCESS_CONTEXT *pr_ctxt)
{
	u32 vaAlign;
	u32 paAlign;
	struct DMM_OBJECT *hDmmMgr;
	u32 sizeAlign;
	DSP_STATUS status = DSP_SOK;
	struct PROC_OBJECT *pProcObject = (struct PROC_OBJECT *)hProcessor;

#ifndef RES_CLEANUP_DISABLE
       HANDLE        dmmRes;
#endif

	GT_6trace(PROC_DebugMask, GT_ENTER, "Entered PROC_Map, args:\n\t"
		 "hProcessor %x, pMpuAddr %x, ulSize %x, pReqAddr %x, "
		 "ulMapAttr %x, ppMapAddr %x\n", hProcessor, pMpuAddr, ulSize,
		 pReqAddr, ulMapAttr, ppMapAddr);
	/* Calculate the page-aligned PA, VA and size */
	vaAlign = PG_ALIGN_LOW((u32) pReqAddr, PG_SIZE_4K);
	paAlign = PG_ALIGN_LOW((u32) pMpuAddr, PG_SIZE_4K);
	sizeAlign = PG_ALIGN_HIGH(ulSize + (u32)pMpuAddr - paAlign,
				 PG_SIZE_4K);

	GT_3trace(PROC_DebugMask, GT_ENTER, "PROC_Map: vaAlign %x, paAlign %x, "
		 "sizeAlign %x\n", vaAlign, paAlign, sizeAlign);

	if (!MEM_IsValidHandle(pProcObject, PROC_SIGNATURE)) {
		status = DSP_EHANDLE;
		GT_0trace(PROC_DebugMask, GT_7CLASS, "PROC_Map: "
			 "InValid Processor Handle \n");
		goto func_end;
	}
	/* Critical section */
	(void)SYNC_EnterCS(hProcLock);
	status = DMM_GetHandle(pProcObject, &hDmmMgr);
	if (DSP_FAILED(status)) {
		GT_1trace(PROC_DebugMask, GT_7CLASS,
			 "PROC_Map: Failed to get DMM Mgr "
			 "handle: 0x%x\n", status);
	} else {
		status = DMM_MapMemory(hDmmMgr, vaAlign, sizeAlign);
	}
	/* Add mapping to the page tables. */
	if (DSP_SUCCEEDED(status)) {

		status = (*pProcObject->pIntfFxns->pfnBrdMemMap)
			(pProcObject->hWmdContext, paAlign, vaAlign, sizeAlign,
			ulMapAttr);
	}
	if (DSP_SUCCEEDED(status)) {
		/* Mapped address = MSB of VA | LSB of PA */
		*ppMapAddr = (void *) (vaAlign | ((u32) pMpuAddr &
			     (PG_SIZE_4K - 1)));
	} else {
		DMM_UnMapMemory(hDmmMgr, vaAlign, &sizeAlign);
	}
	(void)SYNC_LeaveCS(hProcLock);

#ifndef RES_CLEANUP_DISABLE
	if (DSP_SUCCEEDED(status)) {
		spin_lock(&pr_ctxt->dmm_list_lock);

		DRV_InsertDMMResElement(&dmmRes, pr_ctxt);
		DRV_UpdateDMMResElement(dmmRes, (u32)pMpuAddr, ulSize,
				(u32)pReqAddr, (u32)*ppMapAddr, hProcessor);

		spin_unlock(&pr_ctxt->dmm_list_lock);
	}
#endif
func_end:
	GT_1trace(PROC_DebugMask, GT_ENTER, "Leaving PROC_Map [0x%x]", status);
	return status;
}

/*
 *  ======== PROC_RegisterNotify ========
 *  Purpose:
 *      Register to be notified of specific processor events.
 */
DSP_STATUS PROC_RegisterNotify(DSP_HPROCESSOR hProcessor, u32 uEventMask,
			      u32 uNotifyType, struct DSP_NOTIFICATION
			      *hNotification)
{
	DSP_STATUS status = DSP_SOK;
	struct PROC_OBJECT *pProcObject = (struct PROC_OBJECT *)hProcessor;
	struct DEH_MGR *hDehMgr;

	DBC_Require(hNotification != NULL);
	DBC_Require(cRefs > 0);

	GT_4trace(PROC_DebugMask, GT_ENTER,
		 "Entered PROC_RegisterNotify, args:\n\t"
		 "hProcessor:  0x%x\n\tuEventMask:  0x%x\n\tuNotifyMask:"
		 " 0x%x\n\t hNotification 0x%x\n", hProcessor, uEventMask,
		 uNotifyType, hNotification);

	/* Check processor handle */
	if (!MEM_IsValidHandle(pProcObject, PROC_SIGNATURE)) {
		status = DSP_EHANDLE;
		GT_1trace(PROC_DebugMask, GT_7CLASS,
			 "PROC_RegsiterNotify Invalid "
			 "ProcessorHandle 0x%x\n", hProcessor);
		goto func_end;
	}
	/* Check if event mask is a valid processor related event */
	if (uEventMask & ~(DSP_PROCESSORSTATECHANGE | DSP_PROCESSORATTACH |
	   DSP_PROCESSORDETACH | DSP_PROCESSORRESTART | DSP_MMUFAULT |
	   DSP_SYSERROR | DSP_PWRERROR))
		status = DSP_EVALUE;

	/* Check if notify type is valid */
	if (uNotifyType != DSP_SIGNALEVENT)
		status = DSP_EVALUE;

	if (DSP_SUCCEEDED(status)) {
		/* If event mask is not DSP_SYSERROR, DSP_MMUFAULT,
		 * or DSP_PWRERROR then register event immediately. */
		if (uEventMask &
		    ~(DSP_SYSERROR | DSP_MMUFAULT | DSP_PWRERROR)) {
			status = NTFY_Register(pProcObject->hNtfy,
				 hNotification,	uEventMask, uNotifyType);
			/* Special case alert, special case alert!
			 * If we're trying to *deregister* (i.e. uEventMask
			 * is 0), a DSP_SYSERROR or DSP_MMUFAULT notification,
			 * we have to deregister with the DEH manager.
			 * There's no way to know, based on uEventMask which
			 * manager the notification event was registered with,
			 * so if we're trying to deregister and NTFY_Register
			 * failed, we'll give the deh manager a shot.
			 */
			if ((uEventMask == 0) && DSP_FAILED(status)) {
				status = DEV_GetDehMgr(pProcObject->hDevObject,
					 &hDehMgr);
				DBC_Assert(pProcObject->pIntfFxns->
					   pfnDehRegisterNotify);
				status = (*pProcObject->pIntfFxns->
					 pfnDehRegisterNotify)
					 (hDehMgr, uEventMask, uNotifyType,
					 hNotification);
			}
		} else {
			status = DEV_GetDehMgr(pProcObject->hDevObject,
					      &hDehMgr);
			DBC_Assert(pProcObject->pIntfFxns->
				  pfnDehRegisterNotify);
			status = (*pProcObject->pIntfFxns->pfnDehRegisterNotify)
				 (hDehMgr, uEventMask, uNotifyType,
				 hNotification);
			if (DSP_FAILED(status))
				status = DSP_EFAIL;

		}
	}
func_end:
	return status;
}

/*
 *  ======== PROC_ReserveMemory ========
 *  Purpose:
 *      Reserve a virtually contiguous region of DSP address space.
 */
DSP_STATUS PROC_ReserveMemory(DSP_HPROCESSOR hProcessor, u32 ulSize,
			     void **ppRsvAddr)
{
	struct DMM_OBJECT *hDmmMgr;
	DSP_STATUS status = DSP_SOK;
	struct PROC_OBJECT *pProcObject = (struct PROC_OBJECT *)hProcessor;

	GT_3trace(PROC_DebugMask, GT_ENTER,
		 "Entered PROC_ReserveMemory, args:\n\t"
		 "hProcessor: 0x%x ulSize: 0x%x ppRsvAddr: 0x%x\n", hProcessor,
		 ulSize, ppRsvAddr);
	if (!MEM_IsValidHandle(pProcObject, PROC_SIGNATURE)) {
		status = DSP_EHANDLE;
		GT_0trace(PROC_DebugMask, GT_7CLASS, "PROC_Map: "
			 "InValid Processor Handle \n");
		goto func_end;
	}
	status = DMM_GetHandle(pProcObject, &hDmmMgr);
	if (DSP_FAILED(status)) {
		GT_1trace(PROC_DebugMask, GT_7CLASS, "PROC_ReserveMemory: "
			 "Failed to get DMM Mgr handle: 0x%x\n", status);
	} else
		status = DMM_ReserveMemory(hDmmMgr, ulSize, (u32 *)ppRsvAddr);

	GT_1trace(PROC_DebugMask, GT_ENTER, "Leaving PROC_ReserveMemory [0x%x]",
		 status);
func_end:
	return status;
}

/*
 *  ======== PROC_Start ========
 *  Purpose:
 *      Start a processor running.
 */
DSP_STATUS PROC_Start(DSP_HPROCESSOR hProcessor)
{
	DSP_STATUS status = DSP_SOK;
	struct PROC_OBJECT *pProcObject = (struct PROC_OBJECT *)hProcessor;
	struct COD_MANAGER *hCodMgr;	/* Code manager handle    */
	u32 dwDspAddr;	/* Loaded code's entry point.    */
#ifdef DEBUG
	BRD_STATUS uBrdState;
#endif
	DBC_Require(cRefs > 0);
	GT_1trace(PROC_DebugMask, GT_ENTER, "Entered PROC_Start, args:\n\t"
		 "hProcessor:  0x%x\n", hProcessor);
	if (!MEM_IsValidHandle(pProcObject, PROC_SIGNATURE)) {
		status = DSP_EHANDLE;
		GT_0trace(PROC_DebugMask, GT_7CLASS,
			 "PROC_Start :InValid Handle \n");
		goto func_end;
	}
	/* Call the WMD_BRD_Start */
	if (pProcObject->sState != PROC_LOADED) {
		GT_0trace(PROC_DebugMask, GT_7CLASS,
			 "PROC_Start :Wrong state \n");
		status = DSP_EWRONGSTATE;
		goto func_end;
	}
	status = DEV_GetCodMgr(pProcObject->hDevObject, &hCodMgr);
	if (DSP_FAILED(status)) {
		status = DSP_EFAIL;
		GT_1trace(PROC_DebugMask, GT_7CLASS,
			 "Processor Start DSP_FAILED "
			 "in Getting DEV_GetCodMgr status 0x%x\n", status);
		goto func_cont;
	}
	status = COD_GetEntry(hCodMgr, &dwDspAddr);
	if (DSP_FAILED(status)) {
		status = DSP_EFAIL;
		GT_1trace(PROC_DebugMask, GT_7CLASS,
			 "Processor Start  DSP_FAILED in "
			 "Getting COD_GetEntry status 0x%x\n", status);
		goto func_cont;
	}
	status = (*pProcObject->pIntfFxns->pfnBrdStart)
		 (pProcObject->hWmdContext, dwDspAddr);
	if (DSP_FAILED(status)) {
		status = DSP_EFAIL;
		GT_0trace(PROC_DebugMask, GT_7CLASS,
			 "PROC_Start Failed to Start the board\n");
		goto func_cont;
	}
	/* Call DEV_Create2 */
	status = DEV_Create2(pProcObject->hDevObject);
	if (DSP_SUCCEEDED(status)) {
		pProcObject->sState = PROC_RUNNING;
		/* Deep sleep switces off the peripheral clocks.
		 * we just put the DSP CPU in idle in the idle loop.
		 * so there is no need to send a command to DSP */

		if (pProcObject->hNtfy) {
			PROC_NotifyClients(pProcObject,
					  DSP_PROCESSORSTATECHANGE);
		}
		GT_0trace(PROC_DebugMask, GT_1CLASS, "PROC_Start: Processor "
			 "Started and running \n");
	} else {
		/* Failed to Create Node Manager and DISP Object
		 * Stop the Processor from running. Put it in STOPPED State */
		(void)(*pProcObject->pIntfFxns->pfnBrdStop)(pProcObject->
			hWmdContext);
		status = DSP_EFAIL;
		pProcObject->sState = PROC_STOPPED;
		GT_0trace(PROC_DebugMask, GT_7CLASS, "PROC_Start "
			 "Failed to Create the Node Manager\n");
	}
func_cont:
#ifdef DEBUG
	if (DSP_SUCCEEDED(status)) {
		if (DSP_SUCCEEDED((*pProcObject->pIntfFxns->pfnBrdStatus)
		   (pProcObject->hWmdContext, &uBrdState))) {
			GT_0trace(PROC_DebugMask, GT_1CLASS,
				 "PROC_Start: Processor State is RUNNING \n");
			DBC_Assert(uBrdState != BRD_HIBERNATION);
		}
	}
#endif
func_end:
	GT_1trace(PROC_DebugMask, GT_ENTER,
		 "Exiting PROC_Start, status  0x%x\n", status);
	DBC_Ensure((DSP_SUCCEEDED(status) && pProcObject->sState ==
		  PROC_RUNNING)	|| DSP_FAILED(status));
	return status;
}

/*
 *  ======== PROC_Stop ========
 *  Purpose:
 *      Stop a processor running.
 */
DSP_STATUS PROC_Stop(DSP_HPROCESSOR hProcessor)
{
	DSP_STATUS status = DSP_SOK;
	struct PROC_OBJECT *pProcObject = (struct PROC_OBJECT *)hProcessor;
	struct MSG_MGR *hMsgMgr;
	struct NODE_MGR *hNodeMgr;
	DSP_HNODE hNode;
	u32 uNodeTabSize = 1;
	u32 uNumNodes = 0;
	u32 uNodesAllocated = 0;
	BRD_STATUS uBrdState;

	DBC_Require(cRefs > 0);
	GT_1trace(PROC_DebugMask, GT_ENTER, "Entered PROC_Stop, args:\n\t"
		 "hProcessor:  0x%x\n", hProcessor);
	if (!MEM_IsValidHandle(pProcObject, PROC_SIGNATURE)) {
		status = DSP_EHANDLE;
		GT_0trace(PROC_DebugMask, GT_7CLASS,
			 "PROC_Stop :InValid Handle \n");
		goto func_end;
	}
	if (DSP_SUCCEEDED((*pProcObject->pIntfFxns->pfnBrdStatus)
	   (pProcObject->hWmdContext, &uBrdState))) {
		/* Clean up all the resources except the current running
		 * process resources */
		if (uBrdState == BRD_ERROR)
			PROC_CleanupAllResources();
	}
	/* check if there are any running nodes */
	status = DEV_GetNodeManager(pProcObject->hDevObject, &hNodeMgr);
	if (DSP_SUCCEEDED(status) && hNodeMgr) {
		status = NODE_EnumNodes(hNodeMgr, &hNode, uNodeTabSize,
					&uNumNodes, &uNodesAllocated);
		if ((status == DSP_ESIZE) || (uNodesAllocated > 0)) {
			GT_1trace(PROC_DebugMask, GT_7CLASS,
				 "Can't stop device, Active "
				 "nodes = 0x%x \n", uNodesAllocated);
			return DSP_EWRONGSTATE;
		}
	}
	/* Call the WMD_BRD_Stop */
	/* It is OK to stop a device that does n't have nodes OR not started */
	status = (*pProcObject->pIntfFxns->pfnBrdStop)(pProcObject->
		 hWmdContext);
	if (DSP_SUCCEEDED(status)) {
		GT_0trace(PROC_DebugMask, GT_1CLASS,
			 "PROC_Stop: Processor Stopped, "
			 "i.e in standby mode \n");
		pProcObject->sState = PROC_STOPPED;
		/* Destory the Node Manager, MSG Manager */
		if (DSP_SUCCEEDED(DEV_Destroy2(pProcObject->hDevObject))) {
			/* Destroy the MSG by calling MSG_Delete */
			DEV_GetMsgMgr(pProcObject->hDevObject, &hMsgMgr);
			if (hMsgMgr) {
				MSG_Delete(hMsgMgr);
				DEV_SetMsgMgr(pProcObject->hDevObject, NULL);
			}
#ifdef DEBUG
			if (DSP_SUCCEEDED((*pProcObject->pIntfFxns->
			   pfnBrdStatus)(pProcObject->hWmdContext,
			   &uBrdState))) {
				GT_0trace(PROC_DebugMask, GT_1CLASS,
					 "PROC_Monitor:Processor Stopped \n");
				DBC_Assert(uBrdState == BRD_STOPPED);
			}
#endif
		} else {
			GT_0trace(PROC_DebugMask, GT_7CLASS,
				 "PROC_Stop Couldn't delete node manager \n");
		}
	} else {
		GT_0trace(PROC_DebugMask, GT_7CLASS,
			 "PROC_Stop Failed to Stop the processor/device \n");
	}
func_end:
	GT_1trace(PROC_DebugMask, GT_ENTER, "Exiting PROC_Stop, status  0x%x\n",
		 status);

	return status;
}

/*
 *  ======== PROC_UnMap ========
 *  Purpose:
 *      Removes a MPU buffer mapping from the DSP address space.
 */
DSP_STATUS PROC_UnMap(DSP_HPROCESSOR hProcessor, void *pMapAddr,
		struct PROCESS_CONTEXT *pr_ctxt)
{
	DSP_STATUS status = DSP_SOK;
	struct PROC_OBJECT *pProcObject = (struct PROC_OBJECT *)hProcessor;
	struct DMM_OBJECT *hDmmMgr;
	u32 vaAlign;
	u32 sizeAlign;
#ifndef RES_CLEANUP_DISABLE
	HANDLE	      dmmRes;
#endif
	GT_2trace(PROC_DebugMask, GT_ENTER,
		 "Entered PROC_UnMap, args:\n\thProcessor:"
		 "0x%x pMapAddr: 0x%x\n", hProcessor, pMapAddr);

	vaAlign = PG_ALIGN_LOW((u32) pMapAddr, PG_SIZE_4K);
	if (!MEM_IsValidHandle(pProcObject, PROC_SIGNATURE)) {
		status = DSP_EHANDLE;
		GT_0trace(PROC_DebugMask, GT_7CLASS, "PROC_UnMap: "
			 "InValid Processor Handle \n");
		goto func_end;
	}

	status = DMM_GetHandle(hProcessor, &hDmmMgr);
	if (DSP_FAILED(status))
		goto func_end;
	/* Critical section */
	(void)SYNC_EnterCS(hProcLock);
	if (DSP_FAILED(status)) {
		GT_1trace(PROC_DebugMask, GT_7CLASS, "PROC_UnMap: "
			 "Failed to get DMM Mgr handle: 0x%x\n", status);
	} else {
		/* Update DMM structures. Get the size to unmap.
		 This function returns error if the VA is not mapped */
		status = DMM_UnMapMemory(hDmmMgr, (u32) vaAlign, &sizeAlign);
	}
	/* Remove mapping from the page tables. */
	if (DSP_SUCCEEDED(status)) {
		status = (*pProcObject->pIntfFxns->pfnBrdMemUnMap)
			 (pProcObject->hWmdContext, vaAlign, sizeAlign);
	}
	(void)SYNC_LeaveCS(hProcLock);
#ifndef RES_CLEANUP_DISABLE
	GT_1trace(PROC_DebugMask, GT_ENTER,
		   "PROC_UnMap DRV_GetDMMResElement "
		   "pMapAddr:[0x%x]", pMapAddr);
	if (DSP_FAILED(status))
		goto func_end;

	if (pr_ctxt) {
		DSP_STATUS rc;

		spin_lock(&pr_ctxt->dmm_list_lock);

		rc = DRV_GetDMMResElement((u32)pMapAddr, &dmmRes, pr_ctxt);
		if (rc != DSP_ENOTFOUND)
			DRV_RemoveDMMResElement(dmmRes, pr_ctxt);

		spin_unlock(&pr_ctxt->dmm_list_lock);
	}
#endif
func_end:
	GT_1trace(PROC_DebugMask, GT_ENTER,
		 "Leaving PROC_UnMap [0x%x]", status);
	return status;
}

/*
 *  ======== PROC_UnReserveMemory ========
 *  Purpose:
 *      Frees a previously reserved region of DSP address space.
 */
DSP_STATUS PROC_UnReserveMemory(DSP_HPROCESSOR hProcessor, void *pRsvAddr)
{
	struct DMM_OBJECT *hDmmMgr;
	DSP_STATUS status = DSP_SOK;
	struct PROC_OBJECT *pProcObject = (struct PROC_OBJECT *)hProcessor;

	GT_2trace(PROC_DebugMask, GT_ENTER,
		 "Entered PROC_UnReserveMemory, args:\n\t"
		 "hProcessor: 0x%x pRsvAddr: 0x%x\n", hProcessor, pRsvAddr);
	if (!MEM_IsValidHandle(pProcObject, PROC_SIGNATURE)) {
		status = DSP_EHANDLE;
		GT_0trace(PROC_DebugMask, GT_7CLASS, "PROC_UnMap: "
			 "InValid Processor Handle \n");
		goto func_end;
	}
	status = DMM_GetHandle(pProcObject, &hDmmMgr);
	if (DSP_FAILED(status))
		GT_1trace(PROC_DebugMask, GT_7CLASS,
			 "PROC_UnReserveMemory: Failed to get DMM Mgr "
			 "handle: 0x%x\n", status);
	else
		status = DMM_UnReserveMemory(hDmmMgr, (u32) pRsvAddr);

	GT_1trace(PROC_DebugMask, GT_ENTER,
		 "Leaving PROC_UnReserveMemory [0x%x]",
		 status);
func_end:
	return status;
}

/*
 *  ======== = PROC_Monitor ======== ==
 *  Purpose:
 *      Place the Processor in Monitor State. This is an internal
 *      function and a requirement before Processor is loaded.
 *      This does a WMD_BRD_Stop, DEV_Destroy2 and WMD_BRD_Monitor.
 *      In DEV_Destroy2 we delete the node manager.
 *  Parameters:
 *      hProcObject:    Handle to Processor Object
 *  Returns:
 *      DSP_SOK:	Processor placed in monitor mode.
 *      !DSP_SOK:       Failed to place processor in monitor mode.
 *  Requires:
 *      Valid Processor Handle
 *  Ensures:
 *      Success:	ProcObject state is PROC_IDLE
 */
static DSP_STATUS PROC_Monitor(struct PROC_OBJECT *hProcObject)
{
	DSP_STATUS status = DSP_EFAIL;
	struct PROC_OBJECT *pProcObject = (struct PROC_OBJECT *)hProcObject;
	struct MSG_MGR *hMsgMgr;
#ifdef DEBUG
	BRD_STATUS uBrdState;
#endif

	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(pProcObject, PROC_SIGNATURE));

	GT_1trace(PROC_DebugMask, GT_ENTER, "Entered PROC_Monitor, args:\n\t"
		 "hProcessor: 0x%x\n", hProcObject);
	/* This is needed only when Device is loaded when it is
	 * already 'ACTIVE' */
	/* Destory the Node Manager, MSG Manager */
	if (DSP_SUCCEEDED(DEV_Destroy2(pProcObject->hDevObject))) {
		/* Destroy the MSG by calling MSG_Delete */
		DEV_GetMsgMgr(pProcObject->hDevObject, &hMsgMgr);
		if (hMsgMgr) {
			MSG_Delete(hMsgMgr);
			DEV_SetMsgMgr(pProcObject->hDevObject, NULL);
		}
	}
	/* Place the Board in the Monitor State */
	if (DSP_SUCCEEDED((*pProcObject->pIntfFxns->pfnBrdMonitor)
	   (pProcObject->hWmdContext))) {
		status = DSP_SOK;
#ifdef DEBUG
		if (DSP_SUCCEEDED((*pProcObject->pIntfFxns->pfnBrdStatus)
		   (pProcObject->hWmdContext, &uBrdState))) {
			GT_0trace(PROC_DebugMask, GT_1CLASS,
				 "PROC_Monitor:Processor in "
				 "Monitor State\n");
			DBC_Assert(uBrdState == BRD_IDLE);
		}
#endif
	} else {
		/* Monitor Failure */
		GT_0trace(PROC_DebugMask, GT_7CLASS,
			 "PROC_Monitor: Processor Could not"
			 "be put in Monitor mode \n");
	}
	GT_1trace(PROC_DebugMask, GT_ENTER,
		 "Exiting PROC_Monitor, status  0x%x\n",
		 status);
#ifdef DEBUG
	DBC_Ensure((DSP_SUCCEEDED(status) && uBrdState == BRD_IDLE) ||
		  DSP_FAILED(status));
#endif
	return status;
}

/*
 *  ======== GetEnvpCount ========
 *  Purpose:
 *      Return the number of elements in the envp array, including the
 *      terminating NULL element.
 */
static s32 GetEnvpCount(char **envp)
{
	s32 cRetval = 0;
	if (envp) {
		while (*envp++)
			cRetval++;

		cRetval += 1;	/* Include the terminating NULL in the count. */
	}

	return cRetval;
}

/*
 *  ======== PrependEnvp ========
 *  Purpose:
 *      Prepend an environment variable=value pair to the new envp array, and
 *      copy in the existing var=value pairs in the old envp array.
 */
static char **PrependEnvp(char **newEnvp, char **envp, s32 cEnvp, s32 cNewEnvp,
			 char *szVar)
{
	char **ppEnvp = newEnvp;

	DBC_Require(newEnvp);

	/* Prepend new environ var=value string */
	*newEnvp++ = szVar;

	/* Copy user's environment into our own. */
	while (cEnvp--)
		*newEnvp++ = *envp++;

	/* Ensure NULL terminates the new environment strings array. */
	if (cEnvp == 0)
		*newEnvp = NULL;

	return ppEnvp;
}

/*
 *  ======== PROC_NotifyClients ========
 *  Purpose:
 *      Notify the processor the events.
 */
DSP_STATUS PROC_NotifyClients(DSP_HPROCESSOR hProc, u32 uEvents)
{
	DSP_STATUS status = DSP_SOK;
	struct PROC_OBJECT *pProcObject = (struct PROC_OBJECT *)hProc;

	DBC_Require(MEM_IsValidHandle(pProcObject, PROC_SIGNATURE));
	DBC_Require(IsValidProcEvent(uEvents));
	DBC_Require(cRefs > 0);
	if (!MEM_IsValidHandle(pProcObject, PROC_SIGNATURE)) {
		status = DSP_EHANDLE;
		GT_0trace(PROC_DebugMask, GT_7CLASS, "PROC_NotifyClients: "
			 "InValid Processor Handle \n");
		goto func_end;
	}

	NTFY_Notify(pProcObject->hNtfy, uEvents);
	GT_0trace(PROC_DebugMask, GT_1CLASS,
		 "PROC_NotifyClients :Signaled. \n");
func_end:
	return status;
}

/*
 *  ======== PROC_NotifyAllClients ========
 *  Purpose:
 *      Notify the processor the events. This includes notifying all clients
 *      attached to a particulat DSP.
 */
DSP_STATUS PROC_NotifyAllClients(DSP_HPROCESSOR hProc, u32 uEvents)
{
	DSP_STATUS status = DSP_SOK;
	struct PROC_OBJECT *pProcObject = (struct PROC_OBJECT *)hProc;

	DBC_Require(MEM_IsValidHandle(pProcObject, PROC_SIGNATURE));
	DBC_Require(IsValidProcEvent(uEvents));
	DBC_Require(cRefs > 0);

	DEV_NotifyClients(pProcObject->hDevObject, uEvents);

	GT_0trace(PROC_DebugMask, GT_1CLASS,
		 "PROC_NotifyAllClients :Signaled. \n");

	return status;
}

/*
 *  ======== PROC_GetProcessorId ========
 *  Purpose:
 *      Retrieves the processor ID.
 */
DSP_STATUS PROC_GetProcessorId(DSP_HPROCESSOR hProc, u32 *procID)
{
	DSP_STATUS status = DSP_SOK;
	struct PROC_OBJECT *pProcObject = (struct PROC_OBJECT *)hProc;

	if (MEM_IsValidHandle(pProcObject, PROC_SIGNATURE))
		*procID = pProcObject->uProcessor;
	else {
		status = DSP_EHANDLE;
		GT_0trace(PROC_DebugMask, GT_7CLASS, "PROC_GetProcessorId: "
			 "InValid Processor Handle \n");
	}
	return status;
}

