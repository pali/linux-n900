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

static void *gpvAnchor;

static int fb_idx;

#define OMAPLFB_COMMAND_COUNT		1

static IMG_BOOL (*pfnGetPVRJTable)(struct PVRSRV_DC_DISP2SRV_KMJTABLE *);

static struct OMAPLFB_DEVINFO *GetAnchorPtr(void)
{
	return (struct OMAPLFB_DEVINFO *)gpvAnchor;
}

static void SetAnchorPtr(struct OMAPLFB_DEVINFO *psDevInfo)
{
	gpvAnchor = (void *) psDevInfo;
}

static void FlushInternalVSyncQueue(struct OMAPLFB_SWAPCHAIN *psSwapChain)
{
	struct OMAPLFB_VSYNC_FLIP_ITEM *psFlipItem;
	u32 ui32MaxIndex;
	u32 i;

	psFlipItem = &psSwapChain->psVSyncFlips[psSwapChain->ui32RemoveIndex];
	ui32MaxIndex = psSwapChain->ui32BufferCount - 1;

	for (i = 0; i < psSwapChain->ui32BufferCount; i++) {
		if (!psFlipItem->bValid)
			continue;

		DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
			      ": %s: Flushing swap buffer (index %u)\n",
			      __func__, psSwapChain->ui32RemoveIndex));

		if (psFlipItem->bFlipped == IMG_FALSE)

			OMAPLFBFlip(psSwapChain,
				    (u32) psFlipItem->sSysAddr);

		if (psFlipItem->bCmdCompleted == IMG_FALSE) {
			DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
		  ": %s: Calling command complete for swap buffer (index %u)\n",
			  __func__, psSwapChain->ui32RemoveIndex));

			psSwapChain->psPVRJTable->
			    pfnPVRSRVCmdComplete(psFlipItem->hCmdComplete,
						 IMG_TRUE);
		}

		psSwapChain->ui32RemoveIndex++;

		if (psSwapChain->ui32RemoveIndex > ui32MaxIndex)
			psSwapChain->ui32RemoveIndex = 0;

		psFlipItem->bFlipped = IMG_FALSE;
		psFlipItem->bCmdCompleted = IMG_FALSE;
		psFlipItem->bValid = IMG_FALSE;

		psFlipItem =
		    &psSwapChain->psVSyncFlips[psSwapChain->ui32RemoveIndex];
	}

	psSwapChain->ui32InsertIndex = 0;
	psSwapChain->ui32RemoveIndex = 0;
}

static void SetFlushStateInternalNoLock(struct OMAPLFB_DEVINFO *psDevInfo,
					    IMG_BOOL bFlushState)
{
	struct OMAPLFB_SWAPCHAIN *psSwapChain = psDevInfo->psSwapChain;

	if (psSwapChain == NULL)
		return;

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

static void SetFlushStateInternal(struct OMAPLFB_DEVINFO *psDevInfo,
				      IMG_BOOL bFlushState)
{
	unsigned long ulLockFlags;

	spin_lock_irqsave(&psDevInfo->SwapChainLock, ulLockFlags);
	SetFlushStateInternalNoLock(psDevInfo, bFlushState);
	spin_unlock_irqrestore(&psDevInfo->SwapChainLock, ulLockFlags);
}

static void SetFlushStateExternal(struct OMAPLFB_DEVINFO *psDevInfo,
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

static void SetDCState(void *hDevice, u32 ui32State)
{
	struct OMAPLFB_DEVINFO *psDevInfo = (struct OMAPLFB_DEVINFO *)hDevice;

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
	struct OMAPLFB_DEVINFO *psDevInfo;
	struct OMAPLFB_SWAPCHAIN *psSwapChain;
	struct fb_event *psFBEvent = (struct fb_event *)data;
	IMG_BOOL bBlanked;;

	if (event != FB_EVENT_BLANK)
		return 0;

	psDevInfo = GetAnchorPtr();
	psSwapChain = psDevInfo->psSwapChain;

	bBlanked = (*(int *)psFBEvent->data != 0);

	if (bBlanked != psSwapChain->bBlanked) {
		psSwapChain->bBlanked = bBlanked;

		if (bBlanked)

			SetFlushStateInternal(psDevInfo, IMG_TRUE);
		else

			SetFlushStateInternal(psDevInfo, IMG_FALSE);
	}

	return 0;
}

static enum PVRSRV_ERROR UnblankDisplay(struct OMAPLFB_DEVINFO *psDevInfo)
{
	int res;

	console_lock();
	res = fb_blank(psDevInfo->psLINFBInfo, 0);
	console_unlock();
	if (res != 0) {
		printk(KERN_WARNING DRIVER_PREFIX
		       ": fb_blank failed (%d)", res);
		return PVRSRV_ERROR_GENERIC;
	}

	return PVRSRV_OK;
}

static enum PVRSRV_ERROR EnableLFBEventNotification(struct OMAPLFB_DEVINFO
								   *psDevInfo)
{
	int res;
	struct OMAPLFB_SWAPCHAIN *psSwapChain = psDevInfo->psSwapChain;
	enum PVRSRV_ERROR eError;

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

static enum PVRSRV_ERROR DisableLFBEventNotification(struct OMAPLFB_DEVINFO
								   *psDevInfo)
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

static enum PVRSRV_ERROR OpenDCDevice(u32 ui32DeviceID, void **phDevice,
				struct PVRSRV_SYNC_DATA *psSystemBufferSyncData)
{
	struct OMAPLFB_DEVINFO *psDevInfo;
	enum PVRSRV_ERROR eError;

	PVR_UNREFERENCED_PARAMETER(ui32DeviceID);

	psDevInfo = GetAnchorPtr();

	psDevInfo->sSystemBuffer.psSyncData = psSystemBufferSyncData;

	eError = UnblankDisplay(psDevInfo);
	if (eError != PVRSRV_OK) {
		DEBUG_PRINTK((KERN_WARNING DRIVER_PREFIX
			      ": UnblankDisplay failed (%d)", eError));
		return eError;
	}

	*phDevice = (void *) psDevInfo;

	return PVRSRV_OK;
}

static enum PVRSRV_ERROR CloseDCDevice(void *hDevice)
{
	PVR_UNREFERENCED_PARAMETER(hDevice);

	return PVRSRV_OK;
}

static enum PVRSRV_ERROR EnumDCFormats(void *hDevice, u32 *pui32NumFormats,
				  struct DISPLAY_FORMAT *psFormat)
{
	struct OMAPLFB_DEVINFO *psDevInfo;

	if (!hDevice || !pui32NumFormats)
		return PVRSRV_ERROR_INVALID_PARAMS;

	psDevInfo = (struct OMAPLFB_DEVINFO *)hDevice;

	*pui32NumFormats = 1;

	if (psFormat)
		psFormat[0] = psDevInfo->sDisplayFormat;

	return PVRSRV_OK;
}

static enum PVRSRV_ERROR EnumDCDims(void *hDevice,
			       struct DISPLAY_FORMAT *psFormat,
			       u32 *pui32NumDims, struct DISPLAY_DIMS *psDim)
{
	struct OMAPLFB_DEVINFO *psDevInfo;

	if (!hDevice || !psFormat || !pui32NumDims)
		return PVRSRV_ERROR_INVALID_PARAMS;

	psDevInfo = (struct OMAPLFB_DEVINFO *)hDevice;

	*pui32NumDims = 1;

	if (psDim)
		psDim[0] = psDevInfo->sDisplayDim;

	return PVRSRV_OK;
}

static enum PVRSRV_ERROR GetDCSystemBuffer(void *hDevice, void **phBuffer)
{
	struct OMAPLFB_DEVINFO *psDevInfo;

	if (!hDevice || !phBuffer)
		return PVRSRV_ERROR_INVALID_PARAMS;

	psDevInfo = (struct OMAPLFB_DEVINFO *)hDevice;

	*phBuffer = (void *) &psDevInfo->sSystemBuffer;

	return PVRSRV_OK;
}

static enum PVRSRV_ERROR GetDCInfo(void *hDevice, struct DISPLAY_INFO *psDCInfo)
{
	struct OMAPLFB_DEVINFO *psDevInfo;

	if (!hDevice || !psDCInfo)
		return PVRSRV_ERROR_INVALID_PARAMS;

	psDevInfo = (struct OMAPLFB_DEVINFO *)hDevice;

	*psDCInfo = psDevInfo->sDisplayInfo;

	return PVRSRV_OK;
}

static enum PVRSRV_ERROR GetDCBufferAddr(void *hDevice, void *hBuffer,
				    struct IMG_SYS_PHYADDR **ppsSysAddr,
				    u32 *pui32ByteSize,
				    void __iomem **ppvCpuVAddr,
				    void **phOSMapInfo,
				    IMG_BOOL *pbIsContiguous)
{
	struct OMAPLFB_DEVINFO *psDevInfo;
	struct OMAPLFB_BUFFER *psSystemBuffer;

	if (!hDevice)
		return PVRSRV_ERROR_INVALID_PARAMS;
	psDevInfo = (struct OMAPLFB_DEVINFO *)hDevice;

	if (!hBuffer)
		return PVRSRV_ERROR_INVALID_PARAMS;
	psSystemBuffer = (struct OMAPLFB_BUFFER *)hBuffer;

	if (!ppsSysAddr)
		return PVRSRV_ERROR_INVALID_PARAMS;

	*ppsSysAddr = &psSystemBuffer->sSysAddr;

	if (!pui32ByteSize)
		return PVRSRV_ERROR_INVALID_PARAMS;

	*pui32ByteSize = psDevInfo->sFBInfo.ui32BufferSize;

	if (ppvCpuVAddr)
		*ppvCpuVAddr = psSystemBuffer->sCPUVAddr;

	if (phOSMapInfo)
		*phOSMapInfo = (void *) 0;

	if (pbIsContiguous)
		*pbIsContiguous = IMG_TRUE;

	return PVRSRV_OK;
}

static enum PVRSRV_ERROR CreateDCSwapChain(void *hDevice, u32 ui32Flags,
			      struct DISPLAY_SURF_ATTRIBUTES *psDstSurfAttrib,
			      struct DISPLAY_SURF_ATTRIBUTES *psSrcSurfAttrib,
			      u32 ui32BufferCount,
			      struct PVRSRV_SYNC_DATA **ppsSyncData,
			      u32 ui32OEMFlags, void **phSwapChain,
			      u32 *pui32SwapChainID)
{
	struct OMAPLFB_DEVINFO *psDevInfo;
	struct OMAPLFB_SWAPCHAIN *psSwapChain;
	struct OMAPLFB_BUFFER *psBuffer;
	struct OMAPLFB_VSYNC_FLIP_ITEM *psVSyncFlips;
	u32 i;
	enum PVRSRV_ERROR eError = PVRSRV_ERROR_GENERIC;
	unsigned long ulLockFlags;
	u32 ui32BuffersToSkip;

	PVR_UNREFERENCED_PARAMETER(ui32OEMFlags);
	PVR_UNREFERENCED_PARAMETER(pui32SwapChainID);

	if (!hDevice
	    || !psDstSurfAttrib
	    || !psSrcSurfAttrib || !ppsSyncData || !phSwapChain)
		return PVRSRV_ERROR_INVALID_PARAMS;

	psDevInfo = (struct OMAPLFB_DEVINFO *)hDevice;

	if (psDevInfo->sDisplayInfo.ui32MaxSwapChains == 0)
		return PVRSRV_ERROR_NOT_SUPPORTED;

	if (psDevInfo->psSwapChain != NULL)
		return PVRSRV_ERROR_FLIP_CHAIN_EXISTS;

	if (ui32BufferCount > psDevInfo->sDisplayInfo.ui32MaxSwapChainBuffers)
		return PVRSRV_ERROR_TOOMANYBUFFERS;

	if ((psDevInfo->sFBInfo.ui32RoundedBufferSize * ui32BufferCount) >
	    psDevInfo->sFBInfo.ui32FBSize)
		return PVRSRV_ERROR_TOOMANYBUFFERS;

	ui32BuffersToSkip =
	    psDevInfo->sDisplayInfo.ui32MaxSwapChainBuffers - ui32BufferCount;

	if (psDstSurfAttrib->pixelformat !=
	    psDevInfo->sDisplayFormat.pixelformat
	    || psDstSurfAttrib->sDims.ui32ByteStride !=
	    psDevInfo->sDisplayDim.ui32ByteStride
	    || psDstSurfAttrib->sDims.ui32Width !=
	    psDevInfo->sDisplayDim.ui32Width
	    || psDstSurfAttrib->sDims.ui32Height !=
	    psDevInfo->sDisplayDim.ui32Height)

		return PVRSRV_ERROR_INVALID_PARAMS;

	if (psDstSurfAttrib->pixelformat != psSrcSurfAttrib->pixelformat
	    || psDstSurfAttrib->sDims.ui32ByteStride !=
	    psSrcSurfAttrib->sDims.ui32ByteStride
	    || psDstSurfAttrib->sDims.ui32Width !=
	    psSrcSurfAttrib->sDims.ui32Width
	    || psDstSurfAttrib->sDims.ui32Height !=
	    psSrcSurfAttrib->sDims.ui32Height)

		return PVRSRV_ERROR_INVALID_PARAMS;

	PVR_UNREFERENCED_PARAMETER(ui32Flags);

	psSwapChain = (struct OMAPLFB_SWAPCHAIN *)
			OMAPLFBAllocKernelMem(sizeof(struct OMAPLFB_SWAPCHAIN));
	if (!psSwapChain)
		return PVRSRV_ERROR_OUT_OF_MEMORY;

	psBuffer = (struct OMAPLFB_BUFFER *)
	     OMAPLFBAllocKernelMem(sizeof(struct OMAPLFB_BUFFER) *
						     ui32BufferCount);
	if (!psBuffer) {
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto ErrorFreeSwapChain;
	}

	psVSyncFlips = (struct OMAPLFB_VSYNC_FLIP_ITEM *)
		OMAPLFBAllocKernelMem(sizeof(struct OMAPLFB_VSYNC_FLIP_ITEM) *
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

	for (i = 0; i < ui32BufferCount - 1; i++)
		psBuffer[i].psNext = &psBuffer[i + 1];

	psBuffer[i].psNext = &psBuffer[0];

	for (i = 0; i < ui32BufferCount; i++) {
		u32 ui32SwapBuffer = i + ui32BuffersToSkip;
		u32 ui32BufferOffset =
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
	if (psSwapChain->pvRegs == NULL) {
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

	*phSwapChain = (void *) psSwapChain;

	return PVRSRV_OK;

ErrorUninstallVSyncInterrupt:
	if (OMAPLFBUninstallVSyncISR(psSwapChain) != PVRSRV_OK)
		printk(KERN_WARNING DRIVER_PREFIX
		       ": Couldn't uninstall VSync ISR\n");
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

static enum PVRSRV_ERROR DestroyDCSwapChain(void *hDevice, void *hSwapChain)
{
	struct OMAPLFB_DEVINFO *psDevInfo;
	struct OMAPLFB_SWAPCHAIN *psSwapChain;
	unsigned long ulLockFlags;
	enum PVRSRV_ERROR eError;

	if (!hDevice || !hSwapChain)
		return PVRSRV_ERROR_INVALID_PARAMS;

	psDevInfo = (struct OMAPLFB_DEVINFO *)hDevice;
	psSwapChain = (struct OMAPLFB_SWAPCHAIN *)hSwapChain;
	if (psSwapChain != psDevInfo->psSwapChain)
		return PVRSRV_ERROR_INVALID_PARAMS;

	eError = DisableLFBEventNotification(psDevInfo);
	if (eError != PVRSRV_OK)
		printk(KERN_WARNING DRIVER_PREFIX
		       ": Couldn't disable framebuffer event notification\n");

	spin_lock_irqsave(&psDevInfo->SwapChainLock, ulLockFlags);

	OMAPLFBDisableVSyncInterrupt(psSwapChain);

	FlushInternalVSyncQueue(psSwapChain);

	OMAPLFBFlip(psSwapChain, psDevInfo->sFBInfo.sSysAddr.uiAddr);

	psDevInfo->psSwapChain = NULL;

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

static enum PVRSRV_ERROR SetDCDstRect(void *hDevice,
				 void *hSwapChain, struct IMG_RECT *psRect)
{
	PVR_UNREFERENCED_PARAMETER(hDevice);
	PVR_UNREFERENCED_PARAMETER(hSwapChain);
	PVR_UNREFERENCED_PARAMETER(psRect);

	return PVRSRV_ERROR_NOT_SUPPORTED;
}

static enum PVRSRV_ERROR SetDCSrcRect(void *hDevice,
				 void *hSwapChain, struct IMG_RECT *psRect)
{
	PVR_UNREFERENCED_PARAMETER(hDevice);
	PVR_UNREFERENCED_PARAMETER(hSwapChain);
	PVR_UNREFERENCED_PARAMETER(psRect);

	return PVRSRV_ERROR_NOT_SUPPORTED;
}

static enum PVRSRV_ERROR SetDCDstColourKey(void *hDevice, void *hSwapChain,
				      u32 ui32CKColour)
{
	PVR_UNREFERENCED_PARAMETER(hDevice);
	PVR_UNREFERENCED_PARAMETER(hSwapChain);
	PVR_UNREFERENCED_PARAMETER(ui32CKColour);

	return PVRSRV_ERROR_NOT_SUPPORTED;
}

static enum PVRSRV_ERROR SetDCSrcColourKey(void *hDevice, void *hSwapChain,
				      u32 ui32CKColour)
{
	PVR_UNREFERENCED_PARAMETER(hDevice);
	PVR_UNREFERENCED_PARAMETER(hSwapChain);
	PVR_UNREFERENCED_PARAMETER(ui32CKColour);

	return PVRSRV_ERROR_NOT_SUPPORTED;
}

static enum PVRSRV_ERROR GetDCBuffers(void *hDevice, void *hSwapChain,
				 u32 *pui32BufferCount, void **phBuffer)
{
	struct OMAPLFB_DEVINFO *psDevInfo;
	struct OMAPLFB_SWAPCHAIN *psSwapChain;
	u32 i;

	if (!hDevice || !hSwapChain || !pui32BufferCount || !phBuffer)
		return PVRSRV_ERROR_INVALID_PARAMS;

	psDevInfo = (struct OMAPLFB_DEVINFO *)hDevice;
	psSwapChain = (struct OMAPLFB_SWAPCHAIN *)hSwapChain;
	if (psSwapChain != psDevInfo->psSwapChain)
		return PVRSRV_ERROR_INVALID_PARAMS;

	*pui32BufferCount = psSwapChain->ui32BufferCount;

	for (i = 0; i < psSwapChain->ui32BufferCount; i++)
		phBuffer[i] = (void *) &psSwapChain->psBuffer[i];

	return PVRSRV_OK;
}

static enum PVRSRV_ERROR SwapToDCBuffer(void *hDevice, void *hBuffer,
				   u32 ui32SwapInterval, void *hPrivateTag,
				   u32 ui32ClipRectCount,
				   struct IMG_RECT *psClipRect)
{
	struct OMAPLFB_DEVINFO *psDevInfo;

	PVR_UNREFERENCED_PARAMETER(ui32SwapInterval);
	PVR_UNREFERENCED_PARAMETER(hPrivateTag);
	PVR_UNREFERENCED_PARAMETER(psClipRect);

	if (!hDevice || !hBuffer || (ui32ClipRectCount != 0))
		return PVRSRV_ERROR_INVALID_PARAMS;

	psDevInfo = (struct OMAPLFB_DEVINFO *)hDevice;

	PVR_UNREFERENCED_PARAMETER(hBuffer);

	return PVRSRV_OK;
}

static enum PVRSRV_ERROR SwapToDCSystem(void *hDevice, void *hSwapChain)
{
	struct OMAPLFB_DEVINFO *psDevInfo;
	struct OMAPLFB_SWAPCHAIN *psSwapChain;
	unsigned long ulLockFlags;

	if (!hDevice || !hSwapChain)
		return PVRSRV_ERROR_INVALID_PARAMS;

	psDevInfo = (struct OMAPLFB_DEVINFO *)hDevice;
	psSwapChain = (struct OMAPLFB_SWAPCHAIN *)hSwapChain;
	if (psSwapChain != psDevInfo->psSwapChain)
		return PVRSRV_ERROR_INVALID_PARAMS;

	spin_lock_irqsave(&psDevInfo->SwapChainLock, ulLockFlags);

	FlushInternalVSyncQueue(psSwapChain);

	OMAPLFBFlip(psSwapChain, psDevInfo->sFBInfo.sSysAddr.uiAddr);

	spin_unlock_irqrestore(&psDevInfo->SwapChainLock, ulLockFlags);

	return PVRSRV_OK;
}

IMG_BOOL OMAPLFBVSyncIHandler(struct OMAPLFB_SWAPCHAIN *psSwapChain)
{
	IMG_BOOL bStatus = IMG_FALSE;
	struct OMAPLFB_VSYNC_FLIP_ITEM *psFlipItem;
	u32 ui32MaxIndex;
	unsigned long ulLockFlags;

	psFlipItem = &psSwapChain->psVSyncFlips[psSwapChain->ui32RemoveIndex];
	ui32MaxIndex = psSwapChain->ui32BufferCount - 1;

	spin_lock_irqsave(psSwapChain->psSwapChainLock, ulLockFlags);

	if (psSwapChain->bFlushCommands)
		goto ExitUnlock;

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

				if (psSwapChain->ui32RemoveIndex >
								ui32MaxIndex)
					psSwapChain->ui32RemoveIndex = 0;

				psFlipItem->bCmdCompleted = IMG_FALSE;
				psFlipItem->bFlipped = IMG_FALSE;

				psFlipItem->bValid = IMG_FALSE;
			} else {

				break;
			}
		} else {

			OMAPLFBFlip(psSwapChain,
				    (u32) psFlipItem->sSysAddr);

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

static IMG_BOOL ProcessFlip(void *hCmdCookie, u32 ui32DataSize, void *pvData)
{
	struct DISPLAYCLASS_FLIP_COMMAND *psFlipCmd;
	struct OMAPLFB_DEVINFO *psDevInfo;
	struct OMAPLFB_BUFFER *psBuffer;
	struct OMAPLFB_SWAPCHAIN *psSwapChain;
	struct OMAPLFB_VSYNC_FLIP_ITEM *psFlipItem;
	unsigned long ulLockFlags;

	if (!hCmdCookie || !pvData)
		return IMG_FALSE;

	psFlipCmd = (struct DISPLAYCLASS_FLIP_COMMAND *)pvData;

	if (psFlipCmd == NULL
	    || sizeof(struct DISPLAYCLASS_FLIP_COMMAND) != ui32DataSize)
		return IMG_FALSE;

	psDevInfo = (struct OMAPLFB_DEVINFO *)psFlipCmd->hExtDevice;

	psBuffer = (struct OMAPLFB_BUFFER *)psFlipCmd->hExtBuffer;
	psSwapChain = (struct OMAPLFB_SWAPCHAIN *)psFlipCmd->hExtSwapChain;

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
		u32 ui32MaxIndex = psSwapChain->ui32BufferCount - 1;

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
		if (psSwapChain->ui32InsertIndex > ui32MaxIndex)
			psSwapChain->ui32InsertIndex = 0;

		goto ExitTrueUnlock;
	}

	spin_unlock_irqrestore(&psDevInfo->SwapChainLock, ulLockFlags);
	return IMG_FALSE;

ExitTrueUnlock:
	spin_unlock_irqrestore(&psDevInfo->SwapChainLock, ulLockFlags);
	return IMG_TRUE;
}

static void SetDevinfo(struct OMAPLFB_DEVINFO *psDevInfo)
{
	struct OMAPLFB_FBINFO *psPVRFBInfo = &psDevInfo->sFBInfo;
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

	if (psLINFBInfo->var.bits_per_pixel == 16)
		if ((psLINFBInfo->var.red.length == 5) &&
		    (psLINFBInfo->var.green.length == 6) &&
		    (psLINFBInfo->var.blue.length == 5) &&
		    (psLINFBInfo->var.red.offset == 11) &&
		    (psLINFBInfo->var.green.offset == 5) &&
		    (psLINFBInfo->var.blue.offset == 0) &&
		    (psLINFBInfo->var.red.msb_right == 0))
			psPVRFBInfo->ePixelFormat = PVRSRV_PIXEL_FORMAT_RGB565;
		else
			printk("Unknown FB format\n");
	else
	if (psLINFBInfo->var.bits_per_pixel == 32)
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
			printk(KERN_ERR "Unknown FB format\n");
		}
	else
		printk(KERN_ERR "Unknown FB format\n");

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
	struct OMAPLFB_DEVINFO *psDevInfo;
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

static enum PVRSRV_ERROR InitDev(struct OMAPLFB_DEVINFO *psDevInfo)
{
	struct fb_info *psLINFBInfo;
	struct module *psLINFBOwner;
	struct OMAPLFB_FBINFO *psPVRFBInfo = &psDevInfo->sFBInfo;
	enum PVRSRV_ERROR eError = PVRSRV_ERROR_GENERIC;

	console_lock();

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
	console_unlock();
	return eError;
}

static void DeInitDev(struct OMAPLFB_DEVINFO *psDevInfo)
{
	struct fb_info *psLINFBInfo = psDevInfo->psLINFBInfo;
	struct module *psLINFBOwner;

	console_lock();

	psLINFBOwner = psLINFBInfo->fbops->owner;

	if (psLINFBInfo->fbops->fb_release != NULL)
		(void)psLINFBInfo->fbops->fb_release(psLINFBInfo, 0);

	module_put(psLINFBOwner);

	console_unlock();
}

enum PVRSRV_ERROR OMAPLFBInit(void)
{
	struct OMAPLFB_DEVINFO *psDevInfo;

	psDevInfo = GetAnchorPtr();

	if (psDevInfo == NULL) {
		IMG_BOOL (*pfnCmdProcList[OMAPLFB_COMMAND_COUNT])
							(void *, u32, void *);
		u32 aui32SyncCountList[OMAPLFB_COMMAND_COUNT][2];

		psDevInfo = (struct OMAPLFB_DEVINFO *)
			  OMAPLFBAllocKernelMem(sizeof(struct OMAPLFB_DEVINFO));

		if (!psDevInfo)
			return PVRSRV_ERROR_OUT_OF_MEMORY;

		memset(psDevInfo, 0, sizeof(struct OMAPLFB_DEVINFO));

		SetAnchorPtr((void *) psDevInfo);

		psDevInfo->ui32RefCount = 0;

		if (InitDev(psDevInfo) != PVRSRV_OK)
			return PVRSRV_ERROR_INIT_FAILURE;

		if (OMAPLFBGetLibFuncAddr
		    ("PVRGetDisplayClassJTable",
		     &pfnGetPVRJTable) != PVRSRV_OK)
			return PVRSRV_ERROR_INIT_FAILURE;

		if (!(*pfnGetPVRJTable) (&psDevInfo->sPVRJTable))
			return PVRSRV_ERROR_INIT_FAILURE;

		spin_lock_init(&psDevInfo->SwapChainLock);

		psDevInfo->psSwapChain = NULL;
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
			      ": Maximum number of swap chain buffers: %u\n",
			      psDevInfo->sDisplayInfo.ui32MaxSwapChainBuffers));

		psDevInfo->sDCJTable.ui32TableSize =
		    sizeof(struct PVRSRV_DC_SRV2DISP_KMJTABLE);
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
		    PVRSRV_OK)
			return PVRSRV_ERROR_DEVICE_REGISTER_FAILED;

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

enum PVRSRV_ERROR OMAPLFBDeinit(void)
{
	struct OMAPLFB_DEVINFO *psDevInfo, *psDevFirst;

	psDevFirst = GetAnchorPtr();
	psDevInfo = psDevFirst;

	if (psDevInfo == NULL)
		return PVRSRV_ERROR_GENERIC;

	psDevInfo->ui32RefCount--;

	if (psDevInfo->ui32RefCount == 0) {
		struct PVRSRV_DC_DISP2SRV_KMJTABLE *psJTable =
							&psDevInfo->sPVRJTable;
		if (psDevInfo->sPVRJTable.
		    pfnPVRSRVRemoveCmdProcList(psDevInfo->ui32DeviceID,
					       OMAPLFB_COMMAND_COUNT) !=
		    PVRSRV_OK)
			return PVRSRV_ERROR_GENERIC;

		if (psJTable->
		    pfnPVRSRVRemoveDCDevice(psDevInfo->ui32DeviceID) !=
		    PVRSRV_OK)
			return PVRSRV_ERROR_GENERIC;

		DeInitDev(psDevInfo);

		OMAPLFBFreeKernelMem(psDevInfo);
	}

	SetAnchorPtr(NULL);

	return PVRSRV_OK;
}

void OMAPLFBDriverSuspend(void)
{
	struct OMAPLFB_DEVINFO *psDevInfo = GetAnchorPtr();
	unsigned long ulLockFlags;

	spin_lock_irqsave(&psDevInfo->SwapChainLock, ulLockFlags);

	if (psDevInfo->bDeviceSuspended)
		goto ExitUnlock;
	psDevInfo->bDeviceSuspended = IMG_TRUE;

	SetFlushStateInternalNoLock(psDevInfo, IMG_TRUE);

	spin_unlock_irqrestore(&psDevInfo->SwapChainLock, ulLockFlags);

	if (psDevInfo->psSwapChain != NULL)
		OMAPLFBDisableDisplayRegisterAccess();

	return;

ExitUnlock:
	spin_unlock_irqrestore(&psDevInfo->SwapChainLock, ulLockFlags);
}

void OMAPLFBDriverResume(void)
{
	struct OMAPLFB_DEVINFO *psDevInfo = GetAnchorPtr();
	unsigned long ulLockFlags;

	if (!psDevInfo->bDeviceSuspended)
		return;

	if (psDevInfo->psSwapChain != NULL)
		OMAPLFBEnableDisplayRegisterAccess();

	spin_lock_irqsave(&psDevInfo->SwapChainLock, ulLockFlags);

	SetFlushStateInternalNoLock(psDevInfo, IMG_FALSE);

	psDevInfo->bDeviceSuspended = IMG_FALSE;

	spin_unlock_irqrestore(&psDevInfo->SwapChainLock, ulLockFlags);
}
