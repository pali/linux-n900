/*
 * cod.c
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
 *  ======== cod.c ========
 *  This module implements DSP code management for the DSP/BIOS Bridge
 *  environment. It is mostly a thin wrapper.
 *
 *  This module provides an interface for loading both static and
 *  dynamic code objects onto DSP systems.
 *
 *! Revision History
 *! ================
 *! 08-Apr-2003 map: Consolidated DBL to DBLL loader name
 *! 24-Feb-2003 swa: PMGR Code review comments incorporated.
 *! 18-Apr-2002 jeh: Added DBL function tables.
 *! 20-Nov-2001 jeh: Removed call to ZL_loadArgs function.
 *! 19-Oct-2001 jeh: Access DBL as a static library. Added COD_GetBaseLib,
 *!		  COD_GetLoader, removed COD_LoadSection, COD_UnloadSection.
 *! 07-Sep-2001 jeh: Added COD_LoadSection(), COD_UnloadSection().
 *! 07-Aug-2001 rr:  hMgr->baseLib is updated after zlopen in COD_LoadBase.
 *! 18-Apr-2001 jeh: Check for fLoaded flag before ZL_unload, to allow
 *!		  COD_OpenBase to be used.
 *! 11-Jan-2001 jeh: Added COD_OpenBase (not used yet, since there is an
 *!		  occasional crash).
 *! 02-Aug-2000 kc:  Added COD_ReadSection to COD module. Incorporates use
 *!		  of ZL_readSect (new function in ZL module).
 *! 28-Feb-2000 rr:  New GT Usage Implementation
 *! 08-Dec-1999 ag:  Removed x86 specific __asm int 3.
 *! 02-Oct-1999 ag:  Added #ifdef DEBUGINT3COD for debug.
 *! 20-Sep-1999 ag:  Removed call to GT_set().
 *! 04-Jun-1997 cr:  Added validation of argc/argv pair in COD_LoadBase, as it
 *!		     is a requirement to ZL_loadArgs.
 *! 31-May-1997 cr:  Changed COD_LoadBase argc value from u32 to int, added
 *!	       DSP_ENOTIMPL return value to COD_Create when attrs != NULL.
 *! 29-May-1997 cr:  Added debugging support.
 *! 24-Oct-1996 gp:  Added COD_GetSection().
 *! 18-Jun-1996 gp:  Updated GetSymValue() to check for lib; updated E_ codes.
 *! 12-Jun-1996 gp:  Imported CSL_ services for strcpyn(); Added ref counting.
 *! 20-May-1996 mg:  Adapted for new MEM and LDR modules.
 *! 08-May-1996 mg:  Created.
 */

/*  ----------------------------------- Host OS */
#include <dspbridge/host_os.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>
#include <dspbridge/dbdefs.h>
#include <dspbridge/errbase.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>
#include <dspbridge/gt.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/csl.h>
#include <dspbridge/kfile.h>
#include <dspbridge/ldr.h>
#include <dspbridge/mem.h>

/*  ----------------------------------- Platform Manager */
/* Include appropriate loader header file */
#include <dspbridge/dbll.h>

/*  ----------------------------------- This */
#include <dspbridge/cod.h>

/* magic number for handle validation */
#define MAGIC	 0xc001beef

/* macro to validate COD manager handles */
#define IsValid(h)    ((h) != NULL && (h)->ulMagic == MAGIC)

/*
 *  ======== COD_MANAGER ========
 */
struct COD_MANAGER {
	struct DBLL_TarObj *target;
	struct DBLL_LibraryObj *baseLib;
	bool fLoaded;		/* Base library loaded? */
	u32 ulEntry;
	struct LDR_MODULE *hDll;
	struct DBLL_Fxns fxns;
	struct DBLL_Attrs attrs;
	char szZLFile[COD_MAXPATHLENGTH];
	u32 ulMagic;
} ;

/*
 *  ======== COD_LIBRARYOBJ ========
 */
struct COD_LIBRARYOBJ {
	struct DBLL_LibraryObj *dbllLib;
	struct COD_MANAGER *hCodMgr;
} ;

static u32 cRefs = 0L;

#if GT_TRACE
static struct GT_Mask COD_debugMask = { NULL, NULL };
#endif

static struct DBLL_Fxns dbllFxns = {
	(DBLL_CloseFxn) DBLL_close,
	(DBLL_CreateFxn) DBLL_create,
	(DBLL_DeleteFxn) DBLL_delete,
	(DBLL_ExitFxn) DBLL_exit,
	(DBLL_GetAttrsFxn) DBLL_getAttrs,
	(DBLL_GetAddrFxn) DBLL_getAddr,
	(DBLL_GetCAddrFxn) DBLL_getCAddr,
	(DBLL_GetSectFxn) DBLL_getSect,
	(DBLL_InitFxn) DBLL_init,
	(DBLL_LoadFxn) DBLL_load,
	(DBLL_LoadSectFxn) DBLL_loadSect,
	(DBLL_OpenFxn) DBLL_open,
	(DBLL_ReadSectFxn) DBLL_readSect,
	(DBLL_SetAttrsFxn) DBLL_setAttrs,
	(DBLL_UnloadFxn) DBLL_unload,
	(DBLL_UnloadSectFxn) DBLL_unloadSect,
};

static bool NoOp(void);

/*
 *  ======== COD_Close ========
 */
void COD_Close(struct COD_LIBRARYOBJ *lib)
{
	struct COD_MANAGER *hMgr;

	DBC_Require(cRefs > 0);
	DBC_Require(lib != NULL);
	DBC_Require(IsValid(((struct COD_LIBRARYOBJ *)lib)->hCodMgr));

	hMgr = lib->hCodMgr;
	hMgr->fxns.closeFxn(lib->dbllLib);

	MEM_Free(lib);
}

/*
 *  ======== COD_Create ========
 *  Purpose:
 *      Create an object to manage code on a DSP system.
 *      This object can be used to load an initial program image with
 *      arguments that can later be expanded with
 *      dynamically loaded object files.
 *
 */
DSP_STATUS COD_Create(OUT struct COD_MANAGER **phMgr, char *pstrDummyFile,
		     IN OPTIONAL CONST struct COD_ATTRS *attrs)
{
	struct COD_MANAGER *hMgrNew;
	struct DBLL_Attrs zlAttrs;
	DSP_STATUS status = DSP_SOK;

	DBC_Require(cRefs > 0);
	DBC_Require(phMgr != NULL);

	GT_3trace(COD_debugMask, GT_ENTER,
		  "Entered COD_Create, Args: \t\nphMgr: "
		  "0x%x\t\npstrDummyFile: 0x%x\t\nattr: 0x%x\n",
		  phMgr, pstrDummyFile, attrs);
	/* assume failure */
	*phMgr = NULL;

	/* we don't support non-default attrs yet */
	if (attrs != NULL)
		return DSP_ENOTIMPL;

	hMgrNew = MEM_Calloc(sizeof(struct COD_MANAGER), MEM_NONPAGED);
	if (hMgrNew == NULL) {
		GT_0trace(COD_debugMask, GT_7CLASS,
			  "COD_Create: Out Of Memory\n");
		return DSP_EMEMORY;
	}

	hMgrNew->ulMagic = MAGIC;

	/* Set up loader functions */
	hMgrNew->fxns = dbllFxns;

	/* initialize the ZL module */
	hMgrNew->fxns.initFxn();

	zlAttrs.alloc = (DBLL_AllocFxn)NoOp;
	zlAttrs.free = (DBLL_FreeFxn)NoOp;
	zlAttrs.fread = (DBLL_ReadFxn)KFILE_Read;
	zlAttrs.fseek = (DBLL_SeekFxn)KFILE_Seek;
	zlAttrs.ftell = (DBLL_TellFxn)KFILE_Tell;
	zlAttrs.fclose = (DBLL_FCloseFxn)KFILE_Close;
	zlAttrs.fopen = (DBLL_FOpenFxn)KFILE_Open;
	zlAttrs.symLookup = NULL;
	zlAttrs.baseImage = true;
	zlAttrs.logWrite = NULL;
	zlAttrs.logWriteHandle = NULL;
	zlAttrs.write = NULL;
	zlAttrs.rmmHandle = NULL;
	zlAttrs.wHandle = NULL;
	zlAttrs.symHandle = NULL;
	zlAttrs.symArg = NULL;

	hMgrNew->attrs = zlAttrs;

	status = hMgrNew->fxns.createFxn(&hMgrNew->target, &zlAttrs);

	if (DSP_FAILED(status)) {
		COD_Delete(hMgrNew);
		GT_1trace(COD_debugMask, GT_7CLASS,
			  "COD_Create:ZL Create Failed: 0x%x\n", status);
		return COD_E_ZLCREATEFAILED;
	}

	/* return the new manager */
	*phMgr = hMgrNew;
	GT_1trace(COD_debugMask, GT_1CLASS,
		  "COD_Create: Success CodMgr: 0x%x\n",	*phMgr);
	return DSP_SOK;
}

/*
 *  ======== COD_Delete ========
 *  Purpose:
 *      Delete a code manager object.
 */
void COD_Delete(struct COD_MANAGER *hMgr)
{
	DBC_Require(cRefs > 0);
	DBC_Require(IsValid(hMgr));

	GT_1trace(COD_debugMask, GT_ENTER, "COD_Delete:hMgr 0x%x\n", hMgr);
	if (hMgr->baseLib) {
		if (hMgr->fLoaded)
			hMgr->fxns.unloadFxn(hMgr->baseLib, &hMgr->attrs);

		hMgr->fxns.closeFxn(hMgr->baseLib);
	}
	if (hMgr->target) {
		hMgr->fxns.deleteFxn(hMgr->target);
		hMgr->fxns.exitFxn();
	}
	hMgr->ulMagic = ~MAGIC;
	MEM_Free(hMgr);
}

/*
 *  ======== COD_Exit ========
 *  Purpose:
 *      Discontinue usage of the COD module.
 *
 */
void COD_Exit(void)
{
	DBC_Require(cRefs > 0);

	cRefs--;

	GT_1trace(COD_debugMask, GT_ENTER,
		  "Entered COD_Exit, ref count:  0x%x\n", cRefs);

	DBC_Ensure(cRefs >= 0);
}

/*
 *  ======== COD_GetBaseLib ========
 *  Purpose:
 *      Get handle to the base image DBL library.
 */
DSP_STATUS COD_GetBaseLib(struct COD_MANAGER *hManager,
				struct DBLL_LibraryObj **plib)
{
	DSP_STATUS status = DSP_SOK;

	DBC_Require(cRefs > 0);
	DBC_Require(IsValid(hManager));
	DBC_Require(plib != NULL);

	*plib = (struct DBLL_LibraryObj *) hManager->baseLib;

	return status;
}

/*
 *  ======== COD_GetBaseName ========
 */
DSP_STATUS COD_GetBaseName(struct COD_MANAGER *hManager, char *pszName,
				u32 uSize)
{
	DSP_STATUS status = DSP_SOK;

	DBC_Require(cRefs > 0);
	DBC_Require(IsValid(hManager));
	DBC_Require(pszName != NULL);

	if (uSize <= COD_MAXPATHLENGTH)
               strncpy(pszName, hManager->szZLFile, uSize);
	else
		status = DSP_EFAIL;

	return status;
}

/*
 *  ======== COD_GetEntry ========
 *  Purpose:
 *      Retrieve the entry point of a loaded DSP program image
 *
 */
DSP_STATUS COD_GetEntry(struct COD_MANAGER *hManager, u32 *pulEntry)
{
	DBC_Require(cRefs > 0);
	DBC_Require(IsValid(hManager));
	DBC_Require(pulEntry != NULL);

	*pulEntry = hManager->ulEntry;

	GT_1trace(COD_debugMask, GT_ENTER, "COD_GetEntry:ulEntr 0x%x\n",
		  *pulEntry);

	return DSP_SOK;
}

/*
 *  ======== COD_GetLoader ========
 *  Purpose:
 *      Get handle to the DBLL loader.
 */
DSP_STATUS COD_GetLoader(struct COD_MANAGER *hManager,
			       struct DBLL_TarObj **phLoader)
{
	DSP_STATUS status = DSP_SOK;

	DBC_Require(cRefs > 0);
	DBC_Require(IsValid(hManager));
	DBC_Require(phLoader != NULL);

	*phLoader = (struct DBLL_TarObj *)hManager->target;

	return status;
}

/*
 *  ======== COD_GetSection ========
 *  Purpose:
 *      Retrieve the starting address and length of a section in the COFF file
 *      given the section name.
 */
DSP_STATUS COD_GetSection(struct COD_LIBRARYOBJ *lib, IN char *pstrSect,
			  OUT u32 *puAddr, OUT u32 *puLen)
{
	struct COD_MANAGER *hManager;
	DSP_STATUS status = DSP_SOK;

	DBC_Require(cRefs > 0);
	DBC_Require(lib != NULL);
	DBC_Require(IsValid(lib->hCodMgr));
	DBC_Require(pstrSect != NULL);
	DBC_Require(puAddr != NULL);
	DBC_Require(puLen != NULL);

	GT_4trace(COD_debugMask, GT_ENTER,
		  "Entered COD_GetSection Args \t\n lib: "
		  "0x%x\t\npstrsect: 0x%x\t\npuAddr: 0x%x\t\npuLen: 0x%x\n",
		  lib, pstrSect, puAddr, puLen);
	*puAddr = 0;
	*puLen = 0;
	if (lib != NULL) {
		hManager = lib->hCodMgr;
		status = hManager->fxns.getSectFxn(lib->dbllLib, pstrSect,
						   puAddr, puLen);
		if (DSP_FAILED(status)) {
			GT_1trace(COD_debugMask, GT_7CLASS,
				 "COD_GetSection: Section %s not"
				 "found\n", pstrSect);
		}
	} else {
		status = COD_E_NOSYMBOLSLOADED;
		GT_0trace(COD_debugMask, GT_7CLASS,
			  "COD_GetSection:No Symbols loaded\n");
	}

	DBC_Ensure(DSP_SUCCEEDED(status) || ((*puAddr == 0) && (*puLen == 0)));

	return status;
}

/*
 *  ======== COD_GetSymValue ========
 *  Purpose:
 *      Retrieve the value for the specified symbol. The symbol is first
 *      searched for literally and then, if not found, searched for as a
 *      C symbol.
 *
 */
DSP_STATUS COD_GetSymValue(struct COD_MANAGER *hMgr, char *pstrSym,
			   u32 *pulValue)
{
	struct DBLL_Symbol *pSym;

	DBC_Require(cRefs > 0);
	DBC_Require(IsValid(hMgr));
	DBC_Require(pstrSym != NULL);
	DBC_Require(pulValue != NULL);

	GT_3trace(COD_debugMask, GT_ENTER, "Entered COD_GetSymValue Args \t\n"
		  "hMgr: 0x%x\t\npstrSym: 0x%x\t\npulValue: 0x%x\n",
		  hMgr, pstrSym, pulValue);
	if (hMgr->baseLib) {
		if (!hMgr->fxns.getAddrFxn(hMgr->baseLib, pstrSym, &pSym)) {
			if (!hMgr->fxns.getCAddrFxn(hMgr->baseLib, pstrSym,
			    &pSym)) {
				GT_0trace(COD_debugMask, GT_7CLASS,
					  "COD_GetSymValue: "
					  "Symbols not found\n");
				return COD_E_SYMBOLNOTFOUND;
			}
		}
	} else {
		GT_0trace(COD_debugMask, GT_7CLASS, "COD_GetSymValue: "
			 "No Symbols loaded\n");
		return COD_E_NOSYMBOLSLOADED;
	}

	*pulValue = pSym->value;

	return DSP_SOK;
}

/*
 *  ======== COD_Init ========
 *  Purpose:
 *      Initialize the COD module's private state.
 *
 */
bool COD_Init(void)
{
	bool fRetVal = true;

	DBC_Require(cRefs >= 0);

	if (cRefs == 0) {
		DBC_Assert(!COD_debugMask.flags);
		GT_create(&COD_debugMask, "CO");
	}

	if (fRetVal)
		cRefs++;


	GT_1trace(COD_debugMask, GT_1CLASS,
		  "Entered COD_Init, ref count: 0x%x\n", cRefs);
	DBC_Ensure((fRetVal && cRefs > 0) || (!fRetVal && cRefs >= 0));
	return fRetVal;
}

/*
 *  ======== COD_LoadBase ========
 *  Purpose:
 *      Load the initial program image, optionally with command-line arguments,
 *      on the DSP system managed by the supplied handle. The program to be
 *      loaded must be the first element of the args array and must be a fully
 *      qualified pathname.
 *  Details:
 *      if nArgc doesn't match the number of arguments in the aArgs array, the
 *      aArgs array is searched for a NULL terminating entry, and argc is
 *      recalculated to reflect this.  In this way, we can support NULL
 *      terminating aArgs arrays, if nArgc is very large.
 */
DSP_STATUS COD_LoadBase(struct COD_MANAGER *hMgr, u32 nArgc, char *aArgs[],
			COD_WRITEFXN pfnWrite, void *pArb, char *envp[])
{
	DBLL_Flags flags;
	struct DBLL_Attrs saveAttrs;
	struct DBLL_Attrs newAttrs;
	DSP_STATUS status;
	u32 i;

	DBC_Require(cRefs > 0);
	DBC_Require(IsValid(hMgr));
	DBC_Require(nArgc > 0);
	DBC_Require(aArgs != NULL);
	DBC_Require(aArgs[0] != NULL);
	DBC_Require(pfnWrite != NULL);
	DBC_Require(hMgr->baseLib != NULL);

	GT_6trace(COD_debugMask, GT_ENTER,
		 "Entered COD_LoadBase, hMgr:  0x%x\n \t"
		 "nArgc:  0x%x\n\taArgs:  0x%x\n\tpfnWrite:  0x%x\n\tpArb:"
		 " 0x%x\n \tenvp:  0x%x\n", hMgr, nArgc, aArgs, pfnWrite,
		 pArb, envp);
	/*
	 *  Make sure every argv[] stated in argc has a value, or change argc to
	 *  reflect true number in NULL terminated argv array.
	 */
	for (i = 0; i < nArgc; i++) {
		if (aArgs[i] == NULL) {
			nArgc = i;
			break;
		}
	}

	/* set the write function for this operation */
	hMgr->fxns.getAttrsFxn(hMgr->target, &saveAttrs);

	newAttrs = saveAttrs;
	newAttrs.write = (DBLL_WriteFxn)pfnWrite;
	newAttrs.wHandle = pArb;
	newAttrs.alloc = (DBLL_AllocFxn)NoOp;
	newAttrs.free = (DBLL_FreeFxn)NoOp;
	newAttrs.logWrite = NULL;
	newAttrs.logWriteHandle = NULL;

	/* Load the image */
	flags = DBLL_CODE | DBLL_DATA | DBLL_SYMB;
	status = hMgr->fxns.loadFxn(hMgr->baseLib, flags, &newAttrs,
		 &hMgr->ulEntry);
	if (DSP_FAILED(status)) {
		hMgr->fxns.closeFxn(hMgr->baseLib);
		GT_1trace(COD_debugMask, GT_7CLASS,
			  "COD_LoadBase: COD Load failed: "
			  "0x%x\n", status);
	}
	if (DSP_SUCCEEDED(status))
		hMgr->fLoaded = true;
	else
		hMgr->baseLib = NULL;

	return status;
}

/*
 *  ======== COD_Open ========
 *      Open library for reading sections.
 */
DSP_STATUS COD_Open(struct COD_MANAGER *hMgr, IN char *pszCoffPath,
		    COD_FLAGS flags, struct COD_LIBRARYOBJ **pLib)
{
	DSP_STATUS status = DSP_SOK;
	struct COD_LIBRARYOBJ *lib = NULL;

	DBC_Require(cRefs > 0);
	DBC_Require(IsValid(hMgr));
	DBC_Require(pszCoffPath != NULL);
	DBC_Require(flags == COD_NOLOAD || flags == COD_SYMB);
	DBC_Require(pLib != NULL);

	GT_4trace(COD_debugMask, GT_ENTER, "Entered COD_Open, hMgr: 0x%x\n\t "
		  "pszCoffPath:  0x%x\tflags: 0x%x\tlib: 0x%x\n", hMgr,
		  pszCoffPath, flags, pLib);

	*pLib = NULL;

	lib = MEM_Calloc(sizeof(struct COD_LIBRARYOBJ), MEM_NONPAGED);
	if (lib == NULL) {
		GT_0trace(COD_debugMask, GT_7CLASS,
			 "COD_Open: Out Of Memory\n");
		status = DSP_EMEMORY;
	}

	if (DSP_SUCCEEDED(status)) {
		lib->hCodMgr = hMgr;
		status = hMgr->fxns.openFxn(hMgr->target, pszCoffPath, flags,
					   &lib->dbllLib);
		if (DSP_FAILED(status)) {
			GT_1trace(COD_debugMask, GT_7CLASS,
				 "COD_Open failed: 0x%x\n", status);
		} else {
			*pLib = lib;
		}
	}

	return status;
}

/*
 *  ======== COD_OpenBase ========
 *  Purpose:
 *      Open base image for reading sections.
 */
DSP_STATUS COD_OpenBase(struct COD_MANAGER *hMgr, IN char *pszCoffPath,
			DBLL_Flags flags)
{
	DSP_STATUS status = DSP_SOK;
	struct DBLL_LibraryObj *lib;

	DBC_Require(cRefs > 0);
	DBC_Require(IsValid(hMgr));
	DBC_Require(pszCoffPath != NULL);

	GT_2trace(COD_debugMask, GT_ENTER,
		  "Entered COD_OpenBase, hMgr:  0x%x\n\t"
		  "pszCoffPath:  0x%x\n", hMgr, pszCoffPath);

	/* if we previously opened a base image, close it now */
	if (hMgr->baseLib) {
		if (hMgr->fLoaded) {
			GT_0trace(COD_debugMask, GT_7CLASS,
				 "Base Image is already loaded. "
				 "Unloading it...\n");
			hMgr->fxns.unloadFxn(hMgr->baseLib, &hMgr->attrs);
			hMgr->fLoaded = false;
		}
		hMgr->fxns.closeFxn(hMgr->baseLib);
		hMgr->baseLib = NULL;
	} else {
		GT_0trace(COD_debugMask, GT_1CLASS,
			 "COD_OpenBase: Opening the base image ...\n");
	}
	status = hMgr->fxns.openFxn(hMgr->target, pszCoffPath, flags, &lib);
	if (DSP_FAILED(status)) {
		GT_0trace(COD_debugMask, GT_7CLASS,
			 "COD_OpenBase: COD Open failed\n");
	} else {
		/* hang onto the library for subsequent sym table usage */
		hMgr->baseLib = lib;
		strncpy(hMgr->szZLFile, pszCoffPath, COD_MAXPATHLENGTH - 1);
		hMgr->szZLFile[COD_MAXPATHLENGTH - 1] = '\0';
	}

	return status;
}

/*
 *  ======== COD_ReadSection ========
 *  Purpose:
 *      Retrieve the content of a code section given the section name.
 */
DSP_STATUS COD_ReadSection(struct COD_LIBRARYOBJ *lib, IN char *pstrSect,
			   OUT char *pstrContent, IN u32 cContentSize)
{
	DSP_STATUS status = DSP_SOK;

	DBC_Require(cRefs > 0);
	DBC_Require(lib != NULL);
	DBC_Require(IsValid(lib->hCodMgr));
	DBC_Require(pstrSect != NULL);
	DBC_Require(pstrContent != NULL);

	GT_4trace(COD_debugMask, GT_ENTER, "Entered COD_ReadSection Args: 0x%x,"
		 " 0x%x, 0x%x, 0x%x\n", lib, pstrSect, pstrContent,
		 cContentSize);

	if (lib != NULL) {
		status = lib->hCodMgr->fxns.readSectFxn(lib->dbllLib, pstrSect,
							pstrContent,
							cContentSize);
		if (DSP_FAILED(status)) {
			GT_1trace(COD_debugMask, GT_7CLASS,
				 "COD_ReadSection failed: 0x%lx\n", status);
		}
	} else {
		status = COD_E_NOSYMBOLSLOADED;
		GT_0trace(COD_debugMask, GT_7CLASS,
			  "COD_ReadSection: No Symbols loaded\n");
	}
	return status;
}

/*
 *  ======== NoOp ========
 *  Purpose:
 *      No Operation.
 *
 */
static bool NoOp(void)
{
	return true;
}

