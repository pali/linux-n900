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

#ifndef __BC_EXAMPLE_H__
#define __BC_EXAMPLE_H__

#include "img_defs.h"
#include "servicesext.h"
#include "kernelbuffer.h"


	extern IMG_IMPORT IMG_BOOL
	    PVRGetBufferClassJTable(PVRSRV_BC_BUFFER2SRV_KMJTABLE * psJTable);

#define BC_EXAMPLE_NUM_BUFFERS	3

#define YUV420 1
#ifdef YUV420

#define BC_EXAMPLE_WIDTH		(320)
#define BC_EXAMPLE_HEIGHT		(160)
#define BC_EXAMPLE_STRIDE		(320)
#define BC_EXAMPLE_PIXELFORMAT	(PVRSRV_PIXEL_FORMAT_NV12)

#else

#define BC_EXAMPLE_WIDTH		(320)
#define BC_EXAMPLE_HEIGHT		(160)
#define BC_EXAMPLE_STRIDE		(320*2)
#define BC_EXAMPLE_PIXELFORMAT	(PVRSRV_PIXEL_FORMAT_RGB565)

#endif

#define BC_EXAMPLE_DEVICEID		 0

	typedef struct BC_EXAMPLE_BUFFER_TAG {
		IMG_UINT32 ui32Size;
		IMG_HANDLE hMemHandle;
		IMG_SYS_PHYADDR sSysAddr;
		IMG_SYS_PHYADDR sPageAlignSysAddr;
		IMG_CPU_VIRTADDR sCPUVAddr;
		PVRSRV_SYNC_DATA *psSyncData;
		struct BC_EXAMPLE_BUFFER_TAG *psNext;
	} BC_EXAMPLE_BUFFER;

	typedef struct BC_EXAMPLE_DEVINFO_TAG {
		IMG_UINT32 ui32DeviceID;

		BC_EXAMPLE_BUFFER *psSystemBuffer;

		BUFFER_INFO sBufferInfo;

		IMG_UINT32 ui32NumBuffers;

		PVRSRV_BC_BUFFER2SRV_KMJTABLE sPVRJTable;

		PVRSRV_BC_SRV2BUFFER_KMJTABLE sBCJTable;

		IMG_HANDLE hPVRServices;

		IMG_UINT32 ui32RefCount;

	} BC_EXAMPLE_DEVINFO;

	PVRSRV_ERROR BC_Example_Init(IMG_VOID);
	PVRSRV_ERROR BC_Example_Deinit(IMG_VOID);

	PVRSRV_ERROR BCOpenPVRServices(IMG_HANDLE * phPVRServices);
	PVRSRV_ERROR BCClosePVRServices(IMG_HANDLE hPVRServices);

	IMG_VOID *BCAllocKernelMem(IMG_UINT32 ui32Size);
	IMG_VOID BCFreeKernelMem(IMG_VOID * pvMem);

	PVRSRV_ERROR BCAllocContigMemory(IMG_UINT32 ui32Size,
					 IMG_HANDLE * phMemHandle,
					 IMG_CPU_VIRTADDR * pLinAddr,
					 IMG_CPU_PHYADDR * pPhysAddr);
	IMG_VOID BCFreeContigMemory(IMG_UINT32 ui32Size,
				    IMG_HANDLE hMemHandle,
				    IMG_CPU_VIRTADDR LinAddr,
				    IMG_CPU_PHYADDR PhysAddr);

	IMG_SYS_PHYADDR CpuPAddrToSysPAddrBC(IMG_CPU_PHYADDR cpu_paddr);
	IMG_CPU_PHYADDR SysPAddrToCpuPAddrBC(IMG_SYS_PHYADDR sys_paddr);

	IMG_VOID *MapPhysAddr(IMG_SYS_PHYADDR sSysAddr, IMG_UINT32 ui32Size);
	IMG_VOID UnMapPhysAddr(IMG_VOID * pvAddr, IMG_UINT32 ui32Size);

	PVRSRV_ERROR BCGetLibFuncAddr(IMG_HANDLE hExtDrv,
				      IMG_CHAR * szFunctionName,
				      PFN_BC_GET_PVRJTABLE * ppfnFuncTable);
	BC_EXAMPLE_DEVINFO *GetAnchorPtr(IMG_VOID);

#endif
