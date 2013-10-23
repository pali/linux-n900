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
#include "resman.h"

#ifndef AUTOCONF_INCLUDED
#include <linux/config.h>
#endif

#include <linux/version.h>
#include <linux/sched.h>
#include <linux/hardirq.h>

#include <linux/semaphore.h>

static DECLARE_MUTEX(lock);

#define ACQUIRE_SYNC_OBJ  do {							\
		if (in_interrupt()) { 							\
			printk ("ISR cannot take RESMAN mutex\n"); 	\
			BUG(); 										\
		} 												\
		else down (&lock); 								\
} while (0)
#define RELEASE_SYNC_OBJ up (&lock)


#define RESMAN_SIGNATURE 0x12345678

typedef struct _RESMAN_ITEM_ {
#ifdef DEBUG
	IMG_UINT32 ui32Signature;
#endif
	struct _RESMAN_ITEM_ **ppsThis;
	struct _RESMAN_ITEM_ *psNext;

	IMG_UINT32 ui32Flags;
	IMG_UINT32 ui32ResType;

	IMG_PVOID pvParam;
	IMG_UINT32 ui32Param;

	RESMAN_FREE_FN pfnFreeResource;
} RESMAN_ITEM;

typedef struct _RESMAN_CONTEXT_ {
#ifdef DEBUG
	IMG_UINT32 ui32Signature;
#endif
	struct _RESMAN_CONTEXT_ **ppsThis;
	struct _RESMAN_CONTEXT_ *psNext;

	PVRSRV_PER_PROCESS_DATA *psPerProc;

	RESMAN_ITEM *psResItemList;

} RESMAN_CONTEXT;

typedef struct {
	RESMAN_CONTEXT *psContextList;

} RESMAN_LIST, *PRESMAN_LIST;

PRESMAN_LIST gpsResList = IMG_NULL;

#define PRINT_RESLIST(x, y, z)

static PVRSRV_ERROR FreeResourceByPtr(RESMAN_ITEM * psItem,
				      IMG_BOOL bExecuteCallback);

static PVRSRV_ERROR FreeResourceByCriteria(PRESMAN_CONTEXT psContext,
					   IMG_UINT32 ui32SearchCriteria,
					   IMG_UINT32 ui32ResType,
					   IMG_PVOID pvParam,
					   IMG_UINT32 ui32Param,
					   IMG_BOOL bExecuteCallback);

#ifdef DEBUG
static IMG_VOID ValidateResList(PRESMAN_LIST psResList);
#define VALIDATERESLIST() ValidateResList(gpsResList)
#else
#define VALIDATERESLIST()
#endif

PVRSRV_ERROR ResManInit(IMG_VOID)
{
	if (gpsResList == IMG_NULL) {

		if (OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
			       sizeof(*gpsResList),
			       (IMG_VOID **) & gpsResList,
			       IMG_NULL) != PVRSRV_OK) {
			return PVRSRV_ERROR_OUT_OF_MEMORY;
		}

		gpsResList->psContextList = IMG_NULL;

		VALIDATERESLIST();
	}

	return PVRSRV_OK;
}

IMG_VOID ResManDeInit(IMG_VOID)
{
	if (gpsResList != IMG_NULL) {

		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(*gpsResList),
			  gpsResList, IMG_NULL);
	}
}

PVRSRV_ERROR PVRSRVResManConnect(IMG_HANDLE hPerProc,
				 PRESMAN_CONTEXT * phResManContext)
{
	PVRSRV_ERROR eError;
	PRESMAN_CONTEXT psResManContext;

	ACQUIRE_SYNC_OBJ;

	VALIDATERESLIST();

	eError = OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(*psResManContext),
			    (IMG_VOID **) & psResManContext, IMG_NULL);
	if (eError != PVRSRV_OK) {
		PVR_DPF((PVR_DBG_ERROR,
			 "PVRSRVResManConnect: ERROR allocating new RESMAN context struct"));

		VALIDATERESLIST();

		RELEASE_SYNC_OBJ;

		return eError;
	}
#ifdef DEBUG
	psResManContext->ui32Signature = RESMAN_SIGNATURE;
#endif
	psResManContext->psResItemList = IMG_NULL;
	psResManContext->psPerProc = hPerProc;

	psResManContext->psNext = gpsResList->psContextList;
	psResManContext->ppsThis = &gpsResList->psContextList;
	gpsResList->psContextList = psResManContext;
	if (psResManContext->psNext) {
		psResManContext->psNext->ppsThis = &(psResManContext->psNext);
	}

	VALIDATERESLIST();

	RELEASE_SYNC_OBJ;

	*phResManContext = psResManContext;

	return PVRSRV_OK;
}

IMG_VOID PVRSRVResManDisconnect(PRESMAN_CONTEXT psResManContext,
				IMG_BOOL bKernelContext)
{

	ACQUIRE_SYNC_OBJ;

	VALIDATERESLIST();

	PRINT_RESLIST(gpsResList, psResManContext, IMG_TRUE);

	if (!bKernelContext) {

		FreeResourceByCriteria(psResManContext, RESMAN_CRITERIA_RESTYPE,
				       RESMAN_TYPE_OS_USERMODE_MAPPING, 0, 0,
				       IMG_TRUE);

		FreeResourceByCriteria(psResManContext, RESMAN_CRITERIA_RESTYPE,
				       RESMAN_TYPE_EVENT_OBJECT, 0, 0,
				       IMG_TRUE);

		FreeResourceByCriteria(psResManContext, RESMAN_CRITERIA_RESTYPE,
				       RESMAN_TYPE_HW_RENDER_CONTEXT, 0, 0,
				       IMG_TRUE);
		FreeResourceByCriteria(psResManContext, RESMAN_CRITERIA_RESTYPE,
				       RESMAN_TYPE_HW_TRANSFER_CONTEXT, 0, 0,
				       IMG_TRUE);
		FreeResourceByCriteria(psResManContext, RESMAN_CRITERIA_RESTYPE,
				       RESMAN_TYPE_HW_2D_CONTEXT, 0, 0,
				       IMG_TRUE);
		FreeResourceByCriteria(psResManContext, RESMAN_CRITERIA_RESTYPE,
				       RESMAN_TYPE_TRANSFER_CONTEXT, 0, 0,
				       IMG_TRUE);
		FreeResourceByCriteria(psResManContext, RESMAN_CRITERIA_RESTYPE,
				       RESMAN_TYPE_SHARED_PB_DESC_CREATE_LOCK,
				       0, 0, IMG_TRUE);
		FreeResourceByCriteria(psResManContext, RESMAN_CRITERIA_RESTYPE,
				       RESMAN_TYPE_SHARED_PB_DESC, 0, 0,
				       IMG_TRUE);

		FreeResourceByCriteria(psResManContext, RESMAN_CRITERIA_RESTYPE,
				       RESMAN_TYPE_DISPLAYCLASS_SWAPCHAIN, 0, 0,
				       IMG_TRUE);
		FreeResourceByCriteria(psResManContext, RESMAN_CRITERIA_RESTYPE,
				       RESMAN_TYPE_DISPLAYCLASS_DEVICE, 0, 0,
				       IMG_TRUE);

		FreeResourceByCriteria(psResManContext, RESMAN_CRITERIA_RESTYPE,
				       RESMAN_TYPE_BUFFERCLASS_DEVICE, 0, 0,
				       IMG_TRUE);

		FreeResourceByCriteria(psResManContext, RESMAN_CRITERIA_RESTYPE,
				       RESMAN_TYPE_DEVICECLASSMEM_MAPPING, 0, 0,
				       IMG_TRUE);
		FreeResourceByCriteria(psResManContext, RESMAN_CRITERIA_RESTYPE,
				       RESMAN_TYPE_DEVICEMEM_WRAP, 0, 0,
				       IMG_TRUE);
		FreeResourceByCriteria(psResManContext, RESMAN_CRITERIA_RESTYPE,
				       RESMAN_TYPE_DEVICEMEM_MAPPING, 0, 0,
				       IMG_TRUE);
		FreeResourceByCriteria(psResManContext, RESMAN_CRITERIA_RESTYPE,
				       RESMAN_TYPE_KERNEL_DEVICEMEM_ALLOCATION,
				       0, 0, IMG_TRUE);
		FreeResourceByCriteria(psResManContext, RESMAN_CRITERIA_RESTYPE,
				       RESMAN_TYPE_DEVICEMEM_ALLOCATION, 0, 0,
				       IMG_TRUE);
		FreeResourceByCriteria(psResManContext, RESMAN_CRITERIA_RESTYPE,
				       RESMAN_TYPE_DEVICEMEM_CONTEXT, 0, 0,
				       IMG_TRUE);
	}

	PVR_ASSERT(psResManContext->psResItemList == IMG_NULL);

	*(psResManContext->ppsThis) = psResManContext->psNext;
	if (psResManContext->psNext) {
		psResManContext->psNext->ppsThis = psResManContext->ppsThis;
	}

	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP, sizeof(RESMAN_CONTEXT),
		  psResManContext, IMG_NULL);

	VALIDATERESLIST();

	PRINT_RESLIST(gpsResList, psResManContext, IMG_FALSE);

	RELEASE_SYNC_OBJ;
}

PRESMAN_ITEM ResManRegisterRes(PRESMAN_CONTEXT psResManContext,
			       IMG_UINT32 ui32ResType,
			       IMG_PVOID pvParam,
			       IMG_UINT32 ui32Param,
			       RESMAN_FREE_FN pfnFreeResource)
{
	PRESMAN_ITEM psNewResItem;

	PVR_ASSERT(psResManContext != IMG_NULL);
	PVR_ASSERT(ui32ResType != 0);

	ACQUIRE_SYNC_OBJ;

	VALIDATERESLIST();

	PVR_DPF((PVR_DBG_MESSAGE, "ResManRegisterRes: register resource "
		 "Context 0x%x, ResType 0x%x, pvParam 0x%x, ui32Param 0x%x, "
		 "FreeFunc %08X",
		 psResManContext, ui32ResType, (IMG_UINT32) pvParam,
		 ui32Param, pfnFreeResource));

	if (OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
		       sizeof(RESMAN_ITEM), (IMG_VOID **) & psNewResItem,
		       IMG_NULL) != PVRSRV_OK) {
		PVR_DPF((PVR_DBG_ERROR, "ResManRegisterRes: "
			 "ERROR allocating new resource item"));

		RELEASE_SYNC_OBJ;

		return ((PRESMAN_ITEM) IMG_NULL);
	}

#ifdef DEBUG
	psNewResItem->ui32Signature = RESMAN_SIGNATURE;
#endif
	psNewResItem->ui32ResType = ui32ResType;
	psNewResItem->pvParam = pvParam;
	psNewResItem->ui32Param = ui32Param;
	psNewResItem->pfnFreeResource = pfnFreeResource;
	psNewResItem->ui32Flags = 0;

	psNewResItem->ppsThis = &psResManContext->psResItemList;
	psNewResItem->psNext = psResManContext->psResItemList;
	psResManContext->psResItemList = psNewResItem;
	if (psNewResItem->psNext) {
		psNewResItem->psNext->ppsThis = &psNewResItem->psNext;
	}

	VALIDATERESLIST();

	RELEASE_SYNC_OBJ;

	return (psNewResItem);
}

PVRSRV_ERROR ResManFreeResByPtr(RESMAN_ITEM * psResItem)
{
	PVRSRV_ERROR eError;

	PVR_ASSERT(psResItem != IMG_NULL);

	if (psResItem == IMG_NULL) {
		PVR_DPF((PVR_DBG_MESSAGE,
			 "ResManFreeResByPtr: NULL ptr - nothing to do"));
		return PVRSRV_OK;
	}

	PVR_DPF((PVR_DBG_MESSAGE,
		 "ResManFreeResByPtr: freeing resource at %08X", psResItem));

	ACQUIRE_SYNC_OBJ;

	VALIDATERESLIST();

	eError = FreeResourceByPtr(psResItem, IMG_TRUE);

	VALIDATERESLIST();

	RELEASE_SYNC_OBJ;

	return (eError);
}

PVRSRV_ERROR ResManFreeResByCriteria(PRESMAN_CONTEXT psResManContext,
				     IMG_UINT32 ui32SearchCriteria,
				     IMG_UINT32 ui32ResType,
				     IMG_PVOID pvParam, IMG_UINT32 ui32Param)
{
	PVRSRV_ERROR eError;

	PVR_ASSERT(psResManContext != IMG_NULL);

	ACQUIRE_SYNC_OBJ;

	VALIDATERESLIST();

	PVR_DPF((PVR_DBG_MESSAGE, "ResManFreeResByCriteria: "
		 "Context 0x%x, Criteria 0x%x, Type 0x%x, Addr 0x%x, Param 0x%x",
		 psResManContext, ui32SearchCriteria, ui32ResType,
		 (IMG_UINT32) pvParam, ui32Param));

	eError = FreeResourceByCriteria(psResManContext, ui32SearchCriteria,
					ui32ResType, pvParam, ui32Param,
					IMG_TRUE);

	VALIDATERESLIST();

	RELEASE_SYNC_OBJ;

	return eError;
}

IMG_VOID ResManDissociateRes(RESMAN_ITEM * psResItem,
			     PRESMAN_CONTEXT psNewResManContext)
{
	PVR_ASSERT(psResItem != IMG_NULL);
	PVR_ASSERT(psResItem->ui32Signature == RESMAN_SIGNATURE);

	if (psNewResManContext != IMG_NULL) {

		if (psResItem->psNext) {
			psResItem->psNext->ppsThis = psResItem->ppsThis;
		}
		*psResItem->ppsThis = psResItem->psNext;

		psResItem->ppsThis = &psNewResManContext->psResItemList;
		psResItem->psNext = psNewResManContext->psResItemList;
		psNewResManContext->psResItemList = psResItem;
		if (psResItem->psNext) {
			psResItem->psNext->ppsThis = &psResItem->psNext;
		}
	} else {
		FreeResourceByPtr(psResItem, IMG_FALSE);
	}
}

IMG_INTERNAL PVRSRV_ERROR ResManFindResourceByPtr(PRESMAN_CONTEXT
						  psResManContext,
						  RESMAN_ITEM * psItem)
{
	RESMAN_ITEM *psCurItem;

	PVR_ASSERT(psResManContext != IMG_NULL);
	PVR_ASSERT(psItem != IMG_NULL);
	PVR_ASSERT(psItem->ui32Signature == RESMAN_SIGNATURE);

	ACQUIRE_SYNC_OBJ;

	PVR_DPF((PVR_DBG_MESSAGE,
		 "FindResourceByPtr: psItem=%08X, psItem->psNext=%08X",
		 psItem, psItem->psNext));

	PVR_DPF((PVR_DBG_MESSAGE,
		 "FindResourceByPtr: Resource Ctx 0x%x, Type 0x%x, Addr 0x%x, "
		 "Param 0x%x, FnCall %08X, Flags 0x%x",
		 psResManContext,
		 psItem->ui32ResType, (IMG_UINT32) psItem->pvParam,
		 psItem->ui32Param, psItem->pfnFreeResource,
		 psItem->ui32Flags));

	psCurItem = psResManContext->psResItemList;

	while (psCurItem != IMG_NULL) {

		if (psCurItem != psItem) {

			psCurItem = psCurItem->psNext;
		} else {

			RELEASE_SYNC_OBJ;
			return PVRSRV_OK;
		}
	}

	RELEASE_SYNC_OBJ;

	return PVRSRV_ERROR_NOT_OWNER;
}

static PVRSRV_ERROR FreeResourceByPtr(RESMAN_ITEM * psItem,
				      IMG_BOOL bExecuteCallback)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	PVR_ASSERT(psItem != IMG_NULL);
	PVR_ASSERT(psItem->ui32Signature == RESMAN_SIGNATURE);

	PVR_DPF((PVR_DBG_MESSAGE,
		 "FreeResourceByPtr: psItem=%08X, psItem->psNext=%08X",
		 psItem, psItem->psNext));

	PVR_DPF((PVR_DBG_MESSAGE,
		 "FreeResourceByPtr: Type 0x%x, Addr 0x%x, "
		 "Param 0x%x, FnCall %08X, Flags 0x%x",
		 psItem->ui32ResType, (IMG_UINT32) psItem->pvParam,
		 psItem->ui32Param, psItem->pfnFreeResource,
		 psItem->ui32Flags));

	if (psItem->psNext) {
		psItem->psNext->ppsThis = psItem->ppsThis;
	}
	*psItem->ppsThis = psItem->psNext;

	RELEASE_SYNC_OBJ;

	if (bExecuteCallback) {
		eError =
		    psItem->pfnFreeResource(psItem->pvParam, psItem->ui32Param);
		if (eError != PVRSRV_OK) {
			PVR_DPF((PVR_DBG_ERROR,
				 "FreeResourceByPtr: ERROR calling FreeResource function"));
		}
	}

	ACQUIRE_SYNC_OBJ;

	if (OSFreeMem
	    (PVRSRV_OS_PAGEABLE_HEAP, sizeof(RESMAN_ITEM), psItem, IMG_NULL)
	    != PVRSRV_OK) {
		PVR_DPF((PVR_DBG_ERROR,
			 "FreeResourceByPtr: ERROR freeing resource list item memory"));
		eError = PVRSRV_ERROR_GENERIC;
	}

	return (eError);
}

static PVRSRV_ERROR FreeResourceByCriteria(PRESMAN_CONTEXT psResManContext,
					   IMG_UINT32 ui32SearchCriteria,
					   IMG_UINT32 ui32ResType,
					   IMG_PVOID pvParam,
					   IMG_UINT32 ui32Param,
					   IMG_BOOL bExecuteCallback)
{
	PRESMAN_ITEM psCurItem;
	IMG_BOOL bMatch;
	PVRSRV_ERROR eError = PVRSRV_OK;

	psCurItem = psResManContext->psResItemList;

	while (psCurItem != IMG_NULL) {

		bMatch = IMG_TRUE;

		if ((ui32SearchCriteria & RESMAN_CRITERIA_RESTYPE) &&
		    psCurItem->ui32ResType != ui32ResType) {
			bMatch = IMG_FALSE;
		}

		else if ((ui32SearchCriteria & RESMAN_CRITERIA_PVOID_PARAM) &&
			 psCurItem->pvParam != pvParam) {
			bMatch = IMG_FALSE;
		}

		else if ((ui32SearchCriteria & RESMAN_CRITERIA_UI32_PARAM) &&
			 psCurItem->ui32Param != ui32Param) {
			bMatch = IMG_FALSE;
		}

		if (!bMatch) {

			psCurItem = psCurItem->psNext;
		} else {

			eError = FreeResourceByPtr(psCurItem, bExecuteCallback);

			if (eError != PVRSRV_OK) {
				return eError;
			}

			psCurItem = psResManContext->psResItemList;
		}
	}

	return eError;
}

#ifdef DEBUG
static IMG_VOID ValidateResList(PRESMAN_LIST psResList)
{
	PRESMAN_ITEM psCurItem, *ppsThisItem;
	PRESMAN_CONTEXT psCurContext, *ppsThisContext;

	if (psResList == IMG_NULL) {
		PVR_DPF((PVR_DBG_MESSAGE,
			 "ValidateResList: resman not initialised yet"));
		return;
	}

	psCurContext = psResList->psContextList;
	ppsThisContext = &psResList->psContextList;

	while (psCurContext != IMG_NULL) {

		PVR_ASSERT(psCurContext->ui32Signature == RESMAN_SIGNATURE);
		if (psCurContext->ppsThis != ppsThisContext) {
			PVR_DPF((PVR_DBG_WARNING,
				 "psCC=%08X psCC->ppsThis=%08X psCC->psNext=%08X ppsTC=%08X",
				 psCurContext, psCurContext->ppsThis,
				 psCurContext->psNext, ppsThisContext));
			PVR_ASSERT(psCurContext->ppsThis == ppsThisContext);
		}

		psCurItem = psCurContext->psResItemList;
		ppsThisItem = &psCurContext->psResItemList;
		while (psCurItem != IMG_NULL) {

			PVR_ASSERT(psCurItem->ui32Signature ==
				   RESMAN_SIGNATURE);
			if (psCurItem->ppsThis != ppsThisItem) {
				PVR_DPF((PVR_DBG_WARNING,
					 "psCurItem=%08X psCurItem->ppsThis=%08X psCurItem->psNext=%08X ppsThisItem=%08X",
					 psCurItem, psCurItem->ppsThis,
					 psCurItem->psNext, ppsThisItem));
				PVR_ASSERT(psCurItem->ppsThis == ppsThisItem);
			}

			ppsThisItem = &psCurItem->psNext;
			psCurItem = psCurItem->psNext;
		}

		ppsThisContext = &psCurContext->psNext;
		psCurContext = psCurContext->psNext;
	}
}
#endif
