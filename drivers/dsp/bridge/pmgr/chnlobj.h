/*
 * chnlobj.h
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
 *  ======== chnlobj.h ========
 *  Description:
 *      Structure subcomponents of channel class library channel objects which
 *      are exposed to class driver from mini-driver.
 *
 *  Public Functions:
 *      None.
 *
 *! Revision History:
 *! ================
 *! 24-Feb-2003 swa 	PMGR Code review comments incorporated.
 *! 17-Nov-2000 jeh     Removed some fields from CHNL_MGR_ to match CHNL_MGR
 *!                     structure defined in _chnl_sm.h.
 *! 16-Jan-1997 gp:     Created from chnlpriv.h
 */

#ifndef CHNLOBJ_
#define CHNLOBJ_

#include <dspbridge/chnldefs.h>
#include <dspbridge/wmd.h>

/* Object validateion macros: */
#define CHNL_IsValidMgr(h) \
		((h != NULL) && ((h)->dwSignature == CHNL_MGRSIGNATURE))

#define CHNL_IsValidChnl(h)\
		((h != NULL) && ((h)->dwSignature == CHNL_SIGNATURE))

/*
 *  This struct is the first field in a CHNL_MGR struct, as implemented in
 *  a WMD channel class library.  Other, implementation specific fields
 *  follow this structure in memory.
 */
struct CHNL_MGR_ {
	/* These must be the first fields in a CHNL_MGR struct: */
	u32 dwSignature;	/* Used for object validation.   */
	struct WMD_DRV_INTERFACE *pIntfFxns;	/* Function interface to WMD. */
} ;

/*
 *  This struct is the first field in a CHNL_OBJECT struct, as implemented in
 *  a WMD channel class library.  Other, implementation specific fields
 *  follow this structure in memory.
 */
struct CHNL_OBJECT_ {
	/* These must be the first fields in a CHNL_OBJECT struct: */
	u32 dwSignature;	/* Used for object validation.      */
	struct CHNL_MGR_ *pChnlMgr;	/* Pointer back to channel manager. */
} ;

#endif				/* CHNLOBJ_ */

