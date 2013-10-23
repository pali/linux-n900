/*
 * msgobj.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Structure subcomponents of channel class library msg_ctrl objects which
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

#ifndef MSGOBJ_
#define MSGOBJ_

#include <dspbridge/wmd.h>

#include <dspbridge/msgdefs.h>

/*
 *  This struct is the first field in a msg_mgr struct, as implemented in
 *  a WMD channel class library.  Other, implementation specific fields
 *  follow this structure in memory.
 */
struct msg_mgr_ {
	/* The first two fields must match those in msgobj.h */
	u32 dw_signature;
	struct bridge_drv_interface *intf_fxns;	/* Function interface to WMD. */
};

#endif /* MSGOBJ_ */
