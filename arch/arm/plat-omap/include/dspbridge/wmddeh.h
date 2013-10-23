/*
 * wmddeh.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Defines upper edge DEH functions required by all WMD/WCD driver
 * interface tables.
 *
 * Notes:
 *   Function comment headers reside with the function typedefs in wmd.h.
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

#ifndef WMDDEH_
#define WMDDEH_

#include <dspbridge/devdefs.h>

#include <dspbridge/dehdefs.h>

extern dsp_status bridge_deh_create(OUT struct deh_mgr **phDehMgr,
				    struct dev_object *hdev_obj);

extern dsp_status bridge_deh_destroy(struct deh_mgr *hdeh_mgr);

extern dsp_status bridge_deh_get_info(struct deh_mgr *hdeh_mgr,
				   struct dsp_errorinfo *pErrInfo);

extern dsp_status bridge_deh_register_notify(struct deh_mgr *hdeh_mgr,
					  u32 event_mask,
					  u32 notify_type,
					  struct dsp_notification
					  *hnotification);

extern void bridge_deh_notify(struct deh_mgr *hdeh_mgr,
			      u32 ulEventMask, u32 dwErrInfo);

extern void bridge_deh_release_dummy_mem(void);
#endif /* WMDDEH_ */
