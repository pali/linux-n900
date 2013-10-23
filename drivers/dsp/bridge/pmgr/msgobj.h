/*
 * msgobj.h
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
 *  ======== msgobj.h ========
 *  Description:
 *      Structure subcomponents of channel class library MSG objects which
 *      are exposed to class driver from mini-driver.
 *
 *  Public Functions:
 *      None.
 *
 *! Revision History:
 *! ================
 *! 24-Feb-2003 swa 	PMGR Code review comments incorporated.
 *! 17-Nov-2000 jeh     Created.
 */

#ifndef MSGOBJ_
#define MSGOBJ_

#include <dspbridge/wmd.h>

#include <dspbridge/msgdefs.h>

/*
 *  This struct is the first field in a MSG_MGR struct, as implemented in
 *  a WMD channel class library.  Other, implementation specific fields
 *  follow this structure in memory.
 */
struct MSG_MGR_ {
	/* The first two fields must match those in msgobj.h */
	u32 dwSignature;
	struct WMD_DRV_INTERFACE *pIntfFxns;	/* Function interface to WMD. */
};

#endif				/* MSGOBJ_ */

