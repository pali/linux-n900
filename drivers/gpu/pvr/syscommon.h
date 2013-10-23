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

#ifndef _SYSCOMMON_H
#define _SYSCOMMON_H

#include "sysconfig.h"
#include "sysinfo.h"
#include "servicesint.h"
#include "queue.h"
#include "power.h"
#include "resman.h"
#include "ra.h"
#include "device.h"
#include "buffer_manager.h"


	typedef struct _SYS_DEVICE_ID_TAG {
		IMG_UINT32 uiID;
		IMG_BOOL bInUse;

	} SYS_DEVICE_ID;

#define SYS_MAX_LOCAL_DEVMEM_ARENAS	4

	typedef struct _SYS_DATA_TAG_ {
		IMG_UINT32 ui32NumDevices;
		SYS_DEVICE_ID sDeviceID[SYS_DEVICE_COUNT];
		PVRSRV_DEVICE_NODE *psDeviceNodeList;
		PVRSRV_POWER_DEV *psPowerDeviceList;
		PVRSRV_RESOURCE sPowerStateChangeResource;
		PVR_POWER_STATE eCurrentPowerState;
		PVR_POWER_STATE eFailedPowerState;
		IMG_UINT32 ui32CurrentOSPowerState;
		PVRSRV_QUEUE_INFO *psQueueList;
		PVRSRV_KERNEL_SYNC_INFO *psSharedSyncInfoList;
		IMG_PVOID pvEnvSpecificData;
		IMG_PVOID pvSysSpecificData;
		PVRSRV_RESOURCE sQProcessResource;
		IMG_VOID *pvSOCRegsBase;
		IMG_HANDLE hSOCTimerRegisterOSMemHandle;
		IMG_UINT32 *pvSOCTimerRegisterKM;
		IMG_VOID *pvSOCClockGateRegsBase;
		IMG_UINT32 ui32SOCClockGateRegsSize;
		PFN_CMD_PROC *ppfnCmdProcList[SYS_DEVICE_COUNT];

		PCOMMAND_COMPLETE_DATA *ppsCmdCompleteData[SYS_DEVICE_COUNT];

		IMG_BOOL bReProcessQueues;

		RA_ARENA *apsLocalDevMemArena[SYS_MAX_LOCAL_DEVMEM_ARENAS];

		IMG_CHAR *pszVersionString;
		PVRSRV_EVENTOBJECT *psGlobalEventObject;
	} SYS_DATA;

	PVRSRV_ERROR SysInitialise(IMG_VOID);
	PVRSRV_ERROR SysFinalise(IMG_VOID);

	IMG_UINT32 GetCPUTranslatedAddress(IMG_VOID);

	PVRSRV_ERROR SysDeinitialise(SYS_DATA * psSysData);

	PVRSRV_ERROR SysGetDeviceMemoryMap(PVRSRV_DEVICE_TYPE eDeviceType,
					   IMG_VOID ** ppvDeviceMap);

	IMG_VOID SysRegisterExternalDevice(PVRSRV_DEVICE_NODE * psDeviceNode);
	IMG_VOID SysRemoveExternalDevice(PVRSRV_DEVICE_NODE * psDeviceNode);

	IMG_UINT32 SysGetInterruptSource(SYS_DATA * psSysData,
					 PVRSRV_DEVICE_NODE * psDeviceNode);

	IMG_VOID SysClearInterrupts(SYS_DATA * psSysData,
				    IMG_UINT32 ui32ClearBits);

	PVRSRV_ERROR SysResetDevice(IMG_UINT32 ui32DeviceIndex);

	PVRSRV_ERROR SysSystemPrePowerState(PVR_POWER_STATE eNewPowerState);
	PVRSRV_ERROR SysSystemPostPowerState(PVR_POWER_STATE eNewPowerState);
	PVRSRV_ERROR SysDevicePrePowerState(IMG_UINT32 ui32DeviceIndex,
					    PVR_POWER_STATE eNewPowerState,
					    PVR_POWER_STATE eCurrentPowerState);
	PVRSRV_ERROR SysDevicePostPowerState(IMG_UINT32 ui32DeviceIndex,
					     PVR_POWER_STATE eNewPowerState,
					     PVR_POWER_STATE
					     eCurrentPowerState);

	PVRSRV_ERROR SysOEMFunction(IMG_UINT32 ui32ID,
				    IMG_VOID * pvIn,
				    IMG_UINT32 ulInSize,
				    IMG_VOID * pvOut, IMG_UINT32 ulOutSize);

	IMG_DEV_PHYADDR SysCpuPAddrToDevPAddr(PVRSRV_DEVICE_TYPE eDeviceType,
					      IMG_CPU_PHYADDR cpu_paddr);
	IMG_DEV_PHYADDR SysSysPAddrToDevPAddr(PVRSRV_DEVICE_TYPE eDeviceType,
					      IMG_SYS_PHYADDR SysPAddr);
	IMG_SYS_PHYADDR SysDevPAddrToSysPAddr(PVRSRV_DEVICE_TYPE eDeviceType,
					      IMG_DEV_PHYADDR SysPAddr);
	IMG_CPU_PHYADDR SysSysPAddrToCpuPAddr(IMG_SYS_PHYADDR SysPAddr);
	IMG_SYS_PHYADDR SysCpuPAddrToSysPAddr(IMG_CPU_PHYADDR cpu_paddr);

	extern SYS_DATA *gpsSysData;


	static INLINE PVRSRV_ERROR SysAcquireData(SYS_DATA ** ppsSysData) {

		*ppsSysData = gpsSysData;

		if (!gpsSysData) {
			return PVRSRV_ERROR_GENERIC;
		}

		return PVRSRV_OK;
	}

	static INLINE PVRSRV_ERROR SysInitialiseCommon(SYS_DATA * psSysData) {
		PVRSRV_ERROR eError;

		eError = PVRSRVInit(psSysData);

		return eError;
	}

	static INLINE IMG_VOID SysDeinitialiseCommon(SYS_DATA * psSysData) {

		PVRSRVDeInit(psSysData);

		OSDestroyResource(&psSysData->sPowerStateChangeResource);
	}

#define	SysReadHWReg(p, o) OSReadHWReg(p, o)
#define SysWriteHWReg(p, o, v) OSWriteHWReg(p, o, v)


#endif
