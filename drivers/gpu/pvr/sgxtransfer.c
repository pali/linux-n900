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


#include <stddef.h>

#include "sgxdefs.h"
#include "services_headers.h"
#include "buffer_manager.h"
#include "sgxinfo.h"
#include "sysconfig.h"
#include "pdump_km.h"
#include "mmu.h"
#include "pvr_bridge.h"
#include "sgx_bridge_km.h"
#include "sgxinfokm.h"
#include "osfunc.h"
#include "pvr_debug.h"
#include "sgxutils.h"

enum PVRSRV_ERROR SGXSubmitTransferKM(void *hDevHandle,
				struct PVRSRV_TRANSFER_SGX_KICK *psKick)
{
	struct PVRSRV_KERNEL_MEM_INFO *psCCBMemInfo =
	    (struct PVRSRV_KERNEL_MEM_INFO *)psKick->hCCBMemInfo;
	struct PVRSRV_SGX_COMMAND sCommand = { 0 };
	struct PVR3DIF4_TRANSFERCMD_SHARED *psTransferCmd;
	struct PVRSRV_KERNEL_SYNC_INFO *psSyncInfo;
	u32 i;
	enum PVRSRV_ERROR eError;

	if (!CCB_OFFSET_IS_VALID
	    (struct PVR3DIF4_TRANSFERCMD_SHARED, psCCBMemInfo, psKick,
	     ui32SharedCmdCCBOffset)) {
		PVR_DPF(PVR_DBG_ERROR,
			 "SGXSubmitTransferKM: Invalid CCB offset");
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
	psTransferCmd = CCB_DATA_FROM_OFFSET(struct PVR3DIF4_TRANSFERCMD_SHARED,
					     psCCBMemInfo, psKick,
					     ui32SharedCmdCCBOffset);

	if (psTransferCmd->ui32NumStatusVals > SGXTQ_MAX_STATUS)
		return PVRSRV_ERROR_INVALID_PARAMS;

	if (psKick->ui32StatusFirstSync +
	    (psKick->ui32NumSrcSync ? (psKick->ui32NumSrcSync - 1) : 0) +
	    (psKick->ui32NumDstSync ? (psKick->ui32NumDstSync - 1) : 0) >
	    psTransferCmd->ui32NumStatusVals)
		return PVRSRV_ERROR_INVALID_PARAMS;

	if (psKick->hTASyncInfo != NULL) {
		psSyncInfo = (struct PVRSRV_KERNEL_SYNC_INFO *)
			psKick->hTASyncInfo;

		psTransferCmd->ui32TASyncWriteOpsPendingVal =
		    psSyncInfo->psSyncData->ui32WriteOpsPending++;
		psTransferCmd->ui32TASyncReadOpsPendingVal =
		    psSyncInfo->psSyncData->ui32ReadOpsPending;

		psTransferCmd->sTASyncWriteOpsCompleteDevVAddr =
		    psSyncInfo->sWriteOpsCompleteDevVAddr;
		psTransferCmd->sTASyncReadOpsCompleteDevVAddr =
		    psSyncInfo->sReadOpsCompleteDevVAddr;
	} else {
		psTransferCmd->sTASyncWriteOpsCompleteDevVAddr.uiAddr = 0;
		psTransferCmd->sTASyncReadOpsCompleteDevVAddr.uiAddr = 0;
	}

	if (psKick->h3DSyncInfo != NULL) {
		psSyncInfo = (struct PVRSRV_KERNEL_SYNC_INFO *)
							psKick->h3DSyncInfo;

		psTransferCmd->ui323DSyncWriteOpsPendingVal =
		    psSyncInfo->psSyncData->ui32WriteOpsPending++;
		psTransferCmd->ui323DSyncReadOpsPendingVal =
		    psSyncInfo->psSyncData->ui32ReadOpsPending;

		psTransferCmd->s3DSyncWriteOpsCompleteDevVAddr =
		    psSyncInfo->sWriteOpsCompleteDevVAddr;
		psTransferCmd->s3DSyncReadOpsCompleteDevVAddr =
		    psSyncInfo->sReadOpsCompleteDevVAddr;
	} else {
		psTransferCmd->s3DSyncWriteOpsCompleteDevVAddr.uiAddr = 0;
		psTransferCmd->s3DSyncReadOpsCompleteDevVAddr.uiAddr = 0;
	}

	psTransferCmd->ui32NumSrcSync = psKick->ui32NumSrcSync;
	psTransferCmd->ui32NumDstSync = psKick->ui32NumDstSync;

	if (psKick->ui32NumSrcSync > 0) {
		psSyncInfo =
		    (struct PVRSRV_KERNEL_SYNC_INFO *)psKick->ahSrcSyncInfo[0];

		psTransferCmd->ui32SrcWriteOpPendingVal =
		    psSyncInfo->psSyncData->ui32WriteOpsPending;
		psTransferCmd->ui32SrcReadOpPendingVal =
		    psSyncInfo->psSyncData->ui32ReadOpsPending;

		psTransferCmd->sSrcWriteOpsCompleteDevAddr =
		    psSyncInfo->sWriteOpsCompleteDevVAddr;
		psTransferCmd->sSrcReadOpsCompleteDevAddr =
		    psSyncInfo->sReadOpsCompleteDevVAddr;
	}
	if (psKick->ui32NumDstSync > 0) {
		psSyncInfo =
		    (struct PVRSRV_KERNEL_SYNC_INFO *)psKick->ahDstSyncInfo[0];

		psTransferCmd->ui32DstWriteOpPendingVal =
		    psSyncInfo->psSyncData->ui32WriteOpsPending;
		psTransferCmd->ui32DstReadOpPendingVal =
		    psSyncInfo->psSyncData->ui32ReadOpsPending;

		psTransferCmd->sDstWriteOpsCompleteDevAddr =
		    psSyncInfo->sWriteOpsCompleteDevVAddr;
		psTransferCmd->sDstReadOpsCompleteDevAddr =
		    psSyncInfo->sReadOpsCompleteDevVAddr;
	}

	if (psKick->ui32NumSrcSync > 0) {
		psSyncInfo = (struct PVRSRV_KERNEL_SYNC_INFO *)
						psKick->ahSrcSyncInfo[0];
		psSyncInfo->psSyncData->ui32ReadOpsPending++;

	}
	if (psKick->ui32NumDstSync > 0) {
		psSyncInfo = (struct PVRSRV_KERNEL_SYNC_INFO *)
						psKick->ahDstSyncInfo[0];
		psSyncInfo->psSyncData->ui32WriteOpsPending++;
	}

	if (psKick->ui32NumSrcSync > 1)
		for (i = 1; i < psKick->ui32NumSrcSync; i++) {
			psSyncInfo =
			    (struct PVRSRV_KERNEL_SYNC_INFO *)psKick->
			    ahSrcSyncInfo[i];

			psTransferCmd->sCtlStatusInfo[psKick->
			      ui32StatusFirstSync].ui32StatusValue =
				  psSyncInfo->psSyncData->ui32ReadOpsPending++;

			psTransferCmd->sCtlStatusInfo[psKick->
			      ui32StatusFirstSync].sStatusDevAddr =
				  psSyncInfo->sReadOpsCompleteDevVAddr;

			psKick->ui32StatusFirstSync++;
		}

	if (psKick->ui32NumDstSync > 1)
		for (i = 1; i < psKick->ui32NumDstSync; i++) {
			psSyncInfo = (struct PVRSRV_KERNEL_SYNC_INFO *)
				psKick->ahDstSyncInfo[i];

			psTransferCmd->sCtlStatusInfo[psKick->
			    ui32StatusFirstSync].ui32StatusValue =
				 psSyncInfo->psSyncData->ui32WriteOpsPending++;

			psTransferCmd->sCtlStatusInfo[psKick->
			    ui32StatusFirstSync].sStatusDevAddr =
				 psSyncInfo->sWriteOpsCompleteDevVAddr;

			psKick->ui32StatusFirstSync++;
		}
#if defined(PDUMP)
	if (PDumpIsCaptureFrameKM()) {
		PDUMPCOMMENT("Shared part of transfer command\r\n");
		PDUMPMEM(psTransferCmd, psCCBMemInfo, psKick->ui32CCBDumpWOff,
			 sizeof(struct PVR3DIF4_TRANSFERCMD_SHARED), 0,
			 MAKEUNIQUETAG(psCCBMemInfo));

		if (psKick->ui32NumSrcSync > 0) {
			psSyncInfo = psKick->ahSrcSyncInfo[0];

			PDUMPCOMMENT
			    ("Hack src surface write op in transfer cmd\r\n");
			PDUMPMEM(&psSyncInfo->psSyncData->ui32LastOpDumpVal,
				 psCCBMemInfo, psKick->ui32CCBDumpWOff +
				    offsetof(struct PVR3DIF4_TRANSFERCMD_SHARED,
					    ui32SrcWriteOpPendingVal),
				 sizeof(psSyncInfo->psSyncData->
					ui32LastOpDumpVal), 0,
				 MAKEUNIQUETAG(psCCBMemInfo));

			PDUMPCOMMENT
			    ("Hack src surface read op in transfer cmd\r\n");
			PDUMPMEM(&psSyncInfo->psSyncData->ui32LastReadOpDumpVal,
				 psCCBMemInfo, psKick->ui32CCBDumpWOff +
				    offsetof(struct PVR3DIF4_TRANSFERCMD_SHARED,
					  ui32SrcReadOpPendingVal),
				 sizeof(psSyncInfo->psSyncData->
					ui32LastReadOpDumpVal), 0,
				 MAKEUNIQUETAG(psCCBMemInfo));
		}
		if (psKick->ui32NumDstSync > 0) {
			psSyncInfo = psKick->ahDstSyncInfo[0];

			PDUMPCOMMENT
			    ("Hack dest surface write op in transfer cmd\r\n");
			PDUMPMEM(&psSyncInfo->psSyncData->ui32LastOpDumpVal,
				 psCCBMemInfo, psKick->ui32CCBDumpWOff +
				    offsetof(struct PVR3DIF4_TRANSFERCMD_SHARED,
					  ui32DstWriteOpPendingVal),
				 sizeof(psSyncInfo->psSyncData->
					ui32LastOpDumpVal), 0,
				 MAKEUNIQUETAG(psCCBMemInfo));

			PDUMPCOMMENT
			    ("Hack dest surface read op in transfer cmd\r\n");
			PDUMPMEM(&psSyncInfo->psSyncData->ui32LastReadOpDumpVal,
				 psCCBMemInfo, psKick->ui32CCBDumpWOff +
				    offsetof(struct PVR3DIF4_TRANSFERCMD_SHARED,
					  ui32DstReadOpPendingVal),
				 sizeof(psSyncInfo->psSyncData->
					ui32LastReadOpDumpVal), 0,
				 MAKEUNIQUETAG(psCCBMemInfo));
		}

		if (psKick->ui32NumSrcSync > 0) {
			psSyncInfo = (struct PVRSRV_KERNEL_SYNC_INFO *)
				psKick->ahSrcSyncInfo[0];
			psSyncInfo->psSyncData->ui32LastReadOpDumpVal++;

		}
		if (psKick->ui32NumDstSync > 0) {
			psSyncInfo = (struct PVRSRV_KERNEL_SYNC_INFO *)
				psKick->ahDstSyncInfo[0];
			psSyncInfo->psSyncData->ui32LastOpDumpVal++;
		}
	}
#endif

	sCommand.ui32Data[0] = PVRSRV_CCBFLAGS_TRANSFERCMD;
	sCommand.ui32Data[1] = psKick->sHWTransferContextDevVAddr.uiAddr;

	eError =
	    SGXScheduleCCBCommandKM(hDevHandle, PVRSRV_SGX_COMMAND_EDM_KICK,
				    &sCommand, KERNEL_ID);


	return eError;
}

