/*
 * ue_deh.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Implements upper edge DSP exception handling (DEH) functions.
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

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>
#include <dspbridge/dbdefs.h>
#include <dspbridge/errbase.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/cfg.h>
#include <dspbridge/mem.h>
#include <dspbridge/ntfy.h>
#include <dspbridge/drv.h>

/*  ----------------------------------- Link Driver */
#include <dspbridge/wmddeh.h>

/*  ----------------------------------- Platform Manager */
#include <dspbridge/dev.h>
#include <dspbridge/wcd.h>
#include <dspbridge/wdt.h>

/* ------------------------------------ Hardware Abstraction Layer */
#include <hw_defs.h>
#include <hw_mmu.h>

/*  ----------------------------------- This */
#include "mmu_fault.h"
#include "_tiomap.h"
#include "_deh.h"
#include "_tiomap_mmu.h"
#include "_tiomap_pwr.h"
#include <dspbridge/io_sm.h>

static struct hw_mmu_map_attrs_t map_attrs = { HW_LITTLE_ENDIAN,
	HW_ELEM_SIZE16BIT,
	HW_MMU_CPUES
};

#define VIRT_TO_PHYS(x)       ((x) - PAGE_OFFSET + PHYS_OFFSET)

/*
 *  ======== bridge_deh_create ========
 *      Creates DEH manager object.
 */
dsp_status bridge_deh_create(OUT struct deh_mgr **phDehMgr,
			     struct dev_object *hdev_obj)
{
	dsp_status status = DSP_SOK;
	struct deh_mgr *deh_mgr_obj = NULL;
	struct cfg_hostres cfg_host_res;
	struct cfg_devnode *dev_node_obj;
	struct wmd_dev_context *hwmd_context = NULL;

	/*  Message manager will be created when a file is loaded, since
	 *  size of message buffer in shared memory is configurable in
	 *  the base image. */
	/* Get WMD context info. */
	dev_get_wmd_context(hdev_obj, &hwmd_context);
	DBC_ASSERT(hwmd_context);
	/* Allocate IO manager object: */
	MEM_ALLOC_OBJECT(deh_mgr_obj, struct deh_mgr, SIGNATURE);
	if (deh_mgr_obj == NULL) {
		status = DSP_EMEMORY;
	} else {
		/* Create an NTFY object to manage notifications */
		status = ntfy_create(&deh_mgr_obj->ntfy_obj);

		/* Create a MMUfault DPC */
		tasklet_init(&deh_mgr_obj->dpc_tasklet, mmu_fault_dpc,
			     (u32) deh_mgr_obj);

		if (DSP_SUCCEEDED(status))
			status = dev_get_dev_node(hdev_obj, &dev_node_obj);

		if (DSP_SUCCEEDED(status))
			status =
			    cfg_get_host_resources(dev_node_obj, &cfg_host_res);

		if (DSP_SUCCEEDED(status)) {
			/* Fill in context structure */
			deh_mgr_obj->hwmd_context = hwmd_context;
			deh_mgr_obj->err_info.dw_err_mask = 0L;
			deh_mgr_obj->err_info.dw_val1 = 0L;
			deh_mgr_obj->err_info.dw_val2 = 0L;
			deh_mgr_obj->err_info.dw_val3 = 0L;
			/* Install ISR function for DSP MMU fault */
			if ((request_irq(INT_DSP_MMU_IRQ, mmu_fault_isr, 0,
					 "DspBridge\tiommu fault",
					 (void *)deh_mgr_obj)) == 0)
				status = DSP_SOK;
			else
				status = DSP_EFAIL;
		}
	}
	if (DSP_FAILED(status)) {
		/* If create failed, cleanup */
		bridge_deh_destroy((struct deh_mgr *)deh_mgr_obj);
		*phDehMgr = NULL;
	} else {
		*phDehMgr = (struct deh_mgr *)deh_mgr_obj;
	}

	return status;
}

/*
 *  ======== bridge_deh_destroy ========
 *      Destroys DEH manager object.
 */
dsp_status bridge_deh_destroy(struct deh_mgr *hdeh_mgr)
{
	dsp_status status = DSP_SOK;
	struct deh_mgr *deh_mgr_obj = (struct deh_mgr *)hdeh_mgr;

	if (MEM_IS_VALID_HANDLE(deh_mgr_obj, SIGNATURE)) {
		/* Release dummy VA buffer */
		bridge_deh_release_dummy_mem();
		/* If notification object exists, delete it */
		if (deh_mgr_obj->ntfy_obj)
			(void)ntfy_delete(deh_mgr_obj->ntfy_obj);
		/* Disable DSP MMU fault */
		free_irq(INT_DSP_MMU_IRQ, deh_mgr_obj);

		/* Free DPC object */
		tasklet_kill(&deh_mgr_obj->dpc_tasklet);

		/* Deallocate the DEH manager object */
		MEM_FREE_OBJECT(deh_mgr_obj);
	}

	return status;
}

/*
 *  ======== bridge_deh_register_notify ========
 *      Registers for DEH notifications.
 */
dsp_status bridge_deh_register_notify(struct deh_mgr *hdeh_mgr, u32 event_mask,
				   u32 notify_type,
				   struct dsp_notification *hnotification)
{
	dsp_status status = DSP_SOK;
	struct deh_mgr *deh_mgr_obj = (struct deh_mgr *)hdeh_mgr;

	if (MEM_IS_VALID_HANDLE(deh_mgr_obj, SIGNATURE)) {
		status = ntfy_register(deh_mgr_obj->ntfy_obj, hnotification,
				       event_mask, notify_type);
	}

	return status;
}

/*
 *  ======== bridge_deh_notify ========
 *      DEH error notification function. Informs user about the error.
 */
void bridge_deh_notify(struct deh_mgr *hdeh_mgr, u32 ulEventMask, u32 dwErrInfo)
{
	struct deh_mgr *deh_mgr_obj = (struct deh_mgr *)hdeh_mgr;
	struct wmd_dev_context *dev_context;
	dsp_status status = DSP_SOK;
	u32 hw_mmu_max_tlb_count = 31;
	extern u32 fault_addr;
	struct cfg_hostres resources;
	hw_status hw_status_obj;

	status = cfg_get_host_resources((struct cfg_devnode *)
					drv_get_first_dev_extension(),
					&resources);

	if (MEM_IS_VALID_HANDLE(deh_mgr_obj, SIGNATURE)) {
		printk(KERN_INFO
		       "bridge_deh_notify: ********** DEVICE EXCEPTION "
		       "**********\n");
		dev_context =
		    (struct wmd_dev_context *)deh_mgr_obj->hwmd_context;

		switch (ulEventMask) {
		case DSP_SYSERROR:
			/* reset err_info structure before use */
			deh_mgr_obj->err_info.dw_err_mask = DSP_SYSERROR;
			deh_mgr_obj->err_info.dw_val1 = 0L;
			deh_mgr_obj->err_info.dw_val2 = 0L;
			deh_mgr_obj->err_info.dw_val3 = 0L;
			deh_mgr_obj->err_info.dw_val1 = dwErrInfo;
			printk(KERN_ERR
			       "bridge_deh_notify: DSP_SYSERROR, err_info "
			       "= 0x%x\n", dwErrInfo);
			break;
		case DSP_MMUFAULT:
			/* MMU fault routine should have set err info
			 * structure */
			deh_mgr_obj->err_info.dw_err_mask = DSP_MMUFAULT;
			printk(KERN_INFO "bridge_deh_notify: DSP_MMUFAULT,"
			       "err_info = 0x%x\n", dwErrInfo);
			printk(KERN_INFO
			       "bridge_deh_notify: DSP_MMUFAULT, High "
			       "Address = 0x%x\n",
			       (unsigned int)deh_mgr_obj->err_info.dw_val1);
			printk(KERN_INFO "bridge_deh_notify: DSP_MMUFAULT, Low "
			       "Address = 0x%x\n",
			       (unsigned int)deh_mgr_obj->err_info.dw_val2);
			printk(KERN_INFO
			       "bridge_deh_notify: DSP_MMUFAULT, fault "
			       "address = 0x%x\n", (unsigned int)fault_addr);
			dev_context = (struct wmd_dev_context *)
			    deh_mgr_obj->hwmd_context;
			/* Reset the dynamic mmu index to fixed count if it
			 * exceeds 31. So that the dynmmuindex is always
			 * between the range of standard/fixed entries
			 * and 31. */
			if (dev_context->num_tlb_entries >
			    hw_mmu_max_tlb_count) {
				dev_context->num_tlb_entries =
				    dev_context->fixed_tlb_entries;
			}
			if (DSP_SUCCEEDED(status)) {
				hw_status_obj =
				    hw_mmu_tlb_add(resources.dw_dmmu_base,
						   0, fault_addr,
						   HW_PAGE_SIZE4KB, 1,
						   &map_attrs, HW_SET, HW_SET);
			}
			/* send an interrupt to DSP */
			omap_mbox_msg_send(dev_context->mbox,
						MBX_DEH_CLASS | MBX_DEH_EMMU);
			/* Clear MMU interrupt */
			hw_mmu_event_ack(resources.dw_dmmu_base,
					 HW_MMU_TRANSLATION_FAULT);
			break;
#ifdef CONFIG_BRIDGE_NTFY_PWRERR
		case DSP_PWRERROR:
			/* reset err_info structure before use */
			deh_mgr_obj->err_info.dw_err_mask = DSP_PWRERROR;
			deh_mgr_obj->err_info.dw_val1 = 0L;
			deh_mgr_obj->err_info.dw_val2 = 0L;
			deh_mgr_obj->err_info.dw_val3 = 0L;
			deh_mgr_obj->err_info.dw_val1 = dwErrInfo;
			printk(KERN_ERR
			       "bridge_deh_notify: DSP_PWRERROR, err_info "
			       "= 0x%x\n", dwErrInfo);
			break;
#endif /* CONFIG_BRIDGE_NTFY_PWRERR */
		case DSP_WDTOVERFLOW:
			deh_mgr_obj->err_info.dw_err_mask = DSP_WDTOVERFLOW;
			deh_mgr_obj->err_info.dw_val1 = 0L;
			deh_mgr_obj->err_info.dw_val2 = 0L;
			deh_mgr_obj->err_info.dw_val3 = 0L;
			dev_err(bridge, "%s: DSP_WDTOVERFLOW\n", __func__);
			break;
		default:
			dev_dbg(bridge, "%s: Unknown Error, err_info = 0x%x\n",
				__func__, dwErrInfo);
			break;
		}

		/* Filter subsequent notifications when an error occurs */
		if (dev_context->dw_brd_state != BRD_ERROR) {
			ntfy_notify(deh_mgr_obj->ntfy_obj, ulEventMask);
#ifdef CONFIG_BRIDGE_RECOVERY
			bridge_recover_schedule();
#endif
		}

		/* Set the Board state as ERROR */
		dev_context->dw_brd_state = BRD_ERROR;
		/* Disable all the clocks that were enabled by DSP */
		(void)dsp_peripheral_clocks_disable(dev_context, NULL);
		/* Call DSP Trace Buffer */
		print_dsp_trace_buffer(hdeh_mgr->hwmd_context);
		/*
		* Avoid the subsequent WDT if it happens once,
		* also if fatal error occurs.
		*/
		dsp_wdt_enable(false);
	}
}

/*
 *  ======== bridge_deh_get_info ========
 *      Retrieves error information.
 */
dsp_status bridge_deh_get_info(struct deh_mgr *hdeh_mgr,
			    struct dsp_errorinfo *pErrInfo)
{
	dsp_status status = DSP_SOK;
	struct deh_mgr *deh_mgr_obj = (struct deh_mgr *)hdeh_mgr;

	DBC_REQUIRE(deh_mgr_obj);
	DBC_REQUIRE(pErrInfo);

	if (MEM_IS_VALID_HANDLE(deh_mgr_obj, SIGNATURE)) {
		/* Copy DEH error info structure to PROC error info
		 * structure. */
		pErrInfo->dw_err_mask = deh_mgr_obj->err_info.dw_err_mask;
		pErrInfo->dw_val1 = deh_mgr_obj->err_info.dw_val1;
		pErrInfo->dw_val2 = deh_mgr_obj->err_info.dw_val2;
		pErrInfo->dw_val3 = deh_mgr_obj->err_info.dw_val3;
	} else {
		status = DSP_EHANDLE;
	}

	return status;
}

/*
 *  ======== bridge_deh_release_dummy_mem ========
 *      Releases memory allocated for dummy page
 */
void bridge_deh_release_dummy_mem(void)
{
}
