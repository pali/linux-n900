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

#if !defined(__SGXINFO_H__)
#define __SGXINFO_H__

#include "sgxscript.h"

#include "servicesint.h"

#include "services.h"
#include "sgxapi_km.h"

#define SGX_MAX_DEV_DATA		24
#define	SGX_MAX_INIT_MEM_HANDLES	16

struct SGX_BRIDGE_INFO_FOR_SRVINIT {
	struct IMG_DEV_PHYADDR sPDDevPAddr;
	struct PVRSRV_HEAP_INFO asHeapInfo[PVRSRV_MAX_CLIENT_HEAPS];
};

struct SGX_BRIDGE_INIT_INFO {
	void *hKernelCCBMemInfo;
	void *hKernelCCBCtlMemInfo;
	void *hKernelCCBEventKickerMemInfo;
	void *hKernelSGXHostCtlMemInfo;
	u32 ui32TAKickAddress;
	u32 ui32VideoHandlerAddress;
	void *hKernelHWPerfCBMemInfo;

	u32 ui32EDMTaskReg0;
	u32 ui32EDMTaskReg1;

	u32 ui32ClkGateCtl;
	u32 ui32ClkGateCtl2;
	u32 ui32ClkGateStatusMask;

	u32 ui32CacheControl;

	u32 asInitDevData[SGX_MAX_DEV_DATA];
	void *asInitMemHandles[SGX_MAX_INIT_MEM_HANDLES];

	struct SGX_INIT_SCRIPTS sScripts;

};

struct PVRSRV_SGX_COMMAND {
	u32 ui32ServiceAddress;
	u32 ui32Data[7];
};

struct PVRSRV_SGX_KERNEL_CCB {
	struct PVRSRV_SGX_COMMAND asCommands[256];
};

struct PVRSRV_SGX_CCB_CTL {
	u32 ui32WriteOffset;
	u32 ui32ReadOffset;
};

#define SGX_AUXCCBFLAGS_SHARED					0x00000001

enum PVRSRV_SGX_COMMAND_TYPE {
	PVRSRV_SGX_COMMAND_EDM_KICK = 0,
	PVRSRV_SGX_COMMAND_VIDEO_KICK = 1,

	PVRSRV_SGX_COMMAND_FORCE_I32 = 0xFFFFFFFF,

};

#define PVRSRV_CCBFLAGS_RASTERCMD			0x1
#define PVRSRV_CCBFLAGS_TRANSFERCMD			0x2
#define PVRSRV_CCBFLAGS_PROCESS_QUEUESCMD		0x3

#define	SGX_BIF_INVALIDATE_PTCACHE			0x1
#define	SGX_BIF_INVALIDATE_PDCACHE			0x2

struct PVR3DIF4_CCB_KICK {
	enum PVRSRV_SGX_COMMAND_TYPE eCommand;
	struct PVRSRV_SGX_COMMAND sCommand;
	void *hCCBKernelMemInfo;
	void *hRenderSurfSyncInfo;

	u32 ui32NumTAStatusVals;
	void *ahTAStatusSyncInfo[SGX_MAX_TA_STATUS_VALS];

	u32 ui32Num3DStatusVals;
	void *ah3DStatusSyncInfo[SGX_MAX_3D_STATUS_VALS];

	IMG_BOOL bFirstKickOrResume;
#ifdef PDUMP
	IMG_BOOL bTerminateOrAbort;
#endif
	IMG_BOOL bKickRender;

	u32 ui32CCBOffset;

	u32 ui32NumSrcSyncs;
	void *ahSrcKernelSyncInfo[SGX_MAX_SRC_SYNCS];

	IMG_BOOL bTADependency;
	void *hTA3DSyncInfo;

	void *hTASyncInfo;
	void *h3DSyncInfo;
#if defined(PDUMP)
	u32 ui32CCBDumpWOff;
#endif
};

#define SGX_VIDEO_USE_CODE_BASE_INDEX		14
#define SGX_KERNEL_USE_CODE_BASE_INDEX		15

struct PVRSRV_SGX_HOST_CTL {

	volatile u32 ui32PowManFlags;
	u32 ui32uKernelDetectedLockups;
	u32 ui32HostDetectedLockups;
	u32 ui32HWRecoverySampleRate;
	u32 ui32ActivePowManSampleRate;
	u32 ui32InterruptFlags;
	u32 ui32InterruptClearFlags;

	u32 ui32ResManFlags;
	struct IMG_DEV_VIRTADDR sResManCleanupData;

	struct IMG_DEV_VIRTADDR sTAHWPBDesc;
	struct IMG_DEV_VIRTADDR s3DHWPBDesc;
	struct IMG_DEV_VIRTADDR sHostHWPBDesc;

	u32 ui32NumActivePowerEvents;

	u32 ui32HWPerfFlags;

	u32 ui32TimeWraps;
};

struct PVR3DIF4_CLIENT_INFO {
	u32 ui32ProcessID;
	void *pvProcess;
	struct PVRSRV_MISC_INFO sMiscInfo;

	u32 asDevData[SGX_MAX_DEV_DATA];

};

struct PVR3DIF4_INTERNAL_DEVINFO {
	u32 ui32Flags;
	void *hCtlKernelMemInfoHandle;
	IMG_BOOL bForcePTOff;
};

struct PVRSRV_SGX_SHARED_CCB {
	struct PVRSRV_CLIENT_MEM_INFO *psCCBClientMemInfo;
	struct PVRSRV_CLIENT_MEM_INFO *psCCBCtlClientMemInfo;
	u32 *pui32CCBLinAddr;
	struct IMG_DEV_VIRTADDR sCCBDevAddr;
	u32 *pui32WriteOffset;
	volatile u32 *pui32ReadOffset;
	u32 ui32Size;
	u32 ui32AllocGran;

#ifdef PDUMP
	u32 ui32CCBDumpWOff;
#endif
};

struct PVRSRV_SGX_CCB {
	struct PVRSRV_KERNEL_MEM_INFO *psCCBMemInfo;
	struct PVRSRV_KERNEL_MEM_INFO *psCCBCtlMemInfo;
	u32 *pui32CCBLinAddr;
	struct IMG_DEV_VIRTADDR sCCBDevAddr;
	u32 *pui32WriteOffset;
	volatile u32 *pui32ReadOffset;
	u32 ui32Size;
	u32 ui32AllocGran;

#ifdef PDUMP
	u32 ui32CCBDumpWOff;
#endif
};

struct CTL_STATUS {
	struct IMG_DEV_VIRTADDR sStatusDevAddr;
	u32 ui32StatusValue;
};

#define SGXTQ_MAX_STATUS		(SGX_MAX_TRANSFER_STATUS_VALS + 2)
struct PVR3DIF4_CMDTA_SHARED {
	u32 ui32NumTAStatusVals;
	u32 ui32Num3DStatusVals;

	u32 ui32WriteOpsPendingVal;
	struct IMG_DEV_VIRTADDR sWriteOpsCompleteDevVAddr;
	u32 ui32ReadOpsPendingVal;
	struct IMG_DEV_VIRTADDR sReadOpsCompleteDevVAddr;

	u32 ui32TQSyncWriteOpsPendingVal;
	struct IMG_DEV_VIRTADDR sTQSyncWriteOpsCompleteDevVAddr;
	u32 ui32TQSyncReadOpsPendingVal;
	struct IMG_DEV_VIRTADDR sTQSyncReadOpsCompleteDevVAddr;

	u32 ui323DTQSyncWriteOpsPendingVal;
	struct IMG_DEV_VIRTADDR s3DTQSyncWriteOpsCompleteDevVAddr;
	u32 ui323DTQSyncReadOpsPendingVal;
	struct IMG_DEV_VIRTADDR s3DTQSyncReadOpsCompleteDevVAddr;

	u32 ui32NumSrcSyncs;
	struct PVRSRV_DEVICE_SYNC_OBJECT asSrcSyncs[SGX_MAX_SRC_SYNCS];

	struct CTL_STATUS sCtlTAStatusInfo[SGX_MAX_TA_STATUS_VALS];
	struct CTL_STATUS sCtl3DStatusInfo[SGX_MAX_3D_STATUS_VALS];

	struct PVRSRV_DEVICE_SYNC_OBJECT sTA3DDependancy;

};

struct PVR3DIF4_TRANSFERCMD_SHARED {

	u32 ui32SrcReadOpPendingVal;
	struct IMG_DEV_VIRTADDR sSrcReadOpsCompleteDevAddr;

	u32 ui32SrcWriteOpPendingVal;
	struct IMG_DEV_VIRTADDR sSrcWriteOpsCompleteDevAddr;

	u32 ui32DstReadOpPendingVal;
	struct IMG_DEV_VIRTADDR sDstReadOpsCompleteDevAddr;

	u32 ui32DstWriteOpPendingVal;
	struct IMG_DEV_VIRTADDR sDstWriteOpsCompleteDevAddr;

	u32 ui32TASyncWriteOpsPendingVal;
	struct IMG_DEV_VIRTADDR sTASyncWriteOpsCompleteDevVAddr;
	u32 ui32TASyncReadOpsPendingVal;
	struct IMG_DEV_VIRTADDR sTASyncReadOpsCompleteDevVAddr;

	u32 ui323DSyncWriteOpsPendingVal;
	struct IMG_DEV_VIRTADDR s3DSyncWriteOpsCompleteDevVAddr;
	u32 ui323DSyncReadOpsPendingVal;
	struct IMG_DEV_VIRTADDR s3DSyncReadOpsCompleteDevVAddr;

	u32 ui32NumStatusVals;
	struct CTL_STATUS sCtlStatusInfo[SGXTQ_MAX_STATUS];

	u32 ui32NumSrcSync;
	u32 ui32NumDstSync;

	struct IMG_DEV_VIRTADDR sSrcWriteOpsDevVAddr[SGX_MAX_TRANSFER_SYNC_OPS];
	struct IMG_DEV_VIRTADDR sSrcReadOpsDevVAddr[SGX_MAX_TRANSFER_SYNC_OPS];

	struct IMG_DEV_VIRTADDR sDstWriteOpsDevVAddr[SGX_MAX_TRANSFER_SYNC_OPS];
	struct IMG_DEV_VIRTADDR sDstReadOpsDevVAddr[SGX_MAX_TRANSFER_SYNC_OPS];
};

struct PVRSRV_TRANSFER_SGX_KICK {
	void *hCCBMemInfo;
	u32 ui32SharedCmdCCBOffset;

	struct IMG_DEV_VIRTADDR sHWTransferContextDevVAddr;

	void *hTASyncInfo;
	void *h3DSyncInfo;

	u32 ui32NumSrcSync;
	void *ahSrcSyncInfo[SGX_MAX_TRANSFER_SYNC_OPS];

	u32 ui32NumDstSync;
	void *ahDstSyncInfo[SGX_MAX_TRANSFER_SYNC_OPS];

	u32 ui32StatusFirstSync;

#if defined(PDUMP)
	u32 ui32CCBDumpWOff;
#endif
};

#define PVRSRV_SGX_DIFF_NUM_COUNTERS	9

struct PVRSRV_SGXDEV_DIFF_INFO {
	u32 aui32Counters[PVRSRV_SGX_DIFF_NUM_COUNTERS];
	u32 ui32Time[2];
	u32 ui32Marker[2];
};

#endif
