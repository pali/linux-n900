/*
 * dbldefs.h
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
 *  ======== dbldefs.h ========
 *
 *! Revision History
 *! ================
 *! 19-Mar-2002 jeh     Added DBL_Fxns type (to make it easier to switch
 *!                     between different loaders).
 *! 28-Sep-2001 jeh     Created from zl.h.
 */
#ifndef DBLDEFS_
#define DBLDEFS_

/*
 *  Bit masks for DBL_Flags.
 */
#define DBL_NOLOAD   0x0	/* Don't load symbols, code, or data */
#define DBL_SYMB     0x1	/* load symbols */
#define DBL_CODE     0x2	/* load code */
#define DBL_DATA     0x4	/* load data */
#define DBL_DYNAMIC  0x8	/* dynamic load */
#define DBL_BSS      0x20	/* Unitialized section */

#define DBL_MAXPATHLENGTH       255



/*
 *  ======== DBL_Flags ========
 *  Specifies whether to load code, data, or symbols
 */
typedef s32 DBL_Flags;

/*
 *  ======== DBL_SectInfo ========
 *  For collecting info on overlay sections
 */
struct DBL_SectInfo {
	const char *name;	/* name of section */
	u32 runAddr;		/* run address of section */
	u32 loadAddr;		/* load address of section */
	u32 size;		/* size of section (target MAUs) */
	DBL_Flags type;		/* Code, data, or BSS */
} ;

/*
 *  ======== DBL_Symbol ========
 *  (Needed for dynamic load library)
 */
struct DBL_Symbol {
	u32 value;
};

/*
 *  ======== DBL_AllocFxn ========
 *  Allocate memory function.  Allocate or reserve (if reserved == TRUE)
 *  "size" bytes of memory from segment "space" and return the address in
 *  *dspAddr (or starting at *dspAddr if reserve == TRUE). Returns 0 on
 *  success, or an error code on failure.
 */
typedef s32(*DBL_AllocFxn) (void *hdl, s32 space, u32 size, u32 align,
			u32 *dspAddr, s32 segId, s32 req, bool reserved);



/*
 *  ======== DBL_FreeFxn ========
 *  Free memory function.  Free, or unreserve (if reserved == TRUE) "size"
 *  bytes of memory from segment "space"
 */
typedef bool(*DBL_FreeFxn) (void *hdl, u32 addr, s32 space, u32 size,
			    bool reserved);

/*
 *  ======== DBL_LogWriteFxn ========
 *  Function to call when writing data from a section, to log the info.
 *  Can be NULL if no logging is required.
 */
typedef DSP_STATUS(*DBL_LogWriteFxn) (void *handle, struct DBL_SectInfo *sect,
				      u32 addr, u32 nBytes);


/*
 *  ======== DBL_SymLookup ========
 *  Symbol lookup function - Find the symbol name and return its value.
 *
 *  Parameters:
 *      handle          - Opaque handle
 *      pArg            - Opaque argument.
 *      name            - Name of symbol to lookup.
 *      sym             - Location to store address of symbol structure.
 *
 *  Returns:
 *      TRUE:           Success (symbol was found).
 *      FALSE:          Failed to find symbol.
 */
typedef bool(*DBL_SymLookup) (void *handle, void *pArg, void *rmmHandle,
			      const char *name, struct DBL_Symbol **sym);


/*
 *  ======== DBL_WriteFxn ========
 *  Write memory function.  Write "n" HOST bytes of memory to segment "mtype"
 *  starting at address "dspAddr" from the buffer "buf".  The buffer is
 *  formatted as an array of words appropriate for the DSP.
 */
typedef s32(*DBL_WriteFxn) (void *hdl, u32 dspAddr, void *buf,
			    u32 n, s32 mtype);

/*
 *  ======== DBL_Attrs ========
 */
struct DBL_Attrs {
	DBL_AllocFxn alloc;
	DBL_FreeFxn free;
	void *rmmHandle;	/* Handle to pass to alloc, free functions */
	DBL_WriteFxn write;
	void *wHandle;		/* Handle to pass to write, cinit function */

	DBL_LogWriteFxn logWrite;
	void *logWriteHandle;

	/* Symbol matching function and handle to pass to it */
	DBL_SymLookup symLookup;
	void *symHandle;
	void *symArg;

	/*
	 *  These file manipulation functions should be compatible with the
	 *  "C" run time library functions of the same name.
	 */
	s32(*fread) (void *, size_t, size_t, void *);
	s32(*fseek) (void *, long, int);
	s32(*ftell) (void *);
	s32(*fclose) (void *);
	void *(*fopen) (const char *, const char *);
} ;

#endif				/* DBLDEFS_ */
