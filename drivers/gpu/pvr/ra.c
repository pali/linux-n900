/**********************************************************************
 *
 * Copyright(c) 2008 Imagination Technologies Ltd. All rights reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope it will be useful but, except 
 * as otherwise stated in writing, without any warranty; without even the 
 * implied warranty of merchantability or fitness for a particular purpose. 
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * 
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * Imagination Technologies Ltd. <gpl-support@imgtec.com>
 * Home Park Estate, Kings Langley, Herts, WD4 8LZ, UK 
 *
 ******************************************************************************/

#include "services_headers.h"
#include "hash.h"
#include "ra.h"
#include "buffer_manager.h"
#include "osfunc.h"

#include <linux/kernel.h>
#include "proc.h"


#define MINIMUM_HASH_SIZE (64)

struct _BT_ {
	enum bt_type {
		btt_span,
		btt_free,
		btt_live
	} type;

	IMG_UINTPTR_T base;
	IMG_SIZE_T uSize;

	struct _BT_ *pNextSegment;
	struct _BT_ *pPrevSegment;

	struct _BT_ *pNextFree;
	struct _BT_ *pPrevFree;

	BM_MAPPING *psMapping;
};
typedef struct _BT_ BT;

struct _RA_ARENA_ {

	char *name;

	IMG_UINT32 uQuantum;

	 IMG_BOOL(*pImportAlloc) (void *,
				  IMG_SIZE_T uSize,
				  IMG_SIZE_T * pActualSize,
				  BM_MAPPING ** ppsMapping,
				  IMG_UINT32 uFlags, IMG_UINTPTR_T * pBase);
	void (*pImportFree) (void *, IMG_UINTPTR_T, BM_MAPPING * psMapping);
	void (*pBackingStoreFree) (void *, IMG_UINT32, IMG_UINT32, IMG_HANDLE);

	void *pImportHandle;

#define FREE_TABLE_LIMIT 32

	BT *aHeadFree[FREE_TABLE_LIMIT];

	BT *pHeadSegment;
	BT *pTailSegment;

	HASH_TABLE *pSegmentHash;

#ifdef RA_STATS
	RA_STATISTICS sStatistics;
#endif

#if defined(CONFIG_PROC_FS) && defined(DEBUG)
#define PROC_NAME_SIZE		32
	char szProcInfoName[PROC_NAME_SIZE];
	char szProcSegsName[PROC_NAME_SIZE];
#endif
};

void RA_Dump(RA_ARENA * pArena);

#if defined(CONFIG_PROC_FS) && defined(DEBUG)
static int
RA_DumpSegs(char *page, char **start, off_t off, int count, int *eof,
	    void *data);
static int RA_DumpInfo(char *page, char **start, off_t off, int count, int *eof,
		       void *data);
#endif


static IMG_BOOL
_RequestAllocFail(void *_h,
		  IMG_SIZE_T _uSize,
		  IMG_SIZE_T * _pActualSize,
		  BM_MAPPING ** _ppsMapping,
		  IMG_UINT32 _uFlags, IMG_UINTPTR_T * _pBase)
{
	PVR_UNREFERENCED_PARAMETER(_h);
	PVR_UNREFERENCED_PARAMETER(_uSize);
	PVR_UNREFERENCED_PARAMETER(_pActualSize);
	PVR_UNREFERENCED_PARAMETER(_ppsMapping);
	PVR_UNREFERENCED_PARAMETER(_uFlags);
	PVR_UNREFERENCED_PARAMETER(_pBase);

	return IMG_FALSE;
}

static IMG_UINT32 pvr_log2(IMG_SIZE_T n)
{
	IMG_UINT32 l = 0;
	n >>= 1;
	while (n > 0) {
		n >>= 1;
		l++;
	}
	return l;
}

static void
_SegmentListInsertAfter(RA_ARENA * pArena, BT * pInsertionPoint, BT * pBT)
{
	PVR_ASSERT(pArena != IMG_NULL);
	PVR_ASSERT(pInsertionPoint != IMG_NULL);

	pBT->pNextSegment = pInsertionPoint->pNextSegment;
	pBT->pPrevSegment = pInsertionPoint;
	if (pInsertionPoint->pNextSegment == IMG_NULL)
		pArena->pTailSegment = pBT;
	else
		pInsertionPoint->pNextSegment->pPrevSegment = pBT;
	pInsertionPoint->pNextSegment = pBT;
}

static void _SegmentListInsert(RA_ARENA * pArena, BT * pBT)
{

	if (pArena->pHeadSegment == IMG_NULL) {
		pArena->pHeadSegment = pArena->pTailSegment = pBT;
		pBT->pNextSegment = pBT->pPrevSegment = IMG_NULL;
	} else {
		BT *pBTScan;
		pBTScan = pArena->pHeadSegment;
		while (pBTScan->pNextSegment != IMG_NULL
		       && pBT->base >= pBTScan->pNextSegment->base)
			pBTScan = pBTScan->pNextSegment;
		_SegmentListInsertAfter(pArena, pBTScan, pBT);
	}
}

static void _SegmentListRemove(RA_ARENA * pArena, BT * pBT)
{
	if (pBT->pPrevSegment == IMG_NULL)
		pArena->pHeadSegment = pBT->pNextSegment;
	else
		pBT->pPrevSegment->pNextSegment = pBT->pNextSegment;

	if (pBT->pNextSegment == IMG_NULL)
		pArena->pTailSegment = pBT->pPrevSegment;
	else
		pBT->pNextSegment->pPrevSegment = pBT->pPrevSegment;
}

static BT *_SegmentSplit(RA_ARENA * pArena, BT * pBT, IMG_SIZE_T uSize)
{
	BT *pNeighbour;

	PVR_ASSERT(pArena != IMG_NULL);

	if (OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
		       sizeof(BT),
		       (IMG_VOID **) & pNeighbour, IMG_NULL) != PVRSRV_OK) {
		return IMG_NULL;
	}

	pNeighbour->pPrevSegment = pBT;
	pNeighbour->pNextSegment = pBT->pNextSegment;
	if (pBT->pNextSegment == IMG_NULL)
		pArena->pTailSegment = pNeighbour;
	else
		pBT->pNextSegment->pPrevSegment = pNeighbour;
	pBT->pNextSegment = pNeighbour;

	pNeighbour->type = btt_free;
	pNeighbour->uSize = pBT->uSize - uSize;
	pNeighbour->base = pBT->base + uSize;
	pNeighbour->psMapping = pBT->psMapping;
	pBT->uSize = uSize;
	return pNeighbour;
}

static void _FreeListInsert(RA_ARENA * pArena, BT * pBT)
{
	IMG_UINT32 uIndex;
	uIndex = pvr_log2(pBT->uSize);
	pBT->type = btt_free;
	pBT->pNextFree = pArena->aHeadFree[uIndex];
	pBT->pPrevFree = IMG_NULL;
	if (pArena->aHeadFree[uIndex] != IMG_NULL)
		pArena->aHeadFree[uIndex]->pPrevFree = pBT;
	pArena->aHeadFree[uIndex] = pBT;
}

static void _FreeListRemove(RA_ARENA * pArena, BT * pBT)
{
	IMG_UINT32 uIndex;
	uIndex = pvr_log2(pBT->uSize);
	if (pBT->pNextFree != IMG_NULL)
		pBT->pNextFree->pPrevFree = pBT->pPrevFree;
	if (pBT->pPrevFree == IMG_NULL)
		pArena->aHeadFree[uIndex] = pBT->pNextFree;
	else
		pBT->pPrevFree->pNextFree = pBT->pNextFree;
}

static BT *_BuildSpanMarker(IMG_UINTPTR_T base, IMG_SIZE_T uSize)
{
	BT *pBT;

	if (OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
		       sizeof(BT),
		       (IMG_VOID **) & pBT, IMG_NULL) != PVRSRV_OK) {
		return IMG_NULL;
	}

	pBT->type = btt_span;
	pBT->base = base;
	pBT->uSize = uSize;
	pBT->psMapping = IMG_NULL;

	return pBT;
}

static BT *_BuildBT(IMG_UINTPTR_T base, IMG_SIZE_T uSize)
{
	BT *pBT;

	if (OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
		       sizeof(BT),
		       (IMG_VOID **) & pBT, IMG_NULL) != PVRSRV_OK) {
		return IMG_NULL;
	}

	pBT->type = btt_free;
	pBT->base = base;
	pBT->uSize = uSize;

	return pBT;
}

static BT *_InsertResource(RA_ARENA * pArena, IMG_UINTPTR_T base,
			   IMG_SIZE_T uSize)
{
	BT *pBT;
	PVR_ASSERT(pArena != IMG_NULL);
	pBT = _BuildBT(base, uSize);
	if (pBT != IMG_NULL) {
		_SegmentListInsert(pArena, pBT);
		_FreeListInsert(pArena, pBT);
#ifdef RA_STATS
		pArena->sStatistics.uTotalResourceCount += uSize;
		pArena->sStatistics.uFreeResourceCount += uSize;
		pArena->sStatistics.uSpanCount++;
#endif
	}
	return pBT;
}

static BT *_InsertResourceSpan(RA_ARENA * pArena, IMG_UINTPTR_T base,
			       IMG_SIZE_T uSize)
{
	BT *pSpanStart;
	BT *pSpanEnd;
	BT *pBT;

	PVR_ASSERT(pArena != IMG_NULL);

	PVR_DPF((PVR_DBG_MESSAGE,
		 "RA_InsertResourceSpan: arena='%s', base=0x%x, size=0x%x",
		 pArena->name, base, uSize));

	pSpanStart = _BuildSpanMarker(base, uSize);
	if (pSpanStart == IMG_NULL) {
		goto fail_start;
	}
	pSpanEnd = _BuildSpanMarker(base + uSize, 0);
	if (pSpanEnd == IMG_NULL) {
		goto fail_end;
	}

	pBT = _BuildBT(base, uSize);
	if (pBT == IMG_NULL) {
		goto fail_bt;
	}

	_SegmentListInsert(pArena, pSpanStart);
	_SegmentListInsertAfter(pArena, pSpanStart, pBT);
	_FreeListInsert(pArena, pBT);
	_SegmentListInsertAfter(pArena, pBT, pSpanEnd);
#ifdef RA_STATS
	pArena->sStatistics.uTotalResourceCount += uSize;
#endif
	return pBT;

fail_bt:
	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(BT), pSpanEnd, IMG_NULL);
fail_end:
	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(BT), pSpanStart, IMG_NULL);
fail_start:
	return IMG_NULL;
}

static void _FreeBT(RA_ARENA * pArena, BT * pBT, IMG_BOOL bFreeBackingStore)
{
	BT *pNeighbour;
	IMG_UINTPTR_T uOrigBase;
	IMG_SIZE_T uOrigSize;

	PVR_ASSERT(pArena != IMG_NULL);
	PVR_ASSERT(pBT != IMG_NULL);

#ifdef RA_STATS
	pArena->sStatistics.uLiveSegmentCount--;
	pArena->sStatistics.uFreeSegmentCount++;
	pArena->sStatistics.uFreeResourceCount += pBT->uSize;
#endif

	uOrigBase = pBT->base;
	uOrigSize = pBT->uSize;

	pNeighbour = pBT->pPrevSegment;
	if (pNeighbour != IMG_NULL
	    && pNeighbour->type == btt_free
	    && pNeighbour->base + pNeighbour->uSize == pBT->base) {
		_FreeListRemove(pArena, pNeighbour);
		_SegmentListRemove(pArena, pNeighbour);
		pBT->base = pNeighbour->base;
		pBT->uSize += pNeighbour->uSize;
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(BT), pNeighbour,
			  IMG_NULL);
#ifdef RA_STATS
		pArena->sStatistics.uFreeSegmentCount--;
#endif
	}

	pNeighbour = pBT->pNextSegment;
	if (pNeighbour != IMG_NULL
	    && pNeighbour->type == btt_free
	    && pBT->base + pBT->uSize == pNeighbour->base) {
		_FreeListRemove(pArena, pNeighbour);
		_SegmentListRemove(pArena, pNeighbour);
		pBT->uSize += pNeighbour->uSize;
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(BT), pNeighbour,
			  IMG_NULL);
#ifdef RA_STATS
		pArena->sStatistics.uFreeSegmentCount--;
#endif
	}

	if (pArena->pBackingStoreFree != IMG_NULL && bFreeBackingStore) {
		IMG_UINTPTR_T uRoundedStart, uRoundedEnd;

		uRoundedStart =
		    (uOrigBase / pArena->uQuantum) * pArena->uQuantum;

		if (uRoundedStart < pBT->base) {
			uRoundedStart += pArena->uQuantum;
		}

		uRoundedEnd =
		    ((uOrigBase + uOrigSize + pArena->uQuantum -
		      1) / pArena->uQuantum) * pArena->uQuantum;

		if (uRoundedEnd > (pBT->base + pBT->uSize)) {
			uRoundedEnd -= pArena->uQuantum;
		}

		if (uRoundedStart < uRoundedEnd) {
			pArena->pBackingStoreFree(pArena->pImportHandle,
						  uRoundedStart, uRoundedEnd,
						  (IMG_HANDLE) 0);
		}
	}

	if (pBT->pNextSegment != IMG_NULL && pBT->pNextSegment->type == btt_span
	    && pBT->pPrevSegment != IMG_NULL
	    && pBT->pPrevSegment->type == btt_span) {
		BT *next = pBT->pNextSegment;
		BT *prev = pBT->pPrevSegment;
		_SegmentListRemove(pArena, next);
		_SegmentListRemove(pArena, prev);
		_SegmentListRemove(pArena, pBT);
		pArena->pImportFree(pArena->pImportHandle, pBT->base,
				    pBT->psMapping);
#ifdef RA_STATS
		pArena->sStatistics.uSpanCount--;
		pArena->sStatistics.uExportCount++;
		pArena->sStatistics.uFreeSegmentCount--;
		pArena->sStatistics.uFreeResourceCount -= pBT->uSize;
		pArena->sStatistics.uTotalResourceCount -= pBT->uSize;
#endif
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(BT), next, IMG_NULL);
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(BT), prev, IMG_NULL);
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(BT), pBT, IMG_NULL);
	} else
		_FreeListInsert(pArena, pBT);
}

static IMG_BOOL
_AttemptAllocAligned(RA_ARENA * pArena,
		     IMG_SIZE_T uSize,
		     BM_MAPPING ** ppsMapping,
		     IMG_UINT32 uFlags,
		     IMG_UINT32 uAlignment,
		     IMG_UINT32 uAlignmentOffset, IMG_UINTPTR_T * base)
{
	IMG_UINT32 uIndex;
	PVR_ASSERT(pArena != IMG_NULL);

	PVR_UNREFERENCED_PARAMETER(uFlags);

	if (uAlignment > 1)
		uAlignmentOffset %= uAlignment;

	uIndex = pvr_log2(uSize);


	while (uIndex < FREE_TABLE_LIMIT
	       && pArena->aHeadFree[uIndex] == IMG_NULL)
		uIndex++;

	while (uIndex < FREE_TABLE_LIMIT) {
		if (pArena->aHeadFree[uIndex] != IMG_NULL) {

			BT *pBT;

			pBT = pArena->aHeadFree[uIndex];
			while (pBT != IMG_NULL) {
				IMG_UINTPTR_T aligned_base;

				if (uAlignment > 1)
					aligned_base =
					    (pBT->base + uAlignmentOffset +
					     uAlignment -
					     1) / uAlignment * uAlignment -
					    uAlignmentOffset;
				else
					aligned_base = pBT->base;
				PVR_DPF((PVR_DBG_MESSAGE,
					 "RA_AttemptAllocAligned: pBT-base=0x%x "
					 "pBT-size=0x%x alignedbase=0x%x size=0x%x",
					 pBT->base, pBT->uSize, aligned_base,
					 uSize));

				if (pBT->base + pBT->uSize >=
				    aligned_base + uSize) {
					if (!pBT->psMapping
					    || pBT->psMapping->ui32Flags ==
					    uFlags) {
						_FreeListRemove(pArena, pBT);

						PVR_ASSERT(pBT->type ==
							   btt_free);

#ifdef RA_STATS
						pArena->sStatistics.
						    uLiveSegmentCount++;
						pArena->sStatistics.
						    uFreeSegmentCount--;
						pArena->sStatistics.
						    uFreeResourceCount -=
						    pBT->uSize;
#endif

						if (aligned_base > pBT->base) {
							BT *pNeighbour;

							pNeighbour =
							    _SegmentSplit
							    (pArena, pBT,
							     aligned_base -
							     pBT->base);

							if (pNeighbour ==
							    IMG_NULL) {
								PVR_DPF((PVR_DBG_ERROR, "_AttemptAllocAligned: Front split failed"));

								_FreeListInsert
								    (pArena,
								     pBT);
								return
								    IMG_FALSE;
							}

							_FreeListInsert(pArena,
									pBT);
#ifdef RA_STATS
							pArena->sStatistics.
							    uFreeSegmentCount++;
							pArena->sStatistics.
							    uFreeResourceCount
							    += pBT->uSize;
#endif
							pBT = pNeighbour;
						}

						if (pBT->uSize > uSize) {
							BT *pNeighbour;
							pNeighbour =
							    _SegmentSplit
							    (pArena, pBT,
							     uSize);

							if (pNeighbour ==
							    IMG_NULL) {
								PVR_DPF((PVR_DBG_ERROR, "_AttemptAllocAligned: Back split failed"));

								_FreeListInsert
								    (pArena,
								     pBT);
								return
								    IMG_FALSE;
							}

							_FreeListInsert(pArena,
									pNeighbour);
#ifdef RA_STATS
							pArena->sStatistics.
							    uFreeSegmentCount++;
							pArena->sStatistics.
							    uFreeResourceCount
							    +=
							    pNeighbour->uSize;
#endif
						}

						pBT->type = btt_live;

						if (!HASH_Insert
						    (pArena->pSegmentHash,
						     pBT->base,
						     (IMG_UINTPTR_T) pBT)) {
							_FreeBT(pArena, pBT,
								IMG_FALSE);
							return IMG_FALSE;
						}

						if (ppsMapping != IMG_NULL)
							*ppsMapping =
							    pBT->psMapping;

						*base = pBT->base;

						return IMG_TRUE;
					} else {
						PVR_DPF((PVR_DBG_MESSAGE,
							 "AttemptAllocAligned: mismatch in flags. Import has %x, request was %x",
							 pBT->psMapping->
							 ui32Flags, uFlags));

					}
				}
				pBT = pBT->pNextFree;
			}

		}
		uIndex++;
	}

	return IMG_FALSE;
}

RA_ARENA *RA_Create(IMG_CHAR * name,
		    IMG_UINTPTR_T base,
		    IMG_SIZE_T uSize,
		    BM_MAPPING * psMapping,
		    IMG_SIZE_T uQuantum,
		    IMG_BOOL(*alloc) (IMG_VOID *, IMG_SIZE_T uSize,
				      IMG_SIZE_T * pActualSize,
				      BM_MAPPING ** ppsMapping,
				      IMG_UINT32 _flags, IMG_UINTPTR_T * pBase),
		    IMG_VOID(*free) (IMG_VOID *, IMG_UINTPTR_T,
				     BM_MAPPING * psMapping),
		    IMG_VOID(*backingstore_free) (IMG_VOID *, IMG_UINT32,
						  IMG_UINT32, IMG_HANDLE),
		    IMG_VOID * pImportHandle)
{
	RA_ARENA *pArena;
	BT *pBT;
	int i;

	PVR_DPF((PVR_DBG_MESSAGE,
		 "RA_Create: name='%s', base=0x%x, uSize=0x%x, alloc=0x%x, free=0x%x",
		 name, base, uSize, alloc, free));

	if (OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
		       sizeof(*pArena),
		       (IMG_VOID **) & pArena, IMG_NULL) != PVRSRV_OK) {
		goto arena_fail;
	}

	pArena->name = name;
	pArena->pImportAlloc = alloc != IMG_NULL ? alloc : _RequestAllocFail;
	pArena->pImportFree = free;
	pArena->pBackingStoreFree = backingstore_free;
	pArena->pImportHandle = pImportHandle;
	for (i = 0; i < FREE_TABLE_LIMIT; i++)
		pArena->aHeadFree[i] = IMG_NULL;
	pArena->pHeadSegment = IMG_NULL;
	pArena->pTailSegment = IMG_NULL;
	pArena->uQuantum = uQuantum;

#ifdef RA_STATS
	pArena->sStatistics.uSpanCount = 0;
	pArena->sStatistics.uLiveSegmentCount = 0;
	pArena->sStatistics.uFreeSegmentCount = 0;
	pArena->sStatistics.uFreeResourceCount = 0;
	pArena->sStatistics.uTotalResourceCount = 0;
	pArena->sStatistics.uCumulativeAllocs = 0;
	pArena->sStatistics.uCumulativeFrees = 0;
	pArena->sStatistics.uImportCount = 0;
	pArena->sStatistics.uExportCount = 0;
#endif

#if defined(CONFIG_PROC_FS) && defined(DEBUG)
	if (strcmp(pArena->name, "") != 0) {
		sprintf(pArena->szProcInfoName, "ra_info_%s", pArena->name);
		CreateProcEntry(pArena->szProcInfoName, RA_DumpInfo, 0, pArena);
		sprintf(pArena->szProcSegsName, "ra_segs_%s", pArena->name);
		CreateProcEntry(pArena->szProcSegsName, RA_DumpSegs, 0, pArena);
	}
#endif

	pArena->pSegmentHash = HASH_Create(MINIMUM_HASH_SIZE);
	if (pArena->pSegmentHash == IMG_NULL) {
		goto hash_fail;
	}
	if (uSize > 0) {
		uSize = (uSize + uQuantum - 1) / uQuantum * uQuantum;
		pBT = _InsertResource(pArena, base, uSize);
		if (pBT == IMG_NULL) {
			goto insert_fail;
		}
		pBT->psMapping = psMapping;

	}
	return pArena;

insert_fail:
	HASH_Delete(pArena->pSegmentHash);
hash_fail:
	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(RA_ARENA), pArena, IMG_NULL);
arena_fail:
	return IMG_NULL;
}

void RA_Delete(RA_ARENA * pArena)
{
	IMG_UINT32 uIndex;

	PVR_ASSERT(pArena != IMG_NULL);
	PVR_DPF((PVR_DBG_MESSAGE, "RA_Delete: name='%s'", pArena->name));

	for (uIndex = 0; uIndex < FREE_TABLE_LIMIT; uIndex++)
		pArena->aHeadFree[uIndex] = IMG_NULL;

	while (pArena->pHeadSegment != IMG_NULL) {
		BT *pBT = pArena->pHeadSegment;
		PVR_ASSERT(pBT->type == btt_free);
		_SegmentListRemove(pArena, pBT);
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(BT), pBT, IMG_NULL);
#ifdef RA_STATS
		pArena->sStatistics.uSpanCount--;
#endif
	}
#if defined(CONFIG_PROC_FS) && defined(DEBUG)
	RemoveProcEntry(pArena->szProcInfoName);
	RemoveProcEntry(pArena->szProcSegsName);
#endif
	HASH_Delete(pArena->pSegmentHash);
	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(RA_ARENA), pArena, IMG_NULL);
}

IMG_BOOL RA_Add(RA_ARENA * pArena, IMG_UINTPTR_T base, IMG_SIZE_T uSize)
{
	PVR_ASSERT(pArena != IMG_NULL);

	PVR_DPF((PVR_DBG_MESSAGE,
		 "RA_Add: name='%s', base=0x%x, size=0x%x", pArena->name, base,
		 uSize));

	uSize =
	    (uSize + pArena->uQuantum -
	     1) / pArena->uQuantum * pArena->uQuantum;
	return ((IMG_BOOL) (_InsertResource(pArena, base, uSize) != IMG_NULL));
}

IMG_BOOL
RA_Alloc(RA_ARENA * pArena,
	 IMG_SIZE_T uRequestSize,
	 IMG_SIZE_T * pActualSize,
	 BM_MAPPING ** ppsMapping,
	 IMG_UINT32 uFlags,
	 IMG_UINT32 uAlignment,
	 IMG_UINT32 uAlignmentOffset, IMG_UINTPTR_T * base)
{
	IMG_BOOL bResult = IMG_FALSE;
	IMG_SIZE_T uSize = uRequestSize;

	PVR_ASSERT(pArena != IMG_NULL);


	if (pActualSize != IMG_NULL)
		*pActualSize = uSize;

	PVR_DPF((PVR_DBG_MESSAGE,
		 "RA_Alloc: arena='%s', size=0x%x(0x%x), alignment=0x%x, offset=0x%x",
		 pArena->name, uSize, uRequestSize, uAlignment,
		 uAlignmentOffset));

	bResult = _AttemptAllocAligned(pArena, uSize, ppsMapping, uFlags,
				       uAlignment, uAlignmentOffset, base);
	if (!bResult) {
		BM_MAPPING *psImportMapping;
		IMG_UINTPTR_T import_base;
		IMG_SIZE_T uImportSize = uSize;

		if (uAlignment > pArena->uQuantum) {
			uImportSize += (uAlignment - 1);
		}

		uImportSize =
		    ((uImportSize + pArena->uQuantum -
		      1) / pArena->uQuantum) * pArena->uQuantum;

		bResult =
		    pArena->pImportAlloc(pArena->pImportHandle, uImportSize,
					 &uImportSize, &psImportMapping, uFlags,
					 &import_base);
		if (bResult) {
			BT *pBT;
			pBT =
			    _InsertResourceSpan(pArena, import_base,
						uImportSize);

			if (pBT == IMG_NULL) {

				pArena->pImportFree(pArena->pImportHandle,
						    import_base,
						    psImportMapping);
				PVR_DPF((PVR_DBG_MESSAGE,
					 "RA_Alloc: name='%s', size=0x%x failed!",
					 pArena->name, uSize));

				return IMG_FALSE;
			}
			pBT->psMapping = psImportMapping;
#ifdef RA_STATS
			pArena->sStatistics.uFreeSegmentCount++;
			pArena->sStatistics.uFreeResourceCount += uImportSize;
			pArena->sStatistics.uImportCount++;
			pArena->sStatistics.uSpanCount++;
#endif
			bResult =
			    _AttemptAllocAligned(pArena, uSize, ppsMapping,
						 uFlags, uAlignment,
						 uAlignmentOffset, base);
			if (!bResult) {
				PVR_DPF((PVR_DBG_MESSAGE,
					 "RA_Alloc: name='%s' uAlignment failed!",
					 pArena->name));
			}
		}
	}
#ifdef RA_STATS
	if (bResult)
		pArena->sStatistics.uCumulativeAllocs++;
#endif

	PVR_DPF((PVR_DBG_MESSAGE,
		 "RA_Alloc: name='%s', size=0x%x, *base=0x%x = %d",
		 pArena->name, uSize, *base, bResult));

	return bResult;
}

void RA_Free(RA_ARENA * pArena, IMG_UINTPTR_T base, IMG_BOOL bFreeBackingStore)
{
	BT *pBT;

	PVR_ASSERT(pArena != IMG_NULL);


	PVR_DPF((PVR_DBG_MESSAGE,
		 "RA_Free: name='%s', base=0x%x", pArena->name, base));

	pBT = (BT *) HASH_Remove(pArena->pSegmentHash, base);
	PVR_ASSERT(pBT != IMG_NULL);

	if (pBT) {
		PVR_ASSERT(pBT->base == base);

#ifdef RA_STATS
		pArena->sStatistics.uCumulativeFrees++;
#endif

		_FreeBT(pArena, pBT, bFreeBackingStore);
	}
}

IMG_BOOL RA_GetNextLiveSegment(IMG_HANDLE hArena,
			       RA_SEGMENT_DETAILS * psSegDetails)
{
	BT *pBT;

	if (psSegDetails->hSegment) {
		pBT = (BT *) psSegDetails->hSegment;
	} else {
		RA_ARENA *pArena = (RA_ARENA *) hArena;

		pBT = pArena->pHeadSegment;
	}

	while (pBT != IMG_NULL) {
		if (pBT->type == btt_live) {
			psSegDetails->uiSize = pBT->uSize;
			psSegDetails->sCpuPhyAddr.uiAddr = pBT->base;
			psSegDetails->hSegment = (IMG_HANDLE) pBT->pNextSegment;

			return IMG_TRUE;
		}

		pBT = pBT->pNextSegment;
	}

	psSegDetails->uiSize = 0;
	psSegDetails->sCpuPhyAddr.uiAddr = 0;
	psSegDetails->hSegment = (IMG_HANDLE) - 1;

	return IMG_FALSE;
}


#if (defined(CONFIG_PROC_FS) && defined(DEBUG)) || defined (RA_STATS)
static char *_BTType(int eType)
{
	switch (eType) {
	case btt_span:
		return "span";
	case btt_free:
		return "free";
	case btt_live:
		return "live";
	}
	return "junk";
}
#endif

#if defined(CONFIG_PROC_FS) && defined(DEBUG)
static int
RA_DumpSegs(char *page, char **start, off_t off, int count, int *eof,
	    void *data)
{
	BT *pBT = 0;
	int len = 0;
	RA_ARENA *pArena = (RA_ARENA *) data;

	if (count < 80) {
		*start = (char *)0;
		return (0);
	}
	*eof = 0;
	*start = (char *)1;
	if (off == 0) {
		return printAppend(page, count, 0,
				   "Arena \"%s\"\nBase         Size Type Ref\n",
				   pArena->name);
	}
	for (pBT = pArena->pHeadSegment; --off && pBT;
	     pBT = pBT->pNextSegment) ;
	if (pBT) {
		len = printAppend(page, count, 0, "%08x %8x %4s %08x\n",
				  (unsigned int)pBT->base,
				  (unsigned int)pBT->uSize, _BTType(pBT->type),
				  (unsigned int)pBT->psMapping);
	} else {
		*eof = 1;
	}
	return (len);
}

static int
RA_DumpInfo(char *page, char **start, off_t off, int count, int *eof,
	    void *data)
{
	int len = 0;
	RA_ARENA *pArena = (RA_ARENA *) data;

	if (count < 80) {
		*start = (char *)0;
		return (0);
	}
	*eof = 0;
	switch (off) {
	case 0:
		len =
		    printAppend(page, count, 0, "quantum\t\t\t%lu\n",
				pArena->uQuantum);
		break;
	case 1:
		len =
		    printAppend(page, count, 0, "import_handle\t\t%08X\n",
				(unsigned int)pArena->pImportHandle);
		break;
#ifdef RA_STATS
	case 2:
		len =
		    printAppend(page, count, 0, "span count\t\t%lu\n",
				pArena->sStatistics.uSpanCount);
		break;
	case 3:
		len =
		    printAppend(page, count, 0, "live segment count\t%lu\n",
				pArena->sStatistics.uLiveSegmentCount);
		break;
	case 4:
		len =
		    printAppend(page, count, 0, "free segment count\t%lu\n",
				pArena->sStatistics.uFreeSegmentCount);
		break;
	case 5:
		len =
		    printAppend(page, count, 0,
				"free resource count\t%lu (0x%x)\n",
				pArena->sStatistics.uFreeResourceCount,
				(unsigned int)pArena->sStatistics.
				uFreeResourceCount);
		break;
	case 6:
		len =
		    printAppend(page, count, 0, "total allocs\t\t%lu\n",
				pArena->sStatistics.uCumulativeAllocs);
		break;
	case 7:
		len =
		    printAppend(page, count, 0, "total frees\t\t%lu\n",
				pArena->sStatistics.uCumulativeFrees);
		break;
	case 8:
		len =
		    printAppend(page, count, 0, "import count\t\t%lu\n",
				pArena->sStatistics.uImportCount);
		break;
	case 9:
		len =
		    printAppend(page, count, 0, "export count\t\t%lu\n",
				pArena->sStatistics.uExportCount);
		break;
#endif

	default:
		*eof = 1;
	}
	*start = (char *)1;
	return (len);
}
#endif

#ifdef RA_STATS
PVRSRV_ERROR RA_GetStats(RA_ARENA * pArena,
			 IMG_CHAR ** ppszStr, IMG_UINT32 * pui32StrLen)
{
	IMG_CHAR *pszStr = *ppszStr;
	IMG_UINT32 ui32StrLen = *pui32StrLen;
	IMG_INT32 i32Count;
	BT *pBT;

	CHECK_SPACE(ui32StrLen);
	i32Count = OSSNPrintf(pszStr, 100, "\nArena '%s':\n", pArena->name);
	UPDATE_SPACE(pszStr, i32Count, ui32StrLen);

	CHECK_SPACE(ui32StrLen);
	i32Count =
	    OSSNPrintf(pszStr, 100,
		       "  allocCB=%08X freeCB=%08X handle=%08X quantum=%d\n",
		       pArena->pImportAlloc, pArena->pImportFree,
		       pArena->pImportHandle, pArena->uQuantum);
	UPDATE_SPACE(pszStr, i32Count, ui32StrLen);

	CHECK_SPACE(ui32StrLen);
	i32Count =
	    OSSNPrintf(pszStr, 100, "span count\t\t%lu\n",
		       pArena->sStatistics.uSpanCount);
	UPDATE_SPACE(pszStr, i32Count, ui32StrLen);

	CHECK_SPACE(ui32StrLen);
	i32Count =
	    OSSNPrintf(pszStr, 100, "live segment count\t%lu\n",
		       pArena->sStatistics.uLiveSegmentCount);
	UPDATE_SPACE(pszStr, i32Count, ui32StrLen);

	CHECK_SPACE(ui32StrLen);
	i32Count =
	    OSSNPrintf(pszStr, 100, "free segment count\t%lu\n",
		       pArena->sStatistics.uFreeSegmentCount);
	UPDATE_SPACE(pszStr, i32Count, ui32StrLen);

	CHECK_SPACE(ui32StrLen);
	i32Count = OSSNPrintf(pszStr, 100, "free resource count\t%lu (0x%x)\n",
			      pArena->sStatistics.uFreeResourceCount,
			      (unsigned int)pArena->sStatistics.
			      uFreeResourceCount);
	UPDATE_SPACE(pszStr, i32Count, ui32StrLen);

	CHECK_SPACE(ui32StrLen);
	i32Count =
	    OSSNPrintf(pszStr, 100, "total allocs\t\t%lu\n",
		       pArena->sStatistics.uCumulativeAllocs);
	UPDATE_SPACE(pszStr, i32Count, ui32StrLen);

	CHECK_SPACE(ui32StrLen);
	i32Count =
	    OSSNPrintf(pszStr, 100, "total frees\t\t%lu\n",
		       pArena->sStatistics.uCumulativeFrees);
	UPDATE_SPACE(pszStr, i32Count, ui32StrLen);

	CHECK_SPACE(ui32StrLen);
	i32Count =
	    OSSNPrintf(pszStr, 100, "import count\t\t%lu\n",
		       pArena->sStatistics.uImportCount);
	UPDATE_SPACE(pszStr, i32Count, ui32StrLen);

	CHECK_SPACE(ui32StrLen);
	i32Count =
	    OSSNPrintf(pszStr, 100, "export count\t\t%lu\n",
		       pArena->sStatistics.uExportCount);
	UPDATE_SPACE(pszStr, i32Count, ui32StrLen);

	CHECK_SPACE(ui32StrLen);
	i32Count = OSSNPrintf(pszStr, 100, "  segment Chain:\n");
	UPDATE_SPACE(pszStr, i32Count, ui32StrLen);

	if (pArena->pHeadSegment != IMG_NULL &&
	    pArena->pHeadSegment->pPrevSegment != IMG_NULL) {
		CHECK_SPACE(ui32StrLen);
		i32Count =
		    OSSNPrintf(pszStr, 100,
			       "  error: head boundary tag has invalid pPrevSegment\n");
		UPDATE_SPACE(pszStr, i32Count, ui32StrLen);
	}

	if (pArena->pTailSegment != IMG_NULL &&
	    pArena->pTailSegment->pNextSegment != IMG_NULL) {
		CHECK_SPACE(ui32StrLen);
		i32Count =
		    OSSNPrintf(pszStr, 100,
			       "  error: tail boundary tag has invalid pNextSegment\n");
		UPDATE_SPACE(pszStr, i32Count, ui32StrLen);
	}

	for (pBT = pArena->pHeadSegment; pBT != IMG_NULL;
	     pBT = pBT->pNextSegment) {
		CHECK_SPACE(ui32StrLen);
		i32Count =
		    OSSNPrintf(pszStr, 100,
			       "\tbase=0x%x size=0x%x type=%s ref=%08X\n",
			       (unsigned long)pBT->base, pBT->uSize,
			       _BTType(pBT->type), pBT->psMapping);
		UPDATE_SPACE(pszStr, i32Count, ui32StrLen);
	}

	*ppszStr = pszStr;
	*pui32StrLen = ui32StrLen;

	return PVRSRV_OK;
}
#endif
