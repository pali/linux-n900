/*
 * dehdefs.h
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
 *  ======== dehdefs.h ========
 *  Purpose:
 *      Definition for mini-driver module DEH.
 *
 *! Revision History:
 *! ================
 *! 17-Dec-2001 ag: added #include <dspbridge/mbx_sh.h> for shared mailbox codes.
 *! 10-Dec-2001 kc: added DEH error base value and error max value.
 *! 11-Sep-2001 kc: created.
 */

#ifndef DEHDEFS_
#define DEHDEFS_

#include <dspbridge/mbx_sh.h>		/* shared mailbox codes */

/* DEH object manager */
	struct DEH_MGR;

/* Magic code used to determine if DSP signaled exception. */
#define DEH_BASE        MBX_DEH_BASE
#define DEH_USERS_BASE  MBX_DEH_USERS_BASE
#define DEH_LIMIT       MBX_DEH_LIMIT

#endif				/* _DEHDEFS_H */
