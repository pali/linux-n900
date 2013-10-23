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
#include "handle.h"
#include "perproc.h"
#include "pdump_km.h"
#include "ra.h"

PVRSRV_ERROR AllocateDeviceID(SYS_DATA *psSysData, IMG_UINT32 *pui32DevID)
{
	SYS_DEVICE_ID* psDeviceWalker;
	SYS_DEVICE_ID* psDeviceEnd;
	
	psDeviceWalker = &psSysData->sDeviceID[0];
	psDeviceEnd = psDeviceWalker + psSysData->ui32NumDevices;

	
	while (psDeviceWalker < psDeviceEnd)
	{
		if (!psDeviceWalker->bInUse)
		{
			psDeviceWalker->bInUse = IMG_TRUE;
			*pui32DevID = psDeviceWalker->uiID;
			return PVRSRV_OK;
		}
		psDeviceWalker++;
	}
	
	PVR_DPF((PVR_DBG_ERROR,"AllocateDeviceID: No free and valid device IDs available!"));

	
	PVR_ASSERT(psDeviceWalker < psDeviceEnd);

	return PVRSRV_ERROR_GENERIC;
}


PVRSRV_ERROR FreeDeviceID(SYS_DATA *psSysData, IMG_UINT32 ui32DevID)
{
	SYS_DEVICE_ID* psDeviceWalker;
	SYS_DEVICE_ID* psDeviceEnd;

	psDeviceWalker = &psSysData->sDeviceID[0];
	psDeviceEnd = psDeviceWalker + psSysData->ui32NumDevices;

	
	while (psDeviceWalker < psDeviceEnd)
	{
		
		if	(
				(psDeviceWalker->uiID == ui32DevID) &&
				(psDeviceWalker->bInUse)
			)
		{
			psDeviceWalker->bInUse = IMG_FALSE;
			return PVRSRV_OK;
		}
		psDeviceWalker++;
	}
	
	PVR_DPF((PVR_DBG_ERROR,"FreeDeviceID: no matching dev ID that is in use!"));

	
	PVR_ASSERT(psDeviceWalker < psDeviceEnd);

	return PVRSRV_ERROR_GENERIC;
}


#ifndef ReadHWReg
IMG_EXPORT
IMG_UINT32 ReadHWReg(IMG_PVOID pvLinRegBaseAddr, IMG_UINT32 ui32Offset)
{
	return *(volatile IMG_UINT32*)((IMG_UINT32)pvLinRegBaseAddr+ui32Offset);
}
#endif


#ifndef WriteHWReg
IMG_EXPORT
IMG_VOID WriteHWReg(IMG_PVOID pvLinRegBaseAddr, IMG_UINT32 ui32Offset, IMG_UINT32 ui32Value)
{
	PVR_DPF((PVR_DBG_MESSAGE,"WriteHWReg Base:%x, Offset: %x, Value %x",pvLinRegBaseAddr,ui32Offset,ui32Value));

	*(IMG_UINT32*)((IMG_UINT32)pvLinRegBaseAddr+ui32Offset) = ui32Value;
}
#endif


#ifndef WriteHWRegs
IMG_EXPORT
IMG_VOID WriteHWRegs(IMG_PVOID pvLinRegBaseAddr, IMG_UINT32 ui32Count, PVRSRV_HWREG *psHWRegs)
{
	while (ui32Count--)
	{
		WriteHWReg (pvLinRegBaseAddr, psHWRegs->ui32RegAddr, psHWRegs->ui32RegVal);
		psHWRegs++;
	}
}
#endif


IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVEnumerateDevicesKM(IMG_UINT32 *pui32NumDevices,
											 	   PVRSRV_DEVICE_IDENTIFIER *psDevIdList)
{
	PVRSRV_ERROR		eError;
	SYS_DATA			*psSysData;
	PVRSRV_DEVICE_NODE	*psDeviceNode;
	IMG_UINT32 			i;
	
	if (!pui32NumDevices || !psDevIdList)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVEnumerateDevicesKM: Invalid params"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	eError = SysAcquireData(&psSysData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVEnumerateDevicesKM: Failed to get SysData"));
		return eError;
	}

	

	for (i=0; i<PVRSRV_MAX_DEVICES; i++)
	{
		psDevIdList[i].eDeviceType = PVRSRV_DEVICE_TYPE_UNKNOWN;
	}
	
	
	*pui32NumDevices = 0;
	
	



	psDeviceNode = psSysData->psDeviceNodeList;
	for (i=0; psDeviceNode != IMG_NULL; i++)
	{
		
		if(psDeviceNode->sDevId.eDeviceType != PVRSRV_DEVICE_TYPE_EXT)
		{
			
			*psDevIdList++ = psDeviceNode->sDevId;
			
			(*pui32NumDevices)++;
		}
		psDeviceNode = psDeviceNode->psNext;
	}
	
	return PVRSRV_OK;
}


PVRSRV_ERROR IMG_CALLCONV PVRSRVInit(PSYS_DATA psSysData)
{
	PVRSRV_ERROR	eError;

	
	eError = ResManInit();
	if (eError != PVRSRV_OK)
	{
		goto Error;
	}

	eError = PVRSRVPerProcessDataInit();
	if(eError != PVRSRV_OK)
	{
		goto Error;
	}

	
	eError = PVRSRVHandleInit();
	if(eError != PVRSRV_OK)
	{
		goto Error;
	}

	
	eError = OSCreateResource(&psSysData->sPowerStateChangeResource);
	if (eError != PVRSRV_OK)
	{
		goto Error;
	}

	
	gpsSysData->eCurrentPowerState = PVRSRV_POWER_STATE_D0;
	gpsSysData->eFailedPowerState = PVRSRV_POWER_Unspecified;

	
	if(OSAllocMem( PVRSRV_OS_PAGEABLE_HEAP, 
					 sizeof(PVRSRV_EVENTOBJECT) , 
					 (IMG_VOID **)&psSysData->psGlobalEventObject, 0) != PVRSRV_OK)	
	{
		
		goto Error;
	}

	if(OSEventObjectCreate("PVRSRV_GLOBAL_EVENTOBJECT", psSysData->psGlobalEventObject) != PVRSRV_OK)
	{
		goto Error;	
	}

	return eError;
	
Error:
	PVRSRVDeInit(psSysData);
	return eError;
}



IMG_VOID IMG_CALLCONV PVRSRVDeInit(PSYS_DATA psSysData)
{
	PVRSRV_ERROR	eError;
	
	PVR_UNREFERENCED_PARAMETER(psSysData);

	
	if(psSysData->psGlobalEventObject)
	{
		OSEventObjectDestroy(psSysData->psGlobalEventObject);
		OSFreeMem( PVRSRV_OS_PAGEABLE_HEAP, 
						 sizeof(PVRSRV_EVENTOBJECT) , 
						 psSysData->psGlobalEventObject, 0);
	}

	eError = PVRSRVHandleDeInit();
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVDeInit: PVRSRVHandleDeInit failed"));
	}

	eError = PVRSRVPerProcessDataDeInit();
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVDeInit: PVRSRVPerProcessDataDeInit failed"));
	}
	
	ResManDeInit();
}


PVRSRV_ERROR IMG_CALLCONV PVRSRVRegisterDevice(PSYS_DATA psSysData,  
											  PVRSRV_ERROR (*pfnRegisterDevice)(PVRSRV_DEVICE_NODE*),
											  IMG_UINT32 ui32SOCInterruptBit,
			 								  IMG_UINT32 *pui32DeviceIndex)
{
	PVRSRV_ERROR		eError;
	PVRSRV_DEVICE_NODE	*psDeviceNode;
	
	
	if(OSAllocMem( PVRSRV_OS_NON_PAGEABLE_HEAP, 
					 sizeof(PVRSRV_DEVICE_NODE), 
					 (IMG_VOID **)&psDeviceNode, IMG_NULL) != PVRSRV_OK)	
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRegisterDevice : Failed to alloc memory for psDeviceNode"));
		return (PVRSRV_ERROR_OUT_OF_MEMORY);
	}
	OSMemSet (psDeviceNode, 0, sizeof(PVRSRV_DEVICE_NODE));	

	eError = pfnRegisterDevice(psDeviceNode);
	if (eError != PVRSRV_OK)
	{
		OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
					0, psDeviceNode, IMG_NULL);
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRegisterDevice : Failed to register device"));
		return (PVRSRV_ERROR_DEVICE_REGISTER_FAILED);
	}

	




	psDeviceNode->ui32RefCount = 1;
	psDeviceNode->psSysData = psSysData;
	psDeviceNode->ui32SOCInterruptBit = ui32SOCInterruptBit;
	
	
	AllocateDeviceID(psSysData, &psDeviceNode->sDevId.ui32DeviceIndex);
		
	
	psDeviceNode->psNext = psSysData->psDeviceNodeList;
	psSysData->psDeviceNodeList = psDeviceNode;

	
	*pui32DeviceIndex = psDeviceNode->sDevId.ui32DeviceIndex;
	
	return PVRSRV_OK;
}


PVRSRV_ERROR IMG_CALLCONV PVRSRVInitialiseDevice (IMG_UINT32 ui32DevIndex)
{
	PVRSRV_DEVICE_NODE	*psDeviceNode;
	SYS_DATA			*psSysData;
	PVRSRV_ERROR		eError;

	PVR_DPF((PVR_DBG_MESSAGE, "PVRSRVInitialiseDevice"));

	eError = SysAcquireData(&psSysData);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVInitialiseDevice: Failed to get SysData"));
		return(eError);
	}

	
	psDeviceNode = psSysData->psDeviceNodeList;

	while (psDeviceNode)
	{
		if (psDeviceNode->sDevId.ui32DeviceIndex == ui32DevIndex)
		{
			goto FoundDevice;
		}
		psDeviceNode = psDeviceNode->psNext;
	}

	
	PVR_DPF((PVR_DBG_ERROR,"PVRSRVInitialiseDevice: requested device is not present"));
	return PVRSRV_ERROR_INIT_FAILURE;
	
FoundDevice:

	PVR_ASSERT (psDeviceNode->ui32RefCount > 0);

	

	eError = PVRSRVResManConnect(IMG_NULL, &psDeviceNode->hResManContext);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVInitialiseDevice: Failed PVRSRVResManConnect call"));
		return eError;
	}
	
	
	if(psDeviceNode->pfnInitDevice != IMG_NULL)
	{
		eError = psDeviceNode->pfnInitDevice(psDeviceNode);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"PVRSRVInitialiseDevice: Failed InitDevice call"));
			return eError;
		}
	}

	return PVRSRV_OK;
}


PVRSRV_ERROR IMG_CALLCONV PVRSRVFinaliseSystem(IMG_BOOL bInitSuccessful)
{
	PVRSRV_DEVICE_NODE	*psDeviceNode;
	SYS_DATA		*psSysData;
	PVRSRV_ERROR		eError;

	PVR_DPF((PVR_DBG_MESSAGE, "PVRSRVFinaliseSystem"));

	eError = SysAcquireData(&psSysData);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVFinaliseSystem: Failed to get SysData"));
		return(eError);
	}

	if (bInitSuccessful)
	{
		eError = SysFinalise();
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"PVRSRVFinaliseSystem: SysFinalise failed (%d)", eError));
			return eError;
		}

		
		psDeviceNode = psSysData->psDeviceNodeList;
		while (psDeviceNode)
		{
			eError = PVRSRVSetDevicePowerStateKM(psDeviceNode->sDevId.ui32DeviceIndex,
															 PVRSRV_POWER_Unspecified,
															 KERNEL_ID, IMG_FALSE);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,"PVRSRVFinaliseSystem: Failed PVRSRVSetDevicePowerStateKM call (device index: %d)", psDeviceNode->sDevId.ui32DeviceIndex));
			}
			psDeviceNode = psDeviceNode->psNext;
		}
	}

	



	PDUMPENDINITPHASE();

	return PVRSRV_OK;
}


IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVAcquireDeviceDataKM (IMG_UINT32			ui32DevIndex,
													 PVRSRV_DEVICE_TYPE	eDeviceType,
													 IMG_HANDLE			*phDevCookie)
{
	PVRSRV_DEVICE_NODE	*psDeviceNode;
	SYS_DATA			*psSysData;
	PVRSRV_ERROR		eError;

	PVR_DPF((PVR_DBG_MESSAGE, "PVRSRVAcquireDeviceDataKM"));

	eError = SysAcquireData(&psSysData);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVAcquireDeviceDataKM: Failed to get SysData"));
		return(eError);
	}

	
	psDeviceNode = psSysData->psDeviceNodeList;

	if (eDeviceType != PVRSRV_DEVICE_TYPE_UNKNOWN)
	{
		while (psDeviceNode)
		{
			if (psDeviceNode->sDevId.eDeviceType == eDeviceType)
			{
				goto FoundDevice;
			}
			psDeviceNode = psDeviceNode->psNext;
		}
	}
	else
	{
		while (psDeviceNode)
		{
			if (psDeviceNode->sDevId.ui32DeviceIndex == ui32DevIndex)
			{
				goto FoundDevice;
			}
			psDeviceNode = psDeviceNode->psNext;
		}
	}

	
	PVR_DPF((PVR_DBG_ERROR,"PVRSRVAcquireDeviceDataKM: requested device is not present"));
	return PVRSRV_ERROR_INIT_FAILURE;

FoundDevice:

	PVR_ASSERT (psDeviceNode->ui32RefCount > 0);

	
	if (phDevCookie)
	{
		*phDevCookie = (IMG_HANDLE)psDeviceNode;
	}

	return PVRSRV_OK;
}


PVRSRV_ERROR IMG_CALLCONV PVRSRVDeinitialiseDevice(IMG_UINT32 ui32DevIndex)
{
	PVRSRV_DEVICE_NODE	*psDeviceNode;
	PVRSRV_DEVICE_NODE	**ppsDevNode;
	SYS_DATA			*psSysData;
	PVRSRV_ERROR		eError;

	eError = SysAcquireData(&psSysData);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVDeinitialiseDevice: Failed to get SysData"));
		return(eError);
	}

	ppsDevNode = &psSysData->psDeviceNodeList;
	while(*ppsDevNode)
	{
		if((*ppsDevNode)->sDevId.ui32DeviceIndex == ui32DevIndex)
		{
			psDeviceNode = *ppsDevNode;
			goto FoundDevice;
		}
		ppsDevNode = &((*ppsDevNode)->psNext);
	}

	PVR_DPF((PVR_DBG_ERROR,"PVRSRVDeinitialiseDevice: requested device %d is not present", ui32DevIndex));

	return PVRSRV_ERROR_GENERIC;

FoundDevice:

	

	eError = PVRSRVSetDevicePowerStateKM(ui32DevIndex,
										 PVRSRV_POWER_STATE_D3,
										 KERNEL_ID,
										 IMG_FALSE);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVDeinitialiseDevice: Failed PVRSRVSetDevicePowerStateKM call"));
		return eError;
	}

	

	eError = ResManFreeResByCriteria(psDeviceNode->hResManContext,
									 RESMAN_CRITERIA_RESTYPE,
									 RESMAN_TYPE_DEVICEMEM_ALLOCATION,
									 IMG_NULL, 0);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVDeinitialiseDevice: Failed ResManFreeResByCriteria call"));
		return eError;
	}

	

	if(psDeviceNode->pfnDeInitDevice != IMG_NULL)
	{
		eError = psDeviceNode->pfnDeInitDevice(psDeviceNode);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"PVRSRVDeinitialiseDevice: Failed DeInitDevice call"));
			return eError;
		}
	}

	

	PVRSRVResManDisconnect(psDeviceNode->hResManContext, IMG_TRUE);
	psDeviceNode->hResManContext = IMG_NULL;

	
	*ppsDevNode = psDeviceNode->psNext;

		
	FreeDeviceID(psSysData, ui32DevIndex);	
	OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
				0, psDeviceNode, IMG_NULL);
	
	return (PVRSRV_OK);
}


IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PollForValueKM (volatile IMG_UINT32* pui32LinMemAddr,
										  IMG_UINT32 ui32Value,
										  IMG_UINT32 ui32Mask,
										  IMG_UINT32 ui32Waitus,
										  IMG_UINT32 ui32Tries)
{
	IMG_BOOL	bStart = IMG_FALSE;
	IMG_UINT32	uiStart = 0, uiCurrent=0, uiMaxTime;

	uiMaxTime = ui32Tries * ui32Waitus;

	
	do
	{
		if((*pui32LinMemAddr & ui32Mask) == ui32Value)
		{
			return PVRSRV_OK;
		}

		if (bStart == IMG_FALSE)
		{
			bStart = IMG_TRUE;
			uiStart = OSClockus();
		}

		OSWaitus(ui32Waitus);

		uiCurrent = OSClockus();
		if (uiCurrent < uiStart)
		{
			
			uiStart = 0;
		}

	} while ((uiCurrent - uiStart) < uiMaxTime); 



	return PVRSRV_ERROR_GENERIC;
}


#if defined (USING_ISR_INTERRUPTS)

extern IMG_UINT32 gui32EventStatusServicesByISR;

PVRSRV_ERROR PollForInterruptKM (IMG_UINT32 ui32Value,
								 IMG_UINT32 ui32Mask,
								 IMG_UINT32 ui32Waitus,
								 IMG_UINT32 ui32Tries)
{
	IMG_BOOL	bStart = IMG_FALSE;
	IMG_UINT32	uiStart = 0, uiCurrent=0, uiMaxTime;

	uiMaxTime = ui32Tries * ui32Waitus;

	
	do
	{
		if ((gui32EventStatusServicesByISR & ui32Mask) == ui32Value)
		{
			gui32EventStatusServicesByISR = 0;
			return PVRSRV_OK;
		}

		if (bStart == IMG_FALSE)
		{
			bStart = IMG_TRUE;
			uiStart = OSClockus();
		}

		OSWaitus(ui32Waitus);

		uiCurrent = OSClockus();
		if (uiCurrent < uiStart)
		{
			
			uiStart = 0;
		}

	} while ((uiCurrent - uiStart) < uiMaxTime); 

	return PVRSRV_ERROR_GENERIC;
}
#endif  


IMG_EXPORT			
PVRSRV_ERROR IMG_CALLCONV PVRSRVGetMiscInfoKM(PVRSRV_MISC_INFO *psMiscInfo)
{
	SYS_DATA *psSysData;
	PVRSRV_ERROR eError;
	
	if(!psMiscInfo)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVGetMiscInfoKM: invalid parameters"));		
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
	
	
	if(psMiscInfo->ui32StateRequest & ~(PVRSRV_MISC_INFO_TIMER_PRESENT
										|PVRSRV_MISC_INFO_CLOCKGATE_PRESENT
										|PVRSRV_MISC_INFO_MEMSTATS_PRESENT
										|PVRSRV_MISC_INFO_GLOBALEVENTOBJECT_PRESENT))
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVGetMiscInfoKM: invalid state request flags"));
		return PVRSRV_ERROR_INVALID_PARAMS;			
	}

	eError = SysAcquireData(&psSysData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVGetMiscInfoKM: Failed to get SysData"));		
		return eError;	
	}
	
	psMiscInfo->ui32StatePresent = 0;

	
	if((psMiscInfo->ui32StateRequest & PVRSRV_MISC_INFO_TIMER_PRESENT)
	&& psSysData->pvSOCTimerRegisterKM)
	{
		psMiscInfo->ui32StatePresent |= PVRSRV_MISC_INFO_TIMER_PRESENT;
		psMiscInfo->pvSOCTimerRegisterKM = psSysData->pvSOCTimerRegisterKM;
		psMiscInfo->hSOCTimerRegisterOSMemHandle = psSysData->hSOCTimerRegisterOSMemHandle;
	}

	
	if((psMiscInfo->ui32StateRequest & PVRSRV_MISC_INFO_CLOCKGATE_PRESENT)
	&& psSysData->pvSOCClockGateRegsBase)
	{
		psMiscInfo->ui32StatePresent |= PVRSRV_MISC_INFO_CLOCKGATE_PRESENT;
		psMiscInfo->pvSOCClockGateRegs = psSysData->pvSOCClockGateRegsBase;
		psMiscInfo->ui32SOCClockGateRegsSize = psSysData->ui32SOCClockGateRegsSize;
	}

	
	if((psMiscInfo->ui32StateRequest & PVRSRV_MISC_INFO_MEMSTATS_PRESENT)
	&& psMiscInfo->pszMemoryStr)
	{
		RA_ARENA			**ppArena;
		BM_HEAP				*psBMHeap;
    	BM_CONTEXT			*psBMContext;
		PVRSRV_DEVICE_NODE	*psDeviceNode;
		IMG_CHAR			*pszStr;
		IMG_UINT32			ui32StrLen;
		IMG_INT32			i32Count;
		
		pszStr = psMiscInfo->pszMemoryStr;
		ui32StrLen = psMiscInfo->ui32MemoryStrLen;
  
		psMiscInfo->ui32StatePresent |= PVRSRV_MISC_INFO_MEMSTATS_PRESENT;

		
		ppArena = &psSysData->apsLocalDevMemArena[0];
		while(*ppArena)
		{
			CHECK_SPACE(ui32StrLen);
			i32Count = OSSNPrintf(pszStr, 100, "\nLocal Backing Store:\n");
			UPDATE_SPACE(pszStr, i32Count, ui32StrLen);
			
			RA_GetStats(*ppArena,
							&pszStr, 
							&ui32StrLen);
			
			ppArena++;
		}

		
		psDeviceNode = psSysData->psDeviceNodeList;
		while(psDeviceNode)
		{
			CHECK_SPACE(ui32StrLen);
			i32Count = OSSNPrintf(pszStr, 100, "\n\nDevice Type %d:\n", psDeviceNode->sDevId.eDeviceType);
			UPDATE_SPACE(pszStr, i32Count, ui32StrLen);

			
			if(psDeviceNode->sDevMemoryInfo.pBMKernelContext)
			{
				CHECK_SPACE(ui32StrLen);
				i32Count = OSSNPrintf(pszStr, 100, "\nKernel Context:\n");
				UPDATE_SPACE(pszStr, i32Count, ui32StrLen);
				
				psBMHeap = psDeviceNode->sDevMemoryInfo.pBMKernelContext->psBMHeap;
				while(psBMHeap)
				{
					if(psBMHeap->pImportArena)
					{
						RA_GetStats(psBMHeap->pImportArena,
										&pszStr, 
										&ui32StrLen);
					}

					if(psBMHeap->pVMArena)
					{
						RA_GetStats(psBMHeap->pVMArena,
										&pszStr, 
										&ui32StrLen);
					}
					psBMHeap = psBMHeap->psNext;
				}
			}

			
			psBMContext = psDeviceNode->sDevMemoryInfo.pBMContext;
			while(psBMContext)
			{
				CHECK_SPACE(ui32StrLen);
				i32Count = OSSNPrintf(pszStr, 100, "\nApplication Context (hDevMemContext) 0x%08X:\n", (IMG_HANDLE)psBMContext);
				UPDATE_SPACE(pszStr, i32Count, ui32StrLen);

				psBMHeap = psBMContext->psBMHeap;
				while(psBMHeap)
				{
					if(psBMHeap->pImportArena)
					{
						RA_GetStats(psBMHeap->pImportArena,
										&pszStr, 
										&ui32StrLen);
					}

					if(psBMHeap->pVMArena)
					{
						RA_GetStats(psBMHeap->pVMArena,
										&pszStr, 
										&ui32StrLen);
					}
					psBMHeap = psBMHeap->psNext;
				}
				psBMContext = psBMContext->psNext;
			}
			psDeviceNode = psDeviceNode->psNext;
		}

		
		i32Count = OSSNPrintf(pszStr, 100, "\n\0");
		UPDATE_SPACE(pszStr, i32Count, ui32StrLen);
	}

	if((psMiscInfo->ui32StateRequest & PVRSRV_MISC_INFO_GLOBALEVENTOBJECT_PRESENT)
	&& psSysData->psGlobalEventObject)
	{
		psMiscInfo->ui32StatePresent |= PVRSRV_MISC_INFO_GLOBALEVENTOBJECT_PRESENT;
		psMiscInfo->sGlobalEventObject = *psSysData->psGlobalEventObject;
	}
	
	return PVRSRV_OK;
}


PVRSRV_ERROR IMG_CALLCONV PVRSRVGetFBStatsKM(IMG_UINT32		*pui32Total, 
											 IMG_UINT32		*pui32Available)
{
	IMG_UINT32 ui32Total = 0, i = 0;
	IMG_UINT32 ui32Available = 0;

	*pui32Total		= 0;
	*pui32Available = 0;

	
	while(BM_ContiguousStatistics(i, &ui32Total, &ui32Available) == IMG_TRUE)
	{
		*pui32Total		+= ui32Total;
		*pui32Available += ui32Available;

		i++;
	}

	return PVRSRV_OK;
}


IMG_BOOL IMG_CALLCONV PVRSRVDeviceLISR(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	SYS_DATA			*psSysData;
	IMG_BOOL			bStatus = IMG_FALSE;
	IMG_UINT32			ui32InterruptSource;

	if(!psDeviceNode)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVDeviceLISR: Invalid params\n"));
		goto out;
	}
	psSysData = psDeviceNode->psSysData;

	
	ui32InterruptSource = SysGetInterruptSource(psSysData, psDeviceNode);
	if(ui32InterruptSource & psDeviceNode->ui32SOCInterruptBit)
	{
		if(psDeviceNode->pfnDeviceISR != IMG_NULL)
		{
			bStatus = (*psDeviceNode->pfnDeviceISR)(psDeviceNode->pvISRData);		
		}

		SysClearInterrupts(psSysData, psDeviceNode->ui32SOCInterruptBit);
	}

out:
	return bStatus;
}


IMG_BOOL IMG_CALLCONV PVRSRVSystemLISR(IMG_VOID *pvSysData)
{
	SYS_DATA			*psSysData = pvSysData;
	IMG_BOOL			bStatus = IMG_FALSE;
	IMG_UINT32			ui32InterruptSource;
	IMG_UINT32			ui32ClearInterrupts = 0;
	PVRSRV_DEVICE_NODE	*psDeviceNode;

	if(!psSysData)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVSystemLISR: Invalid params\n"));
		goto out;
	}

	
	ui32InterruptSource = SysGetInterruptSource(psSysData, IMG_NULL);
	
	
	if(ui32InterruptSource == 0)
	{
		goto out;
	}
	
	
	psDeviceNode = psSysData->psDeviceNodeList;
	while(psDeviceNode != IMG_NULL)
	{
		if(psDeviceNode->pfnDeviceISR != IMG_NULL)
		{
			if(ui32InterruptSource & psDeviceNode->ui32SOCInterruptBit)
			{
				if((*psDeviceNode->pfnDeviceISR)(psDeviceNode->pvISRData))
				{
					
					bStatus = IMG_TRUE;
				}
				
				ui32ClearInterrupts |= psDeviceNode->ui32SOCInterruptBit;
			}
		}
		psDeviceNode = psDeviceNode->psNext;
	}

	SysClearInterrupts(psSysData, ui32ClearInterrupts);
	
out:
	return bStatus;
}


IMG_VOID IMG_CALLCONV PVRSRVMISR(IMG_VOID *pvSysData)
{
	SYS_DATA			*psSysData = pvSysData;
	PVRSRV_DEVICE_NODE	*psDeviceNode;

	if(!psSysData)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVMISR: Invalid params\n"));
		return;
	}

	
	psDeviceNode = psSysData->psDeviceNodeList;
	while(psDeviceNode != IMG_NULL)
	{
		if(psDeviceNode->pfnDeviceMISR != IMG_NULL)
		{
			(*psDeviceNode->pfnDeviceMISR)(psDeviceNode->pvISRData);
		}
		psDeviceNode = psDeviceNode->psNext;
	}

	
	if (PVRSRVProcessQueues(ISR_ID, IMG_FALSE) == PVRSRV_ERROR_PROCESSING_BLOCKED)
	{
		PVRSRVProcessQueues(ISR_ID, IMG_FALSE);
	}
	
	
	if (psSysData->psGlobalEventObject)
	{
		IMG_HANDLE hOSEventKM = psSysData->psGlobalEventObject->hOSEventKM;
		if(hOSEventKM)
		{
			OSEventObjectSignal(hOSEventKM);
		}
	}	
}


IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVProcessConnect(IMG_UINT32	ui32PID)
{
	return PVRSRVPerProcessDataConnect(ui32PID);
}


IMG_EXPORT
IMG_VOID IMG_CALLCONV PVRSRVProcessDisconnect(IMG_UINT32	ui32PID)
{
	PVRSRVPerProcessDataDisconnect(ui32PID);
}


PVRSRV_ERROR IMG_CALLCONV PVRSRVSaveRestoreLiveSegments(IMG_HANDLE hArena, IMG_PBYTE pbyBuffer, 
														IMG_UINT32 *puiBufSize, IMG_BOOL bSave)
{
	IMG_UINT32         uiBytesSaved = 0;
	IMG_PVOID          pvLocalMemCPUVAddr;
	RA_SEGMENT_DETAILS sSegDetails;

	if (hArena == IMG_NULL)
	{
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}

	sSegDetails.uiSize = 0;
	sSegDetails.sCpuPhyAddr.uiAddr = 0;
	sSegDetails.hSegment = 0;

	
	while (RA_GetNextLiveSegment(hArena, &sSegDetails))
	{
		if (pbyBuffer == IMG_NULL)
		{
			
			uiBytesSaved += sizeof(sSegDetails.uiSize) + sSegDetails.uiSize;
		}
		else
		{
			if ((uiBytesSaved + sizeof(sSegDetails.uiSize) + sSegDetails.uiSize) > *puiBufSize)
			{
				return (PVRSRV_ERROR_OUT_OF_MEMORY);
			}

			PVR_DPF((PVR_DBG_MESSAGE, "PVRSRVSaveRestoreLiveSegments: Base %08x size %08x", sSegDetails.sCpuPhyAddr.uiAddr, sSegDetails.uiSize));

			
			pvLocalMemCPUVAddr = OSMapPhysToLin(sSegDetails.sCpuPhyAddr,
									sSegDetails.uiSize,
									PVRSRV_HAP_KERNEL_ONLY|PVRSRV_HAP_UNCACHED,
									IMG_NULL);
			if (pvLocalMemCPUVAddr == IMG_NULL)
			{
				PVR_DPF((PVR_DBG_ERROR, "PVRSRVSaveRestoreLiveSegments: Failed to map local memory to host"));
				return (PVRSRV_ERROR_OUT_OF_MEMORY);
			}

			if (bSave)
			{
				
				OSMemCopy(pbyBuffer, &sSegDetails.uiSize, sizeof(sSegDetails.uiSize));
				pbyBuffer += sizeof(sSegDetails.uiSize);

				OSMemCopy(pbyBuffer, pvLocalMemCPUVAddr, sSegDetails.uiSize);
				pbyBuffer += sSegDetails.uiSize;
			}
			else
			{
				IMG_UINT32 uiSize;
				
				OSMemCopy(&uiSize, pbyBuffer, sizeof(sSegDetails.uiSize));

				if (uiSize != sSegDetails.uiSize)
				{
					PVR_DPF((PVR_DBG_ERROR, "PVRSRVSaveRestoreLiveSegments: Segment size error"));
				}
				else
				{
					pbyBuffer += sizeof(sSegDetails.uiSize);

					OSMemCopy(pvLocalMemCPUVAddr, pbyBuffer, sSegDetails.uiSize);
					pbyBuffer += sSegDetails.uiSize;
				}
			}


			uiBytesSaved += sizeof(sSegDetails.uiSize) + sSegDetails.uiSize;

			OSUnMapPhysToLin(pvLocalMemCPUVAddr,
		                     sSegDetails.uiSize,
		                     PVRSRV_HAP_KERNEL_ONLY|PVRSRV_HAP_UNCACHED,
		                     IMG_NULL);
		}
	}

	if (pbyBuffer == IMG_NULL)
	{
		*puiBufSize = uiBytesSaved;
	}

	return (PVRSRV_OK);
}


