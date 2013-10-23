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
#include "buffer_manager.h"
#include "kernelbuffer.h"
#include "pvr_bridge_km.h"


PVRSRV_ERROR AllocateDeviceID(SYS_DATA *psSysData, IMG_UINT32 *pui32DevID);
PVRSRV_ERROR FreeDeviceID(SYS_DATA *psSysData, IMG_UINT32 ui32DevID);

typedef struct PVRSRV_DC_SRV2DISP_KMJTABLE_TAG *PPVRSRV_DC_SRV2DISP_KMJTABLE;

typedef struct PVRSRV_DC_BUFFER_TAG
{
	
	PVRSRV_DEVICECLASS_BUFFER sDeviceClassBuffer;

	struct PVRSRV_DISPLAYCLASS_INFO_TAG *psDCInfo;
	struct PVRSRV_DC_SWAPCHAIN_TAG *psSwapChain;
} PVRSRV_DC_BUFFER;

typedef struct PVRSRV_DC_SWAPCHAIN_TAG
{
	IMG_HANDLE							hExtSwapChain;
	PVRSRV_QUEUE_INFO					*psQueue;
	PVRSRV_DC_BUFFER					asBuffer[PVRSRV_MAX_DC_SWAPCHAIN_BUFFERS];
	IMG_UINT32							ui32BufferCount;
	PVRSRV_DC_BUFFER					*psLastFlipBuffer;
	struct PVRSRV_DISPLAYCLASS_INFO_TAG *psDCInfo;
	IMG_HANDLE							hResItem;
} PVRSRV_DC_SWAPCHAIN;

typedef struct PVRSRV_DISPLAYCLASS_INFO_TAG
{
	IMG_UINT32 							ui32RefCount;
	IMG_UINT32							ui32DeviceID;
	IMG_HANDLE							hExtDevice;
	PPVRSRV_DC_SRV2DISP_KMJTABLE		psFuncTable;
	IMG_HANDLE							hDevMemContext;
	PVRSRV_DC_BUFFER 					sSystemBuffer;
} PVRSRV_DISPLAYCLASS_INFO;


typedef struct PVRSRV_DISPLAYCLASS_PERCONTEXT_INFO_TAG
{
	PVRSRV_DISPLAYCLASS_INFO			*psDCInfo;
	PRESMAN_ITEM						hResItem;
} PVRSRV_DISPLAYCLASS_PERCONTEXT_INFO;


typedef struct PVRSRV_BC_SRV2BUFFER_KMJTABLE_TAG *PPVRSRV_BC_SRV2BUFFER_KMJTABLE;

typedef struct PVRSRV_BC_BUFFER_TAG
{
	
	PVRSRV_DEVICECLASS_BUFFER sDeviceClassBuffer;

	struct PVRSRV_BUFFERCLASS_INFO_TAG *psBCInfo;
} PVRSRV_BC_BUFFER;


typedef struct PVRSRV_BUFFERCLASS_INFO_TAG
{
	IMG_UINT32 							ui32RefCount;
	IMG_UINT32							ui32DeviceID;
	IMG_HANDLE							hExtDevice;
	PPVRSRV_BC_SRV2BUFFER_KMJTABLE		psFuncTable;
	IMG_HANDLE							hDevMemContext;
	
	IMG_UINT32							ui32BufferCount;
	PVRSRV_BC_BUFFER 					*psBuffer;

} PVRSRV_BUFFERCLASS_INFO;


typedef struct PVRSRV_BUFFERCLASS_PERCONTEXT_INFO_TAG
{
	PVRSRV_BUFFERCLASS_INFO				*psBCInfo;
	IMG_HANDLE							hResItem;
} PVRSRV_BUFFERCLASS_PERCONTEXT_INFO;


static PVRSRV_DISPLAYCLASS_INFO* DCDeviceHandleToDCInfo (IMG_HANDLE hDeviceKM)
{
	PVRSRV_DISPLAYCLASS_PERCONTEXT_INFO *psDCPerContextInfo;

	psDCPerContextInfo = (PVRSRV_DISPLAYCLASS_PERCONTEXT_INFO *)hDeviceKM;

	return psDCPerContextInfo->psDCInfo;
}


static PVRSRV_BUFFERCLASS_INFO* BCDeviceHandleToBCInfo (IMG_HANDLE hDeviceKM)
{
	PVRSRV_BUFFERCLASS_PERCONTEXT_INFO *psBCPerContextInfo;

	psBCPerContextInfo = (PVRSRV_BUFFERCLASS_PERCONTEXT_INFO *)hDeviceKM;

	return psBCPerContextInfo->psBCInfo;
}


IMG_EXPORT
PVRSRV_ERROR PVRSRVEnumerateDCKM (PVRSRV_DEVICE_CLASS DeviceClass,
								  IMG_UINT32 *pui32DevCount,
								  IMG_UINT32 *pui32DevID )
{
	PVRSRV_DEVICE_NODE	*psDeviceNode;
	IMG_UINT			ui32DevCount = 0;
	SYS_DATA 			*psSysData;

	if (SysAcquireData(&psSysData) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVEnumerateDCKM: Failed to get SysData"));
		return PVRSRV_ERROR_GENERIC;
	}

	

	psDeviceNode = psSysData->psDeviceNodeList;
	while(psDeviceNode)
	{
		if	((psDeviceNode->sDevId.eDeviceClass == DeviceClass)
		&&	(psDeviceNode->sDevId.eDeviceType == PVRSRV_DEVICE_TYPE_EXT))
		{
			ui32DevCount++;
			if(pui32DevID)
			{
				*pui32DevID++ = psDeviceNode->sDevId.ui32DeviceIndex;
			}
		}
		psDeviceNode = psDeviceNode->psNext;
	}

	if(pui32DevCount)
	{
		*pui32DevCount = ui32DevCount;
	}
	else if(pui32DevID == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVEnumerateDCKM: Invalid parameters"));
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}

	return PVRSRV_OK;
}


PVRSRV_ERROR PVRSRVRegisterDCDeviceKM (PVRSRV_DC_SRV2DISP_KMJTABLE *psFuncTable,
									   IMG_UINT32 *pui32DeviceID)
{
	PVRSRV_DISPLAYCLASS_INFO 	*psDCInfo = IMG_NULL;
	PVRSRV_DEVICE_NODE			*psDeviceNode;
	SYS_DATA					*psSysData;

	














	if (SysAcquireData(&psSysData) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRegisterDCDeviceKM: Failed to get SysData"));
		return PVRSRV_ERROR_GENERIC;
	}

	


	
	
	if(OSAllocMem( PVRSRV_OS_PAGEABLE_HEAP,
					 sizeof(*psDCInfo),
					 (IMG_VOID **)&psDCInfo, IMG_NULL) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRegisterDCDeviceKM: Failed psDCInfo alloc"));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
	OSMemSet (psDCInfo, 0, sizeof(*psDCInfo));

	
	if(OSAllocMem( PVRSRV_OS_PAGEABLE_HEAP,
					 sizeof(PVRSRV_DC_SRV2DISP_KMJTABLE),
					 (IMG_VOID **)&psDCInfo->psFuncTable, IMG_NULL) != PVRSRV_OK)
	{		
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRegisterDCDeviceKM: Failed psFuncTable alloc"));
		goto ErrorExit;
	}
	OSMemSet (psDCInfo->psFuncTable, 0, sizeof(PVRSRV_DC_SRV2DISP_KMJTABLE));

	
	*psDCInfo->psFuncTable = *psFuncTable;

	
	if(OSAllocMem( PVRSRV_OS_PAGEABLE_HEAP,
					 sizeof(PVRSRV_DEVICE_NODE),
					 (IMG_VOID **)&psDeviceNode, IMG_NULL) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRegisterDCDeviceKM: Failed psDeviceNode alloc"));
		goto ErrorExit;
	}
	OSMemSet (psDeviceNode, 0, sizeof(PVRSRV_DEVICE_NODE));

	psDeviceNode->pvDevice = (IMG_VOID*)psDCInfo;
	psDeviceNode->ui32pvDeviceSize = sizeof(*psDCInfo);
	psDeviceNode->ui32RefCount = 1;
	psDeviceNode->sDevId.eDeviceType = PVRSRV_DEVICE_TYPE_EXT;
	psDeviceNode->sDevId.eDeviceClass = PVRSRV_DEVICE_CLASS_DISPLAY;
	psDeviceNode->psSysData = psSysData;

	
	AllocateDeviceID(psSysData, &psDeviceNode->sDevId.ui32DeviceIndex);
	psDCInfo->ui32DeviceID = psDeviceNode->sDevId.ui32DeviceIndex;
	if (pui32DeviceID)
	{
		*pui32DeviceID = psDeviceNode->sDevId.ui32DeviceIndex;
	}
	
	
	SysRegisterExternalDevice(psDeviceNode);

	
	psDeviceNode->psNext = psSysData->psDeviceNodeList;
	psSysData->psDeviceNodeList = psDeviceNode;

	return PVRSRV_OK;

ErrorExit:

	if(psDCInfo->psFuncTable)
	{
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PVRSRV_DC_SRV2DISP_KMJTABLE),
				psDCInfo->psFuncTable, IMG_NULL);
	}
	
	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PVRSRV_DISPLAYCLASS_INFO),
			psDCInfo, IMG_NULL);

	return PVRSRV_ERROR_OUT_OF_MEMORY;
}


PVRSRV_ERROR PVRSRVRemoveDCDeviceKM(IMG_UINT32 ui32DevIndex)
{
	SYS_DATA					*psSysData;
	PVRSRV_DEVICE_NODE			**ppsDeviceNode, *psDeviceNode;
	PVRSRV_DISPLAYCLASS_INFO	*psDCInfo;

	if (SysAcquireData(&psSysData) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRemoveDCDeviceKM: Failed to get SysData"));
		return PVRSRV_ERROR_GENERIC;
	}

	ppsDeviceNode = &psSysData->psDeviceNodeList;
	while(*ppsDeviceNode)
	{
		switch((*ppsDeviceNode)->sDevId.eDeviceClass)
		{
			case PVRSRV_DEVICE_CLASS_DISPLAY :
			{
				if((*ppsDeviceNode)->sDevId.ui32DeviceIndex == ui32DevIndex)
				{
					goto FoundDevice;
				}
				break;
			}
			default:
			{
				break;
			}
		}
		ppsDeviceNode = &((*ppsDeviceNode)->psNext);
	}

	PVR_DPF((PVR_DBG_ERROR,"PVRSRVRemoveDCDeviceKM: requested device %d not present", ui32DevIndex));

	return PVRSRV_ERROR_GENERIC;

FoundDevice:
	
	psDeviceNode = *ppsDeviceNode;
	*ppsDeviceNode = psDeviceNode->psNext;

	
	SysRemoveExternalDevice(psDeviceNode);
	
	


	psDCInfo = (PVRSRV_DISPLAYCLASS_INFO*)psDeviceNode->pvDevice;
	PVR_ASSERT(psDCInfo->ui32RefCount == 0);
	FreeDeviceID(psSysData, ui32DevIndex);
	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PVRSRV_DC_SRV2DISP_KMJTABLE),
			psDCInfo->psFuncTable, IMG_NULL);
	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PVRSRV_DISPLAYCLASS_INFO),
			psDCInfo, IMG_NULL);
	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PVRSRV_DEVICE_NODE),
			psDeviceNode, IMG_NULL);
	return PVRSRV_OK;
}


PVRSRV_ERROR PVRSRVRegisterBCDeviceKM (PVRSRV_BC_SRV2BUFFER_KMJTABLE *psFuncTable,
									   IMG_UINT32	*pui32DeviceID)
{
	PVRSRV_BUFFERCLASS_INFO	*psBCInfo = IMG_NULL;
	PVRSRV_DEVICE_NODE		*psDeviceNode;
	SYS_DATA				*psSysData;
	













	if (SysAcquireData(&psSysData) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRegisterBCDeviceKM: Failed to get SysData"));
		return PVRSRV_ERROR_GENERIC;
	}

	


	
	if(OSAllocMem( PVRSRV_OS_PAGEABLE_HEAP,
					 sizeof(*psBCInfo),
					 (IMG_VOID **)&psBCInfo, IMG_NULL) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRegisterBCDeviceKM: Failed psBCInfo alloc"));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
	OSMemSet (psBCInfo, 0, sizeof(*psBCInfo));	

	
	if(OSAllocMem( PVRSRV_OS_PAGEABLE_HEAP,
					 sizeof(PVRSRV_BC_SRV2BUFFER_KMJTABLE),
					 (IMG_VOID **)&psBCInfo->psFuncTable, IMG_NULL) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRegisterBCDeviceKM: Failed psFuncTable alloc"));
		goto ErrorExit;
	}
	OSMemSet (psBCInfo->psFuncTable, 0, sizeof(PVRSRV_BC_SRV2BUFFER_KMJTABLE));

	
	*psBCInfo->psFuncTable = *psFuncTable;

	
	if(OSAllocMem( PVRSRV_OS_PAGEABLE_HEAP,
					 sizeof(PVRSRV_DEVICE_NODE),
					 (IMG_VOID **)&psDeviceNode, IMG_NULL) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRegisterBCDeviceKM: Failed psDeviceNode alloc"));
		goto ErrorExit;
	}
	OSMemSet (psDeviceNode, 0, sizeof(PVRSRV_DEVICE_NODE));

	psDeviceNode->pvDevice = (IMG_VOID*)psBCInfo;
	psDeviceNode->ui32pvDeviceSize = sizeof(*psBCInfo);
	psDeviceNode->ui32RefCount = 1;
	psDeviceNode->sDevId.eDeviceType = PVRSRV_DEVICE_TYPE_EXT;
	psDeviceNode->sDevId.eDeviceClass = PVRSRV_DEVICE_CLASS_BUFFER;
	psDeviceNode->psSysData = psSysData;

	
	AllocateDeviceID(psSysData, &psDeviceNode->sDevId.ui32DeviceIndex);
	psBCInfo->ui32DeviceID = psDeviceNode->sDevId.ui32DeviceIndex;
	if (pui32DeviceID)
	{
		*pui32DeviceID = psDeviceNode->sDevId.ui32DeviceIndex;
	}

	
	psDeviceNode->psNext = psSysData->psDeviceNodeList;
	psSysData->psDeviceNodeList = psDeviceNode;

	return PVRSRV_OK;

ErrorExit:

	if(psBCInfo->psFuncTable)
	{
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PVRSRV_BC_SRV2BUFFER_KMJTABLE),
				psBCInfo->psFuncTable, IMG_NULL);
	}

	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PVRSRV_BUFFERCLASS_INFO),
			psBCInfo, IMG_NULL);

	return PVRSRV_ERROR_OUT_OF_MEMORY;	
}


PVRSRV_ERROR PVRSRVRemoveBCDeviceKM(IMG_UINT32 ui32DevIndex)
{
	SYS_DATA					*psSysData;
	PVRSRV_DEVICE_NODE			**ppsDevNode, *psDevNode;
	PVRSRV_BUFFERCLASS_INFO		*psBCInfo;

	if (SysAcquireData(&psSysData) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRemoveBCDeviceKM: Failed to get SysData"));
		return PVRSRV_ERROR_GENERIC;
	}

	ppsDevNode = &psSysData->psDeviceNodeList;
	while(*ppsDevNode)
	{
		switch((*ppsDevNode)->sDevId.eDeviceClass)
		{
			case PVRSRV_DEVICE_CLASS_BUFFER :
			{
				if((*ppsDevNode)->sDevId.ui32DeviceIndex == ui32DevIndex)
				{
					goto FoundDevice;
				}
				break;
			}
			default:
			{
				break;
			}
		}
		ppsDevNode = &(*ppsDevNode)->psNext;
	}

	PVR_DPF((PVR_DBG_ERROR,"PVRSRVRemoveBCDeviceKM: requested device %d not present", ui32DevIndex));

	return PVRSRV_ERROR_GENERIC;

FoundDevice:
	
	psDevNode = *(ppsDevNode);
	*ppsDevNode = psDevNode->psNext;

	


	FreeDeviceID(psSysData, ui32DevIndex);
	psBCInfo = (PVRSRV_BUFFERCLASS_INFO*)psDevNode->pvDevice;
	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PVRSRV_BC_SRV2BUFFER_KMJTABLE),
			psBCInfo->psFuncTable, IMG_NULL);
	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PVRSRV_BUFFERCLASS_INFO),
			psBCInfo, IMG_NULL);
	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PVRSRV_DEVICE_NODE),
			psDevNode, IMG_NULL);
	return PVRSRV_OK;
}



IMG_EXPORT
PVRSRV_ERROR PVRSRVCloseDCDeviceKM (IMG_HANDLE	hDeviceKM,
									IMG_BOOL	bResManCallback)
{
	PVRSRV_ERROR eError;
	PVRSRV_DISPLAYCLASS_PERCONTEXT_INFO *psDCPerContextInfo;

	PVR_UNREFERENCED_PARAMETER(bResManCallback);

	psDCPerContextInfo = (PVRSRV_DISPLAYCLASS_PERCONTEXT_INFO *)hDeviceKM;

	
	eError = ResManFreeResByPtr(psDCPerContextInfo->hResItem);
			
	return eError;
}
		

static PVRSRV_ERROR CloseDCDeviceCallBack(IMG_PVOID		pvParam,
										  IMG_UINT32	ui32Param)
{
	PVRSRV_DISPLAYCLASS_PERCONTEXT_INFO *psDCPerContextInfo;
	PVRSRV_DISPLAYCLASS_INFO *psDCInfo;

	PVR_UNREFERENCED_PARAMETER(ui32Param);

	psDCPerContextInfo = (PVRSRV_DISPLAYCLASS_PERCONTEXT_INFO *)pvParam;
	psDCInfo = psDCPerContextInfo->psDCInfo;

	psDCInfo->ui32RefCount--;
	if(psDCInfo->ui32RefCount == 0)
	{	
		
		psDCInfo->psFuncTable->pfnCloseDCDevice(psDCInfo->hExtDevice);

		PVRSRVFreeSyncInfoKM(psDCInfo->sSystemBuffer.sDeviceClassBuffer.psKernelSyncInfo);
		
		psDCInfo->hDevMemContext = IMG_NULL;
		psDCInfo->hExtDevice = IMG_NULL;
	}

	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PVRSRV_DISPLAYCLASS_PERCONTEXT_INFO),
			psDCPerContextInfo, IMG_NULL);

	return PVRSRV_OK;
}


IMG_EXPORT
PVRSRV_ERROR PVRSRVOpenDCDeviceKM (PVRSRV_PER_PROCESS_DATA	*psPerProc,
								   IMG_UINT32				ui32DeviceID,
								   IMG_HANDLE				hDevCookie,
								   IMG_HANDLE				*phDeviceKM)
{
	PVRSRV_DISPLAYCLASS_INFO *psDCInfo;
	PVRSRV_DISPLAYCLASS_PERCONTEXT_INFO *psDCPerContextInfo;
	PVRSRV_DEVICE_NODE	*psDeviceNode;
	SYS_DATA			*psSysData;

	if(!phDeviceKM)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVOpenDCDeviceKM: Invalid params"));
		return PVRSRV_ERROR_GENERIC;
	}

	if (SysAcquireData(&psSysData) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVOpenDCDeviceKM: Failed to get SysData"));
		return PVRSRV_ERROR_GENERIC;
	}
	
	psDeviceNode = psSysData->psDeviceNodeList;
	while(psDeviceNode)
	{
		if ((psDeviceNode->sDevId.eDeviceClass == PVRSRV_DEVICE_CLASS_DISPLAY) &&
			(psDeviceNode->sDevId.ui32DeviceIndex == ui32DeviceID))
		{
			


			psDCInfo = (PVRSRV_DISPLAYCLASS_INFO*)psDeviceNode->pvDevice;
			goto FoundDevice;
		}
		psDeviceNode = psDeviceNode->psNext;
	}

	PVR_DPF((PVR_DBG_ERROR,"PVRSRVOpenDCDeviceKM: no devnode matching index %d", ui32DeviceID));

	return PVRSRV_ERROR_GENERIC;

FoundDevice:

	


	if(OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
				  sizeof(*psDCPerContextInfo),
				  (IMG_VOID **)&psDCPerContextInfo, IMG_NULL) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVOpenDCDeviceKM: Failed psDCPerContextInfo alloc"));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
	OSMemSet(psDCPerContextInfo, 0, sizeof(*psDCPerContextInfo));

	if(psDCInfo->ui32RefCount++ == 0)
	{
		PVRSRV_ERROR eError;

		psDeviceNode = (PVRSRV_DEVICE_NODE *)hDevCookie;
		PVR_ASSERT(psDeviceNode != IMG_NULL);

		
		psDCInfo->hDevMemContext = (IMG_HANDLE)psDeviceNode->sDevMemoryInfo.pBMKernelContext;

		
		eError = PVRSRVAllocSyncInfoKM(IMG_NULL, 
									(IMG_HANDLE)psDeviceNode->sDevMemoryInfo.pBMKernelContext,
									&psDCInfo->sSystemBuffer.sDeviceClassBuffer.psKernelSyncInfo);
		if(eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"PVRSRVOpenDCDeviceKM: Failed sync info alloc"));
			psDCInfo->ui32RefCount--;
			return eError;
		}

		
		eError = psDCInfo->psFuncTable->pfnOpenDCDevice(ui32DeviceID,
                                                        	&psDCInfo->hExtDevice,
								(PVRSRV_SYNC_DATA*)psDCInfo->sSystemBuffer.sDeviceClassBuffer.psKernelSyncInfo->psSyncDataMemInfoKM->pvLinAddrKM);
		if(eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"PVRSRVOpenDCDeviceKM: Failed to open external DC device"));
			psDCInfo->ui32RefCount--;
			PVRSRVFreeSyncInfoKM(psDCInfo->sSystemBuffer.sDeviceClassBuffer.psKernelSyncInfo);
			return eError;
		}
	}

	psDCPerContextInfo->psDCInfo = psDCInfo;
	psDCPerContextInfo->hResItem = ResManRegisterRes(psPerProc->hResManContext,
													 RESMAN_TYPE_DISPLAYCLASS_DEVICE,
													 psDCPerContextInfo,
													 0,
													 CloseDCDeviceCallBack);

	
	*phDeviceKM = (IMG_HANDLE)psDCPerContextInfo;

	return PVRSRV_OK;
}


IMG_EXPORT
PVRSRV_ERROR PVRSRVEnumDCFormatsKM (IMG_HANDLE hDeviceKM,
									IMG_UINT32 *pui32Count,
									DISPLAY_FORMAT *psFormat)
{
	PVRSRV_DISPLAYCLASS_INFO *psDCInfo;

	if(!hDeviceKM || !pui32Count || !psFormat)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVEnumDCFormatsKM: Invalid parameters"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDCInfo = DCDeviceHandleToDCInfo(hDeviceKM);

	
	return psDCInfo->psFuncTable->pfnEnumDCFormats(psDCInfo->hExtDevice, pui32Count, psFormat);
}



IMG_EXPORT
PVRSRV_ERROR PVRSRVEnumDCDimsKM (IMG_HANDLE hDeviceKM,
								 DISPLAY_FORMAT *psFormat,
								 IMG_UINT32 *pui32Count,
								 DISPLAY_DIMS *psDim)
{
	PVRSRV_DISPLAYCLASS_INFO *psDCInfo;

	if(!hDeviceKM || !pui32Count || !psFormat)	
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVEnumDCDimsKM: Invalid parameters"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDCInfo = DCDeviceHandleToDCInfo(hDeviceKM);

	
	return psDCInfo->psFuncTable->pfnEnumDCDims(psDCInfo->hExtDevice, psFormat, pui32Count, psDim);
}


IMG_EXPORT
PVRSRV_ERROR PVRSRVGetDCSystemBufferKM (IMG_HANDLE hDeviceKM,
										IMG_HANDLE *phBuffer)
{
	PVRSRV_ERROR eError;
	PVRSRV_DISPLAYCLASS_INFO *psDCInfo;
	IMG_HANDLE hExtBuffer;

	if(!hDeviceKM || !phBuffer)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVGetDCSystemBufferKM: Invalid parameters"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDCInfo = DCDeviceHandleToDCInfo(hDeviceKM);

	
	eError = psDCInfo->psFuncTable->pfnGetDCSystemBuffer(psDCInfo->hExtDevice, &hExtBuffer);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVGetDCSystemBufferKM: Failed to get valid buffer handle from external driver"));
		return eError;		
	}

	
	psDCInfo->sSystemBuffer.sDeviceClassBuffer.pfnGetBufferAddr = psDCInfo->psFuncTable->pfnGetBufferAddr;
	psDCInfo->sSystemBuffer.sDeviceClassBuffer.hDevMemContext = psDCInfo->hDevMemContext;
	psDCInfo->sSystemBuffer.sDeviceClassBuffer.hExtDevice = psDCInfo->hExtDevice;
	psDCInfo->sSystemBuffer.sDeviceClassBuffer.hExtBuffer = hExtBuffer;

	psDCInfo->sSystemBuffer.psDCInfo = psDCInfo;

	
	*phBuffer = (IMG_HANDLE)&(psDCInfo->sSystemBuffer);

	return PVRSRV_OK;
}


IMG_EXPORT
PVRSRV_ERROR PVRSRVGetDCInfoKM (IMG_HANDLE hDeviceKM,
								DISPLAY_INFO *psDisplayInfo)
{
	PVRSRV_DISPLAYCLASS_INFO *psDCInfo;
	PVRSRV_ERROR eError;

	if(!hDeviceKM || !psDisplayInfo)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVGetDCInfoKM: Invalid parameters"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDCInfo = DCDeviceHandleToDCInfo(hDeviceKM);

	
	eError = psDCInfo->psFuncTable->pfnGetDCInfo(psDCInfo->hExtDevice, psDisplayInfo);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	if (psDisplayInfo->ui32MaxSwapChainBuffers > PVRSRV_MAX_DC_SWAPCHAIN_BUFFERS)
	{
		psDisplayInfo->ui32MaxSwapChainBuffers = PVRSRV_MAX_DC_SWAPCHAIN_BUFFERS;
	}

	return PVRSRV_OK;
}


IMG_EXPORT
PVRSRV_ERROR PVRSRVDestroyDCSwapChainKM(IMG_HANDLE hSwapChain)
{
	PVRSRV_ERROR eError;
	PVRSRV_DC_SWAPCHAIN *psSwapChain;

	if(!hSwapChain)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVDestroyDCSwapChainKM: Invalid parameters"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psSwapChain = hSwapChain;

	eError = ResManFreeResByPtr(psSwapChain->hResItem);

	return eError;
}


static PVRSRV_ERROR DestroyDCSwapChainCallBack(IMG_PVOID pvParam, IMG_UINT32 ui32Param)
{
	PVRSRV_ERROR				eError;
	PVRSRV_DC_SWAPCHAIN 		*psSwapChain = pvParam;
	PVRSRV_DISPLAYCLASS_INFO	*psDCInfo = psSwapChain->psDCInfo;
	IMG_UINT32 i;

	PVR_UNREFERENCED_PARAMETER(ui32Param);

	
	PVRSRVDestroyCommandQueueKM(psSwapChain->psQueue);

	
	eError = psDCInfo->psFuncTable->pfnDestroyDCSwapChain(psDCInfo->hExtDevice,
															psSwapChain->hExtSwapChain);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"DestroyDCSwapChainCallBack: Failed to destroy DC swap chain"));
		return eError;
	}

	
	for(i=0; i<psSwapChain->ui32BufferCount; i++)
	{
		if(psSwapChain->asBuffer[i].sDeviceClassBuffer.psKernelSyncInfo)
		{
			PVRSRVFreeSyncInfoKM(psSwapChain->asBuffer[i].sDeviceClassBuffer.psKernelSyncInfo);
		}
	}

	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PVRSRV_DC_SWAPCHAIN),
			psSwapChain, IMG_NULL);

	return eError;
}



IMG_EXPORT
PVRSRV_ERROR PVRSRVCreateDCSwapChainKM (PVRSRV_PER_PROCESS_DATA	*psPerProc,
										IMG_HANDLE				hDeviceKM,
										IMG_UINT32				ui32Flags,
										DISPLAY_SURF_ATTRIBUTES	*psDstSurfAttrib,
										DISPLAY_SURF_ATTRIBUTES *psSrcSurfAttrib,
										IMG_UINT32				ui32BufferCount,
										IMG_UINT32				ui32OEMFlags,
										IMG_HANDLE				*phSwapChain,
										IMG_UINT32				*pui32SwapChainID)
{
	PVRSRV_DISPLAYCLASS_INFO *psDCInfo;
	PVRSRV_DC_SWAPCHAIN *psSwapChain = IMG_NULL;
	PVRSRV_SYNC_DATA *apsSyncData[PVRSRV_MAX_DC_SWAPCHAIN_BUFFERS];
	PVRSRV_QUEUE_INFO *psQueue = IMG_NULL;
	PVRSRV_ERROR eError;
	IMG_UINT32 i;


	if(!hDeviceKM
	|| !psDstSurfAttrib
	|| !psSrcSurfAttrib
	|| !phSwapChain
	|| !pui32SwapChainID)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVCreateDCSwapChainKM: Invalid parameters"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if (ui32BufferCount > PVRSRV_MAX_DC_SWAPCHAIN_BUFFERS)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVCreateDCSwapChainKM: Too many buffers"));
		return PVRSRV_ERROR_TOOMANYBUFFERS;
	}

	if (ui32BufferCount < 2)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVCreateDCSwapChainKM: Too few buffers"));
		return PVRSRV_ERROR_TOO_FEW_BUFFERS;
	}

	psDCInfo = DCDeviceHandleToDCInfo(hDeviceKM);

	
	if(OSAllocMem( PVRSRV_OS_PAGEABLE_HEAP,
					 sizeof(PVRSRV_DC_SWAPCHAIN),
					 (IMG_VOID **)&psSwapChain, IMG_NULL) != PVRSRV_OK)	
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVCreateDCSwapChainKM: Failed psSwapChain alloc"));
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto ErrorExit;
	}
	OSMemSet (psSwapChain, 0, sizeof(PVRSRV_DC_SWAPCHAIN));

	
	eError = PVRSRVCreateCommandQueueKM(1024, &psQueue);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVCreateDCSwapChainKM: Failed to create CmdQueue"));
		goto ErrorExit;
	}

	
	psSwapChain->psQueue = psQueue;

	
	for(i=0; i<ui32BufferCount; i++)
	{
		eError = PVRSRVAllocSyncInfoKM(IMG_NULL,
										psDCInfo->hDevMemContext,
										&psSwapChain->asBuffer[i].sDeviceClassBuffer.psKernelSyncInfo);
		if(eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"PVRSRVCreateDCSwapChainKM: Failed to alloc syninfo for psSwapChain"));
			goto ErrorExit;
		}

		
		psSwapChain->asBuffer[i].sDeviceClassBuffer.pfnGetBufferAddr = psDCInfo->psFuncTable->pfnGetBufferAddr;
		psSwapChain->asBuffer[i].sDeviceClassBuffer.hDevMemContext = psDCInfo->hDevMemContext;
		psSwapChain->asBuffer[i].sDeviceClassBuffer.hExtDevice = psDCInfo->hExtDevice;

		
		psSwapChain->asBuffer[i].psDCInfo = psDCInfo;
		psSwapChain->asBuffer[i].psSwapChain = psSwapChain;

		
		apsSyncData[i] = (PVRSRV_SYNC_DATA*)psSwapChain->asBuffer[i].sDeviceClassBuffer.psKernelSyncInfo->psSyncDataMemInfoKM->pvLinAddrKM;
	}

	psSwapChain->ui32BufferCount = ui32BufferCount;
	psSwapChain->psDCInfo = psDCInfo;

	
	eError =  psDCInfo->psFuncTable->pfnCreateDCSwapChain(psDCInfo->hExtDevice,
														ui32Flags,
														psDstSurfAttrib,
														psSrcSurfAttrib,
														ui32BufferCount,
														apsSyncData,
														ui32OEMFlags,
														&psSwapChain->hExtSwapChain,
														pui32SwapChainID);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVCreateDCSwapChainKM: Failed to create 3rd party SwapChain"));
		goto ErrorExit;
	}
	
	
	*phSwapChain = (IMG_HANDLE)psSwapChain;


	
	psSwapChain->hResItem = ResManRegisterRes(psPerProc->hResManContext,
											  RESMAN_TYPE_DISPLAYCLASS_SWAPCHAIN,
											  psSwapChain,
											  0,
											  DestroyDCSwapChainCallBack);

	return eError;

ErrorExit:

	for(i=0; i<ui32BufferCount; i++)
	{
		if(psSwapChain->asBuffer[i].sDeviceClassBuffer.psKernelSyncInfo)
		{
			PVRSRVFreeSyncInfoKM(psSwapChain->asBuffer[i].sDeviceClassBuffer.psKernelSyncInfo);
		}
	}

	if(psQueue)
	{
		PVRSRVDestroyCommandQueueKM(psQueue);
	}

	if(psSwapChain)
	{
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PVRSRV_DC_SWAPCHAIN),
				psSwapChain, IMG_NULL);
	}

	return eError;
}




IMG_EXPORT
PVRSRV_ERROR PVRSRVSetDCDstRectKM(IMG_HANDLE	hDeviceKM,
								  IMG_HANDLE	hSwapChain,
								  IMG_RECT		*psRect)
{
	PVRSRV_DISPLAYCLASS_INFO *psDCInfo;
	PVRSRV_DC_SWAPCHAIN *psSwapChain;

	if(!hDeviceKM || !hSwapChain)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVSetDCDstRectKM: Invalid parameters"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDCInfo = DCDeviceHandleToDCInfo(hDeviceKM);
	psSwapChain = (PVRSRV_DC_SWAPCHAIN*)hSwapChain;

	return psDCInfo->psFuncTable->pfnSetDCDstRect(psDCInfo->hExtDevice,
													psSwapChain->hExtSwapChain,
													psRect);
}


IMG_EXPORT
PVRSRV_ERROR PVRSRVSetDCSrcRectKM(IMG_HANDLE	hDeviceKM,
								  IMG_HANDLE	hSwapChain,
								  IMG_RECT		*psRect)
{
	PVRSRV_DISPLAYCLASS_INFO *psDCInfo;
	PVRSRV_DC_SWAPCHAIN *psSwapChain;

	if(!hDeviceKM || !hSwapChain)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVSetDCSrcRectKM: Invalid parameters"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDCInfo = DCDeviceHandleToDCInfo(hDeviceKM);
	psSwapChain = (PVRSRV_DC_SWAPCHAIN*)hSwapChain;

	return psDCInfo->psFuncTable->pfnSetDCSrcRect(psDCInfo->hExtDevice,
													psSwapChain->hExtSwapChain,
													psRect);
}


IMG_EXPORT
PVRSRV_ERROR PVRSRVSetDCDstColourKeyKM(IMG_HANDLE	hDeviceKM,
									   IMG_HANDLE	hSwapChain,
									   IMG_UINT32	ui32CKColour)
{
	PVRSRV_DISPLAYCLASS_INFO *psDCInfo;
	PVRSRV_DC_SWAPCHAIN *psSwapChain;

	if(!hDeviceKM || !hSwapChain)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVSetDCDstColourKeyKM: Invalid parameters"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDCInfo = DCDeviceHandleToDCInfo(hDeviceKM);
	psSwapChain = (PVRSRV_DC_SWAPCHAIN*)hSwapChain;

	return psDCInfo->psFuncTable->pfnSetDCDstColourKey(psDCInfo->hExtDevice,
														psSwapChain->hExtSwapChain,
														ui32CKColour);
}


IMG_EXPORT
PVRSRV_ERROR PVRSRVSetDCSrcColourKeyKM(IMG_HANDLE	hDeviceKM,
									   IMG_HANDLE	hSwapChain,
									   IMG_UINT32	ui32CKColour)
{
	PVRSRV_DISPLAYCLASS_INFO *psDCInfo;
	PVRSRV_DC_SWAPCHAIN *psSwapChain;

	if(!hDeviceKM || !hSwapChain)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVSetDCSrcColourKeyKM: Invalid parameters"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDCInfo = DCDeviceHandleToDCInfo(hDeviceKM);
	psSwapChain = (PVRSRV_DC_SWAPCHAIN*)hSwapChain;

	return psDCInfo->psFuncTable->pfnSetDCSrcColourKey(psDCInfo->hExtDevice,
														psSwapChain->hExtSwapChain,
														ui32CKColour);
}


IMG_EXPORT
PVRSRV_ERROR PVRSRVGetDCBuffersKM(IMG_HANDLE	hDeviceKM,
								  IMG_HANDLE	hSwapChain,
								  IMG_UINT32	*pui32BufferCount,
								  IMG_HANDLE	*phBuffer)
{
	PVRSRV_DISPLAYCLASS_INFO *psDCInfo;
	PVRSRV_DC_SWAPCHAIN *psSwapChain;
	IMG_HANDLE ahExtBuffer[PVRSRV_MAX_DC_SWAPCHAIN_BUFFERS];
	PVRSRV_ERROR eError;
	IMG_UINT32 i;

	if(!hDeviceKM || !hSwapChain || !phBuffer)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVGetDCBuffersKM: Invalid parameters"));	
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDCInfo = DCDeviceHandleToDCInfo(hDeviceKM);
	psSwapChain = (PVRSRV_DC_SWAPCHAIN*)hSwapChain;

	
	eError = psDCInfo->psFuncTable->pfnGetDCBuffers(psDCInfo->hExtDevice,
													psSwapChain->hExtSwapChain,
													pui32BufferCount,
													ahExtBuffer);

	PVR_ASSERT(*pui32BufferCount <= PVRSRV_MAX_DC_SWAPCHAIN_BUFFERS);

	


	for(i=0; i<*pui32BufferCount; i++)
	{
		psSwapChain->asBuffer[i].sDeviceClassBuffer.hExtBuffer = ahExtBuffer[i];
		phBuffer[i] = (IMG_HANDLE)&psSwapChain->asBuffer[i];
	}

	return eError;
}


IMG_EXPORT
PVRSRV_ERROR PVRSRVSwapToDCBufferKM(IMG_HANDLE	hDeviceKM,
									IMG_HANDLE	hBuffer,
									IMG_UINT32	ui32SwapInterval,
									IMG_HANDLE	hPrivateTag,
									IMG_UINT32	ui32ClipRectCount,
									IMG_RECT	*psClipRect)
{
	PVRSRV_ERROR eError;
	PVRSRV_DISPLAYCLASS_INFO *psDCInfo;
	PVRSRV_DC_BUFFER *psBuffer;
	PVRSRV_QUEUE_INFO *psQueue;
	DISPLAYCLASS_FLIP_COMMAND *psFlipCmd;
	IMG_UINT32 i;
	IMG_BOOL bStart = IMG_FALSE;
	IMG_UINT32 uiStart = 0;
	IMG_UINT32 ui32NumSrcSyncs = 1;
	PVRSRV_KERNEL_SYNC_INFO *apsSrcSync[2];
	PVRSRV_COMMAND *psCommand;

	if(!hDeviceKM || !hBuffer || !psClipRect)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVSwapToDCBufferKM: Invalid parameters"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

#if defined(SUPPORT_LMA)
	eError = PVRSRVPowerLock(KERNEL_ID, IMG_FALSE);
	if(eError != PVRSRV_OK)
	{
		return eError;
	}
#endif 
	
	psDCInfo = DCDeviceHandleToDCInfo(hDeviceKM);
	psBuffer = (PVRSRV_DC_BUFFER*)hBuffer;

	
	psQueue = psBuffer->psSwapChain->psQueue;

	
	apsSrcSync[0] = psBuffer->sDeviceClassBuffer.psKernelSyncInfo;
	if(psBuffer->psSwapChain->psLastFlipBuffer &&
		psBuffer != psBuffer->psSwapChain->psLastFlipBuffer)
	{
		apsSrcSync[1] = psBuffer->psSwapChain->psLastFlipBuffer->sDeviceClassBuffer.psKernelSyncInfo;
		ui32NumSrcSyncs++;
	}

	
	eError = PVRSRVInsertCommandKM (psQueue,
									&psCommand,
									psDCInfo->ui32DeviceID,
									DC_FLIP_COMMAND,
									0,
									IMG_NULL,
									ui32NumSrcSyncs,
									apsSrcSync,
									sizeof(DISPLAYCLASS_FLIP_COMMAND) + (sizeof(IMG_RECT) * ui32ClipRectCount));
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVSwapToDCBufferKM: Failed to get space in queue"));
		goto Exit;
	}
	
	
	psFlipCmd = (DISPLAYCLASS_FLIP_COMMAND*)psCommand->pvData;

	
	psFlipCmd->hExtDevice = psDCInfo->hExtDevice;

	
	psFlipCmd->hExtSwapChain = psBuffer->psSwapChain->hExtSwapChain;

	
	psFlipCmd->hExtBuffer = psBuffer->sDeviceClassBuffer.hExtBuffer;

	
	psFlipCmd->hPrivateTag = hPrivateTag;

	
	psFlipCmd->ui32ClipRectCount = ui32ClipRectCount;
	
	psFlipCmd->psClipRect = (IMG_RECT*)((IMG_UINT8*)psFlipCmd + sizeof(DISPLAYCLASS_FLIP_COMMAND));	
	
	for(i=0; i<ui32ClipRectCount; i++)
	{
		psFlipCmd->psClipRect[i] = psClipRect[i];
	}

	
	psFlipCmd->ui32SwapInterval = ui32SwapInterval;

	
	eError = PVRSRVSubmitCommandKM (psQueue, psCommand);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVSwapToDCBufferKM: Failed to submit command"));
		goto Exit;
	}
	
	







	do
	{
		if(PVRSRVProcessQueues(KERNEL_ID, IMG_FALSE) != PVRSRV_ERROR_PROCESSING_BLOCKED)
		{
			goto ProcessedQueues;
		}

		if (bStart == IMG_FALSE)
		{
			uiStart = OSClockus();
			bStart = IMG_TRUE;
		}
		OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
	} while ((OSClockus() - uiStart) < MAX_HW_TIME_US);	

	PVR_DPF((PVR_DBG_ERROR,"PVRSRVSwapToDCBufferKM: Failed to process queues"));

	eError = PVRSRV_ERROR_GENERIC;
	goto Exit;

ProcessedQueues:
	
	psBuffer->psSwapChain->psLastFlipBuffer = psBuffer;

Exit:
#if defined(SUPPORT_LMA)
	PVRSRVPowerUnlock(KERNEL_ID);
#endif	
	return eError;
}


IMG_EXPORT
PVRSRV_ERROR PVRSRVSwapToDCSystemKM(IMG_HANDLE	hDeviceKM,
									IMG_HANDLE	hSwapChain)
{
	PVRSRV_ERROR eError;
	PVRSRV_QUEUE_INFO *psQueue;
	PVRSRV_DISPLAYCLASS_INFO *psDCInfo;
	PVRSRV_DC_SWAPCHAIN *psSwapChain;
	DISPLAYCLASS_FLIP_COMMAND *psFlipCmd;
	IMG_BOOL bStart = IMG_FALSE;
	IMG_UINT32 uiStart = 0;
	IMG_UINT32 ui32NumSrcSyncs = 1;
	PVRSRV_KERNEL_SYNC_INFO *apsSrcSync[2];
	PVRSRV_COMMAND *psCommand;

	if(!hDeviceKM || !hSwapChain)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVSwapToDCSystemKM: Invalid parameters"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

#if defined(SUPPORT_LMA)
	eError = PVRSRVPowerLock(KERNEL_ID, IMG_FALSE);
	if(eError != PVRSRV_OK)
	{
		return eError;
	}
#endif 
	
	psDCInfo = DCDeviceHandleToDCInfo(hDeviceKM);
	psSwapChain = (PVRSRV_DC_SWAPCHAIN*)hSwapChain;

	
	psQueue = psSwapChain->psQueue;

	
	apsSrcSync[0] = psDCInfo->sSystemBuffer.sDeviceClassBuffer.psKernelSyncInfo;
	if(psSwapChain->psLastFlipBuffer)
	{
		apsSrcSync[1] = psSwapChain->psLastFlipBuffer->sDeviceClassBuffer.psKernelSyncInfo;
		ui32NumSrcSyncs++;
	}

	
	eError = PVRSRVInsertCommandKM (psQueue,
									&psCommand,
									psDCInfo->ui32DeviceID,
									DC_FLIP_COMMAND,
									0,
									IMG_NULL,
									ui32NumSrcSyncs,
									apsSrcSync,
									sizeof(DISPLAYCLASS_FLIP_COMMAND));
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVSwapToDCSystemKM: Failed to get space in queue"));
		goto Exit;
	}

	
	psFlipCmd = (DISPLAYCLASS_FLIP_COMMAND*)psCommand->pvData;

	
	psFlipCmd->hExtDevice = psDCInfo->hExtDevice;

	
	psFlipCmd->hExtSwapChain = psSwapChain->hExtSwapChain;

	
	psFlipCmd->hExtBuffer = psDCInfo->sSystemBuffer.sDeviceClassBuffer.hExtBuffer;

	
	psFlipCmd->hPrivateTag = IMG_NULL;

	
	psFlipCmd->ui32ClipRectCount = 0;

	psFlipCmd->ui32SwapInterval = 1;

	
	eError = PVRSRVSubmitCommandKM (psQueue, psCommand);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVSwapToDCSystemKM: Failed to submit command"));
		goto Exit;
	}

	






	do
	{
		if(PVRSRVProcessQueues(KERNEL_ID, IMG_FALSE) != PVRSRV_ERROR_PROCESSING_BLOCKED)
		{
			goto ProcessedQueues;
		}

		if (bStart == IMG_FALSE)
		{
			uiStart = OSClockus();
			bStart = IMG_TRUE;
		}
		OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
	} while ((OSClockus() - uiStart) < MAX_HW_TIME_US);

	PVR_DPF((PVR_DBG_ERROR,"PVRSRVSwapToDCSystemKM: Failed to process queues"));
	eError = PVRSRV_ERROR_GENERIC;
	goto Exit;

ProcessedQueues:
	
	psSwapChain->psLastFlipBuffer = &psDCInfo->sSystemBuffer;

	eError = PVRSRV_OK;
	
Exit:
#if defined(SUPPORT_LMA)
	PVRSRVPowerUnlock(KERNEL_ID);
#endif	
	return eError;
}


PVRSRV_ERROR PVRSRVRegisterSystemISRHandler (PFN_ISR_HANDLER	pfnISRHandler,
											 IMG_VOID			*pvISRHandlerData,
											 IMG_UINT32			ui32ISRSourceMask,
											 IMG_UINT32			ui32DeviceID)
{
	SYS_DATA 			*psSysData;
	PVRSRV_DEVICE_NODE	*psDevNode;

	PVR_UNREFERENCED_PARAMETER(ui32ISRSourceMask);

	if (SysAcquireData(&psSysData) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRegisterSystemISRHandler: Failed to get SysData"));
		return PVRSRV_ERROR_GENERIC;
	}

	
	psDevNode = psSysData->psDeviceNodeList;
	while(psDevNode)
	{
		if(psDevNode->sDevId.ui32DeviceIndex == ui32DeviceID)
		{
			break;
		}
		psDevNode = psDevNode->psNext;
	}

	
	psDevNode->pvISRData = (IMG_VOID*) pvISRHandlerData;

	
	psDevNode->pfnDeviceISR	= pfnISRHandler;

	return PVRSRV_OK;
}


IMG_VOID IMG_CALLCONV PVRSRVSetDCState(IMG_UINT32 ui32State)
{
	PVRSRV_DISPLAYCLASS_INFO	*psDCInfo;
	PVRSRV_DEVICE_NODE			*psDeviceNode;
	SYS_DATA					*psSysData;

	if (SysAcquireData(&psSysData) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVSetDCState: Failed to get SysData"));
		return;
	}

	psDeviceNode = psSysData->psDeviceNodeList;
	while(psDeviceNode != IMG_NULL)
	{
		if (psDeviceNode->sDevId.eDeviceClass == PVRSRV_DEVICE_CLASS_DISPLAY)
		{
			psDCInfo = (PVRSRV_DISPLAYCLASS_INFO *)psDeviceNode->pvDevice;
			if (psDCInfo->psFuncTable->pfnSetDCState && psDCInfo->hExtDevice)
			{
				psDCInfo->psFuncTable->pfnSetDCState(psDCInfo->hExtDevice, ui32State);
			}
		}
		psDeviceNode = psDeviceNode->psNext;
	}
}


IMG_EXPORT
IMG_BOOL PVRGetDisplayClassJTable(PVRSRV_DC_DISP2SRV_KMJTABLE *psJTable)
{
	psJTable->ui32TableSize = sizeof(PVRSRV_DC_DISP2SRV_KMJTABLE);
	psJTable->pfnPVRSRVRegisterDCDevice = PVRSRVRegisterDCDeviceKM;
	psJTable->pfnPVRSRVRemoveDCDevice = PVRSRVRemoveDCDeviceKM;
	psJTable->pfnPVRSRVOEMFunction = SysOEMFunction;
	psJTable->pfnPVRSRVRegisterCmdProcList = PVRSRVRegisterCmdProcListKM;
	psJTable->pfnPVRSRVRemoveCmdProcList = PVRSRVRemoveCmdProcListKM;
	psJTable->pfnPVRSRVCmdComplete = PVRSRVCommandCompleteKM;
	psJTable->pfnPVRSRVRegisterSystemISRHandler = PVRSRVRegisterSystemISRHandler;
	psJTable->pfnPVRSRVRegisterPowerDevice = PVRSRVRegisterPowerDevice;

	return IMG_TRUE;
}



IMG_EXPORT
PVRSRV_ERROR PVRSRVCloseBCDeviceKM (IMG_HANDLE	hDeviceKM,
									IMG_BOOL	bResManCallback)
{
	PVRSRV_ERROR eError;
	PVRSRV_BUFFERCLASS_PERCONTEXT_INFO *psBCPerContextInfo;

	PVR_UNREFERENCED_PARAMETER(bResManCallback);

	psBCPerContextInfo = (PVRSRV_BUFFERCLASS_PERCONTEXT_INFO *)hDeviceKM;

	
	eError = ResManFreeResByPtr(psBCPerContextInfo->hResItem);
			
	return eError;
}


static PVRSRV_ERROR CloseBCDeviceCallBack(IMG_PVOID		pvParam,
										  IMG_UINT32	ui32Param)
{
	PVRSRV_BUFFERCLASS_PERCONTEXT_INFO *psBCPerContextInfo;
	PVRSRV_BUFFERCLASS_INFO *psBCInfo;

	PVR_UNREFERENCED_PARAMETER(ui32Param);

	psBCPerContextInfo = (PVRSRV_BUFFERCLASS_PERCONTEXT_INFO *)pvParam;
	psBCInfo = psBCPerContextInfo->psBCInfo;

	psBCInfo->ui32RefCount--;
	if(psBCInfo->ui32RefCount == 0)
	{
		IMG_UINT32 i;

		
		psBCInfo->psFuncTable->pfnCloseBCDevice(psBCInfo->hExtDevice);

		
		for(i=0; i<psBCInfo->ui32BufferCount; i++)
		{
			if(psBCInfo->psBuffer[i].sDeviceClassBuffer.psKernelSyncInfo)
			{
				PVRSRVFreeSyncInfoKM(psBCInfo->psBuffer[i].sDeviceClassBuffer.psKernelSyncInfo);
			}
		}

		
		if(psBCInfo->psBuffer)
		{
			OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
					sizeof(PVRSRV_BC_BUFFER) * psBCInfo->ui32BufferCount,
					psBCInfo->psBuffer, IMG_NULL);
		}
	}

	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PVRSRV_BUFFERCLASS_PERCONTEXT_INFO),
			psBCPerContextInfo, IMG_NULL);

	return PVRSRV_OK;
}


IMG_EXPORT
PVRSRV_ERROR PVRSRVOpenBCDeviceKM (PVRSRV_PER_PROCESS_DATA	*psPerProc,
								   IMG_UINT32				ui32DeviceID,
								   IMG_HANDLE				hDevCookie,
								   IMG_HANDLE				*phDeviceKM)
{
	PVRSRV_BUFFERCLASS_INFO	*psBCInfo;
	PVRSRV_BUFFERCLASS_PERCONTEXT_INFO	*psBCPerContextInfo;
	PVRSRV_DEVICE_NODE		*psDeviceNode;
	SYS_DATA 				*psSysData;
	IMG_UINT32 				i;
	PVRSRV_ERROR			eError;
	BUFFER_INFO sBufferInfo;

	if(!phDeviceKM)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVOpenBCDeviceKM: Invalid params"));
		return PVRSRV_ERROR_GENERIC;
	}

	if (SysAcquireData(&psSysData) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVOpenBCDeviceKM: Failed to get SysData"));
		return PVRSRV_ERROR_GENERIC;
	}

	
	psDeviceNode = psSysData->psDeviceNodeList;
	while(psDeviceNode)
	{
		if ((psDeviceNode->sDevId.eDeviceClass == PVRSRV_DEVICE_CLASS_BUFFER) &&
			(psDeviceNode->sDevId.ui32DeviceIndex == ui32DeviceID))
		{
			


			psBCInfo = (PVRSRV_BUFFERCLASS_INFO*)psDeviceNode->pvDevice;
			goto FoundDevice;
		}
		psDeviceNode = psDeviceNode->psNext;
	}

	PVR_DPF((PVR_DBG_ERROR,"PVRSRVOpenBCDeviceKM: No devnode matching index %d", ui32DeviceID));

	return PVRSRV_ERROR_GENERIC;

FoundDevice:

	


	if(OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
				  sizeof(*psBCPerContextInfo),
				  (IMG_VOID **)&psBCPerContextInfo, IMG_NULL) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVOpenBCDeviceKM: Failed psBCPerContextInfo alloc"));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
	OSMemSet(psBCPerContextInfo, 0, sizeof(*psBCPerContextInfo));

	if(psBCInfo->ui32RefCount++ == 0)
	{
		psDeviceNode = (PVRSRV_DEVICE_NODE *)hDevCookie;
		PVR_ASSERT(psDeviceNode != IMG_NULL);

		
		psBCInfo->hDevMemContext = (IMG_HANDLE)psDeviceNode->sDevMemoryInfo.pBMKernelContext;

		
		eError = psBCInfo->psFuncTable->pfnOpenBCDevice(&psBCInfo->hExtDevice);
		if(eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"PVRSRVOpenBCDeviceKM: Failed to open external BC device"));
			return eError;
		}

		
		eError = psBCInfo->psFuncTable->pfnGetBCInfo(psBCInfo->hExtDevice, &sBufferInfo);
		if(eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"PVRSRVOpenBCDeviceKM : Failed to get BC Info"));
			return eError;
		}

		
		psBCInfo->ui32BufferCount = sBufferInfo.ui32BufferCount;
		

		
		eError = OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
							  sizeof(PVRSRV_BC_BUFFER) * sBufferInfo.ui32BufferCount,
							  (IMG_VOID **)&psBCInfo->psBuffer, 
						 	  IMG_NULL);
		if(eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"PVRSRVOpenBCDeviceKM: Failed to allocate BC buffers"));
			return eError;
		}
		OSMemSet (psBCInfo->psBuffer,
					0,
					sizeof(PVRSRV_BC_BUFFER) * sBufferInfo.ui32BufferCount);
	
		for(i=0; i<psBCInfo->ui32BufferCount; i++)
		{
			
			eError = PVRSRVAllocSyncInfoKM(IMG_NULL,
										psBCInfo->hDevMemContext,
										&psBCInfo->psBuffer[i].sDeviceClassBuffer.psKernelSyncInfo);
			if(eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,"PVRSRVOpenBCDeviceKM: Failed sync info alloc"));
				goto ErrorExit;
			}
			
			


			eError = psBCInfo->psFuncTable->pfnGetBCBuffer(psBCInfo->hExtDevice,
															i,
															psBCInfo->psBuffer[i].sDeviceClassBuffer.psKernelSyncInfo->psSyncData,
															&psBCInfo->psBuffer[i].sDeviceClassBuffer.hExtBuffer);
			if(eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,"PVRSRVOpenBCDeviceKM: Failed to get BC buffers"));
				goto ErrorExit;
			}

			
			psBCInfo->psBuffer[i].sDeviceClassBuffer.pfnGetBufferAddr = psBCInfo->psFuncTable->pfnGetBufferAddr;
			psBCInfo->psBuffer[i].sDeviceClassBuffer.hDevMemContext = psBCInfo->hDevMemContext;
			psBCInfo->psBuffer[i].sDeviceClassBuffer.hExtDevice = psBCInfo->hExtDevice;
		}
	}

	psBCPerContextInfo->psBCInfo = psBCInfo;
	psBCPerContextInfo->hResItem = ResManRegisterRes(psPerProc->hResManContext,
													 RESMAN_TYPE_BUFFERCLASS_DEVICE,
													 psBCPerContextInfo,
													 0,
													 CloseBCDeviceCallBack);
	
	
	*phDeviceKM = (IMG_HANDLE)psBCPerContextInfo;

	return PVRSRV_OK;

ErrorExit:

	
	for(i=0; i<psBCInfo->ui32BufferCount; i++)
	{
		if(psBCInfo->psBuffer[i].sDeviceClassBuffer.psKernelSyncInfo)
		{
			PVRSRVFreeSyncInfoKM(psBCInfo->psBuffer[i].sDeviceClassBuffer.psKernelSyncInfo);
		}
	}

	
	if(psBCInfo->psBuffer)
	{
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
				sizeof(PVRSRV_BC_BUFFER) * sBufferInfo.ui32BufferCount,
				psBCInfo->psBuffer, IMG_NULL);
	}

	return eError;
}




IMG_EXPORT
PVRSRV_ERROR PVRSRVGetBCInfoKM (IMG_HANDLE hDeviceKM,
								BUFFER_INFO *psBufferInfo)
{
	PVRSRV_BUFFERCLASS_INFO *psBCInfo;
	PVRSRV_ERROR 			eError;

	if(!hDeviceKM || !psBufferInfo)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVGetBCInfoKM: Invalid parameters"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psBCInfo = BCDeviceHandleToBCInfo(hDeviceKM);

	eError = psBCInfo->psFuncTable->pfnGetBCInfo(psBCInfo->hExtDevice, psBufferInfo);

	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVGetBCInfoKM : Failed to get BC Info"));
		return eError;
	}

	return PVRSRV_OK;
}


IMG_EXPORT
PVRSRV_ERROR PVRSRVGetBCBufferKM (IMG_HANDLE hDeviceKM,
								  IMG_UINT32 ui32BufferIndex,
								  IMG_HANDLE *phBuffer)
{
	PVRSRV_BUFFERCLASS_INFO *psBCInfo;

	if(!hDeviceKM || !phBuffer)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVGetBCBufferKM: Invalid parameters"));	
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psBCInfo = BCDeviceHandleToBCInfo(hDeviceKM);

	if(ui32BufferIndex < psBCInfo->ui32BufferCount)
	{
		*phBuffer = (IMG_HANDLE)&psBCInfo->psBuffer[ui32BufferIndex];
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVGetBCBufferKM: Buffer index %d out of range (%d)", ui32BufferIndex,psBCInfo->ui32BufferCount));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	return PVRSRV_OK;
}


IMG_EXPORT
IMG_BOOL PVRGetBufferClassJTable(PVRSRV_BC_BUFFER2SRV_KMJTABLE *psJTable)
{
	psJTable->ui32TableSize = sizeof(PVRSRV_BC_BUFFER2SRV_KMJTABLE);

	psJTable->pfnPVRSRVRegisterBCDevice = PVRSRVRegisterBCDeviceKM;
	psJTable->pfnPVRSRVRemoveBCDevice = PVRSRVRemoveBCDeviceKM;

	return IMG_TRUE;
}

