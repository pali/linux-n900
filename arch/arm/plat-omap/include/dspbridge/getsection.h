/*
 * getsection.h
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


#ifndef _GETSECTION_H_
#define _GETSECTION_H_


#include "dynamic_loader.h"

/*
 * Get Section Information
 *
 * This file provides an API add-on to the dynamic loader that allows the user
 * to query section information and extract section data from dynamic load
 * modules.
 *
 * NOTE:
 * Functions in this API assume that the supplied Dynamic_Loader_Stream object
 * supports the set_file_posn method.
 */

	/* opaque handle for module information */
	typedef void *DLOAD_module_info;

/*
 * Procedure DLOAD_module_open
 *
 * Parameters:
 *  module  The input stream that supplies the module image
 *  syms    Host-side malloc/free and error reporting functions.
 *          Other methods are unused.
 *
 * Effect:
 *  Reads header information from a dynamic loader module using the specified
 * stream object, and returns a handle for the module information.  This
 * handle may be used in subsequent query calls to obtain information
 * contained in the module.
 *
 * Returns:
 *  NULL if an error is encountered, otherwise a module handle for use
 * in subsequent operations.
 */
	extern DLOAD_module_info DLOAD_module_open(struct Dynamic_Loader_Stream
						   *module,
						   struct Dynamic_Loader_Sym
						   *syms);

/*
 * Procedure DLOAD_GetSectionInfo
 *
 * Parameters:
 *  minfo       Handle from DLOAD_module_open for this module
 *  sectionName Pointer to the string name of the section desired
 *  sectionInfo Address of a section info structure pointer to be initialized
 *
 * Effect:
 *  Finds the specified section in the module information, and fills in
 * the provided LDR_SECTION_INFO structure.
 *
 * Returns:
 *  TRUE for success, FALSE for section not found
 */
	extern int DLOAD_GetSectionInfo(DLOAD_module_info minfo,
					const char *sectionName,
					const struct LDR_SECTION_INFO
					** const sectionInfo);

/*
 * Procedure DLOAD_GetSection
 *
 * Parameters:
 *  minfo       Handle from DLOAD_module_open for this module
 *  sectionInfo Pointer to a section info structure for the desired section
 *  sectionData Buffer to contain the section initialized data
 *
 * Effect:
 *  Copies the initialized data for the specified section into the
 * supplied buffer.
 *
 * Returns:
 *  TRUE for success, FALSE for section not found
 */
	extern int DLOAD_GetSection(DLOAD_module_info minfo,
				    const struct LDR_SECTION_INFO *sectionInfo,
				    void *sectionData);

/*
 * Procedure DLOAD_module_close
 *
 * Parameters:
 *  minfo       Handle from DLOAD_module_open for this module
 *
 * Effect:
 *  Releases any storage associated with the module handle.  On return,
 * the module handle is invalid.
 *
 * Returns:
 *  Zero for success. On error, the number of errors detected is returned.
 * Individual errors are reported using syms->Error_Report(), where syms was
 * an argument to DLOAD_module_open
 */
	extern void DLOAD_module_close(DLOAD_module_info minfo);

#endif				/* _GETSECTION_H_ */
