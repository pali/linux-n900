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

#include "services_headers.h"

#if defined(__linux__) && defined(__KERNEL__)
#include "proc.h"

static int
QueuePrintCommands (PVRSRV_QUEUE_INFO * psQueue, char * buffer, size_t size)
{
	off_t off = 0;
	int cmds = 0;
	IMG_UINT32 ui32ReadOffset  = psQueue->ui32ReadOffset;
	IMG_UINT32 ui32WriteOffset = psQueue->ui32WriteOffset;
	PVRSRV_COMMAND * psCmd;

	while (ui32ReadOffset != ui32WriteOffset)
	{
		psCmd= (PVRSRV_COMMAND *)((IMG_UINT32)psQueue->pvLinQueueKM + ui32ReadOffset);

		off = printAppend(buffer, size, off, "%p %p  %5lu  %6lu  %3lu  %5lu   %2lu   %2lu    %3lu  \n",
							psQueue,
					 		psCmd,
					 		psCmd->ui32ProcessID,
							psCmd->CommandType,
							psCmd->ui32CmdSize,
							psCmd->ui32DevIndex,
							psCmd->ui32DstSyncCount,
							psCmd->ui32SrcSyncCount,
							psCmd->ui32DataSize);
		
		ui32ReadOffset += psCmd->ui32CmdSize;
		ui32ReadOffset &= psQueue->ui32QueueSize - 1;
		cmds++;
	}
	if (cmds == 0)
		off = printAppend(buffer, size, off, "%p <empty>\n", psQueue);
	return off;
} 


off_t
QueuePrintQueues (char * buffer, size_t size, off_t off)
{
	SYS_DATA * psSysData;
	PVRSRV_QUEUE_INFO * psQueue;
	
	if (SysAcquireData(&psSysData) != PVRSRV_OK)
		return END_OF_FILE;

	 if (!off)
		  return printAppend (buffer, size, 0,
								"Command Queues\n"
								"Queue    CmdPtr      Pid Command Size DevInd  DSC  SSC  #Data ...\n");

	
 
	for (psQueue = psSysData->psQueueList; --off && psQueue; psQueue = psQueue->psNextKM)
		;

	return psQueue ? QueuePrintCommands (psQueue, buffer, size) : END_OF_FILE;
} 
#endif 

#define GET_SPACE_IN_CMDQ(psQueue)										\
	(((psQueue->ui32ReadOffset - psQueue->ui32WriteOffset)				\
	+ (psQueue->ui32QueueSize - 1)) & (psQueue->ui32QueueSize - 1))

#define UPDATE_QUEUE_WOFF(psQueue, ui32Size)							\
	psQueue->ui32WriteOffset = (psQueue->ui32WriteOffset + ui32Size)	\
	& (psQueue->ui32QueueSize - 1);

#define SYNCOPS_STALE(ui32OpsComplete, ui32OpsPending)					\
	(ui32OpsComplete >= ui32OpsPending)

IMG_UINT32 NearestPower2(IMG_UINT32 ui32Value)
{
	IMG_UINT32 ui32Temp, ui32Result = 1;

	if(!ui32Value)
		return 0;

	ui32Temp = ui32Value - 1;
	while(ui32Temp)
	{
		ui32Result <<= 1;
		ui32Temp >>= 1;
	}

	return ui32Result;
}


IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVCreateCommandQueueKM(IMG_UINT32 ui32QueueSize,
													 PVRSRV_QUEUE_INFO **ppsQueueInfo)
{
	PVRSRV_QUEUE_INFO	*psQueueInfo;
	IMG_UINT32			ui32Power2QueueSize = NearestPower2(ui32QueueSize);
	SYS_DATA			*psSysData;
	PVRSRV_ERROR		eError;
	IMG_HANDLE			hMemBlock;

	eError = SysAcquireData(&psSysData);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	
	if(OSAllocMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
					 sizeof(PVRSRV_QUEUE_INFO),
					 (IMG_VOID **)&psQueueInfo, &hMemBlock) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVCreateCommandQueueKM: Failed to alloc queue struct"));
		goto ErrorExit;
	}
	OSMemSet(psQueueInfo, 0, sizeof(PVRSRV_QUEUE_INFO));

	psQueueInfo->hMemBlock[0] = hMemBlock;
	psQueueInfo->ui32ProcessID = OSGetCurrentProcessIDKM();

	
	if(OSAllocMem(PVRSRV_OS_NON_PAGEABLE_HEAP, 
					 ui32Power2QueueSize + PVRSRV_MAX_CMD_SIZE, 
					 &psQueueInfo->pvLinQueueKM, &hMemBlock) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVCreateCommandQueueKM: Failed to alloc queue buffer"));
		goto ErrorExit;
	}

	psQueueInfo->hMemBlock[1] = hMemBlock;
	psQueueInfo->pvLinQueueUM = psQueueInfo->pvLinQueueKM;

	
	PVR_ASSERT(psQueueInfo->ui32ReadOffset == 0);
	PVR_ASSERT(psQueueInfo->ui32WriteOffset == 0);

	psQueueInfo->ui32QueueSize = ui32Power2QueueSize;

	
	if (psSysData->psQueueList == IMG_NULL)
	{
		eError = OSCreateResource(&psSysData->sQProcessResource);
		if (eError != PVRSRV_OK)
		{
			goto ErrorExit;
		}
	}
	
	
	if (OSLockResource(&psSysData->sQProcessResource, 
							KERNEL_ID) != PVRSRV_OK)
	{
		goto ErrorExit;
	}

	psQueueInfo->psNextKM = psSysData->psQueueList;
	psSysData->psQueueList = psQueueInfo;

	if (OSUnlockResource(&psSysData->sQProcessResource, KERNEL_ID) != PVRSRV_OK)
	{
		goto ErrorExit;
	}

	*ppsQueueInfo = psQueueInfo;

	return PVRSRV_OK;
	
ErrorExit:

	if(psQueueInfo)
	{
		if(psQueueInfo->pvLinQueueKM)
		{
			OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
						psQueueInfo->ui32QueueSize,
						psQueueInfo->pvLinQueueKM,
						psQueueInfo->hMemBlock[1]);
		}

		OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP, 
					sizeof(PVRSRV_QUEUE_INFO), 
					psQueueInfo, 
					psQueueInfo->hMemBlock[0]);
	}

	return PVRSRV_ERROR_GENERIC;
}


IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVDestroyCommandQueueKM(PVRSRV_QUEUE_INFO *psQueueInfo)
{
	PVRSRV_QUEUE_INFO	*psQueue;
	SYS_DATA			*psSysData;
	PVRSRV_ERROR		eError;
	IMG_BOOL			bTimeout = IMG_TRUE;
	IMG_BOOL			bStart = IMG_FALSE;
	IMG_UINT32			uiStart = 0;

	eError = SysAcquireData(&psSysData);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	psQueue = psSysData->psQueueList;

	do
	{
		if(psQueueInfo->ui32ReadOffset == psQueueInfo->ui32WriteOffset)
		{
			bTimeout = IMG_FALSE;
			break;
		}

		if (bStart == IMG_FALSE)
		{
			bStart = IMG_TRUE;
			uiStart = OSClockus();
		}
		OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
	} while ((OSClockus() - uiStart) < MAX_HW_TIME_US);

	if (bTimeout)
	{
		
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVDestroyCommandQueueKM : Failed to empty queue"));
		eError = PVRSRV_ERROR_CANNOT_FLUSH_QUEUE;
	}

	
	eError = OSLockResource(&psSysData->sQProcessResource, 
								KERNEL_ID);
	if (eError != PVRSRV_OK)
	{
		goto ErrorExit;
	}
	
	if(psQueue == psQueueInfo)
	{
		psSysData->psQueueList = psQueueInfo->psNextKM;

		OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
					psQueueInfo->ui32QueueSize,
					psQueueInfo->pvLinQueueKM,
					psQueueInfo->hMemBlock[1]);
		OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
					sizeof(PVRSRV_QUEUE_INFO),
					psQueueInfo,
					psQueueInfo->hMemBlock[0]);
	}
	else
	{
		while(psQueue)
		{
			if(psQueue->psNextKM == psQueueInfo)
			{
				psQueue->psNextKM = psQueueInfo->psNextKM;

				OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
							psQueueInfo->ui32QueueSize,
							psQueueInfo->pvLinQueueKM,
							psQueueInfo->hMemBlock[1]);
				OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
							sizeof(PVRSRV_QUEUE_INFO),
							psQueueInfo,
							psQueueInfo->hMemBlock[0]);
				break;
			}
			psQueue = psQueue->psNextKM;
		}

		if(!psQueue)
		{
			eError = OSUnlockResource(&psSysData->sQProcessResource, KERNEL_ID);
			if (eError != PVRSRV_OK)
			{
				goto ErrorExit;
			}
			eError = PVRSRV_ERROR_INVALID_PARAMS;
			goto ErrorExit;
		}
	}

	
	eError = OSUnlockResource(&psSysData->sQProcessResource, KERNEL_ID);
	if (eError != PVRSRV_OK)
	{
		goto ErrorExit;
	}

	
	if (psSysData->psQueueList == IMG_NULL)
	{
		eError = OSDestroyResource(&psSysData->sQProcessResource);
		if (eError != PVRSRV_OK)
		{
			goto ErrorExit;
		}
	}
	
ErrorExit:

	return eError;	
}


IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVGetQueueSpaceKM(PVRSRV_QUEUE_INFO *psQueue,
												IMG_UINT32 ui32ParamSize,
												IMG_VOID **ppvSpace)
{
	IMG_BOOL bTimeout = IMG_TRUE;
	IMG_BOOL bStart = IMG_FALSE;
	IMG_UINT32 uiStart = 0, uiCurrent = 0;

	
	ui32ParamSize =  (ui32ParamSize+3) & 0xFFFFFFFC;

	if (ui32ParamSize > PVRSRV_MAX_CMD_SIZE)
	{
		PVR_DPF((PVR_DBG_WARNING,"PVRSRVGetQueueSpace: max command size is %d bytes", PVRSRV_MAX_CMD_SIZE));
		return PVRSRV_ERROR_CMD_TOO_BIG;
	}

	do
	{
		if (GET_SPACE_IN_CMDQ(psQueue) > ui32ParamSize)
		{
			bTimeout = IMG_FALSE;
			break;	
		}
		
		if (bStart == IMG_FALSE)
		{
			bStart = IMG_TRUE;
			uiStart = OSClockus();
		}
		OSWaitus(MAX_HW_TIME_US/WAIT_TRY_COUNT);
		
		uiCurrent = OSClockus();
		if (uiCurrent < uiStart)
		{
			
			uiStart = 0;
		}
	} while ((uiCurrent - uiStart) < MAX_HW_TIME_US);

	if (bTimeout == IMG_TRUE)
	{
		*ppvSpace = IMG_NULL;

		return PVRSRV_ERROR_CANNOT_GET_QUEUE_SPACE;
	}
	else
	{
		*ppvSpace = (IMG_VOID *)(psQueue->ui32WriteOffset + (IMG_UINT32)psQueue->pvLinQueueUM);
	}

	return PVRSRV_OK;
}


IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVInsertCommandKM(PVRSRV_QUEUE_INFO	*psQueue,
												PVRSRV_COMMAND		**ppsCommand,
												IMG_UINT32			ui32DevIndex,
												IMG_UINT16			CommandType,
												IMG_UINT32			ui32DstSyncCount,
												PVRSRV_KERNEL_SYNC_INFO	*apsDstSync[],
												IMG_UINT32			ui32SrcSyncCount,
												PVRSRV_KERNEL_SYNC_INFO	*apsSrcSync[],
												IMG_UINT32			ui32DataByteSize )
{
	PVRSRV_ERROR 	eError;
	PVRSRV_COMMAND	*psCommand;
	IMG_UINT32		ui32CommandSize;
	IMG_UINT32		i;

	
	ui32DataByteSize = (ui32DataByteSize + 3) & 0xFFFFFFFC;

	
	ui32CommandSize = sizeof(PVRSRV_COMMAND) 
					+ ((ui32DstSyncCount + ui32SrcSyncCount) * sizeof(PVRSRV_SYNC_OBJECT))
					+ ui32DataByteSize;

	
	eError = PVRSRVGetQueueSpaceKM (psQueue, ui32CommandSize, (IMG_VOID**)&psCommand);
	if(eError != PVRSRV_OK)
	{
		return eError;
	}

	psCommand->ui32ProcessID	= OSGetCurrentProcessIDKM();

	
	psCommand->ui32CmdSize		= ui32CommandSize; 
	psCommand->ui32DevIndex 	= ui32DevIndex;
	psCommand->CommandType 		= CommandType;
	psCommand->ui32DstSyncCount	= ui32DstSyncCount;
	psCommand->ui32SrcSyncCount	= ui32SrcSyncCount;
	psCommand->psDstSync		= (PVRSRV_SYNC_OBJECT*)(((IMG_UINT8 *)psCommand) + sizeof(PVRSRV_COMMAND));	


	psCommand->psSrcSync		= (PVRSRV_SYNC_OBJECT*)(((IMG_UINT8 *)psCommand->psDstSync) 
								+ (ui32DstSyncCount * sizeof(PVRSRV_SYNC_OBJECT)));

	psCommand->pvData			= (PVRSRV_SYNC_OBJECT*)(((IMG_UINT8 *)psCommand->psSrcSync) 
								+ (ui32SrcSyncCount * sizeof(PVRSRV_SYNC_OBJECT)));

	psCommand->ui32DataSize		= ui32DataByteSize;

	
	for (i=0; i<ui32DstSyncCount; i++)
	{
		psCommand->psDstSync[i].psKernelSyncInfoKM = apsDstSync[i];
		psCommand->psDstSync[i].ui32WriteOpsPending = PVRSRVGetWriteOpsPending(apsDstSync[i], IMG_FALSE);
		psCommand->psDstSync[i].ui32ReadOpsPending = PVRSRVGetReadOpsPending(apsDstSync[i], IMG_FALSE);
	}

	
	for (i=0; i<ui32SrcSyncCount; i++)
	{
		psCommand->psSrcSync[i].psKernelSyncInfoKM = apsSrcSync[i];
		psCommand->psSrcSync[i].ui32WriteOpsPending = PVRSRVGetWriteOpsPending(apsSrcSync[i], IMG_TRUE);
		psCommand->psSrcSync[i].ui32ReadOpsPending = PVRSRVGetReadOpsPending(apsSrcSync[i], IMG_TRUE);	
	}

	
	*ppsCommand = psCommand;

	return PVRSRV_OK;
}


IMG_EXPORT
PVRSRV_ERROR IMG_CALLCONV PVRSRVSubmitCommandKM(PVRSRV_QUEUE_INFO *psQueue,
												PVRSRV_COMMAND *psCommand)
{
	
	if (psCommand->ui32DstSyncCount > 0)
	{
		psCommand->psDstSync = (PVRSRV_SYNC_OBJECT*)(((IMG_UINT8 *)psQueue->pvLinQueueKM) 
									+ psQueue->ui32WriteOffset + sizeof(PVRSRV_COMMAND));
	}

	if (psCommand->ui32SrcSyncCount > 0)
	{
		psCommand->psSrcSync = (PVRSRV_SYNC_OBJECT*)(((IMG_UINT8 *)psQueue->pvLinQueueKM) 
									+ psQueue->ui32WriteOffset + sizeof(PVRSRV_COMMAND)
									+ (psCommand->ui32DstSyncCount * sizeof(PVRSRV_SYNC_OBJECT)));
	}

	psCommand->pvData = (PVRSRV_SYNC_OBJECT*)(((IMG_UINT8 *)psQueue->pvLinQueueKM) 
									+ psQueue->ui32WriteOffset + sizeof(PVRSRV_COMMAND)
									+ (psCommand->ui32DstSyncCount * sizeof(PVRSRV_SYNC_OBJECT))
									+ (psCommand->ui32SrcSyncCount * sizeof(PVRSRV_SYNC_OBJECT)));

	
	UPDATE_QUEUE_WOFF(psQueue, psCommand->ui32CmdSize);
	
	return PVRSRV_OK;
}



IMG_EXPORT
PVRSRV_ERROR PVRSRVProcessCommand(SYS_DATA			*psSysData,
								  PVRSRV_COMMAND	*psCommand,
								  IMG_BOOL			bFlush)
{
	PVRSRV_SYNC_OBJECT		*psWalkerObj;
	PVRSRV_SYNC_OBJECT		*psEndObj;
	IMG_UINT32				i;
	COMMAND_COMPLETE_DATA	*psCmdCompleteData;
	PVRSRV_ERROR			eError = PVRSRV_OK;
	IMG_UINT32				ui32WriteOpsComplete;
	IMG_UINT32				ui32ReadOpsComplete;

	
	psWalkerObj = psCommand->psDstSync;
	psEndObj = psWalkerObj + psCommand->ui32DstSyncCount;
	while (psWalkerObj < psEndObj)
	{
		PVRSRV_SYNC_DATA *psSyncData = psWalkerObj->psKernelSyncInfoKM->psSyncData;

		ui32WriteOpsComplete = psSyncData->ui32WriteOpsComplete;
		ui32ReadOpsComplete = psSyncData->ui32ReadOpsComplete;
		
		if ((ui32WriteOpsComplete != psWalkerObj->ui32WriteOpsPending)
		||	(ui32ReadOpsComplete != psWalkerObj->ui32ReadOpsPending))
		{
			if (!bFlush ||
				!SYNCOPS_STALE(ui32WriteOpsComplete, psWalkerObj->ui32WriteOpsPending) ||
				!SYNCOPS_STALE(ui32ReadOpsComplete, psWalkerObj->ui32ReadOpsPending))
			{
				return PVRSRV_ERROR_FAILED_DEPENDENCIES;
			}
		}

		psWalkerObj++;
	}

	
	psWalkerObj = psCommand->psSrcSync;
	psEndObj = psWalkerObj + psCommand->ui32SrcSyncCount;
	while (psWalkerObj < psEndObj)
	{
		PVRSRV_SYNC_DATA *psSyncData = psWalkerObj->psKernelSyncInfoKM->psSyncData;

		ui32ReadOpsComplete = psSyncData->ui32ReadOpsComplete;
		ui32WriteOpsComplete = psSyncData->ui32WriteOpsComplete;
		
		if ((ui32WriteOpsComplete != psWalkerObj->ui32WriteOpsPending)
		|| (ui32ReadOpsComplete != psWalkerObj->ui32ReadOpsPending))
		{
			if (!bFlush &&
				SYNCOPS_STALE(ui32WriteOpsComplete, psWalkerObj->ui32WriteOpsPending) &&
				SYNCOPS_STALE(ui32ReadOpsComplete, psWalkerObj->ui32ReadOpsPending))
			{
				PVR_DPF((PVR_DBG_WARNING,
						"PVRSRVProcessCommand: Stale syncops psSyncData:0x%x ui32WriteOpsComplete:0x%x ui32WriteOpsPending:0x%x",
						psSyncData, ui32WriteOpsComplete, psWalkerObj->ui32WriteOpsPending));
			}

			if (!bFlush ||
				!SYNCOPS_STALE(ui32WriteOpsComplete, psWalkerObj->ui32WriteOpsPending) ||
				!SYNCOPS_STALE(ui32ReadOpsComplete, psWalkerObj->ui32ReadOpsPending))
			{
				return PVRSRV_ERROR_FAILED_DEPENDENCIES;
			}
		}
		psWalkerObj++;
	}

	
	if (psCommand->ui32DevIndex >= SYS_DEVICE_COUNT)
	{
		PVR_DPF((PVR_DBG_ERROR,
					"PVRSRVProcessCommand: invalid DeviceType 0x%x",
					psCommand->ui32DevIndex));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	
	psCmdCompleteData = psSysData->ppsCmdCompleteData[psCommand->ui32DevIndex][psCommand->CommandType];
	if (psCmdCompleteData->bInUse)
	{
		
		return PVRSRV_ERROR_FAILED_DEPENDENCIES;
	}

	
	psCmdCompleteData->bInUse = IMG_TRUE;

	
	psCmdCompleteData->ui32DstSyncCount = psCommand->ui32DstSyncCount;
	for (i=0; i<psCommand->ui32DstSyncCount; i++)
	{
		psCmdCompleteData->psDstSync[i] = psCommand->psDstSync[i];
	}

	
	psCmdCompleteData->ui32SrcSyncCount = psCommand->ui32SrcSyncCount;
	for (i=0; i<psCommand->ui32SrcSyncCount; i++)
	{
		psCmdCompleteData->psSrcSync[i] = psCommand->psSrcSync[i];
	}

	









	if (psSysData->ppfnCmdProcList[psCommand->ui32DevIndex][psCommand->CommandType]((IMG_HANDLE)psCmdCompleteData, 
																				psCommand->ui32DataSize, 
																				psCommand->pvData) == IMG_FALSE)
	{
		


		psCmdCompleteData->bInUse = IMG_FALSE;
		eError = PVRSRV_ERROR_CMD_NOT_PROCESSED;
	}
	
	return eError;
}


IMG_EXPORT
PVRSRV_ERROR PVRSRVProcessQueues(IMG_UINT32	ui32CallerID,
								 IMG_BOOL	bFlush)
{
	PVRSRV_QUEUE_INFO 	*psQueue;
	SYS_DATA			*psSysData;
	PVRSRV_COMMAND 		*psCommand;
	PVRSRV_DEVICE_NODE	*psDeviceNode;
	PVRSRV_ERROR		eError;

	eError = SysAcquireData(&psSysData);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	
	psSysData->bReProcessQueues = IMG_FALSE;

	
	eError = OSLockResource(&psSysData->sQProcessResource,
							ui32CallerID);
	if(eError != PVRSRV_OK)
	{
		
		psSysData->bReProcessQueues = IMG_TRUE;

		
		if(ui32CallerID == ISR_ID)
		{
			if (bFlush)
			{
				PVR_DPF((PVR_DBG_ERROR,"PVRSRVProcessQueues: Couldn't acquire queue processing lock for FLUSH"));
			}
			else
			{
				PVR_DPF((PVR_DBG_MESSAGE,"PVRSRVProcessQueues: Couldn't acquire queue processing lock"));			
			}
		}
		else
		{
			PVR_DPF((PVR_DBG_MESSAGE,"PVRSRVProcessQueues: Queue processing lock-acquire failed when called from the Services driver."));
			PVR_DPF((PVR_DBG_MESSAGE,"                     This is due to MISR queue processing being interrupted by the Services driver."));
		}
		
		return PVRSRV_OK;
	}

	psQueue = psSysData->psQueueList;

	if(!psQueue)
	{
		PVR_DPF((PVR_DBG_MESSAGE,"No Queues installed - cannot process commands"));
	}

	if (bFlush)
	{
		PVRSRVSetDCState(DC_STATE_FLUSH_COMMANDS);
	}

	while (psQueue)
	{
		while (psQueue->ui32ReadOffset != psQueue->ui32WriteOffset)
		{
			psCommand = (PVRSRV_COMMAND*)((IMG_UINT32)psQueue->pvLinQueueKM + psQueue->ui32ReadOffset);

			if (PVRSRVProcessCommand(psSysData, psCommand, bFlush) == PVRSRV_OK)
			{
					
				UPDATE_QUEUE_ROFF(psQueue, psCommand->ui32CmdSize)
				
				if (bFlush)
				{
					continue;
				}
			}

			break;
		}
		psQueue = psQueue->psNextKM;
	}

	if (bFlush)
	{
		PVRSRVSetDCState(DC_STATE_NO_FLUSH_COMMANDS);
	}

	
	psDeviceNode = psSysData->psDeviceNodeList;
	while(psDeviceNode != IMG_NULL)
	{
		if (psDeviceNode->bReProcessDeviceCommandComplete &&
			psDeviceNode->pfnDeviceCommandComplete != IMG_NULL)
		{
			(*psDeviceNode->pfnDeviceCommandComplete)(psDeviceNode);
		}
		psDeviceNode = psDeviceNode->psNext;
	}

	
	OSUnlockResource(&psSysData->sQProcessResource, ui32CallerID);
	
	
	if(psSysData->bReProcessQueues)
	{
		return PVRSRV_ERROR_PROCESSING_BLOCKED;
	}
	
	return PVRSRV_OK;
}


IMG_EXPORT
IMG_VOID PVRSRVCommandCompleteKM(IMG_HANDLE hCmdCookie, IMG_BOOL bScheduleMISR)
{
	IMG_UINT32				i;
	COMMAND_COMPLETE_DATA	*psCmdCompleteData = (COMMAND_COMPLETE_DATA *)hCmdCookie;
	SYS_DATA				*psSysData;

	if (SysAcquireData(&psSysData) != PVRSRV_OK)
	{
		return;
	}

	
	for (i=0; i<psCmdCompleteData->ui32DstSyncCount; i++)
	{
		psCmdCompleteData->psDstSync[i].psKernelSyncInfoKM->psSyncData->ui32WriteOpsComplete++;
	}

	
	for (i=0; i<psCmdCompleteData->ui32SrcSyncCount; i++)
	{
		psCmdCompleteData->psSrcSync[i].psKernelSyncInfoKM->psSyncData->ui32ReadOpsComplete++;
	}
	
	
	psCmdCompleteData->bInUse = IMG_FALSE;
	
	
	PVRSRVCommandCompleteCallbacks();
	
#if defined(SYS_USING_INTERRUPTS)
	if(bScheduleMISR)
	{
		OSScheduleMISR(psSysData);
	}
#else
	PVR_UNREFERENCED_PARAMETER(bScheduleMISR);
#endif 
}


IMG_VOID PVRSRVCommandCompleteCallbacks(IMG_VOID)
{
	SYS_DATA				*psSysData;
	PVRSRV_DEVICE_NODE		*psDeviceNode;

	if (SysAcquireData(&psSysData) != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVCommandCompleteCallbacks: SysAcquireData failed"));
		return;
	}

	psDeviceNode = psSysData->psDeviceNodeList;
	while(psDeviceNode != IMG_NULL)
	{
		if(psDeviceNode->pfnDeviceCommandComplete != IMG_NULL)
		{
			
			(*psDeviceNode->pfnDeviceCommandComplete)(psDeviceNode);
		}
		psDeviceNode = psDeviceNode->psNext;
	}
}

IMG_EXPORT
PVRSRV_ERROR PVRSRVRegisterCmdProcListKM(IMG_UINT32		ui32DevIndex,
										 PFN_CMD_PROC	*ppfnCmdProcList,
										 IMG_UINT32		ui32MaxSyncsPerCmd[][2],
										 IMG_UINT32		ui32CmdCount)
{
	SYS_DATA				*psSysData;
	PVRSRV_ERROR			eError;
	IMG_UINT32				i;
	IMG_UINT32				ui32AllocSize;
	PFN_CMD_PROC			*ppfnCmdProc;
	COMMAND_COMPLETE_DATA	*psCmdCompleteData;

	
	if(ui32DevIndex >= SYS_DEVICE_COUNT)
	{
		PVR_DPF((PVR_DBG_ERROR,
					"PVRSRVRegisterCmdProcListKM: invalid DeviceType 0x%x",
					ui32DevIndex));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	
	eError = SysAcquireData(&psSysData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRegisterCmdProcListKM: SysAcquireData failed"));
		return eError;
	}

	
	eError = OSAllocMem( PVRSRV_OS_PAGEABLE_HEAP, 
					 ui32CmdCount * sizeof(PFN_CMD_PROC), 
					 (IMG_VOID **)&psSysData->ppfnCmdProcList[ui32DevIndex], IMG_NULL);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRegisterCmdProcListKM: Failed to alloc queue"));
		return eError;
	}

	
	ppfnCmdProc = psSysData->ppfnCmdProcList[ui32DevIndex];

	
	for (i=0; i<ui32CmdCount; i++)
	{
		ppfnCmdProc[i] = ppfnCmdProcList[i];
	}

	
	ui32AllocSize = ui32CmdCount * sizeof(COMMAND_COMPLETE_DATA*);
	eError = OSAllocMem( PVRSRV_OS_NON_PAGEABLE_HEAP,
					 ui32AllocSize, 
					 (IMG_VOID **)&psSysData->ppsCmdCompleteData[ui32DevIndex], IMG_NULL);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRegisterCmdProcListKM: Failed to alloc CC data"));
		goto ErrorExit;
	}

	for (i=0; i<ui32CmdCount; i++)
	{
		

		ui32AllocSize = sizeof(COMMAND_COMPLETE_DATA) 
					  + ((ui32MaxSyncsPerCmd[i][0]
					  +	ui32MaxSyncsPerCmd[i][1])
					  * sizeof(PVRSRV_SYNC_OBJECT));	 

		eError = OSAllocMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
							ui32AllocSize,
							(IMG_VOID **)&psSysData->ppsCmdCompleteData[ui32DevIndex][i],
							IMG_NULL);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"PVRSRVRegisterCmdProcListKM: Failed to alloc cmd %d",i));
			goto ErrorExit;
		}

		
		OSMemSet(psSysData->ppsCmdCompleteData[ui32DevIndex][i], 0x00, ui32AllocSize);

		psCmdCompleteData = psSysData->ppsCmdCompleteData[ui32DevIndex][i];

		
		psCmdCompleteData->psDstSync = (PVRSRV_SYNC_OBJECT*)
										(((IMG_UINT32)psCmdCompleteData) 
										+ sizeof(COMMAND_COMPLETE_DATA));
		psCmdCompleteData->psSrcSync = (PVRSRV_SYNC_OBJECT*)
										(((IMG_UINT32)psCmdCompleteData->psDstSync) 
										+ (sizeof(PVRSRV_SYNC_OBJECT) * ui32MaxSyncsPerCmd[i][0]));
	}

	return PVRSRV_OK;

ErrorExit:

	

	if(psSysData->ppsCmdCompleteData[ui32DevIndex] != IMG_NULL)
	{
		for (i=0; i<ui32CmdCount; i++)
		{
			if (psSysData->ppsCmdCompleteData[ui32DevIndex][i] != IMG_NULL)
			{
				OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP, 0, psSysData->ppsCmdCompleteData[ui32DevIndex][i], IMG_NULL);
			}
		}

		OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP, 0, psSysData->ppsCmdCompleteData[ui32DevIndex], IMG_NULL);
	}

	if(psSysData->ppfnCmdProcList[ui32DevIndex] != IMG_NULL)
	{
		OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP, 0, psSysData->ppfnCmdProcList[ui32DevIndex], IMG_NULL);
	}
	
	return eError;
}


IMG_EXPORT
PVRSRV_ERROR PVRSRVRemoveCmdProcListKM(IMG_UINT32 ui32DevIndex,
									   IMG_UINT32 ui32CmdCount)
{
	SYS_DATA		*psSysData;
	PVRSRV_ERROR	eError;
	IMG_UINT32		i;

	
	if(ui32DevIndex >= SYS_DEVICE_COUNT)
	{
		PVR_DPF((PVR_DBG_ERROR,
					"PVRSRVRemoveCmdProcListKM: invalid DeviceType 0x%x",
					ui32DevIndex));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	
	eError = SysAcquireData(&psSysData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVRemoveCmdProcListKM: SysAcquireData failed"));
		return eError;
	}

	if(psSysData->ppsCmdCompleteData[ui32DevIndex] != IMG_NULL)
	{
		for(i=0; i<ui32CmdCount; i++)
		{
			
			if(psSysData->ppsCmdCompleteData[ui32DevIndex][i] != IMG_NULL)
			{
				OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP, 0, psSysData->ppsCmdCompleteData[ui32DevIndex][i], IMG_NULL);
			}
		}

		
		OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP, 0, psSysData->ppsCmdCompleteData[ui32DevIndex], IMG_NULL);
	}

	
	if(psSysData->ppfnCmdProcList[ui32DevIndex] != IMG_NULL)
	{
		OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP, 0, psSysData->ppfnCmdProcList[ui32DevIndex], IMG_NULL);
	}

	return PVRSRV_OK;
}

