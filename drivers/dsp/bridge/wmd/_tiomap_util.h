/*
 * _tiomap_util.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Definitions and types for the utility routines.
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

#ifndef _TIOMAP_UTIL_
#define _TIOMAP_UTIL_

/* Time out Values in uSeconds */
#define TIHELEN_ACKTIMEOUT  10000

/*
 * Time out for power state transition (in msecs), due to system
 * latencies and HZ resolution this timer can vary.
 */
#define PWRSTST_TIMEOUT		200

/*  Time delay for HOM->SAM transition. */
#define  WAIT_SAM   1000000	/* in usec (1000 millisec) */

/*
 *  ======== wait_for_start ========
 *  Wait for the singal from DSP that it has started, or time out.
 *  The argument dw_sync_addr is set to 1 before releasing the DSP.
 *  If the DSP starts running, it will clear this location.
 */
extern bool wait_for_start(struct wmd_dev_context *dev_context,
			   u32 dw_sync_addr);

#endif /* _TIOMAP_UTIL_ */
