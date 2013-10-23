/*
 * strm.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * DSP/BIOS Bridge Stream Manager.
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
#include <dspbridge/mem.h>
#include <dspbridge/sync.h>

/*  ----------------------------------- Mini Driver */
#include <dspbridge/wmd.h>

/*  ----------------------------------- Resource Manager */
#include <dspbridge/nodepriv.h>

/*  ----------------------------------- Others */
#include <dspbridge/cmm.h>

/*  ----------------------------------- This */
#include <dspbridge/strm.h>

#include <dspbridge/cfg.h>
#include <dspbridge/resourcecleanup.h>

/*  ----------------------------------- Defines, Data Structures, Typedefs */
#define STRM_SIGNATURE      0x4d525453	/* "MRTS" */
#define STRMMGR_SIGNATURE   0x5254534d	/* "RTSM" */

#define DEFAULTTIMEOUT      10000
#define DEFAULTNUMBUFS      2

/*
 *  ======== strm_mgr ========
 *  The strm_mgr contains device information needed to open the underlying
 *  channels of a stream.
 */
struct strm_mgr {
	u32 dw_signature;
	struct dev_object *dev_obj;	/* Device for this processor */
	struct chnl_mgr *hchnl_mgr;	/* Channel manager */
	struct bridge_drv_interface *intf_fxns;	/* Function interface to WMD */
	struct sync_csobject *sync_obj;	/* For critical sections */
};

/*
 *  ======== strm_object ========
 *  This object is allocated in strm_open().
 */
struct strm_object {
	u32 dw_signature;
	struct strm_mgr *strm_mgr_obj;
	struct chnl_object *chnl_obj;
	u32 dir;		/* DSP_TONODE or DSP_FROMNODE */
	u32 utimeout;
	u32 num_bufs;		/* Max # of bufs allowed in stream */
	u32 un_bufs_in_strm;	/* Current # of bufs in stream */
	u32 ul_n_bytes;		/* bytes transferred since idled */
	/* STREAM_IDLE, STREAM_READY, ... */
	enum dsp_streamstate strm_state;
	bhandle user_event;	/* Saved for strm_get_info() */
	enum dsp_strmmode strm_mode;	/* STRMMODE_[PROCCOPY][ZEROCOPY]... */
	u32 udma_chnl_id;	/* DMA chnl id */
	u32 udma_priority;	/* DMA priority:DMAPRI_[LOW][HIGH] */
	u32 segment_id;		/* >0 is SM segment.=0 is local heap */
	u32 buf_alignment;	/* Alignment for stream bufs */
	/* Stream's SM address translator */
	struct cmm_xlatorobject *xlator;
};

/*  ----------------------------------- Globals */
static u32 refs;		/* module reference count */

/*  ----------------------------------- Function Prototypes */
static dsp_status delete_strm(struct strm_object *hStrm);
static void delete_strm_mgr(struct strm_mgr *strm_mgr_obj);

/*
 *  ======== strm_allocate_buffer ========
 *  Purpose:
 *      Allocates buffers for a stream.
 */
dsp_status strm_allocate_buffer(struct strm_object *hStrm, u32 usize,
				OUT u8 **ap_buffer, u32 num_bufs,
				struct process_context *pr_ctxt)
{
	dsp_status status = DSP_SOK;
	u32 alloc_cnt = 0;
	u32 i;

	bhandle hstrm_res;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(ap_buffer != NULL);

	if (MEM_IS_VALID_HANDLE(hStrm, STRM_SIGNATURE)) {
		/*
		 * Allocate from segment specified at time of stream open.
		 */
		if (usize == 0)
			status = DSP_ESIZE;

	} else {
		status = DSP_EHANDLE;
	}

	if (DSP_FAILED(status))
		goto func_end;

	for (i = 0; i < num_bufs; i++) {
		DBC_ASSERT(hStrm->xlator != NULL);
		(void)cmm_xlator_alloc_buf(hStrm->xlator, &ap_buffer[i], usize);
		if (ap_buffer[i] == NULL) {
			status = DSP_EMEMORY;
			alloc_cnt = i;
			break;
		}
	}
	if (DSP_FAILED(status))
		strm_free_buffer(hStrm, ap_buffer, alloc_cnt, pr_ctxt);

	if (DSP_FAILED(status))
		goto func_end;

	if (drv_get_strm_res_element(hStrm, &hstrm_res, pr_ctxt) !=
	    DSP_ENOTFOUND)
		drv_proc_update_strm_res(num_bufs, hstrm_res);

func_end:
	return status;
}

/*
 *  ======== strm_close ========
 *  Purpose:
 *      Close a stream opened with strm_open().
 */
dsp_status strm_close(struct strm_object *hStrm,
		      struct process_context *pr_ctxt)
{
	struct bridge_drv_interface *intf_fxns;
	struct chnl_info chnl_info_obj;
	dsp_status status = DSP_SOK;

	bhandle hstrm_res;

	DBC_REQUIRE(refs > 0);

	if (!MEM_IS_VALID_HANDLE(hStrm, STRM_SIGNATURE)) {
		status = DSP_EHANDLE;
	} else {
		/* Have all buffers been reclaimed? If not, return
		 * DSP_EPENDING */
		intf_fxns = hStrm->strm_mgr_obj->intf_fxns;
		status =
		    (*intf_fxns->pfn_chnl_get_info) (hStrm->chnl_obj,
						     &chnl_info_obj);
		DBC_ASSERT(DSP_SUCCEEDED(status));

		if (chnl_info_obj.cio_cs > 0 || chnl_info_obj.cio_reqs > 0)
			status = DSP_EPENDING;
		else
			status = delete_strm(hStrm);
	}

	if (DSP_FAILED(status))
		goto func_end;

	if (drv_get_strm_res_element(hStrm, &hstrm_res, pr_ctxt) !=
	    DSP_ENOTFOUND)
		drv_proc_remove_strm_res_element(hstrm_res, pr_ctxt);
func_end:
	DBC_ENSURE(status == DSP_SOK || status == DSP_EHANDLE ||
		   status == DSP_EPENDING || status == DSP_EFAIL);

	dev_dbg(bridge, "%s: hStrm: %p, status 0x%x\n", __func__,
		hStrm, status);
	return status;
}

/*
 *  ======== strm_create ========
 *  Purpose:
 *      Create a STRM manager object.
 */
dsp_status strm_create(OUT struct strm_mgr **phStrmMgr,
		       struct dev_object *dev_obj)
{
	struct strm_mgr *strm_mgr_obj;
	dsp_status status = DSP_SOK;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(phStrmMgr != NULL);
	DBC_REQUIRE(dev_obj != NULL);

	*phStrmMgr = NULL;
	/* Allocate STRM manager object */
	MEM_ALLOC_OBJECT(strm_mgr_obj, struct strm_mgr, STRMMGR_SIGNATURE);
	if (strm_mgr_obj == NULL)
		status = DSP_EMEMORY;
	else
		strm_mgr_obj->dev_obj = dev_obj;

	/* Get Channel manager and WMD function interface */
	if (DSP_SUCCEEDED(status)) {
		status = dev_get_chnl_mgr(dev_obj, &(strm_mgr_obj->hchnl_mgr));
		if (DSP_SUCCEEDED(status)) {
			(void)dev_get_intf_fxns(dev_obj,
						&(strm_mgr_obj->intf_fxns));
			DBC_ASSERT(strm_mgr_obj->intf_fxns != NULL);
		}
	}
	if (DSP_SUCCEEDED(status))
		status = sync_initialize_cs(&strm_mgr_obj->sync_obj);

	if (DSP_SUCCEEDED(status))
		*phStrmMgr = strm_mgr_obj;
	else
		delete_strm_mgr(strm_mgr_obj);

	DBC_ENSURE(DSP_SUCCEEDED(status) &&
		   (MEM_IS_VALID_HANDLE((*phStrmMgr), STRMMGR_SIGNATURE) ||
		    (DSP_FAILED(status) && *phStrmMgr == NULL)));

	return status;
}

/*
 *  ======== strm_delete ========
 *  Purpose:
 *      Delete the STRM Manager Object.
 */
void strm_delete(struct strm_mgr *strm_mgr_obj)
{
	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(MEM_IS_VALID_HANDLE(strm_mgr_obj, STRMMGR_SIGNATURE));

	delete_strm_mgr(strm_mgr_obj);

	DBC_ENSURE(!MEM_IS_VALID_HANDLE(strm_mgr_obj, STRMMGR_SIGNATURE));
}

/*
 *  ======== strm_exit ========
 *  Purpose:
 *      Discontinue usage of STRM module.
 */
void strm_exit(void)
{
	DBC_REQUIRE(refs > 0);

	refs--;

	DBC_ENSURE(refs >= 0);
}

/*
 *  ======== strm_free_buffer ========
 *  Purpose:
 *      Frees the buffers allocated for a stream.
 */
dsp_status strm_free_buffer(struct strm_object *hStrm, u8 ** ap_buffer,
			    u32 num_bufs, struct process_context *pr_ctxt)
{
	dsp_status status = DSP_SOK;
	u32 i = 0;

	bhandle hstrm_res = NULL;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(ap_buffer != NULL);

	if (!MEM_IS_VALID_HANDLE(hStrm, STRM_SIGNATURE))
		status = DSP_EHANDLE;

	if (DSP_SUCCEEDED(status)) {
		for (i = 0; i < num_bufs; i++) {
			DBC_ASSERT(hStrm->xlator != NULL);
			status =
			    cmm_xlator_free_buf(hStrm->xlator, ap_buffer[i]);
			if (DSP_FAILED(status))
				break;
			ap_buffer[i] = NULL;
		}
	}
	if (drv_get_strm_res_element(hStrm, hstrm_res, pr_ctxt) !=
	    DSP_ENOTFOUND)
		drv_proc_update_strm_res(num_bufs - i, hstrm_res);

	return status;
}

/*
 *  ======== strm_get_info ========
 *  Purpose:
 *      Retrieves information about a stream.
 */
dsp_status strm_get_info(struct strm_object *hStrm,
			 OUT struct stream_info *stream_info,
			 u32 stream_info_size)
{
	struct bridge_drv_interface *intf_fxns;
	struct chnl_info chnl_info_obj;
	dsp_status status = DSP_SOK;
	void *virt_base = NULL;	/* NULL if no SM used */

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(stream_info != NULL);
	DBC_REQUIRE(stream_info_size >= sizeof(struct stream_info));

	if (!MEM_IS_VALID_HANDLE(hStrm, STRM_SIGNATURE)) {
		status = DSP_EHANDLE;
	} else {
		if (stream_info_size < sizeof(struct stream_info)) {
			/* size of users info */
			status = DSP_ESIZE;
		}
	}
	if (DSP_FAILED(status))
		goto func_end;

	intf_fxns = hStrm->strm_mgr_obj->intf_fxns;
	status =
	    (*intf_fxns->pfn_chnl_get_info) (hStrm->chnl_obj, &chnl_info_obj);
	if (DSP_FAILED(status))
		goto func_end;

	if (hStrm->xlator) {
		/* We have a translator */
		DBC_ASSERT(hStrm->segment_id > 0);
		cmm_xlator_info(hStrm->xlator, (u8 **) &virt_base, 0,
				hStrm->segment_id, false);
	}
	stream_info->segment_id = hStrm->segment_id;
	stream_info->strm_mode = hStrm->strm_mode;
	stream_info->virt_base = virt_base;
	stream_info->user_strm->number_bufs_allowed = hStrm->num_bufs;
	stream_info->user_strm->number_bufs_in_stream = chnl_info_obj.cio_cs +
	    chnl_info_obj.cio_reqs;
	/* # of bytes transferred since last call to DSPStream_Idle() */
	stream_info->user_strm->ul_number_bytes = chnl_info_obj.bytes_tx;
	stream_info->user_strm->sync_object_handle = chnl_info_obj.event_obj;
	/* Determine stream state based on channel state and info */
	if (chnl_info_obj.dw_state & CHNL_STATEEOS) {
		stream_info->user_strm->ss_stream_state = STREAM_DONE;
	} else {
		if (chnl_info_obj.cio_cs > 0)
			stream_info->user_strm->ss_stream_state = STREAM_READY;
		else if (chnl_info_obj.cio_reqs > 0)
			stream_info->user_strm->ss_stream_state =
			    STREAM_PENDING;
		else
			stream_info->user_strm->ss_stream_state = STREAM_IDLE;

	}
func_end:
	return status;
}

/*
 *  ======== strm_idle ========
 *  Purpose:
 *      Idles a particular stream.
 */
dsp_status strm_idle(struct strm_object *hStrm, bool fFlush)
{
	struct bridge_drv_interface *intf_fxns;
	dsp_status status = DSP_SOK;

	DBC_REQUIRE(refs > 0);

	if (!MEM_IS_VALID_HANDLE(hStrm, STRM_SIGNATURE)) {
		status = DSP_EHANDLE;
	} else {
		intf_fxns = hStrm->strm_mgr_obj->intf_fxns;

		status = (*intf_fxns->pfn_chnl_idle) (hStrm->chnl_obj,
						      hStrm->utimeout, fFlush);
	}

	dev_dbg(bridge, "%s: hStrm: %p fFlush: 0x%x status: 0x%x\n",
		__func__, hStrm, fFlush, status);
	return status;
}

/*
 *  ======== strm_init ========
 *  Purpose:
 *      Initialize the STRM module.
 */
bool strm_init(void)
{
	bool ret = true;

	DBC_REQUIRE(refs >= 0);

	if (ret)
		refs++;

	DBC_ENSURE((ret && (refs > 0)) || (!ret && (refs >= 0)));

	return ret;
}

/*
 *  ======== strm_issue ========
 *  Purpose:
 *      Issues a buffer on a stream
 */
dsp_status strm_issue(struct strm_object *hStrm, IN u8 *pbuf, u32 ul_bytes,
		      u32 ul_buf_size, u32 dw_arg)
{
	struct bridge_drv_interface *intf_fxns;
	dsp_status status = DSP_SOK;
	void *tmp_buf = NULL;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(pbuf != NULL);

	if (!MEM_IS_VALID_HANDLE(hStrm, STRM_SIGNATURE)) {
		status = DSP_EHANDLE;
	} else {
		intf_fxns = hStrm->strm_mgr_obj->intf_fxns;

		if (hStrm->segment_id != 0) {
			tmp_buf = cmm_xlator_translate(hStrm->xlator,
						       (void *)pbuf,
						       CMM_VA2DSPPA);
			if (tmp_buf == NULL)
				status = DSP_ETRANSLATE;

		}
		if (DSP_SUCCEEDED(status)) {
			status = (*intf_fxns->pfn_chnl_add_io_req)
			    (hStrm->chnl_obj, pbuf, ul_bytes, ul_buf_size,
			     (u32) tmp_buf, dw_arg);
		}
		if (status == CHNL_E_NOIORPS)
			status = DSP_ESTREAMFULL;
	}

	dev_dbg(bridge, "%s: hStrm: %p pbuf: %p ul_bytes: 0x%x dw_arg: 0x%x "
		"status: 0x%x\n", __func__, hStrm, pbuf,
		ul_bytes, dw_arg, status);
	return status;
}

/*
 *  ======== strm_open ========
 *  Purpose:
 *      Open a stream for sending/receiving data buffers to/from a task or
 *      XDAIS socket node on the DSP.
 */
dsp_status strm_open(struct node_object *hnode, u32 dir, u32 index,
		     IN struct strm_attr *pattr,
		     OUT struct strm_object **phStrm,
		     struct process_context *pr_ctxt)
{
	struct strm_mgr *strm_mgr_obj;
	struct bridge_drv_interface *intf_fxns;
	u32 ul_chnl_id;
	struct strm_object *strm_obj = NULL;
	short int chnl_mode;
	struct chnl_attr chnl_attr_obj;
	dsp_status status = DSP_SOK;
	struct cmm_object *hcmm_mgr = NULL;	/* Shared memory manager hndl */

	bhandle hstrm_res;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(phStrm != NULL);
	DBC_REQUIRE(pattr != NULL);
	*phStrm = NULL;
	if (dir != DSP_TONODE && dir != DSP_FROMNODE) {
		status = DSP_EDIRECTION;
	} else {
		/* Get the channel id from the node (set in node_connect()) */
		status = node_get_channel_id(hnode, dir, index, &ul_chnl_id);
	}
	if (DSP_SUCCEEDED(status))
		status = node_get_strm_mgr(hnode, &strm_mgr_obj);

	if (DSP_SUCCEEDED(status)) {
		MEM_ALLOC_OBJECT(strm_obj, struct strm_object, STRM_SIGNATURE);
		if (strm_obj == NULL) {
			status = DSP_EMEMORY;
		} else {
			strm_obj->strm_mgr_obj = strm_mgr_obj;
			strm_obj->dir = dir;
			strm_obj->strm_state = STREAM_IDLE;
			strm_obj->user_event = pattr->user_event;
			if (pattr->stream_attr_in != NULL) {
				strm_obj->utimeout =
				    pattr->stream_attr_in->utimeout;
				strm_obj->num_bufs =
				    pattr->stream_attr_in->num_bufs;
				strm_obj->strm_mode =
				    pattr->stream_attr_in->strm_mode;
				strm_obj->segment_id =
				    pattr->stream_attr_in->segment_id;
				strm_obj->buf_alignment =
				    pattr->stream_attr_in->buf_alignment;
				strm_obj->udma_chnl_id =
				    pattr->stream_attr_in->udma_chnl_id;
				strm_obj->udma_priority =
				    pattr->stream_attr_in->udma_priority;
				chnl_attr_obj.uio_reqs =
				    pattr->stream_attr_in->num_bufs;
			} else {
				strm_obj->utimeout = DEFAULTTIMEOUT;
				strm_obj->num_bufs = DEFAULTNUMBUFS;
				strm_obj->strm_mode = STRMMODE_PROCCOPY;
				strm_obj->segment_id = 0;	/* local mem */
				strm_obj->buf_alignment = 0;
				strm_obj->udma_chnl_id = 0;
				strm_obj->udma_priority = 0;
				chnl_attr_obj.uio_reqs = DEFAULTNUMBUFS;
			}
			chnl_attr_obj.reserved1 = NULL;
			/* DMA chnl flush timeout */
			chnl_attr_obj.reserved2 = strm_obj->utimeout;
			chnl_attr_obj.event_obj = NULL;
			if (pattr->user_event != NULL)
				chnl_attr_obj.event_obj = pattr->user_event;

		}
	}
	if (DSP_FAILED(status))
		goto func_cont;

	if ((pattr->virt_base == NULL) || !(pattr->ul_virt_size > 0))
		goto func_cont;

	/* No System DMA */
	DBC_ASSERT(strm_obj->strm_mode != STRMMODE_LDMA);
	/* Get the shared mem mgr for this streams dev object */
	status = dev_get_cmm_mgr(strm_mgr_obj->dev_obj, &hcmm_mgr);
	if (DSP_SUCCEEDED(status)) {
		/*Allocate a SM addr translator for this strm. */
		status = cmm_xlator_create(&strm_obj->xlator, hcmm_mgr, NULL);
		if (DSP_SUCCEEDED(status)) {
			DBC_ASSERT(strm_obj->segment_id > 0);
			/*  Set translators Virt Addr attributes */
			status = cmm_xlator_info(strm_obj->xlator,
						 (u8 **) &pattr->virt_base,
						 pattr->ul_virt_size,
						 strm_obj->segment_id, true);
		}
	}
func_cont:
	if (DSP_SUCCEEDED(status)) {
		/* Open channel */
		chnl_mode = (dir == DSP_TONODE) ?
		    CHNL_MODETODSP : CHNL_MODEFROMDSP;
		intf_fxns = strm_mgr_obj->intf_fxns;
		status = (*intf_fxns->pfn_chnl_open) (&(strm_obj->chnl_obj),
						      strm_mgr_obj->hchnl_mgr,
						      chnl_mode, ul_chnl_id,
						      &chnl_attr_obj);
		if (DSP_FAILED(status)) {
			/*
			 * over-ride non-returnable status codes so we return
			 * something documented
			 */
			if (status != DSP_EMEMORY && status !=
			    DSP_EINVALIDARG && status != DSP_EFAIL) {
				/*
				 * We got a status that's not return-able.
				 * Assert that we got something we were
				 * expecting (DSP_EHANDLE isn't acceptable,
				 * strm_mgr_obj->hchnl_mgr better be valid or we
				 * assert here), and then return DSP_EFAIL.
				 */
				DBC_ASSERT(status == CHNL_E_OUTOFSTREAMS ||
					   status == CHNL_E_BADCHANID ||
					   status == CHNL_E_CHANBUSY ||
					   status == CHNL_E_NOIORPS);
				status = DSP_EFAIL;
			}
		}
	}
	if (DSP_SUCCEEDED(status)) {
		*phStrm = strm_obj;
		drv_proc_insert_strm_res_element(*phStrm, &hstrm_res, pr_ctxt);
	} else {
		(void)delete_strm(strm_obj);
	}

	/* ensure we return a documented error code */
	DBC_ENSURE((DSP_SUCCEEDED(status) &&
		    MEM_IS_VALID_HANDLE((*phStrm), STRM_SIGNATURE)) ||
		   (*phStrm == NULL && (status == DSP_EHANDLE ||
					status == DSP_EDIRECTION
					|| status == DSP_EVALUE
					|| status == DSP_EFAIL)));

	dev_dbg(bridge, "%s: hnode: %p dir: 0x%x index: 0x%x pattr: %p "
		"phStrm: %p status: 0x%x\n", __func__,
		hnode, dir, index, pattr, phStrm, status);
	return status;
}

/*
 *  ======== strm_reclaim ========
 *  Purpose:
 *      Relcaims a buffer from a stream.
 */
dsp_status strm_reclaim(struct strm_object *hStrm, OUT u8 ** buf_ptr,
			u32 *pulBytes, u32 *pulBufSize, u32 *pdw_arg)
{
	struct bridge_drv_interface *intf_fxns;
	struct chnl_ioc chnl_ioc_obj;
	dsp_status status = DSP_SOK;
	void *tmp_buf = NULL;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(buf_ptr != NULL);
	DBC_REQUIRE(pulBytes != NULL);
	DBC_REQUIRE(pdw_arg != NULL);

	if (!MEM_IS_VALID_HANDLE(hStrm, STRM_SIGNATURE)) {
		status = DSP_EHANDLE;
		goto func_end;
	}
	intf_fxns = hStrm->strm_mgr_obj->intf_fxns;

	status =
	    (*intf_fxns->pfn_chnl_get_ioc) (hStrm->chnl_obj, hStrm->utimeout,
					    &chnl_ioc_obj);
	if (DSP_SUCCEEDED(status)) {
		*pulBytes = chnl_ioc_obj.byte_size;
		if (pulBufSize)
			*pulBufSize = chnl_ioc_obj.buf_size;

		*pdw_arg = chnl_ioc_obj.dw_arg;
		if (!CHNL_IS_IO_COMPLETE(chnl_ioc_obj)) {
			if (CHNL_IS_TIMED_OUT(chnl_ioc_obj)) {
				status = DSP_ETIMEOUT;
			} else {
				/* Allow reclaims after idle to succeed */
				if (!CHNL_IS_IO_CANCELLED(chnl_ioc_obj))
					status = DSP_EFAIL;

			}
		}
		/* Translate zerocopy buffer if channel not canceled. */
		if (DSP_SUCCEEDED(status)
		    && (!CHNL_IS_IO_CANCELLED(chnl_ioc_obj))
		    && (hStrm->strm_mode == STRMMODE_ZEROCOPY)) {
			/*
			 *  This is a zero-copy channel so chnl_ioc_obj.pbuf
			 *  contains the DSP address of SM. We need to
			 *  translate it to a virtual address for the user
			 *  thread to access.
			 *  Note: Could add CMM_DSPPA2VA to CMM in the future.
			 */
			tmp_buf = cmm_xlator_translate(hStrm->xlator,
						       chnl_ioc_obj.pbuf,
						       CMM_DSPPA2PA);
			if (tmp_buf != NULL) {
				/* now convert this GPP Pa to Va */
				tmp_buf = cmm_xlator_translate(hStrm->xlator,
							       tmp_buf,
							       CMM_PA2VA);
			}
			if (tmp_buf == NULL)
				status = DSP_ETRANSLATE;

			chnl_ioc_obj.pbuf = tmp_buf;
		}
		*buf_ptr = chnl_ioc_obj.pbuf;
	}
func_end:
	/* ensure we return a documented return code */
	DBC_ENSURE(DSP_SUCCEEDED(status) || status == DSP_EHANDLE ||
		   status == DSP_ETIMEOUT || status == DSP_ETRANSLATE ||
		   status == DSP_EFAIL);

	dev_dbg(bridge, "%s: hStrm: %p buf_ptr: %p pulBytes: %p pdw_arg: %p "
		"status 0x%x\n", __func__, hStrm,
		buf_ptr, pulBytes, pdw_arg, status);
	return status;
}

/*
 *  ======== strm_register_notify ========
 *  Purpose:
 *      Register to be notified on specific events for this stream.
 */
dsp_status strm_register_notify(struct strm_object *hStrm, u32 event_mask,
				u32 notify_type, struct dsp_notification
				* hnotification)
{
	struct bridge_drv_interface *intf_fxns;
	dsp_status status = DSP_SOK;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(hnotification != NULL);

	if (!MEM_IS_VALID_HANDLE(hStrm, STRM_SIGNATURE)) {
		status = DSP_EHANDLE;
	} else if ((event_mask & ~((DSP_STREAMIOCOMPLETION) |
				   DSP_STREAMDONE)) != 0) {
		status = DSP_EVALUE;
	} else {
		if (notify_type != DSP_SIGNALEVENT)
			status = DSP_ENOTIMPL;

	}
	if (DSP_SUCCEEDED(status)) {
		intf_fxns = hStrm->strm_mgr_obj->intf_fxns;

		status =
		    (*intf_fxns->pfn_chnl_register_notify) (hStrm->chnl_obj,
							    event_mask,
							    notify_type,
							    hnotification);
	}
	/* ensure we return a documented return code */
	DBC_ENSURE(DSP_SUCCEEDED(status) || status == DSP_EHANDLE ||
		   status == DSP_ETIMEOUT || status == DSP_ETRANSLATE ||
		   status == DSP_ENOTIMPL || status == DSP_EFAIL);
	return status;
}

/*
 *  ======== strm_select ========
 *  Purpose:
 *      Selects a ready stream.
 */
dsp_status strm_select(IN struct strm_object **strm_tab, u32 nStrms,
		       OUT u32 *pmask, u32 utimeout)
{
	u32 index;
	struct chnl_info chnl_info_obj;
	struct bridge_drv_interface *intf_fxns;
	struct sync_object **sync_events = NULL;
	u32 i;
	dsp_status status = DSP_SOK;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(strm_tab != NULL);
	DBC_REQUIRE(pmask != NULL);
	DBC_REQUIRE(nStrms > 0);

	*pmask = 0;
	for (i = 0; i < nStrms; i++) {
		if (!MEM_IS_VALID_HANDLE(strm_tab[i], STRM_SIGNATURE)) {
			status = DSP_EHANDLE;
			break;
		}
	}
	if (DSP_FAILED(status))
		goto func_end;

	/* Determine which channels have IO ready */
	for (i = 0; i < nStrms; i++) {
		intf_fxns = strm_tab[i]->strm_mgr_obj->intf_fxns;
		status = (*intf_fxns->pfn_chnl_get_info) (strm_tab[i]->chnl_obj,
							  &chnl_info_obj);
		if (DSP_FAILED(status)) {
			break;
		} else {
			if (chnl_info_obj.cio_cs > 0)
				*pmask |= (1 << i);

		}
	}
	if (DSP_SUCCEEDED(status) && utimeout > 0 && *pmask == 0) {
		/* Non-zero timeout */
		sync_events = (struct sync_object **)mem_alloc(nStrms *
						sizeof(struct sync_object *),
						MEM_PAGED);
		if (sync_events == NULL) {
			status = DSP_EMEMORY;
		} else {
			for (i = 0; i < nStrms; i++) {
				intf_fxns =
				    strm_tab[i]->strm_mgr_obj->intf_fxns;
				status = (*intf_fxns->pfn_chnl_get_info)
				    (strm_tab[i]->chnl_obj, &chnl_info_obj);
				if (DSP_FAILED(status))
					break;
				else
					sync_events[i] =
					    chnl_info_obj.sync_event;

			}
		}
		if (DSP_SUCCEEDED(status)) {
			status =
			    sync_wait_on_multiple_events(sync_events, nStrms,
							 utimeout, &index);
			if (DSP_SUCCEEDED(status)) {
				/* Since we waited on the event, we have to
				 * reset it */
				sync_set_event(sync_events[index]);
				*pmask = 1 << index;
			}
		}
	}
func_end:
	kfree(sync_events);

	DBC_ENSURE((DSP_SUCCEEDED(status) && (*pmask != 0 || utimeout == 0)) ||
		   (DSP_FAILED(status) && *pmask == 0));

	return status;
}

/*
 *  ======== delete_strm ========
 *  Purpose:
 *      Frees the resources allocated for a stream.
 */
static dsp_status delete_strm(struct strm_object *hStrm)
{
	struct bridge_drv_interface *intf_fxns;
	dsp_status status = DSP_SOK;

	if (MEM_IS_VALID_HANDLE(hStrm, STRM_SIGNATURE)) {
		if (hStrm->chnl_obj) {
			intf_fxns = hStrm->strm_mgr_obj->intf_fxns;
			/* Channel close can fail only if the channel handle
			 * is invalid. */
			status = (*intf_fxns->pfn_chnl_close) (hStrm->chnl_obj);
			/* Free all SM address translator resources */
			if (DSP_SUCCEEDED(status)) {
				if (hStrm->xlator) {
					/* force free */
					(void)cmm_xlator_delete(hStrm->xlator,
								true);
				}
			}
		}
		MEM_FREE_OBJECT(hStrm);
	} else {
		status = DSP_EHANDLE;
	}
	return status;
}

/*
 *  ======== delete_strm_mgr ========
 *  Purpose:
 *      Frees stream manager.
 */
static void delete_strm_mgr(struct strm_mgr *strm_mgr_obj)
{
	if (MEM_IS_VALID_HANDLE(strm_mgr_obj, STRMMGR_SIGNATURE)) {

		if (strm_mgr_obj->sync_obj)
			sync_delete_cs(strm_mgr_obj->sync_obj);

		MEM_FREE_OBJECT(strm_mgr_obj);
	}
}
