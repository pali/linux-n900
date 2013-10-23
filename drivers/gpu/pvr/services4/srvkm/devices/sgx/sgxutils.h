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

#include "perproc.h"

#define GET_CCB_SPACE(WOff, ROff, CCBSize) \
	(((ROff - WOff) + (CCBSize - 1)) & (CCBSize - 1))

#define UPDATE_CCB_OFFSET(Off, PacketSize, CCBSize) \
	Off = ((Off + PacketSize) & (CCBSize - 1))

#define CCB_OFFSET_IS_VALID(type, psCCBMemInfo, psCCBKick, offset) \
	((sizeof(type) <= (psCCBMemInfo)->ui32AllocSize) && \
	((psCCBKick)->offset <= (psCCBMemInfo)->ui32AllocSize - sizeof(type)))

#define	CCB_DATA_FROM_OFFSET(type, psCCBMemInfo, psCCBKick, offset) \
	((type *)(((char *)(psCCBMemInfo)->pvLinAddrKM) + \
		(psCCBKick)->offset))

static INLINE IMG_UINT32 SGXCalcContextCCBParamSize(IMG_UINT32 ui32ParamSize, IMG_UINT32 ui32AllocGran)
{
	return (ui32ParamSize + (ui32AllocGran - 1)) & ~(ui32AllocGran - 1);
}

static INLINE IMG_PVOID SGXAcquireCCB(PVRSRV_SGX_CCB *psCCB, IMG_UINT32 ui32CmdSize)
{
	IMG_BOOL	bStart = IMG_FALSE;
	IMG_UINT32	uiStart = 0;

	do
	{
		if(GET_CCB_SPACE(*psCCB->pui32WriteOffset, *psCCB->pui32ReadOffset, psCCB->ui32Size) > ui32CmdSize)
		{
			return (IMG_PVOID)((IMG_UINT32)psCCB->psCCBMemInfo->pvLinAddrKM + *psCCB->pui32WriteOffset);
		}

		if (bStart == IMG_FALSE)
		{
			bStart = IMG_TRUE;
			uiStart = OSClockus();
		}
		OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
	} while ((OSClockus() - uiStart) < MAX_HW_TIME_US);

	
	return IMG_NULL;
}


#if defined (PDUMP)
IMG_VOID DumpBufferArray(PPVR3DIF4_KICKTA_DUMP_BUFFER	psBufferArray,
						 IMG_UINT32						ui32BufferArrayLength,
						 IMG_BOOL						bDumpPolls);
#endif


#if defined(SUPPORT_ACTIVE_POWER_MANAGEMENT)
IMG_IMPORT
IMG_VOID SGXTestActivePowerEvent(PVRSRV_DEVICE_NODE	*psDeviceNode,
								 IMG_UINT32			ui32CallerID);
#endif 

IMG_IMPORT
PVRSRV_ERROR SGXScheduleCCBCommandKM(PVRSRV_DEVICE_NODE			*psDeviceNode,
									 PVRSRV_SGX_COMMAND_TYPE	eCommandType,
									 PVRSRV_SGX_COMMAND			*psCommandData,
									 IMG_UINT32					ui32CallerID);

IMG_IMPORT
IMG_VOID SGXScheduleProcessQueues(PVRSRV_DEVICE_NODE *psDeviceNode);

IMG_IMPORT
IMG_BOOL SGXIsDevicePowered(PVRSRV_DEVICE_NODE *psDeviceNode);

IMG_IMPORT
IMG_HANDLE SGXRegisterHWRenderContextKM(IMG_HANDLE				psDeviceNode,
										IMG_DEV_VIRTADDR		*psHWRenderContextDevVAddr,
										PVRSRV_PER_PROCESS_DATA *psPerProc);

IMG_IMPORT
IMG_HANDLE SGXRegisterHWTransferContextKM(IMG_HANDLE				psDeviceNode,
										  IMG_DEV_VIRTADDR			*psHWTransferContextDevVAddr,
										  PVRSRV_PER_PROCESS_DATA	*psPerProc);

IMG_IMPORT
IMG_VOID SGXFlushHWRenderTargetKM(IMG_HANDLE psSGXDevInfo, IMG_DEV_VIRTADDR psHWRTDataSetDevVAddr);

IMG_IMPORT
PVRSRV_ERROR SGXUnregisterHWRenderContextKM(IMG_HANDLE hHWRenderContext);

IMG_IMPORT
PVRSRV_ERROR SGXUnregisterHWTransferContextKM(IMG_HANDLE hHWTransferContext);

#if defined(SGX_FEATURE_2D_HARDWARE)
IMG_IMPORT
IMG_HANDLE SGXRegisterHW2DContextKM(IMG_HANDLE				psDeviceNode,
									IMG_DEV_VIRTADDR		*psHW2DContextDevVAddr,
									PVRSRV_PER_PROCESS_DATA *psPerProc);

IMG_IMPORT
PVRSRV_ERROR SGXUnregisterHW2DContextKM(IMG_HANDLE hHW2DContext);
#endif

