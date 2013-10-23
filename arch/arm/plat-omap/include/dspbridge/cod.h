/*
 * cod.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Code management module for DSPs. This module provides an interface
 * interface for loading both static and dynamic code objects onto DSP
 * systems.
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

#ifndef COD_
#define COD_

#include <dspbridge/dblldefs.h>

#define COD_MAXPATHLENGTH       255
#define COD_TRACEBEG            "SYS_PUTCBEG"
#define COD_TRACEEND            "SYS_PUTCEND"
#define COD_TRACESECT           "trace"
#define COD_TRACEBEGOLD         "PUTCBEG"
#define COD_TRACEENDOLD         "PUTCEND"

#define COD_NOLOAD              DBLL_NOLOAD
#define COD_SYMB                DBLL_SYMB

/* Flags passed to cod_open */
typedef dbll_flags cod_flags;

/* COD code manager handle */
struct cod_manager;

/* COD library handle */
struct cod_libraryobj;

/* COD attributes */
struct cod_attrs {
	u32 ul_reserved;
};

/*
 *  Function prototypes for writing memory to a DSP system, allocating
 *  and freeing DSP memory.
 */
typedef u32(*cod_writefxn) (void *priv_ref, u32 ulDspAddr,
			    void *pbuf, u32 ul_num_bytes, u32 nMemSpace);

/*
 *  ======== cod_close ========
 *  Purpose:
 *      Close a library opened with cod_open().
 *  Parameters:
 *      lib             - Library handle returned by cod_open().
 *  Returns:
 *      None.
 *  Requires:
 *      COD module initialized.
 *      valid lib.
 *  Ensures:
 *
 */
extern void cod_close(struct cod_libraryobj *lib);

/*
 *  ======== cod_create ========
 *  Purpose:
 *      Create an object to manage code on a DSP system. This object can be
 *      used to load an initial program image with arguments that can later
 *      be expanded with dynamically loaded object files.
 *      Symbol table information is managed by this object and can be retrieved
 *      using the cod_get_sym_value() function.
 *  Parameters:
 *      phManager:      created manager object
 *      pstrZLFile:     ZL DLL filename, of length < COD_MAXPATHLENGTH.
 *      attrs:          attributes to be used by this object. A NULL value
 *                      will cause default attrs to be used.
 *  Returns:
 *      DSP_SOK:                Success.
 *      COD_E_NOZLFUNCTIONS:    Could not initialize ZL functions.
 *      COD_E_ZLCREATEFAILED:   ZL_Create failed.
 *      DSP_ENOTIMPL:           attrs was not NULL.  We don't yet support
 *                              non default values of attrs.
 *  Requires:
 *      COD module initialized.
 *      pstrZLFile != NULL
 *  Ensures:
 */
extern dsp_status cod_create(OUT struct cod_manager **phManager,
			     char *pstrZLFile,
			     IN OPTIONAL CONST struct cod_attrs *attrs);

/*
 *  ======== cod_delete ========
 *  Purpose:
 *      Delete a code manager object.
 *  Parameters:
 *      cod_mgr_obj:   handle of manager to be deleted
 *  Returns:
 *      None.
 *  Requires:
 *      COD module initialized.
 *      valid cod_mgr_obj.
 *  Ensures:
 */
extern void cod_delete(struct cod_manager *cod_mgr_obj);

/*
 *  ======== cod_exit ========
 *  Purpose:
 *      Discontinue usage of the COD module.
 *  Parameters:
 *      None.
 *  Returns:
 *      None.
 *  Requires:
 *      COD initialized.
 *  Ensures:
 *      Resources acquired in cod_init(void) are freed.
 */
extern void cod_exit(void);

/*
 *  ======== cod_get_base_lib ========
 *  Purpose:
 *      Get handle to the base image DBL library.
 *  Parameters:
 *      cod_mgr_obj:   handle of manager to be deleted
 *      plib:       location to store library handle on output.
 *  Returns:
 *      DSP_SOK:    Success.
 *  Requires:
 *      COD module initialized.
 *      valid cod_mgr_obj.
 *      plib != NULL.
 *  Ensures:
 */
extern dsp_status cod_get_base_lib(struct cod_manager *cod_mgr_obj,
				   struct dbll_library_obj **plib);

/*
 *  ======== cod_get_base_name ========
 *  Purpose:
 *      Get the name of the base image DBL library.
 *  Parameters:
 *      cod_mgr_obj:   handle of manager to be deleted
 *      pszName:    location to store library name on output.
 *      usize:       size of name buffer.
 *  Returns:
 *      DSP_SOK:    Success.
 *      DSP_EFAIL:  Buffer too small.
 *  Requires:
 *      COD module initialized.
 *      valid cod_mgr_obj.
 *      pszName != NULL.
 *  Ensures:
 */
extern dsp_status cod_get_base_name(struct cod_manager *cod_mgr_obj,
				    char *pszName, u32 usize);

/*
 *  ======== cod_get_entry ========
 *  Purpose:
 *      Retrieve the entry point of a loaded DSP program image
 *  Parameters:
 *      cod_mgr_obj:   handle of manager to be deleted
 *      pulEntry:   pointer to location for entry point
 *  Returns:
 *      DSP_SOK:       Success.
 *  Requires:
 *      COD module initialized.
 *      valid cod_mgr_obj.
 *      pulEntry != NULL.
 *  Ensures:
 */
extern dsp_status cod_get_entry(struct cod_manager *cod_mgr_obj,
				u32 *pulEntry);

/*
 *  ======== cod_get_loader ========
 *  Purpose:
 *      Get handle to the DBL loader.
 *  Parameters:
 *      cod_mgr_obj:   handle of manager to be deleted
 *      phLoader:   location to store loader handle on output.
 *  Returns:
 *      DSP_SOK:    Success.
 *  Requires:
 *      COD module initialized.
 *      valid cod_mgr_obj.
 *      phLoader != NULL.
 *  Ensures:
 */
extern dsp_status cod_get_loader(struct cod_manager *cod_mgr_obj,
				 struct dbll_tar_obj **phLoader);

/*
 *  ======== cod_get_section ========
 *  Purpose:
 *      Retrieve the starting address and length of a section in the COFF file
 *      given the section name.
 *  Parameters:
 *      lib         Library handle returned from cod_open().
 *      pstrSect:   name of the section, with or without leading "."
 *      puAddr:     Location to store address.
 *      puLen:      Location to store length.
 *  Returns:
 *      DSP_SOK:                Success
 *      COD_E_NOSYMBOLSLOADED:  Symbols have not been loaded onto the board.
 *      COD_E_SYMBOLNOTFOUND:   The symbol could not be found.
 *  Requires:
 *      COD module initialized.
 *      valid cod_mgr_obj.
 *      pstrSect != NULL;
 *      puAddr != NULL;
 *      puLen != NULL;
 *  Ensures:
 *      DSP_SOK:  *puAddr and *puLen contain the address and length of the
 *                 section.
 *      else:  *puAddr == 0 and *puLen == 0;
 *
 */
extern dsp_status cod_get_section(struct cod_libraryobj *lib,
				  IN char *pstrSect,
				  OUT u32 *puAddr, OUT u32 *puLen);

/*
 *  ======== cod_get_sym_value ========
 *  Purpose:
 *      Retrieve the value for the specified symbol. The symbol is first
 *      searched for literally and then, if not found, searched for as a
 *      C symbol.
 *  Parameters:
 *      lib:        library handle returned from cod_open().
 *      pstrSymbol: name of the symbol
 *      value:      value of the symbol
 *  Returns:
 *      DSP_SOK:                Success.
 *      COD_E_NOSYMBOLSLOADED:  Symbols have not been loaded onto the board.
 *      COD_E_SYMBOLNOTFOUND:   The symbol could not be found.
 *  Requires:
 *      COD module initialized.
 *      Valid cod_mgr_obj.
 *      pstrSym != NULL.
 *      pul_value != NULL.
 *  Ensures:
 */
extern dsp_status cod_get_sym_value(struct cod_manager *cod_mgr_obj,
				    IN char *pstrSym, OUT u32 * pul_value);

/*
 *  ======== cod_init ========
 *  Purpose:
 *      Initialize the COD module's private state.
 *  Parameters:
 *      None.
 *  Returns:
 *      TRUE if initialized; FALSE if error occured.
 *  Requires:
 *  Ensures:
 *      A requirement for each of the other public COD functions.
 */
extern bool cod_init(void);

/*
 *  ======== cod_load_base ========
 *  Purpose:
 *      Load the initial program image, optionally with command-line arguments,
 *      on the DSP system managed by the supplied handle. The program to be
 *      loaded must be the first element of the args array and must be a fully
 *      qualified pathname.
 *  Parameters:
 *      hmgr:       manager to load the code with
 *      nArgc:      number of arguments in the args array
 *      args:       array of strings for arguments to DSP program
 *      write_fxn:   board-specific function to write data to DSP system
 *      pArb:       arbitrary pointer to be passed as first arg to write_fxn
 *      envp:       array of environment strings for DSP exec.
 *  Returns:
 *      DSP_SOK:                   Success.
 *      COD_E_OPENFAILED:       Failed to open target code.
 *      COD_E_LOADFAILED:       Failed to load code onto target.
 *  Requires:
 *      COD module initialized.
 *      hmgr is valid.
 *      nArgc > 0.
 *      aArgs != NULL.
 *      aArgs[0] != NULL.
 *      pfn_write != NULL.
 *  Ensures:
 */
extern dsp_status cod_load_base(struct cod_manager *cod_mgr_obj,
				u32 nArgc, char *aArgs[],
				cod_writefxn pfn_write, void *pArb,
				char *envp[]);

/*
 *  ======== cod_open ========
 *  Purpose:
 *      Open a library for reading sections. Does not load or set the base.
 *  Parameters:
 *      hmgr:           manager to load the code with
 *      pszCoffPath:    Coff file to open.
 *      flags:          COD_NOLOAD (don't load symbols) or COD_SYMB (load
 *                      symbols).
 *      pLib:           Handle returned that can be used in calls to cod_close
 *                      and cod_get_section.
 *  Returns:
 *      S_OK:                   Success.
 *      COD_E_OPENFAILED:       Failed to open target code.
 *  Requires:
 *      COD module initialized.
 *      hmgr is valid.
 *      flags == COD_NOLOAD || flags == COD_SYMB.
 *      pszCoffPath != NULL.
 *  Ensures:
 */
extern dsp_status cod_open(struct cod_manager *hmgr,
			   IN char *pszCoffPath,
			   cod_flags flags, OUT struct cod_libraryobj **pLib);

/*
 *  ======== cod_open_base ========
 *  Purpose:
 *      Open base image for reading sections. Does not load the base.
 *  Parameters:
 *      hmgr:           manager to load the code with
 *      pszCoffPath:    Coff file to open.
 *      flags:          Specifies whether to load symbols.
 *  Returns:
 *      DSP_SOK:            Success.
 *      COD_E_OPENFAILED:   Failed to open target code.
 *  Requires:
 *      COD module initialized.
 *      hmgr is valid.
 *      pszCoffPath != NULL.
 *  Ensures:
 */
extern dsp_status cod_open_base(struct cod_manager *hmgr, IN char *pszCoffPath,
				dbll_flags flags);

/*
 *  ======== cod_read_section ========
 *  Purpose:
 *      Retrieve the content of a code section given the section name.
 *  Parameters:
 *      cod_mgr_obj    - manager in which to search for the symbol
 *      pstrSect    - name of the section, with or without leading "."
 *      pstrContent - buffer to store content of the section.
 *  Returns:
 *      DSP_SOK: on success, error code on failure
 *      COD_E_NOSYMBOLSLOADED:  Symbols have not been loaded onto the board.
 *      COD_E_READFAILED: Failed to read content of code section.
 *  Requires:
 *      COD module initialized.
 *      valid cod_mgr_obj.
 *      pstrSect != NULL;
 *      pstrContent != NULL;
 *  Ensures:
 *      DSP_SOK:  *pstrContent stores the content of the named section.
 */
extern dsp_status cod_read_section(struct cod_libraryobj *lib,
				   IN char *pstrSect,
				   OUT char *pstrContent, IN u32 cContentSize);

#endif /* COD_ */
