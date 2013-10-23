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
#include "sgxapi_km.h"
#include "sgxinfo.h"
#include "sgxinfokm.h"
#include "pvr_bridge_km.h"
#include "pdump_km.h"

#ifndef __linux__
#pragma message("TODO: Review use of OS_PAGEABLE vs OS_NON_PAGEABLE")
#endif

static PRESMAN_ITEM psResItemCreateSharedPB = IMG_NULL;
static PVRSRV_PER_PROCESS_DATA *psPerProcCreateSharedPB = IMG_NULL;

static PVRSRV_ERROR SGXCleanupSharedPBDescCallback(IMG_PVOID pvParam, IMG_UINT32 ui32Param);
static PVRSRV_ERROR SGXCleanupSharedPBDescCreateLockCallback(IMG_PVOID pvParam, IMG_UINT32 ui32Param);

IMG_EXPORT PVRSRV_ERROR
SGXFindSharedPBDescKM(PVRSRV_PER_PROCESS_DATA	*psPerProc,
					  IMG_HANDLE 				hDevCookie,
					  IMG_BOOL 				bLockOnFailure,
					  IMG_UINT32 				ui32TotalPBSize,
					  IMG_HANDLE 				*phSharedPBDesc,
					  PVRSRV_KERNEL_MEM_INFO 	**ppsSharedPBDescKernelMemInfo,
					  PVRSRV_KERNEL_MEM_INFO 	**ppsHWPBDescKernelMemInfo,
					  PVRSRV_KERNEL_MEM_INFO 	**ppsBlockKernelMemInfo,
					  PVRSRV_KERNEL_MEM_INFO 	***pppsSharedPBDescSubKernelMemInfos,
					  IMG_UINT32				*ui32SharedPBDescSubKernelMemInfosCount)
{
	PVRSRV_STUB_PBDESC *psStubPBDesc;
	PVRSRV_KERNEL_MEM_INFO **ppsSharedPBDescSubKernelMemInfos=IMG_NULL;
	PVRSRV_SGXDEV_INFO *psSGXDevInfo;
	PVRSRV_ERROR eError = PVRSRV_ERROR_GENERIC;

	psSGXDevInfo = ((PVRSRV_DEVICE_NODE *)hDevCookie)->pvDevice;

	


	psStubPBDesc = psSGXDevInfo->psStubPBDescListKM;
	if (psStubPBDesc != IMG_NULL)
	{
		if(psStubPBDesc->ui32TotalPBSize != ui32TotalPBSize)
		{
			PVR_DPF((PVR_DBG_WARNING,
					"SGXFindSharedPBDescKM: Shared PB requested with different size (0x%x) from existing shared PB (0x%x) - requested size ignored",
					ui32TotalPBSize, psStubPBDesc->ui32TotalPBSize));
		}
		{
			IMG_UINT32 i;
			PRESMAN_ITEM psResItem;

			if(OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
						  sizeof(PVRSRV_KERNEL_MEM_INFO *)
							* psStubPBDesc->ui32SubKernelMemInfosCount,
						  (IMG_VOID **)&ppsSharedPBDescSubKernelMemInfos,
						  IMG_NULL) != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "SGXFindSharedPBDescKM: OSAllocMem failed"));

				eError = PVRSRV_ERROR_OUT_OF_MEMORY;
				goto ExitNotFound;
			}
			
			psResItem = ResManRegisterRes(psPerProc->hResManContext,
										  RESMAN_TYPE_SHARED_PB_DESC,
										  psStubPBDesc,
										  0,
										  &SGXCleanupSharedPBDescCallback);

			if (psResItem == IMG_NULL)
			{
				OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
						  sizeof(PVRSRV_KERNEL_MEM_INFO *)
							* psStubPBDesc->ui32SubKernelMemInfosCount,
						  ppsSharedPBDescSubKernelMemInfos,
						  0);

				PVR_DPF((PVR_DBG_ERROR, "SGXFindSharedPBDescKM: ResManRegisterRes failed"));

				eError = PVRSRV_ERROR_GENERIC;
				goto ExitNotFound;
			}

			*ppsSharedPBDescKernelMemInfo = psStubPBDesc->psSharedPBDescKernelMemInfo;
			*ppsHWPBDescKernelMemInfo = psStubPBDesc->psHWPBDescKernelMemInfo;
			*ppsBlockKernelMemInfo = psStubPBDesc->psBlockKernelMemInfo;

			*ui32SharedPBDescSubKernelMemInfosCount =
				psStubPBDesc->ui32SubKernelMemInfosCount;

			*pppsSharedPBDescSubKernelMemInfos = ppsSharedPBDescSubKernelMemInfos;

			for(i=0; i<psStubPBDesc->ui32SubKernelMemInfosCount; i++)
			{
				ppsSharedPBDescSubKernelMemInfos[i] =
					psStubPBDesc->ppsSubKernelMemInfos[i];
			}

			psStubPBDesc->ui32RefCount++;
			*phSharedPBDesc = (IMG_HANDLE)psResItem;
			return PVRSRV_OK;
		}
	}

	eError = PVRSRV_OK;
	if (bLockOnFailure)
	{
		if (psResItemCreateSharedPB == IMG_NULL)
		{
			psResItemCreateSharedPB = ResManRegisterRes(psPerProc->hResManContext,
				  RESMAN_TYPE_SHARED_PB_DESC_CREATE_LOCK,
				  psPerProc,
				  0,
				  &SGXCleanupSharedPBDescCreateLockCallback);

			if (psResItemCreateSharedPB == IMG_NULL)
			{
				PVR_DPF((PVR_DBG_ERROR, "SGXFindSharedPBDescKM: ResManRegisterRes failed"));

				eError = PVRSRV_ERROR_GENERIC;
				goto ExitNotFound;
			}
			PVR_ASSERT(psPerProcCreateSharedPB == IMG_NULL);
			psPerProcCreateSharedPB = psPerProc;
		}
		else
		{
			 eError = PVRSRV_ERROR_PROCESSING_BLOCKED;
		}
	}
ExitNotFound:
	*phSharedPBDesc = IMG_NULL;

	return eError;
}


static PVRSRV_ERROR
SGXCleanupSharedPBDescKM(PVRSRV_STUB_PBDESC *psStubPBDescIn)
{
	PVRSRV_STUB_PBDESC **ppsStubPBDesc;
	IMG_UINT32 i;
	PVRSRV_SGXDEV_INFO *psSGXDevInfo;

	psSGXDevInfo = (PVRSRV_SGXDEV_INFO *)((PVRSRV_DEVICE_NODE *)psStubPBDescIn->hDevCookie)->pvDevice;

	for(ppsStubPBDesc = (PVRSRV_STUB_PBDESC **)&psSGXDevInfo->psStubPBDescListKM;
		*ppsStubPBDesc != IMG_NULL;
		ppsStubPBDesc = &(*ppsStubPBDesc)->psNext)
	{
		PVRSRV_STUB_PBDESC *psStubPBDesc = *ppsStubPBDesc;

		if(psStubPBDesc == psStubPBDescIn)
		{
			psStubPBDesc->ui32RefCount--;
			PVR_ASSERT((IMG_INT32)psStubPBDesc->ui32RefCount >= 0);

			if(psStubPBDesc->ui32RefCount == 0)
			{
				PVRSRV_SGX_HOST_CTL	*psSGXHostCtl = (PVRSRV_SGX_HOST_CTL *)psSGXDevInfo->psSGXHostCtl;
#if defined (PDUMP)
				IMG_HANDLE hUniqueTag = MAKEUNIQUETAG(psSGXDevInfo->psKernelSGXHostCtlMemInfo);
#endif

				
				
				psSGXHostCtl->sTAHWPBDesc.uiAddr = 0;
				psSGXHostCtl->s3DHWPBDesc.uiAddr = 0;

				
				PDUMPCOMMENT("TA/3D CCB Control - Reset HW PBDesc records");
				PDUMPMEM(IMG_NULL, psSGXDevInfo->psKernelSGXHostCtlMemInfo, offsetof(PVRSRV_SGX_HOST_CTL, sTAHWPBDesc), sizeof(IMG_DEV_VIRTADDR), PDUMP_FLAGS_CONTINUOUS, hUniqueTag);
				PDUMPMEM(IMG_NULL, psSGXDevInfo->psKernelSGXHostCtlMemInfo, offsetof(PVRSRV_SGX_HOST_CTL, s3DHWPBDesc), sizeof(IMG_DEV_VIRTADDR), PDUMP_FLAGS_CONTINUOUS, hUniqueTag);

				*ppsStubPBDesc = psStubPBDesc->psNext;

				for(i=0 ; i<psStubPBDesc->ui32SubKernelMemInfosCount; i++)
				{
					
					PVRSRVFreeDeviceMemKM(psStubPBDesc->hDevCookie,
										  psStubPBDesc->ppsSubKernelMemInfos[i]);
				}

				OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
						  sizeof(PVRSRV_KERNEL_MEM_INFO *)
						  * psStubPBDesc->ui32SubKernelMemInfosCount,
						  psStubPBDesc->ppsSubKernelMemInfos,
						  0);

				PVRSRVFreeSharedSysMemoryKM(psStubPBDesc->psBlockKernelMemInfo);

				PVRSRVFreeDeviceMemKM(psStubPBDesc->hDevCookie, psStubPBDesc->psHWPBDescKernelMemInfo);

				PVRSRVFreeSharedSysMemoryKM(psStubPBDesc->psSharedPBDescKernelMemInfo);
		
				OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
						  sizeof(PVRSRV_STUB_PBDESC),
						  psStubPBDesc,
						  0);

			}
			return PVRSRV_OK;
		}
	}

	return PVRSRV_ERROR_INVALID_PARAMS;
}

static PVRSRV_ERROR SGXCleanupSharedPBDescCallback(IMG_PVOID pvParam, IMG_UINT32 ui32Param)
{
	PVRSRV_STUB_PBDESC *psStubPBDesc = (PVRSRV_STUB_PBDESC *)pvParam;

	PVR_UNREFERENCED_PARAMETER(ui32Param);

	return SGXCleanupSharedPBDescKM(psStubPBDesc);
}

static PVRSRV_ERROR SGXCleanupSharedPBDescCreateLockCallback(IMG_PVOID pvParam, IMG_UINT32 ui32Param)
{
#ifdef DEBUG
	PVRSRV_PER_PROCESS_DATA *psPerProc = (PVRSRV_PER_PROCESS_DATA *)pvParam;
#else
	PVR_UNREFERENCED_PARAMETER(pvParam);
#endif

	PVR_UNREFERENCED_PARAMETER(ui32Param);

	PVR_ASSERT(psPerProc == psPerProcCreateSharedPB);

	psPerProcCreateSharedPB = IMG_NULL;
	psResItemCreateSharedPB = IMG_NULL;

	return PVRSRV_OK;
}


IMG_EXPORT PVRSRV_ERROR
SGXUnrefSharedPBDescKM(IMG_HANDLE hSharedPBDesc)
{
	PVR_ASSERT(hSharedPBDesc != IMG_NULL);

	return ResManFreeResByPtr(hSharedPBDesc);
}


IMG_EXPORT PVRSRV_ERROR
SGXAddSharedPBDescKM(PVRSRV_PER_PROCESS_DATA	*psPerProc,
					 IMG_HANDLE					hDevCookie,
					 PVRSRV_KERNEL_MEM_INFO		*psSharedPBDescKernelMemInfo,
					 PVRSRV_KERNEL_MEM_INFO		*psHWPBDescKernelMemInfo,
					 PVRSRV_KERNEL_MEM_INFO		*psBlockKernelMemInfo,
					 IMG_UINT32					ui32TotalPBSize,
					 IMG_HANDLE					*phSharedPBDesc,
					 PVRSRV_KERNEL_MEM_INFO		**ppsSharedPBDescSubKernelMemInfos,
					 IMG_UINT32					ui32SharedPBDescSubKernelMemInfosCount)
{
	PVRSRV_STUB_PBDESC *psStubPBDesc=IMG_NULL;
	PVRSRV_ERROR eRet = PVRSRV_ERROR_GENERIC;
	IMG_UINT32 i;
	PVRSRV_SGXDEV_INFO *psSGXDevInfo;
	PRESMAN_ITEM psResItem;

	
	if (psPerProcCreateSharedPB != psPerProc)
	{
		goto NoAdd;
	}
	else
	{
		PVR_ASSERT(psResItemCreateSharedPB != IMG_NULL);

		ResManFreeResByPtr(psResItemCreateSharedPB);

		PVR_ASSERT(psResItemCreateSharedPB == IMG_NULL);
		PVR_ASSERT(psPerProcCreateSharedPB == IMG_NULL);
	}

	psSGXDevInfo = (PVRSRV_SGXDEV_INFO *)((PVRSRV_DEVICE_NODE *)hDevCookie)->pvDevice;

	psStubPBDesc = psSGXDevInfo->psStubPBDescListKM;
	if (psStubPBDesc != IMG_NULL)
	{
		if(psStubPBDesc->ui32TotalPBSize != ui32TotalPBSize)
		{
			PVR_DPF((PVR_DBG_WARNING,
					"SGXAddSharedPBDescKM: Shared PB requested with different size (0x%x) from existing shared PB (0x%x) - requested size ignored",
					ui32TotalPBSize, psStubPBDesc->ui32TotalPBSize));
				
		}
		{
			
			psResItem = ResManRegisterRes(psPerProc->hResManContext,
										  RESMAN_TYPE_SHARED_PB_DESC,
										  psStubPBDesc,
										  0,
										  &SGXCleanupSharedPBDescCallback);
			if (psResItem == IMG_NULL)
			{
				PVR_DPF((PVR_DBG_ERROR,
					"SGXAddSharedPBDescKM: "
					"Failed to register existing shared "
					"PBDesc with the resource manager"));
				goto NoAddKeepPB;
			}

			
			psStubPBDesc->ui32RefCount++;

			*phSharedPBDesc = (IMG_HANDLE)psResItem;
			eRet = PVRSRV_OK;
			goto NoAddKeepPB;
		}
	}

	if(OSAllocMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
				  sizeof(PVRSRV_STUB_PBDESC),
				  (IMG_VOID **)&psStubPBDesc,
				  0) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "SGXAddSharedPBDescKM: Failed to alloc "
					"StubPBDesc"));
		eRet = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto NoAdd;
	}


	psStubPBDesc->ppsSubKernelMemInfos = IMG_NULL;

	if(OSAllocMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
				  sizeof(PVRSRV_KERNEL_MEM_INFO *)
				  * ui32SharedPBDescSubKernelMemInfosCount,
				  (IMG_VOID **)&psStubPBDesc->ppsSubKernelMemInfos,
				  0) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "SGXAddSharedPBDescKM: "
				 "Failed to alloc "
				 "StubPBDesc->ppsSubKernelMemInfos"));
		eRet = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto NoAdd;
	}

	if(PVRSRVDissociateMemFromResmanKM(psSharedPBDescKernelMemInfo)
	   != PVRSRV_OK)
	{
		goto NoAdd;
	}

	if(PVRSRVDissociateMemFromResmanKM(psHWPBDescKernelMemInfo)
	   != PVRSRV_OK)
	{
		goto NoAdd;
	}

	if(PVRSRVDissociateMemFromResmanKM(psBlockKernelMemInfo)
	   != PVRSRV_OK)
	{
		goto NoAdd;
	}
	
	psStubPBDesc->ui32RefCount = 1;
	psStubPBDesc->ui32TotalPBSize = ui32TotalPBSize;
	psStubPBDesc->psSharedPBDescKernelMemInfo = psSharedPBDescKernelMemInfo;
	psStubPBDesc->psHWPBDescKernelMemInfo = psHWPBDescKernelMemInfo;
	psStubPBDesc->psBlockKernelMemInfo = psBlockKernelMemInfo;

	psStubPBDesc->ui32SubKernelMemInfosCount =
		ui32SharedPBDescSubKernelMemInfosCount;
	for(i=0; i<ui32SharedPBDescSubKernelMemInfosCount; i++)
	{
		psStubPBDesc->ppsSubKernelMemInfos[i] = ppsSharedPBDescSubKernelMemInfos[i];
		if(PVRSRVDissociateMemFromResmanKM(ppsSharedPBDescSubKernelMemInfos[i])
		   != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "SGXAddSharedPBDescKM: "
					 "Failed to dissociate shared PBDesc "
					 "from process"));
			goto NoAdd;
		}
	}

	psResItem = ResManRegisterRes(psPerProc->hResManContext,
								  RESMAN_TYPE_SHARED_PB_DESC,
								  psStubPBDesc,
								  0,
								  &SGXCleanupSharedPBDescCallback);
	if (psResItem == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "SGXAddSharedPBDescKM: "
					 "Failed to register shared PBDesc "
					 " with the resource manager"));
		goto NoAdd;
	}
	psStubPBDesc->hDevCookie = hDevCookie;

	
	psStubPBDesc->psNext = psSGXDevInfo->psStubPBDescListKM;
	psSGXDevInfo->psStubPBDescListKM = psStubPBDesc;

	*phSharedPBDesc = (IMG_HANDLE)psResItem;

	return PVRSRV_OK;

NoAdd:
	if(psStubPBDesc)
	{
		if(psStubPBDesc->ppsSubKernelMemInfos)
		{
			OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
					  sizeof(PVRSRV_KERNEL_MEM_INFO *)
					  * ui32SharedPBDescSubKernelMemInfosCount,
					  psStubPBDesc->ppsSubKernelMemInfos,
					  0);
		}
		OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
				  sizeof(PVRSRV_STUB_PBDESC),
				  psStubPBDesc,
				  0);
	}

NoAddKeepPB:
	for (i = 0; i < ui32SharedPBDescSubKernelMemInfosCount; i++)
	{
		PVRSRVFreeDeviceMemKM(hDevCookie, ppsSharedPBDescSubKernelMemInfos[i]);
	}

	PVRSRVFreeSharedSysMemoryKM(psSharedPBDescKernelMemInfo);
	PVRSRVFreeDeviceMemKM(hDevCookie, psHWPBDescKernelMemInfo);

	PVRSRVFreeSharedSysMemoryKM(psBlockKernelMemInfo);

	return eRet;
}

