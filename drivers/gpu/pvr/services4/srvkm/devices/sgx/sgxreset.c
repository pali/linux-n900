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
#include "sgxmmu.h"
#include "services_headers.h"
#include "sgxinfokm.h"
#include "sgxconfig.h"

#include "pdump_km.h"


#if defined(SGX_FEATURE_MULTIPLE_MEM_CONTEXTS)
#define SGX_BIF_DIR_LIST_INDEX_EDM	15
#define SGX_BIF_DIR_LIST_REG_EDM	EUR_CR_BIF_DIR_LIST_BASE15
#else
#define SGX_BIF_DIR_LIST_REG_EDM	EUR_CR_BIF_DIR_LIST_BASE0
#endif


static IMG_VOID SGXResetSoftReset(PVRSRV_SGXDEV_INFO	*psDevInfo,
								  IMG_BOOL				bResetBIF,
								  IMG_UINT32			ui32PDUMPFlags,
								  IMG_BOOL				bPDump)
{
	#if !defined(SGX540)
	IMG_UINT32 ui32SoftResetRegVal =
					#ifdef EUR_CR_SOFT_RESET_TWOD_RESET_MASK
					EUR_CR_SOFT_RESET_TWOD_RESET_MASK	|
					#endif
					EUR_CR_SOFT_RESET_DPM_RESET_MASK	|
					EUR_CR_SOFT_RESET_TA_RESET_MASK		|
					EUR_CR_SOFT_RESET_USE_RESET_MASK	|
					EUR_CR_SOFT_RESET_ISP_RESET_MASK	|
					EUR_CR_SOFT_RESET_TSP_RESET_MASK;
	#else
	IMG_UINT32 ui32SoftResetRegVal =
					EUR_CR_SOFT_RESET_VDM_RESET_MASK |
					EUR_CR_SOFT_RESET_DPM_RESET_MASK |
					EUR_CR_SOFT_RESET_TE_RESET_MASK |
					EUR_CR_SOFT_RESET_MTE_RESET_MASK |
					EUR_CR_SOFT_RESET_ISP_RESET_MASK |
					EUR_CR_SOFT_RESET_ISP2_RESET_MASK |
					EUR_CR_SOFT_RESET_TSP_RESET_MASK |
					EUR_CR_SOFT_RESET_PDS_RESET_MASK |
					EUR_CR_SOFT_RESET_PBE_RESET_MASK |
					EUR_CR_SOFT_RESET_CACHEL2_RESET_MASK |
					EUR_CR_SOFT_RESET_UCACHEL2_RESET_MASK |
					EUR_CR_SOFT_RESET_MADD_RESET_MASK |
					EUR_CR_SOFT_RESET_ITR_RESET_MASK |
					EUR_CR_SOFT_RESET_TEX_RESET_MASK |
					EUR_CR_SOFT_RESET_USE_RESET_MASK |
					EUR_CR_SOFT_RESET_IDXFIFO_RESET_MASK |
					EUR_CR_SOFT_RESET_TA_RESET_MASK;
	#endif

#if !defined(PDUMP)
	PVR_UNREFERENCED_PARAMETER(ui32PDUMPFlags);
#endif 

	if (bResetBIF)
	{
		ui32SoftResetRegVal |= EUR_CR_SOFT_RESET_BIF_RESET_MASK;
	}
	
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_SOFT_RESET, ui32SoftResetRegVal);
	if (bPDump)
	{
		PDUMPREGWITHFLAGS(EUR_CR_SOFT_RESET, ui32SoftResetRegVal, ui32PDUMPFlags);
	}
}


static IMG_VOID SGXResetSleep(PVRSRV_SGXDEV_INFO	*psDevInfo,
							  IMG_UINT32			ui32PDUMPFlags,
							  IMG_BOOL				bPDump)
{
#if !defined(PDUMP)
	PVR_UNREFERENCED_PARAMETER(ui32PDUMPFlags);
#endif 

	
	OSWaitus(1000 * 1000000 / psDevInfo->ui32CoreClockSpeed);
	if (bPDump)
	{
		PDUMPIDLWITHFLAGS(30, ui32PDUMPFlags);
#if defined(PDUMP)
		PDumpRegRead(EUR_CR_SOFT_RESET, ui32PDUMPFlags);
#endif
	}
	
	

}


static IMG_VOID SGXResetInvalDC(PVRSRV_SGXDEV_INFO	*psDevInfo,
							    IMG_UINT32			ui32PDUMPFlags,
								IMG_BOOL			bPDump)
{
	IMG_UINT32 ui32RegVal;

	
	ui32RegVal = EUR_CR_BIF_CTRL_INVALDC_MASK;
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_CTRL, ui32RegVal);
	if (bPDump)
	{
		PDUMPREGWITHFLAGS(EUR_CR_BIF_CTRL, ui32RegVal, ui32PDUMPFlags);
	}
	SGXResetSleep(psDevInfo, ui32PDUMPFlags, bPDump);

	ui32RegVal = 0;
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_CTRL, ui32RegVal);
	if (bPDump)
	{
		PDUMPREGWITHFLAGS(EUR_CR_BIF_CTRL, ui32RegVal, ui32PDUMPFlags);
	}
	SGXResetSleep(psDevInfo, ui32PDUMPFlags, bPDump);

#if !defined(SGX_FEATURE_MULTIPLE_MEM_CONTEXTS)
	{
		


		if (PollForValueKM((IMG_UINT32 *)((IMG_UINT8*)psDevInfo->pvRegsBaseKM + EUR_CR_BIF_MEM_REQ_STAT),
							0,
							EUR_CR_BIF_MEM_REQ_STAT_READS_MASK,
							MAX_HW_TIME_US/WAIT_TRY_COUNT,
							WAIT_TRY_COUNT) != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"Wait for DC invalidate failed."));
		}
		
		if (bPDump)
		{
			PDUMPREGPOLWITHFLAGS(EUR_CR_BIF_MEM_REQ_STAT, 0, EUR_CR_BIF_MEM_REQ_STAT_READS_MASK, ui32PDUMPFlags);
		}
	}
#endif 	
}


IMG_VOID SGXReset(PVRSRV_SGXDEV_INFO	*psDevInfo,
				  IMG_UINT32			 ui32PDUMPFlags)
{
	IMG_UINT32 ui32RegVal;

	const IMG_UINT32 ui32BifFaultMask =
						EUR_CR_BIF_INT_STAT_FAULT_MASK;

#if defined(SGX_FEATURE_MULTIPLE_MEM_CONTEXTS)
	IMG_UINT32			ui32BIFCtrl;
#if defined(EUR_CR_BIF_MEM_ARB_CONFIG)
	IMG_UINT32			ui32BIFMemArb;
#endif 
#endif 

#ifndef PDUMP
	PVR_UNREFERENCED_PARAMETER(ui32PDUMPFlags);
#endif 

	psDevInfo->ui32NumResets++;

	PDUMPCOMMENTWITHFLAGS(ui32PDUMPFlags, "Start of SGX reset sequence\r\n");

#if defined(FIX_HW_BRN_23944)
	
	ui32RegVal = EUR_CR_BIF_CTRL_PAUSE_MASK;
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_CTRL, ui32RegVal);
	PDUMPREGWITHFLAGS(EUR_CR_BIF_CTRL, ui32RegVal, ui32PDUMPFlags);

	SGXResetSleep(psDevInfo, ui32PDUMPFlags, IMG_TRUE);
	
	ui32RegVal = OSReadHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_INT_STAT);
	if (ui32RegVal & ui32BifFaultMask)
	{
		
		ui32RegVal = EUR_CR_BIF_CTRL_PAUSE_MASK | EUR_CR_BIF_CTRL_CLEAR_FAULT_MASK;
		OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_CTRL, ui32RegVal);
		PDUMPREGWITHFLAGS(EUR_CR_BIF_CTRL, ui32RegVal, ui32PDUMPFlags);

		SGXResetSleep(psDevInfo, ui32PDUMPFlags, IMG_TRUE);

		ui32RegVal = EUR_CR_BIF_CTRL_PAUSE_MASK;
		OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_CTRL, ui32RegVal);
		PDUMPREGWITHFLAGS(EUR_CR_BIF_CTRL, ui32RegVal, ui32PDUMPFlags);

		SGXResetSleep(psDevInfo, ui32PDUMPFlags, IMG_TRUE);
	}
#endif 

	
	SGXResetSoftReset(psDevInfo, IMG_TRUE, ui32PDUMPFlags, IMG_TRUE);

	SGXResetSleep(psDevInfo, ui32PDUMPFlags, IMG_TRUE);
	
	

#if defined(SGX_FEATURE_MULTIPLE_MEM_CONTEXTS)
	ui32RegVal = 0;
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_BANK_SET, ui32RegVal);
	PDUMPREGWITHFLAGS(EUR_CR_BIF_BANK_SET, ui32RegVal, ui32PDUMPFlags);
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_BANK0, ui32RegVal);
	PDUMPREGWITHFLAGS(EUR_CR_BIF_BANK0, ui32RegVal, ui32PDUMPFlags);

#if defined(EUR_CR_BIF_MEM_ARB_CONFIG)
	

	ui32BIFMemArb	= (12UL << EUR_CR_BIF_MEM_ARB_CONFIG_PAGE_SIZE_SHIFT) |
					  (7UL << EUR_CR_BIF_MEM_ARB_CONFIG_BEST_CNT_SHIFT) |
					  (12UL << EUR_CR_BIF_MEM_ARB_CONFIG_TTE_THRESH_SHIFT);
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_MEM_ARB_CONFIG, ui32BIFMemArb);
	PDUMPREGWITHFLAGS(EUR_CR_BIF_MEM_ARB_CONFIG, ui32BIFMemArb, ui32PDUMPFlags);
#endif 
#endif 


	




	ui32RegVal = psDevInfo->sBIFResetPDDevPAddr.uiAddr;
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_DIR_LIST_BASE0, ui32RegVal);

	SGXResetSleep(psDevInfo, ui32PDUMPFlags, IMG_FALSE);

	
	SGXResetSoftReset(psDevInfo, IMG_FALSE, ui32PDUMPFlags, IMG_TRUE);
	SGXResetSleep(psDevInfo, ui32PDUMPFlags, IMG_FALSE);

	SGXResetInvalDC(psDevInfo, ui32PDUMPFlags, IMG_FALSE);

	

	for (;;)
	{
		IMG_UINT32 ui32BifIntStat = OSReadHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_INT_STAT);
		IMG_DEV_VIRTADDR sBifFault;
		IMG_UINT32 ui32PDIndex, ui32PTIndex;

		if ((ui32BifIntStat & ui32BifFaultMask) == 0)
		{
			break;
		}
		
		


		sBifFault.uiAddr = OSReadHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_FAULT);
		PVR_DPF((PVR_DBG_WARNING, "SGXReset: Page fault 0x%x/0x%x", ui32BifIntStat, sBifFault.uiAddr));
		ui32PDIndex = sBifFault.uiAddr >> (SGX_MMU_PAGE_SHIFT + SGX_MMU_PT_SHIFT);
		ui32PTIndex = (sBifFault.uiAddr & SGX_MMU_PT_MASK) >> SGX_MMU_PAGE_SHIFT;

		
		SGXResetSoftReset(psDevInfo, IMG_TRUE, ui32PDUMPFlags, IMG_FALSE);

		
		psDevInfo->pui32BIFResetPD[ui32PDIndex] = psDevInfo->sBIFResetPTDevPAddr.uiAddr | SGX_MMU_PDE_VALID;
		psDevInfo->pui32BIFResetPT[ui32PTIndex] = psDevInfo->sBIFResetPageDevPAddr.uiAddr | SGX_MMU_PTE_VALID;

		
		ui32RegVal = OSReadHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_EVENT_STATUS);
		OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_EVENT_HOST_CLEAR, ui32RegVal);
		ui32RegVal = OSReadHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_EVENT_STATUS2);
		OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_EVENT_HOST_CLEAR2, ui32RegVal);

		SGXResetSleep(psDevInfo, ui32PDUMPFlags, IMG_FALSE);

		
		SGXResetSoftReset(psDevInfo, IMG_FALSE, ui32PDUMPFlags, IMG_FALSE);
		SGXResetSleep(psDevInfo, ui32PDUMPFlags, IMG_FALSE);

		
		SGXResetInvalDC(psDevInfo, ui32PDUMPFlags, IMG_FALSE);

		
		psDevInfo->pui32BIFResetPD[ui32PDIndex] = 0;
		psDevInfo->pui32BIFResetPT[ui32PTIndex] = 0;
	}


	

#if defined(SGX_FEATURE_MULTIPLE_MEM_CONTEXTS)
	
	ui32BIFCtrl = (SGX_BIF_DIR_LIST_INDEX_EDM << EUR_CR_BIF_BANK0_INDEX_EDM_SHIFT);
#ifdef SGX_FEATURE_2D_HARDWARE
	
	ui32BIFCtrl |= (SGX_BIF_DIR_LIST_INDEX_EDM << EUR_CR_BIF_BANK0_INDEX_2D_SHIFT);
#endif
#if defined(FIX_HW_BRN_23410)
	
	ui32BIFCtrl |= (SGX_BIF_DIR_LIST_INDEX_EDM << EUR_CR_BIF_BANK0_INDEX_TA_SHIFT);
#endif

	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_BANK0, ui32BIFCtrl);
	PDUMPREGWITHFLAGS(EUR_CR_BIF_BANK0, ui32BIFCtrl, ui32PDUMPFlags);
#endif 

	
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, SGX_BIF_DIR_LIST_REG_EDM, psDevInfo->sKernelPDDevPAddr.uiAddr);
	PDUMPPDREGWITHFLAGS(SGX_BIF_DIR_LIST_REG_EDM, psDevInfo->sKernelPDDevPAddr.uiAddr, ui32PDUMPFlags, PDUMP_PD_UNIQUETAG);

#ifdef SGX_FEATURE_2D_HARDWARE
	
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_BIF_TWOD_REQ_BASE, SGX_2D_HEAP_BASE);
	PDUMPREGWITHFLAGS(EUR_CR_BIF_TWOD_REQ_BASE, SGX_2D_HEAP_BASE, ui32PDUMPFlags);
#endif
	
	
	SGXResetInvalDC(psDevInfo, ui32PDUMPFlags, IMG_TRUE);
	
	PVR_DPF((PVR_DBG_WARNING,"Soft Reset of SGX"));
	SGXResetSleep(psDevInfo, ui32PDUMPFlags, IMG_TRUE);

	
	ui32RegVal = 0;
	OSWriteHWReg(psDevInfo->pvRegsBaseKM, EUR_CR_SOFT_RESET, ui32RegVal);
	PDUMPREGWITHFLAGS(EUR_CR_SOFT_RESET, ui32RegVal, ui32PDUMPFlags);

	
	SGXResetSleep(psDevInfo, ui32PDUMPFlags, IMG_TRUE);

	PDUMPCOMMENTWITHFLAGS(ui32PDUMPFlags, "End of SGX reset sequence\r\n");
}


