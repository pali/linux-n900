
/*
 * tiomap.c
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
 *  ======== tiomap.c ========
 *  Processor Manager Driver for TI OMAP3430 EVM.
 *
 *  Public Function:
 *      WMD_DRV_Entry
 *
 *! Revision History:
 *! ================
 *   26-March-2008 HK and AL:  Added WMD_DEV_WalkTbl funciton.
 */

/*  ----------------------------------- Host OS */
#include <dspbridge/host_os.h>
#include <mach-omap2/prm.h>
#include <mach-omap2/cm.h>
#include <mach-omap2/prm-regbits-34xx.h>
#include <mach-omap2/cm-regbits-34xx.h>
#include <mach/control.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>
#include <dspbridge/dbdefs.h>
#include <dspbridge/errbase.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>
#include <dspbridge/dbg.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/mem.h>
#include <dspbridge/util.h>
#include <dspbridge/reg.h>
#include <dspbridge/dbreg.h>
#include <dspbridge/cfg.h>
#include <dspbridge/drv.h>
#include <dspbridge/csl.h>
#include <dspbridge/sync.h>

/* ------------------------------------ Hardware Abstraction Layer */
#include <hw_defs.h>
#include <hw_dspssC64P.h>
#include <hw_prcm.h>
#include <hw_mmu.h>
#include <hw_mbox.h>

/*  ----------------------------------- Link Driver */
#include <dspbridge/wmd.h>
#include <dspbridge/wmdchnl.h>
#include <dspbridge/wmddeh.h>
#include <dspbridge/wmdio.h>
#include <dspbridge/wmdmsg.h>
#include <dspbridge/pwr.h>
#include <dspbridge/chnl_sm.h>
#include <dspbridge/io_sm.h>

/*  ----------------------------------- Platform Manager */
#include <dspbridge/dev.h>
#include <dspbridge/wcd.h>
#include <dspbridge/dmm.h>

/*  ----------------------------------- Local */
#include "_tiomap.h"
#include "_tiomap_pwr.h"
#include "_tiomap_mmu.h"
#include "_tiomap_util.h"
#include "tiomap_io.h"


/* Offset in shared mem to write to in order to synchronize start with DSP */
#define SHMSYNCOFFSET 4		/* GPP byte offset */

#define BUFFERSIZE 1024

#define MMU_SECTION_ADDR_MASK    0xFFF00000
#define MMU_SSECTION_ADDR_MASK   0xFF000000
#define MMU_LARGE_PAGE_MASK      0xFFFF0000
#define MMU_SMALL_PAGE_MASK      0xFFFFF000
#define PAGES_II_LVL_TABLE   512

#define MMU_GFLUSH 0x60

/* Forward Declarations: */
static DSP_STATUS WMD_BRD_Monitor(struct WMD_DEV_CONTEXT *pDevContext);
static DSP_STATUS WMD_BRD_Read(struct WMD_DEV_CONTEXT *pDevContext,
			OUT u8 *pbHostBuf,
			u32 dwDSPAddr, u32 ulNumBytes, u32 ulMemType);
static DSP_STATUS WMD_BRD_Start(struct WMD_DEV_CONTEXT *pDevContext,
			u32 dwDSPAddr);
static DSP_STATUS WMD_BRD_Status(struct WMD_DEV_CONTEXT *pDevContext,
			OUT BRD_STATUS *pdwState);
static DSP_STATUS WMD_BRD_Stop(struct WMD_DEV_CONTEXT *pDevContext);
static DSP_STATUS WMD_BRD_Write(struct WMD_DEV_CONTEXT *pDevContext,
			IN u8 *pbHostBuf,
			u32 dwDSPAddr, u32 ulNumBytes, u32 ulMemType);
static DSP_STATUS WMD_BRD_SetState(struct WMD_DEV_CONTEXT *hDevContext,
			u32 ulBrdState);
static DSP_STATUS WMD_BRD_MemCopy(struct WMD_DEV_CONTEXT *hDevContext,
			u32 ulDspDestAddr, u32 ulDspSrcAddr,
			u32 ulNumBytes, u32 ulMemType);
static DSP_STATUS WMD_BRD_MemWrite(struct WMD_DEV_CONTEXT *pDevContext,
			IN u8 *pbHostBuf, u32 dwDSPAddr,
			u32 ulNumBytes, u32 ulMemType);
static DSP_STATUS WMD_BRD_MemMap(struct WMD_DEV_CONTEXT *hDevContext,
			u32 ulMpuAddr, u32 ulVirtAddr, u32 ulNumBytes,
			u32 ulMapAttr);
static DSP_STATUS WMD_BRD_MemUnMap(struct WMD_DEV_CONTEXT *hDevContext,
			u32 ulVirtAddr, u32 ulNumBytes);
static DSP_STATUS WMD_DEV_Create(OUT struct WMD_DEV_CONTEXT **ppDevContext,
			struct DEV_OBJECT *hDevObject,
			IN CONST struct CFG_HOSTRES *pConfig,
			IN CONST struct CFG_DSPRES *pDspConfig);
static DSP_STATUS WMD_DEV_Ctrl(struct WMD_DEV_CONTEXT *pDevContext, u32 dwCmd,
			IN OUT void *pArgs);
static DSP_STATUS WMD_DEV_Destroy(struct WMD_DEV_CONTEXT *pDevContext);
static DSP_STATUS TIOMAP_VirtToPhysical(struct mm_struct *mm, u32 ulMpuAddr,
			u32 ulNumBytes, u32 *numOfTableEntries,
			u32 *physicalAddrTable);
static DSP_STATUS PteUpdate(struct WMD_DEV_CONTEXT *hDevContext, u32 pa,
			u32 va, u32 size,
			struct HW_MMUMapAttrs_t *mapAttrs);
static DSP_STATUS PteSet(struct PgTableAttrs *pt, u32 pa, u32 va,
			u32 size, struct HW_MMUMapAttrs_t *attrs);
static DSP_STATUS MemMapVmalloc(struct WMD_DEV_CONTEXT *hDevContext,
			u32 ulMpuAddr, u32 ulVirtAddr,
			u32 ulNumBytes, u32 ulMapAttr);
static DSP_STATUS run_IdleBoot(u32 prcm_base, u32 cm_base,
			u32 sysctrl_base);
void GetHWRegs(u32 prcm_base, u32 cm_base);

/*  ----------------------------------- Globals */

/* Attributes of L2 page tables for DSP MMU */
struct PageInfo {
	u32 numEntries;	/* Number of valid PTEs in the L2 PT */
} ;

/* Attributes used to manage the DSP MMU page tables */
struct PgTableAttrs {
	struct SYNC_CSOBJECT *hCSObj;	/* Critical section object handle */

	u32 L1BasePa;		/* Physical address of the L1 PT */
	u32 L1BaseVa;		/* Virtual  address of the L1 PT */
	u32 L1size;		/* Size of the L1 PT */
	u32 L1TblAllocPa;
	/* Physical address of Allocated mem for L1 table. May not be aligned */
	u32 L1TblAllocVa;
	/* Virtual address of Allocated mem for L1 table. May not be aligned */
	u32 L1TblAllocSz;
	/* Size of consistent memory allocated for L1 table.
	 * May not be aligned */

	u32 L2BasePa;		/* Physical address of the L2 PT */
	u32 L2BaseVa;		/* Virtual  address of the L2 PT */
	u32 L2size;		/* Size of the L2 PT */
	u32 L2TblAllocPa;
	/* Physical address of Allocated mem for L2 table. May not be aligned */
	u32 L2TblAllocVa;
	/* Virtual address of Allocated mem for L2 table. May not be aligned */
	u32 L2TblAllocSz;
	/* Size of consistent memory allocated for L2 table.
	 * May not be aligned */

	u32 L2NumPages;	/* Number of allocated L2 PT */
	struct PageInfo *pgInfo;  /* Array [L2NumPages] of L2 PT info structs */
} ;

/*
 *  If dsp_debug is true, do not branch to the DSP entry point and wait for DSP
 *  to boot
 */
extern s32 dsp_debug;


/*
 *  This mini driver's function interface table.
 */
static struct WMD_DRV_INTERFACE drvInterfaceFxns = {
	WCD_MAJOR_VERSION,  /* WCD ver. for which this mini driver is built. */
	WCD_MINOR_VERSION,
	WMD_DEV_Create,
	WMD_DEV_Destroy,
	WMD_DEV_Ctrl,
	WMD_BRD_Monitor,
	WMD_BRD_Start,
	WMD_BRD_Stop,
	WMD_BRD_Status,
	WMD_BRD_Read,
	WMD_BRD_Write,
	WMD_BRD_SetState,
	WMD_BRD_MemCopy,
	WMD_BRD_MemWrite,
	WMD_BRD_MemMap,
	WMD_BRD_MemUnMap,
	/* The following CHNL functions are provided by chnl_io.lib: */
	WMD_CHNL_Create,
	WMD_CHNL_Destroy,
	WMD_CHNL_Open,
	WMD_CHNL_Close,
	WMD_CHNL_AddIOReq,
	WMD_CHNL_GetIOC,
	WMD_CHNL_CancelIO,
	WMD_CHNL_FlushIO,
	WMD_CHNL_GetInfo,
	WMD_CHNL_GetMgrInfo,
	WMD_CHNL_Idle,
	WMD_CHNL_RegisterNotify,
	/* The following DEH functions are provided by tihelen_ue_deh.c */
	WMD_DEH_Create,
	WMD_DEH_Destroy,
	WMD_DEH_Notify,
	WMD_DEH_RegisterNotify,
	WMD_DEH_GetInfo,
	/* The following IO functions are provided by chnl_io.lib: */
	WMD_IO_Create,
	WMD_IO_Destroy,
	WMD_IO_OnLoaded,
	WMD_IO_GetProcLoad,
	/* The following MSG functions are provided by chnl_io.lib: */
	WMD_MSG_Create,
	WMD_MSG_CreateQueue,
	WMD_MSG_Delete,
	WMD_MSG_DeleteQueue,
	WMD_MSG_Get,
	WMD_MSG_Put,
	WMD_MSG_RegisterNotify,
	WMD_MSG_SetQueueId,
};

static inline void tlb_flush_all(const u32 base)
{
    __raw_writeb(__raw_readb(base + MMU_GFLUSH) | 1, base + MMU_GFLUSH);
}

static inline void flush_all(struct WMD_DEV_CONTEXT *pDevContext)
{
	struct CFG_HOSTRES resources;
	u32 temp = 0;

	CFG_GetHostResources((struct CFG_DEVNODE *)DRV_GetFirstDevExtension(), &resources);
	HW_PWRST_IVA2RegGet(resources.dwPrmBase, &temp);

	if ((temp & HW_PWR_STATE_ON) == HW_PWR_STATE_OFF ||
	    (temp & HW_PWR_STATE_ON) == HW_PWR_STATE_RET) {
		CLK_Enable(SERVICESCLK_iva2_ck);
		WakeDSP(pDevContext, NULL);
		tlb_flush_all(pDevContext->dwDSPMmuBase);
		CLK_Disable(SERVICESCLK_iva2_ck);
	} else
		tlb_flush_all(pDevContext->dwDSPMmuBase);
}

/*
 *  ======== WMD_DRV_Entry ========
 *  purpose:
 *      Mini Driver entry point.
 */
void WMD_DRV_Entry(OUT struct WMD_DRV_INTERFACE **ppDrvInterface,
	      IN CONST char *pstrWMDFileName)
{

	DBC_Require(pstrWMDFileName != NULL);
	DBG_Trace(DBG_ENTER, "In the WMD_DRV_Entry \n");

       IO_SM_init(); /* Initialization of io_sm module */

       if (strcmp(pstrWMDFileName, "UMA") == 0)
		*ppDrvInterface = &drvInterfaceFxns;
	else
		DBG_Trace(DBG_LEVEL7, "WMD_DRV_Entry Unknown WMD file name");

}

/*
 *  ======== WMD_BRD_Monitor ========
 *  purpose:
 *      This WMD_BRD_Monitor puts DSP into a Loadable state.
 *      i.e Application can load and start the device.
 *
 *  Preconditions:
 *      Device in 'OFF' state.
 */
static DSP_STATUS WMD_BRD_Monitor(struct WMD_DEV_CONTEXT *hDevContext)
{
	DSP_STATUS status = DSP_SOK;
	struct WMD_DEV_CONTEXT *pDevContext = hDevContext;
	struct CFG_HOSTRES resources;
	u32 temp;
	enum HW_PwrState_t    pwrState;

	DBG_Trace(DBG_ENTER, "Board in the monitor state  \n");
	status = CFG_GetHostResources(
		 (struct CFG_DEVNODE *)DRV_GetFirstDevExtension(), &resources);
	if (DSP_FAILED(status))
		goto error_return;

	GetHWRegs(resources.dwPrmBase, resources.dwCmBase);
	HW_PWRST_IVA2RegGet(resources.dwPrmBase, &temp);
    if ((temp & 0x03) != 0x03 || (temp & 0x03) != 0x02) {
		/* IVA2 is not in ON state */
		/* Read and set PM_PWSTCTRL_IVA2  to ON */
		HW_PWR_IVA2PowerStateSet(resources.dwPrmBase,
					  HW_PWR_DOMAIN_DSP,
					  HW_PWR_STATE_ON);
		/* Set the SW supervised state transition */
		HW_PWR_CLKCTRL_IVA2RegSet(resources.dwCmBase,
					   HW_SW_SUP_WAKEUP);
		/* Wait until the state has moved to ON */
		HW_PWR_IVA2StateGet(resources.dwPrmBase, HW_PWR_DOMAIN_DSP,
				     &pwrState);
		/* Disable Automatic transition */
	HW_PWR_CLKCTRL_IVA2RegSet(resources.dwCmBase, HW_AUTOTRANS_DIS);
	}
	DBG_Trace(DBG_LEVEL6, "WMD_BRD_Monitor - Middle ****** \n");
	GetHWRegs(resources.dwPrmBase, resources.dwCmBase);
	status = run_IdleBoot(resources.dwPrmBase, resources.dwCmBase,
			      resources.dwSysCtrlBase);
	if (DSP_SUCCEEDED(status)) {
		/* set the device state to IDLE */
		pDevContext->dwBrdState = BRD_IDLE;

	}
error_return:
	DBG_Trace(DBG_LEVEL6, "WMD_BRD_Monitor - End ****** \n");
	GetHWRegs(resources.dwPrmBase, resources.dwCmBase);
	return status;
}

/*
 *  ======== WMD_BRD_Read ========
 *  purpose:
 *      Reads buffers for DSP memory.
 */
static DSP_STATUS WMD_BRD_Read(struct WMD_DEV_CONTEXT *hDevContext,
			       OUT u8 *pbHostBuf, u32 dwDSPAddr,
			       u32 ulNumBytes, u32 ulMemType)
{
	DSP_STATUS status = DSP_SOK;
	struct WMD_DEV_CONTEXT *pDevContext = hDevContext;
	u32 offset;
	u32 dspBaseAddr = hDevContext->dwDspBaseAddr;

	DBG_Trace(DBG_ENTER, "WMD_BRD_Read, pDevContext: 0x%x\n\t\tpbHostBuf:"
		  " 0x%x\n\t\tdwDSPAddr:  0x%x\n\t\tulNumBytes:  0x%x\n\t\t"
		  "ulMemType:  0x%x\n", pDevContext, pbHostBuf,
		  dwDSPAddr, ulNumBytes, ulMemType);
	if (dwDSPAddr < pDevContext->dwDSPStartAdd) {
		DBG_Trace(DBG_LEVEL7,
			  "WMD_BRD_Read: DSP address < start address \n ");
		status = DSP_EFAIL;
		return status;
	}
	/* change here to account for the 3 bands of the DSP internal memory */
	if ((dwDSPAddr - pDevContext->dwDSPStartAdd) <
	    pDevContext->dwInternalSize) {
		offset = dwDSPAddr - pDevContext->dwDSPStartAdd;
	} else {
		DBG_Trace(DBG_LEVEL1,
			  "**** Reading From external memory ****  \n ");
		status = ReadExtDspData(pDevContext, pbHostBuf, dwDSPAddr,
					ulNumBytes, ulMemType);
		return status;
	}
	/* copy the data from  DSP memory, */
	memcpy(pbHostBuf, (void *)(dspBaseAddr + offset), ulNumBytes);
	return status;
}

/*
 *  ======== WMD_BRD_SetState ========
 *  purpose:
 *      This routine updates the Board status.
 */
static DSP_STATUS WMD_BRD_SetState(struct WMD_DEV_CONTEXT *hDevContext,
				   u32 ulBrdState)
{
	DSP_STATUS status = DSP_SOK;
	struct WMD_DEV_CONTEXT *pDevContext = hDevContext;

	DBG_Trace(DBG_ENTER, "WMD_BRD_SetState: Board State: 0x%x \n",
		  ulBrdState);
	pDevContext->dwBrdState = ulBrdState;
	return status;
}

/*
 *  ======== WMD_BRD_Start ========
 *  purpose:
 *      Initializes DSP MMU and Starts DSP.
 *
 *  Preconditions:
 *  a) DSP domain is 'ACTIVE'.
 *  b) DSP_RST1 is asserted.
 *  b) DSP_RST2 is released.
 */
static DSP_STATUS WMD_BRD_Start(struct WMD_DEV_CONTEXT *hDevContext,
				u32 dwDSPAddr)
{
	DSP_STATUS status = DSP_SOK;
	struct WMD_DEV_CONTEXT *pDevContext = hDevContext;
	u32 dwSyncAddr = 0;
	u32 ulShmBase;	/* Gpp Phys SM base addr(byte) */
	u32 ulShmBaseVirt;	/* Dsp Virt SM base addr */
	u32 ulTLBBaseVirt;	/* Base of MMU TLB entry */
	u32 ulShmOffsetVirt;	/* offset of ulShmBaseVirt from ulTLBBaseVirt */
	s32 iEntryNdx;
	s32 itmpEntryNdx = 0;	/* DSP-MMU TLB entry base address */
	struct CFG_HOSTRES resources;
	u32 temp;
	u32 ulDspClkRate;
	u32 ulDspClkAddr;
	u32 ulBiosGpTimer;
	u32 uClkCmd;
	struct IO_MGR *hIOMgr;
	u32 ulLoadMonitorTimer;
	u32 extClkId = 0;
	u32 tmpIndex;
	u32 clkIdIndex = MBX_PM_MAX_RESOURCES;

	DBG_Trace(DBG_ENTER, "Entering WMD_BRD_Start:\n hDevContext: 0x%x\n\t "
			     "dwDSPAddr: 0x%x\n", hDevContext, dwDSPAddr);

	 /* The device context contains all the mmu setup info from when the
	 * last dsp base image was loaded. The first entry is always
	 * SHMMEM base. */
	/* Get SHM_BEG - convert to byte address */
	(void) DEV_GetSymbol(pDevContext->hDevObject, SHMBASENAME,
			     &ulShmBaseVirt);
	ulShmBaseVirt *= DSPWORDSIZE;
	DBC_Assert(ulShmBaseVirt != 0);
	/* DSP Virtual address */
	ulTLBBaseVirt = pDevContext->aTLBEntry[0].ulDspVa;
	DBC_Assert(ulTLBBaseVirt <= ulShmBaseVirt);
	ulShmOffsetVirt = ulShmBaseVirt - (ulTLBBaseVirt * DSPWORDSIZE);
	/* Kernel logical address */
	ulShmBase = pDevContext->aTLBEntry[0].ulGppVa + ulShmOffsetVirt;

	DBC_Assert(ulShmBase != 0);
	/* 2nd wd is used as sync field */
	dwSyncAddr = ulShmBase + SHMSYNCOFFSET;
	 /* Write a signature into the SHM base + offset; this will
	 * get cleared when the DSP program starts.  */
	if ((ulShmBaseVirt == 0) || (ulShmBase == 0)) {
		DBG_Trace(DBG_LEVEL6, "WMD_BRD_Start: Illegal SM base\n");
		status = DSP_EFAIL;
	} else
		*((volatile u32 *)dwSyncAddr) = 0xffffffff;

	if (DSP_SUCCEEDED(status)) {
		status = CFG_GetHostResources(
			(struct CFG_DEVNODE *)DRV_GetFirstDevExtension(),
			&resources);
		/* Assert RST1 i.e only the RST only for DSP megacell  */
		/* HW_RST_Reset(resources.dwPrcmBase, HW_RST1_IVA2);*/
		if (DSP_SUCCEEDED(status)) {
			HW_RST_Reset(resources.dwPrmBase, HW_RST1_IVA2);
			if (dsp_debug) {
				/* Set the bootmode to self loop  */
				DBG_Trace(DBG_LEVEL7,
						"Set boot mode to self loop"
						" for IVA2 Device\n");
				HW_DSPSS_BootModeSet(resources.dwSysCtrlBase,
					HW_DSPSYSC_SELFLOOPBOOT, dwDSPAddr);
			} else {
				/* Set the bootmode to '0' - direct boot */
				DBG_Trace(DBG_LEVEL7,
						"Set boot mode to direct"
						" boot for IVA2 Device \n");
				HW_DSPSS_BootModeSet(resources.dwSysCtrlBase,
					HW_DSPSYSC_DIRECTBOOT, dwDSPAddr);
			}
		}
	}
	if (DSP_SUCCEEDED(status)) {
		/* Reset and Unreset the RST2, so that BOOTADDR is copied to
		 * IVA2 SYSC register */
		HW_RST_Reset(resources.dwPrmBase, HW_RST2_IVA2);
		udelay(100);
		HW_RST_UnReset(resources.dwPrmBase, HW_RST2_IVA2);
		udelay(100);
		DBG_Trace(DBG_LEVEL6, "WMD_BRD_Start 0 ****** \n");
		GetHWRegs(resources.dwPrmBase, resources.dwCmBase);
		/* Disbale the DSP MMU */
		HW_MMU_Disable(resources.dwDmmuBase);
		/* Disable TWL */
		HW_MMU_TWLDisable(resources.dwDmmuBase);

		/* Only make TLB entry if both addresses are non-zero */
		for (iEntryNdx = 0; iEntryNdx < WMDIOCTL_NUMOFMMUTLB;
			iEntryNdx++) {
			if ((pDevContext->aTLBEntry[iEntryNdx].ulGppPa != 0) &&
                               (pDevContext->aTLBEntry[iEntryNdx].ulDspVa != 0)) {
				DBG_Trace(DBG_LEVEL4, "** (proc) MMU %d GppPa:"
                                       " 0x%x DspVa 0x%x Size 0x%x\n",
                                       itmpEntryNdx,
                                       pDevContext->aTLBEntry[iEntryNdx].ulGppPa,
                                       pDevContext->aTLBEntry[iEntryNdx].ulDspVa,
                                       pDevContext->aTLBEntry[iEntryNdx].ulSize);
				configureDspMmu(pDevContext,
                                       pDevContext->aTLBEntry[iEntryNdx].ulGppPa,
                                       pDevContext->aTLBEntry[iEntryNdx].ulDspVa *
						DSPWORDSIZE,
                                       pDevContext->aTLBEntry[iEntryNdx].ulSize,
                                       itmpEntryNdx,
                                       pDevContext->aTLBEntry[iEntryNdx].endianism,
                                       pDevContext->aTLBEntry[iEntryNdx].elemSize,
                                       pDevContext->aTLBEntry[iEntryNdx].
						mixedMode);
				itmpEntryNdx++;
			}
		}		/* end for */
	}

	/* Lock the above TLB entries and get the BIOS and load monitor timer
	 * information*/
	if (DSP_SUCCEEDED(status)) {
		HW_MMU_NumLockedSet(resources.dwDmmuBase, itmpEntryNdx);
		HW_MMU_VictimNumSet(resources.dwDmmuBase, itmpEntryNdx);
		HW_MMU_TTBSet(resources.dwDmmuBase,
				pDevContext->pPtAttrs->L1BasePa);
		HW_MMU_TWLEnable(resources.dwDmmuBase);
		/* Enable the SmartIdle and AutoIdle bit for MMU_SYSCONFIG */

		temp = (u32) *((REG_UWORD32 *)
				((u32) (resources.dwDmmuBase) + 0x10));
		temp = (temp & 0xFFFFFFEF) | 0x11;
		*((REG_UWORD32 *) ((u32) (resources.dwDmmuBase) + 0x10)) =
			(u32) temp;

		/* Let the DSP MMU run */
		HW_MMU_Enable(resources.dwDmmuBase);

		/* Enable the BIOS clock  */
		(void)DEV_GetSymbol(pDevContext->hDevObject,
					BRIDGEINIT_BIOSGPTIMER,
				     &ulBiosGpTimer);
		DBG_Trace(DBG_LEVEL7, "BIOS GPTimer : 0x%x\n", ulBiosGpTimer);
		(void)DEV_GetSymbol(pDevContext->hDevObject,
				BRIDGEINIT_LOADMON_GPTIMER,
				     &ulLoadMonitorTimer);
		DBG_Trace(DBG_LEVEL7, "Load Monitor Timer : 0x%x\n",
			  ulLoadMonitorTimer);
	}

	if (DSP_SUCCEEDED(status)) {
		if (ulLoadMonitorTimer != 0xFFFF) {
			uClkCmd = (BPWR_DisableClock << MBX_PM_CLK_CMDSHIFT) |
						ulLoadMonitorTimer;
			DBG_Trace(DBG_LEVEL7,
                                       "encoded LoadMonitor cmd for Disable: 0x%x\n",
                                       uClkCmd);
			DSPPeripheralClkCtrl(pDevContext, &uClkCmd);

			extClkId = uClkCmd & MBX_PM_CLK_IDMASK;
			for (tmpIndex = 0; tmpIndex < MBX_PM_MAX_RESOURCES;
                                       tmpIndex++) {
				if (extClkId == BPWR_CLKID[tmpIndex]) {
					clkIdIndex = tmpIndex;
					break;
				}
			}

			if (clkIdIndex < MBX_PM_MAX_RESOURCES)
				status = CLK_Set_32KHz(
						BPWR_Clks[clkIdIndex].funClk);
			else
				status = DSP_EFAIL;

			if (DSP_FAILED(status)) {
				DBG_Trace(DBG_LEVEL7, " Error while setting"
							"LM Timer  to 32KHz\n");
			}
			uClkCmd = (BPWR_EnableClock << MBX_PM_CLK_CMDSHIFT) |
				  ulLoadMonitorTimer;
			DBG_Trace(DBG_LEVEL7,
				 "encoded LoadMonitor cmd for Enable : 0x%x\n",
				 uClkCmd);
			DSPPeripheralClkCtrl(pDevContext, &uClkCmd);

		} else {
			DBG_Trace(DBG_LEVEL7,
				  "Not able to get the symbol for Load "
				  "Monitor Timer\n");
		}
	}

	if (DSP_SUCCEEDED(status)) {
		if (ulBiosGpTimer != 0xFFFF) {
			uClkCmd = (BPWR_DisableClock << MBX_PM_CLK_CMDSHIFT) |
								ulBiosGpTimer;
			DBG_Trace(DBG_LEVEL7, "encoded BIOS GPTimer cmd for"
					"Disable: 0x%x\n", uClkCmd);
			DSPPeripheralClkCtrl(pDevContext, &uClkCmd);

			extClkId = uClkCmd & MBX_PM_CLK_IDMASK;

			for (tmpIndex = 0; tmpIndex < MBX_PM_MAX_RESOURCES;
			     tmpIndex++) {
				if (extClkId == BPWR_CLKID[tmpIndex]) {
					clkIdIndex = tmpIndex;
					break;
				}
			}

			if (clkIdIndex < MBX_PM_MAX_RESOURCES)
				status = CLK_Set_32KHz(
						BPWR_Clks[clkIdIndex].funClk);
			else
				status = DSP_EFAIL;

			if (DSP_FAILED(status)) {
				DBG_Trace(DBG_LEVEL7,
				" Error while setting BIOS Timer  to 32KHz\n");
			}

			uClkCmd = (BPWR_EnableClock << MBX_PM_CLK_CMDSHIFT) |
				   ulBiosGpTimer;
			DBG_Trace(DBG_LEVEL7, "encoded BIOS GPTimer cmd :"
						"0x%x\n", uClkCmd);
			DSPPeripheralClkCtrl(pDevContext, &uClkCmd);

		} else {
		DBG_Trace(DBG_LEVEL7,
                               "Not able to get the symbol for BIOS Timer\n");
		}
	}

	if (DSP_SUCCEEDED(status)) {
		/* Set the DSP clock rate */
		(void)DEV_GetSymbol(pDevContext->hDevObject,
					"_BRIDGEINIT_DSP_FREQ", &ulDspClkAddr);
               /*Set Autoidle Mode for IVA2 PLL */
               temp = (u32) *((REG_UWORD32 *)
                               ((u32) (resources.dwCmBase) + 0x34));
               temp = (temp & 0xFFFFFFFE) | 0x1;
               *((REG_UWORD32 *) ((u32) (resources.dwCmBase) + 0x34)) =
                       (u32) temp;
		DBG_Trace(DBG_LEVEL5, "WMD_BRD_Start: _BRIDGE_DSP_FREQ Addr:"
				"0x%x \n", ulDspClkAddr);
		if ((unsigned int *)ulDspClkAddr != NULL) {
			/* Get the clock rate */
			status = CLK_GetRate(SERVICESCLK_iva2_ck,
				 &ulDspClkRate);
			DBG_Trace(DBG_LEVEL5,
				 "WMD_BRD_Start: DSP clock rate (KHZ): 0x%x \n",
				 ulDspClkRate);
			(void)WMD_BRD_Write(pDevContext, (u8 *)&ulDspClkRate,
				 ulDspClkAddr, sizeof(u32), 0);
		}
/*PM_IVA2GRPSEL_PER = 0xC0;*/
               temp = (u32) *((REG_UWORD32 *)
                               ((u32) (resources.dwPerPmBase) + 0xA8));
               temp = (temp & 0xFFFFFF30) | 0xC0;
               *((REG_UWORD32 *) ((u32) (resources.dwPerPmBase) + 0xA8)) =
                       (u32) temp;

/*PM_MPUGRPSEL_PER &= 0xFFFFFF3F;*/
               temp = (u32) *((REG_UWORD32 *)
                               ((u32) (resources.dwPerPmBase) + 0xA4));
               temp = (temp & 0xFFFFFF3F);
               *((REG_UWORD32 *) ((u32) (resources.dwPerPmBase) + 0xA4)) =
                       (u32) temp;
/*CM_SLEEPDEP_PER |= 0x04;*/
               temp = (u32) *((REG_UWORD32 *)
                               ((u32) (resources.dwPerBase) + 0x44));
               temp = (temp & 0xFFFFFFFB) | 0x04;
               *((REG_UWORD32 *) ((u32) (resources.dwPerBase) + 0x44)) =
                       (u32) temp;

/*CM_CLKSTCTRL_IVA2 = 0x00000003 -To Allow automatic transitions*/
               temp = (u32) *((REG_UWORD32 *)
                               ((u32) (resources.dwCmBase) + 0x48));
               temp = (temp & 0xFFFFFFFC) | 0x03;
               *((REG_UWORD32 *) ((u32) (resources.dwCmBase) + 0x48)) =
                       (u32) temp;


		/* Enable Mailbox events and also drain any pending
		 * stale messages */
		(void)CHNLSM_EnableInterrupt(pDevContext);
	}

	if (DSP_SUCCEEDED(status)) {
		HW_RSTCTRL_RegGet(resources.dwPrmBase, HW_RST1_IVA2, &temp);
		DBG_Trace(DBG_LEVEL7, "BRD_Start: RM_RSTCTRL_DSP = 0x%x \n",
				temp);
		HW_RSTST_RegGet(resources.dwPrmBase, HW_RST1_IVA2, &temp);
		DBG_Trace(DBG_LEVEL7, "BRD_Start0: RM_RSTST_DSP = 0x%x \n",
				temp);

		/* Let DSP go */
		DBG_Trace(DBG_LEVEL7, "Unreset, WMD_BRD_Start\n");
		/* Enable DSP MMU Interrupts */
		HW_MMU_EventEnable(resources.dwDmmuBase,
				HW_MMU_ALL_INTERRUPTS);
		/* release the RST1, DSP starts executing now .. */
		HW_RST_UnReset(resources.dwPrmBase, HW_RST1_IVA2);

		HW_RSTST_RegGet(resources.dwPrmBase, HW_RST1_IVA2, &temp);
		DBG_Trace(DBG_LEVEL7, "BRD_Start: RM_RSTST_DSP = 0x%x \n",
				temp);
		HW_RSTCTRL_RegGet(resources.dwPrmBase, HW_RST1_IVA2, &temp);
		DBG_Trace(DBG_LEVEL5, "WMD_BRD_Start: CM_RSTCTRL_DSP: 0x%x \n",
				temp);
		DBG_Trace(DBG_LEVEL7, "Driver waiting for Sync @ 0x%x \n",
				dwSyncAddr);
		DBG_Trace(DBG_LEVEL7, "DSP c_int00 Address =  0x%x \n",
				dwDSPAddr);
		if (dsp_debug)
			while (*((volatile u16 *)dwSyncAddr))
				;;
	}

	if (DSP_SUCCEEDED(status)) {
		/* Wait for DSP to clear word in shared memory */
		/* Read the Location */
		if (!WaitForStart(pDevContext, dwSyncAddr)) {
			status = WMD_E_TIMEOUT;
			DBG_Trace(DBG_LEVEL7,
				 "WMD_BRD_Start Failed to Synchronize\n");
		}
		status = DEV_GetIOMgr(pDevContext->hDevObject, &hIOMgr);
		if (DSP_SUCCEEDED(status)) {
			IO_SHMsetting(hIOMgr, SHM_OPPINFO, NULL);
			DBG_Trace(DBG_LEVEL7,
			"WMD_BRD_Start: OPP information initialzed\n");
			/* Write the synchronization bit to indicate the
			 * completion of OPP table update to DSP
			 */
			*((volatile u32 *)dwSyncAddr) = 0XCAFECAFE;
		}
		if (DSP_SUCCEEDED(status)) {
			/* update board state */
			pDevContext->dwBrdState = BRD_RUNNING;
			/* (void)CHNLSM_EnableInterrupt(pDevContext);*/
			DBG_Trace(DBG_LEVEL7, "Device Started \n ");
		} else {
			pDevContext->dwBrdState = BRD_UNKNOWN;
			DBG_Trace(DBG_LEVEL7, "Device not Started \n ");
		}
	}
	return status;
}

/*
 *  ======== WMD_BRD_Stop ========
 *  purpose:
 *      Puts DSP in self loop.
 *
 *  Preconditions :
 *  a) None
 */
static DSP_STATUS WMD_BRD_Stop(struct WMD_DEV_CONTEXT *hDevContext)
{
	DSP_STATUS status = DSP_SOK;
	struct WMD_DEV_CONTEXT *pDevContext = hDevContext;
	struct CFG_HOSTRES resources;
	struct PgTableAttrs *pPtAttrs;
	u32 dspPwrState;
	DSP_STATUS clk_status;

	DBG_Trace(DBG_ENTER, "Entering WMD_BRD_Stop:\nhDevContext: 0x%x\n",
		  hDevContext);

	/* Disable the mail box interrupts */
	(void)CHNLSM_DisableInterrupt(pDevContext);

	if (pDevContext->dwBrdState == BRD_STOPPED)
		return status;

	/* as per TRM, it is advised to first drive the IVA2 to 'Standby' mode,
	 * before turning off the clocks.. This is to ensure that there are no
	 * pending L3 or other transactons from IVA2 */
	status = CFG_GetHostResources(
			(struct CFG_DEVNODE *)DRV_GetFirstDevExtension(),
			&resources);
	if (DSP_FAILED(status)) {
		DBG_Trace(DBG_LEVEL7,
			  "WMD_BRD_Stop: Get Host resources failed \n");
		DBG_Trace(DBG_LEVEL1, "Device Stopp failed \n ");
		return DSP_EFAIL;
	}

	HW_PWRST_IVA2RegGet(resources.dwPrmBase, &dspPwrState);
	if (dspPwrState != HW_PWR_STATE_OFF) {

			CHNLSM_InterruptDSP2(pDevContext, MBX_PM_DSPIDLE);

			mdelay(10);

			GetHWRegs(resources.dwPrmBase, resources.dwCmBase);

		run_IdleBoot(resources.dwPrmBase, resources.dwCmBase,
			     resources.dwSysCtrlBase);

		udelay(50);

		clk_status = CLK_Disable(SERVICESCLK_iva2_ck);
		if (DSP_FAILED(clk_status)) {
			DBG_Trace(DBG_LEVEL6,
				 "\n WMD_BRD_Stop: CLK_Disable failed "
				 "for iva2_fck\n");
			}
		/* IVA2 is not in OFF state */
		/* Set PM_PWSTCTRL_IVA2  to OFF */
		HW_PWR_IVA2PowerStateSet(resources.dwPrmBase,
					  HW_PWR_DOMAIN_DSP,
					  HW_PWR_STATE_OFF);
		/* Set the SW supervised state transition for Sleep */
		HW_PWR_CLKCTRL_IVA2RegSet(resources.dwCmBase,
					   HW_SW_SUP_SLEEP);
	} else {
		clk_status = CLK_Disable(SERVICESCLK_iva2_ck);
		if (DSP_FAILED(clk_status)) {
			DBG_Trace(DBG_LEVEL6,
				 "\n WMD_BRD_Stop: Else loop CLK_Disable failed"
				 " for iva2_fck\n");
		}
	}
	udelay(10);
	/* Release the Ext Base virtual Address as the next DSP Program
	 * may have a different load address */
	if (pDevContext->dwDspExtBaseAddr)
		pDevContext->dwDspExtBaseAddr = 0;

	pDevContext->dwBrdState = BRD_STOPPED;	/* update board state */
	DBG_Trace(DBG_LEVEL1, "Device Stopped \n ");
	/* This is a good place to clear the MMU page tables as well */
	if (pDevContext->pPtAttrs) {
		pPtAttrs = pDevContext->pPtAttrs;
		memset((u8 *) pPtAttrs->L1BaseVa, 0x00, pPtAttrs->L1size);
		memset((u8 *) pPtAttrs->L2BaseVa, 0x00, pPtAttrs->L2size);
		memset((u8 *) pPtAttrs->pgInfo, 0x00,
		       (pPtAttrs->L2NumPages * sizeof(struct PageInfo)));
	}

		DBG_Trace(DBG_LEVEL6, "WMD_BRD_Stop - End ****** \n");

	return status;
}


/*
 *  ======== WMD_BRD_Delete ========
 *  purpose:
 *      Puts DSP in Low power mode
 *
 *  Preconditions :
 *  a) None
 */
static DSP_STATUS WMD_BRD_Delete(struct WMD_DEV_CONTEXT *hDevContext)
{
	DSP_STATUS status = DSP_SOK;
	struct WMD_DEV_CONTEXT *pDevContext = hDevContext;
	struct CFG_HOSTRES resources;
	struct PgTableAttrs *pPtAttrs;
	DSP_STATUS clk_status;

	DBG_Trace(DBG_ENTER, "Entering WMD_BRD_Delete:\nhDevContext: 0x%x\n",
		  hDevContext);

	/* Disable the mail box interrupts */
	(void) CHNLSM_DisableInterrupt(pDevContext);

	if (pDevContext->dwBrdState == BRD_STOPPED)
		return status;

	/* as per TRM, it is advised to first drive
	 * the IVA2 to 'Standby' mode, before turning off the clocks.. This is
	 * to ensure that there are no pending L3 or other transactons from
	 * IVA2 */
	status = CFG_GetHostResources(
		(struct CFG_DEVNODE *)DRV_GetFirstDevExtension(), &resources);
	if (DSP_FAILED(status)) {
		DBG_Trace(DBG_LEVEL7,
			 "WMD_BRD_Stop: Get Host resources failed \n");
		DBG_Trace(DBG_LEVEL1, "Device Delete failed \n ");
		return DSP_EFAIL;
	}
	status = SleepDSP(pDevContext, PWR_EMERGENCYDEEPSLEEP, NULL);
	clk_status = CLK_Disable(SERVICESCLK_iva2_ck);
	if (DSP_FAILED(clk_status)) {
		DBG_Trace(DBG_LEVEL6, "\n WMD_BRD_Stop: CLK_Disable failed for"
			  " iva2_fck\n");
	}
	/* Release the Ext Base virtual Address as the next DSP Program
	 * may have a different load address */
	if (pDevContext->dwDspExtBaseAddr)
		pDevContext->dwDspExtBaseAddr = 0;

	pDevContext->dwBrdState = BRD_STOPPED;	/* update board state */
	DBG_Trace(DBG_LEVEL1, "Device Stopped \n ");
	/* This is a good place to clear the MMU page tables as well */
	if (pDevContext->pPtAttrs) {
		pPtAttrs = pDevContext->pPtAttrs;
		memset((u8 *)pPtAttrs->L1BaseVa, 0x00, pPtAttrs->L1size);
		memset((u8 *)pPtAttrs->L2BaseVa, 0x00, pPtAttrs->L2size);
		memset((u8 *)pPtAttrs->pgInfo, 0x00,
			(pPtAttrs->L2NumPages * sizeof(struct PageInfo)));
	}
	DBG_Trace(DBG_LEVEL6, "WMD_BRD_Stop - End ****** \n");
	return status;
}


/*
 *  ======== WMD_BRD_Status ========
 *      Returns the board status.
 */
static DSP_STATUS WMD_BRD_Status(struct WMD_DEV_CONTEXT *hDevContext,
				 OUT BRD_STATUS *pdwState)
{
	struct WMD_DEV_CONTEXT *pDevContext = hDevContext;
	*pdwState = pDevContext->dwBrdState;
	return DSP_SOK;
}

/*
 *  ======== WMD_BRD_Write ========
 *      Copies the buffers to DSP internal or external memory.
 */
static DSP_STATUS WMD_BRD_Write(struct WMD_DEV_CONTEXT *hDevContext,
				IN u8 *pbHostBuf, u32 dwDSPAddr,
				u32 ulNumBytes, u32 ulMemType)
{
	DSP_STATUS status = DSP_SOK;
	struct WMD_DEV_CONTEXT *pDevContext = hDevContext;

	DBG_Trace(DBG_ENTER, "WMD_BRD_Write, pDevContext: 0x%x\n\t\t "
		 "pbHostBuf: 0x%x\n\t\tdwDSPAddr: 0x%x\n\t\tulNumBytes: "
		 "0x%x\n \t\t ulMemtype: 0x%x\n", pDevContext, pbHostBuf,
		 dwDSPAddr, ulNumBytes, ulMemType);
	if (dwDSPAddr < pDevContext->dwDSPStartAdd) {
		DBG_Trace(DBG_LEVEL7,
			 "WMD_BRD_Write: DSP address < start address \n ");
		status = DSP_EFAIL;
		return status;
	}
	if ((dwDSPAddr - pDevContext->dwDSPStartAdd) <
	   pDevContext->dwInternalSize) {
		status = WriteDspData(hDevContext, pbHostBuf, dwDSPAddr,
			 ulNumBytes, ulMemType);
	} else {
		status = WriteExtDspData(pDevContext, pbHostBuf, dwDSPAddr,
					 ulNumBytes, ulMemType, false);
	}

	DBG_Trace(DBG_ENTER, "WMD_BRD_Write, memcopy :  DspLogicAddr=0x%x \n",
			pDevContext->dwDspBaseAddr);
	return status;
}

/*
 *  ======== WMD_DEV_Create ========
 *      Creates a driver object. Puts DSP in self loop.
 */
static DSP_STATUS WMD_DEV_Create(OUT struct WMD_DEV_CONTEXT **ppDevContext,
				 struct DEV_OBJECT *hDevObject,
				 IN CONST struct CFG_HOSTRES *pConfig,
				 IN CONST struct CFG_DSPRES *pDspConfig)
{
	DSP_STATUS status = DSP_SOK;
	struct WMD_DEV_CONTEXT *pDevContext = NULL;
	s32 iEntryNdx;
	s32 tcWordSwap;
	u32 tcWordSwapSize = sizeof(tcWordSwap);
	struct CFG_HOSTRES resources;
	struct PgTableAttrs *pPtAttrs;
	u32   pg_tbl_pa;
	u32   pg_tbl_va;
	u32   align_size;

	DBG_Trace(DBG_ENTER, "WMD_DEV_Create, ppDevContext: 0x%x\n\t\t "
		  "hDevObject: 0x%x\n\t\tpConfig: 0x%x\n\t\tpDspConfig: 0x%x\n",
		  ppDevContext, hDevObject, pConfig, pDspConfig);
	 /* Allocate and initialize a data structure to contain the mini driver
	 *  state, which becomes the context for later calls into this WMD.  */
	pDevContext = MEM_Calloc(sizeof(struct WMD_DEV_CONTEXT), MEM_NONPAGED);
	if (!pDevContext) {
		DBG_Trace(DBG_ENTER, "Failed to allocate mem  \n");
		status = DSP_EMEMORY;
		goto func_end;
	}
	status = CFG_GetHostResources(
		(struct CFG_DEVNODE *)DRV_GetFirstDevExtension(), &resources);
	if (DSP_FAILED(status)) {
		DBG_Trace(DBG_ENTER, "Failed to get host resources   \n");
		status = DSP_EMEMORY;
		goto func_end;
	}

	pDevContext->dwDSPStartAdd = (u32)OMAP_GEM_BASE;
	pDevContext->dwSelfLoop = (u32)NULL;
	pDevContext->uDspPerClks = 0;
	pDevContext->dwInternalSize = OMAP_DSP_SIZE;
	/*  Clear dev context MMU table entries.
	 *  These get set on WMD_BRD_IOCTL() call after program loaded. */
	for (iEntryNdx = 0; iEntryNdx < WMDIOCTL_NUMOFMMUTLB; iEntryNdx++) {
		pDevContext->aTLBEntry[iEntryNdx].ulGppPa =
			pDevContext->aTLBEntry[iEntryNdx].ulDspVa = 0;
	}
	pDevContext->numTLBEntries = 0;
	pDevContext->dwDspBaseAddr = (u32)MEM_LinearAddress((void *)
			(pConfig->dwMemBase[3]), pConfig->dwMemLength[3]);
	if (!pDevContext->dwDspBaseAddr) {
		status = DSP_EFAIL;
		DBG_Trace(DBG_LEVEL7,
			 "WMD_DEV_Create: failed to Map the API memory\n");
	}
	pPtAttrs = MEM_Calloc(sizeof(struct PgTableAttrs), MEM_NONPAGED);
	if (pPtAttrs != NULL) {
		/* Assuming that we use only DSP's memory map
		 * until 0x4000:0000 , we would need only 1024
		 * L1 enties i.e L1 size = 4K */
		pPtAttrs->L1size = 0x1000;
		align_size = pPtAttrs->L1size;
		/* Align sizes are expected to be power of 2 */
		/* we like to get aligned on L1 table size */
		pg_tbl_va = (u32)MEM_AllocPhysMem(pPtAttrs->L1size,
		    align_size, &pg_tbl_pa);
	/* Check if the PA is aligned for us */
	if ((pg_tbl_pa) & (align_size-1)) {
                       /* PA not aligned to page table size ,
                       * try with more allocation and align */
                       MEM_FreePhysMem((void *)pg_tbl_va, pg_tbl_pa, pPtAttrs->L1size);
                       /* we like to get aligned on L1 table size */
                       pg_tbl_va = (u32) MEM_AllocPhysMem((pPtAttrs->L1size)*2,
                       align_size, &pg_tbl_pa);
                       /* We should be able to get aligned table now */
                       pPtAttrs->L1TblAllocPa = pg_tbl_pa;
                       pPtAttrs->L1TblAllocVa = pg_tbl_va;
                       pPtAttrs->L1TblAllocSz = pPtAttrs->L1size * 2;
                       /* Align the PA to the next 'align'  boundary */
                       pPtAttrs->L1BasePa = ((pg_tbl_pa) + (align_size-1)) &
				 (~(align_size-1));
                       pPtAttrs->L1BaseVa = pg_tbl_va + (pPtAttrs->L1BasePa -
				 pg_tbl_pa);
	} else {
                       /* We got aligned PA, cool */
                       pPtAttrs->L1TblAllocPa = pg_tbl_pa;
                       pPtAttrs->L1TblAllocVa = pg_tbl_va;
                       pPtAttrs->L1TblAllocSz = pPtAttrs->L1size;
                       pPtAttrs->L1BasePa = pg_tbl_pa;
                       pPtAttrs->L1BaseVa = pg_tbl_va;
	}
       if (pPtAttrs->L1BaseVa)
                       memset((u8 *)pPtAttrs->L1BaseVa, 0x00, pPtAttrs->L1size);

		/* number of L2 page tables = DMM pool used + SHMMEM +EXTMEM +
		 * L4 pages */
		pPtAttrs->L2NumPages = ((DMMPOOLSIZE >> 20) + 6);
		pPtAttrs->L2size = HW_MMU_COARSE_PAGE_SIZE *
				   pPtAttrs->L2NumPages;
		align_size = 4; /* Make it u32 aligned  */
		/* we like to get aligned on L1 table size */
		pg_tbl_va = (u32)MEM_AllocPhysMem(pPtAttrs->L2size,
			    align_size, &pg_tbl_pa);
	pPtAttrs->L2TblAllocPa = pg_tbl_pa;
	pPtAttrs->L2TblAllocVa = pg_tbl_va;
	pPtAttrs->L2TblAllocSz = pPtAttrs->L2size;
	pPtAttrs->L2BasePa = pg_tbl_pa;
	pPtAttrs->L2BaseVa = pg_tbl_va;

       if (pPtAttrs->L2BaseVa)
                       memset((u8 *)pPtAttrs->L2BaseVa, 0x00, pPtAttrs->L2size);

	pPtAttrs->pgInfo = MEM_Calloc(pPtAttrs->L2NumPages *
				sizeof(struct PageInfo), MEM_NONPAGED);
		DBG_Trace(DBG_LEVEL1, "L1 pa %x, va %x, size %x\n L2 pa %x, va "
			 "%x, size %x\n", pPtAttrs->L1BasePa,
			 pPtAttrs->L1BaseVa, pPtAttrs->L1size,
			 pPtAttrs->L2BasePa, pPtAttrs->L2BaseVa,
			 pPtAttrs->L2size);
		DBG_Trace(DBG_LEVEL1, "pPtAttrs %x L2 NumPages %x pgInfo %x\n",
			 pPtAttrs, pPtAttrs->L2NumPages, pPtAttrs->pgInfo);
	}
	if ((pPtAttrs != NULL) && (pPtAttrs->L1BaseVa != 0) &&
	   (pPtAttrs->L2BaseVa != 0) && (pPtAttrs->pgInfo != NULL))
		pDevContext->pPtAttrs = pPtAttrs;
	else
		status = DSP_EMEMORY;

	if (DSP_SUCCEEDED(status))
		status = SYNC_InitializeCS(&pPtAttrs->hCSObj);

	if (DSP_SUCCEEDED(status)) {
		/* Set the Endianism Register */ /* Need to set this */
		/* Retrieve the TC u16 SWAP Option */
		status = REG_GetValue(NULL, CURRENTCONFIG, TCWORDSWAP,
				     (u8 *)&tcWordSwap, &tcWordSwapSize);
		/* Save the value */
		pDevContext->tcWordSwapOn = tcWordSwap;
	}
	if (DSP_SUCCEEDED(status)) {
		/* Set the Clock Divisor for the DSP module */
		DBG_Trace(DBG_LEVEL7, "WMD_DEV_create:Reset mail box and "
			  "enable the clock \n");
		status = CLK_Enable(SERVICESCLK_mailbox_ick);
		if (DSP_FAILED(status)) {
			DBG_Trace(DBG_LEVEL7,
				 "WMD_DEV_create:Reset mail box and "
				 "enable the clock Fail\n");
		}
		udelay(5);
		/* 24xx-Linux MMU address is obtained from the host
		 * resources struct */
		pDevContext->dwDSPMmuBase = resources.dwDmmuBase;
	}
	if (DSP_SUCCEEDED(status)) {
		pDevContext->hDevObject = hDevObject;
		pDevContext->ulIntMask = 0;
		/* Store current board state. */
		pDevContext->dwBrdState = BRD_STOPPED;
		/* Return this ptr to our device state to the WCD for storage:*/
		*ppDevContext = pDevContext;
		DBG_Trace(DBG_ENTER, "Device Created \n");
	} else {
		if (pPtAttrs != NULL) {
			if (pPtAttrs->hCSObj)
				SYNC_DeleteCS(pPtAttrs->hCSObj);

			if (pPtAttrs->pgInfo)
				MEM_Free(pPtAttrs->pgInfo);

			if (pPtAttrs->L2TblAllocVa) {
				MEM_FreePhysMem((void *)pPtAttrs->L2TblAllocVa,
						pPtAttrs->L2TblAllocPa,
						pPtAttrs->L2TblAllocSz);
			}
			if (pPtAttrs->L1TblAllocVa) {
				MEM_FreePhysMem((void *)pPtAttrs->L1TblAllocVa,
						pPtAttrs->L1TblAllocPa,
						pPtAttrs->L1TblAllocSz);
			}
		}
		if (pPtAttrs)
			MEM_Free(pPtAttrs);

		if (pDevContext)
			MEM_Free(pDevContext);

		DBG_Trace(DBG_LEVEL7,
			 "WMD_DEV_Create Error Device  not created\n");
	}
func_end:
	return status;
}

/*
 *  ======== WMD_DEV_Ctrl ========
 *      Receives device specific commands.
 */
static DSP_STATUS WMD_DEV_Ctrl(struct WMD_DEV_CONTEXT *pDevContext, u32 dwCmd,
				IN OUT void *pArgs)
{
	DSP_STATUS status = DSP_SOK;
	struct WMDIOCTL_EXTPROC *paExtProc = (struct WMDIOCTL_EXTPROC *)pArgs;
	s32 ndx;

	DBG_Trace(DBG_ENTER, "WMD_DEV_Ctrl, pDevContext:  0x%x\n\t\t dwCmd:  "
		  "0x%x\n\t\tpArgs:  0x%x\n", pDevContext, dwCmd, pArgs);
	switch (dwCmd) {
	case WMDIOCTL_CHNLREAD:
		break;
	case WMDIOCTL_CHNLWRITE:
		break;
	case WMDIOCTL_SETMMUCONFIG:
		/* store away dsp-mmu setup values for later use */
		for (ndx = 0; ndx < WMDIOCTL_NUMOFMMUTLB; ndx++, paExtProc++)
			pDevContext->aTLBEntry[ndx] = *paExtProc;

		break;
	case WMDIOCTL_DEEPSLEEP:
	case WMDIOCTL_EMERGENCYSLEEP:
		/* Currently only DSP Idle is supported Need to update for
		 * later releases */
		DBG_Trace(DBG_LEVEL5, "WMDIOCTL_DEEPSLEEP\n");
		status = SleepDSP(pDevContext, PWR_DEEPSLEEP, pArgs);
		break;
	case WMDIOCTL_WAKEUP:
		DBG_Trace(DBG_LEVEL5, "WMDIOCTL_WAKEUP\n");
		status = WakeDSP(pDevContext, pArgs);
		break;
	case WMDIOCTL_CLK_CTRL:
		DBG_Trace(DBG_LEVEL5, "WMDIOCTL_CLK_CTRL\n");
		status = DSP_SOK;
		/* Looking For Baseport Fix for Clocks */
		status = DSPPeripheralClkCtrl(pDevContext, pArgs);
		break;
	case WMDIOCTL_PWR_HIBERNATE:
		DBG_Trace(DBG_LEVEL5, "WMDIOCTL_PWR_HIBERNATE\n");
		status = handle_hibernation_fromDSP(pDevContext);
		break;
	case WMDIOCTL_PRESCALE_NOTIFY:
		DBG_Trace(DBG_LEVEL5, "WMDIOCTL_PRESCALE_NOTIFY\n");
		status = PreScale_DSP(pDevContext, pArgs);
		break;
	case WMDIOCTL_POSTSCALE_NOTIFY:
		DBG_Trace(DBG_LEVEL5, "WMDIOCTL_POSTSCALE_NOTIFY\n");
		status = PostScale_DSP(pDevContext, pArgs);
		break;
	case WMDIOCTL_CONSTRAINT_REQUEST:
		DBG_Trace(DBG_LEVEL5, "WMDIOCTL_CONSTRAINT_REQUEST\n");
		status = handle_constraints_set(pDevContext, pArgs);
		break;
	default:
		status = DSP_EFAIL;
		DBG_Trace(DBG_LEVEL7, "Error in WMD_BRD_Ioctl \n");
		break;
	}
	return status;
}

/*
 *  ======== WMD_DEV_Destroy ========
 *      Destroys the driver object.
 */
static DSP_STATUS WMD_DEV_Destroy(struct WMD_DEV_CONTEXT *hDevContext)
{
	struct PgTableAttrs *pPtAttrs;
	DSP_STATUS status = DSP_SOK;
	struct WMD_DEV_CONTEXT *pDevContext = (struct WMD_DEV_CONTEXT *)
						hDevContext;
	DBG_Trace(DBG_ENTER, "Entering WMD_DEV_Destroy:n hDevContext ::0x%x\n",
		  hDevContext);
	/* first put the device to stop state */
	WMD_BRD_Delete(pDevContext);
	if (pDevContext && pDevContext->pPtAttrs) {
		pPtAttrs = pDevContext->pPtAttrs;
		if (pPtAttrs->hCSObj)
			SYNC_DeleteCS(pPtAttrs->hCSObj);

		if (pPtAttrs->pgInfo)
			MEM_Free(pPtAttrs->pgInfo);

		if (pPtAttrs->L2TblAllocVa) {
			MEM_FreePhysMem((void *)pPtAttrs->L2TblAllocVa,
					pPtAttrs->L2TblAllocPa, pPtAttrs->
					L2TblAllocSz);
		}
		if (pPtAttrs->L1TblAllocVa) {
			MEM_FreePhysMem((void *)pPtAttrs->L1TblAllocVa,
					pPtAttrs->L1TblAllocPa, pPtAttrs->
					L1TblAllocSz);
		}
		if (pPtAttrs)
			MEM_Free(pPtAttrs);

	}
	/* Free the driver's device context: */
	MEM_Free((void *) hDevContext);
	return status;
}

static DSP_STATUS WMD_BRD_MemCopy(struct WMD_DEV_CONTEXT *hDevContext,
				  u32 ulDspDestAddr, u32 ulDspSrcAddr,
				  u32 ulNumBytes, u32 ulMemType)
{
	DSP_STATUS status = DSP_SOK;
	u32 srcAddr = ulDspSrcAddr;
	u32 destAddr = ulDspDestAddr;
	u32 copyBytes = 0;
	u32 totalBytes = ulNumBytes;
	u8 hostBuf[BUFFERSIZE];
	struct WMD_DEV_CONTEXT *pDevContext = hDevContext;
	while ((totalBytes > 0) && DSP_SUCCEEDED(status)) {
		copyBytes = totalBytes > BUFFERSIZE ? BUFFERSIZE : totalBytes;
		/* Read from External memory */
		status = ReadExtDspData(hDevContext, hostBuf, srcAddr,
					copyBytes, ulMemType);
		if (DSP_SUCCEEDED(status)) {
			if (destAddr < (pDevContext->dwDSPStartAdd +
			    pDevContext->dwInternalSize)) {
				/* Write to Internal memory */
				status = WriteDspData(hDevContext, hostBuf,
					 destAddr, copyBytes, ulMemType);
			} else {
				/* Write to External memory */
				status = WriteExtDspData(hDevContext, hostBuf,
					 destAddr, copyBytes, ulMemType, false);
			}
		}
		totalBytes -= copyBytes;
		srcAddr += copyBytes;
		destAddr += copyBytes;
	}
	return status;
}

/* Mem Write does not halt the DSP to write unlike WMD_BRD_Write */
static DSP_STATUS WMD_BRD_MemWrite(struct WMD_DEV_CONTEXT *hDevContext,
				   IN u8 *pbHostBuf, u32 dwDSPAddr,
				   u32 ulNumBytes, u32 ulMemType)
{
	DSP_STATUS status = DSP_SOK;
	struct WMD_DEV_CONTEXT *pDevContext = hDevContext;
	u32 ulRemainBytes = 0;
	u32 ulBytes = 0;
	ulRemainBytes = ulNumBytes;
	while (ulRemainBytes > 0 && DSP_SUCCEEDED(status)) {
		ulBytes =
			ulRemainBytes > BUFFERSIZE ? BUFFERSIZE : ulRemainBytes;
		if (dwDSPAddr < (pDevContext->dwDSPStartAdd +
		    pDevContext->dwInternalSize)) {
			status = WriteDspData(hDevContext, pbHostBuf, dwDSPAddr,
					      ulBytes, ulMemType);
		} else {
			status = WriteExtDspData(hDevContext, pbHostBuf,
				 dwDSPAddr, ulBytes, ulMemType, true);
		}
		ulRemainBytes -= ulBytes;
		dwDSPAddr += ulBytes;
		pbHostBuf = pbHostBuf + ulBytes;
	}
	return status;
}

/*
 *  ======== WMD_BRD_MemMap ========
 *      This function maps MPU buffer to the DSP address space. It performs
 *  linear to physical address translation if required. It translates each
 *  page since linear addresses can be physically non-contiguous
 *  All address & size arguments are assumed to be page aligned (in proc.c)
 *
 *  TODO: Disable MMU while updating the page tables (but that'll stall DSP)
 */
static DSP_STATUS WMD_BRD_MemMap(struct WMD_DEV_CONTEXT *hDevContext,
				 u32 ulMpuAddr, u32 ulVirtAddr,
				 u32 ulNumBytes, u32 ulMapAttr)
{
	u32 attrs;
	DSP_STATUS status = DSP_SOK;
	struct WMD_DEV_CONTEXT *pDevContext = hDevContext;
	struct HW_MMUMapAttrs_t hwAttrs;
	u32 numOfActualTabEntries = 0;
	u32 *pPhysAddrPageTbl = NULL;
	struct vm_area_struct *vma;
	struct mm_struct *mm = current->mm;
	u32 temp = 0;

	DBG_Trace(DBG_ENTER, "> WMD_BRD_MemMap hDevContext %x, pa %x, va %x, "
		 "size %x, ulMapAttr %x\n", hDevContext, ulMpuAddr, ulVirtAddr,
		 ulNumBytes, ulMapAttr);
	if (ulNumBytes == 0)
		return DSP_EINVALIDARG;

	if (ulMapAttr != 0) {
		attrs = ulMapAttr;
	} else {
		/* Assign default attributes */
		attrs = DSP_MAPVIRTUALADDR | DSP_MAPELEMSIZE16;
	}
	/* Take mapping properties */
	if (attrs & DSP_MAPBIGENDIAN)
		hwAttrs.endianism = HW_BIG_ENDIAN;
	else
		hwAttrs.endianism = HW_LITTLE_ENDIAN;

	hwAttrs.mixedSize = (enum HW_MMUMixedSize_t)
                               ((attrs & DSP_MAPMIXEDELEMSIZE) >> 2);
	/* Ignore elementSize if mixedSize is enabled */
	if (hwAttrs.mixedSize == 0) {
		if (attrs & DSP_MAPELEMSIZE8) {
			/* Size is 8 bit */
			hwAttrs.elementSize = HW_ELEM_SIZE_8BIT;
		} else if (attrs & DSP_MAPELEMSIZE16) {
			/* Size is 16 bit */
			hwAttrs.elementSize = HW_ELEM_SIZE_16BIT;
		} else if (attrs & DSP_MAPELEMSIZE32) {
			/* Size is 32 bit */
			hwAttrs.elementSize = HW_ELEM_SIZE_32BIT;
		} else if (attrs & DSP_MAPELEMSIZE64) {
			/* Size is 64 bit */
			hwAttrs.elementSize = HW_ELEM_SIZE_64BIT;
		} else {
			/* Mixedsize isn't enabled, so size can't be
			 * zero here */
			DBG_Trace(DBG_LEVEL7,
				 "WMD_BRD_MemMap: MMU element size is zero\n");
			return DSP_EINVALIDARG;
		}
	}
	if (attrs & DSP_MAPVMALLOCADDR) {
		status = MemMapVmalloc(hDevContext, ulMpuAddr, ulVirtAddr,
				       ulNumBytes, ulMapAttr);
		return status;
	}
	 /* Do OS-specific user-va to pa translation.
	 * Combine physically contiguous regions to reduce TLBs.
	 * Pass the translated pa to PteUpdate.  */
	if ((attrs & DSP_MAPPHYSICALADDR)) {
		status = PteUpdate(pDevContext, ulMpuAddr, ulVirtAddr,
			 ulNumBytes, &hwAttrs);
		goto func_cont;
	}

	/* Important Note: ulMpuAddr is mapped from user application process
	 * to current process - it must lie completely within the current
	 * virtual memory address space in order to be of use to us here!  */
	down_read(&mm->mmap_sem);
	vma = find_vma(mm, ulMpuAddr);
	up_read(&mm->mmap_sem);
	if (vma)
		DBG_Trace(DBG_LEVEL6, "VMAfor UserBuf: ulMpuAddr=%x, "
			"ulNumBytes=%x, vm_start=%x vm_end=%x vm_flags=%x \n",
			ulMpuAddr, ulNumBytes, vma->vm_start,
			vma->vm_end, vma->vm_flags);

	/* It is observed that under some circumstances, the user buffer is
	 * spread across several VMAs. So loop through and check if the entire
	 * user buffer is covered */
	while ((vma != NULL) && (ulMpuAddr + ulNumBytes > vma->vm_end)) {
		/* jump to the next VMA region */
		down_read(&mm->mmap_sem);
		vma = find_vma(mm, vma->vm_end + 1);
		up_read(&mm->mmap_sem);
		DBG_Trace(DBG_LEVEL6, "VMAfor UserBuf ulMpuAddr=%x, "
                       "ulNumBytes=%x, vm_start=%x vm_end=%x vm_flags=%x\n",
                       ulMpuAddr, ulNumBytes, vma->vm_start,
                       vma->vm_end, vma->vm_flags);
	}
	if (vma == NULL) {
		DBG_Trace(DBG_LEVEL7, "Failed to get the VMA region for "
			  "MPU Buffer !!! \n");
		status = DSP_EINVALIDARG;
	}
	if (DSP_FAILED(status))
		goto func_cont;
	pPhysAddrPageTbl = DMM_GetPhysicalAddrTable();
	/* Build the array with virtual to physical translations */
	status = TIOMAP_VirtToPhysical(mm, ulMpuAddr, ulNumBytes,
				&numOfActualTabEntries, pPhysAddrPageTbl);
	if (DSP_FAILED(status)) {
		DBG_Trace(DBG_LEVEL7,
			 "WMD_BRD_MemMap: TIOMAP_VirtToPhysical",
			 " failed\n");
		return DSP_EFAIL;
	}
	temp = 0;
	DBG_Trace(DBG_LEVEL4, "WMD_BRD_MemMap: numOfActualTabEntries=%d, "
		  "ulNumBytes= %d\n",  numOfActualTabEntries, ulNumBytes);
	/* Update the DSP MMU table with the physical addresses received from
       from translation function */
	while (temp < numOfActualTabEntries) {
		status = PteSet(pDevContext->pPtAttrs, pPhysAddrPageTbl[temp++],
				ulVirtAddr, HW_PAGE_SIZE_4KB, &hwAttrs);
		if (DSP_FAILED(status)) {
			DBG_Trace(DBG_LEVEL7,
				 "WMD_BRD_MemMap: FAILED IN PTESET \n");
			return DSP_EFAIL;
		}
		ulVirtAddr += HW_PAGE_SIZE_4KB;
	}
	if (DSP_FAILED(status))
		DBG_Trace(DBG_LEVEL5, "WMD_BRD_MemMap: PteSet failed \n");
	else
		DBG_Trace(DBG_LEVEL5, "WMD_BRD_MemMap: PteSet passed \n");

func_cont:
	/* Don't propogate Linux or HW status to upper layers */
	if (DSP_SUCCEEDED(status)) {
		status = DSP_SOK;
	} else {
		DBG_Trace(DBG_LEVEL7, "< WMD_BRD_MemMap status %x\n", status);
		status = DSP_EFAIL;
	}
	 /* In any case, flush the TLB
	 * This is called from here instead from PteUpdate to avoid unnecessary
	 * repetition while mapping non-contiguous physical regions of a virtual
	 * region */
	flush_all(pDevContext);
	DBG_Trace(DBG_ENTER, "< WMD_BRD_MemMap status %x\n", status);
	return status;
}

/*
 *  ======== WMD_BRD_MemUnMap ========
 *      Invalidate the PTEs for the DSP VA block to be unmapped.
 *
 *      PTEs of a mapped memory block are contiguous in any page table
 *      So, instead of looking up the PTE address for every 4K block,
 *      we clear consecutive PTEs until we unmap all the bytes
 */
static DSP_STATUS WMD_BRD_MemUnMap(struct WMD_DEV_CONTEXT *hDevContext,
				   u32 ulVirtAddr, u32 ulNumBytes)
{
	u32 L1BaseVa;
	u32 L2BaseVa;
	u32 L2BasePa;
	u32 L2PageNum;
	u32 pteVal;
	u32 pteSize;
	u32 pteCount;
	u32 pteAddrL1;
	u32 pteAddrL2 = 0;
	u32 remBytes;
	u32 remBytesL2;
	u32 vaCurr;
	DSP_STATUS status = DSP_SOK;
	struct WMD_DEV_CONTEXT *pDevContext = hDevContext;
	struct PgTableAttrs *pt = pDevContext->pPtAttrs;

	DBG_Trace(DBG_ENTER, "> WMD_BRD_MemUnMap hDevContext %x, va %x, "
		  "NumBytes %x\n", hDevContext, ulVirtAddr, ulNumBytes);
	vaCurr = ulVirtAddr;
	remBytes = ulNumBytes;
	remBytesL2 = 0;
	L1BaseVa = pt->L1BaseVa;
	pteAddrL1 = HW_MMU_PteAddrL1(L1BaseVa, vaCurr);
	DBG_Trace(DBG_ENTER, "WMD_BRD_MemUnMap L1BaseVa %x, pteAddrL1 %x "
		  "vaCurr %x remBytes %x\n", L1BaseVa, pteAddrL1,
		  vaCurr, remBytes);
	while (remBytes && (DSP_SUCCEEDED(status))) {
		u32 vaCurrOrig = vaCurr;
		/* Find whether the L1 PTE points to a valid L2 PT */
		pteAddrL1 = HW_MMU_PteAddrL1(L1BaseVa, vaCurr);
		pteVal = *(u32 *)pteAddrL1;
		pteSize = HW_MMU_PteSizeL1(pteVal);
		if (pteSize == HW_MMU_COARSE_PAGE_SIZE) {
			/* Get the L2 PA from the L1 PTE, and find
			 * corresponding L2 VA */
			L2BasePa = HW_MMU_PteCoarseL1(pteVal);
			L2BaseVa = L2BasePa - pt->L2BasePa + pt->L2BaseVa;
			L2PageNum = (L2BasePa - pt->L2BasePa) /
				    HW_MMU_COARSE_PAGE_SIZE;
			 /* Find the L2 PTE address from which we will start
			 * clearing, the number of PTEs to be cleared on this
			 * page, and the size of VA space that needs to be
			 * cleared on this L2 page */
			pteAddrL2 = HW_MMU_PteAddrL2(L2BaseVa, vaCurr);
			pteCount = pteAddrL2 & (HW_MMU_COARSE_PAGE_SIZE - 1);
			pteCount = (HW_MMU_COARSE_PAGE_SIZE - pteCount) /
				    sizeof(u32);
			if (remBytes < (pteCount * PG_SIZE_4K))
				pteCount = remBytes / PG_SIZE_4K;

			remBytesL2 = pteCount * PG_SIZE_4K;
			DBG_Trace(DBG_LEVEL1, "WMD_BRD_MemUnMap L2BasePa %x, "
				  "L2BaseVa %x pteAddrL2 %x, remBytesL2 %x\n",
				  L2BasePa, L2BaseVa, pteAddrL2, remBytesL2);
			 /* Unmap the VA space on this L2 PT. A quicker way
			 * would be to clear pteCount entries starting from
			 * pteAddrL2. However, below code checks that we don't
			 * clear invalid entries or less than 64KB for a 64KB
			 * entry. Similar checking is done for L1 PTEs too
			 * below */
			while (remBytesL2 && (DSP_SUCCEEDED(status))) {
				pteVal = *(u32 *)pteAddrL2;
				pteSize = HW_MMU_PteSizeL2(pteVal);
				/* vaCurr aligned to pteSize? */
				if ((pteSize != 0) && (remBytesL2 >= pteSize) &&
				   !(vaCurr & (pteSize - 1))) {
					if (HW_MMU_PteClear(pteAddrL2,
						vaCurr, pteSize) == RET_OK) {
						status = DSP_SOK;
						remBytesL2 -= pteSize;
						vaCurr += pteSize;
						pteAddrL2 += (pteSize >> 12) *
								sizeof(u32);
					} else {
						status = DSP_EFAIL;
						goto EXIT_LOOP;
					}
				} else {
					status = DSP_EFAIL;
				}
			}
			SYNC_EnterCS(pt->hCSObj);
			if (remBytesL2 == 0) {
				pt->pgInfo[L2PageNum].numEntries -= pteCount;
				if (pt->pgInfo[L2PageNum].numEntries == 0) {
					/* Clear the L1 PTE pointing to the
					 * L2 PT */
					if (RET_OK == HW_MMU_PteClear(L1BaseVa,
					vaCurrOrig, HW_MMU_COARSE_PAGE_SIZE))
						status = DSP_SOK;
					else {
						status = DSP_EFAIL;
						goto EXIT_LOOP;
					}
				}
				remBytes -= pteCount * PG_SIZE_4K;
			} else {
				status = DSP_EFAIL;
			}
			DBG_Trace(DBG_LEVEL1, "WMD_BRD_MemUnMap L2PageNum %x, "
				  "numEntries %x, pteCount %x, status: 0x%x\n",
				  L2PageNum, pt->pgInfo[L2PageNum].numEntries,
				  pteCount, status);
			SYNC_LeaveCS(pt->hCSObj);
		} else
			/* vaCurr aligned to pteSize? */
			/* pteSize = 1 MB or 16 MB */
			if ((pteSize != 0) && (remBytes >= pteSize) &&
			   !(vaCurr & (pteSize - 1))) {
				if (HW_MMU_PteClear(L1BaseVa,
					vaCurr, pteSize) == RET_OK) {
					status = DSP_SOK;
					remBytes -= pteSize;
					vaCurr += pteSize;
				} else {
					status = DSP_EFAIL;
					goto EXIT_LOOP;
				}
		} else {
			status = DSP_EFAIL;
		}
	}
	 /* It is better to flush the TLB here, so that any stale old entries
	 * get flushed */
EXIT_LOOP:
	flush_all(pDevContext);
	DBG_Trace(DBG_LEVEL1, "WMD_BRD_MemUnMap vaCurr %x, pteAddrL1 %x "
		  "pteAddrL2 %x\n", vaCurr, pteAddrL1, pteAddrL2);
	DBG_Trace(DBG_ENTER, "< WMD_BRD_MemUnMap status %x remBytes %x, "
		  "remBytesL2 %x\n", status, remBytes, remBytesL2);
	return status;
}
/*
 * ========= TIOMAP_VirtToPhysical ==========
 * Purpose:
 * 		This function builds the array with virtual to physical
 *	    address translation
 */
static DSP_STATUS TIOMAP_VirtToPhysical(struct mm_struct *mm, u32 ulMpuAddr,
					u32 ulNumBytes,
					u32 *numOfTableEntries,
					u32 *physicalAddrTable)
{
	u32 pAddr;
	u32 chunkSz;
	DSP_STATUS status = DSP_SOK;
	volatile u32 pteVal;
	u32 pteSize;
	pgd_t *pgd;
	pmd_t *pmd;
	volatile pte_t *ptep;
	u32 numEntries = 0;
	u32 numof4KPages = 0;
	u32 phyEntryCounter = 0;
	u32 temp = 0;
	u32 numUsrPgs;
	struct task_struct *curr_task = current;

	DBG_Trace(DBG_ENTER, "TIOMAP_VirtToPhysical: START:ulMpuAddr=%x, "
		  "ulNumBytes=%x\n", ulMpuAddr, ulNumBytes);
	if (physicalAddrTable == NULL)
		return DSP_EMEMORY;
	while (ulNumBytes) {
		DBG_Trace(DBG_LEVEL4, "TIOMAP_VirtToPhysical:Read the next PGD "
			  "and PMD entry\n");
		numEntries = 0;
		/* Get the first level page table entry information */
		/* Read the pointer to first level page table entry */
		pgd = pgd_offset(mm, ulMpuAddr);
		/* Read the value in the first level page table entry */
		pteVal = *(u32 *)pgd;
		/* Find the page size that is pointed by the first level page
		 * table entry */
		pteSize = HW_MMU_PteSizeL1(pteVal); /* update 16 or 1 */
		/* If pteSize is zero, then call the get_user_pages to create
		 * the page table entries for this buffer
		 */
		if (!pteSize) {
			down_read(&mm->mmap_sem);
			/* This call invokes handle_mmu _fault call, which
			 *causes all pages to be created before we scan the
			 * page tables
			 */
			numUsrPgs = get_user_pages(curr_task, mm, ulMpuAddr, 1,
							true, 0, NULL, NULL);
			up_read(&mm->mmap_sem);
			/* Get the first level page table entry information */
			/* Read the pointer to first level page table entry */
			pgd = pgd_offset(mm, ulMpuAddr);
			/* Read the value in the first level page table entry*/
			pteVal = *(u32 *)pgd;
			/* Find the page size that is pointed by the first level
			 * page table entry
			 */
			pteSize = HW_MMU_PteSizeL1(pteVal);
			DBG_Trace(DBG_LEVEL4, "First level  get_user_pages "
							"called\n");
		}
		/* If the page size is 4K or 64K, then we have to traverse to
		 * second level page table */
		if (pteSize == HW_MMU_COARSE_PAGE_SIZE) {
			DBG_Trace(DBG_LEVEL5, "Read the next PMD entry\n");
			/* Get the second level page table information */
			pmd = pmd_offset(pgd, ulMpuAddr);
			ptep = pte_offset_map(pmd, ulMpuAddr);
			do {
				ptep = ptep+numEntries;
				/* Read the value of second level page table
				 * entry */
				pteVal = *(u32 *)ptep;
				/* Find the size of page the second level
				 * table entry is pointing */
				/* update 64 or 4 */
				pteSize = HW_MMU_PteSizeL2(pteVal);
				/* If pteSize is invalid, then call
				 * get_user_pages to create the
				 * page table entries
				 */
				if (!pteSize) {
					numUsrPgs =
						(ulNumBytes/HW_PAGE_SIZE_4KB);
					down_read(&mm->mmap_sem);
					/* This call invokes
					 *handle_mmu _fault call, which causes
					 *all pages to be created before we scan
					 * the page tables */
					if (numUsrPgs <= PAGES_II_LVL_TABLE) {
						get_user_pages(curr_task, mm,
						ulMpuAddr, numUsrPgs, true,  0,
						NULL, NULL);
						DBG_Trace(DBG_LEVEL4,
						"get_user_pages, numUsrPgs"
						"= %d\n", numUsrPgs);
					} else {
						get_user_pages(curr_task, mm,
						ulMpuAddr, PAGES_II_LVL_TABLE,
						true, 0, NULL, NULL);
						DBG_Trace(DBG_LEVEL4,
						"get_user_pages, numUsrPgs"
						"= %d\n", PAGES_II_LVL_TABLE);
					}
					up_read(&mm->mmap_sem);
					/* Read the value of second level page
					 * table  entry */
					pteVal = *(u32 *)ptep;
					/* Find the size of page the second
					 * level table entry is pointing */
					pteSize = HW_MMU_PteSizeL2(pteVal);
				}
				DBG_Trace(DBG_LEVEL4, "TIOMAP_VirtToPhysical:"
					"*pmd=%x, *pgd=%x, ptep = %x, pteVal="
					" %x, pteSize=%x\n", *pmd,
					*(u32 *)pgd, (u32)ptep, pteVal,
					pteSize);

				/* Extract the physical Addresses */
				switch (pteSize) {
				case HW_PAGE_SIZE_64KB:
					pAddr = pteVal & MMU_LARGE_PAGE_MASK;
					chunkSz = HW_PAGE_SIZE_64KB;
					numEntries = 16;
					numof4KPages = 16;
					break;
				case HW_PAGE_SIZE_4KB:
					pAddr = pteVal & MMU_SMALL_PAGE_MASK;
					chunkSz = HW_PAGE_SIZE_4KB;
					numEntries = 1;
					numof4KPages = 1;
					break;
				default:
					DBG_Trace(DBG_LEVEL7,
						"TIOMAP_VirtToPhysical:"
						"Descriptor"
						"Format Fault-II level,"
						" PTE size = %x\n",
						pteSize);
					return DSP_EFAIL;
				}
				temp = 0;
				while (temp++ < numof4KPages) {
					physicalAddrTable[phyEntryCounter++] =
									pAddr;
					DBG_Trace(DBG_LEVEL4,
						 "TIOMAP_VirtToPhysical:"
						 "physicalAddrTable[%d]= %x\n",
						 (phyEntryCounter-1), pAddr);
					pAddr += HW_PAGE_SIZE_4KB;
				}
				if (DSP_SUCCEEDED(status)) {
					/* Go to the next page */
					ulMpuAddr += chunkSz;
					/* Update the number of bytes that
					 * are copied */
					ulNumBytes -= chunkSz;
					DBG_Trace(DBG_LEVEL4,
						"TIOMAP_VirtToPhysical: mpuCurr"
						" = %x, pagesize = %x, "
						"numBytesRem=%x\n",
						ulMpuAddr, chunkSz, ulNumBytes);
				} else {
					DBG_Trace(DBG_LEVEL7,
					     " TIOMAP_VirtToPhysical:PTEupdate"
					     "failed\n");
				}
			/* It is observed that the pgd value (first level page
			 * table entry) is changed after reading the 512
			 * entries in second level table. So, call the pgd
			 * functions after reaching 512 entries in second
			 * level table */
			} while ((ulMpuAddr & 0x001ff000) && (ulNumBytes));
		} else {
			/* Extract the Address to update the IVA MMU table
			 * with */
			switch (pteSize) {
			case HW_PAGE_SIZE_16MB:
				pAddr = pteVal & MMU_SSECTION_ADDR_MASK;
					chunkSz = HW_PAGE_SIZE_16MB;
					numEntries = 16;
					numof4KPages = 4096;
					break;
			case HW_PAGE_SIZE_1MB:
				pAddr = pteVal & MMU_SECTION_ADDR_MASK;
					chunkSz = HW_PAGE_SIZE_1MB;
					numEntries = 1;
					numof4KPages = 256;
					break;
			default:
				DBG_Trace(DBG_LEVEL7,
				     "TIOMAP_VirtToPhysical:Descriptor"
				     "Format Faul-I level, PTE size = "
				     "%x\n", pteSize);
				return DSP_EFAIL;
			}
			temp = 0;
			while (temp++ < numof4KPages) {
					physicalAddrTable[phyEntryCounter++] =
									 pAddr;
					DBG_Trace(DBG_LEVEL4,
						 "TIOMAP_VirtToPhysical:"
						 "physicalAddrTable[%d]= %x\n",
						 (phyEntryCounter-1), pAddr);
					pAddr += HW_PAGE_SIZE_4KB;
			}
			if (DSP_SUCCEEDED(status)) {
				/* Go to the next page */
				ulMpuAddr += chunkSz;
				/* Update the number of bytes that are copied */
				ulNumBytes -= chunkSz;
				DBG_Trace(DBG_LEVEL4,
					 "TIOMAP_VirtToPhysical: mpuCurr = %x, "
					 "pagesize = %x, numBytesRem=%x\n",
					 ulMpuAddr, chunkSz, ulNumBytes);
			} else {
				DBG_Trace(DBG_LEVEL7,
					 " TIOMAP_VirtToPhysical:PTEupdate"
					 "failed\n");
			}

		}
	}
	*numOfTableEntries = phyEntryCounter;
	DBG_Trace(DBG_LEVEL4, " TIOMAP_VirtToPhysical:numofTableEntries=%d\n",
		  phyEntryCounter);
	return status;
}

/*
 *  ======== PteUpdate ========
 *      This function calculates the optimum page-aligned addresses and sizes
 *      Caller must pass page-aligned values
 */
static DSP_STATUS PteUpdate(struct WMD_DEV_CONTEXT *hDevContext, u32 pa,
			    u32 va, u32 size,
			    struct HW_MMUMapAttrs_t *mapAttrs)
{
	u32 i;
	u32 allBits;
	u32 paCurr = pa;
	u32 vaCurr = va;
	u32 numBytes = size;
	struct WMD_DEV_CONTEXT *pDevContext = hDevContext;
	DSP_STATUS status = DSP_SOK;
	u32 pgSize[] = { HW_PAGE_SIZE_16MB, HW_PAGE_SIZE_1MB,
			   HW_PAGE_SIZE_64KB, HW_PAGE_SIZE_4KB };
	DBG_Trace(DBG_ENTER, "> PteUpdate hDevContext %x, pa %x, va %x, "
		 "size %x, mapAttrs %x\n", hDevContext, pa, va, size, mapAttrs);
	while (numBytes && DSP_SUCCEEDED(status)) {
		/* To find the max. page size with which both PA & VA are
		 * aligned */
		allBits = paCurr | vaCurr;
		DBG_Trace(DBG_LEVEL1, "allBits %x, paCurr %x, vaCurr %x, "
			 "numBytes %x ", allBits, paCurr, vaCurr, numBytes);
		for (i = 0; i < 4; i++) {
			if ((numBytes >= pgSize[i]) && ((allBits &
			   (pgSize[i] - 1)) == 0)) {
				DBG_Trace(DBG_LEVEL1, "pgSize %x\n", pgSize[i]);
				status = PteSet(pDevContext->pPtAttrs, paCurr,
						vaCurr, pgSize[i], mapAttrs);
				paCurr += pgSize[i];
				vaCurr += pgSize[i];
				numBytes -= pgSize[i];
				 /* Don't try smaller sizes. Hopefully we have
				 * reached an address aligned to a bigger page
				 * size */
				break;
			}
		}
	}
	DBG_Trace(DBG_ENTER, "< PteUpdate status %x numBytes %x\n", status,
		  numBytes);
	return status;
}

/*
 *  ======== PteSet ========
 *      This function calculates PTE address (MPU virtual) to be updated
 *      It also manages the L2 page tables
 */
static DSP_STATUS PteSet(struct PgTableAttrs *pt, u32 pa, u32 va,
			 u32 size, struct HW_MMUMapAttrs_t *attrs)
{
	u32 i;
	u32 pteVal;
	u32 pteAddrL1;
	u32 pteSize;
	u32 pgTblVa;      /* Base address of the PT that will be updated */
	u32 L1BaseVa;
	 /* Compiler warns that the next three variables might be used
	 * uninitialized in this function. Doesn't seem so. Working around,
	 * anyways.  */
	u32 L2BaseVa = 0;
	u32 L2BasePa = 0;
	u32 L2PageNum = 0;
	DSP_STATUS status = DSP_SOK;
	DBG_Trace(DBG_ENTER, "> PteSet pPgTableAttrs %x, pa %x, va %x, "
		 "size %x, attrs %x\n", pt, pa, va, size, attrs);
	L1BaseVa = pt->L1BaseVa;
	pgTblVa = L1BaseVa;
	if ((size == HW_PAGE_SIZE_64KB) || (size == HW_PAGE_SIZE_4KB)) {
		/* Find whether the L1 PTE points to a valid L2 PT */
		pteAddrL1 = HW_MMU_PteAddrL1(L1BaseVa, va);
		if (pteAddrL1 <= (pt->L1BaseVa + pt->L1size)) {
			pteVal = *(u32 *)pteAddrL1;
			pteSize = HW_MMU_PteSizeL1(pteVal);
		} else {
			return DSP_EFAIL;
		}
		SYNC_EnterCS(pt->hCSObj);
		if (pteSize == HW_MMU_COARSE_PAGE_SIZE) {
			/* Get the L2 PA from the L1 PTE, and find
			 * corresponding L2 VA */
			L2BasePa = HW_MMU_PteCoarseL1(pteVal);
			L2BaseVa = L2BasePa - pt->L2BasePa + pt->L2BaseVa;
			L2PageNum = (L2BasePa - pt->L2BasePa) /
				    HW_MMU_COARSE_PAGE_SIZE;
		} else if (pteSize == 0) {
			/* L1 PTE is invalid. Allocate a L2 PT and
			 * point the L1 PTE to it */
			/* Find a free L2 PT. */
			for (i = 0; (i < pt->L2NumPages) &&
			    (pt->pgInfo[i].numEntries != 0); i++)
				;;
			if (i < pt->L2NumPages) {
				L2PageNum = i;
				L2BasePa = pt->L2BasePa + (L2PageNum *
					   HW_MMU_COARSE_PAGE_SIZE);
				L2BaseVa = pt->L2BaseVa + (L2PageNum *
					   HW_MMU_COARSE_PAGE_SIZE);
				/* Endianness attributes are ignored for
				 * HW_MMU_COARSE_PAGE_SIZE */
				status = HW_MMU_PteSet(L1BaseVa, L2BasePa, va,
					 HW_MMU_COARSE_PAGE_SIZE, attrs);
			} else {
				status = DSP_EMEMORY;
			}
		} else {
			/* Found valid L1 PTE of another size.
			 * Should not overwrite it. */
			status = DSP_EFAIL;
		}
		if (DSP_SUCCEEDED(status)) {
			pgTblVa = L2BaseVa;
			if (size == HW_PAGE_SIZE_64KB)
				pt->pgInfo[L2PageNum].numEntries += 16;
			else
				pt->pgInfo[L2PageNum].numEntries++;

			DBG_Trace(DBG_LEVEL1, "L2 BaseVa %x, BasePa %x, "
				 "PageNum %x numEntries %x\n", L2BaseVa,
				 L2BasePa, L2PageNum,
				 pt->pgInfo[L2PageNum].numEntries);
		}
		SYNC_LeaveCS(pt->hCSObj);
	}
	if (DSP_SUCCEEDED(status)) {
		DBG_Trace(DBG_LEVEL1, "PTE pgTblVa %x, pa %x, va %x, size %x\n",
			 pgTblVa, pa, va, size);
		DBG_Trace(DBG_LEVEL1, "PTE endianism %x, elementSize %x, "
			  "mixedSize %x\n", attrs->endianism,
			  attrs->elementSize, attrs->mixedSize);
		status = HW_MMU_PteSet(pgTblVa, pa, va, size, attrs);
	}
	DBG_Trace(DBG_ENTER, "< PteSet status %x\n", status);
	return status;
}

/* Memory map kernel VA -- memory allocated with vmalloc */
static DSP_STATUS MemMapVmalloc(struct WMD_DEV_CONTEXT *hDevContext,
				u32 ulMpuAddr, u32 ulVirtAddr,
				u32 ulNumBytes, u32 ulMapAttr)
{
	u32 attrs = ulMapAttr;
	DSP_STATUS status = DSP_SOK;
	struct WMD_DEV_CONTEXT *pDevContext = hDevContext;
	struct HW_MMUMapAttrs_t hwAttrs;
	struct page *pPage[1];
	u32 i;
	u32 paCurr;
	u32 paNext;
	u32 vaCurr;
	u32 sizeCurr;
	u32 numPages;

	DBG_Trace(DBG_ENTER, "> MemMapVmalloc hDevContext %x, pa %x, va %x, "
		  "size %x, ulMapAttr %x\n", hDevContext, ulMpuAddr,
		  ulVirtAddr, ulNumBytes, ulMapAttr);
	/* Take mapping properties */
	if (attrs & DSP_MAPBIGENDIAN)
		hwAttrs.endianism = HW_BIG_ENDIAN;
	else
		hwAttrs.endianism = HW_LITTLE_ENDIAN;

	hwAttrs.mixedSize = (enum HW_MMUMixedSize_t)
			     ((attrs & DSP_MAPMIXEDELEMSIZE) >> 2);
	/* Ignore elementSize if mixedSize is enabled */
	if (hwAttrs.mixedSize == 0) {
		if (attrs & DSP_MAPELEMSIZE8) {
			/* Size is 8 bit */
			hwAttrs.elementSize = HW_ELEM_SIZE_8BIT;
		} else if (attrs & DSP_MAPELEMSIZE16) {
			/* Size is 16 bit */
			hwAttrs.elementSize = HW_ELEM_SIZE_16BIT;
		} else if (attrs & DSP_MAPELEMSIZE32) {
			/* Size is 32 bit */
			hwAttrs.elementSize = HW_ELEM_SIZE_32BIT;
		} else if (attrs & DSP_MAPELEMSIZE64) {
			/* Size is 64 bit */
			hwAttrs.elementSize = HW_ELEM_SIZE_64BIT;
		} else {
			/* Mixedsize isn't enabled, so size can't be zero
			 * here */
			DBG_Trace(DBG_LEVEL7, "WMD_BRD_MemMap: MMU element "
				 "size is zero\n");
			return DSP_EINVALIDARG;
		}
	}
	 /* Do Kernel va to pa translation.
	 * Combine physically contiguous regions to reduce TLBs.
	 * Pass the translated pa to PteUpdate.  */
	numPages = ulNumBytes / PAGE_SIZE; /* PAGE_SIZE = OS page size */
	if (DSP_FAILED(status))
		goto func_cont;

	i = 0;
	vaCurr = ulMpuAddr;
	pPage[0] = vmalloc_to_page((void *)vaCurr);
	paNext = page_to_phys(pPage[0]);
	while (DSP_SUCCEEDED(status) && (i < numPages)) {
		/* Reuse paNext from the previous iteraion to avoid
		 * an extra va2pa call */
		paCurr = paNext;
		sizeCurr = PAGE_SIZE;
		/* If the next page is physically contiguous,
		 * map it with the current one by increasing
		 * the size of the region to be mapped */
		while (++i < numPages) {
			pPage[0] = vmalloc_to_page((void *)(vaCurr + sizeCurr));
			paNext = page_to_phys(pPage[0]);
			DBG_Trace(DBG_LEVEL5, "Xlate Vmalloc VA=0x%x , "
				 "PA=0x%x \n", (vaCurr + sizeCurr), paNext);
			if (paNext == (paCurr + sizeCurr))
				sizeCurr += PAGE_SIZE;
			else
				break;

		}
		if (paNext == 0) {
			status = DSP_EMEMORY;
			break;
		}
		status = PteUpdate(pDevContext, paCurr, ulVirtAddr +
				  (vaCurr - ulMpuAddr), sizeCurr, &hwAttrs);
		vaCurr += sizeCurr;
	}
func_cont:
	/* Don't propogate Linux or HW status to upper layers */
	if (DSP_SUCCEEDED(status)) {
		status = DSP_SOK;
		DBG_Trace(DBG_LEVEL7, "< WMD_BRD_MemMap succeeded %x\n",
			 status);
	} else {
		DBG_Trace(DBG_LEVEL7, "< WMD_BRD_MemMap status %x\n", status);
		status = DSP_EFAIL;
	}
	 /* In any case, flush the TLB
	 * This is called from here instead from PteUpdate to avoid unnecessary
	 * repetition while mapping non-contiguous physical regions of a virtual
	 * region */
	flush_all(pDevContext);
	DBG_Trace(DBG_LEVEL7, "< WMD_BRD_MemMap  at end status %x\n", status);
	return status;
}

static DSP_STATUS run_IdleBoot(u32 prm_base, u32 cm_base,
			       u32 sysctrl_base)
{
	u32 temp;
	DSP_STATUS status = DSP_SOK;
	DSP_STATUS clk_status = DSP_SOK;
	enum HW_PwrState_t    pwrState;

	/* Read PM_PWSTST_IVA2 */
	HW_PWRST_IVA2RegGet(prm_base, &temp);
	 if ((temp & 0x03) != 0x03 || (temp & 0x03) != 0x02) {
		/* IVA2 is not in ON state */
		/* Set PM_PWSTCTRL_IVA2  to ON */
		HW_PWR_IVA2PowerStateSet(prm_base, HW_PWR_DOMAIN_DSP,
					  HW_PWR_STATE_ON);
		/* Set the SW supervised state transition */
		HW_PWR_CLKCTRL_IVA2RegSet(cm_base, HW_SW_SUP_WAKEUP);
		/* Wait until the state has moved to ON */
		HW_PWR_IVA2StateGet(prm_base, HW_PWR_DOMAIN_DSP, &pwrState);
	}
	clk_status = CLK_Disable(SERVICESCLK_iva2_ck);
	if (DSP_FAILED(clk_status)) {
		DBG_Trace(DBG_LEVEL6, "CLK_Disbale failed for clk = 0x%x \n",
			  SERVICESCLK_iva2_ck);
	}
	udelay(10);
	/* Assert IVA2-RST1 and IVA2-RST2  */
	*((REG_UWORD32 *)((u32)(prm_base) + 0x50)) = (u32)0x07;
	udelay(30);
	/* set the SYSC for Idle Boot */
	*((REG_UWORD32 *)((u32)(sysctrl_base) + 0x404)) = (u32)0x01;
               temp = (u32) *((REG_UWORD32 *)
                               ((u32) (cm_base) + 0x34));
               temp = (temp & 0xFFFFFFFE) | 0x1;
               *((REG_UWORD32 *) ((u32) (cm_base) + 0x34)) =
                       (u32) temp;
               temp = (u32) *((REG_UWORD32 *)
                       ((u32) (cm_base) + 0x4));
               temp =  (temp & 0xFFFFFC8) | 0x37;
               *((REG_UWORD32 *) ((u32) (cm_base) + 0x4)) =
                       (u32) temp;
	clk_status = CLK_Enable(SERVICESCLK_iva2_ck);
	if (DSP_FAILED(clk_status)) {
		DBG_Trace(DBG_LEVEL6, "CLK_Enable failed for clk = 0x%x \n",
			  SERVICESCLK_iva2_ck);
	}
	udelay(20);
	GetHWRegs(prm_base, cm_base);
	/* Release Reset1 and Reset2 */
	*((REG_UWORD32 *)((u32)(prm_base) + 0x50)) = (u32)0x05;
	udelay(20);
	*((REG_UWORD32 *)((u32)(prm_base) + 0x50)) = (u32)0x04;
	udelay(30);
	return status;
}


void GetHWRegs(u32 prm_base, u32 cm_base)
{
	u32 temp;
       temp = (u32)*((REG_UWORD32 *)((u32)(cm_base) + 0x00));
	   DBG_Trace(DBG_LEVEL6, "CM_FCLKEN_IVA2 = 0x%x \n", temp);
       temp = (u32)*((REG_UWORD32 *)((u32)(cm_base) + 0x10));
	   DBG_Trace(DBG_LEVEL6, "CM_ICLKEN1_IVA2 = 0x%x \n", temp);
       temp = (u32)*((REG_UWORD32 *)((u32)(cm_base) + 0x20));
	   DBG_Trace(DBG_LEVEL6, "CM_IDLEST_IVA2 = 0x%x \n", temp);
       temp = (u32)*((REG_UWORD32 *)((u32)(cm_base) + 0x48));
	   DBG_Trace(DBG_LEVEL6, "CM_CLKSTCTRL_IVA2 = 0x%x \n", temp);
       temp = (u32)*((REG_UWORD32 *)((u32)(cm_base) + 0x4c));
	   DBG_Trace(DBG_LEVEL6, "CM_CLKSTST_IVA2 = 0x%x \n", temp);
       temp = (u32)*((REG_UWORD32 *)((u32)(prm_base) + 0x50));
	   DBG_Trace(DBG_LEVEL6, "RM_RSTCTRL_IVA2 = 0x%x \n", temp);
       temp = (u32)*((REG_UWORD32 *)((u32)(prm_base) + 0x58));
	   DBG_Trace(DBG_LEVEL6, "RM_RSTST_IVA2 = 0x%x \n", temp);
       temp = (u32)*((REG_UWORD32 *)((u32)(prm_base) + 0xE0));
	   DBG_Trace(DBG_LEVEL6, "PM_PWSTCTRL_IVA2 = 0x%x \n", temp);
       temp = (u32)*((REG_UWORD32 *)((u32)(prm_base) + 0xE4));
	   DBG_Trace(DBG_LEVEL6, "PM_PWSTST_IVA2 = 0x%x \n", temp);
       temp = (u32)*((REG_UWORD32 *)((u32)(cm_base) + 0xA10));
	   DBG_Trace(DBG_LEVEL6, "CM_ICLKEN1_CORE = 0x%x \n", temp);
}

/*
 *  ======== configureDspMmu ========
 *      Make DSP MMU page table entries.
 */
void configureDspMmu(struct WMD_DEV_CONTEXT *pDevContext, u32 dataBasePhys,
		    u32 dspBaseVirt, u32 sizeInBytes, s32 nEntryStart,
		    enum HW_Endianism_t endianism,
		    enum HW_ElementSize_t elemSize,
		    enum HW_MMUMixedSize_t mixedSize)
{
	struct CFG_HOSTRES resources;
	struct HW_MMUMapAttrs_t mapAttrs = { endianism, elemSize, mixedSize };
	DSP_STATUS status = DSP_SOK;

	DBC_Require(sizeInBytes > 0);
	DBG_Trace(DBG_LEVEL1,
		 "configureDspMmu entry %x pa %x, va %x, bytes %x ",
		 nEntryStart, dataBasePhys, dspBaseVirt, sizeInBytes);

	DBG_Trace(DBG_LEVEL1, "endianism %x, elemSize %x, mixedSize %x\n",
		 endianism, elemSize, mixedSize);
	status = CFG_GetHostResources(
		 (struct CFG_DEVNODE *)DRV_GetFirstDevExtension(), &resources);
	status = HW_MMU_TLBAdd(pDevContext->dwDSPMmuBase, dataBasePhys,
				dspBaseVirt, sizeInBytes, nEntryStart,
				&mapAttrs, HW_SET, HW_SET);
}

/*
 *  ======== WaitForStart ========
 *      Wait for the singal from DSP that it has started, or time out.
 */
bool WaitForStart(struct WMD_DEV_CONTEXT *pDevContext, u32 dwSyncAddr)
{
	u16 usCount = TIHELEN_ACKTIMEOUT;

	/*  Wait for response from board */
	while (*((volatile u16 *)dwSyncAddr) && --usCount)
		udelay(10);

	/*  If timed out: return FALSE */
	if (!usCount) {
		DBG_Trace(DBG_LEVEL7, "Timed out Waiting for DSP to Start\n");
		return FALSE;
	}
	return TRUE;
}
