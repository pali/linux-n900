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

#include "img_defs.h"
#include "services.h"
#include "pvr_bridge.h"
#include "perproc.h"
#include "mutex.h"
#include "syscommon.h"
#include "pvr_debug.h"
#include "proc.h"

#include "sgx_bridge.h"

#include "bridged_pvr_bridge.h"

#ifdef MODULE_TEST
#include "pvr_test_bridge.h"
#include "kern_test.h"
#endif


#if defined(DEBUG_BRIDGE_KM)
static off_t printLinuxBridgeStats(char * buffer, size_t size, off_t off);
#endif

extern PVRSRV_LINUX_MUTEX gPVRSRVLock;


PVRSRV_ERROR
LinuxBridgeInit(IMG_VOID)
{
#if defined(DEBUG_BRIDGE_KM)
	{
		int iStatus;
		iStatus = CreateProcReadEntry("bridge_stats", printLinuxBridgeStats);
		if(iStatus!=0)
		{
			return PVRSRV_ERROR_OUT_OF_MEMORY;
		}
	}
#endif
	return CommonBridgeInit();
}

IMG_VOID
LinuxBridgeDeInit(IMG_VOID)
{
#if defined(DEBUG_BRIDGE_KM)
	RemoveProcEntry("bridge_stats");
#endif
}

#if defined(DEBUG_BRIDGE_KM)
static off_t
printLinuxBridgeStats(char * buffer, size_t count, off_t off)
{
	PVRSRV_BRIDGE_DISPATCH_TABLE_ENTRY *psEntry;
	off_t Ret;

	LinuxLockMutex(&gPVRSRVLock);

	if(!off)
	{
		if(count < 500)
		{
			Ret = 0;
			goto unlock_and_return;
		}
		Ret = printAppend(buffer, count, 0,
						  "Total ioctl call count = %lu\n"
						  "Total number of bytes copied via copy_from_user = %lu\n"
						  "Total number of bytes copied via copy_to_user = %lu\n"
						  "Total number of bytes copied via copy_*_user = %lu\n\n"
						  "%-45s | %-40s | %10s | %20s | %10s\n",
						  g_BridgeGlobalStats.ui32IOCTLCount,
						  g_BridgeGlobalStats.ui32TotalCopyFromUserBytes,
						  g_BridgeGlobalStats.ui32TotalCopyToUserBytes,
						  g_BridgeGlobalStats.ui32TotalCopyFromUserBytes+g_BridgeGlobalStats.ui32TotalCopyToUserBytes,
						  "Bridge Name",
						  "Wrapper Function",
						  "Call Count",
						  "copy_from_user Bytes",
						  "copy_to_user Bytes"
						 );
		goto unlock_and_return;
	}

	if(off > BRIDGE_DISPATCH_TABLE_ENTRY_COUNT)
	{
		Ret = END_OF_FILE;
		goto unlock_and_return;
	}

	if(count < 300)
	{
		Ret = 0;
		goto unlock_and_return;
	}

	psEntry = &g_BridgeDispatchTable[off-1];
	Ret =  printAppend(buffer, count, 0,
					   "%-45s   %-40s   %-10lu   %-20lu   %-10lu\n",
					   psEntry->pszIOCName,
					   psEntry->pszFunctionName,
					   psEntry->ui32CallCount,
					   psEntry->ui32CopyFromUserTotalBytes,
					   psEntry->ui32CopyToUserTotalBytes);

unlock_and_return:
	LinuxUnLockMutex(&gPVRSRVLock);
	return Ret;
}
#endif 



long
PVRSRV_BridgeDispatchKM(struct file *file,
						unsigned int cmd,
						unsigned long arg)
{
	IMG_UINT32 ui32BridgeID = PVRSRV_GET_BRIDGE_ID(cmd);
	PVRSRV_BRIDGE_PACKAGE *psBridgePackageUM = (PVRSRV_BRIDGE_PACKAGE *)arg;
	PVRSRV_BRIDGE_PACKAGE sBridgePackageKM;
	IMG_UINT32 ui32PID = OSGetCurrentProcessIDKM();
	PVRSRV_PER_PROCESS_DATA *psPerProc;
	int err = -EFAULT;

	LinuxLockMutex(&gPVRSRVLock);


	if(!OSAccessOK(PVR_VERIFY_WRITE,
				   psBridgePackageUM,
				   sizeof(PVRSRV_BRIDGE_PACKAGE)))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Received invalid pointer to function arguments",
				 __FUNCTION__));

		goto unlock_and_return;
	}
	
	
	if(OSCopyFromUser(IMG_NULL,
					  &sBridgePackageKM,
					  psBridgePackageUM,
					  sizeof(PVRSRV_BRIDGE_PACKAGE))
	  != PVRSRV_OK)
	{
		goto unlock_and_return;
	}

#ifdef MODULE_TEST
	switch (cmd)
	{
		case PVRSRV_BRIDGE_SERVICES_TEST_MEM1:
			{
				PVRSRV_ERROR eError = MemTest1();
				if (sBridgePackageKM.ui32OutBufferSize == sizeof(PVRSRV_BRIDGE_RETURN))
				{
					PVRSRV_BRIDGE_RETURN* pReturn = (PVRSRV_BRIDGE_RETURN*)sBridgePackageKM.pvParamOut ;
					pReturn->eError = eError;
				}
			}
			err = 0;
			goto unlock_and_return;
		case PVRSRV_BRIDGE_SERVICES_TEST_MEM2:
			{
				PVRSRV_ERROR eError = MemTest2();
				if (sBridgePackageKM.ui32OutBufferSize == sizeof(PVRSRV_BRIDGE_RETURN))
				{
					PVRSRV_BRIDGE_RETURN* pReturn = (PVRSRV_BRIDGE_RETURN*)sBridgePackageKM.pvParamOut ;
					pReturn->eError = eError;
				}
			}
			err = 0;
			goto unlock_and_return;

		case PVRSRV_BRIDGE_SERVICES_TEST_RESOURCE:
			{
				PVRSRV_ERROR eError = ResourceTest();
				if (sBridgePackageKM.ui32OutBufferSize == sizeof(PVRSRV_BRIDGE_RETURN))
				{
					PVRSRV_BRIDGE_RETURN* pReturn = (PVRSRV_BRIDGE_RETURN*)sBridgePackageKM.pvParamOut ;
					pReturn->eError = eError;
				}
			}
			err = 0;
			goto unlock_and_return;

		case PVRSRV_BRIDGE_SERVICES_TEST_EVENTOBJECT:
			{
				PVRSRV_ERROR eError = EventObjectTest();
				if (sBridgePackageKM.ui32OutBufferSize == sizeof(PVRSRV_BRIDGE_RETURN))
				{
					PVRSRV_BRIDGE_RETURN* pReturn = (PVRSRV_BRIDGE_RETURN*)sBridgePackageKM.pvParamOut ;
					pReturn->eError = eError;
				}
			}
			err = 0;
			goto unlock_and_return;

		case PVRSRV_BRIDGE_SERVICES_TEST_MEMMAPPING:
			{
				PVRSRV_ERROR eError = MemMappingTest();
				if (sBridgePackageKM.ui32OutBufferSize == sizeof(PVRSRV_BRIDGE_RETURN))
				{
					PVRSRV_BRIDGE_RETURN* pReturn = (PVRSRV_BRIDGE_RETURN*)sBridgePackageKM.pvParamOut ;
					pReturn->eError = eError;
				}
			}
			err = 0;
			goto unlock_and_return;

		case PVRSRV_BRIDGE_SERVICES_TEST_PROCESSID:
			{
				PVRSRV_ERROR eError = ProcessIDTest();
				if (sBridgePackageKM.ui32OutBufferSize == sizeof(PVRSRV_BRIDGE_RETURN))
				{
					PVRSRV_BRIDGE_RETURN* pReturn = (PVRSRV_BRIDGE_RETURN*)sBridgePackageKM.pvParamOut ;
					pReturn->eError = eError;
				}
			}
			err = 0;
			goto unlock_and_return;

		case PVRSRV_BRIDGE_SERVICES_TEST_CLOCKUSWAITUS:
			{
				PVRSRV_ERROR eError = ClockusWaitusTest();
				if (sBridgePackageKM.ui32OutBufferSize == sizeof(PVRSRV_BRIDGE_RETURN))
				{
					PVRSRV_BRIDGE_RETURN* pReturn = (PVRSRV_BRIDGE_RETURN*)sBridgePackageKM.pvParamOut ;
					pReturn->eError = eError;
				}
			}
			err = 0;
			goto unlock_and_return;

		case PVRSRV_BRIDGE_SERVICES_TEST_TIMER:
			{
				PVRSRV_ERROR eError = TimerTest();
				if (sBridgePackageKM.ui32OutBufferSize == sizeof(PVRSRV_BRIDGE_RETURN))
				{
					PVRSRV_BRIDGE_RETURN* pReturn = (PVRSRV_BRIDGE_RETURN*)sBridgePackageKM.pvParamOut ;
					pReturn->eError = eError;
				}
			}
			err = 0;
			goto unlock_and_return;

		case PVRSRV_BRIDGE_SERVICES_TEST_PRIVSRV:
			{
				PVRSRV_ERROR eError = PrivSrvTest();
				if (sBridgePackageKM.ui32OutBufferSize == sizeof(PVRSRV_BRIDGE_RETURN))
				{
					PVRSRV_BRIDGE_RETURN* pReturn = (PVRSRV_BRIDGE_RETURN*)sBridgePackageKM.pvParamOut ;
					pReturn->eError = eError;
				}
			}
			err = 0;
			goto unlock_and_return;
		case PVRSRV_BRIDGE_SERVICES_TEST_COPYDATA:
		{
			IMG_UINT32               ui32PID;
			PVRSRV_PER_PROCESS_DATA *psPerProc;
			PVRSRV_ERROR eError;
			
			ui32PID = OSGetCurrentProcessIDKM();
		
			PVRSRVTrace("PVRSRV_BRIDGE_SERVICES_TEST_COPYDATA %d", ui32PID);
			
			psPerProc = PVRSRVPerProcessData(ui32PID);
						
			eError = CopyDataTest(sBridgePackageKM.pvParamIn, sBridgePackageKM.pvParamOut, psPerProc);
			
			*(PVRSRV_ERROR*)sBridgePackageKM.pvParamOut = eError;
			err = 0;
			goto unlock_and_return;
		}


		case PVRSRV_BRIDGE_SERVICES_TEST_POWERMGMT:
    			{
				PVRSRV_ERROR eError = PowerMgmtTest();
				if (sBridgePackageKM.ui32OutBufferSize == sizeof(PVRSRV_BRIDGE_RETURN))
				{
					PVRSRV_BRIDGE_RETURN* pReturn = (PVRSRV_BRIDGE_RETURN*)sBridgePackageKM.pvParamOut ;
					pReturn->eError = eError;
				}
			}
			err = 0;
			goto unlock_and_return;

	}
#endif
	
	if(ui32BridgeID != PVRSRV_GET_BRIDGE_ID(PVRSRV_BRIDGE_CONNECT_SERVICES))
	{
		PVRSRV_ERROR eError;

		eError = PVRSRVLookupHandle(KERNEL_HANDLE_BASE,
									(IMG_PVOID *)&psPerProc,
									sBridgePackageKM.hKernelServices,
									PVRSRV_HANDLE_TYPE_PERPROC_DATA);
		if(eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Invalid kernel services handle (%d)",
					 __FUNCTION__, eError));
			goto unlock_and_return;
		}

		if(psPerProc->ui32PID != ui32PID)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Process %d tried to access data "
					 "belonging to process %d", __FUNCTION__, ui32PID,
					 psPerProc->ui32PID));
			goto unlock_and_return;
		}
	}
	else
	{
		
		psPerProc = PVRSRVPerProcessData(ui32PID);
		if(psPerProc == IMG_NULL)
		{
			PVR_DPF((PVR_DBG_ERROR, "PVRSRV_BridgeDispatchKM: "
					 "Couldn't create per-process data area"));
			goto unlock_and_return;
		}
	}

	sBridgePackageKM.ui32BridgeID = PVRSRV_GET_BRIDGE_ID(sBridgePackageKM.ui32BridgeID);
	
	err = BridgedDispatchKM(psPerProc, &sBridgePackageKM);
	
unlock_and_return:
	LinuxUnLockMutex(&gPVRSRVLock);
	return err;
}

