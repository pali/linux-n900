/*
 * node.c
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
 *  ======== node.c ========
 *
 *  Description:
 *      DSP/BIOS Bridge Node Manager.
 *
 *  Public Functions:
 *      NODE_Allocate
 *      NODE_AllocMsgBuf
 *      NODE_ChangePriority
 *      NODE_Connect
 *      NODE_Create
 *      NODE_CreateMgr
 *      NODE_Delete
 *      NODE_DeleteMgr
 *      NODE_EnumNodes
 *      NODE_Exit
 *      NODE_FreeMsgBuf
 *      NODE_GetAttr
 *      NODE_GetChannelId
 *      NODE_GetMessage
 *      NODE_GetStrmMgr
 *      NODE_Init
 *      NODE_OnExit
 *      NODE_Pause
 *      NODE_PutMessage
 *      NODE_RegisterNotify
 *      NODE_Run
 *      NODE_Terminate
 *
 *! Revision History:
 *! =================
 *! 12-Apr-2004 hp  Compile IVA only for 24xx
 *! 09-Feb-2004 vp  Updated to support IVA.
 *! 07-Apr-2003 map	    Eliminated references to old DLDR
 *! 26-Mar-2003 vp  Commented the call to DSP deep sleep in Node_Delete
 *!		    function.
 *! 18-Feb-2003 vp  Code review updates.
 *! 06-Feb-2003 kc      Fixed FreeStream to release streams correctly.
 *! 23-Jan-2003 map     Removed call to DISP_DoCinit within Write()
 *! 03-Jan-2003 map     Only unload code after phase has executed if
 *!		     overlay or split dynload phases
 *! 18-Oct-2002 vp  Ported to Linux platform.
 *! 06-Nov-2002 map     Fixed NODE_Run on NODE_PAUSED bug
 *! 12-Oct-2002 map     Fixed DeleteNode bug in NODE_Create
 *! 11-Sep-2002 rr      DeleteNode frees the memory for strmConnect and dcd obj
 *! 29-Aug-2002 map     Modified Ovly and Write to use ARM-side copy
 *! 22-May-2002 sg      Changed use of cbData for PWR calls.
 *! 17-May-2002 jeh     Removed LoadLoaderFxns(). Get address of RMS_cinit()
 *!		     function. Call DISP_DoCinit() from Write(), if .cinit.
 *! 13-May-2002 sg      Added timeout to wake/sleep calls.
 *! 02-May-2002 sg      Added wake/sleep of DSP to support "nap" mode.
 *! 18-Apr-2002 jeh     Use dynamic loader if compile flag is set.
 *! 13-Feb-2002 jeh     Get uSysStackSize from DSP_NDBPROPS.
 *! 07-Jan-2002 ag      STRMMODE_ZEROCOPY(shared memory buffer swap) enabled.
 *! 17-Dec-2001 ag      STRMMODE_RDMA(DDMA) enabled.
 *! 12-Dec-2001 ag      Check for valid stream mode in NODE_Connect().
 *! 04-Dec-2001 jeh     Check for node sufficiently connected in NODE_Create().
 *! 15-Nov-2001 jeh     Removed DBC_Require(pNode->hXlator != NULL) from
 *!		     NODE_AllocMsgBuf(), and check node type != NODE_DEVICE.
 *! 11-Sep-2001 ag      Zero-copy messaging support.
 *! 28-Aug-2001 jeh     Overlay/dynamic loader infrastructure added. Removed
 *!		     NODE_GetDispatcher, excess node states.
 *! 07-Aug-2001 jeh     Removed critical section for dispatcher.
 *! 26-Jul-2001 jeh     Get ZL dll name through CFG.
 *! 05-Jun-2001 jeh     Assume DSP_STRMATTRS.uBufsize in GPP bytes.
 *! 11-May-2001 jeh     Some code review cleanup.
 *! 13-Feb-2001 kc:     DSP/BIOS Bridge name updates.
 *! 15-Dec-2000 sg      Convert IALG_Fxn address from byte addr to word addr.
 *! 04-Dec-2000 jeh     Call MSG Get and Put functions.
 *! 04-Dec-2000 ag      Added SM support for node messaging.
 *! 10-Nov-2000 rr:     NODE_MIN/MAX Priority is defined in dspdefs.h.
 *! 27-Oct-2000 jeh     Added NODE_AllocMsgBuf(), NODE_FreeMsgBuf().
 *! 11-Oct-2000 jeh     Changed NODE_EnumNodeInfo to NODE_EnumNodes. Added
 *!		     NODE_CloseOrphans(). Remove NODE_RegisterNotifyAllNodes
 *! 19-Jun-2000 jeh     Created.
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
#include <dspbridge/cfg.h>
#include <dspbridge/csl.h>
#include <dspbridge/list.h>
#include <dspbridge/mem.h>
#include <dspbridge/proc.h>
#include <dspbridge/strm.h>
#include <dspbridge/sync.h>
#include <dspbridge/ntfy.h>

/*  ----------------------------------- Platform Manager */
#include <dspbridge/cmm.h>
#include <dspbridge/cod.h>
#include <dspbridge/dev.h>
#include <dspbridge/msg.h>

/*  ----------------------------------- Resource Manager */
#include <dspbridge/dbdcd.h>
#include <dspbridge/disp.h>
#include <dspbridge/rms_sh.h>

/*  ----------------------------------- Link Driver */
#include <dspbridge/wmd.h>
#include <dspbridge/wmdioctl.h>

/*  ----------------------------------- Others */
#include <dspbridge/gb.h>
#ifdef DEBUG
#include <dspbridge/uuidutil.h>
#include <dspbridge/dbg.h>
#endif

/*  ----------------------------------- This */
#include <dspbridge/nodepriv.h>
#include <dspbridge/node.h>

/* Static/Dynamic Loader includes */
#include <dspbridge/dbll.h>
#include <dspbridge/nldr.h>

#ifndef RES_CLEANUP_DISABLE
#include <dspbridge/drv.h>
#include <dspbridge/drvdefs.h>
#include <dspbridge/dbreg.h>
#include <dspbridge/resourcecleanup.h>
#endif


#define NODE_SIGNATURE      0x45444f4e	/* "EDON" */
#define NODEMGR_SIGNATURE   0x52474d4e	/* "RGMN" */

#define HOSTPREFIX	  "/host"
#define PIPEPREFIX	  "/dbpipe"

#define MaxInputs(h)  ((h)->dcdProps.objData.nodeObj.ndbProps.uNumInputStreams)
#define MaxOutputs(h) ((h)->dcdProps.objData.nodeObj.ndbProps.uNumOutputStreams)

#define NODE_GetPriority(h) ((h)->nPriority)
#define NODE_SetPriority(hNode, nPriority) ((hNode)->nPriority = nPriority)
#define NODE_SetState(hNode, state) ((hNode)->nState = state)

#define MAXPIPES	100	/* Max # of /pipe connections (CSL limit) */
#define MAXDEVSUFFIXLEN 2	/* Max(Log base 10 of MAXPIPES, MAXSTREAMS) */

#define PIPENAMELEN     (sizeof(PIPEPREFIX) + MAXDEVSUFFIXLEN)
#define HOSTNAMELEN     (sizeof(HOSTPREFIX) + MAXDEVSUFFIXLEN)

#define MAXDEVNAMELEN	32	/* DSP_NDBPROPS.acName size */
#define CREATEPHASE	1
#define EXECUTEPHASE	2
#define DELETEPHASE	3

/* Define default STRM parameters */
/*
 *  TBD: Put in header file, make global DSP_STRMATTRS with defaults,
 *  or make defaults configurable.
 */
#define DEFAULTBUFSIZE		32
#define DEFAULTNBUFS		2
#define DEFAULTSEGID		0
#define DEFAULTALIGNMENT	0
#define DEFAULTTIMEOUT		10000

#define RMSQUERYSERVER		0
#define RMSCONFIGURESERVER	1
#define RMSCREATENODE		2
#define RMSEXECUTENODE		3
#define RMSDELETENODE		4
#define RMSCHANGENODEPRIORITY	5
#define RMSREADMEMORY		6
#define RMSWRITEMEMORY		7
#define RMSCOPY			8
#define MAXTIMEOUT		2000

#define NUMRMSFXNS		9

#define PWR_TIMEOUT		500	/* default PWR timeout in msec */

#define STACKSEGLABEL "L1DSRAM_HEAP"  /* Label for DSP Stack Segment Address */

/*
 *  ======== NODE_MGR ========
 */
struct NODE_MGR {
	u32 dwSignature;	/* For object validation */
	struct DEV_OBJECT *hDevObject;	/* Device object */
	struct WMD_DRV_INTERFACE *pIntfFxns;	/* Function interface to WMD */
	struct DCD_MANAGER *hDcdMgr;	/* Proc/Node data manager */
	struct DISP_OBJECT *hDisp;	/* Node dispatcher */
	struct LST_LIST *nodeList;	/* List of all allocated nodes */
	u32 uNumNodes;		/* Number of nodes in nodeList */
	u32 uNumCreated;	/* Number of nodes *created* on DSP */
	struct GB_TMap *pipeMap;		/* Pipe connection bit map */
	struct GB_TMap *pipeDoneMap;	/* Pipes that are half free */
	struct GB_TMap *chnlMap;		/* Channel allocation bit map */
	struct GB_TMap *dmaChnlMap;	/* DMA Channel allocation bit map */
	struct GB_TMap *zChnlMap;	/* Zero-Copy Channel alloc bit map */
	struct NTFY_OBJECT *hNtfy;	/* Manages registered notifications */
	struct SYNC_CSOBJECT *hSync;	/* For critical sections */
	u32 ulFxnAddrs[NUMRMSFXNS];	/* RMS function addresses */
	struct MSG_MGR *hMsg;

	/* Processor properties needed by Node Dispatcher */
	u32 ulNumChnls;	/* Total number of channels */
	u32 ulChnlOffset;	/* Offset of chnl ids rsvd for RMS */
	u32 ulChnlBufSize;	/* Buffer size for data to RMS */
	DSP_PROCFAMILY procFamily;	/* eg, 5000 */
	DSP_PROCTYPE procType;	/* eg, 5510 */
	u32 uDSPWordSize;	/* Size of DSP word on host bytes */
	u32 uDSPDataMauSize;	/* Size of DSP data MAU */
	u32 uDSPMauSize;	/* Size of MAU */
	s32 nMinPri;		/* Minimum runtime priority for node */
	s32 nMaxPri;		/* Maximum runtime priority for node */

	struct STRM_MGR *hStrmMgr;	/* STRM manager */

	/* Loader properties */
	struct NLDR_OBJECT *hNldr;	/* Handle to loader */
	struct NLDR_FXNS nldrFxns;	/* Handle to loader functions */
	bool fLoaderInit;	/* Loader Init function succeeded? */
};

/*
 *  ======== CONNECTTYPE ========
 */
enum CONNECTTYPE {
	NOTCONNECTED = 0,
	NODECONNECT,
	HOSTCONNECT,
	DEVICECONNECT,
} ;

/*
 *  ======== STREAM ========
 */
struct STREAM {
	enum CONNECTTYPE type;	/* Type of stream connection */
	u32 devId;		/* pipe or channel id */
};

/*
 *  ======== NODE_OBJECT ========
 */
struct NODE_OBJECT {
	struct LST_ELEM listElem;
	u32 dwSignature;	/* For object validation */
	struct NODE_MGR *hNodeMgr;	/* The manager of this node */
	struct PROC_OBJECT *hProcessor;	/* Back pointer to processor */
	struct DSP_UUID nodeId;	/* Node's ID */
	s32 nPriority;		/* Node's current priority */
	u32 uTimeout;		/* Timeout for blocking NODE calls */
	u32 uHeapSize;		/* Heap Size */
	u32 uDSPHeapVirtAddr;	/* Heap Size */
	u32 uGPPHeapVirtAddr;	/* Heap Size */
	enum NODE_TYPE nType;	/* Type of node: message, task, etc */
	enum NODE_STATE nState;	/* NODE_ALLOCATED, NODE_CREATED, ... */
	u32 uNumInputs;	/* Current number of inputs */
	u32 uNumOutputs;	/* Current number of outputs */
	u32 uMaxInputIndex;	/* Current max input stream index */
	u32 uMaxOutputIndex;	/* Current max output stream index */
	struct STREAM *inputs;		/* Node's input streams */
	struct STREAM *outputs;	/* Node's output streams */
	struct NODE_CREATEARGS createArgs;  /* Args for node create function */
	NODE_ENV nodeEnv;	/* Environment returned by RMS */
	struct DCD_GENERICOBJ dcdProps;	/* Node properties from DCD */
	struct DSP_CBDATA *pArgs;	/* Optional args to pass to node */
	struct NTFY_OBJECT *hNtfy;	/* Manages registered notifications */
	char *pstrDevName;	/* device name, if device node */
	struct SYNC_OBJECT *hSyncDone;	/* Synchronize NODE_Terminate */
	s32 nExitStatus;	/* execute function return status */

	/* Information needed for NODE_GetAttr() */
	DSP_HNODE hDeviceOwner;	/* If dev node, task that owns it */
	u32 uNumGPPInputs;	/* Current # of from GPP streams */
	u32 uNumGPPOutputs;	/* Current # of to GPP streams */
	/* Current stream connections */
	struct DSP_STREAMCONNECT *streamConnect;

	/* Message queue */
	struct MSG_QUEUE *hMsgQueue;

	/* These fields used for SM messaging */
	struct CMM_XLATOROBJECT *hXlator;   /* Node's SM address translator */

	/* Handle to pass to dynamic loader */
	struct NLDR_NODEOBJECT *hNldrNode;
	bool fLoaded;		/* Code is (dynamically) loaded */
	bool fPhaseSplit;	/* Phases split in many libs or ovly */

} ;

/* Default buffer attributes */
static struct DSP_BUFFERATTR NODE_DFLTBUFATTRS = {
	0, 			/* cbStruct */
	1, 			/* uSegment */
	0, 			/* uAlignment */
};

static void DeleteNode(struct NODE_OBJECT *hNode,
		struct PROCESS_CONTEXT *pr_ctxt);
static void DeleteNodeMgr(struct NODE_MGR *hNodeMgr);
static void FillStreamConnect(struct NODE_OBJECT *hNode1,
			     struct NODE_OBJECT *hNode2, u32 uStream1,
			     u32 uStream2);
static void FillStreamDef(struct NODE_OBJECT *hNode,
			struct NODE_STRMDEF *pstrmDef,
			struct DSP_STRMATTR *pAttrs);
static void FreeStream(struct NODE_MGR *hNodeMgr, struct STREAM stream);
static DSP_STATUS GetFxnAddress(struct NODE_OBJECT *hNode, u32 *pulFxnAddr,
				u32 uPhase);
static DSP_STATUS GetNodeProps(struct DCD_MANAGER *hDcdMgr,
			       struct NODE_OBJECT *hNode,
			       CONST struct DSP_UUID *pNodeId,
			       struct DCD_GENERICOBJ *pdcdProps);
static DSP_STATUS GetProcProps(struct NODE_MGR *hNodeMgr,
			      struct DEV_OBJECT *hDevObject);
static DSP_STATUS GetRMSFxns(struct NODE_MGR *hNodeMgr);
static u32 Ovly(void *pPrivRef, u32 ulDspRunAddr, u32 ulDspLoadAddr,
			u32 ulNumBytes, u32 nMemSpace);
static u32 Write(void *pPrivRef, u32 ulDspAddr, void *pBuf,
			u32 ulNumBytes, u32 nMemSpace);

#if GT_TRACE
static struct GT_Mask NODE_debugMask = { NULL, NULL };  /* GT trace variable */
#endif

#ifdef DSP_DMM_DEBUG
extern u32 DMM_MemMapDump(struct DMM_OBJECT *hDmmMgr);
#endif

static u32 cRefs;		/* module reference count */

/* Dynamic loader functions. */
static struct NLDR_FXNS nldrFxns = {
	NLDR_Allocate,
	NLDR_Create,
	NLDR_Delete,
	NLDR_Exit,
	NLDR_Free,
	NLDR_GetFxnAddr,
	NLDR_Init,
	NLDR_Load,
	NLDR_Unload,
};

enum NODE_STATE NODE_GetState(HANDLE hNode)
{
       struct NODE_OBJECT *pNode = (struct NODE_OBJECT *)hNode;
       if (!MEM_IsValidHandle(pNode, NODE_SIGNATURE)) {
               GT_1trace(NODE_debugMask, GT_5CLASS,
                "NODE_GetState:hNode 0x%x\n", pNode);
               return  -1;
       } else
               return pNode->nState;

}

/*
 *  ======== NODE_Allocate ========
 *  Purpose:
 *      Allocate GPP resources to manage a node on the DSP.
 */
DSP_STATUS NODE_Allocate(struct PROC_OBJECT *hProcessor,
			IN CONST struct DSP_UUID *pNodeId,
			OPTIONAL IN CONST struct DSP_CBDATA *pArgs,
			OPTIONAL IN CONST struct DSP_NODEATTRIN *pAttrIn,
			OUT struct NODE_OBJECT **phNode,
			struct PROCESS_CONTEXT *pr_ctxt)
{
	struct NODE_MGR *hNodeMgr;
	struct DEV_OBJECT *hDevObject;
	struct NODE_OBJECT *pNode = NULL;
	enum NODE_TYPE nodeType = NODE_TASK;
	struct NODE_MSGARGS *pmsgArgs;
	struct NODE_TASKARGS *ptaskArgs;
	u32 uNumStreams;
	struct WMD_DRV_INTERFACE *pIntfFxns;
	DSP_STATUS status = DSP_SOK;
	struct CMM_OBJECT *hCmmMgr = NULL; /* Shared memory manager hndl */
	u32 procId;
	char *label;
	u32 pulValue;
	u32 dynextBase;
	u32 offSet = 0;
	u32 ulStackSegAddr, ulStackSegVal;
	u32 ulGppMemBase;
	struct CFG_HOSTRES hostRes;
	u32 pMappedAddr = 0;
	u32 mapAttrs = 0x0;
	struct DSP_PROCESSORSTATE procStatus;
#ifdef DSP_DMM_DEBUG
	struct DMM_OBJECT *hDmmMgr;
	struct PROC_OBJECT *pProcObject = (struct PROC_OBJECT *)hProcessor;
#endif

#ifndef RES_CLEANUP_DISABLE
	HANDLE	     nodeRes;
#endif

	DBC_Require(cRefs > 0);
	DBC_Require(hProcessor != NULL);
	DBC_Require(phNode != NULL);
	DBC_Require(pNodeId != NULL);

	GT_5trace(NODE_debugMask, GT_ENTER, "NODE_Allocate: \thProcessor: "
		"0x%x\tpNodeId: 0x%x\tpArgs: 0x%x\tpAttrIn: "
		"0x%x\tphNode: 0x%x\n", hProcessor, pNodeId, pArgs, pAttrIn,
		phNode);

	*phNode = NULL;

	status = PROC_GetProcessorId(hProcessor, &procId);

	status = PROC_GetDevObject(hProcessor, &hDevObject);
	if (DSP_SUCCEEDED(status)) {
		status = DEV_GetNodeManager(hDevObject, &hNodeMgr);
		if (hNodeMgr == NULL)
			status = DSP_EFAIL;

	}
	if (procId != DSP_UNIT)
		goto func_cont;

	if (DSP_FAILED(status))
		goto func_cont;

	status = PROC_GetState(hProcessor, &procStatus,
			sizeof(struct DSP_PROCESSORSTATE));
	if (DSP_FAILED(status))
		goto func_end;
	/* If processor is in error state then don't attempt
	    to send the message */
	if (procStatus.iState == PROC_ERROR) {
		GT_1trace(NODE_debugMask, GT_5CLASS,
			"NODE_Allocate: proc Status 0x%x\n",
			procStatus.iState);
		status = DSP_EFAIL;
		goto func_end;
	}

	/* Assuming that 0 is not a valid function address */
	if (hNodeMgr->ulFxnAddrs[0] == 0) {
		/* No RMS on target - we currently can't handle this */
		GT_0trace(NODE_debugMask, GT_5CLASS, "No RMS functions in base "
			 "image. Node allocation fails.\n");
		status = DSP_EFAIL;
	} else {
		/* Validate pAttrIn fields, if non-NULL */
		if (pAttrIn) {
			/* Check if pAttrIn->iPriority is within range */
			if (pAttrIn->iPriority < hNodeMgr->nMinPri ||
			   pAttrIn->iPriority > hNodeMgr->nMaxPri)
				status = DSP_ERANGE;
		}
	}
func_cont:
	/* Allocate node object and fill in */
	if (DSP_FAILED(status))
		goto func_cont2;

	MEM_AllocObject(pNode, struct NODE_OBJECT, NODE_SIGNATURE);
	if (pNode == NULL) {
		status = DSP_EMEMORY;
		goto func_cont1;
	}
	pNode->hNodeMgr = hNodeMgr;
	/* This critical section protects GetNodeProps */
	status = SYNC_EnterCS(hNodeMgr->hSync);
	if (procId != DSP_UNIT)
		goto func_cont3;

	/* Get DSP_NDBPROPS from node database */
	status = GetNodeProps(hNodeMgr->hDcdMgr, pNode, pNodeId,
			     &(pNode->dcdProps));
	if (DSP_FAILED(status))
		goto func_cont3;

	pNode->nodeId = *pNodeId;
	pNode->hProcessor = hProcessor;
	pNode->nType = pNode->dcdProps.objData.nodeObj.ndbProps.uNodeType;
	pNode->uTimeout = pNode->dcdProps.objData.nodeObj.ndbProps.uTimeout;
	pNode->nPriority = pNode->dcdProps.objData.nodeObj.ndbProps.iPriority;

	/* Currently only C64 DSP builds support Node Dynamic * heaps */
	/* Allocate memory for node heap */
	pNode->createArgs.asa.taskArgs.uHeapSize = 0;
	pNode->createArgs.asa.taskArgs.uDSPHeapAddr = 0;
	pNode->createArgs.asa.taskArgs.uDSPHeapResAddr = 0;
	pNode->createArgs.asa.taskArgs.uGPPHeapAddr = 0;
	if (!pAttrIn)
		goto func_cont3;

	/* Check if we have a user allocated node heap */
	if (!(pAttrIn->pGPPVirtAddr))
		goto func_cont3;

	/* check for page aligned Heap size */
	if (((pAttrIn->uHeapSize) & (PG_SIZE_4K - 1))) {
		GT_1trace(NODE_debugMask, GT_7CLASS,
			 "NODE_Allocate: node heap page size"
			 " not aligned to 4K page, size=0x%x \n",
			 pAttrIn->uHeapSize);
		status = DSP_EINVALIDARG;
	} else {
		pNode->createArgs.asa.taskArgs.uHeapSize = pAttrIn->uHeapSize;
		pNode->createArgs.asa.taskArgs.uGPPHeapAddr =
						 (u32)pAttrIn->pGPPVirtAddr;
	}
	if (DSP_FAILED(status))
		goto func_cont3;

	status = PROC_ReserveMemory(hProcessor,
			pNode->createArgs.asa.taskArgs.uHeapSize + PAGE_SIZE,
			(void **)&(pNode->createArgs.asa.taskArgs.
				uDSPHeapResAddr));
	if (DSP_FAILED(status)) {
		GT_1trace(NODE_debugMask, GT_5CLASS,
			 "NODE_Allocate:Failed to reserve "
			 "memory for Heap: 0x%x\n", status);
	} else {
		GT_1trace(NODE_debugMask, GT_5CLASS,
			 "NODE_Allocate: DSPProcessor_Reserve"
			 " Memory successful: 0x%x\n", status);
	}
#ifdef DSP_DMM_DEBUG
	status = DMM_GetHandle(pProcObject, &hDmmMgr);
	if (DSP_SUCCEEDED(status))
		DMM_MemMapDump(hDmmMgr);
#endif
	if (DSP_FAILED(status))
		goto func_cont3;

	mapAttrs |= DSP_MAPLITTLEENDIAN;
	mapAttrs |= DSP_MAPELEMSIZE32;
	mapAttrs |= DSP_MAPVIRTUALADDR;
	status = PROC_Map(hProcessor, (void *)pAttrIn->pGPPVirtAddr,
			pNode->createArgs.asa.taskArgs.uHeapSize,
			(void *)pNode->createArgs.asa.taskArgs.uDSPHeapResAddr,
			(void **)&pMappedAddr, mapAttrs, pr_ctxt);
	if (DSP_FAILED(status)) {
		GT_1trace(NODE_debugMask, GT_5CLASS,
			 "NODE_Allocate: Failed to map memory"
			 " for Heap: 0x%x\n", status);
	} else {
		pNode->createArgs.asa.taskArgs.uDSPHeapAddr =
			(u32) pMappedAddr;
		GT_1trace(NODE_debugMask, GT_5CLASS,
			 "NODE_Allocate:DSPProcessor_Map"
			 " successful: 0x%x\n", status);
	}

func_cont3:
	(void)SYNC_LeaveCS(hNodeMgr->hSync);
func_cont1:
	if (pAttrIn != NULL) {
		/* Overrides of NBD properties */
		pNode->uTimeout = pAttrIn->uTimeout;
		pNode->nPriority = pAttrIn->iPriority;
	}
func_cont2:
	/* Create object to manage notifications */
	if (DSP_SUCCEEDED(status))
		status = NTFY_Create(&pNode->hNtfy);

	if (DSP_SUCCEEDED(status)) {
		nodeType = NODE_GetType(pNode);
		 /*  Allocate DSP_STREAMCONNECT array for device, task, and
		 *  dais socket nodes.  */
		if (nodeType != NODE_MESSAGE) {
			uNumStreams = MaxInputs(pNode) + MaxOutputs(pNode);
			pNode->streamConnect = MEM_Calloc(uNumStreams *
					       sizeof(struct DSP_STREAMCONNECT),
					       MEM_PAGED);
			if (uNumStreams > 0 && pNode->streamConnect == NULL)
				status = DSP_EMEMORY;

		}
		if (DSP_SUCCEEDED(status) && (nodeType == NODE_TASK ||
		   nodeType == NODE_DAISSOCKET)) {
			/* Allocate arrays for maintainig stream connections */
			pNode->inputs =
				MEM_Calloc(MaxInputs(pNode) *
					sizeof(struct STREAM), MEM_PAGED);
			pNode->outputs =
				MEM_Calloc(MaxOutputs(pNode) *
					sizeof(struct STREAM), MEM_PAGED);
			ptaskArgs = &(pNode->createArgs.asa.taskArgs);
			ptaskArgs->strmInDef =
				MEM_Calloc(MaxInputs(pNode) *
					sizeof(struct NODE_STRMDEF),
					MEM_PAGED);
			ptaskArgs->strmOutDef =
					MEM_Calloc(MaxOutputs(pNode) *
						sizeof(struct NODE_STRMDEF),
						MEM_PAGED);
			if ((MaxInputs(pNode) > 0 && (pNode->inputs == NULL ||
			   ptaskArgs->strmInDef == NULL)) ||
			   (MaxOutputs(pNode) > 0 && (pNode->outputs == NULL ||
			   ptaskArgs->strmOutDef == NULL)))
				status = DSP_EMEMORY;
		}
	}
	if (DSP_SUCCEEDED(status) && (nodeType != NODE_DEVICE)) {
		/* Create an event that will be posted when RMS_EXIT is
		 * received. */
		status = SYNC_OpenEvent(&pNode->hSyncDone, NULL);
		if (DSP_SUCCEEDED(status)) {
			/*Get the shared mem mgr for this nodes dev object */
			status = CMM_GetHandle(hProcessor, &hCmmMgr);
			if (DSP_FAILED(status)) {
				GT_1trace(NODE_debugMask, GT_5CLASS,
					 "NODE_Allocate: Failed to"
					 " get CMM Mgr handle: 0x%x\n", status);
			} else {
				/* Allocate a SM addr translator for this node
				 * w/ deflt attr */
				status = CMM_XlatorCreate(&pNode->hXlator,
					 hCmmMgr, NULL);
				if (DSP_FAILED(status)) {
					GT_1trace(NODE_debugMask, GT_5CLASS,
					    "NODE_Allocate: Failed"
					    " to create SM translator: 0x%x\n",
					    status);
				}
			}
		}
		if (DSP_SUCCEEDED(status)) {
			/* Fill in message args */
			if ((pArgs != NULL) && (pArgs->cbData > 0)) {
				pmsgArgs = &(pNode->createArgs.asa.msgArgs);
				pmsgArgs->pData = MEM_Calloc(pArgs->cbData,
							    MEM_PAGED);
				if (pmsgArgs->pData == NULL) {
					status = DSP_EMEMORY;
				} else {
					pmsgArgs->uArgLength = pArgs->cbData;
					memcpy(pmsgArgs->pData, pArgs->cData,
					      pArgs->cbData);
				}
			}
		}
	}

	if (DSP_SUCCEEDED(status) && nodeType != NODE_DEVICE) {
		/* Create a message queue for this node */
		pIntfFxns = hNodeMgr->pIntfFxns;
		status = (*pIntfFxns->pfnMsgCreateQueue)(hNodeMgr->hMsg,
				&pNode->hMsgQueue, 0,
				    pNode->createArgs.asa.msgArgs.uMaxMessages,
				    pNode);
	}

	if (DSP_SUCCEEDED(status)) {
		/* Create object for dynamic loading */

		status = hNodeMgr->nldrFxns.pfnAllocate(hNodeMgr->hNldr,
					     (void *) pNode,
					     &pNode->dcdProps.objData.nodeObj,
					     &pNode->hNldrNode,
					     &pNode->fPhaseSplit);
		if (DSP_FAILED(status)) {
			GT_1trace(NODE_debugMask, GT_5CLASS,
				 "NODE_Allocate: Failed to "
				 "allocate NLDR node: 0x%x\n", status);
		}
	}

	/* Comapare value read from Node Properties and check if it is same as
	 * STACKSEGLABEL, if yes read the Address of STACKSEGLABEL, calculate
	 * GPP Address, Read the value in that address and override the
	 * uStackSeg value in task args */
	if (DSP_SUCCEEDED(status) &&
	   (char *)pNode->dcdProps.objData.nodeObj.ndbProps.uStackSegName !=
	   NULL) {
		label = MEM_Calloc(sizeof(STACKSEGLABEL)+1, MEM_PAGED);
               strncpy(label, STACKSEGLABEL, sizeof(STACKSEGLABEL)+1);

               if (strcmp((char *)pNode->dcdProps.objData.nodeObj.
				     ndbProps.uStackSegName, label) == 0) {
			status = hNodeMgr->nldrFxns.pfnGetFxnAddr(pNode->
				 hNldrNode, "DYNEXT_BEG", &dynextBase);
			if (DSP_FAILED(status)) {
				GT_1trace(NODE_debugMask, GT_5CLASS,
				"NODE_Allocate: Failed to get Address for "
				"DYNEXT_BEG: 0x%x\n", status);
			}

			status = hNodeMgr->nldrFxns.pfnGetFxnAddr(pNode->
				 hNldrNode, "L1DSRAM_HEAP", &pulValue);

			if (DSP_FAILED(status)) {
				GT_1trace(NODE_debugMask, GT_5CLASS,
				"NODE_Allocate: Failed to get Address for "
				"L1DSRAM_HEAP: 0x%x\n", status);
			}

			status = CFG_GetHostResources((struct CFG_DEVNODE *)
				 DRV_GetFirstDevExtension(), &hostRes);

			if (DSP_FAILED(status)) {
				GT_1trace(NODE_debugMask, GT_5CLASS,
				"NODE_Allocate: Failed to get host resource "
				"0x%x\n", status);
			}

			ulGppMemBase = (u32)hostRes.dwMemBase[1];
			offSet = pulValue - dynextBase;
			ulStackSegAddr = ulGppMemBase + offSet;
			ulStackSegVal = (u32)*((REG_UWORD32 *)
					((u32)(ulStackSegAddr)));

			GT_1trace(NODE_debugMask, GT_5CLASS,
				 "StackSegVal =0x%x\n", ulStackSegVal);
			GT_1trace(NODE_debugMask, GT_5CLASS,
				 "ulStackSegAddr = 0x%x\n", ulStackSegAddr);

			pNode->createArgs.asa.taskArgs.uStackSeg =
				ulStackSegVal;

		}

		if (label)
			MEM_Free(label);

	}


	if (DSP_SUCCEEDED(status)) {
		/* Add the node to the node manager's list of allocated
		 * nodes. */
		LST_InitElem((struct LST_ELEM *)pNode);
		NODE_SetState(pNode, NODE_ALLOCATED);

		status = SYNC_EnterCS(hNodeMgr->hSync);

		if (DSP_SUCCEEDED(status)) {
			LST_PutTail(hNodeMgr->nodeList,
			(struct LST_ELEM *) pNode);
			++(hNodeMgr->uNumNodes);
		}

		/* Exit critical section */
		(void) SYNC_LeaveCS(hNodeMgr->hSync);

		/* Preset this to assume phases are split
		 * (for overlay and dll) */
		pNode->fPhaseSplit = true;

		if (DSP_SUCCEEDED(status))
			*phNode = pNode;


		/* Notify all clients registered for DSP_NODESTATECHANGE. */
		PROC_NotifyAllClients(hProcessor, DSP_NODESTATECHANGE);
	} else {
		/* Cleanup */
		if (pNode)
			DeleteNode(pNode, pr_ctxt);

	}

#ifndef RES_CLEANUP_DISABLE
	if (DSP_SUCCEEDED(status)) {
		DRV_InsertNodeResElement(*phNode, &nodeRes, pr_ctxt);
		DRV_ProcNodeUpdateHeapStatus(nodeRes, true);
		DRV_ProcNodeUpdateStatus(nodeRes, true);
	}
#endif
	DBC_Ensure((DSP_FAILED(status) && (*phNode == NULL)) ||
		  (DSP_SUCCEEDED(status)
		    && MEM_IsValidHandle((*phNode), NODE_SIGNATURE)));
func_end:
	return status;
}

/*
 *  ======== NODE_AllocMsgBuf ========
 *  Purpose:
 *      Allocates buffer for zero copy messaging.
 */
DBAPI NODE_AllocMsgBuf(struct NODE_OBJECT *hNode, u32 uSize,
		       OPTIONAL IN OUT struct DSP_BUFFERATTR *pAttr,
		       OUT u8 **pBuffer)
{
	struct NODE_OBJECT *pNode = (struct NODE_OBJECT *)hNode;
	DSP_STATUS status = DSP_SOK;
	bool bVirtAddr = false;
	bool bSetInfo;
	u32 procId;

	DBC_Require(cRefs > 0);
	DBC_Require(pBuffer != NULL);

	DBC_Require(uSize > 0);

	GT_4trace(NODE_debugMask, GT_ENTER,
		 "NODE_AllocMsgBuf: hNode: 0x%x\tuSize:"
		 " 0x%x\tpAttr: 0x%x\tpBuffer: %d\n", pNode, uSize, pAttr,
		 pBuffer);

	if (!MEM_IsValidHandle(pNode, NODE_SIGNATURE))
		status = DSP_EHANDLE;
	else if (NODE_GetType(pNode) == NODE_DEVICE)
		status = DSP_ENODETYPE;

	if (DSP_FAILED(status))
		goto func_end;

	if (pAttr == NULL)
		pAttr = &NODE_DFLTBUFATTRS;	/* set defaults */

	status = PROC_GetProcessorId(pNode->hProcessor, &procId);
	if (procId != DSP_UNIT) {
		DBC_Assert(NULL);
		goto func_end;
	}
	 /*  If segment ID includes MEM_SETVIRTUALSEGID then pBuffer is a
	 *  virt  address, so set this info in this node's translator
	 *  object for  future ref. If MEM_GETVIRTUALSEGID then retrieve
	 *  virtual address  from node's translator.  */
	if ((pAttr->uSegment & MEM_SETVIRTUALSEGID) ||
			    (pAttr->uSegment & MEM_GETVIRTUALSEGID)) {
		bVirtAddr = true;
		bSetInfo = (pAttr->uSegment & MEM_SETVIRTUALSEGID) ?
			   true : false;
		pAttr->uSegment &= ~MEM_MASKVIRTUALSEGID; /* clear mask bits */
		/* Set/get this node's translators virtual address base/size */
		status = CMM_XlatorInfo(pNode->hXlator, pBuffer, uSize,
					pAttr->uSegment, bSetInfo);
		if (DSP_FAILED(status)) {
			GT_1trace(NODE_debugMask, GT_7CLASS,
				 "NODE_AllocMsgBuf "
				 "failed: 0x%lx\n", status);
		}
	}
	if (DSP_SUCCEEDED(status) && (!bVirtAddr)) {
		if (pAttr->uSegment != 1) {
			/* Node supports single SM segment only. */
			status = DSP_EBADSEGID;
		}
		 /*  Arbitrary SM buffer alignment not supported for host side
		  *  allocs, but guaranteed for the following alignment
		  *  values.  */
		switch (pAttr->uAlignment) {
		case 0:
		case 1:
		case 2:
		case 4:
			break;
		default:
			/* alignment value not suportted */
			status = DSP_EALIGNMENT;
			break;
		}
		if (DSP_SUCCEEDED(status)) {
			/* allocate physical buffer from segId in node's
			 * translator */
			(void)CMM_XlatorAllocBuf(pNode->hXlator, pBuffer,
						 uSize);
			if (*pBuffer == NULL) {
				GT_0trace(NODE_debugMask, GT_7CLASS,
					 "NODE_AllocMsgBuf: "
					 "ERROR: Out of shared memory.\n");
				status = DSP_EMEMORY;
			}
		}
	}
func_end:
	return status;
}

/*
 *  ======== NODE_ChangePriority ========
 *  Purpose:
 *      Change the priority of a node in the allocated state, or that is
 *      currently running or paused on the target.
 */
DSP_STATUS NODE_ChangePriority(struct NODE_OBJECT *hNode, s32 nPriority)
{
	struct NODE_OBJECT *pNode = (struct NODE_OBJECT *)hNode;
	struct NODE_MGR *hNodeMgr = NULL;
	enum NODE_TYPE nodeType;
	enum NODE_STATE state;
	DSP_STATUS status = DSP_SOK;
	u32 procId;

	DBC_Require(cRefs > 0);

	GT_2trace(NODE_debugMask, GT_ENTER, "NODE_ChangePriority: "
		 "hNode: 0x%x\tnPriority: %d\n", hNode, nPriority);

	if (!MEM_IsValidHandle(hNode, NODE_SIGNATURE)) {
		GT_1trace(NODE_debugMask, GT_7CLASS,
			 "Invalid NODE Handle: 0x%x\n", hNode);
		status = DSP_EHANDLE;
	} else {
		hNodeMgr = hNode->hNodeMgr;
		nodeType = NODE_GetType(hNode);
		if (nodeType != NODE_TASK && nodeType != NODE_DAISSOCKET)
			status = DSP_ENODETYPE;
		else if (nPriority < hNodeMgr->nMinPri ||
				nPriority > hNodeMgr->nMaxPri)
				status = DSP_ERANGE;
	}
	if (DSP_FAILED(status))
		goto func_end;

	/* Enter critical section */
	status = SYNC_EnterCS(hNodeMgr->hSync);
	if (DSP_FAILED(status))
		goto func_cont;

	state = NODE_GetState(hNode);
	if (state == NODE_ALLOCATED || state == NODE_PAUSED) {
		NODE_SetPriority(hNode, nPriority);
	} else {
		if (state != NODE_RUNNING) {
			status = DSP_EWRONGSTATE;
			goto func_cont;
		}
		if (DSP_SUCCEEDED(status)) {
			status = PROC_GetProcessorId(pNode->hProcessor,
						    &procId);
			if (procId == DSP_UNIT) {
				status = DISP_NodeChangePriority(hNodeMgr->
				    hDisp, hNode,
				    hNodeMgr->ulFxnAddrs[RMSCHANGENODEPRIORITY],
				    hNode->nodeEnv, nPriority);
			}
			if (DSP_SUCCEEDED(status))
				NODE_SetPriority(hNode, nPriority);

		}
	}
func_cont:
		/* Leave critical section */
		(void)SYNC_LeaveCS(hNodeMgr->hSync);
func_end:
	return status;
}

/*
 *  ======== NODE_Connect ========
 *  Purpose:
 *      Connect two nodes on the DSP, or a node on the DSP to the GPP.
 */
DSP_STATUS NODE_Connect(struct NODE_OBJECT *hNode1, u32 uStream1,
			struct NODE_OBJECT *hNode2,
			u32 uStream2, OPTIONAL IN struct DSP_STRMATTR *pAttrs,
			OPTIONAL IN struct DSP_CBDATA *pConnParam)
{
	struct NODE_MGR *hNodeMgr;
	char *pstrDevName = NULL;
	enum NODE_TYPE node1Type = NODE_TASK;
	enum NODE_TYPE node2Type = NODE_TASK;
	struct NODE_STRMDEF *pstrmDef;
	struct NODE_STRMDEF *pInput = NULL;
	struct NODE_STRMDEF *pOutput = NULL;
	struct NODE_OBJECT *hDevNode;
	struct NODE_OBJECT *hNode;
	struct STREAM *pStream;
	GB_BitNum pipeId = GB_NOBITS;
	GB_BitNum chnlId = GB_NOBITS;
	CHNL_MODE uMode;
	u32 dwLength;
	DSP_STATUS status = DSP_SOK;
	DBC_Require(cRefs > 0);
	GT_5trace(NODE_debugMask, GT_ENTER,
		 "NODE_Connect: hNode1: 0x%x\tuStream1:"
		 " %d\thNode2: 0x%x\tuStream2: %d\tpAttrs: 0x%x\n", hNode1,
		 uStream1, hNode2, uStream2, pAttrs);
	if (DSP_SUCCEEDED(status)) {
		if ((hNode1 != (struct NODE_OBJECT *) DSP_HGPPNODE &&
		   !MEM_IsValidHandle(hNode1, NODE_SIGNATURE)) ||
		   (hNode2 != (struct NODE_OBJECT *) DSP_HGPPNODE &&
		   !MEM_IsValidHandle(hNode2, NODE_SIGNATURE)))
			status = DSP_EHANDLE;
	}
	if (DSP_SUCCEEDED(status)) {
		/* The two nodes must be on the same processor */
		if (hNode1 != (struct NODE_OBJECT *)DSP_HGPPNODE &&
		   hNode2 != (struct NODE_OBJECT *)DSP_HGPPNODE &&
		   hNode1->hNodeMgr != hNode2->hNodeMgr)
			status = DSP_EFAIL;
		/* Cannot connect a node to itself */
		if (hNode1 == hNode2)
			status = DSP_EFAIL;

	}
	if (DSP_SUCCEEDED(status)) {
		/* NODE_GetType() will return NODE_GPP if hNode =
		 * DSP_HGPPNODE. */
		node1Type = NODE_GetType(hNode1);
		node2Type = NODE_GetType(hNode2);
	/* Check stream indices ranges */
		if ((node1Type != NODE_GPP && node1Type != NODE_DEVICE &&
		   uStream1 >= MaxOutputs(hNode1)) || (node2Type != NODE_GPP &&
		   node2Type != NODE_DEVICE && uStream2 >= MaxInputs(hNode2)))
			status = DSP_EVALUE;
	}
	if (DSP_SUCCEEDED(status)) {
		/*
		 *  Only the following types of connections are allowed:
		 *      task/dais socket < == > task/dais socket
		 *      task/dais socket < == > device
		 *      task/dais socket < == > GPP
		 *
		 *  ie, no message nodes, and at least one task or dais
		 *  socket node.
		 */
		if (node1Type == NODE_MESSAGE || node2Type == NODE_MESSAGE ||
		    (node1Type != NODE_TASK && node1Type != NODE_DAISSOCKET &&
		     node2Type != NODE_TASK && node2Type != NODE_DAISSOCKET))
			status = DSP_EFAIL;
	}
	/*
	 * Check stream mode. Default is STRMMODE_PROCCOPY.
	 */
	if (DSP_SUCCEEDED(status) && pAttrs) {
		if (pAttrs->lMode != STRMMODE_PROCCOPY)
			status = DSP_ESTRMMODE;	/* illegal stream mode */

	}
	if (DSP_FAILED(status))
		goto func_end;

	if (node1Type != NODE_GPP) {
		hNodeMgr = hNode1->hNodeMgr;
	} else {
		DBC_Assert(hNode2 != (struct NODE_OBJECT *)DSP_HGPPNODE);
		hNodeMgr = hNode2->hNodeMgr;
	}
	/* Enter critical section */
	status = SYNC_EnterCS(hNodeMgr->hSync);
	if (DSP_FAILED(status))
		goto func_cont;

	/* Nodes must be in the allocated state */
	if (node1Type != NODE_GPP && NODE_GetState(hNode1) != NODE_ALLOCATED)
		status = DSP_EWRONGSTATE;

	if (node2Type != NODE_GPP && NODE_GetState(hNode2) != NODE_ALLOCATED)
		status = DSP_EWRONGSTATE;

	if (DSP_SUCCEEDED(status)) {
		/*  Check that stream indices for task and dais socket nodes
		 *  are not already be used. (Device nodes checked later) */
		if (node1Type == NODE_TASK || node1Type == NODE_DAISSOCKET) {
			pOutput = &(hNode1->createArgs.asa.taskArgs.
				  strmOutDef[uStream1]);
			if (pOutput->szDevice != NULL)
				status = DSP_EALREADYCONNECTED;

		}
		if (node2Type == NODE_TASK || node2Type == NODE_DAISSOCKET) {
			pInput = &(hNode2->createArgs.asa.taskArgs.
				 strmInDef[uStream2]);
			if (pInput->szDevice != NULL)
				status = DSP_EALREADYCONNECTED;

		}
	}
	/* Connecting two task nodes? */
	if (DSP_SUCCEEDED(status) && ((node1Type == NODE_TASK ||
	   node1Type == NODE_DAISSOCKET) && (node2Type == NODE_TASK ||
	   node2Type == NODE_DAISSOCKET))) {
		/* Find available pipe */
		pipeId = GB_findandset(hNodeMgr->pipeMap);
		if (pipeId == GB_NOBITS) {
			status = DSP_ENOMORECONNECTIONS;
		} else {
			hNode1->outputs[uStream1].type = NODECONNECT;
			hNode2->inputs[uStream2].type = NODECONNECT;
			hNode1->outputs[uStream1].devId = pipeId;
			hNode2->inputs[uStream2].devId = pipeId;
			pOutput->szDevice = MEM_Calloc(PIPENAMELEN + 1,
						      MEM_PAGED);
			pInput->szDevice = MEM_Calloc(PIPENAMELEN + 1,
						     MEM_PAGED);
			if (pOutput->szDevice == NULL ||
			   pInput->szDevice == NULL) {
				/* Undo the connection */
				if (pOutput->szDevice)
					MEM_Free(pOutput->szDevice);

				if (pInput->szDevice)
					MEM_Free(pInput->szDevice);

				pOutput->szDevice = NULL;
				pInput->szDevice = NULL;
				GB_clear(hNodeMgr->pipeMap, pipeId);
				status = DSP_EMEMORY;
			} else {
				/* Copy "/dbpipe<pipId>" name to device names */
				sprintf(pOutput->szDevice, "%s%d",
						PIPEPREFIX, pipeId);
				strcpy(pInput->szDevice, pOutput->szDevice);
			}
		}
	}
	/* Connecting task node to host? */
	if (DSP_SUCCEEDED(status) && (node1Type == NODE_GPP ||
	   node2Type == NODE_GPP)) {
		if (node1Type == NODE_GPP) {
			uMode = CHNL_MODETODSP;
		} else {
			DBC_Assert(node2Type == NODE_GPP);
			uMode = CHNL_MODEFROMDSP;
		}
		 /*  Reserve a channel id. We need to put the name "/host<id>"
		 *  in the node's createArgs, but the host
		 *  side channel will not be opened until DSPStream_Open is
		 *  called for this node.  */
		if (pAttrs) {
			if (pAttrs->lMode == STRMMODE_RDMA) {
				chnlId = GB_findandset(hNodeMgr->dmaChnlMap);
				/* dma chans are 2nd transport chnl set
				 * ids(e.g. 16-31)*/
				(chnlId != GB_NOBITS) ?
				   (chnlId = chnlId + hNodeMgr->ulNumChnls) :
				   chnlId;
			} else if (pAttrs->lMode == STRMMODE_ZEROCOPY) {
				chnlId = GB_findandset(hNodeMgr->zChnlMap);
				/* zero-copy chans are 3nd transport set
				 * (e.g. 32-47) */
				(chnlId != GB_NOBITS) ?  (chnlId = chnlId +
					(2 * hNodeMgr->ulNumChnls)) : chnlId;
			} else {	/* must be PROCCOPY */
				DBC_Assert(pAttrs->lMode == STRMMODE_PROCCOPY);
				chnlId = GB_findandset(hNodeMgr->chnlMap);
				/* e.g. 0-15 */
			}
		} else {
			/* default to PROCCOPY */
			chnlId = GB_findandset(hNodeMgr->chnlMap);
		}
		if (chnlId == GB_NOBITS) {
			status = DSP_ENOMORECONNECTIONS;
			goto func_cont2;
		}
		pstrDevName = MEM_Calloc(HOSTNAMELEN + 1, MEM_PAGED);
		if (pstrDevName != NULL)
			goto func_cont2;

		if (pAttrs) {
			if (pAttrs->lMode == STRMMODE_RDMA) {
				GB_clear(hNodeMgr->dmaChnlMap, chnlId -
					 hNodeMgr->ulNumChnls);
			} else if (pAttrs->lMode == STRMMODE_ZEROCOPY) {
				GB_clear(hNodeMgr->zChnlMap, chnlId -
					(2*hNodeMgr->ulNumChnls));
			} else {
				DBC_Assert(pAttrs->lMode == STRMMODE_PROCCOPY);
				GB_clear(hNodeMgr->chnlMap, chnlId);
			}
		} else {
			GB_clear(hNodeMgr->chnlMap, chnlId);
		}
		status = DSP_EMEMORY;
func_cont2:
		if (DSP_SUCCEEDED(status)) {
			if (hNode1 == (struct NODE_OBJECT *) DSP_HGPPNODE) {
				hNode2->inputs[uStream2].type = HOSTCONNECT;
				hNode2->inputs[uStream2].devId = chnlId;
				pInput->szDevice = pstrDevName;
			} else {
				hNode1->outputs[uStream1].type = HOSTCONNECT;
				hNode1->outputs[uStream1].devId = chnlId;
				pOutput->szDevice = pstrDevName;
			}
			sprintf(pstrDevName, "%s%d", HOSTPREFIX, chnlId);
		}
	}
	/* Connecting task node to device node? */
	if (DSP_SUCCEEDED(status) && ((node1Type == NODE_DEVICE) ||
	   (node2Type == NODE_DEVICE))) {
		if (node2Type == NODE_DEVICE) {
			/* node1 == > device */
			hDevNode = hNode2;
			hNode = hNode1;
			pStream = &(hNode1->outputs[uStream1]);
			pstrmDef = pOutput;
		} else {
			/* device == > node2 */
			hDevNode = hNode1;
			hNode = hNode2;
			pStream = &(hNode2->inputs[uStream2]);
			pstrmDef = pInput;
		}
		/* Set up create args */
		pStream->type = DEVICECONNECT;
               dwLength = strlen(hDevNode->pstrDevName);
		if (pConnParam != NULL) {
			pstrmDef->szDevice = MEM_Calloc(dwLength + 1 +
						(u32) pConnParam->cbData,
						MEM_PAGED);
		} else {
			pstrmDef->szDevice = MEM_Calloc(dwLength + 1,
							MEM_PAGED);
		}
		if (pstrmDef->szDevice == NULL) {
			status = DSP_EMEMORY;
		} else {
			/* Copy device name */
                       strncpy(pstrmDef->szDevice, hDevNode->pstrDevName,
				   dwLength);
			if (pConnParam != NULL) {
                               strncat(pstrmDef->szDevice,
                                       (char *)pConnParam->cData,
                                       (u32)pConnParam->cbData);
			}
			hDevNode->hDeviceOwner = hNode;
		}
	}
	if (DSP_SUCCEEDED(status)) {
		/* Fill in create args */
		if (node1Type == NODE_TASK || node1Type == NODE_DAISSOCKET) {
			hNode1->createArgs.asa.taskArgs.uNumOutputs++;
			FillStreamDef(hNode1, pOutput, pAttrs);
		}
		if (node2Type == NODE_TASK || node2Type == NODE_DAISSOCKET) {
			hNode2->createArgs.asa.taskArgs.uNumInputs++;
			FillStreamDef(hNode2, pInput, pAttrs);
		}
		/* Update hNode1 and hNode2 streamConnect */
		if (node1Type != NODE_GPP && node1Type != NODE_DEVICE) {
			hNode1->uNumOutputs++;
			if (uStream1 > hNode1->uMaxOutputIndex)
				hNode1->uMaxOutputIndex = uStream1;

		}
		if (node2Type != NODE_GPP && node2Type != NODE_DEVICE) {
			hNode2->uNumInputs++;
			if (uStream2 > hNode2->uMaxInputIndex)
				hNode2->uMaxInputIndex = uStream2;

		}
		FillStreamConnect(hNode1, hNode2, uStream1, uStream2);
	}
func_cont:
	/* end of SYNC_EnterCS */
	/* Exit critical section */
	(void)SYNC_LeaveCS(hNodeMgr->hSync);
func_end:
	return status;
}

/*
 *  ======== NODE_Create ========
 *  Purpose:
 *      Create a node on the DSP by remotely calling the node's create function.
 */
DSP_STATUS NODE_Create(struct NODE_OBJECT *hNode)
{
	struct NODE_OBJECT *pNode = (struct NODE_OBJECT *)hNode;
	struct NODE_MGR *hNodeMgr;
	struct WMD_DRV_INTERFACE *pIntfFxns;
	u32 ulCreateFxn;
	enum NODE_TYPE nodeType;
	DSP_STATUS status = DSP_SOK;
	DSP_STATUS status1 = DSP_SOK;
	bool bJustWokeDSP = false;
	struct DSP_CBDATA cbData;
	u32 procId = 255;
	struct DSP_PROCESSORSTATE procStatus;
	struct PROC_OBJECT *hProcessor;
#if defined(CONFIG_BRIDGE_DVFS) && !defined(CONFIG_CPU_FREQ)
	struct dspbridge_platform_data *pdata =
				omap_dspbridge_dev->dev.platform_data;
#endif

	DBC_Require(cRefs > 0);
	GT_1trace(NODE_debugMask, GT_ENTER, "NODE_Create: hNode: 0x%x\n",
		 hNode);
	if (!MEM_IsValidHandle(pNode, NODE_SIGNATURE)) {
		status = DSP_EHANDLE;
		goto func_end;
	}
	hProcessor = hNode->hProcessor;
	status = PROC_GetState(hProcessor, &procStatus,
					sizeof(struct DSP_PROCESSORSTATE));
	if (DSP_FAILED(status))
		goto func_end;
	/* If processor is in error state then don't attempt to create
	    new node */
	if (procStatus.iState == PROC_ERROR) {
		GT_1trace(NODE_debugMask, GT_4CLASS, "NODE_Create:"
			"		proc Status 0x%x\n", procStatus.iState);
		status = DSP_EFAIL;
		goto func_end;
	}
	/* create struct DSP_CBDATA struct for PWR calls */
	cbData.cbData = PWR_TIMEOUT;
	nodeType = NODE_GetType(hNode);
	hNodeMgr = hNode->hNodeMgr;
	pIntfFxns = hNodeMgr->pIntfFxns;
	/* Get access to node dispatcher */
	status = SYNC_EnterCS(hNodeMgr->hSync);
	if (DSP_FAILED(status))
		goto func_cont;

	/* Check node state */
	if (NODE_GetState(hNode) != NODE_ALLOCATED)
		status = DSP_EWRONGSTATE;

	if (DSP_SUCCEEDED(status))
		status = PROC_GetProcessorId(pNode->hProcessor, &procId);

	if (DSP_FAILED(status))
		goto func_cont2;

	if (procId != DSP_UNIT)
		goto func_cont2;

	/* Make sure streams are properly connected */
	if ((hNode->uNumInputs && hNode->uMaxInputIndex >
	   hNode->uNumInputs - 1) ||
	   (hNode->uNumOutputs && hNode->uMaxOutputIndex >
	   hNode->uNumOutputs - 1))
		status = DSP_ENOTCONNECTED;

	if (DSP_SUCCEEDED(status)) {
		/* If node's create function is not loaded, load it */
		/* Boost the OPP level to max level that DSP can be requested */
#if defined(CONFIG_BRIDGE_DVFS) && !defined(CONFIG_CPU_FREQ)
		if (pdata->cpu_set_freq) {
			(*pdata->cpu_set_freq)(pdata->mpu_speed[VDD1_OPP3]);

			if (pdata->dsp_get_opp) {
				GT_1trace(NODE_debugMask, GT_4CLASS, "opp level"
				"after setting to VDD1_OPP3 is %d\n",
				(*pdata->dsp_get_opp)());
			}
		}
#endif
		status = hNodeMgr->nldrFxns.pfnLoad(hNode->hNldrNode,
						   NLDR_CREATE);
		/* Get address of node's create function */
		if (DSP_SUCCEEDED(status)) {
			hNode->fLoaded = true;
			if (nodeType != NODE_DEVICE) {
				status = GetFxnAddress(hNode, &ulCreateFxn,
							CREATEPHASE);
			}
		} else {
			GT_1trace(NODE_debugMask, GT_ENTER,
				 "NODE_Create: failed to load"
				 " create code: 0x%x\n", status);
		}
		/* Request the lowest OPP level*/
#if defined(CONFIG_BRIDGE_DVFS) && !defined(CONFIG_CPU_FREQ)
		if (pdata->cpu_set_freq) {
			(*pdata->cpu_set_freq)(pdata->mpu_speed[VDD1_OPP1]);

			if (pdata->dsp_get_opp) {
				GT_1trace(NODE_debugMask, GT_4CLASS, "opp level"
				"after setting to VDD1_OPP1 is %d\n",
				(*pdata->dsp_get_opp)());
			}
		}
#endif
		/* Get address of iAlg functions, if socket node */
		if (DSP_SUCCEEDED(status)) {
			if (nodeType == NODE_DAISSOCKET) {
				status = hNodeMgr->nldrFxns.pfnGetFxnAddr
					(hNode->hNldrNode, hNode->dcdProps.
					objData.nodeObj.pstrIAlgName,
					&hNode->createArgs.asa.taskArgs.
					ulDaisArg);
			}
		}
	}
	if (DSP_SUCCEEDED(status)) {
		if (nodeType != NODE_DEVICE) {
			status = DISP_NodeCreate(hNodeMgr->hDisp, hNode,
				 hNodeMgr->ulFxnAddrs[RMSCREATENODE],
				 ulCreateFxn, &(hNode->createArgs),
				 &(hNode->nodeEnv));
			if (DSP_SUCCEEDED(status)) {
				/* Set the message queue id to the node env
				 * pointer */
				pIntfFxns = hNodeMgr->pIntfFxns;
				(*pIntfFxns->pfnMsgSetQueueId)(hNode->hMsgQueue,
				hNode->nodeEnv);
			}
		}
	}
	/*  Phase II/Overlays: Create, execute, delete phases  possibly in
	 *  different files/sections. */
	if (hNode->fLoaded && hNode->fPhaseSplit) {
		/* If create code was dynamically loaded, we can now unload
		 * it. */
		status1 = hNodeMgr->nldrFxns.pfnUnload(hNode->hNldrNode,
						      NLDR_CREATE);
		hNode->fLoaded = false;
	}
	if (DSP_FAILED(status1)) {
		GT_1trace(NODE_debugMask, GT_5CLASS,
			 "NODE_Create: Failed to unload "
			 "create code: 0x%x\n", status1);
	}
func_cont2:
	/* Update node state and node manager state */
	if (DSP_SUCCEEDED(status)) {
		NODE_SetState(hNode, NODE_CREATED);
		hNodeMgr->uNumCreated++;
		goto func_cont;
	}
	if (status != DSP_EWRONGSTATE) {
		/* Put back in NODE_ALLOCATED state if error occurred */
		NODE_SetState(hNode, NODE_ALLOCATED);
	}
	if (procId == DSP_UNIT) {
		/* If node create failed, see if should sleep DSP now */
		if (bJustWokeDSP == true) {
			/* Check to see if partial create happened on DSP */
			if (hNode->nodeEnv == (u32)NULL) {
				/* No environment allocated on DSP, re-sleep
				 * DSP now */
				PROC_Ctrl(hNode->hProcessor, WMDIOCTL_DEEPSLEEP,
					 &cbData);
			} else {
				/* Increment count, sleep later when node fully
				 * deleted */
				hNodeMgr->uNumCreated++;
			}
		}
	}
func_cont:
		/* Free access to node dispatcher */
		(void)SYNC_LeaveCS(hNodeMgr->hSync);
func_end:
	if (DSP_SUCCEEDED(status)) {
		PROC_NotifyClients(hNode->hProcessor, DSP_NODESTATECHANGE);
		NTFY_Notify(hNode->hNtfy, DSP_NODESTATECHANGE);
	}

	return status;
}

/*
 *  ======== NODE_CreateMgr ========
 *  Purpose:
 *      Create a NODE Manager object.
 */
DSP_STATUS NODE_CreateMgr(OUT struct NODE_MGR **phNodeMgr,
			 struct DEV_OBJECT *hDevObject)
{
	u32 i;
	struct NODE_MGR *pNodeMgr = NULL;
	struct DISP_ATTRS dispAttrs;
	char *szZLFile = "";
	struct NLDR_ATTRS nldrAttrs;
	DSP_STATUS status = DSP_SOK;
	u32 devType;
	DBC_Require(cRefs > 0);
	DBC_Require(phNodeMgr != NULL);
	DBC_Require(hDevObject != NULL);
	GT_2trace(NODE_debugMask, GT_ENTER, "NODE_CreateMgr: phNodeMgr: 0x%x\t"
		 "hDevObject: 0x%x\n", phNodeMgr, hDevObject);
	*phNodeMgr = NULL;
	/* Allocate Node manager object */
	MEM_AllocObject(pNodeMgr, struct NODE_MGR, NODEMGR_SIGNATURE);
	if (pNodeMgr) {
		pNodeMgr->hDevObject = hDevObject;
		pNodeMgr->nodeList = LST_Create();
		pNodeMgr->pipeMap = GB_create(MAXPIPES);
		pNodeMgr->pipeDoneMap = GB_create(MAXPIPES);
		if (pNodeMgr->nodeList == NULL || pNodeMgr->pipeMap == NULL ||
		   pNodeMgr->pipeDoneMap == NULL) {
			status = DSP_EMEMORY;
			GT_0trace(NODE_debugMask, GT_6CLASS,
				 "NODE_CreateMgr: Memory "
				 "allocation failed\n");
		} else {
			status = NTFY_Create(&pNodeMgr->hNtfy);
		}
		pNodeMgr->uNumCreated = 0;
	} else {
		GT_0trace(NODE_debugMask, GT_6CLASS,
			 "NODE_CreateMgr: Memory allocation failed\n");
		status = DSP_EMEMORY;
	}
	/* get devNodeType */
	if (DSP_SUCCEEDED(status))
		status = DEV_GetDevType(hDevObject, &devType);

	/* Create the DCD Manager */
	if (DSP_SUCCEEDED(status)) {
		status = DCD_CreateManager(szZLFile, &pNodeMgr->hDcdMgr);
		if (DSP_SUCCEEDED(status))
			status = GetProcProps(pNodeMgr, hDevObject);

	}
	/* Create NODE Dispatcher */
	if (DSP_SUCCEEDED(status)) {
		dispAttrs.ulChnlOffset = pNodeMgr->ulChnlOffset;
		dispAttrs.ulChnlBufSize = pNodeMgr->ulChnlBufSize;
		dispAttrs.procFamily = pNodeMgr->procFamily;
		dispAttrs.procType = pNodeMgr->procType;
		status = DISP_Create(&pNodeMgr->hDisp, hDevObject, &dispAttrs);
	}
	/* Create a STRM Manager */
	if (DSP_SUCCEEDED(status))
		status = STRM_Create(&pNodeMgr->hStrmMgr, hDevObject);

	if (DSP_SUCCEEDED(status)) {
		DEV_GetIntfFxns(hDevObject, &pNodeMgr->pIntfFxns);
		/* Get MSG queue manager */
		DEV_GetMsgMgr(hDevObject, &pNodeMgr->hMsg);
		status = SYNC_InitializeCS(&pNodeMgr->hSync);
		if (DSP_FAILED(status))
			status = DSP_EMEMORY;
	}
	if (DSP_SUCCEEDED(status)) {
		pNodeMgr->chnlMap = GB_create(pNodeMgr->ulNumChnls);
		/* dma chnl map. ulNumChnls is # per transport */
		pNodeMgr->dmaChnlMap = GB_create(pNodeMgr->ulNumChnls);
		pNodeMgr->zChnlMap = GB_create(pNodeMgr->ulNumChnls);
		if ((pNodeMgr->chnlMap == NULL) ||
		   (pNodeMgr->dmaChnlMap == NULL) ||
		   (pNodeMgr->zChnlMap == NULL)) {
			status = DSP_EMEMORY;
		} else {
			/* Block out reserved channels */
			for (i = 0; i < pNodeMgr->ulChnlOffset; i++)
				GB_set(pNodeMgr->chnlMap, i);

			/* Block out channels reserved for RMS */
			GB_set(pNodeMgr->chnlMap, pNodeMgr->ulChnlOffset);
			GB_set(pNodeMgr->chnlMap, pNodeMgr->ulChnlOffset + 1);
		}
	}
	if (DSP_SUCCEEDED(status)) {
		/* NO RM Server on the IVA */
		if (devType != IVA_UNIT) {
			/* Get addresses of any RMS functions loaded */
			status = GetRMSFxns(pNodeMgr);
			if (DSP_FAILED(status)) {
				GT_1trace(NODE_debugMask, GT_6CLASS,
				"NODE_CreateMgr: Failed to"
				" get RMS functions: status = 0x%x", status);
			}
		}
	}

	/* Get loader functions and create loader */
	if (DSP_SUCCEEDED(status)) {
		GT_0trace(NODE_debugMask, GT_1CLASS,
			 "NODE_CreateMgr: using dynamic loader\n");
		pNodeMgr->nldrFxns = nldrFxns;	/* Dynamic loader functions */
	}
	if (DSP_SUCCEEDED(status)) {
		nldrAttrs.pfnOvly = Ovly;
		nldrAttrs.pfnWrite = Write;
		nldrAttrs.usDSPWordSize = pNodeMgr->uDSPWordSize;
		nldrAttrs.usDSPMauSize = pNodeMgr->uDSPMauSize;
		pNodeMgr->fLoaderInit = pNodeMgr->nldrFxns.pfnInit();
		status = pNodeMgr->nldrFxns.pfnCreate(&pNodeMgr->hNldr,
						     hDevObject, &nldrAttrs);
		if (DSP_FAILED(status)) {
			GT_1trace(NODE_debugMask, GT_6CLASS,
				 "NODE_CreateMgr: Failed to "
				 "create loader: status = 0x%x\n", status);
		}
	}
	if (DSP_SUCCEEDED(status))
		*phNodeMgr = pNodeMgr;
	else
		DeleteNodeMgr(pNodeMgr);

	DBC_Ensure((DSP_FAILED(status) && (*phNodeMgr == NULL)) ||
		  (DSP_SUCCEEDED(status) &&
		  MEM_IsValidHandle((*phNodeMgr), NODEMGR_SIGNATURE)));

	return status;
}

/*
 *  ======== NODE_Delete ========
 *  Purpose:
 *      Delete a node on the DSP by remotely calling the node's delete function.
 *      Loads the node's delete function if necessary. Free GPP side resources
 *      after node's delete function returns.
 */
DSP_STATUS NODE_Delete(struct NODE_OBJECT *hNode,
		struct PROCESS_CONTEXT *pr_ctxt)
{
	struct NODE_OBJECT *pNode = (struct NODE_OBJECT *)hNode;
	struct NODE_MGR *hNodeMgr;
	struct PROC_OBJECT *hProcessor;
	struct DISP_OBJECT *hDisp;
	u32 ulDeleteFxn;
	enum NODE_TYPE nodeType;
	enum NODE_STATE state;
	DSP_STATUS status = DSP_SOK;
	DSP_STATUS status1 = DSP_SOK;
	struct DSP_CBDATA cbData;
	u32 procId;
	struct WMD_DRV_INTERFACE *pIntfFxns;

#ifndef RES_CLEANUP_DISABLE
	HANDLE		nodeRes;
#endif
	struct DSP_PROCESSORSTATE procStatus;
	DBC_Require(cRefs > 0);
	GT_1trace(NODE_debugMask, GT_ENTER, "NODE_Delete: hNode: 0x%x\n",
		  hNode);
	if (!MEM_IsValidHandle(hNode, NODE_SIGNATURE)) {
		status = DSP_EHANDLE;
		goto func_end;
	}
	/* create struct DSP_CBDATA struct for PWR call */
	cbData.cbData = PWR_TIMEOUT;
	hNodeMgr = hNode->hNodeMgr;
	hProcessor = hNode->hProcessor;
	hDisp = hNodeMgr->hDisp;
	nodeType = NODE_GetType(hNode);
	pIntfFxns = hNodeMgr->pIntfFxns;
	/* Enter critical section */
	status = SYNC_EnterCS(hNodeMgr->hSync);
	if (DSP_FAILED(status))
		goto func_end;

	state = NODE_GetState(hNode);
	 /*  Execute delete phase code for non-device node in all cases
	  *  except when the node was only allocated. Delete phase must be
	  *  executed even if create phase was executed, but failed.
	  *  If the node environment pointer is non-NULL, the delete phase
	  *  code must be  executed.  */
	if (!(state == NODE_ALLOCATED && hNode->nodeEnv == (u32)NULL) &&
	   nodeType != NODE_DEVICE) {
		status = PROC_GetProcessorId(pNode->hProcessor, &procId);
		if (DSP_FAILED(status))
			goto func_cont1;

		if (procId == DSP_UNIT || procId == IVA_UNIT) {
			/*  If node has terminated, execute phase code will
			 *  have already been unloaded in NODE_OnExit(). If the
			 *  node is PAUSED, the execute phase is loaded, and it
			 *  is now ok to unload it. If the node is running, we
			 *  will unload the execute phase only after deleting
			 *  the node.  */
			if (state == NODE_PAUSED && hNode->fLoaded &&
			   hNode->fPhaseSplit) {
				/* Ok to unload execute code as long as node
				 * is not * running */
				status1 = hNodeMgr->nldrFxns.pfnUnload(hNode->
					  hNldrNode, NLDR_EXECUTE);
				hNode->fLoaded = false;
				NODE_SetState(hNode, NODE_DONE);
			}
			/* Load delete phase code if not loaded or if haven't
			 * * unloaded EXECUTE phase */
			if ((!(hNode->fLoaded) || (state == NODE_RUNNING)) &&
			   hNode->fPhaseSplit) {
				status = hNodeMgr->nldrFxns.pfnLoad(hNode->
					 hNldrNode, NLDR_DELETE);
				if (DSP_SUCCEEDED(status)) {
					hNode->fLoaded = true;
				} else {
					GT_1trace(NODE_debugMask, GT_ENTER,
						 "NODE_Delete: failed to "
						 "load delete code: 0x%x\n",
						 status);
				}
			}
		}
func_cont1:
		if (DSP_SUCCEEDED(status)) {
			/* Unblock a thread trying to terminate the node */
			(void)SYNC_SetEvent(hNode->hSyncDone);
			if (procId == DSP_UNIT) {
				/* ulDeleteFxn = address of node's delete
				 * function */
				status = GetFxnAddress(hNode, &ulDeleteFxn,
						      DELETEPHASE);
			} else if (procId == IVA_UNIT)
				ulDeleteFxn = (u32)hNode->nodeEnv;
			if (DSP_SUCCEEDED(status)) {
				status = PROC_GetState(hProcessor, &procStatus,
					sizeof(struct DSP_PROCESSORSTATE));
				GT_1trace(NODE_debugMask, GT_4CLASS,
						 "NODE_Delete: proc Status "
						 "0x%x\n", procStatus.iState);
				if (procStatus.iState != PROC_ERROR) {
					status = DISP_NodeDelete(hDisp, hNode,
					hNodeMgr->ulFxnAddrs[RMSDELETENODE],
					ulDeleteFxn, hNode->nodeEnv);
				} else
					NODE_SetState(hNode, NODE_DONE);

				/* Unload execute, if not unloaded, and delete
				 * function */
				if (state == NODE_RUNNING &&
				   hNode->fPhaseSplit) {
					status1 = hNodeMgr->nldrFxns.pfnUnload(
						hNode->hNldrNode, NLDR_EXECUTE);
				}
				if (DSP_FAILED(status1)) {
					GT_1trace(NODE_debugMask, GT_ENTER,
						  "NODE_Delete: failed to"
						  "unload execute code: 0x%x\n",
						  status1);
				}
				status1 = hNodeMgr->nldrFxns.pfnUnload(
					  hNode->hNldrNode, NLDR_DELETE);
				hNode->fLoaded = false;
				if (DSP_FAILED(status1)) {
					GT_1trace(NODE_debugMask, GT_ENTER,
						  "NODE_Delete: failed to"
						  "unload delete code: 0x%x\n",
						  status1);
				}
			}
		}
	}
	/* Free host side resources even if a failure occurred */
	/* Remove node from hNodeMgr->nodeList */
	LST_RemoveElem(hNodeMgr->nodeList, (struct LST_ELEM *) hNode);
	hNodeMgr->uNumNodes--;
	/* Decrement count of nodes created on DSP */
	if ((state != NODE_ALLOCATED) || ((state == NODE_ALLOCATED) &&
	(hNode->nodeEnv != (u32) NULL)))
		hNodeMgr->uNumCreated--;
	 /*  Free host-side resources allocated by NODE_Create()
	 *  DeleteNode() fails if SM buffers not freed by client!  */
#ifndef RES_CLEANUP_DISABLE
	if (!pr_ctxt)
		goto func_cont;
	if (DRV_GetNodeResElement(hNode, &nodeRes, pr_ctxt) != DSP_ENOTFOUND) {
		GT_0trace(NODE_debugMask, GT_5CLASS, "\nNODE_Delete12:\n");
		DRV_ProcNodeUpdateStatus(nodeRes, false);
	}
#endif
func_cont:
	GT_0trace(NODE_debugMask, GT_ENTER, "\nNODE_Delete13:\n ");
	DeleteNode(hNode, pr_ctxt);
#ifndef RES_CLEANUP_DISABLE
	GT_0trace(NODE_debugMask, GT_5CLASS, "\nNODE_Delete2:\n ");
	if (pr_ctxt)
		DRV_RemoveNodeResElement(nodeRes, pr_ctxt);
#endif
	GT_0trace(NODE_debugMask, GT_ENTER, "\nNODE_Delete3:\n ");
	/* Exit critical section */
	(void)SYNC_LeaveCS(hNodeMgr->hSync);
	PROC_NotifyClients(hProcessor, DSP_NODESTATECHANGE);
func_end:
	return status;
}

/*
 *  ======== NODE_DeleteMgr ========
 *  Purpose:
 *      Delete the NODE Manager.
 */
DSP_STATUS NODE_DeleteMgr(struct NODE_MGR *hNodeMgr)
{
	DSP_STATUS status = DSP_SOK;

	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(hNodeMgr, NODEMGR_SIGNATURE));

	GT_1trace(NODE_debugMask, GT_ENTER, "NODE_DeleteMgr: hNodeMgr: 0x%x\n",
		 hNodeMgr);
	DeleteNodeMgr(hNodeMgr);

	return status;
}

/*
 *  ======== NODE_EnumNodes ========
 *  Purpose:
 *      Enumerate currently allocated nodes.
 */
DSP_STATUS NODE_EnumNodes(struct NODE_MGR *hNodeMgr, IN DSP_HNODE *aNodeTab,
			 u32 uNodeTabSize, OUT u32 *puNumNodes,
			 OUT u32 *puAllocated)
{
	struct NODE_OBJECT *hNode;
	u32 i;
	DSP_STATUS status = DSP_SOK;
	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(hNodeMgr, NODEMGR_SIGNATURE));
	DBC_Require(aNodeTab != NULL || uNodeTabSize == 0);
	DBC_Require(puNumNodes != NULL);
	DBC_Require(puAllocated != NULL);
	GT_5trace(NODE_debugMask, GT_ENTER, "NODE_EnumNodes: hNodeMgr: 0x%x\t"
		 "aNodeTab: %d\tuNodeTabSize: 0x%x\tpuNumNodes: 0x%x\t"
		 "puAllocated\n", hNodeMgr, aNodeTab, uNodeTabSize, puNumNodes,
		 puAllocated);
	/* Enter critical section */
	status = SYNC_EnterCS(hNodeMgr->hSync);
	if (DSP_SUCCEEDED(status)) {
		if (hNodeMgr->uNumNodes > uNodeTabSize) {
			*puAllocated = hNodeMgr->uNumNodes;
			*puNumNodes = 0;
			status = DSP_ESIZE;
		} else {
			hNode = (struct NODE_OBJECT *)LST_First(hNodeMgr->
				nodeList);
			for (i = 0; i < hNodeMgr->uNumNodes; i++) {
				DBC_Assert(MEM_IsValidHandle(hNode,
					  NODE_SIGNATURE));
				aNodeTab[i] = hNode;
				hNode = (struct NODE_OBJECT *)LST_Next
					(hNodeMgr->nodeList,
					(struct LST_ELEM *)hNode);
			}
			*puAllocated = *puNumNodes = hNodeMgr->uNumNodes;
		}
	}
	/* end of SYNC_EnterCS */
	/* Exit critical section */
	(void)SYNC_LeaveCS(hNodeMgr->hSync);
	return status;
}

/*
 *  ======== NODE_Exit ========
 *  Purpose:
 *      Discontinue usage of NODE module.
 */
void NODE_Exit(void)
{
	DBC_Require(cRefs > 0);

	cRefs--;

	GT_1trace(NODE_debugMask, GT_5CLASS,
		 "Entered NODE_Exit, ref count:  0x%x\n", cRefs);

	DBC_Ensure(cRefs >= 0);
}

/*
 *  ======== NODE_FreeMsgBuf ========
 *  Purpose:
 *      Frees the message buffer.
 */
DSP_STATUS NODE_FreeMsgBuf(struct NODE_OBJECT *hNode, IN u8 *pBuffer,
				 OPTIONAL struct DSP_BUFFERATTR *pAttr)
{
	struct NODE_OBJECT *pNode = (struct NODE_OBJECT *)hNode;
	DSP_STATUS status = DSP_SOK;
	u32 procId;
	DBC_Require(cRefs > 0);
	DBC_Require(pBuffer != NULL);
	DBC_Require(pNode != NULL);
	DBC_Require(pNode->hXlator != NULL);
	GT_3trace(NODE_debugMask, GT_ENTER, "NODE_FreeMsgBuf: hNode: 0x%x\t"
		 "pBuffer: 0x%x\tpAttr: 0x%x\n", hNode, pBuffer, pAttr);
	if (!MEM_IsValidHandle(hNode, NODE_SIGNATURE)) {
		status = DSP_EHANDLE;
		goto func_end;
	}
	status = PROC_GetProcessorId(pNode->hProcessor, &procId);
	if (procId == DSP_UNIT) {
		if (DSP_SUCCEEDED(status)) {
			if (pAttr == NULL) {
				/* set defaults */
				pAttr = &NODE_DFLTBUFATTRS;
			}
			 /* Node supports single SM segment only */
			if (pAttr->uSegment != 1)
				status = DSP_EBADSEGID;

			/* pBuffer is clients Va. */
			status = CMM_XlatorFreeBuf(pNode->hXlator, pBuffer);
			if (DSP_FAILED(status))
				status = DSP_EFAIL;
			else
				status = DSP_SOK;

		}
	} else {
		DBC_Assert(NULL);	/* BUG */
	}
func_end:
	return status;
}

/*
 *  ======== NODE_GetAttr ========
 *  Purpose:
 *      Copy the current attributes of the specified node into a DSP_NODEATTR
 *      structure.
 */
DSP_STATUS NODE_GetAttr(struct NODE_OBJECT *hNode,
			OUT struct DSP_NODEATTR *pAttr, u32 uAttrSize)
{
	struct NODE_MGR *hNodeMgr;
	DSP_STATUS status = DSP_SOK;
	DBC_Require(cRefs > 0);
	DBC_Require(pAttr != NULL);
	DBC_Require(uAttrSize >= sizeof(struct DSP_NODEATTR));
	GT_3trace(NODE_debugMask, GT_ENTER, "NODE_GetAttr: hNode: "
		 "0x%x\tpAttr: 0x%x \tuAttrSize: 0x%x\n", hNode, pAttr,
		 uAttrSize);
	if (!MEM_IsValidHandle(hNode, NODE_SIGNATURE)) {
		status = DSP_EHANDLE;
	} else {
		hNodeMgr = hNode->hNodeMgr;
		 /* Enter hNodeMgr critical section (since we're accessing
		  * data that could be changed by NODE_ChangePriority() and
		  * NODE_Connect().  */
		status = SYNC_EnterCS(hNodeMgr->hSync);
		if (DSP_SUCCEEDED(status)) {
			pAttr->cbStruct = sizeof(struct DSP_NODEATTR);
			/* DSP_NODEATTRIN */
			pAttr->inNodeAttrIn.cbStruct =
					 sizeof(struct DSP_NODEATTRIN);
			pAttr->inNodeAttrIn.iPriority = hNode->nPriority;
			pAttr->inNodeAttrIn.uTimeout = hNode->uTimeout;
			pAttr->inNodeAttrIn.uHeapSize =
				hNode->createArgs.asa.taskArgs.uHeapSize;
			pAttr->inNodeAttrIn.pGPPVirtAddr = (void *)
				hNode->createArgs.asa.taskArgs.uGPPHeapAddr;
			pAttr->uInputs = hNode->uNumGPPInputs;
			pAttr->uOutputs = hNode->uNumGPPOutputs;
			/* DSP_NODEINFO */
			GetNodeInfo(hNode, &(pAttr->iNodeInfo));
		}
		/* end of SYNC_EnterCS */
		/* Exit critical section */
		(void)SYNC_LeaveCS(hNodeMgr->hSync);
	}
	return status;
}

/*
 *  ======== NODE_GetChannelId ========
 *  Purpose:
 *      Get the channel index reserved for a stream connection between the
 *      host and a node.
 */
DSP_STATUS NODE_GetChannelId(struct NODE_OBJECT *hNode, u32 uDir, u32 uIndex,
			    OUT u32 *pulId)
{
	enum NODE_TYPE nodeType;
	DSP_STATUS status = DSP_EVALUE;
	DBC_Require(cRefs > 0);
	DBC_Require(uDir == DSP_TONODE || uDir == DSP_FROMNODE);
	DBC_Require(pulId != NULL);
	GT_4trace(NODE_debugMask, GT_ENTER, "NODE_GetChannelId: hNode: "
		 "0x%x\tuDir: %d\tuIndex: %d\tpulId: 0x%x\n", hNode, uDir,
		 uIndex, pulId);
	if (!MEM_IsValidHandle(hNode, NODE_SIGNATURE)) {
		status = DSP_EHANDLE;
		return status;
	}
	nodeType = NODE_GetType(hNode);
	if (nodeType != NODE_TASK && nodeType != NODE_DAISSOCKET) {
		status = DSP_ENODETYPE;
		return status;
	}
	if (uDir == DSP_TONODE) {
		if (uIndex < MaxInputs(hNode)) {
			if (hNode->inputs[uIndex].type == HOSTCONNECT) {
				*pulId = hNode->inputs[uIndex].devId;
				status = DSP_SOK;
			}
		}
	} else {
		DBC_Assert(uDir == DSP_FROMNODE);
		if (uIndex < MaxOutputs(hNode)) {
			if (hNode->outputs[uIndex].type == HOSTCONNECT) {
				*pulId = hNode->outputs[uIndex].devId;
				status = DSP_SOK;
			}
		}
	}
	return status;
}

/*
 *  ======== NODE_GetMessage ========
 *  Purpose:
 *      Retrieve a message from a node on the DSP.
 */
DSP_STATUS NODE_GetMessage(struct NODE_OBJECT *hNode, OUT struct DSP_MSG *pMsg,
			  u32 uTimeout)
{
	struct NODE_MGR *hNodeMgr;
	enum NODE_TYPE nodeType;
	struct WMD_DRV_INTERFACE *pIntfFxns;
	DSP_STATUS status = DSP_SOK;
	void *pTmpBuf;
	struct DSP_PROCESSORSTATE procStatus;
	struct PROC_OBJECT *hProcessor;

	DBC_Require(cRefs > 0);
	DBC_Require(pMsg != NULL);
	GT_3trace(NODE_debugMask, GT_ENTER,
		 "NODE_GetMessage: hNode: 0x%x\tpMsg: "
		 "0x%x\tuTimeout: 0x%x\n", hNode, pMsg, uTimeout);
	if (!MEM_IsValidHandle(hNode, NODE_SIGNATURE)) {
		status = DSP_EHANDLE;
		goto func_end;
	}
	hProcessor = hNode->hProcessor;
	status = PROC_GetState(hProcessor, &procStatus,
					sizeof(struct DSP_PROCESSORSTATE));
	if (DSP_FAILED(status))
		goto func_end;
	/* If processor is in error state then don't attempt to get the
	    message */
	if (procStatus.iState == PROC_ERROR) {
		GT_1trace(NODE_debugMask, GT_4CLASS, "NODE_GetMessage:"
			"		proc Status 0x%x\n", procStatus.iState);
		status = DSP_EFAIL;
		goto func_end;
	}
	hNodeMgr = hNode->hNodeMgr;
	nodeType = NODE_GetType(hNode);
	if (nodeType != NODE_MESSAGE && nodeType != NODE_TASK &&
	   nodeType != NODE_DAISSOCKET) {
		status = DSP_ENODETYPE;
		goto func_end;
	}
	 /*  This function will block unless a message is available. Since
	 *  DSPNode_RegisterNotify() allows notification when a message
	 *  is available, the system can be designed so that
	 *  DSPNode_GetMessage() is only called when a message is
	 *  available.  */
	pIntfFxns = hNodeMgr->pIntfFxns;
	status = (*pIntfFxns->pfnMsgGet)(hNode->hMsgQueue, pMsg, uTimeout);
	/* Check if message contains SM descriptor */
	if (DSP_FAILED(status) ||  !(pMsg->dwCmd & DSP_RMSBUFDESC))
		goto func_end;

	 /* Translate DSP byte addr to GPP Va.  */
	pTmpBuf = CMM_XlatorTranslate(hNode->hXlator,
		(void *)(pMsg->dwArg1 * hNode->hNodeMgr->uDSPWordSize),
		CMM_DSPPA2PA);
	if (pTmpBuf  != NULL) {
		/* now convert this GPP Pa to Va */
		pTmpBuf = CMM_XlatorTranslate(hNode->hXlator, pTmpBuf,
							CMM_PA2VA);
		if (pTmpBuf != NULL) {
			/* Adjust SM size in msg */
			pMsg->dwArg1 = (u32) pTmpBuf;
			pMsg->dwArg2 *= hNode->hNodeMgr->uDSPWordSize;
		} else {
			GT_0trace(NODE_debugMask, GT_7CLASS, "NODE_GetMessage: "
				 "Failed SM translation!\n");
			status = DSP_ETRANSLATE;
		}
	} else {
		GT_0trace(NODE_debugMask, GT_7CLASS, "NODE_GetMessage: Failed "
			 "SM Pa/Pa translation!\n");
		status = DSP_ETRANSLATE;
	}
func_end:
	return status;
}

/*
 *   ======== NODE_GetNldrObj ========
 */
DSP_STATUS NODE_GetNldrObj(struct NODE_MGR *hNodeMgr,
			  struct NLDR_OBJECT **phNldrObj)
{
	DSP_STATUS status = DSP_SOK;
	struct NODE_MGR *pNodeMgr = hNodeMgr;
	DBC_Require(phNldrObj != NULL);
	GT_2trace(NODE_debugMask, GT_ENTER,
		 "Entered NODE_GetNldrObj, hNodeMgr: "
		 "0x%x\n\tphNldrObj:  0x%x\n", hNodeMgr, phNldrObj);
	if (!MEM_IsValidHandle(hNodeMgr, NODEMGR_SIGNATURE))
		status = DSP_EHANDLE;
	else
		*phNldrObj = pNodeMgr->hNldr;

	GT_2trace(NODE_debugMask, GT_ENTER,
		 "Exit NODE_GetNldrObj: status 0x%x\n\t"
		 "phNldrObj:  0x%x\n", status, *phNldrObj);
	DBC_Ensure(DSP_SUCCEEDED(status) || ((phNldrObj != NULL) &&
		  (*phNldrObj == NULL)));
	return status;
}

/*
 *  ======== NODE_GetStrmMgr ========
 *  Purpose:
 *      Returns the Stream manager.
 */
DSP_STATUS NODE_GetStrmMgr(struct NODE_OBJECT *hNode,
			  struct STRM_MGR **phStrmMgr)
{
	DSP_STATUS status = DSP_SOK;

	DBC_Require(cRefs > 0);

	if (!MEM_IsValidHandle(hNode, NODE_SIGNATURE))
		status = DSP_EHANDLE;
	else
		*phStrmMgr = hNode->hNodeMgr->hStrmMgr;

	return status;
}

/*
 *  ======== NODE_GetLoadType ========
 */
enum NLDR_LOADTYPE NODE_GetLoadType(struct NODE_OBJECT *hNode)
{

	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(hNode, NODE_SIGNATURE));
       if (!MEM_IsValidHandle(hNode, NODE_SIGNATURE)) {
               GT_1trace(NODE_debugMask, GT_5CLASS,
                        "NODE_GetLoadType: Failed. hNode:"
                         " 0x%x\n", hNode);
               return -1;
       } else
               return hNode->dcdProps.objData.nodeObj.usLoadType;
}

/*
 *  ======== NODE_GetTimeout ========
 *  Purpose:
 *      Returns the timeout value for this node.
 */
u32 NODE_GetTimeout(struct NODE_OBJECT *hNode)
{
	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(hNode, NODE_SIGNATURE));
       if (!MEM_IsValidHandle(hNode, NODE_SIGNATURE)) {
               GT_1trace(NODE_debugMask, GT_5CLASS,
                        "NODE_GetTimeout: Failed. hNode:"
                         " 0x%x\n", hNode);
               return 0;
       } else
               return hNode->uTimeout;
}

/*
 *  ======== NODE_GetType ========
 *  Purpose:
 *      Returns the node type.
 */
enum NODE_TYPE NODE_GetType(struct NODE_OBJECT *hNode)
{
	enum NODE_TYPE nodeType;

	if (hNode == (struct NODE_OBJECT *) DSP_HGPPNODE)
		nodeType = NODE_GPP;
	else {
                if (!MEM_IsValidHandle(hNode, NODE_SIGNATURE))
                        nodeType = -1;
                else
                        nodeType = hNode->nType;
	}
	return nodeType;
}

/*
 *  ======== NODE_Init ========
 *  Purpose:
 *      Initialize the NODE module.
 */
bool NODE_Init(void)
{
	bool fRetVal = true;

	DBC_Require(cRefs >= 0);

	if (cRefs == 0) {
		DBC_Assert(!NODE_debugMask.flags);
		GT_create(&NODE_debugMask, "NO");	/* "NO" for NOde */
	}

	if (fRetVal)
		cRefs++;

	GT_1trace(NODE_debugMask, GT_5CLASS, "NODE_Init(), ref count: 0x%x\n",
		 cRefs);

	DBC_Ensure((fRetVal && (cRefs > 0)) || (!fRetVal && (cRefs >= 0)));
	return fRetVal;
}

/*
 *  ======== NODE_OnExit ========
 *  Purpose:
 *      Gets called when RMS_EXIT is received for a node.
 */
void NODE_OnExit(struct NODE_OBJECT *hNode, s32 nStatus)
{
	DBC_Assert(MEM_IsValidHandle(hNode, NODE_SIGNATURE));
	/* Set node state to done */
	NODE_SetState(hNode, NODE_DONE);
	hNode->nExitStatus = nStatus;
	if (hNode->fLoaded && hNode->fPhaseSplit) {
		(void)hNode->hNodeMgr->nldrFxns.pfnUnload(hNode->hNldrNode,
							 NLDR_EXECUTE);
		hNode->fLoaded = false;
	}
	/* Unblock call to NODE_Terminate */
	(void) SYNC_SetEvent(hNode->hSyncDone);
	/* Notify clients */
	PROC_NotifyClients(hNode->hProcessor, DSP_NODESTATECHANGE);
	NTFY_Notify(hNode->hNtfy, DSP_NODESTATECHANGE);
}

/*
 *  ======== NODE_Pause ========
 *  Purpose:
 *      Suspend execution of a node currently running on the DSP.
 */
DSP_STATUS NODE_Pause(struct NODE_OBJECT *hNode)
{
	struct NODE_OBJECT *pNode = (struct NODE_OBJECT *)hNode;
	enum NODE_TYPE nodeType;
	enum NODE_STATE state;
	struct NODE_MGR *hNodeMgr;
	DSP_STATUS status = DSP_SOK;
	u32 procId;
	struct DSP_PROCESSORSTATE procStatus;
	struct PROC_OBJECT *hProcessor;

	DBC_Require(cRefs > 0);

	GT_1trace(NODE_debugMask, GT_ENTER, "NODE_Pause: hNode: 0x%x\n", hNode);

	if (!MEM_IsValidHandle(hNode, NODE_SIGNATURE)) {
		status = DSP_EHANDLE;
		goto func_end;
	} else {
		nodeType = NODE_GetType(hNode);
		if (nodeType != NODE_TASK && nodeType != NODE_DAISSOCKET)
			status = DSP_ENODETYPE;
	}
	if (DSP_FAILED(status))
		goto func_end;

	status = PROC_GetProcessorId(pNode->hProcessor, &procId);

	if (procId == IVA_UNIT)
		status = DSP_ENOTIMPL;

	if (DSP_SUCCEEDED(status)) {
		hNodeMgr = hNode->hNodeMgr;

		/* Enter critical section */
		status = SYNC_EnterCS(hNodeMgr->hSync);

		if (DSP_SUCCEEDED(status)) {
			state = NODE_GetState(hNode);
			/* Check node state */
			if (state != NODE_RUNNING)
				status = DSP_EWRONGSTATE;

			hProcessor = hNode->hProcessor;
			status = PROC_GetState(hProcessor, &procStatus,
					sizeof(struct DSP_PROCESSORSTATE));
			if (DSP_FAILED(status))
				goto func_end;
			/* If processor is in error state then don't attempt
			    to send the message */
			if (procStatus.iState == PROC_ERROR) {
				GT_1trace(NODE_debugMask, GT_4CLASS,
					"NODE_Pause: proc Status 0x%x\n",
					procStatus.iState);
				status = DSP_EFAIL;
				goto func_end;
			}
			if (DSP_SUCCEEDED(status)) {
				status = DISP_NodeChangePriority(hNodeMgr->
				   hDisp, hNode,
				   hNodeMgr->ulFxnAddrs[RMSCHANGENODEPRIORITY],
				   hNode->nodeEnv, NODE_SUSPENDEDPRI);
			}

			/* Update state */
			if (DSP_SUCCEEDED(status)) {
				NODE_SetState(hNode, NODE_PAUSED);
			} else {
				GT_1trace(NODE_debugMask, GT_6CLASS,
					 "NODE_Pause: Failed. hNode:"
					 " 0x%x\n", hNode);
			}
		}
		/* End of SYNC_EnterCS */
		/* Leave critical section */
		(void)SYNC_LeaveCS(hNodeMgr->hSync);
		if (DSP_SUCCEEDED(status)) {
			PROC_NotifyClients(hNode->hProcessor,
					  DSP_NODESTATECHANGE);
			NTFY_Notify(hNode->hNtfy, DSP_NODESTATECHANGE);
		}
	}
func_end:
	return status;
}

/*
 *  ======== NODE_PutMessage ========
 *  Purpose:
 *      Send a message to a message node, task node, or XDAIS socket node. This
 *      function will block until the message stream can accommodate the
 *      message, or a timeout occurs.
 */
DSP_STATUS NODE_PutMessage(struct NODE_OBJECT *hNode,
			  IN CONST struct DSP_MSG *pMsg, u32 uTimeout)
{
	struct NODE_MGR *hNodeMgr = NULL;
	enum NODE_TYPE nodeType;
	struct WMD_DRV_INTERFACE *pIntfFxns;
	enum NODE_STATE state;
	DSP_STATUS status = DSP_SOK;
	void *pTmpBuf;
	struct DSP_MSG newMsg;
	struct DSP_PROCESSORSTATE procStatus;
	struct PROC_OBJECT *hProcessor;

	DBC_Require(cRefs > 0);
	DBC_Require(pMsg != NULL);
	GT_3trace(NODE_debugMask, GT_ENTER,
		 "NODE_PutMessage: hNode: 0x%x\tpMsg: "
		 "0x%x\tuTimeout: 0x%x\n", hNode, pMsg, uTimeout);
	if (!MEM_IsValidHandle(hNode, NODE_SIGNATURE)) {
		status = DSP_EHANDLE;
		goto func_end;
	}
	hProcessor = hNode->hProcessor;
	status = PROC_GetState(hProcessor, &procStatus,
					sizeof(struct DSP_PROCESSORSTATE));
	if (DSP_FAILED(status))
		goto func_end;
	/* If processor is in bad state then don't attempt sending the
	    message */
	if (procStatus.iState == PROC_ERROR) {
		GT_1trace(NODE_debugMask, GT_4CLASS, "NODE_PutMessage:"
			"		proc Status 0x%x\n", procStatus.iState);
		status = DSP_EFAIL;
		goto func_end;
	}
	hNodeMgr = hNode->hNodeMgr;
	nodeType = NODE_GetType(hNode);
	if (nodeType != NODE_MESSAGE && nodeType != NODE_TASK &&
	    nodeType != NODE_DAISSOCKET)
		status = DSP_ENODETYPE;

	if (DSP_SUCCEEDED(status)) {
		/*  Check node state. Can't send messages to a node after
		 *  we've sent the RMS_EXIT command. There is still the
		 *  possibility that NODE_Terminate can be called after we've
		 *  checked the state. Could add another SYNC object to
		 *  prevent this (can't use hNodeMgr->hSync, since we don't
		 *  want to block other NODE functions). However, the node may
		 *  still exit on its own, before this message is sent.  */
		status = SYNC_EnterCS(hNodeMgr->hSync);
		if (DSP_SUCCEEDED(status)) {
			state = NODE_GetState(hNode);
			if (state == NODE_TERMINATING || state == NODE_DONE)
				status = DSP_EWRONGSTATE;

		}
		/* end of SYNC_EnterCS */
		(void)SYNC_LeaveCS(hNodeMgr->hSync);
	}
	if (DSP_FAILED(status))
		goto func_end;

	/* assign pMsg values to new msg  */
	newMsg = *pMsg;
	/* Now, check if message contains a SM buffer descriptor */
	if (pMsg->dwCmd & DSP_RMSBUFDESC) {
		/* Translate GPP Va to DSP physical buf Ptr. */
		pTmpBuf = CMM_XlatorTranslate(hNode->hXlator,
			(void *)newMsg.dwArg1, CMM_VA2DSPPA);
		if (pTmpBuf != NULL) {
			/* got translation, convert to MAUs in msg */
			if (hNode->hNodeMgr->uDSPWordSize != 0) {
				newMsg.dwArg1 =
					(u32)pTmpBuf /
					hNode->hNodeMgr->uDSPWordSize;
				/* MAUs */
				newMsg.dwArg2 /= hNode->hNodeMgr->uDSPWordSize;
			} else {
				GT_0trace(NODE_debugMask, GT_7CLASS,
					 "NODE_PutMessage: "
					 "uDSPWordSize is zero!\n");
				status = DSP_EFAIL;	/* bad DSPWordSize */
			}
		} else {	/* failed to translate buffer address */
			GT_0trace(NODE_debugMask, GT_7CLASS,
				 "NODE_PutMessage: Failed to"
				 " translate SM address\n");
			status = DSP_ETRANSLATE;
		}
	}
	if (DSP_SUCCEEDED(status)) {
		pIntfFxns = hNodeMgr->pIntfFxns;
		status = (*pIntfFxns->pfnMsgPut)(hNode->hMsgQueue,
			 &newMsg, uTimeout);
	}
func_end:
	return status;
}

/*
 *  ======== NODE_RegisterNotify ========
 *  Purpose:
 *      Register to be notified on specific events for this node.
 */
DSP_STATUS NODE_RegisterNotify(struct NODE_OBJECT *hNode, u32 uEventMask,
			       u32 uNotifyType,
			       struct DSP_NOTIFICATION *hNotification)
{
	struct WMD_DRV_INTERFACE *pIntfFxns;
	DSP_STATUS status = DSP_SOK;

	DBC_Require(cRefs > 0);
	DBC_Require(hNotification != NULL);

	GT_4trace(NODE_debugMask, GT_ENTER,
		 "NODE_RegisterNotify: hNode: 0x%x\t"
		 "uEventMask: 0x%x\tuNotifyType: 0x%x\thNotification: 0x%x\n",
		 hNode, uEventMask, uNotifyType, hNotification);

	if (!MEM_IsValidHandle(hNode, NODE_SIGNATURE)) {
		status = DSP_EHANDLE;
	} else {
		/* Check if event mask is a valid node related event */
		if (uEventMask & ~(DSP_NODESTATECHANGE |
		   DSP_NODEMESSAGEREADY))
			status = DSP_EVALUE;

		/* Check if notify type is valid */
		if (uNotifyType != DSP_SIGNALEVENT)
			status = DSP_EVALUE;

		/* Only one Notification can be registered at a
		 * time - Limitation */
		if (uEventMask == (DSP_NODESTATECHANGE |
		   DSP_NODEMESSAGEREADY))
			status = DSP_EVALUE;
	}
	if (DSP_SUCCEEDED(status)) {
		if (uEventMask == DSP_NODESTATECHANGE) {
			status = NTFY_Register(hNode->hNtfy, hNotification,
				 uEventMask & DSP_NODESTATECHANGE, uNotifyType);
		} else {
			/* Send Message part of event mask to MSG */
			pIntfFxns = hNode->hNodeMgr->pIntfFxns;
			status = (*pIntfFxns->pfnMsgRegisterNotify)
				 (hNode->hMsgQueue,
				 uEventMask & DSP_NODEMESSAGEREADY, uNotifyType,
				 hNotification);
		}

	}
	return status;
}

/*
 *  ======== NODE_Run ========
 *  Purpose:
 *      Start execution of a node's execute phase, or resume execution of a node
 *      that has been suspended (via NODE_NodePause()) on the DSP. Load the
 *      node's execute function if necessary.
 */
DSP_STATUS NODE_Run(struct NODE_OBJECT *hNode)
{
	struct NODE_OBJECT *pNode = (struct NODE_OBJECT *)hNode;
	struct NODE_MGR *hNodeMgr;
	enum NODE_TYPE nodeType;
	enum NODE_STATE state;
	u32 ulExecuteFxn;
	u32 ulFxnAddr;
	DSP_STATUS status = DSP_SOK;
	u32 procId;
	struct WMD_DRV_INTERFACE *pIntfFxns;
	struct DSP_PROCESSORSTATE procStatus;
	struct PROC_OBJECT *hProcessor;

	DBC_Require(cRefs > 0);
	GT_1trace(NODE_debugMask, GT_ENTER, "NODE_Run: hNode: 0x%x\n", hNode);
	if (!MEM_IsValidHandle(hNode, NODE_SIGNATURE)) {
		status = DSP_EHANDLE;
		goto func_end;
	}
	hProcessor = hNode->hProcessor;
	status = PROC_GetState(hProcessor, &procStatus,
					sizeof(struct DSP_PROCESSORSTATE));
	if (DSP_FAILED(status))
		goto func_end;
	/* If processor is in error state then don't attempt to run the node */
	if (procStatus.iState == PROC_ERROR) {
		GT_1trace(NODE_debugMask, GT_4CLASS, "NODE_Run:"
			"		proc Status 0x%x\n", procStatus.iState);
		status = DSP_EFAIL;
		goto func_end;
	}
	nodeType = NODE_GetType(hNode);
	if (nodeType == NODE_DEVICE)
		status = DSP_ENODETYPE;
	if (DSP_FAILED(status))
		goto func_end;

	hNodeMgr = hNode->hNodeMgr;
	pIntfFxns = hNodeMgr->pIntfFxns;
	/* Enter critical section */
	status = SYNC_EnterCS(hNodeMgr->hSync);
	if (DSP_FAILED(status))
		goto func_cont;

	state = NODE_GetState(hNode);
	if (state != NODE_CREATED && state != NODE_PAUSED)
		status = DSP_EWRONGSTATE;

	if (DSP_SUCCEEDED(status))
		status = PROC_GetProcessorId(pNode->hProcessor, &procId);

	if (DSP_FAILED(status))
		goto func_cont1;

	if ((procId != DSP_UNIT) && (procId != IVA_UNIT))
		goto func_cont1;

	if (state == NODE_CREATED) {
		/* If node's execute function is not loaded, load it */
		if (!(hNode->fLoaded) && hNode->fPhaseSplit) {
			status = hNodeMgr->nldrFxns.pfnLoad(hNode->hNldrNode,
				NLDR_EXECUTE);
			if (DSP_SUCCEEDED(status)) {
				hNode->fLoaded = true;
			} else {
				GT_1trace(NODE_debugMask, GT_ENTER,
					 "NODE_Run: failed to load "
					 "execute code:0x%x\n", status);
			}
		}
		if (DSP_SUCCEEDED(status)) {
			/* Get address of node's execute function */
			if (procId == IVA_UNIT)
				ulExecuteFxn = (u32) hNode->nodeEnv;
			else {
				status = GetFxnAddress(hNode, &ulExecuteFxn,
					 EXECUTEPHASE);
			}
		}
		if (DSP_SUCCEEDED(status)) {
			ulFxnAddr = hNodeMgr->ulFxnAddrs[RMSEXECUTENODE];
			status = DISP_NodeRun(hNodeMgr->hDisp, hNode, ulFxnAddr,
					     ulExecuteFxn, hNode->nodeEnv);
		}
	} else if (state == NODE_PAUSED) {
		ulFxnAddr = hNodeMgr->ulFxnAddrs[RMSCHANGENODEPRIORITY];
		status = DISP_NodeChangePriority(hNodeMgr->hDisp, hNode,
						ulFxnAddr, hNode->nodeEnv,
						NODE_GetPriority(hNode));
	} else {
		/* We should never get here */
		DBC_Assert(false);
	}
func_cont1:
	/* Update node state. */
	if (DSP_SUCCEEDED(status))
		NODE_SetState(hNode, NODE_RUNNING);
	 else /* Set state back to previous value */
		NODE_SetState(hNode, state);
	/*End of SYNC_EnterCS */
	/* Exit critical section */
func_cont:
	(void)SYNC_LeaveCS(hNodeMgr->hSync);
	if (DSP_SUCCEEDED(status)) {
		PROC_NotifyClients(hNode->hProcessor,
			  DSP_NODESTATECHANGE);
		NTFY_Notify(hNode->hNtfy, DSP_NODESTATECHANGE);
	}
func_end:
	return status;
}

/*
 *  ======== NODE_Terminate ========
 *  Purpose:
 *      Signal a node running on the DSP that it should exit its execute phase
 *      function.
 */
DSP_STATUS NODE_Terminate(struct NODE_OBJECT *hNode, OUT DSP_STATUS *pStatus)
{
	struct NODE_OBJECT *pNode = (struct NODE_OBJECT *)hNode;
	struct NODE_MGR *hNodeMgr = NULL;
	enum NODE_TYPE nodeType;
	struct WMD_DRV_INTERFACE *pIntfFxns;
	enum NODE_STATE state;
	struct DSP_MSG msg, killmsg;
	DSP_STATUS status = DSP_SOK;
	u32 procId, killTimeOut;
	struct DEH_MGR *hDehMgr;
	struct DSP_PROCESSORSTATE procStatus;

	DBC_Require(cRefs > 0);
	DBC_Require(pStatus != NULL);

	GT_1trace(NODE_debugMask, GT_ENTER,
		 "NODE_Terminate: hNode: 0x%x\n", hNode);
	if (!MEM_IsValidHandle(hNode, NODE_SIGNATURE)) {
		status = DSP_EHANDLE;
		goto func_end;
	}
	if (pNode->hProcessor == NULL) {
		GT_1trace(NODE_debugMask, GT_4CLASS,
		"NODE_Terminate: pNode->hProcessor = 0x%x\n",
		pNode->hProcessor);
		goto func_end;
	}
	status = PROC_GetProcessorId(pNode->hProcessor, &procId);

	if (DSP_SUCCEEDED(status)) {
		hNodeMgr = hNode->hNodeMgr;
		nodeType = NODE_GetType(hNode);
		if (nodeType != NODE_TASK && nodeType !=
			   NODE_DAISSOCKET)
				status = DSP_ENODETYPE;
	}
	if (DSP_SUCCEEDED(status)) {
		/* Check node state */
		status = SYNC_EnterCS(hNodeMgr->hSync);
		if (DSP_SUCCEEDED(status)) {
			state = NODE_GetState(hNode);
			if (state != NODE_RUNNING) {
				status = DSP_EWRONGSTATE;
				/* Set the exit status if node terminated on
				 * its own. */
				if (state == NODE_DONE)
					*pStatus = hNode->nExitStatus;

			} else {
				NODE_SetState(hNode, NODE_TERMINATING);
			}
		}
		/* end of SYNC_EnterCS */
		(void)SYNC_LeaveCS(hNodeMgr->hSync);
	}
	if (DSP_SUCCEEDED(status)) {
		/*
		 *  Send exit message. Do not change state to NODE_DONE
		 *  here. That will be done in callback.
		 */
		GT_1trace(NODE_debugMask, GT_5CLASS,
			 "NODE_Terminate: env = 0x%x\n", hNode->nodeEnv);

		status = PROC_GetState(pNode->hProcessor, &procStatus,
					sizeof(struct DSP_PROCESSORSTATE));
		if (DSP_FAILED(status))
			goto func_cont;
		/* If processor is in error state then don't attempt to send
		 * A kill task command */
		if (procStatus.iState == PROC_ERROR) {
			GT_1trace(NODE_debugMask, GT_4CLASS, "NODE_Terminate:"
				" proc Status 0x%x\n", procStatus.iState);
			status = DSP_EFAIL;
			goto func_cont;
		}

		msg.dwCmd = RMS_EXIT;
		msg.dwArg1 = hNode->nodeEnv;
		killmsg.dwCmd = RMS_KILLTASK;
		killmsg.dwArg1 = hNode->nodeEnv;
		pIntfFxns = hNodeMgr->pIntfFxns;

		if (hNode->uTimeout > MAXTIMEOUT)
			killTimeOut = MAXTIMEOUT;
		else
			killTimeOut = (hNode->uTimeout)*2;

		status = (*pIntfFxns->pfnMsgPut)(hNode->hMsgQueue, &msg,
							hNode->uTimeout);
		if (DSP_SUCCEEDED(status)) {
			/*  Wait on synchronization object that will be
			 *  posted in the callback on receiving RMS_EXIT
			 *  message, or by NODE_Delete. Check for valid hNode,
			 *  in case posted by NODE_Delete().  */
			status = SYNC_WaitOnEvent(hNode->hSyncDone,
							killTimeOut/2);
			if (DSP_FAILED(status)) {
				if (status == DSP_ETIMEOUT) {
					status = (*pIntfFxns->pfnMsgPut)
						 (hNode->hMsgQueue, &killmsg,
						 hNode->uTimeout);
					if (DSP_SUCCEEDED(status)) {
						status = SYNC_WaitOnEvent
							(hNode->hSyncDone,
							killTimeOut/2);
						if (DSP_FAILED(status)) {
							/* Here it goes the part
							* of the simulation of
							* the DSP exception */
						    DEV_GetDehMgr(hNodeMgr->
							hDevObject, &hDehMgr);
						    if (hDehMgr) {
							(*pIntfFxns->
							pfnDehNotify)(hDehMgr,
							DSP_SYSERROR,
							DSP_EXCEPTIONABORT);
							    status = DSP_EFAIL;
						    }
						} else
						    status = DSP_SOK;
					}
				} else
					status = DSP_EFAIL;
			} else 	/* Convert SYNC status to DSP status */
				status = DSP_SOK;
		}
	}
func_cont:
	if (DSP_SUCCEEDED(status)) {
		/* Enter CS before getting exit status, in case node was
		 * deleted. */
		status = SYNC_EnterCS(hNodeMgr->hSync);
		/* Make sure node wasn't deleted while we blocked */
		if (!MEM_IsValidHandle(hNode, NODE_SIGNATURE)) {
			status = DSP_EFAIL;
		} else {
			*pStatus = hNode->nExitStatus;
			GT_1trace(NODE_debugMask, GT_ENTER,
				 "NODE_Terminate: env = 0x%x "
				 "succeeded.\n", hNode->nodeEnv);
		}
		(void)SYNC_LeaveCS(hNodeMgr->hSync);
	}		/*End of SYNC_EnterCS */
func_end:
	return status;
}

/*
 *  ======== DeleteNode ========
 *  Purpose:
 *      Free GPP resources allocated in NODE_Allocate() or NODE_Connect().
 */
static void DeleteNode(struct NODE_OBJECT *hNode,
		struct PROCESS_CONTEXT *pr_ctxt)
{
	struct NODE_MGR *hNodeMgr;
	struct CMM_XLATOROBJECT *hXlator;
	struct WMD_DRV_INTERFACE *pIntfFxns;
	u32 i;
	enum NODE_TYPE nodeType;
	struct STREAM stream;
	struct NODE_MSGARGS msgArgs;
	struct NODE_TASKARGS taskArgs;
#ifdef DSP_DMM_DEBUG
	struct DMM_OBJECT *hDmmMgr;
	struct PROC_OBJECT *pProcObject =
			(struct PROC_OBJECT *)hNode->hProcessor;
#endif
	DSP_STATUS status;
	DBC_Require(MEM_IsValidHandle(hNode, NODE_SIGNATURE));
	hNodeMgr = hNode->hNodeMgr;
       if (!MEM_IsValidHandle(hNodeMgr, NODEMGR_SIGNATURE))
               return;
	hXlator = hNode->hXlator;
	nodeType = NODE_GetType(hNode);
	if (nodeType != NODE_DEVICE) {
		msgArgs = hNode->createArgs.asa.msgArgs;
		if (msgArgs.pData)
			MEM_Free(msgArgs.pData);

		/* Free MSG queue */
		if (hNode->hMsgQueue) {
			pIntfFxns = hNodeMgr->pIntfFxns;
			(*pIntfFxns->pfnMsgDeleteQueue) (hNode->hMsgQueue);
                       hNode->hMsgQueue = NULL;

		}
		if (hNode->hSyncDone)
			(void) SYNC_CloseEvent(hNode->hSyncDone);

		/* Free all stream info */
		if (hNode->inputs) {
			for (i = 0; i < MaxInputs(hNode); i++) {
				stream = hNode->inputs[i];
				FreeStream(hNodeMgr, stream);
			}
			MEM_Free(hNode->inputs);
                       hNode->inputs = NULL;
		}
		if (hNode->outputs) {
			for (i = 0; i < MaxOutputs(hNode); i++) {
				stream = hNode->outputs[i];
				FreeStream(hNodeMgr, stream);
			}
			MEM_Free(hNode->outputs);
                       hNode->outputs = NULL;
		}
		taskArgs = hNode->createArgs.asa.taskArgs;
		if (taskArgs.strmInDef) {
			for (i = 0; i < MaxInputs(hNode); i++) {
				if (taskArgs.strmInDef[i].szDevice) {
					MEM_Free(taskArgs.strmInDef[i].
						szDevice);
                                       taskArgs.strmInDef[i].szDevice = NULL;
				}
			}
			MEM_Free(taskArgs.strmInDef);
			taskArgs.strmInDef = NULL;
		}
		if (taskArgs.strmOutDef) {
			for (i = 0; i < MaxOutputs(hNode); i++) {
				if (taskArgs.strmOutDef[i].szDevice) {
					MEM_Free(taskArgs.strmOutDef[i].
						szDevice);
                                       taskArgs.strmOutDef[i].szDevice = NULL;
				}
			}
			MEM_Free(taskArgs.strmOutDef);
			taskArgs.strmOutDef = NULL;
		}
		if (taskArgs.uDSPHeapResAddr) {
			status = PROC_UnMap(hNode->hProcessor,
					(void *)taskArgs.uDSPHeapAddr,
					pr_ctxt);
			if (DSP_SUCCEEDED(status)) {
				GT_0trace(NODE_debugMask, GT_5CLASS,
					 "DSPProcessor_UnMap succeeded.\n");
			} else {
				GT_1trace(NODE_debugMask, GT_5CLASS,
					 "DSPProcessor_UnMap failed."
					 " Status = 0x%x\n", (u32)status);
			}
			status = PROC_UnReserveMemory(hNode->hProcessor,
					(void *)taskArgs.uDSPHeapResAddr);
			if (DSP_SUCCEEDED(status)) {
				GT_0trace(NODE_debugMask, GT_5CLASS,
					 "DSPProcessor_UnReserveMemory "
					 "succeeded.\n");
			} else {
				GT_1trace(NODE_debugMask, GT_5CLASS,
					 "DSPProcessor_UnReserveMemory "
					 "failed. Status = 0x%x\n",
					 (u32)status);
			}
#ifdef DSP_DMM_DEBUG
			status = DMM_GetHandle(pProcObject, &hDmmMgr);
			if (DSP_SUCCEEDED(status))
				DMM_MemMapDump(hDmmMgr);
#endif
		}
	}
	if (nodeType != NODE_MESSAGE) {
               if (hNode->streamConnect) {
			MEM_Free(hNode->streamConnect);
                       hNode->streamConnect = NULL;
               }
	}
       if (hNode->pstrDevName) {
		MEM_Free(hNode->pstrDevName);
               hNode->pstrDevName = NULL;
       }

       if (hNode->hNtfy) {
		NTFY_Delete(hNode->hNtfy);
               hNode->hNtfy = NULL;
       }

	/* These were allocated in DCD_GetObjectDef (via NODE_Allocate) */
       if (hNode->dcdProps.objData.nodeObj.pstrCreatePhaseFxn) {
		MEM_Free(hNode->dcdProps.objData.nodeObj.pstrCreatePhaseFxn);
               hNode->dcdProps.objData.nodeObj.pstrCreatePhaseFxn = NULL;
       }

       if (hNode->dcdProps.objData.nodeObj.pstrExecutePhaseFxn) {
		MEM_Free(hNode->dcdProps.objData.nodeObj.pstrExecutePhaseFxn);
               hNode->dcdProps.objData.nodeObj.pstrExecutePhaseFxn = NULL;
       }

       if (hNode->dcdProps.objData.nodeObj.pstrDeletePhaseFxn) {
		MEM_Free(hNode->dcdProps.objData.nodeObj.pstrDeletePhaseFxn);
               hNode->dcdProps.objData.nodeObj.pstrDeletePhaseFxn = NULL;
       }

       if (hNode->dcdProps.objData.nodeObj.pstrIAlgName) {
		MEM_Free(hNode->dcdProps.objData.nodeObj.pstrIAlgName);
               hNode->dcdProps.objData.nodeObj.pstrIAlgName = NULL;
       }

	/* Free all SM address translator resources */
       if (hXlator) {
		(void) CMM_XlatorDelete(hXlator, TRUE);	/* force free */
               hXlator = NULL;
       }

       if (hNode->hNldrNode) {
		hNodeMgr->nldrFxns.pfnFree(hNode->hNldrNode);
               hNode->hNldrNode = NULL;
       }

	MEM_FreeObject(hNode);
       hNode = NULL;
}

/*
 *  ======== DeleteNodeMgr ========
 *  Purpose:
 *      Frees the node manager.
 */
static void DeleteNodeMgr(struct NODE_MGR *hNodeMgr)
{
	struct NODE_OBJECT *hNode;

	if (MEM_IsValidHandle(hNodeMgr, NODEMGR_SIGNATURE)) {
		/* Free resources */
		if (hNodeMgr->hDcdMgr)
			DCD_DestroyManager(hNodeMgr->hDcdMgr);

		/* Remove any elements remaining in lists */
		if (hNodeMgr->nodeList) {
			while ((hNode =
				(struct NODE_OBJECT *)LST_GetHead(hNodeMgr->
				nodeList)))
					DeleteNode(hNode, NULL);

			DBC_Assert(LST_IsEmpty(hNodeMgr->nodeList));
			LST_Delete(hNodeMgr->nodeList);
		}
		if (hNodeMgr->hNtfy)
			NTFY_Delete(hNodeMgr->hNtfy);

		if (hNodeMgr->pipeMap)
			GB_delete(hNodeMgr->pipeMap);

		if (hNodeMgr->pipeDoneMap)
			GB_delete(hNodeMgr->pipeDoneMap);

		if (hNodeMgr->chnlMap)
			GB_delete(hNodeMgr->chnlMap);

		if (hNodeMgr->dmaChnlMap)
			GB_delete(hNodeMgr->dmaChnlMap);

		if (hNodeMgr->zChnlMap)
			GB_delete(hNodeMgr->zChnlMap);

		if (hNodeMgr->hDisp)
			DISP_Delete(hNodeMgr->hDisp);

		if (hNodeMgr->hSync)
			SYNC_DeleteCS(hNodeMgr->hSync);

		if (hNodeMgr->hStrmMgr)
			STRM_Delete(hNodeMgr->hStrmMgr);

		/* Delete the loader */
		if (hNodeMgr->hNldr)
			hNodeMgr->nldrFxns.pfnDelete(hNodeMgr->hNldr);

		if (hNodeMgr->fLoaderInit)
			hNodeMgr->nldrFxns.pfnExit();

		MEM_FreeObject(hNodeMgr);
	}
}

/*
 *  ======== FillStreamConnect ========
 *  Purpose:
 *      Fills stream information.
 */
static void FillStreamConnect(struct NODE_OBJECT *hNode1,
			     struct NODE_OBJECT *hNode2,
			     u32 uStream1, u32 uStream2)
{
	u32 uStrmIndex;
	struct DSP_STREAMCONNECT *pStrm1 = NULL;
	struct DSP_STREAMCONNECT *pStrm2 = NULL;
	enum NODE_TYPE node1Type = NODE_TASK;
	enum NODE_TYPE node2Type = NODE_TASK;

	node1Type = NODE_GetType(hNode1);
	node2Type = NODE_GetType(hNode2);
	if (hNode1 != (struct NODE_OBJECT *)DSP_HGPPNODE) {

		if (node1Type != NODE_DEVICE) {
			uStrmIndex = hNode1->uNumInputs +
				     hNode1->uNumOutputs - 1;
			pStrm1 = &(hNode1->streamConnect[uStrmIndex]);
			pStrm1->cbStruct = sizeof(struct DSP_STREAMCONNECT);
			pStrm1->uThisNodeStreamIndex = uStream1;
		}

		if (hNode2 != (struct NODE_OBJECT *)DSP_HGPPNODE) {
				/* NODE == > NODE */
			if (node1Type != NODE_DEVICE) {
				pStrm1->hConnectedNode = hNode2;
				pStrm1->uiConnectedNodeID = hNode2->nodeId;
				pStrm1->uConnectedNodeStreamIndex = uStream2;
				pStrm1->lType = CONNECTTYPE_NODEOUTPUT;
			}
			if (node2Type != NODE_DEVICE) {
				uStrmIndex = hNode2->uNumInputs +
						hNode2->uNumOutputs - 1;
				pStrm2 = &(hNode2->streamConnect[uStrmIndex]);
				pStrm2->cbStruct =
					sizeof(struct DSP_STREAMCONNECT);
				pStrm2->uThisNodeStreamIndex = uStream2;
				pStrm2->hConnectedNode = hNode1;
				pStrm2->uiConnectedNodeID = hNode1->nodeId;
				pStrm2->uConnectedNodeStreamIndex = uStream1;
				pStrm2->lType = CONNECTTYPE_NODEINPUT;
			}
		} else if (node1Type != NODE_DEVICE)
				pStrm1->lType = CONNECTTYPE_GPPOUTPUT;
	} else {
		/* GPP == > NODE */
		DBC_Assert(hNode2 != (struct NODE_OBJECT *)DSP_HGPPNODE);
		uStrmIndex = hNode2->uNumInputs + hNode2->uNumOutputs - 1;
		pStrm2 = &(hNode2->streamConnect[uStrmIndex]);
		pStrm2->cbStruct = sizeof(struct DSP_STREAMCONNECT);
		pStrm2->uThisNodeStreamIndex = uStream2;
		pStrm2->lType = CONNECTTYPE_GPPINPUT;
	}
}

/*
 *  ======== FillStreamDef ========
 *  Purpose:
 *      Fills Stream attributes.
 */
static void FillStreamDef(struct NODE_OBJECT *hNode,
			  struct NODE_STRMDEF *pstrmDef,
			  struct DSP_STRMATTR *pAttrs)
{
	struct NODE_MGR *hNodeMgr = hNode->hNodeMgr;

	if (pAttrs != NULL) {
		pstrmDef->uNumBufs = pAttrs->uNumBufs;
		pstrmDef->uBufsize = pAttrs->uBufsize / hNodeMgr->
							uDSPDataMauSize;
		pstrmDef->uSegid = pAttrs->uSegid;
		pstrmDef->uAlignment = pAttrs->uAlignment;
		pstrmDef->uTimeout = pAttrs->uTimeout;
	} else {
		pstrmDef->uNumBufs = DEFAULTNBUFS;
		pstrmDef->uBufsize = DEFAULTBUFSIZE / hNodeMgr->
						      uDSPDataMauSize;
		pstrmDef->uSegid = DEFAULTSEGID;
		pstrmDef->uAlignment = DEFAULTALIGNMENT;
		pstrmDef->uTimeout = DEFAULTTIMEOUT;
	}
}

/*
 *  ======== FreeStream ========
 *  Purpose:
 *      Updates the channel mask and frees the pipe id.
 */
static void FreeStream(struct NODE_MGR *hNodeMgr, struct STREAM stream)
{
	/* Free up the pipe id unless other node has not yet been deleted. */
	if (stream.type == NODECONNECT) {
		if (GB_test(hNodeMgr->pipeDoneMap, stream.devId)) {
			/* The other node has already been deleted */
			GB_clear(hNodeMgr->pipeDoneMap, stream.devId);
			GB_clear(hNodeMgr->pipeMap, stream.devId);
		} else {
			/* The other node has not been deleted yet */
			GB_set(hNodeMgr->pipeDoneMap, stream.devId);
		}
	} else if (stream.type == HOSTCONNECT) {
		if (stream.devId < hNodeMgr->ulNumChnls) {
			GB_clear(hNodeMgr->chnlMap, stream.devId);
		} else if (stream.devId < (2 * hNodeMgr->ulNumChnls)) {
			/* dsp-dma */
			GB_clear(hNodeMgr->dmaChnlMap, stream.devId -
				(1 * hNodeMgr->ulNumChnls));
		} else if (stream.devId < (3 * hNodeMgr->ulNumChnls)) {
			/* zero-copy */
			GB_clear(hNodeMgr->zChnlMap, stream.devId -
				(2 * hNodeMgr->ulNumChnls));
		}
	}
}

/*
 *  ======== GetFxnAddress ========
 *  Purpose:
 *      Retrieves the address for create, execute or delete phase for a node.
 */
static DSP_STATUS GetFxnAddress(struct NODE_OBJECT *hNode, u32 *pulFxnAddr,
				u32 uPhase)
{
	char *pstrFxnName = NULL;
	struct NODE_MGR *hNodeMgr = hNode->hNodeMgr;
	DSP_STATUS status = DSP_SOK;
	DBC_Require(NODE_GetType(hNode) == NODE_TASK ||
			NODE_GetType(hNode) == NODE_DAISSOCKET ||
			NODE_GetType(hNode) == NODE_MESSAGE);

	switch (uPhase) {
	case CREATEPHASE:
		pstrFxnName = hNode->dcdProps.objData.nodeObj.
			      pstrCreatePhaseFxn;
		break;
	case EXECUTEPHASE:
		pstrFxnName = hNode->dcdProps.objData.nodeObj.
			      pstrExecutePhaseFxn;
		break;
	case DELETEPHASE:
		pstrFxnName = hNode->dcdProps.objData.nodeObj.
			      pstrDeletePhaseFxn;
		break;
	default:
		/* Should never get here */
		DBC_Assert(false);
		break;
	}

	status = hNodeMgr->nldrFxns.pfnGetFxnAddr(hNode->hNldrNode, pstrFxnName,
						pulFxnAddr);

	return status;
}

/*
 *  ======== GetNodeInfo ========
 *  Purpose:
 *      Retrieves the node information.
 */
void GetNodeInfo(struct NODE_OBJECT *hNode, struct DSP_NODEINFO *pNodeInfo)
{
	u32 i;

	DBC_Require(MEM_IsValidHandle(hNode, NODE_SIGNATURE));
	DBC_Require(pNodeInfo != NULL);

	pNodeInfo->cbStruct = sizeof(struct DSP_NODEINFO);
	pNodeInfo->nbNodeDatabaseProps = hNode->dcdProps.objData.nodeObj.
					 ndbProps;
	pNodeInfo->uExecutionPriority = hNode->nPriority;
	pNodeInfo->hDeviceOwner = hNode->hDeviceOwner;
	pNodeInfo->uNumberStreams = hNode->uNumInputs + hNode->uNumOutputs;
	pNodeInfo->uNodeEnv = hNode->nodeEnv;

	pNodeInfo->nsExecutionState = NODE_GetState(hNode);

	/* Copy stream connect data */
	for (i = 0; i < hNode->uNumInputs + hNode->uNumOutputs; i++)
		pNodeInfo->scStreamConnection[i] = hNode->streamConnect[i];

}

/*
 *  ======== GetNodeProps ========
 *  Purpose:
 *      Retrieve node properties.
 */
static DSP_STATUS GetNodeProps(struct DCD_MANAGER *hDcdMgr,
			      struct NODE_OBJECT *hNode,
			      CONST struct DSP_UUID *pNodeId,
			      struct DCD_GENERICOBJ *pdcdProps)
{
	u32 uLen;
	struct NODE_MSGARGS *pMsgArgs;
	struct NODE_TASKARGS *pTaskArgs;
	enum NODE_TYPE nodeType = NODE_TASK;
	struct DSP_NDBPROPS *pndbProps = &(pdcdProps->objData.nodeObj.ndbProps);
	DSP_STATUS status = DSP_SOK;
#ifdef DEBUG
	char szUuid[MAXUUIDLEN];
#endif

	status = DCD_GetObjectDef(hDcdMgr, (struct DSP_UUID *)pNodeId,
				 DSP_DCDNODETYPE, pdcdProps);

	if (DSP_SUCCEEDED(status)) {
		hNode->nType = nodeType = pndbProps->uNodeType;

#ifdef DEBUG
		/* Create UUID value to set in registry. */
		UUID_UuidToString((struct DSP_UUID *)pNodeId, szUuid,
				 MAXUUIDLEN);
		DBG_Trace(DBG_LEVEL7, "\n** (node) UUID: %s\n", szUuid);
#endif

		/* Fill in message args that come from NDB */
		if (nodeType != NODE_DEVICE) {
			pMsgArgs = &(hNode->createArgs.asa.msgArgs);
			pMsgArgs->uSegid = pdcdProps->objData.nodeObj.uMsgSegid;
			pMsgArgs->uNotifyType = pdcdProps->objData.nodeObj.
						uMsgNotifyType;
			pMsgArgs->uMaxMessages = pndbProps->uMessageDepth;
#ifdef DEBUG
			DBG_Trace(DBG_LEVEL7,
				 "** (node) Max Number of Messages: 0x%x\n",
				 pMsgArgs->uMaxMessages);
#endif
		} else {
			/* Copy device name */
                       DBC_Require(pndbProps->acName);
                       uLen = strlen(pndbProps->acName);
			DBC_Assert(uLen < MAXDEVNAMELEN);
			hNode->pstrDevName = MEM_Calloc(uLen + 1, MEM_PAGED);
			if (hNode->pstrDevName == NULL) {
				status = DSP_EMEMORY;
			} else {
                               strncpy(hNode->pstrDevName,
					   pndbProps->acName, uLen);
			}
		}
	}
	if (DSP_SUCCEEDED(status)) {
		/* Fill in create args that come from NDB */
		if (nodeType == NODE_TASK || nodeType == NODE_DAISSOCKET) {
			pTaskArgs = &(hNode->createArgs.asa.taskArgs);
			pTaskArgs->nPriority = pndbProps->iPriority;
			pTaskArgs->uStackSize = pndbProps->uStackSize;
			pTaskArgs->uSysStackSize = pndbProps->uSysStackSize;
			pTaskArgs->uStackSeg = pndbProps->uStackSeg;
#ifdef DEBUG
			DBG_Trace(DBG_LEVEL7,
				 "** (node) Priority: 0x%x\n" "** (node) Stack"
				 " Size: 0x%x words\n" "** (node) System Stack"
				 " Size: 0x%x words\n" "** (node) Stack"
				 " Segment: 0x%x\n\n",
				  "** (node) profile count : 0x%x \n \n",
				  pTaskArgs->nPriority, pTaskArgs->uStackSize,
				  pTaskArgs->uSysStackSize,
				  pTaskArgs->uStackSeg,
				  pndbProps->uCountProfiles);
#endif
		}
	}

	return status;
}

/*
 *  ======== GetProcProps ========
 *  Purpose:
 *      Retrieve the processor properties.
 */
static DSP_STATUS GetProcProps(struct NODE_MGR *hNodeMgr,
				struct DEV_OBJECT *hDevObject)
{
	struct CFG_DEVNODE *hDevNode;
	struct CFG_HOSTRES hostRes;
	DSP_STATUS status = DSP_SOK;

	status = DEV_GetDevNode(hDevObject, &hDevNode);
	if (DSP_SUCCEEDED(status))
		status = CFG_GetHostResources(hDevNode, &hostRes);

	if (DSP_SUCCEEDED(status)) {
		hNodeMgr->ulChnlOffset = hostRes.dwChnlOffset;
		hNodeMgr->ulChnlBufSize = hostRes.dwChnlBufSize;
		hNodeMgr->ulNumChnls = hostRes.dwNumChnls;

		/*
		 *  PROC will add an API to get DSP_PROCESSORINFO.
		 *  Fill in default values for now.
		 */
		/* TODO -- Instead of hard coding, take from registry */
		hNodeMgr->procFamily = 6000;
		hNodeMgr->procType = 6410;
		hNodeMgr->nMinPri = DSP_NODE_MIN_PRIORITY;
		hNodeMgr->nMaxPri = DSP_NODE_MAX_PRIORITY;
		hNodeMgr->uDSPWordSize = DSPWORDSIZE;
		hNodeMgr->uDSPDataMauSize = DSPWORDSIZE;
		hNodeMgr->uDSPMauSize = 1;

	}
	return status;
}



/*
 *  ======== NODE_GetUUIDProps ========
 *  Purpose:
 *      Fetch Node UUID properties from DCD/DOF file.
 */
DSP_STATUS NODE_GetUUIDProps(DSP_HPROCESSOR hProcessor,
			    IN CONST struct DSP_UUID *pNodeId,
			    OUT struct DSP_NDBPROPS *pNodeProps)
{
	struct NODE_MGR *hNodeMgr = NULL;
	struct DEV_OBJECT *hDevObject;
	DSP_STATUS status = DSP_SOK;
	struct DCD_NODEPROPS   dcdNodeProps;
	struct DSP_PROCESSORSTATE procStatus;

	DBC_Require(cRefs > 0);
	DBC_Require(hProcessor != NULL);
	DBC_Require(pNodeId != NULL);

	if (hProcessor == NULL || pNodeId == NULL) {
		status = DSP_EHANDLE;
		goto func_end;
	}
	status = PROC_GetState(hProcessor, &procStatus,
			sizeof(struct DSP_PROCESSORSTATE));
	if (DSP_FAILED(status))
		goto func_end;
	/* If processor is in error state then don't attempt
	    to send the message */
	if (procStatus.iState == PROC_ERROR) {
		GT_1trace(NODE_debugMask, GT_5CLASS,
			"NODE_GetUUIDProps: proc Status 0x%x\n",
			procStatus.iState);
		status = DSP_EFAIL;
		goto func_end;
	}

	GT_3trace(NODE_debugMask, GT_ENTER,
		 "NODE_GetUUIDProps: " "\thProcessor: "
		 "0x%x\tpNodeId: 0x%x" "\tpNodeProps: 0x%x\n", hProcessor,
		 pNodeId, pNodeProps);

	status = PROC_GetDevObject(hProcessor, &hDevObject);
	if (DSP_SUCCEEDED(status) && hDevObject != NULL) {
		status = DEV_GetNodeManager(hDevObject, &hNodeMgr);
		if (hNodeMgr == NULL) {
			status = DSP_EHANDLE;
			goto func_end;
		}
	}

	/*
	 * Enter the critical section. This is needed because
	 * DCD_GetObjectDef will ultimately end up calling DBLL_open/close,
	 * which needs to be protected in order to not corrupt the zlib manager
	 * (COD).
	 */
	status = SYNC_EnterCS(hNodeMgr->hSync);

	if (DSP_SUCCEEDED(status)) {
		dcdNodeProps.pstrCreatePhaseFxn = NULL;
		dcdNodeProps.pstrExecutePhaseFxn = NULL;
		dcdNodeProps.pstrDeletePhaseFxn = NULL;
		dcdNodeProps.pstrIAlgName = NULL;

		status = DCD_GetObjectDef(hNodeMgr->hDcdMgr,
				(struct DSP_UUID *) pNodeId,
				DSP_DCDNODETYPE,
				(struct DCD_GENERICOBJ *) &dcdNodeProps);
		if (DSP_SUCCEEDED(status)) {
			*pNodeProps = dcdNodeProps.ndbProps;
			if (dcdNodeProps.pstrCreatePhaseFxn)
				MEM_Free(dcdNodeProps.pstrCreatePhaseFxn);

			if (dcdNodeProps.pstrExecutePhaseFxn)
				MEM_Free(dcdNodeProps.pstrExecutePhaseFxn);

			if (dcdNodeProps.pstrDeletePhaseFxn)
				MEM_Free(dcdNodeProps.pstrDeletePhaseFxn);

			if (dcdNodeProps.pstrIAlgName)
				MEM_Free(dcdNodeProps.pstrIAlgName);
		}
		/*  Leave the critical section, we're done.  */
		(void)SYNC_LeaveCS(hNodeMgr->hSync);
	}
func_end:
	return status;
}

/*
 *  ======== GetRMSFxns ========
 *  Purpose:
 *      Retrieve the RMS functions.
 */
static DSP_STATUS GetRMSFxns(struct NODE_MGR *hNodeMgr)
{
	s32 i;
	struct DEV_OBJECT *hDev = hNodeMgr->hDevObject;
	DSP_STATUS status = DSP_SOK;

	static char *pszFxns[NUMRMSFXNS] = {
		"RMS_queryServer", 	/* RMSQUERYSERVER */
		"RMS_configureServer", 	/* RMSCONFIGURESERVER */
		"RMS_createNode", 	/* RMSCREATENODE */
		"RMS_executeNode", 	/* RMSEXECUTENODE */
		"RMS_deleteNode", 	/* RMSDELETENODE */
		"RMS_changeNodePriority", 	/* RMSCHANGENODEPRIORITY */
		"RMS_readMemory", 	/* RMSREADMEMORY */
		"RMS_writeMemory", 	/* RMSWRITEMEMORY */
		"RMS_copy", 	/* RMSCOPY */
	};

	for (i = 0; i < NUMRMSFXNS; i++) {
		status = DEV_GetSymbol(hDev, pszFxns[i],
			 &(hNodeMgr->ulFxnAddrs[i]));
		if (DSP_FAILED(status)) {
			if (status == COD_E_SYMBOLNOTFOUND) {
				/*
				 *  May be loaded dynamically (in the future),
				 *  but return an error for now.
				 */
				GT_1trace(NODE_debugMask, GT_6CLASS,
					 "RMS function: %s "
					 "currently not loaded\n", pszFxns[i]);
			} else {
				GT_2trace(NODE_debugMask, GT_6CLASS,
					 "GetRMSFxns: Symbol not "
					 "found: %s\tstatus = 0x%x\n",
					 pszFxns[i], status);
				break;
			}
		}
	}

	return status;
}

/*
 *  ======== Ovly ========
 *  Purpose:
 *      Called during overlay.Sends command to RMS to copy a block of data.
 */
static u32 Ovly(void *pPrivRef, u32 ulDspRunAddr, u32 ulDspLoadAddr,
			u32 ulNumBytes, u32 nMemSpace)
{
	struct NODE_OBJECT *hNode = (struct NODE_OBJECT *)pPrivRef;
	struct NODE_MGR *hNodeMgr;
	u32 ulBytes = 0;
	u32 ulSize;
	u32 ulTimeout;
	DSP_STATUS status = DSP_SOK;
	struct WMD_DEV_CONTEXT *hWmdContext;
	struct WMD_DRV_INTERFACE *pIntfFxns;	/* Function interface to WMD */

	DBC_Require(MEM_IsValidHandle(hNode, NODE_SIGNATURE));

	hNodeMgr = hNode->hNodeMgr;

	ulSize = ulNumBytes / hNodeMgr->uDSPWordSize;
	ulTimeout = hNode->uTimeout;

	/* Call new MemCopy function */
	pIntfFxns = hNodeMgr->pIntfFxns;
	status = DEV_GetWMDContext(hNodeMgr->hDevObject, &hWmdContext);
	status = (*pIntfFxns->pfnBrdMemCopy)(hWmdContext, ulDspRunAddr,
		 ulDspLoadAddr,	ulNumBytes, (u32) nMemSpace);

	if (DSP_SUCCEEDED(status))
		ulBytes = ulNumBytes;

	return ulBytes;
}

/*
 *  ======== Write ========
 */
static u32 Write(void *pPrivRef, u32 ulDspAddr, void *pBuf,
			u32 ulNumBytes, u32 nMemSpace)
{
	struct NODE_OBJECT *hNode = (struct NODE_OBJECT *) pPrivRef;
	struct NODE_MGR *hNodeMgr;
	u16 memType;
	u32 ulTimeout;
	DSP_STATUS status = DSP_SOK;
	struct WMD_DEV_CONTEXT *hWmdContext;
	struct WMD_DRV_INTERFACE *pIntfFxns;	/* Function interface to WMD */

	DBC_Require(MEM_IsValidHandle(hNode, NODE_SIGNATURE));
	DBC_Require(nMemSpace & DBLL_CODE || nMemSpace & DBLL_DATA);

	hNodeMgr = hNode->hNodeMgr;

	ulTimeout = hNode->uTimeout;
	memType = (nMemSpace & DBLL_CODE) ? RMS_CODE : RMS_DATA;

	/* Call new MemWrite function */
	pIntfFxns = hNodeMgr->pIntfFxns;
	status = DEV_GetWMDContext(hNodeMgr->hDevObject, &hWmdContext);
	status = (*pIntfFxns->pfnBrdMemWrite) (hWmdContext, pBuf, ulDspAddr,
		 ulNumBytes, memType);

	return ulNumBytes;
}

