/*
 * devdefs.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Copyright (C) 2008 Texas Instruments, Inc.
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
 *  ======== devdefs.h ========
 *  Purpose:
 *      Definition of common include typedef between wmd.h and dev.h. Required
 *      to break circular dependency between WMD and DEV include files.
 *
 *! Revision History:
 *! ================
 *! 12-Nov-1996 gp: Renamed from dev1.h.
 *! 30-May-1996 gp: Broke out from dev.h
 */

#ifndef DEVDEFS_
#define DEVDEFS_

/* WCD Device Object */
	struct DEV_OBJECT;

#endif				/* DEVDEFS_ */
