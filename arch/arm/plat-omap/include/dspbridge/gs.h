/*
 * gs.h
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
 *  ======== gs.h ========
 *  Memory allocation/release wrappers.  This module allows clients to
 *  avoid OS spacific issues related to memory allocation.  It also provides
 *  simple diagnostic capabilities to assist in the detection of memory
 *  leaks.
 *! Revision History
 *! ================
 */

#ifndef GS_
#define GS_

/*
 *  ======== GS_alloc ========
 *  Alloc size bytes of space.  Returns pointer to space
 *  allocated, otherwise NULL.
 */
extern void *GS_alloc(u32 size);

/*
 *  ======== GS_exit ========
 *  Module exit.  Do not change to "#define GS_init()"; in
 *  some environments this operation must actually do some work!
 */
extern void GS_exit(void);

/*
 *  ======== GS_free ========
 *  Free space allocated by GS_alloc() or GS_calloc().
 */
extern void GS_free(void *ptr);

/*
 *  ======== GS_frees ========
 *  Free space allocated by GS_alloc() or GS_calloc() and assert that
 *  the size of the allocation is size bytes.
 */
extern void GS_frees(void *ptr, u32 size);

/*
 *  ======== GS_init ========
 *  Module initialization.  Do not change to "#define GS_init()"; in
 *  some environments this operation must actually do some work!
 */
extern void GS_init(void);

#endif				/*GS_ */
