/*
 * gh.h
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
 *  ======== gh.h ========
 *
 *! Revision History
 *! ================
 */

#ifndef GH_
#define GH_
#include <dspbridge/host_os.h>

extern struct GH_THashTab *GH_create(u16 maxBucket, u16 valSize,
		u16(*hash) (void *, u16), bool(*match) (void *, void *),
		void(*delete) (void *));
extern void GH_delete(struct GH_THashTab *hashTab);
extern void GH_exit(void);
extern void *GH_find(struct GH_THashTab *hashTab, void *key);
extern void GH_init(void);
extern void *GH_insert(struct GH_THashTab *hashTab, void *key, void *value);
#endif				/* GH_ */
