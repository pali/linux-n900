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

#include "services_headers.h"

#include "sysconfig.h"
#include "hash.h"
#include "ra.h"
#include "pdump_km.h"

#define MIN(a,b)       (a > b ? b : a)

static IMG_BOOL
ZeroBuf(BM_BUF *pBuf, BM_MAPPING *pMapping, IMG_UINT32 ui32Bytes, IMG_UINT32 ui32Flags);
static void
BM_FreeMemory (void *pH, IMG_UINTPTR_T base, BM_MAPPING *psMapping);
static IMG_BOOL
BM_ImportMemory(void *pH, IMG_SIZE_T uSize,
					IMG_SIZE_T *pActualSize, BM_MAPPING **ppsMapping,
					IMG_UINT32 uFlags, IMG_UINTPTR_T *pBase);

static IMG_BOOL
DevMemoryAlloc (BM_CONTEXT *pBMContext,
				BM_MAPPING *pMapping, 
				IMG_SIZE_T *pActualSize,
				IMG_UINT32 uFlags,
				IMG_UINT32 dev_vaddr_alignment,
				IMG_DEV_VIRTADDR *pDevVAddr);
static void
DevMemoryFree (BM_MAPPING *pMapping);

static IMG_BOOL
AllocMemory (BM_CONTEXT				*pBMContext,
				BM_HEAP				*psBMHeap,
				IMG_DEV_VIRTADDR	*psDevVAddr,
				IMG_SIZE_T			uSize,
				IMG_UINT32			uFlags,
				IMG_UINT32			uDevVAddrAlignment,
				BM_BUF				*pBuf)
{
	BM_MAPPING			*pMapping;
	IMG_UINTPTR_T		uOffset;
	RA_ARENA			*pArena = IMG_NULL;

	PVR_DPF ((PVR_DBG_MESSAGE,
			  "AllocMemory (pBMContext=%08X, uSize=0x%x, uFlags=0x%x, align=0x%x, pBuf=%08X)",
			  pBMContext, uSize, uFlags, uDevVAddrAlignment, pBuf));

	


	if(uFlags & PVRSRV_MEM_RAM_BACKED_ALLOCATION)
	{
		if(uFlags & PVRSRV_MEM_USER_SUPPLIED_DEVVADDR)
		{
			
			PVR_DPF ((PVR_DBG_ERROR, "AllocMemory: combination of DevVAddr management and RAM backing mode unsupported"));			
			return IMG_FALSE;
		}

		

		
		if(psBMHeap->ui32Attribs
		   &	(PVRSRV_BACKINGSTORE_SYSMEM_NONCONTIG
		   |PVRSRV_BACKINGSTORE_LOCALMEM_CONTIG))
		{
			
			pArena = psBMHeap->pImportArena;
		}
		else
		{
			PVR_DPF ((PVR_DBG_ERROR, "AllocMemory: backing store type doesn't match heap"));
			return IMG_FALSE;
		}

		
		if (!RA_Alloc(pArena,
					  uSize,
					  IMG_NULL,
					  (void*) &pMapping,
					  uFlags,
					  uDevVAddrAlignment,
					  0,
					  (IMG_UINTPTR_T *)&(pBuf->DevVAddr.uiAddr)))
		{
			PVR_DPF((PVR_DBG_ERROR, "AllocMemory: RA_Alloc(0x%x) FAILED", uSize));
			return IMG_FALSE;
		}

		uOffset = pBuf->DevVAddr.uiAddr - pMapping->DevVAddr.uiAddr;
		if(pMapping->CpuVAddr)
		{
			pBuf->CpuVAddr = (void*) ((IMG_UINTPTR_T)pMapping->CpuVAddr + uOffset);
		}
		else
		{
			pBuf->CpuVAddr = IMG_NULL;
		}

		if(uSize == pMapping->uSize)
		{
			pBuf->hOSMemHandle = pMapping->hOSMemHandle;
		}
		else
		{
			if(OSGetSubMemHandle(pMapping->hOSMemHandle,
								 uOffset,
								 uSize,
								 psBMHeap->ui32Attribs,
								 &pBuf->hOSMemHandle)!=PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "AllocMemory: OSGetSubMemHandle FAILED"));
				return IMG_FALSE;
			}
		}

		
		pBuf->CpuPAddr = pMapping->CpuPAddr;

		if(uFlags & PVRSRV_MEM_ZERO)
		{
			if(!ZeroBuf(pBuf, pMapping, uSize, psBMHeap->ui32Attribs | uFlags))
			{
				return IMG_FALSE;
			}
		}
	}
	else
	{
		if(uFlags & PVRSRV_MEM_USER_SUPPLIED_DEVVADDR)
		{
			
			PVR_ASSERT(psDevVAddr != IMG_NULL);

			
			pBMContext->psDeviceNode->pfnMMUAlloc (psBMHeap->pMMUHeap,
													uSize,
													IMG_NULL,
													PVRSRV_MEM_USER_SUPPLIED_DEVVADDR,
													uDevVAddrAlignment,
													psDevVAddr);

			
			pBuf->DevVAddr = *psDevVAddr;
		}
		else
		{
			

			
			pBMContext->psDeviceNode->pfnMMUAlloc (psBMHeap->pMMUHeap,
													uSize,
													IMG_NULL,
													0,
													uDevVAddrAlignment,
													&pBuf->DevVAddr);
		}

		
		if (OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
							sizeof (struct _BM_MAPPING_),
							(IMG_PVOID *)&pMapping, IMG_NULL) != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "AllocMemory: OSAllocMem(0x%x) FAILED"));
			return IMG_FALSE;
		}

		
		pBuf->CpuVAddr = IMG_NULL;
		pBuf->hOSMemHandle = 0;
		pBuf->CpuPAddr.uiAddr = 0;

		
		pMapping->CpuVAddr = IMG_NULL;
		pMapping->CpuPAddr.uiAddr = 0;
		pMapping->DevVAddr = pBuf->DevVAddr;
		pMapping->psSysAddr = IMG_NULL;
		pMapping->uSize = uSize;
		pMapping->hOSMemHandle = 0;
	}

	
	pMapping->pArena = pArena;

	
	pMapping->pBMHeap = psBMHeap;
	pBuf->pMapping = pMapping;

	
	PVR_DPF ((PVR_DBG_MESSAGE,
				"AllocMemory: pMapping=%08X: DevV=%08X CpuV=%08X CpuP=%08X uSize=0x%x",
				pMapping,
				pMapping->DevVAddr.uiAddr,
				pMapping->CpuVAddr,
				pMapping->CpuPAddr.uiAddr,
				pMapping->uSize));

	PVR_DPF ((PVR_DBG_MESSAGE,
				"AllocMemory: pBuf=%08X: DevV=%08X CpuV=%08X CpuP=%08X uSize=0x%x",
				pBuf,
				pBuf->DevVAddr.uiAddr,
				pBuf->CpuVAddr,
				pBuf->CpuPAddr.uiAddr,
				uSize));

	
	PVR_ASSERT(((pBuf->DevVAddr.uiAddr) & (uDevVAddrAlignment - 1)) == 0);

	return IMG_TRUE;
}


static IMG_BOOL
WrapMemory (BM_HEAP *psBMHeap,
			IMG_SIZE_T uSize,
			IMG_UINT32 ui32BaseOffset,
			IMG_BOOL bPhysContig,
			IMG_SYS_PHYADDR *psAddr,
			IMG_VOID *pvCPUVAddr,
			IMG_UINT32 uFlags,
			BM_BUF *pBuf)
{
	IMG_DEV_VIRTADDR DevVAddr = {0};
	BM_MAPPING *pMapping;
	IMG_BOOL bResult;
	IMG_UINT32 const ui32PageSize = HOST_PAGESIZE();

	PVR_DPF ((PVR_DBG_MESSAGE,
			  "WrapMemory(psBMHeap=%08X, size=0x%x, offset=0x%x, bPhysContig=0x%x, pvCPUVAddr = 0x%x, flags=0x%x, pBuf=%08X)",
			  psBMHeap, uSize, ui32BaseOffset, bPhysContig, pvCPUVAddr, uFlags, pBuf));

	PVR_ASSERT((psAddr->uiAddr & (ui32PageSize - 1)) == 0);
	
	PVR_ASSERT(((IMG_UINT32)pvCPUVAddr & (ui32PageSize - 1)) == 0);

	uSize += ui32BaseOffset;
	uSize = HOST_PAGEALIGN (uSize);

	
	if (OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
						sizeof(*pMapping),
						(IMG_PVOID *)&pMapping, IMG_NULL) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "WrapMemory: OSAllocMem(0x%x) FAILED",sizeof(*pMapping)));
		return IMG_FALSE;
	}

	OSMemSet(pMapping, 0, sizeof (*pMapping));
	
	pMapping->uSize = uSize;
	pMapping->pBMHeap = psBMHeap;

	if(pvCPUVAddr)
	{
		pMapping->CpuVAddr = pvCPUVAddr;
	
		if (bPhysContig)
		{
			pMapping->eCpuMemoryOrigin = hm_wrapped_virtaddr;
			pMapping->CpuPAddr = SysSysPAddrToCpuPAddr(psAddr[0]);

			if(OSRegisterMem(pMapping->CpuPAddr, 
							pMapping->CpuVAddr,
							pMapping->uSize,
							uFlags,
							&pMapping->hOSMemHandle) != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,	"WrapMemory: OSRegisterMem Phys=0x%08X, CpuVAddr = 0x%08X, Size=%d) failed",
					pMapping->CpuPAddr, pMapping->CpuVAddr, pMapping->uSize));
				goto fail_cleanup;
			}
		}
		else
		{
			pMapping->eCpuMemoryOrigin = hm_wrapped_scatter_virtaddr;
			pMapping->psSysAddr = psAddr;

			if(OSRegisterDiscontigMem(pMapping->psSysAddr, 
							pMapping->CpuVAddr,
							pMapping->uSize,
							uFlags,
							&pMapping->hOSMemHandle) != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,	"WrapMemory: OSRegisterDiscontigMem CpuVAddr = 0x%08X, Size=%d) failed",
					pMapping->CpuVAddr, pMapping->uSize));
				goto fail_cleanup;
			}
		}
	}
	else
	{
		if (bPhysContig)
		{
			pMapping->eCpuMemoryOrigin = hm_wrapped;
			pMapping->CpuPAddr = SysSysPAddrToCpuPAddr(psAddr[0]);

			if(OSReservePhys(pMapping->CpuPAddr,
							 pMapping->uSize,
							 uFlags,
							 &pMapping->CpuVAddr,
							 &pMapping->hOSMemHandle) != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,	"WrapMemory: OSReservePhys Phys=0x%08X, Size=%d) failed",
					pMapping->CpuPAddr, pMapping->uSize));
				goto fail_cleanup;
			}
		}
		else
		{
			pMapping->eCpuMemoryOrigin = hm_wrapped_scatter;
			pMapping->psSysAddr = psAddr;

			if(OSReserveDiscontigPhys(pMapping->psSysAddr,
							 pMapping->uSize,
							 uFlags,
							 &pMapping->CpuVAddr,
							 &pMapping->hOSMemHandle) != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,	"WrapMemory: OSReserveDiscontigPhys Size=%d) failed",
					pMapping->uSize));
				goto fail_cleanup;
			}
		}
	}

	
	bResult = DevMemoryAlloc(psBMHeap->pBMContext,
							 pMapping,
							 IMG_NULL,
							 uFlags | PVRSRV_MEM_READ | PVRSRV_MEM_WRITE,
							 ui32PageSize,
							 &DevVAddr);
	if (!bResult)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"WrapMemory: DevMemoryAlloc(0x%x) failed",
				pMapping->uSize));
		goto fail_cleanup;
	}

	
	pBuf->CpuPAddr.uiAddr = pMapping->CpuPAddr.uiAddr + ui32BaseOffset;
	if(!ui32BaseOffset)
	{
		pBuf->hOSMemHandle = pMapping->hOSMemHandle;
	}
	else
	{
		if(OSGetSubMemHandle(pMapping->hOSMemHandle,
							 ui32BaseOffset,
							 (pMapping->uSize-ui32BaseOffset),
							 uFlags,
							 &pBuf->hOSMemHandle)!=PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "WrapMemory: OSGetSubMemHandle failed"));
			goto fail_cleanup;
		}
	}
	if(pMapping->CpuVAddr)
	{
		pBuf->CpuVAddr = (void*) ((IMG_UINTPTR_T)pMapping->CpuVAddr + ui32BaseOffset);
	}
	pBuf->DevVAddr.uiAddr = pMapping->DevVAddr.uiAddr + ui32BaseOffset;

	if(uFlags & PVRSRV_MEM_ZERO)
	{
		if(!ZeroBuf(pBuf, pMapping, uSize, uFlags))
		{
			return IMG_FALSE;
		}
	}

	PVR_DPF ((PVR_DBG_MESSAGE, "DevVaddr.uiAddr=%08X", DevVAddr.uiAddr));
	PVR_DPF ((PVR_DBG_MESSAGE,
				"WrapMemory: pMapping=%08X: DevV=%08X CpuV=%08X CpuP=%08X uSize=0x%x",
				pMapping, pMapping->DevVAddr.uiAddr,
				pMapping->CpuVAddr, pMapping->CpuPAddr.uiAddr, pMapping->uSize));
	PVR_DPF ((PVR_DBG_MESSAGE,
				"WrapMemory: pBuf=%08X: DevV=%08X CpuV=%08X CpuP=%08X uSize=0x%x",
				pBuf, pBuf->DevVAddr.uiAddr,
				pBuf->CpuVAddr, pBuf->CpuPAddr.uiAddr, uSize));

	pBuf->pMapping = pMapping;
	return IMG_TRUE;

fail_cleanup:
	if(ui32BaseOffset && pBuf->hOSMemHandle)
	{
		OSReleaseSubMemHandle(pBuf->hOSMemHandle, uFlags);
	}

	if(pMapping && (pMapping->CpuVAddr || pMapping->hOSMemHandle))
	{
		switch(pMapping->eCpuMemoryOrigin)
		{
			case hm_wrapped:
				OSUnReservePhys(pMapping->CpuVAddr, pMapping->uSize, uFlags, pMapping->hOSMemHandle);
				break;
			case hm_wrapped_virtaddr:
				OSUnRegisterMem(pMapping->CpuVAddr, pMapping->uSize, uFlags, pMapping->hOSMemHandle);
				break;
			case hm_wrapped_scatter:
				OSUnReserveDiscontigPhys(pMapping->CpuVAddr, pMapping->uSize, uFlags, pMapping->hOSMemHandle);
				break;
			case hm_wrapped_scatter_virtaddr:
				OSUnRegisterDiscontigMem(pMapping->CpuVAddr, pMapping->uSize, uFlags, pMapping->hOSMemHandle);
				break;
			default:
				break;
		}

	}

	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(BM_MAPPING), pMapping, IMG_NULL);

	return IMG_FALSE;
}


static IMG_BOOL
ZeroBuf(BM_BUF *pBuf, BM_MAPPING *pMapping, IMG_UINT32 ui32Bytes, IMG_UINT32 ui32Flags)
{
	IMG_VOID *pvCpuVAddr;

	if(pBuf->CpuVAddr)
	{
		OSMemSet(pBuf->CpuVAddr, 0, ui32Bytes);
	}
	else if(pMapping->eCpuMemoryOrigin == hm_contiguous
			|| pMapping->eCpuMemoryOrigin == hm_wrapped)
	{
		pvCpuVAddr = OSMapPhysToLin(pBuf->CpuPAddr,
									ui32Bytes,
									PVRSRV_HAP_KERNEL_ONLY
									| (ui32Flags & PVRSRV_HAP_CACHETYPE_MASK),
									IMG_NULL);
		if(!pvCpuVAddr)
		{
			PVR_DPF((PVR_DBG_ERROR, "ZeroBuf: OSMapPhysToLin for contiguous buffer failed"));
			return IMG_FALSE;
		}
		OSMemSet(pvCpuVAddr, 0, ui32Bytes);
		OSUnMapPhysToLin(pvCpuVAddr,
						 ui32Bytes,
						 PVRSRV_HAP_KERNEL_ONLY
						 | (ui32Flags & PVRSRV_HAP_CACHETYPE_MASK),
						 IMG_NULL);
	}
	else
	{
		IMG_UINT32 ui32BytesRemaining = ui32Bytes;
		IMG_UINT32 ui32CurrentOffset = 0;
		IMG_CPU_PHYADDR CpuPAddr;

		
		PVR_ASSERT(pBuf->hOSMemHandle);

		while(ui32BytesRemaining > 0)
		{
			IMG_UINT32 ui32BlockBytes = MIN(ui32BytesRemaining, HOST_PAGESIZE());
			CpuPAddr = OSMemHandleToCpuPAddr(pBuf->hOSMemHandle, ui32CurrentOffset);
			
			if(CpuPAddr.uiAddr & (HOST_PAGESIZE() -1))
			{
				ui32BlockBytes =
					MIN(ui32BytesRemaining, HOST_PAGEALIGN(CpuPAddr.uiAddr) - CpuPAddr.uiAddr);
			}

			pvCpuVAddr = OSMapPhysToLin(CpuPAddr,
										ui32BlockBytes,
										PVRSRV_HAP_KERNEL_ONLY
										| (ui32Flags & PVRSRV_HAP_CACHETYPE_MASK),
										IMG_NULL);
			if(!pvCpuVAddr)
			{
				PVR_DPF((PVR_DBG_ERROR, "ZeroBuf: OSMapPhysToLin while zeroing non-contiguous memory FAILED"));
				return IMG_FALSE;
			}
			OSMemSet(pvCpuVAddr, 0, ui32BlockBytes);
			OSUnMapPhysToLin(pvCpuVAddr,
							 ui32BlockBytes,
							 PVRSRV_HAP_KERNEL_ONLY
							 | (ui32Flags & PVRSRV_HAP_CACHETYPE_MASK),
							 IMG_NULL);

			ui32BytesRemaining -= ui32BlockBytes;
			ui32CurrentOffset += ui32BlockBytes;
		}
	}

	return IMG_TRUE;
}

static void
FreeBuf (BM_BUF *pBuf, IMG_UINT32 ui32Flags)
{
	BM_MAPPING *pMapping;

	PVR_DPF ((PVR_DBG_MESSAGE,
			"FreeBuf: pBuf=%08X: DevVAddr=%08X CpuVAddr=%08X CpuPAddr=%08X",
			pBuf, pBuf->DevVAddr.uiAddr, pBuf->CpuVAddr, pBuf->CpuPAddr.uiAddr));

	
	pMapping = pBuf->pMapping;

	if(ui32Flags & PVRSRV_MEM_USER_SUPPLIED_DEVVADDR)
	{
		
		if(ui32Flags & PVRSRV_MEM_RAM_BACKED_ALLOCATION)
		{
			
			PVR_DPF ((PVR_DBG_ERROR, "FreeBuf: combination of DevVAddr management and RAM backing mode unsupported"));
		}
		else
		{
			
			OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(BM_MAPPING), pMapping, IMG_NULL);
		}
	}
	else
	{
		
		if(pBuf->hOSMemHandle != pMapping->hOSMemHandle)
		{
			OSReleaseSubMemHandle(pBuf->hOSMemHandle, ui32Flags);
		}
		if(ui32Flags & PVRSRV_MEM_RAM_BACKED_ALLOCATION)
		{
			


			RA_Free (pBuf->pMapping->pArena, pBuf->DevVAddr.uiAddr, IMG_FALSE);
		}
		else
		{
			switch (pMapping->eCpuMemoryOrigin)
			{
				case hm_wrapped:
					OSUnReservePhys(pMapping->CpuVAddr, pMapping->uSize, ui32Flags, pMapping->hOSMemHandle);
					break;
				case hm_wrapped_virtaddr:
					OSUnRegisterMem(pMapping->CpuVAddr, pMapping->uSize, ui32Flags, pMapping->hOSMemHandle);
					break;
				case hm_wrapped_scatter:
					OSUnReserveDiscontigPhys(pMapping->CpuVAddr, pMapping->uSize, ui32Flags, pMapping->hOSMemHandle);
					break;
				case hm_wrapped_scatter_virtaddr:
					OSUnRegisterDiscontigMem(pMapping->CpuVAddr, pMapping->uSize, ui32Flags, pMapping->hOSMemHandle);
					break;
				default:
					break;
			}
			
			DevMemoryFree (pMapping);

			
			OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(BM_MAPPING), pMapping, IMG_NULL);
		}
	}

	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(BM_BUF), pBuf, IMG_NULL);
}



PVRSRV_ERROR
BM_DestroyContext(IMG_HANDLE	hBMContext,
				  IMG_BOOL		*pbDestroyed)
{
	PVRSRV_ERROR eError;
	BM_CONTEXT *pBMContext = (BM_CONTEXT*)hBMContext;

	PVR_DPF ((PVR_DBG_MESSAGE, "BM_DestroyContext"));

	if (pbDestroyed != IMG_NULL)
	{
		*pbDestroyed = IMG_FALSE;
	}

	

	if (pBMContext == IMG_NULL)
	{
		PVR_DPF ((PVR_DBG_ERROR, "BM_DestroyContext: Invalid handle"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	pBMContext->ui32RefCount--;

	if (pBMContext->ui32RefCount > 0)
	{
		
		return PVRSRV_OK;
	}

	eError = ResManFreeResByPtr(pBMContext->hResItem);

	if (eError != PVRSRV_OK)
	{
		PVR_DPF ((PVR_DBG_ERROR, "BM_DestroyContext: ResManFreeResByPtr failed %d",eError));
		return eError;
	}
	
	if (pbDestroyed != IMG_NULL)
	{
		*pbDestroyed = IMG_TRUE;
	}

	return PVRSRV_OK;
}


static PVRSRV_ERROR BM_DestroyContextCallBack(IMG_PVOID		pvParam,
											  IMG_UINT32	ui32Param)
{
	BM_CONTEXT *pBMContext = pvParam;
	BM_CONTEXT **ppBMContext;
	BM_HEAP *psBMHeap, *psTmpBMHeap;
	PVRSRV_DEVICE_NODE *psDeviceNode;

	PVR_UNREFERENCED_PARAMETER(ui32Param);

	

	psDeviceNode = pBMContext->psDeviceNode;

	

	psBMHeap = pBMContext->psBMHeap;
	while(psBMHeap)
	{
		
		if(psBMHeap->ui32Attribs 
		& 	(PVRSRV_BACKINGSTORE_SYSMEM_NONCONTIG
			|PVRSRV_BACKINGSTORE_LOCALMEM_CONTIG))
		{
			if (psBMHeap->pImportArena)
			{
				RA_Delete (psBMHeap->pImportArena);
			}
		}
		else
		{
			PVR_DPF((PVR_DBG_ERROR, "BM_DestroyContext: backing store type unsupported"));
			return PVRSRV_ERROR_GENERIC;
		}

		
		psDeviceNode->pfnMMUDelete(psBMHeap->pMMUHeap);

		
		psTmpBMHeap = psBMHeap;

		
		psBMHeap = psBMHeap->psNext;

		
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, 0, psTmpBMHeap, IMG_NULL);
	}

	

	if (pBMContext->psMMUContext)
	{
		psDeviceNode->pfnMMUFinalise(pBMContext->psMMUContext);
	}
	
	

	if (pBMContext->pBufferHash)
	{
		HASH_Delete(pBMContext->pBufferHash);
	}

	if (pBMContext == psDeviceNode->sDevMemoryInfo.pBMKernelContext)
	{
		
		psDeviceNode->sDevMemoryInfo.pBMKernelContext = IMG_NULL;
	}
	else
	{
		
		for (ppBMContext = &psDeviceNode->sDevMemoryInfo.pBMContext;
			 *ppBMContext;
			 ppBMContext = &((*ppBMContext)->psNext))
		{
			if(*ppBMContext == pBMContext)
			{
				
				*ppBMContext = pBMContext->psNext;

				break;
			}
		}
	}

	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, 0, pBMContext, IMG_NULL);

	return PVRSRV_OK;
}


IMG_HANDLE 
BM_CreateContext(PVRSRV_DEVICE_NODE			*psDeviceNode,
				 IMG_DEV_PHYADDR			*psPDDevPAddr,
				 PVRSRV_PER_PROCESS_DATA	*psPerProc,
				 IMG_BOOL					*pbCreated)
{
	BM_CONTEXT			*pBMContext;
	BM_HEAP				*psBMHeap;
	DEVICE_MEMORY_INFO	*psDevMemoryInfo;
	IMG_BOOL			bKernelContext;
	PRESMAN_CONTEXT		hResManContext;

	PVR_DPF((PVR_DBG_MESSAGE, "BM_CreateContext"));

	if (psPerProc == IMG_NULL)
	{
		bKernelContext = IMG_TRUE;
		hResManContext = psDeviceNode->hResManContext;
	}
	else
	{
		bKernelContext = IMG_FALSE;
		hResManContext = psPerProc->hResManContext;
	}

	if (pbCreated != IMG_NULL)
	{
		*pbCreated = IMG_FALSE;
	}

	
	psDevMemoryInfo = &psDeviceNode->sDevMemoryInfo;

	if (bKernelContext == IMG_FALSE)
	{
		for (pBMContext = psDevMemoryInfo->pBMContext;
			 pBMContext != IMG_NULL;
			 pBMContext = pBMContext->psNext)
		{
			if(ResManFindResourceByPtr(hResManContext, pBMContext->hResItem) == PVRSRV_OK)
			{
				
				pBMContext->ui32RefCount++;

				return (IMG_HANDLE)pBMContext;
			}
		}
	}

	
	if (OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
					 sizeof (struct _BM_CONTEXT_),
					 (IMG_PVOID *)&pBMContext, IMG_NULL) != PVRSRV_OK)
	{
		PVR_DPF ((PVR_DBG_ERROR, "BM_CreateContext: Alloc failed"));
		return IMG_NULL;
	}
	OSMemSet(pBMContext, 0, sizeof (BM_CONTEXT));

	
	pBMContext->psDeviceNode = psDeviceNode;

	
	
	pBMContext->pBufferHash = HASH_Create(32);
	if (pBMContext->pBufferHash==IMG_NULL)
	{
		PVR_DPF ((PVR_DBG_ERROR, "BM_CreateContext: HASH_Create failed"));
		goto cleanup;
	}

	if(psDeviceNode->pfnMMUInitialise(psDeviceNode,
										&pBMContext->psMMUContext,
										psPDDevPAddr) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "BM_CreateContext: MMUInitialise failed"));
		goto cleanup;
	}

	if(bKernelContext)
	{
		
		PVR_ASSERT(psDevMemoryInfo->pBMKernelContext == IMG_NULL);
		psDevMemoryInfo->pBMKernelContext = pBMContext;
	}
	else
	{
		




		PVR_ASSERT(psDevMemoryInfo->pBMKernelContext);
		PVR_ASSERT(psDevMemoryInfo->pBMKernelContext->psBMHeap);

		



		pBMContext->psBMSharedHeap = psDevMemoryInfo->pBMKernelContext->psBMHeap;
		
		


		psBMHeap = pBMContext->psBMSharedHeap;
		while(psBMHeap)
		{
			switch(psBMHeap->sDevArena.DevMemHeapType)
			{
				case DEVICE_MEMORY_HEAP_SHARED:
				case DEVICE_MEMORY_HEAP_SHARED_EXPORTED:
				{
					
					psDeviceNode->pfnMMUInsertHeap(pBMContext->psMMUContext, psBMHeap->pMMUHeap);
					break;
				}
			}
			
			psBMHeap = psBMHeap->psNext;
		}

		
		pBMContext->psNext = psDevMemoryInfo->pBMContext;
		psDevMemoryInfo->pBMContext = pBMContext;
	}

	
	pBMContext->ui32RefCount++;

	
	pBMContext->hResItem = ResManRegisterRes(hResManContext,
											RESMAN_TYPE_DEVICEMEM_CONTEXT,
											pBMContext,
											0,
											BM_DestroyContextCallBack);
	if (pBMContext->hResItem == IMG_NULL)
	{
		PVR_DPF ((PVR_DBG_ERROR, "BM_CreateContext: ResManRegisterRes failed"));
		goto cleanup;
	}

	if (pbCreated != IMG_NULL)
	{
		*pbCreated = IMG_TRUE;
	}
	return (IMG_HANDLE)pBMContext;

cleanup:
	BM_DestroyContextCallBack(pBMContext, 0);

	return IMG_NULL;
}


IMG_HANDLE
BM_CreateHeap (IMG_HANDLE hBMContext,
			   DEVICE_MEMORY_HEAP_INFO *psDevMemHeapInfo)
{
	BM_CONTEXT *pBMContext = (BM_CONTEXT*)hBMContext;
	PVRSRV_DEVICE_NODE *psDeviceNode = pBMContext->psDeviceNode;
	BM_HEAP *psBMHeap;

	PVR_DPF((PVR_DBG_MESSAGE, "BM_CreateHeap"));

	if(!pBMContext)
	{
		return IMG_NULL;
	}

	




	if(pBMContext->ui32RefCount > 0)
	{
		psBMHeap = pBMContext->psBMHeap;

		while(psBMHeap)
		{
			if(psBMHeap->sDevArena.ui32HeapID ==  psDevMemHeapInfo->ui32HeapID)
			
			{
				
				return psBMHeap;
			}
			psBMHeap = psBMHeap->psNext;
		}
	}


	if (OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
						sizeof (BM_HEAP),
						(IMG_PVOID *)&psBMHeap, IMG_NULL) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "BM_CreateHeap: Alloc failed"));
		return IMG_NULL;
	}

	OSMemSet (psBMHeap, 0, sizeof (BM_HEAP));

	psBMHeap->sDevArena.ui32HeapID = psDevMemHeapInfo->ui32HeapID;
	psBMHeap->sDevArena.pszName = psDevMemHeapInfo->pszName;
	psBMHeap->sDevArena.BaseDevVAddr = psDevMemHeapInfo->sDevVAddrBase;
	psBMHeap->sDevArena.ui32Size = psDevMemHeapInfo->ui32HeapSize;
	psBMHeap->sDevArena.DevMemHeapType = psDevMemHeapInfo->DevMemHeapType;
	psBMHeap->sDevArena.psDeviceMemoryHeapInfo = psDevMemHeapInfo;
	psBMHeap->ui32Attribs = psDevMemHeapInfo->ui32Attribs;

	
	psBMHeap->pBMContext = pBMContext;

	psBMHeap->pMMUHeap = psDeviceNode->pfnMMUCreate (pBMContext->psMMUContext,
													&psBMHeap->sDevArena,
													&psBMHeap->pVMArena);
	if (!psBMHeap->pMMUHeap)
	{
		PVR_DPF((PVR_DBG_ERROR, "BM_CreateHeap: MMUCreate failed"));
		goto ErrorExit;
	}

	
	psBMHeap->pImportArena = RA_Create (psDevMemHeapInfo->pszBSName,
										0, 0, IMG_NULL,
										HOST_PAGESIZE(),
										BM_ImportMemory, 
										BM_FreeMemory, 
										IMG_NULL,
										psBMHeap);
	if(psBMHeap->pImportArena == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "BM_CreateHeap: RA_Create failed"));
		goto ErrorExit;
	}

	if(psBMHeap->ui32Attribs & PVRSRV_BACKINGSTORE_LOCALMEM_CONTIG)
	{
		



		psBMHeap->pLocalDevMemArena = psDevMemHeapInfo->psLocalDevMemArena;
		if(psBMHeap->pLocalDevMemArena == IMG_NULL)
		{
			PVR_DPF((PVR_DBG_ERROR, "BM_CreateHeap: LocalDevMemArena null"));
			goto ErrorExit;
		}
	}

	
	psBMHeap->psNext = pBMContext->psBMHeap;
	pBMContext->psBMHeap = psBMHeap;

	return (IMG_HANDLE)psBMHeap;

	
ErrorExit:

	
	if (psBMHeap->pMMUHeap != IMG_NULL)
	{
		psDeviceNode->pfnMMUDelete (psBMHeap->pMMUHeap);
		psDeviceNode->pfnMMUFinalise (pBMContext->psMMUContext);
	}

	
	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, 0, psBMHeap, IMG_NULL);

	return IMG_NULL;
}

IMG_VOID
BM_DestroyHeap (IMG_HANDLE hDevMemHeap)
{
	BM_HEAP* psBMHeap = (BM_HEAP*)hDevMemHeap;
	PVRSRV_DEVICE_NODE *psDeviceNode = psBMHeap->pBMContext->psDeviceNode;

	PVR_DPF((PVR_DBG_MESSAGE, "BM_DestroyHeap"));

	if(psBMHeap)
	{
		BM_HEAP	**ppsBMHeap;
		
		
		if(psBMHeap->ui32Attribs 
		&	(PVRSRV_BACKINGSTORE_SYSMEM_NONCONTIG
			|PVRSRV_BACKINGSTORE_LOCALMEM_CONTIG))
		{
			if (psBMHeap->pImportArena)
			{
				RA_Delete (psBMHeap->pImportArena);
			}
		}
		else
		{
			PVR_DPF((PVR_DBG_ERROR, "BM_DestroyHeap: backing store type unsupported"));
			return;
		}

		
		psDeviceNode->pfnMMUDelete (psBMHeap->pMMUHeap);
		
		
		ppsBMHeap = &psBMHeap->pBMContext->psBMHeap;
		while(*ppsBMHeap)
		{
			if(*ppsBMHeap == psBMHeap)
			{
				
				*ppsBMHeap = psBMHeap->psNext;
				OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, 0, psBMHeap, IMG_NULL);
				break;
			}
			ppsBMHeap = &((*ppsBMHeap)->psNext);
		}
	}
	else
	{
		PVR_DPF ((PVR_DBG_ERROR, "BM_DestroyHeap: invalid heap handle"));	
	}
}


IMG_BOOL 
BM_Reinitialise (PVRSRV_DEVICE_NODE *psDeviceNode)
{

	PVR_DPF((PVR_DBG_MESSAGE, "BM_Reinitialise"));
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);


	return IMG_TRUE;
}

IMG_BOOL
BM_Alloc (  IMG_HANDLE			hDevMemHeap,
			IMG_DEV_VIRTADDR	*psDevVAddr,
			IMG_SIZE_T			uSize,
			IMG_UINT32			*pui32Flags,
			IMG_UINT32			uDevVAddrAlignment,
			BM_HANDLE			*phBuf)
{
	BM_BUF *pBuf;
	BM_CONTEXT *pBMContext;
	BM_HEAP *psBMHeap;
	SYS_DATA *psSysData;
	IMG_UINT32 uFlags = 0;

	if (pui32Flags)
	{
		uFlags = *pui32Flags;
	}

	PVR_DPF ((PVR_DBG_MESSAGE,
		  "BM_Alloc (uSize=0x%x, uFlags=0x%x, uDevVAddrAlignment=0x%x)",
			uSize, uFlags, uDevVAddrAlignment));

	if (SysAcquireData(&psSysData) != PVRSRV_OK)
	{
		return IMG_FALSE;
	}

	psBMHeap = (BM_HEAP*)hDevMemHeap;
	pBMContext = psBMHeap->pBMContext;

	if(uDevVAddrAlignment == 0)
	{
		uDevVAddrAlignment = 1;
	}

	
	if (OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
				   sizeof (BM_BUF),
				   (IMG_PVOID *)&pBuf, IMG_NULL) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "BM_Alloc: BM_Buf alloc FAILED"));
		return IMG_FALSE;
	}
	OSMemSet(pBuf, 0, sizeof (BM_BUF));

	
	if (AllocMemory(pBMContext,
					psBMHeap,
					psDevVAddr,
					uSize,
					uFlags,
					uDevVAddrAlignment,
					pBuf) != IMG_TRUE)
	{
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof (BM_BUF), pBuf, IMG_NULL);
		PVR_DPF((PVR_DBG_ERROR, "BM_Alloc: AllocMemory FAILED"));
		return IMG_FALSE;
	}

	PVR_DPF ((PVR_DBG_MESSAGE,
		  "BM_Alloc (uSize=0x%x, uFlags=0x%x)=%08X",
		  uSize, uFlags, pBuf));

	
	pBuf->ui32RefCount = 1;
	*phBuf = (BM_HANDLE)pBuf;
	*pui32Flags = uFlags | psBMHeap->ui32Attribs;

	return IMG_TRUE;
}



IMG_BOOL
BM_Wrap (	IMG_HANDLE hDevMemHeap,
			IMG_UINT32 ui32Size,
			IMG_UINT32 ui32Offset,
			IMG_BOOL bPhysContig,
			IMG_SYS_PHYADDR *psSysAddr,
			IMG_VOID *pvCPUVAddr,
			IMG_UINT32 *pui32Flags,
			BM_HANDLE *phBuf)
{
	BM_BUF *pBuf;
	BM_CONTEXT *psBMContext;
	BM_HEAP *psBMHeap;
	SYS_DATA *psSysData;
	IMG_SYS_PHYADDR sHashAddress;
	IMG_UINT32 uFlags;

	psBMHeap = (BM_HEAP*)hDevMemHeap;
	psBMContext = psBMHeap->pBMContext;

	uFlags = psBMHeap->ui32Attribs & (PVRSRV_HAP_CACHETYPE_MASK | PVRSRV_HAP_MAPTYPE_MASK);

	if(pui32Flags)
		uFlags |= *pui32Flags;

	PVR_DPF ((PVR_DBG_MESSAGE,
		  "BM_Wrap (uSize=0x%x, uOffset=0x%x, bPhysContig=0x%x, pvCPUVAddr=0x%x, uFlags=0x%x)",
			ui32Size, ui32Offset, bPhysContig, pvCPUVAddr, uFlags));

	if(SysAcquireData (&psSysData) != PVRSRV_OK)
		return IMG_FALSE;

	
	sHashAddress = psSysAddr[0];
	
	
	sHashAddress.uiAddr += ui32Offset;

	
	pBuf = (BM_BUF *)HASH_Retrieve(psBMContext->pBufferHash, (IMG_UINTPTR_T) sHashAddress.uiAddr);

	if(pBuf)
	{
		IMG_UINT32 ui32MappingSize = HOST_PAGEALIGN (ui32Size + ui32Offset);

		
		if(pBuf->pMapping->uSize == ui32MappingSize && (pBuf->pMapping->eCpuMemoryOrigin == hm_wrapped ||
														pBuf->pMapping->eCpuMemoryOrigin == hm_wrapped_virtaddr))
		{
			PVR_DPF((PVR_DBG_MESSAGE,
					"BM_Wrap (Matched previous Wrap! uSize=0x%x, uOffset=0x%x, SysAddr=%08X)",
					ui32Size, ui32Offset, sHashAddress.uiAddr));

			pBuf->ui32RefCount++;
			*phBuf = (BM_HANDLE)pBuf;
			if(pui32Flags)
				*pui32Flags = uFlags;

			return IMG_TRUE;
		}
	}

	
	if (OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
						sizeof (BM_BUF),
						(IMG_PVOID *)&pBuf, IMG_NULL) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "BM_Wrap: BM_Buf alloc FAILED"));
		return IMG_FALSE;
	}
	OSMemSet(pBuf, 0, sizeof (BM_BUF));

	
	if (WrapMemory (psBMHeap, ui32Size, ui32Offset, bPhysContig, psSysAddr, pvCPUVAddr, uFlags, pBuf) != IMG_TRUE)
	{
		PVR_DPF((PVR_DBG_ERROR, "BM_Wrap: WrapMemory FAILED"));
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof (BM_BUF), pBuf, IMG_NULL);
		return IMG_FALSE;
	}

	
	if(pBuf->pMapping->eCpuMemoryOrigin == hm_wrapped || pBuf->pMapping->eCpuMemoryOrigin == hm_wrapped_virtaddr)
	{
		
		PVR_ASSERT(SysSysPAddrToCpuPAddr(sHashAddress).uiAddr == pBuf->CpuPAddr.uiAddr);

		if (!HASH_Insert (psBMContext->pBufferHash, (IMG_UINTPTR_T) sHashAddress.uiAddr, (IMG_UINTPTR_T)pBuf))
		{
			FreeBuf (pBuf, uFlags);
			OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof (BM_BUF), pBuf, IMG_NULL);
			PVR_DPF((PVR_DBG_ERROR, "BM_Wrap: HASH_Insert FAILED"));
			return IMG_FALSE;
		}
	}

	PVR_DPF ((PVR_DBG_MESSAGE,
			"BM_Wrap (uSize=0x%x, uFlags=0x%x)=%08X(devVAddr=%08X)",
			ui32Size, uFlags, pBuf, pBuf->DevVAddr.uiAddr));

	
	pBuf->ui32RefCount = 1;
	*phBuf = (BM_HANDLE)pBuf;
	if(pui32Flags)
		*pui32Flags = uFlags;

	return IMG_TRUE;
}


void
BM_Free (BM_HANDLE hBuf,
		IMG_UINT32 ui32Flags)
{
	BM_BUF *pBuf = (BM_BUF *)hBuf;
	SYS_DATA *psSysData;
	IMG_SYS_PHYADDR sHashAddr;

	PVR_DPF ((PVR_DBG_MESSAGE, "BM_Free (h=%08X)", hBuf));
	/* Calling BM_Free with NULL hBuf is either a bug or out-of-memory condition.
	 * Bail out if in debug mode, continue in release builds */
	PVR_ASSERT (pBuf!=IMG_NULL);
#if !defined(DEBUG)
	if (!pBuf)
	{
		return;
	}
#endif

	if(SysAcquireData (&psSysData) != PVRSRV_OK)
		return;

	pBuf->ui32RefCount--;

	if(pBuf->ui32RefCount == 0)
	{
		if(pBuf->pMapping->eCpuMemoryOrigin == hm_wrapped || pBuf->pMapping->eCpuMemoryOrigin == hm_wrapped_virtaddr)
		{
			sHashAddr = SysCpuPAddrToSysPAddr(pBuf->CpuPAddr);

			HASH_Remove (pBuf->pMapping->pBMHeap->pBMContext->pBufferHash,	(IMG_UINTPTR_T)sHashAddr.uiAddr);
		}
		FreeBuf (pBuf, ui32Flags);
	}
}


IMG_CPU_VIRTADDR
BM_HandleToCpuVaddr (BM_HANDLE hBuf)
{
	BM_BUF *pBuf = (BM_BUF *)hBuf;

	PVR_ASSERT (pBuf != IMG_NULL);
	PVR_DPF ((PVR_DBG_MESSAGE,
				"BM_HandleToCpuVaddr(h=%08X)=%08X",
				hBuf, pBuf->CpuVAddr));
	return pBuf->CpuVAddr;
}


IMG_DEV_VIRTADDR
BM_HandleToDevVaddr (BM_HANDLE hBuf)
{
	BM_BUF *pBuf = (BM_BUF *)hBuf;

	PVR_ASSERT (pBuf != IMG_NULL);
	PVR_DPF ((PVR_DBG_MESSAGE, "BM_HandleToDevVaddr(h=%08X)=%08X", hBuf, pBuf->DevVAddr));
	return pBuf->DevVAddr;
}


IMG_SYS_PHYADDR
BM_HandleToSysPaddr (BM_HANDLE hBuf)
{
	BM_BUF *pBuf = (BM_BUF *)hBuf;

	PVR_ASSERT (pBuf != IMG_NULL);
	PVR_DPF ((PVR_DBG_MESSAGE, "BM_HandleToSysPaddr(h=%08X)=%08X", hBuf, pBuf->CpuPAddr.uiAddr));
	return SysCpuPAddrToSysPAddr (pBuf->CpuPAddr);
}

IMG_HANDLE
BM_HandleToOSMemHandle(BM_HANDLE hBuf)
{
	BM_BUF *pBuf = (BM_BUF *)hBuf;

	PVR_ASSERT (pBuf != IMG_NULL);

	PVR_DPF ((PVR_DBG_MESSAGE,
				"BM_HandleToOSMemHandle(h=%08X)=%08X",
				hBuf, pBuf->hOSMemHandle));
	return pBuf->hOSMemHandle;
}

IMG_BOOL
BM_ContiguousStatistics (IMG_UINT32 uFlags,
						 IMG_UINT32 *pTotalBytes,
						 IMG_UINT32 *pAvailableBytes)
{
	if (pAvailableBytes || pTotalBytes || uFlags);
	return IMG_FALSE;
}


static IMG_BOOL
DevMemoryAlloc (BM_CONTEXT *pBMContext,
				BM_MAPPING *pMapping, 
				IMG_SIZE_T *pActualSize,
				IMG_UINT32 uFlags,
				IMG_UINT32 dev_vaddr_alignment,
				IMG_DEV_VIRTADDR *pDevVAddr)
{
	PVRSRV_DEVICE_NODE *psDeviceNode;
#ifdef PDUMP
	IMG_UINT32 ui32PDumpSize = pMapping->uSize;
#endif

	psDeviceNode = pBMContext->psDeviceNode;

	if(uFlags & PVRSRV_MEM_INTERLEAVED)
	{
		
		pMapping->uSize *= 2;
	}
	
#ifdef PDUMP
	if(uFlags & PVRSRV_MEM_DUMMY)
	{
		
		ui32PDumpSize = HOST_PAGESIZE();
	}
#endif

	
	if (!psDeviceNode->pfnMMUAlloc (pMapping->pBMHeap->pMMUHeap, 
									pMapping->uSize, 
									pActualSize, 
									0,
									dev_vaddr_alignment, 
									&(pMapping->DevVAddr)))
	{
		PVR_DPF((PVR_DBG_ERROR, "DevMemoryAlloc ERROR MMU_Alloc"));
		return IMG_FALSE;
	}

#ifdef SUPPORT_SGX_MMU_BYPASS
	EnableHostAccess(pBMContext->psMMUContext);
#endif

	
	
	PDUMPMALLOCPAGES(psDeviceNode->sDevId.eDeviceType, pMapping->DevVAddr.uiAddr, pMapping->CpuVAddr, pMapping->hOSMemHandle, ui32PDumpSize, (IMG_HANDLE)pMapping);

	switch (pMapping->eCpuMemoryOrigin)
	{
		case hm_wrapped:
		case hm_wrapped_virtaddr:
		case hm_contiguous:
		{
			psDeviceNode->pfnMMUMapPages (	pMapping->pBMHeap->pMMUHeap,
							pMapping->DevVAddr,
							SysCpuPAddrToSysPAddr (pMapping->CpuPAddr),
							pMapping->uSize,
							uFlags,
							(IMG_HANDLE)pMapping);

			*pDevVAddr = pMapping->DevVAddr;
			break;
		}
		case hm_env:
		{
			psDeviceNode->pfnMMUMapShadow (	pMapping->pBMHeap->pMMUHeap,
							pMapping->DevVAddr,
							pMapping->uSize,
							pMapping->CpuVAddr,
							pMapping->hOSMemHandle,
							pDevVAddr,
							uFlags,
							(IMG_HANDLE)pMapping);
			break;
		}
		case hm_wrapped_scatter:
		case hm_wrapped_scatter_virtaddr:
		{
			psDeviceNode->pfnMMUMapScatter (pMapping->pBMHeap->pMMUHeap,
							pMapping->DevVAddr,
							pMapping->psSysAddr,
							pMapping->uSize,
							uFlags,
							(IMG_HANDLE)pMapping);

			*pDevVAddr = pMapping->DevVAddr;
			break;
		}
		default:
			PVR_DPF((PVR_DBG_ERROR,
				"Illegal value %d for pMapping->eCpuMemoryOrigin",
				pMapping->eCpuMemoryOrigin));
			return IMG_FALSE;
	}

#ifdef SUPPORT_SGX_MMU_BYPASS
	DisableHostAccess(pBMContext->psMMUContext);
#endif

	return IMG_TRUE;
}

static void
DevMemoryFree (BM_MAPPING *pMapping)
{
	PVRSRV_DEVICE_NODE *psDeviceNode;
#ifdef PDUMP
	IMG_UINT32 ui32PSize;
#endif

#ifdef PDUMP
	
	if(pMapping->ui32Flags & PVRSRV_MEM_DUMMY)
	{
		
		ui32PSize = HOST_PAGESIZE();
	}
	else
	{
		ui32PSize = pMapping->uSize;
	}

	PDUMPFREEPAGES(pMapping->pBMHeap, pMapping->DevVAddr,
				   ui32PSize, (IMG_HANDLE)pMapping,
				   (IMG_BOOL)(pMapping->ui32Flags & PVRSRV_MEM_INTERLEAVED));
#endif

	psDeviceNode = pMapping->pBMHeap->pBMContext->psDeviceNode;

	psDeviceNode->pfnMMUFree (pMapping->pBMHeap->pMMUHeap, pMapping->DevVAddr, pMapping->uSize);
}

static IMG_BOOL
BM_ImportMemory (void *pH,
			  IMG_SIZE_T uRequestSize,
			  IMG_SIZE_T *pActualSize,
			  BM_MAPPING **ppsMapping,
			  IMG_UINT32 uFlags,
			  IMG_UINTPTR_T *pBase)
{
	BM_MAPPING *pMapping;
	BM_HEAP *pBMHeap = pH;
	BM_CONTEXT *pBMContext = pBMHeap->pBMContext;
	IMG_BOOL bResult;
	IMG_SIZE_T uSize;
	IMG_SIZE_T uPSize;
	IMG_UINT32 uDevVAddrAlignment = 0;

	PVR_DPF ((PVR_DBG_MESSAGE,
			  "BM_ImportMemory (pBMContext=%08X, uRequestSize=0x%x, uFlags=0x%x, uAlign=0x%x)",
			  pBMContext, uRequestSize, uFlags, uDevVAddrAlignment));

	PVR_ASSERT (ppsMapping != IMG_NULL);
	PVR_ASSERT (pBMContext != IMG_NULL);

	uSize = HOST_PAGEALIGN (uRequestSize);
	PVR_ASSERT (uSize >= uRequestSize);

	if (OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
						sizeof (BM_MAPPING),
						(IMG_PVOID *)&pMapping, IMG_NULL) != PVRSRV_OK)
	{
		PVR_DPF ((PVR_DBG_ERROR, "BM_ImportMemory: failed BM_MAPPING alloc"));
		goto fail_exit;
	}

	pMapping->hOSMemHandle = 0;
	pMapping->CpuVAddr = 0;
	pMapping->DevVAddr.uiAddr = 0;
	pMapping->CpuPAddr.uiAddr = 0;
	pMapping->uSize = uSize;
	pMapping->pBMHeap = pBMHeap;
	pMapping->ui32Flags = uFlags;

	
	if (pActualSize)
	{
		*pActualSize = uSize;
	}
	
	
	if(pMapping->ui32Flags & PVRSRV_MEM_DUMMY)
	{
		uPSize = HOST_PAGESIZE();
	}
	else
	{
		uPSize = pMapping->uSize;
	}

	

	if(pBMHeap->ui32Attribs & PVRSRV_BACKINGSTORE_SYSMEM_NONCONTIG)
	{
		
		if (OSAllocPages(pBMHeap->ui32Attribs,
						 uPSize, 
						 (IMG_VOID **)&pMapping->CpuVAddr,
						 &pMapping->hOSMemHandle) != PVRSRV_OK) 
		{
			PVR_DPF((PVR_DBG_ERROR,
					"BM_ImportMemory: OSAllocPages(0x%x) failed",
					uPSize));
			goto fail_mapping_alloc;
		}

		
		pMapping->eCpuMemoryOrigin = hm_env;
	}
	else if(pBMHeap->ui32Attribs & PVRSRV_BACKINGSTORE_LOCALMEM_CONTIG)
	{
		IMG_SYS_PHYADDR sSysPAddr;

		
		PVR_ASSERT(pBMHeap->pLocalDevMemArena != IMG_NULL);

		if (!RA_Alloc (pBMHeap->pLocalDevMemArena,
					   uPSize,
					   IMG_NULL,
					   IMG_NULL,
					   0,
					   HOST_PAGESIZE(),
					   0,
					   (IMG_UINTPTR_T *)&sSysPAddr.uiAddr))
		{
			PVR_DPF((PVR_DBG_ERROR, "BM_ImportMemory: RA_Alloc(0x%x) FAILED", uPSize));
			goto fail_mapping_alloc;
		}

		
		pMapping->CpuPAddr = SysSysPAddrToCpuPAddr(sSysPAddr);
		if(OSReservePhys(pMapping->CpuPAddr,
						 uPSize,
						 pBMHeap->ui32Attribs,
						 &pMapping->CpuVAddr,
						 &pMapping->hOSMemHandle) != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,	"BM_ImportMemory: OSReservePhys failed"));
			goto fail_dev_mem_alloc;
		}

		
		pMapping->eCpuMemoryOrigin = hm_contiguous;
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR,	"BM_ImportMemory: Invalid backing store type"));
		goto fail_mapping_alloc;
	}

	
	bResult = DevMemoryAlloc (pBMContext, pMapping, IMG_NULL, uFlags,
							  uDevVAddrAlignment, &pMapping->DevVAddr);
	if (!bResult)
	{
		PVR_DPF((PVR_DBG_ERROR,
				"BM_ImportMemory: DevMemoryAlloc(0x%x) failed",
				pMapping->uSize));
		goto fail_dev_mem_alloc;
	}

	PVR_ASSERT (uDevVAddrAlignment>1?(pMapping->DevVAddr.uiAddr%uDevVAddrAlignment)==0:1);

	*pBase = pMapping->DevVAddr.uiAddr;
	*ppsMapping = pMapping;

	PVR_DPF ((PVR_DBG_MESSAGE, "BM_ImportMemory: IMG_TRUE"));
	return IMG_TRUE;

fail_dev_mem_alloc:
	if (pMapping && (pMapping->CpuVAddr || pMapping->hOSMemHandle))
	{
		
		if(pMapping->ui32Flags & PVRSRV_MEM_INTERLEAVED)
		{
			pMapping->uSize /= 2;
		}

		if(pMapping->ui32Flags & PVRSRV_MEM_DUMMY)
		{
			uPSize = HOST_PAGESIZE();
		}
		else
		{
			uPSize = pMapping->uSize;
		}

		if(pBMHeap->ui32Attribs & PVRSRV_BACKINGSTORE_SYSMEM_NONCONTIG)
		{
			OSFreePages(pBMHeap->ui32Attribs, 
						  uPSize, 
						  (void *) pMapping->CpuVAddr,
						  pMapping->hOSMemHandle);
		}
		else
		{
			IMG_SYS_PHYADDR sSysPAddr;

			if(pMapping->CpuVAddr)
			{
				OSUnReservePhys(pMapping->CpuVAddr, uPSize, pBMHeap->ui32Attribs, pMapping->hOSMemHandle);
			}
			sSysPAddr = SysCpuPAddrToSysPAddr(pMapping->CpuPAddr);		
			RA_Free (pBMHeap->pLocalDevMemArena, sSysPAddr.uiAddr, IMG_FALSE);	
		}
	}
fail_mapping_alloc:
	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(BM_MAPPING), pMapping, IMG_NULL);
fail_exit:
	return IMG_FALSE;
}


static void
BM_FreeMemory (void *h, IMG_UINTPTR_T _base, BM_MAPPING *psMapping)
{
	BM_HEAP *pBMHeap = h;
	IMG_SIZE_T uPSize;

	PVR_UNREFERENCED_PARAMETER (_base);

	PVR_DPF ((PVR_DBG_MESSAGE,
			  "BM_FreeMemory (h=%08X, base=0x%x, psMapping=0x%x)", h, _base, psMapping));

	PVR_ASSERT (psMapping != IMG_NULL);

	DevMemoryFree (psMapping);

	
	if((psMapping->ui32Flags & PVRSRV_MEM_INTERLEAVED) != 0)
	{
		psMapping->uSize /= 2;
	}
	
	if(psMapping->ui32Flags & PVRSRV_MEM_DUMMY)
	{
		uPSize = HOST_PAGESIZE();
	}
	else
	{
		uPSize = psMapping->uSize;
	}
	
	if(pBMHeap->ui32Attribs & PVRSRV_BACKINGSTORE_SYSMEM_NONCONTIG)
	{
		OSFreePages(pBMHeap->ui32Attribs,
						uPSize,
						(void *) psMapping->CpuVAddr,
						psMapping->hOSMemHandle);
	}
	else if(pBMHeap->ui32Attribs & PVRSRV_BACKINGSTORE_LOCALMEM_CONTIG)
	{
		IMG_SYS_PHYADDR sSysPAddr;

		OSUnReservePhys(psMapping->CpuVAddr, uPSize, pBMHeap->ui32Attribs, psMapping->hOSMemHandle);

		sSysPAddr = SysCpuPAddrToSysPAddr(psMapping->CpuPAddr);

		RA_Free (pBMHeap->pLocalDevMemArena, sSysPAddr.uiAddr, IMG_FALSE);
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR,	"BM_FreeMemory: Invalid backing store type"));
	}

	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(BM_MAPPING), psMapping, IMG_NULL);

	PVR_DPF((PVR_DBG_MESSAGE,
			"..BM_FreeMemory (h=%08X, base=0x%x, psMapping=0x%x)",
			h, _base, psMapping));
}

PVRSRV_ERROR BM_GetPhysPageAddr(PVRSRV_KERNEL_MEM_INFO *psMemInfo,
								IMG_DEV_VIRTADDR sDevVPageAddr,
								IMG_DEV_PHYADDR *psDevPAddr)
{
	PVRSRV_DEVICE_NODE *psDeviceNode;

	PVR_DPF((PVR_DBG_MESSAGE, "BM_GetPhysPageAddr"));

	if(!psMemInfo || !psDevPAddr)
	{
		PVR_DPF((PVR_DBG_ERROR,	"BM_GetPhysPageAddr: Invalid params"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	
	PVR_ASSERT((sDevVPageAddr.uiAddr & 0xFFF) == 0);

	psDeviceNode = ((BM_BUF*)psMemInfo->sMemBlk.hBuffer)->pMapping->pBMHeap->pBMContext->psDeviceNode;

	*psDevPAddr = psDeviceNode->pfnMMUGetPhysPageAddr(((BM_BUF*)psMemInfo->sMemBlk.hBuffer)->pMapping->pBMHeap->pMMUHeap, 
												sDevVPageAddr);

	return PVRSRV_OK;
}


PVRSRV_ERROR BM_GetHeapInfo(IMG_HANDLE hDevMemHeap, PVRSRV_HEAP_INFO *psHeapInfo)
{
	BM_HEAP *psBMHeap = (BM_HEAP *)hDevMemHeap;

	PVR_DPF((PVR_DBG_VERBOSE, "BM_GetHeapInfo"));

	psHeapInfo->hDevMemHeap = hDevMemHeap;
	psHeapInfo->sDevVAddrBase = psBMHeap->sDevArena.BaseDevVAddr;
	psHeapInfo->ui32HeapByteSize = psBMHeap->sDevArena.ui32Size;
	psHeapInfo->ui32Attribs = psBMHeap->ui32Attribs;

	return PVRSRV_OK;
}


MMU_CONTEXT* BM_GetMMUContext(IMG_HANDLE hDevMemHeap)
{
	BM_HEAP *pBMHeap = (BM_HEAP*)hDevMemHeap;

	PVR_DPF((PVR_DBG_VERBOSE, "BM_GetMMUContext"));

	return pBMHeap->pBMContext->psMMUContext;
}

MMU_CONTEXT* BM_GetMMUContextFromMemContext(IMG_HANDLE hDevMemContext)
{
	BM_CONTEXT *pBMContext = (BM_CONTEXT*)hDevMemContext;

	PVR_DPF ((PVR_DBG_VERBOSE, "BM_GetMMUContextFromMemContext"));

	return pBMContext->psMMUContext;
}

IMG_HANDLE BM_GetMMUHeap(IMG_HANDLE hDevMemHeap)
{
	PVR_DPF((PVR_DBG_VERBOSE, "BM_GetMMUHeap"));

	return (IMG_HANDLE)((BM_HEAP*)hDevMemHeap)->pMMUHeap;
}


PVRSRV_DEVICE_NODE* BM_GetDeviceNode(IMG_HANDLE hDevMemContext)
{
	PVR_DPF((PVR_DBG_VERBOSE, "BM_GetDeviceNode"));

	return ((BM_CONTEXT*)hDevMemContext)->psDeviceNode;
}


IMG_HANDLE BM_GetMappingHandle(PVRSRV_KERNEL_MEM_INFO *psMemInfo)
{
	PVR_DPF((PVR_DBG_VERBOSE, "BM_GetMappingHandle"));

	return ((BM_BUF*)psMemInfo->sMemBlk.hBuffer)->pMapping->hOSMemHandle;
}

