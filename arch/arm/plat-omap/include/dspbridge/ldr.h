/*
 * ldr.h
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
 *  ======== ldr.h ========
 *  Purpose:
 *      Provide module loading services and symbol export services.
 *
 *  Public Functions:
 *      LDR_Exit
 *      LDR_FreeModule
 *      LDR_GetProcAddress
 *      LDR_Init
 *      LDR_LoadModule
 *
 *  Notes:
 *      This service is meant to be used by modules of the DSP/BIOS Bridge
 *       class driver.
 *
 *! Revision History:
 *! ================
 *! 22-Nov-1999 kc: Changes from code review.
 *! 12-Nov-1999 kc: Removed declaration of unused loader object.
 *! 29-Oct-1999 kc: Cleaned up for code review.
 *! 12-Jan-1998 cr: Cleaned up for code review.
 *! 04-Aug-1997 cr: Added explicit CDECL identifiers.
 *! 11-Nov-1996 cr: Cleaned up for code review.
 *! 16-May-1996 gp: Created.
 */

#ifndef LDR_
#define LDR_

/* Loader objects: */
	struct LDR_MODULE;

#endif				/* LDR_ */
