/*
 * msgdefs.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Global msg_ctrl constants and types.
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

#ifndef MSGDEFS_
#define MSGDEFS_

#define MSGMGR_SIGNATURE    0x4d47534d	/* "MGSM" */

/* msg_ctrl Objects: */
struct msg_mgr;
struct msg_queue;

/* Function prototype for callback to be called on RMS_EXIT message received */
typedef void (*msg_onexit) (bhandle h, s32 nStatus);

#endif /* MSGDEFS_ */
