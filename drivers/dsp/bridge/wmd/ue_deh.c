/*
 * ue_deh.c
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
 *  ======== ue_deh.c ========
 *  Description:
 *      Implements upper edge DSP exception handling (DEH) functions.
 *
 *! Revision History:
 *! ================
 *! 03-Jan-2005 hn: Support for IVA DEH.
 *! 05-Jan-2004 vp: Updated for the 24xx HW library.
 *! 19-Feb-2003 vp: Code review updates.
 *!                 - Cosmetic changes.
 *! 18-Oct-2002 sb: Ported to Linux platform.
 *! 10-Dec-2001 kc: Updated DSP error reporting in DEBUG mode.
 *! 10-Sep-2001 kc: created.
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

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/csl.h>
#include <dspbridge/cfg.h>
#include <dspbridge/dpc.h>
#include <dspbridge/mem.h>
#include <dspbridge/ntfy.h>
#include <dspbridge/drv.h>

/*  ----------------------------------- Link Driver */
#include <dspbridge/wmddeh.h>

/*  ----------------------------------- Platform Manager */
#include <dspbridge/dev.h>
#include <dspbridge/wcd.h>

/* ------------------------------------ Hardware Abstraction Layer */
#include <hw_defs.h>
#include <hw_mmu.h>

/*  ----------------------------------- This */
#include "mmu_fault.h"
#include "_tiomap.h"
#include "_deh.h"
#include "_tiomap_mmu.h"
#include "_tiomap_pwr.h"
#include <dspbridge/io_sm.h>

static struct HW_MMUMapAttrs_t  mapAttrs = { HW_LITTLE_ENDIAN,
					HW_ELEM_SIZE_16BIT,
					HW_MMU_CPUES} ;
#define VirtToPhys(x)       ((x) - PAGE_OFFSET + PHYS_OFFSET)

static u32 dummyVaAddr;
/*
 *  ======== WMD_DEH_Create ========
 *      Creates DEH manager object.
 */
DSP_STATUS WMD_DEH_Create(OUT struct DEH_MGR **phDehMgr,
			 struct DEV_OBJECT *hDevObject)
{
	DSP_STATUS status = DSP_SOK;
	struct DEH_MGR *pDehMgr = NULL;
	struct CFG_HOSTRES cfgHostRes;
	struct CFG_DEVNODE *hDevNode;
	struct WMD_DEV_CONTEXT *hWmdContext = NULL;

	DBG_Trace(DBG_LEVEL1, "Entering DEH_Create: 0x%x\n", phDehMgr);
	 /*  Message manager will be created when a file is loaded, since
	 *  size of message buffer in shared memory is configurable in
	 *  the base image.  */
	/* Get WMD context info. */
	DEV_GetWMDContext(hDevObject, &hWmdContext);
	DBC_Assert(hWmdContext);
	dummyVaAddr = 0;
	/* Allocate IO manager object: */
	MEM_AllocObject(pDehMgr, struct DEH_MGR, SIGNATURE);
	if (pDehMgr == NULL) {
		status = DSP_EMEMORY;
	} else {
		/* Create an NTFY object to manage notifications */
		if (DSP_SUCCEEDED(status))
			status = NTFY_Create(&pDehMgr->hNtfy);

		/* Create a DPC object. */
		status = DPC_Create(&pDehMgr->hMmuFaultDpc, MMU_FaultDpc,
				   (void *)pDehMgr);
		if (DSP_SUCCEEDED(status))
			status = DEV_GetDevNode(hDevObject, &hDevNode);

		if (DSP_SUCCEEDED(status))
			status = CFG_GetHostResources(hDevNode, &cfgHostRes);

		if (DSP_SUCCEEDED(status)) {
			/* Fill in context structure */
			pDehMgr->hWmdContext = hWmdContext;
			pDehMgr->errInfo.dwErrMask = 0L;
			pDehMgr->errInfo.dwVal1 = 0L;
			pDehMgr->errInfo.dwVal2 = 0L;
			pDehMgr->errInfo.dwVal3 = 0L;
			/* Install ISR function for DSP MMU fault */
                       if ((request_irq(INT_DSP_MMU_IRQ, MMU_FaultIsr, 0,
                                           "DspBridge\tiommu fault", (void *)pDehMgr)) == 0)
                               status = DSP_SOK;
                       else
                               status = DSP_EFAIL;
		}
	}
	if (DSP_FAILED(status)) {
		/* If create failed, cleanup */
		WMD_DEH_Destroy((struct DEH_MGR *)pDehMgr);
		*phDehMgr = NULL;
	} else {
		*phDehMgr = (struct DEH_MGR *)pDehMgr;
               DBG_Trace(DBG_LEVEL1, "ISR_IRQ Object 0x%x \n",
                                        pDehMgr);
	}
	DBG_Trace(DBG_LEVEL1, "Exiting DEH_Create.\n");
	return status;
}

/*
 *  ======== WMD_DEH_Destroy ========
 *      Destroys DEH manager object.
 */
DSP_STATUS WMD_DEH_Destroy(struct DEH_MGR *hDehMgr)
{
	DSP_STATUS status = DSP_SOK;
	struct DEH_MGR *pDehMgr = (struct DEH_MGR *)hDehMgr;

	DBG_Trace(DBG_LEVEL1, "Entering DEH_Destroy: 0x%x\n", pDehMgr);
	if (MEM_IsValidHandle(pDehMgr, SIGNATURE)) {
		/* Release dummy VA buffer */
		WMD_DEH_ReleaseDummyMem();
		/* If notification object exists, delete it */
		if (pDehMgr->hNtfy)
			(void)NTFY_Delete(pDehMgr->hNtfy);
		/* Disable DSP MMU fault */
               free_irq(INT_DSP_MMU_IRQ, pDehMgr);
		(void)DPC_Destroy(pDehMgr->hMmuFaultDpc);
		/* Deallocate the DEH manager object */
		MEM_FreeObject(pDehMgr);
	}
	DBG_Trace(DBG_LEVEL1, "Exiting DEH_Destroy.\n");
	return status;
}

/*
 *  ======== WMD_DEH_RegisterNotify ========
 *      Registers for DEH notifications.
 */
DSP_STATUS WMD_DEH_RegisterNotify(struct DEH_MGR *hDehMgr, u32 uEventMask,
				 u32 uNotifyType,
				 struct DSP_NOTIFICATION *hNotification)
{
	DSP_STATUS status = DSP_SOK;
	struct DEH_MGR *pDehMgr = (struct DEH_MGR *)hDehMgr;

	DBG_Trace(DBG_LEVEL1, "Entering WMD_DEH_RegisterNotify: 0x%x\n",
		 pDehMgr);

	if (MEM_IsValidHandle(pDehMgr, SIGNATURE)) {
		status = NTFY_Register(pDehMgr->hNtfy, hNotification,
			 uEventMask, uNotifyType);
	}
	DBG_Trace(DBG_LEVEL1, "Exiting WMD_DEH_RegisterNotify.\n");
	return status;
}


/*
 *  ======== WMD_DEH_Notify ========
 *      DEH error notification function. Informs user about the error.
 */
void WMD_DEH_Notify(struct DEH_MGR *hDehMgr, u32 ulEventMask,
			 u32 dwErrInfo)
{
	struct DEH_MGR *pDehMgr = (struct DEH_MGR *)hDehMgr;
	struct WMD_DEV_CONTEXT *pDevContext;
	DSP_STATUS status = DSP_SOK;
	DSP_STATUS status1 = DSP_EFAIL;
	u32 memPhysical = 0;
	u32 HW_MMU_MAX_TLB_COUNT = 31;
	extern u32 faultAddr;
	struct CFG_HOSTRES resources;
	HW_STATUS hwStatus;

	status = CFG_GetHostResources(
			(struct CFG_DEVNODE *)DRV_GetFirstDevExtension(),
			&resources);
	if (DSP_FAILED(status))
		DBG_Trace(DBG_LEVEL7,
			 "**Failed to get Host Resources in MMU ISR **\n");

	DBG_Trace(DBG_LEVEL1, "Entering WMD_DEH_Notify: 0x%x, 0x%x\n", pDehMgr,
		 ulEventMask);
	if (MEM_IsValidHandle(pDehMgr, SIGNATURE)) {
		printk(KERN_INFO "WMD_DEH_Notify: ********** DEVICE EXCEPTION "
			"**********\n");
		pDevContext = (struct WMD_DEV_CONTEXT *)pDehMgr->hWmdContext;

		switch (ulEventMask) {
		case DSP_SYSERROR:
			/* reset errInfo structure before use */
			pDehMgr->errInfo.dwErrMask = DSP_SYSERROR;
			pDehMgr->errInfo.dwVal1 = 0L;
			pDehMgr->errInfo.dwVal2 = 0L;
			pDehMgr->errInfo.dwVal3 = 0L;
			pDehMgr->errInfo.dwVal1 = dwErrInfo;
			printk(KERN_ERR "WMD_DEH_Notify: DSP_SYSERROR, errInfo "
				"= 0x%x\n", dwErrInfo);
			break;
		case DSP_MMUFAULT:
			/* MMU fault routine should have set err info
			 * structure */
			pDehMgr->errInfo.dwErrMask = DSP_MMUFAULT;
			printk(KERN_INFO "WMD_DEH_Notify: DSP_MMUFAULT,"
				"errInfo = 0x%x\n", dwErrInfo);
			printk(KERN_INFO "WMD_DEH_Notify: DSP_MMUFAULT, High "
				"Address = 0x%x\n",
				(unsigned int)pDehMgr->errInfo.dwVal1);
			printk(KERN_INFO "WMD_DEH_Notify: DSP_MMUFAULT, Low "
				"Address = 0x%x\n",
				(unsigned int)pDehMgr->errInfo.dwVal2);
			printk(KERN_INFO "WMD_DEH_Notify: DSP_MMUFAULT, fault "
				"address = 0x%x\n", (unsigned int)faultAddr);
			dummyVaAddr = (u32)MEM_Calloc(sizeof(char) * 0x1000,
					MEM_PAGED);
			memPhysical  = VirtToPhys(PG_ALIGN_LOW((u32)dummyVaAddr,
								PG_SIZE_4K));
DBG_Trace(DBG_LEVEL6, "WMD_DEH_Notify: DSP_MMUFAULT, "
				 "mem Physical= 0x%x\n", memPhysical);
			pDevContext = (struct WMD_DEV_CONTEXT *)
						pDehMgr->hWmdContext;
			/* Reset the dynamic mmu index to fixed count if it
			 * exceeds 31. So that the dynmmuindex is always
			 * between the range of standard/fixed entries
			 * and 31.  */
			if (pDevContext->numTLBEntries >
			   HW_MMU_MAX_TLB_COUNT) {
				pDevContext->numTLBEntries = pDevContext->
					fixedTLBEntries;
			}
			DBG_Trace(DBG_LEVEL6, "Adding TLB Entry %d: VA: 0x%x, "
				 "PA: 0x%x\n", pDevContext->
				numTLBEntries, faultAddr, memPhysical);
			if (DSP_SUCCEEDED(status)) {
				hwStatus = HW_MMU_TLBAdd(resources.dwDmmuBase,
					memPhysical, faultAddr,
					HW_PAGE_SIZE_4KB, 1, &mapAttrs,
					HW_SET, HW_SET);
			}
			/* send an interrupt to DSP */
			HW_MBOX_MsgWrite(resources.dwMboxBase, MBOX_ARM2DSP,
					 MBX_DEH_CLASS | MBX_DEH_EMMU);
			/* Clear MMU interrupt */
			HW_MMU_EventAck(resources.dwDmmuBase,
					 HW_MMU_TRANSLATION_FAULT);
			break;
		case DSP_PWRERROR:
			/* reset errInfo structure before use */
			pDehMgr->errInfo.dwErrMask = DSP_PWRERROR;
			pDehMgr->errInfo.dwVal1 = 0L;
			pDehMgr->errInfo.dwVal2 = 0L;
			pDehMgr->errInfo.dwVal3 = 0L;
			pDehMgr->errInfo.dwVal1 = dwErrInfo;
			printk(KERN_ERR "WMD_DEH_Notify: DSP_PWRERROR, errInfo "
					"= 0x%x\n", dwErrInfo);
			break;
		default:
			DBG_Trace(DBG_LEVEL6,
				 "WMD_DEH_Notify: Unknown Error, errInfo = "
				 "0x%x\n", dwErrInfo);
			break;
		}

		/* Filter subsequent notifications when an error occurs */
		if (pDevContext->dwBrdState != BRD_ERROR) {
			/* Use it as a flag to send notifications the
			 * first time and error occurred, next time
			 * state will be BRD_ERROR */
			status1 = DSP_EFAIL;
		}

		/* Filter subsequent notifications when an error occurs */
		if (pDevContext->dwBrdState != BRD_ERROR)
			status1 = DSP_SOK;

		/* Set the Board state as ERROR */
		pDevContext->dwBrdState = BRD_ERROR;
		/* Disable all the clocks that were enabled by DSP */
		(void)DSP_PeripheralClocks_Disable(pDevContext, NULL);
		/* Call DSP Trace Buffer */
		PrintDspTraceBuffer(hDehMgr->hWmdContext);

		if (DSP_SUCCEEDED(status1)) {
			/* Signal DSP error/exception event. */
			NTFY_Notify(pDehMgr->hNtfy, ulEventMask);
		}

	}
	DBG_Trace(DBG_LEVEL1, "Exiting WMD_DEH_Notify\n");

}

/*
 *  ======== WMD_DEH_GetInfo ========
 *      Retrieves error information.
 */
DSP_STATUS WMD_DEH_GetInfo(struct DEH_MGR *hDehMgr,
			  struct DSP_ERRORINFO *pErrInfo)
{
	DSP_STATUS status = DSP_SOK;
	struct DEH_MGR *pDehMgr = (struct DEH_MGR *)hDehMgr;

	DBC_Require(pDehMgr);
	DBC_Require(pErrInfo);

	DBG_Trace(DBG_LEVEL1, "Entering WMD_DEH_GetInfo: 0x%x\n", hDehMgr);

	if (MEM_IsValidHandle(pDehMgr, SIGNATURE)) {
		/* Copy DEH error info structure to PROC error info
		 * structure. */
		pErrInfo->dwErrMask = pDehMgr->errInfo.dwErrMask;
		pErrInfo->dwVal1 = pDehMgr->errInfo.dwVal1;
		pErrInfo->dwVal2 = pDehMgr->errInfo.dwVal2;
		pErrInfo->dwVal3 = pDehMgr->errInfo.dwVal3;
	}

	DBG_Trace(DBG_LEVEL1, "Exiting WMD_DEH_GetInfo\n");

	return status;
}


/*
 *  ======== WMD_DEH_ReleaseDummyMem ========
 *      Releases memory allocated for dummy page
 */
void WMD_DEH_ReleaseDummyMem(void)
{
	if (dummyVaAddr) {
		MEM_Free((void *)dummyVaAddr);
		dummyVaAddr = 0;
	}
}

