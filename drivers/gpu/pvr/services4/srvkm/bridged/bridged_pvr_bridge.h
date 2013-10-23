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

#ifndef __BRIDGED_PVR_BRIDGE_H__
#define __BRIDGED_PVR_BRIDGE_H__

#include "pvr_bridge.h"

#if defined(__cplusplus)
extern "C" {
#endif

#if defined(__linux__)
#define PVRSRV_GET_BRIDGE_ID(X)	_IOC_NR(X)
#else
#define PVRSRV_GET_BRIDGE_ID(X)	(X - PVRSRV_IOWR(PVRSRV_BRIDGE_CORE_CMD_FIRST))
#endif

typedef int (*BridgeWrapperFunction)(IMG_UINT32 ui32BridgeID,
									 IMG_VOID *psBridgeIn,
									 IMG_VOID *psBridgeOut,
									 PVRSRV_PER_PROCESS_DATA *psPerProc);

typedef struct _PVRSRV_BRIDGE_DISPATCH_TABLE_ENTRY
{
	BridgeWrapperFunction pfFunction; 
#if defined(DEBUG_BRIDGE_KM)
	const IMG_CHAR *pszIOCName; 
	const IMG_CHAR *pszFunctionName; 
	IMG_UINT32 ui32CallCount; 
	IMG_UINT32 ui32CopyFromUserTotalBytes; 
	IMG_UINT32 ui32CopyToUserTotalBytes; 
#endif
}PVRSRV_BRIDGE_DISPATCH_TABLE_ENTRY;


#if defined(SUPPORT_SGX)
#define BRIDGE_DISPATCH_TABLE_ENTRY_COUNT (PVRSRV_BRIDGE_LAST_SGX_CMD+1)
#else
#error "FIXME: BRIDGE_DISPATCH_TABLE_ENTRY_COUNT unset"
#endif

extern PVRSRV_BRIDGE_DISPATCH_TABLE_ENTRY g_BridgeDispatchTable[BRIDGE_DISPATCH_TABLE_ENTRY_COUNT];


#if defined(DEBUG_BRIDGE_KM)
typedef struct _PVRSRV_BRIDGE_GLOBAL_STATS
{
	IMG_UINT32 ui32IOCTLCount;
	IMG_UINT32 ui32TotalCopyFromUserBytes;
	IMG_UINT32 ui32TotalCopyToUserBytes;
}PVRSRV_BRIDGE_GLOBAL_STATS;

extern PVRSRV_BRIDGE_GLOBAL_STATS g_BridgeGlobalStats;
#endif


PVRSRV_ERROR CommonBridgeInit(IMG_VOID);

int BridgedDispatchKM(PVRSRV_PER_PROCESS_DATA * psPerProc,
					  PVRSRV_BRIDGE_PACKAGE   * psBridgePackageKM);

#if defined (__cplusplus)
}
#endif

#endif 

