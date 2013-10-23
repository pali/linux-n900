/*
 * ioobj.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
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


/*
 *  ======== ioobj.h ========
 *  Description:
 *      Structure subcomponents of channel class library IO objects which
 *      are exposed to class driver from mini-driver.
 *
 *  Public Functions:
 *      None.
 *
 *! Revision History:
 *! ================
 *! 24-Feb-2003 swa PMGR Code review comments incorporated.
 *! 01/16/97 gp: Created from chnlpriv.h
 */

#ifndef IOOBJ_
#define IOOBJ_

#include <dspbridge/devdefs.h>
#include <dspbridge/wmd.h>

/*
 *  This struct is the first field in a IO_MGR struct, as implemented in
 *  a WMD channel class library.  Other, implementation specific fields
 *  follow this structure in memory.
 */
struct IO_MGR_ {
	/* These must be the first fields in a IO_MGR struct: */
	u32 dwSignature;	/* Used for object validation.   */
	struct WMD_DEV_CONTEXT *hWmdContext;	/* WMD device context.  */
	struct WMD_DRV_INTERFACE *pIntfFxns;	/* Function interface to WMD. */
	struct DEV_OBJECT *hDevObject;	/* Device this board represents. */
} ;

#endif				/* IOOBJ_ */
