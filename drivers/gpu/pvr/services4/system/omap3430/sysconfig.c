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
#include "kerneldisplay.h"
#include "oemfuncs.h"
#include "sgxinfo.h"
#include "pdump_km.h"
#include "sgxinfokm.h"
#include "syslocal.h"
#include "sysconfig.h"

SYS_DATA* gpsSysData = (SYS_DATA*)IMG_NULL;
SYS_DATA  gsSysData;

static SYS_SPECIFIC_DATA gsSysSpecificData;
SYS_SPECIFIC_DATA *gpsSysSpecificData;

static IMG_UINT32	gui32SGXDeviceID;
static SGX_DEVICE_MAP	gsSGXDeviceMap;
static PVRSRV_DEVICE_NODE *gpsSGXDevNode;

#define DEVICE_SGX_INTERRUPT (1 << 0)

#if defined(NO_HARDWARE)
static IMG_CPU_VIRTADDR gsSGXRegsCPUVAddr;
#endif

IMG_UINT32 PVRSRV_BridgeDispatchKM(IMG_UINT32	Ioctl,
								   IMG_BYTE		*pInBuf,
								   IMG_UINT32	InBufLen,
								   IMG_BYTE		*pOutBuf,
								   IMG_UINT32	OutBufLen,
								   IMG_UINT32	*pdwBytesTransferred);

static PVRSRV_ERROR SysLocateDevices(SYS_DATA *psSysData)
{
#if defined(NO_HARDWARE)
	PVRSRV_ERROR eError;
	IMG_CPU_PHYADDR sCpuPAddr;
#endif

	PVR_UNREFERENCED_PARAMETER(psSysData);

	
	gsSGXDeviceMap.ui32Flags = 0x0;
	
#if defined(NO_HARDWARE)
	
	
	eError = OSBaseAllocContigMemory(SYS_OMAP3430_SGX_REGS_SIZE, 
									 &gsSGXRegsCPUVAddr,
									 &sCpuPAddr);
	if(eError != PVRSRV_OK)
	{
		return eError;
	}
	gsSGXDeviceMap.sRegsCpuPBase = sCpuPAddr;
	gsSGXDeviceMap.sRegsSysPBase = SysCpuPAddrToSysPAddr(gsSGXDeviceMap.sRegsCpuPBase);
	gsSGXDeviceMap.ui32RegsSize = SYS_OMAP3430_SGX_REGS_SIZE;
#if defined(__linux__)
	
	gsSGXDeviceMap.pvRegsCpuVBase = gsSGXRegsCPUVAddr;
#else
	
	gsSGXDeviceMap.pvRegsCpuVBase = IMG_NULL;
#endif

	OSMemSet(gsSGXRegsCPUVAddr, 0, SYS_OMAP3430_SGX_REGS_SIZE);

	


	gsSGXDeviceMap.ui32IRQ = 0;

#else 

	gsSGXDeviceMap.sRegsSysPBase.uiAddr = SYS_OMAP3430_SGX_REGS_SYS_PHYS_BASE;
	gsSGXDeviceMap.sRegsCpuPBase = SysSysPAddrToCpuPAddr(gsSGXDeviceMap.sRegsSysPBase);
	gsSGXDeviceMap.ui32RegsSize = SYS_OMAP3430_SGX_REGS_SIZE;

	gsSGXDeviceMap.ui32IRQ = SYS_OMAP3430_SGX_IRQ;

#endif 


	


	return PVRSRV_OK;
}


IMG_CHAR *SysCreateVersionString(IMG_CPU_PHYADDR sRegRegion)
{
	static IMG_CHAR aszVersionString[100];
	SYS_DATA	*psSysData;
	IMG_UINT32	ui32SGXRevision;
	IMG_INT32	i32Count;
#if !defined(NO_HARDWARE)
	IMG_VOID	*pvRegsLinAddr;

	pvRegsLinAddr = OSMapPhysToLin(sRegRegion,
								   SYS_OMAP3430_SGX_REGS_SIZE,
								   PVRSRV_HAP_UNCACHED|PVRSRV_HAP_KERNEL_ONLY,
								   IMG_NULL);
	if(!pvRegsLinAddr)
	{
		return IMG_NULL;
	}

	ui32SGXRevision = OSReadHWReg((IMG_PVOID)((IMG_PBYTE)pvRegsLinAddr),
								  EUR_CR_CORE_REVISION);
#else
	ui32SGXRevision = 0;
#endif

	if (SysAcquireData(&psSysData) != PVRSRV_OK)
	{
		return IMG_NULL;
	}

	i32Count = OSSNPrintf(aszVersionString, 100,
						  "SGX revision = %u.%u.%u",
						  (unsigned int)((ui32SGXRevision & EUR_CR_CORE_REVISION_MAJOR_MASK)
							>> EUR_CR_CORE_REVISION_MAJOR_SHIFT),
						  (unsigned int)((ui32SGXRevision & EUR_CR_CORE_REVISION_MINOR_MASK)
							>> EUR_CR_CORE_REVISION_MINOR_SHIFT),
						  (unsigned int)((ui32SGXRevision & EUR_CR_CORE_REVISION_MAINTENANCE_MASK)
							>> EUR_CR_CORE_REVISION_MAINTENANCE_SHIFT)
						 );

#if !defined(NO_HARDWARE)
	OSUnMapPhysToLin(pvRegsLinAddr,
					 SYS_OMAP3430_SGX_REGS_SIZE,
					 PVRSRV_HAP_UNCACHED|PVRSRV_HAP_KERNEL_ONLY,
					 IMG_NULL);
#endif

	if(i32Count == -1)
	{
		return IMG_NULL;
	}

	return aszVersionString;
}


PVRSRV_ERROR SysInitialise(IMG_VOID)
{
	IMG_UINT32			i;
	PVRSRV_ERROR 		eError;
	PVRSRV_DEVICE_NODE	*psDeviceNode;
	IMG_CPU_PHYADDR		TimerRegPhysBase;
#if !defined(SGX_DYNAMIC_TIMING_INFO)
	SGX_TIMING_INFORMATION*	psTimingInfo;
#endif

	gpsSysData = &gsSysData;
	OSMemSet(gpsSysData, 0, sizeof(SYS_DATA));

	gpsSysSpecificData =  &gsSysSpecificData;
	OSMemSet(gpsSysSpecificData, 0, sizeof(SYS_SPECIFIC_DATA));

	gpsSysData->pvSysSpecificData = gpsSysSpecificData;

	eError = OSInitEnvData(&gpsSysData->pvEnvSpecificData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysInitialise: Failed to setup env structure"));
		SysDeinitialise(gpsSysData);
		gpsSysData = IMG_NULL;
		return eError;
	}
	SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_ENVDATA);

	gpsSysData->ui32NumDevices = SYS_DEVICE_COUNT;

	
	for(i=0; i<SYS_DEVICE_COUNT; i++)
	{
		gpsSysData->sDeviceID[i].uiID = i;
		gpsSysData->sDeviceID[i].bInUse = IMG_FALSE;
	}

	gpsSysData->psDeviceNodeList = IMG_NULL;
	gpsSysData->psQueueList = IMG_NULL;

	eError = SysInitialiseCommon(gpsSysData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysInitialise: Failed in SysInitialiseCommon"));
		SysDeinitialise(gpsSysData);
		gpsSysData = IMG_NULL;
		return eError;
	}

	TimerRegPhysBase.uiAddr =
		SYS_OMAP3430_GP11TIMER_PHYS_BASE + SYS_OMAP3430_GPTIMER_REGS;
	gpsSysData->pvSOCTimerRegisterKM = IMG_NULL;
	gpsSysData->hSOCTimerRegisterOSMemHandle = 0;
	OSReservePhys(TimerRegPhysBase,
				  4,
				  PVRSRV_HAP_MULTI_PROCESS|PVRSRV_HAP_UNCACHED,
				  (IMG_VOID **)&gpsSysData->pvSOCTimerRegisterKM,
				  &gpsSysData->hSOCTimerRegisterOSMemHandle);

#if !defined(SGX_DYNAMIC_TIMING_INFO)
	
	psTimingInfo = &gsSGXDeviceMap.sTimingInfo;
	psTimingInfo->ui32CoreClockSpeed = SYS_SGX_CLOCK_SPEED;
	psTimingInfo->ui32HWRecoveryFreq = SYS_SGX_HWRECOVERY_TIMEOUT_FREQ; 
	psTimingInfo->ui32ActivePowManLatencyms = SYS_SGX_ACTIVE_POWER_LATENCY_MS; 
	psTimingInfo->ui32uKernelFreq = SYS_SGX_PDS_TIMER_FREQ; 
#endif
	



	eError = SysLocateDevices(gpsSysData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysInitialise: Failed to locate devices"));
		SysDeinitialise(gpsSysData);
		gpsSysData = IMG_NULL;
		return eError;
	}
	SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_LOCATEDEV);

	


	eError = PVRSRVRegisterDevice(gpsSysData, SGXRegisterDevice,
								  DEVICE_SGX_INTERRUPT, &gui32SGXDeviceID);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysInitialise: Failed to register device!"));
		SysDeinitialise(gpsSysData);
		gpsSysData = IMG_NULL;
		return eError;
	}
	SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_REGDEV);

	


	
	psDeviceNode = gpsSysData->psDeviceNodeList;
	while(psDeviceNode)
	{
		
		switch(psDeviceNode->sDevId.eDeviceType)
		{
			case PVRSRV_DEVICE_TYPE_SGX:
			{
				DEVICE_MEMORY_INFO *psDevMemoryInfo;
				DEVICE_MEMORY_HEAP_INFO *psDeviceMemoryHeap;

				


				psDeviceNode->psLocalDevMemArena = IMG_NULL;

				
				psDevMemoryInfo = &psDeviceNode->sDevMemoryInfo;
				psDeviceMemoryHeap = psDevMemoryInfo->psDeviceMemoryHeap;

				
				for(i=0; i<psDevMemoryInfo->ui32HeapCount; i++)
				{
					psDeviceMemoryHeap[i].ui32Attribs |= PVRSRV_BACKINGSTORE_SYSMEM_NONCONTIG;
				}

				gpsSGXDevNode = psDeviceNode;
				gsSysSpecificData.psSGXDevNode = psDeviceNode;

				break;
			}
			default:
				PVR_DPF((PVR_DBG_ERROR,"SysInitialise: Failed to find SGX device node!"));
				return PVRSRV_ERROR_INIT_FAILURE;
		}

		
		psDeviceNode = psDeviceNode->psNext;
	}

	PDUMPINIT();
	SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_PDUMPINIT);

	eError = InitSystemClocks(gpsSysData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysInitialise: Failed to init system clocks (%d)", eError));
		SysDeinitialise(gpsSysData);
		gpsSysData = IMG_NULL;
		return eError;
	}

	eError = EnableSystemClocks(gpsSysData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysInitialise: Failed to Enable system clocks (%d)", eError));
		SysDeinitialise(gpsSysData);
		gpsSysData = IMG_NULL;
		return eError;
	}
	SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_SYSCLOCKS);

	eError = OSInitPerf(gpsSysData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysInitialise: Failed to init DVFS (%d)", eError));
		SysDeinitialise(gpsSysData);
		gpsSysData = IMG_NULL;
		return eError;
	}

#if defined(SUPPORT_ACTIVE_POWER_MANAGEMENT)
	eError = EnableSGXClocks(gpsSysData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysInitialise: Failed to Enable SGX clocks (%d)", eError));
		SysDeinitialise(gpsSysData);
		gpsSysData = IMG_NULL;
		return eError;
	}
#endif	

	eError = PVRSRVInitialiseDevice(gui32SGXDeviceID);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysInitialise: Failed to initialise device!"));
		SysDeinitialise(gpsSysData);
		gpsSysData = IMG_NULL;
		return eError;
	}
	SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_INITDEV);

#if defined(SUPPORT_ACTIVE_POWER_MANAGEMENT)
	
	DisableSGXClocks(gpsSysData);
#endif	

	return PVRSRV_OK;
}


PVRSRV_ERROR SysFinalise(IMG_VOID)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

#if defined(SUPPORT_ACTIVE_POWER_MANAGEMENT)
	eError = EnableSGXClocks(gpsSysData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysInitialise: Failed to Enable SGX clocks (%d)", eError));
		SysDeinitialise(gpsSysData);
		gpsSysData = IMG_NULL;
		return eError;
	}
#endif	

#if defined(SYS_USING_INTERRUPTS)

	eError = OSInstallMISR(gpsSysData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysFinalise: Failed to install MISR"));
		SysDeinitialise(gpsSysData);
		gpsSysData = IMG_NULL;
		return eError;
	}
	SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_MISR);

	
	eError = OSInstallDeviceLISR(gpsSysData, gsSGXDeviceMap.ui32IRQ, "SGX ISR", gpsSGXDevNode);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysFinalise: Failed to install ISR"));
		SysDeinitialise(gpsSysData);
		gpsSysData = IMG_NULL;
		return eError;
	}
	SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_LISR);
#endif 

	
	gpsSysData->pszVersionString = SysCreateVersionString(gsSGXDeviceMap.sRegsCpuPBase);
	if (!gpsSysData->pszVersionString)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysFinalise: Failed to create a system version string"));
	}
	else
	{
		PVR_DPF((PVR_DBG_WARNING, "SysFinalise: Version string: %s", gpsSysData->pszVersionString));
	}

#if defined(SUPPORT_ACTIVE_POWER_MANAGEMENT)
	
	DisableSGXClocks(gpsSysData);
#endif	

	gpsSysSpecificData->bSGXInitComplete = IMG_TRUE;

	return eError;
}


PVRSRV_ERROR SysDeinitialise (SYS_DATA *psSysData)
{
	PVRSRV_ERROR eError;
	
	PVR_UNREFERENCED_PARAMETER(psSysData);

#if defined(SYS_USING_INTERRUPTS)
	if (SYS_SPECIFIC_DATA_TEST(gpsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_LISR))
	{
		eError = OSUninstallDeviceLISR(psSysData);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"SysDeinitialise: OSUninstallDeviceLISR failed"));
			return eError;
		}
	}

	if (SYS_SPECIFIC_DATA_TEST(gpsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_MISR))
	{
		eError = OSUninstallMISR(psSysData);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"SysDeinitialise: OSUninstallMISR failed"));
			return eError;
		}
	}
#endif

	eError = OSCleanupPerf(gpsSysSpecificData);
	if (eError != PVRSRV_OK) {
		PVR_DPF((PVR_DBG_ERROR, "SysDeinitialise: OSCleanupDvfs failed"));
		return eError;
	}

	if (SYS_SPECIFIC_DATA_TEST(gpsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_INITDEV))
	{
#if defined(SUPPORT_ACTIVE_POWER_MANAGEMENT)
		PVR_ASSERT(SYS_SPECIFIC_DATA_TEST(gpsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_SYSCLOCKS));
		
		eError = EnableSGXClocks(gpsSysData);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"SysDeinitialise: EnableSGXClocks failed"));
			return eError;
		}
#endif	

		
		eError = PVRSRVDeinitialiseDevice (gui32SGXDeviceID);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"SysDeinitialise: failed to de-init the device"));
			return eError;
		}
	}
	
	if (SYS_SPECIFIC_DATA_TEST(gpsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_SYSCLOCKS))
	{
		DisableSystemClocks(gpsSysData);
	}

	CleanupSystemClocks(gpsSysData);

	if (SYS_SPECIFIC_DATA_TEST(gpsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_ENVDATA))
	{	
		eError = OSDeInitEnvData(gpsSysData->pvEnvSpecificData);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"SysDeinitialise: failed to de-init env structure"));
			return eError;
		}
	}

	if(gpsSysData->pvSOCTimerRegisterKM)
	{
		OSUnReservePhys(gpsSysData->pvSOCTimerRegisterKM,
						4,
						PVRSRV_HAP_MULTI_PROCESS|PVRSRV_HAP_UNCACHED,
						gpsSysData->hSOCTimerRegisterOSMemHandle);
	}

	SysDeinitialiseCommon(gpsSysData);

#if defined(NO_HARDWARE)
	if (SYS_SPECIFIC_DATA_TEST(gpsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_LOCATEDEV))
	{
		
		OSBaseFreeContigMemory(SYS_OMAP3430_SGX_REGS_SIZE, gsSGXRegsCPUVAddr, gsSGXDeviceMap.sRegsCpuPBase);
	}
#endif

	
	if (SYS_SPECIFIC_DATA_TEST(gpsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_PDUMPINIT))
	{
		PDUMPDEINIT();
	}

	gpsSysSpecificData->ui32SysSpecificData = 0;
	gpsSysSpecificData->bSGXInitComplete = IMG_FALSE;

	gpsSysData = IMG_NULL;

	return PVRSRV_OK;
}


PVRSRV_ERROR SysGetDeviceMemoryMap(PVRSRV_DEVICE_TYPE	eDeviceType,
								   IMG_VOID				**ppvDeviceMap)
{

	switch(eDeviceType)
	{
		case PVRSRV_DEVICE_TYPE_SGX:
		{
			
			*ppvDeviceMap = (IMG_VOID*)&gsSGXDeviceMap;

			break;
		}
		default:
		{
			PVR_DPF((PVR_DBG_ERROR,"SysGetDeviceMemoryMap: unsupported device type"));
		}
	}
	return PVRSRV_OK;
}


IMG_DEV_PHYADDR SysCpuPAddrToDevPAddr(PVRSRV_DEVICE_TYPE	eDeviceType,
									  IMG_CPU_PHYADDR		CpuPAddr)
{
	IMG_DEV_PHYADDR DevPAddr;

	PVR_UNREFERENCED_PARAMETER(eDeviceType);

	
	DevPAddr.uiAddr = CpuPAddr.uiAddr;
	
	return DevPAddr;
}

IMG_CPU_PHYADDR SysSysPAddrToCpuPAddr (IMG_SYS_PHYADDR sys_paddr)
{
	IMG_CPU_PHYADDR cpu_paddr;

	
	cpu_paddr.uiAddr = sys_paddr.uiAddr;
	return cpu_paddr;
}

IMG_SYS_PHYADDR SysCpuPAddrToSysPAddr (IMG_CPU_PHYADDR cpu_paddr)
{
	IMG_SYS_PHYADDR sys_paddr;

	
	sys_paddr.uiAddr = cpu_paddr.uiAddr;
	return sys_paddr;
}


IMG_DEV_PHYADDR SysSysPAddrToDevPAddr(PVRSRV_DEVICE_TYPE eDeviceType, IMG_SYS_PHYADDR SysPAddr)
{
	IMG_DEV_PHYADDR DevPAddr;

	PVR_UNREFERENCED_PARAMETER(eDeviceType);

	
	DevPAddr.uiAddr = SysPAddr.uiAddr;

	return DevPAddr;
}


IMG_SYS_PHYADDR SysDevPAddrToSysPAddr(PVRSRV_DEVICE_TYPE eDeviceType, IMG_DEV_PHYADDR DevPAddr)
{
	IMG_SYS_PHYADDR SysPAddr;

	PVR_UNREFERENCED_PARAMETER(eDeviceType);

	
	SysPAddr.uiAddr = DevPAddr.uiAddr;

	return SysPAddr;
}


IMG_VOID SysRegisterExternalDevice(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
}


IMG_VOID SysRemoveExternalDevice(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
}


IMG_UINT32 SysGetInterruptSource(SYS_DATA			*psSysData,
								 PVRSRV_DEVICE_NODE	*psDeviceNode)
{
	PVR_UNREFERENCED_PARAMETER(psSysData);
#if defined(NO_HARDWARE)
	
	return 0xFFFFFFFF;
#else
	
	return psDeviceNode->ui32SOCInterruptBit;
#endif
}


IMG_VOID SysClearInterrupts(SYS_DATA* psSysData, IMG_UINT32 ui32ClearBits)
{
	PVR_UNREFERENCED_PARAMETER(psSysData);
	PVR_UNREFERENCED_PARAMETER(ui32ClearBits);

	/* Flush posted write for the irq status to avoid spurious interrupts */
	OSReadHWReg(((PVRSRV_SGXDEV_INFO *)gpsSGXDevNode->pvDevice)->pvRegsBaseKM,
						EUR_CR_EVENT_HOST_CLEAR);
}


PVRSRV_ERROR SysSystemPrePowerState(PVR_POWER_STATE eNewPowerState)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	if (eNewPowerState == PVRSRV_POWER_STATE_D3)
	{
		PVR_TRACE(("SysSystemPrePowerState: Entering state D3"));

#if defined(SYS_USING_INTERRUPTS)
		if (SYS_SPECIFIC_DATA_TEST(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_LISR))
		{
			eError = OSUninstallDeviceLISR(gpsSysData);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,"SysSystemPrePowerState: OSUninstallDeviceLISR failed (%d)", eError));
				return eError;
			}
			SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_PM_UNINSTALL_LISR);
			SYS_SPECIFIC_DATA_CLEAR(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_LISR);
		}
#endif

		if (SYS_SPECIFIC_DATA_TEST(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_SYSCLOCKS))
		{
			DisableSystemClocks(gpsSysData);

			SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_PM_DISABLE_SYSCLOCKS);
			SYS_SPECIFIC_DATA_CLEAR(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_SYSCLOCKS);
		}
	}

	return eError;
}


PVRSRV_ERROR SysSystemPostPowerState(PVR_POWER_STATE eNewPowerState)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	if (eNewPowerState == PVRSRV_POWER_STATE_D0)
	{
		PVR_TRACE(("SysSystemPostPowerState: Entering state D0"));

		if (SYS_SPECIFIC_DATA_TEST(&gsSysSpecificData, SYS_SPECIFIC_DATA_PM_DISABLE_SYSCLOCKS))
		{
			eError = EnableSystemClocks(gpsSysData);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,"SysSystemPostPowerState: EnableSystemClocks failed (%d)", eError));
				return eError;
			}
			SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_SYSCLOCKS);
			SYS_SPECIFIC_DATA_CLEAR(&gsSysSpecificData, SYS_SPECIFIC_DATA_PM_DISABLE_SYSCLOCKS);
		}

#if defined(SYS_USING_INTERRUPTS)
		if (SYS_SPECIFIC_DATA_TEST(&gsSysSpecificData, SYS_SPECIFIC_DATA_PM_UNINSTALL_LISR))
		{
			eError = OSInstallDeviceLISR(gpsSysData, gsSGXDeviceMap.ui32IRQ, "SGX ISR", gpsSGXDevNode);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,"SysSystemPostPowerState: OSInstallDeviceLISR failed to install ISR (%d)", eError));
				return eError;
			}
			SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_LISR);
			SYS_SPECIFIC_DATA_CLEAR(&gsSysSpecificData, SYS_SPECIFIC_DATA_PM_UNINSTALL_LISR);
		}
#endif
	}
	return eError;
}


PVRSRV_ERROR SysDevicePrePowerState(IMG_UINT32			ui32DeviceIndex,
									PVR_POWER_STATE		eNewPowerState,
									PVR_POWER_STATE		eCurrentPowerState)
{
	PVR_UNREFERENCED_PARAMETER(eCurrentPowerState);

	if (ui32DeviceIndex != gui32SGXDeviceID)
	{
		return PVRSRV_OK;
	}

#if defined(SUPPORT_ACTIVE_POWER_MANAGEMENT)
	if (eNewPowerState == PVRSRV_POWER_STATE_D3)
	{
		PVR_TRACE(("SysDevicePrePowerState: SGX Entering state D3"));
		DisableSGXClocks(gpsSysData);
	}
#else	
	PVR_UNREFERENCED_PARAMETER(eNewPowerState );
#endif 
	return PVRSRV_OK;
}


PVRSRV_ERROR SysDevicePostPowerState(IMG_UINT32			ui32DeviceIndex,
									 PVR_POWER_STATE	eNewPowerState,
									 PVR_POWER_STATE	eCurrentPowerState)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVR_UNREFERENCED_PARAMETER(eNewPowerState);

	if (ui32DeviceIndex != gui32SGXDeviceID)
	{
		return eError;
	}

#if defined(SUPPORT_ACTIVE_POWER_MANAGEMENT)
	if (eCurrentPowerState == PVRSRV_POWER_STATE_D3)
	{
		PVR_TRACE(("SysDevicePostPowerState: SGX Leaving state D3"));
		eError = EnableSGXClocks(gpsSysData);
	}
#else	
	PVR_UNREFERENCED_PARAMETER(eCurrentPowerState);
#endif	

	return eError;
}


PVRSRV_ERROR SysOEMFunction (	IMG_UINT32	ui32ID,
								IMG_VOID	*pvIn,
								IMG_UINT32	ulInSize,
								IMG_VOID	*pvOut,
								IMG_UINT32	ulOutSize)
{
	PVR_UNREFERENCED_PARAMETER(ui32ID);
	PVR_UNREFERENCED_PARAMETER(pvIn);
	PVR_UNREFERENCED_PARAMETER(ulInSize);
	PVR_UNREFERENCED_PARAMETER(pvOut);
	PVR_UNREFERENCED_PARAMETER(ulOutSize);

	if ((ui32ID == OEM_GET_EXT_FUNCS) &&
		(ulOutSize == sizeof(PVRSRV_DC_OEM_JTABLE)))
	{
		
		PVRSRV_DC_OEM_JTABLE *psOEMJTable = (PVRSRV_DC_OEM_JTABLE*) pvOut;
		psOEMJTable->pfnOEMBridgeDispatch = &PVRSRV_BridgeDispatchKM;
		return PVRSRV_OK;
	}

	return PVRSRV_ERROR_INVALID_PARAMS;
}
