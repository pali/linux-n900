/*
 * node.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * DSP/BIOS Bridge Node Manager.
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

#ifndef NODE_
#define NODE_

#include <dspbridge/procpriv.h>

#include <dspbridge/nodedefs.h>
#include <dspbridge/dispdefs.h>
#include <dspbridge/nldrdefs.h>
#include <dspbridge/drv.h>

/*
 *  ======== node_allocate ========
 *  Purpose:
 *      Allocate GPP resources to manage a node on the DSP.
 *  Parameters:
 *      hprocessor:         Handle of processor that is allocating the node.
 *      pNodeId:            Pointer to a dsp_uuid for the node.
 *      pargs:              Optional arguments to be passed to the node.
 *      attr_in:            Optional pointer to node attributes (priority,
 *                          timeout...)
 *      ph_node:             Location to store node handle on output.
 *  Returns:
 *      DSP_SOK:            Success.
 *      DSP_EMEMORY:        Insufficient memory on GPP.
 *      DSP_EUUID:          Node UUID has not been registered.
 *      DSP_ESYMBOL:        iAlg functions not found for a DAIS node.
 *      DSP_ERANGE:         attr_in != NULL and attr_in->prio out of
 *                          range.
 *      DSP_EFAIL:          A failure occured, unable to allocate node.
 *      DSP_EWRONGSTATE:    Proccessor is not in the running state.
 *  Requires:
 *      node_init(void) called.
 *      hprocessor != NULL.
 *      pNodeId != NULL.
 *      ph_node != NULL.
 *  Ensures:
 *      DSP_SOK:            IsValidNode(*ph_node).
 *      error:              *ph_node == NULL.
 */
extern dsp_status node_allocate(struct proc_object *hprocessor,
				IN CONST struct dsp_uuid *pNodeId,
				OPTIONAL IN CONST struct dsp_cbdata
				*pargs, OPTIONAL IN CONST struct dsp_nodeattrin
				*attr_in,
				OUT struct node_object **ph_node,
				struct process_context *pr_ctxt);

/*
 *  ======== node_alloc_msg_buf ========
 *  Purpose:
 *      Allocate and Prepare a buffer whose descriptor will be passed to a
 *      Node within a (dsp_msg)message
 *  Parameters:
 *      hnode:          The node handle.
 *      usize:          The size of the buffer to be allocated.
 *      pattr:          Pointer to a dsp_bufferattr structure.
 *      pbuffer:        Location to store the address of the allocated
 *                      buffer on output.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid node handle.
 *      DSP_EMEMORY:    Insufficent memory.
 *      DSP_EFAIL:      General Failure.
 *      DSP_ESIZE:      Invalid Size.
 *  Requires:
 *      node_init(void) called.
 *      pbuffer != NULL.
 *  Ensures:
 */
extern dsp_status node_alloc_msg_buf(struct node_object *hnode,
				     u32 usize, OPTIONAL struct dsp_bufferattr
				     *pattr, OUT u8 **pbuffer);

/*
 *  ======== node_change_priority ========
 *  Purpose:
 *      Change the priority of an allocated node.
 *  Parameters:
 *      hnode:              Node handle returned from node_allocate.
 *      prio:          New priority level to set node's priority to.
 *  Returns:
 *      DSP_SOK:            Success.
 *      DSP_EHANDLE:        Invalid hnode.
 *      DSP_ERANGE:         prio is out of range.
 *      DSP_ENODETYPE:      The specified node is not a task node.
 *      DSP_EWRONGSTATE:    Node is not in the NODE_ALLOCATED, NODE_PAUSED,
 *                          or NODE_RUNNING state.
 *      DSP_ETIMEOUT:       A timeout occurred before the DSP responded.
 *      DSP_ERESTART:       A critical error has occurred and the DSP is
 *                          being restarted.
 *      DSP_EFAIL:          Unable to change node's runtime priority level.
 *  Requires:
 *      node_init(void) called.
 *  Ensures:
 *      DSP_SOK && (Node's current priority == prio)
 */
extern dsp_status node_change_priority(struct node_object *hnode, s32 prio);

/*
 *  ======== node_close_orphans ========
 *  Purpose:
 *      Delete all nodes whose owning processor is being destroyed.
 *  Parameters:
 *      hnode_mgr:       Node manager object.
 *      hProc:          Handle to processor object being destroyed.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EFAIL:      Unable to delete all nodes belonging to hProc.
 *  Requires:
 *      Valid hnode_mgr.
 *      hProc != NULL.
 *  Ensures:
 */
extern dsp_status node_close_orphans(struct node_mgr *hnode_mgr,
				     struct proc_object *hProc);

/*
 *  ======== node_connect ========
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
 *                      node_allocate().
 *      uStream1:       Output stream index on first node, to be connected
 *                      to second node's input stream. Value must range from
 *                      0 <= uStream1 < number of output streams.
 *      uStream2:       Input stream index on second node. Value must range
 *                      from 0 <= uStream2 < number of input streams.
 *      pattrs:         Stream attributes (NULL ==> use defaults).
 *      conn_param:     A pointer to a dsp_cbdata structure that defines
 *                      connection parameter for device nodes to pass to DSP
 *                      side.
 *                      If the value of this parameter is NULL, then this API
 *                      behaves like DSPNode_Connect. This parameter will have
 *                      length of the string and the null terminated string in
 *                      dsp_cbdata struct. This can be extended in future tp
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
 *      node_init(void) called.
 *  Ensures:
 */
extern dsp_status node_connect(struct node_object *hNode1,
			       u32 uStream1,
			       struct node_object *hNode2,
			       u32 uStream2,
			       OPTIONAL IN struct dsp_strmattr *pattrs,
			       OPTIONAL IN struct dsp_cbdata
			       *conn_param);

/*
 *  ======== node_create ========
 *  Purpose:
 *      Create a node on the DSP by remotely calling the node's create
 *      function. If necessary, load code that contains the node's create
 *      function.
 *  Parameters:
 *      hnode:              Node handle returned from node_allocate().
 *  Returns:
 *      DSP_SOK:            Success.
 *      DSP_EHANDLE:        Invalid hnode.
 *      DSP_ESYMBOL:        Create function not found in the COFF file.
 *      DSP_EWRONGSTATE:    Node is not in the NODE_ALLOCATED state.
 *      DSP_EMEMORY:        Memory allocation failure on the DSP.
 *      DSP_ETASK:          Unable to create node's task or process on the DSP.
 *      DSP_ESTREAM:        Stream creation failure on the DSP.
 *      DSP_ETIMEOUT:       A timeout occurred before the DSP responded.
 *      DSP_EUSER1-16:      A user-defined failure occurred on the DSP.
 *      DSP_EFAIL:          A failure occurred, unable to create node.
 *  Requires:
 *      node_init(void) called.
 *  Ensures:
 */
extern dsp_status node_create(struct node_object *hnode);

/*
 *  ======== node_create_mgr ========
 *  Purpose:
 *      Create a NODE Manager object. This object handles the creation,
 *      deletion, and execution of nodes on the DSP target. The NODE Manager
 *      also maintains a pipe map of used and available node connections.
 *      Each DEV object should have exactly one NODE Manager object.
 *
 *  Parameters:
 *      phNodeMgr:      Location to store node manager handle on output.
 *      hdev_obj:     Device for this processor.
 *  Returns:
 *      DSP_SOK:        Success;
 *      DSP_EMEMORY:    Insufficient memory for requested resources.
 *      DSP_EFAIL:      General failure.
 *  Requires:
 *      node_init(void) called.
 *      phNodeMgr != NULL.
 *      hdev_obj != NULL.
 *  Ensures:
 *      DSP_SOK:        Valide *phNodeMgr.
 *      error:          *phNodeMgr == NULL.
 */
extern dsp_status node_create_mgr(OUT struct node_mgr **phNodeMgr,
				  struct dev_object *hdev_obj);

/*
 *  ======== node_delete ========
 *  Purpose:
 *      Delete resources allocated in node_allocate(). If the node was
 *      created, delete the node on the DSP by remotely calling the node's
 *      delete function. Loads the node's delete function if necessary.
 *      GPP side resources are freed after node's delete function returns.
 *  Parameters:
 *      hnode:              Node handle returned from node_allocate().
 *  Returns:
 *      DSP_SOK:            Success.
 *      DSP_EHANDLE:        Invalid hnode.
 *      DSP_ETIMEOUT:       A timeout occurred before the DSP responded.
 *      DSP_EDELETE:        A deletion failure occurred.
 *      DSP_EUSER1-16:      Node specific failure occurred on the DSP.
 *      DSP_EFAIL:          A failure occurred in deleting the node.
 *      DSP_ESYMBOL:        Delete function not found in the COFF file.
 *  Requires:
 *      node_init(void) called.
 *  Ensures:
 *      DSP_SOK:            hnode is invalid.
 */
extern dsp_status node_delete(struct node_object *hnode,
			      struct process_context *pr_ctxt);

/*
 *  ======== node_delete_mgr ========
 *  Purpose:
 *      Delete the NODE Manager.
 *  Parameters:
 *      hnode_mgr:       Node manager object.
 *  Returns:
 *      DSP_SOK:        Success.
 *  Requires:
 *      node_init(void) called.
 *      Valid hnode_mgr.
 *  Ensures:
 */
extern dsp_status node_delete_mgr(struct node_mgr *hnode_mgr);

/*
 *  ======== node_enum_nodes ========
 *  Purpose:
 *      Enumerate the nodes currently allocated for the DSP.
 *  Parameters:
 *      hnode_mgr:       Node manager returned from node_create_mgr().
 *      node_tab:       Array to copy node handles into.
 *      node_tab_size:   Number of handles that can be written to node_tab.
 *      pu_num_nodes:     Location where number of node handles written to
 *                      node_tab will be written.
 *      pu_allocated:    Location to write total number of allocated nodes.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_ESIZE:      node_tab is too small to hold all node handles.
 *  Requires:
 *      Valid hnode_mgr.
 *      node_tab != NULL || node_tab_size == 0.
 *      pu_num_nodes != NULL.
 *      pu_allocated != NULL.
 *  Ensures:
 *      - (DSP_ESIZE && *pu_num_nodes == 0)
 *      - || (DSP_SOK && *pu_num_nodes <= node_tab_size)  &&
 *        (*pu_allocated == *pu_num_nodes)
 */
extern dsp_status node_enum_nodes(struct node_mgr *hnode_mgr,
				  void **node_tab,
				  u32 node_tab_size,
				  OUT u32 *pu_num_nodes,
				  OUT u32 *pu_allocated);

/*
 *  ======== node_exit ========
 *  Purpose:
 *      Discontinue usage of NODE module.
 *  Parameters:
 *  Returns:
 *  Requires:
 *      node_init(void) successfully called before.
 *  Ensures:
 *      Any resources acquired in node_init(void) will be freed when last NODE
 *      client calls node_exit(void).
 */
extern void node_exit(void);

/*
 *  ======== node_free_msg_buf ========
 *  Purpose:
 *      Free a message buffer previously allocated with node_alloc_msg_buf.
 *  Parameters:
 *      hnode:          The node handle.
 *      pbuffer:        (Address) Buffer allocated by node_alloc_msg_buf.
 *      pattr:          Same buffer attributes passed to node_alloc_msg_buf.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid node handle.
 *      DSP_EFAIL:      Failure to free the buffer.
 *  Requires:
 *      node_init(void) called.
 *      pbuffer != NULL.
 *  Ensures:
 */
extern dsp_status node_free_msg_buf(struct node_object *hnode,
				    IN u8 *pbuffer,
				    OPTIONAL struct dsp_bufferattr
				    *pattr);

/*
 *  ======== node_get_attr ========
 *  Purpose:
 *      Copy the current attributes of the specified node into a dsp_nodeattr
 *      structure.
 *  Parameters:
 *      hnode:          Node object allocated from node_allocate().
 *      pattr:          Pointer to dsp_nodeattr structure to copy node's
 *                      attributes.
 *      attr_size:      Size of pattr.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hnode.
 *  Requires:
 *      node_init(void) called.
 *      pattr != NULL.
 *  Ensures:
 *      DSP_SOK:        *pattrs contains the node's current attributes.
 */
extern dsp_status node_get_attr(struct node_object *hnode,
				OUT struct dsp_nodeattr *pattr, u32 attr_size);

/*
 *  ======== node_get_message ========
 *  Purpose:
 *      Retrieve a message from a node on the DSP. The node must be either a
 *      message node, task node, or XDAIS socket node.
 *      If a message is not available, this function will block until a
 *      message is available, or the node's timeout value is reached.
 *  Parameters:
 *      hnode:          Node handle returned from node_allocate().
 *      message:       Pointer to dsp_msg structure to copy the
 *                      message into.
 *      utimeout:       Timeout in milliseconds to wait for message.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hnode.
 *      DSP_ENODETYPE:  Cannot retrieve messages from this type of node.
 *      DSP_ETIMEOUT:   Timeout occurred and no message is available.
 *      DSP_EFAIL:      Error occurred while trying to retrieve a message.
 *  Requires:
 *      node_init(void) called.
 *      message != NULL.
 *  Ensures:
 */
extern dsp_status node_get_message(struct node_object *hnode,
				   OUT struct dsp_msg *message, u32 utimeout);

/*
 *  ======== node_get_nldr_obj ========
 *  Purpose:
 *      Retrieve the Nldr manager
 *  Parameters:
 *      hnode_mgr:       Node Manager
 *      phNldrObj:      Pointer to a Nldr manager handle
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hnode.
 *  Ensures:
 */
extern dsp_status node_get_nldr_obj(struct node_mgr *hnode_mgr,
				    OUT struct nldr_object **phNldrObj);

/*
 *  ======== node_init ========
 *  Purpose:
 *      Initialize the NODE module.
 *  Parameters:
 *  Returns:
 *      TRUE if initialization succeeded, FALSE otherwise.
 *  Ensures:
 */
extern bool node_init(void);

/*
 *  ======== node_on_exit ========
 *  Purpose:
 *      Gets called when RMS_EXIT is received for a node. PROC needs to pass
 *      this function as a parameter to msg_create(). This function then gets
 *      called by the mini-driver when an exit message for a node is received.
 *  Parameters:
 *      hnode:      Handle of the node that the exit message is for.
 *      nStatus:    Return status of the node's execute phase.
 *  Returns:
 *  Ensures:
 */
void node_on_exit(struct node_object *hnode, s32 nStatus);

/*
 *  ======== node_pause ========
 *  Purpose:
 *      Suspend execution of a node currently running on the DSP.
 *  Parameters:
 *      hnode:              Node object representing a node currently
 *                          running on the DSP.
 *  Returns:
 *      DSP_SOK:            Success.
 *      DSP_EHANDLE:        Invalid hnode.
 *      DSP_ENODETYPE:      Node is not a task or socket node.
 *      DSP_ETIMEOUT:       A timeout occurred before the DSP responded.
 *      DSP_EWRONGSTSATE:   Node is not in NODE_RUNNING state.
 *      DSP_EFAIL:          Failed to pause node.
 *  Requires:
 *      node_init(void) called.
 *  Ensures:
 */
extern dsp_status node_pause(struct node_object *hnode);

/*
 *  ======== node_put_message ========
 *  Purpose:
 *      Send a message to a message node, task node, or XDAIS socket node.
 *      This function will block until the message stream can accommodate
 *      the message, or a timeout occurs. The message will be copied, so Msg
 *      can be re-used immediately after return.
 *  Parameters:
 *      hnode:              Node handle returned by node_allocate().
 *      pmsg:               Location of message to be sent to the node.
 *      utimeout:           Timeout in msecs to wait.
 *  Returns:
 *      DSP_SOK:            Success.
 *      DSP_EHANDLE:        Invalid hnode.
 *      DSP_ENODETYPE:      Messages can't be sent to this type of node.
 *      DSP_ETIMEOUT:       Timeout occurred before message could be set.
 *      DSP_EWRONGSTATE:    Node is in invalid state for sending messages.
 *      DSP_EFAIL:          Unable to send message.
 *  Requires:
 *      node_init(void) called.
 *      pmsg != NULL.
 *  Ensures:
 */
extern dsp_status node_put_message(struct node_object *hnode,
				   IN CONST struct dsp_msg *pmsg, u32 utimeout);

/*
 *  ======== node_register_notify ========
 *  Purpose:
 *      Register to be notified on specific events for this node.
 *  Parameters:
 *      hnode:          Node handle returned by node_allocate().
 *      event_mask:     Mask of types of events to be notified about.
 *      notify_type:    Type of notification to be sent.
 *      hnotification:  Handle to be used for notification.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hnode.
 *      DSP_EMEMORY:    Insufficient memory on GPP.
 *      DSP_EVALUE:     event_mask is invalid.
 *      DSP_ENOTIMPL:   Notification type specified by notify_type is not
 *                      supported.
 *  Requires:
 *      node_init(void) called.
 *      hnotification != NULL.
 *  Ensures:
 */
extern dsp_status node_register_notify(struct node_object *hnode,
				       u32 event_mask, u32 notify_type,
				       struct dsp_notification
				       *hnotification);

/*
 *  ======== node_run ========
 *  Purpose:
 *      Start execution of a node's execute phase, or resume execution of
 *      a node that has been suspended (via node_pause()) on the DSP. Load
 *      the node's execute function if necessary.
 *  Parameters:
 *      hnode:              Node object representing a node currently
 *                          running on the DSP.
 *  Returns:
 *      DSP_SOK:            Success.
 *      DSP_EHANDLE:        Invalid hnode.
 *      DSP_ENODETYPE:      hnode doesn't represent a message, task or dais
 *                          socket node.
 *      DSP_ETIMEOUT:       A timeout occurred before the DSP responded.
 *      DSP_EWRONGSTSATE:   Node is not in NODE_PAUSED or NODE_CREATED state.
 *      DSP_EFAIL:          Unable to start or resume execution.
 *      DSP_ESYMBOL:        Execute function not found in the COFF file.
 *  Requires:
 *      node_init(void) called.
 *  Ensures:
 */
extern dsp_status node_run(struct node_object *hnode);

/*
 *  ======== node_terminate ========
 *  Purpose:
 *      Signal a node running on the DSP that it should exit its execute
 *      phase function.
 *  Parameters:
 *      hnode:              Node object representing a node currently
 *                          running on the DSP.
 *      pstatus:            Location to store execute-phase function return
 *                          value (DSP_EUSER1-16).
 *  Returns:
 *      DSP_SOK:            Success.
 *      DSP_EHANDLE:        Invalid hnode.
 *      DSP_ETIMEOUT:       A timeout occurred before the DSP responded.
 *      DSP_ENODETYPE:      Type of node specified cannot be terminated.
 *      DSP_EWRONGSTATE:    Operation not valid for the current node state.
 *      DSP_EFAIL:          Unable to terminate the node.
 *  Requires:
 *      node_init(void) called.
 *      pstatus != NULL.
 *  Ensures:
 */
extern dsp_status node_terminate(struct node_object *hnode,
				 OUT dsp_status *pstatus);

/*
 *  ======== node_get_uuid_props ========
 *  Purpose:
 *      Fetch Node properties given the UUID
 *  Parameters:
 *
 */
extern dsp_status node_get_uuid_props(void *hprocessor,
				      IN CONST struct dsp_uuid *pNodeId,
				      OUT struct dsp_ndbprops
				      *node_props);

#endif /* NODE_ */
