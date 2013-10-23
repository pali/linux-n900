/*
 * io_sm.c
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
 *  ======== io_sm.c ========
 *  Description:
 *      IO dispatcher for a shared memory channel driver.
 *
 *  Public Functions:
 *      WMD_IO_Create
 *      WMD_IO_Destroy
 *      WMD_IO_OnLoaded
 *      IO_AndSetValue
 *      IO_BufSize
 *      IO_CancelChnl
 *      IO_DPC
 *      IO_ISR
 *      IO_IVAISR
 *      IO_OrSetValue
 *      IO_ReadValue
 *      IO_ReadValueLong
 *      IO_RequestChnl
 *      IO_Schedule
 *      IO_WriteValue
 *      IO_WriteValueLong
 *
 *  Channel Invariant:
 *      There is an important invariant condition which must be maintained per
 *      channel outside of WMD_CHNL_GetIOC() and IO_Dispatch(), violation of
 *      which may cause timeouts and/or failure of the WIN32_WaitSingleObject
 *      function (SYNC_WaitOnEvent).
 *
 */

/*  ----------------------------------- Host OS */
#include <dspbridge/host_os.h>
#include <linux/workqueue.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>
#include <dspbridge/dbdefs.h>
#include <dspbridge/errbase.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>
#include <dspbridge/dbg.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/cfg.h>
#include <dspbridge/dpc.h>
#include <dspbridge/mem.h>
#include <dspbridge/ntfy.h>
#include <dspbridge/sync.h>
#include <dspbridge/reg.h>

/* ------------------------------------ Hardware Abstraction Layer */
#include <hw_defs.h>
#include <hw_mmu.h>

/*  ----------------------------------- Mini Driver */
#include <dspbridge/wmddeh.h>
#include <dspbridge/wmdio.h>
#include <dspbridge/wmdioctl.h>
#include <_tiomap.h>
#include <tiomap_io.h>
#include <_tiomap_pwr.h>
#include <tiomap_io.h>

/*  ----------------------------------- Platform Manager */
#include <dspbridge/cod.h>
#include <dspbridge/dev.h>
#include <dspbridge/chnl_sm.h>
#include <dspbridge/dbreg.h>

/*  ----------------------------------- Others */
#include <dspbridge/rms_sh.h>
#include <dspbridge/mgr.h>
#include <dspbridge/drv.h>
#include "_cmm.h"

/*  ----------------------------------- This */
#include <dspbridge/io_sm.h>
#include "_msg_sm.h"
#include <dspbridge/gt.h>

/*  ----------------------------------- Defines, Data Structures, Typedefs */
#define OUTPUTNOTREADY  0xffff
#define NOTENABLED      0xffff	/* channel(s) not enabled */

#define EXTEND      "_EXT_END"

#define SwapWord(x)     (x)
#define ulPageAlignSize 0x10000   /* Page Align Size */

#define MAX_PM_REQS 32

/* IO Manager: only one created per board: */
struct IO_MGR {
	/* These four fields must be the first fields in a IO_MGR_ struct: */
	u32 dwSignature; 	/* Used for object validation   */
	struct WMD_DEV_CONTEXT *hWmdContext; 	/* WMD device context  */
	struct WMD_DRV_INTERFACE *pIntfFxns; 	/* Function interface to WMD */
	struct DEV_OBJECT *hDevObject; 	/* Device this board represents */

	/* These fields initialized in WMD_IO_Create():    */
	struct CHNL_MGR *hChnlMgr;
	struct SHM *pSharedMem; 	/* Shared Memory control	*/
	u8 *pInput; 		/* Address of input channel     */
	u8 *pOutput; 		/* Address of output channel    */
	struct MSG_MGR *hMsgMgr; 	/* Message manager */
	struct MSG *pMsgInputCtrl; 	/* Msg control for from DSP messages */
	struct MSG *pMsgOutputCtrl; 	/* Msg control for to DSP messages */
	u8 *pMsgInput; 	/* Address of input messages    */
	u8 *pMsgOutput; 	/* Address of output messages   */
	u32 uSMBufSize; 	/* Size of a shared memory I/O channel */
	bool fSharedIRQ; 	/* Is this IRQ shared?	  */
	struct DPC_OBJECT *hDPC; 	/* DPC object handle	    */
	struct SYNC_CSOBJECT *hCSObj; 	/* Critical section object handle */
	u32 uWordSize; 	/* Size in bytes of DSP word    */
	u16 wIntrVal; 		/* interrupt value	      */
	/* private extnd proc info; mmu setup */
	struct MGR_PROCESSOREXTINFO extProcInfo;
	struct CMM_OBJECT *hCmmMgr; 	/* Shared Mem Mngr	      */
       struct work_struct io_workq;     /*workqueue */
	u32 dQuePowerMbxVal[MAX_PM_REQS];
	u32 iQuePowerHead;
	u32 iQuePowerTail;
#ifndef DSP_TRACEBUF_DISABLED
	u32 ulTraceBufferBegin; 	/* Trace message start address */
	u32 ulTraceBufferEnd; 	/* Trace message end address */
	u32 ulTraceBufferCurrent; 	/* Trace message current address */
	u32 ulGPPReadPointer; 	/* GPP Read pointer to Trace buffer */
	u8 *pMsg;
	u32 ulGppVa;
	u32 ulDspVa;
#endif
} ;

/*  ----------------------------------- Function Prototypes */
static void IO_DispatchChnl(IN struct IO_MGR *pIOMgr,
			   IN OUT struct CHNL_OBJECT *pChnl, u32 iMode);
static void IO_DispatchMsg(IN struct IO_MGR *pIOMgr, struct MSG_MGR *hMsgMgr);
static void IO_DispatchPM(struct work_struct *work);
static void NotifyChnlComplete(struct CHNL_OBJECT *pChnl,
				struct CHNL_IRP *pChirp);
static void InputChnl(struct IO_MGR *pIOMgr, struct CHNL_OBJECT *pChnl,
			u32 iMode);
static void OutputChnl(struct IO_MGR *pIOMgr, struct CHNL_OBJECT *pChnl,
			u32 iMode);
static void InputMsg(struct IO_MGR *pIOMgr, struct MSG_MGR *hMsgMgr);
static void OutputMsg(struct IO_MGR *pIOMgr, struct MSG_MGR *hMsgMgr);
static u32 FindReadyOutput(struct CHNL_MGR *pChnlMgr,
			     struct CHNL_OBJECT *pChnl, u32 dwMask);
static u32 ReadData(struct WMD_DEV_CONTEXT *hDevContext, void *pDest,
			void *pSrc, u32 uSize);
static u32 WriteData(struct WMD_DEV_CONTEXT *hDevContext, void *pDest,
			void *pSrc, u32 uSize);
static struct workqueue_struct *bridge_workqueue;
#ifndef DSP_TRACEBUF_DISABLED
void PrintDSPDebugTrace(struct IO_MGR *hIOMgr);
#endif

/* Bus Addr (cached kernel)*/
static DSP_STATUS registerSHMSegs(struct IO_MGR *hIOMgr,
				  struct COD_MANAGER *hCodMan,
				  u32 dwGPPBasePA);

#ifdef CONFIG_BRIDGE_DVFS
/* The maximum number of OPPs that are supported */
extern s32 dsp_max_opps;
/* The Vdd1 opp table information */
extern u32 vdd1_dsp_freq[6][4] ;
#endif

#if GT_TRACE
static struct GT_Mask dsp_trace_mask = { NULL, NULL }; /* GT trace variable */
#endif

/*
 *  ======== WMD_IO_Create ========
 *      Create an IO manager object.
 */
DSP_STATUS WMD_IO_Create(OUT struct IO_MGR **phIOMgr,
			 struct DEV_OBJECT *hDevObject,
			 IN CONST struct IO_ATTRS *pMgrAttrs)
{
	DSP_STATUS status = DSP_SOK;
	struct IO_MGR *pIOMgr = NULL;
	struct SHM *pSharedMem = NULL;
	struct WMD_DEV_CONTEXT *hWmdContext = NULL;
	struct CFG_HOSTRES hostRes;
	struct CFG_DEVNODE *hDevNode;
	struct CHNL_MGR *hChnlMgr;
	static int ref_count;
	u32 devType;
	/* Check DBC requirements:  */
	DBC_Require(phIOMgr != NULL);
	DBC_Require(pMgrAttrs != NULL);
	DBC_Require(pMgrAttrs->uWordSize != 0);
	/* This for the purposes of DBC_Require: */
	status = DEV_GetChnlMgr(hDevObject, &hChnlMgr);
	DBC_Require(status != DSP_EHANDLE);
	DBC_Require(hChnlMgr != NULL);
	DBC_Require(hChnlMgr->hIOMgr == NULL);
	/*
	 * Message manager will be created when a file is loaded, since
	 * size of message buffer in shared memory is configurable in
	 * the base image.
	 */
	DEV_GetWMDContext(hDevObject, &hWmdContext);
	DBC_Assert(hWmdContext);
	DEV_GetDevType(hDevObject, &devType);
	/*
	 * DSP shared memory area will get set properly when
	 * a program is loaded. They are unknown until a COFF file is
	 * loaded. I chose the value -1 because it was less likely to be
	 * a valid address than 0.
	 */
	pSharedMem = (struct SHM *) -1;
	if (DSP_FAILED(status))
		goto func_cont;

	/* Create a Single Threaded Work Queue */
	if (ref_count == 0)
		bridge_workqueue = create_workqueue("bridge_work-queue");

	if (!bridge_workqueue)
		DBG_Trace(DBG_LEVEL1, "Workqueue creation failed!\n");

	/* Allocate IO manager object: */
	MEM_AllocObject(pIOMgr, struct IO_MGR, IO_MGRSIGNATURE);
	if (pIOMgr == NULL) {
		status = DSP_EMEMORY;
		goto func_cont;
	}

	/* Intializing Work Element */
	if (ref_count == 0) {
		INIT_WORK(&pIOMgr->io_workq, (void *)IO_DispatchPM);
		ref_count = 1;
	} else
		PREPARE_WORK(&pIOMgr->io_workq, (void *)IO_DispatchPM);

	/* Initialize CHNL_MGR object:    */
#ifndef DSP_TRACEBUF_DISABLED
	pIOMgr->pMsg = NULL;
#endif
	pIOMgr->hChnlMgr = hChnlMgr;
	pIOMgr->uWordSize = pMgrAttrs->uWordSize;
	pIOMgr->pSharedMem = pSharedMem;
	if (DSP_SUCCEEDED(status))
		status = SYNC_InitializeCS(&pIOMgr->hCSObj);

	if (devType == DSP_UNIT) {
		/* Create a DPC object: */
		status = DPC_Create(&pIOMgr->hDPC, IO_DPC, (void *)pIOMgr);
		if (DSP_SUCCEEDED(status))
			status = DEV_GetDevNode(hDevObject, &hDevNode);

		pIOMgr->iQuePowerHead = 0;
		pIOMgr->iQuePowerTail = 0;
	}
	if (DSP_SUCCEEDED(status)) {
		status = CFG_GetHostResources((struct CFG_DEVNODE *)
				DRV_GetFirstDevExtension() , &hostRes);
	}
	if (DSP_SUCCEEDED(status)) {
		pIOMgr->hWmdContext = hWmdContext;
		pIOMgr->fSharedIRQ = pMgrAttrs->fShared;
		IO_DisableInterrupt(hWmdContext);
		if (devType == DSP_UNIT) {
			HW_MBOX_initSettings(hostRes.dwMboxBase);
			/* Plug the channel ISR:. */
			if ((request_irq(INT_MAIL_MPU_IRQ, IO_ISR, 0,
				"DspBridge\tmailbox", (void *)pIOMgr)) == 0)
				status = DSP_SOK;
			else
				status = DSP_EFAIL;
		}
		if (DSP_SUCCEEDED(status))
			DBG_Trace(DBG_LEVEL1, "ISR_IRQ Object 0x%x \n",
					pIOMgr);
		else
			status = CHNL_E_ISR;
	} else
		status = CHNL_E_ISR;
func_cont:
	if (DSP_FAILED(status)) {
		/* Cleanup: */
		WMD_IO_Destroy(pIOMgr);
		*phIOMgr = NULL;
	} else {
		/* Return IO manager object to caller... */
		hChnlMgr->hIOMgr = pIOMgr;
		*phIOMgr = pIOMgr;
	}
	return status;
}

/*
 *  ======== WMD_IO_Destroy ========
 *  Purpose:
 *      Disable interrupts, destroy the IO manager.
 */
DSP_STATUS WMD_IO_Destroy(struct IO_MGR *hIOMgr)
{
	DSP_STATUS status = DSP_SOK;
	struct WMD_DEV_CONTEXT *hWmdContext;
	if (MEM_IsValidHandle(hIOMgr, IO_MGRSIGNATURE)) {
		/* Unplug IRQ:    */
               /* Disable interrupts from the board:  */
               if (DSP_SUCCEEDED(DEV_GetWMDContext(hIOMgr->hDevObject,
                      &hWmdContext)))
                               DBC_Assert(hWmdContext);
               (void)CHNLSM_DisableInterrupt(hWmdContext);
               destroy_workqueue(bridge_workqueue);
               /* Linux function to uninstall ISR */
               free_irq(INT_MAIL_MPU_IRQ, (void *)hIOMgr);
               (void)DPC_Destroy(hIOMgr->hDPC);
#ifndef DSP_TRACEBUF_DISABLED
		if (hIOMgr->pMsg)
			MEM_Free(hIOMgr->pMsg);
#endif
		SYNC_DeleteCS(hIOMgr->hCSObj); 	/* Leak Fix. */
		/* Free this IO manager object: */
		MEM_FreeObject(hIOMgr);
       } else
		status = DSP_EHANDLE;

	return status;
}

/*
 *  ======== WMD_IO_OnLoaded ========
 *  Purpose:
 *      Called when a new program is loaded to get shared memory buffer
 *      parameters from COFF file. ulSharedBufferBase and ulSharedBufferLimit
 *      are in DSP address units.
 */
DSP_STATUS WMD_IO_OnLoaded(struct IO_MGR *hIOMgr)
{
	struct COD_MANAGER *hCodMan;
	struct CHNL_MGR *hChnlMgr;
	struct MSG_MGR *hMsgMgr;
	u32 ulShmBase;
	u32 ulShmBaseOffset;
	u32 ulShmLimit;
	u32 ulShmLength = -1;
	u32 ulMemLength = -1;
	u32 ulMsgBase;
	u32 ulMsgLimit;
	u32 ulMsgLength = -1;
	u32 ulExtEnd;
	u32 ulGppPa = 0;
	u32 ulGppVa = 0;
	u32 ulDspVa = 0;
	u32 ulSegSize = 0;
	u32 ulPadSize = 0;
	u32 i;
	DSP_STATUS status = DSP_SOK;
	u32 uNumProcs = 0;
	s32 ndx = 0;
	/* DSP MMU setup table */
	struct WMDIOCTL_EXTPROC aEProc[WMDIOCTL_NUMOFMMUTLB];
	struct CFG_HOSTRES hostRes;
	u32 mapAttrs;
	u32 ulShm0End;
	u32 ulDynExtBase;
	u32 ulSeg1Size = 0;
	u32 paCurr = 0;
	u32 vaCurr = 0;
	u32 gppVaCurr = 0;
	u32 numBytes = 0;
	u32 allBits = 0;
	u32 pgSize[] = { HW_PAGE_SIZE_16MB, HW_PAGE_SIZE_1MB,
			   HW_PAGE_SIZE_64KB, HW_PAGE_SIZE_4KB };

	status = DEV_GetCodMgr(hIOMgr->hDevObject, &hCodMan);
	DBC_Assert(DSP_SUCCEEDED(status));
	hChnlMgr = hIOMgr->hChnlMgr;
	 /*  The message manager is destroyed when the board is stopped.  */
	DEV_GetMsgMgr(hIOMgr->hDevObject, &hIOMgr->hMsgMgr);
	hMsgMgr = hIOMgr->hMsgMgr;
	DBC_Assert(MEM_IsValidHandle(hChnlMgr, CHNL_MGRSIGNATURE));
	DBC_Assert(MEM_IsValidHandle(hMsgMgr, MSGMGR_SIGNATURE));
	if (hIOMgr->pSharedMem)
		hIOMgr->pSharedMem = NULL;

	/* Get start and length of channel part of shared memory */
	status = COD_GetSymValue(hCodMan, CHNL_SHARED_BUFFER_BASE_SYM,
				 &ulShmBase);
	if (DSP_FAILED(status)) {
		status = CHNL_E_NOMEMMAP;
		goto func_cont1;
	}
	status = COD_GetSymValue(hCodMan, CHNL_SHARED_BUFFER_LIMIT_SYM,
				&ulShmLimit);
	if (DSP_FAILED(status)) {
		status = CHNL_E_NOMEMMAP;
		goto func_cont1;
	}
	if (ulShmLimit <= ulShmBase) {
		status = CHNL_E_INVALIDMEMBASE;
	} else {
		/* get total length in bytes */
		ulShmLength = (ulShmLimit - ulShmBase + 1) * hIOMgr->uWordSize;
		/* Calculate size of a PROCCOPY shared memory region */
		DBG_Trace(DBG_LEVEL7,
			 "**(proc)PROCCOPY SHMMEM SIZE: 0x%x bytes\n",
			  (ulShmLength - sizeof(struct SHM)));
	}
func_cont1:
	if (DSP_SUCCEEDED(status)) {
		/* Get start and length of message part of shared memory */
		status = COD_GetSymValue(hCodMan, MSG_SHARED_BUFFER_BASE_SYM,
					&ulMsgBase);
	}
	if (DSP_SUCCEEDED(status)) {
		status = COD_GetSymValue(hCodMan, MSG_SHARED_BUFFER_LIMIT_SYM,
					&ulMsgLimit);
		if (DSP_SUCCEEDED(status)) {
			if (ulMsgLimit <= ulMsgBase) {
				status = CHNL_E_INVALIDMEMBASE;
			} else {
				/* Length (bytes) of messaging part of shared
				 * memory */
				ulMsgLength = (ulMsgLimit - ulMsgBase + 1) *
					      hIOMgr->uWordSize;
				/* Total length (bytes) of shared memory:
				 * chnl + msg */
				ulMemLength = ulShmLength + ulMsgLength;
			}
		} else {
			status = CHNL_E_NOMEMMAP;
		}
	}
	if (DSP_SUCCEEDED(status)) {
#ifndef DSP_TRACEBUF_DISABLED
		status = COD_GetSymValue(hCodMan, DSP_TRACESEC_END, &ulShm0End);
		DBG_Trace(DBG_LEVEL7, "_BRIDGE_TRACE_END value = %x \n",
			 ulShm0End);
#else
		status = COD_GetSymValue(hCodMan, SHM0_SHARED_END_SYM,
					 &ulShm0End);
		DBG_Trace(DBG_LEVEL7, "_SHM0_END = %x \n", ulShm0End);
#endif
		if (DSP_FAILED(status))
			status = CHNL_E_NOMEMMAP;

	}
	if (DSP_SUCCEEDED(status)) {
		status = COD_GetSymValue(hCodMan, DYNEXTBASE, &ulDynExtBase);
		if (DSP_FAILED(status))
			status = CHNL_E_NOMEMMAP;

	}
	if (DSP_SUCCEEDED(status)) {
		status = COD_GetSymValue(hCodMan, EXTEND, &ulExtEnd);
		if (DSP_FAILED(status))
			status = CHNL_E_NOMEMMAP;

	}
	if (DSP_SUCCEEDED(status)) {
		/* Get memory reserved in host resources */
		(void)MGR_EnumProcessorInfo(0,
			(struct DSP_PROCESSORINFO *)&hIOMgr->extProcInfo,
			sizeof(struct MGR_PROCESSOREXTINFO), &uNumProcs);
		CFG_GetHostResources((
			struct CFG_DEVNODE *)DRV_GetFirstDevExtension(),
			&hostRes);
		/* The first MMU TLB entry(TLB_0) in DCD is ShmBase. */
		ndx = 0;
		ulGppPa = hostRes.dwMemPhys[1];
		ulGppVa = hostRes.dwMemBase[1];
		/* THIS IS THE VIRTUAL UNCACHED IOREMAPPED ADDRESS !!! */
		/* Why can't we directly take the DSPVA from the symbols? */
		ulDspVa = hIOMgr->extProcInfo.tyTlb[0].ulDspVirt;
		ulSegSize = (ulShm0End - ulDspVa) * hIOMgr->uWordSize;
		ulSeg1Size = (ulExtEnd - ulDynExtBase) * hIOMgr->uWordSize;
		ulSeg1Size = (ulSeg1Size + 0xFFF) & (~0xFFFUL); /* 4K align*/
		ulSegSize = (ulSegSize + 0xFFFF) & (~0xFFFFUL); /* 64K align*/
		ulPadSize = ulPageAlignSize - ((ulGppPa + ulSeg1Size) %
			     ulPageAlignSize);
			if (ulPadSize == ulPageAlignSize)
				ulPadSize = 0x0;

		 DBG_Trace(DBG_LEVEL7, "ulGppPa %x, ulGppVa %x, ulDspVa %x, "
			  "ulShm0End %x, ulDynExtBase %x, ulExtEnd %x, "
			  "ulSegSize %x ulSeg1Size %x \n", ulGppPa, ulGppVa,
			  ulDspVa, ulShm0End, ulDynExtBase, ulExtEnd, ulSegSize,
			  ulSeg1Size);

		if ((ulSegSize + ulSeg1Size + ulPadSize) >
		   hostRes.dwMemLength[1]) {
			DBG_Trace(DBG_LEVEL7, "ulGppPa %x, ulGppVa %x, ulDspVa "
				 "%x, ulShm0End %x, ulDynExtBase %x, ulExtEnd "
				 "%x, ulSegSize %x, ulSeg1Size %x \n", ulGppPa,
				 ulGppVa, ulDspVa, ulShm0End, ulDynExtBase,
				 ulExtEnd, ulSegSize, ulSeg1Size);
			DBG_Trace(DBG_LEVEL7, "Insufficient SHM Reserved 0x%x. "
				 "Required 0x%x\n", hostRes.dwMemLength[1],
				 ulSegSize + ulSeg1Size + ulPadSize);
			status = DSP_EMEMORY;
		}
	}
	if (DSP_FAILED(status))
		goto func_cont;

	paCurr = ulGppPa;
	vaCurr = ulDynExtBase * hIOMgr->uWordSize;
	gppVaCurr = ulGppVa;
	numBytes = ulSeg1Size;

	/*
	 * Try to fit into TLB entries. If not possible, push them to page
	 * tables. It is quite possible that if sections are not on
	 * bigger page boundary, we may end up making several small pages.
	 * So, push them onto page tables, if that is the case.
	 */
	mapAttrs = 0x00000000;
	mapAttrs = DSP_MAPLITTLEENDIAN;
	mapAttrs |= DSP_MAPPHYSICALADDR;
	mapAttrs |= DSP_MAPELEMSIZE32;
	mapAttrs |= DSP_MAPDONOTLOCK;

	while (numBytes && DSP_SUCCEEDED(status)) {
		/* To find the max. page size with which both PA & VA are
		 * aligned */
		allBits = paCurr | vaCurr;
		DBG_Trace(DBG_LEVEL1, "allBits %x, paCurr %x, vaCurr %x, "
			 "numBytes %x\n", allBits, paCurr, vaCurr, numBytes);
		for (i = 0; i < 4; i++) {
			if ((numBytes >= pgSize[i]) && ((allBits &
			   (pgSize[i] - 1)) == 0)) {
				status = hIOMgr->pIntfFxns->pfnBrdMemMap
					(hIOMgr->hWmdContext, paCurr, vaCurr,
					pgSize[i], mapAttrs);
				DBC_Assert(DSP_SUCCEEDED(status));
				paCurr += pgSize[i];
				vaCurr += pgSize[i];
				gppVaCurr += pgSize[i];
				numBytes -= pgSize[i];
				/* Don't try smaller sizes. Hopefully we have
				 * reached an address aligned to a bigger page
				 * size*/
				break;
			}
		}
	}
	paCurr += ulPadSize;
	vaCurr += ulPadSize;
	gppVaCurr += ulPadSize;

	/* configure the TLB entries for the next cacheable segment */
	numBytes = ulSegSize;
	vaCurr = ulDspVa * hIOMgr->uWordSize;
	allBits = 0x0;
	while (numBytes && DSP_SUCCEEDED(status)) {
		/* To find the max. page size with which both PA & VA are
		 * aligned*/
		allBits = paCurr | vaCurr;
		DBG_Trace(DBG_LEVEL1, "allBits for Seg1 %x, paCurr %x, "
			 "vaCurr %x, numBytes %x\n", allBits, paCurr, vaCurr,
			 numBytes);
		for (i = 0; i < 4; i++) {
			if (!(numBytes >= pgSize[i]) ||
			   !((allBits & (pgSize[i]-1)) == 0))
				continue;
			if (ndx < MAX_LOCK_TLB_ENTRIES) {
				/* This is the physical address written to
				 * DSP MMU */
				aEProc[ndx].ulGppPa = paCurr;
				/* THIS IS THE VIRTUAL UNCACHED IOREMAPPED
				 * ADDRESS!!! */
				aEProc[ndx].ulGppVa = gppVaCurr;
				aEProc[ndx].ulDspVa = vaCurr / hIOMgr->
						      uWordSize;
				aEProc[ndx].ulSize = pgSize[i];
				aEProc[ndx].endianism = HW_LITTLE_ENDIAN;
				aEProc[ndx].elemSize = HW_ELEM_SIZE_16BIT;
				aEProc[ndx].mixedMode = HW_MMU_CPUES;
				DBG_Trace(DBG_LEVEL1, "SHM MMU TLB entry PA %lx"
					 " VA %lx DSP_VA %lx Size %lx\n",
					 aEProc[ndx].ulGppPa,
					 aEProc[ndx].ulGppVa,
					 aEProc[ndx].ulDspVa *
					 hIOMgr->uWordSize, pgSize[i]);
				ndx++;
			} else {
				status = hIOMgr->pIntfFxns->pfnBrdMemMap(
				hIOMgr->hWmdContext, paCurr, vaCurr, pgSize[i],
					mapAttrs);
				DBG_Trace(DBG_LEVEL1, "SHM MMU PTE entry PA %lx"
					 " VA %lx DSP_VA %lx Size %lx\n",
					 aEProc[ndx].ulGppPa,
					 aEProc[ndx].ulGppVa,
					 aEProc[ndx].ulDspVa *
					 hIOMgr->uWordSize, pgSize[i]);
				DBC_Assert(DSP_SUCCEEDED(status));
			}
			paCurr += pgSize[i];
			vaCurr += pgSize[i];
			gppVaCurr += pgSize[i];
			numBytes -= pgSize[i];
			/* Don't try smaller sizes. Hopefully we have reached
			 an address aligned to a bigger page size*/
			break;
		}
	}

	 /* Copy remaining entries from CDB. All entries are 1 MB and should not
	 * conflict with SHM entries on MPU or DSP side */
	for (i = 3; i < 7 && ndx < WMDIOCTL_NUMOFMMUTLB &&
	    DSP_SUCCEEDED(status); i++) {
		if (hIOMgr->extProcInfo.tyTlb[i].ulGppPhys == 0)
			continue;

		if ((hIOMgr->extProcInfo.tyTlb[i].ulGppPhys > ulGppPa - 0x100000
			&& hIOMgr->extProcInfo.tyTlb[i].ulGppPhys <=
				ulGppPa + ulSegSize)
			|| (hIOMgr->extProcInfo.tyTlb[i].ulDspVirt > ulDspVa -
				0x100000 / hIOMgr->uWordSize && hIOMgr->
				extProcInfo.tyTlb[i].ulDspVirt
				<= ulDspVa + ulSegSize / hIOMgr->uWordSize)) {
			DBG_Trace(DBG_LEVEL7, "CDB MMU entry %d conflicts with "
				 "SHM.\n\tCDB: GppPa %x, DspVa %x.\n\tSHM: "
				 "GppPa %x, DspVa %x, Bytes %x.\n", i,
				 hIOMgr->extProcInfo.tyTlb[i].ulGppPhys,
				 hIOMgr->extProcInfo.tyTlb[i].ulDspVirt,
				 ulGppPa, ulDspVa, ulSegSize);
			status = DSP_EFAIL;
		} else {
			if (ndx < MAX_LOCK_TLB_ENTRIES) {
				aEProc[ndx].ulDspVa = hIOMgr->extProcInfo.
					tyTlb[i].ulDspVirt;
				aEProc[ndx].ulGppPa = hIOMgr->extProcInfo.
					tyTlb[i].ulGppPhys;
				aEProc[ndx].ulGppVa = 0;
				/* Can't convert, so set to zero*/
				aEProc[ndx].ulSize = 0x100000; 	/* 1 MB*/
				DBG_Trace(DBG_LEVEL1, "SHM MMU entry PA %x "
					 "DSP_VA 0x%x\n", aEProc[ndx].ulGppPa,
					aEProc[ndx].ulDspVa);
				ndx++;
			} else {
				status = hIOMgr->pIntfFxns->pfnBrdMemMap
					(hIOMgr->hWmdContext,
					hIOMgr->extProcInfo.tyTlb[i].ulGppPhys,
					hIOMgr->extProcInfo.tyTlb[i].ulDspVirt,
					0x100000, mapAttrs);
			}
		}
	}
	if (i < 7 && DSP_SUCCEEDED(status)) {
		/* All CDB entries could not be made*/
		status = DSP_EFAIL;
	}
func_cont:
	mapAttrs = 0x00000000;
	mapAttrs = DSP_MAPLITTLEENDIAN;
	mapAttrs |= DSP_MAPPHYSICALADDR;
	mapAttrs |= DSP_MAPELEMSIZE32;
	mapAttrs |= DSP_MAPDONOTLOCK;

	/* Map the L4 peripherals */
	i = 0;
	while (L4PeripheralTable[i].physAddr && DSP_SUCCEEDED(status)) {
		status = hIOMgr->pIntfFxns->pfnBrdMemMap
			(hIOMgr->hWmdContext, L4PeripheralTable[i].physAddr,
			L4PeripheralTable[i].dspVirtAddr, HW_PAGE_SIZE_4KB,
			mapAttrs);
		if (DSP_FAILED(status))
			break;
		i++;
	}

	if (DSP_SUCCEEDED(status)) {
		for (i = ndx; i < WMDIOCTL_NUMOFMMUTLB; i++) {
			aEProc[i].ulDspVa = 0;
			aEProc[i].ulGppPa = 0;
			aEProc[i].ulGppVa = 0;
			aEProc[i].ulSize = 0;
		}
		/* Set the SHM physical address entry (grayed out in CDB file)
		 * to the virtual uncached ioremapped address of SHM reserved
		 * on MPU */
		hIOMgr->extProcInfo.tyTlb[0].ulGppPhys = (ulGppVa + ulSeg1Size +
							 ulPadSize);
		DBG_Trace(DBG_LEVEL1, "*********extProcInfo *********%x \n",
			  hIOMgr->extProcInfo.tyTlb[0].ulGppPhys);
		/* Need SHM Phys addr. IO supports only one DSP for now:
		 * uNumProcs=1 */
		if ((hIOMgr->extProcInfo.tyTlb[0].ulGppPhys == 0) ||
		   (uNumProcs != 1)) {
			status = CHNL_E_NOMEMMAP;
			DBC_Assert(false);
		} else {
			DBC_Assert(aEProc[0].ulDspVa <= ulShmBase);
			/* ulShmBase may not be at ulDspVa address */
			ulShmBaseOffset = (ulShmBase - aEProc[0].ulDspVa) *
			    hIOMgr->uWordSize;
			 /* WMD_BRD_Ctrl() will set dev context dsp-mmu info. In
			 *   _BRD_Start() the MMU will be re-programed with MMU
			 *   DSPVa-GPPPa pair info while DSP is in a known
			 *   (reset) state.  */
			DBC_Assert(hIOMgr->pIntfFxns != NULL);
			DBC_Assert(hIOMgr->hWmdContext != NULL);
			status = hIOMgr->pIntfFxns->pfnDevCntrl(hIOMgr->
				 hWmdContext, WMDIOCTL_SETMMUCONFIG, aEProc);
			ulShmBase = hIOMgr->extProcInfo.tyTlb[0].ulGppPhys;
			DBG_Trace(DBG_LEVEL1, "extProcInfo.tyTlb[0].ulGppPhys "
				 "%x \n ", hIOMgr->extProcInfo.tyTlb[0].
				 ulGppPhys);
			ulShmBase += ulShmBaseOffset;
			ulShmBase = (u32)MEM_LinearAddress((void *)ulShmBase,
				    ulMemLength);
			DBC_Assert(ulShmBase != 0);
			if (DSP_SUCCEEDED(status)) {
				status = registerSHMSegs(hIOMgr, hCodMan,
					 aEProc[0].ulGppPa);
				/* Register SM */
			}
		}
	}
	if (DSP_SUCCEEDED(status)) {
		hIOMgr->pSharedMem = (struct SHM *)ulShmBase;
		hIOMgr->pInput = (u8 *)hIOMgr->pSharedMem +
				 sizeof(struct SHM);
		hIOMgr->pOutput = hIOMgr->pInput + (ulShmLength -
				  sizeof(struct SHM))/2;
		hIOMgr->uSMBufSize = hIOMgr->pOutput - hIOMgr->pInput;
		DBG_Trace(DBG_LEVEL3,
			 "hIOMgr: pInput %p pOutput %p ulShmLength %x\n",
			 hIOMgr->pInput, hIOMgr->pOutput, ulShmLength);
		DBG_Trace(DBG_LEVEL3,
			 "pSharedMem %p uSMBufSize %x sizeof(SHM) %x\n",
			 hIOMgr->pSharedMem, hIOMgr->uSMBufSize,
			 sizeof(struct SHM));
		 /*  Set up Shared memory addresses for messaging. */
		hIOMgr->pMsgInputCtrl = (struct MSG *)((u8 *)
					hIOMgr->pSharedMem +
					ulShmLength);
		hIOMgr->pMsgInput = (u8 *)hIOMgr->pMsgInputCtrl +
				    sizeof(struct MSG);
		hIOMgr->pMsgOutputCtrl = (struct MSG *)((u8 *)hIOMgr->
					 pMsgInputCtrl + ulMsgLength / 2);
		hIOMgr->pMsgOutput = (u8 *)hIOMgr->pMsgOutputCtrl +
				     sizeof(struct MSG);
		hMsgMgr->uMaxMsgs = ((u8 *)hIOMgr->pMsgOutputCtrl -
				    hIOMgr->pMsgInput) /
				    sizeof(struct MSG_DSPMSG);
		DBG_Trace(DBG_LEVEL7, "IO MGR SHM details : pSharedMem 0x%x, "
			 "pInput 0x%x, pOutput 0x%x, pMsgInputCtrl 0x%x, "
			 "pMsgInput 0x%x, pMsgOutputCtrl 0x%x, pMsgOutput "
			 "0x%x \n", (u8 *)hIOMgr->pSharedMem,
			 (u8 *)hIOMgr->pInput, (u8 *)hIOMgr->pOutput,
			 (u8 *)hIOMgr->pMsgInputCtrl,
			 (u8 *)hIOMgr->pMsgInput,
			 (u8 *)hIOMgr->pMsgOutputCtrl,
			 (u8 *)hIOMgr->pMsgOutput);
		DBG_Trace(DBG_LEVEL7, "** (proc) MAX MSGS IN SHARED MEMORY: "
			 "0x%x\n", hMsgMgr->uMaxMsgs);
		memset((void *) hIOMgr->pSharedMem, 0, sizeof(struct SHM));
	}
#ifndef DSP_TRACEBUF_DISABLED
	if (DSP_SUCCEEDED(status)) {
		/* Get the start address of trace buffer */
		if (DSP_SUCCEEDED(status)) {
			status = COD_GetSymValue(hCodMan, SYS_PUTCBEG,
				 &hIOMgr->ulTraceBufferBegin);
			if (DSP_FAILED(status))
				status = CHNL_E_NOMEMMAP;

		}
		hIOMgr->ulGPPReadPointer = hIOMgr->ulTraceBufferBegin =
			(ulGppVa + ulSeg1Size + ulPadSize) +
			(hIOMgr->ulTraceBufferBegin - ulDspVa);
		/* Get the end address of trace buffer */
		if (DSP_SUCCEEDED(status)) {
			status = COD_GetSymValue(hCodMan, SYS_PUTCEND,
				 &hIOMgr->ulTraceBufferEnd);
			if (DSP_FAILED(status))
				status = CHNL_E_NOMEMMAP;

		}
		hIOMgr->ulTraceBufferEnd = (ulGppVa + ulSeg1Size + ulPadSize) +
					   (hIOMgr->ulTraceBufferEnd - ulDspVa);
		/* Get the current address of DSP write pointer */
		if (DSP_SUCCEEDED(status)) {
			status = COD_GetSymValue(hCodMan,
				 BRIDGE_SYS_PUTC_current,
				 &hIOMgr->ulTraceBufferCurrent);
			if (DSP_FAILED(status))
				status = CHNL_E_NOMEMMAP;

		}
		hIOMgr->ulTraceBufferCurrent = (ulGppVa + ulSeg1Size +
						ulPadSize) + (hIOMgr->
						ulTraceBufferCurrent - ulDspVa);
		/* Calculate the size of trace buffer */
		if (hIOMgr->pMsg)
			MEM_Free(hIOMgr->pMsg);
		hIOMgr->pMsg = MEM_Alloc(((hIOMgr->ulTraceBufferEnd -
					hIOMgr->ulTraceBufferBegin) *
					hIOMgr->uWordSize) + 2, MEM_NONPAGED);
		if (!hIOMgr->pMsg)
			status = DSP_EMEMORY;

		DBG_Trace(DBG_LEVEL1, "** hIOMgr->pMsg: 0x%x\n", hIOMgr->pMsg);
		hIOMgr->ulDspVa = ulDspVa;
		hIOMgr->ulGppVa = (ulGppVa + ulSeg1Size + ulPadSize);
    }
#endif
	IO_EnableInterrupt(hIOMgr->hWmdContext);
	return status;
}

/*
 *  ======== IO_BufSize ========
 *      Size of shared memory I/O channel.
 */
u32 IO_BufSize(struct IO_MGR *hIOMgr)
{
	DBC_Require(MEM_IsValidHandle(hIOMgr, IO_MGRSIGNATURE));

	return hIOMgr->uSMBufSize;
}

/*
 *  ======== IO_CancelChnl ========
 *      Cancel IO on a given PCPY channel.
 */
void IO_CancelChnl(struct IO_MGR *hIOMgr, u32 ulChnl)
{
	struct IO_MGR *pIOMgr = (struct IO_MGR *)hIOMgr;
	struct SHM *sm;

	DBC_Require(MEM_IsValidHandle(hIOMgr, IO_MGRSIGNATURE));
	sm = hIOMgr->pSharedMem;

	/* Inform DSP that we have no more buffers on this channel:  */
	IO_AndValue(pIOMgr->hWmdContext, struct SHM, sm, hostFreeMask,
		   (~(1 << ulChnl)));

	CHNLSM_InterruptDSP2(pIOMgr->hWmdContext, MBX_PCPY_CLASS);
}

/*
 *  ======== IO_DispatchChnl ========
 *      Proc-copy chanl dispatch.
 */
static void IO_DispatchChnl(IN struct IO_MGR *pIOMgr,
			   IN OUT struct CHNL_OBJECT *pChnl, u32 iMode)
{
	DBC_Require(MEM_IsValidHandle(pIOMgr, IO_MGRSIGNATURE));

	DBG_Trace(DBG_LEVEL3, "Entering IO_DispatchChnl \n");

	/* See if there is any data available for transfer: */
	DBC_Assert(iMode == IO_SERVICE);

	/* Any channel will do for this mode: */
	InputChnl(pIOMgr, pChnl, iMode);
	OutputChnl(pIOMgr, pChnl, iMode);
}

/*
 *  ======== IO_DispatchMsg ========
 *      Performs I/O dispatch on message queues.
 */
static void IO_DispatchMsg(IN struct IO_MGR *pIOMgr, struct MSG_MGR *hMsgMgr)
{
	DBC_Require(MEM_IsValidHandle(pIOMgr, IO_MGRSIGNATURE));

	DBG_Trace(DBG_LEVEL3, "Entering IO_DispatchMsg \n");

	/*  We are performing both input and output processing. */
	InputMsg(pIOMgr, hMsgMgr);
	OutputMsg(pIOMgr, hMsgMgr);
}

/*
 *  ======== IO_DispatchPM ========
 *      Performs I/O dispatch on PM related messages from DSP
 */
static void IO_DispatchPM(struct work_struct *work)
{
       struct IO_MGR *pIOMgr =
                               container_of(work, struct IO_MGR, io_workq);
	DSP_STATUS status;
	u32 pArg[2];

       /*DBC_Require(MEM_IsValidHandle(pIOMgr, IO_MGRSIGNATURE));*/

	DBG_Trace(DBG_LEVEL7, "IO_DispatchPM: Entering IO_DispatchPM : \n");

	/*  Perform Power message processing here  */
	while (pIOMgr->iQuePowerHead != pIOMgr->iQuePowerTail) {
		pArg[0] = *(u32 *)&(pIOMgr->dQuePowerMbxVal[pIOMgr->
			  iQuePowerTail]);
		DBG_Trace(DBG_LEVEL7, "IO_DispatchPM - pArg[0] - 0x%x: \n",
			 pArg[0]);
		/* Send the command to the WMD clk/pwr manager to handle */
		if (pArg[0] ==  MBX_PM_HIBERNATE_EN) {
			DBG_Trace(DBG_LEVEL7, "IO_DispatchPM : Hibernate "
				 "command\n");
			status = pIOMgr->pIntfFxns->pfnDevCntrl(pIOMgr->
				 hWmdContext, WMDIOCTL_PWR_HIBERNATE, pArg);
			if (DSP_FAILED(status)) {
				DBG_Trace(DBG_LEVEL7, "IO_DispatchPM : "
					 "Hibernation command failed\n");
			}
		} else if (pArg[0] == MBX_PM_OPP_REQ) {
			pArg[1] = pIOMgr->pSharedMem->oppRequest.rqstOppPt;
			DBG_Trace(DBG_LEVEL7, "IO_DispatchPM : Value of OPP "
				 "value =0x%x \n", pArg[1]);
			status = pIOMgr->pIntfFxns->pfnDevCntrl(pIOMgr->
				 hWmdContext, WMDIOCTL_CONSTRAINT_REQUEST,
				 pArg);
			if (DSP_FAILED(status)) {
				DBG_Trace(DBG_LEVEL7, "IO_DispatchPM : Failed "
					 "to set constraint = 0x%x \n",
					 pArg[1]);
			}

		} else {
			DBG_Trace(DBG_LEVEL7, "IO_DispatchPM - clock control - "
				 "value of msg = 0x%x: \n", pArg[0]);
			status = pIOMgr->pIntfFxns->pfnDevCntrl(pIOMgr->
				 hWmdContext, WMDIOCTL_CLK_CTRL, pArg);
			if (DSP_FAILED(status)) {
				DBG_Trace(DBG_LEVEL7, "IO_DispatchPM : Failed "
					 "to control the DSP clk = 0x%x \n",
					 *pArg);
			}
		}
		/* increment the tail count here */
		pIOMgr->iQuePowerTail++;
		if (pIOMgr->iQuePowerTail >= MAX_PM_REQS)
			pIOMgr->iQuePowerTail = 0;

	}

}

/*
 *  ======== IO_DPC ========
 *      Deferred procedure call for shared memory channel driver ISR.  Carries
 *      out the dispatch of I/O as a non-preemptible event.It can only be
 *      pre-empted      by an ISR.
 */
void IO_DPC(IN OUT void *pRefData)
{
	struct IO_MGR *pIOMgr = (struct IO_MGR *)pRefData;
	struct CHNL_MGR *pChnlMgr;
	struct MSG_MGR *pMsgMgr;
	struct DEH_MGR *hDehMgr;

	DBC_Require(MEM_IsValidHandle(pIOMgr, IO_MGRSIGNATURE));
	pChnlMgr = pIOMgr->hChnlMgr;
	DEV_GetMsgMgr(pIOMgr->hDevObject, &pMsgMgr);
	DEV_GetDehMgr(pIOMgr->hDevObject, &hDehMgr);
	DBC_Require(MEM_IsValidHandle(pChnlMgr, CHNL_MGRSIGNATURE));
	DBG_Trace(DBG_LEVEL7, "Entering IO_DPC(0x%x)\n", pRefData);
	/* Check value of interrupt register to ensure it is a valid error */
	if ((pIOMgr->wIntrVal > DEH_BASE) && (pIOMgr->wIntrVal < DEH_LIMIT)) {
		/* notify DSP/BIOS exception */
		if (hDehMgr)
			WMD_DEH_Notify(hDehMgr, DSP_SYSERROR, pIOMgr->wIntrVal);

	}
	IO_DispatchChnl(pIOMgr, NULL, IO_SERVICE);
#ifdef CHNL_MESSAGES
	if (pMsgMgr) {
		DBC_Require(MEM_IsValidHandle(pMsgMgr, MSGMGR_SIGNATURE));
		IO_DispatchMsg(pIOMgr, pMsgMgr);
	}
#endif
#ifndef DSP_TRACEBUF_DISABLED
	if (pIOMgr->wIntrVal & MBX_DBG_CLASS) {
		/* notify DSP Trace message */
		if (pIOMgr->wIntrVal & MBX_DBG_SYSPRINTF)
			PrintDSPDebugTrace(pIOMgr);
	}
#endif

#ifndef DSP_TRACEBUF_DISABLED
	PrintDSPDebugTrace(pIOMgr);
#endif
}


/*
 *  ======== IO_ISR ========
 *      Main interrupt handler for the shared memory IO manager.
 *      Calls the WMD's CHNL_ISR to determine if this interrupt is ours, then
 *      schedules a DPC to dispatch I/O.
 */
irqreturn_t IO_ISR(int irq, IN void *pRefData)
{
	struct IO_MGR *hIOMgr = (struct IO_MGR *)pRefData;
	bool fSchedDPC;
       DBC_Require(irq == INT_MAIL_MPU_IRQ);
	DBC_Require(MEM_IsValidHandle(hIOMgr, IO_MGRSIGNATURE));
	DBG_Trace(DBG_LEVEL3, "Entering IO_ISR(0x%x)\n", pRefData);

	/* Call WMD's CHNLSM_ISR() to see if interrupt is ours, and process. */
	if (IO_CALLISR(hIOMgr->hWmdContext, &fSchedDPC, &hIOMgr->wIntrVal)) {
		{
			DBG_Trace(DBG_LEVEL3, "IO_ISR %x\n", hIOMgr->wIntrVal);
			if (hIOMgr->wIntrVal & MBX_PM_CLASS) {
				hIOMgr->dQuePowerMbxVal[hIOMgr->iQuePowerHead] =
					hIOMgr->wIntrVal;
				hIOMgr->iQuePowerHead++;
				if (hIOMgr->iQuePowerHead >= MAX_PM_REQS)
					hIOMgr->iQuePowerHead = 0;

                               queue_work(bridge_workqueue, &hIOMgr->io_workq);
			}
			if (hIOMgr->wIntrVal == MBX_DEH_RESET) {
				DBG_Trace(DBG_LEVEL6, "*** DSP RESET ***\n");
				hIOMgr->wIntrVal = 0;
			} else if (fSchedDPC) {
				/* PROC-COPY defer i/o  */
				DPC_Schedule(hIOMgr->hDPC);
			}
		}
       } else
		/* Ensure that, if WMD didn't claim it, the IRQ is shared. */
		DBC_Ensure(hIOMgr->fSharedIRQ);
       return IRQ_HANDLED;
}

/*
 *  ======== IO_RequestChnl ========
 *  Purpose:
 *      Request chanenel I/O from the DSP. Sets flags in shared memory, then
 *      interrupts the DSP.
 */
void IO_RequestChnl(struct IO_MGR *pIOMgr, struct CHNL_OBJECT *pChnl,
		   u32 iMode, OUT u16 *pwMbVal)
{
	struct CHNL_MGR *pChnlMgr;
	struct SHM *sm;
	DBC_Require(pChnl != NULL);
	DBC_Require(pwMbVal != NULL);
	pChnlMgr = pIOMgr->hChnlMgr;
	sm = pIOMgr->pSharedMem;
	if (iMode == IO_INPUT) {
		/*  Assertion fires if CHNL_AddIOReq() called on a stream
		 * which was cancelled, or attached to a dead board: */
		DBC_Assert((pChnl->dwState == CHNL_STATEREADY) ||
			  (pChnl->dwState == CHNL_STATEEOS));
		/* Indicate to the DSP we have a buffer available for input: */
		IO_OrValue(pIOMgr->hWmdContext, struct SHM, sm, hostFreeMask,
			  (1 << pChnl->uId));
		*pwMbVal = MBX_PCPY_CLASS;
	} else if (iMode == IO_OUTPUT) {
		/*  This assertion fails if CHNL_AddIOReq() was called on a
		 * stream which was cancelled, or attached to a dead board: */
		DBC_Assert((pChnl->dwState & ~CHNL_STATEEOS) ==
			  CHNL_STATEREADY);
		/* Record the fact that we have a buffer available for
		 * output: */
		pChnlMgr->dwOutputMask |= (1 << pChnl->uId);
	} else {
		DBC_Assert(iMode); 	/* Shouldn't get here. */
	}
}

/*
 *  ======== IO_Schedule ========
 *      Schedule DPC for IO.
 */
void IO_Schedule(struct IO_MGR *pIOMgr)
{
	DBC_Require(MEM_IsValidHandle(pIOMgr, IO_MGRSIGNATURE));

	DPC_Schedule(pIOMgr->hDPC);
}

/*
 *  ======== FindReadyOutput ========
 *      Search for a host output channel which is ready to send.  If this is
 *      called as a result of servicing the DPC, then implement a round
 *      robin search; otherwise, this was called by a client thread (via
 *      IO_Dispatch()), so just start searching from the current channel id.
 */
static u32 FindReadyOutput(struct CHNL_MGR *pChnlMgr,
			     struct CHNL_OBJECT *pChnl, u32 dwMask)
{
	u32 uRetval = OUTPUTNOTREADY;
	u32 id, startId;
	u32 shift;

	id = (pChnl != NULL ? pChnl->uId : (pChnlMgr->dwLastOutput + 1));
	id = ((id == CHNL_MAXCHANNELS) ? 0 : id);
	DBC_Assert(id < CHNL_MAXCHANNELS);
	if (dwMask) {
		shift = (1 << id);
		startId = id;
		do {
			if (dwMask & shift) {
				uRetval = id;
				if (pChnl == NULL)
					pChnlMgr->dwLastOutput = id;

				break;
			}
			id = id + 1;
			id = ((id == CHNL_MAXCHANNELS) ? 0 : id);
			shift = (1 << id);
		} while (id != startId);
	}
	DBC_Ensure((uRetval == OUTPUTNOTREADY) || (uRetval < CHNL_MAXCHANNELS));
	return uRetval;
}

/*
 *  ======== InputChnl ========
 *      Dispatch a buffer on an input channel.
 */
static void InputChnl(struct IO_MGR *pIOMgr, struct CHNL_OBJECT *pChnl,
		      u32 iMode)
{
	struct CHNL_MGR *pChnlMgr;
	struct SHM *sm;
	u32 chnlId;
	u32 uBytes;
	struct CHNL_IRP *pChirp = NULL;
	u32 dwArg;
	bool fClearChnl = false;
	bool fNotifyClient = false;

	sm = pIOMgr->pSharedMem;
	pChnlMgr = pIOMgr->hChnlMgr;

	DBG_Trace(DBG_LEVEL3, "> InputChnl\n");

	/* Attempt to perform input.... */
	if (!IO_GetValue(pIOMgr->hWmdContext, struct SHM, sm, inputFull))
		goto func_end;

	uBytes = IO_GetValue(pIOMgr->hWmdContext, struct SHM, sm, inputSize) *
			    pChnlMgr->uWordSize;
	chnlId = IO_GetValue(pIOMgr->hWmdContext, struct SHM, sm, inputId);
	dwArg = IO_GetLong(pIOMgr->hWmdContext, struct SHM, sm, arg);
	if (chnlId >= CHNL_MAXCHANNELS) {
		/* Shouldn't be here: would indicate corrupted SHM. */
		DBC_Assert(chnlId);
		goto func_end;
	}
	pChnl = pChnlMgr->apChannel[chnlId];
	if ((pChnl != NULL) && CHNL_IsInput(pChnl->uMode)) {
		if ((pChnl->dwState & ~CHNL_STATEEOS) == CHNL_STATEREADY) {
                       if (!pChnl->pIORequests)
                               goto func_end;
			/* Get the I/O request, and attempt a transfer:  */
			pChirp = (struct CHNL_IRP *)LST_GetHead(pChnl->
				 pIORequests);
			if (pChirp) {
				pChnl->cIOReqs--;
				DBC_Assert(pChnl->cIOReqs >= 0);
				/* Ensure we don't overflow the client's
				 * buffer: */
				uBytes = min(uBytes, pChirp->cBytes);
				/* Transfer buffer from DSP side: */
				uBytes = ReadData(pIOMgr->hWmdContext,
						pChirp->pHostSysBuf,
						pIOMgr->pInput, uBytes);
				pChnl->cBytesMoved += uBytes;
				pChirp->cBytes = uBytes;
				pChirp->dwArg = dwArg;
				pChirp->status = CHNL_IOCSTATCOMPLETE;
				DBG_Trace(DBG_LEVEL7, "Input Chnl:status= 0x%x "
					 "\n", *((RMS_WORD *)(pChirp->
					 pHostSysBuf)));
				if (uBytes == 0) {
					/* This assertion fails if the DSP
					 * sends EOS more than once on this
					 * channel: */
					DBC_Assert(!(pChnl->dwState &
						  CHNL_STATEEOS));
					 /* Zero bytes indicates EOS. Update
					  * IOC status for this chirp, and also
					  * the channel state: */
					pChirp->status |= CHNL_IOCSTATEOS;
					pChnl->dwState |= CHNL_STATEEOS;
					/* Notify that end of stream has
					 * occurred */
					NTFY_Notify(pChnl->hNtfy,
						   DSP_STREAMDONE);
					DBG_Trace(DBG_LEVEL7, "Input Chnl NTFY "
						 "chnl = 0x%x\n", pChnl);
				}
				/* Tell DSP if no more I/O buffers available: */
                               if (!pChnl->pIORequests)
                                       goto func_end;
				if (LST_IsEmpty(pChnl->pIORequests)) {
					IO_AndValue(pIOMgr->hWmdContext,
						   struct SHM, sm, hostFreeMask,
						   ~(1 << pChnl->uId));
				}
				fClearChnl = true;
				fNotifyClient = true;
			} else {
				/* Input full for this channel, but we have no
				 * buffers available.  The channel must be
				 * "idling". Clear out the physical input
				 * channel.  */
				fClearChnl = true;
			}
		} else {
			/* Input channel cancelled:  clear input channel.  */
			fClearChnl = true;
		}
	} else {
		/* DPC fired after host closed channel: clear input channel. */
		fClearChnl = true;
	}
	if (fClearChnl) {
		/* Indicate to the DSP we have read the input: */
		IO_SetValue(pIOMgr->hWmdContext, struct SHM, sm, inputFull, 0);
		CHNLSM_InterruptDSP2(pIOMgr->hWmdContext, MBX_PCPY_CLASS);
	}
	if (fNotifyClient) {
		/* Notify client with IO completion record:  */
		NotifyChnlComplete(pChnl, pChirp);
	}
func_end:
	DBG_Trace(DBG_LEVEL3, "< InputChnl\n");
}

/*
 *  ======== InputMsg ========
 *      Copies messages from shared memory to the message queues.
 */
static void InputMsg(struct IO_MGR *pIOMgr, struct MSG_MGR *hMsgMgr)
{
	u32 uMsgs;
	u32 i;
	u8 *pMsgInput;
	struct MSG_QUEUE *hMsgQueue;
	struct MSG_FRAME *pMsg;
	struct MSG_DSPMSG msg;
	struct MSG *pCtrl;
	u32 fInputEmpty;
	u32 addr;

	pCtrl = pIOMgr->pMsgInputCtrl;
	/* Get the number of input messages to be read. */
	fInputEmpty = IO_GetValue(pIOMgr->hWmdContext, struct MSG, pCtrl,
				 bufEmpty);
	uMsgs = IO_GetValue(pIOMgr->hWmdContext, struct MSG, pCtrl, size);
	if (fInputEmpty || uMsgs >= hMsgMgr->uMaxMsgs)
		return;

	pMsgInput = pIOMgr->pMsgInput;
	for (i = 0; i < uMsgs; i++) {
		/* Read the next message */
		addr = (u32)&(((struct MSG_DSPMSG *)pMsgInput)->msg.dwCmd);
		msg.msg.dwCmd = ReadExt32BitDspData(pIOMgr->hWmdContext, addr);
		addr = (u32)&(((struct MSG_DSPMSG *)pMsgInput)->msg.dwArg1);
		msg.msg.dwArg1 = ReadExt32BitDspData(pIOMgr->hWmdContext, addr);
		addr = (u32)&(((struct MSG_DSPMSG *)pMsgInput)->msg.dwArg2);
		msg.msg.dwArg2 = ReadExt32BitDspData(pIOMgr->hWmdContext, addr);
		addr = (u32)&(((struct MSG_DSPMSG *)pMsgInput)->dwId);
		msg.dwId = ReadExt32BitDspData(pIOMgr->hWmdContext, addr);
		pMsgInput += sizeof(struct MSG_DSPMSG);
               if (!hMsgMgr->queueList)
                       goto func_end;

		/* Determine which queue to put the message in */
		hMsgQueue = (struct MSG_QUEUE *)LST_First(hMsgMgr->queueList);
		DBG_Trace(DBG_LEVEL7, "InputMsg RECVD: dwCmd=0x%x dwArg1=0x%x "
			 "dwArg2=0x%x dwId=0x%x \n", msg.msg.dwCmd,
			 msg.msg.dwArg1, msg.msg.dwArg2, msg.dwId);
		 /*  Interrupt may occur before shared memory and message
		 *  input locations have been set up. If all nodes were
		 *  cleaned up, hMsgMgr->uMaxMsgs should be 0.  */
               if (hMsgQueue && uMsgs > hMsgMgr->uMaxMsgs)
                       goto func_end;

		while (hMsgQueue != NULL) {
			if (msg.dwId == hMsgQueue->dwId) {
				/* Found it */
				if (msg.msg.dwCmd == RMS_EXITACK) {
					/* The exit message does not get
					 * queued */
					/* Call the node exit notification */
					/* Node handle */ /* status */
					(*hMsgMgr->onExit)((HANDLE)hMsgQueue->
						hArg, msg.msg.dwArg1);
				} else {
					/* Not an exit acknowledgement, queue
					 * the message */
                                       if (!hMsgQueue->msgFreeList)
                                               goto func_end;
					pMsg = (struct MSG_FRAME *)LST_GetHead
						(hMsgQueue->msgFreeList);
                                       if (hMsgQueue->msgUsedList && pMsg) {
						pMsg->msgData = msg;
						LST_PutTail(hMsgQueue->
						      msgUsedList,
						      (struct LST_ELEM *)pMsg);
						NTFY_Notify(hMsgQueue->hNtfy,
							DSP_NODEMESSAGEREADY);
						SYNC_SetEvent(hMsgQueue->
							hSyncEvent);
					} else {
						/* No free frame to copy the
						 * message into */
						DBG_Trace(DBG_LEVEL7, "NO FREE "
							"MSG FRAMES, DISCARDING"
							" MESSAGE\n");
					}
				}
				break;
			}

                       if (!hMsgMgr->queueList || !hMsgQueue)
                               goto func_end;
			hMsgQueue = (struct MSG_QUEUE *)LST_Next(hMsgMgr->
				    queueList, (struct LST_ELEM *)hMsgQueue);
		}
	}
	/* Set the post SWI flag */
	if (uMsgs > 0) {
		/* Tell the DSP we've read the messages */
		IO_SetValue(pIOMgr->hWmdContext, struct MSG, pCtrl, bufEmpty,
			   true);
		IO_SetValue(pIOMgr->hWmdContext, struct MSG, pCtrl, postSWI,
			   true);
		CHNLSM_InterruptDSP2(pIOMgr->hWmdContext, MBX_PCPY_CLASS);
	}
func_end:
       return;

}

/*
 *  ======== NotifyChnlComplete ========
 *  Purpose:
 *      Signal the channel event, notifying the client that I/O has completed.
 */
static void NotifyChnlComplete(struct CHNL_OBJECT *pChnl,
			      struct CHNL_IRP *pChirp)
{
	bool fSignalEvent;

	DBC_Require(MEM_IsValidHandle(pChnl, CHNL_SIGNATURE));
	DBC_Require(pChnl->hSyncEvent != NULL);
	 /*  Note: we signal the channel event only if the queue of IO
	  *  completions is empty.  If it is not empty, the event is sure to be
	  *  signalled by the only IO completion list consumer:
	  *  WMD_CHNL_GetIOC().  */
	fSignalEvent = LST_IsEmpty(pChnl->pIOCompletions);
	/* Enqueue the IO completion info for the client: */
	LST_PutTail(pChnl->pIOCompletions, (struct LST_ELEM *) pChirp);
	pChnl->cIOCs++;
	DBC_Assert(pChnl->cIOCs <= pChnl->cChirps);
	/* Signal the channel event (if not already set) that IO is complete: */
	if (fSignalEvent)
		SYNC_SetEvent(pChnl->hSyncEvent);

	/* Notify that IO is complete */
	NTFY_Notify(pChnl->hNtfy, DSP_STREAMIOCOMPLETION);
}

/*
 *  ======== OutputChnl ========
 *  Purpose:
 *      Dispatch a buffer on an output channel.
 */
static void OutputChnl(struct IO_MGR *pIOMgr, struct CHNL_OBJECT *pChnl,
			u32 iMode)
{
	struct CHNL_MGR *pChnlMgr;
	struct SHM *sm;
	u32 chnlId;
	struct CHNL_IRP *pChirp;
	u32 dwDspFMask;

	pChnlMgr = pIOMgr->hChnlMgr;
	sm = pIOMgr->pSharedMem;
	DBG_Trace(DBG_LEVEL3, "> OutputChnl\n");
	/* Attempt to perform output: */
	if (IO_GetValue(pIOMgr->hWmdContext, struct SHM, sm, outputFull))
		goto func_end;

	if (pChnl && !((pChnl->dwState & ~CHNL_STATEEOS) == CHNL_STATEREADY))
		goto func_end;

	/* Look to see if both a PC and DSP output channel are ready: */
	dwDspFMask = IO_GetValue(pIOMgr->hWmdContext, struct SHM, sm,
				 dspFreeMask);
	chnlId = FindReadyOutput(pChnlMgr, pChnl, (pChnlMgr->dwOutputMask &
				 dwDspFMask));
	if (chnlId == OUTPUTNOTREADY)
		goto func_end;

	pChnl = pChnlMgr->apChannel[chnlId];
       if (!pChnl || !pChnl->pIORequests) {
		/* Shouldn't get here: */
		goto func_end;
	}
	/* Get the I/O request, and attempt a transfer:  */
	pChirp = (struct CHNL_IRP *)LST_GetHead(pChnl->pIORequests);
	if (!pChirp)
		goto func_end;

	pChnl->cIOReqs--;
       if (pChnl->cIOReqs < 0 || !pChnl->pIORequests)
               goto func_end;

	/* Record fact that no more I/O buffers available:  */
	if (LST_IsEmpty(pChnl->pIORequests))
		pChnlMgr->dwOutputMask &= ~(1 << chnlId);

	/* Transfer buffer to DSP side: */
	pChirp->cBytes = WriteData(pIOMgr->hWmdContext, pIOMgr->pOutput,
			pChirp->pHostSysBuf, min(pIOMgr->uSMBufSize, pChirp->
			cBytes));
	pChnl->cBytesMoved += pChirp->cBytes;
	/* Write all 32 bits of arg */
	IO_SetLong(pIOMgr->hWmdContext, struct SHM, sm, arg, pChirp->dwArg);
#if _CHNL_WORDSIZE == 2
	IO_SetValue(pIOMgr->hWmdContext, struct SHM, sm, outputId,
		   (u16)chnlId);
	IO_SetValue(pIOMgr->hWmdContext, struct SHM, sm, outputSize,
		   (u16)(pChirp->cBytes + (pChnlMgr->uWordSize-1)) /
		   (u16)pChnlMgr->uWordSize);
#else
	IO_SetValue(pIOMgr->hWmdContext, struct SHM, sm, outputId, chnlId);
	IO_SetValue(pIOMgr->hWmdContext, struct SHM, sm, outputSize,
		   (pChirp->cBytes + (pChnlMgr->uWordSize - 1)) / pChnlMgr->
		   uWordSize);
#endif
	IO_SetValue(pIOMgr->hWmdContext, struct SHM, sm, outputFull, 1);
	/* Indicate to the DSP we have written the output: */
	CHNLSM_InterruptDSP2(pIOMgr->hWmdContext, MBX_PCPY_CLASS);
	/* Notify client with IO completion record (keep EOS) */
	pChirp->status &= CHNL_IOCSTATEOS;
	NotifyChnlComplete(pChnl, pChirp);
	/* Notify if stream is done. */
	if (pChirp->status & CHNL_IOCSTATEOS)
		NTFY_Notify(pChnl->hNtfy, DSP_STREAMDONE);

func_end:
	DBG_Trace(DBG_LEVEL3, "< OutputChnl\n");
}
/*
 *  ======== OutputMsg ========
 *      Copies messages from the message queues to the shared memory.
 */
static void OutputMsg(struct IO_MGR *pIOMgr, struct MSG_MGR *hMsgMgr)
{
	u32 uMsgs = 0;
	u32 i;
	u8 *pMsgOutput;
	struct MSG_FRAME *pMsg;
	struct MSG *pCtrl;
	u32 fOutputEmpty;
	u32 val;
	u32 addr;

	pCtrl = pIOMgr->pMsgOutputCtrl;

	/* Check if output has been cleared */
	fOutputEmpty = IO_GetValue(pIOMgr->hWmdContext, struct MSG, pCtrl,
				  bufEmpty);
	if (fOutputEmpty) {
		uMsgs = (hMsgMgr->uMsgsPending > hMsgMgr->uMaxMsgs) ?
			 hMsgMgr->uMaxMsgs : hMsgMgr->uMsgsPending;
		pMsgOutput = pIOMgr->pMsgOutput;
		/* Copy uMsgs messages into shared memory */
		for (i = 0; i < uMsgs; i++) {
                       if (!hMsgMgr->msgUsedList) {
                               DBG_Trace(DBG_LEVEL3, "msgUsedList is NULL\n");
                               pMsg = NULL;
                               goto func_end;
                       } else
                               pMsg = (struct MSG_FRAME *)LST_GetHead(
                                       hMsgMgr->msgUsedList);
			if (pMsg != NULL) {
				val = (pMsg->msgData).dwId;
				addr = (u32)&(((struct MSG_DSPMSG *)
					pMsgOutput)->dwId);
				WriteExt32BitDspData(pIOMgr->hWmdContext, addr,
						     val);
				val = (pMsg->msgData).msg.dwCmd;
				addr = (u32)&((((struct MSG_DSPMSG *)
					pMsgOutput)->msg).dwCmd);
				WriteExt32BitDspData(pIOMgr->hWmdContext, addr,
						     val);
				val = (pMsg->msgData).msg.dwArg1;
				addr =
					(u32)&((((struct MSG_DSPMSG *)
					pMsgOutput)->msg).dwArg1);
				WriteExt32BitDspData(pIOMgr->hWmdContext, addr,
						    val);
				val = (pMsg->msgData).msg.dwArg2;
				addr =
					(u32)&((((struct MSG_DSPMSG *)
					pMsgOutput)->msg).dwArg2);
				WriteExt32BitDspData(pIOMgr->hWmdContext, addr,
						    val);
				pMsgOutput += sizeof(struct MSG_DSPMSG);
                               if (!hMsgMgr->msgFreeList)
                                       goto func_end;
				LST_PutTail(hMsgMgr->msgFreeList,
					   (struct LST_ELEM *) pMsg);
				SYNC_SetEvent(hMsgMgr->hSyncEvent);
			} else {
				DBG_Trace(DBG_LEVEL3, "pMsg is NULL\n");
			}
		}

		if (uMsgs > 0) {
			hMsgMgr->uMsgsPending -= uMsgs;
#if _CHNL_WORDSIZE == 2
			IO_SetValue(pIOMgr->hWmdContext, struct MSG, pCtrl,
				   size, (u16)uMsgs);
#else
			IO_SetValue(pIOMgr->hWmdContext, struct MSG, pCtrl,
				   size, uMsgs);
#endif
			IO_SetValue(pIOMgr->hWmdContext, struct MSG, pCtrl,
				   bufEmpty, false);
			/* Set the post SWI flag */
			IO_SetValue(pIOMgr->hWmdContext, struct MSG, pCtrl,
				   postSWI, true);
			/* Tell the DSP we have written the output. */
			CHNLSM_InterruptDSP2(pIOMgr->hWmdContext, MBX_PCPY_CLASS);
		}
	}
func_end:
       return;

}

/*
 *  ======== registerSHMSegs ========
 *  purpose:
 *      Registers GPP SM segment with CMM.
 */
static DSP_STATUS registerSHMSegs(struct IO_MGR *hIOMgr,
				 struct COD_MANAGER *hCodMan,
				 u32 dwGPPBasePA)
{
	DSP_STATUS status = DSP_SOK;
	u32 ulShm0_Base = 0;
	u32 ulShm0_End = 0;
	u32 ulShm0_RsrvdStart = 0;
	u32 ulRsrvdSize = 0;
	u32 ulGppPhys;
	u32 ulDspVirt;
	u32 ulShmSegId0 = 0;
	u32 dwOffset, dwGPPBaseVA, ulDSPSize;

	/* Read address and size info for first SM region.*/
	/* Get start of 1st SM Heap region */
	status = COD_GetSymValue(hCodMan, SHM0_SHARED_BASE_SYM, &ulShm0_Base);
	DBC_Assert(ulShm0_Base != 0);
	/* Get end of 1st SM Heap region */
	if (DSP_SUCCEEDED(status)) {
		/* Get start and length of message part of shared memory */
		status = COD_GetSymValue(hCodMan, SHM0_SHARED_END_SYM,
					 &ulShm0_End);
		DBC_Assert(ulShm0_End != 0);
	}
	/* start of Gpp reserved region */
	if (DSP_SUCCEEDED(status)) {
		/* Get start and length of message part of shared memory */
		status = COD_GetSymValue(hCodMan, SHM0_SHARED_RESERVED_BASE_SYM,
					&ulShm0_RsrvdStart);
		DBG_Trace(DBG_LEVEL1, "***ulShm0_RsrvdStart  0x%x \n",
			 ulShm0_RsrvdStart);
		DBC_Assert(ulShm0_RsrvdStart != 0);
	}
	/* Register with CMM */
	if (DSP_SUCCEEDED(status)) {
		status = DEV_GetCmmMgr(hIOMgr->hDevObject, &hIOMgr->hCmmMgr);
		if (DSP_SUCCEEDED(status)) {
			status = CMM_UnRegisterGPPSMSeg(hIOMgr->hCmmMgr,
				 CMM_ALLSEGMENTS);
			if (DSP_FAILED(status)) {
				DBG_Trace(DBG_LEVEL7, "ERROR - Unable to "
					 "Un-Register SM segments \n");
			}
		} else {
			DBG_Trace(DBG_LEVEL7, "ERROR - Unable to get CMM "
				 "Handle \n");
		}
	}
	/* Register new SM region(s) */
	if (DSP_SUCCEEDED(status) && (ulShm0_End - ulShm0_Base) > 0) {
		/* calc size (bytes) of SM the GPP can alloc from */
		ulRsrvdSize = (ulShm0_End - ulShm0_RsrvdStart + 1) * hIOMgr->
			      uWordSize;
		DBC_Assert(ulRsrvdSize > 0);
		/* calc size of SM DSP can alloc from */
		ulDSPSize = (ulShm0_RsrvdStart - ulShm0_Base) * hIOMgr->
			uWordSize;
		DBC_Assert(ulDSPSize > 0);
		/*  First TLB entry reserved for Bridge SM use.*/
		ulGppPhys = hIOMgr->extProcInfo.tyTlb[0].ulGppPhys;
		/* get size in bytes */
		ulDspVirt = hIOMgr->extProcInfo.tyTlb[0].ulDspVirt * hIOMgr->
			uWordSize;
		 /* Calc byte offset used to convert GPP phys <-> DSP byte
		  * address.*/
		if (dwGPPBasePA > ulDspVirt)
			dwOffset = dwGPPBasePA - ulDspVirt;
		else
			dwOffset = ulDspVirt - dwGPPBasePA;

		DBC_Assert(ulShm0_RsrvdStart * hIOMgr->uWordSize >= ulDspVirt);
		/* calc Gpp phys base of SM region */
		/* Linux - this is actually uncached kernel virtual address*/
		dwGPPBaseVA = ulGppPhys + ulShm0_RsrvdStart * hIOMgr->uWordSize
				- ulDspVirt;
		/* calc Gpp phys base of SM region */
		/* Linux - this is the physical address*/
		dwGPPBasePA = dwGPPBasePA + ulShm0_RsrvdStart * hIOMgr->
			      uWordSize - ulDspVirt;
		 /* Register SM Segment 0.*/
		status = CMM_RegisterGPPSMSeg(hIOMgr->hCmmMgr, dwGPPBasePA,
			 ulRsrvdSize, dwOffset, (dwGPPBasePA > ulDspVirt) ?
			 CMM_ADDTODSPPA : CMM_SUBFROMDSPPA,
			 (u32)(ulShm0_Base * hIOMgr->uWordSize),
			 ulDSPSize, &ulShmSegId0, dwGPPBaseVA);
		if (DSP_FAILED(status)) {
			DBG_Trace(DBG_LEVEL7, "ERROR - Failed to register SM "
				 "Seg 0 \n");
		}
		/* first SM region is segId = 1 */
		DBC_Assert(ulShmSegId0 == 1);
	}
	return status;
}

/*
 *  ======== ReadData ========
 *      Copies buffers from the shared memory to the host buffer.
 */
static u32 ReadData(struct WMD_DEV_CONTEXT *hDevContext, void *pDest,
		     void *pSrc, u32 uSize)
{
	memcpy(pDest, pSrc, uSize);
	return uSize;
}

/*
 *  ======== WriteData ========
 *      Copies buffers from the host side buffer to the shared memory.
 */
static u32 WriteData(struct WMD_DEV_CONTEXT *hDevContext, void *pDest,
		       void *pSrc, u32 uSize)
{
	memcpy(pDest, pSrc, uSize);
	return uSize;
}

/* ZCPY IO routines. */
void IO_IntrDSP2(IN struct IO_MGR *pIOMgr, IN u16 wMbVal)
{
	CHNLSM_InterruptDSP2(pIOMgr->hWmdContext, wMbVal);
}

/*
 *  ======== IO_SHMcontrol ========
 *      Sets the requested SHM setting.
 */
DSP_STATUS IO_SHMsetting(IN struct IO_MGR *hIOMgr, IN enum SHM_DESCTYPE desc,
			 IN void *pArgs)
{
#ifdef CONFIG_BRIDGE_DVFS
	u32 i;
	struct dspbridge_platform_data *pdata =
				omap_dspbridge_dev->dev.platform_data;

	switch (desc) {
	case SHM_CURROPP:
		/* Update the shared memory with requested OPP information */
		if (pArgs != NULL)
			hIOMgr->pSharedMem->oppTableStruct.currOppPt =
				*(u32 *)pArgs;
		else
			return DSP_EFAIL;
		break;
	case SHM_OPPINFO:
		/* Update the shared memory with the voltage, frequency,
				   min and max frequency values for an OPP */
		for (i = 0; i <= dsp_max_opps; i++) {
			hIOMgr->pSharedMem->oppTableStruct.oppPoint[i].voltage =
				vdd1_dsp_freq[i][0];
			DBG_Trace(DBG_LEVEL5, "OPP shared memory -voltage: "
				 "%d\n", hIOMgr->pSharedMem->oppTableStruct.
				 oppPoint[i].voltage);
			hIOMgr->pSharedMem->oppTableStruct.oppPoint[i].
				frequency = vdd1_dsp_freq[i][1];
			DBG_Trace(DBG_LEVEL5, "OPP shared memory -frequency: "
				 "%d\n", hIOMgr->pSharedMem->oppTableStruct.
				 oppPoint[i].frequency);
			hIOMgr->pSharedMem->oppTableStruct.oppPoint[i].minFreq =
				vdd1_dsp_freq[i][2];
			DBG_Trace(DBG_LEVEL5, "OPP shared memory -min value: "
				 "%d\n", hIOMgr->pSharedMem->oppTableStruct.
				  oppPoint[i].minFreq);
			hIOMgr->pSharedMem->oppTableStruct.oppPoint[i].maxFreq =
				vdd1_dsp_freq[i][3];
			DBG_Trace(DBG_LEVEL5, "OPP shared memory -max value: "
				 "%d\n", hIOMgr->pSharedMem->oppTableStruct.
				 oppPoint[i].maxFreq);
		}
		hIOMgr->pSharedMem->oppTableStruct.numOppPts = dsp_max_opps;
		DBG_Trace(DBG_LEVEL5, "OPP shared memory - max OPP number: "
			 "%d\n", hIOMgr->pSharedMem->oppTableStruct.numOppPts);
		/* Update the current OPP number */
		if (pdata->dsp_get_opp)
			i = (*pdata->dsp_get_opp)();
		hIOMgr->pSharedMem->oppTableStruct.currOppPt = i;
		DBG_Trace(DBG_LEVEL7, "OPP value programmed to shared memory: "
			 "%d\n", i);
		break;
	case SHM_GETOPP:
		/* Get the OPP that DSP has requested */
		*(u32 *)pArgs = hIOMgr->pSharedMem->oppRequest.rqstOppPt;
		break;
	default:
		break;
	}
#endif
	return DSP_SOK;
}

/*
 *  ======== WMD_IO_GetProcLoad ========
 *      Gets the Processor's Load information
 */
DSP_STATUS WMD_IO_GetProcLoad(IN struct IO_MGR *hIOMgr,
			     OUT struct DSP_PROCLOADSTAT *pProcStat)
{
	pProcStat->uCurrLoad = hIOMgr->pSharedMem->loadMonInfo.currDspLoad;
	pProcStat->uPredictedLoad = hIOMgr->pSharedMem->loadMonInfo.predDspLoad;
	pProcStat->uCurrDspFreq = hIOMgr->pSharedMem->loadMonInfo.currDspFreq;
	pProcStat->uPredictedFreq = hIOMgr->pSharedMem->loadMonInfo.predDspFreq;

	DBG_Trace(DBG_LEVEL4, "Curr Load =%d, Pred Load = %d, Curr Freq = %d, "
			     "Pred Freq = %d\n", pProcStat->uCurrLoad,
			     pProcStat->uPredictedLoad, pProcStat->uCurrDspFreq,
			     pProcStat->uPredictedFreq);
	return DSP_SOK;
}

#ifndef DSP_TRACEBUF_DISABLED
void PrintDSPDebugTrace(struct IO_MGR *hIOMgr)
{
	u32 ulNewMessageLength = 0, ulGPPCurPointer;

       GT_0trace(dsp_trace_mask, GT_ENTER, "Entering PrintDSPDebugTrace\n");

	while (true) {
		/* Get the DSP current pointer */
		ulGPPCurPointer = *(u32 *) (hIOMgr->ulTraceBufferCurrent);
		ulGPPCurPointer = hIOMgr->ulGppVa + (ulGPPCurPointer -
				  hIOMgr->ulDspVa);

		/* No new debug messages available yet */
		if (ulGPPCurPointer == hIOMgr->ulGPPReadPointer)
			break;

		/* Continuous data */
		else if (ulGPPCurPointer > hIOMgr->ulGPPReadPointer) {
			ulNewMessageLength = ulGPPCurPointer - hIOMgr->
					     ulGPPReadPointer;

			memcpy(hIOMgr->pMsg, (char *)hIOMgr->ulGPPReadPointer,
			       ulNewMessageLength);
			hIOMgr->pMsg[ulNewMessageLength] = '\0';
			/* Advance the GPP trace pointer to DSP current
			 * pointer */
			hIOMgr->ulGPPReadPointer += ulNewMessageLength;
			/* Print the trace messages */
                       GT_0trace(dsp_trace_mask, GT_1CLASS, hIOMgr->pMsg);
		}
		/* Handle trace buffer wraparound */
		else if (ulGPPCurPointer < hIOMgr->ulGPPReadPointer) {
			memcpy(hIOMgr->pMsg, (char *)hIOMgr->ulGPPReadPointer,
				hIOMgr->ulTraceBufferEnd -
				hIOMgr->ulGPPReadPointer);
			ulNewMessageLength = ulGPPCurPointer -
				hIOMgr->ulTraceBufferBegin;
			memcpy(&hIOMgr->pMsg[hIOMgr->ulTraceBufferEnd -
				hIOMgr->ulGPPReadPointer],
				(char *)hIOMgr->ulTraceBufferBegin,
				ulNewMessageLength);
			hIOMgr->pMsg[hIOMgr->ulTraceBufferEnd -
				hIOMgr->ulGPPReadPointer +
				ulNewMessageLength] = '\0';
			/* Advance the GPP trace pointer to DSP current
			 * pointer */
			hIOMgr->ulGPPReadPointer = hIOMgr->ulTraceBufferBegin +
						   ulNewMessageLength;
			/* Print the trace messages */
                       GT_0trace(dsp_trace_mask, GT_1CLASS, hIOMgr->pMsg);
		}
	}
}
#endif

/*
 *  ======== PackTraceBuffer ========
 *      Removes extra nulls from the trace buffer returned from the DSP.
 *      Works even on buffers that already are packed (null removed); but has
 *      one bug in that case -- loses the last character (replaces with '\0').
 *      Continues through conversion for full set of nBytes input characters.
 *  Parameters:
 *    lpBuf:            Pointer to input/output buffer
 *    nBytes:           Number of characters in the buffer
 *    ulNumWords:       Number of DSP words in the buffer.  Indicates potential
 *                      number of extra carriage returns to generate.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EMEMORY:    Unable to allocate memory.
 *  Requires:
 *      lpBuf must be a fully allocated writable block of at least nBytes.
 *      There are no more than ulNumWords extra characters needed (the number of
 *      linefeeds minus the number of NULLS in the input buffer).
 */
#if (defined(DEBUG) || defined(DDSP_DEBUG_PRODUCT)) && GT_TRACE
static DSP_STATUS PackTraceBuffer(char *lpBuf, u32 nBytes, u32 ulNumWords)
{
       DSP_STATUS status = DSP_SOK;
       char *lpTmpBuf;
       char *lpBufStart;
       char *lpTmpStart;
       u32 nCnt;
       char thisChar;

       /* tmp workspace, 1 KB longer than input buf */
       lpTmpBuf = MEM_Calloc((nBytes + ulNumWords), MEM_PAGED);
       if (lpTmpBuf == NULL) {
               DBG_Trace(DBG_LEVEL7, "PackTrace buffer:OutofMemory \n");
               status = DSP_EMEMORY;
       }

       if (DSP_SUCCEEDED(status)) {
               lpBufStart = lpBuf;
               lpTmpStart = lpTmpBuf;
               for (nCnt = nBytes; nCnt > 0; nCnt--) {
                       thisChar = *lpBuf++;
                       switch (thisChar) {
                       case '\0':      /* Skip null bytes */
                       break;
                       case '\n':      /* Convert \n to \r\n */
                       /* NOTE: do not reverse order; Some OS */
                       /* editors control doesn't understand "\n\r" */
                       *lpTmpBuf++ = '\r';
                       *lpTmpBuf++ = '\n';
                       break;
                       default:        /* Copy in the actual ascii byte */
                       *lpTmpBuf++ = thisChar;
                       break;
                       }
               }
               *lpTmpBuf = '\0';    /* Make sure tmp buf is null terminated */
               /* Cut output down to input buf size */
               strncpy(lpBufStart, lpTmpStart, nBytes);
               /*Make sure output is null terminated */
               lpBufStart[nBytes - 1] = '\0';
               MEM_Free(lpTmpStart);
       }

       return status;
}
#endif    /* (defined(DEBUG) || defined(DDSP_DEBUG_PRODUCT)) && GT_TRACE */

/*
 *  ======== PrintDspTraceBuffer ========
 *      Prints the trace buffer returned from the DSP (if DBG_Trace is enabled).
 *  Parameters:
 *    hDehMgr:          Handle to DEH manager object
 *                      number of extra carriage returns to generate.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EMEMORY:    Unable to allocate memory.
 *  Requires:
 *      hDehMgr muse be valid. Checked in WMD_DEH_Notify.
 */
DSP_STATUS PrintDspTraceBuffer(struct WMD_DEV_CONTEXT *hWmdContext)
{
       DSP_STATUS status = DSP_SOK;

#if (defined(DEBUG) || defined(DDSP_DEBUG_PRODUCT)) && GT_TRACE
       struct COD_MANAGER *hCodMgr;
       u32 ulTraceEnd;
       u32 ulTraceBegin;
       u32 ulNumBytes = 0;
       u32 ulNumWords = 0;
       u32 ulWordSize = 2;
       CONST u32 uMaxSize = 512;
       char *pszBuf;
       u16 *lpszBuf;

       struct WMD_DEV_CONTEXT *pWmdContext = (struct WMD_DEV_CONTEXT *)
                                               hWmdContext;
       struct WMD_DRV_INTERFACE *pIntfFxns;
       struct DEV_OBJECT *pDevObject = (struct DEV_OBJECT *)
                                       pWmdContext->hDevObject;

       status = DEV_GetCodMgr(pDevObject, &hCodMgr);
       if (DSP_FAILED(status))
               GT_0trace(dsp_trace_mask, GT_2CLASS,
               "PrintDspTraceBuffer: Failed on DEV_GetCodMgr.\n");

       if (DSP_SUCCEEDED(status)) {
               /* Look for SYS_PUTCBEG/SYS_PUTCEND: */
               status = COD_GetSymValue(hCodMgr, COD_TRACEBEG, &ulTraceBegin);
               GT_1trace(dsp_trace_mask, GT_2CLASS,
                       "PrintDspTraceBuffer: ulTraceBegin Value 0x%x\n",
                       ulTraceBegin);
               if (DSP_FAILED(status))
                       GT_0trace(dsp_trace_mask, GT_2CLASS,
                               "PrintDspTraceBuffer: Failed on "
                               "COD_GetSymValue.\n");
       }
       if (DSP_SUCCEEDED(status)) {
               status = COD_GetSymValue(hCodMgr, COD_TRACEEND, &ulTraceEnd);
               GT_1trace(dsp_trace_mask, GT_2CLASS,
                       "PrintDspTraceBuffer: ulTraceEnd Value 0x%x\n",
                       ulTraceEnd);
               if (DSP_FAILED(status))
                       GT_0trace(dsp_trace_mask, GT_2CLASS,
                               "PrintDspTraceBuffer: Failed on "
                               "COD_GetSymValue.\n");
       }
       if (DSP_SUCCEEDED(status)) {
               ulNumBytes = (ulTraceEnd - ulTraceBegin) * ulWordSize;
               /*  If the chip type is 55 then the addresses will be
               *  byte addresses; convert them to word addresses.  */
               if (ulNumBytes > uMaxSize)
                       ulNumBytes = uMaxSize;

               /* make sure the data we request fits evenly */
               ulNumBytes = (ulNumBytes / ulWordSize) * ulWordSize;
               GT_1trace(dsp_trace_mask, GT_2CLASS, "PrintDspTraceBuffer: "
                       "ulNumBytes 0x%x\n", ulNumBytes);
               ulNumWords = ulNumBytes * ulWordSize;
               GT_1trace(dsp_trace_mask, GT_2CLASS, "PrintDspTraceBuffer: "
                       "ulNumWords 0x%x\n", ulNumWords);
               status = DEV_GetIntfFxns(pDevObject, &pIntfFxns);
       }

       if (DSP_SUCCEEDED(status)) {
               pszBuf = MEM_Calloc(uMaxSize, MEM_NONPAGED);
               lpszBuf = MEM_Calloc(ulNumBytes * 2, MEM_NONPAGED);
               if (pszBuf != NULL) {
                       /* Read bytes from the DSP trace buffer... */
                       status = (*pIntfFxns->pfnBrdRead)(hWmdContext,
                               (u8 *)pszBuf, (u32)ulTraceBegin,
                               ulNumBytes, 0);
                       if (DSP_FAILED(status))
                               GT_0trace(dsp_trace_mask, GT_2CLASS,
                                       "PrintDspTraceBuffer: "
                                       "Failed to Read Trace Buffer.\n");

                       if (DSP_SUCCEEDED(status)) {
                               /* Pack and do newline conversion */
                               GT_0trace(dsp_trace_mask, GT_2CLASS,
                                       "PrintDspTraceBuffer: "
                                       "before pack and unpack.\n");
                               PackTraceBuffer(pszBuf, ulNumBytes, ulNumWords);
                               GT_1trace(dsp_trace_mask, GT_1CLASS,
                                       "DSP Trace Buffer:\n%s\n", pszBuf);
                       }
                       MEM_Free(pszBuf);
                       MEM_Free(lpszBuf);
               } else {
                       GT_0trace(dsp_trace_mask, GT_2CLASS,
                               "PrintDspTraceBuffer: Failed to "
                               "allocate trace buffer.\n");
                       status = DSP_EMEMORY;
               }
       }
#endif
       return status;
}

void IO_SM_init(void)
{

       GT_create(&dsp_trace_mask, "DT"); /* DSP Trace Mask */

}
