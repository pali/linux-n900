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
#include <asm/io.h>
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
#include <asm/hardirq.h>
#include <linux/timer.h>
#include <linux/capability.h>
#include <asm/uaccess.h>

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

extern PVRSRV_LINUX_MUTEX gPVRSRVLock;

#define HOST_ALLOC_MEM_USING_KMALLOC ((IMG_HANDLE)0)
#define HOST_ALLOC_MEM_USING_VMALLOC ((IMG_HANDLE)1)

#define LINUX_KMALLOC_LIMIT	PAGE_SIZE	/* 4k */

#if !defined(DEBUG_LINUX_MEMORY_ALLOCATIONS)
PVRSRV_ERROR OSAllocMem(IMG_UINT32 ui32Flags, IMG_UINT32 ui32Size,
			IMG_PVOID * ppvCpuVAddr, IMG_HANDLE * phBlockAlloc)
#else
PVRSRV_ERROR _OSAllocMem(IMG_UINT32 ui32Flags, IMG_UINT32 ui32Size,
			 IMG_PVOID * ppvCpuVAddr, IMG_HANDLE * phBlockAlloc,
			 IMG_CHAR * pszFilename, IMG_UINT32 ui32Line)
#endif
{
	IMG_UINT32 ui32Threshold;

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
PVRSRV_ERROR OSFreeMem(IMG_UINT32 ui32Flags, IMG_UINT32 ui32Size,
		       IMG_PVOID pvCpuVAddr, IMG_HANDLE hBlockAlloc)
#else
PVRSRV_ERROR _OSFreeMem(IMG_UINT32 ui32Flags, IMG_UINT32 ui32Size,
			IMG_PVOID pvCpuVAddr, IMG_HANDLE hBlockAlloc,
			IMG_CHAR * pszFilename, IMG_UINT32 ui32Line)
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

	return PVRSRV_OK;
}

PVRSRV_ERROR
OSAllocPages(IMG_UINT32 ui32AllocFlags,
	     IMG_UINT32 ui32Size,
	     IMG_VOID ** ppvCpuVAddr, IMG_HANDLE * phOSMemHandle)
{
	LinuxMemArea *psLinuxMemArea;


	switch (ui32AllocFlags & PVRSRV_HAP_MAPTYPE_MASK) {
	case PVRSRV_HAP_KERNEL_ONLY:
		{
			psLinuxMemArea =
			    NewVMallocLinuxMemArea(ui32Size, ui32AllocFlags);
			if (!psLinuxMemArea) {
				return PVRSRV_ERROR_OUT_OF_MEMORY;
			}
			break;
		}
	case PVRSRV_HAP_SINGLE_PROCESS:
		{

			psLinuxMemArea =
			    NewAllocPagesLinuxMemArea(ui32Size, ui32AllocFlags);
			if (!psLinuxMemArea) {
				return PVRSRV_ERROR_OUT_OF_MEMORY;
			}
			PVRMMapRegisterArea("Import Arena", psLinuxMemArea,
					    ui32AllocFlags);
			break;
		}

	case PVRSRV_HAP_MULTI_PROCESS:
		{
			psLinuxMemArea =
			    NewVMallocLinuxMemArea(ui32Size, ui32AllocFlags);
			if (!psLinuxMemArea) {
				return PVRSRV_ERROR_OUT_OF_MEMORY;
			}
			PVRMMapRegisterArea("Import Arena", psLinuxMemArea,
					    ui32AllocFlags);
			break;
		}
	default:
		PVR_DPF((PVR_DBG_ERROR, "OSAllocPages: invalid flags 0x%x\n",
			 ui32AllocFlags));
		*ppvCpuVAddr = NULL;
		*phOSMemHandle = (IMG_HANDLE) 0;
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	*ppvCpuVAddr = LinuxMemAreaToCpuVAddr(psLinuxMemArea);
	*phOSMemHandle = psLinuxMemArea;

	LinuxMemAreaRegister(psLinuxMemArea);

	return PVRSRV_OK;
}

PVRSRV_ERROR
OSFreePages(IMG_UINT32 ui32AllocFlags, IMG_UINT32 ui32Bytes,
	    IMG_VOID * pvCpuVAddr, IMG_HANDLE hOSMemHandle)
{
	LinuxMemArea *psLinuxMemArea;
	PVR_UNREFERENCED_PARAMETER(pvCpuVAddr);

	psLinuxMemArea = (LinuxMemArea *) hOSMemHandle;

	switch (ui32AllocFlags & PVRSRV_HAP_MAPTYPE_MASK) {
	case PVRSRV_HAP_KERNEL_ONLY:
		break;
	case PVRSRV_HAP_SINGLE_PROCESS:
	case PVRSRV_HAP_MULTI_PROCESS:
		if (PVRMMapRemoveRegisteredArea(psLinuxMemArea) != PVRSRV_OK) {
			PVR_DPF((PVR_DBG_ERROR,
				 "OSFreePages(ui32AllocFlags=0x%08X, ui32Bytes=%ld, "
				 "pvCpuVAddr=%p, hOSMemHandle=%p) FAILED!",
				 ui32AllocFlags, ui32Bytes, pvCpuVAddr,
				 hOSMemHandle));
			return PVRSRV_ERROR_GENERIC;
		}
		break;
	default:
		PVR_DPF((PVR_DBG_ERROR, "%s: invalid flags 0x%x\n",
			 __FUNCTION__, ui32AllocFlags));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	LinuxMemAreaDeepFree(psLinuxMemArea);

	return PVRSRV_OK;
}

PVRSRV_ERROR
OSGetSubMemHandle(IMG_HANDLE hOSMemHandle,
		  IMG_UINT32 ui32ByteOffset,
		  IMG_UINT32 ui32Bytes,
		  IMG_UINT32 ui32Flags, IMG_HANDLE * phOSMemHandleRet)
{
	LinuxMemArea *psParentLinuxMemArea, *psLinuxMemArea;
	PVRSRV_ERROR eError = PVRSRV_OK;

	psParentLinuxMemArea = (LinuxMemArea *) hOSMemHandle;

	psLinuxMemArea =
	    NewSubLinuxMemArea(psParentLinuxMemArea, ui32ByteOffset, ui32Bytes);
	if (!psLinuxMemArea) {
		*phOSMemHandleRet = NULL;
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}
	*phOSMemHandleRet = psLinuxMemArea;

	if (ui32Flags & PVRSRV_HAP_KERNEL_ONLY) {
		return PVRSRV_OK;
	}

	if (psParentLinuxMemArea->eAreaType == LINUX_MEM_AREA_IO) {
		eError = PVRMMapRegisterArea("Physical", psLinuxMemArea, 0);
		if (eError != PVRSRV_OK) {
			goto failed_register_area;
		}
	} else if (psParentLinuxMemArea->eAreaType ==
		   LINUX_MEM_AREA_ALLOC_PAGES) {
		eError = PVRMMapRegisterArea("Import Arena", psLinuxMemArea, 0);
		if (eError != PVRSRV_OK) {
			goto failed_register_area;
		}
	}

	return PVRSRV_OK;

failed_register_area:
	*phOSMemHandleRet = NULL;
	LinuxMemAreaDeepFree(psLinuxMemArea);
	return eError;
}

PVRSRV_ERROR
OSReleaseSubMemHandle(IMG_VOID * hOSMemHandle, IMG_UINT32 ui32Flags)
{
	LinuxMemArea *psParentLinuxMemArea, *psLinuxMemArea;
	PVRSRV_ERROR eError;

	psLinuxMemArea = (LinuxMemArea *) hOSMemHandle;
	PVR_ASSERT(psLinuxMemArea->eAreaType == LINUX_MEM_AREA_SUB_ALLOC);

	psParentLinuxMemArea =
	    psLinuxMemArea->uData.sSubAlloc.psParentLinuxMemArea;

	if (!(ui32Flags & PVRSRV_HAP_KERNEL_ONLY)
	    && (psParentLinuxMemArea->eAreaType == LINUX_MEM_AREA_IO
		|| psParentLinuxMemArea->eAreaType ==
		LINUX_MEM_AREA_ALLOC_PAGES)
	    ) {
		eError = PVRMMapRemoveRegisteredArea(psLinuxMemArea);
		if (eError != PVRSRV_OK) {
			return eError;
		}
	}
	LinuxMemAreaDeepFree(psLinuxMemArea);

	return PVRSRV_OK;
}

IMG_CPU_PHYADDR
OSMemHandleToCpuPAddr(IMG_VOID * hOSMemHandle, IMG_UINT32 ui32ByteOffset)
{
	PVR_ASSERT(hOSMemHandle);

	return LinuxMemAreaToCpuPAddr(hOSMemHandle, ui32ByteOffset);
}

IMG_VOID OSMemCopy(IMG_VOID * pvDst, IMG_VOID * pvSrc, IMG_UINT32 ui32Size)
{
	memcpy(pvDst, pvSrc, ui32Size);
}

IMG_VOID OSMemSet(IMG_VOID * pvDest, IMG_UINT8 ui8Value, IMG_UINT32 ui32Size)
{
	memset(pvDest, (int)ui8Value, (size_t) ui32Size);
}

IMG_CHAR *OSStringCopy(IMG_CHAR * pszDest, const IMG_CHAR * pszSrc)
{
	return (strcpy(pszDest, pszSrc));
}

IMG_INT32 OSSNPrintf(IMG_CHAR * pStr, IMG_UINT32 ui32Size,
		     const IMG_CHAR * pszFormat, ...)
{
	va_list argList;
	IMG_INT32 iCount;

	va_start(argList, pszFormat);
	iCount = vsnprintf(pStr, (size_t) ui32Size, pszFormat, argList);
	va_end(argList);

	return iCount;
}

IMG_VOID OSBreakResourceLock(PVRSRV_RESOURCE * psResource, IMG_UINT32 ui32ID)
{
	volatile IMG_UINT32 *pui32Access =
	    (volatile IMG_UINT32 *)&psResource->ui32Lock;

	if (*pui32Access) {
		if (psResource->ui32ID == ui32ID) {
			psResource->ui32ID = 0;
			*pui32Access = 0;
		} else {
			PVR_DPF((PVR_DBG_MESSAGE,
				 "OSBreakResourceLock: Resource is not locked for this process."));
		}
	} else {
		PVR_DPF((PVR_DBG_MESSAGE,
			 "OSBreakResourceLock: Resource is not locked"));
	}
}

PVRSRV_ERROR OSCreateResource(PVRSRV_RESOURCE * psResource)
{
	psResource->ui32ID = 0;
	psResource->ui32Lock = 0;

	return PVRSRV_OK;
}

PVRSRV_ERROR OSDestroyResource(PVRSRV_RESOURCE * psResource)
{
	OSBreakResourceLock(psResource, psResource->ui32ID);

	return PVRSRV_OK;
}

PVRSRV_ERROR OSInitEnvData(IMG_PVOID * ppvEnvSpecificData)
{
	ENV_DATA *psEnvData;

	if (OSAllocMem
	    (PVRSRV_OS_PAGEABLE_HEAP, sizeof(ENV_DATA),
	     (IMG_VOID *) & psEnvData, IMG_NULL) != PVRSRV_OK) {
		return PVRSRV_ERROR_GENERIC;
	}

	memset(psEnvData, 0, sizeof(*psEnvData));

	if (OSAllocMem
	    (PVRSRV_OS_PAGEABLE_HEAP,
	     PVRSRV_MAX_BRIDGE_IN_SIZE + PVRSRV_MAX_BRIDGE_OUT_SIZE,
	     &psEnvData->pvBridgeData, IMG_NULL) != PVRSRV_OK) {
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(ENV_DATA), psEnvData,
			  IMG_NULL);
		return PVRSRV_ERROR_GENERIC;
	}

	psEnvData->bMISRInstalled = IMG_FALSE;
	psEnvData->bLISRInstalled = IMG_FALSE;

	*ppvEnvSpecificData = psEnvData;

	return PVRSRV_OK;
}

PVRSRV_ERROR OSDeInitEnvData(IMG_PVOID pvEnvSpecificData)
{
	ENV_DATA *psEnvData = (ENV_DATA *) pvEnvSpecificData;

	PVR_ASSERT(!psEnvData->bMISRInstalled);
	PVR_ASSERT(!psEnvData->bLISRInstalled);

	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
		  PVRSRV_MAX_BRIDGE_IN_SIZE + PVRSRV_MAX_BRIDGE_OUT_SIZE,
		  psEnvData->pvBridgeData, IMG_NULL);

	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(ENV_DATA), pvEnvSpecificData,
		  IMG_NULL);

	return PVRSRV_OK;
}

IMG_VOID OSReleaseThreadQuanta(IMG_VOID)
{
	schedule();
}

IMG_UINT32 OSClockus(IMG_VOID)
{
	unsigned long time, j = jiffies;

	time = j * (1000000 / HZ);

	return time;
}

IMG_VOID OSWaitus(IMG_UINT32 ui32Timeus)
{
	udelay(ui32Timeus);
}

IMG_UINT32 OSGetCurrentProcessIDKM(IMG_VOID)
{
	if (in_interrupt()) {
		return KERNEL_ID;
	}
	return task_tgid_nr(current);
}

IMG_UINT32 OSGetPageSize(IMG_VOID)
{
	return PAGE_SIZE;
}

static irqreturn_t DeviceISRWrapper(int irq, void *dev_id
    )
{
	PVRSRV_DEVICE_NODE *psDeviceNode;
	IMG_BOOL bStatus = IMG_FALSE;

	psDeviceNode = (PVRSRV_DEVICE_NODE *) dev_id;
	if (!psDeviceNode) {
		PVR_DPF((PVR_DBG_ERROR, "DeviceISRWrapper: invalid params\n"));
		goto out;
	}

	bStatus = PVRSRVDeviceLISR(psDeviceNode);

	if (bStatus) {
		SYS_DATA *psSysData = psDeviceNode->psSysData;
		ENV_DATA *psEnvData = (ENV_DATA *) psSysData->pvEnvSpecificData;

		queue_work(psEnvData->psMISRWorkqueue, &psEnvData->sMISRWork);
	}

out:
	return bStatus ? IRQ_HANDLED : IRQ_NONE;
}

static irqreturn_t SystemISRWrapper(int irq, void *dev_id
    )
{
	SYS_DATA *psSysData;
	IMG_BOOL bStatus = IMG_FALSE;

	psSysData = (SYS_DATA *) dev_id;
	if (!psSysData) {
		PVR_DPF((PVR_DBG_ERROR, "SystemISRWrapper: invalid params\n"));
		goto out;
	}

	bStatus = PVRSRVSystemLISR(psSysData);

	if (bStatus) {
		ENV_DATA *psEnvData = (ENV_DATA *) psSysData->pvEnvSpecificData;

		queue_work(psEnvData->psMISRWorkqueue, &psEnvData->sMISRWork);
	}

out:
	return bStatus ? IRQ_HANDLED : IRQ_NONE;
}

PVRSRV_ERROR OSInstallDeviceLISR(IMG_VOID * pvSysData,
				 IMG_UINT32 ui32Irq,
				 IMG_CHAR * pszISRName, IMG_VOID * pvDeviceNode)
{
	SYS_DATA *psSysData = (SYS_DATA *) pvSysData;
	ENV_DATA *psEnvData = (ENV_DATA *) psSysData->pvEnvSpecificData;

	if (psEnvData->bLISRInstalled) {
		PVR_DPF((PVR_DBG_ERROR,
			 "OSInstallDeviceLISR: An ISR has already been installed: IRQ %d cookie %x",
			 psEnvData->ui32IRQ, psEnvData->pvISRCookie));
		return PVRSRV_ERROR_GENERIC;
	}

	PVR_TRACE(("Installing device LISR %s on IRQ %d with cookie %x",
		   pszISRName, ui32Irq, pvDeviceNode));

	if (request_irq(ui32Irq, DeviceISRWrapper,
			IRQF_SHARED
			, pszISRName, pvDeviceNode)) {
		PVR_DPF((PVR_DBG_ERROR,
			 "OSInstallDeviceLISR: Couldn't install device LISR on IRQ %d",
			 ui32Irq));

		return PVRSRV_ERROR_GENERIC;
	}

	psEnvData->ui32IRQ = ui32Irq;
	psEnvData->pvISRCookie = pvDeviceNode;
	psEnvData->bLISRInstalled = IMG_TRUE;

	return PVRSRV_OK;
}

PVRSRV_ERROR OSUninstallDeviceLISR(IMG_VOID * pvSysData)
{
	SYS_DATA *psSysData = (SYS_DATA *) pvSysData;
	ENV_DATA *psEnvData = (ENV_DATA *) psSysData->pvEnvSpecificData;

	if (!psEnvData->bLISRInstalled) {
		PVR_DPF((PVR_DBG_ERROR,
			 "OSUninstallDeviceLISR: No LISR has been installed"));
		return PVRSRV_ERROR_GENERIC;
	}

	PVR_TRACE(("Uninstalling device LISR on IRQ %d with cookie %x",
		   psEnvData->ui32IRQ, psEnvData->pvISRCookie));

	free_irq(psEnvData->ui32IRQ, psEnvData->pvISRCookie);

	psEnvData->bLISRInstalled = IMG_FALSE;

	return PVRSRV_OK;
}

PVRSRV_ERROR OSInstallSystemLISR(IMG_VOID * pvSysData, IMG_UINT32 ui32Irq)
{
	SYS_DATA *psSysData = (SYS_DATA *) pvSysData;
	ENV_DATA *psEnvData = (ENV_DATA *) psSysData->pvEnvSpecificData;

	if (psEnvData->bLISRInstalled) {
		PVR_DPF((PVR_DBG_ERROR,
			 "OSInstallSystemLISR: An LISR has already been installed: IRQ %d cookie %x",
			 psEnvData->ui32IRQ, psEnvData->pvISRCookie));
		return PVRSRV_ERROR_GENERIC;
	}

	PVR_TRACE(("Installing system LISR on IRQ %d with cookie %x", ui32Irq,
		   pvSysData));

	if (request_irq(ui32Irq, SystemISRWrapper,
			IRQF_SHARED
			, "PowerVR", pvSysData)) {
		PVR_DPF((PVR_DBG_ERROR,
			 "OSInstallSystemLISR: Couldn't install system LISR on IRQ %d",
			 ui32Irq));

		return PVRSRV_ERROR_GENERIC;
	}

	psEnvData->ui32IRQ = ui32Irq;
	psEnvData->pvISRCookie = pvSysData;
	psEnvData->bLISRInstalled = IMG_TRUE;

	return PVRSRV_OK;
}

PVRSRV_ERROR OSUninstallSystemLISR(IMG_VOID * pvSysData)
{
	SYS_DATA *psSysData = (SYS_DATA *) pvSysData;
	ENV_DATA *psEnvData = (ENV_DATA *) psSysData->pvEnvSpecificData;

	if (!psEnvData->bLISRInstalled) {
		PVR_DPF((PVR_DBG_ERROR,
			 "OSUninstallSystemLISR: No LISR has been installed"));
		return PVRSRV_ERROR_GENERIC;
	}

	PVR_TRACE(("Uninstalling system LISR on IRQ %d with cookie %x",
		   psEnvData->ui32IRQ, psEnvData->pvISRCookie));

	free_irq(psEnvData->ui32IRQ, psEnvData->pvISRCookie);

	psEnvData->bLISRInstalled = IMG_FALSE;

	return PVRSRV_OK;
}

static void MISRWrapper(struct work_struct *work)
{
	ENV_DATA *psEnvData = container_of(work, ENV_DATA, sMISRWork);
	SYS_DATA *psSysData = (SYS_DATA *) psEnvData->pvSysData;
	PVRSRVMISR(psSysData);
}

PVRSRV_ERROR OSInstallMISR(IMG_VOID * pvSysData)
{
	SYS_DATA *psSysData = (SYS_DATA *) pvSysData;
	ENV_DATA *psEnvData = (ENV_DATA *) psSysData->pvEnvSpecificData;

	if (psEnvData->bMISRInstalled) {
		PVR_DPF((PVR_DBG_ERROR,
			 "OSInstallMISR: An MISR has already been installed"));
		return PVRSRV_ERROR_GENERIC;
	}

	PVR_TRACE(("Installing MISR with cookie %x", pvSysData));

	psEnvData->pvSysData = pvSysData;
	psEnvData->psMISRWorkqueue = create_singlethread_workqueue("sgx_misr");
	INIT_WORK(&psEnvData->sMISRWork, MISRWrapper);

	psEnvData->bMISRInstalled = IMG_TRUE;

	return PVRSRV_OK;
}

PVRSRV_ERROR OSUninstallMISR(IMG_VOID * pvSysData)
{
	SYS_DATA *psSysData = (SYS_DATA *) pvSysData;
	ENV_DATA *psEnvData = (ENV_DATA *) psSysData->pvEnvSpecificData;

	if (!psEnvData->bMISRInstalled) {
		PVR_DPF((PVR_DBG_ERROR,
			 "OSUninstallMISR: No MISR has been installed"));
		return PVRSRV_ERROR_GENERIC;
	}

	PVR_TRACE(("Uninstalling MISR"));

	flush_workqueue(psEnvData->psMISRWorkqueue);
	destroy_workqueue(psEnvData->psMISRWorkqueue);

	psEnvData->bMISRInstalled = IMG_FALSE;

	return PVRSRV_OK;
}

PVRSRV_ERROR OSScheduleMISR(IMG_VOID * pvSysData)
{
	SYS_DATA *psSysData = (SYS_DATA *) pvSysData;
	ENV_DATA *psEnvData = (ENV_DATA *) psSysData->pvEnvSpecificData;

	if (psEnvData->bMISRInstalled) {
		queue_work(psEnvData->psMISRWorkqueue, &psEnvData->sMISRWork);
	}

	return PVRSRV_OK;
}


#define	OS_TAS(p)	xchg((p), 1)
PVRSRV_ERROR OSLockResource(PVRSRV_RESOURCE * psResource, IMG_UINT32 ui32ID)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	if (!OS_TAS(&psResource->ui32Lock))
		psResource->ui32ID = ui32ID;
	else
		eError = PVRSRV_ERROR_GENERIC;

	return eError;
}

PVRSRV_ERROR OSUnlockResource(PVRSRV_RESOURCE * psResource, IMG_UINT32 ui32ID)
{
	volatile IMG_UINT32 *pui32Access =
	    (volatile IMG_UINT32 *)&psResource->ui32Lock;
	PVRSRV_ERROR eError = PVRSRV_OK;

	if (*pui32Access) {
		if (psResource->ui32ID == ui32ID) {
			psResource->ui32ID = 0;
			*pui32Access = 0;
		} else {
			PVR_DPF((PVR_DBG_ERROR,
				 "OSUnlockResource: Resource %p is not locked with expected value.",
				 psResource));
			PVR_DPF((PVR_DBG_MESSAGE, "Should be %x is actually %x",
				 ui32ID, psResource->ui32ID));
			eError = PVRSRV_ERROR_GENERIC;
		}
	} else {
		PVR_DPF((PVR_DBG_ERROR,
			 "OSUnlockResource: Resource %p is not locked",
			 psResource));
		eError = PVRSRV_ERROR_GENERIC;
	}

	return eError;
}

IMG_BOOL OSIsResourceLocked(PVRSRV_RESOURCE * psResource, IMG_UINT32 ui32ID)
{
	volatile IMG_UINT32 *pui32Access =
	    (volatile IMG_UINT32 *)&psResource->ui32Lock;

	return (*(volatile IMG_UINT32 *)pui32Access == 1)
	    && (psResource->ui32ID == ui32ID)
	    ? IMG_TRUE : IMG_FALSE;
}

IMG_CPU_PHYADDR OSMapLinToCPUPhys(IMG_VOID * pvLinAddr)
{
	IMG_CPU_PHYADDR CpuPAddr;

	CpuPAddr.uiAddr = (IMG_UINTPTR_T) VMallocToPhys(pvLinAddr);

	return CpuPAddr;
}

IMG_VOID *OSMapPhysToLin(IMG_CPU_PHYADDR BasePAddr,
			 IMG_UINT32 ui32Bytes,
			 IMG_UINT32 ui32MappingFlags,
			 IMG_HANDLE * phOSMemHandle)
{
	if (phOSMemHandle) {
		*phOSMemHandle = (IMG_HANDLE) 0;
	}

	if (ui32MappingFlags & PVRSRV_HAP_KERNEL_ONLY) {
		IMG_VOID *pvIORemapCookie;
		pvIORemapCookie =
		    IORemapWrapper(BasePAddr, ui32Bytes, ui32MappingFlags);
		if (pvIORemapCookie == IMG_NULL) {
			return NULL;
		}
		return pvIORemapCookie;
	} else {
		PVR_DPF((PVR_DBG_ERROR,
			 "OSMapPhysToLin should only be used with PVRSRV_HAP_KERNEL_ONLY "
			 " (Use OSReservePhys otherwise)"));
		*phOSMemHandle = (IMG_HANDLE) 0;
		return NULL;
	}

	PVR_ASSERT(0);
	return NULL;
}

IMG_BOOL
OSUnMapPhysToLin(IMG_VOID * pvLinAddr, IMG_UINT32 ui32Bytes,
		 IMG_UINT32 ui32MappingFlags, IMG_HANDLE hPageAlloc)
{
	PVR_TRACE(("%s: unmapping %d bytes from 0x%08x", __FUNCTION__,
		   ui32Bytes, pvLinAddr));

	PVR_UNREFERENCED_PARAMETER(hPageAlloc);

	if (ui32MappingFlags & PVRSRV_HAP_KERNEL_ONLY) {
		IOUnmapWrapper(pvLinAddr);
		return IMG_TRUE;
	} else {
		PVR_DPF((PVR_DBG_ERROR,
			 "OSUnMapPhysToLin should only be used with PVRSRV_HAP_KERNEL_ONLY "
			 " (Use OSUnReservePhys otherwise)"));
		return IMG_FALSE;
	}

	PVR_ASSERT(0);
	return IMG_FALSE;
}

static PVRSRV_ERROR
RegisterExternalMem(IMG_SYS_PHYADDR * pBasePAddr,
		    IMG_VOID * pvCPUVAddr,
		    IMG_UINT32 ui32Bytes,
		    IMG_BOOL bPhysContig,
		    IMG_UINT32 ui32MappingFlags, IMG_HANDLE * phOSMemHandle)
{
	LinuxMemArea *psLinuxMemArea;

	switch (ui32MappingFlags & PVRSRV_HAP_MAPTYPE_MASK) {
	case PVRSRV_HAP_KERNEL_ONLY:
		{
			psLinuxMemArea =
			    NewExternalKVLinuxMemArea(pBasePAddr, pvCPUVAddr,
						      ui32Bytes, bPhysContig,
						      ui32MappingFlags);

			if (!psLinuxMemArea) {
				return PVRSRV_ERROR_GENERIC;
			}
			break;
		}
	case PVRSRV_HAP_SINGLE_PROCESS:
		{
			psLinuxMemArea =
			    NewExternalKVLinuxMemArea(pBasePAddr, pvCPUVAddr,
						      ui32Bytes, bPhysContig,
						      ui32MappingFlags);

			if (!psLinuxMemArea) {
				return PVRSRV_ERROR_GENERIC;
			}
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

			if (!psLinuxMemArea) {
				return PVRSRV_ERROR_GENERIC;
			}
			PVRMMapRegisterArea("Physical", psLinuxMemArea,
					    ui32MappingFlags);
			break;
		}
	default:
		PVR_DPF((PVR_DBG_ERROR, "OSRegisterMem : invalid flags 0x%x\n",
			 ui32MappingFlags));
		*phOSMemHandle = (IMG_HANDLE) 0;
		return PVRSRV_ERROR_GENERIC;
	}

	*phOSMemHandle = (IMG_HANDLE) psLinuxMemArea;

	LinuxMemAreaRegister(psLinuxMemArea);

	return PVRSRV_OK;
}

PVRSRV_ERROR
OSRegisterMem(IMG_CPU_PHYADDR BasePAddr,
	      IMG_VOID * pvCPUVAddr,
	      IMG_UINT32 ui32Bytes,
	      IMG_UINT32 ui32MappingFlags, IMG_HANDLE * phOSMemHandle)
{
	IMG_SYS_PHYADDR SysPAddr = SysCpuPAddrToSysPAddr(BasePAddr);

	return RegisterExternalMem(&SysPAddr, pvCPUVAddr, ui32Bytes, IMG_TRUE,
				   ui32MappingFlags, phOSMemHandle);
}

PVRSRV_ERROR OSRegisterDiscontigMem(IMG_SYS_PHYADDR * pBasePAddr,
				    IMG_VOID * pvCPUVAddr, IMG_UINT32 ui32Bytes,
				    IMG_UINT32 ui32MappingFlags,
				    IMG_HANDLE * phOSMemHandle)
{
	return RegisterExternalMem(pBasePAddr, pvCPUVAddr, ui32Bytes, IMG_FALSE,
				   ui32MappingFlags, phOSMemHandle);
}

PVRSRV_ERROR
OSUnRegisterMem(IMG_VOID * pvCpuVAddr,
		IMG_UINT32 ui32Bytes,
		IMG_UINT32 ui32MappingFlags, IMG_HANDLE hOSMemHandle)
{
	LinuxMemArea *psLinuxMemArea = (LinuxMemArea *) hOSMemHandle;

	PVR_UNREFERENCED_PARAMETER(pvCpuVAddr);

	switch (ui32MappingFlags & PVRSRV_HAP_MAPTYPE_MASK) {
	case PVRSRV_HAP_KERNEL_ONLY:
		break;
	case PVRSRV_HAP_SINGLE_PROCESS:
	case PVRSRV_HAP_MULTI_PROCESS:
		{
			if (PVRMMapRemoveRegisteredArea(psLinuxMemArea) !=
			    PVRSRV_OK) {
				PVR_DPF((PVR_DBG_ERROR,
					 "%s(%p, %d, 0x%08X, %p) FAILED!",
					 __FUNCTION__, pvCpuVAddr, ui32Bytes,
					 ui32MappingFlags, hOSMemHandle));
				return PVRSRV_ERROR_GENERIC;
			}
			break;
		}
	default:
		{
			PVR_DPF((PVR_DBG_ERROR,
				 "OSUnRegisterMem : invalid flags 0x%x",
				 ui32MappingFlags));
			return PVRSRV_ERROR_INVALID_PARAMS;
		}
	}

	LinuxMemAreaDeepFree(psLinuxMemArea);

	return PVRSRV_OK;
}

PVRSRV_ERROR OSUnRegisterDiscontigMem(IMG_VOID * pvCpuVAddr,
				      IMG_UINT32 ui32Bytes,
				      IMG_UINT32 ui32Flags,
				      IMG_HANDLE hOSMemHandle)
{
	return OSUnRegisterMem(pvCpuVAddr, ui32Bytes, ui32Flags, hOSMemHandle);
}

PVRSRV_ERROR
OSReservePhys(IMG_CPU_PHYADDR BasePAddr,
	      IMG_UINT32 ui32Bytes,
	      IMG_UINT32 ui32MappingFlags,
	      IMG_VOID ** ppvCpuVAddr, IMG_HANDLE * phOSMemHandle)
{
	LinuxMemArea *psLinuxMemArea;


	switch (ui32MappingFlags & PVRSRV_HAP_MAPTYPE_MASK) {
	case PVRSRV_HAP_KERNEL_ONLY:
		{

			psLinuxMemArea =
			    NewIORemapLinuxMemArea(BasePAddr, ui32Bytes,
						   ui32MappingFlags);
			if (!psLinuxMemArea) {
				return PVRSRV_ERROR_GENERIC;
			}
			break;
		}
	case PVRSRV_HAP_SINGLE_PROCESS:
		{

			psLinuxMemArea =
			    NewIOLinuxMemArea(BasePAddr, ui32Bytes,
					      ui32MappingFlags);
			if (!psLinuxMemArea) {
				return PVRSRV_ERROR_GENERIC;
			}
			PVRMMapRegisterArea("Physical", psLinuxMemArea,
					    ui32MappingFlags);
			break;
		}
	case PVRSRV_HAP_MULTI_PROCESS:
		{
			psLinuxMemArea =
			    NewIORemapLinuxMemArea(BasePAddr, ui32Bytes,
						   ui32MappingFlags);
			if (!psLinuxMemArea) {
				return PVRSRV_ERROR_GENERIC;
			}
			PVRMMapRegisterArea("Physical", psLinuxMemArea,
					    ui32MappingFlags);
			break;
		}
	default:
		PVR_DPF((PVR_DBG_ERROR, "OSMapPhysToLin : invalid flags 0x%x\n",
			 ui32MappingFlags));
		*ppvCpuVAddr = NULL;
		*phOSMemHandle = (IMG_HANDLE) 0;
		return PVRSRV_ERROR_GENERIC;
	}

	*phOSMemHandle = (IMG_HANDLE) psLinuxMemArea;
	*ppvCpuVAddr = LinuxMemAreaToCpuVAddr(psLinuxMemArea);

	LinuxMemAreaRegister(psLinuxMemArea);

	return PVRSRV_OK;
}

PVRSRV_ERROR
OSUnReservePhys(IMG_VOID * pvCpuVAddr,
		IMG_UINT32 ui32Bytes,
		IMG_UINT32 ui32MappingFlags, IMG_HANDLE hOSMemHandle)
{
	LinuxMemArea *psLinuxMemArea;
	PVR_UNREFERENCED_PARAMETER(pvCpuVAddr);

	psLinuxMemArea = (LinuxMemArea *) hOSMemHandle;

	switch (ui32MappingFlags & PVRSRV_HAP_MAPTYPE_MASK) {
	case PVRSRV_HAP_KERNEL_ONLY:
		break;
	case PVRSRV_HAP_SINGLE_PROCESS:
	case PVRSRV_HAP_MULTI_PROCESS:
		{
			if (PVRMMapRemoveRegisteredArea(psLinuxMemArea) !=
			    PVRSRV_OK) {
				PVR_DPF((PVR_DBG_ERROR,
					 "%s(%p, %d, 0x%08X, %p) FAILED!",
					 __FUNCTION__, pvCpuVAddr, ui32Bytes,
					 ui32MappingFlags, hOSMemHandle));
				return PVRSRV_ERROR_GENERIC;
			}
			break;
		}
	default:
		{
			PVR_DPF((PVR_DBG_ERROR,
				 "OSUnMapPhysToLin : invalid flags 0x%x",
				 ui32MappingFlags));
			return PVRSRV_ERROR_INVALID_PARAMS;
		}
	}

	LinuxMemAreaDeepFree(psLinuxMemArea);

	return PVRSRV_OK;
}

PVRSRV_ERROR OSBaseAllocContigMemory(IMG_UINT32 ui32Size,
				     IMG_CPU_VIRTADDR * pvLinAddr,
				     IMG_CPU_PHYADDR * psPhysAddr)
{
	PVR_UNREFERENCED_PARAMETER(ui32Size);
	PVR_UNREFERENCED_PARAMETER(pvLinAddr);
	PVR_UNREFERENCED_PARAMETER(psPhysAddr);
	PVR_DPF((PVR_DBG_ERROR, "%s: Not available", __FUNCTION__));

	return PVRSRV_ERROR_OUT_OF_MEMORY;
}

PVRSRV_ERROR OSBaseFreeContigMemory(IMG_UINT32 ui32Size,
				    IMG_CPU_VIRTADDR pvLinAddr,
				    IMG_CPU_PHYADDR psPhysAddr)
{
	PVR_UNREFERENCED_PARAMETER(ui32Size);
	PVR_UNREFERENCED_PARAMETER(pvLinAddr);
	PVR_UNREFERENCED_PARAMETER(psPhysAddr);

	PVR_DPF((PVR_DBG_WARNING, "%s: Not available", __FUNCTION__));
	return PVRSRV_OK;
}

IMG_UINT32 OSReadHWReg(IMG_PVOID pvLinRegBaseAddr, IMG_UINT32 ui32Offset)
{
	return (IMG_UINT32) readl(pvLinRegBaseAddr + ui32Offset);
}

IMG_VOID OSWriteHWReg(IMG_PVOID pvLinRegBaseAddr, IMG_UINT32 ui32Offset,
		      IMG_UINT32 ui32Value)
{
	writel(ui32Value, pvLinRegBaseAddr + ui32Offset);
}


typedef struct TIMER_CALLBACK_DATA_TAG {
	PFN_TIMER_FUNC pfnTimerFunc;
	IMG_VOID *pvData;
	struct timer_list sTimer;
	IMG_UINT32 ui32Delay;
	IMG_BOOL bActive;
} TIMER_CALLBACK_DATA;

static IMG_VOID OSTimerCallbackWrapper(IMG_UINT32 ui32Data)
{
	TIMER_CALLBACK_DATA *psTimerCBData = (TIMER_CALLBACK_DATA *) ui32Data;

	if (!psTimerCBData->bActive)
		return;

	psTimerCBData->pfnTimerFunc(psTimerCBData->pvData);

	mod_timer(&psTimerCBData->sTimer, psTimerCBData->ui32Delay + jiffies);
}

IMG_HANDLE OSAddTimer(PFN_TIMER_FUNC pfnTimerFunc, IMG_VOID * pvData,
		      IMG_UINT32 ui32MsTimeout)
{
	TIMER_CALLBACK_DATA *psTimerCBData;

	if (!pfnTimerFunc) {
		PVR_DPF((PVR_DBG_ERROR, "OSAddTimer: passed invalid callback"));
		return IMG_NULL;
	}

	if (OSAllocMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
		       sizeof(TIMER_CALLBACK_DATA),
		       (IMG_VOID **) & psTimerCBData, IMG_NULL) != PVRSRV_OK) {
		PVR_DPF((PVR_DBG_ERROR,
			 "OSAddTimer: failed to allocate memory for TIMER_CALLBACK_DATA"));
		return IMG_NULL;
	}

	psTimerCBData->pfnTimerFunc = pfnTimerFunc;
	psTimerCBData->pvData = pvData;
	psTimerCBData->bActive = IMG_FALSE;

	psTimerCBData->ui32Delay = ((HZ * ui32MsTimeout) < 1000)
	    ? 1 : ((HZ * ui32MsTimeout) / 1000);

	init_timer(&psTimerCBData->sTimer);

	psTimerCBData->sTimer.function = OSTimerCallbackWrapper;
	psTimerCBData->sTimer.data = (IMG_UINT32) psTimerCBData;
	psTimerCBData->sTimer.expires = psTimerCBData->ui32Delay + jiffies;

	return (IMG_HANDLE) psTimerCBData;
}

PVRSRV_ERROR OSRemoveTimer(IMG_HANDLE hTimer)
{
	TIMER_CALLBACK_DATA *psTimerCBData = (TIMER_CALLBACK_DATA *) hTimer;

	OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP, sizeof(TIMER_CALLBACK_DATA),
		  psTimerCBData, IMG_NULL);

	return PVRSRV_OK;
}

PVRSRV_ERROR OSEnableTimer(IMG_HANDLE hTimer)
{
	TIMER_CALLBACK_DATA *psTimerCBData = (TIMER_CALLBACK_DATA *) hTimer;

	psTimerCBData->bActive = IMG_TRUE;

	add_timer(&psTimerCBData->sTimer);

	return PVRSRV_OK;
}

PVRSRV_ERROR OSDisableTimer(IMG_HANDLE hTimer)
{
	TIMER_CALLBACK_DATA *psTimerCBData = (TIMER_CALLBACK_DATA *) hTimer;

	psTimerCBData->bActive = IMG_FALSE;

	del_timer_sync(&psTimerCBData->sTimer);

	return PVRSRV_OK;
}

PVRSRV_ERROR OSEventObjectCreate(const IMG_CHAR * pszName,
				 PVRSRV_EVENTOBJECT * psEventObject)
{

	PVRSRV_ERROR eError = PVRSRV_OK;

	if (psEventObject) {
		if (pszName) {

			strncpy(psEventObject->szName, pszName,
				EVENTOBJNAME_MAXLENGTH);
		} else {

			static IMG_UINT16 ui16NameIndex = 0;
			snprintf(psEventObject->szName, EVENTOBJNAME_MAXLENGTH,
				 "PVRSRV_EVENTOBJECT_%d", ui16NameIndex++);
		}

		if (LinuxEventObjectListCreate(&psEventObject->hOSEventKM) !=
		    PVRSRV_OK) {
			eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		}

	} else {
		PVR_DPF((PVR_DBG_ERROR,
			 "OSEventObjectCreate: psEventObject is not a valid pointer"));
		eError = PVRSRV_ERROR_GENERIC;
	}

	return eError;

}

PVRSRV_ERROR OSEventObjectDestroy(PVRSRV_EVENTOBJECT * psEventObject)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	if (psEventObject) {
		if (psEventObject->hOSEventKM) {
			LinuxEventObjectListDestroy(psEventObject->hOSEventKM);
		} else {
			PVR_DPF((PVR_DBG_ERROR,
				 "OSEventObjectDestroy: hOSEventKM is not a valid pointer"));
			eError = PVRSRV_ERROR_INVALID_PARAMS;
		}
	} else {
		PVR_DPF((PVR_DBG_ERROR,
			 "OSEventObjectDestroy: psEventObject is not a valid pointer"));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
	}

	return eError;
}

PVRSRV_ERROR OSEventObjectWait(IMG_HANDLE hOSEventKM)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	if (hOSEventKM) {
		eError =
		    LinuxEventObjectWait(hOSEventKM, EVENT_OBJECT_TIMEOUT_MS);
	} else {
		PVR_DPF((PVR_DBG_ERROR,
			 "OSEventObjectWait: hOSEventKM is not a valid handle"));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
	}

	return eError;
}

PVRSRV_ERROR OSEventObjectOpen(PVRSRV_EVENTOBJECT * psEventObject,
			       IMG_HANDLE * phOSEvent)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	if (psEventObject) {
		if (LinuxEventObjectAdd(psEventObject->hOSEventKM, phOSEvent) !=
		    PVRSRV_OK) {
			PVR_DPF((PVR_DBG_ERROR, "LinuxEventObjectAdd: failed"));
			eError = PVRSRV_ERROR_INVALID_PARAMS;
		}

	} else {
		PVR_DPF((PVR_DBG_ERROR,
			 "OSEventObjectCreate: psEventObject is not a valid pointer"));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
	}

	return eError;
}

PVRSRV_ERROR OSEventObjectClose(PVRSRV_EVENTOBJECT * psEventObject,
				IMG_HANDLE hOSEventKM)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	if (psEventObject) {
		if (LinuxEventObjectDelete
		    (psEventObject->hOSEventKM, hOSEventKM) != PVRSRV_OK) {
			PVR_DPF((PVR_DBG_ERROR,
				 "LinuxEventObjectDelete: failed"));
			eError = PVRSRV_ERROR_INVALID_PARAMS;
		}

	} else {
		PVR_DPF((PVR_DBG_ERROR,
			 "OSEventObjectDestroy: psEventObject is not a valid pointer"));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
	}

	return eError;

}

PVRSRV_ERROR OSEventObjectSignal(IMG_HANDLE hOSEventKM)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	if (hOSEventKM) {
		eError = LinuxEventObjectSignal(hOSEventKM);
	} else {
		PVR_DPF((PVR_DBG_ERROR,
			 "OSEventObjectSignal: hOSEventKM is not a valid handle"));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
	}

	return eError;
}

IMG_BOOL OSProcHasPrivSrvInit(IMG_VOID)
{
	return capable(CAP_SYS_MODULE) != 0;
}

PVRSRV_ERROR OSCopyToUser(IMG_PVOID pvProcess,
			  IMG_VOID * pvDest,
			  IMG_VOID * pvSrc, IMG_UINT32 ui32Bytes)
{
	PVR_UNREFERENCED_PARAMETER(pvProcess);

	if (copy_to_user(pvDest, pvSrc, ui32Bytes) == 0)
		return PVRSRV_OK;
	else
		return PVRSRV_ERROR_GENERIC;
}

PVRSRV_ERROR OSCopyFromUser(IMG_PVOID pvProcess,
			    IMG_VOID * pvDest,
			    IMG_VOID * pvSrc, IMG_UINT32 ui32Bytes)
{
	PVR_UNREFERENCED_PARAMETER(pvProcess);

	if (copy_from_user(pvDest, pvSrc, ui32Bytes) == 0)
		return PVRSRV_OK;
	else
		return PVRSRV_ERROR_GENERIC;
}

IMG_BOOL OSAccessOK(IMG_VERIFY_TEST eVerification, IMG_VOID * pvUserPtr,
		    IMG_UINT32 ui32Bytes)
{
	int linuxType;

	if (eVerification == PVR_VERIFY_READ)
		linuxType = VERIFY_READ;
	else if (eVerification == PVR_VERIFY_WRITE)
		linuxType = VERIFY_WRITE;
	else {
		PVR_DPF((PVR_DBG_ERROR, "%s: Unknown eVerification",
			 __FUNCTION__));
		return PVRSRV_ERROR_GENERIC;
	}
	return (IMG_BOOL) access_ok(linuxType, pvUserPtr, ui32Bytes);
}

typedef enum _eWrapMemType_ {
	WRAP_TYPE_CLEANUP,
	WRAP_TYPE_GET_USER_PAGES,
	WRAP_TYPE_FIND_VMA_PAGES,
	WRAP_TYPE_FIND_VMA_PFN
} eWrapMemType;

typedef struct _sWrapMemInfo_ {
	eWrapMemType eType;
	int iNumPages;
	struct page **ppsPages;
	IMG_SYS_PHYADDR *psPhysAddr;
	int iPageOffset;
	int iContiguous;
#if defined(DEBUG)
	unsigned long ulStartAddr;
	unsigned long ulBeyondEndAddr;
	struct vm_area_struct *psVMArea;
#endif
} sWrapMemInfo;

static void CheckPagesContiguous(sWrapMemInfo * psInfo)
{
	unsigned ui;
	IMG_UINT32 ui32AddrChk;

	BUG_ON(psInfo == IMG_NULL);

	psInfo->iContiguous = 1;

	for (ui = 0, ui32AddrChk = psInfo->psPhysAddr[0].uiAddr;
	     ui < psInfo->iNumPages; ui++, ui32AddrChk += PAGE_SIZE) {
		if (psInfo->psPhysAddr[ui].uiAddr != ui32AddrChk) {
			psInfo->iContiguous = 0;
			break;
		}
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

PVRSRV_ERROR OSReleasePhysPageAddr(IMG_HANDLE hOSWrapMem)
{
	sWrapMemInfo *psInfo = (sWrapMemInfo *) hOSWrapMem;
	unsigned ui;

	BUG_ON(psInfo == IMG_NULL);

#if defined(DEBUG)
	switch (psInfo->eType) {
	case WRAP_TYPE_FIND_VMA_PAGES:

	case WRAP_TYPE_FIND_VMA_PFN:
		{
			struct vm_area_struct *psVMArea;

			down_read(&current->mm->mmap_sem);

			psVMArea = find_vma(current->mm, psInfo->ulStartAddr);
			if (psVMArea == NULL) {
				printk(KERN_WARNING
				       ": OSCpuVToPageListRelease: Couldn't find memory region containing start address %lx",
				       psInfo->ulStartAddr);

				up_read(&current->mm->mmap_sem);
				break;
			}

			if (psInfo->psVMArea != psVMArea) {
				printk(KERN_WARNING
				       ": OSCpuVToPageListRelease: vm_area_struct has a different address from the one used in ImportMem (%p != %p)",
				       psVMArea, psInfo->psVMArea);
			}

			if (psInfo->ulStartAddr < psVMArea->vm_start) {
				printk(KERN_WARNING
				       ": OSCpuVToPageListRelease: Start address %lx is outside of the region returned by find_vma",
				       psInfo->ulStartAddr);
			}

			if (psInfo->ulBeyondEndAddr > psVMArea->vm_end) {
				printk(KERN_WARNING
				       ": OSCpuVToPageListRelease: End address %lx is outside of the region returned by find_vma",
				       psInfo->ulBeyondEndAddr);
			}

			if ((psVMArea->vm_flags & (VM_IO | VM_RESERVED)) !=
			    (VM_IO | VM_RESERVED)) {
				printk(KERN_WARNING
				       ": OSCpuVToPageListRelease: Memory region does not represent memory mapped I/O (VMA flags: 0x%lx)",
				       psVMArea->vm_flags);
			}

			if ((psVMArea->vm_flags & (VM_READ | VM_WRITE)) !=
			    (VM_READ | VM_WRITE)) {
				printk(KERN_WARNING
				       ": OSCpuVToPageListRelease: OSWrapMemReleasePages: No read/write access to memory region (VMA flags: 0x%lx)",
				       psVMArea->vm_flags);
			}

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

				if (!PageReserved(psPage)) ;
				{
					SetPageDirty(psPage);
				}
				page_cache_release(psPage);
			}
			break;
		}
	case WRAP_TYPE_FIND_VMA_PAGES:
		{
			for (ui = 0; ui < psInfo->iNumPages; ui++) {
				put_page_testzero(psInfo->ppsPages[ui]);
			}
			break;
		}
	default:
		{
			printk(KERN_WARNING
			       ": OSCpuVToPageListRelease: Unknown wrap type (%d)",
			       psInfo->eType);
			return PVRSRV_ERROR_GENERIC;
		}
	}

	if (psInfo->ppsPages != IMG_NULL) {
		kfree(psInfo->ppsPages);
	}

	if (psInfo->psPhysAddr != IMG_NULL) {
		kfree(psInfo->psPhysAddr);
	}

	kfree(psInfo);

	return PVRSRV_OK;
}

PVRSRV_ERROR OSAcquirePhysPageAddr(IMG_VOID * pvCPUVAddr,
				   IMG_UINT32 ui32Bytes,
				   IMG_SYS_PHYADDR * psSysPAddr,
				   IMG_HANDLE * phOSWrapMem)
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
	sWrapMemInfo *psInfo;

	ulStartAddr = ulStartAddrOrig & PAGE_MASK;
	ulBeyondEndAddr = PAGE_ALIGN(ulBeyondEndAddrOrig);
	ulAddrRange = ulBeyondEndAddr - ulStartAddr;

	psInfo = kmalloc(sizeof(*psInfo), GFP_KERNEL);
	if (psInfo == NULL) {
		printk(KERN_WARNING
		       ": OSCpuVToPageList: Couldn't allocate information structure");
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
		       ": OSCpuVToPageList: Couldn't allocate page array");
		goto error_free;
	}

	psInfo->ppsPages =
	    kmalloc(psInfo->iNumPages * sizeof(*psInfo->ppsPages), GFP_KERNEL);
	if (psInfo->ppsPages == NULL) {
		printk(KERN_WARNING
		       ": OSCpuVToPageList: Couldn't allocate page array");
		goto error_free;
	}

	down_read(&current->mm->mmap_sem);
	iNumPagesMapped =
	    get_user_pages(current, current->mm, ulStartAddr, psInfo->iNumPages,
			   1, 0, psInfo->ppsPages, NULL);
	up_read(&current->mm->mmap_sem);

	if (iNumPagesMapped >= 0) {

		if (iNumPagesMapped != psInfo->iNumPages) {
			printk(KERN_WARNING
			       ": OSCpuVToPageList: Couldn't map all the pages needed (wanted: %d, got %d)",
			       psInfo->iNumPages, iNumPagesMapped);

			for (ui = 0; ui < iNumPagesMapped; ui++) {
				page_cache_release(psInfo->ppsPages[ui]);

			}
			goto error_free;
		}

		for (ui = 0; ui < psInfo->iNumPages; ui++) {
			IMG_CPU_PHYADDR CPUPhysAddr;

			CPUPhysAddr.uiAddr =
			    page_to_pfn(psInfo->ppsPages[ui]) << PAGE_SHIFT;
			psInfo->psPhysAddr[ui] =
			    SysCpuPAddrToSysPAddr(CPUPhysAddr);
			psSysPAddr[ui] = psInfo->psPhysAddr[ui];

		}

		psInfo->eType = WRAP_TYPE_GET_USER_PAGES;

		goto exit_check;
	}

	printk(KERN_WARNING
	       ": OSCpuVToPageList: get_user_pages failed (%d), trying something else",
	       iNumPagesMapped);

	down_read(&current->mm->mmap_sem);

	psVMArea = find_vma(current->mm, ulStartAddrOrig);
	if (psVMArea == NULL) {
		printk(KERN_WARNING
		       ": OSCpuVToPageList: Couldn't find memory region containing start address %lx",
		       ulStartAddrOrig);

		goto error_release_mmap_sem;
	}
#if defined(DEBUG)
	psInfo->psVMArea = psVMArea;
#endif

	if (ulStartAddrOrig < psVMArea->vm_start) {
		printk(KERN_WARNING
		       ": OSCpuVToPageList: Start address %lx is outside of the region returned by find_vma",
		       ulStartAddrOrig);
		goto error_release_mmap_sem;
	}

	if (ulBeyondEndAddrOrig > psVMArea->vm_end) {
		printk(KERN_WARNING
		       ": OSCpuVToPageList: End address %lx is outside of the region returned by find_vma",
		       ulBeyondEndAddrOrig);
		goto error_release_mmap_sem;
	}

	if ((psVMArea->vm_flags & (VM_IO | VM_RESERVED)) !=
	    (VM_IO | VM_RESERVED)) {
		printk(KERN_WARNING
		       ": OSCpuVToPageList: Memory region does not represent memory mapped I/O (VMA flags: 0x%lx)",
		       psVMArea->vm_flags);
		goto error_release_mmap_sem;
	}

	if ((psVMArea->vm_flags & (VM_READ | VM_WRITE)) != (VM_READ | VM_WRITE)) {
		printk(KERN_WARNING
		       ": OSCpuVToPageList: No read/write access to memory region (VMA flags: 0x%lx)",
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

			printk(KERN_WARNING
			       ": OSCpuVToPageList: Couldn't lookup page structure for address 0x%lx, trying something else",
			       ulAddr);

			for (uj = 0; uj < ui; uj++) {
				put_page_testzero(psInfo->ppsPages[uj]);
			}
			break;
		}

		psInfo->ppsPages[ui] = psPage;
	}

	BUG_ON(ui > psInfo->iNumPages);
	if (ui == psInfo->iNumPages) {

		for (ui = 0; ui < psInfo->iNumPages; ui++) {
			struct page *psPage = psInfo->ppsPages[ui];
			IMG_CPU_PHYADDR CPUPhysAddr;

			CPUPhysAddr.uiAddr = page_to_pfn(psPage) << PAGE_SHIFT;

			psInfo->psPhysAddr[ui] =
			    SysCpuPAddrToSysPAddr(CPUPhysAddr);
			psSysPAddr[ui] = psInfo->psPhysAddr[ui];
		}

		psInfo->eType = WRAP_TYPE_FIND_VMA_PAGES;
	} else {

		if ((psVMArea->vm_flags & VM_PFNMAP) == 0) {
			printk(KERN_WARNING
			       ": OSCpuVToPageList: Region isn't a raw PFN mapping.  Giving up.");
			goto error_release_mmap_sem;
		}

		for (ulAddr = ulStartAddrOrig, ui = 0;
		     ulAddr < ulBeyondEndAddrOrig; ulAddr += PAGE_SIZE, ui++) {
			IMG_CPU_PHYADDR CPUPhysAddr;

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
		       ": OSCpuVToPageList: Region can't be locked down");
	}

	up_read(&current->mm->mmap_sem);

exit_check:
	CheckPagesContiguous(psInfo);

	*phOSWrapMem = (IMG_HANDLE) psInfo;

	return PVRSRV_OK;

error_release_mmap_sem:
	up_read(&current->mm->mmap_sem);
error_free:
	psInfo->eType = WRAP_TYPE_CLEANUP;
	OSReleasePhysPageAddr((IMG_HANDLE) psInfo);
	return PVRSRV_ERROR_GENERIC;
}
