/*
 * dbll.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 *
 * This package is free software;  you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

/*
 *  ======== dbll.c ========
 *
 *! Revision History
 *! ================
 *! 25-Apr-2030 map:    Fixed symbol redefinition bug + unload and return error
 *! 08-Apr-2003 map: 	Consolidated DBL with DBLL loader name
 *! 24-Mar-2003 map:    Updated findSymbol to support dllview update
 *! 23-Jan-2003 map:    Updated rmmAlloc to support memory granularity
 *! 21-Nov-2002 map:    Combine fopen and DLOAD_module_open to increase
 *!         performance on start.
 *! 04-Oct-2002 map:    Integrated new TIP dynamic loader w/ DOF api.
 *! 27-Sep-2002 map:    Changed handle passed to RemoteFree, instead of
 *!         RMM_free;  added GT_trace to rmmDealloc
 *! 20-Sep-2002 map:    Updated from Code Review
 *! 08-Aug-2002 jeh:    Updated to support overlays.
 *! 25-Jun-2002 jeh:    Pass RMM_Addr object to alloc function in rmmAlloc().
 *! 20-Mar-2002 jeh:    Created.
 */

/*  ----------------------------------- Host OS */
#include <dspbridge/host_os.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>
#include <dspbridge/dbdefs.h>
#include <dspbridge/errbase.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/gt.h>
#include <dspbridge/dbc.h>
#include <dspbridge/gh.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/csl.h>
#include <dspbridge/mem.h>

/* Dynamic loader library interface */
#include <dspbridge/dynamic_loader.h>
#include <dspbridge/getsection.h>

/*  ----------------------------------- This */
#include <dspbridge/dbll.h>
#include <dspbridge/rmm.h>

#define DBLL_TARGSIGNATURE      0x544c4c44	/* "TLLD" */
#define DBLL_LIBSIGNATURE       0x4c4c4c44	/* "LLLD" */

/* Number of buckets for symbol hash table */
#define MAXBUCKETS 211

/* Max buffer length */
#define MAXEXPR 128

#ifndef UINT32_C
#define UINT32_C(zzz) ((uint32_t)zzz)
#endif
#define DOFF_ALIGN(x) (((x) + 3) & ~UINT32_C(3))

/*
 *  ======== struct DBLL_TarObj* ========
 *  A target may have one or more libraries of symbols/code/data loaded
 *  onto it, where a library is simply the symbols/code/data contained
 *  in a DOFF file.
 */
/*
 *  ======== DBLL_TarObj ========
 */
struct DBLL_TarObj {
	u32 dwSignature; 	/* For object validation */
	struct DBLL_Attrs attrs;
	struct DBLL_LibraryObj *head; 	/* List of all opened libraries */
} ;

/*
 *  The following 4 typedefs are "super classes" of the dynamic loader
 *  library types used in dynamic loader functions (dynamic_loader.h).
 */
/*
 *  ======== DBLLStream ========
 *  Contains Dynamic_Loader_Stream
 */
struct DBLLStream {
	struct Dynamic_Loader_Stream dlStream;
	struct DBLL_LibraryObj *lib;
} ;

/*
 *  ======== DBLLSymbol ========
 */
struct DBLLSymbol {
	struct Dynamic_Loader_Sym dlSymbol;
	struct DBLL_LibraryObj *lib;
} ;

/*
 *  ======== DBLLAlloc ========
 */
 struct DBLLAlloc {
	struct Dynamic_Loader_Allocate dlAlloc;
	struct DBLL_LibraryObj *lib;
} ;

/*
 *  ======== DBLLInit ========
 */
struct DBLLInit {
	struct Dynamic_Loader_Initialize dlInit;
	struct DBLL_LibraryObj *lib;
};

/*
 *  ======== DBLL_Library ========
 *  A library handle is returned by DBLL_Open() and is passed to DBLL_load()
 *  to load symbols/code/data, and to DBLL_unload(), to remove the
 *  symbols/code/data loaded by DBLL_load().
 */

/*
 *  ======== DBLL_LibraryObj ========
 */
 struct DBLL_LibraryObj {
	u32 dwSignature; 	/* For object validation */
	struct DBLL_LibraryObj *next; 	/* Next library in target's list */
	struct DBLL_LibraryObj *prev; 	/* Previous in the list */
	struct DBLL_TarObj *pTarget; 	/* target for this library */

	/* Objects needed by dynamic loader */
	struct DBLLStream stream;
	struct DBLLSymbol symbol;
	struct DBLLAlloc allocate;
	struct DBLLInit init;
	DLOAD_mhandle mHandle;

	char *fileName; 	/* COFF file name */
	void *fp; 		/* Opaque file handle */
	u32 entry; 		/* Entry point */
	DLOAD_mhandle desc; 	/* desc of DOFF file loaded */
	u32 openRef; 		/* Number of times opened */
	u32 loadRef; 		/* Number of times loaded */
	struct GH_THashTab *symTab; 	/* Hash table of symbols */
	u32 ulPos;
} ;

/*
 *  ======== Symbol ========
 */
struct Symbol {
	struct DBLL_Symbol value;
	char *name;
} ;
extern bool bSymbolsReloaded;

static void dofClose(struct DBLL_LibraryObj *zlLib);
static DSP_STATUS dofOpen(struct DBLL_LibraryObj *zlLib);
static s32 NoOp(struct Dynamic_Loader_Initialize *thisptr, void *bufr,
		LDR_ADDR locn, struct LDR_SECTION_INFO *info, unsigned bytsiz);

/*
 *  Functions called by dynamic loader
 *
 */
/* Dynamic_Loader_Stream */
static int readBuffer(struct Dynamic_Loader_Stream *this, void *buffer,
		     unsigned bufsize);
static int setFilePosn(struct Dynamic_Loader_Stream *this, unsigned int pos);
/* Dynamic_Loader_Sym */
static struct dynload_symbol *findSymbol(struct Dynamic_Loader_Sym *this,
					const char *name);
static struct dynload_symbol *addToSymbolTable(struct Dynamic_Loader_Sym *this,
					      const char *name,
					      unsigned moduleId);
static struct dynload_symbol *findInSymbolTable(struct Dynamic_Loader_Sym *this,
						const char *name,
						unsigned moduleid);
static void purgeSymbolTable(struct Dynamic_Loader_Sym *this,
			    unsigned moduleId);
static void *allocate(struct Dynamic_Loader_Sym *this, unsigned memsize);
static void deallocate(struct Dynamic_Loader_Sym *this, void *memPtr);
static void errorReport(struct Dynamic_Loader_Sym *this, const char *errstr,
			va_list args);
/* Dynamic_Loader_Allocate */
static int rmmAlloc(struct Dynamic_Loader_Allocate *this,
		   struct LDR_SECTION_INFO *info, unsigned align);
static void rmmDealloc(struct Dynamic_Loader_Allocate *this,
		      struct LDR_SECTION_INFO *info);

/* Dynamic_Loader_Initialize */
static int connect(struct Dynamic_Loader_Initialize *this);
static int readMem(struct Dynamic_Loader_Initialize *this, void *buf,
		  LDR_ADDR addr, struct LDR_SECTION_INFO *info,
		  unsigned nbytes);
static int writeMem(struct Dynamic_Loader_Initialize *this, void *buf,
		   LDR_ADDR addr, struct LDR_SECTION_INFO *info,
		   unsigned nbytes);
static int fillMem(struct Dynamic_Loader_Initialize *this, LDR_ADDR addr,
		   struct LDR_SECTION_INFO *info, unsigned nbytes,
		   unsigned val);
static int execute(struct Dynamic_Loader_Initialize *this, LDR_ADDR start);
static void release(struct Dynamic_Loader_Initialize *this);

/* symbol table hash functions */
static u16 nameHash(void *name, u16 maxBucket);
static bool nameMatch(void *name, void *sp);
static void symDelete(void *sp);

#if GT_TRACE
static struct GT_Mask DBLL_debugMask = { NULL, NULL };  /* GT trace variable */
#endif

static u32 cRefs; 		/* module reference count */

/* Symbol Redefinition */
static int bRedefinedSymbol;
static int bGblSearch = 1;

/*
 *  ======== DBLL_close ========
 */
void DBLL_close(struct DBLL_LibraryObj *zlLib)
{
	struct DBLL_TarObj *zlTarget;

	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(zlLib, DBLL_LIBSIGNATURE));
	DBC_Require(zlLib->openRef > 0);
	zlTarget = zlLib->pTarget;
	GT_1trace(DBLL_debugMask, GT_ENTER, "DBLL_close: lib: 0x%x\n", zlLib);
	zlLib->openRef--;
	if (zlLib->openRef == 0) {
		/* Remove library from list */
		if (zlTarget->head == zlLib)
			zlTarget->head = zlLib->next;

		if (zlLib->prev)
			(zlLib->prev)->next = zlLib->next;

		if (zlLib->next)
			(zlLib->next)->prev = zlLib->prev;

		/* Free DOF resources */
		dofClose(zlLib);
		if (zlLib->fileName)
			MEM_Free(zlLib->fileName);

		/* remove symbols from symbol table */
		if (zlLib->symTab)
			GH_delete(zlLib->symTab);

		/* remove the library object itself */
		MEM_FreeObject(zlLib);
		zlLib = NULL;
	}
}

/*
 *  ======== DBLL_create ========
 */
DSP_STATUS DBLL_create(struct DBLL_TarObj **pTarget, struct DBLL_Attrs *pAttrs)
{
	struct DBLL_TarObj *pzlTarget;
	DSP_STATUS status = DSP_SOK;

	DBC_Require(cRefs > 0);
	DBC_Require(pAttrs != NULL);
	DBC_Require(pTarget != NULL);

	GT_2trace(DBLL_debugMask, GT_ENTER,
		  "DBLL_create: pTarget: 0x%x pAttrs: "
		  "0x%x\n", pTarget, pAttrs);
	/* Allocate DBL target object */
	MEM_AllocObject(pzlTarget, struct DBLL_TarObj, DBLL_TARGSIGNATURE);
	if (pTarget != NULL) {
		if (pzlTarget == NULL) {
			GT_0trace(DBLL_debugMask, GT_6CLASS,
				 "DBLL_create: Memory allocation"
				 " failed\n");
			*pTarget = NULL;
			status = DSP_EMEMORY;
		} else {
			pzlTarget->attrs = *pAttrs;
			*pTarget = (struct DBLL_TarObj *)pzlTarget;
		}
		DBC_Ensure((DSP_SUCCEEDED(status) &&
			  MEM_IsValidHandle(((struct DBLL_TarObj *)(*pTarget)),
			  DBLL_TARGSIGNATURE)) || (DSP_FAILED(status) &&
			  *pTarget == NULL));
	}

	return status;
}

/*
 *  ======== DBLL_delete ========
 */
void DBLL_delete(struct DBLL_TarObj *target)
{
	struct DBLL_TarObj *zlTarget = (struct DBLL_TarObj *)target;

	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(zlTarget, DBLL_TARGSIGNATURE));

	GT_1trace(DBLL_debugMask, GT_ENTER, "DBLL_delete: target: 0x%x\n",
		 target);

	if (zlTarget != NULL)
		MEM_FreeObject(zlTarget);

}

/*
 *  ======== DBLL_exit ========
 *  Discontinue usage of DBL module.
 */
void DBLL_exit(void)
{
	DBC_Require(cRefs > 0);

	cRefs--;

	GT_1trace(DBLL_debugMask, GT_5CLASS, "DBLL_exit() ref count: 0x%x\n",
		  cRefs);

	if (cRefs == 0) {
		MEM_Exit();
		CSL_Exit();
		GH_exit();
#if GT_TRACE
		DBLL_debugMask.flags = NULL;
#endif
	}

	DBC_Ensure(cRefs >= 0);
}

/*
 *  ======== DBLL_getAddr ========
 *  Get address of name in the specified library.
 */
bool DBLL_getAddr(struct DBLL_LibraryObj *zlLib, char *name,
		  struct DBLL_Symbol **ppSym)
{
	struct Symbol *sym;
	bool status = false;

	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(zlLib, DBLL_LIBSIGNATURE));
	DBC_Require(name != NULL);
	DBC_Require(ppSym != NULL);
	DBC_Require(zlLib->symTab != NULL);

	GT_3trace(DBLL_debugMask, GT_ENTER,
		 "DBLL_getAddr: lib: 0x%x name: %s pAddr:"
		 " 0x%x\n", zlLib, name, ppSym);
	sym = (struct Symbol *)GH_find(zlLib->symTab, name);
	if (sym != NULL) {
		*ppSym = &sym->value;
		status = true;
	}
	return status;
}

/*
 *  ======== DBLL_getAttrs ========
 *  Retrieve the attributes of the target.
 */
void DBLL_getAttrs(struct DBLL_TarObj *target, struct DBLL_Attrs *pAttrs)
{
	struct DBLL_TarObj *zlTarget = (struct DBLL_TarObj *)target;

	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(zlTarget, DBLL_TARGSIGNATURE));
	DBC_Require(pAttrs != NULL);

	if ((pAttrs != NULL) && (zlTarget != NULL))
		*pAttrs = zlTarget->attrs;

}

/*
 *  ======== DBLL_getCAddr ========
 *  Get address of a "C" name in the specified library.
 */
bool DBLL_getCAddr(struct DBLL_LibraryObj *zlLib, char *name,
		   struct DBLL_Symbol **ppSym)
{
	struct Symbol *sym;
	char cname[MAXEXPR + 1];
	bool status = false;

	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(zlLib, DBLL_LIBSIGNATURE));
	DBC_Require(ppSym != NULL);
	DBC_Require(zlLib->symTab != NULL);
	DBC_Require(name != NULL);

	cname[0] = '_';

       strncpy(cname + 1, name, sizeof(cname) - 2);
	cname[MAXEXPR] = '\0'; 	/* insure '\0' string termination */

	/* Check for C name, if not found */
	sym = (struct Symbol *)GH_find(zlLib->symTab, cname);

	if (sym != NULL) {
		*ppSym = &sym->value;
		status = true;
	}

	return status;
}

/*
 *  ======== DBLL_getSect ========
 *  Get the base address and size (in bytes) of a COFF section.
 */
DSP_STATUS DBLL_getSect(struct DBLL_LibraryObj *lib, char *name, u32 *pAddr,
			u32 *pSize)
{
	u32 uByteSize;
	bool fOpenedDoff = false;
	const struct LDR_SECTION_INFO *sect = NULL;
	struct DBLL_LibraryObj *zlLib = (struct DBLL_LibraryObj *)lib;
	DSP_STATUS status = DSP_SOK;

	DBC_Require(cRefs > 0);
	DBC_Require(name != NULL);
	DBC_Require(pAddr != NULL);
	DBC_Require(pSize != NULL);
	DBC_Require(MEM_IsValidHandle(zlLib, DBLL_LIBSIGNATURE));

	GT_4trace(DBLL_debugMask, GT_ENTER,
		 "DBLL_getSect: lib: 0x%x name: %s pAddr:"
		 " 0x%x pSize: 0x%x\n", lib, name, pAddr, pSize);
	/* If DOFF file is not open, we open it. */
	if (zlLib != NULL) {
		if (zlLib->fp == NULL) {
			status = dofOpen(zlLib);
			if (DSP_SUCCEEDED(status))
				fOpenedDoff = true;

		} else {
			(*(zlLib->pTarget->attrs.fseek))(zlLib->fp,
			 zlLib->ulPos, SEEK_SET);
		}
	} else {
		status = DSP_EHANDLE;
	}
	if (DSP_SUCCEEDED(status)) {
		uByteSize = 1;
		if (DLOAD_GetSectionInfo(zlLib->desc, name, &sect)) {
			*pAddr = sect->load_addr;
			*pSize = sect->size * uByteSize;
			/* Make sure size is even for good swap */
			if (*pSize % 2)
				(*pSize)++;

			/* Align size */
			*pSize = DOFF_ALIGN(*pSize);
		} else {
			status = DSP_ENOSECT;
		}
	}
	if (fOpenedDoff) {
		dofClose(zlLib);
		fOpenedDoff = false;
	}

	return status;
}

/*
 *  ======== DBLL_init ========
 */
bool DBLL_init(void)
{
	bool retVal = true;

	DBC_Require(cRefs >= 0);

	if (cRefs == 0) {
		DBC_Assert(!DBLL_debugMask.flags);
		GT_create(&DBLL_debugMask, "DL"); 	/* "DL" for dbDL */
		GH_init();
		CSL_Init();
		retVal = MEM_Init();
		if (!retVal)
			MEM_Exit();

	}

	if (retVal)
		cRefs++;


	GT_1trace(DBLL_debugMask, GT_5CLASS, "DBLL_init(), ref count:  0x%x\n",
		 cRefs);

	DBC_Ensure((retVal && (cRefs > 0)) || (!retVal && (cRefs >= 0)));

	return retVal;
}

/*
 *  ======== DBLL_load ========
 */
DSP_STATUS DBLL_load(struct DBLL_LibraryObj *lib, DBLL_Flags flags,
		     struct DBLL_Attrs *attrs, u32 *pEntry)
{
	struct DBLL_LibraryObj *zlLib = (struct DBLL_LibraryObj *)lib;
	struct DBLL_TarObj *dbzl;
	bool gotSymbols = true;
	s32 err;
	DSP_STATUS status = DSP_SOK;
	bool fOpenedDoff = false;
	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(zlLib, DBLL_LIBSIGNATURE));
	DBC_Require(pEntry != NULL);
	DBC_Require(attrs != NULL);

	GT_4trace(DBLL_debugMask, GT_ENTER,
		 "DBLL_load: lib: 0x%x flags: 0x%x pEntry:"
		 " 0x%x\n", lib, flags, attrs, pEntry);
	/*
	 *  Load if not already loaded.
	 */
	if (zlLib->loadRef == 0 || !(flags & DBLL_DYNAMIC)) {
		dbzl = zlLib->pTarget;
		dbzl->attrs = *attrs;
		/* Create a hash table for symbols if not already created */
		if (zlLib->symTab == NULL) {
			gotSymbols = false;
			zlLib->symTab = GH_create(MAXBUCKETS,
						 sizeof(struct Symbol),
						 nameHash,
						 nameMatch, symDelete);
			if (zlLib->symTab == NULL)
				status = DSP_EMEMORY;

		}
		/*
		 *  Set up objects needed by the dynamic loader
		 */
		/* Stream */
		zlLib->stream.dlStream.read_buffer = readBuffer;
		zlLib->stream.dlStream.set_file_posn = setFilePosn;
		zlLib->stream.lib = zlLib;
		/* Symbol */
		zlLib->symbol.dlSymbol.Find_Matching_Symbol = findSymbol;
		if (gotSymbols) {
			zlLib->symbol.dlSymbol.Add_To_Symbol_Table =
							findInSymbolTable;
		} else {
			zlLib->symbol.dlSymbol.Add_To_Symbol_Table =
							addToSymbolTable;
		}
		zlLib->symbol.dlSymbol.Purge_Symbol_Table = purgeSymbolTable;
		zlLib->symbol.dlSymbol.Allocate = allocate;
		zlLib->symbol.dlSymbol.Deallocate = deallocate;
		zlLib->symbol.dlSymbol.Error_Report = errorReport;
		zlLib->symbol.lib = zlLib;
		/* Allocate */
		zlLib->allocate.dlAlloc.Allocate = rmmAlloc;
		zlLib->allocate.dlAlloc.Deallocate = rmmDealloc;
		zlLib->allocate.lib = zlLib;
		/* Init */
		zlLib->init.dlInit.connect = connect;
		zlLib->init.dlInit.readmem = readMem;
		zlLib->init.dlInit.writemem = writeMem;
		zlLib->init.dlInit.fillmem = fillMem;
		zlLib->init.dlInit.execute = execute;
		zlLib->init.dlInit.release = release;
		zlLib->init.lib = zlLib;
		/* If COFF file is not open, we open it. */
		if (zlLib->fp == NULL) {
			status = dofOpen(zlLib);
			if (DSP_SUCCEEDED(status))
				fOpenedDoff = true;

		}
		if (DSP_SUCCEEDED(status)) {
			zlLib->ulPos = (*(zlLib->pTarget->attrs.ftell))
					(zlLib->fp);
			/* Reset file cursor */
			(*(zlLib->pTarget->attrs.fseek))(zlLib->fp, (long)0,
				 SEEK_SET);
			bSymbolsReloaded = true;
			/* The 5th argument, DLOAD_INITBSS, tells the DLL
			 * module to zero-init all BSS sections.  In general,
			 * this is not necessary and also increases load time.
			 * We may want to make this configurable by the user */
			err = Dynamic_Load_Module(&zlLib->stream.dlStream,
			      &zlLib->symbol.dlSymbol, &zlLib->allocate.dlAlloc,
			      &zlLib->init.dlInit, DLOAD_INITBSS,
			      &zlLib->mHandle);

			if (err != 0) {
				GT_1trace(DBLL_debugMask, GT_6CLASS,
					 "DBLL_load: "
					 "Dynamic_Load_Module failed: 0x%lx\n",
					 err);
				status = DSP_EDYNLOAD;
			} else if (bRedefinedSymbol) {
				zlLib->loadRef++;
				DBLL_unload(zlLib, (struct DBLL_Attrs *) attrs);
				bRedefinedSymbol = false;
				status = DSP_EDYNLOAD;
			} else {
				*pEntry = zlLib->entry;
			}
		}
	}
	if (DSP_SUCCEEDED(status))
		zlLib->loadRef++;

	/* Clean up DOFF resources */
	if (fOpenedDoff)
		dofClose(zlLib);

	DBC_Ensure(DSP_FAILED(status) || zlLib->loadRef > 0);
	return status;
}

/*
 *  ======== DBLL_loadSect ========
 *  Not supported for COFF.
 */
DSP_STATUS DBLL_loadSect(struct DBLL_LibraryObj *zlLib, char *sectName,
			struct DBLL_Attrs *attrs)
{
	DBC_Require(MEM_IsValidHandle(zlLib, DBLL_LIBSIGNATURE));

	return DSP_ENOTIMPL;
}

/*
 *  ======== DBLL_open ========
 */
DSP_STATUS DBLL_open(struct DBLL_TarObj *target, char *file, DBLL_Flags flags,
		    struct DBLL_LibraryObj **pLib)
{
	struct DBLL_TarObj *zlTarget = (struct DBLL_TarObj *)target;
	struct DBLL_LibraryObj *zlLib = NULL;
	s32 err;
	DSP_STATUS status = DSP_SOK;

	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(zlTarget, DBLL_TARGSIGNATURE));
	DBC_Require(zlTarget->attrs.fopen != NULL);
	DBC_Require(file != NULL);
	DBC_Require(pLib != NULL);

	GT_3trace(DBLL_debugMask, GT_ENTER,
		 "DBLL_open: target: 0x%x file: %s pLib:"
		 " 0x%x\n", target, file, pLib);
	zlLib = zlTarget->head;
	while (zlLib != NULL) {
               if (strcmp(zlLib->fileName, file) == 0) {
			/* Library is already opened */
			zlLib->openRef++;
			break;
		}
		zlLib = zlLib->next;
	}
	if (zlLib == NULL) {
		/* Allocate DBL library object */
		MEM_AllocObject(zlLib, struct DBLL_LibraryObj,
				DBLL_LIBSIGNATURE);
		if (zlLib == NULL) {
			GT_0trace(DBLL_debugMask, GT_6CLASS,
				 "DBLL_open: Memory allocation failed\n");
			status = DSP_EMEMORY;
		} else {
			zlLib->ulPos = 0;
			/* Increment ref count to allow close on failure
			 * later on */
			zlLib->openRef++;
			zlLib->pTarget = zlTarget;
			/* Keep a copy of the file name */
                       zlLib->fileName = MEM_Calloc(strlen(file) + 1,
							MEM_PAGED);
			if (zlLib->fileName == NULL) {
				GT_0trace(DBLL_debugMask, GT_6CLASS,
					 "DBLL_open: Memory "
					 "allocation failed\n");
				status = DSP_EMEMORY;
			} else {
                               strncpy(zlLib->fileName, file,
                                          strlen(file) + 1);
			}
			zlLib->symTab = NULL;
		}
	}
	/*
	 *  Set up objects needed by the dynamic loader
	 */
	if (DSP_FAILED(status))
		goto func_cont;

	/* Stream */
	zlLib->stream.dlStream.read_buffer = readBuffer;
	zlLib->stream.dlStream.set_file_posn = setFilePosn;
	zlLib->stream.lib = zlLib;
	/* Symbol */
	zlLib->symbol.dlSymbol.Add_To_Symbol_Table = addToSymbolTable;
	zlLib->symbol.dlSymbol.Find_Matching_Symbol = findSymbol;
	zlLib->symbol.dlSymbol.Purge_Symbol_Table = purgeSymbolTable;
	zlLib->symbol.dlSymbol.Allocate = allocate;
	zlLib->symbol.dlSymbol.Deallocate = deallocate;
	zlLib->symbol.dlSymbol.Error_Report = errorReport;
	zlLib->symbol.lib = zlLib;
	/* Allocate */
	zlLib->allocate.dlAlloc.Allocate = rmmAlloc;
	zlLib->allocate.dlAlloc.Deallocate = rmmDealloc;
	zlLib->allocate.lib = zlLib;
	/* Init */
	zlLib->init.dlInit.connect = connect;
	zlLib->init.dlInit.readmem = readMem;
	zlLib->init.dlInit.writemem = writeMem;
	zlLib->init.dlInit.fillmem = fillMem;
	zlLib->init.dlInit.execute = execute;
	zlLib->init.dlInit.release = release;
	zlLib->init.lib = zlLib;
	if (DSP_SUCCEEDED(status) && zlLib->fp == NULL)
		status = dofOpen(zlLib);

	zlLib->ulPos = (*(zlLib->pTarget->attrs.ftell)) (zlLib->fp);
	(*(zlLib->pTarget->attrs.fseek))(zlLib->fp, (long) 0, SEEK_SET);
	/* Create a hash table for symbols if flag is set */
	if (zlLib->symTab != NULL || !(flags & DBLL_SYMB))
		goto func_cont;

	zlLib->symTab = GH_create(MAXBUCKETS, sizeof(struct Symbol), nameHash,
				 nameMatch, symDelete);
	if (zlLib->symTab == NULL) {
		status = DSP_EMEMORY;
	} else {
		/* Do a fake load to get symbols - set write function to NoOp */
		zlLib->init.dlInit.writemem = NoOp;
		err = Dynamic_Open_Module(&zlLib->stream.dlStream,
					&zlLib->symbol.dlSymbol,
					&zlLib->allocate.dlAlloc,
					&zlLib->init.dlInit, 0,
					&zlLib->mHandle);
		if (err != 0) {
			GT_1trace(DBLL_debugMask, GT_6CLASS, "DBLL_open: "
				 "Dynamic_Load_Module failed: 0x%lx\n", err);
			status = DSP_EDYNLOAD;
		} else {
			/* Now that we have the symbol table, we can unload */
			err = Dynamic_Unload_Module(zlLib->mHandle,
						   &zlLib->symbol.dlSymbol,
						   &zlLib->allocate.dlAlloc,
						   &zlLib->init.dlInit);
			if (err != 0) {
				GT_1trace(DBLL_debugMask, GT_6CLASS,
					"DBLL_open: "
					"Dynamic_Unload_Module failed: 0x%lx\n",
					err);
				status = DSP_EDYNLOAD;
			}
			zlLib->mHandle = NULL;
		}
	}
func_cont:
	if (DSP_SUCCEEDED(status)) {
		if (zlLib->openRef == 1) {
			/* First time opened - insert in list */
			if (zlTarget->head)
				(zlTarget->head)->prev = zlLib;

			zlLib->prev = NULL;
			zlLib->next = zlTarget->head;
			zlTarget->head = zlLib;
		}
		*pLib = (struct DBLL_LibraryObj *)zlLib;
	} else {
		*pLib = NULL;
		if (zlLib != NULL)
			DBLL_close((struct DBLL_LibraryObj *)zlLib);

	}
	DBC_Ensure((DSP_SUCCEEDED(status) && (zlLib->openRef > 0) &&
		  MEM_IsValidHandle(((struct DBLL_LibraryObj *)(*pLib)),
		  DBLL_LIBSIGNATURE)) || (DSP_FAILED(status) && *pLib == NULL));
	return status;
}

/*
 *  ======== DBLL_readSect ========
 *  Get the content of a COFF section.
 */
DSP_STATUS DBLL_readSect(struct DBLL_LibraryObj *lib, char *name,
			 char *pContent, u32 size)
{
	struct DBLL_LibraryObj *zlLib = (struct DBLL_LibraryObj *)lib;
	bool fOpenedDoff = false;
	u32 uByteSize; 		/* size of bytes */
	u32 ulSectSize; 		/* size of section */
	const struct LDR_SECTION_INFO *sect = NULL;
	DSP_STATUS status = DSP_SOK;

	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(zlLib, DBLL_LIBSIGNATURE));
	DBC_Require(name != NULL);
	DBC_Require(pContent != NULL);
	DBC_Require(size != 0);

	GT_4trace(DBLL_debugMask, GT_ENTER,
		 "DBLL_readSect: lib: 0x%x name: %s "
		 "pContent: 0x%x size: 0x%x\n", lib, name, pContent, size);
	/* If DOFF file is not open, we open it. */
	if (zlLib != NULL) {
		if (zlLib->fp == NULL) {
			status = dofOpen(zlLib);
			if (DSP_SUCCEEDED(status))
				fOpenedDoff = true;

		} else {
			(*(zlLib->pTarget->attrs.fseek))(zlLib->fp,
				zlLib->ulPos, SEEK_SET);
		}
	} else {
		status = DSP_EHANDLE;
	}
	if (DSP_FAILED(status))
		goto func_cont;

	uByteSize = 1;
	if (!DLOAD_GetSectionInfo(zlLib->desc, name, &sect)) {
		status = DSP_ENOSECT;
		goto func_cont;
	}
	/*
	 * Ensure the supplied buffer size is sufficient to store
	 * the section content to be read.
	 */
	ulSectSize = sect->size * uByteSize;
	/* Make sure size is even for good swap */
	if (ulSectSize % 2)
		ulSectSize++;

	/* Align size */
	ulSectSize = DOFF_ALIGN(ulSectSize);
	if (ulSectSize > size) {
		status = DSP_EFAIL;
	} else {
		if (!DLOAD_GetSection(zlLib->desc, sect, pContent))
			status = DSP_EFREAD;

	}
func_cont:
	if (fOpenedDoff) {
		dofClose(zlLib);
		fOpenedDoff = false;
	}
	return status;
}

/*
 *  ======== DBLL_setAttrs ========
 *  Set the attributes of the target.
 */
void DBLL_setAttrs(struct DBLL_TarObj *target, struct DBLL_Attrs *pAttrs)
{
	struct DBLL_TarObj *zlTarget = (struct DBLL_TarObj *)target;
	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(zlTarget, DBLL_TARGSIGNATURE));
	DBC_Require(pAttrs != NULL);
	GT_2trace(DBLL_debugMask, GT_ENTER,
		 "DBLL_setAttrs: target: 0x%x pAttrs: "
		 "0x%x\n", target, pAttrs);
	if ((pAttrs != NULL) && (zlTarget != NULL))
		zlTarget->attrs = *pAttrs;

}

/*
 *  ======== DBLL_unload ========
 */
void DBLL_unload(struct DBLL_LibraryObj *lib, struct DBLL_Attrs *attrs)
{
	struct DBLL_LibraryObj *zlLib = (struct DBLL_LibraryObj *)lib;
	s32 err = 0;

	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(zlLib, DBLL_LIBSIGNATURE));
	DBC_Require(zlLib->loadRef > 0);
	GT_1trace(DBLL_debugMask, GT_ENTER, "DBLL_unload: lib: 0x%x\n", lib);
	zlLib->loadRef--;
	/* Unload only if reference count is 0 */
	if (zlLib->loadRef != 0)
		goto func_end;

	zlLib->pTarget->attrs = *attrs;
	if (zlLib != NULL) {
		if (zlLib->mHandle) {
			err = Dynamic_Unload_Module(zlLib->mHandle,
				&zlLib->symbol.dlSymbol,
				&zlLib->allocate.dlAlloc, &zlLib->init.dlInit);
			if (err != 0) {
				GT_1trace(DBLL_debugMask, GT_5CLASS,
					 "Dynamic_Unload_Module "
					 "failed: 0x%x\n", err);
			}
		}
		/* remove symbols from symbol table */
		if (zlLib->symTab != NULL) {
			GH_delete(zlLib->symTab);
			zlLib->symTab = NULL;
		}
		/* delete DOFF desc since it holds *lots* of host OS
		 * resources */
		dofClose(zlLib);
	}
func_end:
	DBC_Ensure(zlLib->loadRef >= 0);
}

/*
 *  ======== DBLL_unloadSect ========
 *  Not supported for COFF.
 */
DSP_STATUS DBLL_unloadSect(struct DBLL_LibraryObj *lib, char *sectName,
			  struct DBLL_Attrs *attrs)
{
	DBC_Require(cRefs > 0);
	DBC_Require(sectName != NULL);
	GT_2trace(DBLL_debugMask, GT_ENTER,
		 "DBLL_unloadSect: lib: 0x%x sectName: "
		 "%s\n", lib, sectName);
	return DSP_ENOTIMPL;
}

/*
 *  ======== dofClose ========
 */
static void dofClose(struct DBLL_LibraryObj *zlLib)
{
	if (zlLib->desc) {
		DLOAD_module_close(zlLib->desc);
		zlLib->desc = NULL;
	}
	/* close file */
	if (zlLib->fp) {
		(zlLib->pTarget->attrs.fclose) (zlLib->fp);
		zlLib->fp = NULL;
	}
}

/*
 *  ======== dofOpen ========
 */
static DSP_STATUS dofOpen(struct DBLL_LibraryObj *zlLib)
{
	void *open = *(zlLib->pTarget->attrs.fopen);
	DSP_STATUS status = DSP_SOK;

	/* First open the file for the dynamic loader, then open COF */
	zlLib->fp = (void *)((DBLL_FOpenFxn)(open))(zlLib->fileName, "rb");

	/* Open DOFF module */
	if (zlLib->fp && zlLib->desc == NULL) {
		(*(zlLib->pTarget->attrs.fseek))(zlLib->fp, (long)0, SEEK_SET);
		zlLib->desc = DLOAD_module_open(&zlLib->stream.dlStream,
						&zlLib->symbol.dlSymbol);
		if (zlLib->desc == NULL) {
			(zlLib->pTarget->attrs.fclose)(zlLib->fp);
			zlLib->fp = NULL;
			status = DSP_EFOPEN;
		}
	} else {
		status = DSP_EFOPEN;
	}

	return status;
}

/*
 *  ======== nameHash ========
 */
static u16 nameHash(void *key, u16 maxBucket)
{
	u16 ret;
	u16 hash;
	char *name = (char *)key;

	DBC_Require(name != NULL);

	hash = 0;

	while (*name) {
		hash <<= 1;
		hash ^= *name++;
	}

	ret = hash % maxBucket;

	return ret;
}

/*
 *  ======== nameMatch ========
 */
static bool nameMatch(void *key, void *value)
{
	DBC_Require(key != NULL);
	DBC_Require(value != NULL);

	if ((key != NULL) && (value != NULL)) {
               if (strcmp((char *)key, ((struct Symbol *)value)->name) == 0)
			return true;
	}
	return false;
}

/*
 *  ======== NoOp ========
 */
static int NoOp(struct Dynamic_Loader_Initialize *thisptr, void *bufr,
		LDR_ADDR locn, struct LDR_SECTION_INFO *info, unsigned bytsize)
{
	return 1;
}

/*
 *  ======== symDelete ========
 */
static void symDelete(void *value)
{
	struct Symbol *sp = (struct Symbol *)value;

	MEM_Free(sp->name);
}

/*
 *  Dynamic Loader Functions
 */

/* Dynamic_Loader_Stream */
/*
 *  ======== readBuffer ========
 */
static int readBuffer(struct Dynamic_Loader_Stream *this, void *buffer,
		     unsigned bufsize)
{
	struct DBLLStream *pStream = (struct DBLLStream *)this;
	struct DBLL_LibraryObj *lib;
	int bytesRead = 0;

	DBC_Require(this != NULL);
	lib = pStream->lib;
	DBC_Require(MEM_IsValidHandle(lib, DBLL_LIBSIGNATURE));

	if (lib != NULL) {
		bytesRead = (*(lib->pTarget->attrs.fread))(buffer, 1, bufsize,
			    lib->fp);
	}
	return bytesRead;
}

/*
 *  ======== setFilePosn ========
 */
static int setFilePosn(struct Dynamic_Loader_Stream *this, unsigned int pos)
{
	struct DBLLStream *pStream = (struct DBLLStream *)this;
	struct DBLL_LibraryObj *lib;
	int status = 0; 		/* Success */

	DBC_Require(this != NULL);
	lib = pStream->lib;
	DBC_Require(MEM_IsValidHandle(lib, DBLL_LIBSIGNATURE));

	if (lib != NULL) {
		status = (*(lib->pTarget->attrs.fseek))(lib->fp, (long)pos,
			 SEEK_SET);
	}

	return status;
}

/* Dynamic_Loader_Sym */

/*
 *  ======== findSymbol ========
 */
static struct dynload_symbol *findSymbol(struct Dynamic_Loader_Sym *this,
					const char *name)
{
	struct dynload_symbol *retSym;
	struct DBLLSymbol *pSymbol = (struct DBLLSymbol *)this;
	struct DBLL_LibraryObj *lib;
	struct DBLL_Symbol *pSym = NULL;
	bool status = false; 	/* Symbol not found yet */

	DBC_Require(this != NULL);
	lib = pSymbol->lib;
	DBC_Require(MEM_IsValidHandle(lib, DBLL_LIBSIGNATURE));

	if (lib != NULL) {
		if (lib->pTarget->attrs.symLookup) {
			/* Check current lib + base lib + dep lib +
			 * persistent lib */
			status = (*(lib->pTarget->attrs.symLookup))
				 (lib->pTarget->attrs.symHandle,
				 lib->pTarget->attrs.symArg,
				 lib->pTarget->attrs.rmmHandle, name, &pSym);
		} else {
			/* Just check current lib for symbol */
			status = DBLL_getAddr((struct DBLL_LibraryObj *)lib,
				 (char *)name, &pSym);
			if (!status) {
				status =
				   DBLL_getCAddr((struct DBLL_LibraryObj *)lib,
				   (char *)name, &pSym);
			}
		}
	}

	if (!status && bGblSearch) {
		GT_1trace(DBLL_debugMask, GT_6CLASS,
			 "findSymbol: Symbol not found: %s\n", name);
	}

	DBC_Assert((status && (pSym != NULL)) || (!status && (pSym == NULL)));

	retSym = (struct dynload_symbol *)pSym;
	return retSym;
}

/*
 *  ======== findInSymbolTable ========
 */
static struct dynload_symbol *findInSymbolTable(struct Dynamic_Loader_Sym *this,
						const char *name,
						unsigned moduleid)
{
	struct dynload_symbol *retSym;
	struct DBLLSymbol *pSymbol = (struct DBLLSymbol *)this;
	struct DBLL_LibraryObj *lib;
	struct Symbol *sym;

	DBC_Require(this != NULL);
	lib = pSymbol->lib;
	DBC_Require(MEM_IsValidHandle(lib, DBLL_LIBSIGNATURE));
	DBC_Require(lib->symTab != NULL);

	sym = (struct Symbol *)GH_find(lib->symTab, (char *) name);

	retSym = (struct dynload_symbol *)&sym->value;
	return retSym;
}

/*
 *  ======== addToSymbolTable ========
 */
static struct dynload_symbol *addToSymbolTable(struct Dynamic_Loader_Sym *this,
					      const char *name,
					      unsigned moduleId)
{
	struct Symbol *symPtr = NULL;
	struct Symbol symbol;
	struct dynload_symbol *pSym = NULL;
	struct DBLLSymbol *pSymbol = (struct DBLLSymbol *)this;
	struct DBLL_LibraryObj *lib;
	struct dynload_symbol *retVal;

	DBC_Require(this != NULL);
       DBC_Require(name);
	lib = pSymbol->lib;
	DBC_Require(MEM_IsValidHandle(lib, DBLL_LIBSIGNATURE));

	/* Check to see if symbol is already defined in symbol table */
	if (!(lib->pTarget->attrs.baseImage)) {
		bGblSearch = false;
		pSym = findSymbol(this, name);
		bGblSearch = true;
		if (pSym) {
			bRedefinedSymbol = true;
			GT_1trace(DBLL_debugMask, GT_6CLASS,
				 "Symbol already defined in "
				 "symbol table: %s\n", name);
			return NULL;
		}
	}
	/* Allocate string to copy symbol name */
       symbol.name = (char *)MEM_Calloc(strlen((char *const)name) + 1,
							MEM_PAGED);
	if (symbol.name == NULL)
		return NULL;

	if (symbol.name != NULL) {
		/* Just copy name (value will be filled in by dynamic loader) */
               strncpy(symbol.name, (char *const)name,
                          strlen((char *const)name) + 1);

		/* Add symbol to symbol table */
		symPtr = (struct Symbol *)GH_insert(lib->symTab, (void *)name,
			 (void *)&symbol);
		if (symPtr == NULL)
			MEM_Free(symbol.name);

	}
	if (symPtr != NULL)
		retVal = (struct dynload_symbol *)&symPtr->value;
	else
		retVal = NULL;

	return retVal;
}

/*
 *  ======== purgeSymbolTable ========
 */
static void purgeSymbolTable(struct Dynamic_Loader_Sym *this, unsigned moduleId)
{
	struct DBLLSymbol *pSymbol = (struct DBLLSymbol *)this;
	struct DBLL_LibraryObj *lib;

	DBC_Require(this != NULL);
	lib = pSymbol->lib;
	DBC_Require(MEM_IsValidHandle(lib, DBLL_LIBSIGNATURE));

	/* May not need to do anything */
}

/*
 *  ======== allocate ========
 */
static void *allocate(struct Dynamic_Loader_Sym *this, unsigned memsize)
{
	struct DBLLSymbol *pSymbol = (struct DBLLSymbol *)this;
	struct DBLL_LibraryObj *lib;
	void *buf;

	DBC_Require(this != NULL);
	lib = pSymbol->lib;
	DBC_Require(MEM_IsValidHandle(lib, DBLL_LIBSIGNATURE));

	buf = MEM_Calloc(memsize, MEM_PAGED);

	return buf;
}

/*
 *  ======== deallocate ========
 */
static void deallocate(struct Dynamic_Loader_Sym *this, void *memPtr)
{
	struct DBLLSymbol *pSymbol = (struct DBLLSymbol *)this;
	struct DBLL_LibraryObj *lib;

	DBC_Require(this != NULL);
	lib = pSymbol->lib;
	DBC_Require(MEM_IsValidHandle(lib, DBLL_LIBSIGNATURE));

	MEM_Free(memPtr);
}

/*
 *  ======== errorReport ========
 */
static void errorReport(struct Dynamic_Loader_Sym *this, const char *errstr,
			va_list args)
{
	struct DBLLSymbol *pSymbol = (struct DBLLSymbol *)this;
	struct DBLL_LibraryObj *lib;
	char tempBuf[MAXEXPR];

	DBC_Require(this != NULL);
	lib = pSymbol->lib;
	DBC_Require(MEM_IsValidHandle(lib, DBLL_LIBSIGNATURE));
	vsnprintf((char *)tempBuf, MAXEXPR, (char *)errstr, args);
	GT_1trace(DBLL_debugMask, GT_5CLASS, "%s\n", tempBuf);
}

/* Dynamic_Loader_Allocate */

/*
 *  ======== rmmAlloc ========
 */
static int rmmAlloc(struct Dynamic_Loader_Allocate *this,
		   struct LDR_SECTION_INFO *info, unsigned align)
{
	struct DBLLAlloc *pAlloc = (struct DBLLAlloc *)this;
	struct DBLL_LibraryObj *lib;
	DSP_STATUS status = DSP_SOK;
	u32 memType;
	struct RMM_Addr rmmAddr;
	s32 retVal = TRUE;
	unsigned stype = DLOAD_SECTION_TYPE(info->type);
	char *pToken = NULL;
	char *szSecLastToken = NULL;
	char *szLastToken = NULL;
	char *szSectName = NULL;
	char *pszCur;
	s32 tokenLen = 0;
	s32 segId = -1;
	s32 req = -1;
	s32 count = 0;
	u32 allocSize = 0;

	DBC_Require(this != NULL);
	lib = pAlloc->lib;
	DBC_Require(MEM_IsValidHandle(lib, DBLL_LIBSIGNATURE));

	memType = (stype == DLOAD_TEXT) ? DBLL_CODE : (stype == DLOAD_BSS) ?
		   DBLL_BSS : DBLL_DATA;

	/* Attempt to extract the segment ID and requirement information from
	 the name of the section */
       DBC_Require(info->name);
       tokenLen = strlen((char *)(info->name)) + 1;

	szSectName = MEM_Calloc(tokenLen, MEM_PAGED);
	szLastToken = MEM_Calloc(tokenLen, MEM_PAGED);
	szSecLastToken = MEM_Calloc(tokenLen, MEM_PAGED);

	if (szSectName == NULL || szSecLastToken == NULL ||
	   szLastToken == NULL) {
		status = DSP_EMEMORY;
		goto func_cont;
	}
       strncpy(szSectName, (char *)(info->name), tokenLen);
	pszCur = szSectName;
	while ((pToken = strsep(&pszCur, ":")) && *pToken != '\0') {
               strncpy(szSecLastToken, szLastToken, strlen(szLastToken) + 1);
               strncpy(szLastToken, pToken, strlen(pToken) + 1);
		pToken = strsep(&pszCur, ":");
		count++; 	/* optimizes processing*/
	}
	/* If pToken is 0 or 1, and szSecLastToken is DYN_DARAM or DYN_SARAM,
	 or DYN_EXTERNAL, then mem granularity information is present
	 within the section name - only process if there are at least three
	 tokens within the section name (just a minor optimization)*/
	if (count >= 3)
               strict_strtol(szLastToken, 10, (long *)&req);

	if ((req == 0) || (req == 1)) {
               if (strcmp(szSecLastToken, "DYN_DARAM") == 0) {
			segId = 0;
		} else {
                       if (strcmp(szSecLastToken, "DYN_SARAM") == 0) {
				segId = 1;
			} else {
                               if (strcmp(szSecLastToken,
				   "DYN_EXTERNAL") == 0) {
					segId = 2;
				}
			}
		}
		if (segId != -1) {
			GT_2trace(DBLL_debugMask, GT_5CLASS,
				 "Extracted values for memory"
				 " granularity req [%d] segId [%d]\n",
				 req, segId);
		}
	}
	MEM_Free(szSectName);
	szSectName = NULL;
	MEM_Free(szLastToken);
	szLastToken = NULL;
	MEM_Free(szSecLastToken);
	szSecLastToken = NULL;
func_cont:
	if (memType == DBLL_CODE)
		allocSize = info->size + GEM_L1P_PREFETCH_SIZE;
	else
		allocSize = info->size;
	/* TODO - ideally, we can pass the alignment requirement also
	 * from here */
	if (lib != NULL) {
		status = (lib->pTarget->attrs.alloc)(lib->pTarget->
			 attrs.rmmHandle, memType, allocSize, align,
			 (u32 *)&rmmAddr, segId, req, FALSE);
	}
	if (DSP_FAILED(status)) {
		retVal = false;
	} else {
		/* RMM gives word address. Need to convert to byte address */
		info->load_addr = rmmAddr.addr * DSPWORDSIZE;
		info->run_addr = info->load_addr;
		info->context = (u32)rmmAddr.segid;
		GT_3trace(DBLL_debugMask, GT_5CLASS,
			 "Remote alloc: %s  base = 0x%lx len"
			 "= 0x%lx\n", info->name, info->load_addr / DSPWORDSIZE,
			 info->size / DSPWORDSIZE);
	}
	return retVal;
}

/*
 *  ======== rmmDealloc ========
 */
static void rmmDealloc(struct Dynamic_Loader_Allocate *this,
		       struct LDR_SECTION_INFO *info)
{
	struct DBLLAlloc *pAlloc = (struct DBLLAlloc *)this;
	struct DBLL_LibraryObj *lib;
	u32 segid;
	DSP_STATUS status = DSP_SOK;
	unsigned stype = DLOAD_SECTION_TYPE(info->type);
	u32 memType;
	u32 freeSize = 0;

	memType = (stype == DLOAD_TEXT) ? DBLL_CODE : (stype == DLOAD_BSS) ?
		  DBLL_BSS : DBLL_DATA;
	DBC_Require(this != NULL);
	lib = pAlloc->lib;
	DBC_Require(MEM_IsValidHandle(lib, DBLL_LIBSIGNATURE));
	/* segid was set by alloc function */
	segid = (u32)info->context;
	if (memType == DBLL_CODE)
		freeSize = info->size + GEM_L1P_PREFETCH_SIZE;
	else
		freeSize = info->size;
	if (lib != NULL) {
		status = (lib->pTarget->attrs.free)(lib->pTarget->
			 attrs.symHandle, segid, info->load_addr / DSPWORDSIZE,
			 freeSize, false);
	}
	if (DSP_SUCCEEDED(status)) {
		GT_2trace(DBLL_debugMask, GT_5CLASS,
			 "Remote dealloc: base = 0x%lx len ="
			 "0x%lx\n", info->load_addr / DSPWORDSIZE,
			 freeSize / DSPWORDSIZE);
	}
}

/* Dynamic_Loader_Initialize */
/*
 *  ======== connect ========
 */
static int connect(struct Dynamic_Loader_Initialize *this)
{
	return true;
}

/*
 *  ======== readMem ========
 *  This function does not need to be implemented.
 */
static int readMem(struct Dynamic_Loader_Initialize *this, void *buf,
		  LDR_ADDR addr, struct LDR_SECTION_INFO *info,
		  unsigned nbytes)
{
	struct DBLLInit *pInit = (struct DBLLInit *)this;
	struct DBLL_LibraryObj *lib;
	int bytesRead = 0;

	DBC_Require(this != NULL);
	lib = pInit->lib;
	DBC_Require(MEM_IsValidHandle(lib, DBLL_LIBSIGNATURE));
	/* Need WMD_BRD_Read function */
	return bytesRead;
}

/*
 *  ======== writeMem ========
 */
static int writeMem(struct Dynamic_Loader_Initialize *this, void *buf,
		   LDR_ADDR addr, struct LDR_SECTION_INFO *info,
		   unsigned nBytes)
{
	struct DBLLInit *pInit = (struct DBLLInit *)this;
	struct DBLL_LibraryObj *lib;
	struct DBLL_TarObj *pTarget;
	struct DBLL_SectInfo sectInfo;
	u32 memType;
	bool retVal = true;

	DBC_Require(this != NULL);
	lib = pInit->lib;
	DBC_Require(MEM_IsValidHandle(lib, DBLL_LIBSIGNATURE));

	memType = (DLOAD_SECTION_TYPE(info->type) == DLOAD_TEXT) ? DBLL_CODE :
		  DBLL_DATA;
	if ((lib != NULL) &&
	    ((pTarget = lib->pTarget) != NULL) &&
	    (pTarget->attrs.write != NULL)) {
		retVal = (*pTarget->attrs.write)(pTarget->attrs.wHandle,
						 addr, buf, nBytes, memType);

		if (pTarget->attrs.logWrite) {
			sectInfo.name = info->name;
			sectInfo.runAddr = info->run_addr;
			sectInfo.loadAddr = info->load_addr;
			sectInfo.size = info->size;
			sectInfo.type = memType;
			/* Pass the information about what we've written to
			 * another module */
			(*pTarget->attrs.logWrite)(
				pTarget->attrs.logWriteHandle,
				&sectInfo, addr, nBytes);
		}
	}
	return retVal;
}

/*
 *  ======== fillMem ========
 *  Fill nBytes of memory at a given address with a given value by
 *  writing from a buffer containing the given value.  Write in
 *  sets of MAXEXPR (128) bytes to avoid large stack buffer issues.
 */
static int fillMem(struct Dynamic_Loader_Initialize *this, LDR_ADDR addr,
		   struct LDR_SECTION_INFO *info, unsigned nBytes,
		   unsigned val)
{
	bool retVal = true;
	char *pBuf;
	struct DBLL_LibraryObj *lib;
	struct DBLLInit *pInit = (struct DBLLInit *)this;

	DBC_Require(this != NULL);
	lib = pInit->lib;
	pBuf = NULL;
	/* Pass the NULL pointer to writeMem to get the start address of Shared
	    memory. This is a trick to just get the start address, there is no
	    writing taking place with this Writemem
	*/
	if ((lib->pTarget->attrs.write) != (DBLL_WriteFxn)NoOp)
		writeMem(this, &pBuf, addr, info, 0);
	if (pBuf)
		memset(pBuf, val, nBytes);

	return retVal;
}

/*
 *  ======== execute ========
 */
static int execute(struct Dynamic_Loader_Initialize *this, LDR_ADDR start)
{
	struct DBLLInit *pInit = (struct DBLLInit *)this;
	struct DBLL_LibraryObj *lib;
	bool retVal = true;

	DBC_Require(this != NULL);
	lib = pInit->lib;
	DBC_Require(MEM_IsValidHandle(lib, DBLL_LIBSIGNATURE));
	/* Save entry point */
	if (lib != NULL)
		lib->entry = (u32)start;

	return retVal;
}

/*
 *  ======== release ========
 */
static void release(struct Dynamic_Loader_Initialize *this)
{
}

