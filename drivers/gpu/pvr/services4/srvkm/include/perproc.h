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

#ifndef __PERPROC_H__
#define __PERPROC_H__

#if defined (__cplusplus)
extern "C" {
#endif

#include "img_types.h"
#include "resman.h"

#include "handle.h"

typedef struct _PVRSRV_PER_PROCESS_DATA_
{
	IMG_UINT32		ui32PID;
	IMG_HANDLE		hBlockAlloc;
	PRESMAN_CONTEXT 	hResManContext;
	IMG_HANDLE		hPerProcData;
	PVRSRV_HANDLE_BASE	*psHandleBase;
	
	IMG_BOOL		bHandlesBatched;
	IMG_UINT32		ui32RefCount;

	
	IMG_BOOL		bInitProcess;

	
	IMG_HANDLE		hOsPrivateData;
} PVRSRV_PER_PROCESS_DATA;

IMG_IMPORT PVRSRV_PER_PROCESS_DATA *PVRSRVPerProcessData(IMG_UINT32 ui32PID);

PVRSRV_ERROR PVRSRVPerProcessDataConnect(IMG_UINT32	ui32PID);
IMG_VOID PVRSRVPerProcessDataDisconnect(IMG_UINT32	ui32PID);

PVRSRV_ERROR PVRSRVPerProcessDataInit(IMG_VOID);
PVRSRV_ERROR PVRSRVPerProcessDataDeInit(IMG_VOID);

#if defined (__cplusplus)
}
#endif

#endif 

