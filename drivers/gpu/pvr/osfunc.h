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


#ifndef __OSFUNC_H__
#define __OSFUNC_H__


#ifdef	__KERNEL__
#include <linux/string.h>
#endif

#define KERNEL_ID			0xffffffffL
#define POWER_MANAGER_ID	0xfffffffeL
#define ISR_ID				0xfffffffdL
#define TIMER_ID			0xfffffffcL

#define HOST_PAGESIZE			OSGetPageSize
#define HOST_PAGEMASK			(~(HOST_PAGESIZE()-1))
#define HOST_PAGEALIGN(addr)	(((addr)+HOST_PAGESIZE()-1)&HOST_PAGEMASK)

#define PVRSRV_OS_HEAP_MASK			0xf
#define PVRSRV_OS_PAGEABLE_HEAP		0x1
#define PVRSRV_OS_NON_PAGEABLE_HEAP	0x2

	IMG_UINT32 OSClockus(IMG_VOID);
	IMG_UINT32 OSGetPageSize(IMG_VOID);
	PVRSRV_ERROR OSInstallDeviceLISR(IMG_VOID * pvSysData,
					 IMG_UINT32 ui32Irq,
					 IMG_CHAR * pszISRName,
					 IMG_VOID * pvDeviceNode);
	PVRSRV_ERROR OSUninstallDeviceLISR(IMG_VOID * pvSysData);
	PVRSRV_ERROR OSInstallSystemLISR(IMG_VOID * pvSysData,
					 IMG_UINT32 ui32Irq);
	PVRSRV_ERROR OSUninstallSystemLISR(IMG_VOID * pvSysData);
	PVRSRV_ERROR OSInstallMISR(IMG_VOID * pvSysData);
	PVRSRV_ERROR OSUninstallMISR(IMG_VOID * pvSysData);
	PVRSRV_ERROR OSInitPerf(IMG_VOID * pvSysData);
	PVRSRV_ERROR OSCleanupPerf(IMG_VOID * pvSysData);
	IMG_CPU_PHYADDR OSMapLinToCPUPhys(IMG_VOID * pvLinAddr);
	IMG_VOID OSMemCopy(IMG_VOID * pvDst, IMG_VOID * pvSrc,
			   IMG_UINT32 ui32Size);
	IMG_VOID *OSMapPhysToLin(IMG_CPU_PHYADDR BasePAddr,
				 IMG_UINT32 ui32Bytes, IMG_UINT32 ui32Flags,
				 IMG_HANDLE * phOSMemHandle);
	IMG_BOOL OSUnMapPhysToLin(IMG_VOID * pvLinAddr, IMG_UINT32 ui32Bytes,
				  IMG_UINT32 ui32Flags,
				  IMG_HANDLE hOSMemHandle);

	PVRSRV_ERROR OSReservePhys(IMG_CPU_PHYADDR BasePAddr,
				   IMG_UINT32 ui32Bytes, IMG_UINT32 ui32Flags,
				   IMG_VOID ** ppvCpuVAddr,
				   IMG_HANDLE * phOSMemHandle);
	PVRSRV_ERROR OSUnReservePhys(IMG_VOID * pvCpuVAddr,
				     IMG_UINT32 ui32Bytes, IMG_UINT32 ui32Flags,
				     IMG_HANDLE hOSMemHandle);

	PVRSRV_ERROR OSRegisterDiscontigMem(IMG_SYS_PHYADDR * pBasePAddr,
					    IMG_VOID * pvCpuVAddr,
					    IMG_UINT32 ui32Bytes,
					    IMG_UINT32 ui32Flags,
					    IMG_HANDLE * phOSMemHandle);
	PVRSRV_ERROR OSUnRegisterDiscontigMem(IMG_VOID * pvCpuVAddr,
					      IMG_UINT32 ui32Bytes,
					      IMG_UINT32 ui32Flags,
					      IMG_HANDLE hOSMemHandle);

	static INLINE PVRSRV_ERROR OSReserveDiscontigPhys(IMG_SYS_PHYADDR *
							  pBasePAddr,
							  IMG_UINT32 ui32Bytes,
							  IMG_UINT32 ui32Flags,
							  IMG_VOID **
							  ppvCpuVAddr,
							  IMG_HANDLE *
							  phOSMemHandle) {
		*ppvCpuVAddr = IMG_NULL;
		return OSRegisterDiscontigMem(pBasePAddr, *ppvCpuVAddr,
					      ui32Bytes, ui32Flags,
					      phOSMemHandle);
	}

	static INLINE PVRSRV_ERROR OSUnReserveDiscontigPhys(IMG_VOID *
							    pvCpuVAddr,
							    IMG_UINT32
							    ui32Bytes,
							    IMG_UINT32
							    ui32Flags,
							    IMG_HANDLE
							    hOSMemHandle) {
		OSUnRegisterDiscontigMem(pvCpuVAddr, ui32Bytes, ui32Flags,
					 hOSMemHandle);

		return PVRSRV_OK;
	}

	PVRSRV_ERROR OSRegisterMem(IMG_CPU_PHYADDR BasePAddr,
				   IMG_VOID * pvCpuVAddr, IMG_UINT32 ui32Bytes,
				   IMG_UINT32 ui32Flags,
				   IMG_HANDLE * phOSMemHandle);
	PVRSRV_ERROR OSUnRegisterMem(IMG_VOID * pvCpuVAddr,
				     IMG_UINT32 ui32Bytes, IMG_UINT32 ui32Flags,
				     IMG_HANDLE hOSMemHandle);

	PVRSRV_ERROR OSGetSubMemHandle(IMG_HANDLE hOSMemHandle,
				       IMG_UINT32 ui32ByteOffset,
				       IMG_UINT32 ui32Bytes,
				       IMG_UINT32 ui32Flags,
				       IMG_HANDLE * phOSMemHandleRet);
	PVRSRV_ERROR OSReleaseSubMemHandle(IMG_HANDLE hOSMemHandle,
					   IMG_UINT32 ui32Flags);

	IMG_UINT32 OSGetCurrentProcessIDKM(IMG_VOID);
	IMG_UINT32 OSGetCurrentThreadID(IMG_VOID);
	IMG_VOID OSMemSet(IMG_VOID * pvDest, IMG_UINT8 ui8Value,
			  IMG_UINT32 ui32Size);

#ifdef DEBUG_LINUX_MEMORY_ALLOCATIONS
	PVRSRV_ERROR _OSAllocMem(IMG_UINT32 ui32Flags, IMG_UINT32 ui32Size,
				 IMG_PVOID * ppvLinAddr,
				 IMG_HANDLE * phBlockAlloc,
				 IMG_CHAR * pszFilename, IMG_UINT32 ui32Line);
#define OSAllocMem(ui32Flags, ui32Size, ppvLinAddr, phBlockAlloc) _OSAllocMem(ui32Flags, ui32Size, ppvLinAddr, phBlockAlloc, __FILE__, __LINE__)
	PVRSRV_ERROR _OSFreeMem(IMG_UINT32 ui32Flags, IMG_UINT32 ui32Size,
				IMG_PVOID pvLinAddr, IMG_HANDLE hBlockAlloc,
				IMG_CHAR * pszFilename, IMG_UINT32 ui32Line);
#define OSFreeMem(ui32Flags, ui32Size, pvLinAddr, phBlockAlloc) _OSFreeMem(ui32Flags, ui32Size, pvLinAddr, phBlockAlloc, __FILE__, __LINE__)
#else
	PVRSRV_ERROR OSAllocMem(IMG_UINT32 ui32Flags, IMG_UINT32 ui32Size,
				IMG_PVOID * ppvLinAddr,
				IMG_HANDLE * phBlockAlloc);
	PVRSRV_ERROR OSFreeMem(IMG_UINT32 ui32Flags, IMG_UINT32 ui32Size,
			       IMG_PVOID pvLinAddr, IMG_HANDLE hBlockAlloc);
#endif
	PVRSRV_ERROR OSAllocPages(IMG_UINT32 ui32Flags, IMG_UINT32 ui32Size,
				  IMG_PVOID * ppvLinAddr,
				  IMG_HANDLE * phPageAlloc);
	PVRSRV_ERROR OSFreePages(IMG_UINT32 ui32Flags, IMG_UINT32 ui32Size,
				 IMG_PVOID pvLinAddr, IMG_HANDLE hPageAlloc);
	IMG_CPU_PHYADDR OSMemHandleToCpuPAddr(IMG_VOID * hOSMemHandle,
					      IMG_UINT32 ui32ByteOffset);
	PVRSRV_ERROR OSInitEnvData(IMG_PVOID * ppvEnvSpecificData);
	PVRSRV_ERROR OSDeInitEnvData(IMG_PVOID pvEnvSpecificData);
	IMG_CHAR *OSStringCopy(IMG_CHAR * pszDest, const IMG_CHAR * pszSrc);
	IMG_INT32 OSSNPrintf(IMG_CHAR * pStr, IMG_UINT32 ui32Size,
			     const IMG_CHAR * pszFormat, ...);
#define OSStringLength(pszString) strlen(pszString)

	PVRSRV_ERROR OSEventObjectCreate(const IMG_CHAR * pszName,
					 PVRSRV_EVENTOBJECT * psEventObject);
	PVRSRV_ERROR OSEventObjectDestroy(PVRSRV_EVENTOBJECT * psEventObject);
	PVRSRV_ERROR OSEventObjectSignal(IMG_HANDLE hOSEventKM);
	PVRSRV_ERROR OSEventObjectWait(IMG_HANDLE hOSEventKM);
	PVRSRV_ERROR OSEventObjectOpen(PVRSRV_EVENTOBJECT * psEventObject,
				       IMG_HANDLE * phOSEvent);
	PVRSRV_ERROR OSEventObjectClose(PVRSRV_EVENTOBJECT * psEventObject,
					IMG_HANDLE hOSEventKM);

	PVRSRV_ERROR OSBaseAllocContigMemory(IMG_UINT32 ui32Size,
					     IMG_CPU_VIRTADDR * pLinAddr,
					     IMG_CPU_PHYADDR * pPhysAddr);
	PVRSRV_ERROR OSBaseFreeContigMemory(IMG_UINT32 ui32Size,
					    IMG_CPU_VIRTADDR LinAddr,
					    IMG_CPU_PHYADDR PhysAddr);

	IMG_PVOID MapUserFromKernel(IMG_PVOID pvLinAddrKM, IMG_UINT32 ui32Size,
				    IMG_HANDLE * phMemBlock);
	IMG_PVOID OSMapHWRegsIntoUserSpace(IMG_HANDLE hDevCookie,
					   IMG_SYS_PHYADDR sRegAddr,
					   IMG_UINT32 ulSize,
					   IMG_PVOID * ppvProcess);
	IMG_VOID OSUnmapHWRegsFromUserSpace(IMG_HANDLE hDevCookie,
					    IMG_PVOID pvUserAddr,
					    IMG_PVOID pvProcess);

	IMG_VOID UnmapUserFromKernel(IMG_PVOID pvLinAddrUM, IMG_UINT32 ui32Size,
				     IMG_HANDLE hMemBlock);

	PVRSRV_ERROR OSMapPhysToUserSpace(IMG_HANDLE hDevCookie,
					  IMG_SYS_PHYADDR sCPUPhysAddr,
					  IMG_UINT32 uiSizeInBytes,
					  IMG_UINT32 ui32CacheFlags,
					  IMG_PVOID * ppvUserAddr,
					  IMG_UINT32 * puiActualSize,
					  IMG_HANDLE hMappingHandle);

	PVRSRV_ERROR OSUnmapPhysToUserSpace(IMG_HANDLE hDevCookie,
					    IMG_PVOID pvUserAddr,
					    IMG_PVOID pvProcess);

	PVRSRV_ERROR OSLockResource(PVRSRV_RESOURCE * psResource,
				    IMG_UINT32 ui32ID);
	PVRSRV_ERROR OSUnlockResource(PVRSRV_RESOURCE * psResource,
				      IMG_UINT32 ui32ID);
	IMG_BOOL OSIsResourceLocked(PVRSRV_RESOURCE * psResource,
				    IMG_UINT32 ui32ID);
	PVRSRV_ERROR OSCreateResource(PVRSRV_RESOURCE * psResource);
	PVRSRV_ERROR OSDestroyResource(PVRSRV_RESOURCE * psResource);
	IMG_VOID OSBreakResourceLock(PVRSRV_RESOURCE * psResource,
				     IMG_UINT32 ui32ID);
	IMG_VOID OSWaitus(IMG_UINT32 ui32Timeus);
	IMG_VOID OSReleaseThreadQuanta(IMG_VOID);
	IMG_UINT32 OSPCIReadDword(IMG_UINT32 ui32Bus, IMG_UINT32 ui32Dev,
				  IMG_UINT32 ui32Func, IMG_UINT32 ui32Reg);
	IMG_VOID OSPCIWriteDword(IMG_UINT32 ui32Bus, IMG_UINT32 ui32Dev,
				 IMG_UINT32 ui32Func, IMG_UINT32 ui32Reg,
				 IMG_UINT32 ui32Value);

	IMG_UINT32 OSReadHWReg(IMG_PVOID pvLinRegBaseAddr,
			       IMG_UINT32 ui32Offset);
	IMG_VOID OSWriteHWReg(IMG_PVOID pvLinRegBaseAddr, IMG_UINT32 ui32Offset,
			      IMG_UINT32 ui32Value);

	typedef IMG_VOID(*PFN_TIMER_FUNC) (IMG_VOID *);
	IMG_HANDLE OSAddTimer(PFN_TIMER_FUNC pfnTimerFunc, IMG_VOID * pvData,
			      IMG_UINT32 ui32MsTimeout);
	PVRSRV_ERROR OSRemoveTimer(IMG_HANDLE hTimer);
	PVRSRV_ERROR OSEnableTimer(IMG_HANDLE hTimer);
	PVRSRV_ERROR OSDisableTimer(IMG_HANDLE hTimer);

	PVRSRV_ERROR OSGetSysMemSize(IMG_UINT32 * pui32Bytes);

	typedef enum _HOST_PCI_INIT_FLAGS_ {
		HOST_PCI_INIT_FLAG_BUS_MASTER = 0x1,
		HOST_PCI_INIT_FLAG_FORCE_I32 = 0x7fffffff
	} HOST_PCI_INIT_FLAGS;

	struct _PVRSRV_PCI_DEV_OPAQUE_STRUCT_;
	typedef struct _PVRSRV_PCI_DEV_OPAQUE_STRUCT_ *PVRSRV_PCI_DEV_HANDLE;

	PVRSRV_PCI_DEV_HANDLE OSPCIAcquireDev(IMG_UINT16 ui16VendorID,
					      IMG_UINT16 ui16DeviceID,
					      HOST_PCI_INIT_FLAGS eFlags);
	PVRSRV_PCI_DEV_HANDLE OSPCISetDev(IMG_VOID * pvPCICookie,
					  HOST_PCI_INIT_FLAGS eFlags);
	PVRSRV_ERROR OSPCIReleaseDev(PVRSRV_PCI_DEV_HANDLE hPVRPCI);
	PVRSRV_ERROR OSPCIIRQ(PVRSRV_PCI_DEV_HANDLE hPVRPCI,
			      IMG_UINT32 * pui32IRQ);
	IMG_UINT32 OSPCIAddrRangeLen(PVRSRV_PCI_DEV_HANDLE hPVRPCI,
				     IMG_UINT32 ui32Index);
	IMG_UINT32 OSPCIAddrRangeStart(PVRSRV_PCI_DEV_HANDLE hPVRPCI,
				       IMG_UINT32 ui32Index);
	IMG_UINT32 OSPCIAddrRangeEnd(PVRSRV_PCI_DEV_HANDLE hPVRPCI,
				     IMG_UINT32 ui32Index);
	PVRSRV_ERROR OSPCIRequestAddrRange(PVRSRV_PCI_DEV_HANDLE hPVRPCI,
					   IMG_UINT32 ui32Index);
	PVRSRV_ERROR OSPCIReleaseAddrRange(PVRSRV_PCI_DEV_HANDLE hPVRPCI,
					   IMG_UINT32 ui32Index);
	PVRSRV_ERROR OSPCISuspendDev(PVRSRV_PCI_DEV_HANDLE hPVRPCI);
	PVRSRV_ERROR OSPCIResumeDev(PVRSRV_PCI_DEV_HANDLE hPVRPCI);

	PVRSRV_ERROR OSScheduleMISR(IMG_VOID * pvSysData);

	IMG_BOOL OSProcHasPrivSrvInit(IMG_VOID);

	typedef enum _img_verify_test {
		PVR_VERIFY_WRITE = 0,
		PVR_VERIFY_READ
	} IMG_VERIFY_TEST;

	IMG_BOOL OSAccessOK(IMG_VERIFY_TEST eVerification, IMG_VOID * pvUserPtr,
			    IMG_UINT32 ui32Bytes);

	PVRSRV_ERROR OSCopyToUser(IMG_PVOID pvProcess, IMG_VOID * pvDest,
				  IMG_VOID * pvSrc, IMG_UINT32 ui32Bytes);
	PVRSRV_ERROR OSCopyFromUser(IMG_PVOID pvProcess, IMG_VOID * pvDest,
				    IMG_VOID * pvSrc, IMG_UINT32 ui32Bytes);

	PVRSRV_ERROR OSAcquirePhysPageAddr(IMG_VOID * pvCPUVAddr,
					   IMG_UINT32 ui32Bytes,
					   IMG_SYS_PHYADDR * psSysPAddr,
					   IMG_HANDLE * phOSWrapMem);
	PVRSRV_ERROR OSReleasePhysPageAddr(IMG_HANDLE hOSWrapMem);


#endif
