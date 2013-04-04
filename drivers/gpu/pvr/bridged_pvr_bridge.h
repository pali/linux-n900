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

#define PVRSRV_GET_BRIDGE_ID(X)	_IOC_NR(X)

struct PVRSRV_BRIDGE_DISPATCH_TABLE_ENTRY {
	int (*pfFunction)(u32 ui32BridgeID, void *psBridgeIn, void *psBridgeOut,
			  struct PVRSRV_PER_PROCESS_DATA *psPerProc);
#if defined(DEBUG_BRIDGE_KM)
	const char *pszIOCName;
	const char *pszFunctionName;
	u32 ui32CallCount;
	u32 ui32CopyFromUserTotalBytes;
	u32 ui32CopyToUserTotalBytes;
#endif
};

#define BRIDGE_DISPATCH_TABLE_ENTRY_COUNT (PVRSRV_BRIDGE_LAST_SGX_CMD+1)

extern struct PVRSRV_BRIDGE_DISPATCH_TABLE_ENTRY
	    g_BridgeDispatchTable[BRIDGE_DISPATCH_TABLE_ENTRY_COUNT];

#if defined(DEBUG_BRIDGE_KM)
struct PVRSRV_BRIDGE_GLOBAL_STATS {
	u32 ui32IOCTLCount;
	u32 ui32TotalCopyFromUserBytes;
	u32 ui32TotalCopyToUserBytes;
};

extern struct PVRSRV_BRIDGE_GLOBAL_STATS g_BridgeGlobalStats;
#endif

enum PVRSRV_ERROR CommonBridgeInit(void);

int BridgedDispatchKM(struct PVRSRV_PER_PROCESS_DATA *psPerProc,
		      struct PVRSRV_BRIDGE_PACKAGE *psBridgePackageKM);

#endif
