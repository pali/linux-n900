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

#include <stddef.h>

#include "services_headers.h"
#include "handle.h"

#ifdef	DEBUG
#define	HANDLE_BLOCK_SIZE	1
#else
#define	HANDLE_BLOCK_SIZE	256
#endif

#define	HANDLE_HASH_TAB_INIT_SIZE	32

#define	INDEX_IS_VALID(psBase, i) ((i) < (psBase)->ui32TotalHandCount)

#define	INDEX_TO_HANDLE(psBase, idx) ((void *)((idx) + 1))
#define	HANDLE_TO_INDEX(psBase, hand) ((u32)(hand) - 1)

#define INDEX_TO_HANDLE_PTR(psBase, i) (((psBase)->psHandleArray) + (i))

#define	HANDLE_TO_HANDLE_PTR(psBase, h)					\
	(INDEX_TO_HANDLE_PTR(psBase, HANDLE_TO_INDEX(psBase, h)))

#define	HANDLE_PTR_TO_INDEX(psBase, psHandle)				\
	((psHandle) - ((psBase)->psHandleArray))

#define	HANDLE_PTR_TO_HANDLE(psBase, psHandle)				\
	INDEX_TO_HANDLE(psBase, HANDLE_PTR_TO_INDEX(psBase, psHandle))

#define	ROUND_UP_TO_MULTIPLE(a, b) ((((a) + (b) - 1) / (b)) * (b))

#define	HANDLES_BATCHED(psBase) ((psBase)->ui32HandBatchSize != 0)

#define	SET_FLAG(v, f)		((void)((v) |= (f)))
#define	CLEAR_FLAG(v, f)	((void)((v) &= ~(f)))
#define	TEST_FLAG(v, f)		((IMG_BOOL)(((v) & (f)) != 0))

#define	TEST_ALLOC_FLAG(psHandle, f) TEST_FLAG((psHandle)->eFlag, f)

#define	SET_INTERNAL_FLAG(psHandle, f)				\
		 SET_FLAG((psHandle)->eInternalFlag, f)
#define	CLEAR_INTERNAL_FLAG(psHandle, f)			\
		 CLEAR_FLAG((psHandle)->eInternalFlag, f)
#define	TEST_INTERNAL_FLAG(psHandle, f)				\
		 TEST_FLAG((psHandle)->eInternalFlag, f)

#define	BATCHED_HANDLE(psHandle)				\
		 TEST_INTERNAL_FLAG(psHandle, INTERNAL_HANDLE_FLAG_BATCHED)

#define	SET_BATCHED_HANDLE(psHandle)				\
		 SET_INTERNAL_FLAG(psHandle, INTERNAL_HANDLE_FLAG_BATCHED)

#define	SET_UNBATCHED_HANDLE(psHandle)				\
		 CLEAR_INTERNAL_FLAG(psHandle, INTERNAL_HANDLE_FLAG_BATCHED)

#define	BATCHED_HANDLE_PARTIALLY_FREE(psHandle)			\
		 TEST_INTERNAL_FLAG(psHandle,			\
				INTERNAL_HANDLE_FLAG_BATCHED_PARTIALLY_FREE)

#define SET_BATCHED_HANDLE_PARTIALLY_FREE(psHandle)		\
		 SET_INTERNAL_FLAG(psHandle,			\
				INTERNAL_HANDLE_FLAG_BATCHED_PARTIALLY_FREE)

struct sHandleList {
	u32 ui32Prev;
	u32 ui32Next;
	void *hParent;
};

enum ePVRSRVInternalHandleFlag {
	INTERNAL_HANDLE_FLAG_BATCHED = 0x01,
	INTERNAL_HANDLE_FLAG_BATCHED_PARTIALLY_FREE = 0x02,
};

struct sHandle {
	enum PVRSRV_HANDLE_TYPE eType;
	void *pvData;
	u32 ui32NextIndexPlusOne;
	enum ePVRSRVInternalHandleFlag eInternalFlag;
	enum PVRSRV_HANDLE_ALLOC_FLAG eFlag;
	u32 ui32PID;
	u32 ui32Index;
	struct sHandleList sChildren;
	struct sHandleList sSiblings;
};

struct PVRSRV_HANDLE_BASE {
	void *hBaseBlockAlloc;
	u32 ui32PID;
	void *hHandBlockAlloc;
	struct RESMAN_ITEM *psResManItem;
	struct sHandle *psHandleArray;
	struct HASH_TABLE *psHashTab;
	u32 ui32FreeHandCount;
	u32 ui32FirstFreeIndex;
	u32 ui32TotalHandCount;
	u32 ui32LastFreeIndexPlusOne;
	u32 ui32HandBatchSize;
	u32 ui32TotalHandCountPreBatch;
	u32 ui32FirstBatchIndexPlusOne;
	u32 ui32BatchHandAllocFailures;
};

enum eHandKey {
	HAND_KEY_DATA = 0,
	HAND_KEY_TYPE,
	HAND_KEY_PARENT,
	HAND_KEY_LEN
};

struct PVRSRV_HANDLE_BASE *gpsKernelHandleBase;

typedef u32 HAND_KEY[HAND_KEY_LEN];

static inline void HandleListInit(u32 ui32Index, struct sHandleList *psList,
			    void *hParent)
{
	psList->ui32Next = ui32Index;
	psList->ui32Prev = ui32Index;
	psList->hParent = hParent;
}

static inline void InitParentList(struct PVRSRV_HANDLE_BASE *psBase,
			    struct sHandle *psHandle)
{
	u32 ui32Parent = HANDLE_PTR_TO_INDEX(psBase, psHandle);

	HandleListInit(ui32Parent, &psHandle->sChildren,
		       INDEX_TO_HANDLE(psBase, ui32Parent));
}

static inline void InitChildEntry(struct PVRSRV_HANDLE_BASE *psBase,
			    struct sHandle *psHandle)
{
	HandleListInit(HANDLE_PTR_TO_INDEX(psBase, psHandle),
		       &psHandle->sSiblings, NULL);
}

static inline IMG_BOOL HandleListIsEmpty(u32 ui32Index,
					 struct sHandleList *psList)
{
	IMG_BOOL bIsEmpty;

	bIsEmpty = (IMG_BOOL) (psList->ui32Next == ui32Index);

#ifdef	DEBUG
	{
		IMG_BOOL bIsEmpty2;

		bIsEmpty2 = (IMG_BOOL) (psList->ui32Prev == ui32Index);
		PVR_ASSERT(bIsEmpty == bIsEmpty2);
	}
#endif

	return bIsEmpty;
}

#ifdef DEBUG
static inline IMG_BOOL NoChildren(struct PVRSRV_HANDLE_BASE *psBase,
				  struct sHandle *psHandle)
{
	PVR_ASSERT(psHandle->sChildren.hParent ==
		   HANDLE_PTR_TO_HANDLE(psBase, psHandle));

	return HandleListIsEmpty(HANDLE_PTR_TO_INDEX(psBase, psHandle),
				 &psHandle->sChildren);
}

static inline IMG_BOOL NoParent(struct PVRSRV_HANDLE_BASE *psBase,
				struct sHandle *psHandle)
{
	if (HandleListIsEmpty
	    (HANDLE_PTR_TO_INDEX(psBase, psHandle), &psHandle->sSiblings)) {
		PVR_ASSERT(psHandle->sSiblings.hParent == NULL);

		return IMG_TRUE;
	} else {
		PVR_ASSERT(psHandle->sSiblings.hParent != NULL);
	}
	return IMG_FALSE;
}
#endif
static inline void *ParentHandle(struct sHandle *psHandle)
{
	return psHandle->sSiblings.hParent;
}

#define	LIST_PTR_FROM_INDEX_AND_OFFSET(psBase, i, p, po, eo)	\
		((struct sHandleList *)				\
		((char *)(INDEX_TO_HANDLE_PTR(psBase, i)) +	\
			 (((i) == (p)) ? (po) : (eo))))

static inline void HandleListInsertBefore(struct PVRSRV_HANDLE_BASE *psBase,
				    u32 ui32InsIndex,
				    struct sHandleList *psIns,
				    size_t uiParentOffset,
				    u32 ui32EntryIndex,
				    struct sHandleList *psEntry,
				    size_t uiEntryOffset,
				    u32 ui32ParentIndex)
{
	struct sHandleList *psPrevIns =
	    LIST_PTR_FROM_INDEX_AND_OFFSET(psBase, psIns->ui32Prev,
					   ui32ParentIndex, uiParentOffset,
					   uiEntryOffset);

	PVR_ASSERT(psEntry->hParent == NULL);
	PVR_ASSERT(ui32InsIndex == psPrevIns->ui32Next);
	PVR_ASSERT(LIST_PTR_FROM_INDEX_AND_OFFSET
		   (psBase, ui32ParentIndex, ui32ParentIndex, uiParentOffset,
		    uiParentOffset)->hParent == INDEX_TO_HANDLE(psBase,
							ui32ParentIndex));

	psEntry->ui32Prev = psIns->ui32Prev;
	psIns->ui32Prev = ui32EntryIndex;
	psEntry->ui32Next = ui32InsIndex;
	psPrevIns->ui32Next = ui32EntryIndex;

	psEntry->hParent = INDEX_TO_HANDLE(psBase, ui32ParentIndex);
}

static inline void AdoptChild(struct PVRSRV_HANDLE_BASE *psBase,
			      struct sHandle *psParent, struct sHandle *psChild)
{
	u32 ui32Parent =
	    HANDLE_TO_INDEX(psBase, psParent->sChildren.hParent);

	PVR_ASSERT(ui32Parent ==
		   (u32) HANDLE_PTR_TO_INDEX(psBase, psParent));

	HandleListInsertBefore(psBase, ui32Parent, &psParent->sChildren,
			       offsetof(struct sHandle, sChildren),
			       HANDLE_PTR_TO_INDEX(psBase, psChild),
			       &psChild->sSiblings, offsetof(struct sHandle,
							     sSiblings),
			       ui32Parent);

}

static inline void HandleListRemove(struct PVRSRV_HANDLE_BASE *psBase,
			      u32 ui32EntryIndex, struct sHandleList *psEntry,
			      size_t uiEntryOffset, size_t uiParentOffset)
{
	if (!HandleListIsEmpty(ui32EntryIndex, psEntry)) {
		struct sHandleList *psPrev =
		    LIST_PTR_FROM_INDEX_AND_OFFSET(psBase, psEntry->ui32Prev,
						   HANDLE_TO_INDEX(psBase,
								   psEntry->
								   hParent),
						   uiParentOffset,
						   uiEntryOffset);
		struct sHandleList *psNext =
		    LIST_PTR_FROM_INDEX_AND_OFFSET(psBase, psEntry->ui32Next,
						   HANDLE_TO_INDEX(psBase,
								   psEntry->
								   hParent),
						   uiParentOffset,
						   uiEntryOffset);

		PVR_ASSERT(psEntry->hParent != NULL);

		psPrev->ui32Next = psEntry->ui32Next;
		psNext->ui32Prev = psEntry->ui32Prev;

		HandleListInit(ui32EntryIndex, psEntry, NULL);
	}
}

static inline void UnlinkFromParent(struct PVRSRV_HANDLE_BASE *psBase,
			      struct sHandle *psHandle)
{
	HandleListRemove(psBase, HANDLE_PTR_TO_INDEX(psBase, psHandle),
			 &psHandle->sSiblings, offsetof(struct sHandle,
							sSiblings),
			 offsetof(struct sHandle, sChildren));
}

static inline enum PVRSRV_ERROR HandleListIterate(
				   struct PVRSRV_HANDLE_BASE *psBase,
				   struct sHandleList *psHead,
				   size_t uiParentOffset,
				   size_t uiEntryOffset,
				   enum PVRSRV_ERROR(*pfnIterFunc)
						(struct PVRSRV_HANDLE_BASE *,
						 struct sHandle *))
{
	u32 ui32Index;
	u32 ui32Parent = HANDLE_TO_INDEX(psBase, psHead->hParent);

	PVR_ASSERT(psHead->hParent != NULL);

	for (ui32Index = psHead->ui32Next; ui32Index != ui32Parent;) {
		struct sHandle *psHandle =
		    INDEX_TO_HANDLE_PTR(psBase, ui32Index);
		struct sHandleList *psEntry =
		    LIST_PTR_FROM_INDEX_AND_OFFSET(psBase, ui32Index,
						   ui32Parent, uiParentOffset,
						   uiEntryOffset);
		enum PVRSRV_ERROR eError;

		PVR_ASSERT(psEntry->hParent == psHead->hParent);

		ui32Index = psEntry->ui32Next;

		eError = (*pfnIterFunc) (psBase, psHandle);
		if (eError != PVRSRV_OK)
			return eError;
	}

	return PVRSRV_OK;
}

static inline enum PVRSRV_ERROR IterateOverChildren(
				struct PVRSRV_HANDLE_BASE *psBase,
				struct sHandle *psParent,
				enum PVRSRV_ERROR(*pfnIterFunc)
				(struct PVRSRV_HANDLE_BASE *, struct sHandle *))
{
	return HandleListIterate(psBase, &psParent->sChildren,
				 offsetof(struct sHandle, sChildren),
				 offsetof(struct sHandle, sSiblings),
				 pfnIterFunc);
}

static inline enum PVRSRV_ERROR GetHandleStructure(
				    struct PVRSRV_HANDLE_BASE *psBase,
				    struct sHandle **ppsHandle,
				    void *hHandle,
				    enum PVRSRV_HANDLE_TYPE eType)
{
	u32 ui32Index = HANDLE_TO_INDEX(psBase, hHandle);
	struct sHandle *psHandle;

	if (!INDEX_IS_VALID(psBase, ui32Index)) {
		PVR_DPF(PVR_DBG_ERROR, "GetHandleStructure: "
			 "Handle index out of range (%u >= %u)",
			 ui32Index, psBase->ui32TotalHandCount);
		return PVRSRV_ERROR_GENERIC;
	}

	psHandle = INDEX_TO_HANDLE_PTR(psBase, ui32Index);
	if (psHandle->eType == PVRSRV_HANDLE_TYPE_NONE) {
		PVR_DPF(PVR_DBG_ERROR, "GetHandleStructure: "
			 "Handle not allocated (index: %u)", ui32Index);
		return PVRSRV_ERROR_GENERIC;
	}

	if (eType != PVRSRV_HANDLE_TYPE_NONE && eType != psHandle->eType) {
		PVR_DPF(PVR_DBG_ERROR,
			 "GetHandleStructure: Handle type mismatch (%d != %d)",
			 eType, psHandle->eType);
		return PVRSRV_ERROR_GENERIC;
	}

	PVR_ASSERT(psBase->ui32PID == psHandle->ui32PID);

	*ppsHandle = psHandle;

	return PVRSRV_OK;
}

static inline void *ParentIfPrivate(struct sHandle *psHandle)
{
	return TEST_ALLOC_FLAG(psHandle, PVRSRV_HANDLE_ALLOC_FLAG_PRIVATE) ?
		ParentHandle(psHandle) : NULL;
}

static inline void InitKey(HAND_KEY aKey, struct PVRSRV_HANDLE_BASE *psBase,
		     void *pvData, enum PVRSRV_HANDLE_TYPE eType,
		     void *hParent)
{
	PVR_UNREFERENCED_PARAMETER(psBase);

	aKey[HAND_KEY_DATA] = (u32) pvData;
	aKey[HAND_KEY_TYPE] = (u32) eType;
	aKey[HAND_KEY_PARENT] = (u32) hParent;
}

static void FreeHandleArray(struct PVRSRV_HANDLE_BASE *psBase)
{
	enum PVRSRV_ERROR eError = PVRSRV_OK;

	if (psBase->psHandleArray != NULL) {
		OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
			   psBase->ui32TotalHandCount *
			   sizeof(struct sHandle),
			   psBase->psHandleArray,
			   psBase->hHandBlockAlloc);

		psBase->psHandleArray = NULL;
	}
}

static enum PVRSRV_ERROR FreeHandle(struct PVRSRV_HANDLE_BASE *psBase,
			       struct sHandle *psHandle)
{
	HAND_KEY aKey;
	u32 ui32Index = HANDLE_PTR_TO_INDEX(psBase, psHandle);
	enum PVRSRV_ERROR eError;

	PVR_ASSERT(psBase->ui32PID == psHandle->ui32PID);

	InitKey(aKey, psBase, psHandle->pvData, psHandle->eType,
		ParentIfPrivate(psHandle));

	if (!TEST_ALLOC_FLAG(psHandle, PVRSRV_HANDLE_ALLOC_FLAG_MULTI)
	    && !BATCHED_HANDLE_PARTIALLY_FREE(psHandle)) {
		void *hHandle;
		hHandle =
		    (void *) HASH_Remove_Extended(psBase->psHashTab, aKey);

		PVR_ASSERT(hHandle != NULL);
		PVR_ASSERT(hHandle == INDEX_TO_HANDLE(psBase, ui32Index));
		PVR_UNREFERENCED_PARAMETER(hHandle);
	}

	UnlinkFromParent(psBase, psHandle);

	eError = IterateOverChildren(psBase, psHandle, FreeHandle);
	if (eError != PVRSRV_OK) {
		PVR_DPF(PVR_DBG_ERROR,
			 "FreeHandle: Error whilst freeing subhandles (%d)",
			 eError);
		return eError;
	}

	psHandle->eType = PVRSRV_HANDLE_TYPE_NONE;

	if (BATCHED_HANDLE(psHandle)
	    && !BATCHED_HANDLE_PARTIALLY_FREE(psHandle)) {
		SET_BATCHED_HANDLE_PARTIALLY_FREE(psHandle);

		return PVRSRV_OK;
	}

	if (psBase->ui32FreeHandCount == 0) {
		PVR_ASSERT(psBase->ui32FirstFreeIndex == 0);
		PVR_ASSERT(psBase->ui32LastFreeIndexPlusOne == 0);

		psBase->ui32FirstFreeIndex = ui32Index;
	} else {

		PVR_ASSERT(psBase->ui32LastFreeIndexPlusOne != 0);
		PVR_ASSERT(INDEX_TO_HANDLE_PTR
			   (psBase,
			    psBase->ui32LastFreeIndexPlusOne -
			    1)->ui32NextIndexPlusOne == 0);

		INDEX_TO_HANDLE_PTR(psBase,
				    psBase->ui32LastFreeIndexPlusOne -
				    1)->ui32NextIndexPlusOne = ui32Index + 1;
	}

	PVR_ASSERT(psHandle->ui32NextIndexPlusOne == 0);

	psBase->ui32LastFreeIndexPlusOne = ui32Index + 1;

	psBase->ui32FreeHandCount++;

	return PVRSRV_OK;
}

static enum PVRSRV_ERROR FreeAllHandles(struct PVRSRV_HANDLE_BASE *psBase)
{
	u32 i;
	enum PVRSRV_ERROR eError = PVRSRV_OK;

	if (psBase->ui32FreeHandCount == psBase->ui32TotalHandCount)
		return eError;

	for (i = 0; i < psBase->ui32TotalHandCount; i++) {
		struct sHandle *psHandle;

		psHandle = INDEX_TO_HANDLE_PTR(psBase, i);

		if (psHandle->eType != PVRSRV_HANDLE_TYPE_NONE) {
			eError = FreeHandle(psBase, psHandle);
			if (eError != PVRSRV_OK) {
				PVR_DPF(PVR_DBG_ERROR,
				       "FreeAllHandles: FreeHandle failed (%d)",
					eError);
				break;
			}

			if (psBase->ui32FreeHandCount ==
			    psBase->ui32TotalHandCount)
				break;
		}
	}

	return eError;
}

static enum PVRSRV_ERROR FreeHandleBase(struct PVRSRV_HANDLE_BASE *psBase)
{
	enum PVRSRV_ERROR eError;

	if (HANDLES_BATCHED(psBase)) {
		PVR_DPF(PVR_DBG_WARNING,
			"FreeHandleBase: Uncommitted/Unreleased handle batch");
		PVRSRVReleaseHandleBatch(psBase);
	}

	eError = FreeAllHandles(psBase);
	if (eError != PVRSRV_OK) {
		PVR_DPF(PVR_DBG_ERROR,
			 "FreeHandleBase: Couldn't free handles (%d)", eError);
		return eError;
	}

	FreeHandleArray(psBase);

	if (psBase->psHashTab != NULL)

		HASH_Delete(psBase->psHashTab);

	OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
			   sizeof(*psBase), psBase, psBase->hBaseBlockAlloc);

	return PVRSRV_OK;
}

static inline void *FindHandle(struct PVRSRV_HANDLE_BASE *psBase, void *pvData,
			  enum PVRSRV_HANDLE_TYPE eType, void *hParent)
{
	HAND_KEY aKey;

	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);

	InitKey(aKey, psBase, pvData, eType, hParent);

	return (void *)HASH_Retrieve_Extended(psBase->psHashTab, aKey);
}

static enum PVRSRV_ERROR IncreaseHandleArraySize(
					    struct PVRSRV_HANDLE_BASE *psBase,
					    u32 ui32Delta)
{
	struct sHandle *psNewHandleArray;
	void *hNewHandBlockAlloc;
	enum PVRSRV_ERROR eError;
	struct sHandle *psHandle;
	u32 ui32DeltaRounded =
	    ROUND_UP_TO_MULTIPLE(ui32Delta, HANDLE_BLOCK_SIZE);
	u32 ui32NewTotalHandCount =
	    psBase->ui32TotalHandCount + ui32DeltaRounded;
	;

	eError = OSAllocMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
			    ui32NewTotalHandCount * sizeof(struct sHandle),
			    (void **) &psNewHandleArray,
			    &hNewHandBlockAlloc);
	if (eError != PVRSRV_OK) {
		PVR_DPF(PVR_DBG_ERROR, "IncreaseHandleArraySize: "
				"Couldn't allocate new handle array (%d)",
			 eError);
		return eError;
	}

	if (psBase->psHandleArray != NULL)
		OSMemCopy(psNewHandleArray,
			  psBase->psHandleArray,
			  psBase->ui32TotalHandCount * sizeof(struct sHandle));

	for (psHandle = psNewHandleArray + psBase->ui32TotalHandCount;
	     psHandle < psNewHandleArray + ui32NewTotalHandCount; psHandle++) {
		psHandle->eType = PVRSRV_HANDLE_TYPE_NONE;
		psHandle->ui32NextIndexPlusOne = 0;
	}

	FreeHandleArray(psBase);

	psBase->psHandleArray = psNewHandleArray;
	psBase->hHandBlockAlloc = hNewHandBlockAlloc;

	psBase->ui32FreeHandCount += ui32DeltaRounded;

	if (psBase->ui32FirstFreeIndex == 0) {
		PVR_ASSERT(psBase->ui32LastFreeIndexPlusOne == 0);

		psBase->ui32FirstFreeIndex = psBase->ui32TotalHandCount;
	} else {
		PVR_ASSERT(psBase->ui32LastFreeIndexPlusOne != 0);
		    PVR_ASSERT(INDEX_TO_HANDLE_PTR
			       (psBase,
				psBase->ui32LastFreeIndexPlusOne -
				1)->ui32NextIndexPlusOne == 0);

		INDEX_TO_HANDLE_PTR(psBase,
				    psBase->ui32LastFreeIndexPlusOne -
				    1)->ui32NextIndexPlusOne =
		    psBase->ui32TotalHandCount + 1;

	}
	psBase->ui32LastFreeIndexPlusOne = ui32NewTotalHandCount;

	psBase->ui32TotalHandCount = ui32NewTotalHandCount;

	return PVRSRV_OK;
}

static enum PVRSRV_ERROR EnsureFreeHandles(struct PVRSRV_HANDLE_BASE *psBase,
				      u32 ui32Free)
{
	enum PVRSRV_ERROR eError;

	if (ui32Free > psBase->ui32FreeHandCount) {
		u32 ui32FreeHandDelta =
		    ui32Free - psBase->ui32FreeHandCount;
		eError = IncreaseHandleArraySize(psBase, ui32FreeHandDelta);
		if (eError != PVRSRV_OK) {
			PVR_DPF(PVR_DBG_ERROR, "EnsureFreeHandles: "
			       "Couldn't allocate %u handles "
			       "to ensure %u free handles "
			       "(IncreaseHandleArraySize failed with error %d)",
				ui32FreeHandDelta, ui32Free, eError);

			return eError;
		}
	}

	return PVRSRV_OK;
}

static enum PVRSRV_ERROR AllocHandle(struct PVRSRV_HANDLE_BASE *psBase,
				void **phHandle, void *pvData,
				enum PVRSRV_HANDLE_TYPE eType,
				enum PVRSRV_HANDLE_ALLOC_FLAG eFlag,
				void *hParent)
{
	u32 ui32NewIndex;
	struct sHandle *psNewHandle;
	void *hHandle;
	HAND_KEY aKey;
	enum PVRSRV_ERROR eError;

	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);

	PVR_ASSERT(psBase->psHashTab != NULL);

	if (!TEST_FLAG(eFlag, PVRSRV_HANDLE_ALLOC_FLAG_MULTI)) {

		PVR_ASSERT(FindHandle(psBase, pvData, eType, hParent) ==
			   NULL);
	}

	if (psBase->ui32FreeHandCount == 0 && HANDLES_BATCHED(psBase)) {
		PVR_DPF(PVR_DBG_WARNING, "AllocHandle: "
			"Handle batch size (%u) was too small, "
			"allocating additional space",
			 psBase->ui32HandBatchSize);
	}

	eError = EnsureFreeHandles(psBase, 1);
	if (eError != PVRSRV_OK) {
		PVR_DPF(PVR_DBG_ERROR,
			 "AllocHandle: EnsureFreeHandles failed (%d)", eError);
		return eError;
	}
	PVR_ASSERT(psBase->ui32FreeHandCount != 0);

	    ui32NewIndex = psBase->ui32FirstFreeIndex;

	psNewHandle = INDEX_TO_HANDLE_PTR(psBase, ui32NewIndex);

	hHandle = INDEX_TO_HANDLE(psBase, ui32NewIndex);

	if (!TEST_FLAG(eFlag, PVRSRV_HANDLE_ALLOC_FLAG_MULTI)) {

		InitKey(aKey, psBase, pvData, eType, hParent);

		if (!HASH_Insert_Extended
		    (psBase->psHashTab, aKey, (u32) hHandle)) {
			PVR_DPF(PVR_DBG_ERROR, "AllocHandle: "
				"Couldn't add handle to hash table");

			return PVRSRV_ERROR_GENERIC;
		}
	}

	psBase->ui32FreeHandCount--;

	if (psBase->ui32FreeHandCount == 0) {
		PVR_ASSERT(psBase->ui32FirstFreeIndex == ui32NewIndex);
		PVR_ASSERT(psBase->ui32LastFreeIndexPlusOne ==
			   (ui32NewIndex + 1));

		psBase->ui32LastFreeIndexPlusOne = 0;
		psBase->ui32FirstFreeIndex = 0;
	} else {

		psBase->ui32FirstFreeIndex =
		    (psNewHandle->ui32NextIndexPlusOne ==
		     0) ? ui32NewIndex + 1 : psNewHandle->ui32NextIndexPlusOne -
		    1;
	}

	psNewHandle->eType = eType;
	psNewHandle->pvData = pvData;
	psNewHandle->eInternalFlag = 0;
	psNewHandle->eFlag = eFlag;
	psNewHandle->ui32PID = psBase->ui32PID;
	psNewHandle->ui32Index = ui32NewIndex;

	InitParentList(psBase, psNewHandle);
	PVR_ASSERT(NoChildren(psBase, psNewHandle));

	InitChildEntry(psBase, psNewHandle);
	PVR_ASSERT(NoParent(psBase, psNewHandle));

	if (HANDLES_BATCHED(psBase)) {

		psNewHandle->ui32NextIndexPlusOne =
		    psBase->ui32FirstBatchIndexPlusOne;

		psBase->ui32FirstBatchIndexPlusOne = ui32NewIndex + 1;

		SET_BATCHED_HANDLE(psNewHandle);
	} else {
		psNewHandle->ui32NextIndexPlusOne = 0;
	}

	*phHandle = hHandle;

	return PVRSRV_OK;
}

enum PVRSRV_ERROR PVRSRVAllocHandle(struct PVRSRV_HANDLE_BASE *psBase,
			       void **phHandle, void *pvData,
			       enum PVRSRV_HANDLE_TYPE eType,
			       enum PVRSRV_HANDLE_ALLOC_FLAG eFlag)
{
	void *hHandle;
	enum PVRSRV_ERROR eError;

	*phHandle = NULL;

	if (HANDLES_BATCHED(psBase))

		psBase->ui32BatchHandAllocFailures++;

	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);

	if (!TEST_FLAG(eFlag, PVRSRV_HANDLE_ALLOC_FLAG_MULTI)) {

		hHandle = FindHandle(psBase, pvData, eType, NULL);
		if (hHandle != NULL) {
			struct sHandle *psHandle;

			eError =
			    GetHandleStructure(psBase, &psHandle, hHandle,
					       eType);
			if (eError != PVRSRV_OK) {
				PVR_DPF(PVR_DBG_ERROR, "PVRSRVAllocHandle: "
					  "Lookup of existing handle failed");
				return eError;
			}

			if (TEST_FLAG
			    (psHandle->eFlag & eFlag,
			     PVRSRV_HANDLE_ALLOC_FLAG_SHARED)) {
				*phHandle = hHandle;
				eError = PVRSRV_OK;
				goto exit_ok;
			}
			return PVRSRV_ERROR_GENERIC;
		}
	}

	eError = AllocHandle(psBase, phHandle, pvData, eType, eFlag, NULL);

exit_ok:
	if (HANDLES_BATCHED(psBase) && (eError == PVRSRV_OK))
		psBase->ui32BatchHandAllocFailures--;

	return eError;
}

enum PVRSRV_ERROR PVRSRVAllocSubHandle(struct PVRSRV_HANDLE_BASE *psBase,
				  void **phHandle, void *pvData,
				  enum PVRSRV_HANDLE_TYPE eType,
				  enum PVRSRV_HANDLE_ALLOC_FLAG eFlag,
				  void *hParent)
{
	struct sHandle *psPHand;
	struct sHandle *psCHand;
	enum PVRSRV_ERROR eError;
	void *hParentKey;
	void *hHandle;

	*phHandle = NULL;

	if (HANDLES_BATCHED(psBase))

		psBase->ui32BatchHandAllocFailures++;

	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);

	hParentKey = TEST_FLAG(eFlag, PVRSRV_HANDLE_ALLOC_FLAG_PRIVATE) ?
	    hParent : NULL;

	eError =
	    GetHandleStructure(psBase, &psPHand, hParent,
			       PVRSRV_HANDLE_TYPE_NONE);
	if (eError != PVRSRV_OK)
		return PVRSRV_ERROR_GENERIC;

	if (!TEST_FLAG(eFlag, PVRSRV_HANDLE_ALLOC_FLAG_MULTI)) {

		hHandle = FindHandle(psBase, pvData, eType, hParentKey);
		if (hHandle != NULL) {
			struct sHandle *psCHandle;
			enum PVRSRV_ERROR eErr;

			eErr =
			    GetHandleStructure(psBase, &psCHandle, hHandle,
					       eType);
			if (eErr != PVRSRV_OK) {
				PVR_DPF(PVR_DBG_ERROR, "PVRSRVAllocSubHandle: "
					"Lookup of existing handle failed");
				return eErr;
			}

			PVR_ASSERT(hParentKey != NULL
				   &&
				   ParentHandle(HANDLE_TO_HANDLE_PTR
						(psBase, hHandle)) == hParent);

			if (TEST_FLAG
			    (psCHandle->eFlag & eFlag,
			     PVRSRV_HANDLE_ALLOC_FLAG_SHARED)
			    &&
			    ParentHandle(HANDLE_TO_HANDLE_PTR(psBase, hHandle))
			    == hParent) {
				*phHandle = hHandle;
				goto exit_ok;
			}
			return PVRSRV_ERROR_GENERIC;
		}
	}

	eError =
	    AllocHandle(psBase, &hHandle, pvData, eType, eFlag, hParentKey);
	if (eError != PVRSRV_OK)
		return eError;

	psPHand = HANDLE_TO_HANDLE_PTR(psBase, hParent);

	psCHand = HANDLE_TO_HANDLE_PTR(psBase, hHandle);

	AdoptChild(psBase, psPHand, psCHand);

	*phHandle = hHandle;

exit_ok:
	if (HANDLES_BATCHED(psBase))
		psBase->ui32BatchHandAllocFailures--;

	return PVRSRV_OK;
}

enum PVRSRV_ERROR PVRSRVFindHandle(struct PVRSRV_HANDLE_BASE *psBase,
			      void **phHandle, void *pvData,
			      enum PVRSRV_HANDLE_TYPE eType)
{
	void *hHandle;

	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);

	hHandle = (void *) FindHandle(psBase, pvData, eType, NULL);
	if (hHandle == NULL) {
		PVR_DPF(PVR_DBG_ERROR,
			 "PVRSRVFindHandle: couldn't find handle");
		return PVRSRV_ERROR_GENERIC;
	}

	*phHandle = hHandle;

	return PVRSRV_OK;
}

enum PVRSRV_ERROR PVRSRVLookupHandleAnyType(struct PVRSRV_HANDLE_BASE *psBase,
				       void **ppvData,
				       enum PVRSRV_HANDLE_TYPE *peType,
				       void *hHandle)
{
	struct sHandle *psHandle;
	enum PVRSRV_ERROR eError;

	eError =
	    GetHandleStructure(psBase, &psHandle, hHandle,
			       PVRSRV_HANDLE_TYPE_NONE);
	if (eError != PVRSRV_OK) {
		PVR_DPF(PVR_DBG_ERROR, "PVRSRVLookupHandleAnyType: "
					"Error looking up handle (%d)",
					eError);
		return eError;
	}

	*ppvData = psHandle->pvData;
	*peType = psHandle->eType;

	return PVRSRV_OK;
}

enum PVRSRV_ERROR PVRSRVLookupHandle(struct PVRSRV_HANDLE_BASE *psBase,
				void **ppvData, void *hHandle,
				enum PVRSRV_HANDLE_TYPE eType)
{
	struct sHandle *psHandle;
	enum PVRSRV_ERROR eError;

	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);

	eError = GetHandleStructure(psBase, &psHandle, hHandle, eType);
	if (eError != PVRSRV_OK) {
		PVR_DPF(PVR_DBG_ERROR,
			 "PVRSRVLookupHandle: Error looking up handle (%d)",
			 eError);
		return eError;
	}

	*ppvData = psHandle->pvData;

	return PVRSRV_OK;
}

enum PVRSRV_ERROR PVRSRVLookupSubHandle(struct PVRSRV_HANDLE_BASE *psBase,
				   void **ppvData, void *hHandle,
				   enum PVRSRV_HANDLE_TYPE eType,
				   void *hAncestor)
{
	struct sHandle *psPHand;
	struct sHandle *psCHand;
	enum PVRSRV_ERROR eError;

	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);

	eError = GetHandleStructure(psBase, &psCHand, hHandle, eType);
	if (eError != PVRSRV_OK) {
		PVR_DPF(PVR_DBG_ERROR, "PVRSRVLookupSubHandle: "
			"Error looking up subhandle (%d)",
			 eError);
		return eError;
	}

	for (psPHand = psCHand; ParentHandle(psPHand) != hAncestor;) {
		eError =
		    GetHandleStructure(psBase, &psPHand, ParentHandle(psPHand),
				       PVRSRV_HANDLE_TYPE_NONE);
		if (eError != PVRSRV_OK) {
			PVR_DPF(PVR_DBG_ERROR, "PVRSRVLookupSubHandle: "
				"Subhandle doesn't belong to given ancestor");
			return PVRSRV_ERROR_GENERIC;
		}
	}

	*ppvData = psCHand->pvData;

	return PVRSRV_OK;
}

enum PVRSRV_ERROR PVRSRVGetParentHandle(struct PVRSRV_HANDLE_BASE *psBase,
				   void **phParent, void *hHandle,
				   enum PVRSRV_HANDLE_TYPE eType)
{
	struct sHandle *psHandle;
	enum PVRSRV_ERROR eError;

	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);

	eError = GetHandleStructure(psBase, &psHandle, hHandle, eType);
	if (eError != PVRSRV_OK) {
		PVR_DPF(PVR_DBG_ERROR, "PVRSRVGetParentHandle: "
					"Error looking up subhandle (%d)",
			 eError);
		return eError;
	}

	*phParent = ParentHandle(psHandle);

	return PVRSRV_OK;
}

enum PVRSRV_ERROR PVRSRVLookupAndReleaseHandle(
				struct PVRSRV_HANDLE_BASE *psBase,
				void **ppvData, void *hHandle,
				enum PVRSRV_HANDLE_TYPE eType)
{
	struct sHandle *psHandle;
	enum PVRSRV_ERROR eError;

	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);

	eError = GetHandleStructure(psBase, &psHandle, hHandle, eType);
	if (eError != PVRSRV_OK) {
		PVR_DPF(PVR_DBG_ERROR, "PVRSRVLookupAndReleaseHandle: "
			"Error looking up handle (%d)",
			 eError);
		return eError;
	}

	*ppvData = psHandle->pvData;

	eError = FreeHandle(psBase, psHandle);

	return eError;
}

enum PVRSRV_ERROR PVRSRVReleaseHandle(struct PVRSRV_HANDLE_BASE *psBase,
				 void *hHandle, enum PVRSRV_HANDLE_TYPE eType)
{
	struct sHandle *psHandle;
	enum PVRSRV_ERROR eError;

	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);

	eError = GetHandleStructure(psBase, &psHandle, hHandle, eType);
	if (eError != PVRSRV_OK) {
		PVR_DPF(PVR_DBG_ERROR,
			 "PVRSRVReleaseHandle: Error looking up handle (%d)",
			 eError);
		return eError;
	}

	eError = FreeHandle(psBase, psHandle);

	return eError;
}

enum PVRSRV_ERROR PVRSRVNewHandleBatch(struct PVRSRV_HANDLE_BASE *psBase,
				  u32 ui32BatchSize)
{
	enum PVRSRV_ERROR eError;

	if (HANDLES_BATCHED(psBase)) {
		PVR_DPF(PVR_DBG_ERROR, "PVRSRVNewHandleBatch: "
			"There is a handle batch already in use (size %u)",
			 psBase->ui32HandBatchSize);
		return PVRSRV_ERROR_GENERIC;
	}

	if (ui32BatchSize == 0) {
		PVR_DPF(PVR_DBG_ERROR, "PVRSRVNewHandleBatch: "
			"Invalid batch size (%u)", ui32BatchSize);
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	eError = EnsureFreeHandles(psBase, ui32BatchSize);
	if (eError != PVRSRV_OK) {
		PVR_DPF(PVR_DBG_ERROR, "PVRSRVNewHandleBatch: "
			 "EnsureFreeHandles failed (error %d)", eError);
		return eError;
	}

	psBase->ui32HandBatchSize = ui32BatchSize;
	psBase->ui32TotalHandCountPreBatch = psBase->ui32TotalHandCount;

	PVR_ASSERT(psBase->ui32BatchHandAllocFailures == 0);
	PVR_ASSERT(psBase->ui32FirstBatchIndexPlusOne == 0);
	PVR_ASSERT(HANDLES_BATCHED(psBase));

	return PVRSRV_OK;
}

static enum PVRSRV_ERROR PVRSRVHandleBatchCommitOrRelease(
			struct PVRSRV_HANDLE_BASE *psBase, IMG_BOOL bCommit)
{
	u32 ui32IndexPlusOne;
	IMG_BOOL bCommitBatch = bCommit;

	if (!HANDLES_BATCHED(psBase)) {
		PVR_DPF(PVR_DBG_ERROR, "PVRSRVHandleBatchCommitOrRelease: "
					"There is no handle batch");
		return PVRSRV_ERROR_INVALID_PARAMS;

	}

	if (psBase->ui32BatchHandAllocFailures != 0) {
		if (bCommit)
			PVR_DPF(PVR_DBG_ERROR,
				"PVRSRVHandleBatchCommitOrRelease: "
				"Attempting to commit batch "
				"with handle allocation failures.");
		bCommitBatch = IMG_FALSE;
	}

	PVR_ASSERT(psBase->ui32BatchHandAllocFailures == 0 || !bCommit);

	ui32IndexPlusOne = psBase->ui32FirstBatchIndexPlusOne;
	while (ui32IndexPlusOne != 0) {
		struct sHandle *psHandle =
		    INDEX_TO_HANDLE_PTR(psBase, ui32IndexPlusOne - 1);
		u32 ui32NextIndexPlusOne =
		    psHandle->ui32NextIndexPlusOne;
		PVR_ASSERT(BATCHED_HANDLE(psHandle));

		psHandle->ui32NextIndexPlusOne = 0;

		if (!bCommitBatch || BATCHED_HANDLE_PARTIALLY_FREE(psHandle)) {
			enum PVRSRV_ERROR eError;

			if (!BATCHED_HANDLE_PARTIALLY_FREE(psHandle))
				SET_UNBATCHED_HANDLE(psHandle);

			eError = FreeHandle(psBase, psHandle);
			if (eError != PVRSRV_OK)
				PVR_DPF(PVR_DBG_ERROR,
					 "PVRSRVHandleBatchCommitOrRelease: "
					 "Error freeing handle (%d)", eError);
			PVR_ASSERT(eError == PVRSRV_OK);
		} else {
			SET_UNBATCHED_HANDLE(psHandle);
		}

		ui32IndexPlusOne = ui32NextIndexPlusOne;
	}

#ifdef DEBUG
	if (psBase->ui32TotalHandCountPreBatch != psBase->ui32TotalHandCount) {
		u32 ui32Delta =
		    psBase->ui32TotalHandCount -
		    psBase->ui32TotalHandCountPreBatch;

		PVR_ASSERT(psBase->ui32TotalHandCount >
			   psBase->ui32TotalHandCountPreBatch);

		PVR_DPF(PVR_DBG_WARNING,
			 "PVRSRVHandleBatchCommitOrRelease: "
			 "The batch size was too small.  "
			 "Batch size was %u, but needs to be %u",
			 psBase->ui32HandBatchSize,
			 psBase->ui32HandBatchSize + ui32Delta);

	}
#endif

	psBase->ui32HandBatchSize = 0;
	psBase->ui32FirstBatchIndexPlusOne = 0;
	psBase->ui32TotalHandCountPreBatch = 0;
	psBase->ui32BatchHandAllocFailures = 0;

	if (psBase->ui32BatchHandAllocFailures != 0 && bCommit) {
		PVR_ASSERT(!bCommitBatch);

		return PVRSRV_ERROR_GENERIC;
	}

	return PVRSRV_OK;
}

enum PVRSRV_ERROR PVRSRVCommitHandleBatch(struct PVRSRV_HANDLE_BASE *psBase)
{
	return PVRSRVHandleBatchCommitOrRelease(psBase, IMG_TRUE);
}

void PVRSRVReleaseHandleBatch(struct PVRSRV_HANDLE_BASE *psBase)
{
	(void)PVRSRVHandleBatchCommitOrRelease(psBase, IMG_FALSE);
}

enum PVRSRV_ERROR PVRSRVAllocHandleBase(struct PVRSRV_HANDLE_BASE **ppsBase,
				   u32 ui32PID)
{
	struct PVRSRV_HANDLE_BASE *psBase;
	void *hBlockAlloc;
	enum PVRSRV_ERROR eError;

	eError = OSAllocMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
			    sizeof(*psBase),
			    (void **) &psBase, &hBlockAlloc);
	if (eError != PVRSRV_OK) {
		PVR_DPF(PVR_DBG_ERROR, "PVRSRVAllocHandleBase: "
			 "Couldn't allocate handle base (%d)",
			 eError);
		return eError;
	}
	OSMemSet(psBase, 0, sizeof(*psBase));

	psBase->psHashTab =
	    HASH_Create_Extended(HANDLE_HASH_TAB_INIT_SIZE, sizeof(HAND_KEY),
				 HASH_Func_Default, HASH_Key_Comp_Default);
	if (psBase->psHashTab == NULL) {
		PVR_DPF(PVR_DBG_ERROR, "PVRSRVAllocHandleBase: "
			 "Couldn't create data pointer hash table\n");
		goto failure;
	}

	psBase->hBaseBlockAlloc = hBlockAlloc;
	psBase->ui32PID = ui32PID;

	*ppsBase = psBase;

	return PVRSRV_OK;
failure:
	(void)PVRSRVFreeHandleBase(psBase);
	return PVRSRV_ERROR_GENERIC;
}

enum PVRSRV_ERROR PVRSRVFreeHandleBase(struct PVRSRV_HANDLE_BASE *psBase)
{
	enum PVRSRV_ERROR eError;

	PVR_ASSERT(psBase != gpsKernelHandleBase);

	eError = FreeHandleBase(psBase);

	return eError;
}

enum PVRSRV_ERROR PVRSRVHandleInit(void)
{
	enum PVRSRV_ERROR eError;

	PVR_ASSERT(gpsKernelHandleBase == NULL);

	eError = PVRSRVAllocHandleBase(&gpsKernelHandleBase, KERNEL_ID);

	return eError;
}

enum PVRSRV_ERROR PVRSRVHandleDeInit(void)
{
	enum PVRSRV_ERROR eError = PVRSRV_OK;

	if (gpsKernelHandleBase != NULL) {
		eError = FreeHandleBase(gpsKernelHandleBase);
		if (eError == PVRSRV_OK)
			gpsKernelHandleBase = NULL;
	}

	return eError;
}
