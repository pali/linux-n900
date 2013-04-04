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

#include "sgxdefs.h"
#include "services_headers.h"
#include "buffer_manager.h"
#include "sgxapi_km.h"
#include "sgxinfo.h"
#include "sgxinfokm.h"
#include "sysconfig.h"
#include "pdump_km.h"
#include "mmu.h"
#include "pvr_bridge_km.h"
#include "sgx_bridge_km.h"
#include "osfunc.h"
#include "pvr_debug.h"
#include "sgxutils.h"

#include <linux/tty.h>


static IMG_BOOL gbPowerUpPDumped = IMG_FALSE;

void SGXTestActivePowerEvent(struct PVRSRV_DEVICE_NODE *psDeviceNode,
				 u32 ui32CallerID)
{
	enum PVRSRV_ERROR eError = PVRSRV_OK;
	struct PVRSRV_SGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	struct PVRSRV_SGX_HOST_CTL *psSGXHostCtl = psDevInfo->psSGXHostCtl;

	if ((psSGXHostCtl->ui32InterruptFlags &
			PVRSRV_USSE_EDM_INTERRUPT_ACTIVE_POWER) &&
	    !(psSGXHostCtl->ui32PowManFlags &
			PVRSRV_USSE_EDM_POWMAN_POWEROFF_REQUEST)) {
		PDUMPSUSPEND();
		eError = PVRSRVSetDevicePowerStateKM(psDeviceNode->sDevId.
					ui32DeviceIndex, PVRSRV_POWER_STATE_D3,
					ui32CallerID, IMG_FALSE);
		if (eError == PVRSRV_OK) {
			psSGXHostCtl->ui32NumActivePowerEvents++;
			if ((*(volatile u32 *)(&psSGXHostCtl->ui32PowManFlags) &
			     PVRSRV_USSE_EDM_POWMAN_POWEROFF_RESTART_IMMEDIATE)
			     != 0) {
				if (ui32CallerID == ISR_ID)
					psDeviceNode->
					bReProcessDeviceCommandComplete =
								     IMG_TRUE;
				else
					SGXScheduleProcessQueues
						(psDeviceNode);
			}
		}
		if (eError == PVRSRV_ERROR_RETRY)
			eError = PVRSRV_OK;

		PDUMPRESUME();
	}

	if (eError != PVRSRV_OK)
		PVR_DPF(PVR_DBG_ERROR, "SGXTestActivePowerEvent error:%lu",
					eError);
}

static inline struct PVRSRV_SGX_COMMAND *SGXAcquireKernelCCBSlot(
					struct PVRSRV_SGX_CCB_INFO *psCCB)
{
	IMG_BOOL bStart = IMG_FALSE;
	u32 uiStart = 0;

	do {
		if (((*psCCB->pui32WriteOffset + 1) & 255) !=
		    *psCCB->pui32ReadOffset)
			return &psCCB->psCommands[*psCCB->pui32WriteOffset];

		if (bStart == IMG_FALSE) {
			bStart = IMG_TRUE;
			uiStart = OSClockus();
		}
		OSWaitus(MAX_HW_TIME_US / WAIT_TRY_COUNT);
	} while ((OSClockus() - uiStart) < MAX_HW_TIME_US);

	return NULL;
}

enum PVRSRV_ERROR SGXScheduleCCBCommandKM(
				     struct PVRSRV_DEVICE_NODE *psDeviceNode,
				     enum PVRSRV_SGX_COMMAND_TYPE eCommandType,
				     struct PVRSRV_SGX_COMMAND *psCommandData,
				     u32 ui32CallerID)
{
	struct PVRSRV_SGX_CCB_INFO *psKernelCCB;
	enum PVRSRV_ERROR eError = PVRSRV_OK;
	struct PVRSRV_SGXDEV_INFO *psDevInfo;
	struct PVRSRV_SGX_COMMAND *psSGXCommand;
#if defined(PDUMP)
	void *pvDumpCommand;
#endif

	psDevInfo = (struct PVRSRV_SGXDEV_INFO *)psDeviceNode->pvDevice;
	psKernelCCB = psDevInfo->psKernelCCBInfo;

	{
		if (ui32CallerID == ISR_ID || gbPowerUpPDumped)
			PDUMPSUSPEND();

		eError =
		    PVRSRVSetDevicePowerStateKM(psDeviceNode->sDevId.
						ui32DeviceIndex,
						PVRSRV_POWER_STATE_D0,
						ui32CallerID, IMG_TRUE);

		if (ui32CallerID == ISR_ID || gbPowerUpPDumped)
			PDUMPRESUME();
		else if (eError == PVRSRV_OK)
			gbPowerUpPDumped = IMG_TRUE;
	}

	if (eError == PVRSRV_OK) {
		psDeviceNode->bReProcessDeviceCommandComplete = IMG_FALSE;
	} else {
		if (eError == PVRSRV_ERROR_RETRY)
			if (ui32CallerID == ISR_ID) {

				psDeviceNode->bReProcessDeviceCommandComplete =
				    IMG_TRUE;
				eError = PVRSRV_OK;
			} else {

			}
		else
			PVR_DPF(PVR_DBG_ERROR,
			     "SGXScheduleCCBCommandKM failed to acquire lock - "
			     "ui32CallerID:%ld eError:%lu", ui32CallerID,
			     eError);

		return eError;
	}

	psSGXCommand = SGXAcquireKernelCCBSlot(psKernelCCB);

	if (!psSGXCommand) {
		eError = PVRSRV_ERROR_TIMEOUT;
		goto Exit;
	}

	psCommandData->ui32Data[2] = psDevInfo->ui32CacheControl;

#if defined(PDUMP)

	psDevInfo->sPDContext.ui32CacheControl |= psDevInfo->ui32CacheControl;
#endif

	psDevInfo->ui32CacheControl = 0;

	*psSGXCommand = *psCommandData;

	switch (eCommandType) {
	case PVRSRV_SGX_COMMAND_EDM_KICK:
		psSGXCommand->ui32ServiceAddress = psDevInfo->ui32TAKickAddress;
		break;
	case PVRSRV_SGX_COMMAND_VIDEO_KICK:
		psSGXCommand->ui32ServiceAddress =
		    psDevInfo->ui32VideoHandlerAddress;
		break;
	default:
		PVR_DPF(PVR_DBG_ERROR,
			 "SGXScheduleCCBCommandKM: Unknown command type: %d",
			 eCommandType);
		eError = PVRSRV_ERROR_GENERIC;
		goto Exit;
	}

#if defined(PDUMP)
	if (ui32CallerID != ISR_ID) {

		PDUMPCOMMENTWITHFLAGS(0,
				      "Poll for space in the Kernel CCB\r\n");
		PDUMPMEMPOL(psKernelCCB->psCCBCtlMemInfo,
			    offsetof(struct PVRSRV_SGX_CCB_CTL, ui32ReadOffset),
			    (psKernelCCB->ui32CCBDumpWOff + 1) & 0xff, 0xff,
			    PDUMP_POLL_OPERATOR_NOTEQUAL, IMG_FALSE, IMG_FALSE,
			    MAKEUNIQUETAG(psKernelCCB->psCCBCtlMemInfo));

		PDUMPCOMMENTWITHFLAGS(0, "Kernel CCB command\r\n");
		pvDumpCommand =
		    (void *) ((u8 *) psKernelCCB->psCCBMemInfo->
				  pvLinAddrKM +
				  (*psKernelCCB->pui32WriteOffset *
				   sizeof(struct PVRSRV_SGX_COMMAND)));

		PDUMPMEM(pvDumpCommand,
			 psKernelCCB->psCCBMemInfo,
			 psKernelCCB->ui32CCBDumpWOff *
			 sizeof(struct PVRSRV_SGX_COMMAND),
			 sizeof(struct PVRSRV_SGX_COMMAND),
			 0, MAKEUNIQUETAG(psKernelCCB->psCCBMemInfo));

		PDUMPMEM(&psDevInfo->sPDContext.ui32CacheControl,
			 psKernelCCB->psCCBMemInfo,
			 psKernelCCB->ui32CCBDumpWOff *
			 sizeof(struct PVRSRV_SGX_COMMAND) +
			 offsetof(struct PVRSRV_SGX_COMMAND, ui32Data[2]),
			 sizeof(u32), 0,
			 MAKEUNIQUETAG(psKernelCCB->psCCBMemInfo));

		if (PDumpIsCaptureFrameKM())
			psDevInfo->sPDContext.ui32CacheControl = 0;
	}
#endif
	*psKernelCCB->pui32WriteOffset =
	    (*psKernelCCB->pui32WriteOffset + 1) & 255;

#if defined(PDUMP)
	if (ui32CallerID != ISR_ID) {
		if (PDumpIsCaptureFrameKM())
			psKernelCCB->ui32CCBDumpWOff =
			    (psKernelCCB->ui32CCBDumpWOff + 1) & 0xFF;

		PDUMPCOMMENTWITHFLAGS(0, "Kernel CCB write offset\r\n");
		PDUMPMEM(&psKernelCCB->ui32CCBDumpWOff,
			 psKernelCCB->psCCBCtlMemInfo,
			 offsetof(struct PVRSRV_SGX_CCB_CTL, ui32WriteOffset),
			 sizeof(u32),
			 0, MAKEUNIQUETAG(psKernelCCB->psCCBCtlMemInfo));
		PDUMPCOMMENTWITHFLAGS(0, "Kernel CCB event kicker\r\n");
		PDUMPMEM(&psKernelCCB->ui32CCBDumpWOff,
			 psDevInfo->psKernelCCBEventKickerMemInfo,
			 0,
			 sizeof(u32),
			 0,
			 MAKEUNIQUETAG(psDevInfo->
				       psKernelCCBEventKickerMemInfo));
		PDUMPCOMMENTWITHFLAGS(0, "Event kick\r\n");
		PDUMPREGWITHFLAGS(EUR_CR_EVENT_KICK, EUR_CR_EVENT_KICK_NOW_MASK,
				  0);
	}
#endif
	*psDevInfo->pui32KernelCCBEventKicker =
	    (*psDevInfo->pui32KernelCCBEventKicker + 1) & 0xFF;
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_EVENT_KICK,
		     EUR_CR_EVENT_KICK_NOW_MASK);

Exit:
	PVRSRVPowerUnlock(ui32CallerID);

	if (ui32CallerID != ISR_ID)

		SGXTestActivePowerEvent(psDeviceNode, ui32CallerID);

	return eError;
}

void SGXScheduleProcessQueues(struct PVRSRV_DEVICE_NODE *psDeviceNode)
{
	enum PVRSRV_ERROR eError;
	struct PVRSRV_SGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	struct PVRSRV_SGX_HOST_CTL *psHostCtl =
	    psDevInfo->psKernelSGXHostCtlMemInfo->pvLinAddrKM;
	u32 ui32PowManFlags;
	struct PVRSRV_SGX_COMMAND sCommand = { 0 };

	ui32PowManFlags = psHostCtl->ui32PowManFlags;
	if ((ui32PowManFlags & PVRSRV_USSE_EDM_POWMAN_NO_WORK) != 0)
		return;

	sCommand.ui32Data[0] = PVRSRV_CCBFLAGS_PROCESS_QUEUESCMD;
	eError =
	    SGXScheduleCCBCommandKM(psDeviceNode, PVRSRV_SGX_COMMAND_EDM_KICK,
				    &sCommand, ISR_ID);
	if (eError != PVRSRV_OK)
		PVR_DPF(PVR_DBG_ERROR, "SGXScheduleProcessQueues failed to "
					"schedule CCB command: %lu",
			 eError);
}

#if defined(PDUMP)
void DumpBufferArray(struct PVR3DIF4_KICKTA_DUMP_BUFFER *psBufferArray,
			 u32 ui32BufferArrayLength, IMG_BOOL bDumpPolls)
{
	u32 i;

	for (i = 0; i < ui32BufferArrayLength; i++) {
		struct PVR3DIF4_KICKTA_DUMP_BUFFER *psBuffer;
		struct PVRSRV_KERNEL_SYNC_INFO *psSyncInfo;
		char *pszName;
		void *hUniqueTag;

		psBuffer = &psBufferArray[i];
		pszName = psBuffer->pszName;
		if (!pszName)
			pszName = "Nameless buffer";

		hUniqueTag =
		    MAKEUNIQUETAG((struct PVRSRV_KERNEL_MEM_INFO *)psBuffer->
				  hKernelMemInfo);
		psSyncInfo = ((struct PVRSRV_KERNEL_MEM_INFO *)
				psBuffer->hKernelMemInfo)->psKernelSyncInfo;

		if (psBuffer->ui32Start <= psBuffer->ui32End) {
			if (bDumpPolls) {
				PDUMPCOMMENTWITHFLAGS(0,
						      "Wait for %s space\r\n",
						      pszName);
				PDUMPCBP(psSyncInfo->psSyncDataMemInfoKM,
					 offsetof(struct PVRSRV_SYNC_DATA,
						  ui32ReadOpsComplete),
					 psBuffer->ui32Start,
					 psBuffer->ui32SpaceUsed,
					 psBuffer->ui32BufferSize, 0,
					 MAKEUNIQUETAG(psSyncInfo->
						       psSyncDataMemInfoKM));
			}

			PDUMPCOMMENTWITHFLAGS(0, "%s\r\n", pszName);
			PDUMPMEM(NULL,
				 (struct PVRSRV_KERNEL_MEM_INFO *)psBuffer->
				 hKernelMemInfo, psBuffer->ui32Start,
				 psBuffer->ui32End - psBuffer->ui32Start, 0,
				 hUniqueTag);
		} else {
			if (bDumpPolls) {
				PDUMPCOMMENTWITHFLAGS(0,
						      "Wait for %s space\r\n",
						      pszName);
				PDUMPCBP(psSyncInfo->psSyncDataMemInfoKM,
					 offsetof(struct PVRSRV_SYNC_DATA,
						  ui32ReadOpsComplete),
					 psBuffer->ui32Start,
					 psBuffer->ui32BackEndLength,
					 psBuffer->ui32BufferSize, 0,
					 MAKEUNIQUETAG(psSyncInfo->
						       psSyncDataMemInfoKM));
			}
			PDUMPCOMMENTWITHFLAGS(0, "%s (part 1)\r\n", pszName);
			PDUMPMEM(NULL,
				 (struct PVRSRV_KERNEL_MEM_INFO *)psBuffer->
				 hKernelMemInfo, psBuffer->ui32Start,
				 psBuffer->ui32BackEndLength, 0, hUniqueTag);

			if (bDumpPolls) {
				PDUMPMEMPOL(psSyncInfo->psSyncDataMemInfoKM,
					    offsetof(struct PVRSRV_SYNC_DATA,
						     ui32ReadOpsComplete), 0,
					    0xFFFFFFFF,
					    PDUMP_POLL_OPERATOR_NOTEQUAL,
					    IMG_FALSE, IMG_FALSE,
					    MAKEUNIQUETAG(psSyncInfo->
							  psSyncDataMemInfoKM));

				PDUMPCOMMENTWITHFLAGS(0,
						      "Wait for %s space\r\n",
						      pszName);
				PDUMPCBP(psSyncInfo->psSyncDataMemInfoKM,
					 offsetof(struct PVRSRV_SYNC_DATA,
						  ui32ReadOpsComplete), 0,
					 psBuffer->ui32End,
					 psBuffer->ui32BufferSize, 0,
					 MAKEUNIQUETAG(psSyncInfo->
						       psSyncDataMemInfoKM));
			}
			PDUMPCOMMENTWITHFLAGS(0, "%s (part 2)\r\n", pszName);
			PDUMPMEM(NULL,
				 (struct PVRSRV_KERNEL_MEM_INFO *)psBuffer->
				 hKernelMemInfo, 0, psBuffer->ui32End, 0,
				 hUniqueTag);
		}
	}
}
#endif

IMG_BOOL SGXIsDevicePowered(struct PVRSRV_DEVICE_NODE *psDeviceNode)
{
	return PVRSRVIsDevicePowered(psDeviceNode->sDevId.ui32DeviceIndex);
}

enum PVRSRV_ERROR SGXGetInternalDevInfoKM(void *hDevCookie,
			 struct PVR3DIF4_INTERNAL_DEVINFO *psSGXInternalDevInfo)
{
	struct PVRSRV_SGXDEV_INFO *psDevInfo = (struct PVRSRV_SGXDEV_INFO *)
			((struct PVRSRV_DEVICE_NODE *)hDevCookie)->pvDevice;

	psSGXInternalDevInfo->ui32Flags = psDevInfo->ui32Flags;
	psSGXInternalDevInfo->bForcePTOff = (IMG_BOOL)psDevInfo->bForcePTOff;

	psSGXInternalDevInfo->hCtlKernelMemInfoHandle = (void *)
			psDevInfo->psKernelSGXHostCtlMemInfo;

	return PVRSRV_OK;
}

static void SGXCleanupRequest(struct PVRSRV_DEVICE_NODE *psDeviceNode,
				  struct IMG_DEV_VIRTADDR *psHWDataDevVAddr,
				  u32 ui32ResManRequestFlag)
{
	struct PVRSRV_SGXDEV_INFO *psSGXDevInfo =
	    (struct PVRSRV_SGXDEV_INFO *)psDeviceNode->pvDevice;
	struct PVRSRV_KERNEL_MEM_INFO *psSGXHostCtlMemInfo =
	    psSGXDevInfo->psKernelSGXHostCtlMemInfo;
	struct PVRSRV_SGX_HOST_CTL *psSGXHostCtl =
	    (struct PVRSRV_SGX_HOST_CTL *)psSGXHostCtlMemInfo->pvLinAddrKM;
	u32 ui32PowManFlags;
#if defined(PDUMP)
	void *hUniqueTag = MAKEUNIQUETAG(psSGXHostCtlMemInfo);
#endif

	ui32PowManFlags = psSGXHostCtl->ui32PowManFlags;
	if ((ui32PowManFlags & PVRSRV_USSE_EDM_POWMAN_NO_WORK) != 0) {
		;
	} else {
		if (psSGXDevInfo->ui32CacheControl &
					SGX_BIF_INVALIDATE_PDCACHE) {
			psSGXHostCtl->ui32ResManFlags |=
			    PVRSRV_USSE_EDM_RESMAN_CLEANUP_INVALPD;
			psSGXDevInfo->ui32CacheControl ^=
			    SGX_BIF_INVALIDATE_PDCACHE;
		}
		if (psSGXDevInfo->ui32CacheControl &
					SGX_BIF_INVALIDATE_PTCACHE) {
			psSGXHostCtl->ui32ResManFlags |=
			    PVRSRV_USSE_EDM_RESMAN_CLEANUP_INVALPT;
			psSGXDevInfo->ui32CacheControl ^=
			    SGX_BIF_INVALIDATE_PTCACHE;
		}

		psSGXHostCtl->sResManCleanupData.uiAddr =
		    psHWDataDevVAddr->uiAddr;

		psSGXHostCtl->ui32ResManFlags |= ui32ResManRequestFlag;

		PDUMPCOMMENT
		   ("TA/3D CCB Control - Request clean-up event on uKernel...");
		PDUMPMEM(NULL, psSGXHostCtlMemInfo,
			 offsetof(struct PVRSRV_SGX_HOST_CTL,
				  sResManCleanupData.uiAddr),
			 sizeof(u32), PDUMP_FLAGS_CONTINUOUS,
			 hUniqueTag);
		PDUMPMEM(NULL, psSGXHostCtlMemInfo,
			 offsetof(struct PVRSRV_SGX_HOST_CTL, ui32ResManFlags),
			 sizeof(u32), PDUMP_FLAGS_CONTINUOUS,
			 hUniqueTag);

		SGXScheduleProcessQueues(psDeviceNode);

		if (PollForValueKM
		    ((volatile u32 *)(&psSGXHostCtl->ui32ResManFlags),
		     PVRSRV_USSE_EDM_RESMAN_CLEANUP_COMPLETE,
		     PVRSRV_USSE_EDM_RESMAN_CLEANUP_COMPLETE,
		     MAX_HW_TIME_US / WAIT_TRY_COUNT,
		     WAIT_TRY_COUNT) != PVRSRV_OK)
			PVR_DPF(PVR_DBG_ERROR, "SGXCleanupRequest: "
			 "Wait for uKernel to clean up render context failed");

#ifdef PDUMP

		PDUMPCOMMENT
	       ("TA/3D CCB Control - Wait for clean-up request to complete...");
		PDUMPMEMPOL(psSGXHostCtlMemInfo,
			    offsetof(struct PVRSRV_SGX_HOST_CTL,
			    ui32ResManFlags),
			    PVRSRV_USSE_EDM_RESMAN_CLEANUP_COMPLETE,
			    PVRSRV_USSE_EDM_RESMAN_CLEANUP_COMPLETE,
			    PDUMP_POLL_OPERATOR_EQUAL, IMG_FALSE, IMG_FALSE,
			    hUniqueTag);
#endif

		psSGXHostCtl->ui32ResManFlags &= ~(ui32ResManRequestFlag);
		psSGXHostCtl->ui32ResManFlags &=
		    ~(PVRSRV_USSE_EDM_RESMAN_CLEANUP_COMPLETE);
		PDUMPMEM(NULL, psSGXHostCtlMemInfo,
			 offsetof(struct PVRSRV_SGX_HOST_CTL, ui32ResManFlags),
			 sizeof(u32), PDUMP_FLAGS_CONTINUOUS,
			 hUniqueTag);
	}
}

struct SGX_HW_RENDER_CONTEXT_CLEANUP {
	struct PVRSRV_DEVICE_NODE *psDeviceNode;
	struct IMG_DEV_VIRTADDR sHWRenderContextDevVAddr;
	void *hBlockAlloc;
	struct RESMAN_ITEM *psResItem;
};

static enum PVRSRV_ERROR SGXCleanupHWRenderContextCallback(void *pvParam,
						      u32 ui32Param)
{
	struct SGX_HW_RENDER_CONTEXT_CLEANUP *psCleanup = pvParam;

	PVR_UNREFERENCED_PARAMETER(ui32Param);

	SGXCleanupRequest(psCleanup->psDeviceNode,
			  &psCleanup->sHWRenderContextDevVAddr,
			  PVRSRV_USSE_EDM_RESMAN_CLEANUP_RC_REQUEST);

	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
		  sizeof(struct SGX_HW_RENDER_CONTEXT_CLEANUP),
		  psCleanup, psCleanup->hBlockAlloc);

	return PVRSRV_OK;
}

struct SGX_HW_TRANSFER_CONTEXT_CLEANUP {
	struct PVRSRV_DEVICE_NODE *psDeviceNode;
	struct IMG_DEV_VIRTADDR sHWTransferContextDevVAddr;
	void *hBlockAlloc;
	struct RESMAN_ITEM *psResItem;
};

static enum PVRSRV_ERROR SGXCleanupHWTransferContextCallback(void *pvParam,
							u32 ui32Param)
{
	struct SGX_HW_TRANSFER_CONTEXT_CLEANUP *psCleanup =
	    (struct SGX_HW_TRANSFER_CONTEXT_CLEANUP *)pvParam;

	PVR_UNREFERENCED_PARAMETER(ui32Param);

	SGXCleanupRequest(psCleanup->psDeviceNode,
			  &psCleanup->sHWTransferContextDevVAddr,
			  PVRSRV_USSE_EDM_RESMAN_CLEANUP_TC_REQUEST);

	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
		  sizeof(struct SGX_HW_TRANSFER_CONTEXT_CLEANUP),
		  psCleanup, psCleanup->hBlockAlloc);

	return PVRSRV_OK;
}

void *SGXRegisterHWRenderContextKM(void *psDeviceNode,
			    struct IMG_DEV_VIRTADDR *psHWRenderContextDevVAddr,
			    struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	enum PVRSRV_ERROR eError;
	void *hBlockAlloc;
	struct SGX_HW_RENDER_CONTEXT_CLEANUP *psCleanup;
	struct RESMAN_ITEM *psResItem;

	eError = OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
			    sizeof(struct SGX_HW_RENDER_CONTEXT_CLEANUP),
			    (void **) &psCleanup, &hBlockAlloc);

	if (eError != PVRSRV_OK) {
		PVR_DPF(PVR_DBG_ERROR, "%s: alloc failed", __func__);
		return NULL;
	}

	psCleanup->hBlockAlloc = hBlockAlloc;
	psCleanup->psDeviceNode = psDeviceNode;
	psCleanup->sHWRenderContextDevVAddr = *psHWRenderContextDevVAddr;

	psResItem = ResManRegisterRes(psPerProc->hResManContext,
				      RESMAN_TYPE_HW_RENDER_CONTEXT,
				      (void *) psCleanup,
				      0, &SGXCleanupHWRenderContextCallback);

	if (psResItem == NULL) {
		PVR_DPF(PVR_DBG_ERROR,
			 "%s: can't register resource", __func__);
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
			  sizeof(struct SGX_HW_RENDER_CONTEXT_CLEANUP),
			  psCleanup, psCleanup->hBlockAlloc);

		return NULL;
	}

	psCleanup->psResItem = psResItem;

	return (void *)psCleanup;
}

enum PVRSRV_ERROR SGXUnregisterHWRenderContextKM(void *hHWRenderContext)
{
	struct SGX_HW_RENDER_CONTEXT_CLEANUP *psCleanup;

	PVR_ASSERT(hHWRenderContext != NULL);

	psCleanup = (struct SGX_HW_RENDER_CONTEXT_CLEANUP *)hHWRenderContext;

	ResManFreeResByPtr(psCleanup->psResItem);

	return PVRSRV_OK;
}

void *SGXRegisterHWTransferContextKM(void *psDeviceNode,
		      struct IMG_DEV_VIRTADDR *psHWTransferContextDevVAddr,
		      struct PVRSRV_PER_PROCESS_DATA *psPerProc)
{
	enum PVRSRV_ERROR eError;
	void *hBlockAlloc;
	struct SGX_HW_TRANSFER_CONTEXT_CLEANUP *psCleanup;
	struct RESMAN_ITEM *psResItem;

	eError = OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
			    sizeof(struct SGX_HW_TRANSFER_CONTEXT_CLEANUP),
			    (void **) &psCleanup, &hBlockAlloc);

	if (eError != PVRSRV_OK) {
		PVR_DPF(PVR_DBG_ERROR, "%s: alloc failed", __func__);
		return NULL;
	}

	psCleanup->hBlockAlloc = hBlockAlloc;
	psCleanup->psDeviceNode = psDeviceNode;
	psCleanup->sHWTransferContextDevVAddr = *psHWTransferContextDevVAddr;

	psResItem = ResManRegisterRes(psPerProc->hResManContext,
				      RESMAN_TYPE_HW_TRANSFER_CONTEXT,
				      psCleanup,
				      0, &SGXCleanupHWTransferContextCallback);

	if (psResItem == NULL) {
		PVR_DPF(PVR_DBG_ERROR,
			"%s: can't register resource", __func__);
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
			  sizeof(struct SGX_HW_TRANSFER_CONTEXT_CLEANUP),
			  psCleanup, psCleanup->hBlockAlloc);

		return NULL;
	}

	psCleanup->psResItem = psResItem;

	return (void *)psCleanup;
}

enum PVRSRV_ERROR SGXUnregisterHWTransferContextKM(void *hHWTransferContext)
{
	struct SGX_HW_TRANSFER_CONTEXT_CLEANUP *psCleanup;

	PVR_ASSERT(hHWTransferContext != NULL);

	psCleanup = (struct SGX_HW_TRANSFER_CONTEXT_CLEANUP *)
							hHWTransferContext;

	ResManFreeResByPtr(psCleanup->psResItem);

	return PVRSRV_OK;
}


static inline IMG_BOOL SGX2DQuerySyncOpsComplete(
				struct PVRSRV_KERNEL_SYNC_INFO *psSyncInfo)
{
	struct PVRSRV_SYNC_DATA *psSyncData = psSyncInfo->psSyncData;

	return (IMG_BOOL)((psSyncData->ui32ReadOpsComplete ==
			  psSyncData->ui32ReadOpsPending) &&
			  (psSyncData->ui32WriteOpsComplete ==
			  psSyncData->ui32WriteOpsPending));
}

enum PVRSRV_ERROR SGX2DQueryBlitsCompleteKM(
				    struct PVRSRV_SGXDEV_INFO *psDevInfo,
				    struct PVRSRV_KERNEL_SYNC_INFO *psSyncInfo,
				    IMG_BOOL bWaitForComplete)
{
	IMG_BOOL bStart = IMG_FALSE;
	u32 uiStart = 0;

	PVR_UNREFERENCED_PARAMETER(psDevInfo);

	PVR_DPF(PVR_DBG_CALLTRACE, "SGX2DQueryBlitsCompleteKM: Start");

	if (SGX2DQuerySyncOpsComplete(psSyncInfo)) {

		PVR_DPF(PVR_DBG_CALLTRACE, "SGX2DQueryBlitsCompleteKM: "
					"No wait. Blits complete.");
		return PVRSRV_OK;
	}

	if (!bWaitForComplete) {

		PVR_DPF(PVR_DBG_CALLTRACE,
			 "SGX2DQueryBlitsCompleteKM: No wait. Ops pending.");
		return PVRSRV_ERROR_CMD_NOT_PROCESSED;
	}

	PVR_DPF(PVR_DBG_MESSAGE,
		 "SGX2DQueryBlitsCompleteKM: Ops pending. Start polling.");
	do {
		OSWaitus(MAX_HW_TIME_US / WAIT_TRY_COUNT);

		if (SGX2DQuerySyncOpsComplete(psSyncInfo)) {

			PVR_DPF(PVR_DBG_CALLTRACE,
				 "SGX2DQueryBlitsCompleteKM: "
				 "Wait over.  Blits complete.");
			return PVRSRV_OK;
		}

		if (bStart == IMG_FALSE) {
			uiStart = OSClockus();
			bStart = IMG_TRUE;
		}

		OSWaitus(MAX_HW_TIME_US / WAIT_TRY_COUNT);
	} while ((OSClockus() - uiStart) < MAX_HW_TIME_US);

	PVR_DPF(PVR_DBG_ERROR,
		 "SGX2DQueryBlitsCompleteKM: Timed out. Ops pending.");

#if defined(DEBUG)
	{
		struct PVRSRV_SYNC_DATA *psSyncData = psSyncInfo->psSyncData;

		PVR_TRACE("SGX2DQueryBlitsCompleteKM: "
			   "Syncinfo: %p, Syncdata: %p",
			   psSyncInfo, psSyncData);

		PVR_TRACE("SGX2DQueryBlitsCompleteKM: "
			   "Read ops complete: %d, Read ops pending: %d",
			   psSyncData->ui32ReadOpsComplete,
			   psSyncData->ui32ReadOpsPending);
		PVR_TRACE("SGX2DQueryBlitsCompleteKM: "
			  "Write ops complete: %d, Write ops pending: %d",
			  psSyncData->ui32WriteOpsComplete,
			  psSyncData->ui32WriteOpsPending);

	}
#endif
	return PVRSRV_ERROR_TIMEOUT;
}

void SGXFlushHWRenderTargetKM(void *psDeviceNode,
			      struct IMG_DEV_VIRTADDR sHWRTDataSetDevVAddr)
{
	PVR_ASSERT(sHWRTDataSetDevVAddr.uiAddr != 0);

	SGXCleanupRequest((struct PVRSRV_DEVICE_NODE *)psDeviceNode,
			  &sHWRTDataSetDevVAddr,
			  PVRSRV_USSE_EDM_RESMAN_CLEANUP_RT_REQUEST);
}
