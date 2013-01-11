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

#ifndef AUTOCONF_INCLUDED
#include <linux/config.h>
#endif

#include <linux/version.h>
#include <linux/io.h>
#include <asm/page.h>
#include <asm/system.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/hugetlb.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/pci.h>

#include <linux/string.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/hardirq.h>
#include <linux/timer.h>
#include <linux/capability.h>
#include <linux/uaccess.h>

#include "img_types.h"
#include "services_headers.h"
#include "mm.h"
#include "pvrmmap.h"
#include "mmap.h"
#include "env_data.h"
#include "proc.h"
#include "mutex.h"
#include "event.h"

#define EVENT_OBJECT_TIMEOUT_MS		(100)

#define HOST_ALLOC_MEM_USING_KMALLOC ((void *)0)
#define HOST_ALLOC_MEM_USING_VMALLOC ((void *)1)

#define LINUX_KMALLOC_LIMIT	PAGE_SIZE	/* 4k */

#if !defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
enum PVRSRV_ERROR OSAllocMem(u32 ui32Flags, u32 ui32Size,
			void **ppvCpuVAddr, void **phBlockAlloc)
#else
enum PVRSRV_ERROR _OSAllocMem(u32 ui32Flags, u32 ui32Size,
			 void **ppvCpuVAddr, void **phBlockAlloc,
			 char *pszFilename, u32 ui32Line)
#endif
{
	u32 ui32Threshold;

	PVR_UNREFERENCED_PARAMETER(ui32Flags);

	/* determine whether to go straight to vmalloc */
	ui32Threshold = LINUX_KMALLOC_LIMIT;

	if (ui32Size > ui32Threshold) {
#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
		*ppvCpuVAddr = _VMallocWrapper(ui32Size, PVRSRV_HAP_CACHED,
					       pszFilename, ui32Line);
#else
		*ppvCpuVAddr = VMallocWrapper(ui32Size, PVRSRV_HAP_CACHED);
#endif
		if (!*ppvCpuVAddr)
			return PVRSRV_ERROR_OUT_OF_MEMORY;

		if (phBlockAlloc)
			*phBlockAlloc = HOST_ALLOC_MEM_USING_VMALLOC;
	} else {
		/* default - try kmalloc first */

#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
		*ppvCpuVAddr = _KMallocWrapper(ui32Size, pszFilename, ui32Line);
#else
		*ppvCpuVAddr = KMallocWrapper(ui32Size);
#endif

		if (!*ppvCpuVAddr)
			return PVRSRV_ERROR_OUT_OF_MEMORY;

		if (phBlockAlloc)
			*phBlockAlloc = HOST_ALLOC_MEM_USING_KMALLOC;

	}

	return PVRSRV_OK;
}

#if !defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
void OSFreeMem(u32 ui32Flags, u32 ui32Size,
		       void *pvCpuVAddr, void *hBlockAlloc)
#else
void _OSFreeMem(u32 ui32Flags, u32 ui32Size,
			void *pvCpuVAddr, void *hBlockAlloc,
			char *pszFilename, u32 ui32Line)
#endif
{
	PVR_UNREFERENCED_PARAMETER(ui32Flags);

	if (ui32Size > LINUX_KMALLOC_LIMIT) {
#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
		_VFreeWrapper(pvCpuVAddr, pszFilename, ui32Line);
#else
		VFreeWrapper(pvCpuVAddr);
#endif
	} else {
#if defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
		_KFreeWrapper(pvCpuVAddr, pszFilename, ui32Line);
#else
		KFreeWrapper(pvCpuVAddr);
#endif
	}
}

enum PVRSRV_ERROR OSAllocPages(u32 ui32AllocFlags,
	     u32 ui32Size,
	     void **ppvCpuVAddr, void **phOSMemHandle)
{
	struct LinuxMemArea *psLinuxMemArea;


	switch (ui32AllocFlags & PVRSRV_HAP_MAPTYPE_MASK) {
	case PVRSRV_HAP_KERNEL_ONLY:
		{
			psLinuxMemArea =
			    NewVMallocLinuxMemArea(ui32Size, ui32AllocFlags);
			if (!psLinuxMemArea)
				return PVRSRV_ERROR_OUT_OF_MEMORY;
			break;
		}
	case PVRSRV_HAP_SINGLE_PROCESS:
		{

			psLinuxMemArea =
			    NewAllocPagesLinuxMemArea(ui32Size, ui32AllocFlags);
			if (!psLinuxMemArea)
				return PVRSRV_ERROR_OUT_OF_MEMORY;
			PVRMMapRegisterArea("Import Arena", psLinuxMemArea,
					    ui32AllocFlags);
			break;
		}

	case PVRSRV_HAP_MULTI_PROCESS:
		{
			psLinuxMemArea =
			    NewVMallocLinuxMemArea(ui32Size, ui32AllocFlags);
			if (!psLinuxMemArea)
				return PVRSRV_ERROR_OUT_OF_MEMORY;
			PVRMMapRegisterArea("Import Arena", psLinuxMemArea,
					    ui32AllocFlags);
			break;
		}
	default:
		PVR_DPF(PVR_DBG_ERROR, "OSAllocPages: invalid flags 0x%x\n",
			 ui32AllocFlags);
		*ppvCpuVAddr = NULL;
		*phOSMemHandle = (void *) 0;
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	*ppvCpuVAddr = LinuxMemAreaToCpuVAddr(psLinuxMemArea);
	*phOSMemHandle = psLinuxMemArea;

	LinuxMemAreaRegister(psLinuxMemArea);

	return PVRSRV_OK;
}

enum PVRSRV_ERROR OSFreePages(u32 ui32AllocFlags, u32 ui32Bytes,
	    void *pvCpuVAddr, void *hOSMemHandle)
{
	struct LinuxMemArea *psLinuxMemArea;
	PVR_UNREFERENCED_PARAMETER(pvCpuVAddr);

	psLinuxMemArea = (struct LinuxMemArea *)hOSMemHandle;

	switch (ui32AllocFlags & PVRSRV_HAP_MAPTYPE_MASK) {
	case PVRSRV_HAP_KERNEL_ONLY:
		break;
	case PVRSRV_HAP_SINGLE_PROCESS:
	case PVRSRV_HAP_MULTI_PROCESS:
		if (PVRMMapRemoveRegisteredArea(psLinuxMemArea) != PVRSRV_OK) {
			PVR_DPF(PVR_DBG_ERROR,
				 "OSFreePages(ui32AllocFlags=0x%08X, "
				 "ui32Bytes=%ld, "
				 "pvCpuVAddr=%p, hOSMemHandle=%p) FAILED!",
				 ui32AllocFlags, ui32Bytes, pvCpuVAddr,
				 hOSMemHandle);
			return PVRSRV_ERROR_GENERIC;
		}
		break;
	default:
		PVR_DPF(PVR_DBG_ERROR, "%s: invalid flags 0x%x\n",
			 __func__, ui32AllocFlags);
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	LinuxMemAreaDeepFree(psLinuxMemArea);

	return PVRSRV_OK;
}

enum PVRSRV_ERROR OSGetSubMemHandle(void *hOSMemHandle,
		  u32 ui32ByteOffset,
		  u32 ui32Bytes,
		  u32 ui32Flags, void **phOSMemHandleRet)
{
	struct LinuxMemArea *psParentLinuxMemArea, *psLinuxMemArea;
	enum PVRSRV_ERROR eError = PVRSRV_OK;

	psParentLinuxMemArea = (struct LinuxMemArea *)hOSMemHandle;

	psLinuxMemArea =
	    NewSubLinuxMemArea(psParentLinuxMemArea, ui32ByteOffset, ui32Bytes);
	if (!psLinuxMemArea) {
		*phOSMemHandleRet = NULL;
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
	*phOSMemHandleRet = psLinuxMemArea;

	if (ui32Flags & PVRSRV_HAP_KERNEL_ONLY)
		return PVRSRV_OK;

	if (psParentLinuxMemArea->eAreaType == LINUX_MEM_AREA_IO) {
		eError = PVRMMapRegisterArea("Physical", psLinuxMemArea, 0);
		if (eError != PVRSRV_OK)
			goto failed_register_area;
	} else if (psParentLinuxMemArea->eAreaType ==
		   LINUX_MEM_AREA_ALLOC_PAGES) {
		eError = PVRMMapRegisterArea("Import Arena", psLinuxMemArea, 0);
		if (eError != PVRSRV_OK)
			goto failed_register_area;
	}

	return PVRSRV_OK;

failed_register_area:
	*phOSMemHandleRet = NULL;
	LinuxMemAreaDeepFree(psLinuxMemArea);
	return eError;
}

enum PVRSRV_ERROR OSReleaseSubMemHandle(void *hOSMemHandle, u32 ui32Flags)
{
	struct LinuxMemArea *psParentLinuxMemArea, *psLinuxMemArea;
	enum PVRSRV_ERROR eError;

	psLinuxMemArea = (struct LinuxMemArea *)hOSMemHandle;
	PVR_ASSERT(psLinuxMemArea->eAreaType == LINUX_MEM_AREA_SUB_ALLOC);

	psParentLinuxMemArea =
	    psLinuxMemArea->uData.sSubAlloc.psParentLinuxMemArea;

	if (!(ui32Flags & PVRSRV_HAP_KERNEL_ONLY)
	    && (psParentLinuxMemArea->eAreaType == LINUX_MEM_AREA_IO
		|| psParentLinuxMemArea->eAreaType ==
		LINUX_MEM_AREA_ALLOC_PAGES)
	    ) {
		eError = PVRMMapRemoveRegisteredArea(psLinuxMemArea);
		if (eError != PVRSRV_OK)
			return eError;
	}
	LinuxMemAreaDeepFree(psLinuxMemArea);

	return PVRSRV_OK;
}

struct IMG_CPU_PHYADDR OSMemHandleToCpuPAddr(void *hOSMemHandle,
					     u32 ui32ByteOffset)
{
	PVR_ASSERT(hOSMemHandle);

	return LinuxMemAreaToCpuPAddr(hOSMemHandle, ui32ByteOffset);
}

void OSMemCopy(void *pvDst, void *pvSrc, u32 ui32Size)
{
	memcpy(pvDst, pvSrc, ui32Size);
}

void OSMemSet(void *pvDest, u8 ui8Value, u32 ui32Size)
{
	memset(pvDest, (int)ui8Value, (size_t) ui32Size);
}

char *OSStringCopy(char *pszDest, const char *pszSrc)
{
	return strcpy(pszDest, pszSrc);
}

s32 OSSNPrintf(char *pStr, u32 ui32Size, const char *pszFormat, ...)
{
	va_list argList;
	s32 iCount;

	va_start(argList, pszFormat);
	iCount = vsnprintf(pStr, (size_t) ui32Size, pszFormat, argList);
	va_end(argList);

	return iCount;
}

void OSBreakResourceLock(struct PVRSRV_RESOURCE *psResource, u32 ui32ID)
{
	volatile u32 *pui32Access = (volatile u32 *)&psResource->ui32Lock;

	if (*pui32Access)
		if (psResource->ui32ID == ui32ID) {
			psResource->ui32ID = 0;
			*pui32Access = 0;
		} else {
			PVR_DPF(PVR_DBG_MESSAGE, "OSBreakResourceLock: "
				"Resource is not locked for this process.");
		}
	else
		PVR_DPF(PVR_DBG_MESSAGE,
			 "OSBreakResourceLock: Resource is not locked");
}

enum PVRSRV_ERROR OSCreateResource(struct PVRSRV_RESOURCE *psResource)
{
	psResource->ui32ID = 0;
	psResource->ui32Lock = 0;

	return PVRSRV_OK;
}

enum PVRSRV_ERROR OSDestroyResource(struct PVRSRV_RESOURCE *psResource)
{
	OSBreakResourceLock(psResource, psResource->ui32ID);

	return PVRSRV_OK;
}

enum PVRSRV_ERROR OSInitEnvData(void **ppvEnvSpecificData)
{
	struct ENV_DATA *psEnvData;

	if (OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(struct ENV_DATA),
	     (void *)&psEnvData, NULL) != PVRSRV_OK)
		return PVRSRV_ERROR_GENERIC;

	memset(psEnvData, 0, sizeof(*psEnvData));

	if (OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
	     PVRSRV_MAX_BRIDGE_IN_SIZE + PVRSRV_MAX_BRIDGE_OUT_SIZE,
	     &psEnvData->pvBridgeData, NULL) != PVRSRV_OK) {
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(struct ENV_DATA),
			  psEnvData, NULL);
		return PVRSRV_ERROR_GENERIC;
	}

	psEnvData->bMISRInstalled = IMG_FALSE;
	psEnvData->bLISRInstalled = IMG_FALSE;

	*ppvEnvSpecificData = psEnvData;

	return PVRSRV_OK;
}

enum PVRSRV_ERROR OSDeInitEnvData(void *pvEnvSpecificData)
{
	struct ENV_DATA *psEnvData = (struct ENV_DATA *)pvEnvSpecificData;

	PVR_ASSERT(!psEnvData->bMISRInstalled);
	PVR_ASSERT(!psEnvData->bLISRInstalled);

	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
		  PVRSRV_MAX_BRIDGE_IN_SIZE + PVRSRV_MAX_BRIDGE_OUT_SIZE,
		  psEnvData->pvBridgeData, NULL);

	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(struct ENV_DATA),
		  pvEnvSpecificData, NULL);

	return PVRSRV_OK;
}

void OSReleaseThreadQuanta(void)
{
	schedule();
}

u32 OSClockus(void)
{
	unsigned long time, j = jiffies;

	time = j * (1000000 / HZ);

	return time;
}

void OSWaitus(u32 ui32Timeus)
{
	udelay(ui32Timeus);
}

u32 OSGetCurrentProcessIDKM(void)
{
	if (in_interrupt())
		return KERNEL_ID;
	return task_tgid_nr(current);
}

u32 OSGetPageSize(void)
{
	return PAGE_SIZE;
}

static irqreturn_t DeviceISRWrapper(int irq, void *dev_id)
{
	struct PVRSRV_DEVICE_NODE *psDeviceNode;
	IMG_BOOL bStatus = IMG_FALSE;

	psDeviceNode = (struct PVRSRV_DEVICE_NODE *)dev_id;
	if (!psDeviceNode) {
		PVR_DPF(PVR_DBG_ERROR, "DeviceISRWrapper: invalid params\n");
		goto out;
	}

	bStatus = PVRSRVDeviceLISR(psDeviceNode);

	if (bStatus) {
		struct SYS_DATA *psSysData = psDeviceNode->psSysData;
		struct ENV_DATA *psEnvData =
				(struct ENV_DATA *)psSysData->pvEnvSpecificData;

		queue_work(psEnvData->psMISRWorkqueue, &psEnvData->sMISRWork);
	}

out:
	return bStatus ? IRQ_HANDLED : IRQ_NONE;
}

enum PVRSRV_ERROR OSInstallDeviceLISR(void *pvSysData,
				 u32 ui32Irq,
				 char *pszISRName, void *pvDeviceNode)
{
	struct SYS_DATA *psSysData = (struct SYS_DATA *)pvSysData;
	struct ENV_DATA *psEnvData =
			(struct ENV_DATA *)psSysData->pvEnvSpecificData;

	if (psEnvData->bLISRInstalled) {
		PVR_DPF(PVR_DBG_ERROR, "OSInstallDeviceLISR: "
			"An ISR has already been installed: IRQ %d cookie %x",
			psEnvData->ui32IRQ, psEnvData->pvISRCookie);
		return PVRSRV_ERROR_GENERIC;
	}

	PVR_TRACE("Installing device LISR %s on IRQ %d with cookie %x",
		   pszISRName, ui32Irq, pvDeviceNode);

	if (request_irq(ui32Irq, DeviceISRWrapper, IRQF_SHARED, pszISRName,
			pvDeviceNode)) {
		PVR_DPF(PVR_DBG_ERROR, "OSInstallDeviceLISR: "
				    "Couldn't install device LISR on IRQ %d",
			 ui32Irq);

		return PVRSRV_ERROR_GENERIC;
	}

	psEnvData->ui32IRQ = ui32Irq;
	psEnvData->pvISRCookie = pvDeviceNode;
	psEnvData->bLISRInstalled = IMG_TRUE;

	return PVRSRV_OK;
}

enum PVRSRV_ERROR OSUninstallDeviceLISR(void *pvSysData)
{
	struct SYS_DATA *psSysData = (struct SYS_DATA *)pvSysData;
	struct ENV_DATA *psEnvData =
				(struct ENV_DATA *)psSysData->pvEnvSpecificData;

	if (!psEnvData->bLISRInstalled) {
		PVR_DPF(PVR_DBG_ERROR,
			 "OSUninstallDeviceLISR: No LISR has been installed");
		return PVRSRV_ERROR_GENERIC;
	}

	PVR_TRACE("Uninstalling device LISR on IRQ %d with cookie %x",
		   psEnvData->ui32IRQ, psEnvData->pvISRCookie);

	free_irq(psEnvData->ui32IRQ, psEnvData->pvISRCookie);

	psEnvData->bLISRInstalled = IMG_FALSE;

	return PVRSRV_OK;
}

static void MISRWrapper(struct work_struct *work)
{
	struct ENV_DATA *psEnvData = container_of(work, struct ENV_DATA,
						  sMISRWork);
	struct SYS_DATA *psSysData = (struct SYS_DATA *)psEnvData->pvSysData;
	PVRSRVMISR(psSysData);
}

enum PVRSRV_ERROR OSInstallMISR(void *pvSysData)
{
	struct SYS_DATA *psSysData = (struct SYS_DATA *)pvSysData;
	struct ENV_DATA *psEnvData =
				(struct ENV_DATA *)psSysData->pvEnvSpecificData;

	if (psEnvData->bMISRInstalled) {
		PVR_DPF(PVR_DBG_ERROR,
			 "OSInstallMISR: An MISR has already been installed");
		return PVRSRV_ERROR_GENERIC;
	}

	PVR_TRACE("Installing MISR with cookie %x", pvSysData);

	psEnvData->pvSysData = pvSysData;
	psEnvData->psMISRWorkqueue = create_singlethread_workqueue("sgx_misr");
	INIT_WORK(&psEnvData->sMISRWork, MISRWrapper);

	psEnvData->bMISRInstalled = IMG_TRUE;

	return PVRSRV_OK;
}

enum PVRSRV_ERROR OSUninstallMISR(void *pvSysData)
{
	struct SYS_DATA *psSysData = (struct SYS_DATA *)pvSysData;
	struct ENV_DATA *psEnvData =
				(struct ENV_DATA *)psSysData->pvEnvSpecificData;

	if (!psEnvData->bMISRInstalled) {
		PVR_DPF(PVR_DBG_ERROR,
			 "OSUninstallMISR: No MISR has been installed");
		return PVRSRV_ERROR_GENERIC;
	}

	PVR_TRACE("Uninstalling MISR");

	flush_workqueue(psEnvData->psMISRWorkqueue);
	destroy_workqueue(psEnvData->psMISRWorkqueue);

	psEnvData->bMISRInstalled = IMG_FALSE;

	return PVRSRV_OK;
}

enum PVRSRV_ERROR OSScheduleMISR(void *pvSysData)
{
	struct SYS_DATA *psSysData = (struct SYS_DATA *)pvSysData;
	struct ENV_DATA *psEnvData =
				(struct ENV_DATA *)psSysData->pvEnvSpecificData;

	if (psEnvData->bMISRInstalled)
		queue_work(psEnvData->psMISRWorkqueue, &psEnvData->sMISRWork);

	return PVRSRV_OK;
}


#define	OS_TAS(p)	xchg((p), 1)
enum PVRSRV_ERROR OSLockResource(struct PVRSRV_RESOURCE *psResource, u32 ui32ID)
{
	enum PVRSRV_ERROR eError = PVRSRV_OK;

	if (!OS_TAS(&psResource->ui32Lock))
		psResource->ui32ID = ui32ID;
	else
		eError = PVRSRV_ERROR_GENERIC;

	return eError;
}

enum PVRSRV_ERROR OSUnlockResource(struct PVRSRV_RESOURCE *psResource,
				   u32 ui32ID)
{
	volatile u32 *pui32Access = (volatile u32 *)&psResource->ui32Lock;
	enum PVRSRV_ERROR eError = PVRSRV_OK;

	if (*pui32Access) {
		if (psResource->ui32ID == ui32ID) {
			psResource->ui32ID = 0;
			*pui32Access = 0;
		} else {
			PVR_DPF(PVR_DBG_ERROR, "OSUnlockResource: "
			       "Resource %p is not locked with expected value.",
				psResource);
			PVR_DPF(PVR_DBG_MESSAGE, "Should be %x is actually %x",
				 ui32ID, psResource->ui32ID);
			eError = PVRSRV_ERROR_GENERIC;
		}
	} else {
		PVR_DPF(PVR_DBG_ERROR,
			 "OSUnlockResource: Resource %p is not locked",
			 psResource);
		eError = PVRSRV_ERROR_GENERIC;
	}

	return eError;
}

struct IMG_CPU_PHYADDR OSMapLinToCPUPhys(void *pvLinAddr)
{
	struct IMG_CPU_PHYADDR CpuPAddr;

	CpuPAddr.uiAddr = (u32) VMallocToPhys(pvLinAddr);

	return CpuPAddr;
}

void __iomem *OSMapPhysToLin(struct IMG_CPU_PHYADDR BasePAddr, u32 ui32Bytes,
			 u32 ui32MappingFlags, void **phOSMemHandle)
{
	if (phOSMemHandle)
		*phOSMemHandle = (void *) 0;

	if (ui32MappingFlags & PVRSRV_HAP_KERNEL_ONLY) {
		void __iomem *pvIORemapCookie;
		pvIORemapCookie =
		    IORemapWrapper(BasePAddr, ui32Bytes, ui32MappingFlags);
		if (pvIORemapCookie == NULL)
			return NULL;
		return pvIORemapCookie;
	} else {
		PVR_DPF(PVR_DBG_ERROR,
			 "OSMapPhysToLin should only be used with "
			 "PVRSRV_HAP_KERNEL_ONLY  "
			 "(Use OSReservePhys otherwise)");
		*phOSMemHandle = (void *) 0;
		return NULL;
	}

	PVR_ASSERT(0);
	return NULL;
}

IMG_BOOL
OSUnMapPhysToLin(void __iomem *pvLinAddr, u32 ui32Bytes,
		 u32 ui32MappingFlags, void *hPageAlloc)
{
	PVR_TRACE("%s: unmapping %d bytes from 0x%08x", __func__,
		   ui32Bytes, pvLinAddr);

	PVR_UNREFERENCED_PARAMETER(hPageAlloc);

	if (ui32MappingFlags & PVRSRV_HAP_KERNEL_ONLY) {
		IOUnmapWrapper(pvLinAddr);
		return IMG_TRUE;
	} else {
		PVR_DPF(PVR_DBG_ERROR,
			 "OSUnMapPhysToLin should only be used with "
			 "PVRSRV_HAP_KERNEL_ONLY "
			 " (Use OSUnReservePhys otherwise)");
		return IMG_FALSE;
	}

	PVR_ASSERT(0);
	return IMG_FALSE;
}

static enum PVRSRV_ERROR RegisterExternalMem(struct IMG_SYS_PHYADDR *pBasePAddr,
		    void *pvCPUVAddr, u32 ui32Bytes,
		    IMG_BOOL bPhysContig, u32 ui32MappingFlags,
		    void **phOSMemHandle)
{
	struct LinuxMemArea *psLinuxMemArea;

	switch (ui32MappingFlags & PVRSRV_HAP_MAPTYPE_MASK) {
	case PVRSRV_HAP_KERNEL_ONLY:
		{
			psLinuxMemArea =
			    NewExternalKVLinuxMemArea(pBasePAddr, pvCPUVAddr,
						      ui32Bytes, bPhysContig,
						      ui32MappingFlags);

			if (!psLinuxMemArea)
				return PVRSRV_ERROR_GENERIC;
			break;
		}
	case PVRSRV_HAP_SINGLE_PROCESS:
		{
			psLinuxMemArea =
			    NewExternalKVLinuxMemArea(pBasePAddr, pvCPUVAddr,
						      ui32Bytes, bPhysContig,
						      ui32MappingFlags);

			if (!psLinuxMemArea)
				return PVRSRV_ERROR_GENERIC;
			PVRMMapRegisterArea("Physical", psLinuxMemArea,
					    ui32MappingFlags);
			break;
		}
	case PVRSRV_HAP_MULTI_PROCESS:
		{
			psLinuxMemArea =
			    NewExternalKVLinuxMemArea(pBasePAddr, pvCPUVAddr,
						      ui32Bytes, bPhysContig,
						      ui32MappingFlags);

			if (!psLinuxMemArea)
				return PVRSRV_ERROR_GENERIC;
			PVRMMapRegisterArea("Physical", psLinuxMemArea,
					    ui32MappingFlags);
			break;
		}
	default:
		PVR_DPF(PVR_DBG_ERROR, "OSRegisterMem : invalid flags 0x%x\n",
			 ui32MappingFlags);
		*phOSMemHandle = (void *) 0;
		return PVRSRV_ERROR_GENERIC;
	}

	*phOSMemHandle = (void *) psLinuxMemArea;

	LinuxMemAreaRegister(psLinuxMemArea);

	return PVRSRV_OK;
}

enum PVRSRV_ERROR OSRegisterMem(struct IMG_CPU_PHYADDR BasePAddr,
	      void *pvCPUVAddr,
	      u32 ui32Bytes,
	      u32 ui32MappingFlags, void **phOSMemHandle)
{
	struct IMG_SYS_PHYADDR SysPAddr = SysCpuPAddrToSysPAddr(BasePAddr);

	return RegisterExternalMem(&SysPAddr, pvCPUVAddr, ui32Bytes, IMG_TRUE,
				   ui32MappingFlags, phOSMemHandle);
}

enum PVRSRV_ERROR OSRegisterDiscontigMem(struct IMG_SYS_PHYADDR *pBasePAddr,
				    void *pvCPUVAddr, u32 ui32Bytes,
				    u32 ui32MappingFlags,
				    void **phOSMemHandle)
{
	return RegisterExternalMem(pBasePAddr, pvCPUVAddr, ui32Bytes,
				   IMG_FALSE, ui32MappingFlags, phOSMemHandle);
}

enum PVRSRV_ERROR OSUnRegisterMem(void *pvCpuVAddr,
		u32 ui32Bytes,
		u32 ui32MappingFlags, void *hOSMemHandle)
{
	struct LinuxMemArea *psLinuxMemArea = (struct LinuxMemArea *)
								hOSMemHandle;

	PVR_UNREFERENCED_PARAMETER(pvCpuVAddr);

	switch (ui32MappingFlags & PVRSRV_HAP_MAPTYPE_MASK) {
	case PVRSRV_HAP_KERNEL_ONLY:
		break;
	case PVRSRV_HAP_SINGLE_PROCESS:
	case PVRSRV_HAP_MULTI_PROCESS:
		{
			if (PVRMMapRemoveRegisteredArea(psLinuxMemArea) !=
			    PVRSRV_OK) {
				PVR_DPF(PVR_DBG_ERROR,
					 "%s(%p, %d, 0x%08X, %p) FAILED!",
					 __func__, pvCpuVAddr, ui32Bytes,
					 ui32MappingFlags, hOSMemHandle);
				return PVRSRV_ERROR_GENERIC;
			}
			break;
		}
	default:
		{
			PVR_DPF(PVR_DBG_ERROR,
				 "OSUnRegisterMem : invalid flags 0x%x",
				 ui32MappingFlags);
			return PVRSRV_ERROR_INVALID_PARAMS;
		}
	}

	LinuxMemAreaDeepFree(psLinuxMemArea);

	return PVRSRV_OK;
}

enum PVRSRV_ERROR OSUnRegisterDiscontigMem(void *pvCpuVAddr, u32 ui32Bytes,
				      u32 ui32Flags, void *hOSMemHandle)
{
	return OSUnRegisterMem(pvCpuVAddr, ui32Bytes, ui32Flags, hOSMemHandle);
}

enum PVRSRV_ERROR OSReservePhys(struct IMG_CPU_PHYADDR BasePAddr,
	      u32 ui32Bytes, u32 ui32MappingFlags, void **ppvCpuVAddr,
	      void **phOSMemHandle)
{
	struct LinuxMemArea *psLinuxMemArea;

	switch (ui32MappingFlags & PVRSRV_HAP_MAPTYPE_MASK) {
	case PVRSRV_HAP_KERNEL_ONLY:
		{

			psLinuxMemArea =
			    NewIORemapLinuxMemArea(BasePAddr, ui32Bytes,
						   ui32MappingFlags);
			if (!psLinuxMemArea)
				return PVRSRV_ERROR_GENERIC;
			break;
		}
	case PVRSRV_HAP_SINGLE_PROCESS:
		{
			psLinuxMemArea =
			    NewIOLinuxMemArea(BasePAddr, ui32Bytes,
					      ui32MappingFlags);
			if (!psLinuxMemArea)
				return PVRSRV_ERROR_GENERIC;
			PVRMMapRegisterArea("Physical", psLinuxMemArea,
					    ui32MappingFlags);
			break;
		}
	case PVRSRV_HAP_MULTI_PROCESS:
		{
			psLinuxMemArea =
			    NewIORemapLinuxMemArea(BasePAddr, ui32Bytes,
						   ui32MappingFlags);
			if (!psLinuxMemArea)
				return PVRSRV_ERROR_GENERIC;
			PVRMMapRegisterArea("Physical", psLinuxMemArea,
					    ui32MappingFlags);
			break;
		}
	default:
		PVR_DPF(PVR_DBG_ERROR, "OSMapPhysToLin : invalid flags 0x%x\n",
			 ui32MappingFlags);
		*ppvCpuVAddr = NULL;
		*phOSMemHandle = (void *) 0;
		return PVRSRV_ERROR_GENERIC;
	}

	*phOSMemHandle = (void *) psLinuxMemArea;
	*ppvCpuVAddr = LinuxMemAreaToCpuVAddr(psLinuxMemArea);

	LinuxMemAreaRegister(psLinuxMemArea);

	return PVRSRV_OK;
}

enum PVRSRV_ERROR OSUnReservePhys(void *pvCpuVAddr,
		u32 ui32Bytes, u32 ui32MappingFlags, void *hOSMemHandle)
{
	struct LinuxMemArea *psLinuxMemArea;
	PVR_UNREFERENCED_PARAMETER(pvCpuVAddr);

	psLinuxMemArea = (struct LinuxMemArea *)hOSMemHandle;

	switch (ui32MappingFlags & PVRSRV_HAP_MAPTYPE_MASK) {
	case PVRSRV_HAP_KERNEL_ONLY:
		break;
	case PVRSRV_HAP_SINGLE_PROCESS:
	case PVRSRV_HAP_MULTI_PROCESS:
		{
			if (PVRMMapRemoveRegisteredArea(psLinuxMemArea) !=
			    PVRSRV_OK) {
				PVR_DPF(PVR_DBG_ERROR,
					 "%s(%p, %d, 0x%08X, %p) FAILED!",
					 __func__, pvCpuVAddr, ui32Bytes,
					 ui32MappingFlags, hOSMemHandle);
				return PVRSRV_ERROR_GENERIC;
			}
			break;
		}
	default:
		{
			PVR_DPF(PVR_DBG_ERROR,
				 "OSUnMapPhysToLin : invalid flags 0x%x",
				 ui32MappingFlags);
			return PVRSRV_ERROR_INVALID_PARAMS;
		}
	}

	LinuxMemAreaDeepFree(psLinuxMemArea);

	return PVRSRV_OK;
}

enum PVRSRV_ERROR OSBaseAllocContigMemory(u32 ui32Size, void **pvLinAddr,
				     struct IMG_CPU_PHYADDR *psPhysAddr)
{
	PVR_UNREFERENCED_PARAMETER(ui32Size);
	PVR_UNREFERENCED_PARAMETER(pvLinAddr);
	PVR_UNREFERENCED_PARAMETER(psPhysAddr);
	PVR_DPF(PVR_DBG_ERROR, "%s: Not available", __func__);

	return PVRSRV_ERROR_OUT_OF_MEMORY;
}

enum PVRSRV_ERROR OSBaseFreeContigMemory(u32 ui32Size, void *pvLinAddr,
				    struct IMG_CPU_PHYADDR psPhysAddr)
{
	PVR_UNREFERENCED_PARAMETER(ui32Size);
	PVR_UNREFERENCED_PARAMETER(pvLinAddr);
	PVR_UNREFERENCED_PARAMETER(psPhysAddr);

	PVR_DPF(PVR_DBG_WARNING, "%s: Not available", __func__);
	return PVRSRV_OK;
}

u32 OSReadHWReg(void __iomem *pvLinRegBaseAddr, u32 ui32Offset)
{
	return (u32)readl(pvLinRegBaseAddr + ui32Offset);
}

void OSWriteHWReg(void __iomem *pvLinRegBaseAddr, u32 ui32Offset, u32 ui32Value)
{
	writel(ui32Value, pvLinRegBaseAddr + ui32Offset);
}

struct TIMER_CALLBACK_DATA {
	void (*pfnTimerFunc)(void *);
	void *pvData;
	struct timer_list sTimer;
	u32 ui32Delay;
	IMG_BOOL bActive;
};

static void OSTimerCallbackWrapper(unsigned long ui32Data)
{
	struct TIMER_CALLBACK_DATA *psTimerCBData =
					(struct TIMER_CALLBACK_DATA *)ui32Data;

	if (!psTimerCBData->bActive)
		return;

	psTimerCBData->pfnTimerFunc(psTimerCBData->pvData);

	mod_timer(&psTimerCBData->sTimer, psTimerCBData->ui32Delay + jiffies);
}

void *OSAddTimer(void (*pfnTimerFunc)(void *), void *pvData, u32 ui32MsTimeout)
{
	struct TIMER_CALLBACK_DATA *psTimerCBData;

	if (!pfnTimerFunc) {
		PVR_DPF(PVR_DBG_ERROR, "OSAddTimer: passed invalid callback");
		return NULL;
	}

	if (OSAllocMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
		       sizeof(struct TIMER_CALLBACK_DATA),
		       (void **) &psTimerCBData, NULL) != PVRSRV_OK) {
		PVR_DPF(PVR_DBG_ERROR,
			 "OSAddTimer: failed to allocate memory");
		return NULL;
	}

	psTimerCBData->pfnTimerFunc = pfnTimerFunc;
	psTimerCBData->pvData = pvData;
	psTimerCBData->bActive = IMG_FALSE;

	psTimerCBData->ui32Delay = ((HZ * ui32MsTimeout) < 1000)
	    ? 1 : ((HZ * ui32MsTimeout) / 1000);

	init_timer(&psTimerCBData->sTimer);

	psTimerCBData->sTimer.function = OSTimerCallbackWrapper;
	psTimerCBData->sTimer.data = (u32) psTimerCBData;
	psTimerCBData->sTimer.expires = psTimerCBData->ui32Delay + jiffies;

	return (void *)psTimerCBData;
}

enum PVRSRV_ERROR OSRemoveTimer(void *hTimer)
{
	struct TIMER_CALLBACK_DATA *psTimerCBData =
					(struct TIMER_CALLBACK_DATA *)hTimer;

	OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
		  sizeof(struct TIMER_CALLBACK_DATA),
		  psTimerCBData, NULL);

	return PVRSRV_OK;
}

enum PVRSRV_ERROR OSEnableTimer(void *hTimer)
{
	struct TIMER_CALLBACK_DATA *psTimerCBData =
					(struct TIMER_CALLBACK_DATA *)hTimer;

	psTimerCBData->bActive = IMG_TRUE;

	psTimerCBData->sTimer.expires = psTimerCBData->ui32Delay + jiffies;
	add_timer(&psTimerCBData->sTimer);

	return PVRSRV_OK;
}

enum PVRSRV_ERROR OSDisableTimer(void *hTimer)
{
	struct TIMER_CALLBACK_DATA *psTimerCBData =
					(struct TIMER_CALLBACK_DATA *)hTimer;

	psTimerCBData->bActive = IMG_FALSE;

	del_timer_sync(&psTimerCBData->sTimer);

	return PVRSRV_OK;
}

enum PVRSRV_ERROR OSEventObjectCreate(const char *pszName,
				 struct PVRSRV_EVENTOBJECT *psEventObject)
{
	enum PVRSRV_ERROR eError = PVRSRV_OK;

	if (psEventObject) {
		if (pszName) {

			strncpy(psEventObject->szName, pszName,
				EVENTOBJNAME_MAXLENGTH);
		} else {

			static u16 ui16NameIndex;
			snprintf(psEventObject->szName, EVENTOBJNAME_MAXLENGTH,
				 "PVRSRV_EVENTOBJECT_%d", ui16NameIndex++);
		}

		if (LinuxEventObjectListCreate(&psEventObject->hOSEventKM) !=
		    PVRSRV_OK)
			eError = PVRSRV_ERROR_OUT_OF_MEMORY;

	} else {
		PVR_DPF(PVR_DBG_ERROR, "OSEventObjectCreate: "
			"psEventObject is not a valid pointer");
		eError = PVRSRV_ERROR_GENERIC;
	}

	return eError;

}

enum PVRSRV_ERROR OSEventObjectDestroy(struct PVRSRV_EVENTOBJECT *psEventObject)
{
	enum PVRSRV_ERROR eError = PVRSRV_OK;

	if (psEventObject) {
		if (psEventObject->hOSEventKM) {
			LinuxEventObjectListDestroy(psEventObject->hOSEventKM);
		} else {
			PVR_DPF(PVR_DBG_ERROR, "OSEventObjectDestroy: "
				"hOSEventKM is not a valid pointer");
			eError = PVRSRV_ERROR_INVALID_PARAMS;
		}
	} else {
		PVR_DPF(PVR_DBG_ERROR, "OSEventObjectDestroy: "
				"psEventObject is not a valid pointer");
		eError = PVRSRV_ERROR_INVALID_PARAMS;
	}

	return eError;
}

enum PVRSRV_ERROR OSEventObjectWait(void *hOSEventKM)
{
	enum PVRSRV_ERROR eError = PVRSRV_OK;

	if (hOSEventKM) {
		eError =
		    LinuxEventObjectWait(hOSEventKM, EVENT_OBJECT_TIMEOUT_MS);
	} else {
		PVR_DPF(PVR_DBG_ERROR, "OSEventObjectWait: "
			"hOSEventKM is not a valid handle");
		eError = PVRSRV_ERROR_INVALID_PARAMS;
	}

	return eError;
}

enum PVRSRV_ERROR OSEventObjectOpen(struct PVRSRV_EVENTOBJECT *psEventObject,
			       void **phOSEvent)
{
	enum PVRSRV_ERROR eError = PVRSRV_OK;

	if (psEventObject) {
		if (LinuxEventObjectAdd(psEventObject->hOSEventKM, phOSEvent) !=
		    PVRSRV_OK) {
			PVR_DPF(PVR_DBG_ERROR, "LinuxEventObjectAdd: failed");
			eError = PVRSRV_ERROR_INVALID_PARAMS;
		}

	} else {
		PVR_DPF(PVR_DBG_ERROR, "OSEventObjectCreate: "
			"psEventObject is not a valid pointer");
		eError = PVRSRV_ERROR_INVALID_PARAMS;
	}

	return eError;
}

enum PVRSRV_ERROR OSEventObjectClose(struct PVRSRV_EVENTOBJECT *psEventObject,
				void *hOSEventKM)
{
	enum PVRSRV_ERROR eError = PVRSRV_OK;

	if (psEventObject) {
		if (LinuxEventObjectDelete
		    (psEventObject->hOSEventKM, hOSEventKM) != PVRSRV_OK) {
			PVR_DPF(PVR_DBG_ERROR,
				 "LinuxEventObjectDelete: failed");
			eError = PVRSRV_ERROR_INVALID_PARAMS;
		}

	} else {
		PVR_DPF(PVR_DBG_ERROR, "OSEventObjectDestroy: "
				"psEventObject is not a valid pointer");
		eError = PVRSRV_ERROR_INVALID_PARAMS;
	}

	return eError;

}

enum PVRSRV_ERROR OSEventObjectSignal(void *hOSEventKM)
{
	enum PVRSRV_ERROR eError = PVRSRV_OK;

	if (hOSEventKM) {
		eError = LinuxEventObjectSignal(hOSEventKM);
	} else {
		PVR_DPF(PVR_DBG_ERROR, "OSEventObjectSignal: "
			"hOSEventKM is not a valid handle");
		eError = PVRSRV_ERROR_INVALID_PARAMS;
	}

	return eError;
}

IMG_BOOL OSProcHasPrivSrvInit(void)
{
	return capable(CAP_SYS_MODULE) != 0;
}

enum PVRSRV_ERROR OSCopyToUser(void *pvProcess, void __user *pvDest,
			       const void *pvSrc, u32 ui32Bytes)
{
	PVR_UNREFERENCED_PARAMETER(pvProcess);

	if (copy_to_user(pvDest, pvSrc, ui32Bytes) == 0)
		return PVRSRV_OK;
	else
		return PVRSRV_ERROR_GENERIC;
}

enum PVRSRV_ERROR OSCopyFromUser(void *pvProcess, void *pvDest,
				 const void __user *pvSrc, u32 ui32Bytes)
{
	PVR_UNREFERENCED_PARAMETER(pvProcess);

	if (copy_from_user(pvDest, pvSrc, ui32Bytes) == 0)
		return PVRSRV_OK;
	else
		return PVRSRV_ERROR_GENERIC;
}

IMG_BOOL OSAccessOK(enum IMG_VERIFY_TEST eVerification,
		    const void __user *pvUserPtr, u32 ui32Bytes)
{
	int linuxType;

	if (eVerification == PVR_VERIFY_READ)
		linuxType = VERIFY_READ;
	else if (eVerification == PVR_VERIFY_WRITE)
		linuxType = VERIFY_WRITE;
	else {
		PVR_DPF(PVR_DBG_ERROR, "%s: Unknown eVerification", __func__);
		return PVRSRV_ERROR_GENERIC;
	}
	return (IMG_BOOL)access_ok(linuxType, pvUserPtr, ui32Bytes);
}

enum eWrapMemType {
	WRAP_TYPE_CLEANUP,
	WRAP_TYPE_GET_USER_PAGES,
	WRAP_TYPE_FIND_VMA_PAGES,
	WRAP_TYPE_FIND_VMA_PFN
};

struct sWrapMemInfo {
	enum eWrapMemType eType;
	int iNumPages;
	struct page **ppsPages;
	struct IMG_SYS_PHYADDR *psPhysAddr;
	int iPageOffset;
	int iContiguous;
#if defined(DEBUG)
	unsigned long ulStartAddr;
	unsigned long ulBeyondEndAddr;
	struct vm_area_struct *psVMArea;
#endif
};

static void CheckPagesContiguous(struct sWrapMemInfo *psInfo)
{
	unsigned ui;
	u32 ui32AddrChk;

	BUG_ON(psInfo == NULL);

	psInfo->iContiguous = 1;

	for (ui = 0, ui32AddrChk = psInfo->psPhysAddr[0].uiAddr;
	     ui < psInfo->iNumPages; ui++, ui32AddrChk += PAGE_SIZE)
		if (psInfo->psPhysAddr[ui].uiAddr != ui32AddrChk) {
			psInfo->iContiguous = 0;
			break;
		}
}

static struct page *CPUVAddrToPage(struct vm_area_struct *psVMArea,
				   unsigned long ulCPUVAddr)
{
	pgd_t *psPGD;
	pud_t *psPUD;
	pmd_t *psPMD;
	pte_t *psPTE;
	struct mm_struct *psMM = psVMArea->vm_mm;
	unsigned long ulPFN;
	spinlock_t *psPTLock;
	struct page *psPage;

	psPGD = pgd_offset(psMM, ulCPUVAddr);
	if (pgd_none(*psPGD) || pgd_bad(*psPGD))
		return NULL;

	psPUD = pud_offset(psPGD, ulCPUVAddr);
	if (pud_none(*psPUD) || pud_bad(*psPUD))
		return NULL;

	psPMD = pmd_offset(psPUD, ulCPUVAddr);
	if (pmd_none(*psPMD) || pmd_bad(*psPMD))
		return NULL;

	psPage = NULL;

	psPTE = pte_offset_map_lock(psMM, psPMD, ulCPUVAddr, &psPTLock);
	if (pte_none(*psPTE) || !pte_present(*psPTE) || !pte_write(*psPTE))
		goto exit_unlock;

	ulPFN = pte_pfn(*psPTE);
	if (!pfn_valid(ulPFN))
		goto exit_unlock;

	psPage = pfn_to_page(ulPFN);

	get_page(psPage);

exit_unlock:
	pte_unmap_unlock(psPTE, psPTLock);

	return psPage;
}

enum PVRSRV_ERROR OSReleasePhysPageAddr(void *hOSWrapMem, IMG_BOOL bUseLock)
{
	struct sWrapMemInfo *psInfo = (struct sWrapMemInfo *)hOSWrapMem;
	unsigned ui;

	BUG_ON(psInfo == NULL);

#if defined(DEBUG)
	switch (psInfo->eType) {
	case WRAP_TYPE_FIND_VMA_PAGES:

	case WRAP_TYPE_FIND_VMA_PFN:
		{
			struct vm_area_struct *psVMArea;

			if (bUseLock)
				down_read(&current->mm->mmap_sem);

			psVMArea = find_vma(current->mm, psInfo->ulStartAddr);
			if (psVMArea == NULL) {
				printk(KERN_WARNING ": OSCpuVToPageListRelease:"
		 " Couldn't find memory region containing start address %lx",
		 psInfo->ulStartAddr);

				if (bUseLock)
					up_read(&current->mm->mmap_sem);

				break;
			}

			if (psInfo->psVMArea != psVMArea)
				printk(KERN_WARNING ": OSCpuVToPageListRelease:"
					" vm_area_struct has a different "
					"address from the one used in "
					"ImportMem (%p != %p)",
				       psVMArea, psInfo->psVMArea);

			if (psInfo->ulStartAddr < psVMArea->vm_start)
				printk(KERN_WARNING ": OSCpuVToPageListRelease:"
					" Start address %lx is outside of "
					"the region returned by find_vma",
				       psInfo->ulStartAddr);

			if (psInfo->ulBeyondEndAddr > psVMArea->vm_end)
				printk(KERN_WARNING ": OSCpuVToPageListRelease:"
					" End address %lx is outside of the "
					"region returned by find_vma",
				       psInfo->ulBeyondEndAddr);

			if ((psVMArea->vm_flags & (VM_IO | VM_DONTDUMP)) !=
			    (VM_IO | VM_DONTDUMP))
				printk(KERN_WARNING ": OSCpuVToPageListRelease:"
					" Memory region does not represent "
					"memory mapped I/O (VMA flags: 0x%lx)",
				       psVMArea->vm_flags);

			if ((psVMArea->vm_flags & (VM_READ | VM_WRITE)) !=
			    (VM_READ | VM_WRITE))
				printk(KERN_WARNING ": OSCpuVToPageListRelease:"
					" OSWrapMemReleasePages: "
					"No read/write access to memory region "
					"(VMA flags: 0x%lx)",
				       psVMArea->vm_flags);

			if (bUseLock)
				up_read(&current->mm->mmap_sem);

			break;
		}
	default:
		break;
	}
#endif

	switch (psInfo->eType) {
	case WRAP_TYPE_CLEANUP:
		break;
	case WRAP_TYPE_FIND_VMA_PFN:
		break;
	case WRAP_TYPE_GET_USER_PAGES:
		{
			for (ui = 0; ui < psInfo->iNumPages; ui++) {
				struct page *psPage = psInfo->ppsPages[ui];

				if (!PageReserved(psPage))
					;
				{
					SetPageDirty(psPage);
				}
				page_cache_release(psPage);
			}
			break;
		}
	case WRAP_TYPE_FIND_VMA_PAGES:
		{
			for (ui = 0; ui < psInfo->iNumPages; ui++)
				put_page_testzero(psInfo->ppsPages[ui]);
			break;
		}
	default:
		{
			printk(KERN_WARNING ": OSCpuVToPageListRelease: "
			       "Unknown wrap type (%d)", psInfo->eType);
			return PVRSRV_ERROR_GENERIC;
		}
	}

	if (psInfo->ppsPages != NULL)
		kfree(psInfo->ppsPages);

	if (psInfo->psPhysAddr != NULL)
		kfree(psInfo->psPhysAddr);

	kfree(psInfo);

	return PVRSRV_OK;
}

enum PVRSRV_ERROR OSAcquirePhysPageAddr(void *pvCPUVAddr,
				   u32 ui32Bytes,
				   struct IMG_SYS_PHYADDR *psSysPAddr,
				   void **phOSWrapMem,
				   IMG_BOOL bUseLock)
{
	unsigned long ulStartAddrOrig = (unsigned long)pvCPUVAddr;
	unsigned long ulAddrRangeOrig = (unsigned long)ui32Bytes;
	unsigned long ulBeyondEndAddrOrig = ulStartAddrOrig + ulAddrRangeOrig;
	unsigned long ulStartAddr;
	unsigned long ulAddrRange;
	unsigned long ulBeyondEndAddr;
	unsigned long ulAddr;
	int iNumPagesMapped;
	unsigned ui;
	struct vm_area_struct *psVMArea;
	struct sWrapMemInfo *psInfo;

	ulStartAddr = ulStartAddrOrig & PAGE_MASK;
	ulBeyondEndAddr = PAGE_ALIGN(ulBeyondEndAddrOrig);
	ulAddrRange = ulBeyondEndAddr - ulStartAddr;

	psInfo = kmalloc(sizeof(*psInfo), GFP_KERNEL);
	if (psInfo == NULL) {
		printk(KERN_WARNING ": OSCpuVToPageList: "
				"Couldn't allocate information structure\n");
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
	memset(psInfo, 0, sizeof(*psInfo));

#if defined(DEBUG)
	psInfo->ulStartAddr = ulStartAddrOrig;
	psInfo->ulBeyondEndAddr = ulBeyondEndAddrOrig;
#endif

	psInfo->iNumPages = ulAddrRange >> PAGE_SHIFT;
	psInfo->iPageOffset = ulStartAddrOrig & ~PAGE_MASK;

	psInfo->psPhysAddr =
	    kmalloc(psInfo->iNumPages * sizeof(*psInfo->psPhysAddr),
		    GFP_KERNEL);
	if (psInfo->psPhysAddr == NULL) {
		printk(KERN_WARNING
		       ": OSCpuVToPageList: Couldn't allocate page array\n");
		goto error_free;
	}

	psInfo->ppsPages =
	    kmalloc(psInfo->iNumPages * sizeof(*psInfo->ppsPages), GFP_KERNEL);
	if (psInfo->ppsPages == NULL) {
		printk(KERN_WARNING
		       ": OSCpuVToPageList: Couldn't allocate page array\n");
		goto error_free;
	}

	if (bUseLock)
		down_read(&current->mm->mmap_sem);

	iNumPagesMapped =
	    get_user_pages(current, current->mm, ulStartAddr, psInfo->iNumPages,
			   1, 0, psInfo->ppsPages, NULL);

	if (bUseLock)
		up_read(&current->mm->mmap_sem);


	if (iNumPagesMapped >= 0) {

		if (iNumPagesMapped != psInfo->iNumPages) {
			printk(KERN_WARNING ": OSCpuVToPageList: Couldn't "
				"map all the pages needed "
				"(wanted: %d, got %d \n)",
			       psInfo->iNumPages, iNumPagesMapped);

			for (ui = 0; ui < iNumPagesMapped; ui++)
				page_cache_release(psInfo->ppsPages[ui]);

			goto error_free;
		}

		for (ui = 0; ui < psInfo->iNumPages; ui++) {
			struct IMG_CPU_PHYADDR CPUPhysAddr;

			CPUPhysAddr.uiAddr =
			    page_to_pfn(psInfo->ppsPages[ui]) << PAGE_SHIFT;
			psInfo->psPhysAddr[ui] =
			    SysCpuPAddrToSysPAddr(CPUPhysAddr);
			psSysPAddr[ui] = psInfo->psPhysAddr[ui];

		}

		psInfo->eType = WRAP_TYPE_GET_USER_PAGES;

		goto exit_check;
	}

	printk(KERN_WARNING ": OSCpuVToPageList: get_user_pages failed (%d), "
			    "trying something else \n",
			    iNumPagesMapped);

	if (bUseLock)
		down_read(&current->mm->mmap_sem);

	psVMArea = find_vma(current->mm, ulStartAddrOrig);
	if (psVMArea == NULL) {
		printk(KERN_WARNING ": OSCpuVToPageList: "
				"Couldn't find memory region containing "
				"start address %lx \n",
		       ulStartAddrOrig);

		goto error_release_mmap_sem;
	}
#if defined(DEBUG)
	psInfo->psVMArea = psVMArea;
#endif

	if (ulStartAddrOrig < psVMArea->vm_start) {
		printk(KERN_WARNING ": OSCpuVToPageList: "
				"Start address %lx is outside of the "
				"region returned by find_vma\n",
		       ulStartAddrOrig);
		goto error_release_mmap_sem;
	}

	if (ulBeyondEndAddrOrig > psVMArea->vm_end) {
		printk(KERN_WARNING ": OSCpuVToPageList: "
				"End address %lx is outside of the "
				"region returned by find_vma\n",
		       ulBeyondEndAddrOrig);
		goto error_release_mmap_sem;
	}

	if ((psVMArea->vm_flags & (VM_IO | VM_DONTDUMP)) !=
	    (VM_IO | VM_DONTDUMP)) {
		printk(KERN_WARNING ": OSCpuVToPageList: "
				"Memory region does not represent "
				"memory mapped I/O (VMA flags: 0x%lx)\n",
		       psVMArea->vm_flags);
		goto error_release_mmap_sem;
	}

	if ((psVMArea->vm_flags & (VM_READ | VM_WRITE)) !=
	    (VM_READ | VM_WRITE)) {
		printk(KERN_WARNING ": OSCpuVToPageList: "
			"No read/write access to memory region "
			"(VMA flags: 0x%lx)\n",
		       psVMArea->vm_flags);
		goto error_release_mmap_sem;
	}

	for (ulAddr = ulStartAddrOrig, ui = 0; ulAddr < ulBeyondEndAddrOrig;
	     ulAddr += PAGE_SIZE, ui++) {
		struct page *psPage;

		BUG_ON(ui >= psInfo->iNumPages);

		psPage = CPUVAddrToPage(psVMArea, ulAddr);
		if (psPage == NULL) {
			unsigned uj;

			printk(KERN_WARNING ": OSCpuVToPageList: "
				"Couldn't lookup page structure for "
				"address 0x%lx, trying something else\n",
			       ulAddr);

			for (uj = 0; uj < ui; uj++)
				put_page_testzero(psInfo->ppsPages[uj]);
			break;
		}

		psInfo->ppsPages[ui] = psPage;
	}

	BUG_ON(ui > psInfo->iNumPages);
	if (ui == psInfo->iNumPages) {

		for (ui = 0; ui < psInfo->iNumPages; ui++) {
			struct page *psPage = psInfo->ppsPages[ui];
			struct IMG_CPU_PHYADDR CPUPhysAddr;

			CPUPhysAddr.uiAddr = page_to_pfn(psPage) << PAGE_SHIFT;

			psInfo->psPhysAddr[ui] =
			    SysCpuPAddrToSysPAddr(CPUPhysAddr);
			psSysPAddr[ui] = psInfo->psPhysAddr[ui];
		}

		psInfo->eType = WRAP_TYPE_FIND_VMA_PAGES;
	} else {

		if ((psVMArea->vm_flags & VM_PFNMAP) == 0) {
			printk(KERN_WARNING ": OSCpuVToPageList: "
			       "Region isn't a raw PFN mapping.  Giving up.\n");
			goto error_release_mmap_sem;
		}

		for (ulAddr = ulStartAddrOrig, ui = 0;
		     ulAddr < ulBeyondEndAddrOrig; ulAddr += PAGE_SIZE, ui++) {
			struct IMG_CPU_PHYADDR CPUPhysAddr;

			CPUPhysAddr.uiAddr =
			    ((ulAddr - psVMArea->vm_start) +
			     (psVMArea->vm_pgoff << PAGE_SHIFT)) & PAGE_MASK;

			psInfo->psPhysAddr[ui] =
			    SysCpuPAddrToSysPAddr(CPUPhysAddr);
			psSysPAddr[ui] = psInfo->psPhysAddr[ui];
		}
		BUG_ON(ui != psInfo->iNumPages);

		psInfo->eType = WRAP_TYPE_FIND_VMA_PFN;

		printk(KERN_WARNING
		       ": OSCpuVToPageList: Region can't be locked down\n");
	}

	if (bUseLock)
		up_read(&current->mm->mmap_sem);

exit_check:
	CheckPagesContiguous(psInfo);

	*phOSWrapMem = (void *) psInfo;

	return PVRSRV_OK;

error_release_mmap_sem:
	if (bUseLock)
		up_read(&current->mm->mmap_sem);

error_free:
	psInfo->eType = WRAP_TYPE_CLEANUP;
	OSReleasePhysPageAddr((void *) psInfo, bUseLock);
	return PVRSRV_ERROR_GENERIC;
}
