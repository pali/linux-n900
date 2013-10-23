/*
 * cmm.c
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
 *  ======== cmm.c ========
 *  Purpose:
 *      The Communication(Shared) Memory Management(CMM) module provides
 *      shared memory management services for DSP/BIOS Bridge data streaming
 *      and messaging.
 *
 *      Multiple shared memory segments can be registered with CMM.
 *      Each registered SM segment is represented by a SM "allocator" that
 *      describes a block of physically contiguous shared memory used for
 *      future allocations by CMM.
 *
 *      Memory is coelesced back to the appropriate heap when a buffer is
 *      freed.
 *
 *  Public Functions:
 *      CMM_CallocBuf
 *      CMM_Create
 *      CMM_Destroy
 *      CMM_Exit
 *      CMM_FreeBuf
 *      CMM_GetHandle
 *      CMM_GetInfo
 *      CMM_Init
 *      CMM_RegisterGPPSMSeg
 *      CMM_UnRegisterGPPSMSeg
 *
 *      The CMM_Xlator[xxx] routines below are used by Node and Stream
 *      to perform SM address translation to the client process address space.
 *      A "translator" object is created by a node/stream for each SM seg used.
 *
 *  Translator Routines:
 *      CMM_XlatorAllocBuf
 *      CMM_XlatorCreate
 *      CMM_XlatorDelete
 *      CMM_XlatorFreeBuf
 *      CMM_XlatorInfo
 *      CMM_XlatorTranslate
 *
 *  Private Functions:
 *      AddToFreeList
 *      GetAllocator
 *      GetFreeBlock
 *      GetNode
 *      GetSlot
 *      UnRegisterGPPSMSeg
 *
 *  Notes:
 *      Va: Virtual address.
 *      Pa: Physical or kernel system address.
 *
 *! Revision History:
 *! ================
 *! 24-Feb-2003 swa PMGR Code review comments incorporated.
 *! 16-Feb-2002 ag  Code review cleanup.
 *!                 PreOMAP address translation no longner supported.
 *! 30-Jan-2002 ag  Updates to CMM_XlatorTranslate() per TII, ANSI C++
 *!                 warnings.
 *! 27-Jan-2002 ag  Removed unused CMM_[Alloc][Free]Desc() & #ifdef USELOOKUP,
 *!                 & unused VALIDATECMM and VaPaConvert().
 *!                 Removed bFastXlate from CMM_XLATOR. Always fast lookup.
 *! 03-Jan-2002 ag  Clear SM in CMM_AllocBuf(). Renamed to CMM_CallocBuf().
 *! 13-Nov-2001 ag  Now delete pNodeFreeListHead and nodes in CMM_Destroy().
 *! 28-Aug-2001 ag  CMM_GetHandle() returns CMM Mgr hndle given HPROCESSOR.
 *!                 Removed unused CMM_[Un]RegisterDSPSMSeg() &
 *                  CMM_[Un}ReserveVirtSpace fxns. Some cleanup.
 *! 12-Aug-2001 ag  Exposed CMM_UnRegisterGPP[DSP]SMSeg.
 *! 13-Feb-2001 kc  DSP/BIOS Bridge name update.
 *! 21-Dec-2000 rr  GetFreeBlock checks for pAllocator.
 *! 09-Dec-2000 ag  Added GPPPA2DSPPA, DSPPA2GPPPA macros.
 *! 05-Dec-2000 ag  CMM_XlatorDelete() optionally frees SM bufs and descriptors.
 *! 30-Oct-2000 ag  Buf size bug fixed in CMM_AllocBuf() causing leak.
 *!                 Revamped XlatorTranslate() routine.
 *! 10-Oct-2000 ag  Added CMM_Xlator[xxx] functions.
 *! 02-Aug-2000 ag  Created.
 *!
 */

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>
#include <dspbridge/dbdefs.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>
#include <dspbridge/errbase.h>
#include <dspbridge/gt.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/cfg.h>
#include <dspbridge/list.h>
#include <dspbridge/mem.h>
#include <dspbridge/sync.h>
#include <dspbridge/util.h>

/*  ----------------------------------- Platform Manager */
#include <dspbridge/dev.h>
#include <dspbridge/proc.h>

/*  ----------------------------------- This */
#include <dspbridge/cmm.h>

/*  ----------------------------------- Defines, Data Structures, Typedefs */
/* Object signatures */
#define CMMSIGNATURE       0x004d4d43	/* "CMM"   (in reverse) */
#define SMEMSIGNATURE      0x4D454D53	/* "SMEM"  SM space     */
#define CMMXLATESIGNATURE  0x584d4d43	/* "CMMX"  CMM Xlator   */

#define NEXT_PA(pNode)   (pNode->dwPA + pNode->ulSize)

/* Other bus/platform translations */
#define DSPPA2GPPPA(base, x, y)  ((x)+(y))
#define GPPPA2DSPPA(base, x, y)  ((x)-(y))

/*
 *  Allocators define a block of contiguous memory used for future allocations.
 *
 *      sma - shared memory allocator.
 *      vma - virtual memory allocator.(not used).
 */
struct CMM_ALLOCATOR {	/* sma */
	u32 dwSignature;	/* SMA allocator signature SMEMSIGNATURE */
	unsigned int dwSmBase;		/* Start of physical SM block */
	u32 ulSmSize;		/* Size of SM block in bytes */
	unsigned int dwVmBase;		/* Start of VM block. (Dev driver
				 * context for 'sma') */
	u32 dwDSPPhysAddrOffset;	/* DSP PA to GPP PA offset for this
					 * SM space */
	/* CMM_ADDTO[SUBFROM]DSPPA, _POMAPEMIF2DSPBUS */
	enum CMM_CNVTTYPE cFactor;
	unsigned int dwDSPBase;	/* DSP virt base byte address */
	u32 ulDSPSize;	/* DSP seg size in bytes */
	struct CMM_OBJECT *hCmmMgr;	/* back ref to parent mgr */
	struct LST_LIST *pFreeListHead;	/* node list of available memory */
	struct LST_LIST *pInUseListHead;	/* node list of memory in use */
} ;

struct CMM_XLATOR {	/* Pa<->Va translator object */
	u32 dwSignature;	/* "CMMX" */
	struct CMM_OBJECT *hCmmMgr;  /* CMM object this translator associated */
	/*
	 *  Client process virtual base address that corresponds to phys SM
	 *  base address for translator's ulSegId.
	 *  Only 1 segment ID currently supported.
	 */
	unsigned int dwVirtBase;	/* virtual base address */
	u32 ulVirtSize;	/* size of virt space in bytes */
	u32 ulSegId;		/* Segment Id */
} ;

/* CMM Mgr */
struct CMM_OBJECT {
	u32 dwSignature;	/* Used for object validation */
	/*
	 * Cmm Lock is used to serialize access mem manager for multi-threads.
	 */
	struct SYNC_CSOBJECT *hCmmLock;	/* Lock to access cmm mgr */
	struct LST_LIST *pNodeFreeListHead;	/* Free list of memory nodes */
	u32 ulMinBlockSize;	/* Min SM block; default 16 bytes */
	u32 dwPageSize;	/* Memory Page size (1k/4k) */
	/* GPP SM segment ptrs */
	struct CMM_ALLOCATOR *paGPPSMSegTab[CMM_MAXGPPSEGS];
} ;

/* Default CMM Mgr attributes */
static struct CMM_MGRATTRS CMM_DFLTMGRATTRS = {
	16	/* ulMinBlockSize, min block size(bytes) allocated by cmm mgr */
};

/* Default allocation attributes */
static struct CMM_ATTRS CMM_DFLTALCTATTRS = {
	1			/* ulSegId, default segment Id for allocator */
};

/* Address translator default attrs */
static struct CMM_XLATORATTRS CMM_DFLTXLATORATTRS = {
	1,	/* ulSegId, does not have to match CMM_DFLTALCTATTRS ulSegId */
	0,			/* dwDSPBufs */
	0,			/* dwDSPBufSize */
	NULL,			/* pVmBase */
	0,			/* dwVmSize */
};

/* SM node representing a block of memory. */
struct CMM_MNODE {
	struct LST_ELEM link;		/* must be 1st element */
	u32 dwPA;		/* Phys addr */
	u32 dwVA;		/* Virtual address in device process context */
	u32 ulSize;		/* SM block size in bytes */
       u32 hClientProc;        /* Process that allocated this mem block */
} ;


/*  ----------------------------------- Globals */
#if GT_TRACE
static struct GT_Mask CMM_debugMask = { NULL, NULL };	/* GT trace variable */
#endif

static u32 cRefs;		/* module reference count */

/*  ----------------------------------- Function Prototypes */
static void AddToFreeList(struct CMM_ALLOCATOR *pAllocator,
			  struct CMM_MNODE *pNode);
static struct CMM_ALLOCATOR *GetAllocator(struct CMM_OBJECT *pCmmMgr,
					  u32 ulSegId);
static struct CMM_MNODE *GetFreeBlock(struct CMM_ALLOCATOR *pAllocator,
				      u32 uSize);
static struct CMM_MNODE *GetNode(struct CMM_OBJECT *pCmmMgr, u32 dwPA,
				 u32 dwVA, u32 ulSize);
/* get available slot for new allocator */
static s32 GetSlot(struct CMM_OBJECT *hCmmMgr);
static void UnRegisterGPPSMSeg(struct CMM_ALLOCATOR *pSMA);

/*
 *  ======== CMM_CallocBuf ========
 *  Purpose:
 *      Allocate a SM buffer, zero contents, and return the physical address
 *      and optional driver context virtual address(ppBufVA).
 *
 *      The freelist is sorted in increasing size order. Get the first
 *      block that satifies the request and sort the remaining back on
 *      the freelist; if large enough. The kept block is placed on the
 *      inUseList.
 */
void *CMM_CallocBuf(struct CMM_OBJECT *hCmmMgr, u32 uSize,
		    struct CMM_ATTRS *pAttrs, OUT void **ppBufVA)
{
	struct CMM_OBJECT *pCmmMgr = (struct CMM_OBJECT *)hCmmMgr;
	void *pBufPA = NULL;
	struct CMM_MNODE *pNode = NULL;
	struct CMM_MNODE *pNewNode = NULL;
	struct CMM_ALLOCATOR *pAllocator = NULL;
	u32 uDeltaSize;
	u8 *pByte = NULL;
	s32 cnt;

	if (pAttrs == NULL)
		pAttrs = &CMM_DFLTALCTATTRS;

	if (ppBufVA != NULL)
		*ppBufVA = NULL;

	if ((MEM_IsValidHandle(pCmmMgr, CMMSIGNATURE)) && (uSize != 0)) {
		if (pAttrs->ulSegId > 0) {
			/* SegId > 0 is SM  */
			/* get the allocator object for this segment id */
			pAllocator = GetAllocator(pCmmMgr, pAttrs->ulSegId);
			/* keep block size a multiple of ulMinBlockSize */
			uSize = ((uSize - 1) & ~(pCmmMgr->ulMinBlockSize - 1))
				+ pCmmMgr->ulMinBlockSize;
			SYNC_EnterCS(pCmmMgr->hCmmLock);
			pNode = GetFreeBlock(pAllocator, uSize);
		}
		if (pNode) {
			uDeltaSize = (pNode->ulSize - uSize);
			if (uDeltaSize >= pCmmMgr->ulMinBlockSize) {
				/* create a new block with the leftovers and
				 * add to freelist */
				pNewNode = GetNode(pCmmMgr, pNode->dwPA + uSize,
					   pNode->dwVA + uSize,
					   (u32)uDeltaSize);
				/* leftovers go free */
				AddToFreeList(pAllocator, pNewNode);
				/* adjust our node's size */
				pNode->ulSize = uSize;
			}
			/* Tag node with client process requesting allocation
			 * We'll need to free up a process's alloc'd SM if the
			 * client process goes away.
			 */
			/* Return TGID instead of process handle */
			pNode->hClientProc = current->tgid;

			/* put our node on InUse list */
			LST_PutTail(pAllocator->pInUseListHead,
				   (struct LST_ELEM *)pNode);
			pBufPA = (void *)pNode->dwPA;	/* physical address */
			/* clear mem */
			pByte = (u8 *)pNode->dwVA;
			for (cnt = 0; cnt < (s32) uSize; cnt++, pByte++)
				*pByte = 0;

			if (ppBufVA != NULL) {
				/* Virtual address */
				*ppBufVA = (void *)pNode->dwVA;
			}
		}
		GT_3trace(CMM_debugMask, GT_3CLASS,
			  "CMM_CallocBuf dwPA %x, dwVA %x uSize"
			  "%x\n", pNode->dwPA, pNode->dwVA, uSize);
		SYNC_LeaveCS(pCmmMgr->hCmmLock);
	}
	return pBufPA;
}

/*
 *  ======== CMM_Create ========
 *  Purpose:
 *      Create a communication memory manager object.
 */
DSP_STATUS CMM_Create(OUT struct CMM_OBJECT **phCmmMgr,
		      struct DEV_OBJECT *hDevObject,
		      IN CONST struct CMM_MGRATTRS *pMgrAttrs)
{
	struct CMM_OBJECT *pCmmObject = NULL;
	DSP_STATUS status = DSP_SOK;
	struct UTIL_SYSINFO sysInfo;

	DBC_Require(cRefs > 0);
	DBC_Require(phCmmMgr != NULL);

	GT_3trace(CMM_debugMask, GT_ENTER,
		  "CMM_Create: phCmmMgr: 0x%x\thDevObject: "
		  "0x%x\tpMgrAttrs: 0x%x\n", phCmmMgr, hDevObject, pMgrAttrs);
	*phCmmMgr = NULL;
	/* create, zero, and tag a cmm mgr object */
	MEM_AllocObject(pCmmObject, struct CMM_OBJECT, CMMSIGNATURE);
	if (pCmmObject != NULL) {
		if (pMgrAttrs == NULL)
			pMgrAttrs = &CMM_DFLTMGRATTRS;	/* set defaults */

		/* 4 bytes minimum */
		DBC_Assert(pMgrAttrs->ulMinBlockSize >= 4);
		/* save away smallest block allocation for this cmm mgr */
		pCmmObject->ulMinBlockSize = pMgrAttrs->ulMinBlockSize;
		/* save away the systems memory page size */
		sysInfo.dwPageSize = PAGE_SIZE;
		sysInfo.dwAllocationGranularity = PAGE_SIZE;
		sysInfo.dwNumberOfProcessors = 1;
		if (DSP_SUCCEEDED(status)) {
			GT_1trace(CMM_debugMask, GT_5CLASS,
				  "CMM_Create: Got system page size"
				  "= 0x%x\t\n", sysInfo.dwPageSize);
			pCmmObject->dwPageSize = sysInfo.dwPageSize;
		} else {
			GT_0trace(CMM_debugMask, GT_7CLASS,
				  "CMM_Create: failed to get system"
				  "page size\n");
			pCmmObject->dwPageSize = 0;
			status = DSP_EFAIL;
		}
		/* Note: DSP SM seg table(aDSPSMSegTab[]) zero'd by
		 * MEM_AllocObject */
		if (DSP_SUCCEEDED(status)) {
			/* create node free list */
			pCmmObject->pNodeFreeListHead = LST_Create();
			if (pCmmObject->pNodeFreeListHead == NULL) {
				GT_0trace(CMM_debugMask, GT_7CLASS,
					  "CMM_Create: LST_Create() "
					  "failed \n");
				status = DSP_EMEMORY;
			}
		}
		if (DSP_SUCCEEDED(status))
			status = SYNC_InitializeCS(&pCmmObject->hCmmLock);

		if (DSP_SUCCEEDED(status))
			*phCmmMgr = pCmmObject;
		else
			CMM_Destroy(pCmmObject, true);

	} else {
		GT_0trace(CMM_debugMask, GT_6CLASS,
			  "CMM_Create: Object Allocation "
			  "Failure(CMM Object)\n");
		status = DSP_EMEMORY;
	}
	return status;
}

/*
 *  ======== CMM_Destroy ========
 *  Purpose:
 *      Release the communication memory manager resources.
 */
DSP_STATUS CMM_Destroy(struct CMM_OBJECT *hCmmMgr, bool bForce)
{
	struct CMM_OBJECT *pCmmMgr = (struct CMM_OBJECT *)hCmmMgr;
	struct CMM_INFO tempInfo;
	DSP_STATUS status = DSP_SOK;
	s32 nSlot;
	struct CMM_MNODE *pNode;

	DBC_Require(cRefs > 0);
	if (!MEM_IsValidHandle(hCmmMgr, CMMSIGNATURE)) {
		status = DSP_EHANDLE;
		return status;
	}
	SYNC_EnterCS(pCmmMgr->hCmmLock);
	/* If not force then fail if outstanding allocations exist */
	if (!bForce) {
		/* Check for outstanding memory allocations */
		status = CMM_GetInfo(hCmmMgr, &tempInfo);
		if (DSP_SUCCEEDED(status)) {
			if (tempInfo.ulTotalInUseCnt > 0) {
				/* outstanding allocations */
				status = DSP_EFAIL;
			}
		}
	}
	if (DSP_SUCCEEDED(status)) {
		/* UnRegister SM allocator */
		for (nSlot = 0; nSlot < CMM_MAXGPPSEGS; nSlot++) {
			if (pCmmMgr->paGPPSMSegTab[nSlot] != NULL) {
				UnRegisterGPPSMSeg(pCmmMgr->
						   paGPPSMSegTab[nSlot]);
				/* Set slot to NULL for future reuse */
				pCmmMgr->paGPPSMSegTab[nSlot] = NULL;
			}
		}
	}
	if (pCmmMgr->pNodeFreeListHead != NULL) {
		/* Free the free nodes */
		while (!LST_IsEmpty(pCmmMgr->pNodeFreeListHead)) {
			/* (struct LST_ELEM*) pNode =
			 * LST_GetHead(pCmmMgr->pNodeFreeListHead);*/
			pNode = (struct CMM_MNODE *)LST_GetHead(pCmmMgr->
				 pNodeFreeListHead);
			MEM_Free(pNode);
		}
		/* delete NodeFreeList list */
		LST_Delete(pCmmMgr->pNodeFreeListHead);
	}
	SYNC_LeaveCS(pCmmMgr->hCmmLock);
	if (DSP_SUCCEEDED(status)) {
		/* delete CS & cmm mgr object */
		SYNC_DeleteCS(pCmmMgr->hCmmLock);
		MEM_FreeObject(pCmmMgr);
	}
	return status;
}

/*
 *  ======== CMM_Exit ========
 *  Purpose:
 *      Discontinue usage of module; free resources when reference count
 *      reaches 0.
 */
void CMM_Exit(void)
{
	DBC_Require(cRefs > 0);

	cRefs--;

	GT_1trace(CMM_debugMask, GT_ENTER,
		  "exiting CMM_Exit,ref count:0x%x\n", cRefs);
}

/*
 *  ======== CMM_FreeBuf ========
 *  Purpose:
 *      Free the given buffer.
 */
DSP_STATUS CMM_FreeBuf(struct CMM_OBJECT *hCmmMgr, void *pBufPA, u32 ulSegId)
{
	struct CMM_OBJECT *pCmmMgr = (struct CMM_OBJECT *)hCmmMgr;
	DSP_STATUS status = DSP_EPOINTER;
	struct CMM_MNODE *pCurNode = NULL;
	struct CMM_ALLOCATOR *pAllocator = NULL;
	struct CMM_ATTRS *pAttrs;

	DBC_Require(cRefs > 0);
	DBC_Require(pBufPA != NULL);
	GT_1trace(CMM_debugMask, GT_ENTER, "CMM_FreeBuf pBufPA %x\n", pBufPA);
	if (ulSegId == 0) {
		pAttrs = &CMM_DFLTALCTATTRS;
		ulSegId = pAttrs->ulSegId;
	}
	if (!(MEM_IsValidHandle(hCmmMgr, CMMSIGNATURE)) || !(ulSegId > 0)) {
		status = DSP_EHANDLE;
		return status;
	}
	/* get the allocator for this segment id */
	pAllocator = GetAllocator(pCmmMgr, ulSegId);
	if (pAllocator != NULL) {
		SYNC_EnterCS(pCmmMgr->hCmmLock);
		pCurNode = (struct CMM_MNODE *)LST_First(pAllocator->
			    pInUseListHead);
		while (pCurNode) {
			if ((u32)pBufPA == pCurNode->dwPA) {
				/* Found it */
				LST_RemoveElem(pAllocator->pInUseListHead,
					      (struct LST_ELEM *)pCurNode);
				/* back to freelist */
				AddToFreeList(pAllocator, pCurNode);
				status = DSP_SOK;	/* all right! */
				break;
			}
			/* next node. */
			pCurNode = (struct CMM_MNODE *)LST_Next(pAllocator->
				   pInUseListHead, (struct LST_ELEM *)pCurNode);
		}
		SYNC_LeaveCS(pCmmMgr->hCmmLock);
	}
	return status;
}

/*
 *  ======== CMM_GetHandle ========
 *  Purpose:
 *      Return the communication memory manager object for this device.
 *      This is typically called from the client process.
 */
DSP_STATUS CMM_GetHandle(DSP_HPROCESSOR hProcessor,
			OUT struct CMM_OBJECT **phCmmMgr)
{
	DSP_STATUS status = DSP_SOK;
	struct DEV_OBJECT *hDevObject;

	DBC_Require(cRefs > 0);
	DBC_Require(phCmmMgr != NULL);
	if (hProcessor != NULL)
		status = PROC_GetDevObject(hProcessor, &hDevObject);
	else
		hDevObject = DEV_GetFirst();	/* default */

	if (DSP_SUCCEEDED(status))
		status = DEV_GetCmmMgr(hDevObject, phCmmMgr);

	return status;
}

/*
 *  ======== CMM_GetInfo ========
 *  Purpose:
 *      Return the current memory utilization information.
 */
DSP_STATUS CMM_GetInfo(struct CMM_OBJECT *hCmmMgr,
		       OUT struct CMM_INFO *pCmmInfo)
{
	struct CMM_OBJECT *pCmmMgr = (struct CMM_OBJECT *)hCmmMgr;
	u32 ulSeg;
	DSP_STATUS status = DSP_SOK;
	struct CMM_ALLOCATOR *pAltr;
	struct CMM_MNODE *pCurNode = NULL;

	DBC_Require(pCmmInfo != NULL);

	if (!MEM_IsValidHandle(hCmmMgr, CMMSIGNATURE)) {
		status = DSP_EHANDLE;
		return status;
	}
	SYNC_EnterCS(pCmmMgr->hCmmLock);
	pCmmInfo->ulNumGPPSMSegs = 0;	/* # of SM segments */
	pCmmInfo->ulTotalInUseCnt = 0;	/* Total # of outstanding alloc */
	pCmmInfo->ulMinBlockSize = pCmmMgr->ulMinBlockSize; /* min block size */
	/* check SM memory segments */
	for (ulSeg = 1; ulSeg <= CMM_MAXGPPSEGS; ulSeg++) {
		/* get the allocator object for this segment id */
		pAltr = GetAllocator(pCmmMgr, ulSeg);
		if (pAltr != NULL) {
			pCmmInfo->ulNumGPPSMSegs++;
			pCmmInfo->segInfo[ulSeg - 1].dwSegBasePa =
				pAltr->dwSmBase - pAltr->ulDSPSize;
			pCmmInfo->segInfo[ulSeg - 1].ulTotalSegSize =
				pAltr->ulDSPSize + pAltr->ulSmSize;
			pCmmInfo->segInfo[ulSeg - 1].dwGPPBasePA =
				pAltr->dwSmBase;
			pCmmInfo->segInfo[ulSeg - 1].ulGPPSize =
				pAltr->ulSmSize;
			pCmmInfo->segInfo[ulSeg - 1].dwDSPBaseVA =
				pAltr->dwDSPBase;
			pCmmInfo->segInfo[ulSeg - 1].ulDSPSize =
				pAltr->ulDSPSize;
			pCmmInfo->segInfo[ulSeg - 1].dwSegBaseVa =
				pAltr->dwVmBase - pAltr->ulDSPSize;
			pCmmInfo->segInfo[ulSeg - 1].ulInUseCnt = 0;
			pCurNode = (struct CMM_MNODE *)LST_First(pAltr->
				pInUseListHead);
			/* Count inUse blocks */
			while (pCurNode) {
				pCmmInfo->ulTotalInUseCnt++;
				pCmmInfo->segInfo[ulSeg - 1].ulInUseCnt++;
				/* next node. */
				pCurNode = (struct CMM_MNODE *)LST_Next(pAltr->
					pInUseListHead,
					(struct LST_ELEM *)pCurNode);
			}
		}
	}		/* end for */
	SYNC_LeaveCS(pCmmMgr->hCmmLock);
	return status;
}

/*
 *  ======== CMM_Init ========
 *  Purpose:
 *      Initializes private state of CMM module.
 */
bool CMM_Init(void)
{
	bool fRetval = true;

	DBC_Require(cRefs >= 0);
	if (cRefs == 0) {
		/* Set the Trace mask */
		/* "CM" for Comm Memory manager */
		GT_create(&CMM_debugMask, "CM");
	}
	if (fRetval)
		cRefs++;

	GT_1trace(CMM_debugMask, GT_ENTER,
		  "Entered CMM_Init,ref count:0x%x\n", cRefs);

	DBC_Ensure((fRetval && (cRefs > 0)) || (!fRetval && (cRefs >= 0)));

	return fRetval;
}

/*
 *  ======== CMM_RegisterGPPSMSeg ========
 *  Purpose:
 *      Register a block of SM with the CMM to be used for later GPP SM
 *      allocations.
 */
DSP_STATUS CMM_RegisterGPPSMSeg(struct CMM_OBJECT *hCmmMgr, u32 dwGPPBasePA,
				u32 ulSize, u32 dwDSPAddrOffset,
				enum CMM_CNVTTYPE cFactor, u32 dwDSPBase,
				u32 ulDSPSize, u32 *pulSegId,
				u32 dwGPPBaseVA)
{
	struct CMM_OBJECT *pCmmMgr = (struct CMM_OBJECT *)hCmmMgr;
	struct CMM_ALLOCATOR *pSMA = NULL;
	DSP_STATUS status = DSP_SOK;
	struct CMM_MNODE *pNewNode;
	s32 nSlot;

	DBC_Require(ulSize > 0);
	DBC_Require(pulSegId != NULL);
	DBC_Require(dwGPPBasePA != 0);
	DBC_Require(dwGPPBaseVA != 0);
	DBC_Require((cFactor <= CMM_ADDTODSPPA) &&
		   (cFactor >= CMM_SUBFROMDSPPA));
	GT_6trace(CMM_debugMask, GT_ENTER,
		  "CMM_RegisterGPPSMSeg dwGPPBasePA %x "
		  "ulSize %x dwDSPAddrOffset %x dwDSPBase %x ulDSPSize %x "
		  "dwGPPBaseVA %x\n", dwGPPBasePA, ulSize, dwDSPAddrOffset,
		  dwDSPBase, ulDSPSize, dwGPPBaseVA);
	if (!MEM_IsValidHandle(hCmmMgr, CMMSIGNATURE)) {
		status = DSP_EHANDLE;
		return status;
	}
	/* make sure we have room for another allocator */
	SYNC_EnterCS(pCmmMgr->hCmmLock);
	nSlot = GetSlot(pCmmMgr);
	if (nSlot < 0) {
		/* get a slot number */
		status = DSP_EFAIL;
		goto func_end;
	}
	/* Check if input ulSize is big enough to alloc at least one block */
	if (DSP_SUCCEEDED(status)) {
		if (ulSize < pCmmMgr->ulMinBlockSize) {
			GT_0trace(CMM_debugMask, GT_7CLASS,
				  "CMM_RegisterGPPSMSeg: "
				  "ulSize too small\n");
			status = DSP_EINVALIDARG;
			goto func_end;
		}
	}
	if (DSP_SUCCEEDED(status)) {
		/* create, zero, and tag an SM allocator object */
		MEM_AllocObject(pSMA, struct CMM_ALLOCATOR, SMEMSIGNATURE);
	}
	if (pSMA != NULL) {
		pSMA->hCmmMgr = hCmmMgr;	/* ref to parent */
		pSMA->dwSmBase = dwGPPBasePA;	/* SM Base phys */
		pSMA->ulSmSize = ulSize;	/* SM segment size in bytes */
		pSMA->dwVmBase = dwGPPBaseVA;
		pSMA->dwDSPPhysAddrOffset = dwDSPAddrOffset;
		pSMA->cFactor = cFactor;
		pSMA->dwDSPBase = dwDSPBase;
		pSMA->ulDSPSize = ulDSPSize;
		if (pSMA->dwVmBase == 0) {
			GT_0trace(CMM_debugMask, GT_7CLASS,
				  "CMM_RegisterGPPSMSeg: Error"
				  "MEM_LinearAddress()\n");
			status = DSP_EFAIL;
			goto func_end;
		}
		if (DSP_SUCCEEDED(status)) {
			/* return the actual segment identifier */
			*pulSegId = (u32) nSlot + 1;
			/* create memory free list */
			pSMA->pFreeListHead = LST_Create();
			if (pSMA->pFreeListHead == NULL) {
				GT_0trace(CMM_debugMask, GT_7CLASS,
					  "CMM_RegisterGPPSMSeg: "
					  "Out Of Memory \n");
				status = DSP_EMEMORY;
				goto func_end;
			}
		}
		if (DSP_SUCCEEDED(status)) {
			/* create memory in-use list */
			pSMA->pInUseListHead = LST_Create();
			if (pSMA->pInUseListHead == NULL) {
				GT_0trace(CMM_debugMask, GT_7CLASS,
					  "CMM_RegisterGPPSMSeg: "
					  "LST_Create failed\n");
				status = DSP_EMEMORY;
				goto func_end;
			}
		}
		if (DSP_SUCCEEDED(status)) {
			/* Get a mem node for this hunk-o-memory */
			pNewNode = GetNode(pCmmMgr, dwGPPBasePA,
					   pSMA->dwVmBase, ulSize);
			/* Place node on the SM allocator's free list */
			if (pNewNode) {
				LST_PutTail(pSMA->pFreeListHead,
					   (struct LST_ELEM *)pNewNode);
			} else {
				status = DSP_EMEMORY;
				goto func_end;
			}
		}
		if (DSP_FAILED(status)) {
			/* Cleanup allocator */
			UnRegisterGPPSMSeg(pSMA);
		}
	} else {
		GT_0trace(CMM_debugMask, GT_6CLASS,
			  "CMM_RegisterGPPSMSeg: SMA Object "
			  "Allocation Failure\n");
		status = DSP_EMEMORY;
		goto func_end;
	}
	 /* make entry */
	if (DSP_SUCCEEDED(status))
		pCmmMgr->paGPPSMSegTab[nSlot] = pSMA;

func_end:
	SYNC_LeaveCS(pCmmMgr->hCmmLock);
	return status;
}

/*
 *  ======== CMM_UnRegisterGPPSMSeg ========
 *  Purpose:
 *      UnRegister GPP SM segments with the CMM.
 */
DSP_STATUS CMM_UnRegisterGPPSMSeg(struct CMM_OBJECT *hCmmMgr, u32 ulSegId)
{
	struct CMM_OBJECT *pCmmMgr = (struct CMM_OBJECT *)hCmmMgr;
	DSP_STATUS status = DSP_SOK;
	struct CMM_ALLOCATOR *pSMA;
	u32 ulId = ulSegId;

	DBC_Require(ulSegId > 0);
	if (MEM_IsValidHandle(hCmmMgr, CMMSIGNATURE)) {
		if (ulSegId == CMM_ALLSEGMENTS)
			ulId = 1;

		if ((ulId > 0) && (ulId <= CMM_MAXGPPSEGS)) {
			while (ulId <= CMM_MAXGPPSEGS) {
				SYNC_EnterCS(pCmmMgr->hCmmLock);
				/* slot = segId-1 */
				pSMA = pCmmMgr->paGPPSMSegTab[ulId - 1];
				if (pSMA != NULL) {
					UnRegisterGPPSMSeg(pSMA);
					/* Set alctr ptr to NULL for future
					 * reuse */
					pCmmMgr->paGPPSMSegTab[ulId - 1] = NULL;
				} else if (ulSegId != CMM_ALLSEGMENTS) {
					status = DSP_EFAIL;
				}
				SYNC_LeaveCS(pCmmMgr->hCmmLock);
				if (ulSegId != CMM_ALLSEGMENTS)
					break;

				ulId++;
			}	/* end while */
		} else {
			status = DSP_EINVALIDARG;
			GT_0trace(CMM_debugMask, GT_7CLASS,
				  "CMM_UnRegisterGPPSMSeg: Bad "
				  "segment Id\n");
		}
	} else {
		status = DSP_EHANDLE;
	}
	return status;
}

/*
 *  ======== UnRegisterGPPSMSeg ========
 *  Purpose:
 *      UnRegister the SM allocator by freeing all its resources and
 *      nulling cmm mgr table entry.
 *  Note:
 *      This routine is always called within cmm lock crit sect.
 */
static void UnRegisterGPPSMSeg(struct CMM_ALLOCATOR *pSMA)
{
	struct CMM_MNODE *pCurNode = NULL;
	struct CMM_MNODE *pNextNode = NULL;

	DBC_Require(pSMA != NULL);
	if (pSMA->pFreeListHead != NULL) {
		/* free nodes on free list */
		pCurNode = (struct CMM_MNODE *)LST_First(pSMA->pFreeListHead);
		while (pCurNode) {
			pNextNode = (struct CMM_MNODE *)LST_Next(pSMA->
				     pFreeListHead,
				    (struct LST_ELEM *)pCurNode);
			LST_RemoveElem(pSMA->pFreeListHead,
				      (struct LST_ELEM *)pCurNode);
			MEM_Free((void *) pCurNode);
			/* next node. */
			pCurNode = pNextNode;
		}
		LST_Delete(pSMA->pFreeListHead);	/* delete freelist */
		/* free nodes on InUse list */
		pCurNode = (struct CMM_MNODE *)LST_First(pSMA->pInUseListHead);
		while (pCurNode) {
			pNextNode = (struct CMM_MNODE *)LST_Next(pSMA->
				    pInUseListHead,
				    (struct LST_ELEM *)pCurNode);
			LST_RemoveElem(pSMA->pInUseListHead,
				      (struct LST_ELEM *)pCurNode);
			MEM_Free((void *) pCurNode);
			/* next node. */
			pCurNode = pNextNode;
		}
		LST_Delete(pSMA->pInUseListHead);	/* delete InUse list */
	}
	if ((void *) pSMA->dwVmBase != NULL)
		MEM_UnmapLinearAddress((void *) pSMA->dwVmBase);

	/* Free allocator itself */
	MEM_FreeObject(pSMA);
}

/*
 *  ======== GetSlot ========
 *  Purpose:
 *      An available slot # is returned. Returns negative on failure.
 */
static s32 GetSlot(struct CMM_OBJECT *pCmmMgr)
{
	s32 nSlot = -1;		/* neg on failure */
	DBC_Require(pCmmMgr != NULL);
	/* get first available slot in cmm mgr SMSegTab[] */
	for (nSlot = 0; nSlot < CMM_MAXGPPSEGS; nSlot++) {
		if (pCmmMgr->paGPPSMSegTab[nSlot] == NULL)
			break;

	}
	if (nSlot == CMM_MAXGPPSEGS) {
		GT_0trace(CMM_debugMask, GT_7CLASS,
			  "CMM_RegisterGPPSMSeg: Allocator "
			  "entry failure, max exceeded\n");
		nSlot = -1;	/* failed */
	}
	return nSlot;
}

/*
 *  ======== GetNode ========
 *  Purpose:
 *      Get a memory node from freelist or create a new one.
 */
static struct CMM_MNODE *GetNode(struct CMM_OBJECT *pCmmMgr, u32 dwPA,
				 u32 dwVA, u32 ulSize)
{
	struct CMM_MNODE *pNode = NULL;

	DBC_Require(pCmmMgr != NULL);
	DBC_Require(dwPA != 0);
	DBC_Require(dwVA != 0);
	DBC_Require(ulSize != 0);
	/* Check cmm mgr's node freelist */
	if (LST_IsEmpty(pCmmMgr->pNodeFreeListHead)) {
		pNode = (struct CMM_MNODE *)MEM_Calloc(sizeof(struct CMM_MNODE),
			MEM_PAGED);
	} else {
		/* surely a valid element */
		/* (struct LST_ELEM*) pNode = LST_GetHead(pCmmMgr->
		 * pNodeFreeListHead);*/
		pNode = (struct CMM_MNODE *)LST_GetHead(pCmmMgr->
			pNodeFreeListHead);
	}
	if (pNode == NULL) {
		GT_0trace(CMM_debugMask, GT_7CLASS, "GetNode: Out Of Memory\n");
	} else {
		LST_InitElem((struct LST_ELEM *) pNode);	/* set self */
		pNode->dwPA = dwPA;	/* Physical addr of start of block */
		pNode->dwVA = dwVA;	/* Virtual   "            "        */
		pNode->ulSize = ulSize;	/* Size of block */
	}
	return pNode;
}

/*
 *  ======== DeleteNode ========
 *  Purpose:
 *      Put a memory node on the cmm nodelist for later use.
 *      Doesn't actually delete the node. Heap thrashing friendly.
 */
static void DeleteNode(struct CMM_OBJECT *pCmmMgr, struct CMM_MNODE *pNode)
{
	DBC_Require(pNode != NULL);
	LST_InitElem((struct LST_ELEM *) pNode);	/* init .self ptr */
	LST_PutTail(pCmmMgr->pNodeFreeListHead, (struct LST_ELEM *) pNode);
}

/*
 * ====== GetFreeBlock ========
 *  Purpose:
 *      Scan the free block list and return the first block that satisfies
 *      the size.
 */
static struct CMM_MNODE *GetFreeBlock(struct CMM_ALLOCATOR *pAllocator,
				      u32 uSize)
{
	if (pAllocator) {
		struct CMM_MNODE *pCurNode = (struct CMM_MNODE *)
					LST_First(pAllocator->pFreeListHead);
		while (pCurNode) {
			if (uSize <= (u32) pCurNode->ulSize) {
				LST_RemoveElem(pAllocator->pFreeListHead,
					      (struct LST_ELEM *)pCurNode);
				return pCurNode;
			}
			/* next node. */
			pCurNode = (struct CMM_MNODE *)LST_Next(pAllocator->
				    pFreeListHead, (struct LST_ELEM *)pCurNode);
		}
	}
	return NULL;
}

/*
 *  ======== AddToFreeList ========
 *  Purpose:
 *      Coelesce node into the freelist in ascending size order.
 */
static void AddToFreeList(struct CMM_ALLOCATOR *pAllocator,
			  struct CMM_MNODE *pNode)
{
	struct CMM_MNODE *pNodePrev = NULL;
	struct CMM_MNODE *pNodeNext = NULL;
	struct CMM_MNODE *pCurNode;
	u32 dwThisPA;
	u32 dwNextPA;

	DBC_Require(pNode != NULL);
	DBC_Require(pAllocator != NULL);
	dwThisPA = pNode->dwPA;
	dwNextPA = NEXT_PA(pNode);
	pCurNode = (struct CMM_MNODE *)LST_First(pAllocator->pFreeListHead);
	while (pCurNode) {
		if (dwThisPA == NEXT_PA(pCurNode)) {
			/* found the block ahead of this one */
			pNodePrev = pCurNode;
		} else if (dwNextPA == pCurNode->dwPA) {
			pNodeNext = pCurNode;
		}
		if ((pNodePrev == NULL) || (pNodeNext == NULL)) {
			/* next node. */
			pCurNode = (struct CMM_MNODE *)LST_Next(pAllocator->
				    pFreeListHead, (struct LST_ELEM *)pCurNode);
		} else {
			/* got 'em */
			break;
		}
	}			/* while */
	if (pNodePrev != NULL) {
		/* combine with previous block */
		LST_RemoveElem(pAllocator->pFreeListHead,
			      (struct LST_ELEM *)pNodePrev);
		/* grow node to hold both */
		pNode->ulSize += pNodePrev->ulSize;
		pNode->dwPA = pNodePrev->dwPA;
		pNode->dwVA = pNodePrev->dwVA;
		/* place node on mgr nodeFreeList */
		DeleteNode((struct CMM_OBJECT *)pAllocator->hCmmMgr, pNodePrev);
	}
	if (pNodeNext != NULL) {
		/* combine with next block */
		LST_RemoveElem(pAllocator->pFreeListHead,
			      (struct LST_ELEM *)pNodeNext);
		/* grow da node */
		pNode->ulSize += pNodeNext->ulSize;
		/* place node on mgr nodeFreeList */
		DeleteNode((struct CMM_OBJECT *)pAllocator->hCmmMgr, pNodeNext);
	}
	/* Now, let's add to freelist in increasing size order */
	pCurNode = (struct CMM_MNODE *)LST_First(pAllocator->pFreeListHead);
	while (pCurNode) {
		if (pNode->ulSize <= pCurNode->ulSize)
			break;

		/* next node. */
		pCurNode = (struct CMM_MNODE *)LST_Next(pAllocator->
			   pFreeListHead, (struct LST_ELEM *)pCurNode);
	}
	/* if pCurNode is NULL then add our pNode to the end of the freelist */
	if (pCurNode == NULL) {
		LST_PutTail(pAllocator->pFreeListHead,
			   (struct LST_ELEM *)pNode);
	} else {
		/* insert our node before the current traversed node */
		LST_InsertBefore(pAllocator->pFreeListHead,
				(struct LST_ELEM *)pNode,
				(struct LST_ELEM *)pCurNode);
	}
}

/*
 * ======== GetAllocator ========
 *  Purpose:
 *      Return the allocator for the given SM Segid.
 *      SegIds:  1,2,3..max.
 */
static struct CMM_ALLOCATOR *GetAllocator(struct CMM_OBJECT *pCmmMgr,
					  u32 ulSegId)
{
	struct CMM_ALLOCATOR *pAllocator = NULL;

	DBC_Require(pCmmMgr != NULL);
	DBC_Require((ulSegId > 0) && (ulSegId <= CMM_MAXGPPSEGS));
	pAllocator = pCmmMgr->paGPPSMSegTab[ulSegId - 1];
	if (pAllocator != NULL) {
		/* make sure it's for real */
		if (!MEM_IsValidHandle(pAllocator, SMEMSIGNATURE)) {
			pAllocator = NULL;
			DBC_Assert(false);
		}
	}
	return pAllocator;
}

/*
 *  ======== CMM_XlatorCreate ========
 *  Purpose:
 *      Create an address translator object.
 */
DSP_STATUS CMM_XlatorCreate(OUT struct CMM_XLATOROBJECT **phXlator,
				struct CMM_OBJECT *hCmmMgr,
				struct CMM_XLATORATTRS *pXlatorAttrs)
{
	struct CMM_XLATOR *pXlatorObject = NULL;
	DSP_STATUS status = DSP_SOK;

	DBC_Require(cRefs > 0);
	DBC_Require(phXlator != NULL);
	DBC_Require(hCmmMgr != NULL);
	GT_3trace(CMM_debugMask, GT_ENTER,
		  "CMM_XlatorCreate: phXlator: 0x%x\t"
		  "phCmmMgr: 0x%x\tpXlAttrs: 0x%x\n", phXlator,
		  hCmmMgr, pXlatorAttrs);
	*phXlator = NULL;
	if (pXlatorAttrs == NULL)
		pXlatorAttrs = &CMM_DFLTXLATORATTRS;	/* set defaults */

	MEM_AllocObject(pXlatorObject, struct CMM_XLATOR, CMMXLATESIGNATURE);
	if (pXlatorObject != NULL) {
		pXlatorObject->hCmmMgr = hCmmMgr;	/* ref back to CMM */
		pXlatorObject->ulSegId = pXlatorAttrs->ulSegId;	/* SM segId */
	} else {
		GT_0trace(CMM_debugMask, GT_6CLASS,
			  "CMM_XlatorCreate: Object Allocation"
			  "Failure(CMM Xlator)\n");
		status = DSP_EMEMORY;
	}
	if (DSP_SUCCEEDED(status))
		*phXlator = (struct CMM_XLATOROBJECT *) pXlatorObject;

	return status;
}

/*
 *  ======== CMM_XlatorDelete ========
 *  Purpose:
 *      Free the Xlator resources.
 *      VM gets freed later.
 */
DSP_STATUS CMM_XlatorDelete(struct CMM_XLATOROBJECT *hXlator, bool bForce)
{
	struct CMM_XLATOR *pXlator = (struct CMM_XLATOR *)hXlator;
	DSP_STATUS status = DSP_SOK;

	DBC_Require(cRefs > 0);

	if (MEM_IsValidHandle(pXlator, CMMXLATESIGNATURE)) {
		MEM_FreeObject(pXlator);
	} else {
		status = DSP_EHANDLE;
	}

	return status;
}

/*
 *  ======== CMM_XlatorAllocBuf ========
 */
void *CMM_XlatorAllocBuf(struct CMM_XLATOROBJECT *hXlator, void *pVaBuf,
			u32 uPaSize)
{
	struct CMM_XLATOR *pXlator = (struct CMM_XLATOR *)hXlator;
	void *pBuf = NULL;
	struct CMM_ATTRS attrs;

	DBC_Require(cRefs > 0);
	DBC_Require(hXlator != NULL);
	DBC_Require(pXlator->hCmmMgr != NULL);
	DBC_Require(pVaBuf != NULL);
	DBC_Require(uPaSize > 0);
	DBC_Require(pXlator->ulSegId > 0);

	if (MEM_IsValidHandle(pXlator, CMMXLATESIGNATURE)) {
		attrs.ulSegId = pXlator->ulSegId;
		*(volatile u32 *)pVaBuf = 0;
		/* Alloc SM */
		pBuf = CMM_CallocBuf(pXlator->hCmmMgr, uPaSize, &attrs,  NULL);
		if (pBuf) {
			/* convert to translator(node/strm) process Virtual
			 * address */
			*(volatile u32 **)pVaBuf =
				 (u32 *)CMM_XlatorTranslate(hXlator,
							      pBuf, CMM_PA2VA);
		}
	}
	return pBuf;
}

/*
 *  ======== CMM_XlatorFreeBuf ========
 *  Purpose:
 *      Free the given SM buffer and descriptor.
 *      Does not free virtual memory.
 */
DSP_STATUS CMM_XlatorFreeBuf(struct CMM_XLATOROBJECT *hXlator, void *pBufVa)
{
	struct CMM_XLATOR *pXlator = (struct CMM_XLATOR *)hXlator;
	DSP_STATUS status = DSP_EFAIL;
	void *pBufPa = NULL;

	DBC_Require(cRefs > 0);
	DBC_Require(pBufVa != NULL);
	DBC_Require(pXlator->ulSegId > 0);

	if (MEM_IsValidHandle(pXlator, CMMXLATESIGNATURE)) {
		/* convert Va to Pa so we can free it. */
		pBufPa = CMM_XlatorTranslate(hXlator, pBufVa, CMM_VA2PA);
		if (pBufPa) {
			status = CMM_FreeBuf(pXlator->hCmmMgr, pBufPa,
					     pXlator->ulSegId);
			if (DSP_FAILED(status)) {
				/* Uh oh, this shouldn't happen. Descriptor
				 * gone! */
				GT_2trace(CMM_debugMask, GT_7CLASS,
					"Cannot free DMA/ZCPY buffer"
					"not allocated by MPU. PA %x, VA %x\n",
					pBufPa, pBufVa);
				DBC_Assert(false);   /* CMM is leaking mem! */
			}
		}
	}
	return status;
}

/*
 *  ======== CMM_XlatorInfo ========
 *  Purpose:
 *      Set/Get translator info.
 */
DSP_STATUS CMM_XlatorInfo(struct CMM_XLATOROBJECT *hXlator, IN OUT u8 **pAddr,
			 u32 ulSize, u32 uSegId, bool bSetInfo)
{
	struct CMM_XLATOR *pXlator = (struct CMM_XLATOR *)hXlator;
	DSP_STATUS status = DSP_SOK;

	DBC_Require(cRefs > 0);
	DBC_Require(pAddr != NULL);
	DBC_Require((uSegId > 0) && (uSegId <= CMM_MAXGPPSEGS));

	if (MEM_IsValidHandle(pXlator, CMMXLATESIGNATURE)) {
		if (bSetInfo) {
			/* set translators virtual address range */
			pXlator->dwVirtBase = (u32)*pAddr;
			pXlator->ulVirtSize = ulSize;
			GT_2trace(CMM_debugMask, GT_3CLASS,
				  "pXlator->dwVirtBase %x, "
				  "ulVirtSize %x\n", pXlator->dwVirtBase,
				  pXlator->ulVirtSize);
		} else {	/* return virt base address */
			*pAddr = (u8 *)pXlator->dwVirtBase;
		}
	} else {
		status = DSP_EHANDLE;
	}
	return status;
}

/*
 *  ======== CMM_XlatorTranslate ========
 */
void *CMM_XlatorTranslate(struct CMM_XLATOROBJECT *hXlator, void *pAddr,
			  enum CMM_XLATETYPE xType)
{
	u32 dwAddrXlate = 0;
	struct CMM_XLATOR *pXlator = (struct CMM_XLATOR *)hXlator;
	struct CMM_OBJECT *pCmmMgr = NULL;
	struct CMM_ALLOCATOR *pAlctr = NULL;
	u32 dwOffset = 0;

	DBC_Require(cRefs > 0);
	DBC_Require(pAddr != NULL);
	DBC_Require((xType >= CMM_VA2PA) && (xType <= CMM_DSPPA2PA));

	if (!MEM_IsValidHandle(pXlator, CMMXLATESIGNATURE))
		goto loop_cont;

	pCmmMgr = (struct CMM_OBJECT *)pXlator->hCmmMgr;
	/* get this translator's default SM allocator */
	DBC_Assert(pXlator->ulSegId > 0);
	pAlctr = pCmmMgr->paGPPSMSegTab[pXlator->ulSegId - 1];
	if (!MEM_IsValidHandle(pAlctr, SMEMSIGNATURE))
		goto loop_cont;

	if ((xType == CMM_VA2DSPPA) || (xType == CMM_VA2PA) ||
	    (xType == CMM_PA2VA)) {
		if (xType == CMM_PA2VA) {
			/* Gpp Va = Va Base + offset */
			dwOffset = (u8 *)pAddr - (u8 *)(pAlctr->dwSmBase -
				    pAlctr->ulDSPSize);
			dwAddrXlate = pXlator->dwVirtBase + dwOffset;
			/* Check if translated Va base is in range */
			if ((dwAddrXlate < pXlator->dwVirtBase) ||
			   (dwAddrXlate >=
			   (pXlator->dwVirtBase + pXlator->ulVirtSize))) {
				dwAddrXlate = 0;	/* bad address */
				GT_0trace(CMM_debugMask, GT_7CLASS,
					  "CMM_XlatorTranslate: "
					  "Virt addr out of range\n");
			}
		} else {
			/* Gpp PA =  Gpp Base + offset */
			dwOffset = (u8 *)pAddr - (u8 *)pXlator->dwVirtBase;
			dwAddrXlate = pAlctr->dwSmBase - pAlctr->ulDSPSize +
				      dwOffset;
		}
	} else {
		dwAddrXlate = (u32)pAddr;
	}
	 /*Now convert address to proper target physical address if needed*/
	if ((xType == CMM_VA2DSPPA) || (xType == CMM_PA2DSPPA)) {
		/* Got Gpp Pa now, convert to DSP Pa */
		dwAddrXlate = GPPPA2DSPPA((pAlctr->dwSmBase - pAlctr->
					 ulDSPSize), dwAddrXlate,
					 pAlctr->dwDSPPhysAddrOffset *
					 pAlctr->cFactor);
	} else if (xType == CMM_DSPPA2PA) {
		/* Got DSP Pa, convert to GPP Pa */
		dwAddrXlate = DSPPA2GPPPA(pAlctr->dwSmBase - pAlctr->ulDSPSize,
					  dwAddrXlate,
					  pAlctr->dwDSPPhysAddrOffset *
					  pAlctr->cFactor);
	}
loop_cont:
	if (!dwAddrXlate) {
		GT_2trace(CMM_debugMask, GT_7CLASS,
			  "CMM_XlatorTranslate: Can't translate"
			  " address: 0x%x xType %x\n", pAddr, xType);
	} else {
		GT_3trace(CMM_debugMask, GT_3CLASS,
			  "CMM_XlatorTranslate: pAddr %x, xType"
			  " %x, dwAddrXlate %x\n", pAddr, xType, dwAddrXlate);
	}
	return (void *)dwAddrXlate;
}
