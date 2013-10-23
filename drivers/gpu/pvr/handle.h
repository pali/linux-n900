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

#ifndef __HANDLE_H__
#define __HANDLE_H__


#include "img_types.h"
#include "hash.h"
#include "resman.h"

	typedef enum {
		PVRSRV_HANDLE_TYPE_NONE = 0,
		PVRSRV_HANDLE_TYPE_PERPROC_DATA,
		PVRSRV_HANDLE_TYPE_DEV_NODE,
		PVRSRV_HANDLE_TYPE_DEV_MEM_CONTEXT,
		PVRSRV_HANDLE_TYPE_DEV_MEM_HEAP,
		PVRSRV_HANDLE_TYPE_MEM_INFO,
		PVRSRV_HANDLE_TYPE_SYNC_INFO,
		PVRSRV_HANDLE_TYPE_DISP_INFO,
		PVRSRV_HANDLE_TYPE_DISP_SWAP_CHAIN,
		PVRSRV_HANDLE_TYPE_BUF_INFO,
		PVRSRV_HANDLE_TYPE_DISP_BUFFER,
		PVRSRV_HANDLE_TYPE_BUF_BUFFER,
		PVRSRV_HANDLE_TYPE_SGX_HW_RENDER_CONTEXT,
		PVRSRV_HANDLE_TYPE_SGX_HW_TRANSFER_CONTEXT,
		PVRSRV_HANDLE_TYPE_SGX_HW_2D_CONTEXT,
		PVRSRV_HANDLE_TYPE_SHARED_PB_DESC,
		PVRSRV_HANDLE_TYPE_MEM_INFO_REF,
		PVRSRV_HANDLE_TYPE_SHARED_SYS_MEM_INFO,
		PVRSRV_HANDLE_TYPE_SHARED_EVENT_OBJECT,
		PVRSRV_HANDLE_TYPE_EVENT_OBJECT_CONNECT,
	} PVRSRV_HANDLE_TYPE;

	typedef enum {

		PVRSRV_HANDLE_ALLOC_FLAG_NONE = 0,

		PVRSRV_HANDLE_ALLOC_FLAG_SHARED = 1,

		PVRSRV_HANDLE_ALLOC_FLAG_MULTI = 2,

		PVRSRV_HANDLE_ALLOC_FLAG_PRIVATE = 4
	} PVRSRV_HANDLE_ALLOC_FLAG;

	struct _PVRSRV_HANDLE_BASE_;
	typedef struct _PVRSRV_HANDLE_BASE_ PVRSRV_HANDLE_BASE;

	extern PVRSRV_HANDLE_BASE *gpsKernelHandleBase;

#define	KERNEL_HANDLE_BASE (gpsKernelHandleBase)

	PVRSRV_ERROR PVRSRVAllocHandle(PVRSRV_HANDLE_BASE * psBase,
				       IMG_HANDLE * phHandle, IMG_VOID * pvData,
				       PVRSRV_HANDLE_TYPE eType,
				       PVRSRV_HANDLE_ALLOC_FLAG eFlag);

	PVRSRV_ERROR PVRSRVAllocSubHandle(PVRSRV_HANDLE_BASE * psBase,
					  IMG_HANDLE * phHandle,
					  IMG_VOID * pvData,
					  PVRSRV_HANDLE_TYPE eType,
					  PVRSRV_HANDLE_ALLOC_FLAG eFlag,
					  IMG_HANDLE hParent);

	PVRSRV_ERROR PVRSRVFindHandle(PVRSRV_HANDLE_BASE * psBase,
				      IMG_HANDLE * phHandle, IMG_VOID * pvData,
				      PVRSRV_HANDLE_TYPE eType);

	PVRSRV_ERROR PVRSRVLookupHandleAnyType(PVRSRV_HANDLE_BASE * psBase,
					       IMG_PVOID * ppvData,
					       PVRSRV_HANDLE_TYPE * peType,
					       IMG_HANDLE hHandle);

	PVRSRV_ERROR PVRSRVLookupHandle(PVRSRV_HANDLE_BASE * psBase,
					IMG_PVOID * ppvData, IMG_HANDLE hHandle,
					PVRSRV_HANDLE_TYPE eType);

	PVRSRV_ERROR PVRSRVLookupSubHandle(PVRSRV_HANDLE_BASE * psBase,
					   IMG_PVOID * ppvData,
					   IMG_HANDLE hHandle,
					   PVRSRV_HANDLE_TYPE eType,
					   IMG_HANDLE hAncestor);

	PVRSRV_ERROR PVRSRVGetParentHandle(PVRSRV_HANDLE_BASE * psBase,
					   IMG_PVOID * phParent,
					   IMG_HANDLE hHandle,
					   PVRSRV_HANDLE_TYPE eType);

	PVRSRV_ERROR PVRSRVLookupAndReleaseHandle(PVRSRV_HANDLE_BASE * psBase,
						  IMG_PVOID * ppvData,
						  IMG_HANDLE hHandle,
						  PVRSRV_HANDLE_TYPE eType);

	PVRSRV_ERROR PVRSRVReleaseHandle(PVRSRV_HANDLE_BASE * psBase,
					 IMG_HANDLE hHandle,
					 PVRSRV_HANDLE_TYPE eType);

	PVRSRV_ERROR PVRSRVNewHandleBatch(PVRSRV_HANDLE_BASE * psBase,
					  IMG_UINT32 ui32BatchSize);

	PVRSRV_ERROR PVRSRVCommitHandleBatch(PVRSRV_HANDLE_BASE * psBase);

	void PVRSRVReleaseHandleBatch(PVRSRV_HANDLE_BASE * psBase);

	PVRSRV_ERROR PVRSRVAllocHandleBase(PVRSRV_HANDLE_BASE ** ppsBase,
					   IMG_UINT32 ui32PID);

	PVRSRV_ERROR PVRSRVFreeHandleBase(PVRSRV_HANDLE_BASE * psBase);

	PVRSRV_ERROR PVRSRVHandleInit(IMG_VOID);

	PVRSRV_ERROR PVRSRVHandleDeInit(IMG_VOID);


#define PVRSRVAllocHandleNR(psBase, phHandle, pvData, eType, eFlag) \
	(void)PVRSRVAllocHandle(psBase, phHandle, pvData, eType, eFlag)

#define PVRSRVAllocSubHandleNR(psBase, phHandle, pvData, eType, eFlag, hParent) \
	(void)PVRSRVAllocSubHandle(psBase, phHandle, pvData, eType, eFlag, hParent)


#endif
