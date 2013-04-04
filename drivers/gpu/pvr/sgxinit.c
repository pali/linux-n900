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
#include <linux/slab.h>

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
#include "sgx_bridge_km.h"

#include "pdump_km.h"
#include "ra.h"
#include "mmu.h"
#include "handle.h"
#include "perproc.h"

#include "sgxutils.h"

static IMG_BOOL SGX_ISRHandler(void *pvData);

static u32 gui32EventStatusServicesByISR;

static enum PVRSRV_ERROR SGXInitialise(struct PVRSRV_SGXDEV_INFO *psDevInfo,
				  IMG_BOOL bHardwareRecovery);
static enum PVRSRV_ERROR SGXDeinitialise(void *hDevCookie);

struct timer_work_data {
	struct PVRSRV_DEVICE_NODE *psDeviceNode;
	struct delayed_work work;
	struct workqueue_struct *work_queue;
	unsigned int interval;
	bool armed;
};

static enum PVRSRV_ERROR SGXOSTimerEnable(struct timer_work_data *data);
static enum PVRSRV_ERROR SGXOSTimerCancel(struct timer_work_data *data);
static struct timer_work_data *
SGXOSTimerInit(struct PVRSRV_DEVICE_NODE *psDeviceNode);
static void SGXOSTimerDeInit(struct timer_work_data *data);

enum PVR_DEVICE_POWER_STATE {
	PVR_DEVICE_POWER_STATE_ON = 0,
	PVR_DEVICE_POWER_STATE_IDLE = 1,
	PVR_DEVICE_POWER_STATE_OFF = 2,

	PVR_DEVICE_POWER_STATE_FORCE_I32 = 0x7fffffff
};

static enum PVR_DEVICE_POWER_STATE MapDevicePowerState(enum PVR_POWER_STATE
								ePowerState)
{
	enum PVR_DEVICE_POWER_STATE eDevicePowerState;

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
			PVR_DPF(PVR_DBG_ERROR,
				 "MapDevicePowerState: Invalid state: %ld",
				 ePowerState);
			eDevicePowerState = PVR_DEVICE_POWER_STATE_FORCE_I32;
			PVR_ASSERT(eDevicePowerState !=
				   PVR_DEVICE_POWER_STATE_FORCE_I32);
		}
	}

	return eDevicePowerState;
}

static void SGXCommandComplete(struct PVRSRV_DEVICE_NODE *psDeviceNode)
{
	SGXScheduleProcessQueues(psDeviceNode);
}

static u32 DeinitDevInfo(struct PVRSRV_SGXDEV_INFO *psDevInfo)
{
	if (psDevInfo->psKernelCCBInfo != NULL)
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
			  sizeof(struct PVRSRV_SGX_CCB_INFO),
			  psDevInfo->psKernelCCBInfo, NULL);

	return PVRSRV_OK;
}

static enum PVRSRV_ERROR InitDevInfo(struct PVRSRV_PER_PROCESS_DATA *psPerProc,
				struct PVRSRV_DEVICE_NODE *psDeviceNode,
				struct SGX_BRIDGE_INIT_INFO *psInitInfo)
{
	struct PVRSRV_SGXDEV_INFO *psDevInfo =
	    (struct PVRSRV_SGXDEV_INFO *)psDeviceNode->pvDevice;
	enum PVRSRV_ERROR eError;

	struct PVRSRV_SGX_CCB_INFO *psKernelCCBInfo = NULL;

	PVR_UNREFERENCED_PARAMETER(psPerProc);
	psDevInfo->sScripts = psInitInfo->sScripts;

	psDevInfo->psKernelCCBMemInfo =
	    (struct PVRSRV_KERNEL_MEM_INFO *)psInitInfo->hKernelCCBMemInfo;
	psDevInfo->psKernelCCB =
	    (struct PVRSRV_SGX_KERNEL_CCB *)psDevInfo->psKernelCCBMemInfo->
	    pvLinAddrKM;

	psDevInfo->psKernelCCBCtlMemInfo =
	    (struct PVRSRV_KERNEL_MEM_INFO *)psInitInfo->hKernelCCBCtlMemInfo;
	psDevInfo->psKernelCCBCtl =
	    (struct PVRSRV_SGX_CCB_CTL *)psDevInfo->psKernelCCBCtlMemInfo->
	    pvLinAddrKM;

	psDevInfo->psKernelCCBEventKickerMemInfo =
	    (struct PVRSRV_KERNEL_MEM_INFO *)
	    psInitInfo->hKernelCCBEventKickerMemInfo;
	psDevInfo->pui32KernelCCBEventKicker =
	    (u32 *) psDevInfo->psKernelCCBEventKickerMemInfo->pvLinAddrKM;

	psDevInfo->psKernelSGXHostCtlMemInfo =
	  (struct PVRSRV_KERNEL_MEM_INFO *)psInitInfo->hKernelSGXHostCtlMemInfo;
	psDevInfo->psSGXHostCtl =
	    (struct PVRSRV_SGX_HOST_CTL *)psDevInfo->psKernelSGXHostCtlMemInfo->
	    pvLinAddrKM;

	psDevInfo->psKernelHWPerfCBMemInfo =
	    (struct PVRSRV_KERNEL_MEM_INFO *)psInitInfo->hKernelHWPerfCBMemInfo;

	eError = OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
			    sizeof(struct PVRSRV_SGX_CCB_INFO),
			    (void **)&psKernelCCBInfo, NULL);
	if (eError != PVRSRV_OK) {
		PVR_DPF(PVR_DBG_ERROR, "InitDevInfo: Failed to alloc memory");
		goto failed_allockernelccb;
	}

	OSMemSet(psKernelCCBInfo, 0, sizeof(struct PVRSRV_SGX_CCB_INFO));
	psKernelCCBInfo->psCCBMemInfo = psDevInfo->psKernelCCBMemInfo;
	psKernelCCBInfo->psCCBCtlMemInfo = psDevInfo->psKernelCCBCtlMemInfo;
	psKernelCCBInfo->psCommands = psDevInfo->psKernelCCB->asCommands;
	psKernelCCBInfo->pui32WriteOffset =
	    &psDevInfo->psKernelCCBCtl->ui32WriteOffset;
	psKernelCCBInfo->pui32ReadOffset =
	    &psDevInfo->psKernelCCBCtl->ui32ReadOffset;
	psDevInfo->psKernelCCBInfo = psKernelCCBInfo;

	psDevInfo->ui32TAKickAddress = psInitInfo->ui32TAKickAddress;

	psDevInfo->ui32VideoHandlerAddress =
	    psInitInfo->ui32VideoHandlerAddress;

	psDevInfo->bForcePTOff = IMG_FALSE;

	psDevInfo->ui32CacheControl = psInitInfo->ui32CacheControl;

	psDevInfo->ui32EDMTaskReg0 = psInitInfo->ui32EDMTaskReg0;
	psDevInfo->ui32EDMTaskReg1 = psInitInfo->ui32EDMTaskReg1;
	psDevInfo->ui32ClkGateCtl = psInitInfo->ui32ClkGateCtl;
	psDevInfo->ui32ClkGateCtl2 = psInitInfo->ui32ClkGateCtl2;
	psDevInfo->ui32ClkGateStatusMask = psInitInfo->ui32ClkGateStatusMask;

	OSMemCopy(&psDevInfo->asSGXDevData, &psInitInfo->asInitDevData,
		  sizeof(psDevInfo->asSGXDevData));

	return PVRSRV_OK;

failed_allockernelccb:
	DeinitDevInfo(psDevInfo);

	return eError;
}

static void SGXGetTimingInfo(struct PVRSRV_DEVICE_NODE *psDeviceNode)
{
	struct PVRSRV_SGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	struct SGX_TIMING_INFORMATION sSGXTimingInfo = { 0 };
	u32 ui32ActivePowManSampleRate;
	struct timer_work_data *data = psDevInfo->hTimer;
	enum PVRSRV_ERROR eError;

	SysGetSGXTimingInformation(&sSGXTimingInfo);

	if (data) {
		BUG_ON(data->armed);
		/*
		 * The magic calculation below sets the hardware lock-up
		 * detection and recovery timer interval to ~150msecs.
		 * The interval length will be scaled based on the SGX
		 * functional clock frequency. The higher the frequency
		 * the shorter the interval and vice versa.
		 */
		data->interval = 150 * SYS_SGX_PDS_TIMER_FREQ /
			sSGXTimingInfo.ui32uKernelFreq;
	}

	psDevInfo->psSGXHostCtl->ui32HWRecoverySampleRate =
		sSGXTimingInfo.ui32uKernelFreq /
		sSGXTimingInfo.ui32HWRecoveryFreq;

	psDevInfo->ui32CoreClockSpeed = sSGXTimingInfo.ui32CoreClockSpeed;
	psDevInfo->ui32uKernelTimerClock =
		sSGXTimingInfo.ui32CoreClockSpeed /
		sSGXTimingInfo.ui32uKernelFreq;

	ui32ActivePowManSampleRate =
		sSGXTimingInfo.ui32uKernelFreq *
		sSGXTimingInfo.ui32ActivePowManLatencyms / 1000;
	ui32ActivePowManSampleRate += 1;
	psDevInfo->psSGXHostCtl->ui32ActivePowManSampleRate =
	    ui32ActivePowManSampleRate;
}

static void SGXStartTimer(struct PVRSRV_SGXDEV_INFO *psDevInfo,
			      IMG_BOOL bStartOSTimer)
{
	u32 ui32RegVal;


	ui32RegVal =
	    EUR_CR_EVENT_TIMER_ENABLE_MASK | psDevInfo->ui32uKernelTimerClock;
	OSWriteHWReg((void __iomem *)psDevInfo->pvRegsBaseKM,
		     EUR_CR_EVENT_TIMER, ui32RegVal);
	PDUMPREGWITHFLAGS(EUR_CR_EVENT_TIMER, ui32RegVal,
			  PDUMP_FLAGS_CONTINUOUS);

	if (bStartOSTimer) {
		enum PVRSRV_ERROR eError;
		eError = SGXOSTimerEnable(psDevInfo->hTimer);
		if (eError != PVRSRV_OK)
			PVR_DPF(PVR_DBG_ERROR, "SGXStartTimer : "
					"Failed to enable host timer");
	}
}

static enum PVRSRV_ERROR SGXPrePowerState(void *hDevHandle,
			 enum PVR_DEVICE_POWER_STATE eNewPowerState,
			 enum PVR_DEVICE_POWER_STATE eCurrentPowerState)
{
	if ((eNewPowerState != eCurrentPowerState) &&
	    (eNewPowerState != PVR_DEVICE_POWER_STATE_ON)) {
		enum PVRSRV_ERROR eError;
		struct PVRSRV_DEVICE_NODE *psDeviceNode = hDevHandle;
		struct PVRSRV_SGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
		struct PVRSRV_SGX_HOST_CTL *psSGXHostCtl =
							psDevInfo->psSGXHostCtl;
		u32 ui32PowManRequest, ui32PowManComplete;

		eError = SGXOSTimerCancel(psDevInfo->hTimer);
		if (eError != PVRSRV_OK) {
			PVR_DPF(PVR_DBG_ERROR,
				 "SGXPrePowerState: Failed to disable timer");
			return eError;
		}

		if (eNewPowerState == PVR_DEVICE_POWER_STATE_OFF) {
			ui32PowManRequest =
			    PVRSRV_USSE_EDM_POWMAN_POWEROFF_REQUEST;
			ui32PowManComplete =
			    PVRSRV_USSE_EDM_POWMAN_POWEROFF_COMPLETE;
			PDUMPCOMMENT
			    ("TA/3D CCB Control - SGX power off request");
		} else {
			ui32PowManRequest = PVRSRV_USSE_EDM_POWMAN_IDLE_REQUEST;
			ui32PowManComplete =
			    PVRSRV_USSE_EDM_POWMAN_IDLE_COMPLETE;
			PDUMPCOMMENT("TA/3D CCB Control - SGX idle request");
		}

		psSGXHostCtl->ui32PowManFlags |= ui32PowManRequest;
#if defined(PDUMP)
		PDUMPMEM(NULL, psDevInfo->psKernelSGXHostCtlMemInfo,
			 offsetof(struct PVRSRV_SGX_HOST_CTL, ui32PowManFlags),
			 sizeof(u32), PDUMP_FLAGS_CONTINUOUS,
			 MAKEUNIQUETAG(psDevInfo->psKernelSGXHostCtlMemInfo));
#endif

		if (PollForValueKM(&psSGXHostCtl->ui32PowManFlags,
				   ui32PowManComplete,
				   ui32PowManComplete,
				   MAX_HW_TIME_US / WAIT_TRY_COUNT,
				   WAIT_TRY_COUNT) != PVRSRV_OK)
			PVR_DPF(PVR_DBG_ERROR, "SGXPrePowerState: "
			      "Wait for SGX ukernel power transition failed.");

#if defined(PDUMP)
		PDUMPCOMMENT
		    ("TA/3D CCB Control - Wait for power event on uKernel.");
		PDUMPMEMPOL(psDevInfo->psKernelSGXHostCtlMemInfo,
			    offsetof(struct PVRSRV_SGX_HOST_CTL,
				     ui32PowManFlags),
			    ui32PowManComplete, ui32PowManComplete,
			    PDUMP_POLL_OPERATOR_EQUAL, IMG_FALSE, IMG_FALSE,
			    MAKEUNIQUETAG(psDevInfo->
					  psKernelSGXHostCtlMemInfo));
#endif
		{
			if (PollForValueKM(
					(u32 __force *)psDevInfo->pvRegsBaseKM +
					(EUR_CR_CLKGATESTATUS >> 2), 0,
			     psDevInfo->ui32ClkGateStatusMask,
			     MAX_HW_TIME_US / WAIT_TRY_COUNT,
			     WAIT_TRY_COUNT) != PVRSRV_OK)
				PVR_DPF(PVR_DBG_ERROR, "SGXPrePowerState: "
					 "Wait for SGX clock gating failed.");

			PDUMPCOMMENT("Wait for SGX clock gating.");
			PDUMPREGPOL(EUR_CR_CLKGATESTATUS, 0,
				    psDevInfo->ui32ClkGateStatusMask);
		}

		if (eNewPowerState == PVR_DEVICE_POWER_STATE_OFF) {
			eError = SGXDeinitialise(psDevInfo);
			if (eError != PVRSRV_OK) {
				PVR_DPF(PVR_DBG_ERROR, "SGXPrePowerState: "
					"SGXDeinitialise failed: %lu", eError);
				return eError;
			}
		}
	}

	return PVRSRV_OK;
}

static enum PVRSRV_ERROR SGXPostPowerState(void *hDevHandle,
				 enum PVR_DEVICE_POWER_STATE eNewPowerState,
				 enum PVR_DEVICE_POWER_STATE eCurrentPowerState)
{
	if ((eNewPowerState != eCurrentPowerState) &&
	    (eCurrentPowerState != PVR_DEVICE_POWER_STATE_ON)) {
		enum PVRSRV_ERROR eError;
		struct PVRSRV_DEVICE_NODE *psDeviceNode = hDevHandle;
		struct PVRSRV_SGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
		struct PVRSRV_SGX_HOST_CTL *psSGXHostCtl =
							psDevInfo->psSGXHostCtl;

		psSGXHostCtl->ui32PowManFlags = 0;
		PDUMPCOMMENT("TA/3D CCB Control - Reset Power Manager flags");
#if defined(PDUMP)
		PDUMPMEM(NULL, psDevInfo->psKernelSGXHostCtlMemInfo,
			 offsetof(struct PVRSRV_SGX_HOST_CTL, ui32PowManFlags),
			 sizeof(u32), PDUMP_FLAGS_CONTINUOUS,
			 MAKEUNIQUETAG(psDevInfo->psKernelSGXHostCtlMemInfo));
#endif

		if (eCurrentPowerState == PVR_DEVICE_POWER_STATE_OFF) {

			SGXGetTimingInfo(psDeviceNode);

			eError = SGXInitialise(psDevInfo, IMG_FALSE);
			if (eError != PVRSRV_OK) {
				PVR_DPF(PVR_DBG_ERROR, "SGXPostPowerState: "
					"SGXInitialise failed");
				return eError;
			}
		}
	}

	return PVRSRV_OK;
}

static enum PVRSRV_ERROR SGXPrePowerStateExt(void *hDevHandle,
					enum PVR_POWER_STATE eNewPowerState,
					enum PVR_POWER_STATE eCurrentPowerState)
{
	enum PVR_DEVICE_POWER_STATE eNewDevicePowerState =
	    MapDevicePowerState(eNewPowerState);
	enum PVR_DEVICE_POWER_STATE eCurrentDevicePowerState =
	    MapDevicePowerState(eCurrentPowerState);

	return SGXPrePowerState(hDevHandle, eNewDevicePowerState,
				eCurrentDevicePowerState);
}

static enum PVRSRV_ERROR SGXPostPowerStateExt(void *hDevHandle,
				  enum PVR_POWER_STATE eNewPowerState,
				  enum PVR_POWER_STATE eCurrentPowerState)
{
	enum PVRSRV_ERROR eError;
	enum PVR_DEVICE_POWER_STATE eNewDevicePowerState =
	    MapDevicePowerState(eNewPowerState);
	enum PVR_DEVICE_POWER_STATE eCurrentDevicePowerState =
	    MapDevicePowerState(eCurrentPowerState);

	eError =
	    SGXPostPowerState(hDevHandle, eNewDevicePowerState,
			      eCurrentDevicePowerState);
	if (eError != PVRSRV_OK)
		return eError;

	PVR_DPF(PVR_DBG_WARNING,
		 "SGXPostPowerState : SGX Power Transition from %d to %d OK",
		 eCurrentPowerState, eNewPowerState);

	return eError;
}

static enum PVRSRV_ERROR SGXPreClockSpeedChange(void *hDevHandle,
					IMG_BOOL bIdleDevice,
					enum PVR_POWER_STATE eCurrentPowerState)
{
	enum PVRSRV_ERROR eError;
	struct PVRSRV_DEVICE_NODE *psDeviceNode = hDevHandle;
	struct PVRSRV_SGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;

	PVR_UNREFERENCED_PARAMETER(psDevInfo);

	if (eCurrentPowerState == PVRSRV_POWER_STATE_D0)
		if (bIdleDevice) {
			eError =
			    SGXPrePowerState(hDevHandle,
					     PVR_DEVICE_POWER_STATE_IDLE,
					     PVR_DEVICE_POWER_STATE_ON);

			if (eError != PVRSRV_OK)
				return eError;
		}

	PVR_DPF(PVR_DBG_MESSAGE,
		 "SGXPreClockSpeedChange: SGX clock speed was %luHz",
		 psDevInfo->ui32CoreClockSpeed);

	return PVRSRV_OK;
}

static enum PVRSRV_ERROR SGXPostClockSpeedChange(void *hDevHandle,
				    IMG_BOOL bIdleDevice,
				    enum PVR_POWER_STATE eCurrentPowerState)
{
	enum PVRSRV_ERROR eError = PVRSRV_OK;
	struct PVRSRV_DEVICE_NODE *psDeviceNode = hDevHandle;
	struct PVRSRV_SGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	u32 ui32OldClockSpeed = psDevInfo->ui32CoreClockSpeed;

	PVR_UNREFERENCED_PARAMETER(ui32OldClockSpeed);

	if (eCurrentPowerState == PVRSRV_POWER_STATE_D0) {
		SGXGetTimingInfo(psDeviceNode);
		if (bIdleDevice) {
			eError =
			    SGXPostPowerState(hDevHandle,
					      PVR_DEVICE_POWER_STATE_ON,
					      PVR_DEVICE_POWER_STATE_IDLE);

			if (eError != PVRSRV_OK)
				return eError;
		}
		SGXStartTimer(psDevInfo, IMG_TRUE);
	}

	PVR_DPF(PVR_DBG_MESSAGE, "SGXPostClockSpeedChange: "
		 "SGX clock speed changed from %luHz to %luHz",
		 ui32OldClockSpeed, psDevInfo->ui32CoreClockSpeed);

	return PVRSRV_OK;
}

static enum PVRSRV_ERROR SGXRunScript(struct PVRSRV_SGXDEV_INFO *psDevInfo,
				 union SGX_INIT_COMMAND *psScript,
				 u32 ui32NumInitCommands)
{
	u32 ui32PC;
	union SGX_INIT_COMMAND *psComm;

	for (ui32PC = 0, psComm = psScript;
	     ui32PC < ui32NumInitCommands; ui32PC++, psComm++) {
		switch (psComm->eOp) {
		case SGX_INIT_OP_WRITE_HW_REG:
			{
				OSWriteHWReg((void __iomem *)
							psDevInfo->pvRegsBaseKM,
					     psComm->sWriteHWReg.ui32Offset,
					     psComm->sWriteHWReg.ui32Value);
				PDUMPREG(psComm->sWriteHWReg.ui32Offset,
					 psComm->sWriteHWReg.ui32Value);
				break;
			}
#if defined(PDUMP)
		case SGX_INIT_OP_PDUMP_HW_REG:
			{
				PDUMPREG(psComm->sPDumpHWReg.ui32Offset,
					 psComm->sPDumpHWReg.ui32Value);
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
				PVR_DPF(PVR_DBG_ERROR,
				     "SGXRunScript: PC %d: Illegal command: %d",
				      ui32PC, psComm->eOp);
				return PVRSRV_ERROR_GENERIC;
			}
		}

	}

	return PVRSRV_ERROR_GENERIC;;
}

static enum PVRSRV_ERROR SGXInitialise(struct PVRSRV_SGXDEV_INFO *psDevInfo,
				  IMG_BOOL bHardwareRecovery)
{
	enum PVRSRV_ERROR eError;
	u32 ui32ReadOffset, ui32WriteOffset;

	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_CLKGATECTL,
		     psDevInfo->ui32ClkGateCtl);
	PDUMPREGWITHFLAGS(EUR_CR_CLKGATECTL, psDevInfo->ui32ClkGateCtl,
			  PDUMP_FLAGS_CONTINUOUS);

	SGXReset(psDevInfo, PDUMP_FLAGS_CONTINUOUS);



	*psDevInfo->pui32KernelCCBEventKicker = 0;
#if defined(PDUMP)
	PDUMPMEM(NULL, psDevInfo->psKernelCCBEventKickerMemInfo, 0,
		 sizeof(*psDevInfo->pui32KernelCCBEventKicker),
		 PDUMP_FLAGS_CONTINUOUS,
		 MAKEUNIQUETAG(psDevInfo->psKernelCCBEventKickerMemInfo));
#endif

	psDevInfo->psSGXHostCtl->sTAHWPBDesc.uiAddr = 0;
	psDevInfo->psSGXHostCtl->s3DHWPBDesc.uiAddr = 0;
#if defined(PDUMP)
	PDUMPCOMMENT(" CCB Control - Reset HW PBDesc records");
	PDUMPMEM(NULL, psDevInfo->psKernelSGXHostCtlMemInfo,
		 offsetof(struct PVRSRV_SGX_HOST_CTL, sTAHWPBDesc),
		 sizeof(struct IMG_DEV_VIRTADDR), PDUMP_FLAGS_CONTINUOUS,
		 MAKEUNIQUETAG(psDevInfo->psKernelSGXHostCtlMemInfo));
	PDUMPMEM(NULL, psDevInfo->psKernelSGXHostCtlMemInfo,
		 offsetof(struct PVRSRV_SGX_HOST_CTL, s3DHWPBDesc),
		 sizeof(struct IMG_DEV_VIRTADDR), PDUMP_FLAGS_CONTINUOUS,
		 MAKEUNIQUETAG(psDevInfo->psKernelSGXHostCtlMemInfo));
#endif

	eError =
	    SGXRunScript(psDevInfo, psDevInfo->sScripts.asInitCommands,
			 SGX_MAX_INIT_COMMANDS);
	if (eError != PVRSRV_OK) {
		PVR_DPF(PVR_DBG_ERROR,
			 "SGXInitialise: SGXRunScript failed (%d)", eError);
		return PVRSRV_ERROR_GENERIC;
	}

	SGXStartTimer(psDevInfo, !bHardwareRecovery);

	if (bHardwareRecovery) {
		struct PVRSRV_SGX_HOST_CTL *psSGXHostCtl =
		    (struct PVRSRV_SGX_HOST_CTL *)psDevInfo->psSGXHostCtl;

		if (PollForValueKM((volatile u32 *)
		     (&psSGXHostCtl->ui32InterruptClearFlags), 0,
		     PVRSRV_USSE_EDM_INTERRUPT_HWR,
		     MAX_HW_TIME_US / WAIT_TRY_COUNT, 1000) != PVRSRV_OK) {
			PVR_DPF(PVR_DBG_ERROR, "SGXInitialise: "
					"Wait for uKernel HW Recovery failed");
			return PVRSRV_ERROR_RETRY;
		}
	}

	for (ui32ReadOffset = psDevInfo->psKernelCCBCtl->ui32ReadOffset,
	     ui32WriteOffset = psDevInfo->psKernelCCBCtl->ui32WriteOffset;
	     ui32ReadOffset != ui32WriteOffset;
	     ui32ReadOffset = (ui32ReadOffset + 1) & 0xFF) {
		*psDevInfo->pui32KernelCCBEventKicker =
		    (*psDevInfo->pui32KernelCCBEventKicker + 1) & 0xFF;
		OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_EVENT_KICK,
			     EUR_CR_EVENT_KICK_NOW_MASK);
	}

	return PVRSRV_OK;
}

static enum PVRSRV_ERROR SGXDeinitialise(void *hDevCookie)
{
	struct PVRSRV_SGXDEV_INFO *psDevInfo = (struct PVRSRV_SGXDEV_INFO *)
								   hDevCookie;
	enum PVRSRV_ERROR eError;

	if (psDevInfo->pvRegsBaseKM == NULL)
		return PVRSRV_OK;

	eError = SGXRunScript(psDevInfo, psDevInfo->sScripts.asDeinitCommands,
			 SGX_MAX_DEINIT_COMMANDS);
	if (eError != PVRSRV_OK) {
		PVR_DPF(PVR_DBG_ERROR,
			 "SGXDeinitialise: SGXRunScript failed (%d)", eError);
		return PVRSRV_ERROR_GENERIC;
	}

	return PVRSRV_OK;
}

static enum PVRSRV_ERROR DevInitSGXPart1(void *pvDeviceNode)
{
	struct PVRSRV_SGXDEV_INFO *psDevInfo;
	void *hKernelDevMemContext;
	struct IMG_DEV_PHYADDR sPDDevPAddr;
	u32 i;
	struct PVRSRV_DEVICE_NODE *psDeviceNode = (struct PVRSRV_DEVICE_NODE *)
								   pvDeviceNode;
	struct DEVICE_MEMORY_HEAP_INFO *psDeviceMemoryHeap =
	    psDeviceNode->sDevMemoryInfo.psDeviceMemoryHeap;
	void *hDevInfoOSMemHandle = (void *) NULL;
	enum PVRSRV_ERROR eError;

	PDUMPCOMMENT("SGX Initialisation Part 1");

	PDUMPCOMMENT("SGX Core Version Information: %s",
		     SGX_CORE_FRIENDLY_NAME);
#ifdef SGX_CORE_REV
	PDUMPCOMMENT("SGX Core Revision Information: %d", SGX_CORE_REV);
#else
	PDUMPCOMMENT("SGX Core Revision Information: head rtl");
#endif

	if (OSAllocPages
	    (PVRSRV_OS_PAGEABLE_HEAP | PVRSRV_HAP_MULTI_PROCESS |
	     PVRSRV_HAP_CACHED, sizeof(struct PVRSRV_SGXDEV_INFO),
	     (void **) &psDevInfo, &hDevInfoOSMemHandle) != PVRSRV_OK) {
		PVR_DPF(PVR_DBG_ERROR, "DevInitSGXPart1 : "
			"Failed to alloc memory for DevInfo");
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
	OSMemSet(psDevInfo, 0, sizeof(struct PVRSRV_SGXDEV_INFO));

	psDevInfo->eDeviceType = DEV_DEVICE_TYPE;
	psDevInfo->eDeviceClass = DEV_DEVICE_CLASS;

	psDeviceNode->pvDevice = (void *) psDevInfo;
	psDeviceNode->hDeviceOSMemHandle = hDevInfoOSMemHandle;

	psDevInfo->pvDeviceMemoryHeap = (void *) psDeviceMemoryHeap;

	hKernelDevMemContext = BM_CreateContext(psDeviceNode, &sPDDevPAddr,
						NULL, NULL);

	psDevInfo->sKernelPDDevPAddr = sPDDevPAddr;

	for (i = 0; i < psDeviceNode->sDevMemoryInfo.ui32HeapCount; i++) {
		void *hDevMemHeap;

		switch (psDeviceMemoryHeap[i].DevMemHeapType) {
		case DEVICE_MEMORY_HEAP_KERNEL:
		case DEVICE_MEMORY_HEAP_SHARED:
		case DEVICE_MEMORY_HEAP_SHARED_EXPORTED:
			{
				hDevMemHeap =
				    BM_CreateHeap(hKernelDevMemContext,
						  &psDeviceMemoryHeap[i]);

				psDeviceMemoryHeap[i].hDevMemHeap = hDevMemHeap;
				break;
			}
		}
	}

	eError = MMU_BIFResetPDAlloc(psDevInfo);
	if (eError != PVRSRV_OK) {
		PVR_DPF(PVR_DBG_ERROR,
			 "DevInitSGX : Failed to alloc memory for BIF reset");
		return PVRSRV_ERROR_GENERIC;
	}

	return PVRSRV_OK;
}

enum PVRSRV_ERROR SGXGetInfoForSrvinitKM(void *hDevHandle,
				struct SGX_BRIDGE_INFO_FOR_SRVINIT *psInitInfo)
{
	struct PVRSRV_DEVICE_NODE *psDeviceNode;
	struct PVRSRV_SGXDEV_INFO *psDevInfo;
	enum PVRSRV_ERROR eError;

	PDUMPCOMMENT("SGXGetInfoForSrvinit");

	psDeviceNode = (struct PVRSRV_DEVICE_NODE *)hDevHandle;
	psDevInfo = (struct PVRSRV_SGXDEV_INFO *)psDeviceNode->pvDevice;

	psInitInfo->sPDDevPAddr = psDevInfo->sKernelPDDevPAddr;

	eError =
	    PVRSRVGetDeviceMemHeapsKM(hDevHandle, &psInitInfo->asHeapInfo[0]);
	if (eError != PVRSRV_OK) {
		PVR_DPF(PVR_DBG_ERROR, "SGXGetInfoForSrvinit: "
				"PVRSRVGetDeviceMemHeapsKM failed (%d)",
			 eError);
		return PVRSRV_ERROR_GENERIC;
	}

	return eError;
}

enum PVRSRV_ERROR DevInitSGXPart2KM(struct PVRSRV_PER_PROCESS_DATA *psPerProc,
				   void *hDevHandle,
				   struct SGX_BRIDGE_INIT_INFO *psInitInfo)
{
	struct PVRSRV_DEVICE_NODE *psDeviceNode;
	struct PVRSRV_SGXDEV_INFO *psDevInfo;
	enum PVRSRV_ERROR eError;
	struct SGX_DEVICE_MAP *psSGXDeviceMap;
	enum PVR_POWER_STATE eDefaultPowerState;

	PDUMPCOMMENT("SGX Initialisation Part 2");

	psDeviceNode = (struct PVRSRV_DEVICE_NODE *)hDevHandle;
	psDevInfo = (struct PVRSRV_SGXDEV_INFO *)psDeviceNode->pvDevice;

	eError = InitDevInfo(psPerProc, psDeviceNode, psInitInfo);
	if (eError != PVRSRV_OK) {
		PVR_DPF(PVR_DBG_ERROR,
			 "DevInitSGXPart2KM: Failed to load EDM program");
		goto failed_init_dev_info;
	}


	eError = SysGetDeviceMemoryMap(PVRSRV_DEVICE_TYPE_SGX,
				       (void **) &psSGXDeviceMap);
	if (eError != PVRSRV_OK) {
		PVR_DPF(PVR_DBG_ERROR, "DevInitSGXPart2KM: "
					"Failed to get device memory map!");
		return PVRSRV_ERROR_INIT_FAILURE;
	}

	if (psSGXDeviceMap->pvRegsCpuVBase) {
		psDevInfo->pvRegsBaseKM = psSGXDeviceMap->pvRegsCpuVBase;
	} else {

		psDevInfo->pvRegsBaseKM =
		    OSMapPhysToLin(psSGXDeviceMap->sRegsCpuPBase,
				   psSGXDeviceMap->ui32RegsSize,
				   PVRSRV_HAP_KERNEL_ONLY | PVRSRV_HAP_UNCACHED,
				   NULL);
		if (!psDevInfo->pvRegsBaseKM) {
			PVR_DPF(PVR_DBG_ERROR,
				 "DevInitSGXPart2KM: Failed to map in regs\n");
			return PVRSRV_ERROR_BAD_MAPPING;
		}
	}
	psDevInfo->ui32RegSize = psSGXDeviceMap->ui32RegsSize;
	psDevInfo->sRegsPhysBase = psSGXDeviceMap->sRegsSysPBase;



	psDeviceNode->pvISRData = psDeviceNode;

	PVR_ASSERT(psDeviceNode->pfnDeviceISR == SGX_ISRHandler);



	psDevInfo->psSGXHostCtl->ui32PowManFlags |=
	    PVRSRV_USSE_EDM_POWMAN_NO_WORK;
	eDefaultPowerState = PVRSRV_POWER_STATE_D3;
	eError = PVRSRVRegisterPowerDevice(psDeviceNode->sDevId.ui32DeviceIndex,
					   SGXPrePowerStateExt,
					   SGXPostPowerStateExt,
					   SGXPreClockSpeedChange,
					   SGXPostClockSpeedChange,
					   (void *) psDeviceNode,
					   PVRSRV_POWER_STATE_D3,
					   eDefaultPowerState);
	if (eError != PVRSRV_OK) {
		PVR_DPF(PVR_DBG_ERROR, "DevInitSGXPart2KM: "
			 "failed to register device with power manager");
		return eError;
	}

	OSMemSet(psDevInfo->psKernelCCB, 0,
		 sizeof(struct PVRSRV_SGX_KERNEL_CCB));
	OSMemSet(psDevInfo->psKernelCCBCtl, 0,
		 sizeof(struct PVRSRV_SGX_CCB_CTL));
	OSMemSet(psDevInfo->pui32KernelCCBEventKicker, 0,
		 sizeof(*psDevInfo->pui32KernelCCBEventKicker));
	PDUMPCOMMENT("Kernel CCB");
	PDUMPMEM(NULL, psDevInfo->psKernelCCBMemInfo, 0,
		 sizeof(struct PVRSRV_SGX_KERNEL_CCB), PDUMP_FLAGS_CONTINUOUS,
		 MAKEUNIQUETAG(psDevInfo->psKernelCCBMemInfo));
	PDUMPCOMMENT("Kernel CCB Control");
	PDUMPMEM(NULL, psDevInfo->psKernelCCBCtlMemInfo, 0,
		 sizeof(struct PVRSRV_SGX_CCB_CTL), PDUMP_FLAGS_CONTINUOUS,
		 MAKEUNIQUETAG(psDevInfo->psKernelCCBCtlMemInfo));
	PDUMPCOMMENT("Kernel CCB Event Kicker");
	PDUMPMEM(NULL, psDevInfo->psKernelCCBEventKickerMemInfo, 0,
		 sizeof(*psDevInfo->pui32KernelCCBEventKicker),
		 PDUMP_FLAGS_CONTINUOUS,
		 MAKEUNIQUETAG(psDevInfo->psKernelCCBEventKickerMemInfo));

	psDevInfo->hTimer = SGXOSTimerInit(psDeviceNode);
	if (!psDevInfo->hTimer)
		PVR_DPF(PVR_DBG_ERROR, "DevInitSGXPart2KM : "
			"Failed to initialize HW recovery timer");

	return PVRSRV_OK;

failed_init_dev_info:
	return eError;
}

static enum PVRSRV_ERROR DevDeInitSGX(void *pvDeviceNode)
{
	struct PVRSRV_DEVICE_NODE *psDeviceNode =
		(struct PVRSRV_DEVICE_NODE *)pvDeviceNode;
	struct PVRSRV_SGXDEV_INFO *psDevInfo =
		(struct PVRSRV_SGXDEV_INFO *)psDeviceNode->pvDevice;
	void *hDevInfoOSMemHandle = psDeviceNode->hDeviceOSMemHandle;
	enum PVRSRV_ERROR eError = PVRSRV_ERROR_INVALID_PARAMS;
	u32 ui32Heap;
	struct DEVICE_MEMORY_HEAP_INFO *psDeviceMemoryHeap;
	struct SGX_DEVICE_MAP *psSGXDeviceMap;

	if (!psDevInfo) {
		PVR_DPF(PVR_DBG_ERROR, "DevDeInitSGX: Null DevInfo");
		return PVRSRV_OK;
	}
	if (psDevInfo->hTimer) {
		SGXOSTimerCancel(psDevInfo->hTimer);
		SGXOSTimerDeInit(psDevInfo->hTimer);
		psDevInfo->hTimer = NULL;
	}

	MMU_BIFResetPDFree(psDevInfo);

	DeinitDevInfo(psDevInfo);


	psDeviceMemoryHeap =
	    (struct DEVICE_MEMORY_HEAP_INFO *)psDevInfo->pvDeviceMemoryHeap;
	for (ui32Heap = 0;
	     ui32Heap < psDeviceNode->sDevMemoryInfo.ui32HeapCount;
	     ui32Heap++) {
		switch (psDeviceMemoryHeap[ui32Heap].DevMemHeapType) {
		case DEVICE_MEMORY_HEAP_KERNEL:
		case DEVICE_MEMORY_HEAP_SHARED:
		case DEVICE_MEMORY_HEAP_SHARED_EXPORTED:
			{
				if (psDeviceMemoryHeap[ui32Heap].hDevMemHeap !=
				    NULL)
					BM_DestroyHeap(psDeviceMemoryHeap
						       [ui32Heap].hDevMemHeap);
				break;
			}
		}
	}

	if (!pvr_put_ctx(psDeviceNode->sDevMemoryInfo.pBMKernelContext))
		pr_err("%s: kernel context still in use, can't free it",
				__func__);


	eError = PVRSRVRemovePowerDevice(
				((struct PVRSRV_DEVICE_NODE *)pvDeviceNode)->
				  sDevId.ui32DeviceIndex);
	if (eError != PVRSRV_OK)
		return eError;

	eError = SysGetDeviceMemoryMap(PVRSRV_DEVICE_TYPE_SGX,
				       (void **) &psSGXDeviceMap);
	if (eError != PVRSRV_OK) {
		PVR_DPF(PVR_DBG_ERROR,
			 "DevDeInitSGX: Failed to get device memory map!");
		return eError;
	}

	if (!psSGXDeviceMap->pvRegsCpuVBase)
		if (psDevInfo->pvRegsBaseKM != NULL)
			OSUnMapPhysToLin(psDevInfo->pvRegsBaseKM,
					 psDevInfo->ui32RegSize,
					 PVRSRV_HAP_KERNEL_ONLY |
					 PVRSRV_HAP_UNCACHED, NULL);

	OSFreePages(PVRSRV_OS_PAGEABLE_HEAP | PVRSRV_HAP_MULTI_PROCESS,
		    sizeof(struct PVRSRV_SGXDEV_INFO), psDevInfo,
		    hDevInfoOSMemHandle);
	psDeviceNode->pvDevice = NULL;

	if (psDeviceMemoryHeap != NULL)
		OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
			  sizeof(struct DEVICE_MEMORY_HEAP_INFO) *
			  psDeviceNode->sDevMemoryInfo.ui32HeapCount,
			  psDeviceMemoryHeap, NULL);

	return PVRSRV_OK;
}

static
void HWRecoveryResetSGX(struct PVRSRV_DEVICE_NODE *psDeviceNode,
			    u32 ui32Component, u32 ui32CallerID)
{
	enum PVRSRV_ERROR eError;
	struct PVRSRV_SGXDEV_INFO *psDevInfo =
	    (struct PVRSRV_SGXDEV_INFO *)psDeviceNode->pvDevice;
	struct PVRSRV_SGX_HOST_CTL *psSGXHostCtl =
	    (struct PVRSRV_SGX_HOST_CTL *)psDevInfo->psSGXHostCtl;

	PVR_UNREFERENCED_PARAMETER(ui32Component);

	/* SGXOSTimer already has the lock as it needs to read SGX registers */
	if (ui32CallerID != TIMER_ID) {
		eError = PVRSRVPowerLock(ui32CallerID, IMG_FALSE);
		if (eError != PVRSRV_OK) {
			PVR_DPF(PVR_DBG_WARNING, "HWRecoveryResetSGX: "
				"Power transition in progress");
			return;
		}
	}

	psSGXHostCtl->ui32InterruptClearFlags |= PVRSRV_USSE_EDM_INTERRUPT_HWR;

	pr_err("HWRecoveryResetSGX: SGX Hardware Recovery triggered\n");

	PDUMPSUSPEND();

	do {
		eError = SGXInitialise(psDevInfo, IMG_TRUE);
	} while (eError == PVRSRV_ERROR_RETRY);
	if (eError != PVRSRV_OK)
		PVR_DPF(PVR_DBG_ERROR,
			 "HWRecoveryResetSGX: SGXInitialise failed (%d)",
			 eError);

	PDUMPRESUME();

	PVRSRVPowerUnlock(ui32CallerID);

	SGXScheduleProcessQueues(psDeviceNode);

	PVRSRVProcessQueues(ui32CallerID, IMG_TRUE);
}

static void SGXOSTimer(struct work_struct *work)
{
	struct timer_work_data *data = container_of(work,
						    struct timer_work_data,
						    work.work);
	struct PVRSRV_DEVICE_NODE *psDeviceNode = data->psDeviceNode;
	struct PVRSRV_SGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	static u32 ui32EDMTasks;
	static u32 ui32LockupCounter;
	static u32 ui32NumResets;
	u32 ui32CurrentEDMTasks;
	IMG_BOOL bLockup = IMG_FALSE;
	IMG_BOOL bPoweredDown;
	enum PVRSRV_ERROR eError;

	psDevInfo->ui32TimeStamp++;

	eError = PVRSRVPowerLock(TIMER_ID, IMG_FALSE);
	if (eError != PVRSRV_OK) {
		/*
		 * If a power transition is in progress then we're not really
		 * sure what the state of world is going to be after, so we
		 * just "pause" HW recovery and hopefully next time around we
		 * get the lock and can decide what to do
		 */
		goto rearm;
	}

	bPoweredDown = (IMG_BOOL) !SGXIsDevicePowered(psDeviceNode);

	if (bPoweredDown) {
		ui32LockupCounter = 0;
	} else {

		ui32CurrentEDMTasks =
		    OSReadHWReg(psDevInfo->pvRegsBaseKM,
				psDevInfo->ui32EDMTaskReg0);
		if (psDevInfo->ui32EDMTaskReg1 != 0)
			ui32CurrentEDMTasks ^=
			    OSReadHWReg(psDevInfo->pvRegsBaseKM,
					psDevInfo->ui32EDMTaskReg1);
		if ((ui32CurrentEDMTasks == ui32EDMTasks) &&
		    (psDevInfo->ui32NumResets == ui32NumResets)) {
			ui32LockupCounter++;
			if (ui32LockupCounter == 3) {
				ui32LockupCounter = 0;
				PVR_DPF(PVR_DBG_ERROR,
				"SGXOSTimer() detected SGX lockup (0x%x tasks)",
				 ui32EDMTasks);

				bLockup = IMG_TRUE;
			}
		} else {
			ui32LockupCounter = 0;
			ui32EDMTasks = ui32CurrentEDMTasks;
			ui32NumResets = psDevInfo->ui32NumResets;
		}
	}

	if (bLockup) {
		struct PVRSRV_SGX_HOST_CTL *psSGXHostCtl =
		    (struct PVRSRV_SGX_HOST_CTL *)psDevInfo->psSGXHostCtl;

		psSGXHostCtl->ui32HostDetectedLockups++;

		/* Note: This will release the lock when done */
		HWRecoveryResetSGX(psDeviceNode, 0, TIMER_ID);
	} else
		PVRSRVPowerUnlock(TIMER_ID);

 rearm:
	queue_delayed_work(data->work_queue, &data->work,
			   msecs_to_jiffies(data->interval));
}

static struct timer_work_data *
SGXOSTimerInit(struct PVRSRV_DEVICE_NODE *psDeviceNode)
{
	struct timer_work_data *data;

	data = kzalloc(sizeof(struct timer_work_data), GFP_KERNEL);
	if (!data)
		return NULL;

	data->work_queue = create_workqueue("SGXOSTimer");
	if (!data->work_queue) {
		kfree(data);
		return NULL;
	}

	data->interval = 150;
	data->psDeviceNode = psDeviceNode;
	INIT_DELAYED_WORK(&data->work, SGXOSTimer);

	return data;
}

static void SGXOSTimerDeInit(struct timer_work_data *data)
{
	destroy_workqueue(data->work_queue);
	kfree(data);
}

static enum PVRSRV_ERROR SGXOSTimerEnable(struct timer_work_data *data)
{
	if (!data)
		return PVRSRV_ERROR_GENERIC;

	if (queue_delayed_work(data->work_queue, &data->work,
			       msecs_to_jiffies(data->interval))) {
		data->armed = true;
		return PVRSRV_OK;
	}

	return PVRSRV_ERROR_GENERIC;
}

static enum PVRSRV_ERROR SGXOSTimerCancel(struct timer_work_data *data)
{
	if (!data)
		return PVRSRV_ERROR_GENERIC;

	cancel_delayed_work_sync(&data->work);
	data->armed = false;

	return PVRSRV_OK;
}

static IMG_BOOL SGX_ISRHandler(void *pvData)
{
	IMG_BOOL bInterruptProcessed = IMG_FALSE;

	{
		u32 ui32EventStatus, ui32EventEnable;
		u32 ui32EventClear = 0;
		struct PVRSRV_DEVICE_NODE *psDeviceNode;
		struct PVRSRV_SGXDEV_INFO *psDevInfo;

		if (pvData == NULL) {
			PVR_DPF(PVR_DBG_ERROR,
				 "SGX_ISRHandler: Invalid params\n");
			return bInterruptProcessed;
		}

		psDeviceNode = (struct PVRSRV_DEVICE_NODE *)pvData;
		psDevInfo = (struct PVRSRV_SGXDEV_INFO *)psDeviceNode->pvDevice;

		ui32EventStatus =
		    OSReadHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_EVENT_STATUS);
		ui32EventEnable =
		    OSReadHWReg(psDevInfo->pvRegsBaseKM,
				EUR_CR_EVENT_HOST_ENABLE);

		gui32EventStatusServicesByISR = ui32EventStatus;

		ui32EventStatus &= ui32EventEnable;

		if (ui32EventStatus & EUR_CR_EVENT_STATUS_SW_EVENT_MASK)
			ui32EventClear |= EUR_CR_EVENT_HOST_CLEAR_SW_EVENT_MASK;

		if (ui32EventClear) {
			bInterruptProcessed = IMG_TRUE;

			ui32EventClear |=
			    EUR_CR_EVENT_HOST_CLEAR_MASTER_INTERRUPT_MASK;

			OSWriteHWReg(psDevInfo->pvRegsBaseKM,
				     EUR_CR_EVENT_HOST_CLEAR, ui32EventClear);
		}
	}

	return bInterruptProcessed;
}

static void SGX_MISRHandler(void *pvData)
{
	struct PVRSRV_DEVICE_NODE *psDeviceNode =
		(struct PVRSRV_DEVICE_NODE *)pvData;
	struct PVRSRV_SGXDEV_INFO *psDevInfo =
	    (struct PVRSRV_SGXDEV_INFO *)psDeviceNode->pvDevice;
	struct PVRSRV_SGX_HOST_CTL *psSGXHostCtl =
	    (struct PVRSRV_SGX_HOST_CTL *)psDevInfo->psSGXHostCtl;

	if ((psSGXHostCtl->ui32InterruptFlags & PVRSRV_USSE_EDM_INTERRUPT_HWR)
	    && !(psSGXHostCtl->
		 ui32InterruptClearFlags & PVRSRV_USSE_EDM_INTERRUPT_HWR))
		HWRecoveryResetSGX(psDeviceNode, 0, ISR_ID);
	SGXTestActivePowerEvent(psDeviceNode, ISR_ID);
}

enum PVRSRV_ERROR SGXRegisterDevice(struct PVRSRV_DEVICE_NODE *psDeviceNode)
{
	struct DEVICE_MEMORY_INFO *psDevMemoryInfo;
	struct DEVICE_MEMORY_HEAP_INFO *psDeviceMemoryHeap;

	psDeviceNode->sDevId.eDeviceType = DEV_DEVICE_TYPE;
	psDeviceNode->sDevId.eDeviceClass = DEV_DEVICE_CLASS;

	psDeviceNode->pfnInitDevice = DevInitSGXPart1;
	psDeviceNode->pfnDeInitDevice = DevDeInitSGX;

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

	psDeviceNode->pfnDeviceISR = SGX_ISRHandler;
	psDeviceNode->pfnDeviceMISR = SGX_MISRHandler;

	psDeviceNode->pfnDeviceCommandComplete = SGXCommandComplete;

	psDevMemoryInfo = &psDeviceNode->sDevMemoryInfo;
	psDevMemoryInfo->ui32AddressSpaceSizeLog2 = SGX_ADDRESS_SPACE_SIZE;
	psDevMemoryInfo->ui32Flags = 0;
	psDevMemoryInfo->ui32HeapCount = SGX_MAX_HEAP_ID;
	psDevMemoryInfo->ui32SyncHeapID = SGX_SYNCINFO_HEAP_ID;
	psDevMemoryInfo->ui32MappingHeapID = SGX_GENERAL_MAPPING_HEAP_ID;

	if (OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
		       sizeof(struct DEVICE_MEMORY_HEAP_INFO) *
		       psDevMemoryInfo->ui32HeapCount,
		       (void **) &psDevMemoryInfo->psDeviceMemoryHeap,
		       NULL) != PVRSRV_OK) {
		PVR_DPF(PVR_DBG_ERROR,
			 "SGXRegisterDevice : alloc failed for heap info");
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
	OSMemSet(psDevMemoryInfo->psDeviceMemoryHeap, 0,
		 sizeof(struct DEVICE_MEMORY_HEAP_INFO) *
		 psDevMemoryInfo->ui32HeapCount);

	psDeviceMemoryHeap = psDevMemoryInfo->psDeviceMemoryHeap;

	psDeviceMemoryHeap[SGX_GENERAL_HEAP_ID].ui32HeapID =
	    HEAP_ID(PVRSRV_DEVICE_TYPE_SGX, SGX_GENERAL_HEAP_ID);
	psDeviceMemoryHeap[SGX_GENERAL_HEAP_ID].sDevVAddrBase.uiAddr =
	    SGX_GENERAL_HEAP_BASE;
	psDeviceMemoryHeap[SGX_GENERAL_HEAP_ID].ui32HeapSize =
	    SGX_GENERAL_HEAP_SIZE;
	psDeviceMemoryHeap[SGX_GENERAL_HEAP_ID].ui32Attribs =
	    PVRSRV_HAP_WRITECOMBINE | PVRSRV_MEM_RAM_BACKED_ALLOCATION |
	    PVRSRV_HAP_SINGLE_PROCESS;
	psDeviceMemoryHeap[SGX_GENERAL_HEAP_ID].pszName = "General";
	psDeviceMemoryHeap[SGX_GENERAL_HEAP_ID].pszBSName = "General BS";
	psDeviceMemoryHeap[SGX_GENERAL_HEAP_ID].DevMemHeapType =
	    DEVICE_MEMORY_HEAP_PERCONTEXT;

	psDeviceMemoryHeap[SGX_TADATA_HEAP_ID].ui32HeapID =
	    HEAP_ID(PVRSRV_DEVICE_TYPE_SGX, SGX_TADATA_HEAP_ID);
	psDeviceMemoryHeap[SGX_TADATA_HEAP_ID].sDevVAddrBase.uiAddr =
	    SGX_TADATA_HEAP_BASE;
	psDeviceMemoryHeap[SGX_TADATA_HEAP_ID].ui32HeapSize =
	    SGX_TADATA_HEAP_SIZE;
	psDeviceMemoryHeap[SGX_TADATA_HEAP_ID].ui32Attribs =
	    PVRSRV_HAP_WRITECOMBINE | PVRSRV_MEM_RAM_BACKED_ALLOCATION
	    | PVRSRV_HAP_MULTI_PROCESS;
	psDeviceMemoryHeap[SGX_TADATA_HEAP_ID].pszName = "TA Data";
	psDeviceMemoryHeap[SGX_TADATA_HEAP_ID].pszBSName = "TA Data BS";
	psDeviceMemoryHeap[SGX_TADATA_HEAP_ID].DevMemHeapType =
	    DEVICE_MEMORY_HEAP_PERCONTEXT;

	psDeviceMemoryHeap[SGX_KERNEL_CODE_HEAP_ID].ui32HeapID =
	    HEAP_ID(PVRSRV_DEVICE_TYPE_SGX, SGX_KERNEL_CODE_HEAP_ID);
	psDeviceMemoryHeap[SGX_KERNEL_CODE_HEAP_ID].sDevVAddrBase.uiAddr =
	    SGX_KERNEL_CODE_HEAP_BASE;
	psDeviceMemoryHeap[SGX_KERNEL_CODE_HEAP_ID].ui32HeapSize =
	    SGX_KERNEL_CODE_HEAP_SIZE;
	psDeviceMemoryHeap[SGX_KERNEL_CODE_HEAP_ID].ui32Attribs =
	    PVRSRV_HAP_WRITECOMBINE | PVRSRV_MEM_RAM_BACKED_ALLOCATION
	    | PVRSRV_HAP_MULTI_PROCESS;
	psDeviceMemoryHeap[SGX_KERNEL_CODE_HEAP_ID].pszName = "Kernel";
	psDeviceMemoryHeap[SGX_KERNEL_CODE_HEAP_ID].pszBSName = "Kernel BS";
	psDeviceMemoryHeap[SGX_KERNEL_CODE_HEAP_ID].DevMemHeapType =
	    DEVICE_MEMORY_HEAP_SHARED_EXPORTED;

	psDeviceMemoryHeap[SGX_VIDEO_CODE_HEAP_ID].ui32HeapID =
	    HEAP_ID(PVRSRV_DEVICE_TYPE_SGX, SGX_VIDEO_CODE_HEAP_ID);
	psDeviceMemoryHeap[SGX_VIDEO_CODE_HEAP_ID].sDevVAddrBase.uiAddr =
	    SGX_VIDEO_CODE_HEAP_BASE;
	psDeviceMemoryHeap[SGX_VIDEO_CODE_HEAP_ID].ui32HeapSize =
	    SGX_VIDEO_CODE_HEAP_SIZE;
	psDeviceMemoryHeap[SGX_VIDEO_CODE_HEAP_ID].ui32Attribs =
	    PVRSRV_HAP_WRITECOMBINE | PVRSRV_MEM_RAM_BACKED_ALLOCATION |
	    PVRSRV_HAP_KERNEL_ONLY;
	psDeviceMemoryHeap[SGX_VIDEO_CODE_HEAP_ID].pszName = "Video";
	psDeviceMemoryHeap[SGX_VIDEO_CODE_HEAP_ID].pszBSName = "Video BS";
	psDeviceMemoryHeap[SGX_VIDEO_CODE_HEAP_ID].DevMemHeapType =
	    DEVICE_MEMORY_HEAP_SHARED;

	psDeviceMemoryHeap[SGX_KERNEL_VIDEO_DATA_HEAP_ID].ui32HeapID =
	    HEAP_ID(PVRSRV_DEVICE_TYPE_SGX, SGX_KERNEL_VIDEO_DATA_HEAP_ID);
	psDeviceMemoryHeap[SGX_KERNEL_VIDEO_DATA_HEAP_ID].sDevVAddrBase.uiAddr =
	    SGX_KERNEL_VIDEO_DATA_HEAP_BASE;
	psDeviceMemoryHeap[SGX_KERNEL_VIDEO_DATA_HEAP_ID].ui32HeapSize =
	    SGX_KERNEL_VIDEO_DATA_HEAP_SIZE;
	psDeviceMemoryHeap[SGX_KERNEL_VIDEO_DATA_HEAP_ID].ui32Attribs =
	    PVRSRV_HAP_WRITECOMBINE | PVRSRV_MEM_RAM_BACKED_ALLOCATION |
	    PVRSRV_HAP_MULTI_PROCESS;
	psDeviceMemoryHeap[SGX_KERNEL_VIDEO_DATA_HEAP_ID].pszName =
	    "KernelVideoData";
	psDeviceMemoryHeap[SGX_KERNEL_VIDEO_DATA_HEAP_ID].pszBSName =
	    "KernelVideoData BS";
	psDeviceMemoryHeap[SGX_KERNEL_VIDEO_DATA_HEAP_ID].DevMemHeapType =
	    DEVICE_MEMORY_HEAP_SHARED_EXPORTED;

	psDeviceMemoryHeap[SGX_PIXELSHADER_HEAP_ID].ui32HeapID =
	    HEAP_ID(PVRSRV_DEVICE_TYPE_SGX, SGX_PIXELSHADER_HEAP_ID);
	psDeviceMemoryHeap[SGX_PIXELSHADER_HEAP_ID].sDevVAddrBase.uiAddr =
	    SGX_PIXELSHADER_HEAP_BASE;
	psDeviceMemoryHeap[SGX_PIXELSHADER_HEAP_ID].ui32HeapSize =
	    SGX_PIXELSHADER_HEAP_SIZE;
	psDeviceMemoryHeap[SGX_PIXELSHADER_HEAP_ID].ui32Attribs =
	    PVRSRV_HAP_WRITECOMBINE | PVRSRV_MEM_RAM_BACKED_ALLOCATION |
	    PVRSRV_HAP_SINGLE_PROCESS;
	psDeviceMemoryHeap[SGX_PIXELSHADER_HEAP_ID].pszName = "PixelShaderUSSE";
	psDeviceMemoryHeap[SGX_PIXELSHADER_HEAP_ID].pszBSName =
	    "PixelShaderUSSE BS";
	psDeviceMemoryHeap[SGX_PIXELSHADER_HEAP_ID].DevMemHeapType =
	    DEVICE_MEMORY_HEAP_PERCONTEXT;

	psDeviceMemoryHeap[SGX_VERTEXSHADER_HEAP_ID].ui32HeapID =
	    HEAP_ID(PVRSRV_DEVICE_TYPE_SGX, SGX_VERTEXSHADER_HEAP_ID);
	psDeviceMemoryHeap[SGX_VERTEXSHADER_HEAP_ID].sDevVAddrBase.uiAddr =
	    SGX_VERTEXSHADER_HEAP_BASE;
	psDeviceMemoryHeap[SGX_VERTEXSHADER_HEAP_ID].ui32HeapSize =
	    SGX_VERTEXSHADER_HEAP_SIZE;
	psDeviceMemoryHeap[SGX_VERTEXSHADER_HEAP_ID].ui32Attribs =
	    PVRSRV_HAP_WRITECOMBINE | PVRSRV_MEM_RAM_BACKED_ALLOCATION |
	    PVRSRV_HAP_SINGLE_PROCESS;
	psDeviceMemoryHeap[SGX_VERTEXSHADER_HEAP_ID].pszName =
	    "VertexShaderUSSE";
	psDeviceMemoryHeap[SGX_VERTEXSHADER_HEAP_ID].pszBSName =
	    "VertexShaderUSSE BS";
	psDeviceMemoryHeap[SGX_VERTEXSHADER_HEAP_ID].DevMemHeapType =
	    DEVICE_MEMORY_HEAP_PERCONTEXT;

	psDeviceMemoryHeap[SGX_PDSPIXEL_CODEDATA_HEAP_ID].ui32HeapID =
	    HEAP_ID(PVRSRV_DEVICE_TYPE_SGX, SGX_PDSPIXEL_CODEDATA_HEAP_ID);
	psDeviceMemoryHeap[SGX_PDSPIXEL_CODEDATA_HEAP_ID].sDevVAddrBase.uiAddr =
	    SGX_PDSPIXEL_CODEDATA_HEAP_BASE;
	psDeviceMemoryHeap[SGX_PDSPIXEL_CODEDATA_HEAP_ID].ui32HeapSize =
	    SGX_PDSPIXEL_CODEDATA_HEAP_SIZE;
	psDeviceMemoryHeap[SGX_PDSPIXEL_CODEDATA_HEAP_ID].ui32Attribs =
	    PVRSRV_HAP_WRITECOMBINE | PVRSRV_MEM_RAM_BACKED_ALLOCATION |
	    PVRSRV_HAP_SINGLE_PROCESS;
	psDeviceMemoryHeap[SGX_PDSPIXEL_CODEDATA_HEAP_ID].pszName =
	    "PDSPixelCodeData";
	psDeviceMemoryHeap[SGX_PDSPIXEL_CODEDATA_HEAP_ID].pszBSName =
	    "PDSPixelCodeData BS";
	psDeviceMemoryHeap[SGX_PDSPIXEL_CODEDATA_HEAP_ID].DevMemHeapType =
	    DEVICE_MEMORY_HEAP_PERCONTEXT;

	psDeviceMemoryHeap[SGX_PDSVERTEX_CODEDATA_HEAP_ID].ui32HeapID =
	    HEAP_ID(PVRSRV_DEVICE_TYPE_SGX, SGX_PDSVERTEX_CODEDATA_HEAP_ID);
	psDeviceMemoryHeap[SGX_PDSVERTEX_CODEDATA_HEAP_ID].sDevVAddrBase.
	    uiAddr = SGX_PDSVERTEX_CODEDATA_HEAP_BASE;
	psDeviceMemoryHeap[SGX_PDSVERTEX_CODEDATA_HEAP_ID].ui32HeapSize =
	    SGX_PDSVERTEX_CODEDATA_HEAP_SIZE;
	psDeviceMemoryHeap[SGX_PDSVERTEX_CODEDATA_HEAP_ID].ui32Attribs =
	    PVRSRV_HAP_WRITECOMBINE | PVRSRV_MEM_RAM_BACKED_ALLOCATION |
	    PVRSRV_HAP_SINGLE_PROCESS;
	psDeviceMemoryHeap[SGX_PDSVERTEX_CODEDATA_HEAP_ID].pszName =
	    "PDSVertexCodeData";
	psDeviceMemoryHeap[SGX_PDSVERTEX_CODEDATA_HEAP_ID].pszBSName =
	    "PDSVertexCodeData BS";
	psDeviceMemoryHeap[SGX_PDSVERTEX_CODEDATA_HEAP_ID].DevMemHeapType =
	    DEVICE_MEMORY_HEAP_PERCONTEXT;

	psDeviceMemoryHeap[SGX_SYNCINFO_HEAP_ID].ui32HeapID =
	    HEAP_ID(PVRSRV_DEVICE_TYPE_SGX, SGX_SYNCINFO_HEAP_ID);
	psDeviceMemoryHeap[SGX_SYNCINFO_HEAP_ID].sDevVAddrBase.uiAddr =
	    SGX_SYNCINFO_HEAP_BASE;
	psDeviceMemoryHeap[SGX_SYNCINFO_HEAP_ID].ui32HeapSize =
	    SGX_SYNCINFO_HEAP_SIZE;

	psDeviceMemoryHeap[SGX_SYNCINFO_HEAP_ID].ui32Attribs =
	    PVRSRV_HAP_WRITECOMBINE | PVRSRV_MEM_RAM_BACKED_ALLOCATION |
	    PVRSRV_HAP_MULTI_PROCESS;
	psDeviceMemoryHeap[SGX_SYNCINFO_HEAP_ID].pszName = "CacheCoherent";
	psDeviceMemoryHeap[SGX_SYNCINFO_HEAP_ID].pszBSName = "CacheCoherent BS";

	psDeviceMemoryHeap[SGX_SYNCINFO_HEAP_ID].DevMemHeapType =
	    DEVICE_MEMORY_HEAP_SHARED_EXPORTED;

	psDeviceMemoryHeap[SGX_3DPARAMETERS_HEAP_ID].ui32HeapID =
	    HEAP_ID(PVRSRV_DEVICE_TYPE_SGX, SGX_3DPARAMETERS_HEAP_ID);
	psDeviceMemoryHeap[SGX_3DPARAMETERS_HEAP_ID].sDevVAddrBase.uiAddr =
	    SGX_3DPARAMETERS_HEAP_BASE;
	psDeviceMemoryHeap[SGX_3DPARAMETERS_HEAP_ID].ui32HeapSize =
	    SGX_3DPARAMETERS_HEAP_SIZE;
	psDeviceMemoryHeap[SGX_3DPARAMETERS_HEAP_ID].pszName = "3DParameters";
	psDeviceMemoryHeap[SGX_3DPARAMETERS_HEAP_ID].pszBSName =
	    "3DParameters BS";
	psDeviceMemoryHeap[SGX_3DPARAMETERS_HEAP_ID].ui32Attribs =
	    PVRSRV_HAP_WRITECOMBINE | PVRSRV_MEM_RAM_BACKED_ALLOCATION |
	    PVRSRV_HAP_SINGLE_PROCESS;
	psDeviceMemoryHeap[SGX_3DPARAMETERS_HEAP_ID].DevMemHeapType =
	    DEVICE_MEMORY_HEAP_PERCONTEXT;

	psDeviceMemoryHeap[SGX_GENERAL_MAPPING_HEAP_ID].ui32HeapID =
	    HEAP_ID(PVRSRV_DEVICE_TYPE_SGX, SGX_GENERAL_MAPPING_HEAP_ID);
	psDeviceMemoryHeap[SGX_GENERAL_MAPPING_HEAP_ID].sDevVAddrBase.uiAddr =
	    SGX_GENERAL_MAPPING_HEAP_BASE;
	psDeviceMemoryHeap[SGX_GENERAL_MAPPING_HEAP_ID].ui32HeapSize =
	    SGX_GENERAL_MAPPING_HEAP_SIZE;
	psDeviceMemoryHeap[SGX_GENERAL_MAPPING_HEAP_ID].ui32Attribs =
	    PVRSRV_HAP_WRITECOMBINE | PVRSRV_HAP_MULTI_PROCESS;
	psDeviceMemoryHeap[SGX_GENERAL_MAPPING_HEAP_ID].pszName =
	    "GeneralMapping";
	psDeviceMemoryHeap[SGX_GENERAL_MAPPING_HEAP_ID].pszBSName =
	    "GeneralMapping BS";

	psDeviceMemoryHeap[SGX_GENERAL_MAPPING_HEAP_ID].DevMemHeapType =
	    DEVICE_MEMORY_HEAP_SHARED_EXPORTED;

	psDeviceMemoryHeap[SGX_FB_MAPPING_HEAP_ID].ui32HeapID =
	    HEAP_ID(PVRSRV_DEVICE_TYPE_SGX, SGX_FB_MAPPING_HEAP_ID);
	psDeviceMemoryHeap[SGX_FB_MAPPING_HEAP_ID].sDevVAddrBase.uiAddr =
	    SGX_FB_MAPPING_HEAP_BASE;
	psDeviceMemoryHeap[SGX_FB_MAPPING_HEAP_ID].ui32HeapSize =
	    SGX_FB_MAPPING_HEAP_SIZE;
	psDeviceMemoryHeap[SGX_FB_MAPPING_HEAP_ID].ui32Attribs =
	    PVRSRV_HAP_WRITECOMBINE | PVRSRV_HAP_MULTI_PROCESS;
	psDeviceMemoryHeap[SGX_FB_MAPPING_HEAP_ID].pszName =
	    "FramebufferMapping";
	psDeviceMemoryHeap[SGX_FB_MAPPING_HEAP_ID].pszBSName =
	    "FramebufferMapping BS";

	psDeviceMemoryHeap[SGX_FB_MAPPING_HEAP_ID].DevMemHeapType =
	    DEVICE_MEMORY_HEAP_SHARED_EXPORTED;

	psDeviceMemoryHeap[SGX_ALT_MAPPING_HEAP_ID].ui32HeapID =
	    HEAP_ID(PVRSRV_DEVICE_TYPE_SGX, SGX_ALT_MAPPING_HEAP_ID);
	psDeviceMemoryHeap[SGX_ALT_MAPPING_HEAP_ID].sDevVAddrBase.uiAddr =
	    SGX_ALT_MAPPING_HEAP_BASE;
	psDeviceMemoryHeap[SGX_ALT_MAPPING_HEAP_ID].ui32HeapSize =
	    SGX_ALT_MAPPING_HEAP_SIZE;
	psDeviceMemoryHeap[SGX_ALT_MAPPING_HEAP_ID].ui32Attribs =
	    PVRSRV_HAP_WRITECOMBINE | PVRSRV_HAP_MULTI_PROCESS;
	psDeviceMemoryHeap[SGX_ALT_MAPPING_HEAP_ID].pszName = "AltMapping";
	psDeviceMemoryHeap[SGX_ALT_MAPPING_HEAP_ID].pszBSName = "AltMapping BS";

	psDeviceMemoryHeap[SGX_ALT_MAPPING_HEAP_ID].DevMemHeapType =
	    DEVICE_MEMORY_HEAP_SHARED_EXPORTED;

	return PVRSRV_OK;
}

enum PVRSRV_ERROR SGXGetClientInfoKM(void *hDevCookie,
				    struct PVR3DIF4_CLIENT_INFO *psClientInfo)
{
	struct PVRSRV_SGXDEV_INFO *psDevInfo =
	    (struct PVRSRV_SGXDEV_INFO *)
			((struct PVRSRV_DEVICE_NODE *)hDevCookie)->pvDevice;

	psDevInfo->ui32ClientRefCount++;
#ifdef PDUMP
	if (psDevInfo->ui32ClientRefCount == 1)
		psDevInfo->psKernelCCBInfo->ui32CCBDumpWOff = 0;
#endif
	psClientInfo->ui32ProcessID = OSGetCurrentProcessIDKM();

	OSMemCopy(&psClientInfo->asDevData, &psDevInfo->asSGXDevData,
		  sizeof(psClientInfo->asDevData));

	return PVRSRV_OK;
}

enum PVRSRV_ERROR SGXGetMiscInfoKM(struct PVRSRV_SGXDEV_INFO *psDevInfo,
				  struct SGX_MISC_INFO *psMiscInfo)
{
	switch (psMiscInfo->eRequest) {
	case SGX_MISC_INFO_REQUEST_CLOCKSPEED:
		{
			psMiscInfo->uData.ui32SGXClockSpeed =
			    psDevInfo->ui32CoreClockSpeed;
			return PVRSRV_OK;
		}
	case SGX_MISC_INFO_REQUEST_HWPERF_CB_ON:
		{
			psDevInfo->psSGXHostCtl->ui32HWPerfFlags |=
			    PVRSRV_SGX_HWPERF_ON;
			return PVRSRV_OK;
		}
	case SGX_MISC_INFO_REQUEST_HWPERF_CB_OFF:
		{
			psDevInfo->psSGXHostCtl->ui32HWPerfFlags &=
			    ~PVRSRV_SGX_HWPERF_ON;
			return PVRSRV_OK;
		}
	case SGX_MISC_INFO_REQUEST_HWPERF_RETRIEVE_CB:
		{
			struct SGX_MISC_INFO_HWPERF_RETRIEVE_CB *psRetrieve =
			    &psMiscInfo->uData.sRetrieveCB;
			struct PVRSRV_SGX_HWPERF_CB *psHWPerfCB =
			    (struct PVRSRV_SGX_HWPERF_CB *)psDevInfo->
			    psKernelHWPerfCBMemInfo->pvLinAddrKM;
			unsigned i = 0;

			for (;
			     psHWPerfCB->ui32Woff != psHWPerfCB->ui32Roff
			     && i < psRetrieve->ui32ArraySize; i++) {
				struct PVRSRV_SGX_HWPERF_CBDATA *psData =
				    &psHWPerfCB->psHWPerfCBData[psHWPerfCB->
								ui32Roff];
				OSMemCopy(&psRetrieve->psHWPerfData[i], psData,
				      sizeof(struct PVRSRV_SGX_HWPERF_CBDATA));
				psRetrieve->psHWPerfData[i].ui32ClockSpeed =
				    psDevInfo->ui32CoreClockSpeed;
				psRetrieve->psHWPerfData[i].ui32TimeMax =
				    psDevInfo->ui32uKernelTimerClock;
				psHWPerfCB->ui32Roff =
				    (psHWPerfCB->ui32Roff + 1) &
				    (PVRSRV_SGX_HWPERF_CBSIZE - 1);
			}
			psRetrieve->ui32DataCount = i;
			psRetrieve->ui32Time = OSClockus();
			return PVRSRV_OK;
		}
	default:
		{

			return PVRSRV_ERROR_INVALID_PARAMS;
		}
	}
}

enum PVRSRV_ERROR SGXReadDiffCountersKM(void *hDevHandle, u32 ui32Reg,
				   u32 *pui32Old, IMG_BOOL bNew, u32 ui32New,
				   u32 ui32NewReset, u32 ui32CountersReg,
				   u32 *pui32Time, IMG_BOOL *pbActive,
				   struct PVRSRV_SGXDEV_DIFF_INFO *psDiffs)
{
	enum PVRSRV_ERROR eError;
	struct SYS_DATA *psSysData;
	struct PVRSRV_POWER_DEV *psPowerDevice;
	IMG_BOOL bPowered = IMG_FALSE;
	struct PVRSRV_DEVICE_NODE *psDeviceNode = hDevHandle;
	struct PVRSRV_SGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;

	if (bNew)
		psDevInfo->ui32HWGroupRequested = ui32New;
	psDevInfo->ui32HWReset |= ui32NewReset;

	eError = PVRSRVPowerLock(KERNEL_ID, IMG_FALSE);
	if (eError != PVRSRV_OK)
		return eError;

	SysAcquireData(&psSysData);

	psPowerDevice = psSysData->psPowerDeviceList;
	while (psPowerDevice) {
		if (psPowerDevice->ui32DeviceIndex ==
		    psDeviceNode->sDevId.ui32DeviceIndex) {
			bPowered =
			    (IMG_BOOL) (psPowerDevice->eCurrentPowerState ==
					PVRSRV_POWER_STATE_D0);
			break;
		}

		psPowerDevice = psPowerDevice->psNext;
	}

	*pbActive = bPowered;

	{
		struct PVRSRV_SGXDEV_DIFF_INFO sNew,
					       *psPrev = &psDevInfo->sDiffInfo;
		u32 i;

		sNew.ui32Time[0] = OSClockus();
		*pui32Time = sNew.ui32Time[0];
		if (sNew.ui32Time[0] != psPrev->ui32Time[0] && bPowered) {

			*pui32Old =
			    OSReadHWReg(psDevInfo->pvRegsBaseKM, ui32Reg);

			for (i = 0; i < PVRSRV_SGX_DIFF_NUM_COUNTERS; ++i) {
				sNew.aui32Counters[i] =
				    OSReadHWReg(psDevInfo->pvRegsBaseKM,
						ui32CountersReg + (i * 4));
			}

			if (psDevInfo->ui32HWGroupRequested != *pui32Old) {

				if (psDevInfo->ui32HWReset != 0) {
					OSWriteHWReg(psDevInfo->pvRegsBaseKM,
						     ui32Reg,
						     psDevInfo->
						     ui32HWGroupRequested |
						     psDevInfo->ui32HWReset);
					psDevInfo->ui32HWReset = 0;
				}

				OSWriteHWReg(psDevInfo->pvRegsBaseKM, ui32Reg,
					     psDevInfo->ui32HWGroupRequested);
			}

			sNew.ui32Marker[0] = psDevInfo->ui32KickTACounter;
			sNew.ui32Marker[1] = psDevInfo->ui32KickTARenderCounter;

			sNew.ui32Time[1] =
			    psDevInfo->psSGXHostCtl->ui32TimeWraps;

			for (i = 0; i < PVRSRV_SGX_DIFF_NUM_COUNTERS; ++i) {
				psDiffs->aui32Counters[i] =
				    sNew.aui32Counters[i] -
				    psPrev->aui32Counters[i];
			}

			psDiffs->ui32Marker[0] =
			    sNew.ui32Marker[0] - psPrev->ui32Marker[0];
			psDiffs->ui32Marker[1] =
			    sNew.ui32Marker[1] - psPrev->ui32Marker[1];

			psDiffs->ui32Time[0] =
			    sNew.ui32Time[0] - psPrev->ui32Time[0];
			psDiffs->ui32Time[1] =
			    sNew.ui32Time[1] - psPrev->ui32Time[1];

			*psPrev = sNew;
		} else {
			for (i = 0; i < PVRSRV_SGX_DIFF_NUM_COUNTERS; ++i)
				psDiffs->aui32Counters[i] = 0;

			psDiffs->ui32Marker[0] = 0;
			psDiffs->ui32Marker[1] = 0;

			psDiffs->ui32Time[0] = 0;
			psDiffs->ui32Time[1] = 0;
		}
	}

	PVRSRVPowerUnlock(KERNEL_ID);

	SGXTestActivePowerEvent(psDeviceNode, KERNEL_ID);

	return eError;
}
