/*
 * wmdchnl.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Declares the upper edge channel class library functions required by
 * all WMD / WCD driver interface tables.  These functions are implemented
 * by every class of WMD channel library.
 *
 * Notes:
 *   The function comment headers reside in wmd.h.
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

#ifndef WMDCHNL_
#define WMDCHNL_

extern dsp_status bridge_chnl_create(OUT struct chnl_mgr **phChnlMgr,
				     struct dev_object *hdev_obj,
				     IN CONST struct chnl_mgrattrs
				     *pMgrAttrs);

extern dsp_status bridge_chnl_destroy(struct chnl_mgr *hchnl_mgr);

extern dsp_status bridge_chnl_open(OUT struct chnl_object **phChnl,
				   struct chnl_mgr *hchnl_mgr,
				   short int chnl_mode,
				   u32 uChnlId,
				   CONST IN OPTIONAL struct chnl_attr
				   *pattrs);

extern dsp_status bridge_chnl_close(struct chnl_object *chnl_obj);

extern dsp_status bridge_chnl_add_io_req(struct chnl_object *chnl_obj,
				      void *pHostBuf,
				      u32 byte_size, u32 buf_size,
				      OPTIONAL u32 dw_dsp_addr, u32 dw_arg);

extern dsp_status bridge_chnl_get_ioc(struct chnl_object *chnl_obj,
				   u32 dwTimeOut, OUT struct chnl_ioc *pIOC);

extern dsp_status bridge_chnl_cancel_io(struct chnl_object *chnl_obj);

extern dsp_status bridge_chnl_flush_io(struct chnl_object *chnl_obj,
				    u32 dwTimeOut);

extern dsp_status bridge_chnl_get_info(struct chnl_object *chnl_obj,
				    OUT struct chnl_info *pInfo);

extern dsp_status bridge_chnl_get_mgr_info(struct chnl_mgr *hchnl_mgr,
					u32 uChnlID, OUT struct chnl_mgrinfo
					*pMgrInfo);

extern dsp_status bridge_chnl_idle(struct chnl_object *chnl_obj,
				   u32 dwTimeOut, bool fFlush);

extern dsp_status bridge_chnl_register_notify(struct chnl_object *chnl_obj,
					   u32 event_mask,
					   u32 notify_type,
					   struct dsp_notification
					   *hnotification);

#endif /* WMDCHNL_ */
