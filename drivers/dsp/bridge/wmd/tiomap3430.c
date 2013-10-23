/*
 * tiomap.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Processor Manager Driver for TI OMAP3430 EVM.
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

/*  ----------------------------------- Host OS */
#include <dspbridge/host_os.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <mach-omap2/prm.h>
#include <mach-omap2/cm.h>
#include <mach-omap2/prm-regbits-34xx.h>
#include <mach-omap2/cm-regbits-34xx.h>
#include <plat/control.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>
#include <dspbridge/dbdefs.h>
#include <dspbridge/errbase.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/mem.h>
#include <dspbridge/reg.h>
#include <dspbridge/cfg.h>
#include <dspbridge/drv.h>
#include <dspbridge/sync.h>

/* ------------------------------------ Hardware Abstraction Layer */
#include <hw_defs.h>
#include <hw_dspssC64P.h>
#include <hw_prcm.h>
#include <hw_mmu.h>

/*  ----------------------------------- Link Driver */
#include <dspbridge/wmd.h>
#include <dspbridge/wmdchnl.h>
#include <dspbridge/wmddeh.h>
#include <dspbridge/wmdio.h>
#include <dspbridge/wmdmsg.h>
#include <dspbridge/pwr.h>
#include <dspbridge/io_sm.h>

/*  ----------------------------------- Platform Manager */
#include <dspbridge/dev.h>
#include <dspbridge/wcd.h>
#include <dspbridge/dmm.h>
#include <dspbridge/wdt.h>

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
#define PHYS_TO_PAGE(phys)      pfn_to_page((phys) >> PAGE_SHIFT)

#define MMU_GFLUSH 0x60

/* Forward Declarations: */
static dsp_status bridge_brd_monitor(struct wmd_dev_context *dev_context);
static dsp_status bridge_brd_read(struct wmd_dev_context *dev_context,
				  OUT u8 *pbHostBuf,
				  u32 dwDSPAddr, u32 ul_num_bytes,
				  u32 ulMemType);
static dsp_status bridge_brd_start(struct wmd_dev_context *dev_context,
				   u32 dwDSPAddr);
static dsp_status bridge_brd_status(struct wmd_dev_context *dev_context,
				    int *pdwState);
static dsp_status bridge_brd_stop(struct wmd_dev_context *dev_context);
static dsp_status bridge_brd_write(struct wmd_dev_context *dev_context,
				   IN u8 *pbHostBuf,
				   u32 dwDSPAddr, u32 ul_num_bytes,
				   u32 ulMemType);
static dsp_status bridge_brd_set_state(struct wmd_dev_context *hDevContext,
				    u32 ulBrdState);
static dsp_status bridge_brd_mem_copy(struct wmd_dev_context *hDevContext,
				   u32 ulDspDestAddr, u32 ulDspSrcAddr,
				   u32 ul_num_bytes, u32 ulMemType);
static dsp_status bridge_brd_mem_write(struct wmd_dev_context *dev_context,
				    IN u8 *pbHostBuf, u32 dwDSPAddr,
				    u32 ul_num_bytes, u32 ulMemType);
static dsp_status bridge_brd_mem_map(struct wmd_dev_context *hDevContext,
				  u32 ul_mpu_addr, u32 ulVirtAddr,
				  u32 ul_num_bytes, u32 ul_map_attr);
static dsp_status bridge_brd_mem_un_map(struct wmd_dev_context *hDevContext,
				     u32 ulVirtAddr, u32 ul_num_bytes);
static dsp_status bridge_dev_create(OUT struct wmd_dev_context **ppDevContext,
				    struct dev_object *hdev_obj,
				    IN CONST struct cfg_hostres *pConfig,
				    IN CONST struct cfg_dspres *pDspConfig);
static dsp_status bridge_dev_ctrl(struct wmd_dev_context *dev_context,
				  u32 dw_cmd, IN OUT void *pargs);
static dsp_status bridge_dev_destroy(struct wmd_dev_context *dev_context);
static u32 user_va2_pa(struct mm_struct *mm, u32 address);
static dsp_status pte_update(struct wmd_dev_context *hDevContext, u32 pa,
			     u32 va, u32 size,
			     struct hw_mmu_map_attrs_t *map_attrs);
static dsp_status pte_set(struct pg_table_attrs *pt, u32 pa, u32 va,
			  u32 size, struct hw_mmu_map_attrs_t *attrs);
static dsp_status mem_map_vmalloc(struct wmd_dev_context *hDevContext,
				  u32 ul_mpu_addr, u32 ulVirtAddr,
				  u32 ul_num_bytes,
				  struct hw_mmu_map_attrs_t *hw_attrs);

#ifdef CONFIG_BRIDGE_DEBUG
static void get_hw_regs(void __iomem *prm_base, void __iomem *cm_base)
{
	u32 temp;
	temp = __raw_readl((cm_base) + 0x00);
	dev_dbg(bridge, "CM_FCLKEN_IVA2 = 0x%x\n", temp);
	temp = __raw_readl((cm_base) + 0x10);
	dev_dbg(bridge, "CM_ICLKEN1_IVA2 = 0x%x \n", temp);
	temp = __raw_readl((cm_base) + 0x20);
	dev_dbg(bridge, "CM_IDLEST_IVA2 = 0x%x \n", temp);
	temp = __raw_readl((cm_base) + 0x48);
	dev_dbg(bridge, "CM_CLKSTCTRL_IVA2 = 0x%x \n", temp);
	temp = __raw_readl((cm_base) + 0x4c);
	dev_dbg(bridge, "CM_CLKSTST_IVA2 = 0x%x \n", temp);
	temp = __raw_readl((prm_base) + 0x50);
	dev_dbg(bridge, "RM_RSTCTRL_IVA2 = 0x%x \n", temp);
	temp = __raw_readl((prm_base) + 0x58);
	dev_dbg(bridge, "RM_RSTST_IVA2 = 0x%x \n", temp);
	temp = __raw_readl((prm_base) + 0xE0);
	dev_dbg(bridge, "PM_PWSTCTRL_IVA2 = 0x%x \n", temp);
	temp = __raw_readl((prm_base) + 0xE4);
	dev_dbg(bridge, "PM_PWSTST_IVA2 = 0x%x \n", temp);
	temp = __raw_readl((cm_base) + 0xA10);
	dev_dbg(bridge, "CM_ICLKEN1_CORE = 0x%x \n", temp);
}
#else
static inline void get_hw_regs(void __iomem *prm_base, void __iomem *cm_base)
{
}
#endif

/*  ----------------------------------- Globals */

/* Attributes of L2 page tables for DSP MMU */
struct page_info {
	u32 num_entries;	/* Number of valid PTEs in the L2 PT */
};

/* Attributes used to manage the DSP MMU page tables */
struct pg_table_attrs {
	struct sync_csobject *hcs_obj;	/* Critical section object handle */

	u32 l1_base_pa;		/* Physical address of the L1 PT */
	u32 l1_base_va;		/* Virtual  address of the L1 PT */
	u32 l1_size;		/* Size of the L1 PT */
	u32 l1_tbl_alloc_pa;
	/* Physical address of Allocated mem for L1 table. May not be aligned */
	u32 l1_tbl_alloc_va;
	/* Virtual address of Allocated mem for L1 table. May not be aligned */
	u32 l1_tbl_alloc_sz;
	/* Size of consistent memory allocated for L1 table.
	 * May not be aligned */

	u32 l2_base_pa;		/* Physical address of the L2 PT */
	u32 l2_base_va;		/* Virtual  address of the L2 PT */
	u32 l2_size;		/* Size of the L2 PT */
	u32 l2_tbl_alloc_pa;
	/* Physical address of Allocated mem for L2 table. May not be aligned */
	u32 l2_tbl_alloc_va;
	/* Virtual address of Allocated mem for L2 table. May not be aligned */
	u32 l2_tbl_alloc_sz;
	/* Size of consistent memory allocated for L2 table.
	 * May not be aligned */

	u32 l2_num_pages;	/* Number of allocated L2 PT */
	/* Array [l2_num_pages] of L2 PT info structs */
	struct page_info *pg_info;
};

/*
 *  If dsp_debug is true, do not branch to the DSP entry point and wait for DSP
 *  to boot
 */
extern s32 dsp_debug;

/*
 *  This mini driver's function interface table.
 */
static struct bridge_drv_interface drv_interface_fxns = {
	/* WCD ver. for which this mini driver is built. */
	WCD_MAJOR_VERSION,
	WCD_MINOR_VERSION,
	bridge_dev_create,
	bridge_dev_destroy,
	bridge_dev_ctrl,
	bridge_brd_monitor,
	bridge_brd_start,
	bridge_brd_stop,
	bridge_brd_status,
	bridge_brd_read,
	bridge_brd_write,
	bridge_brd_set_state,
	bridge_brd_mem_copy,
	bridge_brd_mem_write,
	bridge_brd_mem_map,
	bridge_brd_mem_un_map,
	/* The following CHNL functions are provided by chnl_io.lib: */
	bridge_chnl_create,
	bridge_chnl_destroy,
	bridge_chnl_open,
	bridge_chnl_close,
	bridge_chnl_add_io_req,
	bridge_chnl_get_ioc,
	bridge_chnl_cancel_io,
	bridge_chnl_flush_io,
	bridge_chnl_get_info,
	bridge_chnl_get_mgr_info,
	bridge_chnl_idle,
	bridge_chnl_register_notify,
	/* The following DEH functions are provided by tihelen_ue_deh.c */
	bridge_deh_create,
	bridge_deh_destroy,
	bridge_deh_notify,
	bridge_deh_register_notify,
	bridge_deh_get_info,
	/* The following IO functions are provided by chnl_io.lib: */
	bridge_io_create,
	bridge_io_destroy,
	bridge_io_on_loaded,
	bridge_io_get_proc_load,
	/* The following msg_ctrl functions are provided by chnl_io.lib: */
	bridge_msg_create,
	bridge_msg_create_queue,
	bridge_msg_delete,
	bridge_msg_delete_queue,
	bridge_msg_get,
	bridge_msg_put,
	bridge_msg_register_notify,
	bridge_msg_set_queue_id,
};

static inline void tlb_flush_all(const void __iomem *base)
{
	__raw_writeb(__raw_readb(base + MMU_GFLUSH) | 1, base + MMU_GFLUSH);
}

static inline void flush_all(struct wmd_dev_context *dev_context)
{
	if (dev_context->dw_brd_state == BRD_DSP_HIBERNATION ||
	    dev_context->dw_brd_state == BRD_HIBERNATION)
		wake_dsp(dev_context, NULL);

	tlb_flush_all(dev_context->dw_dsp_mmu_base);
}

static void bad_page_dump(u32 pa, struct page *pg)
{
	pr_emerg("DSPBRIDGE: MAP function: COUNT 0 FOR PA 0x%x\n", pa);
	pr_emerg("Bad page state in process '%s'\n"
		 "page:%p flags:0x%0*lx mapping:%p mapcount:%d count:%d\n"
		 "Backtrace:\n",
		 current->comm, pg, (int)(2 * sizeof(unsigned long)),
		 (unsigned long)pg->flags, pg->mapping,
		 page_mapcount(pg), page_count(pg));
	dump_stack();
}

/*
 *  ======== bridge_drv_entry ========
 *  purpose:
 *      Mini Driver entry point.
 */
void bridge_drv_entry(OUT struct bridge_drv_interface **ppDrvInterface,
		   IN CONST char *pstrWMDFileName)
{

	DBC_REQUIRE(pstrWMDFileName != NULL);

	io_sm_init();		/* Initialization of io_sm module */

	if (strcmp(pstrWMDFileName, "UMA") == 0)
		*ppDrvInterface = &drv_interface_fxns;
	else
		dev_dbg(bridge, "%s Unknown WMD file name", __func__);

}

/*
 *  ======== bridge_brd_monitor ========
 *  purpose:
 *      This bridge_brd_monitor puts DSP into a Loadable state.
 *      i.e Application can load and start the device.
 *
 *  Preconditions:
 *      Device in 'OFF' state.
 */
static dsp_status bridge_brd_monitor(struct wmd_dev_context *hDevContext)
{
	dsp_status status = DSP_SOK;
	struct wmd_dev_context *dev_context = hDevContext;
	struct cfg_hostres resources;
	u32 temp;
	enum hw_pwr_state_t pwr_state;

	status = cfg_get_host_resources((struct cfg_devnode *)
					drv_get_first_dev_extension(),
					&resources);
	if (DSP_FAILED(status))
		goto error_return;

	get_hw_regs(resources.dw_prm_base, resources.dw_cm_base);
	hw_pwrst_iva2_reg_get(resources.dw_prm_base, &temp);
	if ((temp & 0x03) != 0x03 || (temp & 0x03) != 0x02) {
		/* IVA2 is not in ON state */
		/* Read and set PM_PWSTCTRL_IVA2  to ON */
		hw_pwr_iva2_power_state_set(resources.dw_prm_base,
					    HW_PWR_DOMAIN_DSP, HW_PWR_STATE_ON);
		/* Set the SW supervised state transition */
		hw_pwr_clkctrl_iva2_reg_set(resources.dw_cm_base,
					    HW_SW_SUP_WAKEUP);
		/* Wait until the state has moved to ON */
		hw_pwr_iva2_state_get(resources.dw_prm_base, HW_PWR_DOMAIN_DSP,
				      &pwr_state);
		/* Disable Automatic transition */
		hw_pwr_clkctrl_iva2_reg_set(resources.dw_cm_base,
					    HW_AUTOTRANS_DIS);
	}

	get_hw_regs(resources.dw_prm_base, resources.dw_cm_base);
	hw_rst_un_reset(resources.dw_prm_base, HW_RST2_IVA2);
	services_clk_enable(SERVICESCLK_IVA2_CK);

	if (DSP_SUCCEEDED(status)) {
		/* set the device state to IDLE */
		dev_context->dw_brd_state = BRD_IDLE;
	}
error_return:
	get_hw_regs(resources.dw_prm_base, resources.dw_cm_base);
	return status;
}

/*
 *  ======== bridge_brd_read ========
 *  purpose:
 *      Reads buffers for DSP memory.
 */
static dsp_status bridge_brd_read(struct wmd_dev_context *hDevContext,
				  OUT u8 *pbHostBuf, u32 dwDSPAddr,
				  u32 ul_num_bytes, u32 ulMemType)
{
	dsp_status status = DSP_SOK;
	struct wmd_dev_context *dev_context = hDevContext;
	u32 offset;
	u32 dsp_base_addr = hDevContext->dw_dsp_base_addr;

	if (dwDSPAddr < dev_context->dw_dsp_start_add) {
		status = DSP_EFAIL;
		return status;
	}
	/* change here to account for the 3 bands of the DSP internal memory */
	if ((dwDSPAddr - dev_context->dw_dsp_start_add) <
	    dev_context->dw_internal_size) {
		offset = dwDSPAddr - dev_context->dw_dsp_start_add;
	} else {
		status = read_ext_dsp_data(dev_context, pbHostBuf, dwDSPAddr,
					   ul_num_bytes, ulMemType);
		return status;
	}
	/* copy the data from  DSP memory, */
	memcpy(pbHostBuf, (void *)(dsp_base_addr + offset), ul_num_bytes);
	return status;
}

/*
 *  ======== bridge_brd_set_state ========
 *  purpose:
 *      This routine updates the Board status.
 */
static dsp_status bridge_brd_set_state(struct wmd_dev_context *hDevContext,
				    u32 ulBrdState)
{
	dsp_status status = DSP_SOK;
	struct wmd_dev_context *dev_context = hDevContext;

	dev_context->dw_brd_state = ulBrdState;
	return status;
}

/*
 *  ======== bridge_brd_start ========
 *  purpose:
 *      Initializes DSP MMU and Starts DSP.
 *
 *  Preconditions:
 *  a) DSP domain is 'ACTIVE'.
 *  b) DSP_RST1 is asserted.
 *  b) DSP_RST2 is released.
 */
static dsp_status bridge_brd_start(struct wmd_dev_context *hDevContext,
				   u32 dwDSPAddr)
{
	dsp_status status = DSP_SOK;
	struct wmd_dev_context *dev_context = hDevContext;
	u32 dw_sync_addr = 0;
	u32 ul_shm_base;	/* Gpp Phys SM base addr(byte) */
	u32 ul_shm_base_virt;	/* Dsp Virt SM base addr */
	u32 ul_tlb_base_virt;	/* Base of MMU TLB entry */
	/* Offset of shm_base_virt from tlb_base_virt */
	u32 ul_shm_offset_virt;
	s32 entry_ndx;
	s32 itmp_entry_ndx = 0;	/* DSP-MMU TLB entry base address */
	struct cfg_hostres resources;
	u32 temp;
	u32 ul_dsp_clk_rate;
	u32 ul_dsp_clk_addr;
	u32 ul_bios_gp_timer;
	u32 clk_cmd;
	struct io_mgr *hio_mgr;
	u32 ul_load_monitor_timer;
	u32 ext_clk_id = 0;
	u32 tmp_index;
	u32 clk_id_index = MBX_PM_MAX_RESOURCES;

	/* The device context contains all the mmu setup info from when the
	 * last dsp base image was loaded. The first entry is always
	 * SHMMEM base. */
	/* Get SHM_BEG - convert to byte address */
	(void)dev_get_symbol(dev_context->hdev_obj, SHMBASENAME,
			     &ul_shm_base_virt);
	ul_shm_base_virt *= DSPWORDSIZE;
	DBC_ASSERT(ul_shm_base_virt != 0);
	/* DSP Virtual address */
	ul_tlb_base_virt = dev_context->atlb_entry[0].ul_dsp_va;
	DBC_ASSERT(ul_tlb_base_virt <= ul_shm_base_virt);
	ul_shm_offset_virt =
	    ul_shm_base_virt - (ul_tlb_base_virt * DSPWORDSIZE);
	/* Kernel logical address */
	ul_shm_base = dev_context->atlb_entry[0].ul_gpp_va + ul_shm_offset_virt;

	DBC_ASSERT(ul_shm_base != 0);
	/* 2nd wd is used as sync field */
	dw_sync_addr = ul_shm_base + SHMSYNCOFFSET;
	/* Write a signature into the shm base + offset; this will
	 * get cleared when the DSP program starts. */
	if ((ul_shm_base_virt == 0) || (ul_shm_base == 0)) {
		pr_err("%s: Illegal SM base\n", __func__);
		status = DSP_EFAIL;
	} else
		*((volatile u32 *)dw_sync_addr) = 0xffffffff;

	if (DSP_SUCCEEDED(status)) {
		status = cfg_get_host_resources((struct cfg_devnode *)
						drv_get_first_dev_extension(),
						&resources);
		/* Assert RST1 i.e only the RST only for DSP megacell */
		/* hw_rst_reset(resources.dwPrcmBase, HW_RST1_IVA2); */
		if (DSP_SUCCEEDED(status)) {
			hw_rst_reset(resources.dw_prm_base, HW_RST1_IVA2);
			if (dsp_debug) {
				/* Set the bootmode to self loop */
				dev_dbg(bridge, "Set boot mode to self loop"
					" for IVA2 Device\n");
				hw_dspss_boot_mode_set
				    (resources.dw_sys_ctrl_base,
				     HW_DSPSYSC_SELFLOOPBOOT, dwDSPAddr);
			} else {
				/* Set the bootmode to '0' - direct boot */
				dev_dbg(bridge, "Set boot mode to direct boot"
					" for IVA2 Device\n");
				hw_dspss_boot_mode_set
				    (resources.dw_sys_ctrl_base,
				     HW_DSPSYSC_DIRECTBOOT, dwDSPAddr);
			}
		}
	}
	if (DSP_SUCCEEDED(status)) {
		/* Reset and Unreset the RST2, so that BOOTADDR is copied to
		 * IVA2 SYSC register */
		hw_rst_reset(resources.dw_prm_base, HW_RST2_IVA2);
		udelay(100);
		hw_rst_un_reset(resources.dw_prm_base, HW_RST2_IVA2);
		udelay(100);

		get_hw_regs(resources.dw_prm_base, resources.dw_cm_base);
		/* Disbale the DSP MMU */
		hw_mmu_disable(resources.dw_dmmu_base);
		/* Disable TWL */
		hw_mmu_twl_disable(resources.dw_dmmu_base);

		/* Only make TLB entry if both addresses are non-zero */
		for (entry_ndx = 0; entry_ndx < WMDIOCTL_NUMOFMMUTLB;
		     entry_ndx++) {
			if ((dev_context->atlb_entry[entry_ndx].ul_gpp_pa != 0)
			    && (dev_context->atlb_entry[entry_ndx].ul_dsp_va !=
				0)) {
				dev_dbg(bridge,
					"(proc) MMU %d GppPa:"
					" 0x%x DspVa 0x%x Size 0x%x\n",
					itmp_entry_ndx,
					dev_context->atlb_entry[entry_ndx].
					ul_gpp_pa,
					dev_context->atlb_entry[entry_ndx].
					ul_dsp_va,
					dev_context->atlb_entry[entry_ndx].
					ul_size);
				configure_dsp_mmu(dev_context,
						  dev_context->
						  atlb_entry[entry_ndx].
						  ul_gpp_pa,
						  dev_context->
						  atlb_entry[entry_ndx].
						  ul_dsp_va * DSPWORDSIZE,
						  dev_context->
						  atlb_entry[entry_ndx].ul_size,
						  itmp_entry_ndx,
						  dev_context->
						  atlb_entry[entry_ndx].
						  endianism,
						  dev_context->
						  atlb_entry[entry_ndx].
						  elem_size,
						  dev_context->
						  atlb_entry[entry_ndx].
						  mixed_mode);
				itmp_entry_ndx++;
			}
		}		/* end for */
	}

	/* Lock the above TLB entries and get the BIOS and load monitor timer
	 * information */
	if (DSP_SUCCEEDED(status)) {
		hw_mmu_num_locked_set(resources.dw_dmmu_base, itmp_entry_ndx);
		hw_mmu_victim_num_set(resources.dw_dmmu_base, itmp_entry_ndx);
		hw_mmu_ttb_set(resources.dw_dmmu_base,
			       dev_context->pt_attrs->l1_base_pa);
		hw_mmu_twl_enable(resources.dw_dmmu_base);
		/* Enable the SmartIdle and AutoIdle bit for MMU_SYSCONFIG */

		temp = __raw_readl((resources.dw_dmmu_base) + 0x10);
		temp = (temp & 0xFFFFFFEF) | 0x11;
		__raw_writel(temp, (resources.dw_dmmu_base) + 0x10);

		/* Let the DSP MMU run */
		hw_mmu_enable(resources.dw_dmmu_base);

		/* Enable the BIOS clock */
		(void)dev_get_symbol(dev_context->hdev_obj,
				     BRIDGEINIT_BIOSGPTIMER, &ul_bios_gp_timer);
		(void)dev_get_symbol(dev_context->hdev_obj,
				     BRIDGEINIT_LOADMON_GPTIMER,
				     &ul_load_monitor_timer);
	}

	if (DSP_SUCCEEDED(status)) {
		if (ul_load_monitor_timer != 0xFFFF) {
			clk_cmd = (BPWR_DISABLE_CLOCK << MBX_PM_CLK_CMDSHIFT) |
			    ul_load_monitor_timer;

			dsp_peripheral_clk_ctrl(dev_context, &clk_cmd);

			ext_clk_id = clk_cmd & MBX_PM_CLK_IDMASK;
			for (tmp_index = 0; tmp_index < MBX_PM_MAX_RESOURCES;
			     tmp_index++) {
				if (ext_clk_id == bpwr_clkid[tmp_index]) {
					clk_id_index = tmp_index;
					break;
				}
			}

			if (clk_id_index < MBX_PM_MAX_RESOURCES) {
				status =
				    clk_set32k_hz(bpwr_clks
						  [clk_id_index].fun_clk);
			} else {
				status = DSP_EFAIL;
			}
			clk_cmd = (BPWR_ENABLE_CLOCK << MBX_PM_CLK_CMDSHIFT) |
			    ul_load_monitor_timer;

			dsp_peripheral_clk_ctrl(dev_context, &clk_cmd);

		} else {
			dev_dbg(bridge, "Not able to get the symbol for Load "
				"Monitor Timer\n");
		}
	}

	if (DSP_SUCCEEDED(status)) {
		if (ul_bios_gp_timer != 0xFFFF) {
			clk_cmd = (BPWR_DISABLE_CLOCK << MBX_PM_CLK_CMDSHIFT) |
			    ul_bios_gp_timer;

			dsp_peripheral_clk_ctrl(dev_context, &clk_cmd);

			ext_clk_id = clk_cmd & MBX_PM_CLK_IDMASK;

			for (tmp_index = 0; tmp_index < MBX_PM_MAX_RESOURCES;
			     tmp_index++) {
				if (ext_clk_id == bpwr_clkid[tmp_index]) {
					clk_id_index = tmp_index;
					break;
				}
			}

			if (clk_id_index < MBX_PM_MAX_RESOURCES) {
				status =
				    clk_set32k_hz(bpwr_clks
						  [clk_id_index].fun_clk);
			} else {
				status = DSP_EFAIL;
			}

			clk_cmd = (BPWR_ENABLE_CLOCK << MBX_PM_CLK_CMDSHIFT) |
			    ul_bios_gp_timer;

			dsp_peripheral_clk_ctrl(dev_context, &clk_cmd);

		} else {
			dev_dbg(bridge,
				"Not able to get the symbol for BIOS Timer\n");
		}
	}

	if (DSP_SUCCEEDED(status)) {
		/* Set the DSP clock rate */
		(void)dev_get_symbol(dev_context->hdev_obj,
				     "_BRIDGEINIT_DSP_FREQ", &ul_dsp_clk_addr);
		/*Set Autoidle Mode for IVA2 PLL */
		temp = (u32) *((reg_uword32 *)
				((u32) (resources.dw_cm_base) + 0x34));
		temp = (temp & 0xFFFFFFFE) | 0x1;
		*((reg_uword32 *) ((u32) (resources.dw_cm_base) + 0x34)) =
		    (u32) temp;
		if ((unsigned int *)ul_dsp_clk_addr != NULL) {
			/* Get the clock rate */
			status = services_clk_get_rate(SERVICESCLK_IVA2_CK,
						       &ul_dsp_clk_rate);
			dev_dbg(bridge, "%s: DSP clock rate (KHZ): 0x%x \n",
				__func__, ul_dsp_clk_rate);
			(void)bridge_brd_write(dev_context,
					       (u8 *) &ul_dsp_clk_rate,
					       ul_dsp_clk_addr, sizeof(u32), 0);
		}
		/*
		 * Enable Mailbox events and also drain any pending
		 * stale messages.
		 */
		dev_context->mbox = omap_mbox_get("dsp");
		if (IS_ERR(dev_context->mbox)) {
			dev_context->mbox = NULL;
			pr_err("%s: Failed to get dsp mailbox handle\n",
								__func__);
			status = DSP_EFAIL;
		}

	}
	if (DSP_SUCCEEDED(status)) {
		dev_context->mbox->rxq->callback = (int (*)(void *))io_mbox_msg;

/*PM_IVA2GRPSEL_PER = 0xC0;*/
		temp = (u32) *((reg_uword32 *)
				((u32) (resources.dw_per_pm_base) + 0xA8));
		temp = (temp & 0xFFFFFF30) | 0xC0;
		*((reg_uword32 *) ((u32) (resources.dw_per_pm_base) + 0xA8)) =
		    (u32) temp;

/*PM_MPUGRPSEL_PER &= 0xFFFFFF3F; */
		temp = (u32) *((reg_uword32 *)
				((u32) (resources.dw_per_pm_base) + 0xA4));
		temp = (temp & 0xFFFFFF3F);
		*((reg_uword32 *) ((u32) (resources.dw_per_pm_base) + 0xA4)) =
		    (u32) temp;
/*CM_SLEEPDEP_PER |= 0x04; */
		temp = (u32) *((reg_uword32 *)
				((u32) (resources.dw_per_base) + 0x44));
		temp = (temp & 0xFFFFFFFB) | 0x04;
		*((reg_uword32 *) ((u32) (resources.dw_per_base) + 0x44)) =
		    (u32) temp;

/*CM_CLKSTCTRL_IVA2 = 0x00000003 -To Allow automatic transitions */
		temp = (u32) *((reg_uword32 *)
				((u32) (resources.dw_cm_base) + 0x48));
		temp = (temp & 0xFFFFFFFC) | 0x03;
		*((reg_uword32 *) ((u32) (resources.dw_cm_base) + 0x48)) =
		    (u32) temp;

		/* Let DSP go */
		dev_dbg(bridge, "%s Unreset\n", __func__);
		/* Enable DSP MMU Interrupts */
		hw_mmu_event_enable(resources.dw_dmmu_base,
				    HW_MMU_ALL_INTERRUPTS);
		/* release the RST1, DSP starts executing now .. */
		hw_rst_un_reset(resources.dw_prm_base, HW_RST1_IVA2);

		dev_dbg(bridge, "Waiting for Sync @ 0x%x\n", dw_sync_addr);
		dev_dbg(bridge, "DSP c_int00 Address =  0x%x\n", dwDSPAddr);
		if (dsp_debug)
			while (*((volatile u16 *)dw_sync_addr))
				;;

		/* Wait for DSP to clear word in shared memory */
		/* Read the Location */
		if (!wait_for_start(dev_context, dw_sync_addr))
			status = WMD_E_TIMEOUT;

		/* Start wdt */
		dsp_wdt_sm_set((void *)ul_shm_base);
		dsp_wdt_enable(true);

		status = dev_get_io_mgr(dev_context->hdev_obj, &hio_mgr);
		if (DSP_SUCCEEDED(status)) {
			io_sh_msetting(hio_mgr, SHM_OPPINFO, NULL);
			/* Write the synchronization bit to indicate the
			 * completion of OPP table update to DSP
			 */
			*((volatile u32 *)dw_sync_addr) = 0XCAFECAFE;

			/* update board state */
			dev_context->dw_brd_state = BRD_RUNNING;
			/* (void)chnlsm_enable_interrupt(dev_context); */
		} else {
			dev_context->dw_brd_state = BRD_UNKNOWN;
		}
	}
	return status;
}

/*
 *  ======== bridge_brd_stop ========
 *  purpose:
 *      Puts DSP in self loop.
 *
 *  Preconditions :
 *  a) None
 */
static dsp_status bridge_brd_stop(struct wmd_dev_context *hDevContext)
{
	dsp_status status = DSP_SOK;
	struct wmd_dev_context *dev_context = hDevContext;
	struct cfg_hostres resources;
	struct pg_table_attrs *pt_attrs;
	u32 dsp_pwr_state;
	dsp_status clk_status;

	if (dev_context->dw_brd_state == BRD_STOPPED)
		return status;

	/* as per TRM, it is advised to first drive the IVA2 to 'Standby' mode,
	 * before turning off the clocks.. This is to ensure that there are no
	 * pending L3 or other transactons from IVA2 */
	status = cfg_get_host_resources((struct cfg_devnode *)
					drv_get_first_dev_extension(),
					&resources);
	if (DSP_FAILED(status))
		return DSP_EFAIL;

	hw_pwrst_iva2_reg_get(resources.dw_prm_base, &dsp_pwr_state);
	if (dsp_pwr_state != HW_PWR_STATE_OFF) {
		sm_interrupt_dsp(dev_context, MBX_PM_DSPIDLE);
		mdelay(10);
		get_hw_regs(resources.dw_prm_base, resources.dw_cm_base);
		udelay(50);

		/* IVA2 is not in OFF state */
		/* Set PM_PWSTCTRL_IVA2  to OFF */
		hw_pwr_iva2_power_state_set(resources.dw_prm_base,
					    HW_PWR_DOMAIN_DSP,
					    HW_PWR_STATE_OFF);
		/* Set the SW supervised state transition for Sleep */
		hw_pwr_clkctrl_iva2_reg_set(resources.dw_cm_base,
					    HW_SW_SUP_SLEEP);
	}
	udelay(10);
	/* Release the Ext Base virtual Address as the next DSP Program
	 * may have a different load address */
	if (dev_context->dw_dsp_ext_base_addr)
		dev_context->dw_dsp_ext_base_addr = 0;

	dev_context->dw_brd_state = BRD_STOPPED;	/* update board state */

	dsp_wdt_enable(false);

	/* This is a good place to clear the MMU page tables as well */
	if (dev_context->pt_attrs) {
		pt_attrs = dev_context->pt_attrs;
		memset((u8 *) pt_attrs->l1_base_va, 0x00, pt_attrs->l1_size);
		memset((u8 *) pt_attrs->l2_base_va, 0x00, pt_attrs->l2_size);
		memset((u8 *) pt_attrs->pg_info, 0x00,
		       (pt_attrs->l2_num_pages * sizeof(struct page_info)));
	}
	/* Disable the mailbox interrupts */
	if (dev_context->mbox) {
		omap_mbox_disable_irq(dev_context->mbox, IRQ_RX);
		omap_mbox_put(dev_context->mbox);
		dev_context->mbox = NULL;
	}

	hw_rst_reset(resources.dw_prm_base, HW_RST1_IVA2);
	hw_rst_reset(resources.dw_prm_base, HW_RST2_IVA2);
	hw_rst_reset(resources.dw_prm_base, HW_RST3_IVA2);

	status = dsp_peripheral_clocks_disable(dev_context, NULL);
	clk_status = services_clk_disable(SERVICESCLK_IVA2_CK);

	return status;
}

/*
 *  ======== wmd_brd_delete ========
 *  purpose:
 *      Puts DSP in Low power mode
 *
 *  Preconditions :
 *  a) None
 */
static dsp_status wmd_brd_delete(struct wmd_dev_context *hDevContext)
{
	dsp_status status = DSP_SOK;
	struct wmd_dev_context *dev_context = hDevContext;
	struct cfg_hostres resources;
	struct pg_table_attrs *pt_attrs;
	dsp_status clk_status;

	if (dev_context->dw_brd_state == BRD_STOPPED)
		return status;

	/* as per TRM, it is advised to first drive
	 * the IVA2 to 'Standby' mode, before turning off the clocks.. This is
	 * to ensure that there are no pending L3 or other transactons from
	 * IVA2 */
	status = cfg_get_host_resources((struct cfg_devnode *)
					drv_get_first_dev_extension(),
					&resources);
	if (DSP_FAILED(status))
		return DSP_EFAIL;

	status = sleep_dsp(dev_context, PWR_EMERGENCYDEEPSLEEP, NULL);
	clk_status = services_clk_disable(SERVICESCLK_IVA2_CK);

	/* Release the Ext Base virtual Address as the next DSP Program
	 * may have a different load address */
	if (dev_context->dw_dsp_ext_base_addr)
		dev_context->dw_dsp_ext_base_addr = 0;

	dev_context->dw_brd_state = BRD_STOPPED;	/* update board state */

	/* This is a good place to clear the MMU page tables as well */
	if (dev_context->pt_attrs) {
		pt_attrs = dev_context->pt_attrs;
		memset((u8 *) pt_attrs->l1_base_va, 0x00, pt_attrs->l1_size);
		memset((u8 *) pt_attrs->l2_base_va, 0x00, pt_attrs->l2_size);
		memset((u8 *) pt_attrs->pg_info, 0x00,
		       (pt_attrs->l2_num_pages * sizeof(struct page_info)));
	}
	/* Disable the mail box interrupts */
	if (dev_context->mbox) {
		omap_mbox_disable_irq(dev_context->mbox, IRQ_RX);
		omap_mbox_put(dev_context->mbox);
		dev_context->mbox = NULL;
	}

	hw_rst_reset(resources.dw_prm_base, HW_RST1_IVA2);
	hw_rst_reset(resources.dw_prm_base, HW_RST2_IVA2);
	hw_rst_reset(resources.dw_prm_base, HW_RST3_IVA2);

	return status;
}

/*
 *  ======== bridge_brd_status ========
 *      Returns the board status.
 */
static dsp_status bridge_brd_status(struct wmd_dev_context *hDevContext,
				    int *pdwState)
{
	struct wmd_dev_context *dev_context = hDevContext;
	*pdwState = dev_context->dw_brd_state;
	return DSP_SOK;
}

/*
 *  ======== bridge_brd_write ========
 *      Copies the buffers to DSP internal or external memory.
 */
static dsp_status bridge_brd_write(struct wmd_dev_context *hDevContext,
				   IN u8 *pbHostBuf, u32 dwDSPAddr,
				   u32 ul_num_bytes, u32 ulMemType)
{
	dsp_status status = DSP_SOK;
	struct wmd_dev_context *dev_context = hDevContext;

	if (dwDSPAddr < dev_context->dw_dsp_start_add) {
		status = DSP_EFAIL;
		return status;
	}
	if ((dwDSPAddr - dev_context->dw_dsp_start_add) <
	    dev_context->dw_internal_size) {
		status = write_dsp_data(hDevContext, pbHostBuf, dwDSPAddr,
					ul_num_bytes, ulMemType);
	} else {
		status = write_ext_dsp_data(dev_context, pbHostBuf, dwDSPAddr,
					    ul_num_bytes, ulMemType, false);
	}

	return status;
}

/*
 *  ======== bridge_dev_create ========
 *      Creates a driver object. Puts DSP in self loop.
 */
static dsp_status bridge_dev_create(OUT struct wmd_dev_context **ppDevContext,
				    struct dev_object *hdev_obj,
				    IN CONST struct cfg_hostres *pConfig,
				    IN CONST struct cfg_dspres *pDspConfig)
{
	dsp_status status = DSP_SOK;
	struct wmd_dev_context *dev_context = NULL;
	s32 entry_ndx;
	s32 tc_word_swap;
	u32 tc_word_swap_size = sizeof(tc_word_swap);
	struct cfg_hostres resources;
	struct pg_table_attrs *pt_attrs;
	u32 pg_tbl_pa;
	u32 pg_tbl_va;
	u32 align_size;

	/* Allocate and initialize a data structure to contain the mini driver
	 *  state, which becomes the context for later calls into this WMD. */
	dev_context = mem_calloc(sizeof(struct wmd_dev_context), MEM_NONPAGED);
	if (!dev_context) {
		status = DSP_EMEMORY;
		goto func_end;
	}
	status = cfg_get_host_resources((struct cfg_devnode *)
					drv_get_first_dev_extension(),
					&resources);
	if (DSP_FAILED(status)) {
		status = DSP_EMEMORY;
		goto func_end;
	}

	dev_context->dw_dsp_start_add = (u32) OMAP_GEM_BASE;
	dev_context->dw_self_loop = (u32) NULL;
	dev_context->dsp_per_clks = 0;
	dev_context->dw_internal_size = OMAP_DSP_SIZE;
	/*  Clear dev context MMU table entries.
	 *  These get set on WMD_BRD_IOCTL() call after program loaded. */
	for (entry_ndx = 0; entry_ndx < WMDIOCTL_NUMOFMMUTLB; entry_ndx++) {
		dev_context->atlb_entry[entry_ndx].ul_gpp_pa =
		    dev_context->atlb_entry[entry_ndx].ul_dsp_va = 0;
	}
	dev_context->num_tlb_entries = 0;
	dev_context->dw_dsp_base_addr = (u32) MEM_LINEAR_ADDRESS((void *)
								 (pConfig->
								  dw_mem_base
								  [3]),
								 pConfig->
								 dw_mem_length
								 [3]);
	if (!dev_context->dw_dsp_base_addr)
		status = DSP_EFAIL;

	pt_attrs = mem_calloc(sizeof(struct pg_table_attrs), MEM_NONPAGED);
	if (pt_attrs != NULL) {
		pt_attrs->l1_size = SZ_16K; /* 4096 entries of 32 bits */
		align_size = pt_attrs->l1_size;
		/* Align sizes are expected to be power of 2 */
		/* we like to get aligned on L1 table size */
		pg_tbl_va = (u32) mem_alloc_phys_mem(pt_attrs->l1_size,
						     align_size, &pg_tbl_pa);

		/* Check if the PA is aligned for us */
		if ((pg_tbl_pa) & (align_size - 1)) {
			/* PA not aligned to page table size ,
			 * try with more allocation and align */
			mem_free_phys_mem((void *)pg_tbl_va, pg_tbl_pa,
					  pt_attrs->l1_size);
			/* we like to get aligned on L1 table size */
			pg_tbl_va =
			    (u32) mem_alloc_phys_mem((pt_attrs->l1_size) * 2,
						     align_size, &pg_tbl_pa);
			/* We should be able to get aligned table now */
			pt_attrs->l1_tbl_alloc_pa = pg_tbl_pa;
			pt_attrs->l1_tbl_alloc_va = pg_tbl_va;
			pt_attrs->l1_tbl_alloc_sz = pt_attrs->l1_size * 2;
			/* Align the PA to the next 'align'  boundary */
			pt_attrs->l1_base_pa =
			    ((pg_tbl_pa) +
			     (align_size - 1)) & (~(align_size - 1));
			pt_attrs->l1_base_va =
			    pg_tbl_va + (pt_attrs->l1_base_pa - pg_tbl_pa);
		} else {
			/* We got aligned PA, cool */
			pt_attrs->l1_tbl_alloc_pa = pg_tbl_pa;
			pt_attrs->l1_tbl_alloc_va = pg_tbl_va;
			pt_attrs->l1_tbl_alloc_sz = pt_attrs->l1_size;
			pt_attrs->l1_base_pa = pg_tbl_pa;
			pt_attrs->l1_base_va = pg_tbl_va;
		}
		if (pt_attrs->l1_base_va)
			memset((u8 *) pt_attrs->l1_base_va, 0x00,
			       pt_attrs->l1_size);

		/* number of L2 page tables = DMM pool used + SHMMEM +EXTMEM +
		 * L4 pages */
		pt_attrs->l2_num_pages = ((DMMPOOLSIZE >> 20) + 6);
		pt_attrs->l2_size = HW_MMU_COARSE_PAGE_SIZE *
		    pt_attrs->l2_num_pages;
		align_size = 4;	/* Make it u32 aligned */
		/* we like to get aligned on L1 table size */
		pg_tbl_va = (u32) mem_alloc_phys_mem(pt_attrs->l2_size,
						     align_size, &pg_tbl_pa);
		pt_attrs->l2_tbl_alloc_pa = pg_tbl_pa;
		pt_attrs->l2_tbl_alloc_va = pg_tbl_va;
		pt_attrs->l2_tbl_alloc_sz = pt_attrs->l2_size;
		pt_attrs->l2_base_pa = pg_tbl_pa;
		pt_attrs->l2_base_va = pg_tbl_va;

		if (pt_attrs->l2_base_va)
			memset((u8 *) pt_attrs->l2_base_va, 0x00,
			       pt_attrs->l2_size);

		pt_attrs->pg_info = mem_calloc(pt_attrs->l2_num_pages *
					       sizeof(struct page_info),
					       MEM_NONPAGED);
		dev_dbg(bridge,
			"L1 pa %x, va %x, size %x\n L2 pa %x, va "
			"%x, size %x\n", pt_attrs->l1_base_pa,
			pt_attrs->l1_base_va, pt_attrs->l1_size,
			pt_attrs->l2_base_pa, pt_attrs->l2_base_va,
			pt_attrs->l2_size);
		dev_dbg(bridge, "pt_attrs %p L2 NumPages %x pg_info %p\n",
			pt_attrs, pt_attrs->l2_num_pages, pt_attrs->pg_info);
	}
	if ((pt_attrs != NULL) && (pt_attrs->l1_base_va != 0) &&
	    (pt_attrs->l2_base_va != 0) && (pt_attrs->pg_info != NULL))
		dev_context->pt_attrs = pt_attrs;
	else
		status = DSP_EMEMORY;

	if (DSP_SUCCEEDED(status))
		status = sync_initialize_cs(&pt_attrs->hcs_obj);

	if (DSP_SUCCEEDED(status)) {
		/* Set the Endianism Register *//* Need to set this */
		/* Retrieve the TC u16 SWAP Option */
		status = reg_get_value(TCWORDSWAP, (u8 *) &tc_word_swap,
				       &tc_word_swap_size);
		/* Save the value */
		dev_context->tc_word_swap_on = tc_word_swap;
	}
	if (DSP_SUCCEEDED(status)) {
		/* 24xx-Linux MMU address is obtained from the host
		 * resources struct */
		dev_context->dw_dsp_mmu_base = resources.dw_dmmu_base;
	}
	if (DSP_SUCCEEDED(status)) {
		dev_context->hdev_obj = hdev_obj;
		dev_context->ul_int_mask = 0;
		/* Store current board state. */
		dev_context->dw_brd_state = BRD_STOPPED;
		/* Return ptr to our device state to the WCD for storage */
		*ppDevContext = dev_context;
	} else {
		if (pt_attrs != NULL) {
			if (pt_attrs->hcs_obj)
				sync_delete_cs(pt_attrs->hcs_obj);

			kfree(pt_attrs->pg_info);

			if (pt_attrs->l2_tbl_alloc_va) {
				mem_free_phys_mem((void *)
						  pt_attrs->l2_tbl_alloc_va,
						  pt_attrs->l2_tbl_alloc_pa,
						  pt_attrs->l2_tbl_alloc_sz);
			}
			if (pt_attrs->l1_tbl_alloc_va) {
				mem_free_phys_mem((void *)
						  pt_attrs->l1_tbl_alloc_va,
						  pt_attrs->l1_tbl_alloc_pa,
						  pt_attrs->l1_tbl_alloc_sz);
			}
		}
		kfree(pt_attrs);
		kfree(dev_context);
	}
func_end:
	return status;
}

/*
 *  ======== bridge_dev_ctrl ========
 *      Receives device specific commands.
 */
static dsp_status bridge_dev_ctrl(struct wmd_dev_context *dev_context,
				  u32 dw_cmd, IN OUT void *pargs)
{
	dsp_status status = DSP_SOK;
	struct wmdioctl_extproc *pa_ext_proc = (struct wmdioctl_extproc *)pargs;
	s32 ndx;

	switch (dw_cmd) {
	case WMDIOCTL_CHNLREAD:
		break;
	case WMDIOCTL_CHNLWRITE:
		break;
	case WMDIOCTL_SETMMUCONFIG:
		/* store away dsp-mmu setup values for later use */
		for (ndx = 0; ndx < WMDIOCTL_NUMOFMMUTLB; ndx++, pa_ext_proc++)
			dev_context->atlb_entry[ndx] = *pa_ext_proc;
		break;
	case WMDIOCTL_DEEPSLEEP:
	case WMDIOCTL_EMERGENCYSLEEP:
		/* Currently only DSP Idle is supported Need to update for
		 * later releases */
		status = sleep_dsp(dev_context, PWR_DEEPSLEEP, pargs);
		break;
	case WMDIOCTL_WAKEUP:
		status = wake_dsp(dev_context, pargs);
		break;
	case WMDIOCTL_CLK_CTRL:
		status = DSP_SOK;
		/* Looking For Baseport Fix for Clocks */
		status = dsp_peripheral_clk_ctrl(dev_context, pargs);
		break;
	case WMDIOCTL_PWR_HIBERNATE:
		status = handle_hibernation_from_dsp(dev_context);
		break;
	case WMDIOCTL_PRESCALE_NOTIFY:
		status = pre_scale_dsp(dev_context, pargs);
		break;
	case WMDIOCTL_POSTSCALE_NOTIFY:
		status = post_scale_dsp(dev_context, pargs);
		break;
	case WMDIOCTL_CONSTRAINT_REQUEST:
		status = handle_constraints_set(dev_context, pargs);
		break;
	default:
		status = DSP_EFAIL;
		break;
	}
	return status;
}

/*
 *  ======== bridge_dev_destroy ========
 *      Destroys the driver object.
 */
static dsp_status bridge_dev_destroy(struct wmd_dev_context *hDevContext)
{
	struct pg_table_attrs *pt_attrs;
	dsp_status status = DSP_SOK;
	struct wmd_dev_context *dev_context = (struct wmd_dev_context *)
	    hDevContext;

	/* It should never happen */
	if (!hDevContext)
		return DSP_EHANDLE;

	/* first put the device to stop state */
	wmd_brd_delete(dev_context);
	if (dev_context->pt_attrs) {
		pt_attrs = dev_context->pt_attrs;
		if (pt_attrs->hcs_obj)
			sync_delete_cs(pt_attrs->hcs_obj);

		kfree(pt_attrs->pg_info);

		if (pt_attrs->l2_tbl_alloc_va) {
			mem_free_phys_mem((void *)pt_attrs->l2_tbl_alloc_va,
					  pt_attrs->l2_tbl_alloc_pa,
					  pt_attrs->l2_tbl_alloc_sz);
		}
		if (pt_attrs->l1_tbl_alloc_va) {
			mem_free_phys_mem((void *)pt_attrs->l1_tbl_alloc_va,
					  pt_attrs->l1_tbl_alloc_pa,
					  pt_attrs->l1_tbl_alloc_sz);
		}
		kfree(pt_attrs);

	}
	/* Free the driver's device context: */
	kfree((void *)hDevContext);
	return status;
}

static dsp_status bridge_brd_mem_copy(struct wmd_dev_context *hDevContext,
				   u32 ulDspDestAddr, u32 ulDspSrcAddr,
				   u32 ul_num_bytes, u32 ulMemType)
{
	dsp_status status = DSP_SOK;
	u32 src_addr = ulDspSrcAddr;
	u32 dest_addr = ulDspDestAddr;
	u32 copy_bytes = 0;
	u32 total_bytes = ul_num_bytes;
	u8 host_buf[BUFFERSIZE];
	struct wmd_dev_context *dev_context = hDevContext;
	while ((total_bytes > 0) && DSP_SUCCEEDED(status)) {
		copy_bytes =
		    total_bytes > BUFFERSIZE ? BUFFERSIZE : total_bytes;
		/* Read from External memory */
		status = read_ext_dsp_data(hDevContext, host_buf, src_addr,
					   copy_bytes, ulMemType);
		if (DSP_SUCCEEDED(status)) {
			if (dest_addr < (dev_context->dw_dsp_start_add +
					 dev_context->dw_internal_size)) {
				/* Write to Internal memory */
				status = write_dsp_data(hDevContext, host_buf,
							dest_addr, copy_bytes,
							ulMemType);
			} else {
				/* Write to External memory */
				status =
				    write_ext_dsp_data(hDevContext, host_buf,
						       dest_addr, copy_bytes,
						       ulMemType, false);
			}
		}
		total_bytes -= copy_bytes;
		src_addr += copy_bytes;
		dest_addr += copy_bytes;
	}
	return status;
}

/* Mem Write does not halt the DSP to write unlike bridge_brd_write */
static dsp_status bridge_brd_mem_write(struct wmd_dev_context *hDevContext,
				    IN u8 *pbHostBuf, u32 dwDSPAddr,
				    u32 ul_num_bytes, u32 ulMemType)
{
	dsp_status status = DSP_SOK;
	struct wmd_dev_context *dev_context = hDevContext;
	u32 ul_remain_bytes = 0;
	u32 ul_bytes = 0;
	ul_remain_bytes = ul_num_bytes;
	while (ul_remain_bytes > 0 && DSP_SUCCEEDED(status)) {
		ul_bytes =
		    ul_remain_bytes > BUFFERSIZE ? BUFFERSIZE : ul_remain_bytes;
		if (dwDSPAddr < (dev_context->dw_dsp_start_add +
				 dev_context->dw_internal_size)) {
			status =
			    write_dsp_data(hDevContext, pbHostBuf, dwDSPAddr,
					   ul_bytes, ulMemType);
		} else {
			status = write_ext_dsp_data(hDevContext, pbHostBuf,
						    dwDSPAddr, ul_bytes,
						    ulMemType, true);
		}
		ul_remain_bytes -= ul_bytes;
		dwDSPAddr += ul_bytes;
		pbHostBuf = pbHostBuf + ul_bytes;
	}
	return status;
}

/*
 *  ======== bridge_brd_mem_map ========
 *      This function maps MPU buffer to the DSP address space. It performs
 *  linear to physical address translation if required. It translates each
 *  page since linear addresses can be physically non-contiguous
 *  All address & size arguments are assumed to be page aligned (in proc.c)
 *
 *  TODO: Disable MMU while updating the page tables (but that'll stall DSP)
 */
static dsp_status bridge_brd_mem_map(struct wmd_dev_context *hDevContext,
				  u32 ul_mpu_addr, u32 ulVirtAddr,
				  u32 ul_num_bytes, u32 ul_map_attr)
{
	u32 attrs;
	dsp_status status = DSP_SOK;
	struct wmd_dev_context *dev_context = hDevContext;
	struct hw_mmu_map_attrs_t hw_attrs;
	struct vm_area_struct *vma;
	struct mm_struct *mm = current->mm;
	u32 write = 0;
	u32 num_usr_pgs = 0;
	struct page *mapped_page, *pg;
	s32 pg_num;
	u32 va = ulVirtAddr;
	struct task_struct *curr_task = current;
	u32 pg_i = 0;
	u32 mpu_addr, pa;

	dev_dbg(bridge,
		"%s hDevCtxt %p, pa %x, va %x, size %x, ul_map_attr %x\n",
		__func__, hDevContext, ul_mpu_addr, ulVirtAddr, ul_num_bytes,
		ul_map_attr);
	if (ul_num_bytes == 0)
		return DSP_EINVALIDARG;

	if (ul_map_attr & DSP_MAP_DIR_MASK) {
		attrs = ul_map_attr;
	} else {
		/* Assign default attributes */
		attrs = ul_map_attr | (DSP_MAPVIRTUALADDR | DSP_MAPELEMSIZE16);
	}
	/* Take mapping properties */
	if (attrs & DSP_MAPBIGENDIAN)
		hw_attrs.endianism = HW_BIG_ENDIAN;
	else
		hw_attrs.endianism = HW_LITTLE_ENDIAN;

	hw_attrs.mixed_size = (enum hw_mmu_mixed_size_t)
	    ((attrs & DSP_MAPMIXEDELEMSIZE) >> 2);
	/* Ignore element_size if mixed_size is enabled */
	if (hw_attrs.mixed_size == 0) {
		if (attrs & DSP_MAPELEMSIZE8) {
			/* Size is 8 bit */
			hw_attrs.element_size = HW_ELEM_SIZE8BIT;
		} else if (attrs & DSP_MAPELEMSIZE16) {
			/* Size is 16 bit */
			hw_attrs.element_size = HW_ELEM_SIZE16BIT;
		} else if (attrs & DSP_MAPELEMSIZE32) {
			/* Size is 32 bit */
			hw_attrs.element_size = HW_ELEM_SIZE32BIT;
		} else if (attrs & DSP_MAPELEMSIZE64) {
			/* Size is 64 bit */
			hw_attrs.element_size = HW_ELEM_SIZE64BIT;
		} else {
			/*
			 * Mixedsize isn't enabled, so size can't be
			 * zero here
			 */
			return DSP_EINVALIDARG;
		}
	}
	if (attrs & DSP_MAPDONOTLOCK)
		hw_attrs.donotlockmpupage = 1;
	else
		hw_attrs.donotlockmpupage = 0;

	if (attrs & DSP_MAPVMALLOCADDR) {
		return mem_map_vmalloc(hDevContext, ul_mpu_addr, ulVirtAddr,
				       ul_num_bytes, &hw_attrs);
	}
	/*
	 * Do OS-specific user-va to pa translation.
	 * Combine physically contiguous regions to reduce TLBs.
	 * Pass the translated pa to pte_update.
	 */
	if ((attrs & DSP_MAPPHYSICALADDR)) {
		status = pte_update(dev_context, ul_mpu_addr, ulVirtAddr,
				    ul_num_bytes, &hw_attrs);
		goto func_cont;
	}

	/*
	 * Important Note: ul_mpu_addr is mapped from user application process
	 * to current process - it must lie completely within the current
	 * virtual memory address space in order to be of use to us here!
	 */
	down_read(&mm->mmap_sem);
	vma = find_vma(mm, ul_mpu_addr);
	if (vma)
		dev_dbg(bridge,
			"VMAfor UserBuf: ul_mpu_addr=%x, ul_num_bytes=%x, "
			"vm_start=%lx, vm_end=%lx, vm_flags=%lx\n", ul_mpu_addr,
			ul_num_bytes, vma->vm_start, vma->vm_end,
			vma->vm_flags);

	/*
	 * It is observed that under some circumstances, the user buffer is
	 * spread across several VMAs. So loop through and check if the entire
	 * user buffer is covered
	 */
	while ((vma) && (ul_mpu_addr + ul_num_bytes > vma->vm_end)) {
		/* jump to the next VMA region */
		vma = find_vma(mm, vma->vm_end + 1);
		dev_dbg(bridge,
			"VMA for UserBuf ul_mpu_addr=%x ul_num_bytes=%x, "
			"vm_start=%lx, vm_end=%lx, vm_flags=%lx\n", ul_mpu_addr,
			ul_num_bytes, vma->vm_start, vma->vm_end,
			vma->vm_flags);
	}
	if (!vma) {
		pr_err("%s: Failed to get VMA region for 0x%x (%d)\n",
		       __func__, ul_mpu_addr, ul_num_bytes);
		status = DSP_EINVALIDARG;
		up_read(&mm->mmap_sem);
		goto func_cont;
	}

	if (vma->vm_flags & VM_IO) {
		num_usr_pgs = ul_num_bytes / PG_SIZE4K;
		mpu_addr = ul_mpu_addr;

		/* Get the physical addresses for user buffer */
		for (pg_i = 0; pg_i < num_usr_pgs; pg_i++) {
			pa = user_va2_pa(mm, mpu_addr);
			if (!pa) {
				status = DSP_EFAIL;
				pr_err("DSPBRIDGE: VM_IO mapping physical"
				       "address is invalid\n");
				break;
			}
			if (pfn_valid(__phys_to_pfn(pa))) {
				pg = PHYS_TO_PAGE(pa);
				get_page(pg);
				if (page_count(pg) < 1) {
					pr_err("Bad page in VM_IO buffer\n");
					bad_page_dump(pa, pg);
				}
			}
			status = pte_set(dev_context->pt_attrs, pa,
					 va, HW_PAGE_SIZE4KB, &hw_attrs);
			if (DSP_FAILED(status))
				break;

			va += HW_PAGE_SIZE4KB;
			mpu_addr += HW_PAGE_SIZE4KB;
			pa += HW_PAGE_SIZE4KB;
		}
	} else {
		num_usr_pgs = ul_num_bytes / PG_SIZE4K;
		if (vma->vm_flags & (VM_WRITE | VM_MAYWRITE))
			write = 1;

		for (pg_i = 0; pg_i < num_usr_pgs; pg_i++) {
			pg_num = get_user_pages(curr_task, mm, ul_mpu_addr, 1,
						write, 1, &mapped_page, NULL);
			if (pg_num > 0) {
				if (page_count(mapped_page) < 1) {
					pr_err("Bad page count after doing"
					       "get_user_pages on"
					       "user buffer\n");
					bad_page_dump(page_to_phys(mapped_page),
						      mapped_page);
				}
				status = pte_set(dev_context->pt_attrs,
						 page_to_phys(mapped_page), va,
						 HW_PAGE_SIZE4KB, &hw_attrs);
				if (DSP_FAILED(status))
					break;

				va += HW_PAGE_SIZE4KB;
				ul_mpu_addr += HW_PAGE_SIZE4KB;
			} else {
				pr_err("DSPBRIDGE: get_user_pages FAILED,"
				       "MPU addr = 0x%x,"
				       "vma->vm_flags = 0x%lx,"
				       "get_user_pages Err"
				       "Value = %d, Buffer"
				       "size=0x%x\n", ul_mpu_addr,
				       vma->vm_flags, pg_num, ul_num_bytes);
				status = DSP_EFAIL;
				break;
			}
		}
	}
	up_read(&mm->mmap_sem);
func_cont:
	/* Don't propogate Linux or HW status to upper layers */
	if (DSP_SUCCEEDED(status)) {
		status = DSP_SOK;
	} else {
		/*
		 * Roll out the mapped pages incase it failed in middle of
		 * mapping
		 */
		if (pg_i) {
			bridge_brd_mem_un_map(dev_context, ulVirtAddr,
					   (pg_i * PG_SIZE4K));
		}
		status = DSP_EFAIL;
	}
	/*
	 * In any case, flush the TLB
	 * This is called from here instead from pte_update to avoid unnecessary
	 * repetition while mapping non-contiguous physical regions of a virtual
	 * region
	 */
	flush_all(dev_context);
	dev_dbg(bridge, "%s status %x\n", __func__, status);
	return status;
}

/*
 *  ======== bridge_brd_mem_un_map ========
 *      Invalidate the PTEs for the DSP VA block to be unmapped.
 *
 *      PTEs of a mapped memory block are contiguous in any page table
 *      So, instead of looking up the PTE address for every 4K block,
 *      we clear consecutive PTEs until we unmap all the bytes
 */
static dsp_status bridge_brd_mem_un_map(struct wmd_dev_context *hDevContext,
				     u32 ulVirtAddr, u32 ul_num_bytes)
{
	u32 l1_base_va;
	u32 l2_base_va;
	u32 l2_base_pa;
	u32 l2_page_num;
	u32 pte_val;
	u32 pte_size;
	u32 pte_count;
	u32 pte_addr_l1;
	u32 pte_addr_l2 = 0;
	u32 rem_bytes;
	u32 rem_bytes_l2;
	u32 va_curr;
	struct page *pg = NULL;
	dsp_status status = DSP_SOK;
	struct wmd_dev_context *dev_context = hDevContext;
	struct pg_table_attrs *pt = dev_context->pt_attrs;
	u32 temp;
	u32 paddr;
	u32 numof4k_pages = 0;

	va_curr = ulVirtAddr;
	rem_bytes = ul_num_bytes;
	rem_bytes_l2 = 0;
	l1_base_va = pt->l1_base_va;
	pte_addr_l1 = hw_mmu_pte_addr_l1(l1_base_va, va_curr);
	dev_dbg(bridge, "%s hDevContext %p, va %x, NumBytes %x l1_base_va %x, "
		"pte_addr_l1 %x\n", __func__, hDevContext, ulVirtAddr,
		ul_num_bytes, l1_base_va, pte_addr_l1);

	while (rem_bytes && (DSP_SUCCEEDED(status))) {
		u32 va_curr_orig = va_curr;
		/* Find whether the L1 PTE points to a valid L2 PT */
		pte_addr_l1 = hw_mmu_pte_addr_l1(l1_base_va, va_curr);
		pte_val = *(u32 *) pte_addr_l1;
		pte_size = hw_mmu_pte_size_l1(pte_val);

		if (pte_size != HW_MMU_COARSE_PAGE_SIZE)
			goto skip_coarse_page;

		/*
		 * Get the L2 PA from the L1 PTE, and find
		 * corresponding L2 VA
		 */
		l2_base_pa = hw_mmu_pte_coarse_l1(pte_val);
		l2_base_va = l2_base_pa - pt->l2_base_pa + pt->l2_base_va;
		l2_page_num =
		    (l2_base_pa - pt->l2_base_pa) / HW_MMU_COARSE_PAGE_SIZE;
		/*
		 * Find the L2 PTE address from which we will start
		 * clearing, the number of PTEs to be cleared on this
		 * page, and the size of VA space that needs to be
		 * cleared on this L2 page
		 */
		pte_addr_l2 = hw_mmu_pte_addr_l2(l2_base_va, va_curr);
		pte_count = pte_addr_l2 & (HW_MMU_COARSE_PAGE_SIZE - 1);
		pte_count = (HW_MMU_COARSE_PAGE_SIZE - pte_count) / sizeof(u32);
		if (rem_bytes < (pte_count * PG_SIZE4K))
			pte_count = rem_bytes / PG_SIZE4K;
		rem_bytes_l2 = pte_count * PG_SIZE4K;

		/*
		 * Unmap the VA space on this L2 PT. A quicker way
		 * would be to clear pte_count entries starting from
		 * pte_addr_l2. However, below code checks that we don't
		 * clear invalid entries or less than 64KB for a 64KB
		 * entry. Similar checking is done for L1 PTEs too
		 * below
		 */
		while (rem_bytes_l2 && (DSP_SUCCEEDED(status))) {
			pte_val = *(u32 *) pte_addr_l2;
			pte_size = hw_mmu_pte_size_l2(pte_val);
			/* va_curr aligned to pte_size? */
			if (pte_size == 0 || rem_bytes_l2 < pte_size ||
			    va_curr & (pte_size - 1)) {
				status = DSP_EFAIL;
				break;
			}

			/* Collect Physical addresses from VA */
			paddr = (pte_val & ~(pte_size - 1));
			if (pte_size == HW_PAGE_SIZE64KB)
				numof4k_pages = 16;
			else
				numof4k_pages = 1;
			temp = 0;
			while (temp++ < numof4k_pages) {
				if (!pfn_valid(__phys_to_pfn(paddr))) {
					paddr += HW_PAGE_SIZE4KB;
					continue;
				}
				pg = PHYS_TO_PAGE(paddr);
				if (page_count(pg) < 1) {
					pr_info("DSPBRIDGE: UNMAP function: "
						"COUNT 0 FOR PA 0x%x, size = "
						"0x%x\n", paddr, ul_num_bytes);
					bad_page_dump(paddr, pg);
				} else {
					SetPageDirty(pg);
					page_cache_release(pg);
				}
				paddr += HW_PAGE_SIZE4KB;
			}
			if (hw_mmu_pte_clear(pte_addr_l2, va_curr, pte_size)
			    == RET_FAIL) {
				status = DSP_EFAIL;
				goto EXIT_LOOP;
			}

			status = DSP_SOK;
			rem_bytes_l2 -= pte_size;
			va_curr += pte_size;
			pte_addr_l2 += (pte_size >> 12) * sizeof(u32);
		}
		sync_enter_cs(pt->hcs_obj);
		if (rem_bytes_l2 == 0) {
			pt->pg_info[l2_page_num].num_entries -= pte_count;
			if (pt->pg_info[l2_page_num].num_entries == 0) {
				/*
				 * Clear the L1 PTE pointing to the L2 PT
				 */
				if (hw_mmu_pte_clear(l1_base_va, va_curr_orig,
						     HW_MMU_COARSE_PAGE_SIZE) ==
				    RET_OK)
					status = DSP_SOK;
				else {
					status = DSP_EFAIL;
					sync_leave_cs(pt->hcs_obj);
					goto EXIT_LOOP;
				}
			}
			rem_bytes -= pte_count * PG_SIZE4K;
		} else
			status = DSP_EFAIL;

		sync_leave_cs(pt->hcs_obj);
		continue;
skip_coarse_page:
		/* va_curr aligned to pte_size? */
		/* pte_size = 1 MB or 16 MB */
		if (pte_size == 0 || rem_bytes < pte_size ||
		    va_curr & (pte_size - 1)) {
			status = DSP_EFAIL;
			break;
		}

		if (pte_size == HW_PAGE_SIZE1MB)
			numof4k_pages = 256;
		else
			numof4k_pages = 4096;
		temp = 0;
		/* Collect Physical addresses from VA */
		paddr = (pte_val & ~(pte_size - 1));
		while (temp++ < numof4k_pages) {
			if (pfn_valid(__phys_to_pfn(paddr))) {
				pg = PHYS_TO_PAGE(paddr);
				if (page_count(pg) < 1) {
					pr_info("DSPBRIDGE: UNMAP function: "
						"COUNT 0 FOR PA 0x%x, size = "
						"0x%x\n", paddr, ul_num_bytes);
					bad_page_dump(paddr, pg);
				} else {
					SetPageDirty(pg);
					page_cache_release(pg);
				}
			}
			paddr += HW_PAGE_SIZE4KB;
		}
		if (hw_mmu_pte_clear(l1_base_va, va_curr, pte_size) == RET_OK) {
			status = DSP_SOK;
			rem_bytes -= pte_size;
			va_curr += pte_size;
		} else {
			status = DSP_EFAIL;
			goto EXIT_LOOP;
		}
	}
	/*
	 * It is better to flush the TLB here, so that any stale old entries
	 * get flushed
	 */
EXIT_LOOP:
	flush_all(dev_context);
	dev_dbg(bridge,
		"%s: va_curr %x, pte_addr_l1 %x pte_addr_l2 %x rem_bytes %x,"
		" rem_bytes_l2 %x status %x\n", __func__, va_curr, pte_addr_l1,
		pte_addr_l2, rem_bytes, rem_bytes_l2, status);
	return status;
}

/*
 *  ======== user_va2_pa ========
 *  Purpose:
 *      This function walks through the Linux page tables to convert a userland
 *      virtual address to physical address
 */
static u32 user_va2_pa(struct mm_struct *mm, u32 address)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *ptep, pte;

	pgd = pgd_offset(mm, address);
	if (!(pgd_none(*pgd) || pgd_bad(*pgd))) {
		pmd = pmd_offset(pgd, address);
		if (!(pmd_none(*pmd) || pmd_bad(*pmd))) {
			ptep = pte_offset_map(pmd, address);
			if (ptep) {
				pte = *ptep;
				if (pte_present(pte))
					return pte & PAGE_MASK;
			}
		}
	}

	return 0;
}

/*
 *  ======== pte_update ========
 *      This function calculates the optimum page-aligned addresses and sizes
 *      Caller must pass page-aligned values
 */
static dsp_status pte_update(struct wmd_dev_context *hDevContext, u32 pa,
			     u32 va, u32 size,
			     struct hw_mmu_map_attrs_t *map_attrs)
{
	u32 i;
	u32 all_bits;
	u32 pa_curr = pa;
	u32 va_curr = va;
	u32 num_bytes = size;
	struct wmd_dev_context *dev_context = hDevContext;
	dsp_status status = DSP_SOK;
	u32 page_size[] = { HW_PAGE_SIZE16MB, HW_PAGE_SIZE1MB,
		HW_PAGE_SIZE64KB, HW_PAGE_SIZE4KB
	};

	while (num_bytes && DSP_SUCCEEDED(status)) {
		/* To find the max. page size with which both PA & VA are
		 * aligned */
		all_bits = pa_curr | va_curr;

		for (i = 0; i < 4; i++) {
			if ((num_bytes >= page_size[i]) && ((all_bits &
							     (page_size[i] -
							      1)) == 0)) {
				status =
				    pte_set(dev_context->pt_attrs, pa_curr,
					    va_curr, page_size[i], map_attrs);
				pa_curr += page_size[i];
				va_curr += page_size[i];
				num_bytes -= page_size[i];
				/* Don't try smaller sizes. Hopefully we have
				 * reached an address aligned to a bigger page
				 * size */
				break;
			}
		}
	}

	return status;
}

/*
 *  ======== pte_set ========
 *      This function calculates PTE address (MPU virtual) to be updated
 *      It also manages the L2 page tables
 */
static dsp_status pte_set(struct pg_table_attrs *pt, u32 pa, u32 va,
			  u32 size, struct hw_mmu_map_attrs_t *attrs)
{
	u32 i;
	u32 pte_val;
	u32 pte_addr_l1;
	u32 pte_size;
	/* Base address of the PT that will be updated */
	u32 pg_tbl_va;
	u32 l1_base_va;
	/* Compiler warns that the next three variables might be used
	 * uninitialized in this function. Doesn't seem so. Working around,
	 * anyways. */
	u32 l2_base_va = 0;
	u32 l2_base_pa = 0;
	u32 l2_page_num = 0;
	dsp_status status = DSP_SOK;

	l1_base_va = pt->l1_base_va;
	pg_tbl_va = l1_base_va;
	if ((size == HW_PAGE_SIZE64KB) || (size == HW_PAGE_SIZE4KB)) {
		/* Find whether the L1 PTE points to a valid L2 PT */
		pte_addr_l1 = hw_mmu_pte_addr_l1(l1_base_va, va);
		if (pte_addr_l1 <= (pt->l1_base_va + pt->l1_size)) {
			pte_val = *(u32 *) pte_addr_l1;
			pte_size = hw_mmu_pte_size_l1(pte_val);
		} else {
			return DSP_EFAIL;
		}
		sync_enter_cs(pt->hcs_obj);
		if (pte_size == HW_MMU_COARSE_PAGE_SIZE) {
			/* Get the L2 PA from the L1 PTE, and find
			 * corresponding L2 VA */
			l2_base_pa = hw_mmu_pte_coarse_l1(pte_val);
			l2_base_va =
			    l2_base_pa - pt->l2_base_pa + pt->l2_base_va;
			l2_page_num =
			    (l2_base_pa -
			     pt->l2_base_pa) / HW_MMU_COARSE_PAGE_SIZE;
		} else if (pte_size == 0) {
			/* L1 PTE is invalid. Allocate a L2 PT and
			 * point the L1 PTE to it */
			/* Find a free L2 PT. */
			for (i = 0; (i < pt->l2_num_pages) &&
			     (pt->pg_info[i].num_entries != 0); i++)
				;;
			if (i < pt->l2_num_pages) {
				l2_page_num = i;
				l2_base_pa = pt->l2_base_pa + (l2_page_num *
						HW_MMU_COARSE_PAGE_SIZE);
				l2_base_va = pt->l2_base_va + (l2_page_num *
						HW_MMU_COARSE_PAGE_SIZE);
				/* Endianness attributes are ignored for
				 * HW_MMU_COARSE_PAGE_SIZE */
				status =
				    hw_mmu_pte_set(l1_base_va, l2_base_pa, va,
						   HW_MMU_COARSE_PAGE_SIZE,
						   attrs);
			} else {
				status = DSP_EMEMORY;
			}
		} else {
			/* Found valid L1 PTE of another size.
			 * Should not overwrite it. */
			status = DSP_EFAIL;
		}
		if (DSP_SUCCEEDED(status)) {
			pg_tbl_va = l2_base_va;
			if (size == HW_PAGE_SIZE64KB)
				pt->pg_info[l2_page_num].num_entries += 16;
			else
				pt->pg_info[l2_page_num].num_entries++;
			dev_dbg(bridge, "PTE: L2 BaseVa %x, BasePa %x, PageNum "
				"%x, num_entries %x\n", l2_base_va,
				l2_base_pa, l2_page_num,
				pt->pg_info[l2_page_num].num_entries);
		}
		sync_leave_cs(pt->hcs_obj);
	}
	if (DSP_SUCCEEDED(status)) {
		dev_dbg(bridge, "PTE: pg_tbl_va %x, pa %x, va %x, size %x\n",
			pg_tbl_va, pa, va, size);
		dev_dbg(bridge, "PTE: endianism %x, element_size %x, "
			"mixed_size %x\n", attrs->endianism,
			attrs->element_size, attrs->mixed_size);
		status = hw_mmu_pte_set(pg_tbl_va, pa, va, size, attrs);
	}

	return status;
}

/* Memory map kernel VA -- memory allocated with vmalloc */
static dsp_status mem_map_vmalloc(struct wmd_dev_context *dev_context,
				  u32 ul_mpu_addr, u32 ulVirtAddr,
				  u32 ul_num_bytes,
				  struct hw_mmu_map_attrs_t *hw_attrs)
{
	dsp_status status = DSP_SOK;
	struct page *page[1];
	u32 i;
	u32 pa_curr;
	u32 pa_next;
	u32 va_curr;
	u32 size_curr;
	u32 num_pages;
	u32 pa;
	u32 num_of4k_pages;
	u32 temp = 0;

	/*
	 * Do Kernel va to pa translation.
	 * Combine physically contiguous regions to reduce TLBs.
	 * Pass the translated pa to pte_update.
	 */
	num_pages = ul_num_bytes / PAGE_SIZE;	/* PAGE_SIZE = OS page size */
	i = 0;
	va_curr = ul_mpu_addr;
	page[0] = vmalloc_to_page((void *)va_curr);
	pa_next = page_to_phys(page[0]);
	while (DSP_SUCCEEDED(status) && (i < num_pages)) {
		/*
		 * Reuse pa_next from the previous iteraion to avoid
		 * an extra va2pa call
		 */
		pa_curr = pa_next;
		size_curr = PAGE_SIZE;
		/*
		 * If the next page is physically contiguous,
		 * map it with the current one by increasing
		 * the size of the region to be mapped
		 */
		while (++i < num_pages) {
			page[0] =
			    vmalloc_to_page((void *)(va_curr + size_curr));
			pa_next = page_to_phys(page[0]);

			if (pa_next == (pa_curr + size_curr))
				size_curr += PAGE_SIZE;
			else
				break;

		}
		if (pa_next == 0) {
			status = DSP_EMEMORY;
			break;
		}
		pa = pa_curr;
		num_of4k_pages = size_curr / HW_PAGE_SIZE4KB;
		while (temp++ < num_of4k_pages) {
			get_page(PHYS_TO_PAGE(pa));
			pa += HW_PAGE_SIZE4KB;
		}
		status = pte_update(dev_context, pa_curr, ulVirtAddr +
				    (va_curr - ul_mpu_addr), size_curr,
				    hw_attrs);
		va_curr += size_curr;
	}
	/* Don't propogate Linux or HW status to upper layers */
	if (DSP_SUCCEEDED(status))
		status = DSP_SOK;
	else
		status = DSP_EFAIL;

	/*
	 * In any case, flush the TLB
	 * This is called from here instead from pte_update to avoid unnecessary
	 * repetition while mapping non-contiguous physical regions of a virtual
	 * region
	 */
	flush_all(dev_context);
	dev_dbg(bridge, "%s status %x\n", __func__, status);
	return status;
}

/*
 *  ======== configure_dsp_mmu ========
 *      Make DSP MMU page table entries.
 */
void configure_dsp_mmu(struct wmd_dev_context *dev_context, u32 dataBasePhys,
		       u32 dspBaseVirt, u32 sizeInBytes, s32 nEntryStart,
		       enum hw_endianism_t endianism,
		       enum hw_element_size_t elem_size,
		       enum hw_mmu_mixed_size_t mixed_size)
{
	struct hw_mmu_map_attrs_t map_attrs = { endianism, elem_size,
						mixed_size };

	DBC_REQUIRE(sizeInBytes > 0);
	dev_dbg(bridge, "%s: entry %x pa %x, va %x, bytes %x endianism %x, "
		"elem_size %x, mixed_size %x", __func__, nEntryStart,
		dataBasePhys, dspBaseVirt, sizeInBytes, endianism,
		elem_size, mixed_size);

	hw_mmu_tlb_add(dev_context->dw_dsp_mmu_base, dataBasePhys,
		       dspBaseVirt, sizeInBytes, nEntryStart,
		       &map_attrs, HW_SET, HW_SET);
}

/*
 *  ======== wait_for_start ========
 *      Wait for the singal from DSP that it has started, or time out.
 */
bool wait_for_start(struct wmd_dev_context *dev_context, u32 dw_sync_addr)
{
	u16 timeout = TIHELEN_ACKTIMEOUT;

	/*  Wait for response from board */
	while (*((volatile u16 *)dw_sync_addr) && --timeout)
		udelay(10);

	/*  If timed out: return FALSE */
	if (!timeout) {
		pr_err("%s: Timed out waiting DSP to Start\n", __func__);
		return FALSE;
	}
	return TRUE;
}
