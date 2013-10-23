/*
 * dbg.h
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
 *  ======== dbg.h ========
 *  Purpose:
 *      Provide debugging services for 'Bridge Mini Drivers.
 *
 *  Public Functions:
 *      DBG_Exit
 *      DBG_Init
 *      DBG_Printf
 *      DBG_Trace
 *
 *  Notes:
 *      WMD's must not call DBG_Init or DBG_Exit.
 *
 *! Revision History:
 *! ================
 *! 03-Feb-2000 rr: DBG Levels redefined.
 *! 29-Oct-1999 kc: Cleaned up for code review.
 *! 10-Oct-1997 cr: Added DBG_Printf service.
 *! 29-May-1996 gp: Removed WCD_ prefix.
 *! 15-May-1996 gp: Created.
 */

#ifndef DBG_
#define DBG_
#include <dspbridge/host_os.h>
#include <linux/types.h>

/* Levels of trace debug messages: */
#define DBG_ENTER   (u8)(0x01)	/* Function entry point. */
#define DBG_LEVEL1  (u8)(0x02)	/* Display debugging state/varibles */
#define DBG_LEVEL2  (u8)(0x04)	/* Display debugging state/varibles */
#define DBG_LEVEL3  (u8)(0x08)	/* Display debugging state/varibles */
#define DBG_LEVEL4  (u8)(0x10)	/* Display debugging state/varibles */
#define DBG_LEVEL5  (u8)(0x20)	/* Module Init, Exit */
#define DBG_LEVEL6  (u8)(0x40)	/* Warn SERVICES Failures */
#define DBG_LEVEL7  (u8)(0x80)	/* Warn Critical Errors */

#if (defined(DEBUG) || defined(DDSP_DEBUG_PRODUCT)) && GT_TRACE

/*
 *  ======== DBG_Exit ========
 *  Purpose:
 *      Discontinue usage of module; free resources when reference count
 *      reaches 0.
 *  Parameters:
 *  Returns:
 *  Requires:
 *      DBG initialized.
 *  Ensures:
 *      Resources used by module are freed when cRef reaches zero.
 */
	extern void DBG_Exit(void);

/*
 *  ======== DBG_Init ========
 *  Purpose:
 *      Initializes private state of DBG module.
 *  Parameters:
 *  Returns:
 *      TRUE if initialized; FALSE if error occured.
 *  Requires:
 *  Ensures:
 */
	extern bool DBG_Init(void);

/*
 *  ======== DBG_Trace ========
 *  Purpose:
 *      Output a trace message to the debugger, if the given trace level
 *      is unmasked.
 *  Parameters:
 *      bLevel:         Trace level.
 *      pstrFormat:     sprintf-style format string.
 *      ...:            Arguments for format string.
 *  Returns:
 *      DSP_SOK:        Success, or trace level masked.
 *      DSP_EFAIL:      On Error.
 *  Requires:
 *      DBG initialized.
 *  Ensures:
 *      Debug message is printed to debugger output window, if trace level
 *      is unmasked.
 */
	extern DSP_STATUS DBG_Trace(IN u8 bLevel, IN char *pstrFormat, ...);
#else

#define DBG_Exit(void)
#define DBG_Init(void) true
#define DBG_Trace(bLevel, pstrFormat, args...)

#endif	     /* (defined(DEBUG) || defined(DDSP_DEBUG_PRODUCT)) && GT_TRACE */

#endif				/* DBG_ */
