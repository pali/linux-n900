/*
 * wcd.h
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
 *  ======== wcd.h ========
 *  Description:
 *      'Bridge class driver library functions, object definitions, and
 *      return error/status codes.  To be included by 'Bridge mini drivers.
 *
 *  Public Functions:
 *      See mem.h and dbg.h.
 *
 *  Notes:
 *      'Bridge Class Driver services exported to WMD's are initialized by the
 *      WCD on behalf of the WMD.  WMD's must not call module Init/Exit
 *      functions.
 *
 *      To ensure WMD binary compatibility across different platforms,
 *      for the same processor, a WMD must restrict its usage of system
 *      services to those exported by the 'Bridge class library.
 *
 *! Revision History:
 *! ================
 *! 07-Jun-2000 jeh Added dev.h
 *! 01-Nov-1999 ag: #WINCE# WCD_MAJOR_VERSION=8 & WCD_MINOR_VERSION=0 to match
 *!		    dll stamps.
 *!                 0.80 - 0.89 Alpha, 0.90 - 0.99 Beta, 1.00 - 1.10 FCS.
 *! 17-Sep-1997 gp: Changed size of CFG_HOSTRES structure; and ISR_Install API;
 *!                 Changed WCD_MINOR_VERSION 3 -> 4.
 *! 15-Sep-1997 gp: Moved WCD_(Un)registerMinidriver to drv.
 *! 25-Jul-1997 cr: Added WCD_UnregisterMinidriver.
 *! 22-Jul-1997 cr: Added WCD_RegisterMinidriver, WCD_MINOR_VERSION 2 -> 3.
 *! 12-Nov-1996 gp: Defined port io macros.
 *! 07-Nov-1996 gp: Updated for code review.
 *! 16-Jul-1996 gp: Added CHNL fxns; updated WCD lib version to 2.
 *! 10-May-1996 gp: Separated WMD def.s' into wmd.h.
 *! 03-May-1996 gp: Created.
 */

#ifndef WCD_
#define WCD_

/* This WCD Library Version:  */
#define WCD_MAJOR_VERSION   (u32)8	/* .8x - Alpha, .9x - Beta, 1.x FCS */
#define WCD_MINOR_VERSION   (u32)0

#endif				/* WCD_ */
