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

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <asm/page.h>
#include <asm/shmparam.h>
#include <asm/pgtable.h>
#include <linux/sched.h>
#include <asm/current.h>
#include "img_defs.h"
#include "services.h"
#include "servicesint.h"
#include "pvrmmap.h"
#include "mmap.h"
#include "mm.h"
#include "pvr_debug.h"
#include "osfunc.h"
#include "proc.h"

static struct KV_OFFSET_STRUCT *FindOffsetStructFromLinuxMemArea(
		struct LinuxMemArea *psLinuxMemArea);
static u32 GetFirstFreePageAlignedNumber(void);
static struct KV_OFFSET_STRUCT *FindOffsetStructByKVIndexAddress(
		void *pvVirtAddress, u32 ui32ByteSize);
static void DeterminUsersSizeAndByteOffset(void *pvKVIndexAddress,
		struct LinuxMemArea *psLinuxMemArea, u32 *pui32RealByteSize,
		u32 *pui32ByteOffset);
static struct KV_OFFSET_STRUCT *FindOffsetStructByMMapOffset(u32 ui32Offset);
static IMG_BOOL DoMapToUser(struct LinuxMemArea *psLinuxMemArea,
		struct vm_area_struct *ps_vma, u32 ui32ByteOffset,
		u32 ui32Size);
static IMG_BOOL CheckSize(struct LinuxMemArea *psLinuxMemArea, u32 ui32Size);

#if defined(DEBUG_LINUX_MMAP_AREAS)
static off_t PrintMMapRegistrations(char *buffer, size_t size, off_t off);
#endif

static void MMapVOpen(struct vm_area_struct *ps_vma);
static void MMapVClose(struct vm_area_struct *ps_vma);

static struct vm_operations_struct MMapIOOps = {
	.open	=	MMapVOpen,
	.close	=	MMapVClose,
};

static struct KV_OFFSET_STRUCT *g_psKVOffsetTable;
static struct kmem_cache *g_psMemmapCache;
#if defined(DEBUG_LINUX_MMAP_AREAS)
static u32 g_ui32RegisteredAreas;
static u32 g_ui32TotalByteSize;
#endif

static struct rw_semaphore g_mmap_sem;

void PVRMMapInit(void)
{
	g_psKVOffsetTable = NULL;

	g_psMemmapCache = KMemCacheCreateWrapper("img-mmap",
					sizeof(struct KV_OFFSET_STRUCT), 0, 0);
	if (g_psMemmapCache) {
#if defined(DEBUG_LINUX_MMAP_AREAS)
		CreateProcReadEntry("mmap", PrintMMapRegistrations);
#endif
	} else {
		PVR_DPF(PVR_DBG_ERROR, "%s: failed to allocate kmem_cache",
			 __func__);
	}
	init_rwsem(&g_mmap_sem);
}

void PVRMMapCleanup(void)
{
	struct KV_OFFSET_STRUCT *psOffsetStruct;

	if (!g_psMemmapCache)
		return;

	if (g_psKVOffsetTable) {
		PVR_DPF(PVR_DBG_ERROR, "%s: BUG! g_psMemmapCache isn't empty!",
			 __func__);

		for (psOffsetStruct = g_psKVOffsetTable; psOffsetStruct;
		     psOffsetStruct = psOffsetStruct->psNext) {
			PVR_DPF(PVR_DBG_ERROR, "%s: BUG!: "
				"Un-registering mmapable area: "
				"psLinuxMemArea=0x%p, CpuPAddr=0x%08lx\n",
				 __func__, psOffsetStruct->psLinuxMemArea,
				 LinuxMemAreaToCpuPAddr(psOffsetStruct->
							psLinuxMemArea,
							0).uiAddr);
			PVRMMapRemoveRegisteredArea(psOffsetStruct->
						    psLinuxMemArea);
		}
	}

	RemoveProcEntry("mmap");
	KMemCacheDestroyWrapper(g_psMemmapCache);
	g_psMemmapCache = NULL;
	PVR_DPF(PVR_DBG_MESSAGE, "PVRMMapCleanup: KVOffsetTable deallocated");
}

enum PVRSRV_ERROR
PVRMMapRegisterArea(const char *pszName,
		    struct LinuxMemArea *psLinuxMemArea, u32 ui32AllocFlags)
{
	struct KV_OFFSET_STRUCT *psOffsetStruct;
	enum PVRSRV_ERROR iError = PVRSRV_OK;
	PVR_DPF(PVR_DBG_MESSAGE,
		 "%s(%s, psLinuxMemArea=%p, ui32AllocFlags=0x%8lx)",
		 __func__, pszName, psLinuxMemArea, ui32AllocFlags);

	down_write(&g_mmap_sem);
	psOffsetStruct = FindOffsetStructFromLinuxMemArea(psLinuxMemArea);
	if (psOffsetStruct) {
		PVR_DPF(PVR_DBG_ERROR, "PVRMMapRegisterArea: "
				"psLinuxMemArea=%p is already registered",
			 psOffsetStruct->psLinuxMemArea);
		iError = PVRSRV_ERROR_INVALID_PARAMS;
		goto register_exit;
	}

	psOffsetStruct = KMemCacheAllocWrapper(g_psMemmapCache, GFP_KERNEL);
	if (!psOffsetStruct) {
		PVR_DPF(PVR_DBG_ERROR, "PVRMMapRegisterArea: "
			"Couldn't alloc another mapping record from cache");
		iError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto register_exit;
	}

	psOffsetStruct->ui32MMapOffset = GetFirstFreePageAlignedNumber();
	psOffsetStruct->psLinuxMemArea = psLinuxMemArea;

	if (psLinuxMemArea->eAreaType != LINUX_MEM_AREA_SUB_ALLOC) {
		psOffsetStruct->ui32AllocFlags = ui32AllocFlags;
	} else {
		struct KV_OFFSET_STRUCT *psParentOffsetStruct;
		psParentOffsetStruct =
		    FindOffsetStructFromLinuxMemArea(psLinuxMemArea->uData.
						     sSubAlloc.
						     psParentLinuxMemArea);
		PVR_ASSERT(psParentOffsetStruct);
		psOffsetStruct->ui32AllocFlags =
		    psParentOffsetStruct->ui32AllocFlags;
	}

#if defined(DEBUG_LINUX_MMAP_AREAS)

	psOffsetStruct->pszName = pszName;
	psOffsetStruct->pid = current->pid;
	psOffsetStruct->ui16Mapped = 0;
	psOffsetStruct->ui16Faults = 0;

	g_ui32RegisteredAreas++;
	if (psLinuxMemArea->eAreaType != LINUX_MEM_AREA_SUB_ALLOC)
		g_ui32TotalByteSize += psLinuxMemArea->ui32ByteSize;
#endif

	psOffsetStruct->psNext = g_psKVOffsetTable;

	g_psKVOffsetTable = psOffsetStruct;
register_exit:
	up_write(&g_mmap_sem);
	return iError;
}

enum PVRSRV_ERROR PVRMMapRemoveRegisteredArea(
					struct LinuxMemArea *psLinuxMemArea)
{
	struct KV_OFFSET_STRUCT **ppsOffsetStruct, *psOffsetStruct;
	enum PVRSRV_ERROR iError = PVRSRV_OK;

	down_write(&g_mmap_sem);
	for (ppsOffsetStruct = &g_psKVOffsetTable;
	     (psOffsetStruct = *ppsOffsetStruct);
	     ppsOffsetStruct = &(*ppsOffsetStruct)->psNext) {
		if (psOffsetStruct->psLinuxMemArea == psLinuxMemArea)
			break;

	}

	if (!psOffsetStruct) {
		PVR_DPF(PVR_DBG_ERROR,
			 "%s: Registration for psLinuxMemArea = 0x%p not found",
			 __func__, psLinuxMemArea);
		iError = PVRSRV_ERROR_BAD_MAPPING;
		goto unregister_exit;
	}
#if defined(DEBUG_LINUX_MMAP_AREAS)

	if (psOffsetStruct->ui16Mapped) {
		PVR_DPF(PVR_DBG_ERROR,
			 "%s: Unregistering still-mapped area! "
			 "(psLinuxMemArea=0x%p)\n",
			 __func__, psOffsetStruct->psLinuxMemArea);
		iError = PVRSRV_ERROR_BAD_MAPPING;
		goto unregister_exit;
	}

	g_ui32RegisteredAreas--;

	if (psLinuxMemArea->eAreaType != LINUX_MEM_AREA_SUB_ALLOC)
		g_ui32TotalByteSize -=
		    psOffsetStruct->psLinuxMemArea->ui32ByteSize;
#endif

	PVR_DPF(PVR_DBG_MESSAGE, "%s: Table entry: "
		 "psLinuxMemArea=0x%08lX, CpuPAddr=0x%08lX", __func__,
		 psOffsetStruct->psLinuxMemArea,
		 LinuxMemAreaToCpuPAddr(psOffsetStruct->psLinuxMemArea, 0));

	*ppsOffsetStruct = psOffsetStruct->psNext;

	KMemCacheFreeWrapper(g_psMemmapCache, psOffsetStruct);

unregister_exit:
	up_write(&g_mmap_sem);
	return iError;
}

static struct KV_OFFSET_STRUCT *FindOffsetStructFromLinuxMemArea(
					struct LinuxMemArea *psLinuxMemArea)
{
	struct KV_OFFSET_STRUCT *psOffsetStruct = NULL;

	for (psOffsetStruct = g_psKVOffsetTable; psOffsetStruct;
	     psOffsetStruct = psOffsetStruct->psNext) {
		if (psOffsetStruct->psLinuxMemArea == psLinuxMemArea)
			return psOffsetStruct;

	}
	return NULL;
}

static u32 GetFirstFreePageAlignedNumber(void)
{
	struct KV_OFFSET_STRUCT *psCurrentRec;
	u32 ui32CurrentPageOffset;

	if (!g_psKVOffsetTable)
		return 0;

	psCurrentRec = g_psKVOffsetTable;
	ui32CurrentPageOffset = (g_psKVOffsetTable->ui32MMapOffset);

	while (psCurrentRec) {
		if (ui32CurrentPageOffset != (psCurrentRec->ui32MMapOffset))
			return ui32CurrentPageOffset;
		psCurrentRec = psCurrentRec->psNext;
		ui32CurrentPageOffset += PAGE_SIZE;
	}

	return g_psKVOffsetTable->ui32MMapOffset + PAGE_SIZE;
}

enum PVRSRV_ERROR
PVRMMapKVIndexAddressToMMapData(void *pvKVIndexAddress,
				u32 ui32Size,
				u32 *pui32MMapOffset,
				u32 *pui32ByteOffset,
				u32 *pui32RealByteSize)
{
	struct KV_OFFSET_STRUCT *psOffsetStruct;
	enum PVRSRV_ERROR iError = PVRSRV_OK;

	down_read(&g_mmap_sem);
	psOffsetStruct =
	    FindOffsetStructByKVIndexAddress(pvKVIndexAddress, ui32Size);
	if (!psOffsetStruct) {
		iError = PVRSRV_ERROR_BAD_MAPPING;
		goto indexaddress_exit;
	}

	*pui32MMapOffset = psOffsetStruct->ui32MMapOffset;

	DeterminUsersSizeAndByteOffset(pvKVIndexAddress,
				       psOffsetStruct->psLinuxMemArea,
				       pui32RealByteSize, pui32ByteOffset);

indexaddress_exit:
	up_read(&g_mmap_sem);
	return iError;
}

static struct KV_OFFSET_STRUCT *FindOffsetStructByKVIndexAddress(
		void *pvKVIndexAddress, u32 ui32ByteSize)
{
	struct KV_OFFSET_STRUCT *psOffsetStruct;
	u8 *pui8CpuVAddr;
	u8 *pui8IndexCpuVAddr = (u8 *)pvKVIndexAddress;

	for (psOffsetStruct = g_psKVOffsetTable; psOffsetStruct;
	     psOffsetStruct = psOffsetStruct->psNext) {
		struct LinuxMemArea *psLinuxMemArea =
						psOffsetStruct->psLinuxMemArea;

		switch (psLinuxMemArea->eAreaType) {
		case LINUX_MEM_AREA_IOREMAP:
			pui8CpuVAddr =
			    psLinuxMemArea->uData.sIORemap.pvIORemapCookie;
			break;
		case LINUX_MEM_AREA_VMALLOC:
			pui8CpuVAddr =
			    psLinuxMemArea->uData.sVmalloc.pvVmallocAddress;
			break;
		case LINUX_MEM_AREA_EXTERNAL_KV:
			pui8CpuVAddr =
			    psLinuxMemArea->uData.sExternalKV.pvExternalKV;
			break;
		default:
			pui8CpuVAddr = NULL;
			break;
		}

		if (pui8CpuVAddr) {
			if (pui8IndexCpuVAddr >= pui8CpuVAddr &&
			    (pui8IndexCpuVAddr + ui32ByteSize) <=
			    (pui8CpuVAddr + psLinuxMemArea->ui32ByteSize))
				return psOffsetStruct;
			else
				pui8CpuVAddr = NULL;
		}

		if (pvKVIndexAddress == psOffsetStruct->psLinuxMemArea) {
			if (psLinuxMemArea->eAreaType ==
			    LINUX_MEM_AREA_SUB_ALLOC)
				PVR_ASSERT(psLinuxMemArea->uData.sSubAlloc.
					   psParentLinuxMemArea->eAreaType !=
					   LINUX_MEM_AREA_SUB_ALLOC);
			return psOffsetStruct;
		}
	}
	printk(KERN_ERR "%s: Failed to find offset struct (KVAddress=%p)\n",
	       __func__, pvKVIndexAddress);
	return NULL;
}

static void DeterminUsersSizeAndByteOffset(void *pvKVIndexAddress,
			       struct LinuxMemArea *psLinuxMemArea,
			       u32 *pui32RealByteSize,
			       u32 *pui32ByteOffset)
{
	u8 *pui8StartVAddr = NULL;
	u8 *pui8IndexCpuVAddr = (u8 *) pvKVIndexAddress;
	u32 ui32PageAlignmentOffset = 0;
	struct IMG_CPU_PHYADDR CpuPAddr;

	CpuPAddr = LinuxMemAreaToCpuPAddr(psLinuxMemArea, 0);
	ui32PageAlignmentOffset = ADDR_TO_PAGE_OFFSET(CpuPAddr.uiAddr);

	if (pvKVIndexAddress != psLinuxMemArea &&
	    (psLinuxMemArea->eAreaType == LINUX_MEM_AREA_IOREMAP
	     || psLinuxMemArea->eAreaType == LINUX_MEM_AREA_VMALLOC
	     || psLinuxMemArea->eAreaType == LINUX_MEM_AREA_EXTERNAL_KV)) {
		pui8StartVAddr = LinuxMemAreaToCpuVAddr(psLinuxMemArea);
		*pui32ByteOffset =
		    (pui8IndexCpuVAddr - pui8StartVAddr) +
		    ui32PageAlignmentOffset;
	} else {
		*pui32ByteOffset = ui32PageAlignmentOffset;
	}

	*pui32RealByteSize =
	    PAGE_ALIGN(psLinuxMemArea->ui32ByteSize + ui32PageAlignmentOffset);
}

int PVRMMap(struct file *pFile, struct vm_area_struct *ps_vma)
{
	unsigned long ulBytes;
	struct KV_OFFSET_STRUCT *psCurrentRec = NULL;
	int iRetVal = 0;

	ulBytes = ps_vma->vm_end - ps_vma->vm_start;
	down_read(&g_mmap_sem);
	PVR_DPF(PVR_DBG_MESSAGE,
		 "%s: Recieved mmap(2) request with a ui32MMapOffset=0x%08lx,"
		 " and ui32ByteSize=%ld(0x%08lx)\n", __func__,
		 PFN_TO_PHYS(ps_vma->vm_pgoff), ulBytes, ulBytes);

	if ((ps_vma->vm_flags & VM_WRITE) && !(ps_vma->vm_flags & VM_SHARED)
	    ) {
		PVR_DPF(PVR_DBG_ERROR, "PVRMMap: "
			"Error - Cannot mmap non-shareable writable areas.");
		iRetVal = -EINVAL;
		goto pvrmmap_exit;
	}

	psCurrentRec =
	    FindOffsetStructByMMapOffset(PFN_TO_PHYS(ps_vma->vm_pgoff));
	if (!psCurrentRec) {
		PVR_DPF(PVR_DBG_ERROR, "PVRMMap: "
		  "Error - Attempted to mmap unregistered area at vm_pgoff=%ld",
			 ps_vma->vm_pgoff);
		iRetVal = -EINVAL;
		goto pvrmmap_exit;
	}
	PVR_DPF(PVR_DBG_MESSAGE, "%s: > psCurrentRec->psLinuxMemArea=%p\n",
		 __func__, psCurrentRec->psLinuxMemArea);
	if (!CheckSize(psCurrentRec->psLinuxMemArea, ulBytes)) {
		iRetVal = -EINVAL;
		goto pvrmmap_exit;
	}

	ps_vma->vm_flags |= VM_DONTDUMP;
	ps_vma->vm_flags |= VM_IO;

	ps_vma->vm_flags |= VM_DONTEXPAND;

	ps_vma->vm_private_data = (void *)psCurrentRec;

	switch (psCurrentRec->ui32AllocFlags & PVRSRV_HAP_CACHETYPE_MASK) {
	case PVRSRV_HAP_CACHED:

		break;
	case PVRSRV_HAP_WRITECOMBINE:
		ps_vma->vm_page_prot =
		    pgprot_writecombine(ps_vma->vm_page_prot);
		break;
	case PVRSRV_HAP_UNCACHED:
		ps_vma->vm_page_prot = pgprot_noncached(ps_vma->vm_page_prot);
		break;
	default:
		PVR_DPF(PVR_DBG_ERROR, "%s: unknown cache type",
			 __func__);
	}

	ps_vma->vm_ops = &MMapIOOps;

	if (!DoMapToUser(psCurrentRec->psLinuxMemArea, ps_vma, 0, ulBytes)) {
		iRetVal = -EAGAIN;
		goto pvrmmap_exit;
	}

	MMapVOpen(ps_vma);

	PVR_DPF(PVR_DBG_MESSAGE, "%s: Mapped area at offset 0x%08lx\n",
		 __func__, ps_vma->vm_pgoff);

pvrmmap_exit:
	up_read(&g_mmap_sem);
	return iRetVal;
}

static struct KV_OFFSET_STRUCT *FindOffsetStructByMMapOffset(u32 ui32MMapOffset)
{
	struct KV_OFFSET_STRUCT *psOffsetStruct;

	for (psOffsetStruct = g_psKVOffsetTable; psOffsetStruct;
	     psOffsetStruct = psOffsetStruct->psNext) {
		if (psOffsetStruct->ui32MMapOffset == ui32MMapOffset)
			return psOffsetStruct;

	}
	return NULL;
}

static IMG_BOOL DoMapToUser(struct LinuxMemArea *psLinuxMemArea,
	    struct vm_area_struct *ps_vma,
	    u32 ui32ByteOffset, u32 ui32ByteSize)
{
	if (psLinuxMemArea->eAreaType == LINUX_MEM_AREA_SUB_ALLOC)
		return DoMapToUser(psLinuxMemArea->
				uData.sSubAlloc.psParentLinuxMemArea,
				ps_vma,
				psLinuxMemArea->uData.sSubAlloc.ui32ByteOffset +
				ui32ByteOffset, ui32ByteSize);

	PVR_ASSERT(ADDR_TO_PAGE_OFFSET(ui32ByteSize) == 0);


	if (LinuxMemAreaPhysIsContig(psLinuxMemArea)) {

		unsigned long pfn =
		    LinuxMemAreaToCpuPFN(psLinuxMemArea, ui32ByteOffset);

		int result =
		    IO_REMAP_PFN_RANGE(ps_vma, ps_vma->vm_start, pfn,
				       ui32ByteSize, ps_vma->vm_page_prot);
		if (result != 0) {
			PVR_DPF(PVR_DBG_ERROR, "%s: Error - "
					"Failed to map contiguous physical "
					"address range (%d)",
				 __func__, result);
			return IMG_FALSE;
		}
	} else {

		unsigned long ulVMAPos = ps_vma->vm_start;
		u32 ui32ByteEnd = ui32ByteOffset + ui32ByteSize;
		u32 ui32PA;

		for (ui32PA = ui32ByteOffset; ui32PA < ui32ByteEnd;
		     ui32PA += PAGE_SIZE) {
			unsigned long pfn =
			    LinuxMemAreaToCpuPFN(psLinuxMemArea, ui32PA);

			int result =
			    REMAP_PFN_RANGE(ps_vma, ulVMAPos, pfn, PAGE_SIZE,
					    ps_vma->vm_page_prot);
			if (result != 0) {
				PVR_DPF(PVR_DBG_ERROR, "%s: Error - "
					"Failed to map discontiguous "
					"physical address range (%d)",
					 __func__, result);
				return IMG_FALSE;
			}
			ulVMAPos += PAGE_SIZE;
		}
	}

	return IMG_TRUE;
}

static IMG_BOOL
CheckSize(struct LinuxMemArea *psLinuxMemArea, u32 ui32ByteSize)
{
	struct IMG_CPU_PHYADDR CpuPAddr;
	u32 ui32PageAlignmentOffset;
	u32 ui32RealByteSize;
	CpuPAddr = LinuxMemAreaToCpuPAddr(psLinuxMemArea, 0);
	ui32PageAlignmentOffset = ADDR_TO_PAGE_OFFSET(CpuPAddr.uiAddr);
	ui32RealByteSize =
	    PAGE_ALIGN(psLinuxMemArea->ui32ByteSize + ui32PageAlignmentOffset);
	if (ui32RealByteSize < ui32ByteSize) {
		PVR_DPF(PVR_DBG_ERROR,
			 "Cannot mmap %ld bytes from: "
			 "%-8p %-8p %08lx %-8ld %-24s\n",
			 ui32ByteSize,
			 psLinuxMemArea,
			 LinuxMemAreaToCpuVAddr(psLinuxMemArea),
			 LinuxMemAreaToCpuPAddr(psLinuxMemArea, 0).uiAddr,
			 psLinuxMemArea->ui32ByteSize,
			 LinuxMemAreaTypeToString(psLinuxMemArea->eAreaType));
		return IMG_FALSE;
	}
	return IMG_TRUE;
}

static void MMapVOpen(struct vm_area_struct *ps_vma)
{
#if defined(DEBUG_LINUX_MMAP_AREAS)
	struct KV_OFFSET_STRUCT *psOffsetStruct =
	    (struct KV_OFFSET_STRUCT *)ps_vma->vm_private_data;
	PVR_ASSERT(psOffsetStruct != NULL);
	psOffsetStruct->ui16Mapped++;

	PVR_DPF(PVR_DBG_MESSAGE,
		 "%s: psLinuxMemArea=%p, KVAddress=%p "
		 "MMapOffset=%ld, ui16Mapped=%d",
		 __func__,
		 psOffsetStruct->psLinuxMemArea,
		 LinuxMemAreaToCpuVAddr(psOffsetStruct->psLinuxMemArea),
		 psOffsetStruct->ui32MMapOffset, psOffsetStruct->ui16Mapped);
#endif

}

static void MMapVClose(struct vm_area_struct *ps_vma)
{
#if defined(DEBUG_LINUX_MMAP_AREAS)
	struct KV_OFFSET_STRUCT *psOffsetStruct =
	    (struct KV_OFFSET_STRUCT *)ps_vma->vm_private_data;
	PVR_ASSERT(psOffsetStruct != NULL);
	psOffsetStruct->ui16Mapped--;

	PVR_DPF(PVR_DBG_MESSAGE,
		 "%s: psLinuxMemArea=%p, CpuVAddr=%p "
		 "ui32MMapOffset=%ld, ui16Mapped=%d",
		 __func__,
		 psOffsetStruct->psLinuxMemArea,
		 LinuxMemAreaToCpuVAddr(psOffsetStruct->psLinuxMemArea),
		 psOffsetStruct->ui32MMapOffset, psOffsetStruct->ui16Mapped);
#endif

}

#if defined(DEBUG_LINUX_MMAP_AREAS)
static off_t PrintMMapRegistrations(char *buffer, size_t size, off_t off)
{
	struct KV_OFFSET_STRUCT *psOffsetStruct;
	off_t Ret;

	down_read(&g_mmap_sem);
	if (!off) {
		Ret = printAppend(buffer, size, 0,
				  "Allocations registered for mmap: %u\n"
				  "In total these areas correspond to %u "
				  "bytes (excluding SUB areas)\n"
				  "psLinuxMemArea "
				  "CpuVAddr "
				  "CpuPAddr "
				  "MMapOffset "
				  "ByteLength "
				  "LinuxMemType             "
				  "Pid   Name     Mapped Flags\n",
				  g_ui32RegisteredAreas, g_ui32TotalByteSize);

		goto unlock_and_return;
	}

	if (size < 135) {
		Ret = 0;
		goto unlock_and_return;
	}

	for (psOffsetStruct = g_psKVOffsetTable; --off && psOffsetStruct;
	     psOffsetStruct = psOffsetStruct->psNext)
		;
	if (!psOffsetStruct) {
		Ret = END_OF_FILE;
		goto unlock_and_return;
	}

	Ret = printAppend(buffer, size, 0,
	  "%-8p       %-8p %08x %08x   %-8d   %-24s %-5d %-8s %-5u  %08x(%s)\n",
	  psOffsetStruct->psLinuxMemArea,
	  LinuxMemAreaToCpuVAddr(psOffsetStruct->psLinuxMemArea),
	  LinuxMemAreaToCpuPAddr(psOffsetStruct->psLinuxMemArea, 0).uiAddr,
	  psOffsetStruct->ui32MMapOffset,
	  psOffsetStruct->psLinuxMemArea->ui32ByteSize,
	  LinuxMemAreaTypeToString(psOffsetStruct->psLinuxMemArea->eAreaType),
	  psOffsetStruct->pid, psOffsetStruct->pszName,
	  psOffsetStruct->ui16Mapped, psOffsetStruct->ui32AllocFlags,
	  HAPFlagsToString(psOffsetStruct->ui32AllocFlags));

unlock_and_return:
	up_read(&g_mmap_sem);
	return Ret;
}
#endif
