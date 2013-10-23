/*
 * dbl.h
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
 *  ======== dbl.h ========
 *
 *! Revision History
 *! ================
 *! 19-Mar-2002 jeh     Pass DBL_Symbol pointer to DBL_getAddr, DBL_getCAddr
 *!                     to accomodate dynamic loader library.
 *! 20-Nov-2001 jeh     Removed DBL_loadArgs().
 *! 24-Sep-2001 jeh     Code review changes.
 *! 07-Sep-2001 jeh     Added DBL_LoadSect(), DBL_UnloadSect().
 *! 05-Jun-2001 jeh     Created based on zl.h.
 */

#ifndef DBL_
#define DBL_

#include <dspbridge/dbdefs.h>
#include <dspbridge/dbldefs.h>

/*
 *  ======== DBL_close ========
 *  Close library opened with DBL_open.
 *  Parameters:
 *      lib             - Handle returned from DBL_open().
 *  Returns:
 *  Requires:
 *      DBL initialized.
 *      Valid lib.
 *  Ensures:
 */
	extern void DBL_close(struct DBL_LibraryObj *lib);

/*
 *  ======== DBL_create ========
 *  Create a target object by specifying the alloc, free, and write
 *  functions for the target.
 *  Parameters:
 *      pTarget         - Location to store target handle on output.
 *      pAttrs          - Attributes.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EMEMORY:    Memory allocation failed.
 *  Requires:
 *      DBL initialized.
 *      pAttrs != NULL.
 *      pTarget != NULL;
 *  Ensures:
 *      Success:        *pTarget != NULL.
 *      Failure:        *pTarget == NULL.
 */
	extern DSP_STATUS DBL_create(struct DBL_TargetObj **pTarget,
				     struct DBL_Attrs *pAttrs);

/*
 *  ======== DBL_delete ========
 *  Delete target object and free resources for any loaded libraries.
 *  Parameters:
 *      target          - Handle returned from DBL_Create().
 *  Returns:
 *  Requires:
 *      DBL initialized.
 *      Valid target.
 *  Ensures:
 */
	extern void DBL_delete(struct DBL_TargetObj *target);

/*
 *  ======== DBL_exit ========
 *  Discontinue use of DBL module.
 *  Parameters:
 *  Returns:
 *  Requires:
 *      cRefs > 0.
 *  Ensures:
 *      cRefs >= 0.
 */
	extern void DBL_exit(void);

/*
 *  ======== DBL_getAddr ========
 *  Get address of name in the specified library.
 *  Parameters:
 *      lib             - Handle returned from DBL_open().
 *      name            - Name of symbol
 *      ppSym           - Location to store symbol address on output.
 *  Returns:
 *      TRUE:           Success.
 *      FALSE:          Symbol not found.
 *  Requires:
 *      DBL initialized.
 *      Valid lib.
 *      name != NULL.
 *      pAddr != NULL.
 *  Ensures:
 */
	extern bool DBL_getAddr(struct DBL_LibraryObj *lib, char *name,
				struct DBL_Symbol **ppSym);

/*
 *  ======== DBL_getAttrs ========
 *  Retrieve the attributes of the target.
 *  Parameters:
 *      target          - Handle returned from DBL_Create().
 *      pAttrs          - Location to store attributes on output.
 *  Returns:
 *  Requires:
 *      DBL initialized.
 *      Valid target.
 *      pAttrs != NULL.
 *  Ensures:
 */
	extern void DBL_getAttrs(struct DBL_TargetObj *target,
				 struct DBL_Attrs *pAttrs);

/*
 *  ======== DBL_getCAddr ========
 *  Get address of "C" name in the specified library.
 *  Parameters:
 *      lib             - Handle returned from DBL_open().
 *      name            - Name of symbol
 *      ppSym           - Location to store symbol address on output.
 *  Returns:
 *      TRUE:           Success.
 *      FALSE:          Symbol not found.
 *  Requires:
 *      DBL initialized.
 *      Valid lib.
 *      name != NULL.
 *      pAddr != NULL.
 *  Ensures:
 */
	extern bool DBL_getCAddr(struct DBL_LibraryObj *lib, char *name,
				 struct DBL_Symbol **ppSym);

/*
 *  ======== DBL_getEntry ========
 *  Get program entry point.
 *
 *  Parameters:
 *      lib             - Handle returned from DBL_open().
 *      pEntry          - Location to store entry address on output.
 *  Returns:
 *      TRUE:           Success.
 *      FALSE:          Failure.
 *  Requires:
 *      DBL initialized.
 *      Valid lib.
 *      pEntry != NULL.
 *  Ensures:
 */
	extern bool DBL_getEntry(struct DBL_LibraryObj *lib, u32 *pEntry);

/*
 *  ======== DBL_getSect ========
 *  Get address and size of a named section.
 *  Parameters:
 *      lib             - Library handle returned from DBL_open().
 *      name            - Name of section.
 *      pAddr           - Location to store section address on output.
 *      pSize           - Location to store section size on output.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_ENOSECT:    Section not found.
 *  Requires:
 *      DBL initialized.
 *      Valid lib.
 *      name != NULL.
 *      pAddr != NULL;
 *      pSize != NULL.
 *  Ensures:
 */
	extern DSP_STATUS DBL_getSect(struct DBL_LibraryObj *lib, char *name,
				      u32 *pAddr, u32 *pSize);

/*
 *  ======== DBL_init ========
 *  Initialize DBL module.
 *  Parameters:
 *  Returns:
 *      TRUE:           Success.
 *      FALSE:          Failure.
 *  Requires:
 *      cRefs >= 0.
 *  Ensures:
 *      Success:        cRefs > 0.
 *      Failure:        cRefs >= 0.
 */
	extern bool DBL_init(void);

/*
 *  ======== DBL_load ========
 *  Add symbols/code/data defined in file to that already present on
 *  the target.
 *
 *  Parameters:
 *      lib             - Library handle returned from DBL_open().
 *      flags           - Specifies whether loading code, data, and/or symbols.
 *      attrs           - May contain write, alloc, and free functions.
 *      pulEntry        - Location to store program entry on output.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EFREAD:     File read failed.
 *      DSP_EFWRITE:    Write to target failed.
 *  Requires:
 *      DBL initialized.
 *      Valid lib.
 *      pEntry != NULL.
 *  Ensures:
 */
	extern DSP_STATUS DBL_load(struct DBL_LibraryObj *lib, DBL_Flags flags,
				   struct DBL_Attrs *attrs, u32 *pEntry);

/*
 *  ======== DBL_loadSect ========
 *  Load a named section from an library (for overlay support).
 *  Parameters:
 *      lib             - Handle returned from DBL_open().
 *      sectName        - Name of section to load.
 *      attrs           - Contains write function and handle to pass to it.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_ENOSECT:    Section not found.
 *      DSP_EFWRITE:    Write function failed.
 *  Requires:
 *      Valid lib.
 *      sectName != NULL.
 *      attrs != NULL.
 *      attrs->write != NULL.
 *  Ensures:
 */
	extern DSP_STATUS DBL_loadSect(struct DBL_LibraryObj *lib,
				       char *sectName,
				       struct DBL_Attrs *attrs);

/*
 *  ======== DBL_open ========
 *  DBL_open() returns a library handle that can be used to load/unload
 *  the symbols/code/data via DBL_load()/DBL_unload().
 *  Parameters:
 *      target          - Handle returned from DBL_create().
 *      file            - Name of file to open.
 *      flags           - Specifies whether to load symbols now.
 *      pLib            - Location to store library handle on output.
 *  Returns:
 *      DSP_SOK:            Success.
 *      DSP_EMEMORY:        Memory allocation failure.
 *      DSP_EFOPEN:         File open failure.
 *      DSP_EFREAD:         File read failure.
 *      DSP_ECORRUPTFILE:   Unable to determine target type.
 *  Requires:
 *      DBL initialized.
 *      Valid target.
 *      file != NULL.
 *      pLib != NULL.
 *      struct DBL_Attrs fopen function non-NULL.
 *  Ensures:
 *      Success:        Valid *pLib.
 *      Failure:        *pLib == NULL.
 */
	extern DSP_STATUS DBL_open(struct DBL_TargetObj *target, char *file,
				   DBL_Flags flags,
				   struct DBL_LibraryObj **pLib);

/*
 *  ======== DBL_readSect ========
 *  Read COFF section into a character buffer.
 *  Parameters:
 *      lib             - Library handle returned from DBL_open().
 *      name            - Name of section.
 *      pBuf            - Buffer to write section contents into.
 *      size            - Buffer size
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_ENOSECT:    Named section does not exists.
 *  Requires:
 *      DBL initialized.
 *      Valid lib.
 *      name != NULL.
 *      pBuf != NULL.
 *      size != 0.
 *  Ensures:
 */
	extern DSP_STATUS DBL_readSect(struct DBL_LibraryObj *lib, char *name,
				       char *pBuf, u32 size);

/*
 *  ======== DBL_setAttrs ========
 *  Set the attributes of the target.
 *  Parameters:
 *      target          - Handle returned from DBL_create().
 *      pAttrs          - New attributes.
 *  Returns:
 *  Requires:
 *      DBL initialized.
 *      Valid target.
 *      pAttrs != NULL.
 *  Ensures:
 */
	extern void DBL_setAttrs(struct DBL_TargetObj *target,
				 struct DBL_Attrs *pAttrs);

/*
 *  ======== DBL_unload ========
 *  Remove the symbols/code/data corresponding to the library lib.
 *  Parameters:
 *      lib             - Handle returned from DBL_open().
 *      attrs           - Contains free() function and handle to pass to it.
 *  Returns:
 *  Requires:
 *      DBL initialized.
 *      Valid lib.
 *  Ensures:
 */
	extern void DBL_unload(struct DBL_LibraryObj *lib,
			       struct DBL_Attrs *attrs);

/*
 *  ======== DBL_unloadSect ========
 *  Unload a named section from an library (for overlay support).
 *  Parameters:
 *      lib             - Handle returned from DBL_open().
 *      sectName        - Name of section to load.
 *      attrs           - Contains free() function and handle to pass to it.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_ENOSECT:    Named section not found.
 *  Requires:
 *      DBL initialized.
 *      Valid lib.
 *      sectName != NULL.
 *  Ensures:
 */
	extern DSP_STATUS DBL_unloadSect(struct DBL_LibraryObj *lib,
					 char *sectName,
					 struct DBL_Attrs *attrs);

#endif				/* DBL_ */
