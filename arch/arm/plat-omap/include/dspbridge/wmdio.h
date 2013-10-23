/*
 * wmdio.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Declares the upper edge IO functions required by all WMD / WCD
 * driver interface tables.
 *
 * Notes:
 *   Function comment headers reside in wmd.h.
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

#ifndef WMDIO_
#define WMDIO_

#include <dspbridge/devdefs.h>
#include <dspbridge/iodefs.h>

extern dsp_status bridge_io_create(OUT struct io_mgr **phIOMgr,
				   struct dev_object *hdev_obj,
				   IN CONST struct io_attrs *pMgrAttrs);

extern dsp_status bridge_io_destroy(struct io_mgr *hio_mgr);

extern dsp_status bridge_io_on_loaded(struct io_mgr *hio_mgr);

extern dsp_status iva_io_on_loaded(struct io_mgr *hio_mgr);
extern dsp_status bridge_io_get_proc_load(IN struct io_mgr *hio_mgr,
				       OUT struct dsp_procloadstat *pProcStat);

#endif /* WMDIO_ */
