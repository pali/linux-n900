/*
 * ioobj.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Structure subcomponents of channel class library IO objects which
 * are exposed to class driver from mini-driver.
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

#ifndef IOOBJ_
#define IOOBJ_

#include <dspbridge/devdefs.h>
#include <dspbridge/wmd.h>

/*
 *  This struct is the first field in a io_mgr struct, as implemented in
 *  a WMD channel class library.  Other, implementation specific fields
 *  follow this structure in memory.
 */
struct io_mgr_ {
	/* These must be the first fields in a io_mgr struct: */
	u32 dw_signature;	/* Used for object validation. */
	struct wmd_dev_context *hwmd_context;	/* WMD device context. */
	struct bridge_drv_interface *intf_fxns;	/* Function interface to WMD. */
	struct dev_object *hdev_obj;	/* Device this board represents. */
};

#endif /* IOOBJ_ */
