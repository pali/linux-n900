/*
 * cod.h
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
 *  ======== cod.h ========
 *  Description:
 *      Code management module for DSPs. This module provides an interface
 *      interface for loading both static and dynamic code objects onto DSP
 *      systems.
 *
 *  Public Functions:
 *      COD_Close
 *      COD_Create
 *      COD_Delete
 *      COD_Exit
 *      COD_GetBaseLib
 *      COD_GetBaseName
 *      COD_GetLoader
 *      COD_GetSection
 *      COD_GetSymValue
 *      COD_Init
 *      COD_LoadBase
 *      COD_Open
 *      COD_OpenBase
 *      COD_ReadSection
 *      COD_UnloadSection
 *
 *  Note:
 *      Currently, only static loading is supported.
 *
 *! Revision History
 *! ================
 *! 08-Apr-2003 map: Changed DBL to DBLL
 *! 07-Aug-2002 jeh: Added COD_GetBaseName().
 *! 17-Jul-2002 jeh: Added COD_Open(), COD_Close().
 *! 15-Mar-2002 jeh: Added DBL_Flags param to COD_OpenBase().
 *! 19-Oct-2001 jeh: Added COD_GetBaseLib, COD_GetLoader, (left in
 *!                  COD_LoadSection(), COD_UnloadSection(), since they
 *!                  may be needed for BridgeLite).
 *! 07-Sep-2001 jeh: Added COD_LoadSection(), COD_UnloadSection().
 *! 11-Jan-2001 jeh: Added COD_OpenBase.
 *! 29-Sep-2000 kc:  Added size param to COD_ReadSection for input buffer
 *!                  validation.
 *! 02-Aug-2000 kc:  Added COD_ReadSection.
 *! 04-Sep-1997 gp:  Added CDECL identifier to COD_WRITEFXN (for NT)..
 *! 18-Aug-1997 cr:  Added explicit CDECL identifier.
 *! 28-Oct-1996 gp:  Added COD_GetSection.
 *! 30-Jul-1996 gp:  Added envp[] argument to COD_LoadBase().
 *! 12-Jun-1996 gp:  Moved OUT param first in _Create().  Updated _Create()
 *!                  call to take a ZLFileName.  Moved COD_ processor types
 *!                  to CFG.
 *! 29-May-1996 gp:  Changed WCD_STATUS to DSP_STATUS.  Removed include's.
 *! 07-May-1996 mg:  Created.
 *
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

/* Flags passed to COD_Open */
	typedef DBLL_Flags COD_FLAGS;

/* COD code manager handle */
	struct COD_MANAGER;

/* COD library handle */
	struct COD_LIBRARYOBJ;

/* COD attributes */
	 struct COD_ATTRS {
		u32 ulReserved;
	} ;

/*
 *  Function prototypes for writing memory to a DSP system, allocating
 *  and freeing DSP memory.
 */
       typedef u32(*COD_WRITEFXN) (void *pPrivRef, u32 ulDspAddr,
					     void *pBuf, u32 ulNumBytes,
					     u32 nMemSpace);


/*
 *  ======== COD_Close ========
 *  Purpose:
 *      Close a library opened with COD_Open().
 *  Parameters:
 *      lib             - Library handle returned by COD_Open().
 *  Returns:
 *      None.
 *  Requires:
 *      COD module initialized.
 *      valid lib.
 *  Ensures:
 *
 */
       extern void COD_Close(struct COD_LIBRARYOBJ *lib);

/*
 *  ======== COD_Create ========
 *  Purpose:
 *      Create an object to manage code on a DSP system. This object can be
 *      used to load an initial program image with arguments that can later
 *      be expanded with dynamically loaded object files.
 *      Symbol table information is managed by this object and can be retrieved
 *      using the COD_GetSymValue() function.
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
       extern DSP_STATUS COD_Create(OUT struct COD_MANAGER **phManager,
				    char *pstrZLFile,
				    IN OPTIONAL CONST struct COD_ATTRS *attrs);

/*
 *  ======== COD_Delete ========
 *  Purpose:
 *      Delete a code manager object.
 *  Parameters:
 *      hManager:   handle of manager to be deleted
 *  Returns:
 *      None.
 *  Requires:
 *      COD module initialized.
 *      valid hManager.
 *  Ensures:
 */
       extern void COD_Delete(struct COD_MANAGER *hManager);

/*
 *  ======== COD_Exit ========
 *  Purpose:
 *      Discontinue usage of the COD module.
 *  Parameters:
 *      None.
 *  Returns:
 *      None.
 *  Requires:
 *      COD initialized.
 *  Ensures:
 *      Resources acquired in COD_Init(void) are freed.
 */
       extern void COD_Exit(void);

/*
 *  ======== COD_GetBaseLib ========
 *  Purpose:
 *      Get handle to the base image DBL library.
 *  Parameters:
 *      hManager:   handle of manager to be deleted
 *      plib:       location to store library handle on output.
 *  Returns:
 *      DSP_SOK:    Success.
 *  Requires:
 *      COD module initialized.
 *      valid hManager.
 *      plib != NULL.
 *  Ensures:
 */
       extern DSP_STATUS COD_GetBaseLib(struct COD_MANAGER *hManager,
					       struct DBLL_LibraryObj **plib);

/*
 *  ======== COD_GetBaseName ========
 *  Purpose:
 *      Get the name of the base image DBL library.
 *  Parameters:
 *      hManager:   handle of manager to be deleted
 *      pszName:    location to store library name on output.
 *      uSize:       size of name buffer.
 *  Returns:
 *      DSP_SOK:    Success.
 *      DSP_EFAIL:  Buffer too small.
 *  Requires:
 *      COD module initialized.
 *      valid hManager.
 *      pszName != NULL.
 *  Ensures:
 */
       extern DSP_STATUS COD_GetBaseName(struct COD_MANAGER *hManager,
						char *pszName, u32 uSize);

/*
 *  ======== COD_GetEntry ========
 *  Purpose:
 *      Retrieve the entry point of a loaded DSP program image
 *  Parameters:
 *      hManager:   handle of manager to be deleted
 *      pulEntry:   pointer to location for entry point
 *  Returns:
 *      DSP_SOK:       Success.
 *  Requires:
 *      COD module initialized.
 *      valid hManager.
 *      pulEntry != NULL.
 *  Ensures:
 */
       extern DSP_STATUS COD_GetEntry(struct COD_MANAGER *hManager,
					     u32 *pulEntry);

/*
 *  ======== COD_GetLoader ========
 *  Purpose:
 *      Get handle to the DBL loader.
 *  Parameters:
 *      hManager:   handle of manager to be deleted
 *      phLoader:   location to store loader handle on output.
 *  Returns:
 *      DSP_SOK:    Success.
 *  Requires:
 *      COD module initialized.
 *      valid hManager.
 *      phLoader != NULL.
 *  Ensures:
 */
       extern DSP_STATUS COD_GetLoader(struct COD_MANAGER *hManager,
					      struct DBLL_TarObj **phLoader);

/*
 *  ======== COD_GetSection ========
 *  Purpose:
 *      Retrieve the starting address and length of a section in the COFF file
 *      given the section name.
 *  Parameters:
 *      lib         Library handle returned from COD_Open().
 *      pstrSect:   name of the section, with or without leading "."
 *      puAddr:     Location to store address.
 *      puLen:      Location to store length.
 *  Returns:
 *      DSP_SOK:                Success
 *      COD_E_NOSYMBOLSLOADED:  Symbols have not been loaded onto the board.
 *      COD_E_SYMBOLNOTFOUND:   The symbol could not be found.
 *  Requires:
 *      COD module initialized.
 *      valid hManager.
 *      pstrSect != NULL;
 *      puAddr != NULL;
 *      puLen != NULL;
 *  Ensures:
 *      DSP_SOK:  *puAddr and *puLen contain the address and length of the
 *                 section.
 *      else:  *puAddr == 0 and *puLen == 0;
 *
 */
       extern DSP_STATUS COD_GetSection(struct COD_LIBRARYOBJ *lib,
					       IN char *pstrSect,
					       OUT u32 *puAddr,
					       OUT u32 *puLen);

/*
 *  ======== COD_GetSymValue ========
 *  Purpose:
 *      Retrieve the value for the specified symbol. The symbol is first
 *      searched for literally and then, if not found, searched for as a
 *      C symbol.
 *  Parameters:
 *      lib:        library handle returned from COD_Open().
 *      pstrSymbol: name of the symbol
 *      value:      value of the symbol
 *  Returns:
 *      DSP_SOK:                Success.
 *      COD_E_NOSYMBOLSLOADED:  Symbols have not been loaded onto the board.
 *      COD_E_SYMBOLNOTFOUND:   The symbol could not be found.
 *  Requires:
 *      COD module initialized.
 *      Valid hManager.
 *      pstrSym != NULL.
 *      pulValue != NULL.
 *  Ensures:
 */
       extern DSP_STATUS COD_GetSymValue(struct COD_MANAGER *hManager,
						IN char *pstrSym,
						OUT u32 *pulValue);

/*
 *  ======== COD_Init ========
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
       extern bool COD_Init(void);

/*
 *  ======== COD_LoadBase ========
 *  Purpose:
 *      Load the initial program image, optionally with command-line arguments,
 *      on the DSP system managed by the supplied handle. The program to be
 *      loaded must be the first element of the args array and must be a fully
 *      qualified pathname.
 *  Parameters:
 *      hMgr:       manager to load the code with
 *      nArgc:      number of arguments in the args array
 *      args:       array of strings for arguments to DSP program
 *      writeFxn:   board-specific function to write data to DSP system
 *      pArb:       arbitrary pointer to be passed as first arg to writeFxn
 *      envp:       array of environment strings for DSP exec.
 *  Returns:
 *      DSP_SOK:                   Success.
 *      COD_E_OPENFAILED:       Failed to open target code.
 *      COD_E_LOADFAILED:       Failed to load code onto target.
 *  Requires:
 *      COD module initialized.
 *      hMgr is valid.
 *      nArgc > 0.
 *      aArgs != NULL.
 *      aArgs[0] != NULL.
 *      pfnWrite != NULL.
 *  Ensures:
 */
       extern DSP_STATUS COD_LoadBase(struct COD_MANAGER *hManager,
					     u32 nArgc, char *aArgs[],
					     COD_WRITEFXN pfnWrite, void *pArb,
					     char *envp[]);


/*
 *  ======== COD_Open ========
 *  Purpose:
 *      Open a library for reading sections. Does not load or set the base.
 *  Parameters:
 *      hMgr:           manager to load the code with
 *      pszCoffPath:    Coff file to open.
 *      flags:          COD_NOLOAD (don't load symbols) or COD_SYMB (load
 *                      symbols).
 *      pLib:           Handle returned that can be used in calls to COD_Close
 *                      and COD_GetSection.
 *  Returns:
 *      S_OK:                   Success.
 *      COD_E_OPENFAILED:       Failed to open target code.
 *  Requires:
 *      COD module initialized.
 *      hMgr is valid.
 *      flags == COD_NOLOAD || flags == COD_SYMB.
 *      pszCoffPath != NULL.
 *  Ensures:
 */
	extern DSP_STATUS COD_Open(struct COD_MANAGER *hMgr,
				   IN char *pszCoffPath,
				   COD_FLAGS flags,
				   OUT struct COD_LIBRARYOBJ **pLib);

/*
 *  ======== COD_OpenBase ========
 *  Purpose:
 *      Open base image for reading sections. Does not load the base.
 *  Parameters:
 *      hMgr:           manager to load the code with
 *      pszCoffPath:    Coff file to open.
 *      flags:          Specifies whether to load symbols.
 *  Returns:
 *      DSP_SOK:            Success.
 *      COD_E_OPENFAILED:   Failed to open target code.
 *  Requires:
 *      COD module initialized.
 *      hMgr is valid.
 *      pszCoffPath != NULL.
 *  Ensures:
 */
extern DSP_STATUS COD_OpenBase(struct COD_MANAGER *hMgr, IN char *pszCoffPath,
				       DBLL_Flags flags);

/*
 *  ======== COD_ReadSection ========
 *  Purpose:
 *      Retrieve the content of a code section given the section name.
 *  Parameters:
 *      hManager    - manager in which to search for the symbol
 *      pstrSect    - name of the section, with or without leading "."
 *      pstrContent - buffer to store content of the section.
 *  Returns:
 *      DSP_SOK: on success, error code on failure
 *      COD_E_NOSYMBOLSLOADED:  Symbols have not been loaded onto the board.
 *      COD_E_READFAILED: Failed to read content of code section.
 *  Requires:
 *      COD module initialized.
 *      valid hManager.
 *      pstrSect != NULL;
 *      pstrContent != NULL;
 *  Ensures:
 *      DSP_SOK:  *pstrContent stores the content of the named section.
 */
       extern DSP_STATUS COD_ReadSection(struct COD_LIBRARYOBJ *lib,
						IN char *pstrSect,
						OUT char *pstrContent,
						IN u32 cContentSize);



#endif				/* COD_ */
