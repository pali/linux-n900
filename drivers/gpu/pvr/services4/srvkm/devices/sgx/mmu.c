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

#include "sgxdefs.h"
#include "sgxmmu.h"
#include "services_headers.h"
#include "buffer_manager.h"
#include "hash.h"
#include "ra.h"
#include "pdump_km.h"
#include "sgxapi_km.h"
#include "sgxinfo.h"
#include "sgxinfokm.h"
#include "mmu.h"

typedef struct _MMU_PT_INFO_
{
	
	IMG_VOID *hPTPageOSMemHandle;
	IMG_CPU_VIRTADDR PTPageCpuVAddr;
	IMG_UINT32 ui32ValidPTECount;
} MMU_PT_INFO;

struct _MMU_CONTEXT_
{
	
	PVRSRV_DEVICE_NODE *psDeviceNode;

	
	IMG_CPU_VIRTADDR pvPDCpuVAddr;
	IMG_DEV_PHYADDR sPDDevPAddr;

	IMG_VOID *hPDOSMemHandle;

	
	MMU_PT_INFO *apsPTInfoList[1024];

	PVRSRV_SGXDEV_INFO *psDevInfo;

	struct _MMU_CONTEXT_ *psNext;
};

struct _MMU_HEAP_
{
	MMU_CONTEXT *psMMUContext;

	IMG_UINT32 ui32PTBaseIndex;
	IMG_UINT32 ui32PTPageCount;
	IMG_UINT32 ui32PTEntryCount;

	
	RA_ARENA *psVMArena;

	DEV_ARENA_DESCRIPTOR *psDevArena;
};

#if defined (SUPPORT_SGX_MMU_DUMMY_PAGE)
#define DUMMY_DATA_PAGE_SIGNATURE	0xDEADBEEF
#endif

#if defined(PDUMP)
static IMG_VOID
MMU_PDumpPageTables	(MMU_HEAP *pMMUHeap,
					 IMG_DEV_VIRTADDR DevVAddr,
					 IMG_SIZE_T uSize,
					 IMG_BOOL bForUnmap,
					 IMG_HANDLE hUniqueTag);
#endif 

#define PAGE_TEST					0
#if PAGE_TEST
static void PageTest(void* pMem, IMG_DEV_PHYADDR sDevPAddr);
#endif


#ifdef SUPPORT_SGX_MMU_BYPASS
IMG_VOID
EnableHostAccess (MMU_CONTEXT *psMMUContext)
{
	IMG_UINT32 ui32RegVal;
	IMG_VOID *pvRegsBaseKM = psMMUContext->psDevInfo->pvRegsBaseKM;

	


	ui32RegVal = OSReadHWReg(pvRegsBaseKM, EUR_CR_BIF_CTRL);

	OSWriteHWReg(pvRegsBaseKM,
				EUR_CR_BIF_CTRL,
				ui32RegVal | EUR_CR_BIF_CTRL_MMU_BYPASS_HOST_MASK);
	
	PDUMPREG(EUR_CR_BIF_CTRL, EUR_CR_BIF_CTRL_MMU_BYPASS_HOST_MASK);
}

IMG_VOID
DisableHostAccess (MMU_CONTEXT *psMMUContext)
{
	IMG_UINT32 ui32RegVal;
	IMG_VOID *pvRegsBaseKM = psMMUContext->psDevInfo->pvRegsBaseKM;

	



	OSWriteHWReg(pvRegsBaseKM,
				EUR_CR_BIF_CTRL,
				ui32RegVal & ~EUR_CR_BIF_CTRL_MMU_BYPASS_HOST_MASK);
	
	PDUMPREG(EUR_CR_BIF_CTRL, 0);
}
#endif

IMG_VOID MMU_InvalidateDirectoryCache(PVRSRV_SGXDEV_INFO *psDevInfo)
{
	psDevInfo->ui32CacheControl |= SGX_BIF_INVALIDATE_PDCACHE;	
}


IMG_VOID MMU_InvalidatePageTableCache(PVRSRV_SGXDEV_INFO *psDevInfo)
{
	psDevInfo->ui32CacheControl |= SGX_BIF_INVALIDATE_PTCACHE;	
}


static IMG_BOOL
_AllocPageTables (MMU_HEAP *pMMUHeap)
{
	PVR_DPF ((PVR_DBG_MESSAGE, "_AllocPageTables()"));

	PVR_ASSERT (pMMUHeap!=IMG_NULL);
	PVR_ASSERT (HOST_PAGESIZE() == SGX_MMU_PAGE_SIZE);

	if (pMMUHeap == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "_AllocPageTables: invalid parameter"));
		return IMG_FALSE;
	}

	





	
	pMMUHeap->ui32PTEntryCount = pMMUHeap->psDevArena->ui32Size >> SGX_MMU_PAGE_SHIFT;

	
	pMMUHeap->ui32PTBaseIndex = (pMMUHeap->psDevArena->BaseDevVAddr.uiAddr & (SGX_MMU_PD_MASK | SGX_MMU_PT_MASK)) >> SGX_MMU_PAGE_SHIFT;

	


	pMMUHeap->ui32PTPageCount = (pMMUHeap->ui32PTEntryCount + SGX_MMU_PT_SIZE - 1) >> SGX_MMU_PT_SHIFT;

	return IMG_TRUE;
}

static IMG_VOID
_DeferredFreePageTable (MMU_HEAP *pMMUHeap, IMG_UINT32 ui32PTIndex)
{
	IMG_UINT32 *pui32PDEntry;
	IMG_UINT32 i;
	IMG_UINT32 ui32PDIndex;
	SYS_DATA *psSysData;
	MMU_PT_INFO **ppsPTInfoList;

	if (SysAcquireData(&psSysData) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "_DeferredFreePageTables: ERROR call to SysAcquireData failed"));
		return;
	}

	
	ui32PDIndex = pMMUHeap->psDevArena->BaseDevVAddr.uiAddr >> (SGX_MMU_PAGE_SHIFT + SGX_MMU_PT_SHIFT);

	
	ppsPTInfoList = &pMMUHeap->psMMUContext->apsPTInfoList[ui32PDIndex];

	{
		
		PVR_ASSERT(ppsPTInfoList[ui32PTIndex] == IMG_NULL || ppsPTInfoList[ui32PTIndex]->ui32ValidPTECount == 0);
	}

	
	PDUMPCOMMENT("Free page table (page count == %08X)", pMMUHeap->ui32PTPageCount);
	if(ppsPTInfoList[ui32PTIndex] && ppsPTInfoList[ui32PTIndex]->PTPageCpuVAddr)
	{
		PDUMPFREEPAGETABLE(PVRSRV_DEVICE_TYPE_SGX, ppsPTInfoList[ui32PTIndex]->PTPageCpuVAddr, SGX_MMU_PAGE_SIZE, PDUMP_PT_UNIQUETAG);
	}

	switch(pMMUHeap->psDevArena->DevMemHeapType)
	{
		case DEVICE_MEMORY_HEAP_SHARED :
		case DEVICE_MEMORY_HEAP_SHARED_EXPORTED :
		{
			
			MMU_CONTEXT *psMMUContext = (MMU_CONTEXT*)pMMUHeap->psMMUContext->psDevInfo->pvMMUContextList;

			while(psMMUContext)
			{
				
				pui32PDEntry = (IMG_UINT32*)psMMUContext->pvPDCpuVAddr;
				pui32PDEntry += ui32PDIndex;

#if defined(SUPPORT_SGX_MMU_DUMMY_PAGE)
				
				pui32PDEntry[ui32PTIndex] = psMMUContext->psDevInfo->sDummyPTDevPAddr.uiAddr | SGX_MMU_PDE_VALID;
#else
				
				pui32PDEntry[ui32PTIndex] = 0;
#endif

				
				PDUMPMEM2(PVRSRV_DEVICE_TYPE_SGX, (IMG_VOID*)&pui32PDEntry[ui32PTIndex], sizeof(IMG_UINT32), 0, IMG_FALSE, PDUMP_PT_UNIQUETAG, PDUMP_PT_UNIQUETAG);

				
				psMMUContext = psMMUContext->psNext;
			}
			break;
		}
		case DEVICE_MEMORY_HEAP_PERCONTEXT :
		case DEVICE_MEMORY_HEAP_KERNEL :
		{
			
			pui32PDEntry = (IMG_UINT32*)pMMUHeap->psMMUContext->pvPDCpuVAddr;
			pui32PDEntry += ui32PDIndex;

#if defined(SUPPORT_SGX_MMU_DUMMY_PAGE)
			
			pui32PDEntry[ui32PTIndex] = pMMUHeap->psMMUContext->psDevInfo->sDummyPTDevPAddr.uiAddr | SGX_MMU_PDE_VALID;
#else
			
			pui32PDEntry[ui32PTIndex] = 0;
#endif

			
			PDUMPMEM2(PVRSRV_DEVICE_TYPE_SGX, (IMG_VOID*)&pui32PDEntry[ui32PTIndex], sizeof(IMG_UINT32), 0, IMG_FALSE, PDUMP_PD_UNIQUETAG, PDUMP_PT_UNIQUETAG);
			break;
		}
		default:
		{
			PVR_DPF((PVR_DBG_ERROR, "_DeferredFreePagetable: ERROR invalid heap type"));
			return;
		}
	}

	
	if(ppsPTInfoList[ui32PTIndex] != IMG_NULL)
	{
		if(ppsPTInfoList[ui32PTIndex]->PTPageCpuVAddr != IMG_NULL)
		{
			IMG_PUINT32 pui32Tmp;

			pui32Tmp = (IMG_UINT32*)ppsPTInfoList[ui32PTIndex]->PTPageCpuVAddr;

			


			for(i=0; (i<pMMUHeap->ui32PTEntryCount) && (i<1024); i++)
			{
				pui32Tmp[i] = 0;
			}

			



			if(pMMUHeap->psDevArena->psDeviceMemoryHeapInfo->psLocalDevMemArena == IMG_NULL)
			{
				OSFreePages(PVRSRV_HAP_WRITECOMBINE | PVRSRV_HAP_KERNEL_ONLY,
							  SGX_MMU_PAGE_SIZE,
							  ppsPTInfoList[ui32PTIndex]->PTPageCpuVAddr,
							  ppsPTInfoList[ui32PTIndex]->hPTPageOSMemHandle);
			}
			else
			{
				IMG_SYS_PHYADDR sSysPAddr;
				IMG_CPU_PHYADDR sCpuPAddr;

				
				sCpuPAddr = OSMapLinToCPUPhys(ppsPTInfoList[ui32PTIndex]->PTPageCpuVAddr);
				sSysPAddr = SysCpuPAddrToSysPAddr (sCpuPAddr);

				
				OSUnMapPhysToLin(ppsPTInfoList[ui32PTIndex]->PTPageCpuVAddr,
                                 SGX_MMU_PAGE_SIZE,
                                 PVRSRV_HAP_WRITECOMBINE|PVRSRV_HAP_KERNEL_ONLY,
                                 ppsPTInfoList[ui32PTIndex]->hPTPageOSMemHandle);

				


				RA_Free (pMMUHeap->psDevArena->psDeviceMemoryHeapInfo->psLocalDevMemArena, sSysPAddr.uiAddr, IMG_FALSE);
			}

			


			pMMUHeap->ui32PTEntryCount -= i;
		}
		else
		{
			
			pMMUHeap->ui32PTEntryCount -= 1024;
		}

		
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
					sizeof(MMU_PT_INFO),
					ppsPTInfoList[ui32PTIndex],
					IMG_NULL);
		ppsPTInfoList[ui32PTIndex] = IMG_NULL;
	}
	else
	{
		
		pMMUHeap->ui32PTEntryCount -= 1024;
	}

	PDUMPCOMMENT("Finished free page table (page count == %08X)", pMMUHeap->ui32PTPageCount);
}

static IMG_VOID
_DeferredFreePageTables (MMU_HEAP *pMMUHeap)
{
	IMG_UINT32 i;

	for(i=0; i<pMMUHeap->ui32PTPageCount; i++)
	{
		_DeferredFreePageTable(pMMUHeap, i);
	}
	MMU_InvalidateDirectoryCache(pMMUHeap->psMMUContext->psDevInfo);
}


static IMG_BOOL
_DeferredAllocPagetables(MMU_HEAP *pMMUHeap, IMG_DEV_VIRTADDR DevVAddr, IMG_UINT32 ui32Size)
{
	IMG_UINT32 ui32PTPageCount;
	IMG_UINT32 ui32PDIndex;
	IMG_UINT32 i;
	IMG_UINT32 *pui32PDEntry;
	MMU_PT_INFO **ppsPTInfoList;
	SYS_DATA *psSysData;

	
#if SGX_FEATURE_ADDRESS_SPACE_SIZE < 32
	PVR_ASSERT(DevVAddr.uiAddr < (1<<SGX_FEATURE_ADDRESS_SPACE_SIZE));
#endif

	
	if (SysAcquireData(&psSysData) != PVRSRV_OK)
	{
		return IMG_FALSE;
	}

	
	ui32PDIndex = DevVAddr.uiAddr >> (SGX_MMU_PAGE_SHIFT + SGX_MMU_PT_SHIFT);

	
	ui32PTPageCount = (DevVAddr.uiAddr + ui32Size + (1<<(SGX_MMU_PAGE_SHIFT+SGX_MMU_PT_SHIFT)) - 1)
						>> (SGX_MMU_PAGE_SHIFT + SGX_MMU_PT_SHIFT);
	ui32PTPageCount -= ui32PDIndex;

	
	pui32PDEntry = (IMG_UINT32*)pMMUHeap->psMMUContext->pvPDCpuVAddr;
	pui32PDEntry += ui32PDIndex;

	
	ppsPTInfoList = &pMMUHeap->psMMUContext->apsPTInfoList[ui32PDIndex];

	PDUMPCOMMENT("Alloc page table (page count == %08X)", ui32PTPageCount);
	PDUMPCOMMENT("Page directory mods (page count == %08X)", ui32PTPageCount);

	
	for(i=0; i<ui32PTPageCount; i++)
	{
		if(ppsPTInfoList[i] == IMG_NULL)
		{
			OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
						 sizeof (MMU_PT_INFO),
						 (IMG_VOID **)&ppsPTInfoList[i], IMG_NULL);
			if (ppsPTInfoList[i] == IMG_NULL)
			{
				PVR_DPF((PVR_DBG_ERROR, "_DeferredAllocPagetables: ERROR call to OSAllocMem failed"));
				return IMG_FALSE;
			}
			OSMemSet (ppsPTInfoList[i], 0, sizeof(MMU_PT_INFO));
		}

		if(ppsPTInfoList[i]->hPTPageOSMemHandle == IMG_NULL
		&& ppsPTInfoList[i]->PTPageCpuVAddr == IMG_NULL)
		{
			IMG_CPU_PHYADDR	sCpuPAddr;
			IMG_DEV_PHYADDR	sDevPAddr;
#if defined(SUPPORT_SGX_MMU_DUMMY_PAGE)
			IMG_UINT32 *pui32Tmp;
			IMG_UINT32 j;
#else
			
			PVR_ASSERT(pui32PDEntry[i] == 0);
#endif
		
			


			if(pMMUHeap->psDevArena->psDeviceMemoryHeapInfo->psLocalDevMemArena == IMG_NULL)
			{
				if (OSAllocPages(PVRSRV_HAP_WRITECOMBINE | PVRSRV_HAP_KERNEL_ONLY,
								   SGX_MMU_PAGE_SIZE,
								   (IMG_VOID **)&ppsPTInfoList[i]->PTPageCpuVAddr,
								   &ppsPTInfoList[i]->hPTPageOSMemHandle) != PVRSRV_OK)
				{
					PVR_DPF((PVR_DBG_ERROR, "_DeferredAllocPagetables: ERROR call to OSAllocPages failed"));	
					return IMG_FALSE;
				}

				
				if(ppsPTInfoList[i]->PTPageCpuVAddr)
				{
					sCpuPAddr = OSMapLinToCPUPhys(ppsPTInfoList[i]->PTPageCpuVAddr);
				}
				else
				{
					
					sCpuPAddr = OSMemHandleToCpuPAddr(ppsPTInfoList[i]->hPTPageOSMemHandle, 0);
				}
				sDevPAddr = SysCpuPAddrToDevPAddr (PVRSRV_DEVICE_TYPE_SGX, sCpuPAddr);
			}
			else
			{
				IMG_SYS_PHYADDR sSysPAddr;

				


				if(RA_Alloc(pMMUHeap->psDevArena->psDeviceMemoryHeapInfo->psLocalDevMemArena,
							SGX_MMU_PAGE_SIZE,
							IMG_NULL,
							IMG_NULL,
							0,
							SGX_MMU_PAGE_SIZE, 
							0, 
							&(sSysPAddr.uiAddr))!= IMG_TRUE)
				{
					PVR_DPF((PVR_DBG_ERROR, "_DeferredAllocPagetables: ERROR call to RA_Alloc failed"));
					return IMG_FALSE;
				}

				
				sCpuPAddr = SysSysPAddrToCpuPAddr(sSysPAddr);
				ppsPTInfoList[i]->PTPageCpuVAddr = OSMapPhysToLin(sCpuPAddr,
															SGX_MMU_PAGE_SIZE,
															PVRSRV_HAP_WRITECOMBINE|PVRSRV_HAP_KERNEL_ONLY,
															&ppsPTInfoList[i]->hPTPageOSMemHandle);
				if(!ppsPTInfoList[i]->PTPageCpuVAddr)
				{
					PVR_DPF((PVR_DBG_ERROR, "_DeferredAllocPagetables: ERROR failed to map page tables"));
					return IMG_FALSE;
				}

				
				sDevPAddr = SysCpuPAddrToDevPAddr (PVRSRV_DEVICE_TYPE_SGX, sCpuPAddr);

				#if PAGE_TEST
				PageTest(ppsPTInfoList[i]->PTPageCpuVAddr, sDevPAddr);
				#endif
			}

#if defined(SUPPORT_SGX_MMU_DUMMY_PAGE)
			pui32Tmp = (IMG_UINT32*)ppsPTInfoList[i]->PTPageCpuVAddr;
			
			for(j=0; j<SGX_MMU_PT_SIZE; j++)
			{
				pui32Tmp[j] = pMMUHeap->psMMUContext->psDevInfo->sDummyDataDevPAddr.uiAddr | SGX_MMU_PTE_VALID;
			}
#else
			
			OSMemSet(ppsPTInfoList[i]->PTPageCpuVAddr, 0, SGX_MMU_PAGE_SIZE);
#endif
			
			PDUMPMALLOCPAGETABLE(PVRSRV_DEVICE_TYPE_SGX, ppsPTInfoList[i]->PTPageCpuVAddr, SGX_MMU_PAGE_SIZE, PDUMP_PT_UNIQUETAG);
			
			PDUMPMEM2(PVRSRV_DEVICE_TYPE_SGX, ppsPTInfoList[i]->PTPageCpuVAddr, SGX_MMU_PAGE_SIZE, 0, IMG_TRUE, PDUMP_PT_UNIQUETAG, PDUMP_PT_UNIQUETAG);

			switch(pMMUHeap->psDevArena->DevMemHeapType)
			{
				case DEVICE_MEMORY_HEAP_SHARED :
				case DEVICE_MEMORY_HEAP_SHARED_EXPORTED :
				{
					
					MMU_CONTEXT *psMMUContext = (MMU_CONTEXT*)pMMUHeap->psMMUContext->psDevInfo->pvMMUContextList;

					while(psMMUContext)
					{
						
						pui32PDEntry = (IMG_UINT32*)psMMUContext->pvPDCpuVAddr;
						pui32PDEntry += ui32PDIndex;

						
						pui32PDEntry[i] = sDevPAddr.uiAddr | SGX_MMU_PDE_VALID;

						
						PDUMPMEM2(PVRSRV_DEVICE_TYPE_SGX, (IMG_VOID*)&pui32PDEntry[i], sizeof(IMG_UINT32), 0, IMG_FALSE, PDUMP_PD_UNIQUETAG, PDUMP_PT_UNIQUETAG);

						
						psMMUContext = psMMUContext->psNext;
					}
					break;
				}
				case DEVICE_MEMORY_HEAP_PERCONTEXT :
				case DEVICE_MEMORY_HEAP_KERNEL :
				{
					
					pui32PDEntry[i] = sDevPAddr.uiAddr | SGX_MMU_PDE_VALID;

					
					PDUMPMEM2(PVRSRV_DEVICE_TYPE_SGX, (IMG_VOID*)&pui32PDEntry[i], sizeof(IMG_UINT32), 0, IMG_FALSE, PDUMP_PD_UNIQUETAG, PDUMP_PT_UNIQUETAG);

					break;
				}
				default:
				{
					PVR_DPF((PVR_DBG_ERROR, "_DeferredAllocPagetables: ERROR invalid heap type"));
					return IMG_FALSE;
				}
			}

#if !defined(SGX_FEATURE_MULTIPLE_MEM_CONTEXTS)
			



			MMU_InvalidateDirectoryCache(pMMUHeap->psMMUContext->psDevInfo);
#endif
		}
		else
		{
			
			PVR_ASSERT(pui32PDEntry[i] != 0);
		}
	}

	return IMG_TRUE;
}


PVRSRV_ERROR
MMU_Initialise (PVRSRV_DEVICE_NODE *psDeviceNode, MMU_CONTEXT **ppsMMUContext, IMG_DEV_PHYADDR *psPDDevPAddr)
{
	IMG_UINT32 *pui32Tmp;
	IMG_UINT32 i;
	IMG_CPU_VIRTADDR pvPDCpuVAddr;
	IMG_DEV_PHYADDR sPDDevPAddr;
	IMG_CPU_PHYADDR sCpuPAddr;
	MMU_CONTEXT *psMMUContext;
	IMG_HANDLE hPDOSMemHandle;
	SYS_DATA *psSysData;
	PVRSRV_SGXDEV_INFO *psDevInfo;

	PVR_DPF ((PVR_DBG_MESSAGE, "MMU_Initialise"));

	

	if (SysAcquireData(&psSysData) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "MMU_Initialise: ERROR call to SysAcquireData failed"));
		return PVRSRV_ERROR_GENERIC;
	}

	OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
				 sizeof (MMU_CONTEXT),
				 (IMG_VOID **)&psMMUContext, IMG_NULL);
	if (psMMUContext == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "MMU_Initialise: ERROR call to OSAllocMem failed"));
		return PVRSRV_ERROR_GENERIC;
	}
	OSMemSet (psMMUContext, 0, sizeof(MMU_CONTEXT));

	
	psDevInfo = (PVRSRV_SGXDEV_INFO*)psDeviceNode->pvDevice;
	psMMUContext->psDevInfo = psDevInfo;

	
	psMMUContext->psDeviceNode = psDeviceNode;

	
	if(psDeviceNode->psLocalDevMemArena == IMG_NULL)
	{
		if (OSAllocPages(PVRSRV_HAP_WRITECOMBINE | PVRSRV_HAP_KERNEL_ONLY,
							SGX_MMU_PAGE_SIZE,
							&pvPDCpuVAddr,
							&hPDOSMemHandle) != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "MMU_Initialise: ERROR call to OSAllocPages failed"));
			return PVRSRV_ERROR_GENERIC;
		}

		if(pvPDCpuVAddr)
		{
			sCpuPAddr = OSMapLinToCPUPhys(pvPDCpuVAddr);
		}
		else
		{
			
			sCpuPAddr = OSMemHandleToCpuPAddr(hPDOSMemHandle, 0);
		}
		sPDDevPAddr = SysCpuPAddrToDevPAddr (PVRSRV_DEVICE_TYPE_SGX, sCpuPAddr);

		#if PAGE_TEST
		PageTest(pvPDCpuVAddr, sPDDevPAddr);
		#endif

#if defined(SUPPORT_SGX_MMU_DUMMY_PAGE)
		
		if(!psDevInfo->pvMMUContextList)
		{
			
			if (OSAllocPages(PVRSRV_HAP_WRITECOMBINE | PVRSRV_HAP_KERNEL_ONLY,
								SGX_MMU_PAGE_SIZE, 
								&psDevInfo->pvDummyPTPageCpuVAddr, 
								&psDevInfo->hDummyPTPageOSMemHandle) != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "MMU_Initialise: ERROR call to OSAllocPages failed"));
				return PVRSRV_ERROR_GENERIC;
			}

			if(psDevInfo->pvDummyPTPageCpuVAddr)
			{
				sCpuPAddr = OSMapLinToCPUPhys(psDevInfo->pvDummyPTPageCpuVAddr);
			}
			else
			{
				
				sCpuPAddr = OSMemHandleToCpuPAddr(psDevInfo->hDummyPTPageOSMemHandle, 0);
			}
			psDevInfo->sDummyPTDevPAddr = SysCpuPAddrToDevPAddr (PVRSRV_DEVICE_TYPE_SGX, sCpuPAddr);

			
			if (OSAllocPages(PVRSRV_HAP_WRITECOMBINE | PVRSRV_HAP_KERNEL_ONLY, 
								SGX_MMU_PAGE_SIZE, 
								&psDevInfo->pvDummyDataPageCpuVAddr, 
								&psDevInfo->hDummyDataPageOSMemHandle) != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "MMU_Initialise: ERROR call to OSAllocPages failed"));
				return PVRSRV_ERROR_GENERIC;
			}

			if(psDevInfo->pvDummyDataPageCpuVAddr)
			{
				sCpuPAddr = OSMapLinToCPUPhys(psDevInfo->pvDummyDataPageCpuVAddr);
			}
			else
			{
				sCpuPAddr = OSMemHandleToCpuPAddr(psDevInfo->hDummyDataPageOSMemHandle, 0);
			}
			psDevInfo->sDummyDataDevPAddr = SysCpuPAddrToDevPAddr (PVRSRV_DEVICE_TYPE_SGX, sCpuPAddr);
		}
#endif 
	}
	else
	{
		IMG_SYS_PHYADDR sSysPAddr;

		
		if(RA_Alloc(psDeviceNode->psLocalDevMemArena,
					SGX_MMU_PAGE_SIZE,
					IMG_NULL,
					IMG_NULL,
					0,
					SGX_MMU_PAGE_SIZE,
					0,
					&(sSysPAddr.uiAddr))!= IMG_TRUE)
		{
			PVR_DPF((PVR_DBG_ERROR, "MMU_Initialise: ERROR call to RA_Alloc failed"));
			return PVRSRV_ERROR_GENERIC;
		}

		
		sCpuPAddr = SysSysPAddrToCpuPAddr(sSysPAddr);
		sPDDevPAddr = SysSysPAddrToDevPAddr(PVRSRV_DEVICE_TYPE_SGX, sSysPAddr);
		pvPDCpuVAddr = OSMapPhysToLin(sCpuPAddr, 
										SGX_MMU_PAGE_SIZE, 
										PVRSRV_HAP_WRITECOMBINE|PVRSRV_HAP_KERNEL_ONLY,
										&hPDOSMemHandle);
		if(!pvPDCpuVAddr)
		{
			PVR_DPF((PVR_DBG_ERROR, "MMU_Initialise: ERROR failed to map page tables"));
			return PVRSRV_ERROR_GENERIC;
		}

		#if PAGE_TEST
		PageTest(pvPDCpuVAddr, sPDDevPAddr);
		#endif

#if defined(SUPPORT_SGX_MMU_DUMMY_PAGE)
		
		if(!psDevInfo->pvMMUContextList)
		{
			
			if(RA_Alloc(psDeviceNode->psLocalDevMemArena,
						SGX_MMU_PAGE_SIZE,
						IMG_NULL,
						IMG_NULL,
						0,
						SGX_MMU_PAGE_SIZE,
						0,
						&(sSysPAddr.uiAddr))!= IMG_TRUE)
			{
				PVR_DPF((PVR_DBG_ERROR, "MMU_Initialise: ERROR call to RA_Alloc failed"));
				return PVRSRV_ERROR_GENERIC;
			}

			
			sCpuPAddr = SysSysPAddrToCpuPAddr(sSysPAddr);
			psDevInfo->sDummyPTDevPAddr = SysSysPAddrToDevPAddr(PVRSRV_DEVICE_TYPE_SGX, sSysPAddr);
			psDevInfo->pvDummyPTPageCpuVAddr = OSMapPhysToLin(sCpuPAddr,
																SGX_MMU_PAGE_SIZE,
																PVRSRV_HAP_WRITECOMBINE|PVRSRV_HAP_KERNEL_ONLY,
																&psDevInfo->hDummyPTPageOSMemHandle);
			if(!psDevInfo->pvDummyPTPageCpuVAddr)
			{
				PVR_DPF((PVR_DBG_ERROR, "MMU_Initialise: ERROR failed to map page tables"));
				return PVRSRV_ERROR_GENERIC;
			}

			
			if(RA_Alloc(psDeviceNode->psLocalDevMemArena,
						SGX_MMU_PAGE_SIZE,
						IMG_NULL,
						IMG_NULL,
						0,
						SGX_MMU_PAGE_SIZE,
						0,
						&(sSysPAddr.uiAddr))!= IMG_TRUE)
			{
				PVR_DPF((PVR_DBG_ERROR, "MMU_Initialise: ERROR call to RA_Alloc failed"));
				return PVRSRV_ERROR_GENERIC;
			}

			
			sCpuPAddr = SysSysPAddrToCpuPAddr(sSysPAddr);
			psDevInfo->sDummyDataDevPAddr = SysSysPAddrToDevPAddr(PVRSRV_DEVICE_TYPE_SGX, sSysPAddr);
			psDevInfo->pvDummyDataPageCpuVAddr = OSMapPhysToLin(sCpuPAddr, 
																SGX_MMU_PAGE_SIZE, 
																PVRSRV_HAP_WRITECOMBINE|PVRSRV_HAP_KERNEL_ONLY,
																&psDevInfo->hDummyDataPageOSMemHandle);
			if(!psDevInfo->pvDummyDataPageCpuVAddr)
			{
				PVR_DPF((PVR_DBG_ERROR, "MMU_Initialise: ERROR failed to map page tables"));
				return PVRSRV_ERROR_GENERIC;
			}
		}
#endif 
	}

	
	PDUMPCOMMENT("Alloc page directory");
#ifdef SUPPORT_SGX_MMU_BYPASS
	EnableHostAccess(psMMUContext);
#endif

	PDUMPMALLOCPAGETABLE(PVRSRV_DEVICE_TYPE_SGX, pvPDCpuVAddr, SGX_MMU_PAGE_SIZE, PDUMP_PD_UNIQUETAG);

	if (pvPDCpuVAddr)
	{
		pui32Tmp = (IMG_UINT32 *)pvPDCpuVAddr;
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR, "MMU_Initialise: pvPDCpuVAddr invalid"));
		return PVRSRV_ERROR_GENERIC;
	}

#if defined(SUPPORT_SGX_MMU_DUMMY_PAGE)
	
	for(i=0; i<SGX_MMU_PD_SIZE; i++)
	{
		pui32Tmp[i] = psDevInfo->sDummyPTDevPAddr.uiAddr | SGX_MMU_PDE_VALID;
	}

	if(!psDevInfo->pvMMUContextList)
	{
		


		pui32Tmp = (IMG_UINT32 *)psDevInfo->pvDummyPTPageCpuVAddr;
		for(i=0; i<SGX_MMU_PT_SIZE; i++)
		{
			pui32Tmp[i] = psDevInfo->sDummyDataDevPAddr.uiAddr | SGX_MMU_PTE_VALID;
		}
		
		PDUMPCOMMENT("Dummy Page table contents");
		PDUMPMEM2(PVRSRV_DEVICE_TYPE_SGX, psDevInfo->pvDummyPTPageCpuVAddr, SGX_MMU_PAGE_SIZE, 0, IMG_TRUE, PDUMP_PD_UNIQUETAG, PDUMP_PT_UNIQUETAG);

		

		pui32Tmp = (IMG_UINT32 *)psDevInfo->pvDummyDataPageCpuVAddr;
		for(i=0; i<(SGX_MMU_PAGE_SIZE/4); i++)
		{
			pui32Tmp[i] = DUMMY_DATA_PAGE_SIGNATURE;
		}
		
		PDUMPCOMMENT("Dummy Data Page contents");
		PDUMPMEM2(PVRSRV_DEVICE_TYPE_SGX, psDevInfo->pvDummyDataPageCpuVAddr, SGX_MMU_PAGE_SIZE, 0, IMG_TRUE, PDUMP_PD_UNIQUETAG, PDUMP_PT_UNIQUETAG);
	}
#else 
	
	for(i=0; i<SGX_MMU_PD_SIZE; i++)
	{
		
		pui32Tmp[i] = 0;
	}
#endif 

	
	PDUMPCOMMENT("Page directory contents");
	PDUMPMEM2(PVRSRV_DEVICE_TYPE_SGX, pvPDCpuVAddr, SGX_MMU_PAGE_SIZE, 0, IMG_TRUE, PDUMP_PD_UNIQUETAG, PDUMP_PT_UNIQUETAG);

	
	psMMUContext->pvPDCpuVAddr = pvPDCpuVAddr;
	psMMUContext->sPDDevPAddr = sPDDevPAddr;
	psMMUContext->hPDOSMemHandle = hPDOSMemHandle;

	
	*ppsMMUContext = psMMUContext;

	
	*psPDDevPAddr = sPDDevPAddr;

	
	psMMUContext->psNext = (MMU_CONTEXT*)psDevInfo->pvMMUContextList;
	psDevInfo->pvMMUContextList = (IMG_VOID*)psMMUContext;

#ifdef SUPPORT_SGX_MMU_BYPASS
	DisableHostAccess(psMMUContext);
#endif

	return PVRSRV_OK;
}

IMG_VOID
MMU_Finalise (MMU_CONTEXT *psMMUContext)
{
	IMG_UINT32 *pui32Tmp, i;
	SYS_DATA *psSysData;
	MMU_CONTEXT **ppsMMUContext;
#if defined(SUPPORT_SGX_MMU_DUMMY_PAGE)
	PVRSRV_SGXDEV_INFO *psDevInfo = (PVRSRV_SGXDEV_INFO*)psMMUContext->psDevInfo;
	MMU_CONTEXT *psMMUContextList = (MMU_CONTEXT*)psDevInfo->pvMMUContextList;
#endif
	
	if (SysAcquireData(&psSysData) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "MMU_Finalise: ERROR call to SysAcquireData failed"));
		return;
	}

	
	PDUMPCOMMENT("Free page directory");
	PDUMPFREEPAGETABLE(PVRSRV_DEVICE_TYPE_SGX, psMMUContext->pvPDCpuVAddr, SGX_MMU_PAGE_SIZE, PDUMP_PT_UNIQUETAG);
#if defined(SUPPORT_SGX_MMU_DUMMY_PAGE)
	PDUMPFREEPAGETABLE(PVRSRV_DEVICE_TYPE_SGX, psDevInfo->pvDummyPTPageCpuVAddr, SGX_MMU_PAGE_SIZE, PDUMP_PT_UNIQUETAG);
	PDUMPFREEPAGETABLE(PVRSRV_DEVICE_TYPE_SGX, psDevInfo->pvDummyDataPageCpuVAddr, SGX_MMU_PAGE_SIZE, PDUMP_PT_UNIQUETAG);
#endif

	pui32Tmp = (IMG_UINT32 *)psMMUContext->pvPDCpuVAddr;

	


	for(i=0; i<SGX_MMU_PD_SIZE; i++)
	{
		
		pui32Tmp[i] = 0;
	}

	



	if(psMMUContext->psDeviceNode->psLocalDevMemArena == IMG_NULL)
	{
		OSFreePages(PVRSRV_HAP_WRITECOMBINE | PVRSRV_HAP_KERNEL_ONLY,
						SGX_MMU_PAGE_SIZE,
						psMMUContext->pvPDCpuVAddr,
						psMMUContext->hPDOSMemHandle);

#if defined(SUPPORT_SGX_MMU_DUMMY_PAGE)
		
		if(!psMMUContextList->psNext)
		{
			OSFreePages(PVRSRV_HAP_WRITECOMBINE | PVRSRV_HAP_KERNEL_ONLY,
							SGX_MMU_PAGE_SIZE,
							psDevInfo->pvDummyPTPageCpuVAddr, 
							psDevInfo->hDummyPTPageOSMemHandle);
			OSFreePages(PVRSRV_HAP_WRITECOMBINE | PVRSRV_HAP_KERNEL_ONLY,
							SGX_MMU_PAGE_SIZE,
							psDevInfo->pvDummyDataPageCpuVAddr,
							psDevInfo->hDummyDataPageOSMemHandle);
		}
#endif
	}
	else
	{
		IMG_SYS_PHYADDR sSysPAddr;
		IMG_CPU_PHYADDR sCpuPAddr;

		
		sCpuPAddr = OSMapLinToCPUPhys(psMMUContext->pvPDCpuVAddr);
		sSysPAddr = SysCpuPAddrToSysPAddr(sCpuPAddr);

		
		OSUnMapPhysToLin(psMMUContext->pvPDCpuVAddr, 
							SGX_MMU_PAGE_SIZE,
                            PVRSRV_HAP_WRITECOMBINE|PVRSRV_HAP_KERNEL_ONLY,
							psMMUContext->hPDOSMemHandle);
		
		RA_Free (psMMUContext->psDeviceNode->psLocalDevMemArena, sSysPAddr.uiAddr, IMG_FALSE);

#if defined(SUPPORT_SGX_MMU_DUMMY_PAGE)
		
		if(!psMMUContextList->psNext)
		{
			
			sCpuPAddr = OSMapLinToCPUPhys(psDevInfo->pvDummyPTPageCpuVAddr);
			sSysPAddr = SysCpuPAddrToSysPAddr(sCpuPAddr);
	
			
			OSUnMapPhysToLin(psDevInfo->pvDummyPTPageCpuVAddr, 
								SGX_MMU_PAGE_SIZE,
                                PVRSRV_HAP_WRITECOMBINE|PVRSRV_HAP_KERNEL_ONLY,
								psDevInfo->hDummyPTPageOSMemHandle);
			
			RA_Free (psMMUContext->psDeviceNode->psLocalDevMemArena, sSysPAddr.uiAddr, IMG_FALSE);

			
			sCpuPAddr = OSMapLinToCPUPhys(psDevInfo->pvDummyDataPageCpuVAddr);
			sSysPAddr = SysCpuPAddrToSysPAddr(sCpuPAddr);
	
			
			OSUnMapPhysToLin(psDevInfo->pvDummyDataPageCpuVAddr, 
								SGX_MMU_PAGE_SIZE,
                                PVRSRV_HAP_WRITECOMBINE|PVRSRV_HAP_KERNEL_ONLY,
								psDevInfo->hDummyDataPageOSMemHandle);
			
			RA_Free (psMMUContext->psDeviceNode->psLocalDevMemArena, sSysPAddr.uiAddr, IMG_FALSE);			
		}
#endif
	}
	
	PVR_DPF ((PVR_DBG_MESSAGE, "MMU_Finalise"));

	
	ppsMMUContext = (MMU_CONTEXT**)&psMMUContext->psDevInfo->pvMMUContextList;
	while(*ppsMMUContext)
	{
		if(*ppsMMUContext == psMMUContext)
		{
			
			*ppsMMUContext = psMMUContext->psNext;
			break;
		}
		
		
		ppsMMUContext = &((*ppsMMUContext)->psNext);
	}

	
	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(MMU_CONTEXT), psMMUContext, IMG_NULL);
}


IMG_VOID
MMU_InsertHeap(MMU_CONTEXT *psMMUContext, MMU_HEAP *psMMUHeap)
{
	IMG_UINT32 *pui32PDCpuVAddr = (IMG_UINT32 *) psMMUContext->pvPDCpuVAddr;
	IMG_UINT32 *pui32KernelPDCpuVAddr = (IMG_UINT32 *) psMMUHeap->psMMUContext->pvPDCpuVAddr;
	IMG_UINT32 ui32PDEntry;
#if !defined(SGX_FEATURE_MULTIPLE_MEM_CONTEXTS)
	IMG_BOOL bInvalidateDirectoryCache = IMG_FALSE;
#endif

	
	pui32PDCpuVAddr += psMMUHeap->psDevArena->BaseDevVAddr.uiAddr >> (SGX_MMU_PAGE_SHIFT + SGX_MMU_PT_SHIFT);
	pui32KernelPDCpuVAddr += psMMUHeap->psDevArena->BaseDevVAddr.uiAddr >> (SGX_MMU_PAGE_SHIFT + SGX_MMU_PT_SHIFT);

	


	PDUMPCOMMENT("Page directory shared heap range copy");
#ifdef SUPPORT_SGX_MMU_BYPASS
	EnableHostAccess(psMMUContext);
#endif

	for (ui32PDEntry = 0; ui32PDEntry < psMMUHeap->ui32PTPageCount; ui32PDEntry++)
	{
#if !defined(SUPPORT_SGX_MMU_DUMMY_PAGE)
		
		PVR_ASSERT(pui32PDCpuVAddr[ui32PDEntry] == 0);
#endif

		
		pui32PDCpuVAddr[ui32PDEntry] = pui32KernelPDCpuVAddr[ui32PDEntry];
		if (pui32PDCpuVAddr[ui32PDEntry])
		{
			PDUMPMEM2(PVRSRV_DEVICE_TYPE_SGX, (IMG_VOID *) &pui32PDCpuVAddr[ui32PDEntry], sizeof(IMG_UINT32), 0, IMG_FALSE, PDUMP_PD_UNIQUETAG, PDUMP_PT_UNIQUETAG);

#if !defined(SGX_FEATURE_MULTIPLE_MEM_CONTEXTS)
			bInvalidateDirectoryCache = IMG_TRUE;
#endif
		}
	}

#ifdef SUPPORT_SGX_MMU_BYPASS
	DisableHostAccess(psMMUContext);
#endif

#if !defined(SGX_FEATURE_MULTIPLE_MEM_CONTEXTS)
	if (bInvalidateDirectoryCache)
	{
		



		MMU_InvalidateDirectoryCache(psMMUContext->psDevInfo);
	}
#endif
}


static IMG_VOID
MMU_UnmapPagesAndFreePTs (MMU_HEAP *psMMUHeap,
						  IMG_DEV_VIRTADDR sDevVAddr,
						  IMG_UINT32 ui32PageCount,
						  IMG_HANDLE hUniqueTag)
{
	IMG_UINT32			uPageSize = HOST_PAGESIZE();
	IMG_DEV_VIRTADDR	sTmpDevVAddr;
	IMG_UINT32			i;
	IMG_UINT32			ui32PDIndex;
	IMG_UINT32			ui32PTIndex;
	IMG_UINT32			*pui32Tmp;
	IMG_BOOL			bInvalidateDirectoryCache = IMG_FALSE;

#if !defined (PDUMP)
	PVR_UNREFERENCED_PARAMETER(hUniqueTag);
#endif
	
	sTmpDevVAddr = sDevVAddr;

	for(i=0; i<ui32PageCount; i++)
	{
		MMU_PT_INFO **ppsPTInfoList;

		
		ui32PDIndex = sTmpDevVAddr.uiAddr >> (SGX_MMU_PAGE_SHIFT + SGX_MMU_PT_SHIFT);

		
		ppsPTInfoList = &psMMUHeap->psMMUContext->apsPTInfoList[ui32PDIndex];

		{
			
			ui32PTIndex = (sTmpDevVAddr.uiAddr & SGX_MMU_PT_MASK) >> SGX_MMU_PAGE_SHIFT;
	
			
			if (!ppsPTInfoList[0])
			{
				PVR_DPF((PVR_DBG_MESSAGE, "MMU_UnmapPagesAndFreePTs: Invalid PT for alloc at VAddr:0x%08lX (VaddrIni:0x%08lX AllocPage:%u) PDIdx:%u PTIdx:%u",sTmpDevVAddr.uiAddr, sDevVAddr.uiAddr,i, ui32PDIndex, ui32PTIndex ));
	
				
				sTmpDevVAddr.uiAddr += uPageSize;
	
				
				continue;
			}
	
			
			pui32Tmp = (IMG_UINT32*)ppsPTInfoList[0]->PTPageCpuVAddr;

			
			if (!pui32Tmp)
			{
				continue;
			}
	
			
			if (pui32Tmp[ui32PTIndex] & SGX_MMU_PTE_VALID)
			{
				ppsPTInfoList[0]->ui32ValidPTECount--;
			}
			else
			{
				PVR_DPF((PVR_DBG_MESSAGE, "MMU_UnmapPagesAndFreePTs: Page is already invalid for alloc at VAddr:0x%08lX (VAddrIni:0x%08lX AllocPage:%u) PDIdx:%u PTIdx:%u",sTmpDevVAddr.uiAddr, sDevVAddr.uiAddr,i, ui32PDIndex, ui32PTIndex ));
			}
	
			
			PVR_ASSERT((IMG_INT32)ppsPTInfoList[0]->ui32ValidPTECount >= 0);

#if defined(SUPPORT_SGX_MMU_DUMMY_PAGE)
			
			pui32Tmp[ui32PTIndex] = psMMUHeap->psMMUContext->psDevInfo->sDummyDataDevPAddr.uiAddr | SGX_MMU_PTE_VALID;
#else
			
			pui32Tmp[ui32PTIndex] = 0;
#endif
		}

		

		if (ppsPTInfoList[0] && ppsPTInfoList[0]->ui32ValidPTECount == 0)
		{
			_DeferredFreePageTable(psMMUHeap, ui32PDIndex - (psMMUHeap->ui32PTBaseIndex >> SGX_MMU_PT_SHIFT));
			bInvalidateDirectoryCache = IMG_TRUE;
		}

		
		sTmpDevVAddr.uiAddr += uPageSize;
	}

	if(bInvalidateDirectoryCache)
	{
		MMU_InvalidateDirectoryCache(psMMUHeap->psMMUContext->psDevInfo);
	}
	else
	{
		MMU_InvalidatePageTableCache(psMMUHeap->psMMUContext->psDevInfo);
	}

#if defined(PDUMP)
	MMU_PDumpPageTables (psMMUHeap, sDevVAddr, uPageSize*ui32PageCount, IMG_TRUE, hUniqueTag);
#endif 
}


IMG_VOID MMU_FreePageTables(IMG_PVOID pvMMUHeap,
                            IMG_UINT32 ui32Start,
                            IMG_UINT32 ui32End,
                            IMG_HANDLE hUniqueTag)
{
	MMU_HEAP *pMMUHeap = (MMU_HEAP*)pvMMUHeap;
	IMG_DEV_VIRTADDR Start;

	Start.uiAddr = ui32Start;

	MMU_UnmapPagesAndFreePTs(pMMUHeap, Start, (ui32End - ui32Start) / SGX_MMU_PAGE_SIZE, hUniqueTag);
}

MMU_HEAP *
MMU_Create (MMU_CONTEXT *psMMUContext,
			DEV_ARENA_DESCRIPTOR *psDevArena,
			RA_ARENA **ppsVMArena)
{
	MMU_HEAP *pMMUHeap;
	IMG_BOOL bRes;

	PVR_ASSERT (psDevArena != IMG_NULL);

	if (psDevArena == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "MMU_Create: invalid parameter"));
		return IMG_NULL;
	}

	OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
				 sizeof (MMU_HEAP),
				 (IMG_VOID **)&pMMUHeap, IMG_NULL);
	if (pMMUHeap == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "MMU_Create: ERROR call to OSAllocMem failed"));
		return IMG_NULL;
	}

	pMMUHeap->psMMUContext = psMMUContext;
	pMMUHeap->psDevArena = psDevArena;

	bRes = _AllocPageTables (pMMUHeap);
	if (!bRes)
	{
		PVR_DPF((PVR_DBG_ERROR, "MMU_Create: ERROR call to _AllocPageTables failed"));
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(MMU_HEAP),
				pMMUHeap, IMG_NULL);
		return IMG_NULL;
	}

	
	pMMUHeap->psVMArena = RA_Create(psDevArena->pszName,
									psDevArena->BaseDevVAddr.uiAddr,
									psDevArena->ui32Size,
									IMG_NULL,
									SGX_MMU_PAGE_SIZE,
									IMG_NULL,
									IMG_NULL,
									MMU_FreePageTables,
									pMMUHeap);

	if (pMMUHeap->psVMArena == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "MMU_Create: ERROR call to RA_Create failed"));
		_DeferredFreePageTables (pMMUHeap);
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(MMU_HEAP),
				pMMUHeap, IMG_NULL);
		return IMG_NULL;
	}

#if 0 
	
	if(psDevArena->ui32HeapID == SGX_TILED_HEAP_ID)
	{
		IMG_UINT32 ui32RegVal;
		IMG_UINT32 ui32XTileStride;

		




		ui32XTileStride	= 2;

		ui32RegVal = (EUR_CR_BIF_TILE0_MIN_ADDRESS_MASK
						& ((psDevArena->BaseDevVAddr.uiAddr>>20)
						<< EUR_CR_BIF_TILE0_MIN_ADDRESS_SHIFT))
					|(EUR_CR_BIF_TILE0_MAX_ADDRESS_MASK
						& (((psDevArena->BaseDevVAddr.uiAddr+psDevArena->ui32Size)>>20)
						<< EUR_CR_BIF_TILE0_MAX_ADDRESS_SHIFT))
					|(EUR_CR_BIF_TILE0_CFG_MASK
						& (((ui32XTileStride<<1)|8) << EUR_CR_BIF_TILE0_CFG_SHIFT));
		PDUMPREG(EUR_CR_BIF_TILE0, ui32RegVal);
	}
#endif

	

	*ppsVMArena = pMMUHeap->psVMArena;

	return pMMUHeap;
}

IMG_VOID
MMU_Delete (MMU_HEAP *pMMUHeap)
{
	if (pMMUHeap != IMG_NULL)
	{
		PVR_DPF ((PVR_DBG_MESSAGE, "MMU_Delete"));

		if(pMMUHeap->psVMArena)
		{
			RA_Delete (pMMUHeap->psVMArena);
		}

#ifdef SUPPORT_SGX_MMU_BYPASS
		EnableHostAccess(pMMUHeap->psMMUContext);
#endif
		_DeferredFreePageTables (pMMUHeap);
#ifdef SUPPORT_SGX_MMU_BYPASS
		DisableHostAccess(pMMUHeap->psMMUContext);
#endif

		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(MMU_HEAP),
				pMMUHeap, IMG_NULL);
	}
}

IMG_BOOL
MMU_Alloc (MMU_HEAP *pMMUHeap,
		   IMG_SIZE_T uSize,
		   IMG_SIZE_T *pActualSize,
		   IMG_UINT32 uFlags,
		   IMG_UINT32 uDevVAddrAlignment,
		   IMG_DEV_VIRTADDR *psDevVAddr)
{
	IMG_BOOL bStatus;

	PVR_DPF ((PVR_DBG_MESSAGE,
		"MMU_Alloc: uSize=0x%x, flags=0x%x, align=0x%x",
		uSize, uFlags, uDevVAddrAlignment));

	

	if((uFlags & PVRSRV_MEM_USER_SUPPLIED_DEVVADDR) == 0)
	{
		bStatus = RA_Alloc (pMMUHeap->psVMArena,
							uSize,
							pActualSize,
							IMG_NULL,
							0,
							uDevVAddrAlignment,
							0,
							&(psDevVAddr->uiAddr));
		if(!bStatus)
		{
			PVR_DPF((PVR_DBG_ERROR,"MMU_Alloc: RA_Alloc of VMArena failed"));
			return bStatus;
		}
	}

	#ifdef SUPPORT_SGX_MMU_BYPASS
	EnableHostAccess(pMMUHeap->psMMUContext);
	#endif

	
	bStatus = _DeferredAllocPagetables(pMMUHeap, *psDevVAddr, uSize);
	
	#ifdef SUPPORT_SGX_MMU_BYPASS
	DisableHostAccess(pMMUHeap->psMMUContext);
	#endif

	if (!bStatus)
	{
		PVR_DPF((PVR_DBG_ERROR,"MMU_Alloc: _DeferredAllocPagetables failed"));
		if((uFlags & PVRSRV_MEM_USER_SUPPLIED_DEVVADDR) == 0)
		{
			
			RA_Free (pMMUHeap->psVMArena, psDevVAddr->uiAddr, IMG_FALSE);
		}
	}

	return bStatus;
}

IMG_VOID
MMU_Free (MMU_HEAP *pMMUHeap, IMG_DEV_VIRTADDR DevVAddr, IMG_UINT32 ui32Size)
{
	PVR_ASSERT (pMMUHeap != IMG_NULL);

	if (pMMUHeap == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "MMU_Free: invalid parameter"));
		return;
	}

	PVR_DPF ((PVR_DBG_MESSAGE,
		"MMU_Free: mmu=%08X, dev_vaddr=%08X", pMMUHeap, DevVAddr.uiAddr));

	if((DevVAddr.uiAddr >= pMMUHeap->psDevArena->BaseDevVAddr.uiAddr) && 
		(DevVAddr.uiAddr + ui32Size <= pMMUHeap->psDevArena->BaseDevVAddr.uiAddr + pMMUHeap->psDevArena->ui32Size))
	{
		RA_Free (pMMUHeap->psVMArena, DevVAddr.uiAddr, IMG_TRUE);
		return;
	}

	PVR_DPF((PVR_DBG_ERROR,"MMU_Free: Couldn't find DevVAddr %08X in a DevArena",DevVAddr.uiAddr));
}

IMG_VOID
MMU_Enable (MMU_HEAP *pMMUHeap)
{
	PVR_UNREFERENCED_PARAMETER(pMMUHeap);
	
}

IMG_VOID
MMU_Disable (MMU_HEAP *pMMUHeap)
{
	PVR_UNREFERENCED_PARAMETER(pMMUHeap);
		
}

#if defined(PDUMP)
static IMG_VOID
MMU_PDumpPageTables	(MMU_HEAP *pMMUHeap,
					 IMG_DEV_VIRTADDR DevVAddr,
					 IMG_SIZE_T uSize,
					 IMG_BOOL bForUnmap,
					 IMG_HANDLE hUniqueTag)
{
	IMG_UINT32	ui32NumPTEntries;
	IMG_UINT32	ui32PTIndex;
	IMG_UINT32	*pui32PTEntry;

	MMU_PT_INFO **ppsPTInfoList;
	IMG_UINT32 ui32PDIndex;
	IMG_UINT32 ui32PTDumpCount;

	
	ui32NumPTEntries = (uSize + SGX_MMU_PAGE_SIZE - 1) >> SGX_MMU_PAGE_SHIFT;

	
	ui32PDIndex = DevVAddr.uiAddr >> (SGX_MMU_PAGE_SHIFT + SGX_MMU_PT_SHIFT);

	
	ppsPTInfoList = &pMMUHeap->psMMUContext->apsPTInfoList[ui32PDIndex];

	
	ui32PTIndex = (DevVAddr.uiAddr & SGX_MMU_PT_MASK) >> SGX_MMU_PAGE_SHIFT;

	
	PDUMPCOMMENT("Page table mods (num entries == %08X) %s", ui32NumPTEntries, bForUnmap ? "(for unmap)" : "");

	
	while(ui32NumPTEntries > 0)
	{
		MMU_PT_INFO* psPTInfo = *ppsPTInfoList++;

		if(ui32NumPTEntries <= 1024 - ui32PTIndex)
		{
			ui32PTDumpCount = ui32NumPTEntries;
		}
		else
		{
			ui32PTDumpCount = 1024 - ui32PTIndex;
		}

		if (psPTInfo)
		{
			pui32PTEntry = (IMG_UINT32*)psPTInfo->PTPageCpuVAddr; 
			PDUMPMEM2(PVRSRV_DEVICE_TYPE_SGX, (IMG_VOID *) &pui32PTEntry[ui32PTIndex], ui32PTDumpCount * sizeof(IMG_UINT32), 0, IMG_FALSE, PDUMP_PT_UNIQUETAG, hUniqueTag);
		}

		
		ui32NumPTEntries -= ui32PTDumpCount;

		
		ui32PTIndex = 0;
	}

	PDUMPCOMMENT("Finished page table mods %s", bForUnmap ? "(for unmap)" : "");
}
#endif 


static IMG_VOID
MMU_MapPage (MMU_HEAP *pMMUHeap,
			 IMG_DEV_VIRTADDR DevVAddr,
			 IMG_DEV_PHYADDR DevPAddr,
			 IMG_UINT32 ui32MemFlags)
{
	IMG_UINT32 ui32Index;
	IMG_UINT32 *pui32Tmp;
	IMG_UINT32 ui32MMUFlags = 0;
	MMU_PT_INFO **ppsPTInfoList;

	

	if(((PVRSRV_MEM_READ|PVRSRV_MEM_WRITE) & ui32MemFlags) == (PVRSRV_MEM_READ|PVRSRV_MEM_WRITE))
	{
		
		ui32MMUFlags = 0;
	}
	else if(PVRSRV_MEM_READ & ui32MemFlags)
	{
		
		ui32MMUFlags |= SGX_MMU_PTE_READONLY;
	}
	else if(PVRSRV_MEM_WRITE & ui32MemFlags)
	{
		
		ui32MMUFlags |= SGX_MMU_PTE_WRITEONLY;
	}
	
	
	if(PVRSRV_MEM_CACHE_CONSISTENT & ui32MemFlags)
	{
		ui32MMUFlags |= SGX_MMU_PTE_CACHECONSISTENT;
	}

#if !defined(FIX_HW_BRN_25503)
	
	if(PVRSRV_MEM_EDM_PROTECT & ui32MemFlags)
	{
		ui32MMUFlags |= SGX_MMU_PTE_EDMPROTECT;
	}
#endif
	
	


	
	ui32Index = DevVAddr.uiAddr >> (SGX_MMU_PAGE_SHIFT + SGX_MMU_PT_SHIFT);

	
	ppsPTInfoList = &pMMUHeap->psMMUContext->apsPTInfoList[ui32Index];

	
	ui32Index = (DevVAddr.uiAddr & SGX_MMU_PT_MASK) >> SGX_MMU_PAGE_SHIFT;

	
	pui32Tmp = (IMG_UINT32*)ppsPTInfoList[0]->PTPageCpuVAddr;

#if !defined(SUPPORT_SGX_MMU_DUMMY_PAGE)
	
	if (pui32Tmp[ui32Index] & SGX_MMU_PTE_VALID)
	{
		PVR_DPF((PVR_DBG_ERROR, "MMU_MapPage: Page is already valid for alloc at VAddr:0x%08lX PDIdx:%u PTIdx:%u",DevVAddr.uiAddr, DevVAddr.uiAddr >> (SGX_MMU_PAGE_SHIFT + SGX_MMU_PT_SHIFT), ui32Index ));
	}

	PVR_ASSERT((pui32Tmp[ui32Index] & SGX_MMU_PTE_VALID) == 0);
#endif

	
	ppsPTInfoList[0]->ui32ValidPTECount++;
	
	
	pui32Tmp[ui32Index] = (DevPAddr.uiAddr & SGX_MMU_PTE_ADDR_MASK)
						| SGX_MMU_PTE_VALID
						| ui32MMUFlags;
}


IMG_VOID
MMU_MapScatter (MMU_HEAP *pMMUHeap,
				IMG_DEV_VIRTADDR DevVAddr,
				IMG_SYS_PHYADDR *psSysAddr,
				IMG_SIZE_T uSize,
				IMG_UINT32 ui32MemFlags,
				IMG_HANDLE hUniqueTag)
{
#if defined(PDUMP)
	IMG_DEV_VIRTADDR MapBaseDevVAddr;
#endif 
	IMG_UINT32 uCount, i;
	IMG_DEV_PHYADDR DevPAddr;

	PVR_ASSERT (pMMUHeap != IMG_NULL);

#if defined(PDUMP)
	MapBaseDevVAddr = DevVAddr;
#else
	PVR_UNREFERENCED_PARAMETER(hUniqueTag);
#endif 

	for (i=0, uCount=0; uCount<uSize; i++, uCount+=SGX_MMU_PAGE_SIZE)
	{
		IMG_SYS_PHYADDR sSysAddr;

		sSysAddr = psSysAddr[i];


		DevPAddr = SysSysPAddrToDevPAddr(PVRSRV_DEVICE_TYPE_SGX, sSysAddr);

		MMU_MapPage (pMMUHeap, DevVAddr, DevPAddr, ui32MemFlags);
		DevVAddr.uiAddr += SGX_MMU_PAGE_SIZE;

		PVR_DPF ((PVR_DBG_MESSAGE, 
				 "MMU_MapScatter: devVAddr=%08X, SysAddr=%08X, size=0x%x/0x%x",
				  DevVAddr.uiAddr, sSysAddr.uiAddr, uCount, uSize));
	}

#if defined(PDUMP)
	MMU_PDumpPageTables (pMMUHeap, MapBaseDevVAddr, uSize, IMG_FALSE, hUniqueTag);
#endif 
}

IMG_VOID
MMU_MapPages (MMU_HEAP *pMMUHeap,
			  IMG_DEV_VIRTADDR DevVAddr,
			  IMG_SYS_PHYADDR SysPAddr,
			  IMG_SIZE_T uSize,
			  IMG_UINT32 ui32MemFlags,
			  IMG_HANDLE hUniqueTag)
{
	IMG_DEV_PHYADDR DevPAddr;
#if defined(PDUMP)
	IMG_DEV_VIRTADDR MapBaseDevVAddr;
#endif 
	IMG_UINT32 uCount;
	IMG_UINT32 ui32VAdvance = SGX_MMU_PAGE_SIZE;
	IMG_UINT32 ui32PAdvance = SGX_MMU_PAGE_SIZE;

	PVR_ASSERT (pMMUHeap != IMG_NULL);

	PVR_DPF ((PVR_DBG_MESSAGE,
		  "MMU_MapPages: mmu=%08X, devVAddr=%08X, SysPAddr=%08X, size=0x%x",
		  pMMUHeap, DevVAddr.uiAddr, SysPAddr.uiAddr, uSize));

#if defined(PDUMP)
	MapBaseDevVAddr = DevVAddr;
#else
	PVR_UNREFERENCED_PARAMETER(hUniqueTag);
#endif 

	DevPAddr = SysSysPAddrToDevPAddr(PVRSRV_DEVICE_TYPE_SGX, SysPAddr);

#if defined(FIX_HW_BRN_23281)
	if(ui32MemFlags & PVRSRV_MEM_INTERLEAVED)
	{
		ui32VAdvance *= 2;
	}
#endif

	


	if(ui32MemFlags & PVRSRV_MEM_DUMMY)
	{
		ui32PAdvance = 0;
	}

	for (uCount=0; uCount<uSize; uCount+=ui32VAdvance)
	{
		MMU_MapPage (pMMUHeap, DevVAddr, DevPAddr, ui32MemFlags);
		DevVAddr.uiAddr += ui32VAdvance;
		DevPAddr.uiAddr += ui32PAdvance;
	}

#if defined(PDUMP)
	MMU_PDumpPageTables (pMMUHeap, MapBaseDevVAddr, uSize, IMG_FALSE, hUniqueTag);
#endif 
}

IMG_VOID
MMU_MapShadow (MMU_HEAP          *pMMUHeap,
			   IMG_DEV_VIRTADDR   MapBaseDevVAddr,
			   IMG_SIZE_T         uByteSize,
			   IMG_CPU_VIRTADDR   CpuVAddr,
			   IMG_HANDLE         hOSMemHandle,
			   IMG_DEV_VIRTADDR  *pDevVAddr,
			   IMG_UINT32         ui32MemFlags,
			   IMG_HANDLE         hUniqueTag)
{
	IMG_UINT32			i;
	IMG_UINT32			uOffset = 0;
	IMG_DEV_VIRTADDR	MapDevVAddr;
	IMG_UINT32			ui32VAdvance = SGX_MMU_PAGE_SIZE;
	IMG_UINT32			ui32PAdvance = SGX_MMU_PAGE_SIZE;

#if !defined (PDUMP)
	PVR_UNREFERENCED_PARAMETER(hUniqueTag);
#endif

	PVR_DPF ((PVR_DBG_MESSAGE,
			"MMU_MapShadow: %08X, 0x%x, %08X",
			MapBaseDevVAddr.uiAddr,
			uByteSize,
			CpuVAddr));

	PVR_ASSERT(((IMG_UINT32)CpuVAddr & (SGX_MMU_PAGE_SIZE - 1)) == 0);
	PVR_ASSERT(((IMG_UINT32)uByteSize & (SGX_MMU_PAGE_SIZE - 1)) == 0);
	pDevVAddr->uiAddr = MapBaseDevVAddr.uiAddr;

#if defined(FIX_HW_BRN_23281)
	if(ui32MemFlags & PVRSRV_MEM_INTERLEAVED)
	{
		ui32VAdvance *= 2;
	}
#endif

	


	if(ui32MemFlags & PVRSRV_MEM_DUMMY)
	{
		ui32PAdvance = 0;
	}

	
	MapDevVAddr = MapBaseDevVAddr;
	for (i=0; i<uByteSize; i+=ui32VAdvance)
	{
		IMG_CPU_PHYADDR CpuPAddr;
		IMG_DEV_PHYADDR DevPAddr;

		if(CpuVAddr)
		{
			CpuPAddr = OSMapLinToCPUPhys ((IMG_VOID *)((IMG_UINT32)CpuVAddr + uOffset));
		}
		else
		{
			CpuPAddr = OSMemHandleToCpuPAddr(hOSMemHandle, uOffset);
		}
		DevPAddr = SysCpuPAddrToDevPAddr (PVRSRV_DEVICE_TYPE_SGX, CpuPAddr);

		PVR_DPF ((PVR_DBG_MESSAGE,
				"0x%x: CpuVAddr=%08X, CpuPAddr=%08X, DevVAddr=%08X, DevPAddr=%08X",
				uOffset, 
				(IMG_UINTPTR_T)CpuVAddr + uOffset, 
				CpuPAddr.uiAddr, 
				MapDevVAddr.uiAddr, 
				DevPAddr.uiAddr));

		MMU_MapPage (pMMUHeap, MapDevVAddr, DevPAddr, ui32MemFlags);

		
		MapDevVAddr.uiAddr += ui32VAdvance;
		uOffset += ui32PAdvance;
	}

#if defined(PDUMP)
	MMU_PDumpPageTables (pMMUHeap, MapBaseDevVAddr, uByteSize, IMG_FALSE, hUniqueTag);
#endif 
}


IMG_VOID
MMU_UnmapPages (MMU_HEAP *psMMUHeap,
				IMG_DEV_VIRTADDR sDevVAddr,
				IMG_UINT32 ui32PageCount,
				IMG_HANDLE hUniqueTag)
{
	IMG_UINT32			uPageSize = HOST_PAGESIZE();
	IMG_DEV_VIRTADDR	sTmpDevVAddr;
	IMG_UINT32			i;
	IMG_UINT32			ui32PDIndex;
	IMG_UINT32			ui32PTIndex;
	IMG_UINT32			*pui32Tmp;

#if !defined (PDUMP)
	PVR_UNREFERENCED_PARAMETER(hUniqueTag);
#endif

	
	sTmpDevVAddr = sDevVAddr;

	for(i=0; i<ui32PageCount; i++)
	{
		MMU_PT_INFO **ppsPTInfoList;

		
		ui32PDIndex = sTmpDevVAddr.uiAddr >> (SGX_MMU_PAGE_SHIFT + SGX_MMU_PT_SHIFT);

		
		ppsPTInfoList = &psMMUHeap->psMMUContext->apsPTInfoList[ui32PDIndex];

		
		ui32PTIndex = (sTmpDevVAddr.uiAddr & SGX_MMU_PT_MASK) >> SGX_MMU_PAGE_SHIFT;

		
		if (!ppsPTInfoList[0])
		{
			PVR_DPF((PVR_DBG_ERROR, "MMU_UnmapPages: ERROR Invalid PT for alloc at VAddr:0x%08lX (VaddrIni:0x%08lX AllocPage:%u) PDIdx:%u PTIdx:%u",sTmpDevVAddr.uiAddr, sDevVAddr.uiAddr,i, ui32PDIndex, ui32PTIndex ));

			
			sTmpDevVAddr.uiAddr += uPageSize;

			
			continue;
		}

		
		pui32Tmp = (IMG_UINT32*)ppsPTInfoList[0]->PTPageCpuVAddr;

		
		if (pui32Tmp[ui32PTIndex] & SGX_MMU_PTE_VALID)
		{
			ppsPTInfoList[0]->ui32ValidPTECount--;
		}
		else
		{
			PVR_DPF((PVR_DBG_ERROR, "MMU_UnmapPages: Page is already invalid for alloc at VAddr:0x%08lX (VAddrIni:0x%08lX AllocPage:%u) PDIdx:%u PTIdx:%u",sTmpDevVAddr.uiAddr, sDevVAddr.uiAddr,i, ui32PDIndex, ui32PTIndex ));
		}

		
		PVR_ASSERT((IMG_INT32)ppsPTInfoList[0]->ui32ValidPTECount >= 0);

#if defined(SUPPORT_SGX_MMU_DUMMY_PAGE)
		
		pui32Tmp[ui32PTIndex] = psMMUHeap->psMMUContext->psDevInfo->sDummyDataDevPAddr.uiAddr | SGX_MMU_PTE_VALID;
#else
		
		pui32Tmp[ui32PTIndex] = 0;
#endif

		
		sTmpDevVAddr.uiAddr += uPageSize;
	}

	MMU_InvalidatePageTableCache(psMMUHeap->psMMUContext->psDevInfo);

#if defined(PDUMP)
	MMU_PDumpPageTables (psMMUHeap, sDevVAddr, uPageSize*ui32PageCount, IMG_TRUE, hUniqueTag);
#endif 
}


IMG_DEV_PHYADDR
MMU_GetPhysPageAddr(MMU_HEAP *pMMUHeap, IMG_DEV_VIRTADDR sDevVPageAddr)
{
	IMG_UINT32 *pui32PageTable;
	IMG_UINT32 ui32Index;
	IMG_DEV_PHYADDR sDevPAddr;
	MMU_PT_INFO **ppsPTInfoList;

	
	ui32Index = sDevVPageAddr.uiAddr >> (SGX_MMU_PAGE_SHIFT + SGX_MMU_PT_SHIFT);

	
	ppsPTInfoList = &pMMUHeap->psMMUContext->apsPTInfoList[ui32Index];
	if (!ppsPTInfoList[0])
	{
		PVR_DPF((PVR_DBG_ERROR,"MMU_GetPhysPageAddr: Not mapped in at 0x%08x", sDevVPageAddr.uiAddr));
		sDevPAddr.uiAddr = 0;
		return sDevPAddr;
	}

	
	ui32Index = (sDevVPageAddr.uiAddr & SGX_MMU_PT_MASK) >> SGX_MMU_PAGE_SHIFT;

	
	pui32PageTable = (IMG_UINT32*)ppsPTInfoList[0]->PTPageCpuVAddr;

	
	sDevPAddr.uiAddr = pui32PageTable[ui32Index];

	
	sDevPAddr.uiAddr &= SGX_MMU_PTE_ADDR_MASK;

	return sDevPAddr;
}


IMG_DEV_PHYADDR MMU_GetPDDevPAddr(MMU_CONTEXT *pMMUContext)
{
	return (pMMUContext->sPDDevPAddr);
}


IMG_EXPORT
PVRSRV_ERROR SGXGetPhysPageAddrKM (IMG_HANDLE hDevMemHeap,
								   IMG_DEV_VIRTADDR sDevVAddr,
								   IMG_DEV_PHYADDR *pDevPAddr,
								   IMG_CPU_PHYADDR *pCpuPAddr)
{
	MMU_HEAP *pMMUHeap;
	IMG_DEV_PHYADDR DevPAddr;

	

	pMMUHeap = (MMU_HEAP*)BM_GetMMUHeap(hDevMemHeap);

	DevPAddr = MMU_GetPhysPageAddr(pMMUHeap, sDevVAddr);
	pCpuPAddr->uiAddr = DevPAddr.uiAddr; 
	pDevPAddr->uiAddr = DevPAddr.uiAddr;

	return (pDevPAddr->uiAddr != 0) ? PVRSRV_OK : PVRSRV_ERROR_INVALID_PARAMS;
}


PVRSRV_ERROR SGXGetMMUPDAddrKM(IMG_HANDLE		hDevCookie,
								IMG_HANDLE 		hDevMemContext,
								IMG_DEV_PHYADDR *psPDDevPAddr)
{
	if (!hDevCookie || !hDevMemContext || !psPDDevPAddr)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	PVR_UNREFERENCED_PARAMETER(hDevCookie);	

	
	*psPDDevPAddr = ((BM_CONTEXT*)hDevMemContext)->psMMUContext->sPDDevPAddr;
	
	return PVRSRV_OK;
}

PVRSRV_ERROR MMU_BIFResetPDAlloc(PVRSRV_SGXDEV_INFO *psDevInfo)
{
	PVRSRV_ERROR eError;
	SYS_DATA *psSysData;
	RA_ARENA *psLocalDevMemArena;
	IMG_HANDLE hOSMemHandle = IMG_NULL;
	IMG_BYTE *pui8MemBlock = IMG_NULL;
	IMG_SYS_PHYADDR sMemBlockSysPAddr;
	IMG_CPU_PHYADDR sMemBlockCpuPAddr;

	eError = SysAcquireData(&psSysData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "MMU_BIFResetPDAlloc: ERROR call to SysAcquireData failed"));
		return eError;
	}

	psLocalDevMemArena = psSysData->apsLocalDevMemArena[0];

	
	if(psLocalDevMemArena == IMG_NULL)
	{
		
		eError = OSAllocPages(PVRSRV_HAP_WRITECOMBINE | PVRSRV_HAP_KERNEL_ONLY,
						      3 * SGX_MMU_PAGE_SIZE,
						      (IMG_VOID **)&pui8MemBlock,
						      &hOSMemHandle);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "MMU_BIFResetPDAlloc: ERROR call to OSAllocPages failed"));	
			return eError;
		}

		
		if(pui8MemBlock)
		{
			sMemBlockCpuPAddr = OSMapLinToCPUPhys(pui8MemBlock);
		}
		else
		{
			
			sMemBlockCpuPAddr = OSMemHandleToCpuPAddr(hOSMemHandle, 0);
		}
	}
	else
	{
		

		if(RA_Alloc(psLocalDevMemArena,
					3 * SGX_MMU_PAGE_SIZE,
					IMG_NULL,
					IMG_NULL,
					0,
					SGX_MMU_PAGE_SIZE,
					0,
					&(sMemBlockSysPAddr.uiAddr)) != IMG_TRUE)
		{
			PVR_DPF((PVR_DBG_ERROR, "MMU_BIFResetPDAlloc: ERROR call to RA_Alloc failed"));
			return PVRSRV_ERROR_OUT_OF_MEMORY;
		}

		
		sMemBlockCpuPAddr = SysSysPAddrToCpuPAddr(sMemBlockSysPAddr);
		pui8MemBlock = OSMapPhysToLin(sMemBlockCpuPAddr,
									  SGX_MMU_PAGE_SIZE * 3,
									  PVRSRV_HAP_WRITECOMBINE|PVRSRV_HAP_KERNEL_ONLY,
									  &hOSMemHandle);
		if(!pui8MemBlock)
		{
			PVR_DPF((PVR_DBG_ERROR, "MMU_BIFResetPDAlloc: ERROR failed to map page tables"));
			return PVRSRV_ERROR_BAD_MAPPING;
		}
	}

	psDevInfo->hBIFResetPDOSMemHandle = hOSMemHandle;
	psDevInfo->sBIFResetPDDevPAddr = SysCpuPAddrToDevPAddr(PVRSRV_DEVICE_TYPE_SGX, sMemBlockCpuPAddr);
	psDevInfo->sBIFResetPTDevPAddr.uiAddr = psDevInfo->sBIFResetPDDevPAddr.uiAddr + SGX_MMU_PAGE_SIZE;
	psDevInfo->sBIFResetPageDevPAddr.uiAddr = psDevInfo->sBIFResetPTDevPAddr.uiAddr + SGX_MMU_PAGE_SIZE;
	psDevInfo->pui32BIFResetPD = (IMG_UINT32 *)pui8MemBlock;
	psDevInfo->pui32BIFResetPT = (IMG_UINT32 *)(pui8MemBlock + SGX_MMU_PAGE_SIZE);
	
	
	OSMemSet(psDevInfo->pui32BIFResetPD, 0, SGX_MMU_PAGE_SIZE);
	OSMemSet(psDevInfo->pui32BIFResetPT, 0, SGX_MMU_PAGE_SIZE);
	
	OSMemSet(pui8MemBlock + (2 * SGX_MMU_PAGE_SIZE), 0xDB, SGX_MMU_PAGE_SIZE);

	return PVRSRV_OK;
}

IMG_VOID MMU_BIFResetPDFree(PVRSRV_SGXDEV_INFO *psDevInfo)
{
	PVRSRV_ERROR eError;
	SYS_DATA *psSysData;
	RA_ARENA *psLocalDevMemArena;
	IMG_SYS_PHYADDR sPDSysPAddr;

	eError = SysAcquireData(&psSysData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "MMU_BIFResetPDFree: ERROR call to SysAcquireData failed"));
		return;
	}

	psLocalDevMemArena = psSysData->apsLocalDevMemArena[0];

	
	if(psLocalDevMemArena == IMG_NULL)
	{
		OSFreePages(PVRSRV_HAP_WRITECOMBINE | PVRSRV_HAP_KERNEL_ONLY,
					3 * SGX_MMU_PAGE_SIZE,
					psDevInfo->pui32BIFResetPD,
					psDevInfo->hBIFResetPDOSMemHandle);
	}
	else
	{
		OSUnMapPhysToLin(psDevInfo->pui32BIFResetPD,
                         3 * SGX_MMU_PAGE_SIZE,
                         PVRSRV_HAP_WRITECOMBINE|PVRSRV_HAP_KERNEL_ONLY,
                         psDevInfo->hBIFResetPDOSMemHandle);
						 
		sPDSysPAddr = SysDevPAddrToSysPAddr(PVRSRV_DEVICE_TYPE_SGX, psDevInfo->sBIFResetPDDevPAddr);
		RA_Free(psLocalDevMemArena, sPDSysPAddr.uiAddr, IMG_FALSE);
	}
}




#if PAGE_TEST
static void PageTest(void* pMem, IMG_DEV_PHYADDR sDevPAddr)
{
	volatile IMG_UINT32 ui32WriteData;
	volatile IMG_UINT32 ui32ReadData;
	volatile IMG_UINT32 *pMem32 = (volatile IMG_UINT32 *)pMem;
	int n;
	IMG_BOOL bOK=IMG_TRUE;

	ui32WriteData = 0xffffffff;

	for (n=0; n<1024; n++)
	{
		pMem32[n] = ui32WriteData;
		ui32ReadData = pMem32[n];

		if (ui32WriteData != ui32ReadData)
		{
			
			PVR_DPF ((PVR_DBG_ERROR, "Error - memory page test failed at device phys address 0x%08X", sDevPAddr.uiAddr + (n<<2) ));
			PVR_DBG_BREAK;
			bOK = IMG_FALSE;
		}
 	}

	ui32WriteData = 0;

	for (n=0; n<1024; n++)
	{
		pMem32[n] = ui32WriteData;
		ui32ReadData = pMem32[n];

		if (ui32WriteData != ui32ReadData)
		{
			
			PVR_DPF ((PVR_DBG_ERROR, "Error - memory page test failed at device phys address 0x%08X", sDevPAddr.uiAddr + (n<<2) ));
			PVR_DBG_BREAK;
			bOK = IMG_FALSE;
		}
 	}

	if (bOK)
	{
		PVR_DPF ((PVR_DBG_VERBOSE, "MMU Page 0x%08X is OK", sDevPAddr.uiAddr));
	}
	else
	{
		PVR_DPF ((PVR_DBG_VERBOSE, "MMU Page 0x%08X *** FAILED ***", sDevPAddr.uiAddr));
	}
}
#endif

