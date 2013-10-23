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

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/console.h>
#include <linux/fb.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/notifier.h>
#include <linux/spinlock.h>

#include "img_defs.h"
#include "servicesext.h"
#include "kerneldisplay.h"
#include "omaplfb.h"

static IMG_VOID *gpvAnchor;

static int fb_idx = 0;

#define OMAPLFB_COMMAND_COUNT		1

static PFN_DC_GET_PVRJTABLE pfnGetPVRJTable = IMG_NULL;

static OMAPLFB_DEVINFO *GetAnchorPtr(IMG_VOID)
{
	return (OMAPLFB_DEVINFO *) gpvAnchor;
}

static IMG_VOID SetAnchorPtr(OMAPLFB_DEVINFO * psDevInfo)
{
	gpvAnchor = (IMG_VOID *) psDevInfo;
}

static IMG_VOID FlushInternalVSyncQueue(OMAPLFB_SWAPCHAIN * psSwapChain)
{
	OMAPLFB_VSYNC_FLIP_ITEM *psFlipItem;
	IMG_UINT32 ui32MaxIndex;
	IMG_UINT32 i;

	psFlipItem = &psSwapChain->psVSyncFlips[psSwapChain->ui32RemoveIndex];
	ui32MaxIndex = psSwapChain->ui32BufferCount - 1;

	for (i = 0; i < psSwapChain->ui32BufferCount; i++) {
		if (!psFlipItem->bValid) {
			continue;
		}

		DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
			      ": FlushInternalVSyncQueue: Flushing swap buffer (index %lu)\n",
			      psSwapChain->ui32RemoveIndex));

		if (psFlipItem->bFlipped == IMG_FALSE) {

			OMAPLFBFlip(psSwapChain,
				    (IMG_UINT32) psFlipItem->sSysAddr);
		}

		if (psFlipItem->bCmdCompleted == IMG_FALSE) {
			DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
				      ": FlushInternalVSyncQueue: Calling command complete for swap buffer (index %lu)\n",
				      psSwapChain->ui32RemoveIndex));

			psSwapChain->psPVRJTable->
			    pfnPVRSRVCmdComplete(psFlipItem->hCmdComplete,
						 IMG_TRUE);
		}

		psSwapChain->ui32RemoveIndex++;

		if (psSwapChain->ui32RemoveIndex > ui32MaxIndex) {
			psSwapChain->ui32RemoveIndex = 0;
		}

		psFlipItem->bFlipped = IMG_FALSE;
		psFlipItem->bCmdCompleted = IMG_FALSE;
		psFlipItem->bValid = IMG_FALSE;

		psFlipItem =
		    &psSwapChain->psVSyncFlips[psSwapChain->ui32RemoveIndex];
	}

	psSwapChain->ui32InsertIndex = 0;
	psSwapChain->ui32RemoveIndex = 0;
}

static IMG_VOID SetFlushStateInternalNoLock(OMAPLFB_DEVINFO * psDevInfo,
					    IMG_BOOL bFlushState)
{
	OMAPLFB_SWAPCHAIN *psSwapChain = psDevInfo->psSwapChain;

	if (psSwapChain == IMG_NULL) {
		return;
	}

	if (bFlushState) {
		if (psSwapChain->ui32SetFlushStateRefCount == 0) {
			OMAPLFBDisableVSyncInterrupt(psSwapChain);
			psSwapChain->bFlushCommands = IMG_TRUE;
			FlushInternalVSyncQueue(psSwapChain);
		}
		psSwapChain->ui32SetFlushStateRefCount++;
	} else {
		if (psSwapChain->ui32SetFlushStateRefCount != 0) {
			psSwapChain->ui32SetFlushStateRefCount--;
			if (psSwapChain->ui32SetFlushStateRefCount == 0) {
				psSwapChain->bFlushCommands = IMG_FALSE;
				OMAPLFBEnableVSyncInterrupt(psSwapChain);
			}
		}
	}
}

static IMG_VOID SetFlushStateInternal(OMAPLFB_DEVINFO * psDevInfo,
				      IMG_BOOL bFlushState)
{
	unsigned long ulLockFlags;

	spin_lock_irqsave(&psDevInfo->SwapChainLock, ulLockFlags);

	SetFlushStateInternalNoLock(psDevInfo, bFlushState);

	spin_unlock_irqrestore(&psDevInfo->SwapChainLock, ulLockFlags);
}

static IMG_VOID SetFlushStateExternal(OMAPLFB_DEVINFO * psDevInfo,
				      IMG_BOOL bFlushState)
{
	unsigned long ulLockFlags;

	spin_lock_irqsave(&psDevInfo->SwapChainLock, ulLockFlags);

	if (psDevInfo->bFlushCommands != bFlushState) {
		psDevInfo->bFlushCommands = bFlushState;
		SetFlushStateInternalNoLock(psDevInfo, bFlushState);
	}

	spin_unlock_irqrestore(&psDevInfo->SwapChainLock, ulLockFlags);
}

static IMG_VOID SetDCState(IMG_HANDLE hDevice, IMG_UINT32 ui32State)
{
	OMAPLFB_DEVINFO *psDevInfo = (OMAPLFB_DEVINFO *) hDevice;

	switch (ui32State) {
	case DC_STATE_FLUSH_COMMANDS:
		SetFlushStateExternal(psDevInfo, IMG_TRUE);
		break;
	case DC_STATE_NO_FLUSH_COMMANDS:
		SetFlushStateExternal(psDevInfo, IMG_FALSE);
		break;
	default:
		break;
	}

	return;
}

static int FrameBufferEvents(struct notifier_block *psNotif,
			     unsigned long event, void *data)
{
	OMAPLFB_DEVINFO *psDevInfo;
	OMAPLFB_SWAPCHAIN *psSwapChain;
	struct fb_event *psFBEvent = (struct fb_event *)data;
	IMG_BOOL bBlanked;;

	if (event != FB_EVENT_BLANK) {
		return 0;
	}

	psDevInfo = GetAnchorPtr();
	psSwapChain = psDevInfo->psSwapChain;

	bBlanked = (*(int *)psFBEvent->data != 0);

	if (bBlanked != psSwapChain->bBlanked) {
		psSwapChain->bBlanked = bBlanked;

		if (bBlanked) {

			SetFlushStateInternal(psDevInfo, IMG_TRUE);
		} else {

			SetFlushStateInternal(psDevInfo, IMG_FALSE);
		}
	}

	return 0;
}

static PVRSRV_ERROR UnblankDisplay(OMAPLFB_DEVINFO * psDevInfo)
{
	int res;

	acquire_console_sem();
	res = fb_blank(psDevInfo->psLINFBInfo, 0);
	release_console_sem();
	if (res != 0) {
		printk(KERN_WARNING DRIVER_PREFIX
		       ": fb_blank failed (%d)", res);
		return PVRSRV_ERROR_GENERIC;
	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR EnableLFBEventNotification(OMAPLFB_DEVINFO * psDevInfo)
{
	int res;
	OMAPLFB_SWAPCHAIN *psSwapChain = psDevInfo->psSwapChain;
	PVRSRV_ERROR eError;

	memset(&psDevInfo->sLINNotifBlock, 0,
	       sizeof(psDevInfo->sLINNotifBlock));

	psDevInfo->sLINNotifBlock.notifier_call = FrameBufferEvents;

	psSwapChain->bBlanked = IMG_FALSE;

	res = fb_register_client(&psDevInfo->sLINNotifBlock);
	if (res != 0) {
		printk(KERN_WARNING DRIVER_PREFIX
		       ": fb_register_client failed (%d)", res);

		return PVRSRV_ERROR_GENERIC;
	}

	eError = UnblankDisplay(psDevInfo);
	if (eError != PVRSRV_OK) {
		DEBUG_PRINTK((KERN_WARNING DRIVER_PREFIX
			      ": UnblankDisplay failed (%d)", eError));
		return eError;
	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR DisableLFBEventNotification(OMAPLFB_DEVINFO * psDevInfo)
{
	int res;

	res = fb_unregister_client(&psDevInfo->sLINNotifBlock);
	if (res != 0) {
		printk(KERN_WARNING DRIVER_PREFIX
		       ": fb_unregister_client failed (%d)", res);
		return PVRSRV_ERROR_GENERIC;
	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR OpenDCDevice(IMG_UINT32 ui32DeviceID,
				 IMG_HANDLE * phDevice,
				 PVRSRV_SYNC_DATA * psSystemBufferSyncData)
{
	OMAPLFB_DEVINFO *psDevInfo;
	PVRSRV_ERROR eError;

	PVR_UNREFERENCED_PARAMETER(ui32DeviceID);

	psDevInfo = GetAnchorPtr();

	psDevInfo->sSystemBuffer.psSyncData = psSystemBufferSyncData;

	eError = UnblankDisplay(psDevInfo);
	if (eError != PVRSRV_OK) {
		DEBUG_PRINTK((KERN_WARNING DRIVER_PREFIX
			      ": UnblankDisplay failed (%d)", eError));
		return eError;
	}

	*phDevice = (IMG_HANDLE) psDevInfo;

	return PVRSRV_OK;
}

static PVRSRV_ERROR CloseDCDevice(IMG_HANDLE hDevice)
{
	PVR_UNREFERENCED_PARAMETER(hDevice);

	return PVRSRV_OK;
}

static PVRSRV_ERROR EnumDCFormats(IMG_HANDLE hDevice,
				  IMG_UINT32 * pui32NumFormats,
				  DISPLAY_FORMAT * psFormat)
{
	OMAPLFB_DEVINFO *psDevInfo;

	if (!hDevice || !pui32NumFormats) {
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo = (OMAPLFB_DEVINFO *) hDevice;

	*pui32NumFormats = 1;

	if (psFormat) {
		psFormat[0] = psDevInfo->sDisplayFormat;
	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR EnumDCDims(IMG_HANDLE hDevice,
			       DISPLAY_FORMAT * psFormat,
			       IMG_UINT32 * pui32NumDims, DISPLAY_DIMS * psDim)
{
	OMAPLFB_DEVINFO *psDevInfo;

	if (!hDevice || !psFormat || !pui32NumDims) {
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo = (OMAPLFB_DEVINFO *) hDevice;

	*pui32NumDims = 1;

	if (psDim) {
		psDim[0] = psDevInfo->sDisplayDim;
	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR GetDCSystemBuffer(IMG_HANDLE hDevice, IMG_HANDLE * phBuffer)
{
	OMAPLFB_DEVINFO *psDevInfo;

	if (!hDevice || !phBuffer) {
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo = (OMAPLFB_DEVINFO *) hDevice;

	*phBuffer = (IMG_HANDLE) & psDevInfo->sSystemBuffer;

	return PVRSRV_OK;
}

static PVRSRV_ERROR GetDCInfo(IMG_HANDLE hDevice, DISPLAY_INFO * psDCInfo)
{
	OMAPLFB_DEVINFO *psDevInfo;

	if (!hDevice || !psDCInfo) {
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo = (OMAPLFB_DEVINFO *) hDevice;

	*psDCInfo = psDevInfo->sDisplayInfo;

	return PVRSRV_OK;
}

static PVRSRV_ERROR GetDCBufferAddr(IMG_HANDLE hDevice,
				    IMG_HANDLE hBuffer,
				    IMG_SYS_PHYADDR ** ppsSysAddr,
				    IMG_UINT32 * pui32ByteSize,
				    IMG_VOID ** ppvCpuVAddr,
				    IMG_HANDLE * phOSMapInfo,
				    IMG_BOOL * pbIsContiguous)
{
	OMAPLFB_DEVINFO *psDevInfo;
	OMAPLFB_BUFFER *psSystemBuffer;

	if (!hDevice) {
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
	psDevInfo = (OMAPLFB_DEVINFO *) hDevice;

	if (!hBuffer) {
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
	psSystemBuffer = (OMAPLFB_BUFFER *) hBuffer;

	if (!ppsSysAddr) {
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	*ppsSysAddr = &psSystemBuffer->sSysAddr;

	if (!pui32ByteSize) {
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	*pui32ByteSize = psDevInfo->sFBInfo.ui32BufferSize;

	if (ppvCpuVAddr) {
		*ppvCpuVAddr = psSystemBuffer->sCPUVAddr;
	}

	if (phOSMapInfo) {
		*phOSMapInfo = (IMG_HANDLE) 0;
	}

	if (pbIsContiguous) {
		*pbIsContiguous = IMG_TRUE;
	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR CreateDCSwapChain(IMG_HANDLE hDevice,
				      IMG_UINT32 ui32Flags,
				      DISPLAY_SURF_ATTRIBUTES * psDstSurfAttrib,
				      DISPLAY_SURF_ATTRIBUTES * psSrcSurfAttrib,
				      IMG_UINT32 ui32BufferCount,
				      PVRSRV_SYNC_DATA ** ppsSyncData,
				      IMG_UINT32 ui32OEMFlags,
				      IMG_HANDLE * phSwapChain,
				      IMG_UINT32 * pui32SwapChainID)
{
	OMAPLFB_DEVINFO *psDevInfo;
	OMAPLFB_SWAPCHAIN *psSwapChain;
	OMAPLFB_BUFFER *psBuffer;
	OMAPLFB_VSYNC_FLIP_ITEM *psVSyncFlips;
	IMG_UINT32 i;
	PVRSRV_ERROR eError = PVRSRV_ERROR_GENERIC;
	unsigned long ulLockFlags;
	IMG_UINT32 ui32BuffersToSkip;

	PVR_UNREFERENCED_PARAMETER(ui32OEMFlags);
	PVR_UNREFERENCED_PARAMETER(pui32SwapChainID);

	if (!hDevice
	    || !psDstSurfAttrib
	    || !psSrcSurfAttrib || !ppsSyncData || !phSwapChain) {
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo = (OMAPLFB_DEVINFO *) hDevice;

	if (psDevInfo->sDisplayInfo.ui32MaxSwapChains == 0) {
		return PVRSRV_ERROR_NOT_SUPPORTED;
	}

	if (psDevInfo->psSwapChain != IMG_NULL) {
		return PVRSRV_ERROR_FLIP_CHAIN_EXISTS;
	}

	if (ui32BufferCount > psDevInfo->sDisplayInfo.ui32MaxSwapChainBuffers) {
		return PVRSRV_ERROR_TOOMANYBUFFERS;
	}

	if ((psDevInfo->sFBInfo.ui32RoundedBufferSize * ui32BufferCount) >
	    psDevInfo->sFBInfo.ui32FBSize) {
		return PVRSRV_ERROR_TOOMANYBUFFERS;
	}

	ui32BuffersToSkip =
	    psDevInfo->sDisplayInfo.ui32MaxSwapChainBuffers - ui32BufferCount;

	if (psDstSurfAttrib->pixelformat !=
	    psDevInfo->sDisplayFormat.pixelformat
	    || psDstSurfAttrib->sDims.ui32ByteStride !=
	    psDevInfo->sDisplayDim.ui32ByteStride
	    || psDstSurfAttrib->sDims.ui32Width !=
	    psDevInfo->sDisplayDim.ui32Width
	    || psDstSurfAttrib->sDims.ui32Height !=
	    psDevInfo->sDisplayDim.ui32Height) {

		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if (psDstSurfAttrib->pixelformat != psSrcSurfAttrib->pixelformat
	    || psDstSurfAttrib->sDims.ui32ByteStride !=
	    psSrcSurfAttrib->sDims.ui32ByteStride
	    || psDstSurfAttrib->sDims.ui32Width !=
	    psSrcSurfAttrib->sDims.ui32Width
	    || psDstSurfAttrib->sDims.ui32Height !=
	    psSrcSurfAttrib->sDims.ui32Height) {

		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	PVR_UNREFERENCED_PARAMETER(ui32Flags);

	psSwapChain =
	    (OMAPLFB_SWAPCHAIN *)
	    OMAPLFBAllocKernelMem(sizeof(OMAPLFB_SWAPCHAIN));
	if (!psSwapChain) {
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	psBuffer =
	    (OMAPLFB_BUFFER *) OMAPLFBAllocKernelMem(sizeof(OMAPLFB_BUFFER) *
						     ui32BufferCount);
	if (!psBuffer) {
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto ErrorFreeSwapChain;
	}

	psVSyncFlips =
	    (OMAPLFB_VSYNC_FLIP_ITEM *)
	    OMAPLFBAllocKernelMem(sizeof(OMAPLFB_VSYNC_FLIP_ITEM) *
				  ui32BufferCount);
	if (!psVSyncFlips) {
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto ErrorFreeBuffers;
	}

	psSwapChain->ui32BufferCount = ui32BufferCount;
	psSwapChain->psBuffer = psBuffer;
	psSwapChain->psVSyncFlips = psVSyncFlips;
	psSwapChain->ui32InsertIndex = 0;
	psSwapChain->ui32RemoveIndex = 0;
	psSwapChain->psPVRJTable = &psDevInfo->sPVRJTable;
	psSwapChain->psSwapChainLock = &psDevInfo->SwapChainLock;

	for (i = 0; i < ui32BufferCount - 1; i++) {
		psBuffer[i].psNext = &psBuffer[i + 1];
	}

	psBuffer[i].psNext = &psBuffer[0];

	for (i = 0; i < ui32BufferCount; i++) {
		IMG_UINT32 ui32SwapBuffer = i + ui32BuffersToSkip;
		IMG_UINT32 ui32BufferOffset =
		    ui32SwapBuffer * psDevInfo->sFBInfo.ui32RoundedBufferSize;

		psBuffer[i].psSyncData = ppsSyncData[i];

		psBuffer[i].sSysAddr.uiAddr =
		    psDevInfo->sFBInfo.sSysAddr.uiAddr + ui32BufferOffset;
		psBuffer[i].sCPUVAddr =
		    psDevInfo->sFBInfo.sCPUVAddr + ui32BufferOffset;
	}

	for (i = 0; i < ui32BufferCount; i++) {
		psVSyncFlips[i].bValid = IMG_FALSE;
		psVSyncFlips[i].bFlipped = IMG_FALSE;
		psVSyncFlips[i].bCmdCompleted = IMG_FALSE;
	}

	OMAPLFBEnableDisplayRegisterAccess();

	psSwapChain->pvRegs =
	    ioremap(psDevInfo->psLINFBInfo->fix.mmio_start,
		    psDevInfo->psLINFBInfo->fix.mmio_len);
	if (psSwapChain->pvRegs == IMG_NULL) {
		printk(KERN_WARNING DRIVER_PREFIX
		       ": Couldn't map registers needed for flipping\n");
		goto ErrorDisableDisplayRegisters;
	}

	if (OMAPLFBInstallVSyncISR(psSwapChain) != PVRSRV_OK) {
		printk(KERN_WARNING DRIVER_PREFIX
		       ": ISR handler failed to register\n");
		goto ErrorUnmapRegisters;
	}

	spin_lock_irqsave(&psDevInfo->SwapChainLock, ulLockFlags);

	psDevInfo->psSwapChain = psSwapChain;

	psSwapChain->bFlushCommands = psDevInfo->bFlushCommands;

	if (psSwapChain->bFlushCommands) {
		psSwapChain->ui32SetFlushStateRefCount = 1;
	} else {
		psSwapChain->ui32SetFlushStateRefCount = 0;
		OMAPLFBEnableVSyncInterrupt(psSwapChain);
	}

	spin_unlock_irqrestore(&psDevInfo->SwapChainLock, ulLockFlags);

	eError = EnableLFBEventNotification(psDevInfo);
	if (eError != PVRSRV_OK) {
		printk(KERN_WARNING DRIVER_PREFIX
		       ": Couldn't enable framebuffer event notification\n");
		goto ErrorUninstallVSyncInterrupt;
	}

	*phSwapChain = (IMG_HANDLE) psSwapChain;

	return PVRSRV_OK;

ErrorUninstallVSyncInterrupt:
	if (OMAPLFBUninstallVSyncISR(psSwapChain) != PVRSRV_OK) {
		printk(KERN_WARNING DRIVER_PREFIX
		       ": Couldn't uninstall VSync ISR\n");
	}
ErrorUnmapRegisters:
	iounmap(psSwapChain->pvRegs);
ErrorDisableDisplayRegisters:
	OMAPLFBDisableDisplayRegisterAccess();
	OMAPLFBFreeKernelMem(psVSyncFlips);
ErrorFreeBuffers:
	OMAPLFBFreeKernelMem(psBuffer);
ErrorFreeSwapChain:
	OMAPLFBFreeKernelMem(psSwapChain);

	return eError;
}

static PVRSRV_ERROR DestroyDCSwapChain(IMG_HANDLE hDevice,
				       IMG_HANDLE hSwapChain)
{
	OMAPLFB_DEVINFO *psDevInfo;
	OMAPLFB_SWAPCHAIN *psSwapChain;
	unsigned long ulLockFlags;
	PVRSRV_ERROR eError;

	if (!hDevice || !hSwapChain) {
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo = (OMAPLFB_DEVINFO *) hDevice;
	psSwapChain = (OMAPLFB_SWAPCHAIN *) hSwapChain;
	if (psSwapChain != psDevInfo->psSwapChain) {
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	eError = DisableLFBEventNotification(psDevInfo);
	if (eError != PVRSRV_OK) {
		printk(KERN_WARNING DRIVER_PREFIX
		       ": Couldn't disable framebuffer event notification\n");
	}

	spin_lock_irqsave(&psDevInfo->SwapChainLock, ulLockFlags);

	OMAPLFBDisableVSyncInterrupt(psSwapChain);

	FlushInternalVSyncQueue(psSwapChain);

	OMAPLFBFlip(psSwapChain, psDevInfo->sFBInfo.sSysAddr.uiAddr);

	psDevInfo->psSwapChain = IMG_NULL;

	spin_unlock_irqrestore(&psDevInfo->SwapChainLock, ulLockFlags);

	if (OMAPLFBUninstallVSyncISR(psSwapChain) != PVRSRV_OK) {
		printk(KERN_WARNING DRIVER_PREFIX
		       ": Couldn't uninstall VSync ISR\n");
		return PVRSRV_ERROR_GENERIC;
	}

	iounmap(psSwapChain->pvRegs);

	OMAPLFBDisableDisplayRegisterAccess();

	OMAPLFBFreeKernelMem(psSwapChain->psVSyncFlips);
	OMAPLFBFreeKernelMem(psSwapChain->psBuffer);
	OMAPLFBFreeKernelMem(psSwapChain);

	return PVRSRV_OK;
}

static PVRSRV_ERROR SetDCDstRect(IMG_HANDLE hDevice,
				 IMG_HANDLE hSwapChain, IMG_RECT * psRect)
{
	PVR_UNREFERENCED_PARAMETER(hDevice);
	PVR_UNREFERENCED_PARAMETER(hSwapChain);
	PVR_UNREFERENCED_PARAMETER(psRect);

	return PVRSRV_ERROR_NOT_SUPPORTED;
}

static PVRSRV_ERROR SetDCSrcRect(IMG_HANDLE hDevice,
				 IMG_HANDLE hSwapChain, IMG_RECT * psRect)
{
	PVR_UNREFERENCED_PARAMETER(hDevice);
	PVR_UNREFERENCED_PARAMETER(hSwapChain);
	PVR_UNREFERENCED_PARAMETER(psRect);

	return PVRSRV_ERROR_NOT_SUPPORTED;
}

static PVRSRV_ERROR SetDCDstColourKey(IMG_HANDLE hDevice,
				      IMG_HANDLE hSwapChain,
				      IMG_UINT32 ui32CKColour)
{
	PVR_UNREFERENCED_PARAMETER(hDevice);
	PVR_UNREFERENCED_PARAMETER(hSwapChain);
	PVR_UNREFERENCED_PARAMETER(ui32CKColour);

	return PVRSRV_ERROR_NOT_SUPPORTED;
}

static PVRSRV_ERROR SetDCSrcColourKey(IMG_HANDLE hDevice,
				      IMG_HANDLE hSwapChain,
				      IMG_UINT32 ui32CKColour)
{
	PVR_UNREFERENCED_PARAMETER(hDevice);
	PVR_UNREFERENCED_PARAMETER(hSwapChain);
	PVR_UNREFERENCED_PARAMETER(ui32CKColour);

	return PVRSRV_ERROR_NOT_SUPPORTED;
}

static PVRSRV_ERROR GetDCBuffers(IMG_HANDLE hDevice,
				 IMG_HANDLE hSwapChain,
				 IMG_UINT32 * pui32BufferCount,
				 IMG_HANDLE * phBuffer)
{
	OMAPLFB_DEVINFO *psDevInfo;
	OMAPLFB_SWAPCHAIN *psSwapChain;
	IMG_UINT32 i;

	if (!hDevice || !hSwapChain || !pui32BufferCount || !phBuffer) {
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo = (OMAPLFB_DEVINFO *) hDevice;
	psSwapChain = (OMAPLFB_SWAPCHAIN *) hSwapChain;
	if (psSwapChain != psDevInfo->psSwapChain) {
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	*pui32BufferCount = psSwapChain->ui32BufferCount;

	for (i = 0; i < psSwapChain->ui32BufferCount; i++) {
		phBuffer[i] = (IMG_HANDLE) & psSwapChain->psBuffer[i];
	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR SwapToDCBuffer(IMG_HANDLE hDevice,
				   IMG_HANDLE hBuffer,
				   IMG_UINT32 ui32SwapInterval,
				   IMG_HANDLE hPrivateTag,
				   IMG_UINT32 ui32ClipRectCount,
				   IMG_RECT * psClipRect)
{
	OMAPLFB_DEVINFO *psDevInfo;

	PVR_UNREFERENCED_PARAMETER(ui32SwapInterval);
	PVR_UNREFERENCED_PARAMETER(hPrivateTag);
	PVR_UNREFERENCED_PARAMETER(psClipRect);

	if (!hDevice || !hBuffer || (ui32ClipRectCount != 0)) {
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo = (OMAPLFB_DEVINFO *) hDevice;

	PVR_UNREFERENCED_PARAMETER(hBuffer);

	return PVRSRV_OK;
}

static PVRSRV_ERROR SwapToDCSystem(IMG_HANDLE hDevice, IMG_HANDLE hSwapChain)
{
	OMAPLFB_DEVINFO *psDevInfo;
	OMAPLFB_SWAPCHAIN *psSwapChain;
	unsigned long ulLockFlags;

	if (!hDevice || !hSwapChain) {
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo = (OMAPLFB_DEVINFO *) hDevice;
	psSwapChain = (OMAPLFB_SWAPCHAIN *) hSwapChain;
	if (psSwapChain != psDevInfo->psSwapChain) {
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	spin_lock_irqsave(&psDevInfo->SwapChainLock, ulLockFlags);

	FlushInternalVSyncQueue(psSwapChain);

	OMAPLFBFlip(psSwapChain, psDevInfo->sFBInfo.sSysAddr.uiAddr);

	spin_unlock_irqrestore(&psDevInfo->SwapChainLock, ulLockFlags);

	return PVRSRV_OK;
}

IMG_BOOL OMAPLFBVSyncIHandler(OMAPLFB_SWAPCHAIN * psSwapChain)
{
	IMG_BOOL bStatus = IMG_FALSE;
	OMAPLFB_VSYNC_FLIP_ITEM *psFlipItem;
	IMG_UINT32 ui32MaxIndex;
	unsigned long ulLockFlags;

	psFlipItem = &psSwapChain->psVSyncFlips[psSwapChain->ui32RemoveIndex];
	ui32MaxIndex = psSwapChain->ui32BufferCount - 1;

	spin_lock_irqsave(psSwapChain->psSwapChainLock, ulLockFlags);

	if (psSwapChain->bFlushCommands) {
		goto ExitUnlock;
	}

	while (psFlipItem->bValid) {

		if (psFlipItem->bFlipped) {

			if (!psFlipItem->bCmdCompleted) {

				psSwapChain->psPVRJTable->
				    pfnPVRSRVCmdComplete(psFlipItem->
							 hCmdComplete,
							 IMG_TRUE);

				psFlipItem->bCmdCompleted = IMG_TRUE;
			}

			psFlipItem->ui32SwapInterval--;

			if (psFlipItem->ui32SwapInterval == 0) {

				psSwapChain->ui32RemoveIndex++;

				if (psSwapChain->ui32RemoveIndex > ui32MaxIndex) {
					psSwapChain->ui32RemoveIndex = 0;
				}

				psFlipItem->bCmdCompleted = IMG_FALSE;
				psFlipItem->bFlipped = IMG_FALSE;

				psFlipItem->bValid = IMG_FALSE;
			} else {

				break;
			}
		} else {

			OMAPLFBFlip(psSwapChain,
				    (IMG_UINT32) psFlipItem->sSysAddr);

			psFlipItem->bFlipped = IMG_TRUE;

			break;
		}

		psFlipItem =
		    &psSwapChain->psVSyncFlips[psSwapChain->ui32RemoveIndex];
	}

ExitUnlock:
	spin_unlock_irqrestore(psSwapChain->psSwapChainLock, ulLockFlags);

	return bStatus;
}

static IMG_BOOL ProcessFlip(IMG_HANDLE hCmdCookie,
			    IMG_UINT32 ui32DataSize, IMG_VOID * pvData)
{
	DISPLAYCLASS_FLIP_COMMAND *psFlipCmd;
	OMAPLFB_DEVINFO *psDevInfo;
	OMAPLFB_BUFFER *psBuffer;
	OMAPLFB_SWAPCHAIN *psSwapChain;
	OMAPLFB_VSYNC_FLIP_ITEM *psFlipItem;
	unsigned long ulLockFlags;

	if (!hCmdCookie || !pvData) {
		return IMG_FALSE;
	}

	psFlipCmd = (DISPLAYCLASS_FLIP_COMMAND *) pvData;

	if (psFlipCmd == IMG_NULL
	    || sizeof(DISPLAYCLASS_FLIP_COMMAND) != ui32DataSize) {
		return IMG_FALSE;
	}

	psDevInfo = (OMAPLFB_DEVINFO *) psFlipCmd->hExtDevice;

	psBuffer = (OMAPLFB_BUFFER *) psFlipCmd->hExtBuffer;
	psSwapChain = (OMAPLFB_SWAPCHAIN *) psFlipCmd->hExtSwapChain;

	spin_lock_irqsave(&psDevInfo->SwapChainLock, ulLockFlags);

	if (psDevInfo->bDeviceSuspended) {
		psSwapChain->psPVRJTable->pfnPVRSRVCmdComplete(hCmdCookie,
							       IMG_TRUE);
		goto ExitTrueUnlock;
	}

	if (psFlipCmd->ui32SwapInterval == 0 || psSwapChain->bFlushCommands) {

		OMAPLFBFlip(psSwapChain, psBuffer->sSysAddr.uiAddr);

		psSwapChain->psPVRJTable->pfnPVRSRVCmdComplete(hCmdCookie,
							       IMG_TRUE);

		goto ExitTrueUnlock;
	}

	psFlipItem = &psSwapChain->psVSyncFlips[psSwapChain->ui32InsertIndex];

	if (!psFlipItem->bValid) {
		IMG_UINT32 ui32MaxIndex = psSwapChain->ui32BufferCount - 1;

		if (psSwapChain->ui32InsertIndex ==
		    psSwapChain->ui32RemoveIndex) {

			OMAPLFBFlip(psSwapChain, psBuffer->sSysAddr.uiAddr);

			psFlipItem->bFlipped = IMG_TRUE;
		} else {
			psFlipItem->bFlipped = IMG_FALSE;
		}

		psFlipItem->hCmdComplete = hCmdCookie;
		psFlipItem->ui32SwapInterval = psFlipCmd->ui32SwapInterval;
		psFlipItem->sSysAddr = &psBuffer->sSysAddr;
		psFlipItem->bValid = IMG_TRUE;

		psSwapChain->ui32InsertIndex++;
		if (psSwapChain->ui32InsertIndex > ui32MaxIndex) {
			psSwapChain->ui32InsertIndex = 0;
		}

		goto ExitTrueUnlock;
	}

	spin_unlock_irqrestore(&psDevInfo->SwapChainLock, ulLockFlags);
	return IMG_FALSE;

ExitTrueUnlock:
	spin_unlock_irqrestore(&psDevInfo->SwapChainLock, ulLockFlags);
	return IMG_TRUE;
}

static void SetDevinfo(OMAPLFB_DEVINFO * psDevInfo)
{
	OMAPLFB_FBINFO *psPVRFBInfo = &psDevInfo->sFBInfo;
	struct fb_info *psLINFBInfo = psDevInfo->psLINFBInfo;
	unsigned long FBSize;

	FBSize = (psLINFBInfo->screen_size) != 0 ?
	    psLINFBInfo->screen_size : psLINFBInfo->fix.smem_len;
	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
		      ": Framebuffer physical address: 0x%lx\n",
		      psLINFBInfo->fix.smem_start));
	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
		      ": Framebuffer virtual address: 0x%lx\n",
		      (unsigned long)psLINFBInfo->screen_base));
	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
		      ": Framebuffer size: %lu\n", FBSize));
	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
		      ": Framebuffer virtual width: %u\n",
		      psLINFBInfo->var.xres_virtual));
	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
		      ": Framebuffer virtual height: %u\n",
		      psLINFBInfo->var.yres_virtual));
	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
		      ": Framebuffer width: %u\n", psLINFBInfo->var.xres));
	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
		      ": Framebuffer height: %u\n", psLINFBInfo->var.yres));
	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
		      ": Framebuffer stride: %u\n",
		      psLINFBInfo->fix.line_length));

	psPVRFBInfo->sSysAddr.uiAddr = psLINFBInfo->fix.smem_start;
	psPVRFBInfo->sCPUVAddr = psLINFBInfo->screen_base;

	psPVRFBInfo->ui32Width = psLINFBInfo->var.xres;
	psPVRFBInfo->ui32Height = psLINFBInfo->var.yres;
	psPVRFBInfo->ui32ByteStride = psLINFBInfo->fix.line_length;
	psPVRFBInfo->ui32FBSize = FBSize;
	psPVRFBInfo->ui32BufferSize =
	    psPVRFBInfo->ui32Height * psPVRFBInfo->ui32ByteStride;

	psPVRFBInfo->ui32RoundedBufferSize =
	    OMAPLFB_PAGE_ROUNDUP(psPVRFBInfo->ui32BufferSize);

	if (psLINFBInfo->var.bits_per_pixel == 16) {
		if ((psLINFBInfo->var.red.length == 5) &&
		    (psLINFBInfo->var.green.length == 6) &&
		    (psLINFBInfo->var.blue.length == 5) &&
		    (psLINFBInfo->var.red.offset == 11) &&
		    (psLINFBInfo->var.green.offset == 5) &&
		    (psLINFBInfo->var.blue.offset == 0) &&
		    (psLINFBInfo->var.red.msb_right == 0)) {
			psPVRFBInfo->ePixelFormat = PVRSRV_PIXEL_FORMAT_RGB565;
		} else {
			printk("Unknown FB format\n");
		}
	} else if (psLINFBInfo->var.bits_per_pixel == 32) {
		if ((psLINFBInfo->var.red.length == 8) &&
		    (psLINFBInfo->var.green.length == 8) &&
		    (psLINFBInfo->var.blue.length == 8) &&
		    (psLINFBInfo->var.red.offset == 16) &&
		    (psLINFBInfo->var.green.offset == 8) &&
		    (psLINFBInfo->var.blue.offset == 0) &&
		    (psLINFBInfo->var.red.msb_right == 0)) {
			psPVRFBInfo->ePixelFormat =
			    PVRSRV_PIXEL_FORMAT_ARGB8888;
		} else {
			printk("Unknown FB format\n");
		}
	} else {
		printk("Unknown FB format\n");
	}
	psDevInfo->sDisplayFormat.pixelformat = psDevInfo->sFBInfo.ePixelFormat;
	psDevInfo->sDisplayDim.ui32Width = psDevInfo->sFBInfo.ui32Width;
	psDevInfo->sDisplayDim.ui32Height = psDevInfo->sFBInfo.ui32Height;
	psDevInfo->sDisplayDim.ui32ByteStride =
	    psDevInfo->sFBInfo.ui32ByteStride;
	psDevInfo->sSystemBuffer.sSysAddr = psDevInfo->sFBInfo.sSysAddr;
	psDevInfo->sSystemBuffer.sCPUVAddr = psDevInfo->sFBInfo.sCPUVAddr;
	psDevInfo->sSystemBuffer.ui32BufferSize =
	    psDevInfo->sFBInfo.ui32RoundedBufferSize;
}

static struct FB_EVENTS {
	struct notifier_block notif;
	OMAPLFB_DEVINFO *psDevInfo;
} gFBEventsData;

static int FBEvents(struct notifier_block *psNotif,
		    unsigned long event, void *data)
{
	if (event == FB_EVENT_MODE_CHANGE) {
		struct FB_EVENTS *psEvents =
		    container_of(psNotif, struct FB_EVENTS, notif);
		SetDevinfo(psEvents->psDevInfo);
	}
	return 0;
}

static PVRSRV_ERROR InitDev(OMAPLFB_DEVINFO * psDevInfo)
{
	struct fb_info *psLINFBInfo;
	struct module *psLINFBOwner;
	OMAPLFB_FBINFO *psPVRFBInfo = &psDevInfo->sFBInfo;
	PVRSRV_ERROR eError = PVRSRV_ERROR_GENERIC;

	acquire_console_sem();

	if (fb_idx < 0 || fb_idx >= num_registered_fb) {
		eError = PVRSRV_ERROR_INVALID_DEVICE;
		goto errRelSem;
	}

	psLINFBInfo = registered_fb[fb_idx];

	psLINFBOwner = psLINFBInfo->fbops->owner;
	if (!try_module_get(psLINFBOwner)) {
		printk(KERN_INFO DRIVER_PREFIX
		       ": Couldn't get framebuffer module\n");

		goto errRelSem;
	}

	if (psLINFBInfo->fbops->fb_open != NULL) {
		int res;

		res = psLINFBInfo->fbops->fb_open(psLINFBInfo, 0);
		if (res != 0) {
			printk(KERN_INFO DRIVER_PREFIX
			       ": Couldn't open framebuffer: %d\n", res);

			goto errModPut;
		}
	}

	psDevInfo->psLINFBInfo = psLINFBInfo;

	SetDevinfo(psDevInfo);

	gFBEventsData.notif.notifier_call = FBEvents;
	gFBEventsData.psDevInfo = psDevInfo;
	fb_register_client(&gFBEventsData.notif);

	psDevInfo->sFBInfo.sSysAddr.uiAddr = psPVRFBInfo->sSysAddr.uiAddr;
	psDevInfo->sFBInfo.sCPUVAddr = psPVRFBInfo->sCPUVAddr;

	eError = PVRSRV_OK;
	goto errRelSem;

errModPut:
	module_put(psLINFBOwner);
errRelSem:
	release_console_sem();
	return eError;
}

static IMG_VOID DeInitDev(OMAPLFB_DEVINFO * psDevInfo)
{
	struct fb_info *psLINFBInfo = psDevInfo->psLINFBInfo;
	struct module *psLINFBOwner;

	acquire_console_sem();

	psLINFBOwner = psLINFBInfo->fbops->owner;

	if (psLINFBInfo->fbops->fb_release != NULL) {
		(void)psLINFBInfo->fbops->fb_release(psLINFBInfo, 0);
	}

	module_put(psLINFBOwner);

	release_console_sem();
}

PVRSRV_ERROR OMAPLFBInit(IMG_VOID)
{
	OMAPLFB_DEVINFO *psDevInfo;

	psDevInfo = GetAnchorPtr();

	if (psDevInfo == IMG_NULL) {
		PFN_CMD_PROC pfnCmdProcList[OMAPLFB_COMMAND_COUNT];
		IMG_UINT32 aui32SyncCountList[OMAPLFB_COMMAND_COUNT][2];

		psDevInfo =
		    (OMAPLFB_DEVINFO *)
		    OMAPLFBAllocKernelMem(sizeof(OMAPLFB_DEVINFO));

		if (!psDevInfo) {
			return PVRSRV_ERROR_OUT_OF_MEMORY;
		}

		memset(psDevInfo, 0, sizeof(OMAPLFB_DEVINFO));

		SetAnchorPtr((IMG_VOID *) psDevInfo);

		psDevInfo->ui32RefCount = 0;

		if (InitDev(psDevInfo) != PVRSRV_OK) {
			return PVRSRV_ERROR_INIT_FAILURE;
		}

		if (OMAPLFBGetLibFuncAddr
		    ("PVRGetDisplayClassJTable",
		     &pfnGetPVRJTable) != PVRSRV_OK) {
			return PVRSRV_ERROR_INIT_FAILURE;
		}

		if (!(*pfnGetPVRJTable) (&psDevInfo->sPVRJTable)) {
			return PVRSRV_ERROR_INIT_FAILURE;
		}

		spin_lock_init(&psDevInfo->SwapChainLock);

		psDevInfo->psSwapChain = IMG_NULL;
		psDevInfo->bFlushCommands = IMG_FALSE;
		psDevInfo->bDeviceSuspended = IMG_FALSE;

		psDevInfo->sDisplayInfo.ui32MaxSwapChainBuffers =
		    psDevInfo->sFBInfo.ui32FBSize /
		    psDevInfo->sFBInfo.ui32RoundedBufferSize;
		if (psDevInfo->sDisplayInfo.ui32MaxSwapChainBuffers == 0) {
			psDevInfo->sDisplayInfo.ui32MaxSwapChains = 0;
			psDevInfo->sDisplayInfo.ui32MaxSwapInterval = 0;
		} else {
			psDevInfo->sDisplayInfo.ui32MaxSwapChains = 1;
			psDevInfo->sDisplayInfo.ui32MaxSwapInterval = 3;
		}
		psDevInfo->sDisplayInfo.ui32MinSwapInterval = 0;

		strncpy(psDevInfo->sDisplayInfo.szDisplayName,
			DISPLAY_DEVICE_NAME, MAX_DISPLAY_NAME_SIZE);

		DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
			      ": Maximum number of swap chain buffers: %lu\n",
			      psDevInfo->sDisplayInfo.ui32MaxSwapChainBuffers));

		psDevInfo->sDCJTable.ui32TableSize =
		    sizeof(PVRSRV_DC_SRV2DISP_KMJTABLE);
		psDevInfo->sDCJTable.pfnOpenDCDevice = OpenDCDevice;
		psDevInfo->sDCJTable.pfnCloseDCDevice = CloseDCDevice;
		psDevInfo->sDCJTable.pfnEnumDCFormats = EnumDCFormats;
		psDevInfo->sDCJTable.pfnEnumDCDims = EnumDCDims;
		psDevInfo->sDCJTable.pfnGetDCSystemBuffer = GetDCSystemBuffer;
		psDevInfo->sDCJTable.pfnGetDCInfo = GetDCInfo;
		psDevInfo->sDCJTable.pfnGetBufferAddr = GetDCBufferAddr;
		psDevInfo->sDCJTable.pfnCreateDCSwapChain = CreateDCSwapChain;
		psDevInfo->sDCJTable.pfnDestroyDCSwapChain = DestroyDCSwapChain;
		psDevInfo->sDCJTable.pfnSetDCDstRect = SetDCDstRect;
		psDevInfo->sDCJTable.pfnSetDCSrcRect = SetDCSrcRect;
		psDevInfo->sDCJTable.pfnSetDCDstColourKey = SetDCDstColourKey;
		psDevInfo->sDCJTable.pfnSetDCSrcColourKey = SetDCSrcColourKey;
		psDevInfo->sDCJTable.pfnGetDCBuffers = GetDCBuffers;
		psDevInfo->sDCJTable.pfnSwapToDCBuffer = SwapToDCBuffer;
		psDevInfo->sDCJTable.pfnSwapToDCSystem = SwapToDCSystem;
		psDevInfo->sDCJTable.pfnSetDCState = SetDCState;

		if (psDevInfo->sPVRJTable.
		    pfnPVRSRVRegisterDCDevice(&psDevInfo->sDCJTable,
					      &psDevInfo->ui32DeviceID) !=
		    PVRSRV_OK) {
			return PVRSRV_ERROR_DEVICE_REGISTER_FAILED;
		}

		pfnCmdProcList[DC_FLIP_COMMAND] = ProcessFlip;

		aui32SyncCountList[DC_FLIP_COMMAND][0] = 0;
		aui32SyncCountList[DC_FLIP_COMMAND][1] = 2;

		if (psDevInfo->sPVRJTable.
		    pfnPVRSRVRegisterCmdProcList(psDevInfo->ui32DeviceID,
						 &pfnCmdProcList[0],
						 aui32SyncCountList,
						 OMAPLFB_COMMAND_COUNT) !=
		    PVRSRV_OK) {
			printk(KERN_WARNING DRIVER_PREFIX
			       ": Can't register callback\n");
			return PVRSRV_ERROR_CANT_REGISTER_CALLBACK;
		}

	}

	psDevInfo->ui32RefCount++;

	return PVRSRV_OK;

}

PVRSRV_ERROR OMAPLFBDeinit(IMG_VOID)
{
	OMAPLFB_DEVINFO *psDevInfo, *psDevFirst;

	psDevFirst = GetAnchorPtr();
	psDevInfo = psDevFirst;

	if (psDevInfo == IMG_NULL) {
		return PVRSRV_ERROR_GENERIC;
	}

	psDevInfo->ui32RefCount--;

	if (psDevInfo->ui32RefCount == 0) {

		PVRSRV_DC_DISP2SRV_KMJTABLE *psJTable = &psDevInfo->sPVRJTable;

		if (psDevInfo->sPVRJTable.
		    pfnPVRSRVRemoveCmdProcList(psDevInfo->ui32DeviceID,
					       OMAPLFB_COMMAND_COUNT) !=
		    PVRSRV_OK) {
			return PVRSRV_ERROR_GENERIC;
		}

		if (psJTable->
		    pfnPVRSRVRemoveDCDevice(psDevInfo->ui32DeviceID) !=
		    PVRSRV_OK) {
			return PVRSRV_ERROR_GENERIC;
		}

		DeInitDev(psDevInfo);

		OMAPLFBFreeKernelMem(psDevInfo);
	}

	SetAnchorPtr(IMG_NULL);

	return PVRSRV_OK;
}

IMG_VOID OMAPLFBDriverSuspend(IMG_VOID)
{
	OMAPLFB_DEVINFO *psDevInfo = GetAnchorPtr();
	unsigned long ulLockFlags;

	spin_lock_irqsave(&psDevInfo->SwapChainLock, ulLockFlags);

	if (psDevInfo->bDeviceSuspended) {
		goto ExitUnlock;
	}
	psDevInfo->bDeviceSuspended = IMG_TRUE;

	SetFlushStateInternalNoLock(psDevInfo, IMG_TRUE);

	spin_unlock_irqrestore(&psDevInfo->SwapChainLock, ulLockFlags);

	if (psDevInfo->psSwapChain != IMG_NULL) {
		OMAPLFBDisableDisplayRegisterAccess();
	}

	return;

ExitUnlock:
	spin_unlock_irqrestore(&psDevInfo->SwapChainLock, ulLockFlags);
}

IMG_VOID OMAPLFBDriverResume(IMG_VOID)
{
	OMAPLFB_DEVINFO *psDevInfo = GetAnchorPtr();
	unsigned long ulLockFlags;

	if (!psDevInfo->bDeviceSuspended) {
		return;
	}

	if (psDevInfo->psSwapChain != IMG_NULL) {
		OMAPLFBEnableDisplayRegisterAccess();
	}

	spin_lock_irqsave(&psDevInfo->SwapChainLock, ulLockFlags);

	SetFlushStateInternalNoLock(psDevInfo, IMG_FALSE);

	psDevInfo->bDeviceSuspended = IMG_FALSE;

	spin_unlock_irqrestore(&psDevInfo->SwapChainLock, ulLockFlags);
}
