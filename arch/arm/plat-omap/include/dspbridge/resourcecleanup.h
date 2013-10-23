/*
 * resourcecleanup.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef RES_CLEANUP_DISABLE

#include <dspbridge/nodepriv.h>
#include <dspbridge/drv.h>


extern DSP_STATUS DRV_GetProcCtxtList(struct PROCESS_CONTEXT **pPctxt,
				struct DRV_OBJECT *hDrvObject);

extern DSP_STATUS DRV_InsertProcContext(struct DRV_OBJECT *hDrVObject,
					HANDLE hPCtxt);

extern DSP_STATUS DRV_RemoveAllDMMResElements(HANDLE pCtxt);

extern DSP_STATUS DRV_RemoveAllNodeResElements(HANDLE pCtxt);

extern DSP_STATUS DRV_ProcUpdatestate(HANDLE pCtxt,
				      enum GPP_PROC_RES_STATE resState);

extern DSP_STATUS DRV_ProcSetPID(HANDLE pCtxt, s32 hProcess);

extern DSP_STATUS DRV_GetProcContext(u32 phProcess,
				struct DRV_OBJECT *hDrvObject,
				HANDLE hPCtxt, DSP_HNODE hNode,
				u32 pMapAddr);

extern DSP_STATUS DRV_RemoveAllResources(HANDLE pPctxt);

extern DSP_STATUS DRV_RemoveProcContext(struct DRV_OBJECT *hDRVObject,
				     HANDLE hPCtxt, HANDLE hProcess);

extern DSP_STATUS DRV_GetNodeResElement(HANDLE hNode, HANDLE nodeRes,
					HANDLE pCtxt);

extern DSP_STATUS DRV_InsertNodeResElement(HANDLE hNode, HANDLE nodeRes,
					    HANDLE pCtxt);

extern void DRV_ProcNodeUpdateHeapStatus(HANDLE hNodeRes, s32 status);

extern DSP_STATUS DRV_RemoveNodeResElement(HANDLE nodeRes, HANDLE status);

extern void DRV_ProcNodeUpdateStatus(HANDLE hNodeRes, s32 status);

extern DSP_STATUS DRV_UpdateDMMResElement(HANDLE dmmRes, u32 pMpuAddr,
					  u32 ulSize, u32 pReqAddr,
					  u32 ppMapAddr, HANDLE hProcesso);

extern DSP_STATUS DRV_InsertDMMResElement(HANDLE dmmRes, HANDLE pCtxt);

extern DSP_STATUS DRV_GetDMMResElement(u32 pMapAddr, HANDLE dmmRes,
				       HANDLE pCtxt);

extern DSP_STATUS DRV_RemoveDMMResElement(HANDLE dmmRes, HANDLE pCtxt);

extern DSP_STATUS DRV_ProcUpdateSTRMRes(u32 uNumBufs, HANDLE STRMRes,
					HANDLE pCtxt);

extern DSP_STATUS DRV_ProcInsertSTRMResElement(HANDLE hStrm, HANDLE STRMRes,
						HANDLE pPctxt);

extern DSP_STATUS DRV_GetSTRMResElement(HANDLE hStrm, HANDLE STRMRes,
					HANDLE pCtxt);

extern DSP_STATUS DRV_ProcRemoveSTRMResElement(HANDLE STRMRes, HANDLE pCtxt);

extern DSP_STATUS DRV_RemoveAllSTRMResElements(HANDLE pCtxt);

extern DSP_STATUS DRV_ProcDisplayResInfo(u8 *pBuf, u32 *pSize);

extern enum NODE_STATE NODE_GetState(HANDLE hNode);

#endif
