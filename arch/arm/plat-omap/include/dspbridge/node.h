/*
 * node.h
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
 *  ======== node.h ========
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
 *      NODE_GetMessage
 *      NODE_GetProcessor
 *      NODE_Init
 *      NODE_OnExit
 *      NODE_Pause
 *      NODE_PutMessage
 *      NODE_RegisterNotify
 *      NODE_Run
 *      NODE_Terminate
 *
 *  Notes:
 *
 *! Revision History:
 *! =================
 *! 23-Apr-2001 jeh     Updated with code review changes.
 *! 16-Jan-2001 jeh     Added DSP_ESYMBOL, DSP_EUUID to return codes.
 *! 17-Nov-2000 jeh     Added NODE_OnExit().
 *! 27-Oct-2000 jeh     Added timeouts to NODE_GetMessage, NODE_PutMessage.
 *! 12-Oct-2000 jeh     Changed NODE_EnumNodeInfo to NODE_EnumNodes. Removed
 *!                     NODE_RegisterAllNodes().
 *! 07-Sep-2000 jeh     Changed type HANDLE in NODE_RegisterNotify to
 *!                     DSP_HNOTIFICATION. Added DSP_STRMATTR param to
 *!                     NODE_Connect(). Removed NODE_GetMessageStream().
 *! 17-Jul-2000 jeh     Updated function header descriptions.
 *! 19-Jun-2000 jeh     Created.
 */

#ifndef NODE_
#define NODE_

#include <dspbridge/procpriv.h>

#include <dspbridge/nodedefs.h>
#include <dspbridge/dispdefs.h>
#include <dspbridge/nldrdefs.h>

/*
 *  ======== NODE_Allocate ========
 *  Purpose:
 *      Allocate GPP resources to manage a node on the DSP.
 *  Parameters:
 *      hProcessor:         Handle of processor that is allocating the node.
 *      pNodeId:            Pointer to a DSP_UUID for the node.
 *      pArgs:              Optional arguments to be passed to the node.
 *      pAttrIn:            Optional pointer to node attributes (priority,
 *                          timeout...)
 *      phNode:             Location to store node handle on output.
 *  Returns:
 *      DSP_SOK:            Success.
 *      DSP_EMEMORY:        Insufficient memory on GPP.
 *      DSP_EUUID:          Node UUID has not been registered.
 *      DSP_ESYMBOL:        iAlg functions not found for a DAIS node.
 *      DSP_ERANGE:         pAttrIn != NULL and pAttrIn->iPriority out of
 *                          range.
 *      DSP_EFAIL:          A failure occured, unable to allocate node.
 *      DSP_EWRONGSTATE:    Proccessor is not in the running state.
 *  Requires:
 *      NODE_Init(void) called.
 *      hProcessor != NULL.
 *      pNodeId != NULL.
 *      phNode != NULL.
 *  Ensures:
 *      DSP_SOK:            IsValidNode(*phNode).
 *      error:              *phNode == NULL.
 */
	extern DSP_STATUS NODE_Allocate(struct PROC_OBJECT *hProcessor,
					IN CONST struct DSP_UUID *pNodeId,
					OPTIONAL IN CONST struct DSP_CBDATA
					*pArgs,
					OPTIONAL IN CONST struct DSP_NODEATTRIN
					*pAttrIn,
					OUT struct NODE_OBJECT **phNode);

/*
 *  ======== NODE_AllocMsgBuf ========
 *  Purpose:
 *      Allocate and Prepare a buffer whose descriptor will be passed to a
 *      Node within a (DSP_MSG)message
 *  Parameters:
 *      hNode:          The node handle.
 *      uSize:          The size of the buffer to be allocated.
 *      pAttr:          Pointer to a DSP_BUFFERATTR structure.
 *      pBuffer:        Location to store the address of the allocated
 *                      buffer on output.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid node handle.
 *      DSP_EMEMORY:    Insufficent memory.
 *      DSP_EFAIL:      General Failure.
 *      DSP_ESIZE:      Invalid Size.
 *  Requires:
 *      NODE_Init(void) called.
 *      pBuffer != NULL.
 *  Ensures:
 */
	extern DSP_STATUS NODE_AllocMsgBuf(struct NODE_OBJECT *hNode,
					   u32 uSize,
					   OPTIONAL struct DSP_BUFFERATTR
					   *pAttr,
					   OUT u8 **pBuffer);

/*
 *  ======== NODE_ChangePriority ========
 *  Purpose:
 *      Change the priority of an allocated node.
 *  Parameters:
 *      hNode:              Node handle returned from NODE_Allocate.
 *      nPriority:          New priority level to set node's priority to.
 *  Returns:
 *      DSP_SOK:            Success.
 *      DSP_EHANDLE:        Invalid hNode.
 *      DSP_ERANGE:         nPriority is out of range.
 *      DSP_ENODETYPE:      The specified node is not a task node.
 *      DSP_EWRONGSTATE:    Node is not in the NODE_ALLOCATED, NODE_PAUSED,
 *                          or NODE_RUNNING state.
 *      DSP_ETIMEOUT:       A timeout occurred before the DSP responded.
 *      DSP_ERESTART:       A critical error has occurred and the DSP is
 *                          being restarted.
 *      DSP_EFAIL:          Unable to change node's runtime priority level.
 *  Requires:
 *      NODE_Init(void) called.
 *  Ensures:
 *      DSP_SOK && (Node's current priority == nPriority)
 */
	extern DSP_STATUS NODE_ChangePriority(struct NODE_OBJECT *hNode,
					      s32 nPriority);

/*
 *  ======== NODE_CloseOrphans ========
 *  Purpose:
 *      Delete all nodes whose owning processor is being destroyed.
 *  Parameters:
 *      hNodeMgr:       Node manager object.
 *      hProc:          Handle to processor object being destroyed.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EFAIL:      Unable to delete all nodes belonging to hProc.
 *  Requires:
 *      Valid hNodeMgr.
 *      hProc != NULL.
 *  Ensures:
 */
	extern DSP_STATUS NODE_CloseOrphans(struct NODE_MGR *hNodeMgr,
					    struct PROC_OBJECT *hProc);

/*
 *  ======== NODE_Connect ========
 *  Purpose:
 *      Connect two nodes on the DSP, or a node on the DSP to the GPP. In the
 *      case that the connnection is being made between a node on the DSP and
 *      the GPP, one of the node handles (either hNode1 or hNode2) must be
 *      the constant NODE_HGPPNODE.
 *  Parameters:
 *      hNode1:         Handle of first node to connect to second node. If
 *                      this is a connection from the GPP to hNode2, hNode1
 *                      must be the constant NODE_HGPPNODE. Otherwise, hNode1
 *                      must be a node handle returned from a successful call
 *                      to Node_Allocate().
 *      hNode2:         Handle of second node. Must be either NODE_HGPPNODE
 *                      if this is a connection from DSP node to GPP, or a
 *                      node handle returned from a successful call to
 *                      NODE_Allocate().
 *      uStream1:       Output stream index on first node, to be connected
 *                      to second node's input stream. Value must range from
 *                      0 <= uStream1 < number of output streams.
 *      uStream2:       Input stream index on second node. Value must range
 *                      from 0 <= uStream2 < number of input streams.
 *      pAttrs:         Stream attributes (NULL ==> use defaults).
 *      pConnParam:     A pointer to a DSP_CBDATA structure that defines
 *                      connection parameter for device nodes to pass to DSP
 *                      side.
 *                      If the value of this parameter is NULL, then this API
 *                      behaves like DSPNode_Connect. This parameter will have
 *                      length of the string and the null terminated string in
 *                      DSP_CBDATA struct. This can be extended in future tp
 *                      pass binary data.
 *  Returns:
 *      DSP_SOK:                Success.
 *      DSP_EHANDLE:            Invalid hNode1 or hNode2.
 *      DSP_EMEMORY:            Insufficient host memory.
 *      DSP_EVALUE:             A stream index parameter is invalid.
 *      DSP_EALREADYCONNECTED:  A connection already exists for one of the
 *                              indices uStream1 or uStream2.
 *      DSP_EWRONGSTATE:        Either hNode1 or hNode2 is not in the
 *                              NODE_ALLOCATED state.
 *      DSP_ENOMORECONNECTIONS: No more connections available.
 *      DSP_EFAIL:              Attempt to make an illegal connection (eg,
 *                              Device node to device node, or device node to
 *                              GPP), the two nodes are on different DSPs.
 *  Requires:
 *      NODE_Init(void) called.
 *  Ensures:
 */
	extern DSP_STATUS NODE_Connect(struct NODE_OBJECT *hNode1,
				       u32 uStream1,
				       struct NODE_OBJECT *hNode2,
				       u32 uStream2,
				       OPTIONAL IN struct DSP_STRMATTR *pAttrs,
				       OPTIONAL IN struct DSP_CBDATA
				       *pConnParam);

/*
 *  ======== NODE_Create ========
 *  Purpose:
 *      Create a node on the DSP by remotely calling the node's create
 *      function. If necessary, load code that contains the node's create
 *      function.
 *  Parameters:
 *      hNode:              Node handle returned from NODE_Allocate().
 *  Returns:
 *      DSP_SOK:            Success.
 *      DSP_EHANDLE:        Invalid hNode.
 *      DSP_ESYMBOL:        Create function not found in the COFF file.
 *      DSP_EWRONGSTATE:    Node is not in the NODE_ALLOCATED state.
 *      DSP_EMEMORY:        Memory allocation failure on the DSP.
 *      DSP_ETASK:          Unable to create node's task or process on the DSP.
 *      DSP_ESTREAM:        Stream creation failure on the DSP.
 *      DSP_ETIMEOUT:       A timeout occurred before the DSP responded.
 *      DSP_EUSER1-16:      A user-defined failure occurred on the DSP.
 *      DSP_EFAIL:          A failure occurred, unable to create node.
 *  Requires:
 *      NODE_Init(void) called.
 *  Ensures:
 */
	extern DSP_STATUS NODE_Create(struct NODE_OBJECT *hNode);

/*
 *  ======== NODE_CreateMgr ========
 *  Purpose:
 *      Create a NODE Manager object. This object handles the creation,
 *      deletion, and execution of nodes on the DSP target. The NODE Manager
 *      also maintains a pipe map of used and available node connections.
 *      Each DEV object should have exactly one NODE Manager object.
 *
 *  Parameters:
 *      phNodeMgr:      Location to store node manager handle on output.
 *      hDevObject:     Device for this processor.
 *  Returns:
 *      DSP_SOK:        Success;
 *      DSP_EMEMORY:    Insufficient memory for requested resources.
 *      DSP_EFAIL:      General failure.
 *  Requires:
 *      NODE_Init(void) called.
 *      phNodeMgr != NULL.
 *      hDevObject != NULL.
 *  Ensures:
 *      DSP_SOK:        Valide *phNodeMgr.
 *      error:          *phNodeMgr == NULL.
 */
	extern DSP_STATUS NODE_CreateMgr(OUT struct NODE_MGR **phNodeMgr,
					 struct DEV_OBJECT *hDevObject);

/*
 *  ======== NODE_Delete ========
 *  Purpose:
 *      Delete resources allocated in NODE_Allocate(). If the node was
 *      created, delete the node on the DSP by remotely calling the node's
 *      delete function. Loads the node's delete function if necessary.
 *      GPP side resources are freed after node's delete function returns.
 *  Parameters:
 *      hNode:              Node handle returned from NODE_Allocate().
 *  Returns:
 *      DSP_SOK:            Success.
 *      DSP_EHANDLE:        Invalid hNode.
 *      DSP_ETIMEOUT:       A timeout occurred before the DSP responded.
 *      DSP_EDELETE:        A deletion failure occurred.
 *      DSP_EUSER1-16:      Node specific failure occurred on the DSP.
 *      DSP_EFAIL:          A failure occurred in deleting the node.
 *      DSP_ESYMBOL:        Delete function not found in the COFF file.
 *  Requires:
 *      NODE_Init(void) called.
 *  Ensures:
 *      DSP_SOK:            hNode is invalid.
 */
	extern DSP_STATUS NODE_Delete(struct NODE_OBJECT *hNode);

/*
 *  ======== NODE_DeleteMgr ========
 *  Purpose:
 *      Delete the NODE Manager.
 *  Parameters:
 *      hNodeMgr:       Node manager object.
 *  Returns:
 *      DSP_SOK:        Success.
 *  Requires:
 *      NODE_Init(void) called.
 *      Valid hNodeMgr.
 *  Ensures:
 */
	extern DSP_STATUS NODE_DeleteMgr(struct NODE_MGR *hNodeMgr);

/*
 *  ======== NODE_EnumNodes ========
 *  Purpose:
 *      Enumerate the nodes currently allocated for the DSP.
 *  Parameters:
 *      hNodeMgr:       Node manager returned from NODE_CreateMgr().
 *      aNodeTab:       Array to copy node handles into.
 *      uNodeTabSize:   Number of handles that can be written to aNodeTab.
 *      puNumNodes:     Location where number of node handles written to
 *                      aNodeTab will be written.
 *      puAllocated:    Location to write total number of allocated nodes.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_ESIZE:      aNodeTab is too small to hold all node handles.
 *  Requires:
 *      Valid hNodeMgr.
 *      aNodeTab != NULL || uNodeTabSize == 0.
 *      puNumNodes != NULL.
 *      puAllocated != NULL.
 *  Ensures:
 *      - (DSP_ESIZE && *puNumNodes == 0)
 *      - || (DSP_SOK && *puNumNodes <= uNodeTabSize)  &&
 *        (*puAllocated == *puNumNodes)
 */
	extern DSP_STATUS NODE_EnumNodes(struct NODE_MGR *hNodeMgr,
					 IN DSP_HNODE *aNodeTab,
					 u32 uNodeTabSize,
					 OUT u32 *puNumNodes,
					 OUT u32 *puAllocated);

/*
 *  ======== NODE_Exit ========
 *  Purpose:
 *      Discontinue usage of NODE module.
 *  Parameters:
 *  Returns:
 *  Requires:
 *      NODE_Init(void) successfully called before.
 *  Ensures:
 *      Any resources acquired in NODE_Init(void) will be freed when last NODE
 *      client calls NODE_Exit(void).
 */
	extern void NODE_Exit(void);

/*
 *  ======== NODE_FreeMsgBuf ========
 *  Purpose:
 *      Free a message buffer previously allocated with NODE_AllocMsgBuf.
 *  Parameters:
 *      hNode:          The node handle.
 *      pBuffer:        (Address) Buffer allocated by NODE_AllocMsgBuf.
 *      pAttr:          Same buffer attributes passed to NODE_AllocMsgBuf.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid node handle.
 *      DSP_EFAIL:      Failure to free the buffer.
 *  Requires:
 *      NODE_Init(void) called.
 *      pBuffer != NULL.
 *  Ensures:
 */
	extern DSP_STATUS NODE_FreeMsgBuf(struct NODE_OBJECT *hNode,
					  IN u8 *pBuffer,
					  OPTIONAL struct DSP_BUFFERATTR
					  *pAttr);

/*
 *  ======== NODE_GetAttr ========
 *  Purpose:
 *      Copy the current attributes of the specified node into a DSP_NODEATTR
 *      structure.
 *  Parameters:
 *      hNode:          Node object allocated from NODE_Allocate().
 *      pAttr:          Pointer to DSP_NODEATTR structure to copy node's
 *                      attributes.
 *      uAttrSize:      Size of pAttr.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hNode.
 *  Requires:
 *      NODE_Init(void) called.
 *      pAttr != NULL.
 *  Ensures:
 *      DSP_SOK:        *pAttrs contains the node's current attributes.
 */
	extern DSP_STATUS NODE_GetAttr(struct NODE_OBJECT *hNode,
				       OUT struct DSP_NODEATTR *pAttr,
				       u32 uAttrSize);

/*
 *  ======== NODE_GetMessage ========
 *  Purpose:
 *      Retrieve a message from a node on the DSP. The node must be either a
 *      message node, task node, or XDAIS socket node.
 *      If a message is not available, this function will block until a
 *      message is available, or the node's timeout value is reached.
 *  Parameters:
 *      hNode:          Node handle returned from NODE_Allocate().
 *      pMessage:       Pointer to DSP_MSG structure to copy the
 *                      message into.
 *      uTimeout:       Timeout in milliseconds to wait for message.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hNode.
 *      DSP_ENODETYPE:  Cannot retrieve messages from this type of node.
 *      DSP_ETIMEOUT:   Timeout occurred and no message is available.
 *      DSP_EFAIL:      Error occurred while trying to retrieve a message.
 *  Requires:
 *      NODE_Init(void) called.
 *      pMessage != NULL.
 *  Ensures:
 */
	extern DSP_STATUS NODE_GetMessage(struct NODE_OBJECT *hNode,
					  OUT struct DSP_MSG *pMessage,
					  u32 uTimeout);

/*
 *  ======== NODE_GetNldrObj ========
 *  Purpose:
 *      Retrieve the Nldr manager
 *  Parameters:
 *      hNodeMgr:       Node Manager
 *      phNldrObj:      Pointer to a Nldr manager handle
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hNode.
 *  Ensures:
 */
	extern DSP_STATUS NODE_GetNldrObj(struct NODE_MGR *hNodeMgr,
					  OUT struct NLDR_OBJECT **phNldrObj);

/*
 *  ======== NODE_Init ========
 *  Purpose:
 *      Initialize the NODE module.
 *  Parameters:
 *  Returns:
 *      TRUE if initialization succeeded, FALSE otherwise.
 *  Ensures:
 */
	extern bool NODE_Init(void);

/*
 *  ======== NODE_OnExit ========
 *  Purpose:
 *      Gets called when RMS_EXIT is received for a node. PROC needs to pass
 *      this function as a parameter to MSG_Create(). This function then gets
 *      called by the mini-driver when an exit message for a node is received.
 *  Parameters:
 *      hNode:      Handle of the node that the exit message is for.
 *      nStatus:    Return status of the node's execute phase.
 *  Returns:
 *  Ensures:
 */
	void NODE_OnExit(struct NODE_OBJECT *hNode, s32 nStatus);

/*
 *  ======== NODE_Pause ========
 *  Purpose:
 *      Suspend execution of a node currently running on the DSP.
 *  Parameters:
 *      hNode:              Node object representing a node currently
 *                          running on the DSP.
 *  Returns:
 *      DSP_SOK:            Success.
 *      DSP_EHANDLE:        Invalid hNode.
 *      DSP_ENODETYPE:      Node is not a task or socket node.
 *      DSP_ETIMEOUT:       A timeout occurred before the DSP responded.
 *      DSP_EWRONGSTSATE:   Node is not in NODE_RUNNING state.
 *      DSP_EFAIL:          Failed to pause node.
 *  Requires:
 *      NODE_Init(void) called.
 *  Ensures:
 */
	extern DSP_STATUS NODE_Pause(struct NODE_OBJECT *hNode);

/*
 *  ======== NODE_PutMessage ========
 *  Purpose:
 *      Send a message to a message node, task node, or XDAIS socket node.
 *      This function will block until the message stream can accommodate
 *      the message, or a timeout occurs. The message will be copied, so Msg
 *      can be re-used immediately after return.
 *  Parameters:
 *      hNode:              Node handle returned by NODE_Allocate().
 *      pMsg:               Location of message to be sent to the node.
 *      uTimeout:           Timeout in msecs to wait.
 *  Returns:
 *      DSP_SOK:            Success.
 *      DSP_EHANDLE:        Invalid hNode.
 *      DSP_ENODETYPE:      Messages can't be sent to this type of node.
 *      DSP_ETIMEOUT:       Timeout occurred before message could be set.
 *      DSP_EWRONGSTATE:    Node is in invalid state for sending messages.
 *      DSP_EFAIL:          Unable to send message.
 *  Requires:
 *      NODE_Init(void) called.
 *      pMsg != NULL.
 *  Ensures:
 */
	extern DSP_STATUS NODE_PutMessage(struct NODE_OBJECT *hNode,
					  IN CONST struct DSP_MSG *pMsg,
					  u32 uTimeout);

/*
 *  ======== NODE_RegisterNotify ========
 *  Purpose:
 *      Register to be notified on specific events for this node.
 *  Parameters:
 *      hNode:          Node handle returned by NODE_Allocate().
 *      uEventMask:     Mask of types of events to be notified about.
 *      uNotifyType:    Type of notification to be sent.
 *      hNotification:  Handle to be used for notification.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hNode.
 *      DSP_EMEMORY:    Insufficient memory on GPP.
 *      DSP_EVALUE:     uEventMask is invalid.
 *      DSP_ENOTIMPL:   Notification type specified by uNotifyType is not
 *                      supported.
 *  Requires:
 *      NODE_Init(void) called.
 *      hNotification != NULL.
 *  Ensures:
 */
	extern DSP_STATUS NODE_RegisterNotify(struct NODE_OBJECT *hNode,
					      u32 uEventMask, u32 uNotifyType,
					      struct DSP_NOTIFICATION
					      *hNotification);

/*
 *  ======== NODE_Run ========
 *  Purpose:
 *      Start execution of a node's execute phase, or resume execution of
 *      a node that has been suspended (via NODE_Pause()) on the DSP. Load
 *      the node's execute function if necessary.
 *  Parameters:
 *      hNode:              Node object representing a node currently
 *                          running on the DSP.
 *  Returns:
 *      DSP_SOK:            Success.
 *      DSP_EHANDLE:        Invalid hNode.
 *      DSP_ENODETYPE:      hNode doesn't represent a message, task or dais
 *                          socket node.
 *      DSP_ETIMEOUT:       A timeout occurred before the DSP responded.
 *      DSP_EWRONGSTSATE:   Node is not in NODE_PAUSED or NODE_CREATED state.
 *      DSP_EFAIL:          Unable to start or resume execution.
 *      DSP_ESYMBOL:        Execute function not found in the COFF file.
 *  Requires:
 *      NODE_Init(void) called.
 *  Ensures:
 */
	extern DSP_STATUS NODE_Run(struct NODE_OBJECT *hNode);

/*
 *  ======== NODE_Terminate ========
 *  Purpose:
 *      Signal a node running on the DSP that it should exit its execute
 *      phase function.
 *  Parameters:
 *      hNode:              Node object representing a node currently
 *                          running on the DSP.
 *      pStatus:            Location to store execute-phase function return
 *                          value (DSP_EUSER1-16).
 *  Returns:
 *      DSP_SOK:            Success.
 *      DSP_EHANDLE:        Invalid hNode.
 *      DSP_ETIMEOUT:       A timeout occurred before the DSP responded.
 *      DSP_ENODETYPE:      Type of node specified cannot be terminated.
 *      DSP_EWRONGSTATE:    Operation not valid for the current node state.
 *      DSP_EFAIL:          Unable to terminate the node.
 *  Requires:
 *      NODE_Init(void) called.
 *      pStatus != NULL.
 *  Ensures:
 */
	extern DSP_STATUS NODE_Terminate(struct NODE_OBJECT *hNode,
					 OUT DSP_STATUS *pStatus);



/*
 *  ======== NODE_GetUUIDProps ========
 *  Purpose:
 *      Fetch Node properties given the UUID
 *  Parameters:
 *
 */
	extern DSP_STATUS NODE_GetUUIDProps(DSP_HPROCESSOR hProcessor,
					    IN CONST struct DSP_UUID *pNodeId,
					    OUT struct DSP_NDBPROPS
					    *pNodeProps);

#endif				/* NODE_ */
