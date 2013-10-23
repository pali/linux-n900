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
#include "buffer_manager.h"
#include "pdump_km.h"
#include "sgxmmu.h"
#include "sgxapi_km.h"
#include "pvr_bridge_km.h"

#include "linux/kernel.h"

static PVRSRV_ERROR AllocDeviceMem(IMG_HANDLE		hDevCookie,
									IMG_HANDLE		hDevMemHeap,
									IMG_UINT32		ui32Flags,
									IMG_UINT32		ui32Size,
									IMG_UINT32		ui32Alignment,
									PVRSRV_KERNEL_MEM_INFO	**ppsMemInfo);


IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVGetDeviceMemHeapsKM(IMG_HANDLE hDevCookie,
														 PVRSRV_HEAP_INFO *psHeapInfo
														 )
{
	PVRSRV_DEVICE_NODE *psDeviceNode;
	IMG_UINT32 ui32HeapCount;
	DEVICE_MEMORY_HEAP_INFO *psDeviceMemoryHeap;
	IMG_UINT32 i;

	psDeviceNode = (PVRSRV_DEVICE_NODE *)hDevCookie;
	PVR_ASSERT(psDeviceNode != IMG_NULL);

	
	ui32HeapCount = psDeviceNode->sDevMemoryInfo.ui32HeapCount;
	psDeviceMemoryHeap = psDeviceNode->sDevMemoryInfo.psDeviceMemoryHeap;

	
	PVR_ASSERT(ui32HeapCount <= PVRSRV_MAX_CLIENT_HEAPS);

	
	for(i=0; i<ui32HeapCount; i++)
	{
		
		psHeapInfo[i].ui32HeapID = psDeviceMemoryHeap[i].ui32HeapID;
		psHeapInfo[i].hDevMemHeap = psDeviceMemoryHeap[i].hDevMemHeap;
		psHeapInfo[i].sDevVAddrBase = psDeviceMemoryHeap[i].sDevVAddrBase;
		psHeapInfo[i].ui32HeapByteSize = psDeviceMemoryHeap[i].ui32HeapSize;
		psHeapInfo[i].ui32Attribs = psDeviceMemoryHeap[i].ui32Attribs;
	}

	for(; i < PVRSRV_MAX_CLIENT_HEAPS; i++)
	{
		OSMemSet(psHeapInfo + i, 0, sizeof(*psHeapInfo));
		psHeapInfo[i].ui32HeapID = (IMG_UINT32)SGX_UNDEFINED_HEAP_ID;
	}

	return PVRSRV_OK;
}

IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVCreateDeviceMemContextKM(IMG_HANDLE					hDevCookie,
														 PVRSRV_PER_PROCESS_DATA	*psPerProc,
														 IMG_HANDLE 				*phDevMemContext,
														 IMG_UINT32 				*pui32ClientHeapCount,
														 PVRSRV_HEAP_INFO			*psHeapInfo,
														 IMG_BOOL					*pbCreated
#if defined(PVR_SECURE_HANDLES)
														 , IMG_BOOL *pbShared
#endif
														 )
{
	PVRSRV_DEVICE_NODE *psDeviceNode;
	IMG_UINT32 ui32HeapCount, ui32ClientHeapCount=0;
	DEVICE_MEMORY_HEAP_INFO *psDeviceMemoryHeap;
	IMG_HANDLE hDevMemContext;
	IMG_HANDLE hDevMemHeap;
	IMG_DEV_PHYADDR sPDDevPAddr;
	IMG_UINT32 i;

	psDeviceNode = (PVRSRV_DEVICE_NODE *)hDevCookie;
	PVR_ASSERT(psDeviceNode != IMG_NULL);

	

	ui32HeapCount = psDeviceNode->sDevMemoryInfo.ui32HeapCount;
	psDeviceMemoryHeap = psDeviceNode->sDevMemoryInfo.psDeviceMemoryHeap;

	

	PVR_ASSERT(ui32HeapCount <= PVRSRV_MAX_CLIENT_HEAPS);

	

	hDevMemContext = BM_CreateContext(psDeviceNode,
									  &sPDDevPAddr,
									  psPerProc,
									  pbCreated);
	if (hDevMemContext == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVCreateDeviceMemContextKM: Failed BM_CreateContext"));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	
	for(i=0; i<ui32HeapCount; i++)
	{
		switch(psDeviceMemoryHeap[i].DevMemHeapType)
		{
			case DEVICE_MEMORY_HEAP_SHARED_EXPORTED:
			{
				
				psHeapInfo[ui32ClientHeapCount].ui32HeapID = psDeviceMemoryHeap[i].ui32HeapID;
				psHeapInfo[ui32ClientHeapCount].hDevMemHeap = psDeviceMemoryHeap[i].hDevMemHeap;
				psHeapInfo[ui32ClientHeapCount].sDevVAddrBase = psDeviceMemoryHeap[i].sDevVAddrBase;
				psHeapInfo[ui32ClientHeapCount].ui32HeapByteSize = psDeviceMemoryHeap[i].ui32HeapSize;
				psHeapInfo[ui32ClientHeapCount].ui32Attribs = psDeviceMemoryHeap[i].ui32Attribs;
#if defined(PVR_SECURE_HANDLES)
				pbShared[ui32ClientHeapCount] = IMG_TRUE;
#endif
				ui32ClientHeapCount++;
				break;
			}
			case DEVICE_MEMORY_HEAP_PERCONTEXT:
			{
				hDevMemHeap = BM_CreateHeap(hDevMemContext,
											&psDeviceMemoryHeap[i]);

				
				psHeapInfo[ui32ClientHeapCount].ui32HeapID = psDeviceMemoryHeap[i].ui32HeapID;
				psHeapInfo[ui32ClientHeapCount].hDevMemHeap = hDevMemHeap;
				psHeapInfo[ui32ClientHeapCount].sDevVAddrBase = psDeviceMemoryHeap[i].sDevVAddrBase;
				psHeapInfo[ui32ClientHeapCount].ui32HeapByteSize = psDeviceMemoryHeap[i].ui32HeapSize;
				psHeapInfo[ui32ClientHeapCount].ui32Attribs = psDeviceMemoryHeap[i].ui32Attribs;
#if defined(PVR_SECURE_HANDLES)
				pbShared[ui32ClientHeapCount] = IMG_FALSE;
#endif

				ui32ClientHeapCount++;
				break;
			}
		}
	}

	
	*pui32ClientHeapCount = ui32ClientHeapCount;
	*phDevMemContext = hDevMemContext;
	
	return PVRSRV_OK;
}

IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVDestroyDeviceMemContextKM(IMG_HANDLE hDevCookie,
														  IMG_HANDLE hDevMemContext,
														  IMG_BOOL *pbDestroyed)
{
	PVR_UNREFERENCED_PARAMETER(hDevCookie);

	return BM_DestroyContext(hDevMemContext, pbDestroyed);
}




IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVGetDeviceMemHeapInfoKM(IMG_HANDLE					hDevCookie,
														 IMG_HANDLE 				hDevMemContext,
														 IMG_UINT32 				*pui32ClientHeapCount,
														 PVRSRV_HEAP_INFO			*psHeapInfo
#if defined(PVR_SECURE_HANDLES)
														 , IMG_BOOL *pbShared
#endif
														 )
{
	PVRSRV_DEVICE_NODE *psDeviceNode;
	IMG_UINT32 ui32HeapCount, ui32ClientHeapCount=0;
	DEVICE_MEMORY_HEAP_INFO *psDeviceMemoryHeap;
	IMG_HANDLE hDevMemHeap;
	IMG_UINT32 i;

	psDeviceNode = (PVRSRV_DEVICE_NODE *)hDevCookie;
	PVR_ASSERT(psDeviceNode != IMG_NULL);

	

	ui32HeapCount = psDeviceNode->sDevMemoryInfo.ui32HeapCount;
	psDeviceMemoryHeap = psDeviceNode->sDevMemoryInfo.psDeviceMemoryHeap;

	

	PVR_ASSERT(ui32HeapCount <= PVRSRV_MAX_CLIENT_HEAPS);

	
	for(i=0; i<ui32HeapCount; i++)
	{
		switch(psDeviceMemoryHeap[i].DevMemHeapType)
		{
			case DEVICE_MEMORY_HEAP_SHARED_EXPORTED:
			{
				
				psHeapInfo[ui32ClientHeapCount].ui32HeapID = psDeviceMemoryHeap[i].ui32HeapID;
				psHeapInfo[ui32ClientHeapCount].hDevMemHeap = psDeviceMemoryHeap[i].hDevMemHeap;
				psHeapInfo[ui32ClientHeapCount].sDevVAddrBase = psDeviceMemoryHeap[i].sDevVAddrBase;
				psHeapInfo[ui32ClientHeapCount].ui32HeapByteSize = psDeviceMemoryHeap[i].ui32HeapSize;
				psHeapInfo[ui32ClientHeapCount].ui32Attribs = psDeviceMemoryHeap[i].ui32Attribs;
#if defined(PVR_SECURE_HANDLES)
				pbShared[ui32ClientHeapCount] = IMG_TRUE;
#endif
				ui32ClientHeapCount++;
				break;
			}
			case DEVICE_MEMORY_HEAP_PERCONTEXT:
			{
				hDevMemHeap = BM_CreateHeap(hDevMemContext,
											&psDeviceMemoryHeap[i]);

				
				psHeapInfo[ui32ClientHeapCount].ui32HeapID = psDeviceMemoryHeap[i].ui32HeapID;
				psHeapInfo[ui32ClientHeapCount].hDevMemHeap = hDevMemHeap;
				psHeapInfo[ui32ClientHeapCount].sDevVAddrBase = psDeviceMemoryHeap[i].sDevVAddrBase;
				psHeapInfo[ui32ClientHeapCount].ui32HeapByteSize = psDeviceMemoryHeap[i].ui32HeapSize;
				psHeapInfo[ui32ClientHeapCount].ui32Attribs = psDeviceMemoryHeap[i].ui32Attribs;
#if defined(PVR_SECURE_HANDLES)
				pbShared[ui32ClientHeapCount] = IMG_FALSE;
#endif

				ui32ClientHeapCount++;
				break;
			}
		}
	}

	
	*pui32ClientHeapCount = ui32ClientHeapCount;
	
	return PVRSRV_OK;
}


static PVRSRV_ERROR AllocDeviceMem(IMG_HANDLE		hDevCookie,
									IMG_HANDLE		hDevMemHeap,
									IMG_UINT32		ui32Flags,
									IMG_UINT32		ui32Size,
									IMG_UINT32		ui32Alignment,
									PVRSRV_KERNEL_MEM_INFO	**ppsMemInfo)
{
 	PVRSRV_KERNEL_MEM_INFO	*psMemInfo;
	BM_HANDLE 		hBuffer;
	
	PVRSRV_MEMBLK	*psMemBlock;
	IMG_BOOL		bBMError;

	PVR_UNREFERENCED_PARAMETER(hDevCookie);

	*ppsMemInfo = IMG_NULL;

	if(OSAllocMem( PVRSRV_OS_PAGEABLE_HEAP,
					 sizeof(PVRSRV_KERNEL_MEM_INFO ),
					 (IMG_VOID **)&psMemInfo, IMG_NULL) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"AllocDeviceMem: Failed to alloc memory for block"));
		return (PVRSRV_ERROR_OUT_OF_MEMORY);
	}

	psMemBlock = &(psMemInfo->sMemBlk);

	
	psMemInfo->ui32Flags = ui32Flags | PVRSRV_MEM_RAM_BACKED_ALLOCATION;

	bBMError = BM_Alloc (hDevMemHeap,
							IMG_NULL,
							ui32Size,
							&psMemInfo->ui32Flags,
							ui32Alignment,
							&hBuffer);

	if (!bBMError)
	{
		PVR_DPF((PVR_DBG_ERROR,"AllocDeviceMem: BM_Alloc Failed"));
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, 0, psMemInfo, IMG_NULL);
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	
	psMemBlock->sDevVirtAddr = BM_HandleToDevVaddr(hBuffer);
	psMemBlock->hOSMemHandle = BM_HandleToOSMemHandle(hBuffer);

	
	psMemBlock->hBuffer = (IMG_HANDLE)hBuffer;

	

	psMemInfo->pvLinAddrKM = BM_HandleToCpuVaddr(hBuffer);

	psMemInfo->sDevVAddr = psMemBlock->sDevVirtAddr;

	psMemInfo->ui32AllocSize = ui32Size;

	
	psMemInfo->pvSysBackupBuffer = IMG_NULL;

	
	*ppsMemInfo = psMemInfo;

	
	return (PVRSRV_OK);
}


static PVRSRV_ERROR FreeDeviceMem(PVRSRV_KERNEL_MEM_INFO *psMemInfo)
{
	BM_HANDLE		hBuffer;

	if (!psMemInfo)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	hBuffer = psMemInfo->sMemBlk.hBuffer;

	
	BM_Free(hBuffer, psMemInfo->ui32Flags);

	if(psMemInfo->pvSysBackupBuffer)
	{
		
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, 0, psMemInfo->pvSysBackupBuffer, IMG_NULL);
	}

	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, 0, psMemInfo, IMG_NULL);

	return(PVRSRV_OK);
}


IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVAllocSyncInfoKM(IMG_HANDLE					hDevCookie,
												IMG_HANDLE					hDevMemContext,
												PVRSRV_KERNEL_SYNC_INFO		**ppsKernelSyncInfo)
{
	IMG_HANDLE hSyncDevMemHeap;
	DEVICE_MEMORY_INFO *psDevMemoryInfo;
	BM_CONTEXT *pBMContext;
	PVRSRV_ERROR eError;
	PVRSRV_KERNEL_SYNC_INFO	*psKernelSyncInfo;
	PVRSRV_SYNC_DATA *psSyncData;

	eError = OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
						  sizeof(PVRSRV_KERNEL_SYNC_INFO ),
						  (IMG_VOID **)&psKernelSyncInfo, IMG_NULL);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVAllocSyncInfoKM: Failed to alloc memory"));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	
	pBMContext = (BM_CONTEXT*)hDevMemContext;
	psDevMemoryInfo = &pBMContext->psDeviceNode->sDevMemoryInfo;

	
	hSyncDevMemHeap = psDevMemoryInfo->psDeviceMemoryHeap[psDevMemoryInfo->ui32SyncHeapID].hDevMemHeap;

	eError = AllocDeviceMem(hDevCookie,
							hSyncDevMemHeap,
							0,
							sizeof(PVRSRV_SYNC_DATA),
							sizeof(IMG_UINT32),
							&psKernelSyncInfo->psSyncDataMemInfoKM);

	if (eError != PVRSRV_OK)
	{

		PVR_DPF((PVR_DBG_ERROR,"PVRSRVAllocSyncInfoKM: Failed to alloc memory"));
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, 0, psKernelSyncInfo, IMG_NULL);
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	
	psKernelSyncInfo->psSyncData = psKernelSyncInfo->psSyncDataMemInfoKM->pvLinAddrKM;
	psSyncData = psKernelSyncInfo->psSyncData;

	psSyncData->ui32WriteOpsPending = 0;
	psSyncData->ui32WriteOpsComplete = 0;
	psSyncData->ui32ReadOpsPending = 0;
	psSyncData->ui32ReadOpsComplete = 0;
	psSyncData->ui32LastOpDumpVal = 0;
	psSyncData->ui32LastReadOpDumpVal = 0;

	psKernelSyncInfo->sWriteOpsCompleteDevVAddr.uiAddr = psKernelSyncInfo->psSyncDataMemInfoKM->sDevVAddr.uiAddr + offsetof(PVRSRV_SYNC_DATA, ui32WriteOpsComplete);
	psKernelSyncInfo->sReadOpsCompleteDevVAddr.uiAddr = psKernelSyncInfo->psSyncDataMemInfoKM->sDevVAddr.uiAddr + offsetof(PVRSRV_SYNC_DATA, ui32ReadOpsComplete);

	
	psKernelSyncInfo->psSyncDataMemInfoKM->psKernelSyncInfo = IMG_NULL;

	
	*ppsKernelSyncInfo = psKernelSyncInfo;

	return PVRSRV_OK;
}


IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVFreeSyncInfoKM(PVRSRV_KERNEL_SYNC_INFO	*psKernelSyncInfo)
{
	FreeDeviceMem(psKernelSyncInfo->psSyncDataMemInfoKM);
	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, 0, psKernelSyncInfo, IMG_NULL);

	return PVRSRV_OK;
}


static PVRSRV_ERROR FreeDeviceMemCallBack(IMG_PVOID		pvParam,
										  IMG_UINT32	ui32Param)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVRSRV_KERNEL_MEM_INFO *psMemInfo = pvParam;

	PVR_UNREFERENCED_PARAMETER(ui32Param);

	if (psMemInfo->psKernelSyncInfo)
	{
		eError = PVRSRVFreeSyncInfoKM(psMemInfo->psKernelSyncInfo);
	}

	if (eError == PVRSRV_OK)
	{
		eError = FreeDeviceMem(psMemInfo);
	}

	return eError;
}


IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVFreeDeviceMemKM(IMG_HANDLE				hDevCookie,
												PVRSRV_KERNEL_MEM_INFO	*psMemInfo)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVR_UNREFERENCED_PARAMETER(hDevCookie);

	if (!psMemInfo)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if (psMemInfo->sMemBlk.hResItem != IMG_NULL)
	{
		eError = ResManFreeResByPtr(psMemInfo->sMemBlk.hResItem);	
	}
	else
	{
		
		FreeDeviceMemCallBack(psMemInfo, 0);
	}

	return eError;
}


IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVAllocDeviceMemKM(IMG_HANDLE					hDevCookie,
												 PVRSRV_PER_PROCESS_DATA	*psPerProc,
												 IMG_HANDLE					hDevMemHeap,
												 IMG_UINT32					ui32Flags,
												 IMG_UINT32					ui32Size,
												 IMG_UINT32					ui32Alignment,
												 PVRSRV_KERNEL_MEM_INFO		**ppsMemInfo)
{
	PVRSRV_KERNEL_MEM_INFO	*psMemInfo;
	PVRSRV_ERROR 			eError;
	BM_HEAP					*psBMHeap;
	IMG_HANDLE				hDevMemContext;

	if (!hDevMemHeap ||
		(ui32Size == 0))
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	eError = AllocDeviceMem(hDevCookie,
							hDevMemHeap,
							ui32Flags,
							ui32Size,
							ui32Alignment,
							&psMemInfo);

	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	if (ui32Flags & PVRSRV_MEM_NO_SYNCOBJ)
	{
		psMemInfo->psKernelSyncInfo = IMG_NULL;
	}
	else
	{
		


		psBMHeap = (BM_HEAP*)hDevMemHeap;
		hDevMemContext = (IMG_HANDLE)psBMHeap->pBMContext;
		eError = PVRSRVAllocSyncInfoKM(hDevCookie,
									   hDevMemContext,
									   &psMemInfo->psKernelSyncInfo);
		if(eError != PVRSRV_OK)
		{
			goto free_mainalloc;
		}
	}

	
	*ppsMemInfo = psMemInfo;

	if (ui32Flags & PVRSRV_MEM_NO_RESMAN)
	{
		psMemInfo->sMemBlk.hResItem = IMG_NULL;
	}
	else
	{
		
		psMemInfo->sMemBlk.hResItem = ResManRegisterRes(psPerProc->hResManContext,
														RESMAN_TYPE_DEVICEMEM_ALLOCATION,
														psMemInfo,
														0,
														FreeDeviceMemCallBack);
		if (psMemInfo->sMemBlk.hResItem == IMG_NULL)
		{
			
			eError = PVRSRV_ERROR_OUT_OF_MEMORY;
			goto free_mainalloc;
		}
	}		

	
	return (PVRSRV_OK);

free_mainalloc:
	FreeDeviceMem(psMemInfo);

	return eError;
}


IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVDissociateDeviceMemKM(IMG_HANDLE				hDevCookie,
													  PVRSRV_KERNEL_MEM_INFO	*psMemInfo)
{
	PVRSRV_ERROR		eError = PVRSRV_OK;
	PVRSRV_DEVICE_NODE	*psDeviceNode = hDevCookie;

	PVR_UNREFERENCED_PARAMETER(hDevCookie);

	if (!psMemInfo)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	ResManDissociateRes(psMemInfo->sMemBlk.hResItem, psDeviceNode->hResManContext);

	return eError;
}


IMG_EXPORT			
PVRSRV_ERROR IMG_CALLCONV PVRSRVGetFreeDeviceMemKM(IMG_UINT32 ui32Flags,
												   IMG_UINT32 *pui32Total,
												   IMG_UINT32 *pui32Free,
												   IMG_UINT32 *pui32LargestBlock)
{
	

	PVR_UNREFERENCED_PARAMETER(ui32Flags);
	PVR_UNREFERENCED_PARAMETER(pui32Total);
	PVR_UNREFERENCED_PARAMETER(pui32Free);
	PVR_UNREFERENCED_PARAMETER(pui32LargestBlock);

	return PVRSRV_OK;
}




IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVUnwrapExtMemoryKM (PVRSRV_KERNEL_MEM_INFO	*psMemInfo)
{
	if (!psMemInfo)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	return ResManFreeResByPtr(psMemInfo->sMemBlk.hResItem);
}


static PVRSRV_ERROR UnwrapExtMemoryCallBack(IMG_PVOID	pvParam,
											IMG_UINT32	ui32Param)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVRSRV_KERNEL_MEM_INFO *psMemInfo = pvParam;
	IMG_HANDLE hOSWrapMem;

	PVR_UNREFERENCED_PARAMETER(ui32Param);

	hOSWrapMem = psMemInfo->sMemBlk.hOSWrapMem;

	if (psMemInfo->psKernelSyncInfo)
	{
		eError = PVRSRVFreeSyncInfoKM(psMemInfo->psKernelSyncInfo);
	}

	
	if(psMemInfo->sMemBlk.psIntSysPAddr)
	{
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, 0, psMemInfo->sMemBlk.psIntSysPAddr, IMG_NULL);
	}	

	if (eError == PVRSRV_OK)
	{
		eError = FreeDeviceMem(psMemInfo);
	}

	if(hOSWrapMem)
	{
		OSReleasePhysPageAddr(hOSWrapMem);
	}

	return eError;
}


IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVWrapExtMemoryKM(IMG_HANDLE				hDevCookie,
												PVRSRV_PER_PROCESS_DATA	*psPerProc,
												IMG_UINT32 				ui32ByteSize,
												IMG_UINT32				ui32PageOffset,
												IMG_BOOL				bPhysContig,
												IMG_SYS_PHYADDR	 		*psExtSysPAddr,
												IMG_VOID 				*pvLinAddr,
												PVRSRV_KERNEL_MEM_INFO	**ppsMemInfo)
{
	PVRSRV_KERNEL_MEM_INFO *psMemInfo = IMG_NULL;
	DEVICE_MEMORY_INFO  *psDevMemoryInfo;
	IMG_UINT32			ui32HostPageSize = HOST_PAGESIZE();	
	IMG_HANDLE hDevMemHeap, hDevMemContext;
	PVRSRV_DEVICE_NODE* psDeviceNode;
	BM_HANDLE 			hBuffer;
	PVRSRV_MEMBLK		*psMemBlock;
	IMG_BOOL			bBMError;
	BM_HEAP				*psBMHeap;
	PVRSRV_ERROR		eError;
	IMG_VOID 			*pvPageAlignedCPUVAddr;
	IMG_SYS_PHYADDR	 	*psIntSysPAddr = IMG_NULL;
	IMG_HANDLE			hOSWrapMem = IMG_NULL;


	psDeviceNode = (PVRSRV_DEVICE_NODE*)hDevCookie;
	PVR_ASSERT(psDeviceNode != IMG_NULL);

	if(pvLinAddr)
	{
		IMG_UINT32 ui32PageCount;

		
		ui32PageCount = HOST_PAGEALIGN(ui32ByteSize + ui32PageOffset) / ui32HostPageSize;
		pvPageAlignedCPUVAddr = (IMG_VOID *)((IMG_UINT8 *)pvLinAddr - ui32PageOffset);
	
		
		if(OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
						ui32PageCount*sizeof(IMG_SYS_PHYADDR),
						(IMG_VOID **)&psIntSysPAddr, IMG_NULL) != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"PVRSRVWrapExtMemoryKM: Failed to alloc memory for block"));
			return PVRSRV_ERROR_OUT_OF_MEMORY;
		}

		eError = OSAcquirePhysPageAddr(pvPageAlignedCPUVAddr,
										ui32PageCount * ui32HostPageSize,
										psIntSysPAddr,
										&hOSWrapMem);
		if(eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"PVRSRVWrapExtMemoryKM: Failed to alloc memory for block"));
			eError = PVRSRV_ERROR_OUT_OF_MEMORY;
			goto ErrorExitPhase1;
		}

		
		psExtSysPAddr = psIntSysPAddr;
	}
	else
	{
		
	}

	
	psDevMemoryInfo = &psDeviceNode->sDevMemoryInfo;
	hDevMemHeap = psDevMemoryInfo->psDeviceMemoryHeap[psDevMemoryInfo->ui32MappingHeapID].hDevMemHeap;

	if(OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
					sizeof(PVRSRV_KERNEL_MEM_INFO ),
					(IMG_VOID **)&psMemInfo, IMG_NULL) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVWrapExtMemoryKM: Failed to alloc memory for block"));
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto ErrorExitPhase2;
	}

	OSMemSet(psMemInfo, 0, sizeof(*psMemInfo));

	psMemBlock = &(psMemInfo->sMemBlk);

	bBMError = BM_Wrap(hDevMemHeap,
					   ui32ByteSize,
					   ui32PageOffset,
					   bPhysContig,
					   psExtSysPAddr,
					   IMG_NULL,
					   &psMemInfo->ui32Flags,
					   &hBuffer);
	if (!bBMError)
	{
		/* Alloc failed from current mapping heap, try the other one */
		psDevMemoryInfo->ui32MappingHeapID = 
			psDevMemoryInfo->ui32MappingHeapID == SGX_GENERAL_MAPPING_HEAP_ID ?
			SGX_ALT_MAPPING_HEAP_ID :
			SGX_GENERAL_MAPPING_HEAP_ID;
		hDevMemHeap = psDevMemoryInfo->psDeviceMemoryHeap[psDevMemoryInfo->ui32MappingHeapID].hDevMemHeap;
		bBMError = BM_Wrap(hDevMemHeap,
					ui32ByteSize,
					ui32PageOffset,
					bPhysContig,
					psExtSysPAddr,
					IMG_NULL,
					&psMemInfo->ui32Flags,
					&hBuffer);
		if (!bBMError)
		{
			PVR_DPF((PVR_DBG_ERROR,"PVRSRVWrapExtMemoryKM: BM_Wrap Failed"));
			eError = PVRSRV_ERROR_BAD_MAPPING;
			goto ErrorExitPhase2;
		}		
	}

	
	psMemBlock->sDevVirtAddr = BM_HandleToDevVaddr(hBuffer);
	psMemBlock->hOSMemHandle = BM_HandleToOSMemHandle(hBuffer);
	psMemBlock->hOSWrapMem = hOSWrapMem;
	psMemBlock->psIntSysPAddr = psIntSysPAddr;

	
	psMemBlock->hBuffer = (IMG_HANDLE)hBuffer;

	
	psMemInfo->pvLinAddrKM = BM_HandleToCpuVaddr(hBuffer);
	psMemInfo->sDevVAddr = psMemBlock->sDevVirtAddr;
	psMemInfo->ui32AllocSize = ui32ByteSize;

	

	psMemInfo->pvSysBackupBuffer = IMG_NULL;

	


	psBMHeap = (BM_HEAP*)hDevMemHeap;
	hDevMemContext = (IMG_HANDLE)psBMHeap->pBMContext;
	eError = PVRSRVAllocSyncInfoKM(hDevCookie,
									hDevMemContext,
									&psMemInfo->psKernelSyncInfo);
	if(eError != PVRSRV_OK)
	{
		goto ErrorExitPhase2;
	}

	
	psMemInfo->sMemBlk.hResItem = ResManRegisterRes(psPerProc->hResManContext,
													RESMAN_TYPE_DEVICEMEM_WRAP,
													psMemInfo,
													0,
													UnwrapExtMemoryCallBack);

	
	*ppsMemInfo = psMemInfo;
	
	return PVRSRV_OK;

	

ErrorExitPhase2:
	if(psMemInfo)
	{
		FreeDeviceMem(psMemInfo);
	}

	if(psIntSysPAddr)
	{
		OSReleasePhysPageAddr(hOSWrapMem);
	}
	
ErrorExitPhase1:
	if(psIntSysPAddr)
	{
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, 0, psIntSysPAddr, IMG_NULL);
	}

	return eError;
}


IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVUnmapDeviceMemoryKM (PVRSRV_KERNEL_MEM_INFO *psMemInfo)
{
	if (!psMemInfo)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	return ResManFreeResByPtr(psMemInfo->sMemBlk.hResItem);
}


static PVRSRV_ERROR UnmapDeviceMemoryCallBack(IMG_PVOID pvParam, 
											  IMG_UINT32 ui32Param)
{
	PVRSRV_KERNEL_MEM_INFO *psMemInfo = pvParam;

	PVR_UNREFERENCED_PARAMETER(ui32Param);

	if(psMemInfo->sMemBlk.psIntSysPAddr)
	{
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, 0, psMemInfo->sMemBlk.psIntSysPAddr, IMG_NULL);
	}

	return FreeDeviceMem(psMemInfo);
}


IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVMapDeviceMemoryKM(PVRSRV_PER_PROCESS_DATA	*psPerProc,
												  PVRSRV_KERNEL_MEM_INFO	*psSrcMemInfo,
												  IMG_HANDLE				hDstDevMemHeap,
												  PVRSRV_KERNEL_MEM_INFO	**ppsDstMemInfo)
{
	PVRSRV_ERROR		eError;
	IMG_UINT32			i;
	IMG_UINT32			ui32PageCount, ui32PageOffset;
	IMG_UINT32			ui32HostPageSize = HOST_PAGESIZE();
	IMG_SYS_PHYADDR		*psSysPAddr = IMG_NULL;
	IMG_DEV_PHYADDR		sDevPAddr;
	BM_BUF				*psBuf;
	IMG_DEV_VIRTADDR 	sDevVAddr;
	PVRSRV_KERNEL_MEM_INFO *psMemInfo = IMG_NULL;
	BM_HANDLE 			hBuffer;
	PVRSRV_MEMBLK		*psMemBlock;
	IMG_BOOL			bBMError;
	PVRSRV_DEVICE_NODE	*psDeviceNode;
	IMG_VOID 			*pvPageAlignedCPUVAddr;	

	
	if(!psSrcMemInfo || !hDstDevMemHeap || !ppsDstMemInfo)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVMapDeviceMemoryKM: invalid parameters"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	
	*ppsDstMemInfo = IMG_NULL;
	
	ui32PageOffset = psSrcMemInfo->sDevVAddr.uiAddr & (ui32HostPageSize - 1);
	ui32PageCount = HOST_PAGEALIGN(psSrcMemInfo->ui32AllocSize + ui32PageOffset) / ui32HostPageSize;
	pvPageAlignedCPUVAddr = (IMG_VOID *)((IMG_UINT8 *)psSrcMemInfo->pvLinAddrKM - ui32PageOffset);

	



	if(OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
					ui32PageCount*sizeof(IMG_SYS_PHYADDR),
					(IMG_VOID **)&psSysPAddr, IMG_NULL) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVMapDeviceMemoryKM: Failed to alloc memory for block"));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	psBuf = psSrcMemInfo->sMemBlk.hBuffer;

	
	psDeviceNode = psBuf->pMapping->pBMHeap->pBMContext->psDeviceNode;

	
	sDevVAddr.uiAddr = psSrcMemInfo->sDevVAddr.uiAddr - ui32PageOffset;
	for(i=0; i<ui32PageCount; i++)
	{
		eError = BM_GetPhysPageAddr(psSrcMemInfo, sDevVAddr, &sDevPAddr);
		if(eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"PVRSRVMapDeviceMemoryKM: Failed to retrieve page list from device"));
			goto ErrorExit;
		}

		
		psSysPAddr[i] = SysDevPAddrToSysPAddr (psDeviceNode->sDevId.eDeviceType, sDevPAddr);

		
		sDevVAddr.uiAddr += ui32HostPageSize;
	}

	if(OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
					sizeof(PVRSRV_KERNEL_MEM_INFO ),
					(IMG_VOID **)&psMemInfo, IMG_NULL) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVMapDeviceMemoryKM: Failed to alloc memory for block"));
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto ErrorExit;
	}

	OSMemSet(psMemInfo, 0, sizeof(*psMemInfo));

	psMemBlock = &(psMemInfo->sMemBlk);

	bBMError = BM_Wrap(psBuf->pMapping->pBMHeap,
					   psSrcMemInfo->ui32AllocSize,
					   ui32PageOffset,
					   IMG_FALSE,
					   psSysPAddr,
					   pvPageAlignedCPUVAddr,
					   &psMemInfo->ui32Flags,
					   &hBuffer);

	if (!bBMError)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVMapDeviceMemoryKM: BM_Wrap Failed"));
		eError = PVRSRV_ERROR_BAD_MAPPING;
		goto ErrorExit;		
	}

	
	psMemBlock->sDevVirtAddr = BM_HandleToDevVaddr(hBuffer);
	psMemBlock->hOSMemHandle = BM_HandleToOSMemHandle(hBuffer);

	
	psMemBlock->hBuffer = (IMG_HANDLE)hBuffer;

	
	psMemBlock->psIntSysPAddr = psSysPAddr;
	
	
	psMemInfo->pvLinAddrKM = psSrcMemInfo->pvLinAddrKM;

	
	psMemInfo->sDevVAddr = psMemBlock->sDevVirtAddr;
	psMemInfo->ui32AllocSize = psSrcMemInfo->ui32AllocSize;
	psMemInfo->psKernelSyncInfo = psSrcMemInfo->psKernelSyncInfo;

	

	psMemInfo->pvSysBackupBuffer = IMG_NULL;

	
	psMemInfo->sMemBlk.hResItem = ResManRegisterRes(psPerProc->hResManContext,
													RESMAN_TYPE_DEVICEMEM_MAPPING,
													psMemInfo,
													0,
													UnmapDeviceMemoryCallBack);

	*ppsDstMemInfo = psMemInfo;

	return PVRSRV_OK;

	
	
ErrorExit:

	if(psSysPAddr)
	{
		
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, 0, psSysPAddr, IMG_NULL);
	}

	if(psMemInfo)
	{
		
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, 0, psMemInfo, IMG_NULL);
	}

	return eError;
}


IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVUnmapDeviceClassMemoryKM(PVRSRV_KERNEL_MEM_INFO *psMemInfo)
{
	if (!psMemInfo)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	return ResManFreeResByPtr(psMemInfo->sMemBlk.hResItem);
}


static PVRSRV_ERROR UnmapDeviceClassMemoryCallBack(IMG_PVOID	pvParam, 
												   IMG_UINT32	ui32Param)
{
	PVRSRV_KERNEL_MEM_INFO *psMemInfo = pvParam;

	PVR_UNREFERENCED_PARAMETER(ui32Param);

	return FreeDeviceMem(psMemInfo);
}


IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVMapDeviceClassMemoryKM(PVRSRV_PER_PROCESS_DATA	*psPerProc,
													   IMG_HANDLE				hDeviceClassBuffer,
													   PVRSRV_KERNEL_MEM_INFO	**ppsMemInfo,
													   IMG_HANDLE				*phOSMapInfo)
{
	PVRSRV_ERROR eError;
	PVRSRV_KERNEL_MEM_INFO *psMemInfo;
	PVRSRV_DEVICECLASS_BUFFER *psDeviceClassBuffer;
	IMG_SYS_PHYADDR *psSysPAddr;
	IMG_VOID *pvCPUVAddr, *pvPageAlignedCPUVAddr;
	IMG_BOOL bPhysContig;
	BM_CONTEXT *psBMContext;
	DEVICE_MEMORY_INFO *psDevMemoryInfo;
	IMG_HANDLE hDevMemHeap;
	IMG_UINT32 ui32ByteSize;
	IMG_UINT32 ui32Offset;
	IMG_UINT32 ui32PageSize = HOST_PAGESIZE();
	BM_HANDLE		hBuffer;
	PVRSRV_MEMBLK	*psMemBlock;
	IMG_BOOL		bBMError;

	if(!hDeviceClassBuffer || !ppsMemInfo || !phOSMapInfo)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVMapDeviceClassMemoryKM: invalid parameters"));
		return PVRSRV_ERROR_INVALID_PARAMS;		
	}
	
	psDeviceClassBuffer = (PVRSRV_DEVICECLASS_BUFFER*)hDeviceClassBuffer;
	
	


















	eError = psDeviceClassBuffer->pfnGetBufferAddr(psDeviceClassBuffer->hExtDevice,
												   psDeviceClassBuffer->hExtBuffer,
												   &psSysPAddr,
												   &ui32ByteSize,
												   &pvCPUVAddr,
												   phOSMapInfo,
												   &bPhysContig);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVMapDeviceClassMemoryKM: unable to get buffer address"));	
		return PVRSRV_ERROR_GENERIC;
	}

	
	psBMContext = (BM_CONTEXT*)psDeviceClassBuffer->hDevMemContext;
	psDevMemoryInfo = &psBMContext->psDeviceNode->sDevMemoryInfo;
	hDevMemHeap = psDevMemoryInfo->psDeviceMemoryHeap[psDevMemoryInfo->ui32MappingHeapID].hDevMemHeap;

	
	ui32Offset = ((IMG_UINT32)pvCPUVAddr) & (ui32PageSize - 1);
	pvPageAlignedCPUVAddr = (IMG_VOID *)((IMG_UINT8 *)pvCPUVAddr - ui32Offset);

	if(OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
					sizeof(PVRSRV_KERNEL_MEM_INFO ),
					(IMG_VOID **)&psMemInfo, IMG_NULL) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVMapDeviceClassMemoryKM: Failed to alloc memory for block"));
		return (PVRSRV_ERROR_OUT_OF_MEMORY);
	}

	OSMemSet(psMemInfo, 0, sizeof(*psMemInfo));

	psMemBlock = &(psMemInfo->sMemBlk);

	bBMError = BM_Wrap(hDevMemHeap,
					   ui32ByteSize,
					   ui32Offset,
					   bPhysContig,
					   psSysPAddr,
					   pvPageAlignedCPUVAddr,
					   &psMemInfo->ui32Flags,
					   &hBuffer);

	if (!bBMError)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVMapDeviceClassMemoryKM: BM_Wrap Failed"));
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, 0, psMemInfo, IMG_NULL);
		return PVRSRV_ERROR_BAD_MAPPING;
	}

	
	psMemBlock->sDevVirtAddr = BM_HandleToDevVaddr(hBuffer);
	psMemBlock->hOSMemHandle = BM_HandleToOSMemHandle(hBuffer);

	
	psMemBlock->hBuffer = (IMG_HANDLE)hBuffer;

	

	psMemInfo->pvLinAddrKM = BM_HandleToCpuVaddr(hBuffer);
	
	
	psMemInfo->sDevVAddr = psMemBlock->sDevVirtAddr;
	psMemInfo->ui32AllocSize = ui32ByteSize;
	psMemInfo->psKernelSyncInfo = psDeviceClassBuffer->psKernelSyncInfo;

	

	psMemInfo->pvSysBackupBuffer = IMG_NULL;

	
	psMemInfo->sMemBlk.hResItem = ResManRegisterRes(psPerProc->hResManContext,
													RESMAN_TYPE_DEVICECLASSMEM_MAPPING,
													psMemInfo,
													0,
													UnmapDeviceClassMemoryCallBack);

	
	*ppsMemInfo = psMemInfo;

	return PVRSRV_OK;	
}

