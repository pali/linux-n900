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

#include <linux/platform_data/dsp-omap.h>
#include <linux/iommu.h>
#include <linux/types.h>

/*  ----------------------------------- Host OS */
#include <dspbridge/host_os.h>
#include <linux/mm.h>
#include <linux/mmzone.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/dbdefs.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/drv.h>
#include <dspbridge/sync.h>

/* ------------------------------------ Hardware Abstraction Layer */
#include <hw_defs.h>
#include <hw_mmu.h>

/*  ----------------------------------- Link Driver */
#include <dspbridge/dspdefs.h>
#include <dspbridge/dspchnl.h>
#include <dspbridge/dspdeh.h>
#include <dspbridge/dspio.h>
#include <dspbridge/dspmsg.h>
#include <dspbridge/pwr.h>
#include <dspbridge/io_sm.h>

/*  ----------------------------------- Platform Manager */
#include <dspbridge/dev.h>
#include <dspbridge/dspapi.h>
#include <dspbridge/dmm.h>
#include <dspbridge/wdt.h>

/*  ----------------------------------- Local */
#include "_tiomap.h"
#include "_tiomap_pwr.h"
#include "tiomap_io.h"

#include "../../../../arch/arm/mach-omap2/cm3xxx.h"

/* Offset in shared mem to write to in order to synchronize start with DSP */
#define SHMSYNCOFFSET 4		/* GPP byte offset */

#define BUFFERSIZE 1024

#define TIHELEN_ACKTIMEOUT  10000

#define MMU_SECTION_ADDR_MASK    0xFFF00000
#define MMU_SSECTION_ADDR_MASK   0xFF000000
#define MMU_LARGE_PAGE_MASK      0xFFFF0000
#define MMU_SMALL_PAGE_MASK      0xFFFFF000
#define OMAP3_IVA2_BOOTADDR_MASK 0xFFFFFC00
#define PAGES_II_LVL_TABLE   512
#define PHYS_TO_PAGE(phys)      pfn_to_page((phys) >> PAGE_SHIFT)

/* IVA Boot modes */
#define DIRECT		0
#define IDLE		1

/* Forward Declarations: */
static int bridge_brd_monitor(struct bridge_dev_context *dev_ctxt);
static int bridge_brd_read(struct bridge_dev_context *dev_ctxt,
				  u8 *host_buff,
				  u32 dsp_addr, u32 ul_num_bytes,
				  u32 mem_type);
static int bridge_brd_start(struct bridge_dev_context *dev_ctxt,
				   u32 dsp_addr);
static int bridge_brd_status(struct bridge_dev_context *dev_ctxt,
				    int *board_state);
static int bridge_brd_stop(struct bridge_dev_context *dev_ctxt);
static int bridge_brd_write(struct bridge_dev_context *dev_ctxt,
				   u8 *host_buff,
				   u32 dsp_addr, u32 ul_num_bytes,
				   u32 mem_type);
static int bridge_brd_set_state(struct bridge_dev_context *dev_ctxt,
				    u32 brd_state);
static int bridge_brd_mem_copy(struct bridge_dev_context *dev_ctxt,
				   u32 dsp_dest_addr, u32 dsp_src_addr,
				   u32 ul_num_bytes, u32 mem_type);
static int bridge_brd_mem_write(struct bridge_dev_context *dev_ctxt,
				    u8 *host_buff, u32 dsp_addr,
				    u32 ul_num_bytes, u32 mem_type);
static int bridge_brd_mem_map(struct bridge_dev_context *dev_ctxt,
				  u32 uva, u32 da,
				  u32 size, u32 attr,
				  struct page **usr_pgs);
static int bridge_brd_mem_un_map(struct bridge_dev_context *dev_ctxt,
				     u32 virt_addr, u32 size);
static int bridge_dev_create(struct bridge_dev_context
					**dev_cntxt,
					struct dev_object *hdev_obj,
					struct cfg_hostres *config_param);
static int bridge_dev_ctrl(struct bridge_dev_context *dev_context,
				  u32 dw_cmd, void *pargs);
static int bridge_dev_destroy(struct bridge_dev_context *dev_ctxt);
static int get_io_pages(struct mm_struct *mm, u32 uva, unsigned pages,
			struct page **usr_pgs);
static u32 user_va2_pa(struct mm_struct *mm, u32 address);

bool wait_for_start(struct bridge_dev_context *dev_context,
			void __iomem *sync_addr);

/*  ----------------------------------- Globals */

/* Attributes of L2 page tables for DSP MMU */
struct page_info {
	u32 num_entries;	/* Number of valid PTEs in the L2 PT */
};

/* Attributes used to manage the DSP MMU page tables */
struct pg_table_attrs {
	spinlock_t pg_lock;	/* Critical section object handle */

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
 *  This Bridge driver's function interface table.
 */
static struct bridge_drv_interface drv_interface_fxns = {
	/* Bridge API ver. for which this bridge driver is built. */
	BRD_API_MAJOR_VERSION,
	BRD_API_MINOR_VERSION,
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

static inline void flush_all(struct bridge_dev_context *dev_context)
{
	if (dev_context->brd_state == BRD_DSP_HIBERNATION ||
	    dev_context->brd_state == BRD_HIBERNATION)
		wake_dsp(dev_context, NULL);
}

/*
 *  ======== bridge_drv_entry ========
 *  purpose:
 *      Bridge Driver entry point.
 */
void bridge_drv_entry(struct bridge_drv_interface **drv_intf,
		   const char *driver_file_name)
{
	if (strcmp(driver_file_name, "UMA") == 0)
		*drv_intf = &drv_interface_fxns;
	else
		dev_dbg(bridge, "%s Unknown Bridge file name", __func__);

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
static int bridge_brd_monitor(struct bridge_dev_context *dev_ctxt)
{
	struct bridge_dev_context *dev_context = dev_ctxt;
	u32 temp;
	struct omap_dsp_platform_data *pdata =
		omap_dspbridge_dev->dev.platform_data;

	temp = (*pdata->dsp_prm_read)(OMAP3430_IVA2_MOD, OMAP2_PM_PWSTST) &
					OMAP_POWERSTATEST_MASK;
	if (!(temp & 0x02)) {
		/* IVA2 is not in ON state */
		/* Read and set PM_PWSTCTRL_IVA2  to ON */
		(*pdata->dsp_prm_rmw_bits)(OMAP_POWERSTATEST_MASK,
			PWRDM_POWER_ON, OMAP3430_IVA2_MOD, OMAP2_PM_PWSTCTRL);
		/* Set the SW supervised state transition */
		(*pdata->dsp_cm_write)(OMAP34XX_CLKSTCTRL_FORCE_WAKEUP,
					OMAP3430_IVA2_MOD, OMAP2_CM_CLKSTCTRL);

		/* Wait until the state has moved to ON */
		while ((*pdata->dsp_prm_read)(OMAP3430_IVA2_MOD,
					      OMAP2_PM_PWSTST) &
						OMAP_INTRANSITION_MASK)
			;
		/* Disable Automatic transition */
		(*pdata->dsp_cm_write)(OMAP34XX_CLKSTCTRL_DISABLE_AUTO,
					OMAP3430_IVA2_MOD, OMAP2_CM_CLKSTCTRL);
	}
	(*pdata->dsp_prm_rmw_bits)(OMAP3430_RST2_IVA2_MASK, 0,
					OMAP3430_IVA2_MOD, OMAP2_RM_RSTCTRL);
	dsp_clk_enable(DSP_CLK_IVA2);

	/* set the device state to IDLE */
	dev_context->brd_state = BRD_IDLE;

	return 0;
}

/*
 *  ======== bridge_brd_read ========
 *  purpose:
 *      Reads buffers for DSP memory.
 */
static int bridge_brd_read(struct bridge_dev_context *dev_ctxt,
				  u8 *host_buff, u32 dsp_addr,
				  u32 ul_num_bytes, u32 mem_type)
{
	int status = 0;
	struct bridge_dev_context *dev_context = dev_ctxt;
	u32 offset;
	u32 dsp_base_addr = dev_ctxt->dsp_base_addr;

	if (dsp_addr < dev_context->dsp_start_add) {
		status = -EPERM;
		return status;
	}
	/* change here to account for the 3 bands of the DSP internal memory */
	if ((dsp_addr - dev_context->dsp_start_add) <
	    dev_context->internal_size) {
		offset = dsp_addr - dev_context->dsp_start_add;
	} else {
		status = read_ext_dsp_data(dev_context, host_buff, dsp_addr,
					   ul_num_bytes, mem_type);
		return status;
	}
	/* copy the data from DSP memory */
	memcpy(host_buff, (void *)(dsp_base_addr + offset), ul_num_bytes);
	return status;
}

/*
 *  ======== bridge_brd_set_state ========
 *  purpose:
 *      This routine updates the Board status.
 */
static int bridge_brd_set_state(struct bridge_dev_context *dev_ctxt,
				    u32 brd_state)
{
	int status = 0;
	struct bridge_dev_context *dev_context = dev_ctxt;

	dev_context->brd_state = brd_state;
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
static int bridge_brd_start(struct bridge_dev_context *dev_ctxt,
				   u32 dsp_addr)
{
	int status = 0;
	struct bridge_dev_context *dev_context = dev_ctxt;
	void __iomem *sync_addr;
	u32 ul_shm_base;	/* Gpp Phys SM base addr(byte) */
	u32 ul_shm_base_virt;	/* Dsp Virt SM base addr */
	u32 ul_tlb_base_virt;	/* Base of MMU TLB entry */
	u32 shm_sync_pa;
	/* Offset of shm_base_virt from tlb_base_virt */
	u32 ul_shm_offset_virt;
	s32 entry_ndx;
	s32 itmp_entry_ndx = 0;	/* DSP-MMU TLB entry base address */
	struct cfg_hostres *resources = NULL;
	u32 ul_dsp_clk_rate;
	u32 ul_dsp_clk_addr;
	u32 ul_bios_gp_timer;
	u32 clk_cmd;
	struct io_mgr *hio_mgr;
	u32 ul_load_monitor_timer;
	u32 wdt_en = 0;
	struct omap_dsp_platform_data *pdata =
		omap_dspbridge_dev->dev.platform_data;

	/* The device context contains all the mmu setup info from when the
	 * last dsp base image was loaded. The first entry is always
	 * SHMMEM base. */
	/* Get SHM_BEG - convert to byte address */
	(void)dev_get_symbol(dev_context->dev_obj, SHMBASENAME,
			     &ul_shm_base_virt);
	ul_shm_base_virt *= DSPWORDSIZE;
	/* DSP Virtual address */
	ul_tlb_base_virt = dev_context->atlb_entry[0].dsp_va;
	ul_shm_offset_virt =
	    ul_shm_base_virt - (ul_tlb_base_virt * DSPWORDSIZE);
	/* Kernel logical address */
	ul_shm_base = dev_context->atlb_entry[0].gpp_va + ul_shm_offset_virt;

	/* SHM physical sync address */
	shm_sync_pa = dev_context->atlb_entry[0].gpp_pa + ul_shm_offset_virt +
			SHMSYNCOFFSET;

	/* 2nd wd is used as sync field */
	sync_addr = ioremap(shm_sync_pa, SZ_32);
	if (!sync_addr)
		return -ENOMEM;

	/* Write a signature into the shm base + offset; this will
	 * get cleared when the DSP program starts. */
	if ((ul_shm_base_virt == 0) || (ul_shm_base == 0)) {
		pr_err("%s: Illegal SM base\n", __func__);
		status = -EPERM;
	} else
		__raw_writel(0xffffffff, sync_addr);

	if (!status) {
		resources = dev_context->resources;
		if (!resources)
			status = -EPERM;

		/* Assert RST1 i.e only the RST only for DSP megacell */
		if (!status) {
			(*pdata->dsp_prm_rmw_bits)(OMAP3430_RST1_IVA2_MASK,
					OMAP3430_RST1_IVA2_MASK,
					OMAP3430_IVA2_MOD,
					OMAP2_RM_RSTCTRL);

			/* Mask address with 1K for compatibility */
			pdata->set_bootaddr(dsp_addr &
						OMAP3_IVA2_BOOTADDR_MASK);
			pdata->set_bootmode(dsp_debug ? IDLE : DIRECT);
		}
	}

	if (!status) {
		/* Only make TLB entry if both addresses are non-zero */
		for (entry_ndx = 0; entry_ndx < BRDIOCTL_NUMOFMMUTLB;
		     entry_ndx++) {
			struct bridge_ioctl_extproc *e =
				&dev_context->atlb_entry[entry_ndx];
			if (!e->gpp_pa || !e->dsp_va)
				continue;

			status = iommu_map(iommu_get_domain_for_dev(bridge),
					   e->dsp_va, e->gpp_pa, e->size,
					   IOMMU_READ|IOMMU_WRITE);
			if (status < 0) {
				dev_err(bridge, "iommu_map: %d\n", status);
				break;
			}

			itmp_entry_ndx++;
		}
	}

	/* Lock the above TLB entries and get the BIOS and load monitor timer
	 * information */
	if (!status) {
		/* Enable the BIOS clock */
		(void)dev_get_symbol(dev_context->dev_obj,
				     BRIDGEINIT_BIOSGPTIMER, &ul_bios_gp_timer);
		(void)dev_get_symbol(dev_context->dev_obj,
				     BRIDGEINIT_LOADMON_GPTIMER,
				     &ul_load_monitor_timer);
	}

	if (!status) {
		if (ul_load_monitor_timer != 0xFFFF) {
			clk_cmd = (BPWR_ENABLE_CLOCK << MBX_PM_CLK_CMDSHIFT) |
			    ul_load_monitor_timer;
			dsp_peripheral_clk_ctrl(dev_context, &clk_cmd);
		} else {
			dev_dbg(bridge, "Not able to get the symbol for Load "
				"Monitor Timer\n");
		}

		if (ul_bios_gp_timer != 0xFFFF) {
			clk_cmd = (BPWR_ENABLE_CLOCK << MBX_PM_CLK_CMDSHIFT) |
			    ul_bios_gp_timer;
			dsp_peripheral_clk_ctrl(dev_context, &clk_cmd);
		} else {
			dev_dbg(bridge,
				"Not able to get the symbol for BIOS Timer\n");
		}

		/* Set the DSP clock rate */
		(void)dev_get_symbol(dev_context->dev_obj,
				     "_BRIDGEINIT_DSP_FREQ", &ul_dsp_clk_addr);
		/* Set Autoidle Mode for IVA2 PLL */
		(*pdata->dsp_cm_write)(1,
				OMAP3430_IVA2_MOD, OMAP3430_CM_AUTOIDLE_PLL);

		if ((unsigned int *)ul_dsp_clk_addr != NULL) {
			/* Get the clock rate */
			ul_dsp_clk_rate = dsp_clk_get_iva2_rate();
			dev_dbg(bridge, "%s: DSP clock rate (KHZ): 0x%x\n",
				__func__, ul_dsp_clk_rate);
			(void)bridge_brd_write(dev_context,
					       (u8 *) &ul_dsp_clk_rate,
					       ul_dsp_clk_addr, sizeof(u32), 0);
		}
		/*
		 * Enable Mailbox events and also drain any pending
		 * stale messages.
		 */
		{
			struct mbox_client *client = &dev_context->mbox_client;

			client->dev = bridge;
			client->tx_tout = 0;
			client->tx_done = NULL;
			client->rx_callback = io_mbox_msg;
			client->tx_block = false;
			client->knows_txdone = false;

			dev_context->mbox = mbox_request_channel(client, 0);

			if (IS_ERR(dev_context->mbox)) {
				status = PTR_ERR(dev_context->mbox);
				pr_err("%s: Failed to get dsp mailbox handle %d\n",
				       __func__, status);
				dev_context->mbox = NULL;
			}
		}
	}
	if (!status) {
		/* Let DSP go */
		dev_dbg(bridge, "%s Unreset\n", __func__);

		/* release the RST1, DSP starts executing now .. */
		(*pdata->dsp_prm_rmw_bits)(OMAP3430_RST1_IVA2_MASK, 0,
					OMAP3430_IVA2_MOD, OMAP2_RM_RSTCTRL);

		dev_dbg(bridge, "Waiting for Sync @ 0x%p\n", sync_addr);
		dev_dbg(bridge, "DSP c_int00 Address =  0x%x\n", dsp_addr);
		if (dsp_debug)
			while (__raw_readw(sync_addr))
				;

		/* Wait for DSP to clear word in shared memory */
		/* Read the Location */
		if (!wait_for_start(dev_context, sync_addr))
			status = -ETIMEDOUT;

		dev_get_symbol(dev_context->dev_obj, "_WDT_enable", &wdt_en);
		if (wdt_en) {
			/* Start wdt */
			dsp_wdt_sm_set((void *)ul_shm_base);
			dsp_wdt_enable(true);
		}

		status = dev_get_io_mgr(dev_context->dev_obj, &hio_mgr);
		if (hio_mgr) {
			io_sh_msetting(hio_mgr, SHM_OPPINFO, NULL);
			/* Write the synchronization bit to indicate the
			 * completion of OPP table update to DSP
			 */
			__raw_writel(0XCAFECAFE, sync_addr);

			/* update board state */
			dev_context->brd_state = BRD_RUNNING;
			/* (void)chnlsm_enable_interrupt(dev_context); */
		} else {
			dev_context->brd_state = BRD_UNKNOWN;
		}
	}

	iounmap(sync_addr);

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
static int bridge_brd_stop(struct bridge_dev_context *dev_ctxt)
{
	int status = 0;
	struct bridge_dev_context *dev_context = dev_ctxt;
	struct pg_table_attrs *pt_attrs;
	u32 dsp_pwr_state;
	struct omap_dsp_platform_data *pdata =
		omap_dspbridge_dev->dev.platform_data;

	if (dev_context->brd_state == BRD_STOPPED)
		return status;

	/* as per TRM, it is advised to first drive the IVA2 to 'Standby' mode,
	 * before turning off the clocks.. This is to ensure that there are no
	 * pending L3 or other transactons from IVA2 */
	dsp_pwr_state = (*pdata->dsp_prm_read)
		(OMAP3430_IVA2_MOD, OMAP2_PM_PWSTST) & OMAP_POWERSTATEST_MASK;
	if (dsp_pwr_state != PWRDM_POWER_OFF) {
		(*pdata->dsp_prm_rmw_bits)(OMAP3430_RST2_IVA2_MASK, 0,
					OMAP3430_IVA2_MOD, OMAP2_RM_RSTCTRL);
		sm_interrupt_dsp(dev_context, MBX_PM_DSPIDLE);
		mdelay(10);

		/* IVA2 is not in OFF state */
		/* Set PM_PWSTCTRL_IVA2  to OFF */
		(*pdata->dsp_prm_rmw_bits)(OMAP_POWERSTATEST_MASK,
			PWRDM_POWER_OFF, OMAP3430_IVA2_MOD, OMAP2_PM_PWSTCTRL);
		/* Set the SW supervised state transition for Sleep */
		(*pdata->dsp_cm_write)(OMAP34XX_CLKSTCTRL_FORCE_SLEEP,
					OMAP3430_IVA2_MOD, OMAP2_CM_CLKSTCTRL);
	}
	udelay(10);
	/* Release the Ext Base virtual Address as the next DSP Program
	 * may have a different load address */
	if (dev_context->dsp_ext_base_addr)
		dev_context->dsp_ext_base_addr = 0;

	dev_context->brd_state = BRD_STOPPED;	/* update board state */

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
		mbox_free_channel(dev_context->mbox);
		dev_context->mbox = NULL;
	}
	/* Reset IVA2 clocks*/
	/*(*pdata->dsp_prm_write)(OMAP3430_RST1_IVA2_MASK |
			OMAP3430_RST2_IVA2_MASK | OMAP3430_RST3_IVA2_MASK,
			OMAP3430_IVA2_MOD, OMAP2_RM_RSTCTRL);*/

	dsp_clock_disable_all(dev_context->dsp_per_clks);
	dsp_clk_disable(DSP_CLK_IVA2);

	return status;
}

/*
 *  ======== bridge_brd_status ========
 *      Returns the board status.
 */
static int bridge_brd_status(struct bridge_dev_context *dev_ctxt,
				    int *board_state)
{
	struct bridge_dev_context *dev_context = dev_ctxt;
	*board_state = dev_context->brd_state;
	return 0;
}

/*
 *  ======== bridge_brd_write ========
 *      Copies the buffers to DSP internal or external memory.
 */
static int bridge_brd_write(struct bridge_dev_context *dev_ctxt,
				   u8 *host_buff, u32 dsp_addr,
				   u32 ul_num_bytes, u32 mem_type)
{
	int status = 0;
	struct bridge_dev_context *dev_context = dev_ctxt;

	if (dsp_addr < dev_context->dsp_start_add) {
		status = -EPERM;
		return status;
	}
	if ((dsp_addr - dev_context->dsp_start_add) <
	    dev_context->internal_size) {
		status = write_dsp_data(dev_ctxt, host_buff, dsp_addr,
					ul_num_bytes, mem_type);
	} else {
		status = write_ext_dsp_data(dev_context, host_buff, dsp_addr,
					    ul_num_bytes, mem_type, false);
	}

	return status;
}

/*
 *  ======== bridge_dev_create ========
 *      Creates a driver object. Puts DSP in self loop.
 */
static int bridge_dev_create(struct bridge_dev_context
					**dev_cntxt,
					struct dev_object *hdev_obj,
					struct cfg_hostres *config_param)
{
	int status = 0;
	struct bridge_dev_context *dev_context = NULL;
	s32 entry_ndx;
	struct cfg_hostres *resources = config_param;
	struct pg_table_attrs *pt_attrs;
	u32 pg_tbl_pa;
	u32 pg_tbl_va;
	u32 align_size;
	struct dsp_device *dsp = dev_get_drvdata(bridge);

	/* Allocate and initialize a data structure to contain the bridge driver
	 *  state, which becomes the context for later calls into this driver */
	dev_context = kzalloc(sizeof(struct bridge_dev_context), GFP_KERNEL);
	if (!dev_context) {
		status = -ENOMEM;
		goto func_end;
	}

	dev_context->dsp_start_add = (u32) OMAP_GEM_BASE;
	dev_context->self_loop = (u32) NULL;
	dev_context->dsp_per_clks = 0;
	dev_context->internal_size = OMAP_DSP_SIZE;
	/*  Clear dev context MMU table entries.
	 *  These get set on bridge_io_on_loaded() call after program loaded. */
	for (entry_ndx = 0; entry_ndx < BRDIOCTL_NUMOFMMUTLB; entry_ndx++) {
		dev_context->atlb_entry[entry_ndx].gpp_pa =
		    dev_context->atlb_entry[entry_ndx].dsp_va = 0;
	}
	dev_context->dsp_base_addr = (u32) MEM_LINEAR_ADDRESS((void *)
								 (config_param->
								  mem_base
								  [3]),
								 config_param->
								 mem_length
								 [3]);
	if (!dev_context->dsp_base_addr)
		status = -EPERM;

	pt_attrs = kzalloc(sizeof(struct pg_table_attrs), GFP_KERNEL);
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

		pt_attrs->pg_info = kzalloc(pt_attrs->l2_num_pages *
					sizeof(struct page_info), GFP_KERNEL);
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
		status = -ENOMEM;

	if (!status) {
		spin_lock_init(&pt_attrs->pg_lock);
		dev_context->tc_word_swap_on = dsp->tc_wordswapon;

		/* Set the Clock Divisor for the DSP module */
		udelay(5);
		/* MMU address is obtained from the host
		 * resources struct */
	}
	if (!status) {
		dev_context->dev_obj = hdev_obj;
		/* Store current board state. */
		dev_context->brd_state = BRD_UNKNOWN;
		dev_context->resources = resources;
		dsp_clk_enable(DSP_CLK_IVA2);
		bridge_brd_stop(dev_context);
		/* Return ptr to our device state to the DSP API for storage */
		*dev_cntxt = dev_context;
	} else {
		if (pt_attrs != NULL) {
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
static int bridge_dev_ctrl(struct bridge_dev_context *dev_context,
				  u32 dw_cmd, void *pargs)
{
	int status = 0;
	struct bridge_ioctl_extproc *pa_ext_proc =
					(struct bridge_ioctl_extproc *)pargs;
	s32 ndx;

	switch (dw_cmd) {
	case BRDIOCTL_CHNLREAD:
		break;
	case BRDIOCTL_CHNLWRITE:
		break;
	case BRDIOCTL_SETMMUCONFIG:
		/* store away dsp-mmu setup values for later use */
		for (ndx = 0; ndx < BRDIOCTL_NUMOFMMUTLB; ndx++, pa_ext_proc++)
			dev_context->atlb_entry[ndx] = *pa_ext_proc;
		break;
	case BRDIOCTL_DEEPSLEEP:
	case BRDIOCTL_EMERGENCYSLEEP:
		/* Currently only DSP Idle is supported Need to update for
		 * later releases */
		status = sleep_dsp(dev_context, PWR_DEEPSLEEP, pargs);
		break;
	case BRDIOCTL_WAKEUP:
		status = wake_dsp(dev_context, pargs);
		break;
	case BRDIOCTL_CLK_CTRL:
		status = 0;
		/* Looking For Baseport Fix for Clocks */
		status = dsp_peripheral_clk_ctrl(dev_context, pargs);
		break;
	case BRDIOCTL_PWR_HIBERNATE:
		status = handle_hibernation_from_dsp(dev_context);
		break;
	case BRDIOCTL_PRESCALE_NOTIFY:
		status = pre_scale_dsp(dev_context, pargs);
		break;
	case BRDIOCTL_POSTSCALE_NOTIFY:
		status = post_scale_dsp(dev_context, pargs);
		break;
	case BRDIOCTL_CONSTRAINT_REQUEST:
		status = handle_constraints_set(dev_context, pargs);
		break;
	default:
		status = -EPERM;
		break;
	}
	return status;
}

/*
 *  ======== bridge_dev_destroy ========
 *      Destroys the driver object.
 */
static int bridge_dev_destroy(struct bridge_dev_context *dev_ctxt)
{
	struct pg_table_attrs *pt_attrs;
	int status = 0;
	struct bridge_dev_context *dev_context = (struct bridge_dev_context *)
	    dev_ctxt;
	struct cfg_hostres *host_res;
	u32 shm_size;
	struct dsp_device *dsp = dev_get_drvdata(bridge);

	/* It should never happen */
	if (!dev_ctxt)
		return -EFAULT;

	/* first put the device to stop state */
	bridge_brd_stop(dev_context);
	if (dev_context->pt_attrs) {
		pt_attrs = dev_context->pt_attrs;
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

	if (dev_context->resources) {
		host_res = dev_context->resources;
		shm_size = dsp->shm_size;
		if (shm_size >= 0x10000) {
			if ((host_res->mem_base[1]) &&
			    (host_res->mem_phys[1])) {
				mem_free_phys_mem((void *)
						  host_res->mem_base
						  [1],
						  host_res->mem_phys
						  [1], shm_size);
			}
		} else {
			dev_dbg(bridge, "%s: Error getting shm size "
				"from registry: %x. Not calling "
				"mem_free_phys_mem\n", __func__,
				status);
		}
		host_res->mem_base[1] = 0;
		host_res->mem_phys[1] = 0;

		if (host_res->mem_base[0])
			iounmap((void *)host_res->mem_base[0]);
		if (host_res->mem_base[2])
			iounmap((void *)host_res->mem_base[2]);
		if (host_res->mem_base[3])
			iounmap((void *)host_res->mem_base[3]);
		if (host_res->mem_base[4])
			iounmap((void *)host_res->mem_base[4]);
		if (host_res->per_base)
			iounmap(host_res->per_base);
		if (host_res->per_pm_base)
			iounmap((void *)host_res->per_pm_base);
		if (host_res->core_pm_base)
			iounmap((void *)host_res->core_pm_base);

		host_res->mem_base[0] = (u32) NULL;
		host_res->mem_base[2] = (u32) NULL;
		host_res->mem_base[3] = (u32) NULL;
		host_res->mem_base[4] = (u32) NULL;

		kfree(host_res);
	}

	/* Free the driver's device context: */
	kfree(dsp->base_img);
	kfree((void *)dev_ctxt);
	return status;
}

static int bridge_brd_mem_copy(struct bridge_dev_context *dev_ctxt,
				   u32 dsp_dest_addr, u32 dsp_src_addr,
				   u32 ul_num_bytes, u32 mem_type)
{
	int status = 0;
	u32 src_addr = dsp_src_addr;
	u32 dest_addr = dsp_dest_addr;
	u32 copy_bytes = 0;
	u32 total_bytes = ul_num_bytes;
	u8 host_buf[BUFFERSIZE];
	struct bridge_dev_context *dev_context = dev_ctxt;

	while (total_bytes > 0 && !status) {
		copy_bytes =
		    total_bytes > BUFFERSIZE ? BUFFERSIZE : total_bytes;
		/* Read from External memory */
		status = read_ext_dsp_data(dev_ctxt, host_buf, src_addr,
					   copy_bytes, mem_type);
		if (!status) {
			if (dest_addr < (dev_context->dsp_start_add +
					 dev_context->internal_size)) {
				/* Write to Internal memory */
				status = write_dsp_data(dev_ctxt, host_buf,
							dest_addr, copy_bytes,
							mem_type);
			} else {
				/* Write to External memory */
				status =
				    write_ext_dsp_data(dev_ctxt, host_buf,
						       dest_addr, copy_bytes,
						       mem_type, false);
			}
		}
		total_bytes -= copy_bytes;
		src_addr += copy_bytes;
		dest_addr += copy_bytes;
	}
	return status;
}

/* Mem Write does not halt the DSP to write unlike bridge_brd_write */
static int bridge_brd_mem_write(struct bridge_dev_context *dev_ctxt,
				    u8 *host_buff, u32 dsp_addr,
				    u32 ul_num_bytes, u32 mem_type)
{
	int status = 0;
	struct bridge_dev_context *dev_context = dev_ctxt;
	u32 ul_remain_bytes = 0;
	u32 ul_bytes = 0;

	ul_remain_bytes = ul_num_bytes;
	while (ul_remain_bytes > 0 && !status) {
		ul_bytes =
		    ul_remain_bytes > BUFFERSIZE ? BUFFERSIZE : ul_remain_bytes;
		if (dsp_addr < (dev_context->dsp_start_add +
				 dev_context->internal_size)) {
			status =
			    write_dsp_data(dev_ctxt, host_buff, dsp_addr,
					   ul_bytes, mem_type);
		} else {
			status = write_ext_dsp_data(dev_ctxt, host_buff,
						    dsp_addr, ul_bytes,
						    mem_type, true);
		}
		ul_remain_bytes -= ul_bytes;
		dsp_addr += ul_bytes;
		host_buff = host_buff + ul_bytes;
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
static int bridge_brd_mem_map(struct bridge_dev_context *dev_ctxt, u32 uva,
			      u32 da, u32 size, u32 attr, struct page **usr_pgs)
{
	struct sg_table *sgt;
	struct scatterlist *sg;
	int res, w;
	unsigned pages, i;
	struct vm_area_struct *vma;
	struct mm_struct *mm = current->mm;

	if (!size || !usr_pgs)
		return -EINVAL;

	pages = size / PG_SIZE4K;

	down_read(&mm->mmap_sem);

	vma = find_vma(mm, uva);
	if (vma)
		dev_dbg(bridge,
			"VMAfor UserBuf: ul_mpu_addr=%x, ul_num_bytes=%x, "
			"vm_start=%lx, vm_end=%lx, vm_flags=%lx\n", uva,
			size, vma->vm_start, vma->vm_end,
			vma->vm_flags);

	/*
	 * It is observed that under some circumstances, the user buffer is
	 * spread across several VMAs. So loop through and check if the entire
	 * user buffer is covered
	 */
	while ((vma) && (uva + size > vma->vm_end)) {
		/* jump to the next VMA region */
		vma = find_vma(mm, vma->vm_end + 1);
		dev_dbg(bridge,
			"VMA for UserBuf ul_mpu_addr=%x ul_num_bytes=%x, "
			"vm_start=%lx, vm_end=%lx, vm_flags=%lx\n", uva,
			size, vma->vm_start, vma->vm_end,
			vma->vm_flags);
	}
	if (!vma) {
		pr_err("%s: Failed to get VMA region for 0x%x (%d)\n",
		       __func__, uva, size);
		up_read(&mm->mmap_sem);
		return -EINVAL;
	}

	if (vma->vm_flags & (VM_WRITE | VM_MAYWRITE))
		w = 1;

	if (vma->vm_flags & VM_IO)
		i = get_io_pages(mm, uva, pages, usr_pgs);
	else
		i = get_user_pages(current, mm, uva, pages, w, 1, usr_pgs,
				   NULL);

	up_read(&mm->mmap_sem);

	if (i < 0) {
		dev_err(bridge, "No pages found: %d\n", i);
		return i;
	}

	if (i < pages) {
		res = -EFAULT;
		goto err_pages;
	}

	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt) {
		res = -ENOMEM;
		goto err_pages;
	}

	res = sg_alloc_table(sgt, pages, GFP_KERNEL);
	if (res < 0)
		goto err_sg;

	for_each_sg(sgt->sgl, sg, sgt->nents, i)
		sg_set_page(sg, usr_pgs[i], PAGE_SIZE, 0);

	res = iommu_map_sg(iommu_get_domain_for_dev(bridge), da, sgt->sgl,
			   sgt->nents, IOMMU_READ | IOMMU_WRITE);
	if (res)
		return 0;

	res = -EFAULT;
	dev_err(bridge, "iommu_map_sg failed\n");

	sg_free_table(sgt);
err_sg:
	kfree(sgt);
	i = pages;
err_pages:
	while (i--)
		put_page(usr_pgs[i]);

	return res;
}

/*
 *  ======== bridge_brd_mem_un_map ========
 *      Invalidate the PTEs for the DSP VA block to be unmapped.
 *
 *      PTEs of a mapped memory block are contiguous in any page table
 *      So, instead of looking up the PTE address for every 4K block,
 *      we clear consecutive PTEs until we unmap all the bytes
 */
static int bridge_brd_mem_un_map(struct bridge_dev_context *dev_ctxt,
				     u32 da, u32 size)
{
	return iommu_unmap(iommu_get_domain_for_dev(bridge), da, size);
}

static int get_io_pages(struct mm_struct *mm, u32 uva, unsigned pages,
			struct page **usr_pgs)
{
	u32 pa;
	int i;
	struct page *pg;

	for (i = 0; i < pages; i++) {
		pa = user_va2_pa(mm, uva);

		if (!pfn_valid(__phys_to_pfn(pa)))
			break;

		pg = PHYS_TO_PAGE(pa);
		usr_pgs[i] = pg;
		get_page(pg);
	}

	return i;
}
/*
 *  ======== user_va2_pa ========
 *  Purpose:
 *      This function walks through the page tables to convert a userland
 *      virtual address to physical address
 */
static u32 user_va2_pa(struct mm_struct *mm, u32 address)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *ptep, pte;

	pgd = pgd_offset(mm, address);
	if (pgd_none(*pgd) || pgd_bad(*pgd))
		return 0;

	pud = pud_offset(pgd, address);
	if (pud_none(*pud) || pud_bad(*pud))
		return 0;

	pmd = pmd_offset(pud, address);
	if (pmd_none(*pmd) || pmd_bad(*pmd))
		return 0;

	ptep = pte_offset_map(pmd, address);
	if (ptep) {
		pte = *ptep;
		if (pte_present(pte))
			return pte & PAGE_MASK;
	}

	return 0;
}

/*
 *  ======== wait_for_start ========
 *      Wait for the singal from DSP that it has started, or time out.
 */
bool wait_for_start(struct bridge_dev_context *dev_context,
			void __iomem *sync_addr)
{
	u16 timeout = TIHELEN_ACKTIMEOUT;

	/*  Wait for response from board */
	while (__raw_readw(sync_addr) && --timeout)
		udelay(10);

	/*  If timed out: return false */
	if (!timeout) {
		pr_err("%s: Timed out waiting DSP to Start\n", __func__);
		return false;
	}
	return true;
}
