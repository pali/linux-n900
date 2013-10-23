/*
 * disp.c
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
 *  ======== disp.c ========
 *
 *  Description:
 *      Node Dispatcher interface. Communicates with Resource Manager Server
 *      (RMS) on DSP. Access to RMS is synchronized in NODE.
 *
 *  Public Functions:
 *      DISP_Create
 *      DISP_Delete
 *      DISP_Exit
 *      DISP_Init
 *      DISP_NodeChangePriority
 *      DISP_NodeCreate
 *      DISP_NodeDelete
 *      DISP_NodePause
 *      DISP_NodeRun
 *
 *! Revision History:
 *! =================
 *! 18-Feb-2003 vp      Code review updates
 *! 18-Oct-2002 vp      Ported to Linux platform
 *! 16-May-2002 jeh     Added DISP_DoCinit().
 *! 24-Apr-2002 jeh     Added DISP_MemWrite().
 *! 13-Feb-2002 jeh     Pass system stack size to RMS.
 *! 16-Jan-2002  ag     Added bufsize param to _ChnlAddIOReq() fxn
 *! 10-May-2001 jeh     Code Review cleanup.
 *! 26-Sep-2000 jeh     Fixed status values in SendMessage().
 *! 19-Jun-2000 jeh     Created.
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

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/dev.h>
#include <dspbridge/mem.h>
#include <dspbridge/sync.h>
#include <dspbridge/csl.h>

/*  ----------------------------------- Link Driver */
#include <dspbridge/wmd.h>

/*  ----------------------------------- Platform Manager */
#include <dspbridge/dev.h>
#include <dspbridge/chnldefs.h>

/*  ----------------------------------- Resource Manager */
#include <dspbridge/nodedefs.h>
#include <dspbridge/nodepriv.h>
#include <dspbridge/rms_sh.h>

/*  ----------------------------------- This */
#include <dspbridge/disp.h>

#define DISP_SIGNATURE       0x50534944	/* "PSID" */

/* Size of a reply from RMS */
#define REPLYSIZE (3 * sizeof(RMS_WORD))

/* Reserved channel offsets for communication with RMS */
#define CHNLTORMSOFFSET       0
#define CHNLFROMRMSOFFSET     1

#define CHNLIOREQS      1

#define SwapWord(x)     (((u32)(x) >> 16) | ((u32)(x) << 16))

/*
 *  ======== DISP_OBJECT ========
 */
struct DISP_OBJECT {
	u32 dwSignature; 	/* Used for object validation */
	struct DEV_OBJECT *hDevObject; 	/* Device for this processor */
	struct WMD_DRV_INTERFACE *pIntfFxns; 	/* Function interface to WMD */
	struct CHNL_MGR *hChnlMgr; 	/* Channel manager */
	struct CHNL_OBJECT *hChnlToDsp;   /* Channel for commands to RMS */
	struct CHNL_OBJECT *hChnlFromDsp;   /* Channel for replies from RMS */
	u8 *pBuf; 		/* Buffer for commands, replies */
	u32 ulBufsize; 	/* pBuf size in bytes */
	u32 ulBufsizeRMS; 	/* pBuf size in RMS words */
	u32 uCharSize; 		/* Size of DSP character */
	u32 uWordSize; 		/* Size of DSP word */
	u32 uDataMauSize; 	/* Size of DSP Data MAU */
};

static u32 cRefs;

/* Debug msgs: */
#if GT_TRACE
static struct GT_Mask DISP_DebugMask = { NULL, NULL };
#endif

static void DeleteDisp(struct DISP_OBJECT *hDisp);
static DSP_STATUS FillStreamDef(RMS_WORD *pdwBuf, u32 *ptotal, u32 offset,
				struct NODE_STRMDEF strmDef, u32 max,
				u32 uCharsInRMSWord);
static DSP_STATUS SendMessage(struct DISP_OBJECT *hDisp, u32 dwTimeout,
			     u32 ulBytes, OUT u32 *pdwArg);

/*
 *  ======== DISP_Create ========
 *  Create a NODE Dispatcher object.
 */
DSP_STATUS DISP_Create(OUT struct DISP_OBJECT **phDispObject,
		      struct DEV_OBJECT *hDevObject,
		      IN CONST struct DISP_ATTRS *pDispAttrs)
{
	struct DISP_OBJECT *pDisp;
	struct WMD_DRV_INTERFACE *pIntfFxns;
	u32 ulChnlId;
	struct CHNL_ATTRS chnlAttrs;
	DSP_STATUS status = DSP_SOK;
	u32 devType;

	DBC_Require(cRefs > 0);
	DBC_Require(phDispObject != NULL);
	DBC_Require(pDispAttrs != NULL);
	DBC_Require(hDevObject != NULL);

	GT_3trace(DISP_DebugMask, GT_ENTER, "DISP_Create: phDispObject: 0x%x\t"
		 "hDevObject: 0x%x\tpDispAttrs: 0x%x\n", phDispObject,
		 hDevObject, pDispAttrs);

	*phDispObject = NULL;

	/* Allocate Node Dispatcher object */
	MEM_AllocObject(pDisp, struct DISP_OBJECT, DISP_SIGNATURE);
	if (pDisp == NULL) {
		status = DSP_EMEMORY;
		GT_0trace(DISP_DebugMask, GT_6CLASS,
			 "DISP_Create: MEM_AllocObject() failed!\n");
	} else {
		pDisp->hDevObject = hDevObject;
	}

	/* Get Channel manager and WMD function interface */
	if (DSP_SUCCEEDED(status)) {
		status = DEV_GetChnlMgr(hDevObject, &(pDisp->hChnlMgr));
		if (DSP_SUCCEEDED(status)) {
			(void) DEV_GetIntfFxns(hDevObject, &pIntfFxns);
			pDisp->pIntfFxns = pIntfFxns;
		} else {
			GT_1trace(DISP_DebugMask, GT_6CLASS,
				 "DISP_Create: Failed to get "
				 "channel manager! status = 0x%x\n", status);
		}
	}

	/* check device type and decide if streams or messag'ing is used for
	 * RMS/EDS */
	if (DSP_FAILED(status))
		goto func_cont;

	status = DEV_GetDevType(hDevObject, &devType);
	GT_1trace(DISP_DebugMask, GT_6CLASS, "DISP_Create: Creating DISP for "
		 "device = 0x%x\n", devType);
	if (DSP_FAILED(status))
		goto func_cont;

	if (devType != DSP_UNIT) {
		GT_0trace(DISP_DebugMask, GT_6CLASS,
			 "DISP_Create: Unkown device "
			 "type in Device object !! \n");
		status = DSP_EFAIL;
		goto func_cont;
	}
	if (DSP_SUCCEEDED(status)) {
		pDisp->uCharSize = DSPWORDSIZE;
		pDisp->uWordSize = DSPWORDSIZE;
		pDisp->uDataMauSize = DSPWORDSIZE;
		/* Open channels for communicating with the RMS */
		chnlAttrs.uIOReqs = CHNLIOREQS;
		chnlAttrs.hEvent = NULL;
		ulChnlId = pDispAttrs->ulChnlOffset + CHNLTORMSOFFSET;
		status = (*pIntfFxns->pfnChnlOpen)(&(pDisp->hChnlToDsp),
			 pDisp->hChnlMgr, CHNL_MODETODSP, ulChnlId, &chnlAttrs);
		if (DSP_FAILED(status)) {
			GT_2trace(DISP_DebugMask, GT_6CLASS,
				 "DISP_Create:  Channel to RMS "
				 "open failed, chnl id = %d, status = 0x%x\n",
				 ulChnlId, status);
		}
	}
	if (DSP_SUCCEEDED(status)) {
		ulChnlId = pDispAttrs->ulChnlOffset + CHNLFROMRMSOFFSET;
		status = (*pIntfFxns->pfnChnlOpen)(&(pDisp->hChnlFromDsp),
			 pDisp->hChnlMgr, CHNL_MODEFROMDSP, ulChnlId,
			 &chnlAttrs);
		if (DSP_FAILED(status)) {
			GT_2trace(DISP_DebugMask, GT_6CLASS,
				 "DISP_Create: Channel from RMS "
				 "open failed, chnl id = %d, status = 0x%x\n",
				 ulChnlId, status);
		}
	}
	if (DSP_SUCCEEDED(status)) {
		/* Allocate buffer for commands, replies */
		pDisp->ulBufsize = pDispAttrs->ulChnlBufSize;
		pDisp->ulBufsizeRMS = RMS_COMMANDBUFSIZE;
		pDisp->pBuf = MEM_Calloc(pDisp->ulBufsize, MEM_PAGED);
		if (pDisp->pBuf == NULL) {
			status = DSP_EMEMORY;
			GT_0trace(DISP_DebugMask, GT_6CLASS,
				 "DISP_Create: Failed "
				 "to allocate channel buffer!\n");
		}
	}
func_cont:
	if (DSP_SUCCEEDED(status))
		*phDispObject = pDisp;
	else
		DeleteDisp(pDisp);

	DBC_Ensure(((DSP_FAILED(status)) && ((*phDispObject == NULL))) ||
		  ((DSP_SUCCEEDED(status)) &&
		  (MEM_IsValidHandle((*phDispObject), DISP_SIGNATURE))));
	return status;
}

/*
 *  ======== DISP_Delete ========
 *  Delete the NODE Dispatcher.
 */
void DISP_Delete(struct DISP_OBJECT *hDisp)
{
	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(hDisp, DISP_SIGNATURE));

	GT_1trace(DISP_DebugMask, GT_ENTER,
		 "DISP_Delete: hDisp: 0x%x\n", hDisp);

	DeleteDisp(hDisp);

	DBC_Ensure(!MEM_IsValidHandle(hDisp, DISP_SIGNATURE));
}

/*
 *  ======== DISP_Exit ========
 *  Discontinue usage of DISP module.
 */
void DISP_Exit(void)
{
	DBC_Require(cRefs > 0);

	cRefs--;

	GT_1trace(DISP_DebugMask, GT_5CLASS,
		 "Entered DISP_Exit, ref count:  0x%x\n", cRefs);

	DBC_Ensure(cRefs >= 0);
}

/*
 *  ======== DISP_Init ========
 *  Initialize the DISP module.
 */
bool DISP_Init(void)
{
	bool fRetVal = true;

	DBC_Require(cRefs >= 0);

	if (cRefs == 0) {
		DBC_Assert(!DISP_DebugMask.flags);
		GT_create(&DISP_DebugMask, "DI");  /* "DI" for DIspatcher */
	}

	if (fRetVal)
		cRefs++;

	GT_1trace(DISP_DebugMask, GT_5CLASS,
		 "DISP_Init(), ref count:  0x%x\n", cRefs);

	DBC_Ensure((fRetVal && (cRefs > 0)) || (!fRetVal && (cRefs >= 0)));
	return fRetVal;
}

/*
 *  ======== DISP_NodeChangePriority ========
 *  Change the priority of a node currently running on the target.
 */
DSP_STATUS DISP_NodeChangePriority(struct DISP_OBJECT *hDisp,
				  struct NODE_OBJECT *hNode,
				  u32 ulRMSFxn, NODE_ENV nodeEnv,
				  s32 nPriority)
{
	u32 dwArg;
	struct RMS_Command *pCommand;
	DSP_STATUS status = DSP_SOK;

	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(hDisp, DISP_SIGNATURE));
	DBC_Require(hNode != NULL);

	GT_5trace(DISP_DebugMask, GT_ENTER, "DISP_NodeChangePriority: hDisp: "
		"0x%x\thNode: 0x%x\tulRMSFxn: 0x%x\tnodeEnv: 0x%x\tnPriority\n",
		hDisp, hNode, ulRMSFxn, nodeEnv, nPriority);

	/* Send message to RMS to change priority */
	pCommand = (struct RMS_Command *)(hDisp->pBuf);
	pCommand->fxn = (RMS_WORD)(ulRMSFxn);
	pCommand->arg1 = (RMS_WORD)nodeEnv;
	pCommand->arg2 = nPriority;
	status = SendMessage(hDisp, NODE_GetTimeout(hNode),
		 sizeof(struct RMS_Command), &dwArg);
	if (DSP_FAILED(status)) {
		GT_1trace(DISP_DebugMask, GT_6CLASS,
			 "DISP_NodeChangePriority failed! "
			 "status = 0x%x\n", status);
	}
	return status;
}

/*
 *  ======== DISP_NodeCreate ========
 *  Create a node on the DSP by remotely calling the node's create function.
 */
DSP_STATUS DISP_NodeCreate(struct DISP_OBJECT *hDisp, struct NODE_OBJECT *hNode,
			  u32 ulRMSFxn, u32 ulCreateFxn,
			  IN CONST struct NODE_CREATEARGS *pArgs,
			  OUT NODE_ENV *pNodeEnv)
{
	struct NODE_MSGARGS msgArgs;
	struct NODE_TASKARGS taskArgs;
	struct RMS_Command *pCommand;
	struct RMS_MsgArgs *pMsgArgs;
	struct RMS_MoreTaskArgs *pMoreTaskArgs;
	enum NODE_TYPE nodeType;
	u32 dwLength;
	RMS_WORD *pdwBuf = NULL;
	u32 ulBytes;
	u32 i;
	u32 total;
	u32 uCharsInRMSWord;
	s32 taskArgsOffset;
	s32 sioInDefOffset;
	s32 sioOutDefOffset;
	s32 sioDefsOffset;
	s32 argsOffset = -1;
	s32 offset;
	struct NODE_STRMDEF strmDef;
	u32 max;
	DSP_STATUS status = DSP_SOK;
	struct DSP_NODEINFO nodeInfo;
	u32 devType;

	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(hDisp, DISP_SIGNATURE));
	DBC_Require(hNode != NULL);
	DBC_Require(NODE_GetType(hNode) != NODE_DEVICE);
	DBC_Require(pNodeEnv != NULL);

	GT_6trace(DISP_DebugMask, GT_ENTER,
	     "DISP_NodeCreate: hDisp: 0x%x\thNode:"
	     " 0x%x\tulRMSFxn: 0x%x\tulCreateFxn: 0x%x\tpArgs: 0x%x\tpNodeEnv:"
	     " 0x%x\n", hDisp, hNode, ulRMSFxn, ulCreateFxn, pArgs, pNodeEnv);

	status = DEV_GetDevType(hDisp->hDevObject, &devType);

	GT_1trace(DISP_DebugMask, GT_6CLASS, "DISP_Create: Creating DISP "
		 "for device = 0x%x\n", devType);

	if (DSP_FAILED(status))
		goto func_end;

	if (devType != DSP_UNIT) {
		GT_1trace(DISP_DebugMask, GT_7CLASS,
			 "DISP_NodeCreate unknown device "
			 "type = 0x%x\n", devType);
		goto func_end;
	}
	DBC_Require(pArgs != NULL);
	nodeType = NODE_GetType(hNode);
	msgArgs = pArgs->asa.msgArgs;
	max = hDisp->ulBufsizeRMS;    /*Max # of RMS words that can be sent */
	DBC_Assert(max == RMS_COMMANDBUFSIZE);
	uCharsInRMSWord = sizeof(RMS_WORD) / hDisp->uCharSize;
	/* Number of RMS words needed to hold arg data */
	dwLength = (msgArgs.uArgLength + uCharsInRMSWord - 1) / uCharsInRMSWord;
	/* Make sure msg args and command fit in buffer */
	total = sizeof(struct RMS_Command) / sizeof(RMS_WORD) +
		sizeof(struct RMS_MsgArgs)
		/ sizeof(RMS_WORD)  - 1 + dwLength;
	if (total >= max) {
		status = DSP_EFAIL;
		GT_2trace(DISP_DebugMask, GT_6CLASS,
			"DISP_NodeCreate: Message args too"
			" large for buffer! Message args size = %d, max = %d\n",
			total, max);
	}
	/*
	 *  Fill in buffer to send to RMS.
	 *  The buffer will have the following  format:
	 *
	 *  RMS command:
	 *      Address of RMS_CreateNode()
	 *      Address of node's create function
	 *      dummy argument
	 *      node type
	 *
	 *  Message Args:
	 *      max number of messages
	 *      segid for message buffer allocation
	 *      notification type to use when message is received
	 *      length of message arg data
	 *      message args data
	 *
	 *  Task Args (if task or socket node):
	 *      priority
	 *      stack size
	 *      system stack size
	 *      stack segment
	 *      misc
	 *      number of input streams
	 *      pSTRMInDef[] - offsets of STRM definitions for input streams
	 *      number of output streams
	 *      pSTRMOutDef[] - offsets of STRM definitions for output
	 *      streams
	 *      STRMInDef[] - array of STRM definitions for input streams
	 *      STRMOutDef[] - array of STRM definitions for output streams
	 *
	 *  Socket Args (if DAIS socket node):
	 *
	 */
	if (DSP_SUCCEEDED(status)) {
		total = 0; 	/* Total number of words in buffer so far */
		pdwBuf = (RMS_WORD *)hDisp->pBuf;
		pCommand = (struct RMS_Command *)pdwBuf;
		pCommand->fxn = (RMS_WORD)(ulRMSFxn);
		pCommand->arg1 = (RMS_WORD)(ulCreateFxn);
		if (NODE_GetLoadType(hNode) == NLDR_DYNAMICLOAD) {
			/* Flush ICACHE on Load */
			pCommand->arg2 = 1; 	/* dummy argument */
		} else {
			/* Do not flush ICACHE */
			pCommand->arg2 = 0; 	/* dummy argument */
		}
		pCommand->data = NODE_GetType(hNode);
		/*
		 *  argsOffset is the offset of the data field in struct
		 *  RMS_Command structure. We need this to calculate stream
		 *  definition offsets.
		 */
		argsOffset = 3;
		total += sizeof(struct RMS_Command) / sizeof(RMS_WORD);
		/* Message args */
		pMsgArgs = (struct RMS_MsgArgs *) (pdwBuf + total);
		pMsgArgs->maxMessages = msgArgs.uMaxMessages;
		pMsgArgs->segid = msgArgs.uSegid;
		pMsgArgs->notifyType = msgArgs.uNotifyType;
		pMsgArgs->argLength = msgArgs.uArgLength;
		total += sizeof(struct RMS_MsgArgs) / sizeof(RMS_WORD) - 1;
		memcpy(pdwBuf + total, msgArgs.pData, msgArgs.uArgLength);
		total += dwLength;
	}
	if (DSP_FAILED(status))
		goto func_end;

	/* If node is a task node, copy task create arguments into  buffer */
	if (nodeType == NODE_TASK || nodeType == NODE_DAISSOCKET) {
		taskArgs = pArgs->asa.taskArgs;
		taskArgsOffset = total;
		total += sizeof(struct RMS_MoreTaskArgs) / sizeof(RMS_WORD) +
			1 + taskArgs.uNumInputs + taskArgs.uNumOutputs;
		/* Copy task arguments */
		if (total < max) {
			total = taskArgsOffset;
			pMoreTaskArgs = (struct RMS_MoreTaskArgs *)(pdwBuf +
					total);
			/*
			 * Get some important info about the node. Note that we
			 * don't just reach into the hNode struct because
			 * that would break the node object's abstraction.
			 */
			GetNodeInfo(hNode, &nodeInfo);
			GT_2trace(DISP_DebugMask, GT_ENTER,
				 "uExecutionPriority %x, nPriority %x\n",
				 nodeInfo.uExecutionPriority,
				 taskArgs.nPriority);
			pMoreTaskArgs->priority = nodeInfo.uExecutionPriority;
			pMoreTaskArgs->stackSize = taskArgs.uStackSize;
			pMoreTaskArgs->sysstackSize = taskArgs.uSysStackSize;
			pMoreTaskArgs->stackSeg = taskArgs.uStackSeg;
			pMoreTaskArgs->heapAddr = taskArgs.uDSPHeapAddr;
			pMoreTaskArgs->heapSize = taskArgs.uHeapSize;
			pMoreTaskArgs->misc = taskArgs.ulDaisArg;
			pMoreTaskArgs->numInputStreams = taskArgs.uNumInputs;
			total +=
			    sizeof(struct RMS_MoreTaskArgs) / sizeof(RMS_WORD);
			GT_2trace(DISP_DebugMask, GT_7CLASS,
				 "DISP::::uDSPHeapAddr %x, "
				 "uHeapSize %x\n", taskArgs.uDSPHeapAddr,
				 taskArgs.uHeapSize);
			/* Keep track of pSIOInDef[] and pSIOOutDef[]
			 * positions in the buffer, since this needs to be
			 * filled in later.  */
			sioInDefOffset = total;
			total += taskArgs.uNumInputs;
			pdwBuf[total++] = taskArgs.uNumOutputs;
			sioOutDefOffset = total;
			total += taskArgs.uNumOutputs;
			sioDefsOffset = total;
			/* Fill SIO defs and offsets */
			offset = sioDefsOffset;
			for (i = 0; i < taskArgs.uNumInputs; i++) {
				if (DSP_FAILED(status))
					break;

				pdwBuf[sioInDefOffset + i] =
					(offset - argsOffset)
					* (sizeof(RMS_WORD) / DSPWORDSIZE);
				strmDef = taskArgs.strmInDef[i];
				status = FillStreamDef(pdwBuf, &total, offset,
					 strmDef, max, uCharsInRMSWord);
				offset = total;
			}
			for (i = 0;  (i < taskArgs.uNumOutputs) &&
			    (DSP_SUCCEEDED(status)); i++) {
				pdwBuf[sioOutDefOffset + i] =
					(offset - argsOffset)
					* (sizeof(RMS_WORD) / DSPWORDSIZE);
				strmDef = taskArgs.strmOutDef[i];
				status = FillStreamDef(pdwBuf, &total, offset,
					 strmDef, max, uCharsInRMSWord);
				offset = total;
			}
			if (DSP_FAILED(status)) {
				GT_2trace(DISP_DebugMask, GT_6CLASS,
				      "DISP_NodeCreate: Message"
				      " args to large for buffer! Message args"
				      " size = %d, max = %d\n", total, max);
			}
		} else {
			/* Args won't fit */
			status = DSP_EFAIL;
			GT_2trace(DISP_DebugMask, GT_6CLASS,
				 "DISP_NodeCreate: Message args "
				 " too large for buffer! Message args size = %d"
				 ", max = %d\n", total, max);
		}
	}
	if (DSP_SUCCEEDED(status)) {
		ulBytes = total * sizeof(RMS_WORD);
		DBC_Assert(ulBytes < (RMS_COMMANDBUFSIZE * sizeof(RMS_WORD)));
		status = SendMessage(hDisp, NODE_GetTimeout(hNode),
			 ulBytes, pNodeEnv);
		if (DSP_FAILED(status)) {
			GT_1trace(DISP_DebugMask, GT_6CLASS,
				  "DISP_NodeCreate  failed! "
				  "status = 0x%x\n", status);
		} else {
			/*
			 * Message successfully received from RMS.
			 * Return the status of the Node's create function
			 * on the DSP-side
			 */
			status = (((RMS_WORD *)(hDisp->pBuf))[0]);
			if (DSP_FAILED(status)) {
				GT_1trace(DISP_DebugMask, GT_6CLASS,
					 "DISP_NodeCreate, "
					 "DSP-side Node Create failed: 0x%x\n",
					 status);
			}

		}
	}
func_end:
	return status;
}

/*
 *  ======== DISP_NodeDelete ========
 *  purpose:
 *      Delete a node on the DSP by remotely calling the node's delete function.
 *
 */
DSP_STATUS DISP_NodeDelete(struct DISP_OBJECT *hDisp, struct NODE_OBJECT *hNode,
			  u32 ulRMSFxn, u32 ulDeleteFxn, NODE_ENV nodeEnv)
{
	u32 dwArg;
	struct RMS_Command *pCommand;
	DSP_STATUS status = DSP_SOK;
	u32 devType;

	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(hDisp, DISP_SIGNATURE));
	DBC_Require(hNode != NULL);

	GT_5trace(DISP_DebugMask, GT_ENTER,
		 "DISP_NodeDelete: hDisp: 0x%xthNode: "
		 "0x%x\tulRMSFxn: 0x%x\tulDeleteFxn: 0x%x\tnodeEnv: 0x%x\n",
		 hDisp, hNode, ulRMSFxn, ulDeleteFxn, nodeEnv);

	status = DEV_GetDevType(hDisp->hDevObject, &devType);

	if (DSP_SUCCEEDED(status)) {

		if (devType == DSP_UNIT) {

			/*
			 *  Fill in buffer to send to RMS
			 */
			pCommand = (struct RMS_Command *)hDisp->pBuf;
			pCommand->fxn = (RMS_WORD)(ulRMSFxn);
			pCommand->arg1 = (RMS_WORD)nodeEnv;
			pCommand->arg2 = (RMS_WORD)(ulDeleteFxn);
			pCommand->data = NODE_GetType(hNode);

			status = SendMessage(hDisp, NODE_GetTimeout(hNode),
					    sizeof(struct RMS_Command), &dwArg);
			if (DSP_FAILED(status)) {
				GT_1trace(DISP_DebugMask, GT_6CLASS,
					 "DISP_NodeDelete failed!"
					 "status = 0x%x\n", status);
			} else {
				/*
				 * Message successfully received from RMS.
				 * Return the status of the Node's delete
				 * function on the DSP-side
				 */
				status = (((RMS_WORD *)(hDisp->pBuf))[0]);
				if (DSP_FAILED(status)) {
					GT_1trace(DISP_DebugMask, GT_6CLASS,
					 "DISP_NodeDelete, "
					 "DSP-side Node Delete failed: 0x%x\n",
					 status);
				}
			}


		}
	}
	return status;
}

/*
 *  ======== DISP_NodeRun ========
 *  purpose:
 *      Start execution of a node's execute phase, or resume execution of a node
 *      that has been suspended (via DISP_NodePause()) on the DSP.
 */
DSP_STATUS DISP_NodeRun(struct DISP_OBJECT *hDisp, struct NODE_OBJECT *hNode,
			u32 ulRMSFxn, u32 ulExecuteFxn, NODE_ENV nodeEnv)
{
	u32 dwArg;
	struct RMS_Command *pCommand;
	DSP_STATUS status = DSP_SOK;
	u32 devType;
	DBC_Require(cRefs > 0);
	DBC_Require(MEM_IsValidHandle(hDisp, DISP_SIGNATURE));
	DBC_Require(hNode != NULL);

	GT_5trace(DISP_DebugMask, GT_ENTER, "DISP_NodeRun: hDisp: 0x%xthNode: \
		 0x%x\tulRMSFxn: 0x%x\tulExecuteFxn: 0x%x\tnodeEnv: 0x%x\n", \
		 hDisp, hNode, ulRMSFxn, ulExecuteFxn, nodeEnv);

	status = DEV_GetDevType(hDisp->hDevObject, &devType);

	if (DSP_SUCCEEDED(status)) {

		if (devType == DSP_UNIT) {

			/*
			 *  Fill in buffer to send to RMS.
			 */
			pCommand = (struct RMS_Command *) hDisp->pBuf;
			pCommand->fxn = (RMS_WORD) (ulRMSFxn);
			pCommand->arg1 = (RMS_WORD) nodeEnv;
			pCommand->arg2 = (RMS_WORD) (ulExecuteFxn);
			pCommand->data = NODE_GetType(hNode);

			status = SendMessage(hDisp, NODE_GetTimeout(hNode),
				 sizeof(struct RMS_Command), &dwArg);
			if (DSP_FAILED(status)) {
				GT_1trace(DISP_DebugMask, GT_6CLASS,
					 "DISP_NodeRun failed!"
					 "status = 0x%x\n", status);
			} else {
				/*
				 * Message successfully received from RMS.
				 * Return the status of the Node's execute
				 * function on the DSP-side
				 */
				status = (((RMS_WORD *)(hDisp->pBuf))[0]);
				if (DSP_FAILED(status)) {
					GT_1trace(DISP_DebugMask, GT_6CLASS,
						"DISP_NodeRun, DSP-side Node "
						"Execute failed: 0x%x\n",
						status);
		}
		}

	}
	}

	return status;
}

/*
 *  ======== DeleteDisp ========
 *  purpose:
 *      Frees the resources allocated for the dispatcher.
 */
static void DeleteDisp(struct DISP_OBJECT *hDisp)
{
	DSP_STATUS status = DSP_SOK;
	struct WMD_DRV_INTERFACE *pIntfFxns;

	if (MEM_IsValidHandle(hDisp, DISP_SIGNATURE)) {
		pIntfFxns = hDisp->pIntfFxns;

		/* Free Node Dispatcher resources */
		if (hDisp->hChnlFromDsp) {
			/* Channel close can fail only if the channel handle
			 * is invalid. */
			status = (*pIntfFxns->pfnChnlClose)
				 (hDisp->hChnlFromDsp);
			if (DSP_FAILED(status)) {
				GT_1trace(DISP_DebugMask, GT_6CLASS,
					 "DISP_Delete: Failed to "
					 "close channel from RMS: 0x%x\n",
					 status);
			}
		}
		if (hDisp->hChnlToDsp) {
			status = (*pIntfFxns->pfnChnlClose)(hDisp->hChnlToDsp);
			if (DSP_FAILED(status)) {
				GT_1trace(DISP_DebugMask, GT_6CLASS,
					 "DISP_Delete: Failed to "
					 "close channel to RMS: 0x%x\n",
					 status);
			}
		}
		if (hDisp->pBuf)
			MEM_Free(hDisp->pBuf);

		MEM_FreeObject(hDisp);
	}
}

/*
 *  ======== FillStreamDef ========
 *  purpose:
 *      Fills stream definitions.
 */
static DSP_STATUS FillStreamDef(RMS_WORD *pdwBuf, u32 *ptotal, u32 offset,
				struct NODE_STRMDEF strmDef, u32 max,
				u32 uCharsInRMSWord)
{
	struct RMS_StrmDef *pStrmDef;
	u32 total = *ptotal;
	u32 uNameLen;
	u32 dwLength;
	DSP_STATUS status = DSP_SOK;

	if (total + sizeof(struct RMS_StrmDef) / sizeof(RMS_WORD) >= max) {
		status = DSP_EFAIL;
	} else {
		pStrmDef = (struct RMS_StrmDef *)(pdwBuf + total);
		pStrmDef->bufsize = strmDef.uBufsize;
		pStrmDef->nbufs = strmDef.uNumBufs;
		pStrmDef->segid = strmDef.uSegid;
		pStrmDef->align = strmDef.uAlignment;
		pStrmDef->timeout = strmDef.uTimeout;
	}

	if (DSP_SUCCEEDED(status)) {
		/*
		 *  Since we haven't added the device name yet, subtract
		 *  1 from total.
		 */
		total += sizeof(struct RMS_StrmDef) / sizeof(RMS_WORD) - 1;
               DBC_Require(strmDef.szDevice);
               dwLength = strlen(strmDef.szDevice) + 1;

		/* Number of RMS_WORDS needed to hold device name */
		uNameLen = (dwLength + uCharsInRMSWord - 1) / uCharsInRMSWord;

		if (total + uNameLen >= max) {
			status = DSP_EFAIL;
		} else {
			/*
			 *  Zero out last word, since the device name may not
			 *  extend to completely fill this word.
			 */
			pdwBuf[total + uNameLen - 1] = 0;
			/** TODO USE SERVICES **/
			memcpy(pdwBuf + total, strmDef.szDevice, dwLength);
			total += uNameLen;
			*ptotal = total;
		}
	}

	return status;
}

/*
 *  ======== SendMessage ======
 *  Send command message to RMS, get reply from RMS.
 */
static DSP_STATUS SendMessage(struct DISP_OBJECT *hDisp, u32 dwTimeout,
			     u32 ulBytes, u32 *pdwArg)
{
	struct WMD_DRV_INTERFACE *pIntfFxns;
	struct CHNL_OBJECT *hChnl;
	u32 dwArg = 0;
	u8 *pBuf;
	struct CHNL_IOC chnlIOC;
	DSP_STATUS status = DSP_SOK;

	DBC_Require(pdwArg != NULL);

	*pdwArg = (u32) NULL;
	pIntfFxns = hDisp->pIntfFxns;
	hChnl = hDisp->hChnlToDsp;
	pBuf = hDisp->pBuf;

	/* Send the command */
	status = (*pIntfFxns->pfnChnlAddIOReq) (hChnl, pBuf, ulBytes, 0,
		 0L, dwArg);

	if (DSP_FAILED(status)) {
		GT_1trace(DISP_DebugMask, GT_6CLASS,
			 "SendMessage: Channel AddIOReq to"
			 " RMS failed! Status = 0x%x\n", status);
		goto func_cont;
	}
	status = (*pIntfFxns->pfnChnlGetIOC) (hChnl, dwTimeout, &chnlIOC);
	if (DSP_SUCCEEDED(status)) {
		if (!CHNL_IsIOComplete(chnlIOC)) {
			if (CHNL_IsTimedOut(chnlIOC)) {
				status = DSP_ETIMEOUT;
			} else {
				GT_1trace(DISP_DebugMask, GT_6CLASS,
					 "SendMessage failed! "
					 "Channel IOC status = 0x%x\n",
					 chnlIOC.status);
				status = DSP_EFAIL;
			}
		}
	} else {
		GT_1trace(DISP_DebugMask, GT_6CLASS,
			 "SendMessage: Channel GetIOC to"
			 " RMS failed! Status = 0x%x\n", status);
	}
func_cont:
	/* Get the reply */
	if (DSP_FAILED(status))
		goto func_end;

	hChnl = hDisp->hChnlFromDsp;
	ulBytes = REPLYSIZE;
	status = (*pIntfFxns->pfnChnlAddIOReq)(hChnl, pBuf, ulBytes,
		 0, 0L, dwArg);
	if (DSP_FAILED(status)) {
		GT_1trace(DISP_DebugMask, GT_6CLASS,
			 "SendMessage: Channel AddIOReq "
			 "from RMS failed! Status = 0x%x\n", status);
		goto func_end;
	}
	status = (*pIntfFxns->pfnChnlGetIOC) (hChnl, dwTimeout, &chnlIOC);
	if (DSP_SUCCEEDED(status)) {
		if (CHNL_IsTimedOut(chnlIOC)) {
			status = DSP_ETIMEOUT;
		} else if (chnlIOC.cBytes < ulBytes) {
			/* Did not get all of the reply from the RMS */
			GT_1trace(DISP_DebugMask, GT_6CLASS,
				 "SendMessage: Did not get all"
				 "of reply from RMS! Bytes received: %d\n",
				 chnlIOC.cBytes);
			status = DSP_EFAIL;
		} else {
			if (CHNL_IsIOComplete(chnlIOC)) {
				DBC_Assert(chnlIOC.pBuf == pBuf);
				status = (*((RMS_WORD *)chnlIOC.pBuf));
				*pdwArg = (((RMS_WORD *)(chnlIOC.pBuf))[1]);
			} else {
				status = DSP_EFAIL;
			}
		}
	} else {
		/* GetIOC failed */
		GT_1trace(DISP_DebugMask, GT_6CLASS,
			 "SendMessage: Failed to get "
			 "reply from RMS! Status = 0x%x\n", status);
	}
func_end:
	return status;
}
