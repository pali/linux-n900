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
#include "services_headers.h"
#include "sgxinfo.h"
#include "sgxinfokm.h"
#if defined (PDUMP)
#include "sgxapi_km.h"
#include "pdump_km.h"
#endif
#include "sgx_bridge_km.h"
#include "osfunc.h"
#include "pvr_debug.h"
#include "sgxutils.h"

IMG_EXPORT
    PVRSRV_ERROR SGXDoKickKM(IMG_HANDLE hDevHandle,
			     PVR3DIF4_CCB_KICK * psCCBKick)
{
	PVRSRV_ERROR eError;
	PVRSRV_KERNEL_SYNC_INFO *psSyncInfo;
	PVRSRV_KERNEL_MEM_INFO *psCCBMemInfo =
	    (PVRSRV_KERNEL_MEM_INFO *) psCCBKick->hCCBKernelMemInfo;
	PVR3DIF4_CMDTA_SHARED *psTACmd;
	IMG_UINT32 i;
	PVRSRV_DEVICE_NODE *psDeviceNode;
	PVRSRV_SGXDEV_INFO *psDevInfo;

	psDeviceNode = (PVRSRV_DEVICE_NODE *) hDevHandle;
	psDevInfo = (PVRSRV_SGXDEV_INFO *) psDeviceNode->pvDevice;

	if (psCCBKick->bKickRender) {
		++psDevInfo->ui32KickTARenderCounter;
	}
	++psDevInfo->ui32KickTACounter;

	if (!CCB_OFFSET_IS_VALID
	    (PVR3DIF4_CMDTA_SHARED, psCCBMemInfo, psCCBKick, ui32CCBOffset)) {
		PVR_DPF((PVR_DBG_ERROR, "SGXDoKickKM: Invalid CCB offset"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
	psTACmd =
	    CCB_DATA_FROM_OFFSET(PVR3DIF4_CMDTA_SHARED, psCCBMemInfo, psCCBKick,
				 ui32CCBOffset);

	if (psCCBKick->hTA3DSyncInfo) {
		psSyncInfo =
		    (PVRSRV_KERNEL_SYNC_INFO *) psCCBKick->hTA3DSyncInfo;
		psTACmd->sTA3DDependancy.sWriteOpsCompleteDevVAddr =
		    psSyncInfo->sWriteOpsCompleteDevVAddr;

		psTACmd->sTA3DDependancy.ui32WriteOpPendingVal =
		    psSyncInfo->psSyncData->ui32WriteOpsPending;

		if (psCCBKick->bTADependency) {
			psSyncInfo->psSyncData->ui32WriteOpsPending++;
		}
	}

	if (psCCBKick->hTASyncInfo != IMG_NULL) {
		psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *) psCCBKick->hTASyncInfo;

		psTACmd->sTQSyncReadOpsCompleteDevVAddr =
		    psSyncInfo->sReadOpsCompleteDevVAddr;
		psTACmd->sTQSyncWriteOpsCompleteDevVAddr =
		    psSyncInfo->sWriteOpsCompleteDevVAddr;

		psTACmd->ui32TQSyncReadOpsPendingVal =
		    psSyncInfo->psSyncData->ui32ReadOpsPending++;
		psTACmd->ui32TQSyncWriteOpsPendingVal =
		    psSyncInfo->psSyncData->ui32WriteOpsPending;
	}

	if (psCCBKick->h3DSyncInfo != IMG_NULL) {
		psSyncInfo = (PVRSRV_KERNEL_SYNC_INFO *) psCCBKick->h3DSyncInfo;

		psTACmd->s3DTQSyncReadOpsCompleteDevVAddr =
		    psSyncInfo->sReadOpsCompleteDevVAddr;
		psTACmd->s3DTQSyncWriteOpsCompleteDevVAddr =
		    psSyncInfo->sWriteOpsCompleteDevVAddr;

		psTACmd->ui323DTQSyncReadOpsPendingVal =
		    psSyncInfo->psSyncData->ui32ReadOpsPending++;
		psTACmd->ui323DTQSyncWriteOpsPendingVal =
		    psSyncInfo->psSyncData->ui32WriteOpsPending;
	}

	psTACmd->ui32NumTAStatusVals = psCCBKick->ui32NumTAStatusVals;
	if (psCCBKick->ui32NumTAStatusVals != 0) {

		for (i = 0; i < psCCBKick->ui32NumTAStatusVals; i++) {
			psSyncInfo =
			    (PVRSRV_KERNEL_SYNC_INFO *) psCCBKick->
			    ahTAStatusSyncInfo[i];

			psTACmd->sCtlTAStatusInfo[i].sStatusDevAddr =
			    psSyncInfo->sReadOpsCompleteDevVAddr;

			psTACmd->sCtlTAStatusInfo[i].ui32StatusValue =
			    psSyncInfo->psSyncData->ui32ReadOpsPending;
		}
	}

	psTACmd->ui32Num3DStatusVals = psCCBKick->ui32Num3DStatusVals;
	if (psCCBKick->ui32Num3DStatusVals != 0) {

		for (i = 0; i < psCCBKick->ui32Num3DStatusVals; i++) {
			psSyncInfo =
			    (PVRSRV_KERNEL_SYNC_INFO *) psCCBKick->
			    ah3DStatusSyncInfo[i];

			psTACmd->sCtl3DStatusInfo[i].sStatusDevAddr =
			    psSyncInfo->sReadOpsCompleteDevVAddr;

			psTACmd->sCtl3DStatusInfo[i].ui32StatusValue =
			    psSyncInfo->psSyncData->ui32ReadOpsPending;
		}
	}

	psTACmd->ui32NumSrcSyncs = psCCBKick->ui32NumSrcSyncs;
	for (i = 0; i < psCCBKick->ui32NumSrcSyncs; i++) {
		psSyncInfo =
		    (PVRSRV_KERNEL_SYNC_INFO *) psCCBKick->
		    ahSrcKernelSyncInfo[i];

		psTACmd->asSrcSyncs[i].sWriteOpsCompleteDevVAddr =
		    psSyncInfo->sWriteOpsCompleteDevVAddr;
		psTACmd->asSrcSyncs[i].sReadOpsCompleteDevVAddr =
		    psSyncInfo->sReadOpsCompleteDevVAddr;

		psTACmd->asSrcSyncs[i].ui32ReadOpPendingVal =
		    psSyncInfo->psSyncData->ui32ReadOpsPending++;

		psTACmd->asSrcSyncs[i].ui32WriteOpPendingVal =
		    psSyncInfo->psSyncData->ui32WriteOpsPending;

	}

	if (psCCBKick->bFirstKickOrResume
	    && psCCBKick->hRenderSurfSyncInfo != IMG_NULL) {
		psSyncInfo =
		    (PVRSRV_KERNEL_SYNC_INFO *) psCCBKick->hRenderSurfSyncInfo;
		psTACmd->sWriteOpsCompleteDevVAddr =
		    psSyncInfo->sWriteOpsCompleteDevVAddr;
		psTACmd->sReadOpsCompleteDevVAddr =
		    psSyncInfo->sReadOpsCompleteDevVAddr;

		psTACmd->ui32ReadOpsPendingVal =
		    psSyncInfo->psSyncData->ui32ReadOpsPending;
		psTACmd->ui32WriteOpsPendingVal =
		    psSyncInfo->psSyncData->ui32WriteOpsPending++;

#if defined(PDUMP)
		if (PDumpIsCaptureFrameKM()) {
			if (psSyncInfo->psSyncData->ui32LastOpDumpVal == 0) {

				PDUMPCOMMENT("Init render surface last op\r\n");

				PDUMPMEM(IMG_NULL,
					 psSyncInfo->psSyncDataMemInfoKM,
					 0,
					 sizeof(PVRSRV_SYNC_DATA),
					 0,
					 MAKEUNIQUETAG(psSyncInfo->
						       psSyncDataMemInfoKM));

				PDUMPMEM(&psSyncInfo->psSyncData->
					 ui32LastOpDumpVal,
					 psSyncInfo->psSyncDataMemInfoKM,
					 offsetof(PVRSRV_SYNC_DATA,
						  ui32WriteOpsComplete),
					 sizeof(psSyncInfo->psSyncData->
						ui32WriteOpsComplete), 0,
					 MAKEUNIQUETAG(psSyncInfo->
						       psSyncDataMemInfoKM));
			}

			psSyncInfo->psSyncData->ui32LastOpDumpVal++;
		}
#endif
	}
#if defined(PDUMP)
	if (PDumpIsCaptureFrameKM()) {
		PDUMPCOMMENT("Shared part of TA command\r\n");

		PDUMPMEM(psTACmd,
			 psCCBMemInfo,
			 psCCBKick->ui32CCBDumpWOff,
			 sizeof(PVR3DIF4_CMDTA_SHARED),
			 0, MAKEUNIQUETAG(psCCBMemInfo));

		if (psCCBKick->hRenderSurfSyncInfo != IMG_NULL) {
			IMG_UINT32 ui32HackValue;

			psSyncInfo =
			    (PVRSRV_KERNEL_SYNC_INFO *) psCCBKick->
			    hRenderSurfSyncInfo;
			ui32HackValue =
			    psSyncInfo->psSyncData->ui32LastOpDumpVal - 1;

			PDUMPCOMMENT
			    ("Hack render surface last op in TA cmd\r\n");

			PDUMPMEM(&ui32HackValue,
				 psCCBMemInfo,
				 psCCBKick->ui32CCBDumpWOff +
				 offsetof(PVR3DIF4_CMDTA_SHARED,
					  ui32WriteOpsPendingVal),
				 sizeof(IMG_UINT32), 0,
				 MAKEUNIQUETAG(psCCBMemInfo));

			ui32HackValue = 0;
			PDUMPCOMMENT
			    ("Hack render surface read op in TA cmd\r\n");

			PDUMPMEM(&ui32HackValue,
				 psCCBMemInfo,
				 psCCBKick->ui32CCBDumpWOff +
				 offsetof(PVR3DIF4_CMDTA_SHARED,
					  sReadOpsCompleteDevVAddr),
				 sizeof(IMG_UINT32), 0,
				 MAKEUNIQUETAG(psCCBMemInfo));
		}

		for (i = 0; i < psCCBKick->ui32NumTAStatusVals; i++) {
			psSyncInfo =
			    (PVRSRV_KERNEL_SYNC_INFO *) psCCBKick->
			    ahTAStatusSyncInfo[i];

			PDUMPCOMMENT("Hack TA status value in TA cmd\r\n");

			PDUMPMEM(&psSyncInfo->psSyncData->ui32LastOpDumpVal,
				 psCCBMemInfo,
				 psCCBKick->ui32CCBDumpWOff +
				 offsetof(PVR3DIF4_CMDTA_SHARED,
					  sCtlTAStatusInfo[i].ui32StatusValue),
				 sizeof(IMG_UINT32), 0,
				 MAKEUNIQUETAG(psCCBMemInfo));
		}

		for (i = 0; i < psCCBKick->ui32Num3DStatusVals; i++) {
			psSyncInfo =
			    (PVRSRV_KERNEL_SYNC_INFO *) psCCBKick->
			    ah3DStatusSyncInfo[i];

			PDUMPCOMMENT("Hack 3D status value in TA cmd\r\n");

			PDUMPMEM(&psSyncInfo->psSyncData->ui32LastOpDumpVal,
				 psCCBMemInfo,
				 psCCBKick->ui32CCBDumpWOff +
				 offsetof(PVR3DIF4_CMDTA_SHARED,
					  sCtl3DStatusInfo[i].ui32StatusValue),
				 sizeof(IMG_UINT32), 0,
				 MAKEUNIQUETAG(psCCBMemInfo));
		}
	}
#endif

	eError =
	    SGXScheduleCCBCommandKM(hDevHandle, psCCBKick->eCommand,
				    &psCCBKick->sCommand, KERNEL_ID);
	if (eError == PVRSRV_ERROR_RETRY) {
		if (psCCBKick->bFirstKickOrResume
		    && psCCBKick->hRenderSurfSyncInfo != IMG_NULL) {

			psSyncInfo =
			    (PVRSRV_KERNEL_SYNC_INFO *) psCCBKick->
			    hRenderSurfSyncInfo;
			psSyncInfo->psSyncData->ui32WriteOpsPending--;
		}

		for (i = 0; i < psCCBKick->ui32NumSrcSyncs; i++) {
			psSyncInfo =
			    (PVRSRV_KERNEL_SYNC_INFO *) psCCBKick->
			    ahSrcKernelSyncInfo[i];
			psSyncInfo->psSyncData->ui32ReadOpsPending--;
		}

#if defined(PDUMP)
		if (psCCBKick->bFirstKickOrResume
		    && psCCBKick->hRenderSurfSyncInfo != IMG_NULL) {
			if (PDumpIsCaptureFrameKM()) {
				psSyncInfo =
				    (PVRSRV_KERNEL_SYNC_INFO *) psCCBKick->
				    hRenderSurfSyncInfo;
				psSyncInfo->psSyncData->ui32LastOpDumpVal--;
			}
		}
#endif

		return eError;
	} else if (PVRSRV_OK != eError) {
		PVR_DPF((PVR_DBG_ERROR,
			 "SGXDoKickKM: SGXScheduleCCBCommandKM failed."));
		return eError;
	}


	return eError;
}
