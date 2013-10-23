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

#if !defined(__MMAP_H__)
#define __MMAP_H__

#include <linux/mm.h>

#include "mm.h"

typedef struct KV_OFFSET_STRUCT_TAG {

	IMG_UINT32 ui32MMapOffset;

	LinuxMemArea *psLinuxMemArea;

	IMG_UINT32 ui32AllocFlags;

#if defined(DEBUG_LINUX_MMAP_AREAS)
	pid_t pid;
	const IMG_CHAR *pszName;
	IMG_UINT16 ui16Mapped;
	IMG_UINT16 ui16Faults;
#endif

	struct KV_OFFSET_STRUCT_TAG *psNext;
} KV_OFFSET_STRUCT, *PKV_OFFSET_STRUCT;

IMG_VOID PVRMMapInit(void);

IMG_VOID PVRMMapCleanup(void);

PVRSRV_ERROR PVRMMapRegisterArea(const IMG_CHAR * pszName,
				 LinuxMemArea * psLinuxMemArea,
				 IMG_UINT32 ui32AllocFlags);

PVRSRV_ERROR PVRMMapRemoveRegisteredArea(LinuxMemArea * psLinuxMemArea);

PVRSRV_ERROR PVRMMapKVIndexAddressToMMapData(IMG_VOID * pvKVIndexAddress,
					     IMG_UINT32 ui32Size,
					     IMG_UINT32 * pui32MMapOffset,
					     IMG_UINT32 * pui32ByteOffset,
					     IMG_UINT32 * pui32RealByteSize);

int PVRMMap(struct file *pFile, struct vm_area_struct *ps_vma);

#endif
