/*
 * proc.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Processor interface at the driver level.
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

/* ------------------------------------ Host OS */
#include <dspbridge/host_os.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>
#include <dspbridge/dbdefs.h>
#include <dspbridge/errbase.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/cfg.h>
#include <dspbridge/list.h>
#include <dspbridge/mem.h>
#include <dspbridge/ntfy.h>
#include <dspbridge/sync.h>
/*  ----------------------------------- Mini Driver */
#include <dspbridge/wmd.h>
#include <dspbridge/wmddeh.h>
/*  ----------------------------------- Platform Manager */
#include <dspbridge/cod.h>
#include <dspbridge/dev.h>
#include <dspbridge/procpriv.h>
#include <dspbridge/dmm.h>

/*  ----------------------------------- Resource Manager */
#include <dspbridge/mgr.h>
#include <dspbridge/node.h>
#include <dspbridge/nldr.h>
#include <dspbridge/rmm.h>

/*  ----------------------------------- Others */
#include <dspbridge/dbdcd.h>
#include <dspbridge/msg.h>
#include <dspbridge/wmdioctl.h>
#include <dspbridge/drv.h>
#include <dspbridge/reg.h>

/*  ----------------------------------- This */
#include <dspbridge/proc.h>
#include <dspbridge/pwr.h>

#include <dspbridge/resourcecleanup.h>
/*  ----------------------------------- Defines, Data Structures, Typedefs */
#define PROC_SIGNATURE	   0x434F5250	/* "PROC" (in reverse). */
#define MAXCMDLINELEN       255
#define PROC_ENVPROCID      "PROC_ID=%d"
#define MAXPROCIDLEN	(8 + 5)
#define PROC_DFLT_TIMEOUT   10000	/* Time out in milliseconds */
#define PWR_TIMEOUT	 500	/* Sleep/wake timout in msec */
#define EXTEND	      "_EXT_END"	/* Extmem end addr in DSP binary */

#define DSP_CACHE_LINE 128

#define BUFMODE_MASK	(3 << 14)

/* Buffer modes from DSP perspective */
#define RBUF		0x4000		/* Input buffer */
#define WBUF		0x8000		/* Output Buffer */

extern char *iva_img;

/*  ----------------------------------- Globals */

/* The proc_object structure. */
struct proc_object {
	struct list_head link;	/* Link to next proc_object */
	u32 dw_signature;	/* Used for object validation */
	struct dev_object *hdev_obj;	/* Device this PROC represents */
	u32 process;		/* Process owning this Processor */
	struct mgr_object *hmgr_obj;	/* Manager Object Handle */
	u32 attach_count;	/* Processor attach count */
	u32 processor_id;	/* Processor number */
	u32 utimeout;		/* Time out count */
	enum dsp_procstate proc_state;	/* Processor state */
	u32 ul_unit;		/* DDSP unit number */
	bool is_already_attached;	/*
					 * True if the Device below has
					 * GPP Client attached
					 */
	struct ntfy_object *ntfy_obj;	/* Manages  notifications */
	struct wmd_dev_context *hwmd_context;	/* WMD Context Handle */
	struct bridge_drv_interface *intf_fxns;	/* Function interface to WMD */
	char *psz_last_coff;
	struct list_head proc_list;
};

static u32 refs;

struct sync_csobject *proc_lock;	/* For critical sections */

/*  ----------------------------------- Function Prototypes */
static dsp_status proc_monitor(struct proc_object *hprocessor);
static s32 get_envp_count(char **envp);
static char **prepend_envp(char **new_envp, char **envp, s32 envp_elems,
			   s32 cnew_envp, char *szVar);

/*
 *  ======== proc_attach ========
 *  Purpose:
 *      Prepare for communication with a particular DSP processor, and return
 *      a handle to the processor object.
 */
dsp_status
proc_attach(u32 processor_id,
	    OPTIONAL CONST struct dsp_processorattrin *attr_in,
	    void **ph_processor, struct process_context *pr_ctxt)
{
	dsp_status status = DSP_SOK;
	struct dev_object *hdev_obj;
	struct proc_object *p_proc_object = NULL;
	struct mgr_object *hmgr_obj = NULL;
	struct drv_object *hdrv_obj = NULL;
	u32 dev_type;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(ph_processor != NULL);

	if (pr_ctxt->hprocessor) {
		*ph_processor = pr_ctxt->hprocessor;
		return status;
	}

	/* Get the Driver and Manager Object Handles */
	status = cfg_get_object((u32 *) &hdrv_obj, REG_DRV_OBJECT);
	if (DSP_SUCCEEDED(status))
		status = cfg_get_object((u32 *) &hmgr_obj, REG_MGR_OBJECT);

	if (DSP_SUCCEEDED(status)) {
		/* Get the Device Object */
		status = drv_get_dev_object(processor_id, hdrv_obj, &hdev_obj);
	}
	if (DSP_SUCCEEDED(status))
		status = dev_get_dev_type(hdev_obj, &dev_type);

	if (DSP_FAILED(status))
		goto func_end;

	/* If we made it this far, create the Proceesor object: */
	MEM_ALLOC_OBJECT(p_proc_object, struct proc_object, PROC_SIGNATURE);
	/* Fill out the Processor Object: */
	if (p_proc_object == NULL) {
		status = DSP_EMEMORY;
		goto func_end;
	}
	p_proc_object->hdev_obj = hdev_obj;
	p_proc_object->hmgr_obj = hmgr_obj;
	p_proc_object->processor_id = dev_type;
	/* Store TGID instead of process handle */
	p_proc_object->process = current->tgid;

	INIT_LIST_HEAD(&p_proc_object->proc_list);

	if (attr_in)
		p_proc_object->utimeout = attr_in->utimeout;
	else
		p_proc_object->utimeout = PROC_DFLT_TIMEOUT;

	status = dev_get_intf_fxns(hdev_obj, &p_proc_object->intf_fxns);
	if (DSP_SUCCEEDED(status)) {
		status = dev_get_wmd_context(hdev_obj,
					     &p_proc_object->hwmd_context);
		if (DSP_FAILED(status))
			MEM_FREE_OBJECT(p_proc_object);
	} else
		MEM_FREE_OBJECT(p_proc_object);

	if (DSP_FAILED(status))
		goto func_end;

	/* Create the Notification Object */
	/* This is created with no event mask, no notify mask
	 * and no valid handle to the notification. They all get
	 * filled up when proc_register_notify is called */
	status = ntfy_create(&p_proc_object->ntfy_obj);
	if (DSP_SUCCEEDED(status)) {
		/* Insert the Processor Object into the DEV List.
		 * Return handle to this Processor Object:
		 * Find out if the Device is already attached to a
		 * Processor. If so, return AlreadyAttached status */
		lst_init_elem(&p_proc_object->link);
		status = dev_insert_proc_object(p_proc_object->hdev_obj,
						(u32) p_proc_object,
						&p_proc_object->
						is_already_attached);
		if (DSP_SUCCEEDED(status)) {
			if (p_proc_object->is_already_attached)
				status = DSP_SALREADYATTACHED;
		} else {
			if (p_proc_object->ntfy_obj)
				ntfy_delete(p_proc_object->ntfy_obj);

			MEM_FREE_OBJECT(p_proc_object);
		}
		if (DSP_SUCCEEDED(status)) {
			*ph_processor = (void *)p_proc_object;
			pr_ctxt->hprocessor = *ph_processor;
			(void)proc_notify_clients(p_proc_object,
						  DSP_PROCESSORATTACH);
		}
	} else {
		/* Don't leak memory if DSP_FAILED */
		MEM_FREE_OBJECT(p_proc_object);
	}
func_end:
	DBC_ENSURE((status == DSP_EFAIL && *ph_processor == NULL) ||
		   (DSP_SUCCEEDED(status) &&
		    MEM_IS_VALID_HANDLE(p_proc_object, PROC_SIGNATURE)) ||
		   (status == DSP_SALREADYATTACHED &&
		    MEM_IS_VALID_HANDLE(p_proc_object, PROC_SIGNATURE)));

	return status;
}

static dsp_status get_exec_file(struct cfg_devnode *dev_node_obj,
				struct dev_object *hdev_obj,
				u32 size, char *execFile)
{
	s32 dev_type;
	s32 len;

	dev_get_dev_type(hdev_obj, (u32 *) &dev_type);
	if (dev_type == DSP_UNIT) {
		return cfg_get_exec_file(dev_node_obj, size, execFile);
	} else if (dev_type == IVA_UNIT) {
		if (iva_img) {
			len = strlen(iva_img);
			strncpy(execFile, iva_img, len + 1);
			return DSP_SOK;
		}
	}
	return DSP_EFILE;
}

/*
 *  ======== proc_auto_start ======== =
 *  Purpose:
 *      A Particular device gets loaded with the default image
 *      if the AutoStart flag is set.
 *  Parameters:
 *      hdev_obj:     Handle to the Device
 *  Returns:
 *      DSP_SOK:   On Successful Loading
 *      DSP_EFAIL  General Failure
 *  Requires:
 *      hdev_obj != NULL
 *  Ensures:
 */
dsp_status proc_auto_start(struct cfg_devnode *dev_node_obj,
			   struct dev_object *hdev_obj)
{
	dsp_status status = DSP_EFAIL;
	struct proc_object *p_proc_object;
	char sz_exec_file[MAXCMDLINELEN];
	char *argv[2];
	struct mgr_object *hmgr_obj = NULL;
	s32 dev_type;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(dev_node_obj != NULL);
	DBC_REQUIRE(hdev_obj != NULL);

	/* Create a Dummy PROC Object */
	status = cfg_get_object((u32 *) &hmgr_obj, REG_MGR_OBJECT);
	if (DSP_FAILED(status))
		goto func_end;

	MEM_ALLOC_OBJECT(p_proc_object, struct proc_object, PROC_SIGNATURE);
	if (p_proc_object == NULL) {
		status = DSP_EMEMORY;
		goto func_end;
	}
	p_proc_object->hdev_obj = hdev_obj;
	p_proc_object->hmgr_obj = hmgr_obj;
	status = dev_get_intf_fxns(hdev_obj, &p_proc_object->intf_fxns);
	if (DSP_SUCCEEDED(status))
		status = dev_get_wmd_context(hdev_obj,
					     &p_proc_object->hwmd_context);
	if (DSP_FAILED(status))
		goto func_cont;

	/* Stop the Device, put it into standby mode */
	status = proc_stop(p_proc_object);

	if (DSP_FAILED(status))
		goto func_cont;

	/* Get the default executable for this board... */
	dev_get_dev_type(hdev_obj, (u32 *) &dev_type);
	p_proc_object->processor_id = dev_type;
	status = get_exec_file(dev_node_obj, hdev_obj, sizeof(sz_exec_file),
			       sz_exec_file);
	if (DSP_SUCCEEDED(status)) {
		argv[0] = sz_exec_file;
		argv[1] = NULL;
		/* ...and try to load it: */
		status = proc_load(p_proc_object, 1, (CONST char **)argv, NULL);
		if (DSP_SUCCEEDED(status))
			status = proc_start(p_proc_object);
	}
	kfree(p_proc_object->psz_last_coff);
	p_proc_object->psz_last_coff = NULL;
func_cont:
	MEM_FREE_OBJECT(p_proc_object);
func_end:
	return status;
}

/*
 *  ======== proc_ctrl ========
 *  Purpose:
 *      Pass control information to the GPP device driver managing the
 *      DSP processor.
 *
 *      This will be an OEM-only function, and not part of the DSP/BIOS Bridge
 *      application developer's API.
 *      Call the WMD_ICOTL fxn with the Argument This is a Synchronous
 *      Operation. arg can be null.
 */
dsp_status proc_ctrl(void *hprocessor, u32 dw_cmd, IN struct dsp_cbdata * arg)
{
	dsp_status status = DSP_SOK;
	struct proc_object *p_proc_object = hprocessor;
	u32 timeout = 0;

	DBC_REQUIRE(refs > 0);

	if (MEM_IS_VALID_HANDLE(p_proc_object, PROC_SIGNATURE)) {
		/* intercept PWR deep sleep command */
		if (dw_cmd == WMDIOCTL_DEEPSLEEP) {
			timeout = arg->cb_data;
			status = pwr_sleep_dsp(PWR_DEEPSLEEP, timeout);
		}
		/* intercept PWR emergency sleep command */
		else if (dw_cmd == WMDIOCTL_EMERGENCYSLEEP) {
			timeout = arg->cb_data;
			status = pwr_sleep_dsp(PWR_EMERGENCYDEEPSLEEP, timeout);
		} else if (dw_cmd == PWR_DEEPSLEEP) {
			/* timeout = arg->cb_data; */
			status = pwr_sleep_dsp(PWR_DEEPSLEEP, timeout);
		}
		/* intercept PWR wake commands */
		else if (dw_cmd == WMDIOCTL_WAKEUP) {
			timeout = arg->cb_data;
			status = pwr_wake_dsp(timeout);
		} else if (dw_cmd == PWR_WAKEUP) {
			/* timeout = arg->cb_data; */
			status = pwr_wake_dsp(timeout);
		} else
		    if (DSP_SUCCEEDED((*p_proc_object->intf_fxns->pfn_dev_cntrl)
				      (p_proc_object->hwmd_context, dw_cmd,
				       arg))) {
			status = DSP_SOK;
		} else {
			status = DSP_EFAIL;
		}
	} else {
		status = DSP_EHANDLE;
	}

	return status;
}

/*
 *  ======== proc_detach ========
 *  Purpose:
 *      Destroys the  Processor Object. Removes the notification from the Dev
 *      List.
 */
dsp_status proc_detach(struct process_context *pr_ctxt)
{
	dsp_status status = DSP_SOK;
	struct proc_object *p_proc_object = NULL;

	DBC_REQUIRE(refs > 0);

	p_proc_object = (struct proc_object *)pr_ctxt->hprocessor;

	if (MEM_IS_VALID_HANDLE(p_proc_object, PROC_SIGNATURE)) {
		/* Notify the Client */
		ntfy_notify(p_proc_object->ntfy_obj, DSP_PROCESSORDETACH);
		/* Remove the notification memory */
		if (p_proc_object->ntfy_obj)
			ntfy_delete(p_proc_object->ntfy_obj);

		kfree(p_proc_object->psz_last_coff);
		p_proc_object->psz_last_coff = NULL;
		/* Remove the Proc from the DEV List */
		(void)dev_remove_proc_object(p_proc_object->hdev_obj,
					     (u32) p_proc_object);
		/* Free the Processor Object */
		MEM_FREE_OBJECT(p_proc_object);
		pr_ctxt->hprocessor = NULL;
	} else {
		status = DSP_EHANDLE;
	}

	return status;
}

/*
 *  ======== proc_enum_nodes ========
 *  Purpose:
 *      Enumerate and get configuration information about nodes allocated
 *      on a DSP processor.
 */
dsp_status proc_enum_nodes(void *hprocessor, void **node_tab,
			   IN u32 node_tab_size, OUT u32 *pu_num_nodes,
			   OUT u32 *pu_allocated)
{
	dsp_status status = DSP_EFAIL;
	struct proc_object *p_proc_object = (struct proc_object *)hprocessor;
	struct node_mgr *hnode_mgr = NULL;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(node_tab != NULL || node_tab_size == 0);
	DBC_REQUIRE(pu_num_nodes != NULL);
	DBC_REQUIRE(pu_allocated != NULL);

	if (MEM_IS_VALID_HANDLE(p_proc_object, PROC_SIGNATURE)) {
		if (DSP_SUCCEEDED(dev_get_node_manager(p_proc_object->hdev_obj,
						       &hnode_mgr))) {
			if (hnode_mgr) {
				status = node_enum_nodes(hnode_mgr, node_tab,
							 node_tab_size,
							 pu_num_nodes,
							 pu_allocated);
			}
		}
	} else {
		status = DSP_EHANDLE;
	}

	return status;
}

/* Cache operation against kernel address instead of users */
static int memory_sync_page(struct vm_area_struct *vma, unsigned long start,
			    ssize_t len, enum dsp_flushtype ftype)
{
	struct page *page;
	void *kaddr;
	unsigned long offset;
	ssize_t rest;

	while (len) {
		page = follow_page(vma, start, FOLL_GET);
		if (!page) {
			pr_err("%s: no page for %08lx\n", __func__, start);
			return -EINVAL;
		} else if (IS_ERR(page)) {
			pr_err("%s: err page for %08lx(%lu)\n", __func__, start,
			       IS_ERR(page));
			return IS_ERR(page);
		}

		offset = start & ~PAGE_MASK;
		kaddr = kmap(page) + offset;
		rest = min_t(ssize_t, PAGE_SIZE - offset, len);
		mem_flush_cache(kaddr, rest, ftype);

		kunmap(page);
		put_page(page);
		len -= rest;
		start += rest;
	}

	return 0;
}

/* Check if the given area blongs to process virtul memory address space */
static int memory_sync_vma(unsigned long start, u32 len,
			   enum dsp_flushtype ftype)
{
	int err = 0;
	unsigned long end;
	struct vm_area_struct *vma;

	end = start + len;
	if (end <= start)
		return -EINVAL;

	while ((vma = find_vma(current->mm, start)) != NULL) {
		ssize_t size;

		if (vma->vm_flags & (VM_IO | VM_PFNMAP))
			return -EINVAL;

		if (vma->vm_start > start)
			return -EINVAL;

		size = min_t(ssize_t, vma->vm_end - start, len);
		err = memory_sync_page(vma, start, size, ftype);
		if (err)
			break;

		if (end <= vma->vm_end)
			break;

		start = vma->vm_end;
		len -= size;
	}

	if (!vma)
		err = -EINVAL;

	return err;
}

static dsp_status proc_memory_sync(void *hprocessor, void *pmpu_addr,
				   u32 ul_size, u32 ul_flags,
				   enum dsp_flushtype FlushMemType)
{
	/* Keep STATUS here for future additions to this function */
	dsp_status status = DSP_SOK;
	struct proc_object *p_proc_object = (struct proc_object *)hprocessor;

	DBC_REQUIRE(refs > 0);

	if (!MEM_IS_VALID_HANDLE(p_proc_object, PROC_SIGNATURE)) {
		status = DSP_EHANDLE;
		goto err_out;
	}

	down_read(&current->mm->mmap_sem);

	if (memory_sync_vma((u32) pmpu_addr, ul_size, FlushMemType)) {
		pr_err("%s: InValid address parameters %p %x\n",
		       __func__, pmpu_addr, ul_size);
		status = DSP_EHANDLE;
	}

	up_read(&current->mm->mmap_sem);

err_out:

	return status;
}

/*
 *  ======== proc_flush_memory ========
 *  Purpose:
 *     Flush cache
 */
dsp_status proc_flush_memory(void *hprocessor, void *pmpu_addr,
			     u32 ul_size, u32 ul_flags)
{
	enum dsp_flushtype mtype = PROC_WRITEBACK_INVALIDATE_MEM;

	return proc_memory_sync(hprocessor, pmpu_addr, ul_size, ul_flags,
				mtype);
}

/*
 *  ======== proc_invalidate_memory ========
 *  Purpose:
 *     Invalidates the memory specified
 */
dsp_status proc_invalidate_memory(void *hprocessor, void *pmpu_addr,
				  u32 ul_size)
{
	enum dsp_flushtype mtype = PROC_INVALIDATE_MEM;

	return proc_memory_sync(hprocessor, pmpu_addr, ul_size, 0, mtype);
}

/*
 *  ======== proc_get_resource_info ========
 *  Purpose:
 *      Enumerate the resources currently available on a processor.
 */
dsp_status proc_get_resource_info(void *hprocessor, u32 resource_type,
				  OUT struct dsp_resourceinfo *resource_info,
				  u32 resource_info_size)
{
	dsp_status status = DSP_EFAIL;
	struct proc_object *p_proc_object = (struct proc_object *)hprocessor;
	struct node_mgr *hnode_mgr = NULL;
	struct nldr_object *nldr_obj = NULL;
	struct rmm_target_obj *rmm = NULL;
	struct io_mgr *hio_mgr = NULL;	/* IO manager handle */

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(resource_info != NULL);
	DBC_REQUIRE(resource_info_size >= sizeof(struct dsp_resourceinfo));

	if (!MEM_IS_VALID_HANDLE(p_proc_object, PROC_SIGNATURE)) {
		status = DSP_EHANDLE;
		goto func_end;
	}
	switch (resource_type) {
	case DSP_RESOURCE_DYNDARAM:
	case DSP_RESOURCE_DYNSARAM:
	case DSP_RESOURCE_DYNEXTERNAL:
	case DSP_RESOURCE_DYNSRAM:
		status = dev_get_node_manager(p_proc_object->hdev_obj,
					      &hnode_mgr);
		if (DSP_FAILED(status))
			goto func_end;

		status = node_get_nldr_obj(hnode_mgr, &nldr_obj);
		if (DSP_SUCCEEDED(status)) {
			status = nldr_get_rmm_manager(nldr_obj, &rmm);
			if (DSP_SUCCEEDED(status)) {
				DBC_ASSERT(rmm != NULL);
				if (!rmm_stat(rmm,
					      (enum dsp_memtype)resource_type,
					      (struct dsp_memstat *)
					      &(resource_info->result.
						mem_stat)))
					status = DSP_EVALUE;
			}
		}
		break;
	case DSP_RESOURCE_PROCLOAD:
		status = dev_get_io_mgr(p_proc_object->hdev_obj, &hio_mgr);
		if (DSP_SUCCEEDED(status))
			status =
			    p_proc_object->intf_fxns->
			    pfn_io_get_proc_load(hio_mgr,
						 (struct dsp_procloadstat *)
						 &(resource_info->result.
						   proc_load_stat));
		break;
	default:
		status = DSP_EFAIL;
		break;
	}
func_end:
	return status;
}

/*
 *  ======== proc_exit ========
 *  Purpose:
 *      Decrement reference count, and free resources when reference count is
 *      0.
 */
void proc_exit(void)
{
	DBC_REQUIRE(refs > 0);

	if (proc_lock)
		(void)sync_delete_cs(proc_lock);

	refs--;

	DBC_ENSURE(refs >= 0);
}

/*
 *  ======== proc_get_dev_object ========
 *  Purpose:
 *      Return the Dev Object handle for a given Processor.
 *
 */
dsp_status proc_get_dev_object(void *hprocessor,
			       struct dev_object **phDevObject)
{
	dsp_status status = DSP_EFAIL;
	struct proc_object *p_proc_object = (struct proc_object *)hprocessor;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(phDevObject != NULL);

	if (MEM_IS_VALID_HANDLE(p_proc_object, PROC_SIGNATURE)) {
		*phDevObject = p_proc_object->hdev_obj;
		status = DSP_SOK;
	} else {
		*phDevObject = NULL;
		status = DSP_EHANDLE;
	}

	DBC_ENSURE((DSP_SUCCEEDED(status) && *phDevObject != NULL) ||
		   (DSP_FAILED(status) && *phDevObject == NULL));

	return status;
}

/*
 *  ======== proc_get_state ========
 *  Purpose:
 *      Report the state of the specified DSP processor.
 */
dsp_status proc_get_state(void *hprocessor,
			  OUT struct dsp_processorstate *proc_state_obj,
			  u32 state_info_size)
{
	dsp_status status = DSP_SOK;
	struct proc_object *p_proc_object = (struct proc_object *)hprocessor;
	int brd_status;
	struct deh_mgr *hdeh_mgr;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(proc_state_obj != NULL);
	DBC_REQUIRE(state_info_size >= sizeof(struct dsp_processorstate));

	if (MEM_IS_VALID_HANDLE(p_proc_object, PROC_SIGNATURE)) {
		/* First, retrieve BRD state information */
		status = (*p_proc_object->intf_fxns->pfn_brd_status)
		    (p_proc_object->hwmd_context, &brd_status);
		if (DSP_SUCCEEDED(status)) {
			switch (brd_status) {
			case BRD_STOPPED:
				proc_state_obj->proc_state = PROC_STOPPED;
				break;
			case BRD_SLEEP_TRANSITION:
			case BRD_DSP_HIBERNATION:
				/* Fall through */
			case BRD_RUNNING:
				proc_state_obj->proc_state = PROC_RUNNING;
				break;
			case BRD_LOADED:
				proc_state_obj->proc_state = PROC_LOADED;
				break;
			case BRD_ERROR:
				proc_state_obj->proc_state = PROC_ERROR;
				break;
			default:
				proc_state_obj->proc_state = 0xFF;
				status = DSP_EFAIL;
				break;
			}
		}
		/* Next, retrieve error information, if any */
		status = dev_get_deh_mgr(p_proc_object->hdev_obj, &hdeh_mgr);
		if (DSP_SUCCEEDED(status) && hdeh_mgr)
			status = (*p_proc_object->intf_fxns->pfn_deh_get_info)
			    (hdeh_mgr, &(proc_state_obj->err_info));
	} else {
		status = DSP_EHANDLE;
	}
	dev_dbg(bridge, "%s, results: status: 0x%x proc_state_obj: 0x%x\n",
		__func__, status, proc_state_obj->proc_state);
	return status;
}

/*
 *  ======== proc_get_trace ========
 *  Purpose:
 *      Retrieve the current contents of the trace buffer, located on the
 *      Processor.  Predefined symbols for the trace buffer must have been
 *      configured into the DSP executable.
 *  Details:
 *      We support using the symbols SYS_PUTCBEG and SYS_PUTCEND to define a
 *      trace buffer, only.  Treat it as an undocumented feature.
 *      This call is destructive, meaning the processor is placed in the monitor
 *      state as a result of this function.
 */
dsp_status proc_get_trace(void *hprocessor, u8 * pbuf, u32 max_size)
{
	dsp_status status;
	status = DSP_ENOTIMPL;
	return status;
}

/*
 *  ======== proc_init ========
 *  Purpose:
 *      Initialize PROC's private state, keeping a reference count on each call
 */
bool proc_init(void)
{
	bool ret = true;

	DBC_REQUIRE(refs >= 0);

	if (refs == 0)
		(void)sync_initialize_cs(&proc_lock);

	if (ret)
		refs++;

	DBC_ENSURE((ret && (refs > 0)) || (!ret && (refs >= 0)));

	return ret;
}

/*
 *  ======== proc_load ========
 *  Purpose:
 *      Reset a processor and load a new base program image.
 *      This will be an OEM-only function, and not part of the DSP/BIOS Bridge
 *      application developer's API.
 */
dsp_status proc_load(void *hprocessor, IN CONST s32 argc_index,
		     IN CONST char **user_args, IN CONST char **user_envp)
{
	dsp_status status = DSP_SOK;
	struct proc_object *p_proc_object = (struct proc_object *)hprocessor;
	struct io_mgr *hio_mgr;	/* IO manager handle */
	struct msg_mgr *hmsg_mgr;
	struct cod_manager *cod_mgr;	/* Code manager handle */
	char *pargv0;		/* temp argv[0] ptr */
	char **new_envp;	/* Updated envp[] array. */
	char sz_proc_id[MAXPROCIDLEN];	/* Size of "PROC_ID=<n>" */
	s32 envp_elems;		/* Num elements in envp[]. */
	s32 cnew_envp;		/* "  " in new_envp[] */
	s32 nproc_id = 0;	/* Anticipate MP version. */
	struct dcd_manager *hdcd_handle;
	struct dmm_object *dmm_mgr;
	u32 dw_ext_end;
	u32 proc_id;
	int brd_state;

#ifdef OPT_LOAD_TIME_INSTRUMENTATION
	struct timeval tv1;
	struct timeval tv2;
#endif

#if defined(CONFIG_BRIDGE_DVFS) && !defined(CONFIG_CPU_FREQ)
	struct dspbridge_platform_data *pdata =
	    omap_dspbridge_dev->dev.platform_data;
#endif

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(argc_index > 0);
	DBC_REQUIRE(user_args != NULL);

#ifdef OPT_LOAD_TIME_INSTRUMENTATION
	do_gettimeofday(&tv1);
#endif
	/* Call the WMD_BRD_Load fxn */
	if (!MEM_IS_VALID_HANDLE(p_proc_object, PROC_SIGNATURE)) {
		status = DSP_EHANDLE;
		goto func_end;
	}
	if (DSP_FAILED(dev_get_cod_mgr(p_proc_object->hdev_obj, &cod_mgr))) {
		status = DSP_EFAIL;
		goto func_end;
	}
	status = proc_stop(hprocessor);
	if (DSP_FAILED(status))
		goto func_end;

	/* Place the board in the monitor state. */
	status = proc_monitor(hprocessor);
	if (DSP_FAILED(status))
		goto func_end;

	/* Save ptr to  original argv[0]. */
	pargv0 = (char *)user_args[0];
	/*Prepend "PROC_ID=<nproc_id>"to envp array for target. */
	envp_elems = get_envp_count((char **)user_envp);
	cnew_envp = (envp_elems ? (envp_elems + 1) : (envp_elems + 2));
	new_envp = mem_calloc(cnew_envp * sizeof(char **), MEM_PAGED);
	if (new_envp) {
		status = snprintf(sz_proc_id, MAXPROCIDLEN, PROC_ENVPROCID,
				  nproc_id);
		if (status == -1) {
			dev_dbg(bridge, "%s: Proc ID string overflow\n",
				__func__);
			status = DSP_EFAIL;
		} else {
			new_envp =
			    prepend_envp(new_envp, (char **)user_envp,
					 envp_elems, cnew_envp, sz_proc_id);
			/* Get the DCD Handle */
			status = mgr_get_dcd_handle(p_proc_object->hmgr_obj,
						    (u32 *) &hdcd_handle);
			if (DSP_SUCCEEDED(status)) {
				/*  Before proceeding with new load,
				 *  check if a previously registered COFF
				 *  exists.
				 *  If yes, unregister nodes in previously
				 *  registered COFF.  If any error occurred,
				 *  set previously registered COFF to NULL. */
				if (p_proc_object->psz_last_coff != NULL) {
					status =
					    dcd_auto_unregister(hdcd_handle,
								p_proc_object->
								psz_last_coff);
					/* Regardless of auto unregister status,
					 *  free previously allocated
					 *  memory. */
					kfree(p_proc_object->psz_last_coff);
					p_proc_object->psz_last_coff = NULL;
				}
			}
			/* On success, do cod_open_base() */
			status = cod_open_base(cod_mgr, (char *)user_args[0],
					       COD_SYMB);
		}
	} else {
		status = DSP_EMEMORY;
	}
	if (DSP_SUCCEEDED(status)) {
		/* Auto-register data base */
		/* Get the DCD Handle */
		status = mgr_get_dcd_handle(p_proc_object->hmgr_obj,
					    (u32 *) &hdcd_handle);
		if (DSP_SUCCEEDED(status)) {
			/*  Auto register nodes in specified COFF
			 *  file.  If registration did not fail,
			 *  (status = DSP_SOK or DSP_EDCDNOAUTOREGISTER)
			 *  save the name of the COFF file for
			 *  de-registration in the future. */
			status =
			    dcd_auto_register(hdcd_handle,
					      (char *)user_args[0]);
			if (status == DSP_EDCDNOAUTOREGISTER)
				status = DSP_SOK;

			if (DSP_FAILED(status)) {
				status = DSP_EFAIL;
			} else {
				DBC_ASSERT(p_proc_object->psz_last_coff ==
					   NULL);
				/* Allocate memory for pszLastCoff */
				p_proc_object->psz_last_coff =
				    mem_calloc((strlen((char *)user_args[0]) +
						1), MEM_PAGED);
				/* If memory allocated, save COFF file name */
				if (p_proc_object->psz_last_coff) {
					strncpy(p_proc_object->psz_last_coff,
						(char *)user_args[0],
						(strlen((char *)user_args[0]) +
						 1));
				}
			}
		}
	}
	/* Update shared memory address and size */
	if (DSP_SUCCEEDED(status)) {
		/*  Create the message manager. This must be done
		 *  before calling the IOOnLoaded function. */
		dev_get_msg_mgr(p_proc_object->hdev_obj, &hmsg_mgr);
		if (!hmsg_mgr) {
			status = msg_create(&hmsg_mgr, p_proc_object->hdev_obj,
					    (msg_onexit) node_on_exit);
			DBC_ASSERT(DSP_SUCCEEDED(status));
			dev_set_msg_mgr(p_proc_object->hdev_obj, hmsg_mgr);
		}
	}
	if (DSP_SUCCEEDED(status)) {
		/* Set the Device object's message manager */
		status = dev_get_io_mgr(p_proc_object->hdev_obj, &hio_mgr);
		DBC_ASSERT(DSP_SUCCEEDED(status));
		status =
		    (*p_proc_object->intf_fxns->pfn_io_on_loaded) (hio_mgr);
	}
	if (DSP_SUCCEEDED(status)) {
		/* Now, attempt to load an exec: */

		/* Boost the OPP level to Maximum level supported by baseport */
#if defined(CONFIG_BRIDGE_DVFS) && !defined(CONFIG_CPU_FREQ)
		if (pdata->cpu_set_freq)
			(*pdata->cpu_set_freq) (pdata->mpu_max_speed);
#endif
		status = cod_load_base(cod_mgr, argc_index, (char **)user_args,
				       dev_brd_write_fxn,
				       p_proc_object->hdev_obj, NULL);
		if (DSP_FAILED(status)) {
			if (status == COD_E_OPENFAILED) {
				dev_dbg(bridge, "%s: Failure to Load the EXE\n",
					__func__);
			}
			if (status == COD_E_SYMBOLNOTFOUND) {
				pr_err("%s: Couldn't parse the file\n",
				       __func__);
			}
		}
		/* Requesting the lowest opp supported */
#if defined(CONFIG_BRIDGE_DVFS) && !defined(CONFIG_CPU_FREQ)
		if (pdata->cpu_set_freq)
			(*pdata->cpu_set_freq) (pdata->mpu_min_speed);
#endif

	}
	if (DSP_SUCCEEDED(status)) {
		/* Update the Processor status to loaded */
		status = (*p_proc_object->intf_fxns->pfn_brd_set_state)
		    (p_proc_object->hwmd_context, BRD_LOADED);
		if (DSP_SUCCEEDED(status)) {
			p_proc_object->proc_state = PROC_LOADED;
			if (p_proc_object->ntfy_obj)
				proc_notify_clients(p_proc_object,
						    DSP_PROCESSORSTATECHANGE);
		}
	}
	if (DSP_SUCCEEDED(status)) {
		status = proc_get_processor_id(hprocessor, &proc_id);
		if (proc_id == DSP_UNIT) {
			/* Use all available DSP address space after EXTMEM
			 * for DMM */
			if (DSP_SUCCEEDED(status))
				status = cod_get_sym_value(cod_mgr, EXTEND,
							   &dw_ext_end);

			/* Reset DMM structs and add an initial free chunk */
			if (DSP_SUCCEEDED(status)) {
				status =
				    dev_get_dmm_mgr(p_proc_object->hdev_obj,
						    &dmm_mgr);
				if (DSP_SUCCEEDED(status)) {
					/* Set dw_ext_end to DMM START u8
					 * address */
					dw_ext_end =
					    (dw_ext_end + 1) * DSPWORDSIZE;
					/* DMM memory is from EXT_END */
					status = dmm_create_tables(dmm_mgr,
								   dw_ext_end,
								   DMMPOOLSIZE);
				}
			}
		}
	}
	/* Restore the original argv[0] */
	kfree(new_envp);
	user_args[0] = pargv0;
	if (DSP_SUCCEEDED(status)) {
		if (DSP_SUCCEEDED((*p_proc_object->intf_fxns->pfn_brd_status)
				  (p_proc_object->hwmd_context, &brd_state))) {
			pr_debug("%s: Processor Loaded %s\n", __func__, pargv0);
			reg_set_value(DEFEXEC, (u8 *)pargv0,
							strlen(pargv0) + 1);
			DBC_ASSERT(brd_state == BRD_LOADED);
		}
	}

func_end:
	if (DSP_FAILED(status)) {
		pr_err("%s: Processor failed to load\n", __func__);
		proc_stop(p_proc_object);
	}
	DBC_ENSURE((DSP_SUCCEEDED(status)
		    && p_proc_object->proc_state == PROC_LOADED)
		   || DSP_FAILED(status));
#ifdef OPT_LOAD_TIME_INSTRUMENTATION
	do_gettimeofday(&tv2);
	if (tv2.tv_usec < tv1.tv_usec) {
		tv2.tv_usec += 1000000;
		tv2.tv_sec--;
	}
	dev_dbg(bridge, "%s: time to load %d sec and %d usec\n", __func__,
		tv2.tv_sec - tv1.tv_sec, tv2.tv_usec - tv1.tv_usec);
#endif
	return status;
}

/*
 *  ======== proc_map ========
 *  Purpose:
 *      Maps a MPU buffer to DSP address space.
 */
dsp_status proc_map(void *hprocessor, void *pmpu_addr, u32 ul_size,
		    void *req_addr, void **pp_map_addr, u32 ul_map_attr,
		    struct process_context *pr_ctxt)
{
	u32 va_align;
	u32 pa_align;
	struct dmm_object *dmm_mgr;
	u32 size_align;
	dsp_status status = DSP_SOK;
	struct proc_object *p_proc_object = (struct proc_object *)hprocessor;
	struct dmm_map_object *map_obj;

#ifdef CONFIG_BRIDGE_CACHE_LINE_CHECK
	if ((ul_map_attr & BUFMODE_MASK) != RBUF) {
		if (!IS_ALIGNED((u32)pmpu_addr, DSP_CACHE_LINE) ||
		    !IS_ALIGNED(ul_size, DSP_CACHE_LINE)) {
			pr_err("%s: not aligned: 0x%x (%d)\n", __func__,
						(u32)pmpu_addr, ul_size);
			return -EFAULT;
		}
	}
#endif

	/* Calculate the page-aligned PA, VA and size */
	va_align = PG_ALIGN_LOW((u32) req_addr, PG_SIZE4K);
	pa_align = PG_ALIGN_LOW((u32) pmpu_addr, PG_SIZE4K);
	size_align = PG_ALIGN_HIGH(ul_size + (u32) pmpu_addr - pa_align,
				   PG_SIZE4K);

	if (!MEM_IS_VALID_HANDLE(p_proc_object, PROC_SIGNATURE)) {
		status = DSP_EHANDLE;
		goto func_end;
	}
	/* Critical section */
	(void)sync_enter_cs(proc_lock);
	status = dmm_get_handle(p_proc_object, &dmm_mgr);
	if (DSP_SUCCEEDED(status))
		status = dmm_map_memory(dmm_mgr, va_align, size_align);

	/* Add mapping to the page tables. */
	if (DSP_SUCCEEDED(status)) {

		status = (*p_proc_object->intf_fxns->pfn_brd_mem_map)
		    (p_proc_object->hwmd_context, pa_align, va_align,
		     size_align, ul_map_attr);
	}
	if (DSP_SUCCEEDED(status)) {
		/* Mapped address = MSB of VA | LSB of PA */
		*pp_map_addr = (void *)(va_align | ((u32) pmpu_addr &
						    (PG_SIZE4K - 1)));
	} else {
		dmm_un_map_memory(dmm_mgr, va_align, &size_align);
	}
	(void)sync_leave_cs(proc_lock);

	if (DSP_FAILED(status))
		goto func_end;

	/*
	 * A successful map should be followed by insertion of map_obj
	 * into dmm_map_list, so that mapped memory resource tracking
	 * remains uptodate
	 */
	map_obj = kmalloc(sizeof(struct dmm_map_object), GFP_KERNEL);
	if (map_obj) {
		map_obj->dsp_addr = (u32) *pp_map_addr;
		spin_lock(&pr_ctxt->dmm_map_lock);
		list_add(&map_obj->link, &pr_ctxt->dmm_map_list);
		spin_unlock(&pr_ctxt->dmm_map_lock);
	}

func_end:
	dev_dbg(bridge, "%s: hprocessor %p, pmpu_addr %p, ul_size %x, "
		"req_addr %p, ul_map_attr %x, pp_map_addr %p, va_align %x, "
		"pa_align %x, size_align %x status 0x%x\n", __func__,
		hprocessor, pmpu_addr, ul_size, req_addr, ul_map_attr,
		pp_map_addr, va_align, pa_align, size_align, status);

	return status;
}

/*
 *  ======== proc_register_notify ========
 *  Purpose:
 *      Register to be notified of specific processor events.
 */
dsp_status proc_register_notify(void *hprocessor, u32 event_mask,
				u32 notify_type, struct dsp_notification
				* hnotification)
{
	dsp_status status = DSP_SOK;
	struct proc_object *p_proc_object = (struct proc_object *)hprocessor;
	struct deh_mgr *hdeh_mgr;

	DBC_REQUIRE(hnotification != NULL);
	DBC_REQUIRE(refs > 0);

	/* Check processor handle */
	if (!MEM_IS_VALID_HANDLE(p_proc_object, PROC_SIGNATURE)) {
		status = DSP_EHANDLE;
		goto func_end;
	}
	/* Check if event mask is a valid processor related event */
	if (event_mask & ~(DSP_PROCESSORSTATECHANGE | DSP_PROCESSORATTACH |
			DSP_PROCESSORDETACH | DSP_PROCESSORRESTART |
			DSP_MMUFAULT | DSP_SYSERROR | DSP_PWRERROR |
			DSP_WDTOVERFLOW))

		status = DSP_EVALUE;

	/* Check if notify type is valid */
	if (notify_type != DSP_SIGNALEVENT)
		status = DSP_EVALUE;

	if (DSP_SUCCEEDED(status)) {
		/* If event mask is not DSP_SYSERROR, DSP_MMUFAULT,
		 * or DSP_PWRERROR then register event immediately. */
		if (event_mask &
		    ~(DSP_SYSERROR | DSP_MMUFAULT | DSP_PWRERROR |
				DSP_WDTOVERFLOW)) {
			status = ntfy_register(p_proc_object->ntfy_obj,
					       hnotification, event_mask,
					       notify_type);
			/* Special case alert, special case alert!
			 * If we're trying to *deregister* (i.e. event_mask
			 * is 0), a DSP_SYSERROR or DSP_MMUFAULT notification,
			 * we have to deregister with the DEH manager.
			 * There's no way to know, based on event_mask which
			 * manager the notification event was registered with,
			 * so if we're trying to deregister and ntfy_register
			 * failed, we'll give the deh manager a shot.
			 */
			if ((event_mask == 0) && DSP_FAILED(status)) {
				status =
				    dev_get_deh_mgr(p_proc_object->hdev_obj,
						    &hdeh_mgr);
				DBC_ASSERT(p_proc_object->
					   intf_fxns->pfn_deh_register_notify);
				status =
				    (*p_proc_object->
				     intf_fxns->pfn_deh_register_notify)
				    (hdeh_mgr, event_mask, notify_type,
				     hnotification);
			}
		} else {
			status = dev_get_deh_mgr(p_proc_object->hdev_obj,
						 &hdeh_mgr);
			DBC_ASSERT(p_proc_object->
				   intf_fxns->pfn_deh_register_notify);
			status =
			    (*p_proc_object->intf_fxns->pfn_deh_register_notify)
			    (hdeh_mgr, event_mask, notify_type, hnotification);

		}
	}
func_end:
	return status;
}

/*
 *  ======== proc_reserve_memory ========
 *  Purpose:
 *      Reserve a virtually contiguous region of DSP address space.
 */
dsp_status proc_reserve_memory(void *hprocessor, u32 ul_size,
			       void **pp_rsv_addr,
			       struct process_context *pr_ctxt)
{
	struct dmm_object *dmm_mgr;
	dsp_status status = DSP_SOK;
	struct proc_object *p_proc_object = (struct proc_object *)hprocessor;
	struct dmm_rsv_object *rsv_obj;

	if (!MEM_IS_VALID_HANDLE(p_proc_object, PROC_SIGNATURE)) {
		status = DSP_EHANDLE;
		goto func_end;
	}

	status = dmm_get_handle(p_proc_object, &dmm_mgr);
	if (DSP_FAILED(status))
		goto func_end;

	status = dmm_reserve_memory(dmm_mgr, ul_size, (u32 *) pp_rsv_addr);
	if (status != DSP_SOK)
		goto func_end;

	/*
	 * A successful reserve should be followed by insertion of rsv_obj
	 * into dmm_rsv_list, so that reserved memory resource tracking
	 * remains uptodate
	 */
	rsv_obj = kmalloc(sizeof(struct dmm_rsv_object), GFP_KERNEL);
	if (rsv_obj) {
		rsv_obj->dsp_reserved_addr = (u32) *pp_rsv_addr;
		spin_lock(&pr_ctxt->dmm_rsv_lock);
		list_add(&rsv_obj->link, &pr_ctxt->dmm_rsv_list);
		spin_unlock(&pr_ctxt->dmm_rsv_lock);
	}

func_end:
	dev_dbg(bridge, "%s: hprocessor: 0x%p ul_size: 0x%x pp_rsv_addr: 0x%p "
		"status 0x%x\n", __func__, hprocessor,
		ul_size, pp_rsv_addr, status);
	return status;
}

/*
 *  ======== proc_start ========
 *  Purpose:
 *      Start a processor running.
 */
dsp_status proc_start(void *hprocessor)
{
	dsp_status status = DSP_SOK;
	struct proc_object *p_proc_object = (struct proc_object *)hprocessor;
	struct cod_manager *cod_mgr;	/* Code manager handle */
	u32 dw_dsp_addr;	/* Loaded code's entry point. */
	int brd_state;

	DBC_REQUIRE(refs > 0);
	if (!MEM_IS_VALID_HANDLE(p_proc_object, PROC_SIGNATURE)) {
		status = DSP_EHANDLE;
		goto func_end;
	}
	/* Call the bridge_brd_start */
	if (p_proc_object->proc_state != PROC_LOADED) {
		status = DSP_EWRONGSTATE;
		goto func_end;
	}
	status = dev_get_cod_mgr(p_proc_object->hdev_obj, &cod_mgr);
	if (DSP_FAILED(status))
		goto func_cont;

	status = cod_get_entry(cod_mgr, &dw_dsp_addr);
	if (DSP_FAILED(status))
		goto func_cont;

	status = (*p_proc_object->intf_fxns->pfn_brd_start)
	    (p_proc_object->hwmd_context, dw_dsp_addr);
	if (DSP_FAILED(status))
		goto func_cont;

	/* Call dev_create2 */
	status = dev_create2(p_proc_object->hdev_obj);
	if (DSP_SUCCEEDED(status)) {
		p_proc_object->proc_state = PROC_RUNNING;
		/* Deep sleep switces off the peripheral clocks.
		 * we just put the DSP CPU in idle in the idle loop.
		 * so there is no need to send a command to DSP */

		if (p_proc_object->ntfy_obj) {
			proc_notify_clients(p_proc_object,
					    DSP_PROCESSORSTATECHANGE);
		}
	} else {
		/* Failed to Create Node Manager and DISP Object
		 * Stop the Processor from running. Put it in STOPPED State */
		(void)(*p_proc_object->intf_fxns->
		       pfn_brd_stop) (p_proc_object->hwmd_context);
		p_proc_object->proc_state = PROC_STOPPED;
	}
func_cont:
	if (DSP_SUCCEEDED(status)) {
		if (DSP_SUCCEEDED((*p_proc_object->intf_fxns->pfn_brd_status)
				  (p_proc_object->hwmd_context, &brd_state))) {
			pr_debug("%s: dsp in running state\n", __func__);
			DBC_ASSERT(brd_state != BRD_HIBERNATION);
		}
	} else {
		pr_err("%s: Failed to start the dsp\n", __func__);
		proc_stop(p_proc_object);
	}

func_end:
	DBC_ENSURE((DSP_SUCCEEDED(status) && p_proc_object->proc_state ==
		    PROC_RUNNING) || DSP_FAILED(status));
	return status;
}

/*
 *  ======== proc_stop ========
 *  Purpose:
 *      Stop a processor running.
 */
dsp_status proc_stop(void *hprocessor)
{
	dsp_status status = DSP_SOK;
	struct proc_object *p_proc_object = (struct proc_object *)hprocessor;
	struct msg_mgr *hmsg_mgr;
	struct node_mgr *hnode_mgr;
	void *hnode;
	u32 node_tab_size = 1;
	u32 num_nodes = 0;
	u32 nodes_allocated = 0;
	int brd_state;

	DBC_REQUIRE(refs > 0);
	if (!MEM_IS_VALID_HANDLE(p_proc_object, PROC_SIGNATURE)) {
		status = DSP_EHANDLE;
		goto func_end;
	}
	if (DSP_SUCCEEDED((*p_proc_object->intf_fxns->pfn_brd_status)
			  (p_proc_object->hwmd_context, &brd_state))) {
		if (brd_state == BRD_ERROR)
			bridge_deh_release_dummy_mem();
	}
	/* check if there are any running nodes */
	status = dev_get_node_manager(p_proc_object->hdev_obj, &hnode_mgr);
	if (DSP_SUCCEEDED(status) && hnode_mgr) {
		status = node_enum_nodes(hnode_mgr, &hnode, node_tab_size,
					 &num_nodes, &nodes_allocated);
		if ((status == DSP_ESIZE) || (nodes_allocated > 0)) {
			pr_err("%s: Can't stop device, active nodes = %d \n",
			       __func__, nodes_allocated);
			return DSP_EWRONGSTATE;
		}
	}
	/* Call the bridge_brd_stop */
	/* It is OK to stop a device that does n't have nodes OR not started */
	status =
	    (*p_proc_object->intf_fxns->
	     pfn_brd_stop) (p_proc_object->hwmd_context);
	if (DSP_SUCCEEDED(status)) {
		dev_dbg(bridge, "%s: processor in standby mode\n", __func__);
		p_proc_object->proc_state = PROC_STOPPED;
		/* Destory the Node Manager, msg_ctrl Manager */
		if (DSP_SUCCEEDED(dev_destroy2(p_proc_object->hdev_obj))) {
			/* Destroy the msg_ctrl by calling msg_delete */
			dev_get_msg_mgr(p_proc_object->hdev_obj, &hmsg_mgr);
			if (hmsg_mgr) {
				msg_delete(hmsg_mgr);
				dev_set_msg_mgr(p_proc_object->hdev_obj, NULL);
			}
			if (DSP_SUCCEEDED
			    ((*p_proc_object->
			      intf_fxns->pfn_brd_status) (p_proc_object->
							  hwmd_context,
							  &brd_state)))
				DBC_ASSERT(brd_state == BRD_STOPPED);
		}
	} else {
		pr_err("%s: Failed to stop the processor\n", __func__);
	}
func_end:

	return status;
}

/*
 *  ======== proc_un_map ========
 *  Purpose:
 *      Removes a MPU buffer mapping from the DSP address space.
 */
dsp_status proc_un_map(void *hprocessor, void *map_addr,
		       struct process_context *pr_ctxt)
{
	dsp_status status = DSP_SOK;
	struct proc_object *p_proc_object = (struct proc_object *)hprocessor;
	struct dmm_object *dmm_mgr;
	u32 va_align;
	u32 size_align;
	struct dmm_map_object *map_obj;

	va_align = PG_ALIGN_LOW((u32) map_addr, PG_SIZE4K);
	if (!MEM_IS_VALID_HANDLE(p_proc_object, PROC_SIGNATURE)) {
		status = DSP_EHANDLE;
		goto func_end;
	}

	status = dmm_get_handle(hprocessor, &dmm_mgr);
	if (DSP_FAILED(status))
		goto func_end;

	/* Critical section */
	(void)sync_enter_cs(proc_lock);
	/*
	 * Update DMM structures. Get the size to unmap.
	 * This function returns error if the VA is not mapped
	 */
	status = dmm_un_map_memory(dmm_mgr, (u32) va_align, &size_align);
	/* Remove mapping from the page tables. */
	if (DSP_SUCCEEDED(status)) {
		status = (*p_proc_object->intf_fxns->pfn_brd_mem_un_map)
		    (p_proc_object->hwmd_context, va_align, size_align);
	}
	(void)sync_leave_cs(proc_lock);
	if (DSP_FAILED(status))
		goto func_end;

	/*
	 * A successful unmap should be followed by removal of map_obj
	 * from dmm_map_list, so that mapped memory resource tracking
	 * remains uptodate
	 */
	spin_lock(&pr_ctxt->dmm_map_lock);
	list_for_each_entry(map_obj, &pr_ctxt->dmm_map_list, link) {
		if (map_obj->dsp_addr == (u32) map_addr) {
			list_del(&map_obj->link);
			kfree(map_obj);
			break;
		}
	}
	spin_unlock(&pr_ctxt->dmm_map_lock);

func_end:
	dev_dbg(bridge, "%s: hprocessor: 0x%p map_addr: 0x%p status: 0x%x\n",
		__func__, hprocessor, map_addr, status);
	return status;
}

/*
 *  ======== proc_un_reserve_memory ========
 *  Purpose:
 *      Frees a previously reserved region of DSP address space.
 */
dsp_status proc_un_reserve_memory(void *hprocessor, void *prsv_addr,
				  struct process_context *pr_ctxt)
{
	struct dmm_object *dmm_mgr;
	dsp_status status = DSP_SOK;
	struct proc_object *p_proc_object = (struct proc_object *)hprocessor;
	struct dmm_rsv_object *rsv_obj;

	if (!MEM_IS_VALID_HANDLE(p_proc_object, PROC_SIGNATURE)) {
		status = DSP_EHANDLE;
		goto func_end;
	}

	status = dmm_get_handle(p_proc_object, &dmm_mgr);
	if (DSP_FAILED(status))
		goto func_end;

	status = dmm_un_reserve_memory(dmm_mgr, (u32) prsv_addr);
	if (status != DSP_SOK)
		goto func_end;

	/*
	 * A successful unreserve should be followed by removal of rsv_obj
	 * from dmm_rsv_list, so that reserved memory resource tracking
	 * remains uptodate
	 */
	spin_lock(&pr_ctxt->dmm_rsv_lock);
	list_for_each_entry(rsv_obj, &pr_ctxt->dmm_rsv_list, link) {
		if (rsv_obj->dsp_reserved_addr == (u32) prsv_addr) {
			list_del(&rsv_obj->link);
			kfree(rsv_obj);
			break;
		}
	}
	spin_unlock(&pr_ctxt->dmm_rsv_lock);

func_end:
	dev_dbg(bridge, "%s: hprocessor: 0x%p prsv_addr: 0x%p status: 0x%x\n",
		__func__, hprocessor, prsv_addr, status);
	return status;
}

/*
 *  ======== = proc_monitor ======== ==
 *  Purpose:
 *      Place the Processor in Monitor State. This is an internal
 *      function and a requirement before Processor is loaded.
 *      This does a bridge_brd_stop, dev_destroy2 and bridge_brd_monitor.
 *      In dev_destroy2 we delete the node manager.
 *  Parameters:
 *      p_proc_object:    Pointer to Processor Object
 *  Returns:
 *      DSP_SOK:	Processor placed in monitor mode.
 *      !DSP_SOK:       Failed to place processor in monitor mode.
 *  Requires:
 *      Valid Processor Handle
 *  Ensures:
 *      Success:	ProcObject state is PROC_IDLE
 */
static dsp_status proc_monitor(struct proc_object *p_proc_object)
{
	dsp_status status = DSP_EFAIL;
	struct msg_mgr *hmsg_mgr;
	int brd_state;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(MEM_IS_VALID_HANDLE(p_proc_object, PROC_SIGNATURE));

	/* This is needed only when Device is loaded when it is
	 * already 'ACTIVE' */
	/* Destory the Node Manager, msg_ctrl Manager */
	if (DSP_SUCCEEDED(dev_destroy2(p_proc_object->hdev_obj))) {
		/* Destroy the msg_ctrl by calling msg_delete */
		dev_get_msg_mgr(p_proc_object->hdev_obj, &hmsg_mgr);
		if (hmsg_mgr) {
			msg_delete(hmsg_mgr);
			dev_set_msg_mgr(p_proc_object->hdev_obj, NULL);
		}
	}
	/* Place the Board in the Monitor State */
	if (DSP_SUCCEEDED((*p_proc_object->intf_fxns->pfn_brd_monitor)
			  (p_proc_object->hwmd_context))) {
		status = DSP_SOK;
		if (DSP_SUCCEEDED((*p_proc_object->intf_fxns->pfn_brd_status)
				  (p_proc_object->hwmd_context, &brd_state)))
			DBC_ASSERT(brd_state == BRD_IDLE);
	}

	DBC_ENSURE((DSP_SUCCEEDED(status) && brd_state == BRD_IDLE) ||
		   DSP_FAILED(status));
	return status;
}

/*
 *  ======== get_envp_count ========
 *  Purpose:
 *      Return the number of elements in the envp array, including the
 *      terminating NULL element.
 */
static s32 get_envp_count(char **envp)
{
	s32 ret = 0;
	if (envp) {
		while (*envp++)
			ret++;

		ret += 1;	/* Include the terminating NULL in the count. */
	}

	return ret;
}

/*
 *  ======== prepend_envp ========
 *  Purpose:
 *      Prepend an environment variable=value pair to the new envp array, and
 *      copy in the existing var=value pairs in the old envp array.
 */
static char **prepend_envp(char **new_envp, char **envp, s32 envp_elems,
			   s32 cnew_envp, char *szVar)
{
	char **pp_envp = new_envp;

	DBC_REQUIRE(new_envp);

	/* Prepend new environ var=value string */
	*new_envp++ = szVar;

	/* Copy user's environment into our own. */
	while (envp_elems--)
		*new_envp++ = *envp++;

	/* Ensure NULL terminates the new environment strings array. */
	if (envp_elems == 0)
		*new_envp = NULL;

	return pp_envp;
}

/*
 *  ======== proc_notify_clients ========
 *  Purpose:
 *      Notify the processor the events.
 */
dsp_status proc_notify_clients(void *hProc, u32 uEvents)
{
	dsp_status status = DSP_SOK;
	struct proc_object *p_proc_object = (struct proc_object *)hProc;

	DBC_REQUIRE(MEM_IS_VALID_HANDLE(p_proc_object, PROC_SIGNATURE));
	DBC_REQUIRE(IS_VALID_PROC_EVENT(uEvents));
	DBC_REQUIRE(refs > 0);
	if (!MEM_IS_VALID_HANDLE(p_proc_object, PROC_SIGNATURE)) {
		status = DSP_EHANDLE;
		goto func_end;
	}

	ntfy_notify(p_proc_object->ntfy_obj, uEvents);
func_end:
	return status;
}

/*
 *  ======== proc_notify_all_clients ========
 *  Purpose:
 *      Notify the processor the events. This includes notifying all clients
 *      attached to a particulat DSP.
 */
dsp_status proc_notify_all_clients(void *hProc, u32 uEvents)
{
	dsp_status status = DSP_SOK;
	struct proc_object *p_proc_object = (struct proc_object *)hProc;

	DBC_REQUIRE(IS_VALID_PROC_EVENT(uEvents));
	DBC_REQUIRE(refs > 0);

	if (!MEM_IS_VALID_HANDLE(p_proc_object, PROC_SIGNATURE)) {
		status = DSP_EHANDLE;
		goto func_end;
	}

	dev_notify_clients(p_proc_object->hdev_obj, uEvents);

func_end:
	return status;
}

/*
 *  ======== proc_get_processor_id ========
 *  Purpose:
 *      Retrieves the processor ID.
 */
dsp_status proc_get_processor_id(void *hProc, u32 * procID)
{
	dsp_status status = DSP_SOK;
	struct proc_object *p_proc_object = (struct proc_object *)hProc;

	if (MEM_IS_VALID_HANDLE(p_proc_object, PROC_SIGNATURE))
		*procID = p_proc_object->processor_id;
	else
		status = DSP_EHANDLE;

	return status;
}
