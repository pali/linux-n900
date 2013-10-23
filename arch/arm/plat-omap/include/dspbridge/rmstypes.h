/*
 * rmstypes.h
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
 *  ======== rmstypes.h ========
 *
 *  DSP/BIOS Bridge Resource Manager Server shared data type definitions.
 *
 *! Revision History
 *! ================
 *! 06-Oct-2000 sg  Added LgFxn type.
 *! 05-Oct-2000 sg  Changed RMS_STATUS to LgUns.
 *! 31-Aug-2000 sg  Added RMS_DSPMSG.
 *! 25-Aug-2000 sg  Initial.
 */

#ifndef RMSTYPES_
#define RMSTYPES_
#include <linux/types.h>
/*
 *  DSP-side definitions.
 */
#include <dspbridge/std.h>
typedef u32 RMS_WORD;
typedef char RMS_CHAR;

#endif				/* RMSTYPES_ */
