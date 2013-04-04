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

static inline u32 SGXCalcContextCCBParamSize(u32 ui32ParamSize,
						    u32 ui32AllocGran)
{
	return (ui32ParamSize + (ui32AllocGran - 1)) & ~(ui32AllocGran - 1);
}

static inline void *SGXAcquireCCB(struct PVRSRV_SGX_CCB *psCCB, u32 ui32CmdSize)
{
	IMG_BOOL bStart = IMG_FALSE;
	u32 uiStart = 0;

	do {
		if (GET_CCB_SPACE(*psCCB->pui32WriteOffset,
				  *psCCB->pui32ReadOffset,
				  psCCB->ui32Size) > ui32CmdSize)
			return (void *)((u32)psCCB->psCCBMemInfo->pvLinAddrKM +
					*psCCB->pui32WriteOffset);

		if (bStart == IMG_FALSE) {
			bStart = IMG_TRUE;
			uiStart = OSClockus();
		}
		OSWaitus(MAX_HW_TIME_US / WAIT_TRY_COUNT);
	} while ((OSClockus() - uiStart) < MAX_HW_TIME_US);

	return NULL;
}

#if defined(PDUMP)
void DumpBufferArray(struct PVR3DIF4_KICKTA_DUMP_BUFFER *psBufferArray,
			 u32 ui32BufferArrayLength, IMG_BOOL bDumpPolls);
#endif

void SGXTestActivePowerEvent(struct PVRSRV_DEVICE_NODE *psDeviceNode,
				     u32 ui32CallerID);

enum PVRSRV_ERROR SGXScheduleCCBCommandKM(
				struct PVRSRV_DEVICE_NODE *psDeviceNode,
				     enum PVRSRV_SGX_COMMAND_TYPE eCommandType,
				     struct PVRSRV_SGX_COMMAND *psCommandData,
				     u32 ui32CallerID);

void SGXScheduleProcessQueues(struct PVRSRV_DEVICE_NODE *psDeviceNode);

IMG_BOOL SGXIsDevicePowered(struct PVRSRV_DEVICE_NODE *psDeviceNode);

void *SGXRegisterHWRenderContextKM(void *psDeviceNode,
			 struct IMG_DEV_VIRTADDR *psHWRenderContextDevVAddr,
			 struct PVRSRV_PER_PROCESS_DATA *psPerProc);

void *SGXRegisterHWTransferContextKM(void *psDeviceNode,
			 struct IMG_DEV_VIRTADDR *psHWTransferContextDevVAddr,
			 struct PVRSRV_PER_PROCESS_DATA *psPerProc);

void SGXFlushHWRenderTargetKM(void *psSGXDevInfo,
			 struct IMG_DEV_VIRTADDR psHWRTDataSetDevVAddr);

enum PVRSRV_ERROR SGXUnregisterHWRenderContextKM(void *hHWRenderContext);

enum PVRSRV_ERROR SGXUnregisterHWTransferContextKM(void *hHWTransferContext);

