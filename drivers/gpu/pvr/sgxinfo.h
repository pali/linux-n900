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

#if !defined (__SGXINFO_H__)
#define __SGXINFO_H__

#include "sgxscript.h"

#include "servicesint.h"

#include "services.h"
#include "sgxapi_km.h"

#define SGX_MAX_DEV_DATA		24
#define	SGX_MAX_INIT_MEM_HANDLES	16

typedef struct _SGX_BRIDGE_INFO_FOR_SRVINIT {
	IMG_DEV_PHYADDR sPDDevPAddr;
	PVRSRV_HEAP_INFO asHeapInfo[PVRSRV_MAX_CLIENT_HEAPS];
} SGX_BRIDGE_INFO_FOR_SRVINIT;

typedef struct _SGX_BRIDGE_INIT_INFO_ {
	IMG_HANDLE hKernelCCBMemInfo;
	IMG_HANDLE hKernelCCBCtlMemInfo;
	IMG_HANDLE hKernelCCBEventKickerMemInfo;
	IMG_HANDLE hKernelSGXHostCtlMemInfo;
	IMG_UINT32 ui32TAKickAddress;
	IMG_UINT32 ui32VideoHandlerAddress;
	IMG_HANDLE hKernelHWPerfCBMemInfo;

	IMG_UINT32 ui32EDMTaskReg0;
	IMG_UINT32 ui32EDMTaskReg1;

	IMG_UINT32 ui32ClkGateCtl;
	IMG_UINT32 ui32ClkGateCtl2;
	IMG_UINT32 ui32ClkGateStatusMask;

	IMG_UINT32 ui32CacheControl;

	IMG_UINT32 asInitDevData[SGX_MAX_DEV_DATA];
	IMG_HANDLE asInitMemHandles[SGX_MAX_INIT_MEM_HANDLES];

	SGX_INIT_SCRIPTS sScripts;

} SGX_BRIDGE_INIT_INFO;

typedef struct _PVRSRV_SGX_COMMAND_ {
	IMG_UINT32 ui32ServiceAddress;
	IMG_UINT32 ui32Data[7];
} PVRSRV_SGX_COMMAND;

typedef struct _PVRSRV_SGX_KERNEL_CCB_ {
	PVRSRV_SGX_COMMAND asCommands[256];
} PVRSRV_SGX_KERNEL_CCB;

typedef struct _PVRSRV_SGX_CCB_CTL_ {
	IMG_UINT32 ui32WriteOffset;
	IMG_UINT32 ui32ReadOffset;
} PVRSRV_SGX_CCB_CTL;

#define SGX_AUXCCBFLAGS_SHARED					0x00000001

typedef enum _PVRSRV_SGX_COMMAND_TYPE_ {
	PVRSRV_SGX_COMMAND_EDM_KICK = 0,
	PVRSRV_SGX_COMMAND_VIDEO_KICK = 1,

	PVRSRV_SGX_COMMAND_FORCE_I32 = 0xFFFFFFFF,

} PVRSRV_SGX_COMMAND_TYPE;

#define PVRSRV_CCBFLAGS_RASTERCMD			0x1
#define PVRSRV_CCBFLAGS_TRANSFERCMD			0x2
#define PVRSRV_CCBFLAGS_PROCESS_QUEUESCMD	0x3

#define	SGX_BIF_INVALIDATE_PTCACHE	0x1
#define	SGX_BIF_INVALIDATE_PDCACHE	0x2

typedef struct _PVR3DIF4_CCB_KICK_ {
	PVRSRV_SGX_COMMAND_TYPE eCommand;
	PVRSRV_SGX_COMMAND sCommand;
	IMG_HANDLE hCCBKernelMemInfo;
	IMG_HANDLE hRenderSurfSyncInfo;

	IMG_UINT32 ui32NumTAStatusVals;
	IMG_HANDLE ahTAStatusSyncInfo[SGX_MAX_TA_STATUS_VALS];

	IMG_UINT32 ui32Num3DStatusVals;
	IMG_HANDLE ah3DStatusSyncInfo[SGX_MAX_3D_STATUS_VALS];

	IMG_BOOL bFirstKickOrResume;
#ifdef PDUMP
	IMG_BOOL bTerminateOrAbort;
#endif
	IMG_BOOL bKickRender;

	IMG_UINT32 ui32CCBOffset;

	IMG_UINT32 ui32NumSrcSyncs;
	IMG_HANDLE ahSrcKernelSyncInfo[SGX_MAX_SRC_SYNCS];

	IMG_BOOL bTADependency;
	IMG_HANDLE hTA3DSyncInfo;

	IMG_HANDLE hTASyncInfo;
	IMG_HANDLE h3DSyncInfo;
#if defined(PDUMP)
	IMG_UINT32 ui32CCBDumpWOff;
#endif
} PVR3DIF4_CCB_KICK;

#define SGX_VIDEO_USE_CODE_BASE_INDEX		14
#define SGX_KERNEL_USE_CODE_BASE_INDEX		15

typedef struct _PVRSRV_SGX_HOST_CTL_ {

	volatile IMG_UINT32 ui32PowManFlags;
	IMG_UINT32 ui32uKernelDetectedLockups;
	IMG_UINT32 ui32HostDetectedLockups;
	IMG_UINT32 ui32HWRecoverySampleRate;
	IMG_UINT32 ui32ActivePowManSampleRate;
	IMG_UINT32 ui32InterruptFlags;
	IMG_UINT32 ui32InterruptClearFlags;

	IMG_UINT32 ui32ResManFlags;
	IMG_DEV_VIRTADDR sResManCleanupData;

	IMG_DEV_VIRTADDR sTAHWPBDesc;
	IMG_DEV_VIRTADDR s3DHWPBDesc;
	IMG_DEV_VIRTADDR sHostHWPBDesc;

	IMG_UINT32 ui32NumActivePowerEvents;

	IMG_UINT32 ui32HWPerfFlags;

	IMG_UINT32 ui32TimeWraps;
} PVRSRV_SGX_HOST_CTL;

typedef struct _PVR3DIF4_CLIENT_INFO_ {
	IMG_UINT32 ui32ProcessID;
	IMG_VOID *pvProcess;
	PVRSRV_MISC_INFO sMiscInfo;

	IMG_UINT32 asDevData[SGX_MAX_DEV_DATA];

} PVR3DIF4_CLIENT_INFO;

typedef struct _PVR3DIF4_INTERNAL_DEVINFO_ {
	IMG_UINT32 ui32Flags;
	IMG_HANDLE hCtlKernelMemInfoHandle;
	IMG_BOOL bForcePTOff;
} PVR3DIF4_INTERNAL_DEVINFO;

typedef struct _PVRSRV_SGX_SHARED_CCB_ {
	PVRSRV_CLIENT_MEM_INFO *psCCBClientMemInfo;
	PVRSRV_CLIENT_MEM_INFO *psCCBCtlClientMemInfo;
	IMG_UINT32 *pui32CCBLinAddr;
	IMG_DEV_VIRTADDR sCCBDevAddr;
	IMG_UINT32 *pui32WriteOffset;
	volatile IMG_UINT32 *pui32ReadOffset;
	IMG_UINT32 ui32Size;
	IMG_UINT32 ui32AllocGran;

#ifdef PDUMP
	IMG_UINT32 ui32CCBDumpWOff;
#endif
} PVRSRV_SGX_SHARED_CCB;

typedef struct _PVRSRV_SGX_CCB_ {
	PVRSRV_KERNEL_MEM_INFO *psCCBMemInfo;
	PVRSRV_KERNEL_MEM_INFO *psCCBCtlMemInfo;
	IMG_PUINT32 pui32CCBLinAddr;
	IMG_DEV_VIRTADDR sCCBDevAddr;
	IMG_UINT32 *pui32WriteOffset;
	volatile IMG_UINT32 *pui32ReadOffset;
	IMG_UINT32 ui32Size;
	IMG_UINT32 ui32AllocGran;

#ifdef PDUMP
	IMG_UINT32 ui32CCBDumpWOff;
#endif
} PVRSRV_SGX_CCB;

typedef struct _CTL_STATUS_ {
	IMG_DEV_VIRTADDR sStatusDevAddr;
	IMG_UINT32 ui32StatusValue;
} CTL_STATUS, *PCTL_STATUS;

#define SGXTQ_MAX_STATUS						SGX_MAX_TRANSFER_STATUS_VALS + 2
typedef struct _PVR3DIF4_CMDTA_SHARED_ {
	IMG_UINT32 ui32NumTAStatusVals;
	IMG_UINT32 ui32Num3DStatusVals;

	IMG_UINT32 ui32WriteOpsPendingVal;
	IMG_DEV_VIRTADDR sWriteOpsCompleteDevVAddr;
	IMG_UINT32 ui32ReadOpsPendingVal;
	IMG_DEV_VIRTADDR sReadOpsCompleteDevVAddr;

	IMG_UINT32 ui32TQSyncWriteOpsPendingVal;
	IMG_DEV_VIRTADDR sTQSyncWriteOpsCompleteDevVAddr;
	IMG_UINT32 ui32TQSyncReadOpsPendingVal;
	IMG_DEV_VIRTADDR sTQSyncReadOpsCompleteDevVAddr;

	IMG_UINT32 ui323DTQSyncWriteOpsPendingVal;
	IMG_DEV_VIRTADDR s3DTQSyncWriteOpsCompleteDevVAddr;
	IMG_UINT32 ui323DTQSyncReadOpsPendingVal;
	IMG_DEV_VIRTADDR s3DTQSyncReadOpsCompleteDevVAddr;

	IMG_UINT32 ui32NumSrcSyncs;
	PVRSRV_DEVICE_SYNC_OBJECT asSrcSyncs[SGX_MAX_SRC_SYNCS];

	CTL_STATUS sCtlTAStatusInfo[SGX_MAX_TA_STATUS_VALS];
	CTL_STATUS sCtl3DStatusInfo[SGX_MAX_3D_STATUS_VALS];

	PVRSRV_DEVICE_SYNC_OBJECT sTA3DDependancy;

} PVR3DIF4_CMDTA_SHARED;

typedef struct _PVR3DIF4_TRANSFERCMD_SHARED_ {

	IMG_UINT32 ui32SrcReadOpPendingVal;
	IMG_DEV_VIRTADDR sSrcReadOpsCompleteDevAddr;

	IMG_UINT32 ui32SrcWriteOpPendingVal;
	IMG_DEV_VIRTADDR sSrcWriteOpsCompleteDevAddr;

	IMG_UINT32 ui32DstReadOpPendingVal;
	IMG_DEV_VIRTADDR sDstReadOpsCompleteDevAddr;

	IMG_UINT32 ui32DstWriteOpPendingVal;
	IMG_DEV_VIRTADDR sDstWriteOpsCompleteDevAddr;

	IMG_UINT32 ui32TASyncWriteOpsPendingVal;
	IMG_DEV_VIRTADDR sTASyncWriteOpsCompleteDevVAddr;
	IMG_UINT32 ui32TASyncReadOpsPendingVal;
	IMG_DEV_VIRTADDR sTASyncReadOpsCompleteDevVAddr;

	IMG_UINT32 ui323DSyncWriteOpsPendingVal;
	IMG_DEV_VIRTADDR s3DSyncWriteOpsCompleteDevVAddr;
	IMG_UINT32 ui323DSyncReadOpsPendingVal;
	IMG_DEV_VIRTADDR s3DSyncReadOpsCompleteDevVAddr;

	IMG_UINT32 ui32NumStatusVals;
	CTL_STATUS sCtlStatusInfo[SGXTQ_MAX_STATUS];

	IMG_UINT32 ui32NumSrcSync;
	IMG_UINT32 ui32NumDstSync;

	IMG_DEV_VIRTADDR sSrcWriteOpsDevVAddr[SGX_MAX_TRANSFER_SYNC_OPS];
	IMG_DEV_VIRTADDR sSrcReadOpsDevVAddr[SGX_MAX_TRANSFER_SYNC_OPS];

	IMG_DEV_VIRTADDR sDstWriteOpsDevVAddr[SGX_MAX_TRANSFER_SYNC_OPS];
	IMG_DEV_VIRTADDR sDstReadOpsDevVAddr[SGX_MAX_TRANSFER_SYNC_OPS];
} PVR3DIF4_TRANSFERCMD_SHARED, *PPVR3DIF4_TRANSFERCMD_SHARED;

typedef struct _PVRSRV_TRANSFER_SGX_KICK_ {
	IMG_HANDLE hCCBMemInfo;
	IMG_UINT32 ui32SharedCmdCCBOffset;

	IMG_DEV_VIRTADDR sHWTransferContextDevVAddr;

	IMG_HANDLE hTASyncInfo;
	IMG_HANDLE h3DSyncInfo;

	IMG_UINT32 ui32NumSrcSync;
	IMG_HANDLE ahSrcSyncInfo[SGX_MAX_TRANSFER_SYNC_OPS];

	IMG_UINT32 ui32NumDstSync;
	IMG_HANDLE ahDstSyncInfo[SGX_MAX_TRANSFER_SYNC_OPS];

	IMG_UINT32 ui32StatusFirstSync;

#if defined(PDUMP)
	IMG_UINT32 ui32CCBDumpWOff;
#endif
} PVRSRV_TRANSFER_SGX_KICK, *PPVRSRV_TRANSFER_SGX_KICK;


#define PVRSRV_SGX_DIFF_NUM_COUNTERS	9

typedef struct _PVRSRV_SGXDEV_DIFF_INFO_ {
	IMG_UINT32 aui32Counters[PVRSRV_SGX_DIFF_NUM_COUNTERS];
	IMG_UINT32 ui32Time[2];
	IMG_UINT32 ui32Marker[2];
} PVRSRV_SGXDEV_DIFF_INFO, *PPVRSRV_SGXDEV_DIFF_INFO;

#endif
