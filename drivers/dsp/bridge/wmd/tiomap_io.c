/*
 * tiomap_io.c
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


/*
 *  ======== _tiomap_io.c ========
 *  Description:
 *      Implementation for the io read/write routines.
 *
 *! Revision History
 *! ================
 *! 16-Feb-2004 vp:  Fixed warning in WriteDspData function.
 *! 16-Apr-2003 vp:  Added support for TC word swap
 *! 26-Feb-2003 vp:  Fixed issue with EXT_BEG and EXT_END address.
 *! 24-Feb-2003 vp:  Ported to Linux platform
 *! 08-Oct-2002 rr:  Created.
 */

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/dbdefs.h>
#include <dspbridge/errbase.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>
#include <dspbridge/dbg.h>

/*  ----------------------------------- Platform Manager */
#include <dspbridge/dev.h>
#include <dspbridge/drv.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/mem.h>
#include <dspbridge/util.h>
#include <dspbridge/cfg.h>

/*  ----------------------------------- specific to this file */
#include "_tiomap.h"
#include "_tiomap_pwr.h"
#include "tiomap_io.h"

static u32 ulExtBase;
static u32 ulExtEnd;

static u32 ulShm0End;
static u32 ulDynExtBase;
static u32 ulTraceSecBeg;
static u32 ulTraceSecEnd;
static u32 ulShmBaseVirt;

bool bSymbolsReloaded = true;

/*
 *  ======== ReadExtDspData ========
 *      Copies DSP external memory buffers to the host side buffers.
 */
DSP_STATUS ReadExtDspData(struct WMD_DEV_CONTEXT *hDevContext,
			 OUT u8 *pbHostBuf, u32 dwDSPAddr,
			 u32 ulNumBytes, u32 ulMemType)
{
	DSP_STATUS	status = DSP_SOK;
	struct WMD_DEV_CONTEXT *pDevContext = hDevContext;
	u32	offset;
	u32	ulTLBBaseVirt = 0;
	u32	ulShmOffsetVirt = 0;
	u32	dwExtProgVirtMem;
	u32	dwBaseAddr = pDevContext->dwDspExtBaseAddr;
	bool	bTraceRead = false;

	DBG_Trace(DBG_ENTER, "ReadExtDspData,"
	"hDevContext: 0x%x\n\t\tpbHostBuf: 0x%x"
	"\n\t\tdwDSPAddr:  0x%x\n\t\tulNumBytes:  0x%x\n\t\t"
	"ulMemType:  0x%x\n", pDevContext, pbHostBuf, dwDSPAddr,
	ulNumBytes, ulMemType);

	if (!ulShmBaseVirt) {
		status = DEV_GetSymbol(pDevContext->hDevObject,
		SHMBASENAME, &ulShmBaseVirt);
	}
	DBC_Assert(ulShmBaseVirt != 0);

	/* Check if it is a read of Trace section */
	if (!ulTraceSecBeg) {
		status = DEV_GetSymbol(pDevContext->hDevObject,
		DSP_TRACESEC_BEG, &ulTraceSecBeg);
	}
	DBC_Assert(ulTraceSecBeg != 0);

	if (DSP_SUCCEEDED(status) && !ulTraceSecEnd) {
		status = DEV_GetSymbol(pDevContext->hDevObject,
		DSP_TRACESEC_END, &ulTraceSecEnd);
	}
	DBC_Assert(ulTraceSecEnd != 0);

	if (DSP_SUCCEEDED(status)) {
		if ((dwDSPAddr <= ulTraceSecEnd) &&
			(dwDSPAddr >= ulTraceSecBeg)) {
			DBG_Trace(DBG_LEVEL5, "Reading from DSP Trace"
				 "section 0x%x \n", dwDSPAddr);
			bTraceRead = true;
		}
	}

	/* If reading from TRACE, force remap/unmap */
	if ((bTraceRead) && dwBaseAddr) {
		dwBaseAddr = 0;
		pDevContext->dwDspExtBaseAddr = 0;
	}

	if (!dwBaseAddr) {
		/* Initialize ulExtBase and ulExtEnd */
		ulExtBase = 0;
		ulExtEnd = 0;

		/* Get DYNEXT_BEG, EXT_BEG and EXT_END.*/
		if (DSP_SUCCEEDED(status) && !ulDynExtBase) {
			status = DEV_GetSymbol(pDevContext->hDevObject,
					DYNEXTBASE, &ulDynExtBase);
		}
		DBC_Assert(ulDynExtBase != 0);

		if (DSP_SUCCEEDED(status)) {
			status = DEV_GetSymbol(pDevContext->hDevObject,
				 EXTBASE, &ulExtBase);
		}
		DBC_Assert(ulExtBase != 0);

		if (DSP_SUCCEEDED(status)) {
			status = DEV_GetSymbol(pDevContext->hDevObject,
					EXTEND,	&ulExtEnd);
		}
		DBC_Assert(ulExtEnd != 0);

	/* Trace buffer is right after the SHM SEG0,
	*  so set the base address to SHMBASE */
		if (bTraceRead) {
			ulExtBase = ulShmBaseVirt;
			ulExtEnd = ulTraceSecEnd;
		}

		DBC_Assert(ulExtEnd != 0);
		DBC_Assert(ulExtEnd > ulExtBase);

		if (ulExtEnd < ulExtBase)
			status = DSP_EFAIL;

		if (DSP_SUCCEEDED(status)) {
			ulTLBBaseVirt =
			pDevContext->aTLBEntry[0].ulDspVa * DSPWORDSIZE;
			DBC_Assert(ulTLBBaseVirt <= ulShmBaseVirt);
			dwExtProgVirtMem = pDevContext->aTLBEntry[0].ulGppVa;

			if (bTraceRead) {
				DBG_Trace(DBG_LEVEL7, "ReadExtDspData: "
				"GPP VA pointing to SHMMEMBASE 0x%x \n",
				 dwExtProgVirtMem);
			} else {
				ulShmOffsetVirt = ulShmBaseVirt - ulTLBBaseVirt;
				ulShmOffsetVirt += PG_ALIGN_HIGH(ulExtEnd -
						ulDynExtBase + 1,
						HW_PAGE_SIZE_64KB);
				dwExtProgVirtMem -= ulShmOffsetVirt;
				dwExtProgVirtMem += (ulExtBase - ulDynExtBase);
				DBG_Trace(DBG_LEVEL7, "ReadExtDspData: "
				"GPP VA pointing to EXTMEMBASE 0x%x \n",
				dwExtProgVirtMem);
				pDevContext->dwDspExtBaseAddr =
						dwExtProgVirtMem;

	/* This dwDspExtBaseAddr will get cleared only when the board is
	* stopped. */
				if (!pDevContext->dwDspExtBaseAddr) {
					status = DSP_EFAIL;
					DBG_Trace(DBG_LEVEL7, "ReadExtDspData: "
					"failed to Map the program memory\n");
				}
			}

			dwBaseAddr = dwExtProgVirtMem;
		}
	}

	if (!dwBaseAddr || !ulExtBase || !ulExtEnd) {
		DBG_Trace(DBG_LEVEL7,
		"Symbols missing for Ext Prog reading \n");
		status = DSP_EFAIL;
	}

	offset = dwDSPAddr - ulExtBase;

	if (DSP_SUCCEEDED(status))
		memcpy(pbHostBuf, (u8 *)dwBaseAddr+offset, ulNumBytes);

	return status;
}
/*
 *  ======== WriteDspData ========
 *  purpose:
 *      Copies buffers to the DSP internal/external memory.
 */
DSP_STATUS WriteDspData(struct WMD_DEV_CONTEXT *hDevContext, IN u8 *pbHostBuf,
			u32 dwDSPAddr, u32 ulNumBytes, u32 ulMemType)
{
	u32 offset;
	u32 dwBaseAddr = hDevContext->dwDspBaseAddr;
	struct CFG_HOSTRES resources;
	DSP_STATUS status;
	u32 base1, base2, base3;
	base1 = OMAP_DSP_MEM1_SIZE;
	base2 = OMAP_DSP_MEM2_BASE - OMAP_DSP_MEM1_BASE;
	base3 = OMAP_DSP_MEM3_BASE - OMAP_DSP_MEM1_BASE;
	DBG_Trace(DBG_ENTER, "Entered WriteDspData \n");

	status =  CFG_GetHostResources(
		 (struct CFG_DEVNODE *)DRV_GetFirstDevExtension(), &resources);

	offset = dwDSPAddr - hDevContext->dwDSPStartAdd;
	if (offset < base1) {
		dwBaseAddr = MEM_LinearAddress(resources.dwMemBase[2],
						resources.dwMemLength[2]);
	} else if (offset > base1 && offset < base2+OMAP_DSP_MEM2_SIZE) {
		dwBaseAddr = MEM_LinearAddress(resources.dwMemBase[3],
						resources.dwMemLength[3]);
		offset = offset - base2;
	} else if (offset >= base2+OMAP_DSP_MEM2_SIZE &&
		offset < base3 + OMAP_DSP_MEM3_SIZE) {
		dwBaseAddr = MEM_LinearAddress(resources.dwMemBase[4],
						resources.dwMemLength[4]);
		offset = offset - base3;
	} else{
		status = DSP_EFAIL;
		return status;
	}
	if (ulNumBytes)
		memcpy((u8 *) (dwBaseAddr+offset), pbHostBuf, ulNumBytes);
	else
		*((u32 *) pbHostBuf) = dwBaseAddr+offset;

	return status;
}

/*
 *  ======== WriteExtDspData ========
 *  purpose:
 *      Copies buffers to the external memory.
 *
 */
DSP_STATUS WriteExtDspData(struct WMD_DEV_CONTEXT *pDevContext,
			  IN u8 *pbHostBuf, u32 dwDSPAddr, u32 ulNumBytes,
			  u32 ulMemType, bool bDynamicLoad)
{
	u32 dwBaseAddr = pDevContext->dwDspExtBaseAddr;
	u32 dwOffset = 0;
	u8 bTempByte1, bTempByte2;
	u8 remainByte[4];
	s32 i;
	DSP_STATUS retVal = DSP_SOK;
	u32 dwExtProgVirtMem;
	u32 ulTLBBaseVirt = 0;
	u32 ulShmOffsetVirt = 0;
	struct CFG_HOSTRES hostRes;
	bool bTraceLoad = false;
	bTempByte1 = 0x0;
	bTempByte2 = 0x0;

	DBG_Trace(DBG_ENTER, "Entered WriteExtDspData dwDSPAddr 0x%x "
		 "ulNumBytes 0x%x \n", dwDSPAddr, ulNumBytes);
	  if (bSymbolsReloaded) {
		/* Check if it is a load to Trace section */
		retVal = DEV_GetSymbol(pDevContext->hDevObject,
					DSP_TRACESEC_BEG, &ulTraceSecBeg);
		if (DSP_SUCCEEDED(retVal))
			retVal = DEV_GetSymbol(pDevContext->hDevObject,
				 DSP_TRACESEC_END, &ulTraceSecEnd);
	}
	if (DSP_SUCCEEDED(retVal)) {
		if ((dwDSPAddr <= ulTraceSecEnd) &&
		   (dwDSPAddr >= ulTraceSecBeg)) {
			DBG_Trace(DBG_LEVEL5, "Writing to DSP Trace "
				 "section 0x%x \n", dwDSPAddr);
			bTraceLoad = true;
		}
	}

	/* If dynamic, force remap/unmap */
	if ((bDynamicLoad || bTraceLoad) && dwBaseAddr) {
		dwBaseAddr = 0;
		MEM_UnmapLinearAddress((void *)pDevContext->dwDspExtBaseAddr);
		pDevContext->dwDspExtBaseAddr = 0x0;
	}
	if (!dwBaseAddr) {
		if (bSymbolsReloaded)
			/* Get SHM_BEG  EXT_BEG and EXT_END. */
			retVal = DEV_GetSymbol(pDevContext->hDevObject,
						SHMBASENAME, &ulShmBaseVirt);
		DBC_Assert(ulShmBaseVirt != 0);
		if (bDynamicLoad) {
			if (DSP_SUCCEEDED(retVal)) {
				if (bSymbolsReloaded)
					retVal = DEV_GetSymbol(pDevContext->
						hDevObject, DYNEXTBASE,
						&ulExtBase);
			}
			DBC_Assert(ulExtBase != 0);
			if (DSP_SUCCEEDED(retVal)) {
				/* DR  OMAPS00013235 : DLModules array may be
				 * in EXTMEM. It is expected that DYNEXTMEM and
				 * EXTMEM are contiguous, so checking for the
				 * upper bound at EXTEND should be Ok. */
				if (bSymbolsReloaded)
					retVal = DEV_GetSymbol(pDevContext->
						hDevObject, EXTEND, &ulExtEnd);
			}
		} else {
			if (bSymbolsReloaded) {
				if (DSP_SUCCEEDED(retVal))
					retVal = DEV_GetSymbol(pDevContext->
						hDevObject, EXTBASE,
						&ulExtBase);
				DBC_Assert(ulExtBase != 0);
				if (DSP_SUCCEEDED(retVal))
					retVal = DEV_GetSymbol(pDevContext->
						hDevObject, EXTEND, &ulExtEnd);
			}
		}
		/* Trace buffer it right after the SHM SEG0, so set the
		 * 	base address to SHMBASE */
		if (bTraceLoad)
			ulExtBase = ulShmBaseVirt;

		DBC_Assert(ulExtEnd != 0);
		DBC_Assert(ulExtEnd > ulExtBase);
		if (ulExtEnd < ulExtBase)
			retVal = DSP_EFAIL;

		if (DSP_SUCCEEDED(retVal)) {
			ulTLBBaseVirt = pDevContext->aTLBEntry[0].ulDspVa *
					DSPWORDSIZE;
			DBC_Assert(ulTLBBaseVirt <= ulShmBaseVirt);

			if (bSymbolsReloaded) {
				if (DSP_SUCCEEDED(retVal)) {
					retVal = DEV_GetSymbol(pDevContext->
						 hDevObject, DSP_TRACESEC_END,
						 &ulShm0End);
				}
				if (DSP_SUCCEEDED(retVal)) {
					retVal = DEV_GetSymbol(pDevContext->
						 hDevObject, DYNEXTBASE,
						 &ulDynExtBase);
				}
			}
			ulShmOffsetVirt = ulShmBaseVirt - ulTLBBaseVirt;
			if (bTraceLoad) {
				dwExtProgVirtMem = pDevContext->aTLBEntry[0].
						   ulGppVa;
			} else {
				CFG_GetHostResources(
					(struct CFG_DEVNODE *)
					DRV_GetFirstDevExtension(), &hostRes);
				dwExtProgVirtMem = hostRes.dwMemBase[1];
				dwExtProgVirtMem += (ulExtBase - ulDynExtBase);
			}
			DBG_Trace(DBG_LEVEL7, "WriteExtDspData: GPP VA "
				 "pointing to EXTMEMBASE 0x%x \n",
				 dwExtProgVirtMem);

			pDevContext->dwDspExtBaseAddr =
				(u32)MEM_LinearAddress((void *)
				TO_VIRTUAL_UNCACHED(dwExtProgVirtMem), ulExtEnd
				- ulExtBase);
			dwBaseAddr += pDevContext->dwDspExtBaseAddr;
			/* This dwDspExtBaseAddr will get cleared only when
			 * the board is stopped.  */
			if (!pDevContext->dwDspExtBaseAddr) {
				retVal = DSP_EFAIL;
				DBG_Trace(DBG_LEVEL7, "WriteExtDspData: failed "
					 "to Map the program memory\n");
			}
		}
	}
	if (!dwBaseAddr || !ulExtBase || !ulExtEnd) {
		DBG_Trace(DBG_LEVEL7, "Symbols missing for Ext Prog loading\n");
		retVal = DSP_EFAIL;
	}
	if (DSP_SUCCEEDED(retVal)) {
		for (i = 0; i < 4; i++)
			remainByte[i] = 0x0;

		dwOffset = dwDSPAddr - ulExtBase;
		/* Also make sure the dwDSPAddr is < ulExtEnd */
		if (dwDSPAddr > ulExtEnd || dwOffset > dwDSPAddr) {
			DBG_Trace(DBG_LEVEL7, "We can not load at this address "
				 "dwDSPAddr=0x%x, ulExt/DynBase=0x%x, "
				 "ulExtEnd=0x%x\n", dwDSPAddr, ulExtBase,
				 ulExtEnd);
			retVal = DSP_EFAIL;
		}
	}
	if (DSP_SUCCEEDED(retVal)) {
		if (ulNumBytes)
			memcpy((u8 *) dwBaseAddr + dwOffset, pbHostBuf,
				ulNumBytes);
		else
			*((u32 *) pbHostBuf) = dwBaseAddr+dwOffset;
	}
	/* Unmap here to force remap for other Ext loads */
	if ((bDynamicLoad || bTraceLoad) && pDevContext->dwDspExtBaseAddr) {
		MEM_UnmapLinearAddress((void *) pDevContext->dwDspExtBaseAddr);
		pDevContext->dwDspExtBaseAddr = 0x0;
	}
	bSymbolsReloaded = false;
	return retVal;
}

