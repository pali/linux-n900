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

#include "bufferclass_example.h"


static IMG_VOID *gpvAnchor = IMG_NULL;
static PFN_BC_GET_PVRJTABLE pfnGetPVRJTable = IMG_NULL;

BC_EXAMPLE_DEVINFO * GetAnchorPtr(IMG_VOID)
{
	return (BC_EXAMPLE_DEVINFO *)gpvAnchor;
}

static IMG_VOID SetAnchorPtr(BC_EXAMPLE_DEVINFO *psDevInfo)
{
	gpvAnchor = (IMG_VOID*)psDevInfo;
}


static PVRSRV_ERROR OpenBCDevice(IMG_HANDLE *phDevice)
{
	BC_EXAMPLE_DEVINFO *psDevInfo;

	psDevInfo = GetAnchorPtr();

	
	*phDevice = (IMG_HANDLE)psDevInfo;

	return PVRSRV_OK;
}


static PVRSRV_ERROR CloseBCDevice(IMG_HANDLE hDevice)
{
	PVR_UNREFERENCED_PARAMETER(hDevice);

	return PVRSRV_OK;
}

static PVRSRV_ERROR GetBCBuffer(IMG_HANDLE			hDevice,
								IMG_UINT32			ui32BufferNumber,
								PVRSRV_SYNC_DATA	*psSyncData,
								IMG_HANDLE			*phBuffer)
{
	BC_EXAMPLE_DEVINFO	*psDevInfo;

	if(!hDevice || !phBuffer)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo = (BC_EXAMPLE_DEVINFO*)hDevice;

	if( ui32BufferNumber < psDevInfo->sBufferInfo.ui32BufferCount )
	{
		psDevInfo->psSystemBuffer[ui32BufferNumber].psSyncData = psSyncData;
		*phBuffer = (IMG_HANDLE)&psDevInfo->psSystemBuffer[ui32BufferNumber];
	}
	else
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	return PVRSRV_OK;
}


static PVRSRV_ERROR GetBCInfo(IMG_HANDLE hDevice, BUFFER_INFO *psBCInfo)
{
	BC_EXAMPLE_DEVINFO	*psDevInfo;

	if(!hDevice || !psBCInfo)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo = (BC_EXAMPLE_DEVINFO*)hDevice;

	*psBCInfo = psDevInfo->sBufferInfo;

	return PVRSRV_OK;
}


static PVRSRV_ERROR GetBCBufferAddr(IMG_HANDLE		hDevice,
									IMG_HANDLE		hBuffer,
									IMG_SYS_PHYADDR	**ppsSysAddr,
									IMG_UINT32		*pui32ByteSize,
									IMG_VOID		**ppvCpuVAddr,
									IMG_HANDLE		*phOSMapInfo,
									IMG_BOOL		*pbIsContiguous)
{
	BC_EXAMPLE_BUFFER *psBuffer;

	if(!hDevice || !hBuffer || !ppsSysAddr || !pui32ByteSize)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psBuffer = (BC_EXAMPLE_BUFFER *) hBuffer;

	*ppsSysAddr = &psBuffer->sPageAlignSysAddr;
	*ppvCpuVAddr = psBuffer->sCPUVAddr;

	*pui32ByteSize = psBuffer->ui32Size;

	*phOSMapInfo = IMG_NULL;
	*pbIsContiguous = IMG_TRUE;

	return PVRSRV_OK;
}




PVRSRV_ERROR  BC_Example_Init(IMG_VOID)
{
	BC_EXAMPLE_DEVINFO	*psDevInfo;
	IMG_CPU_PHYADDR		sSystemBufferCPUPAddr;
	IMG_UINT32 i;
	



	



	

	psDevInfo = GetAnchorPtr();

	if (psDevInfo == IMG_NULL)
	{
		
		psDevInfo = (BC_EXAMPLE_DEVINFO *)BCAllocKernelMem(sizeof(BC_EXAMPLE_DEVINFO));

		if(!psDevInfo)
		{
			return PVRSRV_ERROR_OUT_OF_MEMORY;
		}

		
		SetAnchorPtr((IMG_VOID*)psDevInfo);

		
		psDevInfo->ui32RefCount = 0;

	
		if(BCOpenPVRServices(&psDevInfo->hPVRServices) != PVRSRV_OK)
		{
			return PVRSRV_ERROR_INIT_FAILURE;
		}
		if(BCGetLibFuncAddr (psDevInfo->hPVRServices, "PVRGetBufferClassJTable", &pfnGetPVRJTable) != PVRSRV_OK)
		{
			return PVRSRV_ERROR_INIT_FAILURE;
		}

		
		if(!(*pfnGetPVRJTable)(&psDevInfo->sPVRJTable))
		{
			return PVRSRV_ERROR_INIT_FAILURE;
		}

		

		psDevInfo->ui32NumBuffers = 0;

		psDevInfo->psSystemBuffer = BCAllocKernelMem(sizeof(BC_EXAMPLE_BUFFER) * BC_EXAMPLE_NUM_BUFFERS);

		if(!psDevInfo->psSystemBuffer)
		{
			return PVRSRV_ERROR_OUT_OF_MEMORY;
		}

		

		psDevInfo->sBufferInfo.pixelformat = BC_EXAMPLE_PIXELFORMAT;
		psDevInfo->sBufferInfo.ui32Width = BC_EXAMPLE_WIDTH;
		psDevInfo->sBufferInfo.ui32Height = BC_EXAMPLE_HEIGHT;
		psDevInfo->sBufferInfo.ui32ByteStride = BC_EXAMPLE_STRIDE;		
		psDevInfo->sBufferInfo.ui32BufferDeviceID = BC_EXAMPLE_DEVICEID;
		psDevInfo->sBufferInfo.ui32Flags = PVRSRV_BC_FLAGS_YUVCSC_FULL_RANGE | PVRSRV_BC_FLAGS_YUVCSC_BT601;

		for(i=0; i < BC_EXAMPLE_NUM_BUFFERS; i++)
		{
			IMG_UINT32 ui32Size = BC_EXAMPLE_HEIGHT * BC_EXAMPLE_STRIDE;

			if(psDevInfo->sBufferInfo.pixelformat == PVRSRV_PIXEL_FORMAT_NV12)
			{
				
				ui32Size += ((BC_EXAMPLE_STRIDE >> 1) * (BC_EXAMPLE_HEIGHT >> 1) << 1);
			}

			
			if (BCAllocContigMemory(ui32Size,
								  &psDevInfo->psSystemBuffer[i].hMemHandle,
								  &psDevInfo->psSystemBuffer[i].sCPUVAddr,
								  &sSystemBufferCPUPAddr) != PVRSRV_OK)
			{
				break;
			}

			psDevInfo->ui32NumBuffers++;

			psDevInfo->psSystemBuffer[i].ui32Size = ui32Size;
			psDevInfo->psSystemBuffer[i].sSysAddr = CpuPAddrToSysPAddrBC(sSystemBufferCPUPAddr);
			psDevInfo->psSystemBuffer[i].sPageAlignSysAddr.uiAddr = (psDevInfo->psSystemBuffer[i].sSysAddr.uiAddr & 0xFFFFF000);
			psDevInfo->psSystemBuffer[i].psSyncData = IMG_NULL;
		}

		psDevInfo->sBufferInfo.ui32BufferCount = psDevInfo->ui32NumBuffers;

		

		psDevInfo->sBCJTable.ui32TableSize = sizeof(PVRSRV_BC_SRV2BUFFER_KMJTABLE);
		psDevInfo->sBCJTable.pfnOpenBCDevice = OpenBCDevice;
		psDevInfo->sBCJTable.pfnCloseBCDevice = CloseBCDevice;
		psDevInfo->sBCJTable.pfnGetBCBuffer = GetBCBuffer;
		psDevInfo->sBCJTable.pfnGetBCInfo = GetBCInfo;
		psDevInfo->sBCJTable.pfnGetBufferAddr = GetBCBufferAddr;


		
		
		if(psDevInfo->sPVRJTable.pfnPVRSRVRegisterBCDevice (&psDevInfo->sBCJTable,
															&psDevInfo->ui32DeviceID ) != PVRSRV_OK)
		{
			return PVRSRV_ERROR_DEVICE_REGISTER_FAILED;
		}
	}

	
	psDevInfo->ui32RefCount++;

	
	return PVRSRV_OK;
}


PVRSRV_ERROR BC_Example_Deinit(IMG_VOID)
{
	BC_EXAMPLE_DEVINFO *psDevInfo;
	IMG_UINT32 i;
	psDevInfo = GetAnchorPtr();

	
	if (psDevInfo == IMG_NULL)
	{
		return PVRSRV_ERROR_GENERIC;
	}
	
	psDevInfo->ui32RefCount--;

	if (psDevInfo->ui32RefCount == 0)
	{
		
		PVRSRV_BC_BUFFER2SRV_KMJTABLE	*psJTable = &psDevInfo->sPVRJTable;


		
		if (psJTable->pfnPVRSRVRemoveBCDevice(psDevInfo->ui32DeviceID) != PVRSRV_OK)
		{
			return PVRSRV_ERROR_GENERIC;
		}

		if (BCClosePVRServices(psDevInfo->hPVRServices) != PVRSRV_OK)
		{
			psDevInfo->hPVRServices = IMG_NULL;
			return PVRSRV_ERROR_GENERIC;
		}

		for(i=0; i < psDevInfo->ui32NumBuffers; i++)
		{
			BCFreeContigMemory(psDevInfo->psSystemBuffer[i].ui32Size,
							 psDevInfo->psSystemBuffer[i].hMemHandle,
							 psDevInfo->psSystemBuffer[i].sCPUVAddr,
							 SysPAddrToCpuPAddrBC(psDevInfo->psSystemBuffer[i].sSysAddr));
		}

		
		BCFreeKernelMem(psDevInfo);

		
		SetAnchorPtr(IMG_NULL);
	}

	
	return PVRSRV_OK;
}

