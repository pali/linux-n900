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

#include "img_defs.h"
#include "services.h"
#include "pvr_bridge_km.h"
#include "pvr_debug.h"
#include "ra.h"
#include "pvr_bridge.h"
#include "sgx_bridge.h"
#include "perproc.h"
#include "sgx_bridge_km.h"
#include "pdump_km.h"
#include "sgxutils.h"
#include "mmu.h"

#include "bridged_pvr_bridge.h"
#include "env_data.h"

#include "mmap.h"

#include <linux/pagemap.h>	/* for cache flush */


#if defined(DEBUG)
#define PVRSRV_BRIDGE_ASSERT_CMD(X, Y) PVR_ASSERT(X == PVRSRV_GET_BRIDGE_ID(Y))
#else
#define PVRSRV_BRIDGE_ASSERT_CMD(X, Y) PVR_UNREFERENCED_PARAMETER(X)
#endif

struct PVRSRV_BRIDGE_DISPATCH_TABLE_ENTRY
    g_BridgeDispatchTable[BRIDGE_DISPATCH_TABLE_ENTRY_COUNT];

#if defined(DEBUG_BRIDGE_KM)
struct PVRSRV_BRIDGE_GLOBAL_STATS g_BridgeGlobalStats;
#endif

static IMG_BOOL abSharedDeviceMemHeap[PVRSRV_MAX_CLIENT_HEAPS];

#if defined(DEBUG_BRIDGE_KM)
static enum PVRSRV_ERROR CopyFromUserWrapper(struct PVRSRV_PER_PROCESS_DATA
		*pProcData, u32 ui32BridgeID,
		void *pvDest, void __user *pvSrc, u32 ui32Size)
{
	g_BridgeDispatchTable[ui32BridgeID].ui32CopyFromUserTotalBytes +=
	    ui32Size;
	g_BridgeGlobalStats.ui32TotalCopyFromUserBytes += ui32Size;
	return OSCopyFromUser(pProcData, pvDest, pvSrc, ui32Size);
}

static enum PVRSRV_ERROR CopyToUserWrapper(
		  struct PVRSRV_PER_PROCESS_DATA *pProcData,
		  u32 ui32BridgeID,
		  void __user *pvDest, void *pvSrc, u32 ui32Size)
{
	g_BridgeDispatchTable[ui32BridgeID].ui32CopyToUserTotalBytes +=
	    ui32Size;
	g_BridgeGlobalStats.ui32TotalCopyToUserBytes += ui32Size;
	return OSCopyToUser(pProcData, pvDest, pvSrc, ui32Size);
}
#else
#define CopyFromUserWrapper(pProcData, ui32BridgeID, pvDest, pvSrc, ui32Size) \
	OSCopyFromUser(pProcData, pvDest, pvSrc, ui32Size)
#define CopyToUserWrapper(pProcData, ui32BridgeID, pvDest, pvSrc, ui32Size) \
	OSCopyToUser(pProcData, pvDest, pvSrc, ui32Size)
#endif

#define ASSIGN_AND_RETURN_ON_ERROR(error, src, res)		\
	do {							\
		(error) = (src);				\
		if ((error) != PVRSRV_OK) {			\
			return res;				\
		}						\
	} while (error != PVRSRV_OK)

#define ASSIGN_AND_EXIT_ON_ERROR(error, src)		\
	ASSIGN_AND_RETURN_ON_ERROR(error, src, 0)

static inline enum PVRSRV_ERROR NewHandleBatch(struct PVRSRV_PER_PROCESS_DATA
					  *psPerProc, u32 ui32BatchSize)
{
	enum PVRSRV_ERROR eError;

	PVR_ASSERT(!psPerProc->bHandlesBatched);

	eError = PVRSRVNewHandleBatch(psPerProc->psHandleBase, ui32BatchSize);

	if (eError == PVRSRV_OK)
		psPerProc->bHandlesBatched = IMG_TRUE;

	return eError;
}

#define NEW_HANDLE_BATCH_OR_ERROR(error, psPerProc, ui32BatchSize)	\
      ASSIGN_AND_EXIT_ON_ERROR(error, NewHandleBatch(psPerProc, ui32BatchSize))

static inline enum PVRSRV_ERROR CommitHandleBatch(struct PVRSRV_PER_PROCESS_DATA
					     *psPerProc)
{
	PVR_ASSERT(psPerProc->bHandlesBatched);

	psPerProc->bHandlesBatched = IMG_FALSE;

	return PVRSRVCommitHandleBatch(psPerProc->psHandleBase);
}

#define COMMIT_HANDLE_BATCH_OR_ERROR(error, psPerProc)			\
	ASSIGN_AND_EXIT_ON_ERROR(error, CommitHandleBatch(psPerProc))

static inline void ReleaseHandleBatch(struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	if (psPerProc->bHandlesBatched) {
		psPerProc->bHandlesBatched = IMG_FALSE;

		PVRSRVReleaseHandleBatch(psPerProc->psHandleBase);
	}
}

static int PVRSRVEnumerateDevicesBW(u32 ui32BridgeID,
			 void *psBridgeIn,
			 struct PVRSRV_BRIDGE_OUT_ENUMDEVICE *psEnumDeviceOUT,
			 struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_ENUM_DEVICES);

	PVR_UNREFERENCED_PARAMETER(psPerProc);
	PVR_UNREFERENCED_PARAMETER(psBridgeIn);

	psEnumDeviceOUT->eError =
	    PVRSRVEnumerateDevicesKM(&psEnumDeviceOUT->ui32NumDevices,
				     psEnumDeviceOUT->asDeviceIdentifier);

	return 0;
}

static int PVRSRVAcquireDeviceDataBW(u32 ui32BridgeID,
	struct PVRSRV_BRIDGE_IN_ACQUIRE_DEVICEINFO *psAcquireDevInfoIN,
	struct PVRSRV_BRIDGE_OUT_ACQUIRE_DEVICEINFO *psAcquireDevInfoOUT,
	struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *hDevCookieInt;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_ACQUIRE_DEVICEINFO);

	psAcquireDevInfoOUT->eError =
	    PVRSRVAcquireDeviceDataKM(psAcquireDevInfoIN->uiDevIndex,
				      psAcquireDevInfoIN->eDeviceType,
				      &hDevCookieInt);
	if (psAcquireDevInfoOUT->eError != PVRSRV_OK)
		return 0;

	psAcquireDevInfoOUT->eError =
	    PVRSRVAllocHandle(psPerProc->psHandleBase,
			      &psAcquireDevInfoOUT->hDevCookie,
			      hDevCookieInt,
			      PVRSRV_HANDLE_TYPE_DEV_NODE,
			      PVRSRV_HANDLE_ALLOC_FLAG_SHARED);

	return 0;
}

static int SGXGetInfoForSrvinitBW(u32 ui32BridgeID,
	struct PVRSRV_BRIDGE_IN_SGXINFO_FOR_SRVINIT *psSGXInfoForSrvinitIN,
	struct PVRSRV_BRIDGE_OUT_SGXINFO_FOR_SRVINIT *psSGXInfoForSrvinitOUT,
	struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *hDevCookieInt;
	u32 i;
	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
			PVRSRV_BRIDGE_SGXINFO_FOR_SRVINIT);

	NEW_HANDLE_BATCH_OR_ERROR(psSGXInfoForSrvinitOUT->eError, psPerProc,
			PVRSRV_MAX_CLIENT_HEAPS);

	if (!psPerProc->bInitProcess) {
		psSGXInfoForSrvinitOUT->eError = PVRSRV_ERROR_GENERIC;
		return 0;
	}

	psSGXInfoForSrvinitOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase, &hDevCookieInt,
				psSGXInfoForSrvinitIN->hDevCookie,
				PVRSRV_HANDLE_TYPE_DEV_NODE);

	if (psSGXInfoForSrvinitOUT->eError != PVRSRV_OK)
		return 0;

	psSGXInfoForSrvinitOUT->eError =
		SGXGetInfoForSrvinitKM(hDevCookieInt,
				&psSGXInfoForSrvinitOUT->sInitInfo);

	if (psSGXInfoForSrvinitOUT->eError != PVRSRV_OK)
		return 0;

	for (i = 0; i < PVRSRV_MAX_CLIENT_HEAPS; i++) {
		struct PVRSRV_HEAP_INFO *psHeapInfo;

		psHeapInfo = &psSGXInfoForSrvinitOUT->sInitInfo.asHeapInfo[i];

		if (psHeapInfo->ui32HeapID !=
				(u32) SGX_UNDEFINED_HEAP_ID) {
			void *hDevMemHeapExt;

			if (psHeapInfo->hDevMemHeap != NULL) {

				PVRSRVAllocHandleNR(psPerProc->psHandleBase,
					&hDevMemHeapExt,
					psHeapInfo->hDevMemHeap,
					PVRSRV_HANDLE_TYPE_DEV_MEM_HEAP,
					PVRSRV_HANDLE_ALLOC_FLAG_SHARED);
				psHeapInfo->hDevMemHeap = hDevMemHeapExt;
			}
		}
	}

	COMMIT_HANDLE_BATCH_OR_ERROR(psSGXInfoForSrvinitOUT->eError, psPerProc);

	return 0;
}

static int PVRSRVCreateDeviceMemContextBW(u32 ui32BridgeID,
       struct PVRSRV_BRIDGE_IN_CREATE_DEVMEMCONTEXT *psCreateDevMemContextIN,
       struct PVRSRV_BRIDGE_OUT_CREATE_DEVMEMCONTEXT *psCreateDevMemContextOUT,
       struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *hDevCookieInt;
	void *hDevMemContextInt;
	u32 i;
	IMG_BOOL bCreated;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
			PVRSRV_BRIDGE_CREATE_DEVMEMCONTEXT);

	NEW_HANDLE_BATCH_OR_ERROR(psCreateDevMemContextOUT->eError, psPerProc,
			PVRSRV_MAX_CLIENT_HEAPS + 1);

	psCreateDevMemContextOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase, &hDevCookieInt,
				psCreateDevMemContextIN->hDevCookie,
				PVRSRV_HANDLE_TYPE_DEV_NODE);

	if (psCreateDevMemContextOUT->eError != PVRSRV_OK)
		return 0;

	psCreateDevMemContextOUT->eError =
		PVRSRVCreateDeviceMemContextKM(hDevCookieInt,
				psPerProc,
				&hDevMemContextInt,
				&psCreateDevMemContextOUT->
				ui32ClientHeapCount,
				&psCreateDevMemContextOUT->
				sHeapInfo[0], &bCreated
				, abSharedDeviceMemHeap
				);

	if (psCreateDevMemContextOUT->eError != PVRSRV_OK)
		return 0;

	if (bCreated) {
		PVRSRVAllocHandleNR(psPerProc->psHandleBase,
				&psCreateDevMemContextOUT->hDevMemContext,
				hDevMemContextInt,
				PVRSRV_HANDLE_TYPE_DEV_MEM_CONTEXT,
				PVRSRV_HANDLE_ALLOC_FLAG_NONE);
	} else {
		psCreateDevMemContextOUT->eError =
			PVRSRVFindHandle(psPerProc->psHandleBase,
				&psCreateDevMemContextOUT->hDevMemContext,
				hDevMemContextInt,
				PVRSRV_HANDLE_TYPE_DEV_MEM_CONTEXT);
		if (psCreateDevMemContextOUT->eError != PVRSRV_OK)
			return 0;
	}

	for (i = 0; i < psCreateDevMemContextOUT->ui32ClientHeapCount; i++) {
		void *hDevMemHeapExt;

		if (abSharedDeviceMemHeap[i]) {

			PVRSRVAllocHandleNR(psPerProc->psHandleBase,
					&hDevMemHeapExt,
					psCreateDevMemContextOUT->
					sHeapInfo[i].hDevMemHeap,
					PVRSRV_HANDLE_TYPE_DEV_MEM_HEAP,
					PVRSRV_HANDLE_ALLOC_FLAG_SHARED);
		} else {

			if (bCreated) {
				PVRSRVAllocSubHandleNR(psPerProc->psHandleBase,
						&hDevMemHeapExt,
						psCreateDevMemContextOUT->
						sHeapInfo[i].hDevMemHeap,
						PVRSRV_HANDLE_TYPE_DEV_MEM_HEAP,
						PVRSRV_HANDLE_ALLOC_FLAG_NONE,
						psCreateDevMemContextOUT->
						hDevMemContext);
			} else {
				psCreateDevMemContextOUT->eError =
					PVRSRVFindHandle(
					    psPerProc->psHandleBase,
					    &hDevMemHeapExt,
					    psCreateDevMemContextOUT->
					    sHeapInfo[i].hDevMemHeap,
					    PVRSRV_HANDLE_TYPE_DEV_MEM_HEAP);
				if (psCreateDevMemContextOUT->eError !=
						PVRSRV_OK)
					return 0;
			}
		}
		psCreateDevMemContextOUT->sHeapInfo[i].hDevMemHeap =
			hDevMemHeapExt;
	}

	COMMIT_HANDLE_BATCH_OR_ERROR(psCreateDevMemContextOUT->eError,
			psPerProc);

	return 0;
}

static int PVRSRVDestroyDeviceMemContextBW(u32 ui32BridgeID,
	struct PVRSRV_BRIDGE_IN_DESTROY_DEVMEMCONTEXT *psDestroyDevMemContextIN,
	struct PVRSRV_BRIDGE_RETURN *psRetOUT,
	struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *hDevCookieInt;
	void *hDevMemContextInt;
	IMG_BOOL bDestroyed;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
			PVRSRV_BRIDGE_DESTROY_DEVMEMCONTEXT);

	psRetOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase, &hDevCookieInt,
				psDestroyDevMemContextIN->hDevCookie,
				PVRSRV_HANDLE_TYPE_DEV_NODE);

	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError =
		PVRSRVLookupHandle(psPerProc->psHandleBase, &hDevMemContextInt,
				psDestroyDevMemContextIN->hDevMemContext,
				PVRSRV_HANDLE_TYPE_DEV_MEM_CONTEXT);

	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError =
		PVRSRVDestroyDeviceMemContextKM(hDevCookieInt,
				hDevMemContextInt,
				&bDestroyed);

	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	if (bDestroyed)
		psRetOUT->eError =
			PVRSRVReleaseHandle(psPerProc->psHandleBase,
					psDestroyDevMemContextIN->
					hDevMemContext,
					PVRSRV_HANDLE_TYPE_DEV_MEM_CONTEXT);

	return 0;
}

static int PVRSRVGetDeviceMemHeapInfoBW(u32 ui32BridgeID,
	   struct PVRSRV_BRIDGE_IN_GET_DEVMEM_HEAPINFO *psGetDevMemHeapInfoIN,
	   struct PVRSRV_BRIDGE_OUT_GET_DEVMEM_HEAPINFO *psGetDevMemHeapInfoOUT,
	   struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *hDevCookieInt;
	void *hDevMemContextInt;
	u32 i;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_GET_DEVMEM_HEAPINFO);

	NEW_HANDLE_BATCH_OR_ERROR(psGetDevMemHeapInfoOUT->eError, psPerProc,
				  PVRSRV_MAX_CLIENT_HEAPS);

	psGetDevMemHeapInfoOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase, &hDevCookieInt,
			       psGetDevMemHeapInfoIN->hDevCookie,
			       PVRSRV_HANDLE_TYPE_DEV_NODE);

	if (psGetDevMemHeapInfoOUT->eError != PVRSRV_OK)
		return 0;

	psGetDevMemHeapInfoOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase, &hDevMemContextInt,
			       psGetDevMemHeapInfoIN->hDevMemContext,
			       PVRSRV_HANDLE_TYPE_DEV_MEM_CONTEXT);

	if (psGetDevMemHeapInfoOUT->eError != PVRSRV_OK)
		return 0;

	psGetDevMemHeapInfoOUT->eError =
	    PVRSRVGetDeviceMemHeapInfoKM(hDevCookieInt,
				 hDevMemContextInt,
				 &psGetDevMemHeapInfoOUT->ui32ClientHeapCount,
				 &psGetDevMemHeapInfoOUT->sHeapInfo[0],
				 abSharedDeviceMemHeap);

	if (psGetDevMemHeapInfoOUT->eError != PVRSRV_OK)
		return 0;

	for (i = 0; i < psGetDevMemHeapInfoOUT->ui32ClientHeapCount; i++) {
		void *hDevMemHeapExt;

		if (abSharedDeviceMemHeap[i]) {
			PVRSRVAllocHandleNR(psPerProc->psHandleBase,
			       &hDevMemHeapExt,
			       psGetDevMemHeapInfoOUT->sHeapInfo[i].hDevMemHeap,
			       PVRSRV_HANDLE_TYPE_DEV_MEM_HEAP,
			       PVRSRV_HANDLE_ALLOC_FLAG_SHARED);
		} else {

			psGetDevMemHeapInfoOUT->eError =
			    PVRSRVFindHandle(psPerProc->psHandleBase,
					     &hDevMemHeapExt,
					     psGetDevMemHeapInfoOUT->
					     sHeapInfo[i].hDevMemHeap,
					     PVRSRV_HANDLE_TYPE_DEV_MEM_HEAP);
			if (psGetDevMemHeapInfoOUT->eError != PVRSRV_OK)
				return 0;
		}
		psGetDevMemHeapInfoOUT->sHeapInfo[i].hDevMemHeap =
		    hDevMemHeapExt;
	}

	COMMIT_HANDLE_BATCH_OR_ERROR(psGetDevMemHeapInfoOUT->eError, psPerProc);

	return 0;
}

static int PVRSRVAllocDeviceMemBW(u32 ui32BridgeID,
	       struct PVRSRV_BRIDGE_IN_ALLOCDEVICEMEM *psAllocDeviceMemIN,
	       struct PVRSRV_BRIDGE_OUT_ALLOCDEVICEMEM *psAllocDeviceMemOUT,
	       struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	struct PVRSRV_KERNEL_MEM_INFO *psMemInfo;
	void *hDevCookieInt;
	void *hDevMemHeapInt;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_ALLOC_DEVICEMEM);

	NEW_HANDLE_BATCH_OR_ERROR(psAllocDeviceMemOUT->eError, psPerProc, 2);

	psAllocDeviceMemOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase, &hDevCookieInt,
			       psAllocDeviceMemIN->hDevCookie,
			       PVRSRV_HANDLE_TYPE_DEV_NODE);

	if (psAllocDeviceMemOUT->eError != PVRSRV_OK)
		return 0;

	psAllocDeviceMemOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase, &hDevMemHeapInt,
			       psAllocDeviceMemIN->hDevMemHeap,
			       PVRSRV_HANDLE_TYPE_DEV_MEM_HEAP);

	if (psAllocDeviceMemOUT->eError != PVRSRV_OK)
		return 0;

	psAllocDeviceMemOUT->eError =
	    PVRSRVAllocDeviceMemKM(hDevCookieInt,
				   psPerProc,
				   hDevMemHeapInt,
				   psAllocDeviceMemIN->ui32Attribs,
				   psAllocDeviceMemIN->ui32Size,
				   psAllocDeviceMemIN->ui32Alignment,
				   &psMemInfo);

	if (psAllocDeviceMemOUT->eError != PVRSRV_OK)
		return 0;

	OSMemSet(&psAllocDeviceMemOUT->sClientMemInfo,
		 0, sizeof(psAllocDeviceMemOUT->sClientMemInfo));

	if (psMemInfo->pvLinAddrKM)
		psAllocDeviceMemOUT->sClientMemInfo.pvLinAddrKM =
		    psMemInfo->pvLinAddrKM;
	else
		psAllocDeviceMemOUT->sClientMemInfo.pvLinAddrKM =
		    psMemInfo->sMemBlk.hOSMemHandle;
	psAllocDeviceMemOUT->sClientMemInfo.pvLinAddr = NULL;
	psAllocDeviceMemOUT->sClientMemInfo.sDevVAddr = psMemInfo->sDevVAddr;
	psAllocDeviceMemOUT->sClientMemInfo.ui32Flags = psMemInfo->ui32Flags;
	psAllocDeviceMemOUT->sClientMemInfo.ui32AllocSize =
	    psMemInfo->ui32AllocSize;
	psAllocDeviceMemOUT->sClientMemInfo.hMappingInfo =
	    psMemInfo->sMemBlk.hOSMemHandle;

	PVRSRVAllocHandleNR(psPerProc->psHandleBase,
			    &psAllocDeviceMemOUT->sClientMemInfo.hKernelMemInfo,
			    psMemInfo,
			    PVRSRV_HANDLE_TYPE_MEM_INFO,
			    PVRSRV_HANDLE_ALLOC_FLAG_NONE);

	if (psAllocDeviceMemIN->ui32Attribs & PVRSRV_MEM_NO_SYNCOBJ) {

		OSMemSet(&psAllocDeviceMemOUT->sClientSyncInfo,
			 0, sizeof(struct PVRSRV_CLIENT_SYNC_INFO));
		psAllocDeviceMemOUT->sClientMemInfo.psClientSyncInfo = NULL;
		psAllocDeviceMemOUT->psKernelSyncInfo = NULL;
	} else {

		psAllocDeviceMemOUT->psKernelSyncInfo =
		    psMemInfo->psKernelSyncInfo;

		psAllocDeviceMemOUT->sClientSyncInfo.psSyncData =
		    psMemInfo->psKernelSyncInfo->psSyncData;
		psAllocDeviceMemOUT->sClientSyncInfo.sWriteOpsCompleteDevVAddr =
		    psMemInfo->psKernelSyncInfo->sWriteOpsCompleteDevVAddr;
		psAllocDeviceMemOUT->sClientSyncInfo.sReadOpsCompleteDevVAddr =
		    psMemInfo->psKernelSyncInfo->sReadOpsCompleteDevVAddr;

		psAllocDeviceMemOUT->sClientSyncInfo.hMappingInfo =
		    psMemInfo->psKernelSyncInfo->psSyncDataMemInfoKM->sMemBlk.
		    hOSMemHandle;

		PVRSRVAllocSubHandleNR(psPerProc->psHandleBase,
				       &psAllocDeviceMemOUT->sClientSyncInfo.
				       hKernelSyncInfo,
				       psMemInfo->psKernelSyncInfo,
				       PVRSRV_HANDLE_TYPE_SYNC_INFO,
				       PVRSRV_HANDLE_ALLOC_FLAG_NONE,
				       psAllocDeviceMemOUT->sClientMemInfo.
				       hKernelMemInfo);

		psAllocDeviceMemOUT->sClientMemInfo.psClientSyncInfo =
		    &psAllocDeviceMemOUT->sClientSyncInfo;

	}

	COMMIT_HANDLE_BATCH_OR_ERROR(psAllocDeviceMemOUT->eError, psPerProc);

	return 0;
}


static int PVRSRVFreeDeviceMemBW(u32 ui32BridgeID,
		      struct PVRSRV_BRIDGE_IN_FREEDEVICEMEM *psFreeDeviceMemIN,
		      struct PVRSRV_BRIDGE_RETURN *psRetOUT,
		      struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *hDevCookieInt;
	void *pvKernelMemInfo;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_FREE_DEVICEMEM);

	psRetOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase, &hDevCookieInt,
			       psFreeDeviceMemIN->hDevCookie,
			       PVRSRV_HANDLE_TYPE_DEV_NODE);

	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase, &pvKernelMemInfo,
			       psFreeDeviceMemIN->psKernelMemInfo,
			       PVRSRV_HANDLE_TYPE_MEM_INFO);

	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError =
	    PVRSRVFreeDeviceMemKM(hDevCookieInt, pvKernelMemInfo);

	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError =
	    PVRSRVReleaseHandle(psPerProc->psHandleBase,
				psFreeDeviceMemIN->psKernelMemInfo,
				PVRSRV_HANDLE_TYPE_MEM_INFO);

	return 0;
}

static int PVRSRVMapDeviceMemoryBW(u32 ui32BridgeID,
			struct PVRSRV_BRIDGE_IN_MAP_DEV_MEMORY *psMapDevMemIN,
			struct PVRSRV_BRIDGE_OUT_MAP_DEV_MEMORY *psMapDevMemOUT,
			struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	struct PVRSRV_KERNEL_MEM_INFO *psSrcKernelMemInfo = NULL;
	struct PVRSRV_KERNEL_MEM_INFO *psDstKernelMemInfo = NULL;
	void *hDstDevMemHeap = NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_MAP_DEV_MEMORY);

	NEW_HANDLE_BATCH_OR_ERROR(psMapDevMemOUT->eError, psPerProc, 2);

	psMapDevMemOUT->eError = PVRSRVLookupHandle(psPerProc->psHandleBase,
					    (void **)&psSrcKernelMemInfo,
					    psMapDevMemIN->psSrcKernelMemInfo,
					    PVRSRV_HANDLE_TYPE_MEM_INFO);
	if (psMapDevMemOUT->eError != PVRSRV_OK)
		return 0;

	psMapDevMemOUT->eError = PVRSRVLookupHandle(psPerProc->psHandleBase,
					    &hDstDevMemHeap,
					    psMapDevMemIN->
					    hDstDevMemHeap,
					    PVRSRV_HANDLE_TYPE_DEV_MEM_HEAP);
	if (psMapDevMemOUT->eError != PVRSRV_OK)
		return 0;

	psMapDevMemOUT->eError = PVRSRVMapDeviceMemoryKM(psPerProc,
							 psSrcKernelMemInfo,
							 hDstDevMemHeap,
							 &psDstKernelMemInfo);
	if (psMapDevMemOUT->eError != PVRSRV_OK)
		return 0;

	OSMemSet(&psMapDevMemOUT->sDstClientMemInfo,
		 0, sizeof(psMapDevMemOUT->sDstClientMemInfo));
	OSMemSet(&psMapDevMemOUT->sDstClientSyncInfo,
		 0, sizeof(psMapDevMemOUT->sDstClientSyncInfo));

	if (psDstKernelMemInfo->pvLinAddrKM)
		psMapDevMemOUT->sDstClientMemInfo.pvLinAddrKM =
		    psDstKernelMemInfo->pvLinAddrKM;
	else
		psMapDevMemOUT->sDstClientMemInfo.pvLinAddrKM =
		    psDstKernelMemInfo->sMemBlk.hOSMemHandle;
	psMapDevMemOUT->sDstClientMemInfo.pvLinAddr = NULL;
	psMapDevMemOUT->sDstClientMemInfo.sDevVAddr =
	    psDstKernelMemInfo->sDevVAddr;
	psMapDevMemOUT->sDstClientMemInfo.ui32Flags =
	    psDstKernelMemInfo->ui32Flags;
	psMapDevMemOUT->sDstClientMemInfo.ui32AllocSize =
	    psDstKernelMemInfo->ui32AllocSize;
	psMapDevMemOUT->sDstClientMemInfo.hMappingInfo =
	    psDstKernelMemInfo->sMemBlk.hOSMemHandle;

	PVRSRVAllocHandleNR(psPerProc->psHandleBase,
			    &psMapDevMemOUT->sDstClientMemInfo.hKernelMemInfo,
			    psDstKernelMemInfo,
			    PVRSRV_HANDLE_TYPE_MEM_INFO,
			    PVRSRV_HANDLE_ALLOC_FLAG_NONE);
	psMapDevMemOUT->sDstClientSyncInfo.hKernelSyncInfo = NULL;
	psMapDevMemOUT->psDstKernelSyncInfo = NULL;

	if (psDstKernelMemInfo->psKernelSyncInfo) {
		psMapDevMemOUT->psDstKernelSyncInfo =
		    psDstKernelMemInfo->psKernelSyncInfo;

		psMapDevMemOUT->sDstClientSyncInfo.psSyncData =
		    psDstKernelMemInfo->psKernelSyncInfo->psSyncData;
		psMapDevMemOUT->sDstClientSyncInfo.sWriteOpsCompleteDevVAddr =
		    psDstKernelMemInfo->psKernelSyncInfo->
		    sWriteOpsCompleteDevVAddr;
		psMapDevMemOUT->sDstClientSyncInfo.sReadOpsCompleteDevVAddr =
		    psDstKernelMemInfo->psKernelSyncInfo->
		    sReadOpsCompleteDevVAddr;

		psMapDevMemOUT->sDstClientSyncInfo.hMappingInfo =
		    psDstKernelMemInfo->psKernelSyncInfo->psSyncDataMemInfoKM->
		    sMemBlk.hOSMemHandle;

		psMapDevMemOUT->sDstClientMemInfo.psClientSyncInfo =
		    &psMapDevMemOUT->sDstClientSyncInfo;

		PVRSRVAllocSubHandleNR(psPerProc->psHandleBase,
				       &psMapDevMemOUT->sDstClientSyncInfo.
				       hKernelSyncInfo,
				       psDstKernelMemInfo->psKernelSyncInfo,
				       PVRSRV_HANDLE_TYPE_SYNC_INFO,
				       PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
				       psMapDevMemOUT->sDstClientMemInfo.
				       hKernelMemInfo);
	}

	COMMIT_HANDLE_BATCH_OR_ERROR(psMapDevMemOUT->eError, psPerProc);

	return 0;
}

static int PVRSRVUnmapDeviceMemoryBW(u32 ui32BridgeID,
		  struct PVRSRV_BRIDGE_IN_UNMAP_DEV_MEMORY *psUnmapDevMemIN,
		  struct PVRSRV_BRIDGE_RETURN *psRetOUT,
		  struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	struct PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo = NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_UNMAP_DEV_MEMORY);

	psRetOUT->eError = PVRSRVLookupHandle(psPerProc->psHandleBase,
					      (void **) &psKernelMemInfo,
					      psUnmapDevMemIN->psKernelMemInfo,
					      PVRSRV_HANDLE_TYPE_MEM_INFO);
	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError = PVRSRVUnmapDeviceMemoryKM(psKernelMemInfo);
	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError = PVRSRVReleaseHandle(psPerProc->psHandleBase,
					       psUnmapDevMemIN->psKernelMemInfo,
					       PVRSRV_HANDLE_TYPE_MEM_INFO);

	return 0;
}

static int FlushCacheDRI(int dir, struct BM_BUF *buf)
{
	unsigned long s, e;
	struct IMG_SYS_PHYADDR *paddr;
	int pg_cnt;

	s = (unsigned long)buf->pMapping->CpuVAddr;
	e = s + buf->pMapping->uSize;
	if (s == e)
		return 0;

	pg_cnt = (PAGE_ALIGN(e) - (s & PAGE_MASK)) / PAGE_SIZE;
	paddr = buf->pvPageList;

	for (; pg_cnt; pg_cnt--) {
		struct page *page;
		size_t chunk;
		void *kaddr;

		page = pfn_to_page(paddr->uiAddr >> PAGE_SHIFT);
		kaddr = page_address(page);
		/* Adjust for the first page */
		kaddr += s & ~PAGE_MASK;
		chunk = PAGE_SIZE - (s & ~PAGE_MASK);

		/* Adjust for the last page */
		chunk = min_t(ssize_t, e - s, chunk);
		dma_cache_maint(kaddr, chunk, dir);

		s += chunk;

		paddr++;
	}

	return 0;
}

static struct BM_BUF *get_dev_buf(void *dev_cookie, void *virt_addr)
{
	struct BM_BUF *buf;
	void *heap;
	struct PVRSRV_DEVICE_NODE *devnode;
	struct DEVICE_MEMORY_INFO *dev_mem_info;

	devnode = (struct PVRSRV_DEVICE_NODE *)dev_cookie;
	dev_mem_info = &devnode->sDevMemoryInfo;
	heap = dev_mem_info->
		psDeviceMemoryHeap[SGX_GENERAL_MAPPING_HEAP_ID].hDevMemHeap;
	buf = bm_get_buf_virt(heap, virt_addr);
	if (buf)
		return buf;

	heap = dev_mem_info->
		psDeviceMemoryHeap[SGX_ALT_MAPPING_HEAP_ID].hDevMemHeap;
	buf = bm_get_buf_virt(heap, virt_addr);
	return buf;
}

static int PVRSRVCacheFlushDRIBW(u32 ui32BridgeID,
	      struct PVRSRV_BRIDGE_IN_CACHEFLUSHDRMFROMUSER *psCacheFlushIN,
	      struct PVRSRV_BRIDGE_RETURN *psRetOUT,
	      struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *dev_cookie_int;
	void *start;
	size_t length;
	int res = 0;
	struct BM_BUF *dev_buf;
	int dir;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_CACHE_FLUSH_DRM);

	switch (psCacheFlushIN->ui32Type) {
	case DRM_PVR2D_CFLUSH_FROM_GPU:
		dir = DMA_FROM_DEVICE;
		PVR_DPF(PVR_DBG_MESSAGE,
			 "DRM_PVR2D_CFLUSH_FROM_GPU 0x%08x, length 0x%08x\n",
			 pvVirt, ui32Length);
		break;
	case DRM_PVR2D_CFLUSH_TO_GPU:
		dir = DMA_TO_DEVICE;
		PVR_DPF(PVR_DBG_MESSAGE,
			 "DRM_PVR2D_CFLUSH_TO_GPU 0x%08x, length 0x%08x\n",
			 pvVirt, ui32Length);
		break;
	default:
		PVR_DPF(PVR_DBG_ERROR, "Invalid cflush type 0x%x\n",
			 ui32Type);
		return -EINVAL;
	}

	PVRSRVLookupHandle(psPerProc->psHandleBase, &dev_cookie_int,
			   psCacheFlushIN->hDevCookie,
			   PVRSRV_HANDLE_TYPE_DEV_NODE);
	PVR_ASSERT(dev_cookie_int);

	start = psCacheFlushIN->pvVirt;
	length = psCacheFlushIN->ui32Length;

	down_read(&current->mm->mmap_sem);

	dev_buf = get_dev_buf(dev_cookie_int, start);

	if (dev_buf && length <= dev_buf->pMapping->uSize) {
		psRetOUT->eError = FlushCacheDRI(dir, dev_buf);
	} else {
		printk(KERN_WARNING
			"%s: Start address %p and length %#08x not wrapped \n",
			__func__,
			start, length);
		res = -EINVAL;
	}

	up_read(&current->mm->mmap_sem);

	return res;
}

static int PVRSRVMapDeviceClassMemoryBW(u32 ui32BridgeID,
	   struct PVRSRV_BRIDGE_IN_MAP_DEVICECLASS_MEMORY *psMapDevClassMemIN,
	   struct PVRSRV_BRIDGE_OUT_MAP_DEVICECLASS_MEMORY *psMapDevClassMemOUT,
	   struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	struct PVRSRV_KERNEL_MEM_INFO *psMemInfo;
	void *hOSMapInfo;
	void *hDeviceClassBufferInt;
	enum PVRSRV_HANDLE_TYPE eHandleType;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
			PVRSRV_BRIDGE_MAP_DEVICECLASS_MEMORY);

	NEW_HANDLE_BATCH_OR_ERROR(psMapDevClassMemOUT->eError, psPerProc, 2);

	psMapDevClassMemOUT->eError =
		PVRSRVLookupHandleAnyType(psPerProc->psHandleBase,
				&hDeviceClassBufferInt, &eHandleType,
				psMapDevClassMemIN->hDeviceClassBuffer);

	if (psMapDevClassMemOUT->eError != PVRSRV_OK)
		return 0;

	switch (eHandleType) {
	case PVRSRV_HANDLE_TYPE_DISP_BUFFER:
	case PVRSRV_HANDLE_TYPE_BUF_BUFFER:
		break;
	default:
		psMapDevClassMemOUT->eError = PVRSRV_ERROR_GENERIC;
		return 0;
	}

	psMapDevClassMemOUT->eError =
		PVRSRVMapDeviceClassMemoryKM(psPerProc,
				hDeviceClassBufferInt,
				&psMemInfo, &hOSMapInfo);

	if (psMapDevClassMemOUT->eError != PVRSRV_OK)
		return 0;

	OSMemSet(&psMapDevClassMemOUT->sClientMemInfo,
			0, sizeof(psMapDevClassMemOUT->sClientMemInfo));
	OSMemSet(&psMapDevClassMemOUT->sClientSyncInfo,
			0, sizeof(psMapDevClassMemOUT->sClientSyncInfo));

	if (psMemInfo->pvLinAddrKM)
		psMapDevClassMemOUT->sClientMemInfo.pvLinAddrKM =
			psMemInfo->pvLinAddrKM;
	else
		psMapDevClassMemOUT->sClientMemInfo.pvLinAddrKM =
			psMemInfo->sMemBlk.hOSMemHandle;
	psMapDevClassMemOUT->sClientMemInfo.pvLinAddr = NULL;
	psMapDevClassMemOUT->sClientMemInfo.sDevVAddr = psMemInfo->sDevVAddr;
	psMapDevClassMemOUT->sClientMemInfo.ui32Flags = psMemInfo->ui32Flags;
	psMapDevClassMemOUT->sClientMemInfo.ui32AllocSize =
		psMemInfo->ui32AllocSize;
	psMapDevClassMemOUT->sClientMemInfo.hMappingInfo =
		psMemInfo->sMemBlk.hOSMemHandle;

	PVRSRVAllocSubHandleNR(psPerProc->psHandleBase,
			&psMapDevClassMemOUT->sClientMemInfo.
			hKernelMemInfo, psMemInfo,
			PVRSRV_HANDLE_TYPE_MEM_INFO,
			PVRSRV_HANDLE_ALLOC_FLAG_NONE,
			psMapDevClassMemIN->hDeviceClassBuffer);

	psMapDevClassMemOUT->sClientSyncInfo.hKernelSyncInfo = NULL;
	psMapDevClassMemOUT->psKernelSyncInfo = NULL;

	if (psMemInfo->psKernelSyncInfo) {
		psMapDevClassMemOUT->psKernelSyncInfo =
			psMemInfo->psKernelSyncInfo;

		psMapDevClassMemOUT->sClientSyncInfo.psSyncData =
			psMemInfo->psKernelSyncInfo->psSyncData;
		psMapDevClassMemOUT->sClientSyncInfo.sWriteOpsCompleteDevVAddr =
			psMemInfo->psKernelSyncInfo->sWriteOpsCompleteDevVAddr;
		psMapDevClassMemOUT->sClientSyncInfo.sReadOpsCompleteDevVAddr =
			psMemInfo->psKernelSyncInfo->sReadOpsCompleteDevVAddr;

		psMapDevClassMemOUT->sClientSyncInfo.hMappingInfo =
				psMemInfo->psKernelSyncInfo->
				     psSyncDataMemInfoKM->sMemBlk.hOSMemHandle;

		psMapDevClassMemOUT->sClientMemInfo.psClientSyncInfo =
			&psMapDevClassMemOUT->sClientSyncInfo;

		PVRSRVAllocSubHandleNR(psPerProc->psHandleBase,
				&psMapDevClassMemOUT->sClientSyncInfo.
				hKernelSyncInfo,
				psMemInfo->psKernelSyncInfo,
				PVRSRV_HANDLE_TYPE_SYNC_INFO,
				PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
				psMapDevClassMemOUT->sClientMemInfo.
				hKernelMemInfo);
	}

	COMMIT_HANDLE_BATCH_OR_ERROR(psMapDevClassMemOUT->eError, psPerProc);

	return 0;
}

static int PVRSRVUnmapDeviceClassMemoryBW(u32 ui32BridgeID,
	 struct PVRSRV_BRIDGE_IN_UNMAP_DEVICECLASS_MEMORY *psUnmapDevClassMemIN,
	 struct PVRSRV_BRIDGE_RETURN *psRetOUT,
	 struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *pvKernelMemInfo;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_UNMAP_DEVICECLASS_MEMORY);

	psRetOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase, &pvKernelMemInfo,
			       psUnmapDevClassMemIN->psKernelMemInfo,
			       PVRSRV_HANDLE_TYPE_MEM_INFO);
	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError = PVRSRVUnmapDeviceClassMemoryKM(pvKernelMemInfo);

	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError =
	    PVRSRVReleaseHandle(psPerProc->psHandleBase,
				psUnmapDevClassMemIN->psKernelMemInfo,
				PVRSRV_HANDLE_TYPE_MEM_INFO);

	return 0;
}

static int PVRSRVWrapExtMemoryBW(u32 ui32BridgeID,
		      struct PVRSRV_BRIDGE_IN_WRAP_EXT_MEMORY *psWrapExtMemIN,
		      struct PVRSRV_BRIDGE_OUT_WRAP_EXT_MEMORY *psWrapExtMemOUT,
		      struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *hDevCookieInt;
	struct PVRSRV_KERNEL_MEM_INFO *psMemInfo;
	u32 ui32PageTableSize = 0;
	struct IMG_SYS_PHYADDR *psSysPAddr = NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_WRAP_EXT_MEMORY);

	NEW_HANDLE_BATCH_OR_ERROR(psWrapExtMemOUT->eError, psPerProc, 2);

	psWrapExtMemOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase, &hDevCookieInt,
			       psWrapExtMemIN->hDevCookie,
			       PVRSRV_HANDLE_TYPE_DEV_NODE);
	if (psWrapExtMemOUT->eError != PVRSRV_OK)
		return 0;

	if (psWrapExtMemIN->ui32NumPageTableEntries) {
		ui32PageTableSize = psWrapExtMemIN->ui32NumPageTableEntries
		    * sizeof(struct IMG_SYS_PHYADDR);

		ASSIGN_AND_EXIT_ON_ERROR(psWrapExtMemOUT->eError,
					 OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
						   ui32PageTableSize,
						   (void **)&psSysPAddr, NULL));

		if (CopyFromUserWrapper(psPerProc,
					ui32BridgeID,
					psSysPAddr,
					psWrapExtMemIN->psSysPAddr,
					ui32PageTableSize) != PVRSRV_OK) {
			OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, ui32PageTableSize,
				  (void *) psSysPAddr, NULL);
			return -EFAULT;
		}
	}

	psWrapExtMemOUT->eError =
	    PVRSRVWrapExtMemoryKM(hDevCookieInt,
				  psPerProc,
				  psWrapExtMemIN->ui32ByteSize,
				  psWrapExtMemIN->ui32PageOffset,
				  psWrapExtMemIN->bPhysContig,
				  psSysPAddr,
				  psWrapExtMemIN->pvLinAddr, &psMemInfo);
	if (psWrapExtMemOUT->eError != PVRSRV_OK) {
		/* PVRSRVWrapExtMemoryKM failed, so clean up page list */
		if (psWrapExtMemIN->ui32NumPageTableEntries)
			OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
				  ui32PageTableSize,
				  (void *) psSysPAddr, NULL);
		return 0;
	}

	if (psMemInfo->pvLinAddrKM)
		psWrapExtMemOUT->sClientMemInfo.pvLinAddrKM =
		    psMemInfo->pvLinAddrKM;
	else
		psWrapExtMemOUT->sClientMemInfo.pvLinAddrKM =
		    psMemInfo->sMemBlk.hOSMemHandle;

	psWrapExtMemOUT->sClientMemInfo.pvLinAddr = NULL;
	psWrapExtMemOUT->sClientMemInfo.sDevVAddr = psMemInfo->sDevVAddr;
	psWrapExtMemOUT->sClientMemInfo.ui32Flags = psMemInfo->ui32Flags;
	psWrapExtMemOUT->sClientMemInfo.ui32AllocSize =
	    psMemInfo->ui32AllocSize;
	psWrapExtMemOUT->sClientMemInfo.hMappingInfo = NULL;

	PVRSRVAllocHandleNR(psPerProc->psHandleBase,
			    &psWrapExtMemOUT->sClientMemInfo.hKernelMemInfo,
			    psMemInfo,
			    PVRSRV_HANDLE_TYPE_MEM_INFO,
			    PVRSRV_HANDLE_ALLOC_FLAG_NONE);

	psWrapExtMemOUT->sClientSyncInfo.psSyncData =
	    psMemInfo->psKernelSyncInfo->psSyncData;
	psWrapExtMemOUT->sClientSyncInfo.sWriteOpsCompleteDevVAddr =
	    psMemInfo->psKernelSyncInfo->sWriteOpsCompleteDevVAddr;
	psWrapExtMemOUT->sClientSyncInfo.sReadOpsCompleteDevVAddr =
	    psMemInfo->psKernelSyncInfo->sReadOpsCompleteDevVAddr;

	psWrapExtMemOUT->sClientSyncInfo.hMappingInfo =
	    psMemInfo->psKernelSyncInfo->psSyncDataMemInfoKM->sMemBlk.
	    hOSMemHandle;

	psWrapExtMemOUT->sClientMemInfo.psClientSyncInfo =
	    &psWrapExtMemOUT->sClientSyncInfo;

	PVRSRVAllocSubHandleNR(psPerProc->psHandleBase,
			       &psWrapExtMemOUT->sClientSyncInfo.
			       hKernelSyncInfo,
			       (void *) psMemInfo->psKernelSyncInfo,
			       PVRSRV_HANDLE_TYPE_SYNC_INFO,
			       PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
			       psWrapExtMemOUT->sClientMemInfo.hKernelMemInfo);

	COMMIT_HANDLE_BATCH_OR_ERROR(psWrapExtMemOUT->eError, psPerProc);

	return 0;
}

static int PVRSRVUnwrapExtMemoryBW(u32 ui32BridgeID,
		struct PVRSRV_BRIDGE_IN_UNWRAP_EXT_MEMORY *psUnwrapExtMemIN,
		struct PVRSRV_BRIDGE_RETURN *psRetOUT,
		struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *pvMemInfo;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_UNWRAP_EXT_MEMORY);

	psRetOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &pvMemInfo,
			       psUnwrapExtMemIN->hKernelMemInfo,
			       PVRSRV_HANDLE_TYPE_MEM_INFO);
	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError =
	    PVRSRVUnwrapExtMemoryKM((struct PVRSRV_KERNEL_MEM_INFO *)pvMemInfo);
	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError =
	    PVRSRVReleaseHandle(psPerProc->psHandleBase,
				psUnwrapExtMemIN->hKernelMemInfo,
				PVRSRV_HANDLE_TYPE_MEM_INFO);

	return 0;
}

static int PVRSRVGetFreeDeviceMemBW(u32 ui32BridgeID,
	 struct PVRSRV_BRIDGE_IN_GETFREEDEVICEMEM *psGetFreeDeviceMemIN,
	 struct PVRSRV_BRIDGE_OUT_GETFREEDEVICEMEM *psGetFreeDeviceMemOUT,
	 struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_GETFREE_DEVICEMEM);

	PVR_UNREFERENCED_PARAMETER(psPerProc);

	psGetFreeDeviceMemOUT->eError =
	    PVRSRVGetFreeDeviceMemKM(psGetFreeDeviceMemIN->ui32Flags,
				     &psGetFreeDeviceMemOUT->ui32Total,
				     &psGetFreeDeviceMemOUT->ui32Free,
				     &psGetFreeDeviceMemOUT->ui32LargestBlock);

	return 0;
}

static int PVRMMapKVIndexAddressToMMapDataBW(u32 ui32BridgeID,
		  struct PVRSRV_BRIDGE_IN_KV_TO_MMAP_DATA *psMMapDataIN,
		  struct PVRSRV_BRIDGE_OUT_KV_TO_MMAP_DATA *psMMapDataOUT,
		  struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_KV_TO_MMAP_DATA);
	PVR_UNREFERENCED_PARAMETER(psMMapDataIN);
	PVR_UNREFERENCED_PARAMETER(psPerProc);

	psMMapDataOUT->eError =
	    PVRMMapKVIndexAddressToMMapData(psMMapDataIN->pvKVIndexAddress,
					    psMMapDataIN->ui32Bytes,
					    &psMMapDataOUT->ui32MMapOffset,
					    &psMMapDataOUT->ui32ByteOffset,
					    &psMMapDataOUT->ui32RealByteSize);

	return 0;
}

#ifdef PDUMP
static int PDumpIsCaptureFrameBW(u32 ui32BridgeID,
	      void *psBridgeIn,
	      struct PVRSRV_BRIDGE_OUT_PDUMP_ISCAPTURING *psPDumpIsCapturingOUT,
	      struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_PDUMP_ISCAPTURING);
	PVR_UNREFERENCED_PARAMETER(psBridgeIn);
	PVR_UNREFERENCED_PARAMETER(psPerProc);

	psPDumpIsCapturingOUT->bIsCapturing = PDumpIsCaptureFrameKM();
	psPDumpIsCapturingOUT->eError = PVRSRV_OK;

	return 0;
}

static int PDumpCommentBW(u32 ui32BridgeID,
	       struct PVRSRV_BRIDGE_IN_PDUMP_COMMENT *psPDumpCommentIN,
	       struct PVRSRV_BRIDGE_RETURN *psRetOUT,
	       struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_PDUMP_COMMENT);
	PVR_UNREFERENCED_PARAMETER(psPerProc);

	psRetOUT->eError = PDumpCommentKM(&psPDumpCommentIN->szComment[0],
					  psPDumpCommentIN->ui32Flags);
	return 0;
}

static int PDumpSetFrameBW(u32 ui32BridgeID,
		struct PVRSRV_BRIDGE_IN_PDUMP_SETFRAME *psPDumpSetFrameIN,
		struct PVRSRV_BRIDGE_RETURN *psRetOUT,
		struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_PDUMP_SETFRAME);
	PVR_UNREFERENCED_PARAMETER(psPerProc);

	psRetOUT->eError = PDumpSetFrameKM(psPDumpSetFrameIN->ui32Frame);

	return 0;
}

static int PDumpRegWithFlagsBW(u32 ui32BridgeID,
		    struct PVRSRV_BRIDGE_IN_PDUMP_DUMPREG *psPDumpRegDumpIN,
		    struct PVRSRV_BRIDGE_RETURN *psRetOUT,
		    struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_PDUMP_REG);
	PVR_UNREFERENCED_PARAMETER(psPerProc);

	psRetOUT->eError =
	    PDumpRegWithFlagsKM(psPDumpRegDumpIN->sHWReg.ui32RegAddr,
				psPDumpRegDumpIN->sHWReg.ui32RegVal,
				psPDumpRegDumpIN->ui32Flags);

	return 0;
}

static int PDumpRegPolBW(u32 ui32BridgeID,
	      struct PVRSRV_BRIDGE_IN_PDUMP_REGPOL *psPDumpRegPolIN,
	      struct PVRSRV_BRIDGE_RETURN *psRetOUT,
	      struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_PDUMP_REGPOL);
	PVR_UNREFERENCED_PARAMETER(psPerProc);

	psRetOUT->eError =
	    PDumpRegPolWithFlagsKM(psPDumpRegPolIN->sHWReg.ui32RegAddr,
				   psPDumpRegPolIN->sHWReg.ui32RegVal,
				   psPDumpRegPolIN->ui32Mask,
				   psPDumpRegPolIN->ui32Flags);

	return 0;
}

static int PDumpMemPolBW(u32 ui32BridgeID,
	      struct PVRSRV_BRIDGE_IN_PDUMP_MEMPOL *psPDumpMemPolIN,
	      struct PVRSRV_BRIDGE_RETURN *psRetOUT,
	      struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *pvMemInfo;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_PDUMP_MEMPOL);

	psRetOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &pvMemInfo,
			       psPDumpMemPolIN->psKernelMemInfo,
			       PVRSRV_HANDLE_TYPE_MEM_INFO);
	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError =
	    PDumpMemPolKM(((struct PVRSRV_KERNEL_MEM_INFO *)pvMemInfo),
			  psPDumpMemPolIN->ui32Offset,
			  psPDumpMemPolIN->ui32Value,
			  psPDumpMemPolIN->ui32Mask,
			  PDUMP_POLL_OPERATOR_EQUAL,
			  psPDumpMemPolIN->bLastFrame,
			  psPDumpMemPolIN->bOverwrite,
			  MAKEUNIQUETAG(pvMemInfo));

	return 0;
}

static int PDumpMemBW(u32 ui32BridgeID,
	   struct PVRSRV_BRIDGE_IN_PDUMP_DUMPMEM *psPDumpMemDumpIN,
	   struct PVRSRV_BRIDGE_RETURN *psRetOUT,
	   struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *pvMemInfo;
	void *pvAltLinAddrKM = NULL;
	u32 ui32Bytes = psPDumpMemDumpIN->ui32Bytes;
	void *hBlockAlloc = 0;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_PDUMP_DUMPMEM);

	psRetOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &pvMemInfo,
			       psPDumpMemDumpIN->psKernelMemInfo,
			       PVRSRV_HANDLE_TYPE_MEM_INFO);
	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	if (psPDumpMemDumpIN->pvAltLinAddr) {
		if (OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
			       ui32Bytes,
			       &pvAltLinAddrKM, &hBlockAlloc) != PVRSRV_OK)
			return -EFAULT;

		if (CopyFromUserWrapper(psPerProc,
					ui32BridgeID,
					pvAltLinAddrKM,
					psPDumpMemDumpIN->pvAltLinAddr,
					ui32Bytes) != PVRSRV_OK) {
			OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, ui32Bytes,
				  pvAltLinAddrKM, hBlockAlloc);
			return -EFAULT;
		}
	}

	psRetOUT->eError =
	    PDumpMemKM(pvAltLinAddrKM,
		       pvMemInfo,
		       psPDumpMemDumpIN->ui32Offset,
		       ui32Bytes,
		       psPDumpMemDumpIN->ui32Flags, MAKEUNIQUETAG(pvMemInfo));

	if (pvAltLinAddrKM)
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, ui32Bytes, pvAltLinAddrKM,
			  hBlockAlloc);

	return 0;
}

static int PDumpBitmapBW(u32 ui32BridgeID,
	      struct PVRSRV_BRIDGE_IN_PDUMP_BITMAP *psPDumpBitmapIN,
	      struct PVRSRV_BRIDGE_RETURN *psRetOUT,
	      struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVR_UNREFERENCED_PARAMETER(psPerProc);
	PVR_UNREFERENCED_PARAMETER(ui32BridgeID);

	psRetOUT->eError =
	    PDumpBitmapKM(&psPDumpBitmapIN->szFileName[0],
			  psPDumpBitmapIN->ui32FileOffset,
			  psPDumpBitmapIN->ui32Width,
			  psPDumpBitmapIN->ui32Height,
			  psPDumpBitmapIN->ui32StrideInBytes,
			  psPDumpBitmapIN->sDevBaseAddr,
			  psPDumpBitmapIN->ui32Size,
			  psPDumpBitmapIN->ePixelFormat,
			  psPDumpBitmapIN->eMemFormat,
			  psPDumpBitmapIN->ui32Flags);

	return 0;
}

static int PDumpReadRegBW(u32 ui32BridgeID,
	       struct PVRSRV_BRIDGE_IN_PDUMP_READREG *psPDumpReadRegIN,
	       struct PVRSRV_BRIDGE_RETURN *psRetOUT,
	       struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_PDUMP_DUMPREADREG);
	PVR_UNREFERENCED_PARAMETER(psPerProc);

	psRetOUT->eError =
	    PDumpReadRegKM(&psPDumpReadRegIN->szFileName[0],
			   psPDumpReadRegIN->ui32FileOffset,
			   psPDumpReadRegIN->ui32Address,
			   psPDumpReadRegIN->ui32Size,
			   psPDumpReadRegIN->ui32Flags);

	return 0;
}

static int PDumpDriverInfoBW(u32 ui32BridgeID,
		  struct PVRSRV_BRIDGE_IN_PDUMP_DRIVERINFO *psPDumpDriverInfoIN,
		  struct PVRSRV_BRIDGE_RETURN *psRetOUT,
		  struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	u32 ui32PDumpFlags;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_PDUMP_DRIVERINFO);
	PVR_UNREFERENCED_PARAMETER(psPerProc);

	ui32PDumpFlags = 0;
	if (psPDumpDriverInfoIN->bContinuous)
		ui32PDumpFlags |= PDUMP_FLAGS_CONTINUOUS;
	psRetOUT->eError =
	    PDumpDriverInfoKM(&psPDumpDriverInfoIN->szString[0],
			      ui32PDumpFlags);

	return 0;
}

static int PDumpSyncDumpBW(u32 ui32BridgeID,
		struct PVRSRV_BRIDGE_IN_PDUMP_DUMPSYNC *psPDumpSyncDumpIN,
		struct PVRSRV_BRIDGE_RETURN *psRetOUT,
		struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *pvAltLinAddrKM = NULL;
	u32 ui32Bytes = psPDumpSyncDumpIN->ui32Bytes;
	void *pvSyncInfo;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_PDUMP_DUMPSYNC);

	psRetOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase, &pvSyncInfo,
			       psPDumpSyncDumpIN->psKernelSyncInfo,
			       PVRSRV_HANDLE_TYPE_SYNC_INFO);
	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	if (psPDumpSyncDumpIN->pvAltLinAddr) {
		if (OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
			       ui32Bytes, &pvAltLinAddrKM, 0) != PVRSRV_OK)
			return -EFAULT;

		if (CopyFromUserWrapper(psPerProc,
					ui32BridgeID,
					pvAltLinAddrKM,
					psPDumpSyncDumpIN->pvAltLinAddr,
					ui32Bytes) != PVRSRV_OK) {
			OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, ui32Bytes,
				  pvAltLinAddrKM, 0);
			return -EFAULT;
		}
	}

	psRetOUT->eError =
	    PDumpMemKM(pvAltLinAddrKM,
		       ((struct PVRSRV_KERNEL_SYNC_INFO *)pvSyncInfo)->
		       psSyncDataMemInfoKM, psPDumpSyncDumpIN->ui32Offset,
		       ui32Bytes, 0,
		       MAKEUNIQUETAG(
			       ((struct PVRSRV_KERNEL_SYNC_INFO *)pvSyncInfo)->
				   psSyncDataMemInfoKM));

	if (pvAltLinAddrKM)
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, ui32Bytes, pvAltLinAddrKM,
			  0);

	return 0;
}

static int PDumpSyncPolBW(u32 ui32BridgeID,
	       struct PVRSRV_BRIDGE_IN_PDUMP_SYNCPOL *psPDumpSyncPolIN,
	       struct PVRSRV_BRIDGE_RETURN *psRetOUT,
	       struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	u32 ui32Offset;
	void *pvSyncInfo;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_PDUMP_SYNCPOL);

	psRetOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase, &pvSyncInfo,
			       psPDumpSyncPolIN->psKernelSyncInfo,
			       PVRSRV_HANDLE_TYPE_SYNC_INFO);
	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	if (psPDumpSyncPolIN->bIsRead)
		ui32Offset = offsetof(struct PVRSRV_SYNC_DATA,
				      ui32ReadOpsComplete);
	else
		ui32Offset = offsetof(struct PVRSRV_SYNC_DATA,
				      ui32WriteOpsComplete);

	psRetOUT->eError =
	    PDumpMemPolKM(((struct PVRSRV_KERNEL_SYNC_INFO *)pvSyncInfo)->
			  psSyncDataMemInfoKM, ui32Offset,
			  psPDumpSyncPolIN->ui32Value,
			  psPDumpSyncPolIN->ui32Mask, PDUMP_POLL_OPERATOR_EQUAL,
			  IMG_FALSE, IMG_FALSE,
			  MAKEUNIQUETAG(((struct PVRSRV_KERNEL_SYNC_INFO *)
					  pvSyncInfo)->psSyncDataMemInfoKM));

	return 0;
}

static int PDumpPDRegBW(u32 ui32BridgeID,
	     struct PVRSRV_BRIDGE_IN_PDUMP_DUMPPDREG *psPDumpPDRegDumpIN,
	     struct PVRSRV_BRIDGE_RETURN *psRetOUT,
	     struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_PDUMP_PDREG);
	PVR_UNREFERENCED_PARAMETER(psPerProc);

	PDumpPDReg(psPDumpPDRegDumpIN->sHWReg.ui32RegAddr,
		   psPDumpPDRegDumpIN->sHWReg.ui32RegVal, PDUMP_PD_UNIQUETAG);

	psRetOUT->eError = PVRSRV_OK;
	return 0;
}

static int PDumpCycleCountRegReadBW(u32 ui32BridgeID,
		 struct PVRSRV_BRIDGE_IN_PDUMP_CYCLE_COUNT_REG_READi
					*psPDumpCycleCountRegReadIN,
		 struct PVRSRV_BRIDGE_RETURN *psRetOUT,
		 struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_PDUMP_CYCLE_COUNT_REG_READ);
	PVR_UNREFERENCED_PARAMETER(psPerProc);

	PDumpCycleCountRegRead(psPDumpCycleCountRegReadIN->ui32RegOffset,
			       psPDumpCycleCountRegReadIN->bLastFrame);

	psRetOUT->eError = PVRSRV_OK;

	return 0;
}

static int
PDumpPDDevPAddrBW(u32 ui32BridgeID,
	  struct PVRSRV_BRIDGE_IN_PDUMP_DUMPPDDEVPADDR *psPDumpPDDevPAddrIN,
	  struct PVRSRV_BRIDGE_RETURN *psRetOUT,
	  struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *pvMemInfo;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_PDUMP_DUMPPDDEVPADDR);

	psRetOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase, &pvMemInfo,
			       psPDumpPDDevPAddrIN->hKernelMemInfo,
			       PVRSRV_HANDLE_TYPE_MEM_INFO);
	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError =
	    PDumpPDDevPAddrKM((struct PVRSRV_KERNEL_MEM_INFO *)pvMemInfo,
			      psPDumpPDDevPAddrIN->ui32Offset,
			      psPDumpPDDevPAddrIN->sPDDevPAddr,
			      MAKEUNIQUETAG(pvMemInfo), PDUMP_PD_UNIQUETAG);
	return 0;
}

static int PDumpBufferArrayBW(u32 ui32BridgeID,
	   struct PVRSRV_BRIDGE_IN_PDUMP_BUFFER_ARRAY *psPDumpBufferArrayIN,
	   void *psBridgeOut, struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	u32 i;
	struct PVR3DIF4_KICKTA_DUMP_BUFFER *psKickTADumpBuffer;
	u32 ui32BufferArrayLength =
	    psPDumpBufferArrayIN->ui32BufferArrayLength;
	u32 ui32BufferArraySize =
	    ui32BufferArrayLength * sizeof(struct PVR3DIF4_KICKTA_DUMP_BUFFER);
	enum PVRSRV_ERROR eError = PVRSRV_ERROR_GENERIC;

	PVR_UNREFERENCED_PARAMETER(psBridgeOut);

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_PDUMP_BUFFER_ARRAY);

	if (OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
		       ui32BufferArraySize,
		       (void **) &psKickTADumpBuffer, 0) != PVRSRV_OK)
		return -ENOMEM;

	if (CopyFromUserWrapper(psPerProc,
				ui32BridgeID,
				psKickTADumpBuffer,
				psPDumpBufferArrayIN->psBufferArray,
				ui32BufferArraySize) != PVRSRV_OK) {
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, ui32BufferArraySize,
			  psKickTADumpBuffer, 0);
		return -EFAULT;
	}

	for (i = 0; i < ui32BufferArrayLength; i++) {
		void *pvMemInfo;

		eError = PVRSRVLookupHandle(psPerProc->psHandleBase,
					    &pvMemInfo,
					    psKickTADumpBuffer[i].
					    hKernelMemInfo,
					    PVRSRV_HANDLE_TYPE_MEM_INFO);

		if (eError != PVRSRV_OK) {
			PVR_DPF(PVR_DBG_ERROR,
				 "PVRSRV_BRIDGE_PDUMP_BUFFER_ARRAY: "
				 "PVRSRVLookupHandle failed (%d)", eError);
			break;
		}
		psKickTADumpBuffer[i].hKernelMemInfo = pvMemInfo;
	}

	if (eError == PVRSRV_OK)
		DumpBufferArray(psKickTADumpBuffer,
				ui32BufferArrayLength,
				psPDumpBufferArrayIN->bDumpPolls);

	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, ui32BufferArraySize,
		  psKickTADumpBuffer, 0);

	return 0;
}

static int PDump3DSignatureRegistersBW(u32 ui32BridgeID,
			    struct PVRSRV_BRIDGE_IN_PDUMP_3D_SIGNATURE_REGISTERS
					*psPDump3DSignatureRegistersIN,
			    void *psBridgeOut,
			    struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	u32 ui32RegisterArraySize =
	    psPDump3DSignatureRegistersIN->ui32NumRegisters *
	    sizeof(u32);
	u32 *pui32Registers = NULL;
	int ret = -EFAULT;

	PVR_UNREFERENCED_PARAMETER(psBridgeOut);

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_PDUMP_3D_SIGNATURE_REGISTERS);

	if (ui32RegisterArraySize == 0)
		goto ExitNoError;

	if (OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
		       ui32RegisterArraySize,
		       (void **) &pui32Registers, 0) != PVRSRV_OK) {
		PVR_DPF(PVR_DBG_ERROR,
			 "PDump3DSignatureRegistersBW: OSAllocMem failed");
		goto Exit;
	}

	if (CopyFromUserWrapper(psPerProc,
				ui32BridgeID,
				pui32Registers,
				psPDump3DSignatureRegistersIN->pui32Registers,
				ui32RegisterArraySize) != PVRSRV_OK) {
		PVR_DPF(PVR_DBG_ERROR, "PDump3DSignatureRegistersBW: "
					"CopyFromUserWrapper failed");
		goto Exit;
	}

	PDump3DSignatureRegisters(psPDump3DSignatureRegistersIN->
				  ui32DumpFrameNum,
				  psPDump3DSignatureRegistersIN->bLastFrame,
				  pui32Registers,
				  psPDump3DSignatureRegistersIN->
				  ui32NumRegisters);

ExitNoError:
	ret = 0;
Exit:
	if (pui32Registers != NULL)
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, ui32RegisterArraySize,
			  pui32Registers, 0);

	return ret;
}

static int PDumpCounterRegistersBW(u32 ui32BridgeID,
		struct PVRSRV_BRIDGE_IN_PDUMP_COUNTER_REGISTERS
			*psPDumpCounterRegistersIN,
		void *psBridgeOut,
		struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	u32 ui32RegisterArraySize =
	    psPDumpCounterRegistersIN->ui32NumRegisters * sizeof(u32);
	u32 *pui32Registers = NULL;
	int ret = -EFAULT;

	PVR_UNREFERENCED_PARAMETER(psBridgeOut);

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_PDUMP_COUNTER_REGISTERS);

	if (ui32RegisterArraySize == 0)
		goto ExitNoError;

	if (OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
		       ui32RegisterArraySize,
		       (void **) &pui32Registers, 0) != PVRSRV_OK) {
		PVR_DPF(PVR_DBG_ERROR,
			 "PDumpCounterRegistersBW: OSAllocMem failed");
		ret = -ENOMEM;
		goto Exit;
	}

	if (CopyFromUserWrapper(psPerProc,
				ui32BridgeID,
				pui32Registers,
				psPDumpCounterRegistersIN->pui32Registers,
				ui32RegisterArraySize) != PVRSRV_OK) {
		PVR_DPF(PVR_DBG_ERROR, "PDumpCounterRegistersBW: "
					"CopyFromUserWrapper failed");
		goto Exit;
	}

	PDumpCounterRegisters(psPDumpCounterRegistersIN->ui32DumpFrameNum,
			      psPDumpCounterRegistersIN->bLastFrame,
			      pui32Registers,
			      psPDumpCounterRegistersIN->ui32NumRegisters);

ExitNoError:
	ret = 0;
Exit:
	if (pui32Registers != NULL)
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, ui32RegisterArraySize,
			  pui32Registers, 0);

	return ret;
}

static int PDumpTASignatureRegistersBW(u32 ui32BridgeID,
		    struct PVRSRV_BRIDGE_IN_PDUMP_TA_SIGNATURE_REGISTERS
				*psPDumpTASignatureRegistersIN,
		    void *psBridgeOut,
		    struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	u32 ui32RegisterArraySize =
	    psPDumpTASignatureRegistersIN->ui32NumRegisters *
	    sizeof(u32);
	u32 *pui32Registers = NULL;
	int ret = -EFAULT;

	PVR_UNREFERENCED_PARAMETER(psBridgeOut);

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_PDUMP_TA_SIGNATURE_REGISTERS);

	if (ui32RegisterArraySize == 0)
		goto ExitNoError;

	if (OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
		       ui32RegisterArraySize,
		       (void **) &pui32Registers, 0) != PVRSRV_OK) {
		PVR_DPF(PVR_DBG_ERROR,
			 "PDumpTASignatureRegistersBW: OSAllocMem failed");
		ret = -ENOMEM;
		goto Exit;
	}

	if (CopyFromUserWrapper(psPerProc,
				ui32BridgeID,
				pui32Registers,
				psPDumpTASignatureRegistersIN->pui32Registers,
				ui32RegisterArraySize) != PVRSRV_OK) {
		PVR_DPF(PVR_DBG_ERROR, "PDumpTASignatureRegistersBW: "
					"CopyFromUserWrapper failed");
		goto Exit;
	}

	PDumpTASignatureRegisters(psPDumpTASignatureRegistersIN->
				  ui32DumpFrameNum,
				  psPDumpTASignatureRegistersIN->
				  ui32TAKickCount,
				  psPDumpTASignatureRegistersIN->bLastFrame,
				  pui32Registers,
				  psPDumpTASignatureRegistersIN->
				  ui32NumRegisters);

ExitNoError:
	ret = 0;
Exit:
	if (pui32Registers != NULL)
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, ui32RegisterArraySize,
			  pui32Registers, 0);

	return ret;
}
#endif

static int SGXGetClientInfoBW(u32 ui32BridgeID,
		   struct PVRSRV_BRIDGE_IN_GETCLIENTINFO *psGetClientInfoIN,
		   struct PVRSRV_BRIDGE_OUT_GETCLIENTINFO *psGetClientInfoOUT,
		   struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *hDevCookieInt;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SGX_GETCLIENTINFO);

	psGetClientInfoOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase, &hDevCookieInt,
			       psGetClientInfoIN->hDevCookie,
			       PVRSRV_HANDLE_TYPE_DEV_NODE);
	if (psGetClientInfoOUT->eError != PVRSRV_OK)
		return 0;

	psGetClientInfoOUT->eError =
	    SGXGetClientInfoKM(hDevCookieInt, &psGetClientInfoOUT->sClientInfo);
	return 0;
}

static int SGXReleaseClientInfoBW(u32 ui32BridgeID,
	       struct PVRSRV_BRIDGE_IN_RELEASECLIENTINFO *psReleaseClientInfoIN,
	       struct PVRSRV_BRIDGE_RETURN *psRetOUT,
	       struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	struct PVRSRV_SGXDEV_INFO *psDevInfo;
	void *hDevCookieInt;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_SGX_RELEASECLIENTINFO);

	psRetOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase, &hDevCookieInt,
			       psReleaseClientInfoIN->hDevCookie,
			       PVRSRV_HANDLE_TYPE_DEV_NODE);
	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psDevInfo = (struct PVRSRV_SGXDEV_INFO *)
			((struct PVRSRV_DEVICE_NODE *)hDevCookieInt)->pvDevice;

	PVR_ASSERT(psDevInfo->ui32ClientRefCount > 0);

	psDevInfo->ui32ClientRefCount--;

	psRetOUT->eError = PVRSRV_OK;

	return 0;
}

static int SGXGetInternalDevInfoBW(u32 ui32BridgeID,
	struct PVRSRV_BRIDGE_IN_GETINTERNALDEVINFO *psSGXGetInternalDevInfoIN,
	struct PVRSRV_BRIDGE_OUT_GETINTERNALDEVINFO *psSGXGetInternalDevInfoOUT,
	struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *hDevCookieInt;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_SGX_GETINTERNALDEVINFO);

	psSGXGetInternalDevInfoOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase, &hDevCookieInt,
			       psSGXGetInternalDevInfoIN->hDevCookie,
			       PVRSRV_HANDLE_TYPE_DEV_NODE);
	if (psSGXGetInternalDevInfoOUT->eError != PVRSRV_OK)
		return 0;

	psSGXGetInternalDevInfoOUT->eError =
	    SGXGetInternalDevInfoKM(hDevCookieInt,
				    &psSGXGetInternalDevInfoOUT->
				    sSGXInternalDevInfo);

	psSGXGetInternalDevInfoOUT->eError =
	    PVRSRVAllocHandle(psPerProc->psHandleBase,
			      &psSGXGetInternalDevInfoOUT->sSGXInternalDevInfo.
			      hCtlKernelMemInfoHandle,
			      psSGXGetInternalDevInfoOUT->sSGXInternalDevInfo.
			      hCtlKernelMemInfoHandle,
			      PVRSRV_HANDLE_TYPE_MEM_INFO,
			      PVRSRV_HANDLE_ALLOC_FLAG_SHARED);

	return 0;
}

static int SGXDoKickBW(u32 ui32BridgeID,
	    struct PVRSRV_BRIDGE_IN_DOKICK *psDoKickIN,
	    struct PVRSRV_BRIDGE_RETURN *psRetOUT,
	    struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *hDevCookieInt;
	u32 i;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SGX_DOKICK);

	psRetOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &hDevCookieInt,
			       psDoKickIN->hDevCookie,
			       PVRSRV_HANDLE_TYPE_DEV_NODE);

	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &psDoKickIN->sCCBKick.hCCBKernelMemInfo,
			       psDoKickIN->sCCBKick.hCCBKernelMemInfo,
			       PVRSRV_HANDLE_TYPE_MEM_INFO);

	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	if (psDoKickIN->sCCBKick.hTA3DSyncInfo != NULL) {
		psRetOUT->eError =
		    PVRSRVLookupHandle(psPerProc->psHandleBase,
				       &psDoKickIN->sCCBKick.hTA3DSyncInfo,
				       psDoKickIN->sCCBKick.hTA3DSyncInfo,
				       PVRSRV_HANDLE_TYPE_SYNC_INFO);

		if (psRetOUT->eError != PVRSRV_OK)
			return 0;
	}

	if (psDoKickIN->sCCBKick.hTASyncInfo != NULL) {
		psRetOUT->eError =
		    PVRSRVLookupHandle(psPerProc->psHandleBase,
				       &psDoKickIN->sCCBKick.hTASyncInfo,
				       psDoKickIN->sCCBKick.hTASyncInfo,
				       PVRSRV_HANDLE_TYPE_SYNC_INFO);

		if (psRetOUT->eError != PVRSRV_OK)
			return 0;
	}

	if (psDoKickIN->sCCBKick.h3DSyncInfo != NULL) {
		psRetOUT->eError =
		    PVRSRVLookupHandle(psPerProc->psHandleBase,
				       &psDoKickIN->sCCBKick.h3DSyncInfo,
				       psDoKickIN->sCCBKick.h3DSyncInfo,
				       PVRSRV_HANDLE_TYPE_SYNC_INFO);

		if (psRetOUT->eError != PVRSRV_OK)
			return 0;
	}

	if (psDoKickIN->sCCBKick.ui32NumSrcSyncs > SGX_MAX_SRC_SYNCS) {
		psRetOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;
		return 0;
	}
	for (i = 0; i < psDoKickIN->sCCBKick.ui32NumSrcSyncs; i++) {
		psRetOUT->eError =
		    PVRSRVLookupHandle(psPerProc->psHandleBase,
				       &psDoKickIN->sCCBKick.
				       ahSrcKernelSyncInfo[i],
				       psDoKickIN->sCCBKick.
				       ahSrcKernelSyncInfo[i],
				       PVRSRV_HANDLE_TYPE_SYNC_INFO);

		if (psRetOUT->eError != PVRSRV_OK)
			return 0;
	}

	if (psDoKickIN->sCCBKick.ui32NumTAStatusVals > SGX_MAX_TA_STATUS_VALS) {
		psRetOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;
		return 0;
	}
	for (i = 0; i < psDoKickIN->sCCBKick.ui32NumTAStatusVals; i++) {
		psRetOUT->eError =
		    PVRSRVLookupHandle(psPerProc->psHandleBase,
				       &psDoKickIN->sCCBKick.
				       ahTAStatusSyncInfo[i],
				       psDoKickIN->sCCBKick.
				       ahTAStatusSyncInfo[i],
				       PVRSRV_HANDLE_TYPE_SYNC_INFO);

		if (psRetOUT->eError != PVRSRV_OK)
			return 0;
	}

	if (psDoKickIN->sCCBKick.ui32Num3DStatusVals > SGX_MAX_3D_STATUS_VALS) {
		psRetOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;
		return 0;
	}
	for (i = 0; i < psDoKickIN->sCCBKick.ui32Num3DStatusVals; i++) {
		psRetOUT->eError =
		    PVRSRVLookupHandle(psPerProc->psHandleBase,
				       &psDoKickIN->sCCBKick.
				       ah3DStatusSyncInfo[i],
				       psDoKickIN->sCCBKick.
				       ah3DStatusSyncInfo[i],
				       PVRSRV_HANDLE_TYPE_SYNC_INFO);

		if (psRetOUT->eError != PVRSRV_OK)
			return 0;
	}

	if (psDoKickIN->sCCBKick.hRenderSurfSyncInfo != NULL) {
		psRetOUT->eError =
		    PVRSRVLookupHandle(psPerProc->psHandleBase,
				       &psDoKickIN->sCCBKick.
				       hRenderSurfSyncInfo,
				       psDoKickIN->sCCBKick.hRenderSurfSyncInfo,
				       PVRSRV_HANDLE_TYPE_SYNC_INFO);

		if (psRetOUT->eError != PVRSRV_OK)
			return 0;
	}

	psRetOUT->eError = SGXDoKickKM(hDevCookieInt, &psDoKickIN->sCCBKick);

	return 0;
}

static int SGXSubmitTransferBW(u32 ui32BridgeID,
		    struct PVRSRV_BRIDGE_IN_SUBMITTRANSFER *psSubmitTransferIN,
		    struct PVRSRV_BRIDGE_RETURN *psRetOUT,
		    struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *hDevCookieInt;
	struct PVRSRV_TRANSFER_SGX_KICK *psKick;
	u32 i;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_SGX_SUBMITTRANSFER);
	PVR_UNREFERENCED_PARAMETER(ui32BridgeID);

	psKick = &psSubmitTransferIN->sKick;

	psRetOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &hDevCookieInt,
			       psSubmitTransferIN->hDevCookie,
			       PVRSRV_HANDLE_TYPE_DEV_NODE);
	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &psKick->hCCBMemInfo,
			       psKick->hCCBMemInfo,
			       PVRSRV_HANDLE_TYPE_MEM_INFO);
	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	if (psKick->hTASyncInfo != NULL) {
		psRetOUT->eError =
		    PVRSRVLookupHandle(psPerProc->psHandleBase,
				       &psKick->hTASyncInfo,
				       psKick->hTASyncInfo,
				       PVRSRV_HANDLE_TYPE_SYNC_INFO);
		if (psRetOUT->eError != PVRSRV_OK)
			return 0;
	}

	if (psKick->h3DSyncInfo != NULL) {
		psRetOUT->eError =
		    PVRSRVLookupHandle(psPerProc->psHandleBase,
				       &psKick->h3DSyncInfo,
				       psKick->h3DSyncInfo,
				       PVRSRV_HANDLE_TYPE_SYNC_INFO);
		if (psRetOUT->eError != PVRSRV_OK)
			return 0;
	}

	if (psKick->ui32NumSrcSync > SGX_MAX_TRANSFER_SYNC_OPS) {
		psRetOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;
		return 0;
	}
	for (i = 0; i < psKick->ui32NumSrcSync; i++) {
		psRetOUT->eError =
		    PVRSRVLookupHandle(psPerProc->psHandleBase,
				       &psKick->ahSrcSyncInfo[i],
				       psKick->ahSrcSyncInfo[i],
				       PVRSRV_HANDLE_TYPE_SYNC_INFO);
		if (psRetOUT->eError != PVRSRV_OK)
			return 0;
	}

	if (psKick->ui32NumDstSync > SGX_MAX_TRANSFER_SYNC_OPS) {
		psRetOUT->eError = PVRSRV_ERROR_INVALID_PARAMS;
		return 0;
	}
	for (i = 0; i < psKick->ui32NumDstSync; i++) {
		psRetOUT->eError =
		    PVRSRVLookupHandle(psPerProc->psHandleBase,
				       &psKick->ahDstSyncInfo[i],
				       psKick->ahDstSyncInfo[i],
				       PVRSRV_HANDLE_TYPE_SYNC_INFO);
		if (psRetOUT->eError != PVRSRV_OK)
			return 0;
	}

	psRetOUT->eError = SGXSubmitTransferKM(hDevCookieInt, psKick);

	return 0;
}



static int SGXGetMiscInfoBW(u32 ui32BridgeID,
		 struct PVRSRV_BRIDGE_IN_SGXGETMISCINFO *psSGXGetMiscInfoIN,
		 struct PVRSRV_BRIDGE_RETURN *psRetOUT,
		 struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *hDevCookieInt;
	struct PVRSRV_SGXDEV_INFO *psDevInfo;
	struct SGX_MISC_INFO sMiscInfo;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SGX_GETMISCINFO);

	psRetOUT->eError = PVRSRVLookupHandle(psPerProc->psHandleBase,
					      &hDevCookieInt,
					      psSGXGetMiscInfoIN->hDevCookie,
					      PVRSRV_HANDLE_TYPE_DEV_NODE);

	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psDevInfo = (struct PVRSRV_SGXDEV_INFO *)
		((struct PVRSRV_DEVICE_NODE *)hDevCookieInt)->pvDevice;

	psRetOUT->eError = CopyFromUserWrapper(psPerProc, ui32BridgeID,
					       &sMiscInfo,
					       psSGXGetMiscInfoIN->psMiscInfo,
					       sizeof(struct SGX_MISC_INFO));
	if (psRetOUT->eError != PVRSRV_OK)
		return -EFAULT;
	if (sMiscInfo.eRequest == SGX_MISC_INFO_REQUEST_HWPERF_RETRIEVE_CB) {
		void *pAllocated;
		void *hAllocatedHandle;
		void __user *psTmpUserData;
		int allocatedSize;

		allocatedSize = sMiscInfo.uData.sRetrieveCB.ui32ArraySize *
				sizeof(struct PVRSRV_SGX_HWPERF_CBDATA);

		ASSIGN_AND_EXIT_ON_ERROR(psRetOUT->eError,
					 OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
						    allocatedSize,
						    &pAllocated,
						    &hAllocatedHandle));

		psTmpUserData = (void __force __user *)
				sMiscInfo.uData.sRetrieveCB.psHWPerfData;
		sMiscInfo.uData.sRetrieveCB.psHWPerfData = pAllocated;

		psRetOUT->eError = SGXGetMiscInfoKM(psDevInfo, &sMiscInfo);
		if (psRetOUT->eError != PVRSRV_OK) {
			OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
				  allocatedSize, pAllocated, hAllocatedHandle);
			return -EFAULT;
		}

		psRetOUT->eError = CopyToUserWrapper(
				psPerProc, ui32BridgeID,
				psTmpUserData,
				sMiscInfo.uData.sRetrieveCB.psHWPerfData,
				allocatedSize);

		sMiscInfo.uData.sRetrieveCB.psHWPerfData =
						(void __force *)psTmpUserData;

		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
			  allocatedSize, pAllocated, hAllocatedHandle);

		if (psRetOUT->eError != PVRSRV_OK)
			return -EFAULT;
	} else {
		psRetOUT->eError = SGXGetMiscInfoKM(psDevInfo, &sMiscInfo);
		if (psRetOUT->eError != PVRSRV_OK)
			return -EFAULT;
	}

	psRetOUT->eError = CopyToUserWrapper(psPerProc,
					     ui32BridgeID,
					     psSGXGetMiscInfoIN->psMiscInfo,
					     &sMiscInfo,
					     sizeof(struct SGX_MISC_INFO));
	if (psRetOUT->eError != PVRSRV_OK)
		return -EFAULT;
	return 0;
}

static int SGXReadDiffCountersBW(u32 ui32BridgeID,
      struct PVRSRV_BRIDGE_IN_SGX_READ_DIFF_COUNTERS *psSGXReadDiffCountersIN,
      struct PVRSRV_BRIDGE_OUT_SGX_READ_DIFF_COUNTERS *psSGXReadDiffCountersOUT,
      struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *hDevCookieInt;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_SGX_READ_DIFF_COUNTERS);

	psSGXReadDiffCountersOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase, &hDevCookieInt,
			       psSGXReadDiffCountersIN->hDevCookie,
			       PVRSRV_HANDLE_TYPE_DEV_NODE);

	if (psSGXReadDiffCountersOUT->eError != PVRSRV_OK)
		return 0;

	psSGXReadDiffCountersOUT->eError = SGXReadDiffCountersKM(hDevCookieInt,
			psSGXReadDiffCountersIN->ui32Reg,
			&psSGXReadDiffCountersOUT->ui32Old,
			psSGXReadDiffCountersIN->bNew,
			psSGXReadDiffCountersIN->ui32New,
			psSGXReadDiffCountersIN->ui32NewReset,
			psSGXReadDiffCountersIN->ui32CountersReg,
			&psSGXReadDiffCountersOUT->ui32Time,
			&psSGXReadDiffCountersOUT->bActive,
			&psSGXReadDiffCountersOUT->sDiffs);

	return 0;
}

static int PVRSRVInitSrvConnectBW(u32 ui32BridgeID,
		       void *psBridgeIn,
		       struct PVRSRV_BRIDGE_RETURN *psRetOUT,
		       struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVR_UNREFERENCED_PARAMETER(psBridgeIn);

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_INITSRV_CONNECT);
	PVR_UNREFERENCED_PARAMETER(psBridgeIn);

	if (!OSProcHasPrivSrvInit()
	    || PVRSRVGetInitServerState(PVRSRV_INIT_SERVER_RUNNING)
	    || PVRSRVGetInitServerState(PVRSRV_INIT_SERVER_RAN)) {
		psRetOUT->eError = PVRSRV_ERROR_GENERIC;
		return 0;
	}
	PVRSRVSetInitServerState(PVRSRV_INIT_SERVER_RUNNING, IMG_TRUE);
	psPerProc->bInitProcess = IMG_TRUE;

	psRetOUT->eError = PVRSRV_OK;

	return 0;
}

static int
PVRSRVInitSrvDisconnectBW(u32 ui32BridgeID,
	 struct PVRSRV_BRIDGE_IN_INITSRV_DISCONNECT *psInitSrvDisconnectIN,
	 struct PVRSRV_BRIDGE_RETURN *psRetOUT,
	 struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_INITSRV_DISCONNECT);

	if (!psPerProc->bInitProcess) {
		psRetOUT->eError = PVRSRV_ERROR_GENERIC;
		return 0;
	}

	psPerProc->bInitProcess = IMG_FALSE;

	PVRSRVSetInitServerState(PVRSRV_INIT_SERVER_RUNNING, IMG_FALSE);
	PVRSRVSetInitServerState(PVRSRV_INIT_SERVER_RAN, IMG_TRUE);

	psRetOUT->eError =
	    PVRSRVFinaliseSystem(psInitSrvDisconnectIN->bInitSuccesful);

	PVRSRVSetInitServerState(PVRSRV_INIT_SERVER_SUCCESSFUL,
				 (IMG_BOOL) (((psRetOUT->eError == PVRSRV_OK)
					      && (psInitSrvDisconnectIN->
						  bInitSuccesful))));

	return 0;
}

static int
PVRSRVEventObjectWaitBW(u32 ui32BridgeID,
		struct PVRSRV_BRIDGE_IN_EVENT_OBJECT_WAIT *psEventObjectWaitIN,
		struct PVRSRV_BRIDGE_RETURN *psRetOUT,
		struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *hOSEventKM;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_EVENT_OBJECT_WAIT);

	psRetOUT->eError = PVRSRVLookupHandle(psPerProc->psHandleBase,
				&hOSEventKM,
				psEventObjectWaitIN->hOSEventKM,
				PVRSRV_HANDLE_TYPE_EVENT_OBJECT_CONNECT);

	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError = OSEventObjectWait(hOSEventKM);

	return 0;
}

static int
PVRSRVEventObjectOpenBW(u32 ui32BridgeID,
	struct PVRSRV_BRIDGE_IN_EVENT_OBJECT_OPEN *psEventObjectOpenIN,
	struct PVRSRV_BRIDGE_OUT_EVENT_OBJECT_OPEN *psEventObjectOpenOUT,
	struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_EVENT_OBJECT_OPEN);

	NEW_HANDLE_BATCH_OR_ERROR(psEventObjectOpenOUT->eError, psPerProc, 1);

	psEventObjectOpenOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &psEventObjectOpenIN->sEventObject.hOSEventKM,
			       psEventObjectOpenIN->sEventObject.hOSEventKM,
			       PVRSRV_HANDLE_TYPE_SHARED_EVENT_OBJECT);

	if (psEventObjectOpenOUT->eError != PVRSRV_OK)
		return 0;

	psEventObjectOpenOUT->eError =
	    OSEventObjectOpen(&psEventObjectOpenIN->sEventObject,
			      &psEventObjectOpenOUT->hOSEvent);

	if (psEventObjectOpenOUT->eError != PVRSRV_OK)
		return 0;

	PVRSRVAllocHandleNR(psPerProc->psHandleBase,
			    &psEventObjectOpenOUT->hOSEvent,
			    psEventObjectOpenOUT->hOSEvent,
			    PVRSRV_HANDLE_TYPE_EVENT_OBJECT_CONNECT,
			    PVRSRV_HANDLE_ALLOC_FLAG_NONE);

	COMMIT_HANDLE_BATCH_OR_ERROR(psEventObjectOpenOUT->eError, psPerProc);

	return 0;
}

static int
PVRSRVEventObjectCloseBW(u32 ui32BridgeID,
	 struct PVRSRV_BRIDGE_IN_EVENT_OBJECT_CLOSE *psEventObjectCloseIN,
	 struct PVRSRV_BRIDGE_RETURN *psRetOUT,
	 struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *hOSEventKM;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_EVENT_OBJECT_CLOSE);

	psRetOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &psEventObjectCloseIN->sEventObject.hOSEventKM,
			       psEventObjectCloseIN->sEventObject.hOSEventKM,
			       PVRSRV_HANDLE_TYPE_SHARED_EVENT_OBJECT);
	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError = PVRSRVLookupAndReleaseHandle(psPerProc->psHandleBase,
				&hOSEventKM,
				psEventObjectCloseIN->hOSEventKM,
				PVRSRV_HANDLE_TYPE_EVENT_OBJECT_CONNECT);

	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError =
	    OSEventObjectClose(&psEventObjectCloseIN->sEventObject, hOSEventKM);

	return 0;
}

static int SGXDevInitPart2BW(u32 ui32BridgeID,
		  struct PVRSRV_BRIDGE_IN_SGXDEVINITPART2 *psSGXDevInitPart2IN,
		  struct PVRSRV_BRIDGE_RETURN *psRetOUT,
		  struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *hDevCookieInt;
	enum PVRSRV_ERROR eError;
	IMG_BOOL bDissociateFailed = IMG_FALSE;
	IMG_BOOL bLookupFailed = IMG_FALSE;
	IMG_BOOL bReleaseFailed = IMG_FALSE;
	void *hDummy;
	u32 i;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_SGX_DEVINITPART2);

	if (!psPerProc->bInitProcess) {
		psRetOUT->eError = PVRSRV_ERROR_GENERIC;
		return 0;
	}

	psRetOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &hDevCookieInt,
			       psSGXDevInitPart2IN->hDevCookie,
			       PVRSRV_HANDLE_TYPE_DEV_NODE);
	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	eError = PVRSRVLookupHandle(psPerProc->psHandleBase,
				    &hDummy,
				    psSGXDevInitPart2IN->sInitInfo.
				    hKernelCCBMemInfo,
				    PVRSRV_HANDLE_TYPE_MEM_INFO);
	bLookupFailed |= (IMG_BOOL) (eError != PVRSRV_OK);

	eError = PVRSRVLookupHandle(psPerProc->psHandleBase,
				    &hDummy,
				    psSGXDevInitPart2IN->sInitInfo.
				    hKernelCCBCtlMemInfo,
				    PVRSRV_HANDLE_TYPE_MEM_INFO);
	bLookupFailed |= (IMG_BOOL) (eError != PVRSRV_OK);

	eError = PVRSRVLookupHandle(psPerProc->psHandleBase,
				    &hDummy,
				    psSGXDevInitPart2IN->sInitInfo.
				    hKernelCCBEventKickerMemInfo,
				    PVRSRV_HANDLE_TYPE_MEM_INFO);
	bLookupFailed |= (IMG_BOOL) (eError != PVRSRV_OK);

	eError = PVRSRVLookupHandle(psPerProc->psHandleBase,
				    &hDummy,
				    psSGXDevInitPart2IN->sInitInfo.
				    hKernelSGXHostCtlMemInfo,
				    PVRSRV_HANDLE_TYPE_MEM_INFO);
	bLookupFailed |= (IMG_BOOL) (eError != PVRSRV_OK);


	eError = PVRSRVLookupHandle(psPerProc->psHandleBase,
				    &hDummy,
				    psSGXDevInitPart2IN->sInitInfo.
				    hKernelHWPerfCBMemInfo,
				    PVRSRV_HANDLE_TYPE_MEM_INFO);
	bLookupFailed |= (IMG_BOOL) (eError != PVRSRV_OK);

	for (i = 0; i < SGX_MAX_INIT_MEM_HANDLES; i++) {
		void *hHandle =
		    psSGXDevInitPart2IN->sInitInfo.asInitMemHandles[i];

		if (hHandle == NULL)
			continue;

		eError = PVRSRVLookupHandle(psPerProc->psHandleBase,
					    &hDummy,
					    hHandle,
					    PVRSRV_HANDLE_TYPE_MEM_INFO);
		bLookupFailed |= (IMG_BOOL) (eError != PVRSRV_OK);
	}

	if (bLookupFailed) {
		PVR_DPF(PVR_DBG_ERROR,
			 "DevInitSGXPart2BW: A handle lookup failed");
		psRetOUT->eError = PVRSRV_ERROR_GENERIC;
		return 0;
	}

	eError = PVRSRVLookupAndReleaseHandle(psPerProc->psHandleBase,
					      &psSGXDevInitPart2IN->sInitInfo.
					      hKernelCCBMemInfo,
					      psSGXDevInitPart2IN->sInitInfo.
					      hKernelCCBMemInfo,
					      PVRSRV_HANDLE_TYPE_MEM_INFO);
	bReleaseFailed |= (IMG_BOOL) (eError != PVRSRV_OK);

	eError = PVRSRVLookupAndReleaseHandle(psPerProc->psHandleBase,
					      &psSGXDevInitPart2IN->sInitInfo.
					      hKernelCCBCtlMemInfo,
					      psSGXDevInitPart2IN->sInitInfo.
					      hKernelCCBCtlMemInfo,
					      PVRSRV_HANDLE_TYPE_MEM_INFO);
	bReleaseFailed |= (IMG_BOOL) (eError != PVRSRV_OK);

	eError = PVRSRVLookupAndReleaseHandle(psPerProc->psHandleBase,
					      &psSGXDevInitPart2IN->sInitInfo.
					      hKernelCCBEventKickerMemInfo,
					      psSGXDevInitPart2IN->sInitInfo.
					      hKernelCCBEventKickerMemInfo,
					      PVRSRV_HANDLE_TYPE_MEM_INFO);
	bReleaseFailed |= (IMG_BOOL) (eError != PVRSRV_OK);

	eError = PVRSRVLookupAndReleaseHandle(psPerProc->psHandleBase,
					      &psSGXDevInitPart2IN->sInitInfo.
					      hKernelSGXHostCtlMemInfo,
					      psSGXDevInitPart2IN->sInitInfo.
					      hKernelSGXHostCtlMemInfo,
					      PVRSRV_HANDLE_TYPE_MEM_INFO);
	bReleaseFailed |= (IMG_BOOL) (eError != PVRSRV_OK);


	eError = PVRSRVLookupAndReleaseHandle(psPerProc->psHandleBase,
					      &psSGXDevInitPart2IN->sInitInfo.
					      hKernelHWPerfCBMemInfo,
					      psSGXDevInitPart2IN->sInitInfo.
					      hKernelHWPerfCBMemInfo,
					      PVRSRV_HANDLE_TYPE_MEM_INFO);
	bReleaseFailed |= (IMG_BOOL) (eError != PVRSRV_OK);

	for (i = 0; i < SGX_MAX_INIT_MEM_HANDLES; i++) {
		void **phHandle =
		    &psSGXDevInitPart2IN->sInitInfo.asInitMemHandles[i];

		if (*phHandle == NULL)
			continue;

		eError = PVRSRVLookupAndReleaseHandle(psPerProc->psHandleBase,
						   phHandle,
						   *phHandle,
						   PVRSRV_HANDLE_TYPE_MEM_INFO);
		bReleaseFailed |= (IMG_BOOL) (eError != PVRSRV_OK);
	}

	if (bReleaseFailed) {
		PVR_DPF(PVR_DBG_ERROR,
			 "DevInitSGXPart2BW: A handle release failed");
		psRetOUT->eError = PVRSRV_ERROR_GENERIC;

		PVR_DBG_BREAK;
		return 0;
	}

	eError =
	    PVRSRVDissociateDeviceMemKM(hDevCookieInt,
					psSGXDevInitPart2IN->sInitInfo.
					hKernelCCBMemInfo);
	bDissociateFailed |= (IMG_BOOL) (eError != PVRSRV_OK);

	eError =
	    PVRSRVDissociateDeviceMemKM(hDevCookieInt,
					psSGXDevInitPart2IN->sInitInfo.
					hKernelCCBCtlMemInfo);
	bDissociateFailed |= (IMG_BOOL) (eError != PVRSRV_OK);

	eError =
	    PVRSRVDissociateDeviceMemKM(hDevCookieInt,
					psSGXDevInitPart2IN->sInitInfo.
					hKernelCCBEventKickerMemInfo);
	bDissociateFailed |= (IMG_BOOL) (eError != PVRSRV_OK);

	eError =
	    PVRSRVDissociateDeviceMemKM(hDevCookieInt,
					psSGXDevInitPart2IN->sInitInfo.
					hKernelSGXHostCtlMemInfo);
	bDissociateFailed |= (IMG_BOOL) (eError != PVRSRV_OK);


	eError =
	    PVRSRVDissociateDeviceMemKM(hDevCookieInt,
					psSGXDevInitPart2IN->sInitInfo.
					hKernelHWPerfCBMemInfo);
	bDissociateFailed |= (IMG_BOOL) (eError != PVRSRV_OK);

	for (i = 0; i < SGX_MAX_INIT_MEM_HANDLES; i++) {
		void *hHandle =
		    psSGXDevInitPart2IN->sInitInfo.asInitMemHandles[i];

		if (hHandle == NULL)
			continue;

		eError = PVRSRVDissociateDeviceMemKM(hDevCookieInt, hHandle);
		bDissociateFailed |= (IMG_BOOL) (eError != PVRSRV_OK);
	}

	if (bDissociateFailed) {
		PVRSRVFreeDeviceMemKM(hDevCookieInt,
				      psSGXDevInitPart2IN->sInitInfo.
				      hKernelCCBMemInfo);
		PVRSRVFreeDeviceMemKM(hDevCookieInt,
				      psSGXDevInitPart2IN->sInitInfo.
				      hKernelCCBCtlMemInfo);
		PVRSRVFreeDeviceMemKM(hDevCookieInt,
				      psSGXDevInitPart2IN->sInitInfo.
				      hKernelSGXHostCtlMemInfo);

		for (i = 0; i < SGX_MAX_INIT_MEM_HANDLES; i++) {
			void *hHandle =
			    psSGXDevInitPart2IN->sInitInfo.asInitMemHandles[i];

			if (hHandle == NULL)
				continue;

			PVRSRVFreeDeviceMemKM(hDevCookieInt,
				 (struct PVRSRV_KERNEL_MEM_INFO *)hHandle);

		}

		PVR_DPF(PVR_DBG_ERROR,
			 "DevInitSGXPart2BW: A dissociate failed");

		psRetOUT->eError = PVRSRV_ERROR_GENERIC;

		PVR_DBG_BREAK;
		return 0;
	}

	psRetOUT->eError =
	    DevInitSGXPart2KM(psPerProc,
			      hDevCookieInt, &psSGXDevInitPart2IN->sInitInfo);

	return 0;
}

static int SGXRegisterHWRenderContextBW(u32 ui32BridgeID,
		     struct PVRSRV_BRIDGE_IN_SGX_REGISTER_HW_RENDER_CONTEXT
				*psSGXRegHWRenderContextIN,
		     struct PVRSRV_BRIDGE_OUT_SGX_REGISTER_HW_RENDER_CONTEXT
				 *psSGXRegHWRenderContextOUT,
		     struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *hDevCookieInt;
	void *hHWRenderContextInt;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_SGX_REGISTER_HW_RENDER_CONTEXT);

	NEW_HANDLE_BATCH_OR_ERROR(psSGXRegHWRenderContextOUT->eError, psPerProc,
				  1);

	psSGXRegHWRenderContextOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &hDevCookieInt,
			       psSGXRegHWRenderContextIN->hDevCookie,
			       PVRSRV_HANDLE_TYPE_DEV_NODE);
	if (psSGXRegHWRenderContextOUT->eError != PVRSRV_OK)
		return 0;

	hHWRenderContextInt =
	    SGXRegisterHWRenderContextKM(hDevCookieInt,
					 &psSGXRegHWRenderContextIN->
					 sHWRenderContextDevVAddr, psPerProc);

	if (hHWRenderContextInt == NULL) {
		psSGXRegHWRenderContextOUT->eError = PVRSRV_ERROR_GENERIC;
		return 0;
	}

	PVRSRVAllocHandleNR(psPerProc->psHandleBase,
			    &psSGXRegHWRenderContextOUT->hHWRenderContext,
			    hHWRenderContextInt,
			    PVRSRV_HANDLE_TYPE_SGX_HW_RENDER_CONTEXT,
			    PVRSRV_HANDLE_ALLOC_FLAG_NONE);

	COMMIT_HANDLE_BATCH_OR_ERROR(psSGXRegHWRenderContextOUT->eError,
				     psPerProc);

	return 0;
}

static int SGXUnregisterHWRenderContextBW(u32 ui32BridgeID,
		     struct PVRSRV_BRIDGE_IN_SGX_UNREGISTER_HW_RENDER_CONTEXT *
			psSGXUnregHWRenderContextIN,
		     struct PVRSRV_BRIDGE_RETURN *psRetOUT,
		     struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *hHWRenderContextInt;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
			 PVRSRV_BRIDGE_SGX_UNREGISTER_HW_RENDER_CONTEXT);

	psRetOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &hHWRenderContextInt,
			       psSGXUnregHWRenderContextIN->hHWRenderContext,
			       PVRSRV_HANDLE_TYPE_SGX_HW_RENDER_CONTEXT);
	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError = SGXUnregisterHWRenderContextKM(hHWRenderContextInt);
	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError =
	    PVRSRVReleaseHandle(psPerProc->psHandleBase,
				psSGXUnregHWRenderContextIN->hHWRenderContext,
				PVRSRV_HANDLE_TYPE_SGX_HW_RENDER_CONTEXT);

	return 0;
}

static int SGXRegisterHWTransferContextBW(u32 ui32BridgeID,
	       struct PVRSRV_BRIDGE_IN_SGX_REGISTER_HW_TRANSFER_CONTEXT
		       *psSGXRegHWTransferContextIN,
	       struct PVRSRV_BRIDGE_OUT_SGX_REGISTER_HW_TRANSFER_CONTEXT
		       *psSGXRegHWTransferContextOUT,
	       struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *hDevCookieInt;
	void *hHWTransferContextInt;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
			 PVRSRV_BRIDGE_SGX_REGISTER_HW_TRANSFER_CONTEXT);

	NEW_HANDLE_BATCH_OR_ERROR(psSGXRegHWTransferContextOUT->eError,
				  psPerProc, 1);

	psSGXRegHWTransferContextOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &hDevCookieInt,
			       psSGXRegHWTransferContextIN->hDevCookie,
			       PVRSRV_HANDLE_TYPE_DEV_NODE);
	if (psSGXRegHWTransferContextOUT->eError != PVRSRV_OK)
		return 0;

	hHWTransferContextInt =
	    SGXRegisterHWTransferContextKM(hDevCookieInt,
					   &psSGXRegHWTransferContextIN->
					   sHWTransferContextDevVAddr,
					   psPerProc);

	if (hHWTransferContextInt == NULL) {
		psSGXRegHWTransferContextOUT->eError = PVRSRV_ERROR_GENERIC;
		return 0;
	}

	PVRSRVAllocHandleNR(psPerProc->psHandleBase,
			    &psSGXRegHWTransferContextOUT->hHWTransferContext,
			    hHWTransferContextInt,
			    PVRSRV_HANDLE_TYPE_SGX_HW_TRANSFER_CONTEXT,
			    PVRSRV_HANDLE_ALLOC_FLAG_NONE);

	COMMIT_HANDLE_BATCH_OR_ERROR(psSGXRegHWTransferContextOUT->eError,
				     psPerProc);

	return 0;
}

static int SGXUnregisterHWTransferContextBW(u32 ui32BridgeID,
		 struct PVRSRV_BRIDGE_IN_SGX_UNREGISTER_HW_TRANSFER_CONTEXT
				*psSGXUnregHWTransferContextIN,
		 struct PVRSRV_BRIDGE_RETURN *psRetOUT,
		 struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *hHWTransferContextInt;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
			 PVRSRV_BRIDGE_SGX_UNREGISTER_HW_TRANSFER_CONTEXT);

	psRetOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &hHWTransferContextInt,
			       psSGXUnregHWTransferContextIN->
			       hHWTransferContext,
			       PVRSRV_HANDLE_TYPE_SGX_HW_TRANSFER_CONTEXT);
	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError =
	    SGXUnregisterHWTransferContextKM(hHWTransferContextInt);
	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError =
	    PVRSRVReleaseHandle(psPerProc->psHandleBase,
				psSGXUnregHWTransferContextIN->
				hHWTransferContext,
				PVRSRV_HANDLE_TYPE_SGX_HW_TRANSFER_CONTEXT);

	return 0;
}


static int SGXFlushHWRenderTargetBW(u32 ui32BridgeID,
			 struct PVRSRV_BRIDGE_IN_SGX_FLUSH_HW_RENDER_TARGET
					*psSGXFlushHWRenderTargetIN,
			 struct PVRSRV_BRIDGE_RETURN *psRetOUT,
			 struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *hDevCookieInt;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_SGX_FLUSH_HW_RENDER_TARGET);

	psRetOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &hDevCookieInt,
			       psSGXFlushHWRenderTargetIN->hDevCookie,
			       PVRSRV_HANDLE_TYPE_DEV_NODE);
	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	SGXFlushHWRenderTargetKM(hDevCookieInt,
				 psSGXFlushHWRenderTargetIN->
				 sHWRTDataSetDevVAddr);

	return 0;
}

static int SGX2DQueryBlitsCompleteBW(u32 ui32BridgeID,
	struct PVRSRV_BRIDGE_IN_2DQUERYBLTSCOMPLETE *ps2DQueryBltsCompleteIN,
	struct PVRSRV_BRIDGE_RETURN *psRetOUT,
	struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *hDevCookieInt;
	void *pvSyncInfo;
	struct PVRSRV_SGXDEV_INFO *psDevInfo;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_SGX_2DQUERYBLTSCOMPLETE);

	psRetOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase, &hDevCookieInt,
			       ps2DQueryBltsCompleteIN->hDevCookie,
			       PVRSRV_HANDLE_TYPE_DEV_NODE);
	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase, &pvSyncInfo,
			       ps2DQueryBltsCompleteIN->hKernSyncInfo,
			       PVRSRV_HANDLE_TYPE_SYNC_INFO);
	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psDevInfo = (struct PVRSRV_SGXDEV_INFO *)
		((struct PVRSRV_DEVICE_NODE *)hDevCookieInt)->pvDevice;

	psRetOUT->eError = SGX2DQueryBlitsCompleteKM(psDevInfo,
			      (struct PVRSRV_KERNEL_SYNC_INFO *)pvSyncInfo,
			      ps2DQueryBltsCompleteIN->bWaitForComplete);

	return 0;
}

static int SGXFindSharedPBDescBW(u32 ui32BridgeID,
	      struct PVRSRV_BRIDGE_IN_SGXFINDSHAREDPBDESC
			*psSGXFindSharedPBDescIN,
	      struct PVRSRV_BRIDGE_OUT_SGXFINDSHAREDPBDESC
			*psSGXFindSharedPBDescOUT,
	      struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *hDevCookieInt;
	struct PVRSRV_KERNEL_MEM_INFO *psSharedPBDescKernelMemInfo;
	struct PVRSRV_KERNEL_MEM_INFO *psHWPBDescKernelMemInfo;
	struct PVRSRV_KERNEL_MEM_INFO *psBlockKernelMemInfo;
	struct PVRSRV_KERNEL_MEM_INFO **ppsSharedPBDescSubKernelMemInfos = NULL;
	u32 ui32SharedPBDescSubKernelMemInfosCount = 0;
	u32 i;
	void *hSharedPBDesc = NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_SGX_FINDSHAREDPBDESC);

	NEW_HANDLE_BATCH_OR_ERROR(psSGXFindSharedPBDescOUT->eError, psPerProc,
				  PVRSRV_BRIDGE_SGX_SHAREDPBDESC_MAX_SUBMEMINFOS
				  + 4);

	psSGXFindSharedPBDescOUT->hSharedPBDesc = NULL;

	psSGXFindSharedPBDescOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &hDevCookieInt,
			       psSGXFindSharedPBDescIN->hDevCookie,
			       PVRSRV_HANDLE_TYPE_DEV_NODE);
	if (psSGXFindSharedPBDescOUT->eError != PVRSRV_OK)
		goto PVRSRV_BRIDGE_SGX_FINDSHAREDPBDESC_EXIT;

	psSGXFindSharedPBDescOUT->eError =
	    SGXFindSharedPBDescKM(psPerProc, hDevCookieInt,
				  psSGXFindSharedPBDescIN->bLockOnFailure,
				  psSGXFindSharedPBDescIN->ui32TotalPBSize,
				  &hSharedPBDesc,
				  &psSharedPBDescKernelMemInfo,
				  &psHWPBDescKernelMemInfo,
				  &psBlockKernelMemInfo,
				  &ppsSharedPBDescSubKernelMemInfos,
				  &ui32SharedPBDescSubKernelMemInfosCount);
	if (psSGXFindSharedPBDescOUT->eError != PVRSRV_OK)
		goto PVRSRV_BRIDGE_SGX_FINDSHAREDPBDESC_EXIT;

	PVR_ASSERT(ui32SharedPBDescSubKernelMemInfosCount
		   <= PVRSRV_BRIDGE_SGX_SHAREDPBDESC_MAX_SUBMEMINFOS);

	psSGXFindSharedPBDescOUT->ui32SharedPBDescSubKernelMemInfoHandlesCount =
	    ui32SharedPBDescSubKernelMemInfosCount;

	if (hSharedPBDesc == NULL) {
		psSGXFindSharedPBDescOUT->hSharedPBDescKernelMemInfoHandle =
									NULL;

		goto PVRSRV_BRIDGE_SGX_FINDSHAREDPBDESC_EXIT;
	}

	PVRSRVAllocHandleNR(psPerProc->psHandleBase,
			    &psSGXFindSharedPBDescOUT->hSharedPBDesc,
			    hSharedPBDesc,
			    PVRSRV_HANDLE_TYPE_SHARED_PB_DESC,
			    PVRSRV_HANDLE_ALLOC_FLAG_NONE);

	PVRSRVAllocSubHandleNR(psPerProc->psHandleBase,
			       &psSGXFindSharedPBDescOUT->
			       hSharedPBDescKernelMemInfoHandle,
			       psSharedPBDescKernelMemInfo,
			       PVRSRV_HANDLE_TYPE_MEM_INFO_REF,
			       PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
			       psSGXFindSharedPBDescOUT->hSharedPBDesc);

	PVRSRVAllocSubHandleNR(psPerProc->psHandleBase,
			       &psSGXFindSharedPBDescOUT->
			       hHWPBDescKernelMemInfoHandle,
			       psHWPBDescKernelMemInfo,
			       PVRSRV_HANDLE_TYPE_MEM_INFO_REF,
			       PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
			       psSGXFindSharedPBDescOUT->hSharedPBDesc);

	PVRSRVAllocSubHandleNR(psPerProc->psHandleBase,
			       &psSGXFindSharedPBDescOUT->
			       hBlockKernelMemInfoHandle,
			       psBlockKernelMemInfo,
			       PVRSRV_HANDLE_TYPE_MEM_INFO_REF,
			       PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
			       psSGXFindSharedPBDescOUT->hSharedPBDesc);

	for (i = 0; i < ui32SharedPBDescSubKernelMemInfosCount; i++) {
		struct PVRSRV_BRIDGE_OUT_SGXFINDSHAREDPBDESC *
			psSGXFindSharedPBDescOut = psSGXFindSharedPBDescOUT;

		PVRSRVAllocSubHandleNR(psPerProc->psHandleBase,
				       &psSGXFindSharedPBDescOut->
				       ahSharedPBDescSubKernelMemInfoHandles[i],
				       ppsSharedPBDescSubKernelMemInfos[i],
				       PVRSRV_HANDLE_TYPE_MEM_INFO_REF,
				       PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
				       psSGXFindSharedPBDescOUT->
				       hSharedPBDescKernelMemInfoHandle);
	}

PVRSRV_BRIDGE_SGX_FINDSHAREDPBDESC_EXIT:
	if (ppsSharedPBDescSubKernelMemInfos != NULL)
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
			  sizeof(struct PVRSRV_KERNEL_MEM_INFO *) *
					ui32SharedPBDescSubKernelMemInfosCount,
			  ppsSharedPBDescSubKernelMemInfos, NULL);

	if (psSGXFindSharedPBDescOUT->eError != PVRSRV_OK) {
		if (hSharedPBDesc != NULL)
			SGXUnrefSharedPBDescKM(hSharedPBDesc);
	} else {
		COMMIT_HANDLE_BATCH_OR_ERROR(psSGXFindSharedPBDescOUT->eError,
					     psPerProc);
	}

	return 0;
}

static int SGXUnrefSharedPBDescBW(u32 ui32BridgeID,
       struct PVRSRV_BRIDGE_IN_SGXUNREFSHAREDPBDESC *psSGXUnrefSharedPBDescIN,
       struct PVRSRV_BRIDGE_OUT_SGXUNREFSHAREDPBDESC *psSGXUnrefSharedPBDescOUT,
       struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *hSharedPBDesc;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_SGX_UNREFSHAREDPBDESC);

	psSGXUnrefSharedPBDescOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &hSharedPBDesc,
			       psSGXUnrefSharedPBDescIN->hSharedPBDesc,
			       PVRSRV_HANDLE_TYPE_SHARED_PB_DESC);
	if (psSGXUnrefSharedPBDescOUT->eError != PVRSRV_OK)
		return 0;

	psSGXUnrefSharedPBDescOUT->eError =
	    SGXUnrefSharedPBDescKM(hSharedPBDesc);

	if (psSGXUnrefSharedPBDescOUT->eError != PVRSRV_OK)
		return 0;

	psSGXUnrefSharedPBDescOUT->eError =
	    PVRSRVReleaseHandle(psPerProc->psHandleBase,
				psSGXUnrefSharedPBDescIN->hSharedPBDesc,
				PVRSRV_HANDLE_TYPE_SHARED_PB_DESC);

	return 0;
}

static int
SGXAddSharedPBDescBW(u32 ui32BridgeID,
	   struct PVRSRV_BRIDGE_IN_SGXADDSHAREDPBDESC *psSGXAddSharedPBDescIN,
	   struct PVRSRV_BRIDGE_OUT_SGXADDSHAREDPBDESC *psSGXAddSharedPBDescOUT,
	   struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *hDevCookieInt;
	struct PVRSRV_KERNEL_MEM_INFO *psSharedPBDescKernelMemInfo;
	struct PVRSRV_KERNEL_MEM_INFO *psHWPBDescKernelMemInfo;
	struct PVRSRV_KERNEL_MEM_INFO *psBlockKernelMemInfo;
	u32 ui32KernelMemInfoHandlesCount =
	    psSGXAddSharedPBDescIN->ui32KernelMemInfoHandlesCount;
	int ret = 0;
	void **phKernelMemInfoHandles = NULL;
	struct PVRSRV_KERNEL_MEM_INFO **ppsKernelMemInfos = NULL;
	u32 i;
	enum PVRSRV_ERROR eError;
	void *hSharedPBDesc = NULL;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_SGX_ADDSHAREDPBDESC);

	NEW_HANDLE_BATCH_OR_ERROR(psSGXAddSharedPBDescOUT->eError, psPerProc,
				  1);

	psSGXAddSharedPBDescOUT->hSharedPBDesc = NULL;

	PVR_ASSERT(ui32KernelMemInfoHandlesCount
		   <= PVRSRV_BRIDGE_SGX_SHAREDPBDESC_MAX_SUBMEMINFOS);

	eError = PVRSRVLookupHandle(psPerProc->psHandleBase,
				    &hDevCookieInt,
				    psSGXAddSharedPBDescIN->hDevCookie,
				    PVRSRV_HANDLE_TYPE_DEV_NODE);
	if (eError != PVRSRV_OK)
		goto PVRSRV_BRIDGE_SGX_ADDSHAREDPBDESC_RETURN_RESULT;

	eError = PVRSRVLookupHandle(psPerProc->psHandleBase,
				    (void **) &psSharedPBDescKernelMemInfo,
				    psSGXAddSharedPBDescIN->
				    hSharedPBDescKernelMemInfo,
				    PVRSRV_HANDLE_TYPE_SHARED_SYS_MEM_INFO);
	if (eError != PVRSRV_OK)
		goto PVRSRV_BRIDGE_SGX_ADDSHAREDPBDESC_RETURN_RESULT;

	eError = PVRSRVLookupHandle(psPerProc->psHandleBase,
				    (void **) &psHWPBDescKernelMemInfo,
				    psSGXAddSharedPBDescIN->
				    hHWPBDescKernelMemInfo,
				    PVRSRV_HANDLE_TYPE_MEM_INFO);
	if (eError != PVRSRV_OK)
		goto PVRSRV_BRIDGE_SGX_ADDSHAREDPBDESC_RETURN_RESULT;

	eError = PVRSRVLookupHandle(psPerProc->psHandleBase,
				    (void **) &psBlockKernelMemInfo,
				    psSGXAddSharedPBDescIN->hBlockKernelMemInfo,
				    PVRSRV_HANDLE_TYPE_SHARED_SYS_MEM_INFO);
	if (eError != PVRSRV_OK)
		goto PVRSRV_BRIDGE_SGX_ADDSHAREDPBDESC_RETURN_RESULT;

	if (!OSAccessOK(PVR_VERIFY_READ,
			psSGXAddSharedPBDescIN->phKernelMemInfoHandles,
			ui32KernelMemInfoHandlesCount * sizeof(void *))) {
		PVR_DPF(PVR_DBG_ERROR, "%s: PVRSRV_BRIDGE_SGX_ADDSHAREDPBDESC:"
			 " Invalid phKernelMemInfos pointer", __func__);
		ret = -EFAULT;
		goto PVRSRV_BRIDGE_SGX_ADDSHAREDPBDESC_RETURN_RESULT;
	}

	if (OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
		       ui32KernelMemInfoHandlesCount * sizeof(void *),
		       (void **)&phKernelMemInfoHandles, NULL) != PVRSRV_OK) {
		ret = -ENOMEM;
		goto PVRSRV_BRIDGE_SGX_ADDSHAREDPBDESC_RETURN_RESULT;
	}

	if (CopyFromUserWrapper(psPerProc,
				ui32BridgeID,
				phKernelMemInfoHandles,
				psSGXAddSharedPBDescIN->phKernelMemInfoHandles,
				ui32KernelMemInfoHandlesCount *
				sizeof(void *))
	    != PVRSRV_OK) {
		ret = -EFAULT;
		goto PVRSRV_BRIDGE_SGX_ADDSHAREDPBDESC_RETURN_RESULT;
	}

	if (OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
		       ui32KernelMemInfoHandlesCount *
		       sizeof(struct PVRSRV_KERNEL_MEM_INFO *),
		       (void **) &ppsKernelMemInfos, NULL) != PVRSRV_OK) {
		ret = -ENOMEM;
		goto PVRSRV_BRIDGE_SGX_ADDSHAREDPBDESC_RETURN_RESULT;
	}

	for (i = 0; i < ui32KernelMemInfoHandlesCount; i++) {
		eError = PVRSRVLookupHandle(psPerProc->psHandleBase,
					    (void **) &
					    ppsKernelMemInfos[i],
					    phKernelMemInfoHandles[i],
					    PVRSRV_HANDLE_TYPE_MEM_INFO);
		if (eError != PVRSRV_OK)
			goto PVRSRV_BRIDGE_SGX_ADDSHAREDPBDESC_RETURN_RESULT;
	}

	eError = PVRSRVReleaseHandle(psPerProc->psHandleBase,
				     psSGXAddSharedPBDescIN->
				     hSharedPBDescKernelMemInfo,
				     PVRSRV_HANDLE_TYPE_SHARED_SYS_MEM_INFO);
	PVR_ASSERT(eError == PVRSRV_OK);

	eError = PVRSRVReleaseHandle(psPerProc->psHandleBase,
				     psSGXAddSharedPBDescIN->
				     hHWPBDescKernelMemInfo,
				     PVRSRV_HANDLE_TYPE_MEM_INFO);
	PVR_ASSERT(eError == PVRSRV_OK);

	eError = PVRSRVReleaseHandle(psPerProc->psHandleBase,
				     psSGXAddSharedPBDescIN->
				     hBlockKernelMemInfo,
				     PVRSRV_HANDLE_TYPE_SHARED_SYS_MEM_INFO);
	PVR_ASSERT(eError == PVRSRV_OK);

	for (i = 0; i < ui32KernelMemInfoHandlesCount; i++) {
		eError = PVRSRVReleaseHandle(psPerProc->psHandleBase,
					     phKernelMemInfoHandles[i],
					     PVRSRV_HANDLE_TYPE_MEM_INFO);
		PVR_ASSERT(eError == PVRSRV_OK);
	}

	eError = SGXAddSharedPBDescKM(psPerProc, hDevCookieInt,
				      psSharedPBDescKernelMemInfo,
				      psHWPBDescKernelMemInfo,
				      psBlockKernelMemInfo,
				      psSGXAddSharedPBDescIN->ui32TotalPBSize,
				      &hSharedPBDesc,
				      ppsKernelMemInfos,
				      ui32KernelMemInfoHandlesCount);

	if (eError != PVRSRV_OK)
		goto PVRSRV_BRIDGE_SGX_ADDSHAREDPBDESC_RETURN_RESULT;

	PVRSRVAllocHandleNR(psPerProc->psHandleBase,
			    &psSGXAddSharedPBDescOUT->hSharedPBDesc,
			    hSharedPBDesc,
			    PVRSRV_HANDLE_TYPE_SHARED_PB_DESC,
			    PVRSRV_HANDLE_ALLOC_FLAG_NONE);

PVRSRV_BRIDGE_SGX_ADDSHAREDPBDESC_RETURN_RESULT:

	if (phKernelMemInfoHandles)
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
			  psSGXAddSharedPBDescIN->ui32KernelMemInfoHandlesCount
				* sizeof(void *),
			  (void *)phKernelMemInfoHandles, NULL);
	if (ppsKernelMemInfos)
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
			  psSGXAddSharedPBDescIN->ui32KernelMemInfoHandlesCount
				 * sizeof(struct PVRSRV_KERNEL_MEM_INFO *),
			  (void *)ppsKernelMemInfos, NULL);

	if (ret == 0 && eError == PVRSRV_OK)
		COMMIT_HANDLE_BATCH_OR_ERROR(psSGXAddSharedPBDescOUT->eError,
					     psPerProc);

	psSGXAddSharedPBDescOUT->eError = eError;

	return ret;
}


static int PVRSRVGetMiscInfoBW(u32 ui32BridgeID,
		    struct PVRSRV_BRIDGE_IN_GET_MISC_INFO *psGetMiscInfoIN,
		    struct PVRSRV_BRIDGE_OUT_GET_MISC_INFO *psGetMiscInfoOUT,
		    struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	enum PVRSRV_ERROR eError;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_GET_MISC_INFO);

	OSMemCopy(&psGetMiscInfoOUT->sMiscInfo, &psGetMiscInfoIN->sMiscInfo,
		  sizeof(struct PVRSRV_MISC_INFO));

	if (psGetMiscInfoIN->sMiscInfo.ui32StateRequest &
				PVRSRV_MISC_INFO_MEMSTATS_PRESENT) {

		ASSIGN_AND_EXIT_ON_ERROR(psGetMiscInfoOUT->eError,
		     OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
			   psGetMiscInfoOUT->sMiscInfo.ui32MemoryStrLen,
			   (void **)&psGetMiscInfoOUT->sMiscInfo.pszMemoryStr,
			   NULL));

		psGetMiscInfoOUT->eError = PVRSRVGetMiscInfoKM(
				(struct PVRSRV_MISC_INFO __force *)
					&psGetMiscInfoOUT->sMiscInfo);

		eError = CopyToUserWrapper(psPerProc, ui32BridgeID,
				(void __force __user *)
					psGetMiscInfoIN->sMiscInfo.pszMemoryStr,
				psGetMiscInfoOUT->sMiscInfo.pszMemoryStr,
				psGetMiscInfoOUT->sMiscInfo.ui32MemoryStrLen);

		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
			  psGetMiscInfoOUT->sMiscInfo.ui32MemoryStrLen,
			  (void *)psGetMiscInfoOUT->sMiscInfo.pszMemoryStr,
			  NULL);

		psGetMiscInfoOUT->sMiscInfo.pszMemoryStr =
		    psGetMiscInfoIN->sMiscInfo.pszMemoryStr;

		if (eError != PVRSRV_OK) {

			PVR_DPF(PVR_DBG_ERROR,
				 "PVRSRVGetMiscInfoBW Error copy to user");
			return -EFAULT;
		}
	} else {
		psGetMiscInfoOUT->eError =
		    PVRSRVGetMiscInfoKM(&psGetMiscInfoOUT->sMiscInfo);
	}

	if (psGetMiscInfoIN->sMiscInfo.
	    ui32StateRequest & PVRSRV_MISC_INFO_GLOBALEVENTOBJECT_PRESENT)

		psGetMiscInfoOUT->eError =
		    PVRSRVAllocHandle(psPerProc->psHandleBase,
				      &psGetMiscInfoOUT->sMiscInfo.
				      sGlobalEventObject.hOSEventKM,
				      psGetMiscInfoOUT->sMiscInfo.
				      sGlobalEventObject.hOSEventKM,
				      PVRSRV_HANDLE_TYPE_SHARED_EVENT_OBJECT,
				      PVRSRV_HANDLE_ALLOC_FLAG_SHARED);

	return 0;
}

static int PVRSRVConnectBW(u32 ui32BridgeID,
		void *psBridgeIn,
		struct PVRSRV_BRIDGE_OUT_CONNECT_SERVICES *psConnectServicesOUT,
		struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVR_UNREFERENCED_PARAMETER(psBridgeIn);

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_CONNECT_SERVICES);

	psConnectServicesOUT->hKernelServices = psPerProc->hPerProcData;
	psConnectServicesOUT->eError = PVRSRV_OK;

	return 0;
}

static int PVRSRVDisconnectBW(u32 ui32BridgeID,
		   void *psBridgeIn,
		   struct PVRSRV_BRIDGE_RETURN *psRetOUT,
		   struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVR_UNREFERENCED_PARAMETER(psPerProc);
	PVR_UNREFERENCED_PARAMETER(psBridgeIn);

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_DISCONNECT_SERVICES);

	psRetOUT->eError = PVRSRV_OK;

	return 0;
}

static int PVRSRVEnumerateDCBW(u32 ui32BridgeID,
		    struct PVRSRV_BRIDGE_IN_ENUMCLASS *psEnumDispClassIN,
		    struct PVRSRV_BRIDGE_OUT_ENUMCLASS *psEnumDispClassOUT,
		    struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	PVR_UNREFERENCED_PARAMETER(psPerProc);

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_ENUM_CLASS);

	psEnumDispClassOUT->eError =
	    PVRSRVEnumerateDCKM(psEnumDispClassIN->sDeviceClass,
				&psEnumDispClassOUT->ui32NumDevices,
				&psEnumDispClassOUT->ui32DevID[0]);

	return 0;
}

static int PVRSRVOpenDCDeviceBW(u32 ui32BridgeID,
     struct PVRSRV_BRIDGE_IN_OPEN_DISPCLASS_DEVICE *psOpenDispClassDeviceIN,
     struct PVRSRV_BRIDGE_OUT_OPEN_DISPCLASS_DEVICE *psOpenDispClassDeviceOUT,
     struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *hDevCookieInt;
	void *hDispClassInfoInt;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_OPEN_DISPCLASS_DEVICE);

	NEW_HANDLE_BATCH_OR_ERROR(psOpenDispClassDeviceOUT->eError, psPerProc,
				  1);

	psOpenDispClassDeviceOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &hDevCookieInt,
			       psOpenDispClassDeviceIN->hDevCookie,
			       PVRSRV_HANDLE_TYPE_DEV_NODE);
	if (psOpenDispClassDeviceOUT->eError != PVRSRV_OK)
		return 0;

	psOpenDispClassDeviceOUT->eError =
	    PVRSRVOpenDCDeviceKM(psPerProc,
				 psOpenDispClassDeviceIN->ui32DeviceID,
				 hDevCookieInt, &hDispClassInfoInt);

	if (psOpenDispClassDeviceOUT->eError != PVRSRV_OK)
		return 0;

	PVRSRVAllocHandleNR(psPerProc->psHandleBase,
			    &psOpenDispClassDeviceOUT->hDeviceKM,
			    hDispClassInfoInt,
			    PVRSRV_HANDLE_TYPE_DISP_INFO,
			    PVRSRV_HANDLE_ALLOC_FLAG_NONE);
	COMMIT_HANDLE_BATCH_OR_ERROR(psOpenDispClassDeviceOUT->eError,
				     psPerProc);

	return 0;
}

static int PVRSRVCloseDCDeviceBW(u32 ui32BridgeID,
     struct PVRSRV_BRIDGE_IN_CLOSE_DISPCLASS_DEVICE *psCloseDispClassDeviceIN,
     struct PVRSRV_BRIDGE_RETURN *psRetOUT,
     struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *pvDispClassInfoInt;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_CLOSE_DISPCLASS_DEVICE);

	psRetOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &pvDispClassInfoInt,
			       psCloseDispClassDeviceIN->hDeviceKM,
			       PVRSRV_HANDLE_TYPE_DISP_INFO);

	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError = PVRSRVCloseDCDeviceKM(pvDispClassInfoInt, IMG_FALSE);
	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError =
	    PVRSRVReleaseHandle(psPerProc->psHandleBase,
				psCloseDispClassDeviceIN->hDeviceKM,
				PVRSRV_HANDLE_TYPE_DISP_INFO);
	return 0;
}

static int PVRSRVEnumDCFormatsBW(u32 ui32BridgeID,
     struct PVRSRV_BRIDGE_IN_ENUM_DISPCLASS_FORMATS *psEnumDispClassFormatsIN,
     struct PVRSRV_BRIDGE_OUT_ENUM_DISPCLASS_FORMATS *psEnumDispClassFormatsOUT,
     struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *pvDispClassInfoInt;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_ENUM_DISPCLASS_FORMATS);

	psEnumDispClassFormatsOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &pvDispClassInfoInt,
			       psEnumDispClassFormatsIN->hDeviceKM,
			       PVRSRV_HANDLE_TYPE_DISP_INFO);
	if (psEnumDispClassFormatsOUT->eError != PVRSRV_OK)
		return 0;

	psEnumDispClassFormatsOUT->eError =
	    PVRSRVEnumDCFormatsKM(pvDispClassInfoInt,
				  &psEnumDispClassFormatsOUT->ui32Count,
				  psEnumDispClassFormatsOUT->asFormat);

	return 0;
}

static int PVRSRVEnumDCDimsBW(u32 ui32BridgeID,
	   struct PVRSRV_BRIDGE_IN_ENUM_DISPCLASS_DIMS *psEnumDispClassDimsIN,
	   struct PVRSRV_BRIDGE_OUT_ENUM_DISPCLASS_DIMS *psEnumDispClassDimsOUT,
	   struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *pvDispClassInfoInt;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_ENUM_DISPCLASS_DIMS);

	psEnumDispClassDimsOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &pvDispClassInfoInt,
			       psEnumDispClassDimsIN->hDeviceKM,
			       PVRSRV_HANDLE_TYPE_DISP_INFO);

	if (psEnumDispClassDimsOUT->eError != PVRSRV_OK)
		return 0;

	psEnumDispClassDimsOUT->eError =
	    PVRSRVEnumDCDimsKM(pvDispClassInfoInt,
			       &psEnumDispClassDimsIN->sFormat,
			       &psEnumDispClassDimsOUT->ui32Count,
			       psEnumDispClassDimsOUT->asDim);

	return 0;
}

static int PVRSRVGetDCSystemBufferBW(u32 ui32BridgeID,
   struct PVRSRV_BRIDGE_IN_GET_DISPCLASS_SYSBUFFER *psGetDispClassSysBufferIN,
   struct PVRSRV_BRIDGE_OUT_GET_DISPCLASS_SYSBUFFER *psGetDispClassSysBufferOUT,
   struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *hBufferInt;
	void *pvDispClassInfoInt;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_GET_DISPCLASS_SYSBUFFER);

	NEW_HANDLE_BATCH_OR_ERROR(psGetDispClassSysBufferOUT->eError, psPerProc,
				  1);

	psGetDispClassSysBufferOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &pvDispClassInfoInt,
			       psGetDispClassSysBufferIN->hDeviceKM,
			       PVRSRV_HANDLE_TYPE_DISP_INFO);
	if (psGetDispClassSysBufferOUT->eError != PVRSRV_OK)
		return 0;

	psGetDispClassSysBufferOUT->eError =
	    PVRSRVGetDCSystemBufferKM(pvDispClassInfoInt, &hBufferInt);

	if (psGetDispClassSysBufferOUT->eError != PVRSRV_OK)
		return 0;

	PVRSRVAllocSubHandleNR(psPerProc->psHandleBase,
			       &psGetDispClassSysBufferOUT->hBuffer,
			       hBufferInt,
			       PVRSRV_HANDLE_TYPE_DISP_BUFFER,
			       (enum PVRSRV_HANDLE_ALLOC_FLAG)
			       (PVRSRV_HANDLE_ALLOC_FLAG_PRIVATE |
				PVRSRV_HANDLE_ALLOC_FLAG_SHARED),
			       psGetDispClassSysBufferIN->hDeviceKM);

	COMMIT_HANDLE_BATCH_OR_ERROR(psGetDispClassSysBufferOUT->eError,
				     psPerProc);

	return 0;
}

static int PVRSRVGetDCInfoBW(u32 ui32BridgeID,
	  struct PVRSRV_BRIDGE_IN_GET_DISPCLASS_INFO *psGetDispClassInfoIN,
	  struct PVRSRV_BRIDGE_OUT_GET_DISPCLASS_INFO *psGetDispClassInfoOUT,
	  struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *pvDispClassInfo;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_GET_DISPCLASS_INFO);

	psGetDispClassInfoOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &pvDispClassInfo,
			       psGetDispClassInfoIN->hDeviceKM,
			       PVRSRV_HANDLE_TYPE_DISP_INFO);
	if (psGetDispClassInfoOUT->eError != PVRSRV_OK)
		return 0;

	psGetDispClassInfoOUT->eError =
	    PVRSRVGetDCInfoKM(pvDispClassInfo,
			      &psGetDispClassInfoOUT->sDisplayInfo);

	return 0;
}

static int PVRSRVCreateDCSwapChainBW(u32 ui32BridgeID,
		  struct PVRSRV_BRIDGE_IN_CREATE_DISPCLASS_SWAPCHAIN
				*psCreateDispClassSwapChainIN,
		  struct PVRSRV_BRIDGE_OUT_CREATE_DISPCLASS_SWAPCHAIN
				*psCreateDispClassSwapChainOUT,
		  struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *pvDispClassInfo;
	void *hSwapChainInt;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_CREATE_DISPCLASS_SWAPCHAIN);

	NEW_HANDLE_BATCH_OR_ERROR(psCreateDispClassSwapChainOUT->eError,
				  psPerProc, 1);

	psCreateDispClassSwapChainOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &pvDispClassInfo,
			       psCreateDispClassSwapChainIN->hDeviceKM,
			       PVRSRV_HANDLE_TYPE_DISP_INFO);

	if (psCreateDispClassSwapChainOUT->eError != PVRSRV_OK)
		return 0;

	psCreateDispClassSwapChainOUT->eError =
	    PVRSRVCreateDCSwapChainKM(psPerProc, pvDispClassInfo,
				      psCreateDispClassSwapChainIN->ui32Flags,
				      &psCreateDispClassSwapChainIN->
				      sDstSurfAttrib,
				      &psCreateDispClassSwapChainIN->
				      sSrcSurfAttrib,
				      psCreateDispClassSwapChainIN->
				      ui32BufferCount,
				      psCreateDispClassSwapChainIN->
				      ui32OEMFlags, &hSwapChainInt,
				      &psCreateDispClassSwapChainOUT->
				      ui32SwapChainID);

	if (psCreateDispClassSwapChainOUT->eError != PVRSRV_OK)
		return 0;

	PVRSRVAllocSubHandleNR(psPerProc->psHandleBase,
			       &psCreateDispClassSwapChainOUT->hSwapChain,
			       hSwapChainInt,
			       PVRSRV_HANDLE_TYPE_DISP_SWAP_CHAIN,
			       PVRSRV_HANDLE_ALLOC_FLAG_NONE,
			       psCreateDispClassSwapChainIN->hDeviceKM);

	COMMIT_HANDLE_BATCH_OR_ERROR(psCreateDispClassSwapChainOUT->eError,
				     psPerProc);

	return 0;
}

static int PVRSRVDestroyDCSwapChainBW(u32 ui32BridgeID,
			   struct PVRSRV_BRIDGE_IN_DESTROY_DISPCLASS_SWAPCHAIN
					*psDestroyDispClassSwapChainIN,
			   struct PVRSRV_BRIDGE_RETURN *psRetOUT,
			   struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *pvSwapChain;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_DESTROY_DISPCLASS_SWAPCHAIN);

	psRetOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase, &pvSwapChain,
			       psDestroyDispClassSwapChainIN->hSwapChain,
			       PVRSRV_HANDLE_TYPE_DISP_SWAP_CHAIN);
	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError = PVRSRVDestroyDCSwapChainKM(pvSwapChain);

	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError =
	    PVRSRVReleaseHandle(psPerProc->psHandleBase,
				psDestroyDispClassSwapChainIN->hSwapChain,
				PVRSRV_HANDLE_TYPE_DISP_SWAP_CHAIN);

	return 0;
}

static int PVRSRVSetDCDstRectBW(u32 ui32BridgeID,
	 struct PVRSRV_BRIDGE_IN_SET_DISPCLASS_RECT *psSetDispClassDstRectIN,
	 struct PVRSRV_BRIDGE_RETURN *psRetOUT,
	 struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *pvDispClassInfo;
	void *pvSwapChain;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_SET_DISPCLASS_DSTRECT);

	psRetOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &pvDispClassInfo,
			       psSetDispClassDstRectIN->hDeviceKM,
			       PVRSRV_HANDLE_TYPE_DISP_INFO);
	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &pvSwapChain,
			       psSetDispClassDstRectIN->hSwapChain,
			       PVRSRV_HANDLE_TYPE_DISP_SWAP_CHAIN);

	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError =
	    PVRSRVSetDCDstRectKM(pvDispClassInfo,
				 pvSwapChain, &psSetDispClassDstRectIN->sRect);

	return 0;
}

static int PVRSRVSetDCSrcRectBW(u32 ui32BridgeID,
     struct PVRSRV_BRIDGE_IN_SET_DISPCLASS_RECT *psSetDispClassSrcRectIN,
     struct PVRSRV_BRIDGE_RETURN *psRetOUT,
     struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *pvDispClassInfo;
	void *pvSwapChain;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_SET_DISPCLASS_SRCRECT);

	psRetOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &pvDispClassInfo,
			       psSetDispClassSrcRectIN->hDeviceKM,
			       PVRSRV_HANDLE_TYPE_DISP_INFO);
	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &pvSwapChain,
			       psSetDispClassSrcRectIN->hSwapChain,
			       PVRSRV_HANDLE_TYPE_DISP_SWAP_CHAIN);
	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError =
	    PVRSRVSetDCSrcRectKM(pvDispClassInfo,
				 pvSwapChain, &psSetDispClassSrcRectIN->sRect);

	return 0;
}

static int PVRSRVSetDCDstColourKeyBW(u32 ui32BridgeID,
       struct PVRSRV_BRIDGE_IN_SET_DISPCLASS_COLOURKEY *psSetDispClassColKeyIN,
       struct PVRSRV_BRIDGE_RETURN *psRetOUT,
       struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *pvDispClassInfo;
	void *pvSwapChain;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_SET_DISPCLASS_DSTCOLOURKEY);

	psRetOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &pvDispClassInfo,
			       psSetDispClassColKeyIN->hDeviceKM,
			       PVRSRV_HANDLE_TYPE_DISP_INFO);
	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &pvSwapChain,
			       psSetDispClassColKeyIN->hSwapChain,
			       PVRSRV_HANDLE_TYPE_DISP_SWAP_CHAIN);
	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError =
	    PVRSRVSetDCDstColourKeyKM(pvDispClassInfo,
				      pvSwapChain,
				      psSetDispClassColKeyIN->ui32CKColour);

	return 0;
}

static int PVRSRVSetDCSrcColourKeyBW(u32 ui32BridgeID,
	struct PVRSRV_BRIDGE_IN_SET_DISPCLASS_COLOURKEY *psSetDispClassColKeyIN,
	struct PVRSRV_BRIDGE_RETURN *psRetOUT,
	struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *pvDispClassInfo;
	void *pvSwapChain;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_SET_DISPCLASS_SRCCOLOURKEY);

	psRetOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &pvDispClassInfo,
			       psSetDispClassColKeyIN->hDeviceKM,
			       PVRSRV_HANDLE_TYPE_DISP_INFO);
	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &pvSwapChain,
			       psSetDispClassColKeyIN->hSwapChain,
			       PVRSRV_HANDLE_TYPE_DISP_SWAP_CHAIN);
	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError =
	    PVRSRVSetDCSrcColourKeyKM(pvDispClassInfo,
				      pvSwapChain,
				      psSetDispClassColKeyIN->ui32CKColour);

	return 0;
}

static int PVRSRVGetDCBuffersBW(u32 ui32BridgeID,
     struct PVRSRV_BRIDGE_IN_GET_DISPCLASS_BUFFERS *psGetDispClassBuffersIN,
     struct PVRSRV_BRIDGE_OUT_GET_DISPCLASS_BUFFERS *psGetDispClassBuffersOUT,
     struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *pvDispClassInfo;
	void *pvSwapChain;
	u32 i;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_GET_DISPCLASS_BUFFERS);

	NEW_HANDLE_BATCH_OR_ERROR(psGetDispClassBuffersOUT->eError, psPerProc,
				  PVRSRV_MAX_DC_SWAPCHAIN_BUFFERS);

	psGetDispClassBuffersOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &pvDispClassInfo,
			       psGetDispClassBuffersIN->hDeviceKM,
			       PVRSRV_HANDLE_TYPE_DISP_INFO);
	if (psGetDispClassBuffersOUT->eError != PVRSRV_OK)
		return 0;

	psGetDispClassBuffersOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &pvSwapChain,
			       psGetDispClassBuffersIN->hSwapChain,
			       PVRSRV_HANDLE_TYPE_DISP_SWAP_CHAIN);
	if (psGetDispClassBuffersOUT->eError != PVRSRV_OK)
		return 0;

	psGetDispClassBuffersOUT->eError =
	    PVRSRVGetDCBuffersKM(pvDispClassInfo,
				 pvSwapChain,
				 &psGetDispClassBuffersOUT->ui32BufferCount,
				 psGetDispClassBuffersOUT->ahBuffer);
	if (psGetDispClassBuffersOUT->eError != PVRSRV_OK)
		return 0;

	PVR_ASSERT(psGetDispClassBuffersOUT->ui32BufferCount <=
		   PVRSRV_MAX_DC_SWAPCHAIN_BUFFERS);

	for (i = 0; i < psGetDispClassBuffersOUT->ui32BufferCount; i++) {
		void *hBufferExt;

		PVRSRVAllocSubHandleNR(psPerProc->psHandleBase,
				       &hBufferExt,
				       psGetDispClassBuffersOUT->ahBuffer[i],
				       PVRSRV_HANDLE_TYPE_DISP_BUFFER,
				       (enum PVRSRV_HANDLE_ALLOC_FLAG)
				       (PVRSRV_HANDLE_ALLOC_FLAG_PRIVATE |
					PVRSRV_HANDLE_ALLOC_FLAG_SHARED),
				       psGetDispClassBuffersIN->hSwapChain);

		psGetDispClassBuffersOUT->ahBuffer[i] = hBufferExt;
	}

	COMMIT_HANDLE_BATCH_OR_ERROR(psGetDispClassBuffersOUT->eError,
				     psPerProc);

	return 0;
}

static int PVRSRVSwapToDCBufferBW(u32 ui32BridgeID,
      struct PVRSRV_BRIDGE_IN_SWAP_DISPCLASS_TO_BUFFER *psSwapDispClassBufferIN,
      struct PVRSRV_BRIDGE_RETURN *psRetOUT,
      struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *pvDispClassInfo;
	void *pvSwapChainBuf;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_SWAP_DISPCLASS_TO_BUFFER);

	psRetOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &pvDispClassInfo,
			       psSwapDispClassBufferIN->hDeviceKM,
			       PVRSRV_HANDLE_TYPE_DISP_INFO);
	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError =
	    PVRSRVLookupSubHandle(psPerProc->psHandleBase,
				  &pvSwapChainBuf,
				  psSwapDispClassBufferIN->hBuffer,
				  PVRSRV_HANDLE_TYPE_DISP_BUFFER,
				  psSwapDispClassBufferIN->hDeviceKM);
	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError =
	    PVRSRVSwapToDCBufferKM(pvDispClassInfo,
				   pvSwapChainBuf,
				   psSwapDispClassBufferIN->ui32SwapInterval,
				   psSwapDispClassBufferIN->hPrivateTag,
				   psSwapDispClassBufferIN->ui32ClipRectCount,
				   psSwapDispClassBufferIN->sClipRect);

	return 0;
}

static int PVRSRVSwapToDCSystemBW(u32 ui32BridgeID,
      struct PVRSRV_BRIDGE_IN_SWAP_DISPCLASS_TO_SYSTEM *psSwapDispClassSystemIN,
      struct PVRSRV_BRIDGE_RETURN *psRetOUT,
      struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *pvDispClassInfo;
	void *pvSwapChain;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_SWAP_DISPCLASS_TO_SYSTEM);

	psRetOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &pvDispClassInfo,
			       psSwapDispClassSystemIN->hDeviceKM,
			       PVRSRV_HANDLE_TYPE_DISP_INFO);
	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError =
	    PVRSRVLookupSubHandle(psPerProc->psHandleBase,
				  &pvSwapChain,
				  psSwapDispClassSystemIN->hSwapChain,
				  PVRSRV_HANDLE_TYPE_DISP_SWAP_CHAIN,
				  psSwapDispClassSystemIN->hDeviceKM);
	if (psRetOUT->eError != PVRSRV_OK)
		return 0;
	psRetOUT->eError = PVRSRVSwapToDCSystemKM(pvDispClassInfo, pvSwapChain);

	return 0;
}

static int PVRSRVOpenBCDeviceBW(u32 ui32BridgeID,
   struct PVRSRV_BRIDGE_IN_OPEN_BUFFERCLASS_DEVICE *psOpenBufferClassDeviceIN,
   struct PVRSRV_BRIDGE_OUT_OPEN_BUFFERCLASS_DEVICE *psOpenBufferClassDeviceOUT,
   struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *hDevCookieInt;
	void *hBufClassInfo;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_OPEN_BUFFERCLASS_DEVICE);

	NEW_HANDLE_BATCH_OR_ERROR(psOpenBufferClassDeviceOUT->eError, psPerProc,
				  1);

	psOpenBufferClassDeviceOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &hDevCookieInt,
			       psOpenBufferClassDeviceIN->hDevCookie,
			       PVRSRV_HANDLE_TYPE_DEV_NODE);
	if (psOpenBufferClassDeviceOUT->eError != PVRSRV_OK)
		return 0;

	psOpenBufferClassDeviceOUT->eError =
	    PVRSRVOpenBCDeviceKM(psPerProc,
				 psOpenBufferClassDeviceIN->ui32DeviceID,
				 hDevCookieInt, &hBufClassInfo);
	if (psOpenBufferClassDeviceOUT->eError != PVRSRV_OK)
		return 0;

	PVRSRVAllocHandleNR(psPerProc->psHandleBase,
			    &psOpenBufferClassDeviceOUT->hDeviceKM,
			    hBufClassInfo,
			    PVRSRV_HANDLE_TYPE_BUF_INFO,
			    PVRSRV_HANDLE_ALLOC_FLAG_NONE);

	COMMIT_HANDLE_BATCH_OR_ERROR(psOpenBufferClassDeviceOUT->eError,
				     psPerProc);

	return 0;
}

static int PVRSRVCloseBCDeviceBW(u32 ui32BridgeID,
   struct PVRSRV_BRIDGE_IN_CLOSE_BUFFERCLASS_DEVICE *psCloseBufferClassDeviceIN,
   struct PVRSRV_BRIDGE_RETURN *psRetOUT,
   struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *pvBufClassInfo;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_CLOSE_BUFFERCLASS_DEVICE);

	psRetOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &pvBufClassInfo,
			       psCloseBufferClassDeviceIN->hDeviceKM,
			       PVRSRV_HANDLE_TYPE_BUF_INFO);
	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError = PVRSRVCloseBCDeviceKM(pvBufClassInfo, IMG_FALSE);

	if (psRetOUT->eError != PVRSRV_OK)
		return 0;

	psRetOUT->eError = PVRSRVReleaseHandle(psPerProc->psHandleBase,
					       psCloseBufferClassDeviceIN->
					       hDeviceKM,
					       PVRSRV_HANDLE_TYPE_BUF_INFO);

	return 0;
}

static int PVRSRVGetBCInfoBW(u32 ui32BridgeID,
   struct PVRSRV_BRIDGE_IN_GET_BUFFERCLASS_INFO *psGetBufferClassInfoIN,
   struct PVRSRV_BRIDGE_OUT_GET_BUFFERCLASS_INFO *psGetBufferClassInfoOUT,
   struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *pvBufClassInfo;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_GET_BUFFERCLASS_INFO);

	psGetBufferClassInfoOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &pvBufClassInfo,
			       psGetBufferClassInfoIN->hDeviceKM,
			       PVRSRV_HANDLE_TYPE_BUF_INFO);
	if (psGetBufferClassInfoOUT->eError != PVRSRV_OK)
		return 0;

	psGetBufferClassInfoOUT->eError =
	    PVRSRVGetBCInfoKM(pvBufClassInfo,
			      &psGetBufferClassInfoOUT->sBufferInfo);
	return 0;
}

static int PVRSRVGetBCBufferBW(u32 ui32BridgeID,
    struct PVRSRV_BRIDGE_IN_GET_BUFFERCLASS_BUFFER *psGetBufferClassBufferIN,
    struct PVRSRV_BRIDGE_OUT_GET_BUFFERCLASS_BUFFER *psGetBufferClassBufferOUT,
    struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *pvBufClassInfo;
	void *hBufferInt;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_GET_BUFFERCLASS_BUFFER);

	NEW_HANDLE_BATCH_OR_ERROR(psGetBufferClassBufferOUT->eError, psPerProc,
				  1);

	psGetBufferClassBufferOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       &pvBufClassInfo,
			       psGetBufferClassBufferIN->hDeviceKM,
			       PVRSRV_HANDLE_TYPE_BUF_INFO);
	if (psGetBufferClassBufferOUT->eError != PVRSRV_OK)
		return 0;

	psGetBufferClassBufferOUT->eError =
	    PVRSRVGetBCBufferKM(pvBufClassInfo,
				psGetBufferClassBufferIN->ui32BufferIndex,
				&hBufferInt);

	if (psGetBufferClassBufferOUT->eError != PVRSRV_OK)
		return 0;

	PVRSRVAllocSubHandleNR(psPerProc->psHandleBase,
			       &psGetBufferClassBufferOUT->hBuffer,
			       hBufferInt,
			       PVRSRV_HANDLE_TYPE_BUF_BUFFER,
			       (enum PVRSRV_HANDLE_ALLOC_FLAG)
			       (PVRSRV_HANDLE_ALLOC_FLAG_PRIVATE |
				PVRSRV_HANDLE_ALLOC_FLAG_SHARED),
			       psGetBufferClassBufferIN->hDeviceKM);

	COMMIT_HANDLE_BATCH_OR_ERROR(psGetBufferClassBufferOUT->eError,
				     psPerProc);

	return 0;
}

static int PVRSRVAllocSharedSysMemoryBW(u32 ui32BridgeID,
	  struct PVRSRV_BRIDGE_IN_ALLOC_SHARED_SYS_MEM *psAllocSharedSysMemIN,
	  struct PVRSRV_BRIDGE_OUT_ALLOC_SHARED_SYS_MEM *psAllocSharedSysMemOUT,
	  struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	struct PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_ALLOC_SHARED_SYS_MEM);

	NEW_HANDLE_BATCH_OR_ERROR(psAllocSharedSysMemOUT->eError, psPerProc, 1);

	psAllocSharedSysMemOUT->eError =
	    PVRSRVAllocSharedSysMemoryKM(psPerProc,
					 psAllocSharedSysMemIN->ui32Flags,
					 psAllocSharedSysMemIN->ui32Size,
					 &psKernelMemInfo);
	if (psAllocSharedSysMemOUT->eError != PVRSRV_OK)
		return 0;

	OSMemSet(&psAllocSharedSysMemOUT->sClientMemInfo,
		 0, sizeof(psAllocSharedSysMemOUT->sClientMemInfo));

	if (psKernelMemInfo->pvLinAddrKM)
		psAllocSharedSysMemOUT->sClientMemInfo.pvLinAddrKM =
		    psKernelMemInfo->pvLinAddrKM;
	else
		psAllocSharedSysMemOUT->sClientMemInfo.pvLinAddrKM =
		    psKernelMemInfo->sMemBlk.hOSMemHandle;
	psAllocSharedSysMemOUT->sClientMemInfo.pvLinAddr = NULL;
	psAllocSharedSysMemOUT->sClientMemInfo.ui32Flags =
	    psKernelMemInfo->ui32Flags;
	psAllocSharedSysMemOUT->sClientMemInfo.ui32AllocSize =
	    psKernelMemInfo->ui32AllocSize;
	psAllocSharedSysMemOUT->sClientMemInfo.hMappingInfo =
	    psKernelMemInfo->sMemBlk.hOSMemHandle;

	PVRSRVAllocHandleNR(psPerProc->psHandleBase,
			    &psAllocSharedSysMemOUT->sClientMemInfo.
			    hKernelMemInfo, psKernelMemInfo,
			    PVRSRV_HANDLE_TYPE_SHARED_SYS_MEM_INFO,
			    PVRSRV_HANDLE_ALLOC_FLAG_NONE);

	COMMIT_HANDLE_BATCH_OR_ERROR(psAllocSharedSysMemOUT->eError, psPerProc);

	return 0;
}

static int PVRSRVFreeSharedSysMemoryBW(u32 ui32BridgeID,
	    struct PVRSRV_BRIDGE_IN_FREE_SHARED_SYS_MEM *psFreeSharedSysMemIN,
	    struct PVRSRV_BRIDGE_OUT_FREE_SHARED_SYS_MEM *psFreeSharedSysMemOUT,
	    struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	struct PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_FREE_SHARED_SYS_MEM);

	psFreeSharedSysMemOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase,
			       (void **) &psKernelMemInfo,
			       psFreeSharedSysMemIN->psKernelMemInfo,
			       PVRSRV_HANDLE_TYPE_SHARED_SYS_MEM_INFO);

	if (psFreeSharedSysMemOUT->eError != PVRSRV_OK)
		return 0;

	psFreeSharedSysMemOUT->eError =
	    PVRSRVFreeSharedSysMemoryKM(psKernelMemInfo);
	if (psFreeSharedSysMemOUT->eError != PVRSRV_OK)
		return 0;

	psFreeSharedSysMemOUT->eError =
	    PVRSRVReleaseHandle(psPerProc->psHandleBase,
				psFreeSharedSysMemIN->psKernelMemInfo,
				PVRSRV_HANDLE_TYPE_SHARED_SYS_MEM_INFO);
	return 0;
}

static int PVRSRVMapMemInfoMemBW(u32 ui32BridgeID,
	      struct PVRSRV_BRIDGE_IN_MAP_MEMINFO_MEM *psMapMemInfoMemIN,
	      struct PVRSRV_BRIDGE_OUT_MAP_MEMINFO_MEM *psMapMemInfoMemOUT,
	      struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	struct PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo;
	enum PVRSRV_HANDLE_TYPE eHandleType;
	void *hParent;
	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID, PVRSRV_BRIDGE_MAP_MEMINFO_MEM);

	NEW_HANDLE_BATCH_OR_ERROR(psMapMemInfoMemOUT->eError, psPerProc, 2);

	psMapMemInfoMemOUT->eError =
	    PVRSRVLookupHandleAnyType(psPerProc->psHandleBase,
				      (void **) &psKernelMemInfo,
				      &eHandleType,
				      psMapMemInfoMemIN->hKernelMemInfo);
	if (psMapMemInfoMemOUT->eError != PVRSRV_OK)
		return 0;

	switch (eHandleType) {
	case PVRSRV_HANDLE_TYPE_MEM_INFO:
	case PVRSRV_HANDLE_TYPE_MEM_INFO_REF:
	case PVRSRV_HANDLE_TYPE_SHARED_SYS_MEM_INFO:
		break;
	default:
		psMapMemInfoMemOUT->eError = PVRSRV_ERROR_GENERIC;
		return 0;
	}

	psMapMemInfoMemOUT->eError =
	    PVRSRVGetParentHandle(psPerProc->psHandleBase,
				  &hParent,
				  psMapMemInfoMemIN->hKernelMemInfo,
				  eHandleType);
	if (psMapMemInfoMemOUT->eError != PVRSRV_OK)
		return 0;
	if (hParent == NULL)
		hParent = psMapMemInfoMemIN->hKernelMemInfo;

	OSMemSet(&psMapMemInfoMemOUT->sClientMemInfo,
		 0, sizeof(psMapMemInfoMemOUT->sClientMemInfo));

	if (psKernelMemInfo->pvLinAddrKM)
		psMapMemInfoMemOUT->sClientMemInfo.pvLinAddrKM =
		    psKernelMemInfo->pvLinAddrKM;
	else
		psMapMemInfoMemOUT->sClientMemInfo.pvLinAddrKM =
		    psKernelMemInfo->sMemBlk.hOSMemHandle;

	psMapMemInfoMemOUT->sClientMemInfo.pvLinAddr = NULL;
	psMapMemInfoMemOUT->sClientMemInfo.sDevVAddr =
	    psKernelMemInfo->sDevVAddr;
	psMapMemInfoMemOUT->sClientMemInfo.ui32Flags =
	    psKernelMemInfo->ui32Flags;
	psMapMemInfoMemOUT->sClientMemInfo.ui32AllocSize =
	    psKernelMemInfo->ui32AllocSize;
	psMapMemInfoMemOUT->sClientMemInfo.hMappingInfo =
	    psKernelMemInfo->sMemBlk.hOSMemHandle;

	PVRSRVAllocSubHandleNR(psPerProc->psHandleBase,
			       &psMapMemInfoMemOUT->sClientMemInfo.
			       hKernelMemInfo, psKernelMemInfo,
			       PVRSRV_HANDLE_TYPE_MEM_INFO_REF,
			       PVRSRV_HANDLE_ALLOC_FLAG_MULTI, hParent);

	if (psKernelMemInfo->ui32Flags & PVRSRV_MEM_NO_SYNCOBJ) {

		OSMemSet(&psMapMemInfoMemOUT->sClientSyncInfo,
			 0, sizeof(struct PVRSRV_CLIENT_SYNC_INFO));
		psMapMemInfoMemOUT->psKernelSyncInfo = NULL;
	} else {

		psMapMemInfoMemOUT->sClientSyncInfo.psSyncData =
		    psKernelMemInfo->psKernelSyncInfo->psSyncData;
		psMapMemInfoMemOUT->sClientSyncInfo.sWriteOpsCompleteDevVAddr =
		    psKernelMemInfo->psKernelSyncInfo->
		    sWriteOpsCompleteDevVAddr;
		psMapMemInfoMemOUT->sClientSyncInfo.sReadOpsCompleteDevVAddr =
		    psKernelMemInfo->psKernelSyncInfo->sReadOpsCompleteDevVAddr;

		psMapMemInfoMemOUT->sClientSyncInfo.hMappingInfo =
		    psKernelMemInfo->psKernelSyncInfo->psSyncDataMemInfoKM->
		    sMemBlk.hOSMemHandle;

		psMapMemInfoMemOUT->sClientMemInfo.psClientSyncInfo =
		    &psMapMemInfoMemOUT->sClientSyncInfo;

		PVRSRVAllocSubHandleNR(psPerProc->psHandleBase,
				       &psMapMemInfoMemOUT->sClientSyncInfo.
				       hKernelSyncInfo,
				       psKernelMemInfo->psKernelSyncInfo,
				       PVRSRV_HANDLE_TYPE_SYNC_INFO,
				       PVRSRV_HANDLE_ALLOC_FLAG_MULTI,
				       psMapMemInfoMemOUT->sClientMemInfo.
				       hKernelMemInfo);
	}

	COMMIT_HANDLE_BATCH_OR_ERROR(psMapMemInfoMemOUT->eError, psPerProc);

	return 0;
}

static int MMU_GetPDDevPAddrBW(u32 ui32BridgeID,
	    struct PVRSRV_BRIDGE_IN_GETMMU_PD_DEVPADDR *psGetMmuPDDevPAddrIN,
	    struct PVRSRV_BRIDGE_OUT_GETMMU_PD_DEVPADDR *psGetMmuPDDevPAddrOUT,
	    struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	void *hDevMemContextInt;

	PVRSRV_BRIDGE_ASSERT_CMD(ui32BridgeID,
				 PVRSRV_BRIDGE_GETMMU_PD_DEVPADDR);

	psGetMmuPDDevPAddrOUT->eError =
	    PVRSRVLookupHandle(psPerProc->psHandleBase, &hDevMemContextInt,
			       psGetMmuPDDevPAddrIN->hDevMemContext,
			       PVRSRV_HANDLE_TYPE_DEV_MEM_CONTEXT);
	if (psGetMmuPDDevPAddrOUT->eError != PVRSRV_OK)
		return 0;

	psGetMmuPDDevPAddrOUT->sPDDevPAddr =
	    MMU_GetPDDevPAddr(BM_GetMMUContextFromMemContext
			      (hDevMemContextInt));
	if (psGetMmuPDDevPAddrOUT->sPDDevPAddr.uiAddr)
		psGetMmuPDDevPAddrOUT->eError = PVRSRV_OK;
	else
		psGetMmuPDDevPAddrOUT->eError = PVRSRV_ERROR_GENERIC;
	return 0;
}

static int DummyBW(u32 ui32BridgeID, void *psBridgeIn,
	void *psBridgeOut, struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
#if !defined(DEBUG)
	PVR_UNREFERENCED_PARAMETER(ui32BridgeID);
#endif
	PVR_UNREFERENCED_PARAMETER(psBridgeIn);
	PVR_UNREFERENCED_PARAMETER(psBridgeOut);
	PVR_UNREFERENCED_PARAMETER(psPerProc);

#if defined(DEBUG_BRIDGE_KM)
	PVR_DPF(PVR_DBG_ERROR, "%s: BRIDGE ERROR: BridgeID %lu (%s) mapped to "
		 "Dummy Wrapper (probably not what you want!)",
		 __func__, ui32BridgeID,
		 g_BridgeDispatchTable[ui32BridgeID].pszIOCName);
#else
	PVR_DPF(PVR_DBG_ERROR, "%s: BRIDGE ERROR: BridgeID %lu mapped to "
		 "Dummy Wrapper (probably not what you want!)",
		 __func__, ui32BridgeID);
#endif
	return -ENOTTY;
}

#define SetDispatchTableEntry(ui32Index, pfFunction)			    \
	_SetDispatchTableEntry(PVRSRV_GET_BRIDGE_ID(ui32Index), #ui32Index, \
			(int (*)(u32 ui32BridgeID, void *psBridgeIn,        \
				 void *psBridgeOut,			    \
				 struct PVRSRV_PER_PROCESS_DATA *psPerProc))\
			pfFunction, #pfFunction)

#define DISPATCH_TABLE_GAP_THRESHOLD 5

static void _SetDispatchTableEntry(u32 ui32Index, const char *pszIOCName,
		       int (*pfFunction)(u32 ui32BridgeID, void *psBridgeIn,
		       void *psBridgeOut,
		       struct PVRSRV_PER_PROCESS_DATA *psPerProc),
		       const char *pszFunctionName)
{
	static u32 ui32PrevIndex = ~0UL;
#if !defined(DEBUG)
	PVR_UNREFERENCED_PARAMETER(pszIOCName);
#endif
#if !defined(DEBUG_BRIDGE_KM_DISPATCH_TABLE) && !defined(DEBUG_BRIDGE_KM)
	PVR_UNREFERENCED_PARAMETER(pszFunctionName);
#endif


	if (g_BridgeDispatchTable[ui32Index].pfFunction) {
#if defined(DEBUG_BRIDGE_KM)
		PVR_DPF(PVR_DBG_ERROR, "%s: BUG!: "
			"Adding dispatch table entry for %s "
			"clobbers an existing entry for %s",
			 __func__, pszIOCName,
			 g_BridgeDispatchTable[ui32Index].pszIOCName);
#else
		PVR_DPF(PVR_DBG_ERROR, "%s: BUG!: "
			"Adding dispatch table entry for %s "
			"clobbers an existing entry (index=%lu)",
			 __func__, pszIOCName, ui32Index);
#endif
		PVR_DPF(PVR_DBG_ERROR, "NOTE: "
			"Enabling DEBUG_BRIDGE_KM_DISPATCH_TABLE "
			"may help debug this issue.",
			 __func__);
	}

	if ((ui32PrevIndex != ~0UL) &&
	    ((ui32Index >= ui32PrevIndex + DISPATCH_TABLE_GAP_THRESHOLD) ||
	     (ui32Index <= ui32PrevIndex))) {
#if defined(DEBUG_BRIDGE_KM)
		PVR_DPF(PVR_DBG_WARNING,
			 "%s: There is a gap in the dispatch table "
			 "between indices %lu (%s) and %lu (%s)",
			 __func__, ui32PrevIndex,
			 g_BridgeDispatchTable[ui32PrevIndex].pszIOCName,
			 ui32Index, pszIOCName);
#else
		PVR_DPF(PVR_DBG_WARNING,
			 "%s: There is a gap in the dispatch table "
			 "between indices %lu and %lu (%s)",
			 __func__, ui32PrevIndex, ui32Index, pszIOCName);
#endif
		PVR_DPF(PVR_DBG_ERROR, "NOTE: "
			"Enabling DEBUG_BRIDGE_KM_DISPATCH_TABLE "
			"may help debug this issue.",
			 __func__);
	}

	g_BridgeDispatchTable[ui32Index].pfFunction = pfFunction;
#if defined(DEBUG_BRIDGE_KM)
	g_BridgeDispatchTable[ui32Index].pszIOCName = pszIOCName;
	g_BridgeDispatchTable[ui32Index].pszFunctionName = pszFunctionName;
	g_BridgeDispatchTable[ui32Index].ui32CallCount = 0;
	g_BridgeDispatchTable[ui32Index].ui32CopyFromUserTotalBytes = 0;
#endif

	ui32PrevIndex = ui32Index;
}

enum PVRSRV_ERROR CommonBridgeInit(void)
{
	u32 i;

	SetDispatchTableEntry(PVRSRV_BRIDGE_ENUM_DEVICES,
			      PVRSRVEnumerateDevicesBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_ACQUIRE_DEVICEINFO,
			      PVRSRVAcquireDeviceDataBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_RELEASE_DEVICEINFO, DummyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_CREATE_DEVMEMCONTEXT,
			      PVRSRVCreateDeviceMemContextBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_DESTROY_DEVMEMCONTEXT,
			      PVRSRVDestroyDeviceMemContextBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_GET_DEVMEM_HEAPINFO,
			      PVRSRVGetDeviceMemHeapInfoBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_ALLOC_DEVICEMEM,
			      PVRSRVAllocDeviceMemBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_FREE_DEVICEMEM,
			      PVRSRVFreeDeviceMemBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_GETFREE_DEVICEMEM,
			      PVRSRVGetFreeDeviceMemBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_CREATE_COMMANDQUEUE, DummyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_DESTROY_COMMANDQUEUE, DummyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_KV_TO_MMAP_DATA,
			      PVRMMapKVIndexAddressToMMapDataBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_CONNECT_SERVICES, PVRSRVConnectBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_DISCONNECT_SERVICES,
			      PVRSRVDisconnectBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_WRAP_DEVICE_MEM, DummyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_GET_DEVICEMEMINFO, DummyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_RESERVE_DEV_VIRTMEM, DummyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_FREE_DEV_VIRTMEM, DummyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_MAP_EXT_MEMORY, DummyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_UNMAP_EXT_MEMORY, DummyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_MAP_DEV_MEMORY,
			      PVRSRVMapDeviceMemoryBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_UNMAP_DEV_MEMORY,
			      PVRSRVUnmapDeviceMemoryBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_MAP_DEVICECLASS_MEMORY,
			      PVRSRVMapDeviceClassMemoryBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_UNMAP_DEVICECLASS_MEMORY,
			      PVRSRVUnmapDeviceClassMemoryBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_MAP_MEM_INFO_TO_USER, DummyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_UNMAP_MEM_INFO_FROM_USER, DummyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_CACHE_FLUSH_DRM,
			      PVRSRVCacheFlushDRIBW);

	SetDispatchTableEntry(PVRSRV_BRIDGE_PROCESS_SIMISR_EVENT, DummyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_REGISTER_SIM_PROCESS, DummyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_UNREGISTER_SIM_PROCESS, DummyBW);

	SetDispatchTableEntry(PVRSRV_BRIDGE_MAPPHYSTOUSERSPACE, DummyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_UNMAPPHYSTOUSERSPACE, DummyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_GETPHYSTOUSERSPACEMAP, DummyBW);

	SetDispatchTableEntry(PVRSRV_BRIDGE_GET_FB_STATS, DummyBW);

	SetDispatchTableEntry(PVRSRV_BRIDGE_GET_MISC_INFO, PVRSRVGetMiscInfoBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_RELEASE_MISC_INFO, DummyBW);


#if defined(PDUMP)
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP_INIT, DummyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP_MEMPOL, PDumpMemPolBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP_DUMPMEM, PDumpMemBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP_REG, PDumpRegWithFlagsBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP_REGPOL, PDumpRegPolBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP_COMMENT, PDumpCommentBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP_SETFRAME, PDumpSetFrameBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP_ISCAPTURING,
			      PDumpIsCaptureFrameBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP_DUMPBITMAP, PDumpBitmapBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP_DUMPREADREG, PDumpReadRegBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP_SYNCPOL, PDumpSyncPolBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP_DUMPSYNC, PDumpSyncDumpBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP_DRIVERINFO,
			      PDumpDriverInfoBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP_PDREG, PDumpPDRegBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP_DUMPPDDEVPADDR,
			      PDumpPDDevPAddrBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP_BUFFER_ARRAY,
			      PDumpBufferArrayBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP_CYCLE_COUNT_REG_READ,
			      PDumpCycleCountRegReadBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP_3D_SIGNATURE_REGISTERS,
			      PDump3DSignatureRegistersBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP_COUNTER_REGISTERS,
			      PDumpCounterRegistersBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_PDUMP_TA_SIGNATURE_REGISTERS,
			      PDumpTASignatureRegistersBW);
#endif

	SetDispatchTableEntry(PVRSRV_BRIDGE_GET_OEMJTABLE, DummyBW);

	SetDispatchTableEntry(PVRSRV_BRIDGE_ENUM_CLASS, PVRSRVEnumerateDCBW);

	SetDispatchTableEntry(PVRSRV_BRIDGE_OPEN_DISPCLASS_DEVICE,
			      PVRSRVOpenDCDeviceBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_CLOSE_DISPCLASS_DEVICE,
			      PVRSRVCloseDCDeviceBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_ENUM_DISPCLASS_FORMATS,
			      PVRSRVEnumDCFormatsBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_ENUM_DISPCLASS_DIMS,
			      PVRSRVEnumDCDimsBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_GET_DISPCLASS_SYSBUFFER,
			      PVRSRVGetDCSystemBufferBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_GET_DISPCLASS_INFO,
			      PVRSRVGetDCInfoBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_CREATE_DISPCLASS_SWAPCHAIN,
			      PVRSRVCreateDCSwapChainBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_DESTROY_DISPCLASS_SWAPCHAIN,
			      PVRSRVDestroyDCSwapChainBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SET_DISPCLASS_DSTRECT,
			      PVRSRVSetDCDstRectBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SET_DISPCLASS_SRCRECT,
			      PVRSRVSetDCSrcRectBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SET_DISPCLASS_DSTCOLOURKEY,
			      PVRSRVSetDCDstColourKeyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SET_DISPCLASS_SRCCOLOURKEY,
			      PVRSRVSetDCSrcColourKeyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_GET_DISPCLASS_BUFFERS,
			      PVRSRVGetDCBuffersBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SWAP_DISPCLASS_TO_BUFFER,
			      PVRSRVSwapToDCBufferBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SWAP_DISPCLASS_TO_SYSTEM,
			      PVRSRVSwapToDCSystemBW);

	SetDispatchTableEntry(PVRSRV_BRIDGE_OPEN_BUFFERCLASS_DEVICE,
			      PVRSRVOpenBCDeviceBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_CLOSE_BUFFERCLASS_DEVICE,
			      PVRSRVCloseBCDeviceBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_GET_BUFFERCLASS_INFO,
			      PVRSRVGetBCInfoBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_GET_BUFFERCLASS_BUFFER,
			      PVRSRVGetBCBufferBW);

	SetDispatchTableEntry(PVRSRV_BRIDGE_WRAP_EXT_MEMORY,
			      PVRSRVWrapExtMemoryBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_UNWRAP_EXT_MEMORY,
			      PVRSRVUnwrapExtMemoryBW);

	SetDispatchTableEntry(PVRSRV_BRIDGE_ALLOC_SHARED_SYS_MEM,
			      PVRSRVAllocSharedSysMemoryBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_FREE_SHARED_SYS_MEM,
			      PVRSRVFreeSharedSysMemoryBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_MAP_MEMINFO_MEM,
			      PVRSRVMapMemInfoMemBW);

	SetDispatchTableEntry(PVRSRV_BRIDGE_GETMMU_PD_DEVPADDR,
			      MMU_GetPDDevPAddrBW);

	SetDispatchTableEntry(PVRSRV_BRIDGE_INITSRV_CONNECT,
			      PVRSRVInitSrvConnectBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_INITSRV_DISCONNECT,
			      PVRSRVInitSrvDisconnectBW);

	SetDispatchTableEntry(PVRSRV_BRIDGE_EVENT_OBJECT_WAIT,
			      PVRSRVEventObjectWaitBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_EVENT_OBJECT_OPEN,
			      PVRSRVEventObjectOpenBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_EVENT_OBJECT_CLOSE,
			      PVRSRVEventObjectCloseBW);


	SetDispatchTableEntry(PVRSRV_BRIDGE_SGX_GETCLIENTINFO,
			      SGXGetClientInfoBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SGX_RELEASECLIENTINFO,
			      SGXReleaseClientInfoBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SGX_GETINTERNALDEVINFO,
			      SGXGetInternalDevInfoBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SGX_DOKICK, SGXDoKickBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SGX_GETPHYSPAGEADDR, DummyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SGX_READREGISTRYDWORD, DummyBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SGX_SCHEDULECOMMAND, DummyBW);

	SetDispatchTableEntry(PVRSRV_BRIDGE_SGX_2DQUERYBLTSCOMPLETE,
			      SGX2DQueryBlitsCompleteBW);

	SetDispatchTableEntry(PVRSRV_BRIDGE_SGX_GETMMUPDADDR, DummyBW);

	SetDispatchTableEntry(PVRSRV_BRIDGE_SGX_SUBMITTRANSFER,
			      SGXSubmitTransferBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SGX_GETMISCINFO, SGXGetMiscInfoBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SGXINFO_FOR_SRVINIT,
			      SGXGetInfoForSrvinitBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SGX_DEVINITPART2,
			      SGXDevInitPart2BW);

	SetDispatchTableEntry(PVRSRV_BRIDGE_SGX_FINDSHAREDPBDESC,
			      SGXFindSharedPBDescBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SGX_UNREFSHAREDPBDESC,
			      SGXUnrefSharedPBDescBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SGX_ADDSHAREDPBDESC,
			      SGXAddSharedPBDescBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SGX_REGISTER_HW_RENDER_CONTEXT,
			      SGXRegisterHWRenderContextBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SGX_FLUSH_HW_RENDER_TARGET,
			      SGXFlushHWRenderTargetBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SGX_UNREGISTER_HW_RENDER_CONTEXT,
			      SGXUnregisterHWRenderContextBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SGX_REGISTER_HW_TRANSFER_CONTEXT,
			      SGXRegisterHWTransferContextBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SGX_UNREGISTER_HW_TRANSFER_CONTEXT,
			      SGXUnregisterHWTransferContextBW);
	SetDispatchTableEntry(PVRSRV_BRIDGE_SGX_READ_DIFF_COUNTERS,
			      SGXReadDiffCountersBW);

	for (i = 0; i < BRIDGE_DISPATCH_TABLE_ENTRY_COUNT; i++)
		if (!g_BridgeDispatchTable[i].pfFunction) {
			g_BridgeDispatchTable[i].pfFunction = DummyBW;
#if defined(DEBUG_BRIDGE_KM)
			g_BridgeDispatchTable[i].pszIOCName =
			    "_PVRSRV_BRIDGE_DUMMY";
			g_BridgeDispatchTable[i].pszFunctionName = "DummyBW";
			g_BridgeDispatchTable[i].ui32CallCount = 0;
			g_BridgeDispatchTable[i].ui32CopyFromUserTotalBytes = 0;
			g_BridgeDispatchTable[i].ui32CopyToUserTotalBytes = 0;
#endif
		}

	return PVRSRV_OK;
}

int BridgedDispatchKM(struct PVRSRV_PER_PROCESS_DATA *psPerProc,
		      struct PVRSRV_BRIDGE_PACKAGE *psBridgePackageKM)
{

	void *psBridgeIn;
	void *psBridgeOut;
	int (*pfBridgeHandler)(u32 ui32BridgeID,
			      void *psBridgeIn,
			      void *psBridgeOut,
			      struct PVRSRV_PER_PROCESS_DATA *psPerProc);
	u32 ui32BridgeID = psBridgePackageKM->ui32BridgeID;
	int err = -EFAULT;

#if defined(DEBUG_BRIDGE_KM)
	g_BridgeDispatchTable[ui32BridgeID].ui32CallCount++;
	g_BridgeGlobalStats.ui32IOCTLCount++;
#endif
	if (!psPerProc->bInitProcess) {
		if (PVRSRVGetInitServerState(PVRSRV_INIT_SERVER_RAN)) {
			if (!PVRSRVGetInitServerState
			    (PVRSRV_INIT_SERVER_SUCCESSFUL)) {
				PVR_DPF(PVR_DBG_ERROR,
					 "%s: Initialisation failed.  "
					 "Driver unusable.",
					 __func__);
				goto return_fault;
			}
		} else {
			if (PVRSRVGetInitServerState
			    (PVRSRV_INIT_SERVER_RUNNING)) {
				PVR_DPF(PVR_DBG_ERROR,
					 "%s: Initialisation is in progress",
					 __func__);
				goto return_fault;
			} else {
				switch (ui32BridgeID) {
				case PVRSRV_GET_BRIDGE_ID(
					PVRSRV_BRIDGE_CONNECT_SERVICES):
				case PVRSRV_GET_BRIDGE_ID(
					PVRSRV_BRIDGE_DISCONNECT_SERVICES):
				case PVRSRV_GET_BRIDGE_ID(
					PVRSRV_BRIDGE_INITSRV_CONNECT):
				case PVRSRV_GET_BRIDGE_ID(
					PVRSRV_BRIDGE_INITSRV_DISCONNECT):
					break;
				default:
					PVR_DPF(PVR_DBG_ERROR,
						 "%s: Driver initialisation "
						 "not completed yet.",
						 __func__);
					goto return_fault;
				}
			}
		}
	}

	{
		struct SYS_DATA *psSysData;

		if (SysAcquireData(&psSysData) != PVRSRV_OK)
			goto return_fault;

		psBridgeIn = ((struct ENV_DATA *)
				psSysData->pvEnvSpecificData)->pvBridgeData;
		psBridgeOut = (void *)((u8 *)psBridgeIn +
				 PVRSRV_MAX_BRIDGE_IN_SIZE);

		if (psBridgePackageKM->ui32InBufferSize > 0) {
			if (!OSAccessOK(PVR_VERIFY_READ,
					psBridgePackageKM->pvParamIn,
					psBridgePackageKM->ui32InBufferSize))
				PVR_DPF(PVR_DBG_ERROR,
					 "%s: Invalid pvParamIn pointer",
					 __func__);

			if (CopyFromUserWrapper(psPerProc, ui32BridgeID,
					psBridgeIn,
					psBridgePackageKM->pvParamIn,
					psBridgePackageKM->ui32InBufferSize)
			    != PVRSRV_OK)
				goto return_fault;
		}
	}

	if (ui32BridgeID >= (BRIDGE_DISPATCH_TABLE_ENTRY_COUNT)) {
		PVR_DPF(PVR_DBG_ERROR,
			 "%s: ui32BridgeID = %d is out if range!", __func__,
			 ui32BridgeID);
		goto return_fault;
	}
	pfBridgeHandler = (int (*)(u32 ui32BridgeID, void *psBridgeIn,
				   void *psBridgeOut,
				   struct PVRSRV_PER_PROCESS_DATA *psPerProc))
			   g_BridgeDispatchTable[ui32BridgeID].pfFunction;
	err = pfBridgeHandler(ui32BridgeID, psBridgeIn, psBridgeOut, psPerProc);
	if (err < 0)
		goto return_fault;


	if (CopyToUserWrapper(psPerProc,
			      ui32BridgeID,
			      psBridgePackageKM->pvParamOut,
			      psBridgeOut, psBridgePackageKM->ui32OutBufferSize)
	    != PVRSRV_OK)
		goto return_fault;

	err = 0;
return_fault:
	ReleaseHandleBatch(psPerProc);
	return err;
}
