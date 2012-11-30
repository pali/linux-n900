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

#ifndef __SGXINFOKM_H__
#define __SGXINFOKM_H__

#include "sgxdefs.h"
#include "device.h"
#include "sysconfig.h"
#include "sgxscript.h"
#include "sgxinfo.h"


#define	SGX_HOSTPORT_PRESENT					0x00000001UL

#define PVRSRV_USSE_EDM_POWMAN_IDLE_REQUEST			(1UL << 0)
#define PVRSRV_USSE_EDM_POWMAN_POWEROFF_REQUEST			(1UL << 1)
#define PVRSRV_USSE_EDM_POWMAN_IDLE_COMPLETE			(1UL << 2)
#define PVRSRV_USSE_EDM_POWMAN_POWEROFF_COMPLETE		(1UL << 3)
#define PVRSRV_USSE_EDM_POWMAN_POWEROFF_RESTART_IMMEDIATE	(1UL << 4)
#define PVRSRV_USSE_EDM_POWMAN_NO_WORK				(1UL << 5)

#define PVRSRV_USSE_EDM_INTERRUPT_HWR				(1UL << 0)
#define PVRSRV_USSE_EDM_INTERRUPT_ACTIVE_POWER			(1UL << 1)

#define PVRSRV_USSE_EDM_RESMAN_CLEANUP_RT_REQUEST	0x01UL
#define PVRSRV_USSE_EDM_RESMAN_CLEANUP_RC_REQUEST	0x02UL
#define PVRSRV_USSE_EDM_RESMAN_CLEANUP_TC_REQUEST	0x04UL
#define PVRSRV_USSE_EDM_RESMAN_CLEANUP_2DC_REQUEST	0x08UL
#define PVRSRV_USSE_EDM_RESMAN_CLEANUP_COMPLETE		0x10UL
#define PVRSRV_USSE_EDM_RESMAN_CLEANUP_INVALPD		0x20UL
#define PVRSRV_USSE_EDM_RESMAN_CLEANUP_INVALPT		0x40UL

struct PVRSRV_SGX_CCB_INFO;

struct PVRSRV_SGXDEV_INFO {
	enum PVRSRV_DEVICE_TYPE eDeviceType;
	enum PVRSRV_DEVICE_CLASS eDeviceClass;

	u8 ui8VersionMajor;
	u8 ui8VersionMinor;
	u32 ui32CoreConfig;
	u32 ui32CoreFlags;

	void __iomem *pvRegsBaseKM;

	void *hRegMapping;

	struct IMG_SYS_PHYADDR sRegsPhysBase;

	u32 ui32RegSize;

	u32 ui32CoreClockSpeed;
	u32 ui32uKernelTimerClock;

	void *psStubPBDescListKM;

	struct IMG_DEV_PHYADDR sKernelPDDevPAddr;

	void *pvDeviceMemoryHeap;
	struct PVRSRV_KERNEL_MEM_INFO *psKernelCCBMemInfo;
	struct PVRSRV_SGX_KERNEL_CCB *psKernelCCB;
	struct PVRSRV_SGX_CCB_INFO *psKernelCCBInfo;
	struct PVRSRV_KERNEL_MEM_INFO *psKernelCCBCtlMemInfo;
	struct PVRSRV_SGX_CCB_CTL *psKernelCCBCtl;
	struct PVRSRV_KERNEL_MEM_INFO *psKernelCCBEventKickerMemInfo;
	u32 *pui32KernelCCBEventKicker;
	u32 ui32TAKickAddress;
	u32 ui32TexLoadKickAddress;
	u32 ui32VideoHandlerAddress;
	u32 ui32KickTACounter;
	u32 ui32KickTARenderCounter;
	struct PVRSRV_KERNEL_MEM_INFO *psKernelHWPerfCBMemInfo;
	struct PVRSRV_SGXDEV_DIFF_INFO sDiffInfo;
	u32 ui32HWGroupRequested;
	u32 ui32HWReset;

	u32 ui32ClientRefCount;

	u32 ui32CacheControl;

	void *pvMMUContextList;

	IMG_BOOL bForcePTOff;

	u32 ui32EDMTaskReg0;
	u32 ui32EDMTaskReg1;

	u32 ui32ClkGateCtl;
	u32 ui32ClkGateCtl2;
	u32 ui32ClkGateStatusMask;
	struct SGX_INIT_SCRIPTS sScripts;

	void *hBIFResetPDOSMemHandle;
	struct IMG_DEV_PHYADDR sBIFResetPDDevPAddr;
	struct IMG_DEV_PHYADDR sBIFResetPTDevPAddr;
	struct IMG_DEV_PHYADDR sBIFResetPageDevPAddr;
	u32 *pui32BIFResetPD;
	u32 *pui32BIFResetPT;

	void *hTimer;
	u32 ui32TimeStamp;
	u32 ui32NumResets;

	struct PVRSRV_KERNEL_MEM_INFO *psKernelSGXHostCtlMemInfo;
	struct PVRSRV_SGX_HOST_CTL *psSGXHostCtl;

	u32 ui32Flags;

#if defined(PDUMP)
	struct PVRSRV_SGX_PDUMP_CONTEXT sPDContext;
#endif


	u32 asSGXDevData[SGX_MAX_DEV_DATA];

};

struct SGX_TIMING_INFORMATION {
	u32 ui32CoreClockSpeed;
	u32 ui32HWRecoveryFreq;
	u32 ui32ActivePowManLatencyms;
	u32 ui32uKernelFreq;
};

struct SGX_DEVICE_MAP {
	u32 ui32Flags;

	struct IMG_SYS_PHYADDR sRegsSysPBase;
	struct IMG_CPU_PHYADDR sRegsCpuPBase;
	void __iomem *pvRegsCpuVBase;
	u32 ui32RegsSize;

	struct IMG_SYS_PHYADDR sSPSysPBase;
	struct IMG_CPU_PHYADDR sSPCpuPBase;
	void *pvSPCpuVBase;
	u32 ui32SPSize;

	struct IMG_SYS_PHYADDR sLocalMemSysPBase;
	struct IMG_DEV_PHYADDR sLocalMemDevPBase;
	struct IMG_CPU_PHYADDR sLocalMemCpuPBase;
	u32 ui32LocalMemSize;

	u32 ui32IRQ;
};

struct PVRSRV_STUB_PBDESC;
struct PVRSRV_STUB_PBDESC {
	u32 ui32RefCount;
	u32 ui32TotalPBSize;
	struct PVRSRV_KERNEL_MEM_INFO *psSharedPBDescKernelMemInfo;
	struct PVRSRV_KERNEL_MEM_INFO *psHWPBDescKernelMemInfo;
	struct PVRSRV_KERNEL_MEM_INFO **ppsSubKernelMemInfos;
	u32 ui32SubKernelMemInfosCount;
	void *hDevCookie;
	struct PVRSRV_KERNEL_MEM_INFO *psBlockKernelMemInfo;
	struct PVRSRV_STUB_PBDESC *psNext;
};

struct PVRSRV_SGX_CCB_INFO {
	struct PVRSRV_KERNEL_MEM_INFO *psCCBMemInfo;
	struct PVRSRV_KERNEL_MEM_INFO *psCCBCtlMemInfo;
	struct PVRSRV_SGX_COMMAND *psCommands;
	u32 *pui32WriteOffset;
	volatile u32 *pui32ReadOffset;
#if defined(PDUMP)
	u32 ui32CCBDumpWOff;
#endif
};

enum PVRSRV_ERROR SGXRegisterDevice(struct PVRSRV_DEVICE_NODE *psDeviceNode);
void SysGetSGXTimingInformation(struct SGX_TIMING_INFORMATION *psSGXTimingInfo);
void SGXReset(struct PVRSRV_SGXDEV_INFO *psDevInfo, u32 ui32PDUMPFlags);

#endif
