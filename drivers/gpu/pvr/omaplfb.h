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

#ifndef __OMAPLFB_H__
#define __OMAPLFB_H__

extern IMG_BOOL PVRGetDisplayClassJTable(PVRSRV_DC_DISP2SRV_KMJTABLE *
					 psJTable);

#define OMAPLCD_IRQ			25

#define OMAPLCD_SYSCONFIG		0x0410
#define OMAPLCD_CONFIG			0x0444
#define OMAPLCD_DEFAULT_COLOR0		0x044C
#define OMAPLCD_TIMING_H		0x0464
#define OMAPLCD_TIMING_V		0x0468
#define OMAPLCD_POL_FREQ		0x046C
#define OMAPLCD_DIVISOR			0x0470
#define OMAPLCD_SIZE_DIG		0x0478
#define OMAPLCD_SIZE_LCD		0x047C
#define OMAPLCD_GFX_POSITION		0x0488
#define OMAPLCD_GFX_SIZE		0x048C
#define OMAPLCD_GFX_ATTRIBUTES		0x04a0
#define OMAPLCD_GFX_FIFO_THRESHOLD	0x04a4
#define OMAPLCD_GFX_WINDOW_SKIP		0x04b4

#define OMAPLCD_IRQSTATUS		0x0418
#define OMAPLCD_IRQENABLE		0x041c
#define OMAPLCD_CONTROL			0x0440
#define OMAPLCD_GFX_BA0			0x0480
#define OMAPLCD_GFX_BA1			0x0484
#define OMAPLCD_GFX_ROW_INC		0x04ac
#define OMAPLCD_GFX_PIX_INC		0x04b0
#define OMAPLCD_VID1_BA0		0x04bc
#define OMAPLCD_VID1_BA1		0x04c0
#define OMAPLCD_VID1_ROW_INC		0x04d8
#define OMAPLCD_VID1_PIX_INC		0x04dc

#define	OMAP_CONTROL_GODIGITAL		(1 << 6)
#define	OMAP_CONTROL_GOLCD		(1 << 5)
#define	OMAP_CONTROL_DIGITALENABLE	(1 << 1)
#define	OMAP_CONTROL_LCDENABLE		(1 << 0)

#define OMAPLCD_INTMASK_VSYNC		(1 << 1)
#define OMAPLCD_INTMASK_OFF		0

typedef struct OMAPLFB_BUFFER_TAG {
	IMG_SYS_PHYADDR sSysAddr;
	IMG_CPU_VIRTADDR sCPUVAddr;
	IMG_UINT32 ui32BufferSize;
	PVRSRV_SYNC_DATA *psSyncData;
	struct OMAPLFB_BUFFER_TAG *psNext;
} OMAPLFB_BUFFER;

typedef struct OMAPLFB_VSYNC_FLIP_ITEM_TAG {

	IMG_HANDLE hCmdComplete;

	IMG_SYS_PHYADDR *sSysAddr;

	IMG_UINT32 ui32SwapInterval;

	IMG_BOOL bValid;

	IMG_BOOL bFlipped;

	IMG_BOOL bCmdCompleted;

} OMAPLFB_VSYNC_FLIP_ITEM;

typedef struct PVRPDP_SWAPCHAIN_TAG {

	IMG_UINT32 ui32BufferCount;

	OMAPLFB_BUFFER *psBuffer;

	OMAPLFB_VSYNC_FLIP_ITEM *psVSyncFlips;

	IMG_UINT32 ui32InsertIndex;

	IMG_UINT32 ui32RemoveIndex;

	IMG_VOID *pvRegs;

	PVRSRV_DC_DISP2SRV_KMJTABLE *psPVRJTable;

	IMG_BOOL bFlushCommands;

	IMG_UINT32 ui32SetFlushStateRefCount;

	IMG_BOOL bBlanked;

	spinlock_t *psSwapChainLock;
} OMAPLFB_SWAPCHAIN;

typedef struct OMAPLFB_FBINFO_TAG {
	IMG_SYS_PHYADDR sSysAddr;
	IMG_CPU_VIRTADDR sCPUVAddr;
	IMG_UINT32 ui32FBSize;
	IMG_UINT32 ui32BufferSize;
	IMG_UINT32 ui32RoundedBufferSize;
	IMG_UINT32 ui32Width;
	IMG_UINT32 ui32Height;
	IMG_UINT32 ui32ByteStride;

	PVRSRV_PIXEL_FORMAT ePixelFormat;
} OMAPLFB_FBINFO;

typedef struct OMAPLFB_DEVINFO_TAG {
	IMG_UINT32 ui32DeviceID;
	DISPLAY_INFO sDisplayInfo;

	OMAPLFB_BUFFER sSystemBuffer;

	DISPLAY_FORMAT sDisplayFormat;

	DISPLAY_DIMS sDisplayDim;

	PVRSRV_DC_DISP2SRV_KMJTABLE sPVRJTable;

	PVRSRV_DC_SRV2DISP_KMJTABLE sDCJTable;

	OMAPLFB_FBINFO sFBInfo;

	IMG_UINT32 ui32RefCount;

	OMAPLFB_SWAPCHAIN *psSwapChain;

	IMG_BOOL bFlushCommands;

	IMG_DEV_VIRTADDR sDisplayDevVAddr;

	struct fb_info *psLINFBInfo;

	struct notifier_block sLINNotifBlock;

	IMG_BOOL bDeviceSuspended;

	spinlock_t SwapChainLock;
} OMAPLFB_DEVINFO;

#define	OMAPLFB_PAGE_SIZE 4096
#define	OMAPLFB_PAGE_MASK (OMAPLFB_PAGE_SIZE - 1)
#define	OMAPLFB_PAGE_TRUNC (~OMAPLFB_PAGE_MASK)

#define	OMAPLFB_PAGE_ROUNDUP(x) (((x) + OMAPLFB_PAGE_MASK) & OMAPLFB_PAGE_TRUNC)

#ifdef	DEBUG
#define	DEBUG_PRINTK(x) printk x
#else
#define	DEBUG_PRINTK(x)
#endif

#define DISPLAY_DEVICE_NAME "PowerVR OMAP Linux Display Driver"
#define	DRVNAME	"omaplfb"
#define	DEVNAME	DRVNAME
#define	DRIVER_PREFIX DRVNAME

PVRSRV_ERROR OMAPLFBInit(IMG_VOID);
PVRSRV_ERROR OMAPLFBDeinit(IMG_VOID);

IMG_VOID OMAPLFBDriverSuspend(IMG_VOID);
IMG_VOID OMAPLFBDriverResume(IMG_VOID);

IMG_VOID *OMAPLFBAllocKernelMem(IMG_UINT32 ui32Size);
IMG_VOID OMAPLFBFreeKernelMem(IMG_VOID * pvMem);
PVRSRV_ERROR OMAPLFBGetLibFuncAddr(IMG_CHAR * szFunctionName,
				   PFN_DC_GET_PVRJTABLE * ppfnFuncTable);
PVRSRV_ERROR OMAPLFBInstallVSyncISR(OMAPLFB_SWAPCHAIN * psSwapChain);
PVRSRV_ERROR OMAPLFBUninstallVSyncISR(OMAPLFB_SWAPCHAIN * psSwapChain);
IMG_BOOL OMAPLFBVSyncIHandler(OMAPLFB_SWAPCHAIN * psSwapChain);
IMG_VOID OMAPLFBEnableVSyncInterrupt(OMAPLFB_SWAPCHAIN * psSwapChain);
IMG_VOID OMAPLFBDisableVSyncInterrupt(OMAPLFB_SWAPCHAIN * psSwapChain);
IMG_VOID OMAPLFBEnableDisplayRegisterAccess(IMG_VOID);
IMG_VOID OMAPLFBDisableDisplayRegisterAccess(IMG_VOID);
IMG_VOID OMAPLFBFlip(OMAPLFB_SWAPCHAIN * psSwapChain, IMG_UINT32 aPhyAddr);

#endif
