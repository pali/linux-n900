/*
 * nldr.c
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
 *  ======== nldr.c ========
 *  Description:
 *      DSP/BIOS Bridge dynamic + overlay Node loader.
 *
 *  Public Functions:
 *      NLDR_Allocate
 *      NLDR_Create
 *      NLDR_Delete
 *      NLDR_Exit
 *      NLDR_Free
 *      NLDR_GetFxnAddr
 *      NLDR_Init
 *      NLDR_Load
 *      NLDR_Unload
 *
 *  Notes:
 *
 *! Revision History
 *! ================
 *! 07-Apr-2003 map Removed references to dead DLDR module
 *! 23-Jan-2003 map Updated RemoteAlloc to support memory granularity
 *! 20-Jan-2003 map Updated to maintain persistent dependent libraries
 *! 15-Jan-2003 map Adapted for use with multiple dynamic phase libraries
 *! 19-Dec-2002 map Fixed overlay bug in AddOvlySect for overlay
 *!		 sections > 1024 bytes.
 *! 13-Dec-2002 map Fixed NLDR_GetFxnAddr bug by searching dependent
 *!		 libs for symbols
 *! 27-Sep-2002 map Added RemoteFree to convert size to words for
 *!		 correct deallocation
 *! 16-Sep-2002 map Code Review Cleanup(from dldr.c)
 *! 29-Aug-2002 map Adjusted for ARM-side overlay copy
 *! 05-Aug-2002 jeh Created.
 */

#include <dspbridge/host_os.h>

#include <dspbridge/std.h>
#include <dspbridge/dbdefs.h>
#include <dspbridge/errbase.h>

#include <dspbridge/dbc.h>
#include <dspbridge/gt.h>
#ifdef DEBUG
#include <dspbridge/dbg.h>
#endif

/* OS adaptation layer */
#include <dspbridge/csl.h>
#include <dspbridge/mem.h>

/* Platform manager */
#include <dspbridge/cod.h>
#include <dspbridge/dev.h>

/* Resource manager */
#include <dspbridge/dbll.h>
#include <dspbridge/dbdcd.h>
#include <dspbridge/rmm.h>
#include <dspbridge/uuidutil.h>

#include <dspbridge/nldr.h>

#define NLDR_SIGNATURE      0x52444c4e	/* "RDLN" */
#define NLDR_NODESIGNATURE  0x4e444c4e	/* "NDLN" */

/* Name of section containing dynamic load mem */
#define DYNMEMSECT  ".dspbridge_mem"

/* Name of section containing dependent library information */
#define DEPLIBSECT  ".dspbridge_deplibs"

/* Max depth of recursion for loading node's dependent libraries */
#define MAXDEPTH	    5

/* Max number of persistent libraries kept by a node */
#define MAXLIBS	 5

/*
 *  Defines for extracting packed dynamic load memory requirements from two
 *  masks.
 *  These defines must match node.cdb and dynm.cdb
 *  Format of data/code mask is:
 *   uuuuuuuu|fueeeeee|fudddddd|fucccccc|
 *  where
 *      u = unused
 *      cccccc = prefered/required dynamic mem segid for create phase data/code
 *      dddddd = prefered/required dynamic mem segid for delete phase data/code
 *      eeeeee = prefered/req. dynamic mem segid for execute phase data/code
 *      f = flag indicating if memory is preferred or required:
 *	  f = 1 if required, f = 0 if preferred.
 *
 *  The 6 bits of the segid are interpreted as follows:
 *
 *  If the 6th bit (bit 5) is not set, then this specifies a memory segment
 *  between 0 and 31 (a maximum of 32 dynamic loading memory segments).
 *  If the 6th bit (bit 5) is set, segid has the following interpretation:
 *      segid = 32 - Any internal memory segment can be used.
 *      segid = 33 - Any external memory segment can be used.
 *      segid = 63 - Any memory segment can be used (in this case the
 *		   required/preferred flag is irrelevant).
 *
 */
/* Maximum allowed dynamic loading memory segments */
#define MAXMEMSEGS      32

#define MAXSEGID	3	/* Largest possible (real) segid */
#define MEMINTERNALID   32	/* Segid meaning use internal mem */
#define MEMEXTERNALID   33	/* Segid meaning use external mem */
#define NULLID	  63	/* Segid meaning no memory req/pref */
#define FLAGBIT	 7	/* 7th bit is pref./req. flag */
#define SEGMASK	 0x3f	/* Bits 0 - 5 */

#define CREATEBIT       0	/* Create segid starts at bit 0 */
#define DELETEBIT       8	/* Delete segid starts at bit 8 */
#define EXECUTEBIT      16	/* Execute segid starts at bit 16 */

/*
 *  Masks that define memory type.  Must match defines in dynm.cdb.
 */
#define DYNM_CODE       0x2
#define DYNM_DATA       0x4
#define DYNM_CODEDATA   (DYNM_CODE | DYNM_DATA)
#define DYNM_INTERNAL   0x8
#define DYNM_EXTERNAL   0x10

/*
 *  Defines for packing memory requirement/preference flags for code and
 *  data of each of the node's phases into one mask.
 *  The bit is set if the segid is required for loading code/data of the
 *  given phase. The bit is not set, if the segid is preferred only.
 *
 *  These defines are also used as indeces into a segid array for the node.
 *  eg node's segid[CREATEDATAFLAGBIT] is the memory segment id that the
 *  create phase data is required or preferred to be loaded into.
 */
#define CREATEDATAFLAGBIT   0
#define CREATECODEFLAGBIT   1
#define EXECUTEDATAFLAGBIT  2
#define EXECUTECODEFLAGBIT  3
#define DELETEDATAFLAGBIT   4
#define DELETECODEFLAGBIT   5
#define MAXFLAGS	    6

#define IsInternal(hNldr, segid) (((segid) <= MAXSEGID && \
	    hNldr->segTable[(segid)] & DYNM_INTERNAL) || \
	    (segid) == MEMINTERNALID)

#define IsExternal(hNldr, segid) (((segid) <= MAXSEGID && \
	    hNldr->segTable[(segid)] & DYNM_EXTERNAL) || \
	    (segid) == MEMEXTERNALID)

#define SWAPLONG(x) ((((x) << 24) & 0xFF000000) | (((x) << 8) & 0xFF0000L) | \
	(((x) >> 8) & 0xFF00L) | (((x) >> 24) & 0xFF))

#define SWAPWORD(x) ((((x) << 8) & 0xFF00) | (((x) >> 8) & 0xFF))

    /*
     *  These names may be embedded in overlay sections to identify which
     *  node phase the section should be overlayed.
     */
#define PCREATE	 "create"
#define PDELETE	 "delete"
#define PEXECUTE	"execute"

#define IsEqualUUID(uuid1, uuid2) (\
	((uuid1).ulData1 == (uuid2).ulData1) && \
	((uuid1).usData2 == (uuid2).usData2) && \
	((uuid1).usData3 == (uuid2).usData3) && \
	((uuid1).ucData4 == (uuid2).ucData4) && \
	((uuid1).ucData5 == (uuid2).ucData5) && \
       (strncmp((void *)(uuid1).ucData6, (void *)(uuid2).ucData6, 6)) == 0)

    /*
     *  ======== MemInfo ========
     *  Format of dynamic loading memory segment info in coff file.
     *  Must match dynm.h55.
     */
struct MemInfo {
	u32 segid;		/* Dynamic loading memory segment number */
	u32 base;
	u32 len;
	u32 type;		/* Mask of DYNM_CODE, DYNM_INTERNAL, etc. */
};

/*
 *  ======== LibNode ========
 *  For maintaining a tree of library dependencies.
 */
struct LibNode {
	struct DBLL_LibraryObj *lib;	/* The library */
	u16 nDepLibs;	/* Number of dependent libraries */
	struct LibNode *pDepLibs;	/* Dependent libraries of lib */
};

/*
 *  ======== OvlySect ========
 *  Information needed to overlay a section.
 */
struct OvlySect {
	struct OvlySect *pNextSect;
	u32 loadAddr;		/* Load address of section */
	u32 runAddr;		/* Run address of section */
	u32 size;		/* Size of section */
	u16 page;		/* DBL_CODE, DBL_DATA */
};

/*
 *  ======== OvlyNode ========
 *  For maintaining a list of overlay nodes, with sections that need to be
 *  overlayed for each of the nodes phases.
 */
struct OvlyNode {
	struct DSP_UUID uuid;
	char *pNodeName;
	struct OvlySect *pCreateSects;
	struct OvlySect *pDeleteSects;
	struct OvlySect *pExecuteSects;
	struct OvlySect *pOtherSects;
	u16 nCreateSects;
	u16 nDeleteSects;
	u16 nExecuteSects;
	u16 nOtherSects;
	u16 createRef;
	u16 deleteRef;
	u16 executeRef;
	u16 otherRef;
};

/*
 *  ======== NLDR_OBJECT ========
 *  Overlay loader object.
 */
struct NLDR_OBJECT {
	u32 dwSignature;	/* For object validation */
	struct DEV_OBJECT *hDevObject;	/* Device object */
	struct DCD_MANAGER *hDcdMgr;	/* Proc/Node data manager */
	struct DBLL_TarObj *dbll;	/* The DBL loader */
	struct DBLL_LibraryObj *baseLib;	/* Base image library */
	struct RMM_TargetObj *rmm;	/* Remote memory manager for DSP */
	struct DBLL_Fxns dbllFxns;	/* Loader function table */
	struct DBLL_Attrs dbllAttrs;	/* attrs to pass to loader functions */
	NLDR_OVLYFXN ovlyFxn;	/* "write" for overlay nodes */
	NLDR_WRITEFXN writeFxn;	/* "write" for dynamic nodes */
	struct OvlyNode *ovlyTable;	/* Table of overlay nodes */
	u16 nOvlyNodes;	/* Number of overlay nodes in base */
	u16 nNode;		/* Index for tracking overlay nodes */
	u16 nSegs;		/* Number of dynamic load mem segs */
	u32 *segTable;	/* memtypes of dynamic memory segs
				 * indexed by segid
				 */
	u16 usDSPMauSize;	/* Size of DSP MAU */
	u16 usDSPWordSize;	/* Size of DSP word */
};

/*
 *  ======== NLDR_NODEOBJECT ========
 *  Dynamic node object. This object is created when a node is allocated.
 */
struct NLDR_NODEOBJECT {
	u32 dwSignature;	/* For object validation */
	struct NLDR_OBJECT *pNldr;	/* Dynamic loader handle */
	void *pPrivRef;		/* Handle to pass to DBL_WriteFxn */
	struct DSP_UUID uuid;		/* Node's UUID */
	bool fDynamic;		/* Dynamically loaded node? */
	bool fOverlay;		/* Overlay node? */
	bool *pfPhaseSplit;	/* Multiple phase libraries? */
	struct LibNode root;		/* Library containing node phase */
	struct LibNode createLib;    /* Library containing create phase lib */
	struct LibNode executeLib;   /* Library containing execute phase lib */
	struct LibNode deleteLib;    /* Library containing delete phase lib */
	struct LibNode persLib[MAXLIBS];  /* libs remain loaded until Delete */
	s32 nPersLib;		/* Number of persistent libraries */
	/* Path in lib dependency tree */
	struct DBLL_LibraryObj *libPath[MAXDEPTH + 1];
	enum NLDR_PHASE phase;	/* Node phase currently being loaded */

	/*
	 *  Dynamic loading memory segments for data and code of each phase.
	 */
	u16 segId[MAXFLAGS];

	/*
	 *  Mask indicating whether each mem segment specified in segId[]
	 *  is preferred or required.
	 *  For example if (codeDataFlagMask & (1 << EXECUTEDATAFLAGBIT)) != 0,
	 *  then it is required to load execute phase data into the memory
	 *  specified by segId[EXECUTEDATAFLAGBIT].
	 */
	u32 codeDataFlagMask;
};

/* Dynamic loader function table */
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

static struct GT_Mask NLDR_debugMask = { NULL, NULL };	/* GT trace variable */
static u32 cRefs;		/* module reference count */

static DSP_STATUS AddOvlyInfo(void *handle, struct DBLL_SectInfo *sectInfo,
			     u32 addr, u32 nBytes);
static DSP_STATUS AddOvlyNode(struct DSP_UUID *pUuid,
			     enum DSP_DCDOBJTYPE objType,
			     IN void *handle);
static DSP_STATUS AddOvlySect(struct NLDR_OBJECT *hNldr,
			      struct OvlySect **pList,
			      struct DBLL_SectInfo *pSectInfo, bool *pExists,
			      u32 addr, u32 nBytes);
static s32 fakeOvlyWrite(void *handle, u32 dspAddr, void *buf, u32 nBytes,
			s32 mtype);
static void FreeSects(struct NLDR_OBJECT *hNldr, struct OvlySect *pPhaseSects,
		     u16 nAlloc);
static bool GetSymbolValue(void *handle, void *pArg, void *rmmHandle,
			  char *symName, struct DBLL_Symbol **sym);
static DSP_STATUS LoadLib(struct NLDR_NODEOBJECT *hNldrNode,
			 struct LibNode *root, struct DSP_UUID uuid,
			 bool rootPersistent, struct DBLL_LibraryObj **libPath,
			 enum NLDR_PHASE phase, u16 depth);
static DSP_STATUS LoadOvly(struct NLDR_NODEOBJECT *hNldrNode,
			  enum NLDR_PHASE phase);
static DSP_STATUS RemoteAlloc(void **pRef, u16 memType, u32 size,
			     u32 align, u32 *dspAddr,
			     OPTIONAL s32 segmentId, OPTIONAL s32 req,
			     bool reserve);
static DSP_STATUS RemoteFree(void **pRef, u16 space, u32 dspAddr,
			    u32 size, bool reserve);

static void UnloadLib(struct NLDR_NODEOBJECT *hNldrNode, struct LibNode *root);
static void UnloadOvly(struct NLDR_NODEOBJECT *hNldrNode,
		      enum NLDR_PHASE phase);
static bool findInPersistentLibArray(struct NLDR_NODEOBJECT *hNldrNode,
				    struct DBLL_LibraryObj *lib);
static u32 findLcm(u32 a, u32 b);
static u32 findGcf(u32 a, u32 b);

/*
 *  ======== NLDR_Allocate ========
 */
DSP_STATUS NLDR_Allocate(struct NLDR_OBJECT *hNldr, void *pPrivRef,
			 IN CONST struct DCD_NODEPROPS *pNodeProps,
			 OUT struct NLDR_NODEOBJECT **phNldrNode,
			 IN bool *pfPhaseSplit)
{
	struct NLDR_NODEOBJECT *pNldrNode = NULL;
	DSP_STATUS status = DSP_SOK;

	DBC_Require(cRefs > 0);
	DBC_Require(pNodeProps != NULL);
	DBC_Require(phNldrNode != NULL);
	DBC_Require(MEM_IsValidHandle(hNldr, NLDR_SIGNATURE));

	GT_5trace(NLDR_debugMask, GT_ENTER, "NLDR_Allocate(0x%x, 0x%x, 0x%x, "
		 "0x%x, 0x%x)\n", hNldr, pPrivRef, pNodeProps, phNldrNode,
		 pfPhaseSplit);

	/* Initialize handle in case of failure */
	*phNldrNode = NULL;
	/* Allocate node object */
	MEM_AllocObject(pNldrNode, struct NLDR_NODEOBJECT, NLDR_NODESIGNATURE);

	if (pNldrNode == NULL) {
		GT_0trace(NLDR_debugMask, GT_6CLASS, "NLDR_Allocate: "
			 "Memory allocation failed\n");
		status = DSP_EMEMORY;
	} else {
		pNldrNode->pfPhaseSplit = pfPhaseSplit;
		pNldrNode->nPersLib = 0;
		pNldrNode->pNldr = hNldr;
		pNldrNode->pPrivRef = pPrivRef;
		/* Save node's UUID. */
		pNldrNode->uuid = pNodeProps->ndbProps.uiNodeID;
		/*
		 *  Determine if node is a dynamically loaded node from
		 *  ndbProps.
		 */
		if (pNodeProps->usLoadType == NLDR_DYNAMICLOAD) {
			/* Dynamic node */
			pNldrNode->fDynamic = true;
			/*
			 *  Extract memory requirements from ndbProps masks
			 */
			/* Create phase */
			pNldrNode->segId[CREATEDATAFLAGBIT] = (u16)
				(pNodeProps->ulDataMemSegMask >> CREATEBIT) &
				SEGMASK;
			pNldrNode->codeDataFlagMask |=
				((pNodeProps->ulDataMemSegMask >>
				(CREATEBIT + FLAGBIT)) & 1) <<
				CREATEDATAFLAGBIT;
			pNldrNode->segId[CREATECODEFLAGBIT] = (u16)
				(pNodeProps->ulCodeMemSegMask >>
				CREATEBIT) & SEGMASK;
			pNldrNode->codeDataFlagMask |=
				((pNodeProps->ulCodeMemSegMask >>
				(CREATEBIT + FLAGBIT)) & 1) <<
				CREATECODEFLAGBIT;
			/* Execute phase */
			pNldrNode->segId[EXECUTEDATAFLAGBIT] = (u16)
				(pNodeProps->ulDataMemSegMask >>
				EXECUTEBIT) & SEGMASK;
			pNldrNode->codeDataFlagMask |=
				((pNodeProps->ulDataMemSegMask >>
				(EXECUTEBIT + FLAGBIT)) & 1) <<
				EXECUTEDATAFLAGBIT;
			pNldrNode->segId[EXECUTECODEFLAGBIT] = (u16)
				(pNodeProps->ulCodeMemSegMask >>
				EXECUTEBIT) & SEGMASK;
			pNldrNode->codeDataFlagMask |=
				((pNodeProps->ulCodeMemSegMask >>
				(EXECUTEBIT + FLAGBIT)) & 1) <<
				EXECUTECODEFLAGBIT;
			/* Delete phase */
			pNldrNode->segId[DELETEDATAFLAGBIT] = (u16)
			    (pNodeProps->ulDataMemSegMask >> DELETEBIT) &
			    SEGMASK;
			pNldrNode->codeDataFlagMask |=
				((pNodeProps->ulDataMemSegMask >>
				(DELETEBIT + FLAGBIT)) & 1) <<
				DELETEDATAFLAGBIT;
			pNldrNode->segId[DELETECODEFLAGBIT] = (u16)
				(pNodeProps->ulCodeMemSegMask >>
				DELETEBIT) & SEGMASK;
			pNldrNode->codeDataFlagMask |=
				((pNodeProps->ulCodeMemSegMask >>
				(DELETEBIT + FLAGBIT)) & 1) <<
				DELETECODEFLAGBIT;
		} else {
			/* Non-dynamically loaded nodes are part of the
			 * base image */
			pNldrNode->root.lib = hNldr->baseLib;
			/* Check for overlay node */
			if (pNodeProps->usLoadType == NLDR_OVLYLOAD)
				pNldrNode->fOverlay = true;

		}
		*phNldrNode = (struct NLDR_NODEOBJECT *) pNldrNode;
	}
	/* Cleanup on failure */
	if (DSP_FAILED(status) && pNldrNode)
		NLDR_Free((struct NLDR_NODEOBJECT *) pNldrNode);

	DBC_Ensure((DSP_SUCCEEDED(status) &&
		  MEM_IsValidHandle(((struct NLDR_NODEOBJECT *)(*phNldrNode)),
		  NLDR_NODESIGNATURE)) || (DSP_FAILED(status) &&
		  *phNldrNode == NULL));
	return status;
}

/*
 *  ======== NLDR_Create ========
 */
DSP_STATUS NLDR_Create(OUT struct NLDR_OBJECT **phNldr,
		      struct DEV_OBJECT *hDevObject,
		      IN CONST struct NLDR_ATTRS *pAttrs)
{
	struct COD_MANAGER *hCodMgr;	/* COD manager */
	char *pszCoffBuf = NULL;
	char szZLFile[COD_MAXPATHLENGTH];
	struct NLDR_OBJECT *pNldr = NULL;
	struct DBLL_Attrs saveAttrs;
	struct DBLL_Attrs newAttrs;
	DBLL_Flags flags;
	u32 ulEntry;
	u16 nSegs = 0;
	struct MemInfo *pMemInfo;
	u32 ulLen = 0;
	u32 ulAddr;
	struct RMM_Segment *rmmSegs = NULL;
	u16 i;
	DSP_STATUS status = DSP_SOK;
	DBC_Require(cRefs > 0);
	DBC_Require(phNldr != NULL);
	DBC_Require(hDevObject != NULL);
	DBC_Require(pAttrs != NULL);
	DBC_Require(pAttrs->pfnOvly != NULL);
	DBC_Require(pAttrs->pfnWrite != NULL);
	GT_3trace(NLDR_debugMask, GT_ENTER, "NLDR_Create(0x%x, 0x%x, 0x%x)\n",
		 phNldr, hDevObject, pAttrs);
	/* Allocate dynamic loader object */
	MEM_AllocObject(pNldr, struct NLDR_OBJECT, NLDR_SIGNATURE);
	if (pNldr) {
		pNldr->hDevObject = hDevObject;
		/* warning, lazy status checking alert! */
		status = DEV_GetCodMgr(hDevObject, &hCodMgr);
		DBC_Assert(DSP_SUCCEEDED(status));
		status = COD_GetLoader(hCodMgr, &pNldr->dbll);
		DBC_Assert(DSP_SUCCEEDED(status));
		status = COD_GetBaseLib(hCodMgr, &pNldr->baseLib);
		DBC_Assert(DSP_SUCCEEDED(status));
		status = COD_GetBaseName(hCodMgr, szZLFile, COD_MAXPATHLENGTH);
		DBC_Assert(DSP_SUCCEEDED(status));
		status = DSP_SOK;
		/* end lazy status checking */
		pNldr->usDSPMauSize = pAttrs->usDSPMauSize;
		pNldr->usDSPWordSize = pAttrs->usDSPWordSize;
		pNldr->dbllFxns = dbllFxns;
		if (!(pNldr->dbllFxns.initFxn()))
			status = DSP_EMEMORY;

	} else {
		GT_0trace(NLDR_debugMask, GT_6CLASS, "NLDR_Create: "
			 "Memory allocation failed\n");
		status = DSP_EMEMORY;
	}
	/* Create the DCD Manager */
	if (DSP_SUCCEEDED(status))
		status = DCD_CreateManager(NULL, &pNldr->hDcdMgr);

	/* Get dynamic loading memory sections from base lib */
	if (DSP_SUCCEEDED(status)) {
		status = pNldr->dbllFxns.getSectFxn(pNldr->baseLib, DYNMEMSECT,
			 &ulAddr, &ulLen);
		if (DSP_SUCCEEDED(status)) {
			pszCoffBuf = MEM_Calloc(ulLen * pNldr->usDSPMauSize,
						MEM_PAGED);
			if (!pszCoffBuf) {
				GT_0trace(NLDR_debugMask, GT_6CLASS,
					 "NLDR_Create: Memory "
					 "allocation failed\n");
				status = DSP_EMEMORY;
			}
		} else {
			/* Ok to not have dynamic loading memory */
			status = DSP_SOK;
			ulLen = 0;
			GT_1trace(NLDR_debugMask, GT_6CLASS,
				 "NLDR_Create: DBLL_getSect "
				 "failed (no dynamic loading mem segments): "
				 "0x%lx\n", status);
		}
	}
	if (DSP_SUCCEEDED(status) && ulLen > 0) {
		/* Read section containing dynamic load mem segments */
		status = pNldr->dbllFxns.readSectFxn(pNldr->baseLib, DYNMEMSECT,
						    pszCoffBuf, ulLen);
		if (DSP_FAILED(status)) {
			GT_1trace(NLDR_debugMask, GT_6CLASS,
				 "NLDR_Create: DBLL_read Section"
				 "failed: 0x%lx\n", status);
		}
	}
	if (DSP_SUCCEEDED(status) && ulLen > 0) {
		/* Parse memory segment data */
		nSegs = (u16)(*((u32 *)pszCoffBuf));
		if (nSegs > MAXMEMSEGS) {
			GT_1trace(NLDR_debugMask, GT_6CLASS,
				 "NLDR_Create: Invalid number of "
				 "dynamic load mem segments: 0x%lx\n", nSegs);
			status = DSP_ECORRUPTFILE;
		}
	}
	/* Parse dynamic load memory segments */
	if (DSP_SUCCEEDED(status) && nSegs > 0) {
		rmmSegs = MEM_Calloc(sizeof(struct RMM_Segment) * nSegs,
				    MEM_PAGED);
		pNldr->segTable = MEM_Calloc(sizeof(u32) * nSegs, MEM_PAGED);
		if (rmmSegs == NULL || pNldr->segTable == NULL) {
			status = DSP_EMEMORY;
		} else {
			pNldr->nSegs = nSegs;
			pMemInfo = (struct MemInfo *)(pszCoffBuf +
				   sizeof(u32));
			for (i = 0; i < nSegs; i++) {
				rmmSegs[i].base = (pMemInfo + i)->base;
				rmmSegs[i].length = (pMemInfo + i)->len;
				rmmSegs[i].space = 0;
				pNldr->segTable[i] = (pMemInfo + i)->type;
#ifdef DEBUG
				DBG_Trace(DBG_LEVEL7,
				    "** (proc) DLL MEMSEGMENT: %d, Base: 0x%x, "
				    "Length: 0x%x\n", i, rmmSegs[i].base,
				    rmmSegs[i].length);
#endif
			}
		}
	}
	/* Create Remote memory manager */
	if (DSP_SUCCEEDED(status))
		status = RMM_create(&pNldr->rmm, rmmSegs, nSegs);

	if (DSP_SUCCEEDED(status)) {
		/* set the alloc, free, write functions for loader */
		pNldr->dbllFxns.getAttrsFxn(pNldr->dbll, &saveAttrs);
		newAttrs = saveAttrs;
		newAttrs.alloc = (DBLL_AllocFxn) RemoteAlloc;
		newAttrs.free = (DBLL_FreeFxn) RemoteFree;
		newAttrs.symLookup = (DBLL_SymLookup) GetSymbolValue;
		newAttrs.symHandle = pNldr;
		newAttrs.write = (DBLL_WriteFxn) pAttrs->pfnWrite;
		pNldr->ovlyFxn = pAttrs->pfnOvly;
		pNldr->writeFxn = pAttrs->pfnWrite;
		pNldr->dbllAttrs = newAttrs;
	}
	if (rmmSegs)
		MEM_Free(rmmSegs);

	if (pszCoffBuf)
		MEM_Free(pszCoffBuf);

	/* Get overlay nodes */
	if (DSP_SUCCEEDED(status)) {
		status = COD_GetBaseName(hCodMgr, szZLFile, COD_MAXPATHLENGTH);
		/* lazy check */
		DBC_Assert(DSP_SUCCEEDED(status));
		/* First count number of overlay nodes */
		status = DCD_GetObjects(pNldr->hDcdMgr, szZLFile, AddOvlyNode,
					(void *) pNldr);
		/* Now build table of overlay nodes */
		if (DSP_SUCCEEDED(status) && pNldr->nOvlyNodes > 0) {
			/* Allocate table for overlay nodes */
			pNldr->ovlyTable =
			MEM_Calloc(sizeof(struct OvlyNode) * pNldr->nOvlyNodes,
				  MEM_PAGED);
			/* Put overlay nodes in the table */
			pNldr->nNode = 0;
			status = DCD_GetObjects(pNldr->hDcdMgr, szZLFile,
						AddOvlyNode,
						(void *) pNldr);
		}
	}
	/* Do a fake reload of the base image to get overlay section info */
	if (DSP_SUCCEEDED(status) && pNldr->nOvlyNodes > 0) {
		saveAttrs.write = fakeOvlyWrite;
		saveAttrs.logWrite = AddOvlyInfo;
		saveAttrs.logWriteHandle = pNldr;
		flags = DBLL_CODE | DBLL_DATA | DBLL_SYMB;
		status = pNldr->dbllFxns.loadFxn(pNldr->baseLib, flags,
						&saveAttrs, &ulEntry);
	}
	if (DSP_SUCCEEDED(status)) {
		*phNldr = (struct NLDR_OBJECT *) pNldr;
	} else {
		if (pNldr)
			NLDR_Delete((struct NLDR_OBJECT *) pNldr);

		*phNldr = NULL;
	}
	/* FIXME:Temp. Fix. Must be removed */
	DBC_Ensure((DSP_SUCCEEDED(status) &&
			 MEM_IsValidHandle(((struct NLDR_OBJECT *)*phNldr),
					  NLDR_SIGNATURE))
			|| (DSP_FAILED(status) && (*phNldr == NULL)));
	return status;
}

/*
 *  ======== NLDR_Delete ========
 */
void NLDR_Delete(struct NLDR_OBJECT *hNldr)
{
	struct OvlySect *pSect;
	struct OvlySect *pNext;
	u16 i;
	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(hNldr, NLDR_SIGNATURE));
	GT_1trace(NLDR_debugMask, GT_ENTER, "NLDR_Delete(0x%x)\n", hNldr);
	hNldr->dbllFxns.exitFxn();
	if (hNldr->rmm)
		RMM_delete(hNldr->rmm);

	if (hNldr->segTable)
		MEM_Free(hNldr->segTable);

	if (hNldr->hDcdMgr)
		DCD_DestroyManager(hNldr->hDcdMgr);

	/* Free overlay node information */
	if (hNldr->ovlyTable) {
		for (i = 0; i < hNldr->nOvlyNodes; i++) {
			pSect = hNldr->ovlyTable[i].pCreateSects;
			while (pSect) {
				pNext = pSect->pNextSect;
				MEM_Free(pSect);
				pSect = pNext;
			}
			pSect = hNldr->ovlyTable[i].pDeleteSects;
			while (pSect) {
				pNext = pSect->pNextSect;
				MEM_Free(pSect);
				pSect = pNext;
			}
			pSect = hNldr->ovlyTable[i].pExecuteSects;
			while (pSect) {
				pNext = pSect->pNextSect;
				MEM_Free(pSect);
				pSect = pNext;
			}
			pSect = hNldr->ovlyTable[i].pOtherSects;
			while (pSect) {
				pNext = pSect->pNextSect;
				MEM_Free(pSect);
				pSect = pNext;
			}
		}
		MEM_Free(hNldr->ovlyTable);
	}
	MEM_FreeObject(hNldr);
	DBC_Ensure(!MEM_IsValidHandle(hNldr, NLDR_SIGNATURE));
}

/*
 *  ======== NLDR_Exit ========
 *  Discontinue usage of NLDR module.
 */
void NLDR_Exit(void)
{
	DBC_Require(cRefs > 0);

	cRefs--;

	GT_1trace(NLDR_debugMask, GT_5CLASS,
		 "Entered NLDR_Exit, ref count:  0x%x\n", cRefs);

	if (cRefs == 0) {
		RMM_exit();
		NLDR_debugMask.flags = NULL;
	}

	DBC_Ensure(cRefs >= 0);
}

/*
 *  ======== NLDR_Free ========
 */
void NLDR_Free(struct NLDR_NODEOBJECT *hNldrNode)
{
	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(hNldrNode, NLDR_NODESIGNATURE));

	GT_1trace(NLDR_debugMask, GT_ENTER, "NLDR_Free(0x%x)\n", hNldrNode);

	MEM_FreeObject(hNldrNode);
}

/*
 *  ======== NLDR_GetFxnAddr ========
 */
DSP_STATUS NLDR_GetFxnAddr(struct NLDR_NODEOBJECT *hNldrNode, char *pstrFxn,
			  u32 *pulAddr)
{
	struct DBLL_Symbol *pSym;
	struct NLDR_OBJECT *hNldr;
	DSP_STATUS status = DSP_SOK;
	bool status1 = false;
	s32 i = 0;
	struct LibNode root = { NULL, 0, NULL };
	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(hNldrNode, NLDR_NODESIGNATURE));
	DBC_Require(pulAddr != NULL);
	DBC_Require(pstrFxn != NULL);
	GT_3trace(NLDR_debugMask, GT_ENTER, "NLDR_GetFxnAddr(0x%x, %s, 0x%x)\n",
		 hNldrNode, pstrFxn, pulAddr);

	hNldr = hNldrNode->pNldr;
	/* Called from NODE_Create(), NODE_Delete(), or NODE_Run(). */
	if (hNldrNode->fDynamic && *hNldrNode->pfPhaseSplit) {
		switch (hNldrNode->phase) {
		case NLDR_CREATE:
			root = hNldrNode->createLib;
			break;
		case NLDR_EXECUTE:
			root = hNldrNode->executeLib;
			break;
		case NLDR_DELETE:
			root = hNldrNode->deleteLib;
			break;
		default:
			DBC_Assert(false);
			break;
		}
	} else {
		/* for Overlay nodes or non-split Dynamic nodes */
		root = hNldrNode->root;
	}
	status1 = hNldr->dbllFxns.getCAddrFxn(root.lib, pstrFxn, &pSym);
	if (!status1)
		status1 = hNldr->dbllFxns.getAddrFxn(root.lib, pstrFxn, &pSym);

	/* If symbol not found, check dependent libraries */
	if (!status1) {
		for (i = 0; i < root.nDepLibs; i++) {
			status1 = hNldr->dbllFxns.getAddrFxn(root.pDepLibs[i].
					lib, pstrFxn, &pSym);
			if (!status1) {
				status1 = hNldr->dbllFxns.getCAddrFxn(root.
					pDepLibs[i].lib, pstrFxn, &pSym);
			}
			if (status1) {
				/* Symbol found */
				break;
			}
		}
	}
	/* Check persistent libraries */
	if (!status1) {
		for (i = 0; i < hNldrNode->nPersLib; i++) {
			status1 = hNldr->dbllFxns.getAddrFxn(hNldrNode->
					persLib[i].lib,	pstrFxn, &pSym);
			if (!status1) {
				status1 =
				    hNldr->dbllFxns.getCAddrFxn(hNldrNode->
					persLib[i].lib,	pstrFxn, &pSym);
			}
			if (status1) {
				/* Symbol found */
				break;
			}
		}
	}

	if (status1) {
		*pulAddr = pSym->value;
	} else {
		GT_1trace(NLDR_debugMask, GT_6CLASS,
			 "NLDR_GetFxnAddr: Symbol not found: "
			 "%s\n", pstrFxn);
		status = DSP_ESYMBOL;
	}

	return status;
}

/*
 *  ======== NLDR_GetRmmManager ========
 *  Given a NLDR object, retrieve RMM Manager Handle
 */
DSP_STATUS NLDR_GetRmmManager(struct NLDR_OBJECT *hNldrObject,
			     OUT struct RMM_TargetObj **phRmmMgr)
{
	DSP_STATUS status = DSP_SOK;
	struct NLDR_OBJECT *pNldrObject = hNldrObject;
	DBC_Require(phRmmMgr != NULL);
	GT_2trace(NLDR_debugMask, GT_ENTER, "NLDR_GetRmmManager(0x%x, 0x%x)\n",
		 hNldrObject, phRmmMgr);
	if (MEM_IsValidHandle(hNldrObject, NLDR_SIGNATURE)) {
		*phRmmMgr = pNldrObject->rmm;
	} else {
		*phRmmMgr = NULL;
		status = DSP_EHANDLE;
		GT_0trace(NLDR_debugMask, GT_7CLASS,
			 "NLDR_GetRmmManager:Invalid handle");
	}

	GT_2trace(NLDR_debugMask, GT_ENTER, "Exit NLDR_GetRmmManager: status "
		 "0x%x\n\tphRmmMgr:  0x%x\n", status, *phRmmMgr);

	DBC_Ensure(DSP_SUCCEEDED(status) || ((phRmmMgr != NULL) &&
		  (*phRmmMgr == NULL)));

	return status;
}

/*
 *  ======== NLDR_Init ========
 *  Initialize the NLDR module.
 */
bool NLDR_Init(void)
{
	DBC_Require(cRefs >= 0);

	if (cRefs == 0) {
		DBC_Assert(!NLDR_debugMask.flags);
		GT_create(&NLDR_debugMask, "NL");	/* "NL" for NLdr */

		RMM_init();
	}

	cRefs++;

	GT_1trace(NLDR_debugMask, GT_5CLASS, "NLDR_Init(), ref count: 0x%x\n",
		 cRefs);

	DBC_Ensure(cRefs > 0);
	return true;
}

/*
 *  ======== NLDR_Load ========
 */
DSP_STATUS NLDR_Load(struct NLDR_NODEOBJECT *hNldrNode, enum NLDR_PHASE phase)
{
	struct NLDR_OBJECT *hNldr;
	struct DSP_UUID libUUID;
	DSP_STATUS status = DSP_SOK;

	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(hNldrNode, NLDR_NODESIGNATURE));

	hNldr = hNldrNode->pNldr;

	GT_2trace(NLDR_debugMask, GT_ENTER, "NLDR_Load(0x%x, 0x%x)\n",
		 hNldrNode, phase);

	if (hNldrNode->fDynamic) {
		hNldrNode->phase = phase;

		libUUID = hNldrNode->uuid;

		/* At this point, we may not know if node is split into
		 * different libraries. So we'll go ahead and load the
		 * library, and then save the pointer to the appropriate
		 * location after we know. */

		status = LoadLib(hNldrNode, &hNldrNode->root, libUUID, false,
				hNldrNode->libPath, phase, 0);

		if (DSP_SUCCEEDED(status)) {
			if (*hNldrNode->pfPhaseSplit) {
				switch (phase) {
				case NLDR_CREATE:
					hNldrNode->createLib = hNldrNode->root;
					break;

				case NLDR_EXECUTE:
					hNldrNode->executeLib = hNldrNode->root;
					break;

				case NLDR_DELETE:
					hNldrNode->deleteLib = hNldrNode->root;
					break;

				default:
					DBC_Assert(false);
					break;
				}
			}
		}
	} else {
		if (hNldrNode->fOverlay)
			status = LoadOvly(hNldrNode, phase);

	}

	return status;
}

/*
 *  ======== NLDR_Unload ========
 */
DSP_STATUS NLDR_Unload(struct NLDR_NODEOBJECT *hNldrNode, enum NLDR_PHASE phase)
{
	DSP_STATUS status = DSP_SOK;
	struct LibNode *pRootLib = NULL;
	s32 i = 0;

	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(hNldrNode, NLDR_NODESIGNATURE));
	GT_2trace(NLDR_debugMask, GT_ENTER, "NLDR_Unload(0x%x, 0x%x)\n",
		 hNldrNode, phase);
	if (hNldrNode != NULL) {
		if (hNldrNode->fDynamic) {
			if (*hNldrNode->pfPhaseSplit) {
				switch (phase) {
				case NLDR_CREATE:
					pRootLib = &hNldrNode->createLib;
					break;
				case NLDR_EXECUTE:
					pRootLib = &hNldrNode->executeLib;
					break;
				case NLDR_DELETE:
					pRootLib = &hNldrNode->deleteLib;
					/* Unload persistent libraries */
					for (i = 0; i < hNldrNode->nPersLib;
					    i++) {
						UnloadLib(hNldrNode,
							&hNldrNode->persLib[i]);
					}
					hNldrNode->nPersLib = 0;
					break;
				default:
					DBC_Assert(false);
					break;
				}
			} else {
				/* Unload main library */
				pRootLib = &hNldrNode->root;
			}
			UnloadLib(hNldrNode, pRootLib);
		} else {
			if (hNldrNode->fOverlay)
				UnloadOvly(hNldrNode, phase);

		}
	}
	return status;
}

/*
 *  ======== AddOvlyInfo ========
 */
static DSP_STATUS AddOvlyInfo(void *handle, struct DBLL_SectInfo *sectInfo,
			     u32 addr, u32 nBytes)
{
	char *pNodeName;
	char *pSectName = (char *)sectInfo->name;
	bool fExists = false;
	char seps = ':';
	char *pch;
	u16 i;
	struct NLDR_OBJECT *hNldr = (struct NLDR_OBJECT *)handle;
	DSP_STATUS status = DSP_SOK;

	/* Is this an overlay section (load address != run address)? */
	if (sectInfo->loadAddr == sectInfo->runAddr)
		goto func_end;

	/* Find the node it belongs to */
	for (i = 0; i < hNldr->nOvlyNodes; i++) {
		pNodeName = hNldr->ovlyTable[i].pNodeName;
               DBC_Require(pNodeName);
               if (strncmp(pNodeName, pSectName + 1,
                               strlen(pNodeName)) == 0) {
				/* Found the node */
				break;
		}
	}
	if (!(i < hNldr->nOvlyNodes))
		goto func_end;

	/* Determine which phase this section belongs to */
	for (pch = pSectName + 1; *pch && *pch != seps; pch++)
		;;

	if (*pch) {
		pch++;	/* Skip over the ':' */
               if (strncmp(pch, PCREATE, strlen(PCREATE)) == 0) {
			status = AddOvlySect(hNldr, &hNldr->ovlyTable[i].
				pCreateSects, sectInfo, &fExists, addr, nBytes);
			if (DSP_SUCCEEDED(status) && !fExists)
				hNldr->ovlyTable[i].nCreateSects++;

		} else
               if (strncmp(pch, PDELETE, strlen(PDELETE)) == 0) {
			status = AddOvlySect(hNldr, &hNldr->ovlyTable[i].
					    pDeleteSects, sectInfo, &fExists,
					    addr, nBytes);
			if (DSP_SUCCEEDED(status) && !fExists)
				hNldr->ovlyTable[i].nDeleteSects++;

		} else
               if (strncmp(pch, PEXECUTE, strlen(PEXECUTE)) == 0) {
			status = AddOvlySect(hNldr, &hNldr->ovlyTable[i].
					    pExecuteSects, sectInfo, &fExists,
					    addr, nBytes);
			if (DSP_SUCCEEDED(status) && !fExists)
				hNldr->ovlyTable[i].nExecuteSects++;

		} else {
			/* Put in "other" sectins */
			status = AddOvlySect(hNldr, &hNldr->ovlyTable[i].
					    pOtherSects, sectInfo, &fExists,
					    addr, nBytes);
			if (DSP_SUCCEEDED(status) && !fExists)
				hNldr->ovlyTable[i].nOtherSects++;

		}
	}
func_end:
	return status;
}

/*
 *  ======== AddOvlyNode =========
 *  Callback function passed to DCD_GetObjects.
 */
static DSP_STATUS AddOvlyNode(struct DSP_UUID *pUuid,
			     enum DSP_DCDOBJTYPE objType,
			     IN void *handle)
{
	struct NLDR_OBJECT *hNldr = (struct NLDR_OBJECT *)handle;
	char *pNodeName = NULL;
	char *pBuf = NULL;
	u32 uLen;
	struct DCD_GENERICOBJ objDef;
	DSP_STATUS status = DSP_SOK;

	if (objType != DSP_DCDNODETYPE)
		goto func_end;

	status = DCD_GetObjectDef(hNldr->hDcdMgr, pUuid, objType, &objDef);
	if (DSP_FAILED(status))
		goto func_end;

	/* If overlay node, add to the list */
	if (objDef.objData.nodeObj.usLoadType == NLDR_OVLYLOAD) {
		if (hNldr->ovlyTable == NULL) {
			hNldr->nOvlyNodes++;
		} else {
			/* Add node to table */
			hNldr->ovlyTable[hNldr->nNode].uuid = *pUuid;
                       DBC_Require(objDef.objData.nodeObj.ndbProps.acName);
                       uLen = strlen(objDef.objData.nodeObj.ndbProps.acName);
			pNodeName = objDef.objData.nodeObj.ndbProps.acName;
			pBuf = MEM_Calloc(uLen + 1, MEM_PAGED);
			if (pBuf == NULL) {
				status = DSP_EMEMORY;
			} else {
                               strncpy(pBuf, pNodeName, uLen);
				hNldr->ovlyTable[hNldr->nNode].pNodeName = pBuf;
				hNldr->nNode++;
			}
		}
	}
	/* These were allocated in DCD_GetObjectDef */
	if (objDef.objData.nodeObj.pstrCreatePhaseFxn)
		MEM_Free(objDef.objData.nodeObj.pstrCreatePhaseFxn);

	if (objDef.objData.nodeObj.pstrExecutePhaseFxn)
		MEM_Free(objDef.objData.nodeObj.pstrExecutePhaseFxn);

	if (objDef.objData.nodeObj.pstrDeletePhaseFxn)
		MEM_Free(objDef.objData.nodeObj.pstrDeletePhaseFxn);

	if (objDef.objData.nodeObj.pstrIAlgName)
		MEM_Free(objDef.objData.nodeObj.pstrIAlgName);

func_end:
	return status;
}

/*
 *  ======== AddOvlySect ========
 */
static DSP_STATUS AddOvlySect(struct NLDR_OBJECT *hNldr,
			      struct OvlySect **pList,
			      struct DBLL_SectInfo *pSectInfo, bool *pExists,
			      u32 addr, u32 nBytes)
{
	struct OvlySect *pNewSect = NULL;
	struct OvlySect *pLastSect;
	struct OvlySect *pSect;
	DSP_STATUS status = DSP_SOK;

	pSect = pLastSect = *pList;
	*pExists = false;
	while (pSect) {
		/*
		 *  Make sure section has not already been added. Multiple
		 *  'write' calls may be made to load the section.
		 */
		if (pSect->loadAddr == addr) {
			/* Already added */
			*pExists = true;
			break;
		}
		pLastSect = pSect;
		pSect = pSect->pNextSect;
	}

	if (!pSect) {
		/* New section */
		pNewSect = MEM_Calloc(sizeof(struct OvlySect), MEM_PAGED);
		if (pNewSect == NULL) {
			status = DSP_EMEMORY;
		} else {
			pNewSect->loadAddr = addr;
			pNewSect->runAddr = pSectInfo->runAddr +
					    (addr - pSectInfo->loadAddr);
			pNewSect->size = nBytes;
			pNewSect->page = pSectInfo->type;
		}

		/* Add to the list */
		if (DSP_SUCCEEDED(status)) {
			if (*pList == NULL) {
				/* First in the list */
				*pList = pNewSect;
			} else {
				pLastSect->pNextSect = pNewSect;
			}
		}
	}

	return status;
}

/*
 *  ======== fakeOvlyWrite ========
 */
static s32 fakeOvlyWrite(void *handle, u32 dspAddr, void *buf, u32 nBytes,
			s32 mtype)
{
	return (s32)nBytes;
}

/*
 *  ======== FreeSects ========
 */
static void FreeSects(struct NLDR_OBJECT *hNldr, struct OvlySect *pPhaseSects,
		     u16 nAlloc)
{
	struct OvlySect *pSect = pPhaseSects;
	u16 i = 0;
	bool fRet;

	while (pSect && i < nAlloc) {
		/* 'Deallocate' */
		/* segid - page not supported yet */
		/* Reserved memory */
		fRet = RMM_free(hNldr->rmm, 0, pSect->runAddr, pSect->size,
				true);
		DBC_Assert(fRet);
		pSect = pSect->pNextSect;
		i++;
	}
}

/*
 *  ======== GetSymbolValue ========
 *  Find symbol in library's base image.  If not there, check dependent
 *  libraries.
 */
static bool GetSymbolValue(void *handle, void *pArg, void *rmmHandle,
			  char *name, struct DBLL_Symbol **sym)
{
	struct NLDR_OBJECT *hNldr = (struct NLDR_OBJECT *)handle;
	struct NLDR_NODEOBJECT *hNldrNode = (struct NLDR_NODEOBJECT *)rmmHandle;
	struct LibNode *root = (struct LibNode *)pArg;
	u16 i;
	bool status = false;

	/* check the base image */
	status = hNldr->dbllFxns.getAddrFxn(hNldr->baseLib, name, sym);
	if (!status)
		status = hNldr->dbllFxns.getCAddrFxn(hNldr->baseLib, name, sym);

	/*
	 *  Check in root lib itself. If the library consists of
	 *  multiple object files linked together, some symbols in the
	 *  library may need to be resolved.
	 */
	if (!status) {
		status = hNldr->dbllFxns.getAddrFxn(root->lib, name, sym);
		if (!status) {
			status =
			    hNldr->dbllFxns.getCAddrFxn(root->lib, name, sym);
		}
	}

	/*
	 *  Check in root lib's dependent libraries, but not dependent
	 *  libraries' dependents.
	 */
	if (!status) {
		for (i = 0; i < root->nDepLibs; i++) {
			status = hNldr->dbllFxns.getAddrFxn(root->pDepLibs[i].
							   lib, name, sym);
			if (!status) {
				status = hNldr->dbllFxns.getCAddrFxn(root->
					 pDepLibs[i].lib, name, sym);
			}
			if (status) {
				/* Symbol found */
				break;
			}
		}
	}
	/*
	 * Check in persistent libraries
	 */
	if (!status) {
		for (i = 0; i < hNldrNode->nPersLib; i++) {
			status = hNldr->dbllFxns.getAddrFxn(hNldrNode->
				 persLib[i].lib, name, sym);
			if (!status) {
				status = hNldr->dbllFxns.getCAddrFxn
					(hNldrNode->persLib[i].lib, name, sym);
			}
			if (status) {
				/* Symbol found */
				break;
			}
		}
	}

	return status;
}

/*
 *  ======== LoadLib ========
 *  Recursively load library and all its dependent libraries. The library
 *  we're loading is specified by a uuid.
 */
static DSP_STATUS LoadLib(struct NLDR_NODEOBJECT *hNldrNode,
			 struct LibNode *root, struct DSP_UUID uuid,
			 bool rootPersistent, struct DBLL_LibraryObj **libPath,
			 enum NLDR_PHASE phase, u16 depth)
{
	struct NLDR_OBJECT *hNldr = hNldrNode->pNldr;
	u16 nLibs = 0;	/* Number of dependent libraries */
	u16 nPLibs = 0;	/* Number of persistent libraries */
	u16 nLoaded = 0;	/* Number of dep. libraries loaded */
	u16 i;
	u32 entry;
	u32 dwBufSize = NLDR_MAXPATHLENGTH;
	DBLL_Flags flags = DBLL_SYMB | DBLL_CODE | DBLL_DATA | DBLL_DYNAMIC;
	struct DBLL_Attrs newAttrs;
	char *pszFileName = NULL;
	struct DSP_UUID *depLibUUIDs = NULL;
	bool *persistentDepLibs = NULL;
	DSP_STATUS status = DSP_SOK;
	bool fStatus = false;
	struct LibNode *pDepLib;

	if (depth > MAXDEPTH) {
		/* Error */
		DBC_Assert(false);
	}
	root->lib = NULL;
	/* Allocate a buffer for library file name of size DBL_MAXPATHLENGTH */
	pszFileName = MEM_Calloc(DBLL_MAXPATHLENGTH, MEM_PAGED);
	if (pszFileName == NULL)
		status = DSP_EMEMORY;

	if (DSP_SUCCEEDED(status)) {
		/* Get the name of the library */
		if (depth == 0) {
			status = DCD_GetLibraryName(hNldrNode->pNldr->hDcdMgr,
				&uuid, pszFileName, &dwBufSize, phase,
				hNldrNode->pfPhaseSplit);
		} else {
			/* Dependent libraries are registered with a phase */
			status = DCD_GetLibraryName(hNldrNode->pNldr->hDcdMgr,
				&uuid, pszFileName, &dwBufSize, NLDR_NOPHASE,
				NULL);
		}
	}
	if (DSP_SUCCEEDED(status)) {
		/* Open the library, don't load symbols */
		status = hNldr->dbllFxns.openFxn(hNldr->dbll, pszFileName,
			 DBLL_NOLOAD, &root->lib);
	}
	/* Done with file name */
	if (pszFileName)
		MEM_Free(pszFileName);

	/* Check to see if library not already loaded */
	if (DSP_SUCCEEDED(status) && rootPersistent) {
		fStatus = findInPersistentLibArray(hNldrNode, root->lib);
		/* Close library */
		if (fStatus) {
			hNldr->dbllFxns.closeFxn(root->lib);
			return DSP_SALREADYLOADED;
		}
	}
	if (DSP_SUCCEEDED(status)) {
		/* Check for circular dependencies. */
		for (i = 0; i < depth; i++) {
			if (root->lib == libPath[i]) {
				/* This condition could be checked by a
				 * tool at build time. */
				status = DSP_EDYNLOAD;
			}
		}
	}
	if (DSP_SUCCEEDED(status)) {
		/* Add library to current path in dependency tree */
		libPath[depth] = root->lib;
		depth++;
		/* Get number of dependent libraries */
		status = DCD_GetNumDepLibs(hNldrNode->pNldr->hDcdMgr, &uuid,
					  &nLibs, &nPLibs, phase);
	}
	DBC_Assert(nLibs >= nPLibs);
	if (DSP_SUCCEEDED(status)) {
		if (!(*hNldrNode->pfPhaseSplit))
			nPLibs = 0;

		/* nLibs = #of dependent libraries */
		root->nDepLibs = nLibs - nPLibs;
		if (nLibs > 0) {
			depLibUUIDs = MEM_Calloc(sizeof(struct DSP_UUID) *
				      nLibs, MEM_PAGED);
			persistentDepLibs =
				MEM_Calloc(sizeof(bool) * nLibs, MEM_PAGED);
			if (!depLibUUIDs || !persistentDepLibs)
				status = DSP_EMEMORY;

			if (root->nDepLibs > 0) {
				/* Allocate arrays for dependent lib UUIDs,
				 * lib nodes */
				root->pDepLibs = MEM_Calloc
					(sizeof(struct LibNode) *
					(root->nDepLibs), MEM_PAGED);
				if (!(root->pDepLibs))
					status = DSP_EMEMORY;

			}

			if (DSP_SUCCEEDED(status)) {
				/* Get the dependent library UUIDs */
				status = DCD_GetDepLibs(hNldrNode->pNldr->
					hDcdMgr, &uuid, nLibs, depLibUUIDs,
					persistentDepLibs, phase);
			}
		}
	}

	/*
	 *  Recursively load dependent libraries.
	 */
	if (DSP_SUCCEEDED(status) && persistentDepLibs) {
		for (i = 0; i < nLibs; i++) {
			/* If root library is NOT persistent, and dep library
			 * is, then record it.  If root library IS persistent,
			 * the deplib is already included */
			if (!rootPersistent && persistentDepLibs[i] &&
			   *hNldrNode->pfPhaseSplit) {
				if ((hNldrNode->nPersLib) > MAXLIBS) {
					status = DSP_EDYNLOAD;
					break;
				}

				/* Allocate library outside of phase */
				pDepLib = &hNldrNode->persLib[hNldrNode->
					  nPersLib];
			} else {
				if (rootPersistent)
					persistentDepLibs[i] = true;


				/* Allocate library within phase */
				pDepLib = &root->pDepLibs[nLoaded];
			}

			if (depLibUUIDs) {
				status = LoadLib(hNldrNode, pDepLib,
						depLibUUIDs[i],
						persistentDepLibs[i], libPath,
						phase,
						depth);
			} else {
				status = DSP_EMEMORY;
			}

			if (DSP_SUCCEEDED(status)) {
				if ((status != DSP_SALREADYLOADED) &&
				   !rootPersistent && persistentDepLibs[i] &&
				   *hNldrNode->pfPhaseSplit) {
					(hNldrNode->nPersLib)++;
				} else {
					if (!persistentDepLibs[i] ||
					   !(*hNldrNode->pfPhaseSplit)) {
						nLoaded++;
					}
				}
			} else {
				break;
			}
		}
	}

	/* Now we can load the root library */
	if (DSP_SUCCEEDED(status)) {
		newAttrs = hNldr->dbllAttrs;
		newAttrs.symArg = root;
		newAttrs.rmmHandle = hNldrNode;
		newAttrs.wHandle = hNldrNode->pPrivRef;
		newAttrs.baseImage = false;

		status = hNldr->dbllFxns.loadFxn(root->lib, flags, &newAttrs,
			 &entry);
	}

	/*
	 *  In case of failure, unload any dependent libraries that
	 *  were loaded, and close the root library.
	 *  (Persistent libraries are unloaded from the very top)
	 */
	if (DSP_FAILED(status)) {
		if (phase != NLDR_EXECUTE) {
			for (i = 0; i < hNldrNode->nPersLib; i++)
				UnloadLib(hNldrNode, &hNldrNode->persLib[i]);

			hNldrNode->nPersLib = 0;
		}
		for (i = 0; i < nLoaded; i++)
			UnloadLib(hNldrNode, &root->pDepLibs[i]);

		if (root->lib)
			hNldr->dbllFxns.closeFxn(root->lib);

	}

	/* Going up one node in the dependency tree */
	depth--;

	if (depLibUUIDs) {
		MEM_Free(depLibUUIDs);
		depLibUUIDs = NULL;
	}

	if (persistentDepLibs) {
		MEM_Free(persistentDepLibs);
		persistentDepLibs = NULL;
	}

	return status;
}

/*
 *  ======== LoadOvly ========
 */
static DSP_STATUS LoadOvly(struct NLDR_NODEOBJECT *hNldrNode,
			  enum NLDR_PHASE phase)
{
	struct NLDR_OBJECT *hNldr = hNldrNode->pNldr;
	struct OvlyNode *pONode = NULL;
	struct OvlySect *pPhaseSects = NULL;
	struct OvlySect *pOtherSects = NULL;
	u16 i;
	u16 nAlloc = 0;
	u16 nOtherAlloc = 0;
	u16 *pRefCount = NULL;
	u16 *pOtherRef = NULL;
	u32 nBytes;
	struct OvlySect *pSect;
	DSP_STATUS status = DSP_SOK;

	/* Find the node in the table */
	for (i = 0; i < hNldr->nOvlyNodes; i++) {
		if (IsEqualUUID(hNldrNode->uuid, hNldr->ovlyTable[i].uuid)) {
			/* Found it */
			pONode = &(hNldr->ovlyTable[i]);
			break;
		}
	}

	DBC_Assert(i < hNldr->nOvlyNodes);
	switch (phase) {
	case NLDR_CREATE:
		pRefCount = &(pONode->createRef);
		pOtherRef = &(pONode->otherRef);
		pPhaseSects = pONode->pCreateSects;
		pOtherSects = pONode->pOtherSects;
		break;

	case NLDR_EXECUTE:
		pRefCount = &(pONode->executeRef);
		pPhaseSects = pONode->pExecuteSects;
		break;

	case NLDR_DELETE:
		pRefCount = &(pONode->deleteRef);
		pPhaseSects = pONode->pDeleteSects;
		break;

	default:
		DBC_Assert(false);
		break;
	}

	DBC_Assert(pRefCount != NULL);
	if (DSP_FAILED(status))
		goto func_end;

	if (pRefCount == NULL)
		goto func_end;

	if (*pRefCount != 0)
		goto func_end;

	/* 'Allocate' memory for overlay sections of this phase */
	pSect = pPhaseSects;
	while (pSect) {
		/* allocate */ /* page not supported yet */
		  /* reserve */ /* align */
		status = RMM_alloc(hNldr->rmm, 0, pSect->size, 0,
			 &(pSect->runAddr), true);
		if (DSP_SUCCEEDED(status)) {
			pSect = pSect->pNextSect;
			nAlloc++;
		} else {
			break;
		}
	}
	if (pOtherRef && *pOtherRef == 0) {
		/* 'Allocate' memory for other overlay sections
		 * (create phase) */
		if (DSP_SUCCEEDED(status)) {
			pSect = pOtherSects;
			while (pSect) {
				/* page not supported */ /* align */
				/* reserve */
				status = RMM_alloc(hNldr->rmm, 0, pSect->size,
					 0, &(pSect->runAddr), true);
				if (DSP_SUCCEEDED(status)) {
					pSect = pSect->pNextSect;
					nOtherAlloc++;
				} else {
					break;
				}
			}
		}
	}
	if (*pRefCount == 0) {
		if (DSP_SUCCEEDED(status)) {
			/* Load sections for this phase */
			pSect = pPhaseSects;
			while (pSect && DSP_SUCCEEDED(status)) {
				nBytes = (*hNldr->ovlyFxn)(hNldrNode->pPrivRef,
					 pSect->runAddr, pSect->loadAddr,
					 pSect->size, pSect->page);
				if (nBytes != pSect->size)
					status = DSP_EFAIL;

				pSect = pSect->pNextSect;
			}
		}
	}
	if (pOtherRef && *pOtherRef == 0) {
		if (DSP_SUCCEEDED(status)) {
			/* Load other sections (create phase) */
			pSect = pOtherSects;
			while (pSect && DSP_SUCCEEDED(status)) {
				nBytes = (*hNldr->ovlyFxn)(hNldrNode->pPrivRef,
					 pSect->runAddr, pSect->loadAddr,
					 pSect->size, pSect->page);
				if (nBytes != pSect->size)
					status = DSP_EFAIL;

				pSect = pSect->pNextSect;
			}
		}
	}
	if (DSP_FAILED(status)) {
		/* 'Deallocate' memory */
		FreeSects(hNldr, pPhaseSects, nAlloc);
		FreeSects(hNldr, pOtherSects, nOtherAlloc);
	}
func_end:
	if (DSP_SUCCEEDED(status) && (pRefCount != NULL)) {
		*pRefCount += 1;
		if (pOtherRef)
			*pOtherRef += 1;

	}

	return status;
}

/*
 *  ======== RemoteAlloc ========
 */
static DSP_STATUS RemoteAlloc(void **pRef, u16 space, u32 size,
			     u32 align, u32 *dspAddr,
			     OPTIONAL s32 segmentId, OPTIONAL s32 req,
			     bool reserve)
{
	struct NLDR_NODEOBJECT *hNode = (struct NLDR_NODEOBJECT *)pRef;
	struct NLDR_OBJECT *hNldr;
	struct RMM_TargetObj *rmm;
	u16 memPhaseBit = MAXFLAGS;
	u16 segid = 0;
	u16 i;
	u16 memType;
	u32 nWords;
	struct RMM_Addr *pRmmAddr = (struct RMM_Addr *)dspAddr;
	bool fReq = false;
	DSP_STATUS status = DSP_EMEMORY;	/* Set to fail */
	DBC_Require(MEM_IsValidHandle(hNode, NLDR_NODESIGNATURE));
	DBC_Require(space == DBLL_CODE || space == DBLL_DATA ||
		   space == DBLL_BSS);
	hNldr = hNode->pNldr;
	rmm = hNldr->rmm;
	/* Convert size to DSP words */
	nWords = (size + hNldr->usDSPWordSize - 1) / hNldr->usDSPWordSize;
	/* Modify memory 'align' to account for DSP cache line size */
	align = findLcm(GEM_CACHE_LINE_SIZE, align);
	GT_1trace(NLDR_debugMask, GT_7CLASS,
		 "RemoteAlloc: memory align to 0x%x \n", align);
	if (segmentId != -1) {
		pRmmAddr->segid = segmentId;
		segid = segmentId;
		fReq = req;
	} else {
		switch (hNode->phase) {
		case NLDR_CREATE:
			memPhaseBit = CREATEDATAFLAGBIT;
			break;
		case NLDR_DELETE:
			memPhaseBit = DELETEDATAFLAGBIT;
			break;
		case NLDR_EXECUTE:
			memPhaseBit = EXECUTEDATAFLAGBIT;
			break;
		default:
			DBC_Assert(false);
			break;
		}
		if (space == DBLL_CODE)
			memPhaseBit++;

		if (memPhaseBit < MAXFLAGS)
			segid = hNode->segId[memPhaseBit];

		/* Determine if there is a memory loading requirement */
		if ((hNode->codeDataFlagMask >> memPhaseBit) & 0x1)
			fReq = true;

	}
	memType = (space == DBLL_CODE) ? DYNM_CODE : DYNM_DATA;

	/* Find an appropriate segment based on space */
	if (segid == NULLID) {
		/* No memory requirements of preferences */
		DBC_Assert(!fReq);
		goto func_cont;
	}
	if (segid <= MAXSEGID) {
		DBC_Assert(segid < hNldr->nSegs);
		/* Attempt to allocate from segid first. */
		pRmmAddr->segid = segid;
		status = RMM_alloc(rmm, segid, nWords, align, dspAddr, false);
		if (DSP_FAILED(status)) {
			GT_1trace(NLDR_debugMask, GT_6CLASS,
				 "RemoteAlloc:Unable allocate "
				 "from segment %d.\n", segid);
		}
	} else {
		/* segid > MAXSEGID ==> Internal or external memory */
		DBC_Assert(segid == MEMINTERNALID || segid == MEMEXTERNALID);
		 /*  Check for any internal or external memory segment,
		  *  depending on segid.*/
		memType |= segid == MEMINTERNALID ?
				 DYNM_INTERNAL : DYNM_EXTERNAL;
		for (i = 0; i < hNldr->nSegs; i++) {
			if ((hNldr->segTable[i] & memType) != memType)
				continue;

			status = RMM_alloc(rmm, i, nWords, align, dspAddr,
					   false);
			if (DSP_SUCCEEDED(status)) {
				/* Save segid for freeing later */
				pRmmAddr->segid = i;
				break;
			}
		}
	}
func_cont:
	/* Haven't found memory yet, attempt to find any segment that works */
	if (status == DSP_EMEMORY && !fReq) {
		GT_0trace(NLDR_debugMask, GT_6CLASS,
			 "RemoteAlloc: Preferred segment "
			 "unavailable, trying another segment.\n");
		for (i = 0; i < hNldr->nSegs; i++) {
			/* All bits of memType must be set */
			if ((hNldr->segTable[i] & memType) != memType)
				continue;

			status = RMM_alloc(rmm, i, nWords, align, dspAddr,
					  false);
			if (DSP_SUCCEEDED(status)) {
				/* Save segid */
				pRmmAddr->segid = i;
				break;
			}
		}
	}

	return status;
}

static DSP_STATUS RemoteFree(void **pRef, u16 space, u32 dspAddr,
				u32 size, bool reserve)
{
	struct NLDR_OBJECT *hNldr = (struct NLDR_OBJECT *)pRef;
	struct RMM_TargetObj *rmm;
	u32 nWords;
	DSP_STATUS status = DSP_EMEMORY;	/* Set to fail */

	DBC_Require(MEM_IsValidHandle(hNldr, NLDR_SIGNATURE));

	rmm = hNldr->rmm;

	/* Convert size to DSP words */
	nWords = (size + hNldr->usDSPWordSize - 1) / hNldr->usDSPWordSize;

	if (RMM_free(rmm, space, dspAddr, nWords, reserve))
		status = DSP_SOK;

	return status;
}

/*
 *  ======== UnloadLib ========
 */
static void UnloadLib(struct NLDR_NODEOBJECT *hNldrNode, struct LibNode *root)
{
	struct DBLL_Attrs newAttrs;
	struct NLDR_OBJECT *hNldr = hNldrNode->pNldr;
	u16 i;

	DBC_Assert(root != NULL);

	/* Unload dependent libraries */
	for (i = 0; i < root->nDepLibs; i++)
		UnloadLib(hNldrNode, &root->pDepLibs[i]);

	root->nDepLibs = 0;

	newAttrs = hNldr->dbllAttrs;
	newAttrs.rmmHandle = hNldr->rmm;
	newAttrs.wHandle = hNldrNode->pPrivRef;
	newAttrs.baseImage = false;
	newAttrs.symArg = root;

	if (root->lib) {
		/* Unload the root library */
		hNldr->dbllFxns.unloadFxn(root->lib, &newAttrs);
		hNldr->dbllFxns.closeFxn(root->lib);
	}

	/* Free dependent library list */
	if (root->pDepLibs) {
		MEM_Free(root->pDepLibs);
		root->pDepLibs = NULL;
	}
}

/*
 *  ======== UnloadOvly ========
 */
static void UnloadOvly(struct NLDR_NODEOBJECT *hNldrNode, enum NLDR_PHASE phase)
{
	struct NLDR_OBJECT *hNldr = hNldrNode->pNldr;
	struct OvlyNode *pONode = NULL;
	struct OvlySect *pPhaseSects = NULL;
	struct OvlySect *pOtherSects = NULL;
	u16 i;
	u16 nAlloc = 0;
	u16 nOtherAlloc = 0;
	u16 *pRefCount = NULL;
	u16 *pOtherRef = NULL;
	DSP_STATUS status = DSP_SOK;

	/* Find the node in the table */
	for (i = 0; i < hNldr->nOvlyNodes; i++) {
		if (IsEqualUUID(hNldrNode->uuid, hNldr->ovlyTable[i].uuid)) {
			/* Found it */
			pONode = &(hNldr->ovlyTable[i]);
			break;
		}
	}

	DBC_Assert(i < hNldr->nOvlyNodes);
	switch (phase) {
	case NLDR_CREATE:
		pRefCount = &(pONode->createRef);
		pPhaseSects = pONode->pCreateSects;
		nAlloc = pONode->nCreateSects;
		break;
	case NLDR_EXECUTE:
		pRefCount = &(pONode->executeRef);
		pPhaseSects = pONode->pExecuteSects;
		nAlloc = pONode->nExecuteSects;
		break;
	case NLDR_DELETE:
		pRefCount = &(pONode->deleteRef);
		pOtherRef = &(pONode->otherRef);
		pPhaseSects = pONode->pDeleteSects;
		/* 'Other' overlay sections are unloaded in the delete phase */
		pOtherSects = pONode->pOtherSects;
		nAlloc = pONode->nDeleteSects;
		nOtherAlloc = pONode->nOtherSects;
		break;
	default:
		DBC_Assert(false);
		break;
	}
	if (DSP_SUCCEEDED(status)) {
		DBC_Assert(pRefCount && (*pRefCount > 0));
		 if (pRefCount && (*pRefCount > 0)) {
			*pRefCount -= 1;
			if (pOtherRef) {
				DBC_Assert(*pOtherRef > 0);
				*pOtherRef -= 1;
			}
		}
	}
	if (pRefCount && (*pRefCount == 0)) {
		/* 'Deallocate' memory */
		FreeSects(hNldr, pPhaseSects, nAlloc);
	}
	if (pOtherRef && *pOtherRef == 0)
		FreeSects(hNldr, pOtherSects, nOtherAlloc);

}

/*
 *  ======== findInPersistentLibArray ========
 */
static bool findInPersistentLibArray(struct NLDR_NODEOBJECT *hNldrNode,
				    struct DBLL_LibraryObj *lib)
{
	s32 i = 0;

	for (i = 0; i < hNldrNode->nPersLib; i++) {
		if (lib == hNldrNode->persLib[i].lib)
			return true;

	}

	return false;
}

/*
 * ================ Find LCM (Least Common Multiplier ===
 */
static u32 findLcm(u32 a, u32 b)
{
	u32 retVal;

	retVal = a * b / findGcf(a, b);

	return retVal;
}

/*
 * ================ Find GCF (Greatest Common Factor ) ===
 */
static u32 findGcf(u32 a, u32 b)
{
	u32 c;

	/* Get the GCF (Greatest common factor between the numbers,
	 * using Euclidian Algo */
	while ((c = (a % b))) {
		a = b;
		b = c;
	}
	return b;
}

