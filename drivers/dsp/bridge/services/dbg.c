/*
 * dbg.c
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
 *  ======== dbgce.c ========
 *  Purpose:
 *      Provide debugging services for DSP/BIOS Bridge Mini Drivers.
 *
 *  Public Functions:
 *      DBG_Exit
 *      DBG_Init
 *      DBG_Trace
 *
 *  Notes:
 *      Requires gt.h.
 *
 *      This implementation does not create GT masks on a per WMD basis.
 *      There is currently no facility for a WMD to alter the GT mask.
 *
 *! Revision History:
 *! ================
 *! 15-Feb-2000 rr: DBG_Trace prints based on the DebugZones.
 *! 03-Feb-2000 rr: Module init/exit is handled by SERVICES Init/Exit.
 *!		 GT Changes.
 *! 29-Oct-1999 kc: Cleaned up for code review.
 *! 10-Oct-1997 cr: Added DBG_Printf service.
 *! 28-May-1997 cr: Added reference counting.
 *! 23-May-1997 cr: Updated DBG_Trace to new gt interface.
 *! 29-May-1996 gp: Removed WCD_ prefix.
 *! 20-May-1996 gp: Remove DEBUG conditional compilation.
 *! 15-May-1996 gp: Created.
 */

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>
#include <dspbridge/dbdefs.h>
#include <dspbridge/errbase.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>
#include <dspbridge/gt.h>

/*  ----------------------------------- This */
#include <dspbridge/dbg.h>

/*  ----------------------------------- Globals */
#if GT_TRACE
static struct GT_Mask DBG_debugMask = { NULL, NULL };	/* GT trace var. */
#endif

#if (defined(DEBUG) || defined (DDSP_DEBUG_PRODUCT)) && GT_TRACE

/*
 *  ======== DBG_Init ========
 *  Purpose:
 *      Ensures trace capability is set up for link drivers.
 */
bool DBG_Init(void)
{
	GT_create(&DBG_debugMask, "WD");     /* for WmD (link driver) debug */

	GT_0trace(DBG_debugMask, GT_5CLASS, "DBG_Init\n");

	return true;
}

/*
 *  ======== DBG_Trace ========
 *  Purpose:
 *      Output a trace message to the debugger, if the given trace level
 *      is unmasked.
 */
DSP_STATUS DBG_Trace(u8 bLevel, char *pstrFormat, ...)
{
	s32 arg1, arg2, arg3, arg4, arg5, arg6;
	va_list va;

	va_start(va, pstrFormat);

	arg1 = va_arg(va, s32);
	arg2 = va_arg(va, s32);
	arg3 = va_arg(va, s32);
	arg4 = va_arg(va, s32);
	arg5 = va_arg(va, s32);
	arg6 = va_arg(va, s32);

	va_end(va);

	if (bLevel & *(DBG_debugMask).flags)
		printk(pstrFormat, arg1, arg2, arg3, arg4, arg5, arg6);

	return DSP_SOK;
}

/*
 *  ======== DBG_Exit ========
 *  Purpose:
 *      Discontinue usage of the DBG module.
 */
void DBG_Exit(void)
{
	GT_0trace(DBG_debugMask, GT_5CLASS, "DBG_Exit\n");
}

#endif	/* (defined(DEBUG) || defined(DDSP_DEBUG_PRODUCT)) && GT_TRACE */
