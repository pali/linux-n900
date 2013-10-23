/*
 * dbl.c
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
 *  ======== dbl.c ========
 *  Dynamic BOF Loader library. Contains functions related to
 *  loading and unloading symbols/code/data on DSP.
 *  Also contains other support functions.
 *
 *! Revision History
 *! ================
 *! 24-Feb-2003 swa 	PMGR Code review comments incorporated.
 *! 24-May-2002 jeh     Free DCD sects in DBL_close().
 *! 19-Mar-2002 jeh     Changes made to match dynamic loader (dbll.c): Pass
 *!		     DBL_Library to DBL_getAddr() instead of DBL_Target,
 *!		     eliminate scope param, use DBL_Symbol. Pass attrs to
 *!		     DBL_load(), DBL_unload().
 *! 20-Nov-2001 jeh     Removed DBL_loadArgs().
 *! 07-Sep-2001 jeh     Added overlay support.
 *! 31-Jul-2001 jeh     Include windows.h.
 *! 06-Jun-2001 jeh     Created.
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
#include <dspbridge/mem.h>
#include <dspbridge/kfile.h>

/*  ----------------------------------- This */
#include <dspbridge/dbof.h>
#include <dspbridge/dbl.h>

#define DBL_TARGSIGNATURE      0x544c4244	/* "TLBD" */
#define DBL_LIBSIGNATURE       0x4c4c4244	/* "LLBD" */

#define C54TARG	 0
#define C55TARG	 1
#define NUMTARGS	2

#define C54MAGIC	0x98	/* Magic number for TI C54 COF  */
#define C55MAGIC	0x9c	/* Magic number for LEAD3 (C55) COF  */

/* Three task phases */
#define CREATEPHASE 0
#define DELETEPHASE 1
#define EXECUTEPHASE 2
#define NONE 3		/* For overlay section with phase not specified */

/* Default load buffer size */
#define LOADBUFSIZE     0x800

#define SWAPLONG(x) ((((x) << 24) & 0xFF000000) | (((x) << 8) & 0xFF0000L) | \
		      (((x) >> 8) & 0xFF00L) | (((x) >> 24) & 0xFF))

#define SWAPWORD(x) ((((x) << 8) & 0xFF00) | (((x) >> 8) & 0xFF))

/*
 *  Macros for accessing the following types of overlay data within a
 *  structure of type OvlyData:
 *      - Overlay data not associated with a particular phase
 *      - Create phase overlay data
 *      - Delete phase overlay data
 *      - Execute phase overlay data
 */
#define numOtherSects(pOvlyData)    ((pOvlyData)->hdr.dbofHdr.numOtherSects)
#define numCreateSects(pOvlyData)   ((pOvlyData)->hdr.dbofHdr.numCreateSects)
#define numDeleteSects(pOvlyData)   ((pOvlyData)->hdr.dbofHdr.numDeleteSects)
#define numExecuteSects(pOvlyData)  ((pOvlyData)->hdr.dbofHdr.numExecuteSects)
#define otherOffset(pOvlyData)      0
#define createOffset(pOvlyData)     ((pOvlyData)->hdr.dbofHdr.numOtherSects)
#define deleteOffset(pOvlyData)     (createOffset(pOvlyData) + \
				     (pOvlyData->hdr.dbofHdr.numCreateSects))
#define executeOffset(pOvlyData)    (deleteOffset(pOvlyData) + \
				     (pOvlyData->hdr.dbofHdr.numDeleteSects))
/*
 *  ======== OvlyHdr ========
 */
struct OvlyHdr {
	struct DBOF_OvlySectHdr dbofHdr;
	char *pName; 		/* Name of overlay section */
	u16 createRef; 	/* Reference count for create phase */
	u16 deleteRef; 	/* Reference count for delete phase */
	u16 executeRef; 	/* Execute phase ref count */
	u16 otherRef; 		/* Unspecified phase ref count */
} ;

/*
 *  ======== OvlyData ========
 */
struct OvlyData {
	struct OvlyHdr hdr;
	struct DBOF_OvlySectData data[1];
} ;

/*
 *  ======== Symbol ========
 */
struct Symbol {
	struct DBL_Symbol sym;
	char *pSymName;
};

/*
 *  ======== DCDSect ========
 */
struct DCDSect {
	struct DBOF_DCDSectHdr sectHdr;
	char *pData;
} ;

/*
 *  ======== DBL_TargetObj ========
 */
struct DBL_TargetObj {
	u32 dwSignature; 	/* For object validation */
	struct DBL_Attrs dblAttrs; 	/* file read, write, etc. functions */
	char *pBuf; 		/* Load buffer */
};

/*
 *  ======== TargetInfo ========
 */
struct TargetInfo {
	u16 dspType; 		/* eg, C54TARG, C55TARG */
	u32 magic; 		/* COFF magic number, identifies target type */
	u16 wordSize; 	/* Size of a DSP word */
	u16 mauSize; 		/* Size of minimum addressable unit */
	u16 charSize; 	/* For C55x, mausize = 1, but charsize = 2 */
} ;

/*
 *  ======== DBL_LibraryObj ========
 *  Represents a library loaded on a target.
 */
struct DBL_LibraryObj {
	u32 dwSignature; 	/* For object validation */
	struct DBL_TargetObj *pTarget; 	/* Target for this library */
	struct KFILE_FileObj *file; 	/* DBOF file handle */
	bool byteSwapped; 	/* Are bytes swapped? */
	struct DBOF_FileHdr fileHdr; 	/* Header of DBOF file */
	u16 nSymbols; 		/* Number of DSP/Bridge symbols */
	struct Symbol *symbols; 	/* Table of DSP/Bridge symbols */
	u16 nDCDSects; 	/* Number of DCD sections */
	u16 nOvlySects; 	/* Number of overlay nodes */
	struct DCDSect *dcdSects; 	/* DCD section data */
	struct OvlyData **ppOvlyData; 	/* Array of overlay section data */
	struct TargetInfo *pTargetInfo; 	/* Entry in targetTab[] below */
} ;

#if GT_TRACE
static struct GT_Mask DBL_debugMask = { NULL, NULL }; 	/* GT trace variable */
#endif

static u32 cRefs; 		/* module reference count */

static u32 magicTab[NUMTARGS] = { C54MAGIC, C55MAGIC };

static struct TargetInfo targetTab[] = {
	/* targ     magic       wordsize    mausize    charsize */
	{C54TARG, C54MAGIC, 2, 2, 2}, 	/* C54  */
	{C55TARG, C55MAGIC, 2, 1, 2}, 	/* C55  */
};

static void freeSects(struct DBL_TargetObj *dbl, struct OvlyData *pOvlyData,
		     s32 offset, s32 nSects);
static DSP_STATUS loadSect(struct DBL_TargetObj *dbl,
			  struct DBL_LibraryObj *pdblLib);
static DSP_STATUS readDCDSects(struct DBL_TargetObj *dbl,
			      struct DBL_LibraryObj *pdblLib);
static DSP_STATUS readHeader(struct DBL_TargetObj *dbl,
			    struct DBL_LibraryObj *pdblLib);
static DSP_STATUS readOvlySects(struct DBL_TargetObj *dbl,
				struct DBL_LibraryObj *pdblLib);
static DSP_STATUS readSymbols(struct DBL_TargetObj *dbl,
			     struct DBL_LibraryObj *pdblLib);

/*
 *  ======== DBL_close ========
 *  Purpose:
 *  	Close library opened with DBL_open.
 */
void DBL_close(struct DBL_LibraryObj *lib)
{
	struct DBL_LibraryObj *pdblLib = (struct DBL_LibraryObj *)lib;
	u16 i;

	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(pdblLib, DBL_LIBSIGNATURE));

	GT_1trace(DBL_debugMask, GT_ENTER, "DBL_close: lib: 0x%x\n", lib);

	/* Free symbols */
	if (pdblLib->symbols) {
		for (i = 0; i < pdblLib->nSymbols; i++) {
			if (pdblLib->symbols[i].pSymName)
				MEM_Free(pdblLib->symbols[i].pSymName);

		}
		MEM_Free(pdblLib->symbols);
	}

	/* Free DCD sects */
	if (pdblLib->dcdSects) {
		for (i = 0; i < pdblLib->nDCDSects; i++) {
			if (pdblLib->dcdSects[i].pData)
				MEM_Free(pdblLib->dcdSects[i].pData);

		}
		MEM_Free(pdblLib->dcdSects);
	}

	/* Free overlay sects */
	if (pdblLib->ppOvlyData) {
		for (i = 0;  i < pdblLib->nOvlySects;  i++) {
			if (pdblLib->ppOvlyData[i]) {
				if (pdblLib->ppOvlyData[i]->hdr.pName) {
					MEM_Free(pdblLib->ppOvlyData[i]->
						hdr.pName);
				}
				MEM_Free(pdblLib->ppOvlyData[i]);
			}
		}
		MEM_Free(pdblLib->ppOvlyData);
	}

	/* Close the file */
	if (pdblLib->file)
		(*pdblLib->pTarget->dblAttrs.fclose) (pdblLib->file);


	MEM_FreeObject(pdblLib);
}

/*
 *  ======== DBL_create ========
 *  Purpose:
 *  	Create a target object by specifying the alloc, free, and
 *  	write functions for the target.
 */
DSP_STATUS DBL_create(struct DBL_TargetObj **pTarget, struct DBL_Attrs *pAttrs)
{
	struct DBL_TargetObj *pdblTarget = NULL;
	DSP_STATUS status = DSP_SOK;

	DBC_Require(cRefs > 0);
	DBC_Require(pAttrs != NULL);
	DBC_Require(pTarget != NULL);

	GT_2trace(DBL_debugMask, GT_ENTER,
		 "DBL_create: pTarget: 0x%x pAttrs: 0x%x\n",
		 pTarget, pAttrs);
	/* Allocate DBL target object */
	MEM_AllocObject(pdblTarget, struct DBL_TargetObj, DBL_TARGSIGNATURE);
	if (pdblTarget == NULL) {
		GT_0trace(DBL_debugMask, GT_6CLASS,
			 "DBL_create: Memory allocation failed\n");
		status = DSP_EMEMORY;
	} else {
		pdblTarget->dblAttrs = *pAttrs;
		/* Allocate buffer for loading target */
		pdblTarget->pBuf = MEM_Calloc(LOADBUFSIZE, MEM_PAGED);
		if (pdblTarget->pBuf == NULL)
			status = DSP_EMEMORY;

	}
	if (DSP_SUCCEEDED(status)) {
		*pTarget = pdblTarget;
	} else {
		*pTarget = NULL;
		if (pdblTarget)
			DBL_delete(pdblTarget);

	}
	DBC_Ensure(DSP_SUCCEEDED(status) &&
		  ((MEM_IsValidHandle((*pTarget), DBL_TARGSIGNATURE)) ||
		  (DSP_FAILED(status) && *pTarget == NULL)));
	return status;
}

/*
 *  ======== DBL_delete ========
 *  Purpose:
 *  	Delete target object and free resources for any loaded libraries.
 */
void DBL_delete(struct DBL_TargetObj *target)
{
	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(target, DBL_TARGSIGNATURE));

	GT_1trace(DBL_debugMask, GT_ENTER,
		 "DBL_delete: target: 0x%x\n", target);

	if (target->pBuf)
		MEM_Free(target->pBuf);

	MEM_FreeObject(target);
}

/*
 *  ======== DBL_exit ========
 *  Purpose
 *  	Discontinue usage of DBL module.
 */
void DBL_exit()
{
	DBC_Require(cRefs > 0);
	cRefs--;
	GT_1trace(DBL_debugMask, GT_5CLASS,
		 "DBL_exit() ref count: 0x%x\n", cRefs);
	DBC_Ensure(cRefs >= 0);
}

/*
 *  ======== DBL_getAddr ========
 *  Purpose:
 *  	Get address of name in the specified library.
 */
bool DBL_getAddr(struct DBL_LibraryObj *lib, char *name,
		struct DBL_Symbol **ppSym)
{
	bool retVal = false;
	struct Symbol *symbol;
	u16 i;

	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(lib, DBL_LIBSIGNATURE));
	DBC_Require(name != NULL);
	DBC_Require(ppSym != NULL);

	GT_3trace(DBL_debugMask, GT_ENTER,
		 "DBL_getAddr: libt: 0x%x name: %s pAddr: "
		 "0x%x\n", lib, name, ppSym);
	for (i = 0; i < lib->nSymbols; i++) {
		symbol = &lib->symbols[i];
		if (CSL_Strcmp(name, symbol->pSymName) == 0) {
			/* Found it */
			*ppSym = &lib->symbols[i].sym;
			retVal = true;
			break;
		}
	}
	return retVal;
}

/*
 *  ======== DBL_getAttrs ========
 *  Purpose:
 *  	Retrieve the attributes of the target.
 */
void DBL_getAttrs(struct DBL_TargetObj *target, struct DBL_Attrs *pAttrs)
{
	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(target, DBL_TARGSIGNATURE));
	DBC_Require(pAttrs != NULL);
	GT_2trace(DBL_debugMask, GT_ENTER, "DBL_getAttrs: target: 0x%x pAttrs: "
		  "0x%x\n", target, pAttrs);
	*pAttrs = target->dblAttrs;
}

/*
 *  ======== DBL_getCAddr ========
 *  Purpose:
 *  	Get address of "C" name in the specified library.
 */
bool DBL_getCAddr(struct DBL_LibraryObj *lib, char *name,
		 struct DBL_Symbol **ppSym)
{
	bool retVal = false;
	struct Symbol *symbol;
	u16 i;

	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(lib, DBL_LIBSIGNATURE));
	DBC_Require(name != NULL);
	DBC_Require(ppSym != NULL);

	GT_3trace(DBL_debugMask, GT_ENTER,
		 "DBL_getCAddr: target: 0x%x name:%s pAddr:"
		 " 0x%x\n", lib, name, ppSym);
	for (i = 0;  i < lib->nSymbols;  i++) {
		symbol = &lib->symbols[i];
		if ((CSL_Strcmp(name, symbol->pSymName) == 0) ||
		    (CSL_Strcmp(name, symbol->pSymName + 1) == 0 &&
		     symbol->pSymName[0] == '_')) {
			/* Found it */
			*ppSym = &lib->symbols[i].sym;
			retVal = true;
			break;
		}
	}
	return retVal;
}

/*
 *  ======== DBL_getEntry ========
 *  Purpose:
 *  	Get program entry point.
 *
 */
bool DBL_getEntry(struct DBL_LibraryObj *lib, u32 *pEntry)
{
	struct DBL_LibraryObj *pdblLib = (struct DBL_LibraryObj *)lib;

	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(pdblLib, DBL_LIBSIGNATURE));
	DBC_Require(pEntry != NULL);

	GT_2trace(DBL_debugMask, GT_ENTER,
		 "DBL_getEntry: lib: 0x%x pEntry: 0x%x\n", lib, pEntry);
	*pEntry = pdblLib->fileHdr.entry;

	return true;
}

/*
 *  ======== DBL_getSect ========
 *  Purpose:
 *  	Get address and size of a named section.
 */
DSP_STATUS DBL_getSect(struct DBL_LibraryObj *lib, char *name, u32 *pAddr,
		      u32 *pSize)
{
	struct DBL_LibraryObj *pdblLib = (struct DBL_LibraryObj *)lib;
	u16 i;
	DSP_STATUS status = DSP_ENOSECT;

	DBC_Require(cRefs > 0);
	DBC_Require(name != NULL);
	DBC_Require(pAddr != NULL);
	DBC_Require(pSize != NULL);
	DBC_Require(MEM_IsValidHandle(pdblLib, DBL_LIBSIGNATURE));

	GT_4trace(DBL_debugMask, GT_ENTER,
		 "DBL_getSect: lib: 0x%x name: %s pAddr:"
		 " 0x%x pSize: 0x%x\n", lib, name, pAddr, pSize);

	/*
	 *  Check for DCD and overlay sections. Overlay loader uses DBL_getSect
	 *  to determine whether or not a node has overlay sections.
	 *  DCD section names begin with '.'
	 */
	if (name[0] == '.') {
		/* Get DCD section size (address is 0, since it's a NOLOAD). */
		for (i = 0; i < pdblLib->nDCDSects; i++) {
			if (CSL_Strcmp(pdblLib->dcdSects[i].sectHdr.name,
			   name) == 0) {
				*pAddr = 0;
				*pSize = pdblLib->dcdSects[i].sectHdr.size *
					 pdblLib->pTargetInfo->mauSize;
				status = DSP_SOK;
				break;
			}
		}
	} else {
		/* Check for overlay section */
		for (i = 0;  i < pdblLib->nOvlySects;  i++) {
			if (CSL_Strcmp(pdblLib->ppOvlyData[i]->hdr.pName,
			   name) == 0) {
				/* Address and size are meaningless */
				*pAddr = 0;
				*pSize = 0;
				status = DSP_SOK;
				break;
			}
		}
	}

	return status;
}

/*
 *  ======== DBL_init ========
 *  Purpose:
 *  	Initialize DBL module.
 */
bool DBL_init(void)
{
	bool retVal = true;

	DBC_Require(cRefs >= 0);

	if (cRefs == 0) {
		DBC_Assert(!DBL_debugMask.flags);
		GT_create(&DBL_debugMask, "BL"); 	/* "BL" for dBL */

	}

	if (retVal)
		cRefs++;


	GT_1trace(DBL_debugMask, GT_5CLASS, "DBL_init(), ref count:  0x%x\n",
		  cRefs);

	DBC_Ensure((retVal && (cRefs > 0)) || (!retVal && (cRefs >= 0)));

	return retVal;
}

/*
 *  ======== DBL_load ========
 *  Purpose:
 *  	Add symbols/code/data defined in file to that already present
 *  	on the target.
 */
DSP_STATUS DBL_load(struct DBL_LibraryObj *lib, DBL_Flags flags,
		   struct DBL_Attrs *attrs, u32 *pEntry)
{
	struct DBL_LibraryObj *pdblLib = (struct DBL_LibraryObj *)lib;
	struct DBL_TargetObj *dbl;
	u16 i;
	u16 nSects;
	DSP_STATUS status = DSP_EFAIL;

	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(pdblLib, DBL_LIBSIGNATURE));
	DBC_Require(pEntry != NULL);
	DBC_Require(attrs != NULL);

	GT_4trace(DBL_debugMask, GT_ENTER, "DBL_load: lib: 0x%x flags: "
		 "0x%x attrs: 0x%x pEntry: 0x%x\n", lib, flags, attrs, pEntry);

	dbl = pdblLib->pTarget;
	*pEntry = pdblLib->fileHdr.entry;
	nSects = pdblLib->fileHdr.numSects;
	dbl->dblAttrs = *attrs;

	for (i = 0; i < nSects; i++) {
		/* Load the section at the current file offset */
		status = loadSect(dbl, lib);
		if (DSP_FAILED(status))
			break;

	}

	/* Done with file, we can close it */
	if (pdblLib->file) {
		(*pdblLib->pTarget->dblAttrs.fclose) (pdblLib->file);
		pdblLib->file = NULL;
	}
	return status;
}

/*
 *  ======== DBL_loadSect ========
 *  Purpose:
 *  	Load a named section from an library (for overlay support).
 */
DSP_STATUS DBL_loadSect(struct DBL_LibraryObj *lib, char *sectName,
			struct DBL_Attrs *attrs)
{
	struct DBL_TargetObj *dbl;
	s32 i;
	s32 phase;
	s32 offset = -1;
	s32 nSects = -1;
	s32 allocdSects = 0;
	u32 loadAddr;
	u32 runAddr;
	u32 size;
	u32 space;
	u32 ulBytes;
	u16 mauSize;
	u16 wordSize;
	u16 *phaseRef = NULL;
	u16 *otherRef = NULL;
	char *name = NULL;
	struct OvlyData *pOvlyData;
	DSP_STATUS status = DSP_ENOSECT;

	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(lib, DBL_LIBSIGNATURE));
	DBC_Require(sectName != NULL);
	DBC_Require(attrs != NULL);
	DBC_Require(attrs->write != NULL);
	GT_3trace(DBL_debugMask, GT_ENTER,
		 "DBL_loadSect: lib: 0x%x sectName: %s "
		 "attrs: 0x%x\n", lib, sectName, attrs);
	dbl = lib->pTarget;
	mauSize = lib->pTargetInfo->mauSize;
	wordSize = lib->pTargetInfo->wordSize;
	/* Check for match of sect name in overlay table */
	for (i = 0; i < lib->nOvlySects; i++) {
		name = lib->ppOvlyData[i]->hdr.pName;
		if (!CSL_Strncmp(name, sectName, CSL_Strlen(name))) {
			/* Match found */
			status = DSP_SOK;
			break;
		}
	}
	if (DSP_SUCCEEDED(status)) {
		DBC_Assert(i < lib->nOvlySects);
		pOvlyData = lib->ppOvlyData[i];
		/*
		 *  If node overlay, phase will be encoded in name. If not node
		 *  overlay, set phase to NONE.
		 */
		phase = (CSL_Strcmp(name, sectName)) ?
			CSL_Atoi(sectName + CSL_Strlen(sectName) - 1) : NONE;
		 /*  Get reference count of node phase to be loaded, offset into
		 *  overlay data array, and number of sections to overlay.  */
		switch (phase) {
		case NONE:
			/* Not a node overlay */
			phaseRef = &pOvlyData->hdr.otherRef;
			nSects = numOtherSects(pOvlyData);
			offset = otherOffset(pOvlyData);
			break;
		case CREATEPHASE:
			phaseRef = &pOvlyData->hdr.createRef;
			otherRef = &pOvlyData->hdr.otherRef;
			if (*otherRef) {
				/* The overlay sections where node phase was
				 * not specified, have already been loaded.  */
				nSects = numCreateSects(pOvlyData);
				offset = createOffset(pOvlyData);
			} else {
				/* Overlay sections where node phase was not
				 * specified get loaded at create time, along
				 * with create sects.  */
				nSects = numCreateSects(pOvlyData) +
					 numOtherSects(pOvlyData);
				offset = otherOffset(pOvlyData);
			}
			break;
		case DELETEPHASE:
			phaseRef = &pOvlyData->hdr.deleteRef;
			nSects = numDeleteSects(pOvlyData);
			offset = deleteOffset(pOvlyData);
			break;
		case EXECUTEPHASE:
			phaseRef = &pOvlyData->hdr.executeRef;
			nSects = numExecuteSects(pOvlyData);
			offset = executeOffset(pOvlyData);
			break;
		default:
			/* ERROR */
			DBC_Assert(false);
			break;
		}
		/* Do overlay if reference count is 0 */
		if (!(*phaseRef)) {
			/* "Allocate" all sections */
			for (i = 0; i < nSects; i++) {
				runAddr = pOvlyData->data[offset + i].runAddr;
				size = pOvlyData->data[offset + i].size;
				space = pOvlyData->data[offset + i].page;
				status = (dbl->dblAttrs.alloc)(dbl->dblAttrs.
					 rmmHandle, space, size, 0,
					 &runAddr, true);
				if (DSP_FAILED(status))
					break;

				allocdSects++;
			}
			if (DSP_SUCCEEDED(status)) {
				/* Load sections */
				for (i = 0; i < nSects; i++) {
					loadAddr = pOvlyData->data[offset + i].
						   loadAddr;
					runAddr = pOvlyData->data[offset + i].
						  runAddr;
					size = pOvlyData->data[offset + i].
						size;
					space = pOvlyData->data[offset + i].
						page;
					/* Convert to word address, call
					 * write function */
					loadAddr /= (wordSize / mauSize);
					runAddr /= (wordSize / mauSize);
					ulBytes = size * mauSize;
					if ((*attrs->write)(attrs->wHandle,
					   runAddr, (void *)loadAddr, ulBytes,
					   space) != ulBytes) {
						GT_0trace(DBL_debugMask,
							GT_6CLASS,
							"DBL_loadSect: write"
							" failed\n");
						status = DSP_EFWRITE;
						break;
					}
				}
			}
			/* Free sections on failure */
			if (DSP_FAILED(status))
				freeSects(dbl, pOvlyData, offset, allocdSects);

		}
	}
	if (DSP_SUCCEEDED(status)) {
		/* Increment reference counts */
		if (otherRef)
			*otherRef = *otherRef + 1;

		*phaseRef = *phaseRef + 1;
	}
	return status;
}

/*
 *  ======== DBL_open ========
 *  Purpose:
 *  	DBL_open() returns a library handle that can be used to
 *  	load/unload the symbols/code/data via DBL_load()/DBL_unload().
 */
DSP_STATUS DBL_open(struct DBL_TargetObj *target, char *file, DBL_Flags flags,
		   struct DBL_LibraryObj **pLib)
{
	struct DBL_LibraryObj *pdblLib = NULL;
	u16 nSymbols;
	u16 nDCDSects;
	DSP_STATUS status = DSP_SOK;
	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(target, DBL_TARGSIGNATURE));
	DBC_Require(target->dblAttrs.fopen != NULL);
	DBC_Require(file != NULL);
	DBC_Require(pLib != NULL);

	GT_3trace(DBL_debugMask, GT_ENTER, "DBL_open: target: 0x%x file: %s "
		 "pLib: 0x%x\n", target, file, pLib);
	/* Allocate DBL library object */
	MEM_AllocObject(pdblLib, struct DBL_LibraryObj, DBL_LIBSIGNATURE);
	if (pdblLib == NULL)
		status = DSP_EMEMORY;

	/* Open the file */
	if (DSP_SUCCEEDED(status)) {
		pdblLib->pTarget = target;
		pdblLib->file = (*target->dblAttrs.fopen)(file, "rb");
		if (pdblLib->file == NULL)
			status = DSP_EFOPEN;

	}
	/* Read file header */
	if (DSP_SUCCEEDED(status)) {
		status = readHeader(target, pdblLib);
		if (DSP_FAILED(status)) {
			GT_0trace(DBL_debugMask, GT_6CLASS,
				 "DBL_open(): Failed to read file header\n");
		}
	}
	/* Allocate symbol table */
	if (DSP_SUCCEEDED(status)) {
		nSymbols = pdblLib->nSymbols = pdblLib->fileHdr.numSymbols;
		pdblLib->symbols = MEM_Calloc(nSymbols * sizeof(struct Symbol),
					     MEM_PAGED);
		if (pdblLib->symbols == NULL)
			status = DSP_EMEMORY;

	}
	/* Read all the symbols */
	if (DSP_SUCCEEDED(status)) {
		status = readSymbols(target, pdblLib);
		if (DSP_FAILED(status)) {
			GT_0trace(DBL_debugMask, GT_6CLASS,
				 "DBL_open(): Failed to read symbols\n");
		}
	}
	/* Allocate DCD sect table */
	if (DSP_SUCCEEDED(status)) {
		nDCDSects = pdblLib->nDCDSects = pdblLib->fileHdr.numDCDSects;
		pdblLib->dcdSects = MEM_Calloc(nDCDSects *
					 sizeof(struct DCDSect), MEM_PAGED);
		if (pdblLib->dcdSects == NULL)
			status = DSP_EMEMORY;

	}
	/* Read DCD sections */
	if (DSP_SUCCEEDED(status)) {
		status = readDCDSects(target, pdblLib);
		if (DSP_FAILED(status)) {
			GT_0trace(DBL_debugMask, GT_6CLASS,
				 "DBL_open(): Failed to read DCD sections\n");
		}
	}
	/* Read overlay sections */
	if (DSP_SUCCEEDED(status)) {
		status = readOvlySects(target, pdblLib);
		if (DSP_FAILED(status)) {
			GT_0trace(DBL_debugMask, GT_6CLASS,
				 "DBL_open(): Failed to read "
				 "overlay sections\n");
		}
	}
	if (DSP_FAILED(status)) {
		*pLib = NULL;
		if (pdblLib != NULL)
			DBL_close((struct DBL_LibraryObj *) pdblLib);

	} else {
		*pLib = pdblLib;
	}
	DBC_Ensure((DSP_SUCCEEDED(status) &&
		  (MEM_IsValidHandle((*pLib), DBL_LIBSIGNATURE))) ||
		  (DSP_FAILED(status) && *pLib == NULL));
	return status;
}

/*
 *  ======== DBL_readSect ========
 *  Purpose:
 *  	Read COFF section into a character buffer.
 */
DSP_STATUS DBL_readSect(struct DBL_LibraryObj *lib, char *name, char *pContent,
			u32 size)
{
	struct DBL_LibraryObj *pdblLib = (struct DBL_LibraryObj *)lib;
	u16 i;
	u32 mauSize;
	u32 max;
	DSP_STATUS status = DSP_ENOSECT;

	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(pdblLib, DBL_LIBSIGNATURE));
	DBC_Require(name != NULL);
	DBC_Require(pContent != NULL);
	DBC_Require(size != 0);
	GT_4trace(DBL_debugMask, GT_ENTER, "DBL_readSect: lib: 0x%x name: %s "
		 "pContent: 0x%x size: 0x%x\n", lib, name, pContent, size);

	mauSize = pdblLib->pTargetInfo->mauSize;

	/* Attempt to find match with DCD section names. */
	for (i = 0; i < pdblLib->nDCDSects; i++) {
		if (CSL_Strcmp(pdblLib->dcdSects[i].sectHdr.name, name) == 0) {
			/* Match found */
			max = pdblLib->dcdSects[i].sectHdr.size * mauSize;
			max = (max > size) ? size : max;
			memcpy(pContent, pdblLib->dcdSects[i].pData, max);
			status = DSP_SOK;
			break;
		}
	}

	return status;
}

/*
 *  ======== DBL_setAttrs ========
 *  Purpose:
 *  	Set the attributes of the target.
 */
void DBL_setAttrs(struct DBL_TargetObj *target, struct DBL_Attrs *pAttrs)
{
	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(target, DBL_TARGSIGNATURE));
	DBC_Require(pAttrs != NULL);

	GT_2trace(DBL_debugMask, GT_ENTER, "DBL_setAttrs: target: 0x%x pAttrs: "
		 "0x%x\n", target, pAttrs);

	target->dblAttrs = *pAttrs;
}

/*
 *  ======== DBL_unload ========
 *  Purpose:
 *  	Remove the symbols/code/data corresponding to the library lib.
 */
void DBL_unload(struct DBL_LibraryObj *lib, struct DBL_Attrs *attrs)
{
	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(lib, DBL_LIBSIGNATURE));

	GT_1trace(DBL_debugMask, GT_ENTER, "DBL_unload: lib: 0x%x\n", lib);

	/* Nothing to do for static loading */
}

/*
 *  ======== DBL_unloadSect ========
 *  Purpose:
 *  	Unload a named section from an library (for overlay support).
 */
DSP_STATUS DBL_unloadSect(struct DBL_LibraryObj *lib, char *sectName,
			  struct DBL_Attrs *attrs)
{
	struct DBL_TargetObj *dbl;
	s32 i;
	s32 phase;
	s32 offset = -1;
	s32 nSects = -1;
	u16 *phaseRef = NULL;
	u16 *otherRef = NULL;
	char *pName = NULL;
	struct OvlyData *pOvlyData;
	DSP_STATUS status = DSP_ENOSECT;

	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(lib, DBL_LIBSIGNATURE));
	DBC_Require(sectName != NULL);

	GT_2trace(DBL_debugMask, GT_ENTER,
		 "DBL_unloadSect: lib: 0x%x sectName: %s\n", lib, sectName);
	dbl = lib->pTarget;
	/* Check for match of sect name in overlay table */
	for (i = 0; i < lib->nOvlySects; i++) {
		pName = lib->ppOvlyData[i]->hdr.pName;
		if (!CSL_Strncmp(pName, sectName, CSL_Strlen(pName))) {
			/* Match found */
			status = DSP_SOK;
			break;
		}
	}
	if (DSP_SUCCEEDED(status)) {
		DBC_Assert(i < lib->nOvlySects);
		pOvlyData = lib->ppOvlyData[i];
		/* If node overlay, phase will be encoded in name. */
		phase = (CSL_Strcmp(pName, sectName)) ?
			CSL_Atoi(sectName + CSL_Strlen(sectName) - 1) : NONE;
		switch (phase) {
		case NONE:
			nSects = numOtherSects(pOvlyData);
			phaseRef = &pOvlyData->hdr.otherRef;
			offset = otherOffset(pOvlyData);
			break;
		case CREATEPHASE:
			nSects = numCreateSects(pOvlyData);
			offset = createOffset(pOvlyData);
			phaseRef = &pOvlyData->hdr.createRef;
			break;
		case DELETEPHASE:
			nSects = numDeleteSects(pOvlyData);
			offset = deleteOffset(pOvlyData);
			phaseRef = &pOvlyData->hdr.deleteRef;
			otherRef = &pOvlyData->hdr.otherRef;
			break;
		case EXECUTEPHASE:
			nSects = numExecuteSects(pOvlyData);
			offset = executeOffset(pOvlyData);
			phaseRef = &pOvlyData->hdr.executeRef;
			break;
		default:
			/* ERROR */
			DBC_Assert(false);
			break;
		}
		if (*phaseRef) {
			*phaseRef = *phaseRef - 1;
			if (*phaseRef == 0) {
				/* Unload overlay sections for phase */
				freeSects(dbl, pOvlyData, offset, nSects);
			}
			if (phase == DELETEPHASE) {
				DBC_Assert(*otherRef);
				*otherRef = *otherRef - 1;
				if (*otherRef == 0) {
					/* Unload other overlay sections */
					nSects = numOtherSects(pOvlyData);
					offset = otherOffset(pOvlyData);
					freeSects(dbl, pOvlyData, offset,
						 nSects);
				}
			}
		}
	}

	return status;
}

/*
 *  ======== freeSects ========
 *  Purpose:
 *  	Free section
 */
static void freeSects(struct DBL_TargetObj *dbl, struct OvlyData *pOvlyData,
		     s32 offset, s32 nSects)
{
	u32 runAddr;
	u32 size;
	u32 space;
	s32 i;

	for (i = 0; i < nSects; i++) {
		runAddr = pOvlyData->data[offset + i].runAddr;
		size = pOvlyData->data[offset + i].size;
		space = pOvlyData->data[offset + i].page;
		if (!(dbl->dblAttrs.free)
		    (dbl->dblAttrs.rmmHandle, space, runAddr, size, true)) {
			/*
			 *  Free function will not fail for overlay, unless
			 *  address passed in is bad.
			 */
			DBC_Assert(false);
		}
	}
}

/*
 *  ======== loadSect ========
 *  Purpose:
 *  	Load section to target
 */
static DSP_STATUS loadSect(struct DBL_TargetObj *dbl,
			  struct DBL_LibraryObj *pdblLib)
{
	struct DBOF_SectHdr sectHdr;
	char *pBuf;
	struct KFILE_FileObj *file;
	u32 space;
	u32 addr;
	u32 total;
	u32 nWords = 0;
	u32 nBytes = 0;
	u16 mauSize;
	u32 bufSize;
	DSP_STATUS status = DSP_SOK;

	file = pdblLib->file;
	mauSize = pdblLib->pTargetInfo->mauSize;
	bufSize = LOADBUFSIZE / mauSize;
	pBuf = dbl->pBuf;

	/* Read the section header */
	if ((*dbl->dblAttrs.fread)(&sectHdr, sizeof(struct DBOF_SectHdr),
	   1, file) != 1) {
		GT_0trace(DBL_debugMask, GT_6CLASS,
			 "Failed to read DCD sect header\n");
		status = DSP_EFREAD;
	} else {
		if (pdblLib->byteSwapped) {
			sectHdr.size = SWAPLONG(sectHdr.size);
			sectHdr.addr = SWAPLONG(sectHdr.addr);
			sectHdr.page = SWAPWORD(sectHdr.page);
		}
	}
	if (DSP_SUCCEEDED(status)) {
		addr = sectHdr.addr;
		space = sectHdr.page;
		for (total = sectHdr.size; total > 0; total -= nWords) {
			nWords = min(total, bufSize);
			nBytes = nWords * mauSize;
			/* Read section data */
			if ((*dbl->dblAttrs.fread)(pBuf, nBytes, 1,
			   file) != 1) {
				GT_0trace(DBL_debugMask, GT_6CLASS,
					 "Failed to read DCD sect header\n");
				status = DSP_EFREAD;
				break;
			}
			/* Write section to target */
			if (!(*dbl->dblAttrs.write)(dbl->dblAttrs.wHandle,
			   addr, pBuf, nBytes, space)) {
				GT_0trace(DBL_debugMask, GT_6CLASS,
					 "Failed to write section data\n");
				status = DSP_EFWRITE;
				break;
			}
			addr += nWords;
		}
	}
	return status;
}

/*
 *  ======== readDCDSects ========
 *  Purpose:
 *  	Read DCD sections.
 */
static DSP_STATUS readDCDSects(struct DBL_TargetObj *dbl,
			      struct DBL_LibraryObj *pdblLib)
{
	struct DBOF_DCDSectHdr *pSectHdr;
	struct DCDSect *pSect;
	struct KFILE_FileObj *file;
	u16 nSects;
	u16 i;
	u16 mauSize;
	DSP_STATUS status = DSP_SOK;

	file = pdblLib->file;
	mauSize = pdblLib->pTargetInfo->mauSize;
	nSects = pdblLib->fileHdr.numDCDSects;
	for (i = 0; i < nSects; i++) {
		pSect = &pdblLib->dcdSects[i];
		pSectHdr = &pdblLib->dcdSects[i].sectHdr;
		/* Read sect header */
		if ((*dbl->dblAttrs.fread)(pSectHdr,
		   sizeof(struct DBOF_DCDSectHdr), 1, file) != 1) {
			GT_0trace(DBL_debugMask, GT_6CLASS,
				 "Failed to read DCD sect header\n");
			status = DSP_EFREAD;
			break;
		}
		if (pdblLib->byteSwapped)
			pSectHdr->size = SWAPLONG(pSectHdr->size);

		pSect->pData = (char *)MEM_Calloc(pSectHdr->size *
				mauSize, MEM_PAGED);
		if (pSect->pData == NULL) {
			GT_2trace(DBL_debugMask, GT_6CLASS,
				 "Memory allocation for sect %s "
				 "data failed: Size: 0x%lx\n", pSectHdr->name,
				 pSectHdr->size);
			status = DSP_EMEMORY;
			break;
		}
		/* Read DCD sect data */
		if ((*dbl->dblAttrs.fread)(pSect->pData, mauSize,
		   pSectHdr->size, file) != pSectHdr->size) {
			GT_0trace(DBL_debugMask, GT_6CLASS,
				  "Failed to read DCD sect data\n");
			status = DSP_EFREAD;
			break;
		}
	}

	return status;
}

/*
 *  ======== readHeader ========
 *  Purpose:
 *  	Read Header.
 */
static DSP_STATUS readHeader(struct DBL_TargetObj *dbl,
			    struct DBL_LibraryObj *pdblLib)
{
	struct KFILE_FileObj *file;
	s32 i;
	struct DBOF_FileHdr *pHdr;
	u32 swapMagic;
	DSP_STATUS status = DSP_SOK;

	pdblLib->byteSwapped = false;
	file = pdblLib->file;
	pHdr = &pdblLib->fileHdr;
	if ((*dbl->dblAttrs.fread)(pHdr, sizeof(struct DBOF_FileHdr), 1,
	   file) != 1) {
		GT_0trace(DBL_debugMask, GT_6CLASS,
			 "readHeader: Failed to read file header\n");
		status = DSP_EFREAD;
	}

	if (DSP_SUCCEEDED(status)) {
		/* Determine if byte swapped */
		for (i = 0; i < NUMTARGS; i++) {
			swapMagic = SWAPLONG(pHdr->magic);
			if (pHdr->magic == magicTab[i] || swapMagic ==
			   magicTab[i]) {
				if (swapMagic == magicTab[i]) {
					pdblLib->byteSwapped = true;
					pHdr->magic = SWAPLONG(pHdr->magic);
					pHdr->entry = SWAPLONG(pHdr->entry);
					pHdr->symOffset = SWAPLONG(pHdr->
								symOffset);
					pHdr->dcdSectOffset = SWAPLONG(pHdr->
								dcdSectOffset);
					pHdr->loadSectOffset = SWAPLONG(pHdr->
								loadSectOffset);
					pHdr->ovlySectOffset = SWAPLONG(pHdr->
								ovlySectOffset);
					pHdr->numSymbols = SWAPWORD(pHdr->
								numSymbols);
					pHdr->numDCDSects = SWAPWORD(pHdr->
								numDCDSects);
					pHdr->numSects = SWAPWORD(pHdr->
								numSects);
					pHdr->numOvlySects = SWAPWORD(pHdr->
								numOvlySects);
				}
				break;
			}
		}
		if (i == NUMTARGS) {
			GT_0trace(DBL_debugMask, GT_6CLASS,
				 "readHeader: Failed to determine"
				 " target type\n");
			status = DSP_ECORRUPTFILE;
		} else {
			pdblLib->pTargetInfo = &targetTab[i];
			GT_1trace(DBL_debugMask, GT_ENTER,
				 "COF type: 0x%lx\n", pHdr->magic);
			GT_1trace(DBL_debugMask, GT_ENTER,
				 "Entry point:0x%lx\n", pHdr->entry);
		}
	}
	return status;
}

/*
 *  ======== readOvlySects ========
 *  Purpose:
 *  	Read Overlay Sections
 */
static DSP_STATUS readOvlySects(struct DBL_TargetObj *dbl,
				struct DBL_LibraryObj *pdblLib)
{
	struct DBOF_OvlySectHdr hdr;
	struct DBOF_OvlySectData *pData;
	struct OvlyData *pOvlyData;
	char *pName;
	struct KFILE_FileObj *file;
	u16 i, j;
	u16 nSects;
	u16 n;
	DSP_STATUS status = DSP_SOK;

	pdblLib->nOvlySects = nSects = pdblLib->fileHdr.numOvlySects;
	file = pdblLib->file;
	if (nSects > 0) {
		pdblLib->ppOvlyData = MEM_Calloc(nSects * sizeof(OvlyData *),
						 MEM_PAGED);
		if (pdblLib->ppOvlyData == NULL) {
			GT_0trace(DBL_debugMask, GT_7CLASS,
				 "Failed to allocatate overlay "
				 "data memory\n");
			status = DSP_EMEMORY;
		}
	}
	if (DSP_SUCCEEDED(status)) {
		/* Read overlay data for each node */
		for (i = 0; i < nSects; i++) {
			/* Read overlay section header */
			if ((*dbl->dblAttrs.fread)(&hdr,
			   sizeof(struct DBOF_OvlySectHdr), 1, file) != 1) {
				GT_0trace(DBL_debugMask, GT_6CLASS,
					 "Failed to read overlay sect"
					 " header\n");
				status = DSP_EFREAD;
				break;
			}
			if (pdblLib->byteSwapped) {
				hdr.nameLen = SWAPWORD(hdr.nameLen);
				hdr.numCreateSects =
						SWAPWORD(hdr.numCreateSects);
				hdr.numDeleteSects =
						SWAPWORD(hdr.numDeleteSects);
				hdr.numExecuteSects =
						SWAPWORD(hdr.numExecuteSects);
				hdr.numOtherSects =
						SWAPWORD(hdr.numOtherSects);
				hdr.resvd = SWAPWORD(hdr.resvd);
			}
			n = hdr.numCreateSects + hdr.numDeleteSects +
			    hdr.numExecuteSects + hdr.numOtherSects;

			/* Allocate memory for node's overlay data */
			pOvlyData = (struct OvlyData *)MEM_Calloc
				    (sizeof(struct OvlyHdr) +
				    n * sizeof(struct DBOF_OvlySectData),
				    MEM_PAGED);
			if (pOvlyData == NULL) {
				GT_0trace(DBL_debugMask, GT_7CLASS,
					 "Failed to allocatate ovlyay"
					 " data memory\n");
				status = DSP_EMEMORY;
				break;
			}
			pOvlyData->hdr.dbofHdr = hdr;
			pdblLib->ppOvlyData[i] = pOvlyData;
			/* Allocate memory for section name */
			pName = (char *)MEM_Calloc(hdr.nameLen + 1, MEM_PAGED);
			if (pName == NULL) {
				GT_0trace(DBL_debugMask, GT_7CLASS,
					 "Failed to allocatate ovlyay"
					 " section name\n");
				status = DSP_EMEMORY;
				break;
			}
			pOvlyData->hdr.pName = pName;
			/* Read the overlay section name */
			if ((*dbl->dblAttrs.fread)(pName, sizeof(char),
			   hdr.nameLen, file) != hdr.nameLen) {
				GT_0trace(DBL_debugMask, GT_7CLASS,
					 "readOvlySects: Unable to "
					 "read overlay name.\n");
				status = DSP_EFREAD;
				break;
			}
			/* Read the overlay section data */
			pData = pOvlyData->data;
			if ((*dbl->dblAttrs.fread)(pData,
			   sizeof(struct DBOF_OvlySectData), n, file) != n) {
				GT_0trace(DBL_debugMask, GT_7CLASS,
					 "readOvlySects: Unable to "
					 "read overlay data.\n");
				status = DSP_EFREAD;
				break;
			}
			/* Swap overlay data, if necessary */
			if (pdblLib->byteSwapped) {
				for (j = 0; j < n; j++) {
					pData[j].loadAddr =
						 SWAPLONG(pData[j].loadAddr);
					pData[j].runAddr =
						 SWAPLONG(pData[j].runAddr);
					pData[j].size =
						 SWAPLONG(pData[j].size);
					pData[j].page =
						 SWAPWORD(pData[j].page);
				}
			}
		}
	}
	return status;
}

/*
 *  ======== readSymbols ========
 *  Purpose:
 *  	Read Symbols
 */
static DSP_STATUS readSymbols(struct DBL_TargetObj *dbl,
			     struct DBL_LibraryObj *pdblLib)
{
	struct DBOF_SymbolHdr symHdr;
	struct KFILE_FileObj *file;
	u16 i;
	u16 nSymbols;
	u16 len;
	char *pName = NULL;
	DSP_STATUS status = DSP_SOK;

	file = pdblLib->file;

	nSymbols = pdblLib->fileHdr.numSymbols;

	for (i = 0; i < nSymbols; i++) {
		/* Read symbol value */
		if ((*dbl->dblAttrs.fread)(&symHdr,
		   sizeof(struct DBOF_SymbolHdr), 1, file) != 1) {
			GT_0trace(DBL_debugMask, GT_6CLASS,
				 "Failed to read symbol value\n");
			status = DSP_EFREAD;
			break;
		}
		if (pdblLib->byteSwapped) {
			symHdr.nameLen = SWAPWORD(symHdr.nameLen);
			symHdr.value = SWAPLONG(symHdr.value);
		}
		/* Allocate buffer for symbol name */
		len = symHdr.nameLen;
		pName = (char *)MEM_Calloc(len + 1, MEM_PAGED);
		if (pName == NULL) {
			GT_0trace(DBL_debugMask, GT_6CLASS,
				 "Memory allocation failed\n");
			status = DSP_EMEMORY;
			break;
		}
		pdblLib->symbols[i].pSymName = pName;
		pdblLib->symbols[i].sym.value = symHdr.value;
		/* Read symbol name */
		if ((*dbl->dblAttrs.fread) (pName, sizeof(char), len, file) !=
		   len) {
			GT_0trace(DBL_debugMask, GT_6CLASS,
				 "Failed to read symbol value\n");
			status = DSP_EFREAD;
			break;
		} else {
			pName[len] = '\0';
			GT_2trace(DBL_debugMask, GT_ENTER,
				 "Symbol: %s  Value: 0x%lx\n",
				 pName, symHdr.value);
		}
	}
	return status;
}

