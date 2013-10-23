/*
 * gs.c
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
 *  ======== gs.c ========
 *  Description:
 *      General storage memory allocator services.
 *
 *! Revision History
 *! ================
 *! 29-Sep-1999 ag:  Un-commented MEM_Init in GS_init().
 *! 14-May-1997 mg:  Modified to use new GS API for GS_free() and GS_frees().
 *! 06-Nov-1996 gp:  Re-commented MEM_Init in GS_init(). GS needs GS_Exit().
 *! 21-Oct-1996 db:  Un-commented MEM_Init in GS_init().
 *! 21-May-1996 mg:  Created from original stdlib implementation.
 */

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>
#include <dspbridge/dbdefs.h>
#include <linux/types.h>
/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/mem.h>

/*  ----------------------------------- This */
#include <dspbridge/gs.h>

/*  ----------------------------------- Globals */
static u32 cumsize;

/*
 *  ======== GS_alloc ========
 *  purpose:
 *      Allocates memory of the specified size.
 */
void *GS_alloc(u32 size)
{
	void *p;

	p = MEM_Calloc(size, MEM_PAGED);
	if (p == NULL)
		return NULL;
	cumsize += size;
	return p;
}

/*
 *  ======== GS_exit ========
 *  purpose:
 *      Discontinue the usage of the GS module.
 */
void GS_exit(void)
{
	MEM_Exit();
}

/*
 *  ======== GS_free ========
 *  purpose:
 *      Frees the memory.
 */
void GS_free(void *ptr)
{
	MEM_Free(ptr);
	/* ack! no size info */
	/* cumsize -= size; */
}

/*
 *  ======== GS_frees ========
 *  purpose:
 *      Frees the memory.
 */
void GS_frees(void *ptr, u32 size)
{
	MEM_Free(ptr);
	cumsize -= size;
}

/*
 *  ======== GS_init ========
 *  purpose:
 *      Initializes the GS module.
 */
void GS_init(void)
{
	static bool curInit;

	if (curInit == false) {
		curInit = true;

		MEM_Init();
	}
}
