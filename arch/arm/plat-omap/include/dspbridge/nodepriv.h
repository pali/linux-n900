/*
 * nodepriv.h
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
 *  ======== nodepriv.h ========
 *  Description:
 *      Private node header shared by NODE and DISP.
 *
 *  Public Functions:
 *      NODE_GetChannelId
 *      NODE_GetStrmMgr
 *      NODE_GetTimeout
 *      NODE_GetType
 *      NODE_GetLoadType
 *
 *! Revision History
 *! ================
 *! 19-Nov-2002 map     Added NODE_GetLoadType
 *! 13-Feb-2002 jeh     Added uSysStackSize to NODE_TASKARGS.
 *! 23-Apr-2001 jeh     Removed unused typedefs, defines.
 *! 10-Oct-2000 jeh     Added alignment to NODE_STRMDEF.
 *! 20-Jun-2000 jeh     Created.
 */

#ifndef NODEPRIV_
#define NODEPRIV_

#include <dspbridge/strmdefs.h>
#include <dspbridge/nodedefs.h>
#include <dspbridge/nldrdefs.h>

/* DSP address of node environment structure */
	typedef u32 NODE_ENV;

/*
 *  Node create structures
 */

/* Message node */
	struct NODE_MSGARGS {
		u32 uMaxMessages; /* Max # of simultaneous messages for node */
		u32 uSegid;	/* Segment for allocating message buffers */
		u32 uNotifyType;  /* Notify type (SEM_post, SWI_post, etc.) */
		u32 uArgLength;  /* Length in 32-bit words of arg data block */
		u8 *pData;	/* Argument data for node */
	} ;

	struct NODE_STRMDEF {
		u32 uBufsize;	/* Size of buffers for SIO stream */
		u32 uNumBufs;	/* max # of buffers in SIO stream at once */
		u32 uSegid;	/* Memory segment id to allocate buffers */
		u32 uTimeout;	/* Timeout for blocking SIO calls */
		u32 uAlignment;	/* Buffer alignment */
		char *szDevice;	/* Device name for stream */
	} ;

/* Task node */
	struct NODE_TASKARGS {
		struct NODE_MSGARGS msgArgs;
		s32 nPriority;
		u32 uStackSize;
		u32 uSysStackSize;
		u32 uStackSeg;
		u32 uDSPHeapResAddr;	/* DSP virtual heap address */
		u32 uDSPHeapAddr;	/* DSP virtual heap address */
		u32 uHeapSize;	/* Heap size */
		u32 uGPPHeapAddr;	/* GPP virtual heap address */
		u32 uProfileID;	/* Profile ID */
		u32 uNumInputs;
		u32 uNumOutputs;
		u32 ulDaisArg;	/* Address of iAlg object */
		struct NODE_STRMDEF *strmInDef;
		struct NODE_STRMDEF *strmOutDef;
	} ;

/*
 *  ======== NODE_CREATEARGS ========
 */
	struct NODE_CREATEARGS {
		union {
			struct NODE_MSGARGS msgArgs;
			struct NODE_TASKARGS taskArgs;
		} asa;
	} ;

/*
 *  ======== NODE_GetChannelId ========
 *  Purpose:
 *      Get the channel index reserved for a stream connection between the
 *      host and a node. This index is reserved when NODE_Connect() is called
 *      to connect the node with the host. This index should be passed to
 *      the CHNL_Open function when the stream is actually opened.
 *  Parameters:
 *      hNode:          Node object allocated from NODE_Allocate().
 *      uDir:           Input (DSP_TONODE) or output (DSP_FROMNODE).
 *      uIndex:         Stream index.
 *      pulId:          Location to store channel index.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hNode.
 *      DSP_ENODETYPE:  Not a task or DAIS socket node.
 *      DSP_EVALUE:     The node's stream corresponding to uIndex and uDir
 *                      is not a stream to or from the host.
 *  Requires:
 *      NODE_Init(void) called.
 *      Valid uDir.
 *      pulId != NULL.
 *  Ensures:
 */
	extern DSP_STATUS NODE_GetChannelId(struct NODE_OBJECT *hNode,
					    u32 uDir,
					    u32 uIndex, OUT u32 *pulId);

/*
 *  ======== NODE_GetStrmMgr ========
 *  Purpose:
 *      Get the STRM manager for a node.
 *  Parameters:
 *      hNode:          Node allocated with NODE_Allocate().
 *      phStrmMgr:      Location to store STRM manager on output.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hNode.
 *  Requires:
 *      phStrmMgr != NULL.
 *  Ensures:
 */
	extern DSP_STATUS NODE_GetStrmMgr(struct NODE_OBJECT *hNode,
					  struct STRM_MGR **phStrmMgr);

/*
 *  ======== NODE_GetTimeout ========
 *  Purpose:
 *      Get the timeout value of a node.
 *  Parameters:
 *      hNode:      Node allocated with NODE_Allocate(), or DSP_HGPPNODE.
 *  Returns:
 *      Node's timeout value.
 *  Requires:
 *      Valid hNode.
 *  Ensures:
 */
	extern u32 NODE_GetTimeout(struct NODE_OBJECT *hNode);

/*
 *  ======== NODE_GetType ========
 *  Purpose:
 *      Get the type (device, message, task, or XDAIS socket) of a node.
 *  Parameters:
 *      hNode:      Node allocated with NODE_Allocate(), or DSP_HGPPNODE.
 *  Returns:
 *      Node type:  NODE_DEVICE, NODE_TASK, NODE_XDAIS, or NODE_GPP.
 *  Requires:
 *      Valid hNode.
 *  Ensures:
 */
	extern enum NODE_TYPE NODE_GetType(struct NODE_OBJECT *hNode);

/*
 *  ======== GetNodeInfo ========
 *  Purpose:
 *      Get node information without holding semaphore.
 *  Parameters:
 *      hNode:      Node allocated with NODE_Allocate(), or DSP_HGPPNODE.
 *  Returns:
 *      Node info:  priority, device owner, no. of streams, execution state
 *                  NDB properties.
 *  Requires:
 *      Valid hNode.
 *  Ensures:
 */
	extern void GetNodeInfo(struct NODE_OBJECT *hNode,
				struct DSP_NODEINFO *pNodeInfo);

/*
 *  ======== NODE_GetLoadType ========
 *  Purpose:
 *      Get the load type (dynamic, overlay, static) of a node.
 *  Parameters:
 *      hNode:      Node allocated with NODE_Allocate(), or DSP_HGPPNODE.
 *  Returns:
 *      Node type:  NLDR_DYNAMICLOAD, NLDR_OVLYLOAD, NLDR_STATICLOAD
 *  Requires:
 *      Valid hNode.
 *  Ensures:
 */
	extern enum NLDR_LOADTYPE NODE_GetLoadType(struct NODE_OBJECT *hNode);

#endif				/* NODEPRIV_ */
