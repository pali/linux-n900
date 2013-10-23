/*
 * strm.c
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
 *  ======== strm.c ========
 *  Description:
 *      DSP/BIOS Bridge Stream Manager.
 *
 *  Public Functions:
 *      STRM_AllocateBuffer
 *      STRM_Close
 *      STRM_Create
 *      STRM_Delete
 *      STRM_Exit
 *      STRM_FreeBuffer
 *      STRM_GetEventHandle
 *      STRM_GetInfo
 *      STRM_Idle
 *      STRM_Init
 *      STRM_Issue
 *      STRM_Open
 *      STRM_PrepareBuffer
 *      STRM_Reclaim
 *      STRM_RegisterNotify
 *      STRM_Select
 *      STRM_UnprepareBuffer
 *
 *  Notes:
 *
 *! Revision History:
 *! =================
 *! 18-Feb-2003 vp  Code review updates.
 *! 18-Oct-2002 vp  Ported to Linux platform.
 *! 13-Mar-2002 map    pStrm init'd to NULL in STRM_Open to prevent error
 *! 12-Mar-2002 map Changed return var to WSX "wStatus" instead of "status"
 *!		    in DEV and CMM function calls to avoid confusion.
 *!		    Return DSP_SOK instead of S_OK from API fxns.
 *! 12-Mar-2002 map    Changed FAILED(..) to DSP_FAILED(..)
 *! 25-Jan-2002 ag  Allow neg seg ids(e.g. DSP_SHMSEG0) to denote SM.
 *! 15-Nov-2001 ag  Added STRMMODE & SM for DMA/ZCopy streaming.
 *!		 Changed DSP_STREAMINFO to STRM_INFO in STRM_GetInfo().
 *!		 Use strm timeout value for dma flush timeout.
 *! 09-May-2001 jeh Code review cleanup.
 *! 06-Feb-2001 kc  Updated DBC_Ensure in STRM_Select to check timeout.
 *! 23-Oct-2000 jeh Allow NULL STRM_ATTRS passed to STRM_Open() for DLL
 *!		 tests to pass.
 *! 25-Sep-2000 jeh Created.
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
#include <dspbridge/mem.h>
#include <dspbridge/sync.h>

/*  ----------------------------------- Mini Driver */
#include <dspbridge/wmd.h>

/*  ----------------------------------- Resource Manager */
#include <dspbridge/nodepriv.h>

/*  ----------------------------------- Others */
#include <dspbridge/cmm.h>

/*  ----------------------------------- This */
#include <dspbridge/strm.h>

#ifndef RES_CLEANUP_DISABLE
#include <dspbridge/cfg.h>
#include <dspbridge/dbreg.h>
#include <dspbridge/resourcecleanup.h>
#endif

/*  ----------------------------------- Defines, Data Structures, Typedefs */
#define STRM_SIGNATURE      0x4d525453	/* "MRTS" */
#define STRMMGR_SIGNATURE   0x5254534d	/* "RTSM" */

#define DEFAULTTIMEOUT      10000
#define DEFAULTNUMBUFS      2

/*
 *  ======== STRM_MGR ========
 *  The STRM_MGR contains device information needed to open the underlying
 *  channels of a stream.
 */
struct STRM_MGR {
	u32 dwSignature;
	struct DEV_OBJECT *hDev;	/* Device for this processor */
	struct CHNL_MGR *hChnlMgr;	/* Channel manager */
	struct WMD_DRV_INTERFACE *pIntfFxns;	/* Function interface to WMD */
	struct SYNC_CSOBJECT *hSync;	/* For critical sections */
} ;

/*
 *  ======== STRM_OBJECT ========
 *  This object is allocated in STRM_Open().
 */
 struct STRM_OBJECT {
	u32 dwSignature;
	struct STRM_MGR *hStrmMgr;
	struct CHNL_OBJECT *hChnl;
	u32 uDir;		/* DSP_TONODE or DSP_FROMNODE */
	u32 uTimeout;
	u32 uNumBufs;		/* Max # of bufs allowed in stream */
	u32 uNBufsInStrm;	/* Current # of bufs in stream */
	u32 ulNBytes;		/* bytes transferred since idled */
	enum DSP_STREAMSTATE strmState;	/* STREAM_IDLE, STREAM_READY, ... */
	HANDLE hUserEvent;	/* Saved for STRM_GetInfo() */
	enum DSP_STRMMODE lMode;	/* STRMMODE_[PROCCOPY][ZEROCOPY]... */
	u32 uDMAChnlId;	/* DMA chnl id */
	u32 uDMAPriority;	/* DMA priority:DMAPRI_[LOW][HIGH] */
	u32 uSegment;		/* >0 is SM segment.=0 is local heap */
	u32 uAlignment;	/* Alignment for stream bufs */
	struct CMM_XLATOROBJECT *hXlator;  /* Stream's SM address translator */
} ;

/*  ----------------------------------- Globals */
#if GT_TRACE
static struct GT_Mask STRM_debugMask = { NULL, NULL };	/* GT trace variable */
#endif
static u32 cRefs;		/* module reference count */

/*  ----------------------------------- Function Prototypes */
static DSP_STATUS DeleteStrm(struct STRM_OBJECT *hStrm);
static void DeleteStrmMgr(struct STRM_MGR *hStrmMgr);

/*
 *  ======== STRM_AllocateBuffer ========
 *  Purpose:
 *      Allocates buffers for a stream.
 */
DSP_STATUS STRM_AllocateBuffer(struct STRM_OBJECT *hStrm, u32 uSize,
				OUT u8 **apBuffer, u32 uNumBufs)
{
	DSP_STATUS status = DSP_SOK;
	u32 uAllocated = 0;
	u32 i;
	#ifndef RES_CLEANUP_DISABLE
	DSP_STATUS res_status = DSP_SOK;
       u32                  hProcess;
	HANDLE	     pCtxt = NULL;
	HANDLE	     hDrvObject;
	HANDLE hSTRMRes;
	#endif
	DBC_Require(cRefs > 0);
	DBC_Require(apBuffer != NULL);

	GT_4trace(STRM_debugMask, GT_ENTER, "STRM_AllocateBuffer: hStrm: 0x%x\t"
		 "uSize: 0x%x\tapBuffer: 0x%x\tuNumBufs: 0x%x\n",
		 hStrm, uSize, apBuffer, uNumBufs);
	if (MEM_IsValidHandle(hStrm, STRM_SIGNATURE)) {
		/*
		 * Allocate from segment specified at time of stream open.
		 */
		if (uSize == 0)
			status = DSP_ESIZE;

	}
	if (DSP_FAILED(status)) {
		status = DSP_EHANDLE;
		goto func_end;
	}
	for (i = 0; i < uNumBufs; i++) {
		DBC_Assert(hStrm->hXlator != NULL);
		(void)CMM_XlatorAllocBuf(hStrm->hXlator, &apBuffer[i], uSize);
		if (apBuffer[i] == NULL) {
			GT_0trace(STRM_debugMask, GT_7CLASS,
				 "STRM_AllocateBuffer: "
				 "DSP_FAILED to alloc shared memory.\n");
			status = DSP_EMEMORY;
			uAllocated = i;
			break;
		}
	}
	if (DSP_FAILED(status))
		STRM_FreeBuffer(hStrm, apBuffer, uAllocated);

#ifndef RES_CLEANUP_DISABLE
	if (DSP_FAILED(status))
		goto func_end;

       /* Return PID instead of process handle */
       hProcess = current->pid;

	res_status = CFG_GetObject((u32 *)&hDrvObject, REG_DRV_OBJECT);
	if (DSP_FAILED(res_status))
		goto func_end;

       DRV_GetProcContext(hProcess, (struct DRV_OBJECT *)hDrvObject,
			 &pCtxt, NULL, 0);
	if (pCtxt != NULL) {
		if (DRV_GetSTRMResElement(hStrm, &hSTRMRes, pCtxt) !=
		   DSP_ENOTFOUND) {
			DRV_ProcUpdateSTRMRes(uNumBufs, hSTRMRes, pCtxt);
		}
	}
#endif
func_end:
	return status;
}

/*
 *  ======== STRM_Close ========
 *  Purpose:
 *      Close a stream opened with STRM_Open().
 */
DSP_STATUS STRM_Close(struct STRM_OBJECT *hStrm)
{
	struct WMD_DRV_INTERFACE *pIntfFxns;
	struct CHNL_INFO chnlInfo;
	DSP_STATUS status = DSP_SOK;


#ifndef RES_CLEANUP_DISABLE
    u32                      hProcess;
    HANDLE	      pCtxt = NULL;
    HANDLE	      hDrvObject;
    HANDLE	      hSTRMRes;
    DSP_STATUS	  res_status = DSP_SOK;
#endif


	DBC_Require(cRefs > 0);

	GT_1trace(STRM_debugMask, GT_ENTER, "STRM_Close: hStrm: 0x%x\n", hStrm);

	if (!MEM_IsValidHandle(hStrm, STRM_SIGNATURE)) {
		status = DSP_EHANDLE;
	} else {
		/* Have all buffers been reclaimed? If not, return
		 * DSP_EPENDING */
		pIntfFxns = hStrm->hStrmMgr->pIntfFxns;
		status = (*pIntfFxns->pfnChnlGetInfo) (hStrm->hChnl, &chnlInfo);
		DBC_Assert(DSP_SUCCEEDED(status));

		if (chnlInfo.cIOCs > 0 || chnlInfo.cIOReqs > 0) {
			status = DSP_EPENDING;
		} else {

			status = DeleteStrm(hStrm);

			if (DSP_FAILED(status)) {
				/* we already validated the handle. */
				DBC_Assert(status != DSP_EHANDLE);

				/* make sure we return a documented result */
				status = DSP_EFAIL;
			}
		}
	}
#ifndef RES_CLEANUP_DISABLE
	if (DSP_FAILED(status))
		goto func_end;

	/* Update the node and stream resource status */
       /* Return PID instead of process handle */
       hProcess = current->pid;

	res_status = CFG_GetObject((u32 *)&hDrvObject, REG_DRV_OBJECT);
	if (DSP_FAILED(res_status))
		goto func_end;

       DRV_GetProcContext(hProcess, (struct DRV_OBJECT *)hDrvObject,
			 &pCtxt, NULL, 0);
	if (pCtxt != NULL) {
		if (DRV_GetSTRMResElement(hStrm, &hSTRMRes, pCtxt) !=
		   DSP_ENOTFOUND) {
			DRV_ProcRemoveSTRMResElement(hSTRMRes, pCtxt);
		}
	}
func_end:
#endif
	DBC_Ensure(status == DSP_SOK || status == DSP_EHANDLE ||
		  status == DSP_EPENDING || status == DSP_EFAIL);

	return status;
}

/*
 *  ======== STRM_Create ========
 *  Purpose:
 *      Create a STRM manager object.
 */
DSP_STATUS STRM_Create(OUT struct STRM_MGR **phStrmMgr, struct DEV_OBJECT *hDev)
{
	struct STRM_MGR *pStrmMgr;
	DSP_STATUS status = DSP_SOK;

	DBC_Require(cRefs > 0);
	DBC_Require(phStrmMgr != NULL);
	DBC_Require(hDev != NULL);

	GT_2trace(STRM_debugMask, GT_ENTER, "STRM_Create: phStrmMgr: "
		 "0x%x\thDev: 0x%x\n", phStrmMgr, hDev);
	*phStrmMgr = NULL;
	/* Allocate STRM manager object */
	MEM_AllocObject(pStrmMgr, struct STRM_MGR, STRMMGR_SIGNATURE);
	if (pStrmMgr == NULL) {
		status = DSP_EMEMORY;
		GT_0trace(STRM_debugMask, GT_6CLASS, "STRM_Create: "
			 "MEM_AllocObject() failed!\n ");
	} else {
		pStrmMgr->hDev = hDev;
	}
	/* Get Channel manager and WMD function interface */
	if (DSP_SUCCEEDED(status)) {
		status = DEV_GetChnlMgr(hDev, &(pStrmMgr->hChnlMgr));
		if (DSP_SUCCEEDED(status)) {
			(void) DEV_GetIntfFxns(hDev, &(pStrmMgr->pIntfFxns));
			DBC_Assert(pStrmMgr->pIntfFxns != NULL);
		} else {
			GT_1trace(STRM_debugMask, GT_6CLASS, "STRM_Create: "
				 "Failed to get channel manager! status = "
				 "0x%x\n", status);
		}
	}
	if (DSP_SUCCEEDED(status))
		status = SYNC_InitializeCS(&pStrmMgr->hSync);

	if (DSP_SUCCEEDED(status))
		*phStrmMgr = pStrmMgr;
	else
		DeleteStrmMgr(pStrmMgr);

	DBC_Ensure(DSP_SUCCEEDED(status) &&
		  (MEM_IsValidHandle((*phStrmMgr), STRMMGR_SIGNATURE) ||
		  (DSP_FAILED(status) && *phStrmMgr == NULL)));

	return status;
}

/*
 *  ======== STRM_Delete ========
 *  Purpose:
 *      Delete the STRM Manager Object.
 */
void STRM_Delete(struct STRM_MGR *hStrmMgr)
{
	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(hStrmMgr, STRMMGR_SIGNATURE));

	GT_1trace(STRM_debugMask, GT_ENTER, "STRM_Delete: hStrmMgr: 0x%x\n",
		 hStrmMgr);

	DeleteStrmMgr(hStrmMgr);

	DBC_Ensure(!MEM_IsValidHandle(hStrmMgr, STRMMGR_SIGNATURE));
}

/*
 *  ======== STRM_Exit ========
 *  Purpose:
 *      Discontinue usage of STRM module.
 */
void STRM_Exit(void)
{
	DBC_Require(cRefs > 0);

	cRefs--;

	GT_1trace(STRM_debugMask, GT_5CLASS,
		 "Entered STRM_Exit, ref count:  0x%x\n", cRefs);

	DBC_Ensure(cRefs >= 0);
}

/*
 *  ======== STRM_FreeBuffer ========
 *  Purpose:
 *      Frees the buffers allocated for a stream.
 */
DSP_STATUS STRM_FreeBuffer(struct STRM_OBJECT *hStrm, u8 **apBuffer,
			  u32 uNumBufs)
{
	DSP_STATUS status = DSP_SOK;
	u32 i = 0;

	#ifndef RES_CLEANUP_DISABLE
	DSP_STATUS res_status = DSP_SOK;
       u32                  hProcess;
	HANDLE	     pCtxt = NULL;
	HANDLE	     hDrvObject;
	HANDLE 		    hSTRMRes = NULL;
	#endif
	DBC_Require(cRefs > 0);
	DBC_Require(apBuffer != NULL);

	GT_3trace(STRM_debugMask, GT_ENTER, "STRM_FreeBuffer: hStrm: 0x%x\t"
		 "apBuffer: 0x%x\tuNumBufs: 0x%x\n", hStrm, apBuffer, uNumBufs);

	if (!MEM_IsValidHandle(hStrm, STRM_SIGNATURE))
		status = DSP_EHANDLE;

	if (DSP_SUCCEEDED(status)) {
		for (i = 0; i < uNumBufs; i++) {
			DBC_Assert(hStrm->hXlator != NULL);
			status = CMM_XlatorFreeBuf(hStrm->hXlator, apBuffer[i]);
			if (DSP_FAILED(status)) {
				GT_0trace(STRM_debugMask, GT_7CLASS,
					 "STRM_FreeBuffer: DSP_FAILED"
					 " to free shared memory.\n");
				break;
			}
			apBuffer[i] = NULL;
		}
	}
#ifndef RES_CLEANUP_DISABLE
	/* Update the node and stream resource status */
       /* Return PID instead of process handle */
       hProcess = current->pid;

	res_status = CFG_GetObject((u32 *)&hDrvObject, REG_DRV_OBJECT);
	if (DSP_SUCCEEDED(res_status)) {
               DRV_GetProcContext(hProcess,
				 (struct DRV_OBJECT *)hDrvObject, &pCtxt,
				 NULL, 0);
		if (pCtxt != NULL) {
			if (DRV_GetSTRMResElement(hStrm, hSTRMRes, pCtxt) !=
			   DSP_ENOTFOUND) {
				DRV_ProcUpdateSTRMRes(uNumBufs-i, hSTRMRes,
						     pCtxt);
			}
		}
	}
#endif
	return status;
}

/*
 *  ======== STRM_GetInfo ========
 *  Purpose:
 *      Retrieves information about a stream.
 */
DSP_STATUS STRM_GetInfo(struct STRM_OBJECT *hStrm,
			OUT struct STRM_INFO *pStreamInfo,
			u32 uStreamInfoSize)
{
	struct WMD_DRV_INTERFACE *pIntfFxns;
	struct CHNL_INFO chnlInfo;
	DSP_STATUS status = DSP_SOK;
	void *pVirtBase = NULL;	/* NULL if no SM used */

	DBC_Require(cRefs > 0);
	DBC_Require(pStreamInfo != NULL);
	DBC_Require(uStreamInfoSize >= sizeof(struct STRM_INFO));

	GT_3trace(STRM_debugMask, GT_ENTER, "STRM_GetInfo: hStrm: 0x%x\t"
		 "pStreamInfo: 0x%x\tuStreamInfoSize: 0x%x\n", hStrm,
		 pStreamInfo, uStreamInfoSize);
	if (!MEM_IsValidHandle(hStrm, STRM_SIGNATURE)) {
		status = DSP_EHANDLE;
	} else {
		if (uStreamInfoSize < sizeof(struct STRM_INFO)) {
			/* size of users info */
			status = DSP_ESIZE;
		}
	}
	if (DSP_FAILED(status))
		goto func_end;

	pIntfFxns = hStrm->hStrmMgr->pIntfFxns;
	status = (*pIntfFxns->pfnChnlGetInfo) (hStrm->hChnl, &chnlInfo);
	if (DSP_FAILED(status))
		goto func_end;

	if (hStrm->hXlator) {
		/* We have a translator */
		DBC_Assert(hStrm->uSegment > 0);
		CMM_XlatorInfo(hStrm->hXlator, (u8 **)&pVirtBase, 0,
			      hStrm->uSegment, false);
	}
	pStreamInfo->uSegment = hStrm->uSegment;
	pStreamInfo->lMode = hStrm->lMode;
	pStreamInfo->pVirtBase = pVirtBase;
	pStreamInfo->pUser->uNumberBufsAllowed = hStrm->uNumBufs;
	pStreamInfo->pUser->uNumberBufsInStream = chnlInfo.cIOCs +
						 chnlInfo.cIOReqs;
	/* # of bytes transferred since last call to DSPStream_Idle() */
	pStreamInfo->pUser->ulNumberBytes = chnlInfo.cPosition;
	pStreamInfo->pUser->hSyncObjectHandle = chnlInfo.hEvent;
	/* Determine stream state based on channel state and info */
	if (chnlInfo.dwState & CHNL_STATEEOS) {
		pStreamInfo->pUser->ssStreamState = STREAM_DONE;
	} else {
		if (chnlInfo.cIOCs > 0)
			pStreamInfo->pUser->ssStreamState = STREAM_READY;
		else if (chnlInfo.cIOReqs > 0)
			pStreamInfo->pUser->ssStreamState = STREAM_PENDING;
		else
			pStreamInfo->pUser->ssStreamState = STREAM_IDLE;

	}
func_end:
	return status;
}

/*
 *  ======== STRM_Idle ========
 *  Purpose:
 *      Idles a particular stream.
 */
DSP_STATUS STRM_Idle(struct STRM_OBJECT *hStrm, bool fFlush)
{
	struct WMD_DRV_INTERFACE *pIntfFxns;
	DSP_STATUS status = DSP_SOK;

	DBC_Require(cRefs > 0);

	GT_2trace(STRM_debugMask, GT_ENTER, "STRM_Idle: hStrm: 0x%x\t"
		 "fFlush: 0x%x\n", hStrm, fFlush);

	if (!MEM_IsValidHandle(hStrm, STRM_SIGNATURE)) {
		status = DSP_EHANDLE;
	} else {
		pIntfFxns = hStrm->hStrmMgr->pIntfFxns;

		status = (*pIntfFxns->pfnChnlIdle) (hStrm->hChnl,
			 hStrm->uTimeout, fFlush);
	}
	return status;
}

/*
 *  ======== STRM_Init ========
 *  Purpose:
 *      Initialize the STRM module.
 */
bool STRM_Init(void)
{
	bool fRetVal = true;

	DBC_Require(cRefs >= 0);

	if (cRefs == 0) {
#if GT_TRACE
		DBC_Assert(!STRM_debugMask.flags);
		GT_create(&STRM_debugMask, "ST");	/* "ST" for STrm */
#endif
	}

	if (fRetVal)
		cRefs++;

	GT_1trace(STRM_debugMask, GT_5CLASS, "STRM_Init(), ref count: 0x%x\n",
		 cRefs);

	DBC_Ensure((fRetVal && (cRefs > 0)) || (!fRetVal && (cRefs >= 0)));

	return fRetVal;
}

/*
 *  ======== STRM_Issue ========
 *  Purpose:
 *      Issues a buffer on a stream
 */
DSP_STATUS STRM_Issue(struct STRM_OBJECT *hStrm, IN u8 *pBuf, u32 ulBytes,
		     u32 ulBufSize, u32 dwArg)
{
	struct WMD_DRV_INTERFACE *pIntfFxns;
	DSP_STATUS status = DSP_SOK;
	void *pTmpBuf = NULL;

	DBC_Require(cRefs > 0);
	DBC_Require(pBuf != NULL);

	GT_4trace(STRM_debugMask, GT_ENTER, "STRM_Issue: hStrm: 0x%x\tpBuf: "
		 "0x%x\tulBytes: 0x%x\tdwArg: 0x%x\n", hStrm, pBuf, ulBytes,
		 dwArg);
	if (!MEM_IsValidHandle(hStrm, STRM_SIGNATURE)) {
		status = DSP_EHANDLE;
	} else {
		pIntfFxns = hStrm->hStrmMgr->pIntfFxns;

		if (hStrm->uSegment != 0) {
			pTmpBuf = CMM_XlatorTranslate(hStrm->hXlator,
					(void *)pBuf, CMM_VA2DSPPA);
			if (pTmpBuf == NULL)
				status = DSP_ETRANSLATE;

		}
		if (DSP_SUCCEEDED(status)) {
			status = (*pIntfFxns->pfnChnlAddIOReq)
				 (hStrm->hChnl, pBuf, ulBytes, ulBufSize,
				 (u32) pTmpBuf, dwArg);
		}
		if (DSP_FAILED(status)) {
			if (status == CHNL_E_NOIORPS)
				status = DSP_ESTREAMFULL;
			else
				status = DSP_EFAIL;

		}
	}
	return status;
}

/*
 *  ======== STRM_Open ========
 *  Purpose:
 *      Open a stream for sending/receiving data buffers to/from a task or
 *      XDAIS socket node on the DSP.
 */
DSP_STATUS STRM_Open(struct NODE_OBJECT *hNode, u32 uDir, u32 uIndex,
		    IN struct STRM_ATTR *pAttr, OUT struct STRM_OBJECT **phStrm)
{
	struct STRM_MGR *hStrmMgr;
	struct WMD_DRV_INTERFACE *pIntfFxns;
	u32 ulChnlId;
	struct STRM_OBJECT *pStrm = NULL;
	CHNL_MODE uMode;
	struct CHNL_ATTRS chnlAttrs;
	DSP_STATUS status = DSP_SOK;
	struct CMM_OBJECT *hCmmMgr = NULL;	/* Shared memory manager hndl */

	#ifndef RES_CLEANUP_DISABLE
	DSP_STATUS res_status = DSP_SOK;
       u32                  hProcess;
	HANDLE	     pCtxt = NULL;
	HANDLE	     hDrvObject;
	HANDLE 		    hSTRMRes;
	#endif
	DBC_Require(cRefs > 0);
	DBC_Require(phStrm != NULL);
	DBC_Require(pAttr != NULL);
	GT_5trace(STRM_debugMask, GT_ENTER,
		 "STRM_Open: hNode: 0x%x\tuDir: 0x%x\t"
		 "uIndex: 0x%x\tpAttr: 0x%x\tphStrm: 0x%x\n",
		 hNode, uDir, uIndex, pAttr, phStrm);
	*phStrm = NULL;
	if (uDir != DSP_TONODE && uDir != DSP_FROMNODE) {
		status = DSP_EDIRECTION;
	} else {
		/* Get the channel id from the node (set in NODE_Connect()) */
		status = NODE_GetChannelId(hNode, uDir, uIndex, &ulChnlId);
	}
	if (DSP_SUCCEEDED(status))
		status = NODE_GetStrmMgr(hNode, &hStrmMgr);

	if (DSP_SUCCEEDED(status)) {
		MEM_AllocObject(pStrm, struct STRM_OBJECT, STRM_SIGNATURE);
		if (pStrm == NULL) {
			status = DSP_EMEMORY;
			GT_0trace(STRM_debugMask, GT_6CLASS,
				 "STRM_Open: MEM_AllocObject() failed!\n ");
		} else {
			pStrm->hStrmMgr = hStrmMgr;
			pStrm->uDir = uDir;
			pStrm->strmState = STREAM_IDLE;
			pStrm->hUserEvent = pAttr->hUserEvent;
			if (pAttr->pStreamAttrIn != NULL) {
				pStrm->uTimeout = pAttr->pStreamAttrIn->
						  uTimeout;
				pStrm->uNumBufs = pAttr->pStreamAttrIn->
						  uNumBufs;
				pStrm->lMode = pAttr->pStreamAttrIn->lMode;
				pStrm->uSegment = pAttr->pStreamAttrIn->
						  uSegment;
				pStrm->uAlignment = pAttr->pStreamAttrIn->
						    uAlignment;
				pStrm->uDMAChnlId = pAttr->pStreamAttrIn->
						    uDMAChnlId;
				pStrm->uDMAPriority = pAttr->pStreamAttrIn->
						      uDMAPriority;
				chnlAttrs.uIOReqs = pAttr->pStreamAttrIn->
						    uNumBufs;
			} else {
				pStrm->uTimeout = DEFAULTTIMEOUT;
				pStrm->uNumBufs = DEFAULTNUMBUFS;
				pStrm->lMode = STRMMODE_PROCCOPY;
				pStrm->uSegment = 0;	/* local memory */
				pStrm->uAlignment = 0;
				pStrm->uDMAChnlId = 0;
				pStrm->uDMAPriority = 0;
				chnlAttrs.uIOReqs = DEFAULTNUMBUFS;
			}
			chnlAttrs.hReserved1 = NULL;
			/* DMA chnl flush timeout */
			chnlAttrs.hReserved2 = pStrm->uTimeout;
			chnlAttrs.hEvent = NULL;
			if (pAttr->hUserEvent != NULL)
				chnlAttrs.hEvent = pAttr->hUserEvent;

		}
	}
	if (DSP_FAILED(status))
		goto func_cont;

	if ((pAttr->pVirtBase == NULL) || !(pAttr->ulVirtSize > 0))
		goto func_cont;

	DBC_Assert(pStrm->lMode != STRMMODE_LDMA);	/* no System DMA */
	/* Get the shared mem mgr for this streams dev object */
	status = DEV_GetCmmMgr(hStrmMgr->hDev, &hCmmMgr);
	if (DSP_FAILED(status)) {
		GT_1trace(STRM_debugMask, GT_6CLASS, "STRM_Open: Failed to get "
			 "CMM Mgr handle: 0x%x\n", status);
	} else {
		/*Allocate a SM addr translator for this strm.*/
		status = CMM_XlatorCreate(&pStrm->hXlator, hCmmMgr, NULL);
		if (DSP_FAILED(status)) {
			GT_1trace(STRM_debugMask, GT_6CLASS,
				 "STRM_Open: Failed to "
				 "create SM translator: 0x%x\n", status);
		} else {
			DBC_Assert(pStrm->uSegment > 0);
			/*  Set translators Virt Addr attributes */
			status = CMM_XlatorInfo(pStrm->hXlator,
				 (u8 **)&pAttr->pVirtBase, pAttr->ulVirtSize,
				 pStrm->uSegment, true);
			if (status != DSP_SOK) {
				GT_0trace(STRM_debugMask, GT_6CLASS,
					 "STRM_Open: ERROR: "
					 "in setting CMM_XlatorInfo.\n");
			}
		}
	}
func_cont:
	if (DSP_SUCCEEDED(status)) {
		/* Open channel */
		uMode = (uDir == DSP_TONODE) ?
			CHNL_MODETODSP : CHNL_MODEFROMDSP;
		pIntfFxns = hStrmMgr->pIntfFxns;
		status = (*pIntfFxns->pfnChnlOpen) (&(pStrm->hChnl),
			 hStrmMgr->hChnlMgr, uMode, ulChnlId, &chnlAttrs);
		if (DSP_FAILED(status)) {
			/*
			 * over-ride non-returnable status codes so we return
			 * something documented
			 */
			if (status != DSP_EMEMORY && status !=
			   DSP_EINVALIDARG && status != DSP_EFAIL) {
				/*
				 * We got a status that's not return-able.
				 * Assert that we got something we were
				 * expecting (DSP_EHANDLE isn't acceptable,
				 * hStrmMgr->hChnlMgr better be valid or we
				 * assert here), and then return DSP_EFAIL.
				 */
				DBC_Assert(status == CHNL_E_OUTOFSTREAMS ||
					   status == CHNL_E_BADCHANID ||
					   status == CHNL_E_CHANBUSY ||
					   status == CHNL_E_NOIORPS);
				status = DSP_EFAIL;
			}
			GT_2trace(STRM_debugMask, GT_6CLASS,
				  "STRM_Open: Channel open failed, "
				  "chnl id = %d, status = 0x%x\n", ulChnlId,
				  status);
		}
	}
	if (DSP_SUCCEEDED(status))
		*phStrm = pStrm;
	else
		(void)DeleteStrm(pStrm);

#ifndef RES_CLEANUP_DISABLE
       /* Return PID instead of process handle */
       hProcess = current->pid;

	res_status = CFG_GetObject((u32 *)&hDrvObject, REG_DRV_OBJECT);
	if (DSP_SUCCEEDED(res_status)) {
               DRV_GetProcContext(hProcess,
				 (struct DRV_OBJECT *)hDrvObject, &pCtxt,
				 hNode, 0);
		if (pCtxt != NULL)
			DRV_ProcInsertSTRMResElement(*phStrm, &hSTRMRes, pCtxt);

	}
#endif

	 /* ensure we return a documented error code */
	DBC_Ensure((DSP_SUCCEEDED(status) &&
		  MEM_IsValidHandle((*phStrm), STRM_SIGNATURE)) ||
		  (*phStrm == NULL && (status == DSP_EHANDLE ||
		  status == DSP_EDIRECTION || status == DSP_EVALUE ||
		  status == DSP_EFAIL)));
	return status;
}

/*
 *  ======== STRM_Reclaim ========
 *  Purpose:
 *      Relcaims a buffer from a stream.
 */
DSP_STATUS STRM_Reclaim(struct STRM_OBJECT *hStrm, OUT u8 **pBufPtr,
			u32 *pulBytes, u32 *pulBufSize, u32 *pdwArg)
{
	struct WMD_DRV_INTERFACE *pIntfFxns;
	struct CHNL_IOC chnlIOC;
	DSP_STATUS status = DSP_SOK;
	void *pTmpBuf = NULL;

	DBC_Require(cRefs > 0);
	DBC_Require(pBufPtr != NULL);
	DBC_Require(pulBytes != NULL);
	DBC_Require(pdwArg != NULL);

	GT_4trace(STRM_debugMask, GT_ENTER,
		 "STRM_Reclaim: hStrm: 0x%x\tpBufPtr: 0x%x"
		 "\tpulBytes: 0x%x\tpdwArg: 0x%x\n", hStrm, pBufPtr, pulBytes,
		 pdwArg);

	if (!MEM_IsValidHandle(hStrm, STRM_SIGNATURE)) {
		status = DSP_EHANDLE;
		goto func_end;
	}
	pIntfFxns = hStrm->hStrmMgr->pIntfFxns;

	status = (*pIntfFxns->pfnChnlGetIOC)(hStrm->hChnl, hStrm->uTimeout,
		 &chnlIOC);
	if (DSP_FAILED(status)) {
		GT_1trace(STRM_debugMask, GT_6CLASS,
			 "STRM_Reclaim: GetIOC failed! "
			 "Status = 0x%x\n", status);
	} else {
		*pulBytes = chnlIOC.cBytes;
		if (pulBufSize)
			*pulBufSize = chnlIOC.cBufSize;

		*pdwArg = chnlIOC.dwArg;
		if (!CHNL_IsIOComplete(chnlIOC)) {
			if (CHNL_IsTimedOut(chnlIOC)) {
				status = DSP_ETIMEOUT;
			} else {
				/* Allow reclaims after idle to succeed */
				if (!CHNL_IsIOCancelled(chnlIOC))
					status = DSP_EFAIL;

			}
		}
		/* Translate zerocopy buffer if channel not canceled. */
		if (DSP_SUCCEEDED(status) && (!CHNL_IsIOCancelled(chnlIOC)) &&
		   (hStrm->lMode == STRMMODE_ZEROCOPY)) {
			/*
			 *  This is a zero-copy channel so chnlIOC.pBuf
			 *  contains the DSP address of SM. We need to
			 *  translate it to a virtual address for the user
			 *  thread to access.
			 *  Note: Could add CMM_DSPPA2VA to CMM in the future.
			 */
			pTmpBuf = CMM_XlatorTranslate(hStrm->hXlator,
					chnlIOC.pBuf, CMM_DSPPA2PA);
			if (pTmpBuf != NULL) {
				/* now convert this GPP Pa to Va */
				pTmpBuf = CMM_XlatorTranslate(hStrm->hXlator,
					  pTmpBuf, CMM_PA2VA);
			}
			if (pTmpBuf == NULL) {
				GT_0trace(STRM_debugMask, GT_7CLASS,
					 "STRM_Reclaim: Failed "
					 "SM translation!\n");
				status = DSP_ETRANSLATE;
			}
			chnlIOC.pBuf = pTmpBuf;
		}
		*pBufPtr = chnlIOC.pBuf;
	}
func_end:
	/* ensure we return a documented return code */
	DBC_Ensure(DSP_SUCCEEDED(status) || status == DSP_EHANDLE ||
		  status == DSP_ETIMEOUT || status == DSP_ETRANSLATE ||
		  status == DSP_EFAIL);
	return status;
}

/*
 *  ======== STRM_RegisterNotify ========
 *  Purpose:
 *      Register to be notified on specific events for this stream.
 */
DSP_STATUS STRM_RegisterNotify(struct STRM_OBJECT *hStrm, u32 uEventMask,
			      u32 uNotifyType, struct DSP_NOTIFICATION
			      *hNotification)
{
	struct WMD_DRV_INTERFACE *pIntfFxns;
	DSP_STATUS status = DSP_SOK;

	DBC_Require(cRefs > 0);
	DBC_Require(hNotification != NULL);

	GT_4trace(STRM_debugMask, GT_ENTER,
		 "STRM_RegisterNotify: hStrm: 0x%x\t"
		 "uEventMask: 0x%x\tuNotifyType: 0x%x\thNotification: 0x%x\n",
		 hStrm, uEventMask, uNotifyType, hNotification);
	if (!MEM_IsValidHandle(hStrm, STRM_SIGNATURE)) {
		status = DSP_EHANDLE;
	} else if ((uEventMask & ~((DSP_STREAMIOCOMPLETION) |
		 DSP_STREAMDONE)) != 0) {
		status = DSP_EVALUE;
	} else {
		if (uNotifyType != DSP_SIGNALEVENT)
			status = DSP_ENOTIMPL;

	}
	if (DSP_SUCCEEDED(status)) {
		pIntfFxns = hStrm->hStrmMgr->pIntfFxns;

		status = (*pIntfFxns->pfnChnlRegisterNotify)(hStrm->hChnl,
			 uEventMask, uNotifyType, hNotification);
	}
	/* ensure we return a documented return code */
	DBC_Ensure(DSP_SUCCEEDED(status) || status == DSP_EHANDLE ||
		  status == DSP_ETIMEOUT || status == DSP_ETRANSLATE ||
		  status == DSP_ENOTIMPL || status == DSP_EFAIL);
	return status;
}

/*
 *  ======== STRM_Select ========
 *  Purpose:
 *      Selects a ready stream.
 */
DSP_STATUS STRM_Select(IN struct STRM_OBJECT **aStrmTab, u32 nStrms,
		      OUT u32 *pMask, u32 uTimeout)
{
	u32 uIndex;
	struct CHNL_INFO chnlInfo;
	struct WMD_DRV_INTERFACE *pIntfFxns;
	struct SYNC_OBJECT **hSyncEvents = NULL;
	u32 i;
	DSP_STATUS status = DSP_SOK;

	DBC_Require(cRefs > 0);
	DBC_Require(aStrmTab != NULL);
	DBC_Require(pMask != NULL);
	DBC_Require(nStrms > 0);

	GT_4trace(STRM_debugMask, GT_ENTER,
		 "STRM_Select: aStrmTab: 0x%x \tnStrms: "
		 "0x%x\tpMask: 0x%x\tuTimeout: 0x%x\n", aStrmTab,
		 nStrms, pMask, uTimeout);
	*pMask = 0;
	for (i = 0; i < nStrms; i++) {
		if (!MEM_IsValidHandle(aStrmTab[i], STRM_SIGNATURE)) {
			status = DSP_EHANDLE;
			break;
		}
	}
	if (DSP_FAILED(status))
		goto func_end;

	/* Determine which channels have IO ready */
	for (i = 0; i < nStrms; i++) {
		pIntfFxns = aStrmTab[i]->hStrmMgr->pIntfFxns;
		status = (*pIntfFxns->pfnChnlGetInfo)(aStrmTab[i]->hChnl,
			 &chnlInfo);
		if (DSP_FAILED(status)) {
			break;
		} else {
			if (chnlInfo.cIOCs > 0)
				*pMask |= (1 << i);

		}
	}
	if (DSP_SUCCEEDED(status) && uTimeout > 0 && *pMask == 0) {
		/* Non-zero timeout */
		hSyncEvents = (struct SYNC_OBJECT **)MEM_Alloc(nStrms *
			      sizeof(struct SYNC_OBJECT *), MEM_PAGED);
		if (hSyncEvents == NULL) {
			status = DSP_EMEMORY;
		} else {
			for (i = 0; i < nStrms; i++) {
				pIntfFxns = aStrmTab[i]->hStrmMgr->pIntfFxns;
				status = (*pIntfFxns->pfnChnlGetInfo)
					 (aStrmTab[i]->hChnl, &chnlInfo);
				if (DSP_FAILED(status))
					break;
				else
					hSyncEvents[i] = chnlInfo.hSyncEvent;

			}
		}
		if (DSP_SUCCEEDED(status)) {
			status = SYNC_WaitOnMultipleEvents(hSyncEvents, nStrms,
				uTimeout, &uIndex);
			if (DSP_SUCCEEDED(status)) {
				/* Since we waited on the event, we have to
				 * reset it */
				SYNC_SetEvent(hSyncEvents[uIndex]);
				*pMask = 1 << uIndex;
			}
		}
	}
func_end:
	if (hSyncEvents)
		MEM_Free(hSyncEvents);

	DBC_Ensure((DSP_SUCCEEDED(status) && (*pMask != 0 || uTimeout == 0)) ||
		  (DSP_FAILED(status) && *pMask == 0));

	return status;
}

/*
 *  ======== DeleteStrm ========
 *  Purpose:
 *      Frees the resources allocated for a stream.
 */
static DSP_STATUS DeleteStrm(struct STRM_OBJECT *hStrm)
{
	struct WMD_DRV_INTERFACE *pIntfFxns;
	DSP_STATUS status = DSP_SOK;

	if (MEM_IsValidHandle(hStrm, STRM_SIGNATURE)) {
		if (hStrm->hChnl) {
			pIntfFxns = hStrm->hStrmMgr->pIntfFxns;
			/* Channel close can fail only if the channel handle
			 * is invalid. */
			status = (*pIntfFxns->pfnChnlClose) (hStrm->hChnl);
			/* Free all SM address translator resources */
			if (DSP_SUCCEEDED(status)) {
				if (hStrm->hXlator) {
					/* force free */
					(void)CMM_XlatorDelete(hStrm->hXlator,
					true);
				}
			}
		}
		MEM_FreeObject(hStrm);
	} else {
		status = DSP_EHANDLE;
	}
	return status;
}

/*
 *  ======== DeleteStrmMgr ========
 *  Purpose:
 *      Frees stream manager.
 */
static void DeleteStrmMgr(struct STRM_MGR *hStrmMgr)
{
	if (MEM_IsValidHandle(hStrmMgr, STRMMGR_SIGNATURE)) {

		if (hStrmMgr->hSync)
			SYNC_DeleteCS(hStrmMgr->hSync);

		MEM_FreeObject(hStrmMgr);
	}
}

