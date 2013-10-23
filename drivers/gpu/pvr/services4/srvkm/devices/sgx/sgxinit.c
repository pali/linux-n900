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
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include "sgxdefs.h"
#include "sgxmmu.h"
#include "services_headers.h"
#include "buffer_manager.h"
#include "sgxapi_km.h"
#include "sgxinfo.h"
#include "sgxinfokm.h"
#include "sgxconfig.h"
#include "sysconfig.h"
#include "pvr_bridge_km.h"

#include "pdump_km.h"
#include "ra.h"
#include "mmu.h"
#include "handle.h"
#include "perproc.h"

#include "sgxutils.h"

#if defined (SGX_FEATURE_2D_HARDWARE)
#define	SGX_USING_CMD_PROC_LIST
#endif

IMG_BOOL SGX_ISRHandler(IMG_VOID *pvData);

IMG_UINT32 gui32EventStatusServicesByISR = 0;

IMG_VOID SGXReset(PVRSRV_SGXDEV_INFO	*psDevInfo,
				  IMG_UINT32			 ui32PDUMPFlags);

static PVRSRV_ERROR SGXInitialise(PVRSRV_SGXDEV_INFO	*psDevInfo,
								  IMG_BOOL				bHardwareRecovery);
PVRSRV_ERROR SGXDeinitialise(IMG_HANDLE hDevCookie);

typedef enum _PVR_DEVICE_POWER_STATE_ {
	PVR_DEVICE_POWER_STATE_ON		= 0,
	PVR_DEVICE_POWER_STATE_IDLE		= 1,
	PVR_DEVICE_POWER_STATE_OFF		= 2,

	PVR_DEVICE_POWER_STATE_FORCE_I32 = 0x7fffffff

} PVR_DEVICE_POWER_STATE, *PPVR_DEVICE_POWER_STATE;


static PVR_DEVICE_POWER_STATE MapDevicePowerState(PVR_POWER_STATE	ePowerState)
{
	PVR_DEVICE_POWER_STATE eDevicePowerState;

	switch (ePowerState) {
	case PVRSRV_POWER_STATE_D0:
		{
			eDevicePowerState = PVR_DEVICE_POWER_STATE_ON;
			break;
		}
	case PVRSRV_POWER_STATE_D3:
		{
			eDevicePowerState = PVR_DEVICE_POWER_STATE_OFF;
			break;
		}
	default:
		{
			PVR_DPF((PVR_DBG_ERROR, "MapDevicePowerState: Invalid state: %ld", ePowerState));
			eDevicePowerState = PVR_DEVICE_POWER_STATE_FORCE_I32;
			PVR_ASSERT(eDevicePowerState != PVR_DEVICE_POWER_STATE_FORCE_I32);
		}
	}

	return eDevicePowerState;
}


static IMG_VOID SGXCommandComplete(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	SGXScheduleProcessQueues(psDeviceNode);
}

static IMG_UINT32 DeinitDevInfo(PVRSRV_SGXDEV_INFO *psDevInfo)
{
	if (psDevInfo->psKernelCCBInfo != IMG_NULL)
	{
		

		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(PVRSRV_SGX_CCB_INFO), psDevInfo->psKernelCCBInfo, IMG_NULL);
	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR InitDevInfo(PVRSRV_PER_PROCESS_DATA *psPerProc,
								PVRSRV_DEVICE_NODE *psDeviceNode,
								SGX_BRIDGE_INIT_INFO *psInitInfo)
{
	PVRSRV_SGXDEV_INFO *psDevInfo = (PVRSRV_SGXDEV_INFO *)psDeviceNode->pvDevice;
	PVRSRV_ERROR		eError;

	PVRSRV_SGX_CCB_INFO	*psKernelCCBInfo = IMG_NULL;

	PVR_UNREFERENCED_PARAMETER(psPerProc);
	psDevInfo->sScripts = psInitInfo->sScripts;

	psDevInfo->psKernelCCBMemInfo = (PVRSRV_KERNEL_MEM_INFO *)psInitInfo->hKernelCCBMemInfo;
	psDevInfo->psKernelCCB = (PVRSRV_SGX_KERNEL_CCB *) psDevInfo->psKernelCCBMemInfo->pvLinAddrKM;

	psDevInfo->psKernelCCBCtlMemInfo = (PVRSRV_KERNEL_MEM_INFO *)psInitInfo->hKernelCCBCtlMemInfo;
	psDevInfo->psKernelCCBCtl = (PVRSRV_SGX_CCB_CTL *) psDevInfo->psKernelCCBCtlMemInfo->pvLinAddrKM;

	psDevInfo->psKernelCCBEventKickerMemInfo = (PVRSRV_KERNEL_MEM_INFO *)psInitInfo->hKernelCCBEventKickerMemInfo;
	psDevInfo->pui32KernelCCBEventKicker = (IMG_UINT32 *)psDevInfo->psKernelCCBEventKickerMemInfo->pvLinAddrKM;

	psDevInfo->psKernelSGXHostCtlMemInfo = (PVRSRV_KERNEL_MEM_INFO *)psInitInfo->hKernelSGXHostCtlMemInfo;
	psDevInfo->psSGXHostCtl = (PVRSRV_SGX_HOST_CTL *)psDevInfo->psKernelSGXHostCtlMemInfo->pvLinAddrKM;

#if defined(SGX_SUPPORT_HWPROFILING)
	psDevInfo->psKernelHWProfilingMemInfo = (PVRSRV_KERNEL_MEM_INFO *)psInitInfo->hKernelHWProfilingMemInfo;
#endif
#if defined(SUPPORT_SGX_HWPERF)
	psDevInfo->psKernelHWPerfCBMemInfo = (PVRSRV_KERNEL_MEM_INFO *)psInitInfo->hKernelHWPerfCBMemInfo;
#endif
#if defined(SGX_FEATURE_OVERLAPPED_SPM)
	psDevInfo->psKernelTmpRgnHeaderMemInfo = (PVRSRV_KERNEL_MEM_INFO *)psInitInfo->hKernelTmpRgnHeaderMemInfo;
#endif
#if defined(SGX_FEATURE_SPM_MODE_0)
	psDevInfo->psKernelTmpDPMStateMemInfo = (PVRSRV_KERNEL_MEM_INFO *)psInitInfo->hKernelTmpDPMStateMemInfo;
#endif
	

	eError = OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP, 
						sizeof(PVRSRV_SGX_CCB_INFO),
						(IMG_VOID **)&psKernelCCBInfo, 0);
	if (eError != PVRSRV_OK)	
	{
		PVR_DPF((PVR_DBG_ERROR,"InitDevInfo: Failed to alloc memory"));
		goto failed_allockernelccb;
	}


	OSMemSet(psKernelCCBInfo, 0, sizeof(PVRSRV_SGX_CCB_INFO));
	psKernelCCBInfo->psCCBMemInfo		= psDevInfo->psKernelCCBMemInfo;
	psKernelCCBInfo->psCCBCtlMemInfo	= psDevInfo->psKernelCCBCtlMemInfo;
	psKernelCCBInfo->psCommands			= psDevInfo->psKernelCCB->asCommands;
	psKernelCCBInfo->pui32WriteOffset	= &psDevInfo->psKernelCCBCtl->ui32WriteOffset;
	psKernelCCBInfo->pui32ReadOffset	= &psDevInfo->psKernelCCBCtl->ui32ReadOffset;
	psDevInfo->psKernelCCBInfo = psKernelCCBInfo;

	

	psDevInfo->ui32TAKickAddress = psInitInfo->ui32TAKickAddress;

	

	psDevInfo->ui32VideoHandlerAddress = psInitInfo->ui32VideoHandlerAddress;

	psDevInfo->bForcePTOff = IMG_FALSE;
	
	psDevInfo->ui32CacheControl = psInitInfo->ui32CacheControl;

	psDevInfo->ui32EDMTaskReg0 = psInitInfo->ui32EDMTaskReg0;
	psDevInfo->ui32EDMTaskReg1 = psInitInfo->ui32EDMTaskReg1;
	psDevInfo->ui32ClkGateCtl = psInitInfo->ui32ClkGateCtl;
	psDevInfo->ui32ClkGateCtl2 = psInitInfo->ui32ClkGateCtl2;
	psDevInfo->ui32ClkGateStatusMask = psInitInfo->ui32ClkGateStatusMask;


	
	OSMemCopy(&psDevInfo->asSGXDevData,  &psInitInfo->asInitDevData, sizeof(psDevInfo->asSGXDevData));

	return PVRSRV_OK;

failed_allockernelccb:
	DeinitDevInfo(psDevInfo);

	return eError;
}

static IMG_VOID SGXGetTimingInfo(PVRSRV_DEVICE_NODE	*psDeviceNode)
{
	PVRSRV_SGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;
#if defined(SGX_DYNAMIC_TIMING_INFO)
	SGX_TIMING_INFORMATION	sSGXTimingInfo = {0};
#else
	SGX_DEVICE_MAP		*psSGXDeviceMap;
#endif
	IMG_UINT32		ui32ActivePowManSampleRate;
	SGX_TIMING_INFORMATION	*psSGXTimingInfo;


#if defined(SGX_DYNAMIC_TIMING_INFO)
	psSGXTimingInfo = &sSGXTimingInfo;
	SysGetSGXTimingInformation(psSGXTimingInfo);
#else
	SysGetDeviceMemoryMap(PVRSRV_DEVICE_TYPE_SGX,
						(IMG_VOID **)&psSGXDeviceMap);
	psSGXTimingInfo = &psSGXDeviceMap->sTimingInfo;
#endif

#if defined(SUPPORT_HW_RECOVERY)
	{
		PVRSRV_ERROR			eError;
		IMG_UINT32	ui32OlduKernelFreq;

		if (psDevInfo->hTimer != IMG_NULL) {
			ui32OlduKernelFreq = psDevInfo->ui32CoreClockSpeed / psDevInfo->ui32uKernelTimerClock;
			if (ui32OlduKernelFreq != psSGXTimingInfo->ui32uKernelFreq) {
				eError = OSRemoveTimer(psDevInfo->hTimer);
				if (eError != PVRSRV_OK) {
					PVR_DPF((PVR_DBG_ERROR, "SGXGetTimingInfo: Failed to remove timer"));
				}
				psDevInfo->hTimer = IMG_NULL;
			}
		}
		if (psDevInfo->hTimer == IMG_NULL) {

			psDevInfo->hTimer = OSAddTimer(SGXOSTimer, psDeviceNode,
										1000 * 50 / psSGXTimingInfo->ui32uKernelFreq);
			if (psDevInfo->hTimer == IMG_NULL) {
				PVR_DPF((PVR_DBG_ERROR, "SGXGetTimingInfo : Failed to register timer callback function"));
			}
		}

		psDevInfo->psSGXHostCtl->ui32HWRecoverySampleRate =
			psSGXTimingInfo->ui32uKernelFreq / psSGXTimingInfo->ui32HWRecoveryFreq;
	}
#endif


	psDevInfo->ui32CoreClockSpeed = psSGXTimingInfo->ui32CoreClockSpeed;
	psDevInfo->ui32uKernelTimerClock = psSGXTimingInfo->ui32CoreClockSpeed / psSGXTimingInfo->ui32uKernelFreq;

	ui32ActivePowManSampleRate =
		psSGXTimingInfo->ui32uKernelFreq * psSGXTimingInfo->ui32ActivePowManLatencyms / 1000;
	ui32ActivePowManSampleRate += 1;
	psDevInfo->psSGXHostCtl->ui32ActivePowManSampleRate = ui32ActivePowManSampleRate;
}



static IMG_VOID SGXStartTimer(PVRSRV_SGXDEV_INFO	*psDevInfo,
							  IMG_BOOL				bStartOSTimer)
{
	IMG_UINT32		ui32RegVal;

	#if !defined(SUPPORT_HW_RECOVERY)
	PVR_UNREFERENCED_PARAMETER(bStartOSTimer);
	#endif


	ui32RegVal = EUR_CR_EVENT_TIMER_ENABLE_MASK | psDevInfo->ui32uKernelTimerClock;
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_EVENT_TIMER, ui32RegVal);
	PDUMPREGWITHFLAGS(EUR_CR_EVENT_TIMER, ui32RegVal, PDUMP_FLAGS_CONTINUOUS);

	#if defined(SUPPORT_HW_RECOVERY)
	if (bStartOSTimer) {
		PVRSRV_ERROR	eError;
		eError = OSEnableTimer(psDevInfo->hTimer);
		if (eError != PVRSRV_OK) {
			PVR_DPF((PVR_DBG_ERROR, "SGXStartTimer : Failed to enable host timer"));
		}
	}
	#endif
}

static PVRSRV_ERROR SGXPrePowerState(IMG_HANDLE				hDevHandle,
									PVR_DEVICE_POWER_STATE	eNewPowerState,
									PVR_DEVICE_POWER_STATE	eCurrentPowerState)
{
	if ((eNewPowerState != eCurrentPowerState) &&
		(eNewPowerState != PVR_DEVICE_POWER_STATE_ON))
	{
		PVRSRV_ERROR		eError;
		PVRSRV_DEVICE_NODE	*psDeviceNode = hDevHandle;
		PVRSRV_SGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;
		PVRSRV_SGX_HOST_CTL *psSGXHostCtl = psDevInfo->psSGXHostCtl;
		IMG_UINT32			ui32PowManRequest, ui32PowManComplete;

		#if defined(SUPPORT_HW_RECOVERY)
		eError = OSDisableTimer(psDevInfo->hTimer);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "SGXPrePowerState: Failed to disable timer"));
			return eError;
		}
		#endif

		if (eNewPowerState == PVR_DEVICE_POWER_STATE_OFF) {
			ui32PowManRequest = PVRSRV_USSE_EDM_POWMAN_POWEROFF_REQUEST;
			ui32PowManComplete = PVRSRV_USSE_EDM_POWMAN_POWEROFF_COMPLETE;
			PDUMPCOMMENT("TA/3D CCB Control - SGX power off request");
		} else {
			ui32PowManRequest = PVRSRV_USSE_EDM_POWMAN_IDLE_REQUEST;
			ui32PowManComplete = PVRSRV_USSE_EDM_POWMAN_IDLE_COMPLETE;
			PDUMPCOMMENT("TA/3D CCB Control - SGX idle request");
		}

		psSGXHostCtl->ui32PowManFlags |= ui32PowManRequest;
		#if defined(PDUMP)
		PDUMPMEM(IMG_NULL, psDevInfo->psKernelSGXHostCtlMemInfo,
				 offsetof(PVRSRV_SGX_HOST_CTL, ui32PowManFlags),
				 sizeof(IMG_UINT32), PDUMP_FLAGS_CONTINUOUS,
				 MAKEUNIQUETAG(psDevInfo->psKernelSGXHostCtlMemInfo));
		#endif

		#if !defined(NO_HARDWARE)
		if (PollForValueKM(&psSGXHostCtl->ui32PowManFlags,
							ui32PowManComplete,
							ui32PowManComplete,
							MAX_HW_TIME_US/WAIT_TRY_COUNT,
							WAIT_TRY_COUNT) != PVRSRV_OK) {
			PVR_DPF((PVR_DBG_ERROR, "SGXPrePowerState: Wait for SGX ukernel power transition failed."));
		}
		#endif

		#if defined(PDUMP)
		PDUMPCOMMENT("TA/3D CCB Control - Wait for power event on uKernel.");
		PDUMPMEMPOL(psDevInfo->psKernelSGXHostCtlMemInfo,
					offsetof(PVRSRV_SGX_HOST_CTL, ui32PowManFlags),
					ui32PowManComplete,
					ui32PowManComplete,
					PDUMP_POLL_OPERATOR_EQUAL,
					IMG_FALSE, IMG_FALSE,
					MAKEUNIQUETAG(psDevInfo->psKernelSGXHostCtlMemInfo));
		#endif

		#if defined(SGX_FEATURE_AUTOCLOCKGATING)

		{
			#if !defined(NO_HARDWARE)
			if (PollForValueKM((IMG_UINT32 *)psDevInfo->pvRegsBaseKM + (EUR_CR_CLKGATESTATUS >> 2),
								0,
								psDevInfo->ui32ClkGateStatusMask,
								MAX_HW_TIME_US/WAIT_TRY_COUNT,
								WAIT_TRY_COUNT) != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "SGXPrePowerState: Wait for SGX clock gating failed."));
			}
			#endif
			
			PDUMPCOMMENT("Wait for SGX clock gating.");
			PDUMPREGPOL(EUR_CR_CLKGATESTATUS, 0, psDevInfo->ui32ClkGateStatusMask);
		}
		#endif

		if (eNewPowerState == PVR_DEVICE_POWER_STATE_OFF) {
			eError = SGXDeinitialise(psDevInfo);
			if (eError != PVRSRV_OK) {
				PVR_DPF((PVR_DBG_ERROR, "SGXPrePowerState: SGXDeinitialise failed: %lu", eError));
				return eError;
			}
		}
	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR SGXPostPowerState(IMG_HANDLE				hDevHandle,
									PVR_DEVICE_POWER_STATE	eNewPowerState,
									PVR_DEVICE_POWER_STATE	eCurrentPowerState)
{
	if ((eNewPowerState != eCurrentPowerState) &&
		(eCurrentPowerState != PVR_DEVICE_POWER_STATE_ON))
	{
		PVRSRV_ERROR		eError;
		PVRSRV_DEVICE_NODE	*psDeviceNode = hDevHandle;
		PVRSRV_SGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;
		PVRSRV_SGX_HOST_CTL *psSGXHostCtl = psDevInfo->psSGXHostCtl;


		psSGXHostCtl->ui32PowManFlags = 0;
		PDUMPCOMMENT("TA/3D CCB Control - Reset Power Manager flags");
		#if defined(PDUMP)
		PDUMPMEM(IMG_NULL, psDevInfo->psKernelSGXHostCtlMemInfo,
				 offsetof(PVRSRV_SGX_HOST_CTL, ui32PowManFlags),
				 sizeof(IMG_UINT32), PDUMP_FLAGS_CONTINUOUS,
				 MAKEUNIQUETAG(psDevInfo->psKernelSGXHostCtlMemInfo));
		#endif

		

		if (eCurrentPowerState == PVR_DEVICE_POWER_STATE_OFF)
		{
			

			SGXGetTimingInfo(psDeviceNode);

			

			eError = SGXInitialise(psDevInfo, IMG_FALSE);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,"SGXPostPowerState: SGXInitialise failed"));
				return eError;
			}
		}
	}

	return PVRSRV_OK;
}

PVRSRV_ERROR SGXPrePowerStateExt (IMG_HANDLE		hDevHandle,
								PVR_POWER_STATE	eNewPowerState,
								PVR_POWER_STATE	eCurrentPowerState)
{
	PVR_DEVICE_POWER_STATE	eNewDevicePowerState = MapDevicePowerState(eNewPowerState);
	PVR_DEVICE_POWER_STATE	eCurrentDevicePowerState = MapDevicePowerState(eCurrentPowerState);

	return SGXPrePowerState(hDevHandle, eNewDevicePowerState, eCurrentDevicePowerState);
}


PVRSRV_ERROR SGXPostPowerStateExt (IMG_HANDLE		hDevHandle,
								PVR_POWER_STATE	eNewPowerState,
								PVR_POWER_STATE	eCurrentPowerState)
{
	PVRSRV_ERROR			eError;
	PVR_DEVICE_POWER_STATE	eNewDevicePowerState = MapDevicePowerState(eNewPowerState);
	PVR_DEVICE_POWER_STATE	eCurrentDevicePowerState = MapDevicePowerState(eCurrentPowerState);

	eError = SGXPostPowerState(hDevHandle, eNewDevicePowerState, eCurrentDevicePowerState);
	if (eError != PVRSRV_OK) {
		return eError;
	}

	PVR_DPF((PVR_DBG_WARNING,
			"SGXPostPowerState : SGX Power Transition from %d to %d OK",
			eCurrentPowerState, eNewPowerState));

	return eError;
}


static PVRSRV_ERROR SGXPreClockSpeedChange (IMG_HANDLE		hDevHandle,
											IMG_BOOL		bIdleDevice,
											PVR_POWER_STATE	eCurrentPowerState)
{
	PVRSRV_ERROR		eError;
	PVRSRV_DEVICE_NODE	*psDeviceNode = hDevHandle;
	PVRSRV_SGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;

	PVR_UNREFERENCED_PARAMETER(psDevInfo);

	if (eCurrentPowerState == PVRSRV_POWER_STATE_D0)
	{
		if (bIdleDevice) {

			eError = SGXPrePowerState(hDevHandle, PVR_DEVICE_POWER_STATE_IDLE,
									  PVR_DEVICE_POWER_STATE_ON);

			if (eError != PVRSRV_OK) {
				return eError;
			}
		}
	}

	PVR_DPF((PVR_DBG_MESSAGE, "SGXPreClockSpeedChange: SGX clock speed was %luHz",
			psDevInfo->ui32CoreClockSpeed));

	return PVRSRV_OK;
}


static PVRSRV_ERROR SGXPostClockSpeedChange(IMG_HANDLE			hDevHandle,
											 IMG_BOOL			bIdleDevice,
											 PVR_POWER_STATE	eCurrentPowerState)
{
	PVRSRV_ERROR		eError = PVRSRV_OK;
	PVRSRV_DEVICE_NODE	*psDeviceNode = hDevHandle;
	PVRSRV_SGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;
	IMG_UINT32			ui32OldClockSpeed = psDevInfo->ui32CoreClockSpeed;

	PVR_UNREFERENCED_PARAMETER(ui32OldClockSpeed);

	if (eCurrentPowerState == PVRSRV_POWER_STATE_D0)
	{
		SGXGetTimingInfo(psDeviceNode);
		if (bIdleDevice) {
			eError = SGXPostPowerState(hDevHandle, PVR_DEVICE_POWER_STATE_ON,
									PVR_DEVICE_POWER_STATE_IDLE);

			if (eError != PVRSRV_OK) {
				return eError;
			}
		}
		SGXStartTimer(psDevInfo, IMG_TRUE);
	}

	PVR_DPF((PVR_DBG_MESSAGE, "SGXPostClockSpeedChange: SGX clock speed changed from %luHz to %luHz",
			ui32OldClockSpeed, psDevInfo->ui32CoreClockSpeed));
	
	return PVRSRV_OK;
}


static PVRSRV_ERROR SGXRunScript(PVRSRV_SGXDEV_INFO *psDevInfo, SGX_INIT_COMMAND *psScript, IMG_UINT32 ui32NumInitCommands)
{
	IMG_UINT32 ui32PC;
	SGX_INIT_COMMAND *psComm;

	for (ui32PC = 0, psComm = psScript;
		ui32PC < ui32NumInitCommands;
		ui32PC++, psComm++)
	{
		switch (psComm->eOp)
		{
			case SGX_INIT_OP_WRITE_HW_REG:
			{
				OSWriteHWReg(psDevInfo->pvRegsBaseKM, psComm->sWriteHWReg.ui32Offset, psComm->sWriteHWReg.ui32Value);
				PDUMPREG(psComm->sWriteHWReg.ui32Offset, psComm->sWriteHWReg.ui32Value);
				break;
			}
#if defined(PDUMP)
			case SGX_INIT_OP_PDUMP_HW_REG:
			{
				PDUMPREG(psComm->sPDumpHWReg.ui32Offset, psComm->sPDumpHWReg.ui32Value);
				break;
			}
#endif
			case SGX_INIT_OP_HALT:
			{
				return PVRSRV_OK;
			}
			case SGX_INIT_OP_ILLEGAL:
			
			default:
			{
				PVR_DPF((PVR_DBG_ERROR,"SGXRunScript: PC %d: Illegal command: %d", ui32PC, psComm->eOp));
				return PVRSRV_ERROR_GENERIC;
			}
		}

	}

	return PVRSRV_ERROR_GENERIC;;
}

static PVRSRV_ERROR SGXInitialise(PVRSRV_SGXDEV_INFO	*psDevInfo,
								  IMG_BOOL				bHardwareRecovery)
{
	PVRSRV_ERROR		eError;
	IMG_UINT32			ui32ReadOffset, ui32WriteOffset;

	
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_CLKGATECTL, psDevInfo->ui32ClkGateCtl);
	PDUMPREGWITHFLAGS(EUR_CR_CLKGATECTL, psDevInfo->ui32ClkGateCtl, PDUMP_FLAGS_CONTINUOUS);
#if defined(SGX540)
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_CLKGATECTL2, psDevInfo->ui32ClkGateCtl2);
	PDUMPREGWITHFLAGS(EUR_CR_CLKGATECTL2, psDevInfo->ui32ClkGateCtl2, PDUMP_FLAGS_CONTINUOUS);
#endif

	
	SGXReset(psDevInfo, PDUMP_FLAGS_CONTINUOUS);

#if defined(EUR_CR_POWER)
#if defined(SGX531)
	
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_POWER, 1);
	PDUMPREG(EUR_CR_POWER, 1);
#else
	
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_POWER, 0);
	PDUMPREG(EUR_CR_POWER, 0);
#endif
#endif

#if defined(SGX_FEATURE_SYSTEM_CACHE)
	
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_MNE_CR_CTRL, EUR_CR_MNE_CR_CTRL_BYP_CC_MASK);
	PDUMPREG(EUR_CR_MNE_CR_CTRL, EUR_CR_MNE_CR_CTRL_BYP_CC_MASK);
#endif

	
	*psDevInfo->pui32KernelCCBEventKicker = 0;
#if defined(PDUMP)
	PDUMPMEM(IMG_NULL, psDevInfo->psKernelCCBEventKickerMemInfo, 0,
			 sizeof(*psDevInfo->pui32KernelCCBEventKicker), PDUMP_FLAGS_CONTINUOUS,
			 MAKEUNIQUETAG(psDevInfo->psKernelCCBEventKickerMemInfo));
#endif 

	


	psDevInfo->psSGXHostCtl->sTAHWPBDesc.uiAddr = 0;
	psDevInfo->psSGXHostCtl->s3DHWPBDesc.uiAddr = 0;
#if defined(PDUMP)
	PDUMPCOMMENT(" CCB Control - Reset HW PBDesc records");
	PDUMPMEM(IMG_NULL, psDevInfo->psKernelSGXHostCtlMemInfo,
			 offsetof(PVRSRV_SGX_HOST_CTL, sTAHWPBDesc), sizeof(IMG_DEV_VIRTADDR),
			 PDUMP_FLAGS_CONTINUOUS, MAKEUNIQUETAG(psDevInfo->psKernelSGXHostCtlMemInfo));
	PDUMPMEM(IMG_NULL, psDevInfo->psKernelSGXHostCtlMemInfo,
			 offsetof(PVRSRV_SGX_HOST_CTL, s3DHWPBDesc), sizeof(IMG_DEV_VIRTADDR),
			 PDUMP_FLAGS_CONTINUOUS, MAKEUNIQUETAG(psDevInfo->psKernelSGXHostCtlMemInfo));
#endif 

	eError = SGXRunScript(psDevInfo, psDevInfo->sScripts.asInitCommands, SGX_MAX_INIT_COMMANDS);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SGXInitialise: SGXRunScript failed (%d)", eError));
		return (PVRSRV_ERROR_GENERIC);
	}
	
	SGXStartTimer(psDevInfo, !bHardwareRecovery);
	
	if (bHardwareRecovery)
	{
		PVRSRV_SGX_HOST_CTL	*psSGXHostCtl = (PVRSRV_SGX_HOST_CTL *)psDevInfo->psSGXHostCtl;

		
		if (PollForValueKM((volatile IMG_UINT32 *)(&psSGXHostCtl->ui32InterruptClearFlags),
						   0,
						   PVRSRV_USSE_EDM_INTERRUPT_HWR,
						   MAX_HW_TIME_US/WAIT_TRY_COUNT,
						   1000) != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "SGXInitialise: Wait for uKernel HW Recovery failed"));
			return PVRSRV_ERROR_RETRY;
		}
	}


	


	for (ui32ReadOffset = psDevInfo->psKernelCCBCtl->ui32ReadOffset,
			 ui32WriteOffset = psDevInfo->psKernelCCBCtl->ui32WriteOffset;
		 ui32ReadOffset != ui32WriteOffset;
		 ui32ReadOffset = (ui32ReadOffset + 1) & 0xFF)
	{
		*psDevInfo->pui32KernelCCBEventKicker = (*psDevInfo->pui32KernelCCBEventKicker + 1) & 0xFF;
		OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_EVENT_KICK, EUR_CR_EVENT_KICK_NOW_MASK);
	}

	return PVRSRV_OK;
}

PVRSRV_ERROR SGXDeinitialise(IMG_HANDLE hDevCookie)

{
	PVRSRV_SGXDEV_INFO	*psDevInfo = (PVRSRV_SGXDEV_INFO *) hDevCookie;
	PVRSRV_ERROR		eError;

	
	if (psDevInfo->pvRegsBaseKM == IMG_NULL)
	{
		return PVRSRV_OK;
	}

	eError = SGXRunScript(psDevInfo, psDevInfo->sScripts.asDeinitCommands, SGX_MAX_DEINIT_COMMANDS);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SGXDeinitialise: SGXRunScript failed (%d)", eError));
		return (PVRSRV_ERROR_GENERIC);
	}

	return PVRSRV_OK;
}


static PVRSRV_ERROR DevInitSGXPart1 (IMG_VOID *pvDeviceNode)
{
	PVRSRV_SGXDEV_INFO	*psDevInfo;	
	IMG_HANDLE		hKernelDevMemContext;
	IMG_DEV_PHYADDR		sPDDevPAddr;
	IMG_UINT32		i;
	PVRSRV_DEVICE_NODE  *psDeviceNode = (PVRSRV_DEVICE_NODE *)pvDeviceNode;
	DEVICE_MEMORY_HEAP_INFO *psDeviceMemoryHeap = psDeviceNode->sDevMemoryInfo.psDeviceMemoryHeap;
	IMG_HANDLE          hDevInfoOSMemHandle = (IMG_HANDLE)IMG_NULL;
	PVRSRV_ERROR		eError;

	PDUMPCOMMENT("SGX Initialisation Part 1");

	
	PDUMPCOMMENT("SGX Core Version Information: %s", SGX_CORE_FRIENDLY_NAME);
#ifdef SGX_CORE_REV
	PDUMPCOMMENT("SGX Core Revision Information: %d", SGX_CORE_REV);
#else
	PDUMPCOMMENT("SGX Core Revision Information: head rtl");
#endif	

	
	if(OSAllocPages(PVRSRV_OS_PAGEABLE_HEAP|PVRSRV_HAP_MULTI_PROCESS|PVRSRV_HAP_CACHED,
					sizeof(PVRSRV_SGXDEV_INFO),
					(IMG_VOID **)&psDevInfo,
					&hDevInfoOSMemHandle) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"DevInitSGXPart1 : Failed to alloc memory for DevInfo"));
		return (PVRSRV_ERROR_OUT_OF_MEMORY);
	}
	OSMemSet (psDevInfo, 0, sizeof(PVRSRV_SGXDEV_INFO));

	
	psDevInfo->eDeviceType 		= DEV_DEVICE_TYPE;
	psDevInfo->eDeviceClass 	= DEV_DEVICE_CLASS;

	
	psDeviceNode->pvDevice = (IMG_PVOID)psDevInfo;
	psDeviceNode->hDeviceOSMemHandle = hDevInfoOSMemHandle;
	
	
	psDevInfo->pvDeviceMemoryHeap = (IMG_VOID*)psDeviceMemoryHeap;

	
	hKernelDevMemContext = BM_CreateContext(psDeviceNode,
											&sPDDevPAddr,
											IMG_NULL,
											IMG_NULL);

	psDevInfo->sKernelPDDevPAddr = sPDDevPAddr;


	
	for(i=0; i<psDeviceNode->sDevMemoryInfo.ui32HeapCount; i++)
	{
		IMG_HANDLE hDevMemHeap;

		switch(psDeviceMemoryHeap[i].DevMemHeapType)
		{
			case DEVICE_MEMORY_HEAP_KERNEL:
			case DEVICE_MEMORY_HEAP_SHARED:
			case DEVICE_MEMORY_HEAP_SHARED_EXPORTED:
			{
				hDevMemHeap = BM_CreateHeap (hKernelDevMemContext,
												&psDeviceMemoryHeap[i]);
				


				psDeviceMemoryHeap[i].hDevMemHeap = hDevMemHeap;
				break;
			}
		}
	}
	
	eError = MMU_BIFResetPDAlloc(psDevInfo);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"DevInitSGX : Failed to alloc memory for BIF reset"));
		return PVRSRV_ERROR_GENERIC;
	}

	return PVRSRV_OK;
}

IMG_EXPORT
PVRSRV_ERROR SGXGetInfoForSrvinitKM(IMG_HANDLE hDevHandle, SGX_BRIDGE_INFO_FOR_SRVINIT *psInitInfo)
{
	PVRSRV_DEVICE_NODE	*psDeviceNode;
	PVRSRV_SGXDEV_INFO	*psDevInfo;
	PVRSRV_ERROR		eError;

	PDUMPCOMMENT("SGXGetInfoForSrvinit");

	psDeviceNode = (PVRSRV_DEVICE_NODE *)hDevHandle;
	psDevInfo = (PVRSRV_SGXDEV_INFO *)psDeviceNode->pvDevice;

	psInitInfo->sPDDevPAddr = psDevInfo->sKernelPDDevPAddr;

	eError = PVRSRVGetDeviceMemHeapsKM(hDevHandle, &psInitInfo->asHeapInfo[0]);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SGXGetInfoForSrvinit: PVRSRVGetDeviceMemHeapsKM failed (%d)", eError));
		return PVRSRV_ERROR_GENERIC;
	}

	return eError;
}

IMG_EXPORT
PVRSRV_ERROR DevInitSGXPart2KM (PVRSRV_PER_PROCESS_DATA *psPerProc,
                                IMG_HANDLE hDevHandle,
                                SGX_BRIDGE_INIT_INFO *psInitInfo)
{
	PVRSRV_DEVICE_NODE	*psDeviceNode;
	PVRSRV_SGXDEV_INFO	*psDevInfo;
	PVRSRV_ERROR		eError;
	SGX_DEVICE_MAP		*psSGXDeviceMap;
	PVR_POWER_STATE		eDefaultPowerState;

	PDUMPCOMMENT("SGX Initialisation Part 2");

	psDeviceNode = (PVRSRV_DEVICE_NODE *)hDevHandle;
	psDevInfo = (PVRSRV_SGXDEV_INFO *)psDeviceNode->pvDevice;

	

	eError = InitDevInfo(psPerProc, psDeviceNode, psInitInfo);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"DevInitSGXPart2KM: Failed to load EDM program"));
		goto failed_init_dev_info;
	}

	
#ifdef SGX_FEATURE_2D_HARDWARE
	eError = OSCreateResource(&psDevInfo->s2DSlaveportResource);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"DevInitSGXPart2KM: Failed to create resource !"));
		return PVRSRV_ERROR_INIT_FAILURE;
	}
#endif

	eError = SysGetDeviceMemoryMap(PVRSRV_DEVICE_TYPE_SGX,
									(IMG_VOID**)&psSGXDeviceMap);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"DevInitSGXPart2KM: Failed to get device memory map!"));
		return PVRSRV_ERROR_INIT_FAILURE;
	}

	
	if (psSGXDeviceMap->pvRegsCpuVBase)
	{
		psDevInfo->pvRegsBaseKM = psSGXDeviceMap->pvRegsCpuVBase;
	}
	else
	{
		
		psDevInfo->pvRegsBaseKM = OSMapPhysToLin(psSGXDeviceMap->sRegsCpuPBase,
											   psSGXDeviceMap->ui32RegsSize,
											   PVRSRV_HAP_KERNEL_ONLY|PVRSRV_HAP_UNCACHED,
											   IMG_NULL);
		if (!psDevInfo->pvRegsBaseKM)
		{
			PVR_DPF((PVR_DBG_ERROR,"DevInitSGXPart2KM: Failed to map in regs\n"));
			return PVRSRV_ERROR_BAD_MAPPING;
		}
	}
	psDevInfo->ui32RegSize = psSGXDeviceMap->ui32RegsSize;
	psDevInfo->sRegsPhysBase = psSGXDeviceMap->sRegsSysPBase;


#ifdef SGX_FEATURE_2D_HARDWARE
	
	if (psSGXDeviceMap->pvSPCpuVBase)
	{
		psDevInfo->s2DSlavePortKM.pvData = psSGXDeviceMap->pvSPCpuVBase;
	}
	else
	{
		
		psDevInfo->s2DSlavePortKM.pvData = OSMapPhysToLin (psSGXDeviceMap->sSPCpuPBase,
															psSGXDeviceMap->ui32SPSize,
															PVRSRV_HAP_KERNEL_ONLY|PVRSRV_HAP_UNCACHED,
															IMG_NULL);

			
		if (!psDevInfo->s2DSlavePortKM.pvData)
		{
			PVR_DPF((PVR_DBG_ERROR,"DevInitSGXPart2KM: Failed to map 2D Slave port region\n"));
			return PVRSRV_ERROR_BAD_MAPPING;
		}

	}
	psDevInfo->s2DSlavePortKM.ui32DataRange = psSGXDeviceMap->ui32SPSize;
	psDevInfo->s2DSlavePortKM.sPhysBase = psSGXDeviceMap->sSPSysPBase;
#endif


#if defined (SYS_USING_INTERRUPTS)

	
	psDeviceNode->pvISRData = psDeviceNode;
	
	PVR_ASSERT(psDeviceNode->pfnDeviceISR == SGX_ISRHandler);

#endif 

	
#if defined(SUPPORT_ACTIVE_POWER_MANAGEMENT)
	
	psDevInfo->psSGXHostCtl->ui32PowManFlags |= PVRSRV_USSE_EDM_POWMAN_NO_WORK;
	eDefaultPowerState = PVRSRV_POWER_STATE_D3;
#else	
	eDefaultPowerState = PVRSRV_POWER_STATE_D0;
#endif 
	eError = PVRSRVRegisterPowerDevice (psDeviceNode->sDevId.ui32DeviceIndex,
										SGXPrePowerStateExt, SGXPostPowerStateExt,
										SGXPreClockSpeedChange, SGXPostClockSpeedChange,
										(IMG_HANDLE)psDeviceNode,
										PVRSRV_POWER_STATE_D3,
										eDefaultPowerState);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"DevInitSGXPart2KM: failed to register device with power manager"));
		return eError;
	}



	

	OSMemSet(psDevInfo->psKernelCCB, 0, sizeof(PVRSRV_SGX_KERNEL_CCB));
	OSMemSet(psDevInfo->psKernelCCBCtl, 0, sizeof(PVRSRV_SGX_CCB_CTL));
	OSMemSet(psDevInfo->pui32KernelCCBEventKicker, 0, sizeof(*psDevInfo->pui32KernelCCBEventKicker));
	PDUMPCOMMENT("Kernel CCB");
	PDUMPMEM(IMG_NULL, psDevInfo->psKernelCCBMemInfo, 0, sizeof(PVRSRV_SGX_KERNEL_CCB), PDUMP_FLAGS_CONTINUOUS, MAKEUNIQUETAG(psDevInfo->psKernelCCBMemInfo));
	PDUMPCOMMENT("Kernel CCB Control");
	PDUMPMEM(IMG_NULL, psDevInfo->psKernelCCBCtlMemInfo, 0, sizeof(PVRSRV_SGX_CCB_CTL), PDUMP_FLAGS_CONTINUOUS, MAKEUNIQUETAG(psDevInfo->psKernelCCBCtlMemInfo));
	PDUMPCOMMENT("Kernel CCB Event Kicker");
	PDUMPMEM(IMG_NULL, psDevInfo->psKernelCCBEventKickerMemInfo, 0, sizeof(*psDevInfo->pui32KernelCCBEventKicker), PDUMP_FLAGS_CONTINUOUS, MAKEUNIQUETAG(psDevInfo->psKernelCCBEventKickerMemInfo));

	return PVRSRV_OK;

failed_init_dev_info:
	return eError;
}

static PVRSRV_ERROR DevDeInitSGX (IMG_VOID *pvDeviceNode)
{
	PVRSRV_DEVICE_NODE			*psDeviceNode = (PVRSRV_DEVICE_NODE *)pvDeviceNode;
	PVRSRV_SGXDEV_INFO			*psDevInfo = (PVRSRV_SGXDEV_INFO*)psDeviceNode->pvDevice;
	IMG_HANDLE					hDevInfoOSMemHandle = psDeviceNode->hDeviceOSMemHandle;
	PVRSRV_ERROR				eError = PVRSRV_ERROR_INVALID_PARAMS;
	IMG_UINT32					ui32Heap;
	DEVICE_MEMORY_HEAP_INFO		*psDeviceMemoryHeap;
	SGX_DEVICE_MAP				*psSGXDeviceMap;

	if (!psDevInfo)
	{
		
		PVR_DPF((PVR_DBG_ERROR,"DevDeInitSGX: Null DevInfo"));
		return PVRSRV_OK;
	}

#if defined(SUPPORT_HW_RECOVERY)
	if (psDevInfo->hTimer)
	{
		eError = OSRemoveTimer(psDevInfo->hTimer);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"DevDeInitSGX: Failed to remove timer"));
			return 	eError;
		}
		psDevInfo->hTimer = IMG_NULL;
	}
#endif 


	MMU_BIFResetPDFree(psDevInfo);


	

	DeinitDevInfo(psDevInfo);

#if defined(SGX_USING_CMD_PROC_LIST)
	eError = PVRSRVRemoveCmdProcListKM(psDeviceNode->sDevId.ui32DeviceIndex, SGX_COMMAND_COUNT);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"DevDeInitSGX: PVRSRVRemoveCmdProcList failed"));
		return eError;
	}
#endif
	
	psDeviceMemoryHeap = (DEVICE_MEMORY_HEAP_INFO *)psDevInfo->pvDeviceMemoryHeap;
	for(ui32Heap=0; ui32Heap<psDeviceNode->sDevMemoryInfo.ui32HeapCount; ui32Heap++)
	{
		switch(psDeviceMemoryHeap[ui32Heap].DevMemHeapType)
		{
			case DEVICE_MEMORY_HEAP_KERNEL:
			case DEVICE_MEMORY_HEAP_SHARED:
			case DEVICE_MEMORY_HEAP_SHARED_EXPORTED:
			{
				if (psDeviceMemoryHeap[ui32Heap].hDevMemHeap != IMG_NULL)
				{
					BM_DestroyHeap(psDeviceMemoryHeap[ui32Heap].hDevMemHeap);
				}
				break;
			}
		}
	}

	
	eError = BM_DestroyContext(psDeviceNode->sDevMemoryInfo.pBMKernelContext, IMG_NULL);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"DevDeInitSGX : Failed to destroy kernel context"));
		return eError;
	}

	
	eError = PVRSRVRemovePowerDevice (((PVRSRV_DEVICE_NODE*)pvDeviceNode)->sDevId.ui32DeviceIndex);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

#ifdef SGX_FEATURE_2D_HARDWARE
	eError = OSDestroyResource(&psDevInfo->s2DSlaveportResource);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}
#endif

	eError = SysGetDeviceMemoryMap(PVRSRV_DEVICE_TYPE_SGX, 
									(IMG_VOID**)&psSGXDeviceMap);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"DevDeInitSGX: Failed to get device memory map!"));
		return eError;
	}

	
	if (!psSGXDeviceMap->pvRegsCpuVBase)
	{
		
		if (psDevInfo->pvRegsBaseKM != IMG_NULL)
		{
			OSUnMapPhysToLin(psDevInfo->pvRegsBaseKM,
							 psDevInfo->ui32RegSize,
							 PVRSRV_HAP_KERNEL_ONLY|PVRSRV_HAP_UNCACHED,
							 IMG_NULL);
		}
	}

#ifdef SGX_FEATURE_2D_HARDWARE
	
	if (!psSGXDeviceMap->pvSPCpuVBase)
	{
		if (psDevInfo->s2DSlavePortKM.pvData != IMG_NULL)
		{
			OSUnMapPhysToLin(psDevInfo->s2DSlavePortKM.pvData, 
						   psDevInfo->s2DSlavePortKM.ui32DataRange,
						   PVRSRV_HAP_KERNEL_ONLY|PVRSRV_HAP_UNCACHED,
						   IMG_NULL);
		}
	}
#endif 


	
	
	OSFreePages(PVRSRV_OS_PAGEABLE_HEAP|PVRSRV_HAP_MULTI_PROCESS,
				sizeof(PVRSRV_SGXDEV_INFO),
				psDevInfo,
				hDevInfoOSMemHandle);
	psDeviceNode->pvDevice = IMG_NULL;
	
	if (psDeviceMemoryHeap != IMG_NULL)
	{
	
		OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP, 
				sizeof(DEVICE_MEMORY_HEAP_INFO) * psDeviceNode->sDevMemoryInfo.ui32HeapCount, 
				psDeviceMemoryHeap, 
				0);
	}

	return PVRSRV_OK;
}




#if defined(SYS_USING_INTERRUPTS) || defined(SUPPORT_HW_RECOVERY)
static
IMG_VOID HWRecoveryResetSGX (PVRSRV_DEVICE_NODE *psDeviceNode,
									IMG_UINT32 			ui32Component,
									IMG_UINT32			ui32CallerID)
{
	PVRSRV_ERROR		eError;
	PVRSRV_SGXDEV_INFO	*psDevInfo = (PVRSRV_SGXDEV_INFO*)psDeviceNode->pvDevice;
	PVRSRV_SGX_HOST_CTL	*psSGXHostCtl = (PVRSRV_SGX_HOST_CTL *)psDevInfo->psSGXHostCtl;

	PVR_UNREFERENCED_PARAMETER(ui32Component);

	

	eError = PVRSRVPowerLock(ui32CallerID, IMG_FALSE);
	if(eError != PVRSRV_OK)
	{
		


		PVR_DPF((PVR_DBG_WARNING,"HWRecoveryResetSGX: Power transition in progress"));
		return;
	}

	psSGXHostCtl->ui32InterruptClearFlags |= PVRSRV_USSE_EDM_INTERRUPT_HWR;

	PVR_DPF((PVR_DBG_ERROR, "HWRecoveryResetSGX: SGX Hardware Recovery triggered"));
	
	
	
	PDUMPSUSPEND();

	
	do
	{
		eError = SGXInitialise(psDevInfo, IMG_TRUE);
	}
	while (eError == PVRSRV_ERROR_RETRY);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"HWRecoveryResetSGX: SGXInitialise failed (%d)", eError));
	}

	
	PDUMPRESUME();
	
	PVRSRVPowerUnlock(ui32CallerID);
	
	
	SGXScheduleProcessQueues(psDeviceNode);
	
	
	
	PVRSRVProcessQueues(ui32CallerID, IMG_TRUE);
}
#endif 

static struct workdata
{
	PVRSRV_DEVICE_NODE *psDeviceNode;
	IMG_UINT32 ui32Component;
	IMG_UINT32 ui32CallerID;
} gHWRecoveryParams;


static void HWRecoveryWrapper(struct work_struct *work)
{
	HWRecoveryResetSGX(gHWRecoveryParams.psDeviceNode,
			gHWRecoveryParams.ui32Component,
			gHWRecoveryParams.ui32CallerID);
}
DECLARE_WORK(gWork, HWRecoveryWrapper);



#if defined(SUPPORT_HW_RECOVERY)
IMG_VOID SGXOSTimer(IMG_VOID *pvData)
{
	PVRSRV_DEVICE_NODE *psDeviceNode = pvData;
	PVRSRV_SGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	static IMG_UINT32	ui32EDMTasks = 0;
	static IMG_UINT32	ui32LockupCounter = 0; 
	static IMG_UINT32	ui32NumResets = 0;
	IMG_UINT32		ui32CurrentEDMTasks;
	IMG_BOOL		bLockup = IMG_FALSE;
	IMG_BOOL		bPoweredDown;

	
	psDevInfo->ui32TimeStamp++;

	bPoweredDown = (IMG_BOOL)!SGXIsDevicePowered(psDeviceNode);

	
	
	if (bPoweredDown)
	{
		ui32LockupCounter = 0;
	}
	else
	{
		
		ui32CurrentEDMTasks = OSReadHWReg(psDevInfo->pvRegsBaseKM, psDevInfo->ui32EDMTaskReg0);
		if (psDevInfo->ui32EDMTaskReg1 != 0)
		{
			ui32CurrentEDMTasks ^= OSReadHWReg(psDevInfo->pvRegsBaseKM, psDevInfo->ui32EDMTaskReg1);
		}
		if ((ui32CurrentEDMTasks == ui32EDMTasks) &&
			(psDevInfo->ui32NumResets == ui32NumResets))
		{
			ui32LockupCounter++;
			if (ui32LockupCounter == 3)
			{
				ui32LockupCounter = 0;
				PVR_DPF((PVR_DBG_ERROR, "SGXOSTimer() detected SGX lockup (0x%x tasks)", ui32EDMTasks));

				bLockup = IMG_TRUE;
			}
		}
		else
		{
			ui32LockupCounter = 0;
			ui32EDMTasks = ui32CurrentEDMTasks;
			ui32NumResets = psDevInfo->ui32NumResets;
		}
	}

	if (bLockup)
	{
		PVRSRV_SGX_HOST_CTL	*psSGXHostCtl = (PVRSRV_SGX_HOST_CTL *)psDevInfo->psSGXHostCtl;
		
		
		psSGXHostCtl->ui32HostDetectedLockups ++;

		/*
		 * schedule HWRecoveryResetSGX from a work
		 * in the shared queue
		 */
		gHWRecoveryParams.psDeviceNode = psDeviceNode;
		gHWRecoveryParams.ui32Component = 0;
		gHWRecoveryParams.ui32CallerID = TIMER_ID;
		schedule_work(&gWork);
	}
}
#endif 


#if defined(SYS_USING_INTERRUPTS)


IMG_BOOL SGX_ISRHandler (IMG_VOID *pvData)
{
	IMG_BOOL bInterruptProcessed = IMG_FALSE;

	
	{
		IMG_UINT32 ui32EventStatus, ui32EventEnable;
		IMG_UINT32 ui32EventClear = 0;
		PVRSRV_DEVICE_NODE *psDeviceNode;
		PVRSRV_SGXDEV_INFO *psDevInfo;

		
		if(pvData == IMG_NULL)
		{
			PVR_DPF((PVR_DBG_ERROR, "SGX_ISRHandler: Invalid params\n"));			
			return bInterruptProcessed;
		}

		psDeviceNode = (PVRSRV_DEVICE_NODE *)pvData;
		psDevInfo = (PVRSRV_SGXDEV_INFO *)psDeviceNode->pvDevice;

		ui32EventStatus = OSReadHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_EVENT_STATUS);
		ui32EventEnable = OSReadHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_EVENT_HOST_ENABLE);

		

		gui32EventStatusServicesByISR = ui32EventStatus;

		
		ui32EventStatus &= ui32EventEnable;

		if (ui32EventStatus & EUR_CR_EVENT_STATUS_SW_EVENT_MASK)
		{
			ui32EventClear |= EUR_CR_EVENT_HOST_CLEAR_SW_EVENT_MASK;
		}

		if (ui32EventClear)
		{
			bInterruptProcessed = IMG_TRUE;

			
			ui32EventClear |= EUR_CR_EVENT_HOST_CLEAR_MASTER_INTERRUPT_MASK;

			
			OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_EVENT_HOST_CLEAR, ui32EventClear);
		}
	}

		return bInterruptProcessed;
}


IMG_VOID SGX_MISRHandler (IMG_VOID *pvData)
{
	PVRSRV_DEVICE_NODE	*psDeviceNode = (PVRSRV_DEVICE_NODE *)pvData;
	PVRSRV_SGXDEV_INFO	*psDevInfo = (PVRSRV_SGXDEV_INFO*)psDeviceNode->pvDevice;
	PVRSRV_SGX_HOST_CTL	*psSGXHostCtl = (PVRSRV_SGX_HOST_CTL *)psDevInfo->psSGXHostCtl;
	
	if ((psSGXHostCtl->ui32InterruptFlags & PVRSRV_USSE_EDM_INTERRUPT_HWR) &&
		!(psSGXHostCtl->ui32InterruptClearFlags & PVRSRV_USSE_EDM_INTERRUPT_HWR))
	{
		HWRecoveryResetSGX(psDeviceNode, 0, ISR_ID);
	}

#if defined(SUPPORT_ACTIVE_POWER_MANAGEMENT)
	SGXTestActivePowerEvent(psDeviceNode, ISR_ID);
#endif 
}
#endif 


PVRSRV_ERROR SGXRegisterDevice (PVRSRV_DEVICE_NODE *psDeviceNode)
{
	DEVICE_MEMORY_INFO *psDevMemoryInfo;
	DEVICE_MEMORY_HEAP_INFO *psDeviceMemoryHeap;

	
	psDeviceNode->sDevId.eDeviceType	= DEV_DEVICE_TYPE;
	psDeviceNode->sDevId.eDeviceClass	= DEV_DEVICE_CLASS;

	psDeviceNode->pfnInitDevice		= DevInitSGXPart1;
	psDeviceNode->pfnDeInitDevice		= DevDeInitSGX;

	

	psDeviceNode->pfnMMUInitialise = MMU_Initialise;
	psDeviceNode->pfnMMUFinalise = MMU_Finalise;
	psDeviceNode->pfnMMUInsertHeap = MMU_InsertHeap;
	psDeviceNode->pfnMMUCreate = MMU_Create;
	psDeviceNode->pfnMMUDelete = MMU_Delete;
	psDeviceNode->pfnMMUAlloc = MMU_Alloc;
	psDeviceNode->pfnMMUFree = MMU_Free;
	psDeviceNode->pfnMMUMapPages = MMU_MapPages;
	psDeviceNode->pfnMMUMapShadow = MMU_MapShadow;
	psDeviceNode->pfnMMUUnmapPages = MMU_UnmapPages;
	psDeviceNode->pfnMMUMapScatter = MMU_MapScatter;
	psDeviceNode->pfnMMUGetPhysPageAddr = MMU_GetPhysPageAddr;
	psDeviceNode->pfnMMUGetPDDevPAddr = MMU_GetPDDevPAddr;

#if defined (SYS_USING_INTERRUPTS)
	

	psDeviceNode->pfnDeviceISR = SGX_ISRHandler;
	psDeviceNode->pfnDeviceMISR = SGX_MISRHandler;
#endif

	

	psDeviceNode->pfnDeviceCommandComplete = SGXCommandComplete;
	
	

	psDevMemoryInfo = &psDeviceNode->sDevMemoryInfo;
	
	psDevMemoryInfo->ui32AddressSpaceSizeLog2 = SGX_ADDRESS_SPACE_SIZE;

	
	psDevMemoryInfo->ui32Flags = 0;

	
	psDevMemoryInfo->ui32HeapCount = SGX_MAX_HEAP_ID;

	
	psDevMemoryInfo->ui32SyncHeapID = SGX_SYNCINFO_HEAP_ID;

	
	psDevMemoryInfo->ui32MappingHeapID = SGX_GENERAL_MAPPING_HEAP_ID;

	
	if(OSAllocMem( PVRSRV_OS_PAGEABLE_HEAP, 
					 sizeof(DEVICE_MEMORY_HEAP_INFO) * psDevMemoryInfo->ui32HeapCount, 
					 (IMG_VOID **)&psDevMemoryInfo->psDeviceMemoryHeap, 0) != PVRSRV_OK)	
	{
		PVR_DPF((PVR_DBG_ERROR,"SGXRegisterDevice : Failed to alloc memory for DEVICE_MEMORY_HEAP_INFO"));
		return (PVRSRV_ERROR_OUT_OF_MEMORY);
	}
	OSMemSet(psDevMemoryInfo->psDeviceMemoryHeap, 0, sizeof(DEVICE_MEMORY_HEAP_INFO) * psDevMemoryInfo->ui32HeapCount);
	
	psDeviceMemoryHeap = psDevMemoryInfo->psDeviceMemoryHeap;

	


	
	psDeviceMemoryHeap[SGX_GENERAL_HEAP_ID].ui32HeapID = HEAP_ID( PVRSRV_DEVICE_TYPE_SGX , SGX_GENERAL_HEAP_ID);
	psDeviceMemoryHeap[SGX_GENERAL_HEAP_ID].sDevVAddrBase.uiAddr = SGX_GENERAL_HEAP_BASE;
	psDeviceMemoryHeap[SGX_GENERAL_HEAP_ID].ui32HeapSize = SGX_GENERAL_HEAP_SIZE;
	psDeviceMemoryHeap[SGX_GENERAL_HEAP_ID].ui32Attribs = PVRSRV_HAP_WRITECOMBINE
														| PVRSRV_MEM_RAM_BACKED_ALLOCATION
														| PVRSRV_HAP_SINGLE_PROCESS;
	psDeviceMemoryHeap[SGX_GENERAL_HEAP_ID].pszName = "General";
	psDeviceMemoryHeap[SGX_GENERAL_HEAP_ID].pszBSName = "General BS";
	psDeviceMemoryHeap[SGX_GENERAL_HEAP_ID].DevMemHeapType = DEVICE_MEMORY_HEAP_PERCONTEXT;

	
	psDeviceMemoryHeap[SGX_TADATA_HEAP_ID].ui32HeapID = HEAP_ID( PVRSRV_DEVICE_TYPE_SGX , SGX_TADATA_HEAP_ID);
	psDeviceMemoryHeap[SGX_TADATA_HEAP_ID].sDevVAddrBase.uiAddr = SGX_TADATA_HEAP_BASE;
	psDeviceMemoryHeap[SGX_TADATA_HEAP_ID].ui32HeapSize = SGX_TADATA_HEAP_SIZE;
	psDeviceMemoryHeap[SGX_TADATA_HEAP_ID].ui32Attribs = PVRSRV_HAP_WRITECOMBINE 
														| PVRSRV_MEM_RAM_BACKED_ALLOCATION
#if 0
														| PVRSRV_HAP_KERNEL_ONLY;
#else
														| PVRSRV_HAP_MULTI_PROCESS;
#endif
	psDeviceMemoryHeap[SGX_TADATA_HEAP_ID].pszName = "TA Data";
	psDeviceMemoryHeap[SGX_TADATA_HEAP_ID].pszBSName = "TA Data BS";
	psDeviceMemoryHeap[SGX_TADATA_HEAP_ID].DevMemHeapType = DEVICE_MEMORY_HEAP_PERCONTEXT;

	
	psDeviceMemoryHeap[SGX_KERNEL_CODE_HEAP_ID].ui32HeapID = HEAP_ID( PVRSRV_DEVICE_TYPE_SGX ,SGX_KERNEL_CODE_HEAP_ID);
	psDeviceMemoryHeap[SGX_KERNEL_CODE_HEAP_ID].sDevVAddrBase.uiAddr = SGX_KERNEL_CODE_HEAP_BASE;
	psDeviceMemoryHeap[SGX_KERNEL_CODE_HEAP_ID].ui32HeapSize = SGX_KERNEL_CODE_HEAP_SIZE;
	psDeviceMemoryHeap[SGX_KERNEL_CODE_HEAP_ID].ui32Attribs = PVRSRV_HAP_WRITECOMBINE 
															| PVRSRV_MEM_RAM_BACKED_ALLOCATION
#if 0
															| PVRSRV_HAP_KERNEL_ONLY;
#else
					| PVRSRV_HAP_MULTI_PROCESS;
#endif
	psDeviceMemoryHeap[SGX_KERNEL_CODE_HEAP_ID].pszName = "Kernel";
	psDeviceMemoryHeap[SGX_KERNEL_CODE_HEAP_ID].pszBSName = "Kernel BS";
	psDeviceMemoryHeap[SGX_KERNEL_CODE_HEAP_ID].DevMemHeapType = DEVICE_MEMORY_HEAP_SHARED_EXPORTED;

	
	psDeviceMemoryHeap[SGX_VIDEO_CODE_HEAP_ID].ui32HeapID = HEAP_ID( PVRSRV_DEVICE_TYPE_SGX ,SGX_VIDEO_CODE_HEAP_ID);
	psDeviceMemoryHeap[SGX_VIDEO_CODE_HEAP_ID].sDevVAddrBase.uiAddr = SGX_VIDEO_CODE_HEAP_BASE;
	psDeviceMemoryHeap[SGX_VIDEO_CODE_HEAP_ID].ui32HeapSize = SGX_VIDEO_CODE_HEAP_SIZE;
	psDeviceMemoryHeap[SGX_VIDEO_CODE_HEAP_ID].ui32Attribs = PVRSRV_HAP_WRITECOMBINE 
																| PVRSRV_MEM_RAM_BACKED_ALLOCATION
																| PVRSRV_HAP_KERNEL_ONLY;
	psDeviceMemoryHeap[SGX_VIDEO_CODE_HEAP_ID].pszName = "Video";
	psDeviceMemoryHeap[SGX_VIDEO_CODE_HEAP_ID].pszBSName = "Video BS";
	psDeviceMemoryHeap[SGX_VIDEO_CODE_HEAP_ID].DevMemHeapType = DEVICE_MEMORY_HEAP_SHARED;

	
	psDeviceMemoryHeap[SGX_KERNEL_VIDEO_DATA_HEAP_ID].ui32HeapID = HEAP_ID( PVRSRV_DEVICE_TYPE_SGX ,SGX_KERNEL_VIDEO_DATA_HEAP_ID);
	psDeviceMemoryHeap[SGX_KERNEL_VIDEO_DATA_HEAP_ID].sDevVAddrBase.uiAddr = SGX_KERNEL_VIDEO_DATA_HEAP_BASE;
	psDeviceMemoryHeap[SGX_KERNEL_VIDEO_DATA_HEAP_ID].ui32HeapSize = SGX_KERNEL_VIDEO_DATA_HEAP_SIZE;
	psDeviceMemoryHeap[SGX_KERNEL_VIDEO_DATA_HEAP_ID].ui32Attribs = PVRSRV_HAP_WRITECOMBINE 
																| PVRSRV_MEM_RAM_BACKED_ALLOCATION |
#if 0
																PVRSRV_HAP_KERNEL_ONLY;
#else
						PVRSRV_HAP_MULTI_PROCESS;
#endif
	psDeviceMemoryHeap[SGX_KERNEL_VIDEO_DATA_HEAP_ID].pszName = "KernelVideoData";
	psDeviceMemoryHeap[SGX_KERNEL_VIDEO_DATA_HEAP_ID].pszBSName = "KernelVideoData BS";
	psDeviceMemoryHeap[SGX_KERNEL_VIDEO_DATA_HEAP_ID].DevMemHeapType = DEVICE_MEMORY_HEAP_SHARED_EXPORTED;

	
	psDeviceMemoryHeap[SGX_PIXELSHADER_HEAP_ID].ui32HeapID = HEAP_ID( PVRSRV_DEVICE_TYPE_SGX ,SGX_PIXELSHADER_HEAP_ID);
	psDeviceMemoryHeap[SGX_PIXELSHADER_HEAP_ID].sDevVAddrBase.uiAddr = SGX_PIXELSHADER_HEAP_BASE;
	psDeviceMemoryHeap[SGX_PIXELSHADER_HEAP_ID].ui32HeapSize = SGX_PIXELSHADER_HEAP_SIZE;
	psDeviceMemoryHeap[SGX_PIXELSHADER_HEAP_ID].ui32Attribs = PVRSRV_HAP_WRITECOMBINE 
																| PVRSRV_MEM_RAM_BACKED_ALLOCATION
																| PVRSRV_HAP_SINGLE_PROCESS;
	psDeviceMemoryHeap[SGX_PIXELSHADER_HEAP_ID].pszName = "PixelShaderUSSE";
	psDeviceMemoryHeap[SGX_PIXELSHADER_HEAP_ID].pszBSName = "PixelShaderUSSE BS";
	psDeviceMemoryHeap[SGX_PIXELSHADER_HEAP_ID].DevMemHeapType = DEVICE_MEMORY_HEAP_PERCONTEXT;

	
	psDeviceMemoryHeap[SGX_VERTEXSHADER_HEAP_ID].ui32HeapID = HEAP_ID( PVRSRV_DEVICE_TYPE_SGX ,SGX_VERTEXSHADER_HEAP_ID);
	psDeviceMemoryHeap[SGX_VERTEXSHADER_HEAP_ID].sDevVAddrBase.uiAddr = SGX_VERTEXSHADER_HEAP_BASE;
	psDeviceMemoryHeap[SGX_VERTEXSHADER_HEAP_ID].ui32HeapSize = SGX_VERTEXSHADER_HEAP_SIZE;
	psDeviceMemoryHeap[SGX_VERTEXSHADER_HEAP_ID].ui32Attribs = PVRSRV_HAP_WRITECOMBINE
																| PVRSRV_MEM_RAM_BACKED_ALLOCATION
																| PVRSRV_HAP_SINGLE_PROCESS;
	psDeviceMemoryHeap[SGX_VERTEXSHADER_HEAP_ID].pszName = "VertexShaderUSSE";
	psDeviceMemoryHeap[SGX_VERTEXSHADER_HEAP_ID].pszBSName = "VertexShaderUSSE BS";
	psDeviceMemoryHeap[SGX_VERTEXSHADER_HEAP_ID].DevMemHeapType = DEVICE_MEMORY_HEAP_PERCONTEXT;

	
	psDeviceMemoryHeap[SGX_PDSPIXEL_CODEDATA_HEAP_ID].ui32HeapID = HEAP_ID( PVRSRV_DEVICE_TYPE_SGX ,SGX_PDSPIXEL_CODEDATA_HEAP_ID);
	psDeviceMemoryHeap[SGX_PDSPIXEL_CODEDATA_HEAP_ID].sDevVAddrBase.uiAddr = SGX_PDSPIXEL_CODEDATA_HEAP_BASE;
	psDeviceMemoryHeap[SGX_PDSPIXEL_CODEDATA_HEAP_ID].ui32HeapSize = SGX_PDSPIXEL_CODEDATA_HEAP_SIZE;
	psDeviceMemoryHeap[SGX_PDSPIXEL_CODEDATA_HEAP_ID].ui32Attribs = PVRSRV_HAP_WRITECOMBINE 
																| PVRSRV_MEM_RAM_BACKED_ALLOCATION
																| PVRSRV_HAP_SINGLE_PROCESS;
	psDeviceMemoryHeap[SGX_PDSPIXEL_CODEDATA_HEAP_ID].pszName = "PDSPixelCodeData";
	psDeviceMemoryHeap[SGX_PDSPIXEL_CODEDATA_HEAP_ID].pszBSName = "PDSPixelCodeData BS";
	psDeviceMemoryHeap[SGX_PDSPIXEL_CODEDATA_HEAP_ID].DevMemHeapType = DEVICE_MEMORY_HEAP_PERCONTEXT;

	
	psDeviceMemoryHeap[SGX_PDSVERTEX_CODEDATA_HEAP_ID].ui32HeapID = HEAP_ID( PVRSRV_DEVICE_TYPE_SGX ,SGX_PDSVERTEX_CODEDATA_HEAP_ID);
	psDeviceMemoryHeap[SGX_PDSVERTEX_CODEDATA_HEAP_ID].sDevVAddrBase.uiAddr = SGX_PDSVERTEX_CODEDATA_HEAP_BASE;
	psDeviceMemoryHeap[SGX_PDSVERTEX_CODEDATA_HEAP_ID].ui32HeapSize = SGX_PDSVERTEX_CODEDATA_HEAP_SIZE;
	psDeviceMemoryHeap[SGX_PDSVERTEX_CODEDATA_HEAP_ID].ui32Attribs = PVRSRV_HAP_WRITECOMBINE
																| PVRSRV_MEM_RAM_BACKED_ALLOCATION
																| PVRSRV_HAP_SINGLE_PROCESS;
	psDeviceMemoryHeap[SGX_PDSVERTEX_CODEDATA_HEAP_ID].pszName = "PDSVertexCodeData";
	psDeviceMemoryHeap[SGX_PDSVERTEX_CODEDATA_HEAP_ID].pszBSName = "PDSVertexCodeData BS";
	psDeviceMemoryHeap[SGX_PDSVERTEX_CODEDATA_HEAP_ID].DevMemHeapType = DEVICE_MEMORY_HEAP_PERCONTEXT;

	
	psDeviceMemoryHeap[SGX_SYNCINFO_HEAP_ID].ui32HeapID = HEAP_ID( PVRSRV_DEVICE_TYPE_SGX ,SGX_SYNCINFO_HEAP_ID);
	psDeviceMemoryHeap[SGX_SYNCINFO_HEAP_ID].sDevVAddrBase.uiAddr = SGX_SYNCINFO_HEAP_BASE;
	psDeviceMemoryHeap[SGX_SYNCINFO_HEAP_ID].ui32HeapSize = SGX_SYNCINFO_HEAP_SIZE;
	
	psDeviceMemoryHeap[SGX_SYNCINFO_HEAP_ID].ui32Attribs = PVRSRV_HAP_WRITECOMBINE
														| PVRSRV_MEM_RAM_BACKED_ALLOCATION
														| PVRSRV_HAP_MULTI_PROCESS;
	psDeviceMemoryHeap[SGX_SYNCINFO_HEAP_ID].pszName = "CacheCoherent";
	psDeviceMemoryHeap[SGX_SYNCINFO_HEAP_ID].pszBSName = "CacheCoherent BS";
	
	psDeviceMemoryHeap[SGX_SYNCINFO_HEAP_ID].DevMemHeapType = DEVICE_MEMORY_HEAP_SHARED_EXPORTED;

	
	psDeviceMemoryHeap[SGX_3DPARAMETERS_HEAP_ID].ui32HeapID = HEAP_ID(PVRSRV_DEVICE_TYPE_SGX, SGX_3DPARAMETERS_HEAP_ID);
	psDeviceMemoryHeap[SGX_3DPARAMETERS_HEAP_ID].sDevVAddrBase.uiAddr = SGX_3DPARAMETERS_HEAP_BASE;
	psDeviceMemoryHeap[SGX_3DPARAMETERS_HEAP_ID].ui32HeapSize = SGX_3DPARAMETERS_HEAP_SIZE;
	psDeviceMemoryHeap[SGX_3DPARAMETERS_HEAP_ID].pszName = "3DParameters";
	psDeviceMemoryHeap[SGX_3DPARAMETERS_HEAP_ID].pszBSName = "3DParameters BS";
#if defined(SUPPORT_PERCONTEXT_PB)
	psDeviceMemoryHeap[SGX_3DPARAMETERS_HEAP_ID].ui32Attribs = PVRSRV_HAP_WRITECOMBINE
															| PVRSRV_MEM_RAM_BACKED_ALLOCATION
															| PVRSRV_HAP_SINGLE_PROCESS;
	psDeviceMemoryHeap[SGX_3DPARAMETERS_HEAP_ID].DevMemHeapType = DEVICE_MEMORY_HEAP_PERCONTEXT;
#else
	psDeviceMemoryHeap[SGX_3DPARAMETERS_HEAP_ID].ui32Attribs = PVRSRV_HAP_WRITECOMBINE
													| PVRSRV_MEM_RAM_BACKED_ALLOCATION
													| PVRSRV_HAP_MULTI_PROCESS;
	psDeviceMemoryHeap[SGX_3DPARAMETERS_HEAP_ID].DevMemHeapType = DEVICE_MEMORY_HEAP_SHARED_EXPORTED;
#endif		

	
	psDeviceMemoryHeap[SGX_GENERAL_MAPPING_HEAP_ID].ui32HeapID = HEAP_ID( PVRSRV_DEVICE_TYPE_SGX , SGX_GENERAL_MAPPING_HEAP_ID);
	psDeviceMemoryHeap[SGX_GENERAL_MAPPING_HEAP_ID].sDevVAddrBase.uiAddr = SGX_GENERAL_MAPPING_HEAP_BASE;
	psDeviceMemoryHeap[SGX_GENERAL_MAPPING_HEAP_ID].ui32HeapSize = SGX_GENERAL_MAPPING_HEAP_SIZE;
	psDeviceMemoryHeap[SGX_GENERAL_MAPPING_HEAP_ID].ui32Attribs = PVRSRV_HAP_WRITECOMBINE | PVRSRV_HAP_MULTI_PROCESS;
	psDeviceMemoryHeap[SGX_GENERAL_MAPPING_HEAP_ID].pszName = "GeneralMapping";
	psDeviceMemoryHeap[SGX_GENERAL_MAPPING_HEAP_ID].pszBSName = "GeneralMapping BS";

	psDeviceMemoryHeap[SGX_GENERAL_MAPPING_HEAP_ID].DevMemHeapType = DEVICE_MEMORY_HEAP_SHARED_EXPORTED;

	psDeviceMemoryHeap[SGX_ALT_MAPPING_HEAP_ID].ui32HeapID = HEAP_ID( PVRSRV_DEVICE_TYPE_SGX , SGX_ALT_MAPPING_HEAP_ID);
	psDeviceMemoryHeap[SGX_ALT_MAPPING_HEAP_ID].sDevVAddrBase.uiAddr = SGX_ALT_MAPPING_HEAP_BASE;
	psDeviceMemoryHeap[SGX_ALT_MAPPING_HEAP_ID].ui32HeapSize = SGX_ALT_MAPPING_HEAP_SIZE;
	psDeviceMemoryHeap[SGX_ALT_MAPPING_HEAP_ID].ui32Attribs = PVRSRV_HAP_WRITECOMBINE | PVRSRV_HAP_MULTI_PROCESS;
	psDeviceMemoryHeap[SGX_ALT_MAPPING_HEAP_ID].pszName = "AltMapping";
	psDeviceMemoryHeap[SGX_ALT_MAPPING_HEAP_ID].pszBSName = "AltMapping BS";

	psDeviceMemoryHeap[SGX_ALT_MAPPING_HEAP_ID].DevMemHeapType = DEVICE_MEMORY_HEAP_SHARED_EXPORTED;


#if defined(SGX_FEATURE_2D_HARDWARE)
	

	
	psDeviceMemoryHeap[SGX_2D_HEAP_ID].ui32HeapID = HEAP_ID( PVRSRV_DEVICE_TYPE_SGX ,SGX_2D_HEAP_ID);
	psDeviceMemoryHeap[SGX_2D_HEAP_ID].sDevVAddrBase.uiAddr = SGX_2D_HEAP_BASE;
	psDeviceMemoryHeap[SGX_2D_HEAP_ID].ui32HeapSize = SGX_2D_HEAP_SIZE;
	psDeviceMemoryHeap[SGX_2D_HEAP_ID].ui32Attribs = PVRSRV_HAP_WRITECOMBINE
														| PVRSRV_MEM_RAM_BACKED_ALLOCATION
														| PVRSRV_HAP_SINGLE_PROCESS;
	psDeviceMemoryHeap[SGX_2D_HEAP_ID].pszName = "2D";
	psDeviceMemoryHeap[SGX_2D_HEAP_ID].pszBSName = "2D BS";
	
	psDeviceMemoryHeap[SGX_2D_HEAP_ID].DevMemHeapType = DEVICE_MEMORY_HEAP_SHARED_EXPORTED;
#endif 


	return PVRSRV_OK;
}

IMG_EXPORT
PVRSRV_ERROR SGXGetClientInfoKM(IMG_HANDLE					hDevCookie,
								PVR3DIF4_CLIENT_INFO*		psClientInfo)
{
	PVRSRV_SGXDEV_INFO *psDevInfo = (PVRSRV_SGXDEV_INFO *)((PVRSRV_DEVICE_NODE *)hDevCookie)->pvDevice;

	

	psDevInfo->ui32ClientRefCount++;
#ifdef PDUMP
	if(psDevInfo->ui32ClientRefCount == 1)
	{
		psDevInfo->psKernelCCBInfo->ui32CCBDumpWOff = 0;
	}
#endif
	

	psClientInfo->ui32ProcessID = OSGetCurrentProcessIDKM();

	

	OSMemCopy(&psClientInfo->asDevData, &psDevInfo->asSGXDevData, sizeof(psClientInfo->asDevData));

	
	return PVRSRV_OK;
}


IMG_EXPORT
PVRSRV_ERROR SGXGetMiscInfoKM(PVRSRV_SGXDEV_INFO	*psDevInfo,
							  SGX_MISC_INFO			*psMiscInfo)
{
	switch(psMiscInfo->eRequest)
	{
		case SGX_MISC_INFO_REQUEST_CLOCKSPEED:
		{
			psMiscInfo->uData.ui32SGXClockSpeed = psDevInfo->ui32CoreClockSpeed;
			return PVRSRV_OK;
		}
#ifdef SUPPORT_SGX_HWPERF
		case SGX_MISC_INFO_REQUEST_HWPERF_CB_ON:
		{
			psDevInfo->psSGXHostCtl->ui32HWPerfFlags |= PVRSRV_SGX_HWPERF_ON;
			return PVRSRV_OK;
		}
		case SGX_MISC_INFO_REQUEST_HWPERF_CB_OFF:
		{
			psDevInfo->psSGXHostCtl->ui32HWPerfFlags &= ~PVRSRV_SGX_HWPERF_ON;
			return PVRSRV_OK;
		}
		case SGX_MISC_INFO_REQUEST_HWPERF_RETRIEVE_CB:
		{
			SGX_MISC_INFO_HWPERF_RETRIEVE_CB* psRetrieve = &psMiscInfo->uData.sRetrieveCB;
			PVRSRV_SGX_HWPERF_CB* psHWPerfCB = (PVRSRV_SGX_HWPERF_CB*)psDevInfo->psKernelHWPerfCBMemInfo->pvLinAddrKM;
			IMG_UINT i = 0;

			for (; psHWPerfCB->ui32Woff != psHWPerfCB->ui32Roff && i < psRetrieve->ui32ArraySize; i++)
			{
				PVRSRV_SGX_HWPERF_CBDATA* psData = &psHWPerfCB->psHWPerfCBData[psHWPerfCB->ui32Roff];
				OSMemCopy(&psRetrieve->psHWPerfData[i], psData, sizeof(PVRSRV_SGX_HWPERF_CBDATA));
				psRetrieve->psHWPerfData[i].ui32ClockSpeed = psDevInfo->ui32CoreClockSpeed;
				psRetrieve->psHWPerfData[i].ui32TimeMax = psDevInfo->ui32uKernelTimerClock;
				psHWPerfCB->ui32Roff = (psHWPerfCB->ui32Roff + 1) & (PVRSRV_SGX_HWPERF_CBSIZE - 1);
			}
			psRetrieve->ui32DataCount = i;
			psRetrieve->ui32Time = OSClockus();
			return PVRSRV_OK;
		}
#endif 
		default:
		{
			
			return PVRSRV_ERROR_INVALID_PARAMS;
		}
	}
}

#if defined(SUPPORT_SGX_HWPERF)
IMG_EXPORT
PVRSRV_ERROR SGXReadDiffCountersKM(IMG_HANDLE					hDevHandle,
									 IMG_UINT32					ui32Reg,
									 IMG_UINT32					*pui32Old,
									 IMG_BOOL					bNew,
									 IMG_UINT32					ui32New,
									 IMG_UINT32					ui32NewReset,
									 IMG_UINT32					ui32CountersReg,
									 IMG_UINT32					*pui32Time,
									 IMG_BOOL					*pbActive,
 									 PVRSRV_SGXDEV_DIFF_INFO	*psDiffs)
{
	PVRSRV_ERROR    	eError;
	SYS_DATA			*psSysData;
	PVRSRV_POWER_DEV	*psPowerDevice;
	IMG_BOOL			bPowered = IMG_FALSE;
	PVRSRV_DEVICE_NODE	*psDeviceNode = hDevHandle;
	PVRSRV_SGXDEV_INFO	*psDevInfo = psDeviceNode->pvDevice;

	
	if(bNew)
	{
		psDevInfo->ui32HWGroupRequested = ui32New;
	}
	psDevInfo->ui32HWReset |= ui32NewReset;

	
	eError = PVRSRVPowerLock(KERNEL_ID, IMG_FALSE);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	SysAcquireData(&psSysData);

	
	psPowerDevice = psSysData->psPowerDeviceList;
	while (psPowerDevice)
	{
		if (psPowerDevice->ui32DeviceIndex == psDeviceNode->sDevId.ui32DeviceIndex)
		{
			bPowered = (IMG_BOOL)(psPowerDevice->eCurrentPowerState == PVRSRV_POWER_STATE_D0);
			break;
		}

		psPowerDevice = psPowerDevice->psNext;
	}

	
	*pbActive = bPowered;

	

	{
		PVRSRV_SGXDEV_DIFF_INFO	sNew, *psPrev = &psDevInfo->sDiffInfo;
		IMG_UINT32					i;

		sNew.ui32Time[0] = OSClockus();

		
		*pui32Time = sNew.ui32Time[0];

		
		if(sNew.ui32Time[0] != psPrev->ui32Time[0] && bPowered)
		{
			
			*pui32Old = OSReadHWReg(psDevInfo->pvRegsBaseKM, ui32Reg);

			for (i = 0; i < PVRSRV_SGX_DIFF_NUM_COUNTERS; ++i)
			{
				sNew.aui32Counters[i] = OSReadHWReg(psDevInfo->pvRegsBaseKM, ui32CountersReg + (i * 4));
			}

			

			if (psDevInfo->ui32HWGroupRequested != *pui32Old)
			{
				
				if(psDevInfo->ui32HWReset != 0)
				{
					OSWriteHWReg(psDevInfo->pvRegsBaseKM, ui32Reg, psDevInfo->ui32HWGroupRequested | psDevInfo->ui32HWReset);
					psDevInfo->ui32HWReset = 0;
				}

				OSWriteHWReg(psDevInfo->pvRegsBaseKM, ui32Reg, psDevInfo->ui32HWGroupRequested);
			}

			sNew.ui32Marker[0] = psDevInfo->ui32KickTACounter;
			sNew.ui32Marker[1] = psDevInfo->ui32KickTARenderCounter;

			sNew.ui32Time[1] = psDevInfo->psSGXHostCtl->ui32TimeWraps;

			
			for (i = 0; i < PVRSRV_SGX_DIFF_NUM_COUNTERS; ++i)
			{
				psDiffs->aui32Counters[i] = sNew.aui32Counters[i] - psPrev->aui32Counters[i];
			}

			psDiffs->ui32Marker[0]			= sNew.ui32Marker[0] - psPrev->ui32Marker[0];
			psDiffs->ui32Marker[1]			= sNew.ui32Marker[1] - psPrev->ui32Marker[1];

			psDiffs->ui32Time[0]			= sNew.ui32Time[0] - psPrev->ui32Time[0];
			psDiffs->ui32Time[1]			= sNew.ui32Time[1] - psPrev->ui32Time[1];

			
			*psPrev = sNew;
		}
		else
		{
			
			for (i = 0; i < PVRSRV_SGX_DIFF_NUM_COUNTERS; ++i)
			{
				psDiffs->aui32Counters[i] = 0;
			}

			psDiffs->ui32Marker[0] = 0;
			psDiffs->ui32Marker[1] = 0;

			psDiffs->ui32Time[0] = 0;
			psDiffs->ui32Time[1] = 0;
		}
	}

	
	PVRSRVPowerUnlock(KERNEL_ID);

#if defined(SUPPORT_ACTIVE_POWER_MANAGEMENT)
	SGXTestActivePowerEvent(psDeviceNode, KERNEL_ID);
#endif 

	return eError;
}
#else
#endif 


