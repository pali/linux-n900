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
#include "services_headers.h"
#include "sgxinfo.h"
#include "sgxinfokm.h"

#if defined(SGX_FEATURE_2D_HARDWARE)

#include "sgx2dcore.h"

#define SGX2D_FLUSH_BH	0xF0000000 
#define	SGX2D_FENCE_BH	0x70000000 

#define SGX2D_QUEUED_BLIT_PAD	4

#define SGX2D_COMMAND_QUEUE_SIZE 1024

#define SGX2D_2D_NOT_IDLE(psDevInfo)	((psDevInfo)->ui322DFifoSize > SGX2DFifoFreeSpace(psDevInfo) || SGX2DIsBusy(psDevInfo))

static IMG_VOID SGX2DHardwareKick(PVRSRV_SGXDEV_INFO *psDevInfo)
{
	
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_EVENT_STATUS, EUR_CR_EVENT_STATUS_MASTER_INTERRUPT_MASK | EUR_CR_EVENT_STATUS_TWOD_COMPLETE_MASK);
}

IMG_VOID SGX2DHWRecoveryStart(PVRSRV_SGXDEV_INFO *psDevInfo)
{
	psDevInfo->b2DHWRecoveryInProgress = IMG_TRUE;
	psDevInfo->b2DHWRecoveryEndPending = IMG_FALSE;
}

IMG_VOID SGX2DHWRecoveryEnd(PVRSRV_SGXDEV_INFO *psDevInfo)
{
	psDevInfo->b2DHWRecoveryEndPending = IMG_TRUE;
	psDevInfo->b2DHWRecoveryInProgress = IMG_FALSE;
	SGX2DHardwareKick(psDevInfo);
}

#if !defined(NO_HARDWARE)
static IMG_VOID SGX2DKick(PVRSRV_SGXDEV_INFO *psDevInfo)
{
	IMG_BOOL bStart = IMG_FALSE;
	IMG_UINT32 uiStart = 0;

	PVR_UNREFERENCED_PARAMETER(psDevInfo);

	
	do
	{
		if(PVRSRVProcessQueues(KERNEL_ID, IMG_FALSE) != PVRSRV_ERROR_PROCESSING_BLOCKED)
		{
			break;
		}

		if (bStart == IMG_FALSE)
		{
			uiStart = OSClockus();
			bStart = IMG_TRUE;
		}
		OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
	} while ((OSClockus() - uiStart) < MAX_HW_TIME_US);
}
#endif 

IMG_BOOL SGX2DIsBusy(PVRSRV_SGXDEV_INFO *psDevInfo)
{
	IMG_UINT32 ui32BlitStatus;

	ui32BlitStatus = OSReadHWReg(psDevInfo->pvRegsBaseKM,
		EUR_CR_2D_BLIT_STATUS);

	return (ui32BlitStatus & EUR_CR_2D_BLIT_STATUS_BUSY_MASK) != 0;
}

IMG_UINT32 SGX2DCompletedBlits(PVRSRV_SGXDEV_INFO *psDevInfo)
{
	IMG_UINT32 ui32BlitStatus;

	ui32BlitStatus = OSReadHWReg(psDevInfo->pvRegsBaseKM,
		EUR_CR_2D_BLIT_STATUS);

	return (ui32BlitStatus & EUR_CR_2D_BLIT_STATUS_COMPLETE_MASK) >>
					EUR_CR_2D_BLIT_STATUS_COMPLETE_SHIFT;
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(SGX2DAcquireSlavePort)
#endif
#ifdef INLINE_IS_PRAGMA
#pragma inline(SGX2DAcquireSlavePort)
#endif
static INLINE
PVRSRV_ERROR SGX2DAcquireSlavePort(PVRSRV_SGXDEV_INFO *psDevInfo,
								   IMG_BOOL			  bBlock)
{
#if defined(SGX2D_DIRECT_BLITS)
	PVR_UNREFERENCED_PARAMETER(bBlock);
	return OSLockResource(&psDevInfo->s2DSlaveportResource, ISR_ID);
#else
	PVR_UNREFERENCED_PARAMETER(psDevInfo);
	PVR_UNREFERENCED_PARAMETER(bBlock);

	return PVRSRV_OK;
#endif
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(SGX2DReleaseSlavePort)
#endif
static INLINE
PVRSRV_ERROR SGX2DReleaseSlavePort(PVRSRV_SGXDEV_INFO *psDevInfo)
{
#if defined(SGX2D_DIRECT_BLITS)
	return OSUnlockResource(&psDevInfo->s2DSlaveportResource, ISR_ID);
#else
	PVR_UNREFERENCED_PARAMETER(psDevInfo);

	return PVRSRV_OK;
#endif
}

#ifdef INLINE_IS_PRAGMA
#pragma inline(SGX2DAcquireFifoSpace)
#endif
static INLINE
PVRSRV_ERROR SGX2DAcquireFifoSpace(PVRSRV_SGXDEV_INFO	*psDevInfo,
								   IMG_UINT32			ui32MinBytesRequired,
								   IMG_UINT32			*pui32BytesObtained)
{
	PVRSRV_ERROR	eError = PVRSRV_ERROR_FIFO_SPACE;
	IMG_UINT32		ui32FifoBytes;

#if defined(DEBUG) && defined(SGX2D_DIRECT_BLITS)
	
	if (OSIsResourceLocked(&psDevInfo->s2DSlaveportResource, ISR_ID) == IMG_FALSE)
	{
		PVR_DPF((PVR_DBG_ERROR,"SGX2DAcquireFifoSpace: 2D slaveport is not locked"));
		return PVRSRV_ERROR_PROCESSING_BLOCKED;
	}
#endif 

	
	ui32FifoBytes = SGX2DFifoFreeSpace(psDevInfo);

	
	if (ui32FifoBytes >= ui32MinBytesRequired)
	{
		if (pui32BytesObtained)
			*pui32BytesObtained = ui32FifoBytes;
		
		eError = PVRSRV_OK;
	}

	return eError;
}

#if defined(DEBUG) && defined (SGX2D_TRACE_BLIT)
#ifdef INLINE_IS_PRAGMA
#pragma inline(SGX2DTraceBlt)
#endif
FORCE_INLINE
IMG_VOID SGX2DTraceBlt(IMG_UINT32 *pui32BltData, IMG_UINT32 ui32Count)
{
	IMG_UINT32 i;

	PVR_TRACE(("----SGX 2D BLIT----"));

	for (i = 0; i < ui32Count; i++)
	{
		PVR_TRACE(("word[%02d]: 0x%08x", i, pui32BltData[i]));
	}
}
#else
#define SGX2DTraceBlt(pui32BltData, ui32Count)
#endif

#ifdef INLINE_IS_PRAGMA
#pragma inline(SGX2DWriteSlavePort)
#endif
FORCE_INLINE
IMG_VOID SGX2DWriteSlavePort(PVRSRV_SGXDEV_INFO *psDevInfo,
							 IMG_UINT32 ui32Value)
{
	SGX_SLAVE_PORT		*psSlavePort= &psDevInfo->s2DSlavePortKM;
#if defined(SGX2D_INCREMENTING_SP_WRITES)
	IMG_UINT32 *pui32Offset = psSlavePort->pui32Offset;

	
	if(*pui32Offset > (psSlavePort->ui32DataRange >> 1))
	{
		
		*pui32Offset = 0;
	}
#endif

	SGX2DTraceBlt(&ui32Value, 1);

#if defined(SGX2D_INCREMENTING_SP_WRITES)
	*((IMG_UINT32*)((IMG_UINT32)psSlavePort->pvData + *pui32Offset)) = ui32Value;
#else
	*((IMG_UINT32*)psSlavePort->pvData) = ui32Value;
#endif

#if defined(SGX2D_INCREMENTING_SP_WRITES)
	*pui32Offset += 4;
#endif
}


#ifdef INLINE_IS_PRAGMA
#pragma inline(SGX2DWriteSlavePortBatch)
#endif
FORCE_INLINE
PVRSRV_ERROR SGX2DWriteSlavePortBatch(PVRSRV_SGXDEV_INFO	*psDevInfo,
									  IMG_UINT32			*pui32LinDataAddr,
									  IMG_UINT32			ui32Bytes)
{
	IMG_INT32	i;
	SGX_SLAVE_PORT	*psSlavePort= &psDevInfo->s2DSlavePortKM;
	IMG_UINT32	*pui32LinPortAddrBase = (IMG_UINT32*) psSlavePort->pvData;
	IMG_UINT32 	ui32DWORDs = ui32Bytes >> 2;
#if defined(SGX2D_INCREMENTING_SP_WRITES)
	IMG_UINT32	*pui32Offset = psSlavePort->pui32Offset;
	IMG_UINT32	*pui32LinPortAddr;

	
	if (ui32Bytes > (psSlavePort->ui32DataRange >> 1))
	{
		return PVRSRV_ERROR_CMD_TOO_BIG;
	}

	
	if(*pui32Offset > (psSlavePort->ui32DataRange >> 1))
	{
		
		*pui32Offset = 0;
	}

	
	pui32LinPortAddr = (IMG_UINT32*)((IMG_UINT32)pui32LinPortAddrBase + *pui32Offset);
#endif
	
	SGX2DTraceBlt(pui32LinDataAddr, ui32DWORDs);

	
	for (i = ui32DWORDs; i != 0 ; i -= ui32DWORDs)
	{
		ui32DWORDs = (i < 32) ? i : 32;

		switch(ui32DWORDs)
		{
#if defined(SGX2D_INCREMENTING_SP_WRITES)
			case 32:
			*pui32LinPortAddr++ = *pui32LinDataAddr++;
			case 31:
			*pui32LinPortAddr++ = *pui32LinDataAddr++;
			case 30:
			*pui32LinPortAddr++ = *pui32LinDataAddr++;
			case 29:
			*pui32LinPortAddr++ = *pui32LinDataAddr++;
			case 28:
			*pui32LinPortAddr++ = *pui32LinDataAddr++;
			case 27:
			*pui32LinPortAddr++ = *pui32LinDataAddr++;
			case 26:
			*pui32LinPortAddr++ = *pui32LinDataAddr++;
			case 25:
			*pui32LinPortAddr++ = *pui32LinDataAddr++;
			case 24:
			*pui32LinPortAddr++ = *pui32LinDataAddr++;
			case 23:
			*pui32LinPortAddr++ = *pui32LinDataAddr++;
			case 22:
			*pui32LinPortAddr++ = *pui32LinDataAddr++;
			case 21:
			*pui32LinPortAddr++ = *pui32LinDataAddr++;
			case 20:
			*pui32LinPortAddr++ = *pui32LinDataAddr++;
			case 19:
			*pui32LinPortAddr++ = *pui32LinDataAddr++;
			case 18:
			*pui32LinPortAddr++ = *pui32LinDataAddr++;
			case 17:
			*pui32LinPortAddr++ = *pui32LinDataAddr++;
			case 16:
			*pui32LinPortAddr++ = *pui32LinDataAddr++;
			case 15:
			*pui32LinPortAddr++ = *pui32LinDataAddr++;
			case 14:
			*pui32LinPortAddr++ = *pui32LinDataAddr++;
			case 13:
			*pui32LinPortAddr++ = *pui32LinDataAddr++;
			case 12:
			*pui32LinPortAddr++ = *pui32LinDataAddr++;
			case 11:
			*pui32LinPortAddr++ = *pui32LinDataAddr++;
			case 10:
			*pui32LinPortAddr++ = *pui32LinDataAddr++;
			case 9:
			*pui32LinPortAddr++ = *pui32LinDataAddr++;
			case 8:
			*pui32LinPortAddr++ = *pui32LinDataAddr++;
			case 7:
			*pui32LinPortAddr++ = *pui32LinDataAddr++;
			case 6:
			*pui32LinPortAddr++ = *pui32LinDataAddr++;
			case 5:
			*pui32LinPortAddr++ = *pui32LinDataAddr++;
			case 4:
			*pui32LinPortAddr++ = *pui32LinDataAddr++;
			case 3:
			*pui32LinPortAddr++ = *pui32LinDataAddr++;
			case 2:
			*pui32LinPortAddr++ = *pui32LinDataAddr++;
			case 1:
			*pui32LinPortAddr++ = *pui32LinDataAddr++;
#else
			case 32:
			*pui32LinPortAddrBase = *pui32LinDataAddr++;
			case 31:
			*pui32LinPortAddrBase = *pui32LinDataAddr++;
			case 30:
			*pui32LinPortAddrBase = *pui32LinDataAddr++;
			case 29:
			*pui32LinPortAddrBase = *pui32LinDataAddr++;
			case 28:
			*pui32LinPortAddrBase = *pui32LinDataAddr++;
			case 27:
			*pui32LinPortAddrBase = *pui32LinDataAddr++;
			case 26:
			*pui32LinPortAddrBase = *pui32LinDataAddr++;
			case 25:
			*pui32LinPortAddrBase = *pui32LinDataAddr++;
			case 24:
			*pui32LinPortAddrBase = *pui32LinDataAddr++;
			case 23:
			*pui32LinPortAddrBase = *pui32LinDataAddr++;
			case 22:
			*pui32LinPortAddrBase = *pui32LinDataAddr++;
			case 21:
			*pui32LinPortAddrBase = *pui32LinDataAddr++;
			case 20:
			*pui32LinPortAddrBase = *pui32LinDataAddr++;
			case 19:
			*pui32LinPortAddrBase = *pui32LinDataAddr++;
			case 18:
			*pui32LinPortAddrBase = *pui32LinDataAddr++;
			case 17:
			*pui32LinPortAddrBase = *pui32LinDataAddr++;
			case 16:
			*pui32LinPortAddrBase = *pui32LinDataAddr++;
			case 15:
			*pui32LinPortAddrBase = *pui32LinDataAddr++;
			case 14:
			*pui32LinPortAddrBase = *pui32LinDataAddr++;
			case 13:
			*pui32LinPortAddrBase = *pui32LinDataAddr++;
			case 12:
			*pui32LinPortAddrBase = *pui32LinDataAddr++;
			case 11:
			*pui32LinPortAddrBase = *pui32LinDataAddr++;
			case 10:
			*pui32LinPortAddrBase = *pui32LinDataAddr++;
			case 9:
			*pui32LinPortAddrBase = *pui32LinDataAddr++;
			case 8:
			*pui32LinPortAddrBase = *pui32LinDataAddr++;
			case 7:
			*pui32LinPortAddrBase = *pui32LinDataAddr++;
			case 6:
			*pui32LinPortAddrBase = *pui32LinDataAddr++;
			case 5:
			*pui32LinPortAddrBase = *pui32LinDataAddr++;
			case 4:
			*pui32LinPortAddrBase = *pui32LinDataAddr++;
			case 3:
			*pui32LinPortAddrBase = *pui32LinDataAddr++;
			case 2:
			*pui32LinPortAddrBase = *pui32LinDataAddr++;
			case 1:
			*pui32LinPortAddrBase = *pui32LinDataAddr++;
#endif
		}
	}

#if defined(SGX2D_INCREMENTING_SP_WRITES)
	
	*pui32Offset += ui32Bytes;
#endif

	return PVRSRV_OK;
}

IMG_BOOL SGX2DProcessBlit(IMG_HANDLE		hCmdCookie,
							IMG_UINT32		ui32DataSize,
							IMG_VOID		*pvData)
{
	PVRSRV_BLT_CMD_INFO		*psBltCmd;
	PVRSRV_SGXDEV_INFO		*psDevInfo;
	IMG_UINT32			ui32BytesRequired;
	IMG_UINT32			ui32BytesObtained = 0;
	IMG_BOOL			bError = IMG_TRUE;
	PVRSRV_ERROR			eError;

	PVR_DPF((PVR_DBG_CALLTRACE, "SGX2DProcessBlit: Start"));

	psBltCmd = (PVRSRV_BLT_CMD_INFO*)pvData;

	
	if (psBltCmd == IMG_NULL || psBltCmd->ui32CmdSize != ui32DataSize)
	{
		PVR_DPF((PVR_DBG_ERROR,"ProcessBlit: Data packet size is incorrect"));
		return IMG_FALSE;
	}

	
	psDevInfo = psBltCmd->psDevInfo;

	if (psDevInfo->h2DCmdCookie != IMG_NULL)
	{
		return IMG_FALSE;
	}

	
	if (psDevInfo->b2DHWRecoveryInProgress)
	{
		psDevInfo->h2DCmdCookie = hCmdCookie;
		SGX2DHardwareKick(psDevInfo);
		return IMG_TRUE;
	}

	
	if (SGX2DAcquireSlavePort(psDevInfo, IMG_FALSE) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "ProcessBlit: Couldn't acquire slaveport"));
		return IMG_FALSE;
	}

#ifdef	FIXME
	

#endif

	
	if (psDevInfo->b2DHWRecoveryEndPending && SGX2D_2D_NOT_IDLE(psDevInfo))
	{
				psDevInfo->h2DCmdCookie = hCmdCookie;
				SGX2DHardwareKick(psDevInfo);
				PVR_ASSERT(bError);
				goto ErrorExit;
	}
	psDevInfo->b2DHWRecoveryEndPending = IMG_FALSE;

	ui32BytesRequired = psBltCmd->ui32DataByteSize + SGX2D_QUEUED_BLIT_PAD;

	
	eError = SGX2DAcquireFifoSpace(psDevInfo, ui32BytesRequired,	&ui32BytesObtained);
	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "ProcessBlit: Get Fifo Space failed"));
		bError = IMG_FALSE;
		goto ErrorExit;
	}

	
	SGX2DWriteSlavePortBatch(psDevInfo,
							 psBltCmd->aui32BltData,
							 psBltCmd->ui32DataByteSize);

	
	psDevInfo->h2DCmdCookie = hCmdCookie;

	
	SGX2DWriteSlavePort(psDevInfo, SGX2D_FLUSH_BH);

	PVR_ASSERT(bError);
ErrorExit:

	
	if(SGX2DReleaseSlavePort(psDevInfo) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SGX2DReleaseSlavePort: failed to release slaveport"));
	}

	PVR_DPF((PVR_DBG_CALLTRACE, "SGX2DProcessBlit: Exit.  Error %d", (int)bError));

	return bError;
}

IMG_VOID SGX2DHandle2DComplete(PVRSRV_SGXDEV_INFO *psDevInfo)
{
	IMG_HANDLE hCmdCookie;

	PVR_DPF((PVR_DBG_CALLTRACE, "SGX2DHandle2DComplete: Start"));

	hCmdCookie = psDevInfo->h2DCmdCookie;
	psDevInfo->h2DCmdCookie = IMG_NULL;

	
	if (hCmdCookie != IMG_NULL)
	{
		PVRSRVCommandCompleteKM(hCmdCookie, IMG_TRUE);
	}

	PVR_DPF((PVR_DBG_CALLTRACE, "SGX2DHandle2DComplete: Exit"));
}

IMG_EXPORT
PVRSRV_ERROR SGX2DQueueBlitKM(PVRSRV_SGXDEV_INFO		*psDevInfo,
							  PVRSRV_KERNEL_SYNC_INFO	*psDstSync,
							  IMG_UINT32				ui32NumSrcSyncs,
							  PVRSRV_KERNEL_SYNC_INFO	*apsSrcSync[],
							  IMG_UINT32				ui32DataByteSize,
							  IMG_UINT32				*pui32BltData)
{
#if defined(NO_HARDWARE)
	PVR_UNREFERENCED_PARAMETER(psDevInfo);
	PVR_UNREFERENCED_PARAMETER(psDstSync);
	PVR_UNREFERENCED_PARAMETER(ui32NumSrcSyncs);
	PVR_UNREFERENCED_PARAMETER(apsSrcSync);
	PVR_UNREFERENCED_PARAMETER(ui32DataByteSize);
	PVR_UNREFERENCED_PARAMETER(pui32BltData);

	return PVRSRV_OK;
#else
	PVRSRV_COMMAND		*psCommand;
	PVRSRV_BLT_CMD_INFO	*psBltCmd;
	IMG_UINT32		ui32CmdByteSize;
	IMG_UINT32		i;
	PVRSRV_ERROR		eError;

	PVR_DPF((PVR_DBG_CALLTRACE, "SGX2DQueueBlitKM: Start"));

	
	if (psDevInfo->b2DHWRecoveryInProgress == IMG_TRUE)
	{
		return PVRSRV_ERROR_CANNOT_GET_QUEUE_SPACE;
	}

	
	if ((ui32DataByteSize + SGX2D_QUEUED_BLIT_PAD) > psDevInfo->ui322DFifoSize)
	{
		PVR_DPF((PVR_DBG_ERROR, "SGX2DQueueBlitKM: Blit too big for FIFO. Blit size: %d (+ padding %d), FIFO size: %d", ui32DataByteSize, SGX2D_QUEUED_BLIT_PAD, psDevInfo->ui322DFifoSize));

		return PVRSRV_ERROR_CMD_TOO_BIG;
	}

	ui32CmdByteSize = sizeof(PVRSRV_BLT_CMD_INFO)
				+ ui32DataByteSize
				- sizeof(IMG_UINT32);

	eError = PVRSRVInsertCommandKM((PVRSRV_QUEUE_INFO *)psDevInfo->h2DQueue,
					&psCommand,
					SYS_DEVICE_SGX,	
					SGX_2D_BLT_COMMAND,
					(psDstSync == IMG_NULL) ? 0 : 1,
					&psDstSync,
					ui32NumSrcSyncs,
					apsSrcSync,
					ui32CmdByteSize );
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "SGX2DQueueBlitKM: PVRSRVInsertCommandKM failed. Error %d", eError));
#ifdef DEBUG
		if (eError == PVRSRV_ERROR_CANNOT_GET_QUEUE_SPACE)
		{
			if (!SGX2DIsBusy(psDevInfo))
			{
				
				PVR_DPF((PVR_DBG_ERROR, "SGX2DQueueBlitKM: 2D core not busy, command queue full - lockup suspected"));
			}
		}
#endif
		return eError;
	}

	
	psBltCmd		= (PVRSRV_BLT_CMD_INFO*) psCommand->pvData;
	psBltCmd->ui32CmdSize	= ui32CmdByteSize;
	psBltCmd->psDevInfo	= psDevInfo;

	
	psBltCmd->psDstSync = psDstSync;

	psBltCmd->ui32NumSrcSyncInfos = ui32NumSrcSyncs;
	for(i = 0; i < ui32NumSrcSyncs; i++)
	{
		
		psBltCmd->apsSrcSync[i] = apsSrcSync[i];
	}

	if (pui32BltData != IMG_NULL)
	{
		for(i = 0; i < (ui32DataByteSize>>2); i++)
		{
			psBltCmd->aui32BltData[i] = pui32BltData[i];
		}
	}

	psBltCmd->ui32DataByteSize = ui32DataByteSize;

	
	eError = PVRSRVSubmitCommandKM((PVRSRV_QUEUE_INFO *)psDevInfo->h2DQueue, psCommand);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "SGX2DQueueBlitKM: PVRSRVSubmitCommandKM failed. Error %d", eError));
	}

	SGX2DKick(psDevInfo);

	PVR_DPF((PVR_DBG_CALLTRACE, "SGX2DQueueBlitKM: Exit. Error: %d", eError));

	return eError;
#endif	
}

#if defined(SGX2D_DIRECT_BLITS)
IMG_EXPORT
PVRSRV_ERROR SGX2DDirectBlitKM(PVRSRV_SGXDEV_INFO	*psDevInfo,
							   IMG_UINT32			ui32DataByteSize,
							   IMG_UINT32			*pui32BltData)
{
#if defined(NO_HARDWARE)
	PVR_UNREFERENCED_PARAMETER(psDevInfo);
	PVR_UNREFERENCED_PARAMETER(ui32DataByteSize);
	PVR_UNREFERENCED_PARAMETER(pui32BltData);

	return PVRSRV_OK;
#else
	PVRSRV_ERROR	eError;
	PVRSRV_ERROR	eSrvErr;
	
	IMG_UINT32		ui32CmdByteSize = ui32DataByteSize + 4;
	IMG_BOOL		bStart = IMG_FALSE;
	IMG_UINT32		uiStart = 0;

	PVR_DPF((PVR_DBG_CALLTRACE, "SGX2DDirectBlitKM: Start"));

	
	if (psDevInfo->b2DHWRecoveryInProgress == IMG_TRUE)
	{
		return PVRSRV_ERROR_FIFO_SPACE;
	}

	
	if ( ui32CmdByteSize > psDevInfo->ui322DFifoSize)
	{
		PVR_DPF((PVR_DBG_ERROR, "SGX2DDirectBlitKM: Blit too big for FIFO. Blit size: %d (+ padding %d), FIFO size: %d", ui32DataByteSize, 4, psDevInfo->ui322DFifoSize));

		return PVRSRV_ERROR_CMD_TOO_BIG;
	}

	eSrvErr = SGX2DAcquireSlavePort (psDevInfo, IMG_TRUE);
	if (eSrvErr != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "SGX2DDirectBlitKM: Cannot acquire slaveport. Error %d", eSrvErr));
		return eSrvErr;
	}

#ifdef	FIXME
	

#endif
	do
	{
		eSrvErr = SGX2DAcquireFifoSpace(psDevInfo,
					  ui32CmdByteSize,
					  IMG_NULL);
		if (eSrvErr == PVRSRV_OK)
		{
			break;
		}

		if (bStart == IMG_FALSE)
		{
			uiStart = OSClockus();
			bStart = IMG_TRUE;
		}

		OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
	} while ((OSClockus() - uiStart) < MAX_HW_TIME_US);

	if (eSrvErr != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "SGX2DDirectBlitKM: Cannot acquire FIFO space. Error %d", eSrvErr));
		
		eError = eSrvErr;
	}
	else
	{
		
		if (psDevInfo->b2DHWRecoveryEndPending && SGX2D_2D_NOT_IDLE(psDevInfo))
		{
			eError = PVRSRV_ERROR_FIFO_SPACE;
		}
		else
		{
			eError = PVRSRV_OK;

			psDevInfo->b2DHWRecoveryEndPending = IMG_FALSE;

			SGX2DWriteSlavePortBatch(psDevInfo, pui32BltData, ui32DataByteSize);

			SGX2DWriteSlavePort(psDevInfo, SGX2D_FENCE_BH);
		}
	}

	eSrvErr = SGX2DReleaseSlavePort(psDevInfo);
	if (eSrvErr != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "SGX2DDirectBlitKM: Cannot release slave port.  Error %d", eSrvErr));

		if (eError != PVRSRV_OK)
		{
			eError = eSrvErr;
		}
	}

	PVR_DPF((PVR_DBG_CALLTRACE, "SGX2DDirectBlitKM: Exit.  Error: %d", eError));

	
	SGX2DKick(psDevInfo);

	return eError;
#endif	
}
#endif 

#endif 

#if defined(SGX_FEATURE_2D_HARDWARE) || defined(PVR2D_ALT_2DHW)
#ifdef INLINE_IS_PRAGMA
#pragma inline(SGX2DQuerySyncOpsComplete)
#endif
static INLINE
IMG_BOOL SGX2DQuerySyncOpsComplete(PVRSRV_KERNEL_SYNC_INFO *psSyncInfo)
{
	PVRSRV_SYNC_DATA *psSyncData = psSyncInfo->psSyncData;

	return (IMG_BOOL)(
					  (psSyncData->ui32ReadOpsComplete == psSyncData->ui32ReadOpsPending) &&
					  (psSyncData->ui32WriteOpsComplete == psSyncData->ui32WriteOpsPending)
					 );
}

IMG_EXPORT
PVRSRV_ERROR SGX2DQueryBlitsCompleteKM(PVRSRV_SGXDEV_INFO	*psDevInfo,
									   PVRSRV_KERNEL_SYNC_INFO *psSyncInfo,
									   IMG_BOOL bWaitForComplete)
{
	IMG_BOOL	bStart = IMG_FALSE;
	IMG_UINT32	uiStart = 0;

	PVR_UNREFERENCED_PARAMETER(psDevInfo);

	PVR_DPF((PVR_DBG_CALLTRACE, "SGX2DQueryBlitsCompleteKM: Start"));

	if(SGX2DQuerySyncOpsComplete(psSyncInfo))
	{
		
		PVR_DPF((PVR_DBG_CALLTRACE, "SGX2DQueryBlitsCompleteKM: No wait. Blits complete."));
		return PVRSRV_OK;
	}

	
	if (!bWaitForComplete)
	{
		
		PVR_DPF((PVR_DBG_CALLTRACE, "SGX2DQueryBlitsCompleteKM: No wait. Ops pending."));
		return PVRSRV_ERROR_CMD_NOT_PROCESSED;
	}

	 
	PVR_DPF((PVR_DBG_MESSAGE, "SGX2DQueryBlitsCompleteKM: Ops pending. Start polling."));
	do
	{
		OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);

		if(SGX2DQuerySyncOpsComplete(psSyncInfo))
		{
			
			PVR_DPF((PVR_DBG_CALLTRACE, "SGX2DQueryBlitsCompleteKM: Wait over.  Blits complete."));
			return PVRSRV_OK;
		}

		if (bStart == IMG_FALSE)
		{
			uiStart = OSClockus();
			bStart = IMG_TRUE;
		}

		OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
	} while ((OSClockus() - uiStart) < MAX_HW_TIME_US);

	
	PVR_DPF((PVR_DBG_ERROR,"SGX2DQueryBlitsCompleteKM: Timed out. Ops pending."));

#if defined(DEBUG)
	{
		PVRSRV_SYNC_DATA *psSyncData = psSyncInfo->psSyncData;

		PVR_TRACE(("SGX2DQueryBlitsCompleteKM: Syncinfo: %p, Syncdata: %p", psSyncInfo, psSyncData));

		PVR_TRACE(("SGX2DQueryBlitsCompleteKM: Read ops complete: %d, Read ops pending: %d", psSyncData->ui32ReadOpsComplete, psSyncData->ui32ReadOpsPending));
		PVR_TRACE(("SGX2DQueryBlitsCompleteKM: Write ops complete: %d, Write ops pending: %d", psSyncData->ui32WriteOpsComplete, psSyncData->ui32WriteOpsPending));

	}
#endif

	return PVRSRV_ERROR_TIMEOUT;
}
#endif 

#if defined(SGX_FEATURE_2D_HARDWARE)
PVRSRV_ERROR SGX2DInit(PVRSRV_SGXDEV_INFO *psDevInfo)
{
	PVRSRV_ERROR eError;

	
	PVR_ASSERT(psDevInfo->ui322DFifoSize == 0);
	psDevInfo->ui322DFifoSize =  SGX2DFifoFreeSpace(psDevInfo);

	PVR_TRACE(("SGX2DInit: 2D FIFO size: %d", psDevInfo->ui322DFifoSize));

	
	PVR_ASSERT(psDevInfo->s2DSlavePortKM.pui32Offset == 0);
	PVR_ASSERT(psDevInfo->ui322DFifoOffset == 0);
	psDevInfo->s2DSlavePortKM.pui32Offset = &psDevInfo->ui322DFifoOffset;

	PVR_ASSERT(psDevInfo->h2DQueue == IMG_NULL);
	eError = PVRSRVCreateCommandQueueKM(SGX2D_COMMAND_QUEUE_SIZE,
						(PVRSRV_QUEUE_INFO **)&psDevInfo->h2DQueue);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "SGX2DInit: PVRSRVCreateCommandQueueKM failed (%d)", eError));

		return eError;
	}

	PVR_ASSERT(psDevInfo->h2DCmdCookie == IMG_NULL);
	PVR_ASSERT(!psDevInfo->b2DHWRecoveryInProgress);
	PVR_ASSERT(!psDevInfo->b2DHWRecoveryEndPending);
	PVR_ASSERT(psDevInfo->ui322DCompletedBlits == 0);

	return PVRSRV_OK;
}

PVRSRV_ERROR SGX2DDeInit(PVRSRV_SGXDEV_INFO      *psDevInfo)
{
	PVRSRV_ERROR eError;

	if (psDevInfo->h2DQueue != IMG_NULL)
	{
		eError = PVRSRVDestroyCommandQueueKM((PVRSRV_QUEUE_INFO *)psDevInfo->h2DQueue);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "SGX2DDeInit: PVRSRVDestroyCommandQueueKM failed (%d)", eError));

			return eError;
		}
	}

	return PVRSRV_OK;
}

#endif 

