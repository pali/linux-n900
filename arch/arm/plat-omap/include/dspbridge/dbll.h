/*
 * dbll.h
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
 *  ======== dbll.h ========
 *  DSP/BIOS Bridge Dynamic load library module interface. Function header
 *  comments are in the file dblldefs.h.
 *
 *! Revision History
 *! ================
 *! 31-Jul-2002 jeh     Removed function comments (now in dblldefs.h).
 *! 17-Apr-2002 jeh     Created based on zl.h.
 */

#ifndef DBLL_
#define DBLL_

#include <dspbridge/dbdefs.h>
#include <dspbridge/dblldefs.h>

	extern void DBLL_close(struct DBLL_LibraryObj *lib);
	extern DSP_STATUS DBLL_create(struct DBLL_TarObj **pTarget,
				      struct DBLL_Attrs *pAttrs);
	extern void DBLL_delete(struct DBLL_TarObj *target);
	extern void DBLL_exit(void);
	extern bool DBLL_getAddr(struct DBLL_LibraryObj *lib, char *name,
				 struct DBLL_Symbol **ppSym);
	extern void DBLL_getAttrs(struct DBLL_TarObj *target,
				  struct DBLL_Attrs *pAttrs);
	extern bool DBLL_getCAddr(struct DBLL_LibraryObj *lib, char *name,
				  struct DBLL_Symbol **ppSym);
	extern DSP_STATUS DBLL_getSect(struct DBLL_LibraryObj *lib, char *name,
				       u32 *pAddr, u32 *pSize);
	extern bool DBLL_init(void);
	extern DSP_STATUS DBLL_load(struct DBLL_LibraryObj *lib,
				    DBLL_Flags flags,
				    struct DBLL_Attrs *attrs, u32 *pEntry);
	extern DSP_STATUS DBLL_loadSect(struct DBLL_LibraryObj *lib,
					char *sectName,
					struct DBLL_Attrs *attrs);
	extern DSP_STATUS DBLL_open(struct DBLL_TarObj *target, char *file,
				    DBLL_Flags flags,
				    struct DBLL_LibraryObj **pLib);
	extern DSP_STATUS DBLL_readSect(struct DBLL_LibraryObj *lib,
					char *name,
					char *pBuf, u32 size);
	extern void DBLL_setAttrs(struct DBLL_TarObj *target,
				  struct DBLL_Attrs *pAttrs);
	extern void DBLL_unload(struct DBLL_LibraryObj *lib,
				struct DBLL_Attrs *attrs);
	extern DSP_STATUS DBLL_unloadSect(struct DBLL_LibraryObj *lib,
					  char *sectName,
					  struct DBLL_Attrs *attrs);

#endif				/* DBLL_ */

