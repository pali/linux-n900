/*
 * wcd.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Bridge class driver library functions, object definitions, and
 * return error/status codes.  To be included by Bridge mini drivers.
 *
 * Notes:
 *   Bridge Class Driver services exported to WMD's are initialized by the
 *   WCD on behalf of the WMD.  WMD's must not call module Init/Exit
 *   functions.
 *
 *   To ensure WMD binary compatibility across different platforms,
 *   for the same processor, a WMD must restrict its usage of system
 *   services to those exported by the 'Bridge class library.
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

#ifndef WCD_
#define WCD_

/* This WCD Library Version: */
#define WCD_MAJOR_VERSION   (u32)8	/* .8x - Alpha, .9x - Beta, 1.x FCS */
#define WCD_MINOR_VERSION   (u32)0

#endif /* WCD_ */
