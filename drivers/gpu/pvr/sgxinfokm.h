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


#define		SGX_HOSTPORT_PRESENT			0x00000001UL

#define PVRSRV_USSE_EDM_POWMAN_IDLE_REQUEST					(1UL << 0)
#define PVRSRV_USSE_EDM_POWMAN_POWEROFF_REQUEST				(1UL << 1)
#define PVRSRV_USSE_EDM_POWMAN_IDLE_COMPLETE				(1UL << 2)
#define PVRSRV_USSE_EDM_POWMAN_POWEROFF_COMPLETE			(1UL << 3)
#define PVRSRV_USSE_EDM_POWMAN_POWEROFF_RESTART_IMMEDIATE	(1UL << 4)
#define PVRSRV_USSE_EDM_POWMAN_NO_WORK						(1UL << 5)

#define PVRSRV_USSE_EDM_INTERRUPT_HWR			(1UL << 0)
#define PVRSRV_USSE_EDM_INTERRUPT_ACTIVE_POWER	(1UL << 1)

#define PVRSRV_USSE_EDM_RESMAN_CLEANUP_RT_REQUEST 	0x01UL
#define PVRSRV_USSE_EDM_RESMAN_CLEANUP_RC_REQUEST 	0x02UL
#define PVRSRV_USSE_EDM_RESMAN_CLEANUP_TC_REQUEST 	0x04UL
#define PVRSRV_USSE_EDM_RESMAN_CLEANUP_2DC_REQUEST 	0x08UL
#define PVRSRV_USSE_EDM_RESMAN_CLEANUP_COMPLETE 	0x10UL
#define PVRSRV_USSE_EDM_RESMAN_CLEANUP_INVALPD		0x20UL
#define PVRSRV_USSE_EDM_RESMAN_CLEANUP_INVALPT		0x40UL

	typedef struct _PVRSRV_SGX_CCB_INFO_ *PPVRSRV_SGX_CCB_INFO;

	typedef struct _PVRSRV_SGXDEV_INFO_ {
		PVRSRV_DEVICE_TYPE eDeviceType;
		PVRSRV_DEVICE_CLASS eDeviceClass;

		IMG_UINT8 ui8VersionMajor;
		IMG_UINT8 ui8VersionMinor;
		IMG_UINT32 ui32CoreConfig;
		IMG_UINT32 ui32CoreFlags;

		IMG_PVOID pvRegsBaseKM;

		IMG_HANDLE hRegMapping;

		IMG_SYS_PHYADDR sRegsPhysBase;

		IMG_UINT32 ui32RegSize;

		IMG_UINT32 ui32CoreClockSpeed;
		IMG_UINT32 ui32uKernelTimerClock;


		IMG_VOID *psStubPBDescListKM;

		IMG_DEV_PHYADDR sKernelPDDevPAddr;

		IMG_VOID *pvDeviceMemoryHeap;
		PPVRSRV_KERNEL_MEM_INFO psKernelCCBMemInfo;
		PVRSRV_SGX_KERNEL_CCB *psKernelCCB;
		PPVRSRV_SGX_CCB_INFO psKernelCCBInfo;
		PPVRSRV_KERNEL_MEM_INFO psKernelCCBCtlMemInfo;
		PVRSRV_SGX_CCB_CTL *psKernelCCBCtl;
		PPVRSRV_KERNEL_MEM_INFO psKernelCCBEventKickerMemInfo;
		IMG_UINT32 *pui32KernelCCBEventKicker;
		IMG_UINT32 ui32TAKickAddress;
		IMG_UINT32 ui32TexLoadKickAddress;
		IMG_UINT32 ui32VideoHandlerAddress;
		IMG_UINT32 ui32KickTACounter;
		IMG_UINT32 ui32KickTARenderCounter;
		PPVRSRV_KERNEL_MEM_INFO psKernelHWPerfCBMemInfo;
		PVRSRV_SGXDEV_DIFF_INFO sDiffInfo;
		IMG_UINT32 ui32HWGroupRequested;
		IMG_UINT32 ui32HWReset;

		IMG_UINT32 ui32ClientRefCount;

		IMG_UINT32 ui32CacheControl;

		IMG_VOID *pvMMUContextList;

		IMG_BOOL bForcePTOff;

		IMG_UINT32 ui32EDMTaskReg0;
		IMG_UINT32 ui32EDMTaskReg1;

		IMG_UINT32 ui32ClkGateCtl;
		IMG_UINT32 ui32ClkGateCtl2;
		IMG_UINT32 ui32ClkGateStatusMask;
		SGX_INIT_SCRIPTS sScripts;

		IMG_HANDLE hBIFResetPDOSMemHandle;
		IMG_DEV_PHYADDR sBIFResetPDDevPAddr;
		IMG_DEV_PHYADDR sBIFResetPTDevPAddr;
		IMG_DEV_PHYADDR sBIFResetPageDevPAddr;
		IMG_UINT32 *pui32BIFResetPD;
		IMG_UINT32 *pui32BIFResetPT;


		IMG_HANDLE hTimer;

		IMG_UINT32 ui32TimeStamp;

		IMG_UINT32 ui32NumResets;

		PVRSRV_KERNEL_MEM_INFO *psKernelSGXHostCtlMemInfo;
		PVRSRV_SGX_HOST_CTL *psSGXHostCtl;

		IMG_UINT32 ui32Flags;

#if defined(PDUMP)
		PVRSRV_SGX_PDUMP_CONTEXT sPDContext;
#endif


		IMG_UINT32 asSGXDevData[SGX_MAX_DEV_DATA];

	} PVRSRV_SGXDEV_INFO;

	typedef struct _SGX_TIMING_INFORMATION_ {
		IMG_UINT32 ui32CoreClockSpeed;
		IMG_UINT32 ui32HWRecoveryFreq;
		IMG_UINT32 ui32ActivePowManLatencyms;
		IMG_UINT32 ui32uKernelFreq;
	} SGX_TIMING_INFORMATION;

	typedef struct _SGX_DEVICE_MAP_ {
		IMG_UINT32 ui32Flags;

		IMG_SYS_PHYADDR sRegsSysPBase;
		IMG_CPU_PHYADDR sRegsCpuPBase;
		IMG_CPU_VIRTADDR pvRegsCpuVBase;
		IMG_UINT32 ui32RegsSize;

		IMG_SYS_PHYADDR sSPSysPBase;
		IMG_CPU_PHYADDR sSPCpuPBase;
		IMG_CPU_VIRTADDR pvSPCpuVBase;
		IMG_UINT32 ui32SPSize;

		IMG_SYS_PHYADDR sLocalMemSysPBase;
		IMG_DEV_PHYADDR sLocalMemDevPBase;
		IMG_CPU_PHYADDR sLocalMemCpuPBase;
		IMG_UINT32 ui32LocalMemSize;

		IMG_UINT32 ui32IRQ;

	} SGX_DEVICE_MAP;

	typedef struct _PVRSRV_STUB_PBDESC_ PVRSRV_STUB_PBDESC;
	struct _PVRSRV_STUB_PBDESC_ {
		IMG_UINT32 ui32RefCount;
		IMG_UINT32 ui32TotalPBSize;
		PVRSRV_KERNEL_MEM_INFO *psSharedPBDescKernelMemInfo;
		PVRSRV_KERNEL_MEM_INFO *psHWPBDescKernelMemInfo;
		PVRSRV_KERNEL_MEM_INFO **ppsSubKernelMemInfos;
		IMG_UINT32 ui32SubKernelMemInfosCount;
		IMG_HANDLE hDevCookie;
		PVRSRV_KERNEL_MEM_INFO *psBlockKernelMemInfo;
		PVRSRV_STUB_PBDESC *psNext;
	};

	typedef struct _PVRSRV_SGX_CCB_INFO_ {
		PVRSRV_KERNEL_MEM_INFO *psCCBMemInfo;
		PVRSRV_KERNEL_MEM_INFO *psCCBCtlMemInfo;
		PVRSRV_SGX_COMMAND *psCommands;
		IMG_UINT32 *pui32WriteOffset;
		volatile IMG_UINT32 *pui32ReadOffset;
#if defined(PDUMP)
		IMG_UINT32 ui32CCBDumpWOff;
#endif
	} PVRSRV_SGX_CCB_INFO;

	PVRSRV_ERROR SGXRegisterDevice(PVRSRV_DEVICE_NODE * psDeviceNode);

	IMG_VOID SGXOSTimer(IMG_VOID * pvData);

	IMG_VOID SysGetSGXTimingInformation(SGX_TIMING_INFORMATION *
					    psSGXTimingInfo);

#endif
