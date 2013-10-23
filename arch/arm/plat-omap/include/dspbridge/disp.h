/*
 * disp.h
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
 *  ======== disp.h ========
 *
 *  Description:
 *      DSP/BIOS Bridge Node Dispatcher.
 *
 *  Public Functions:
 *      DISP_Create
 *      DISP_Delete
 *      DISP_Exit
 *      DISP_Init
 *      DISP_NodeChangePriority
 *      DISP_NodeCreate
 *      DISP_NodeDelete
 *      DISP_NodeRun
 *
 *! Revision History:
 *! =================
 *! 28-Jan-2003 map     Removed DISP_DoCinit().
 *! 15-May-2002 jeh     Added DISP_DoCinit().
 *! 24-Apr-2002 jeh     Added DISP_MemWrite().
 *! 07-Sep-2001 jeh     Added DISP_MemCopy().
 *! 10-May-2001 jeh     Code review cleanup.
 *! 08-Aug-2000 jeh     Removed DISP_NodeTerminate since it no longer uses RMS.
 *! 17-Jul-2000 jeh     Updates to function headers.
 *! 19-Jun-2000 jeh     Created.
 */

#ifndef DISP_
#define DISP_

#include <dspbridge/dbdefs.h>
#include <dspbridge/nodedefs.h>
#include <dspbridge/nodepriv.h>
#include <dspbridge/dispdefs.h>

/*
 *  ======== DISP_Create ========
 *  Create a NODE Dispatcher object. This object handles the creation,
 *  deletion, and execution of nodes on the DSP target, through communication
 *  with the Resource Manager Server running on the target. Each NODE
 *  Manager object should have exactly one NODE Dispatcher.
 *
 *  Parameters:
 *      phDispObject:   Location to store node dispatcher object on output.
 *      hDevObject:     Device for this processor.
 *      pDispAttrs:     Node dispatcher attributes.
 *  Returns:
 *      DSP_SOK:                Success;
 *      DSP_EMEMORY:            Insufficient memory for requested resources.
 *      DSP_EFAIL:              Unable to create dispatcher.
 *  Requires:
 *      DISP_Init(void) called.
 *      pDispAttrs != NULL.
 *      hDevObject != NULL.
 *      phDispObject != NULL.
 *  Ensures:
 *      DSP_SOK:        IsValid(*phDispObject).
 *      error:          *phDispObject == NULL.
 */
	extern DSP_STATUS DISP_Create(OUT struct DISP_OBJECT **phDispObject,
				      struct DEV_OBJECT *hDevObject,
				      IN CONST struct DISP_ATTRS *pDispAttrs);

/*
 *  ======== DISP_Delete ========
 *  Delete the NODE Dispatcher.
 *
 *  Parameters:
 *      hDispObject:  Node Dispatcher object.
 *  Returns:
 *  Requires:
 *      DISP_Init(void) called.
 *      Valid hDispObject.
 *  Ensures:
 *      hDispObject is invalid.
 */
	extern void DISP_Delete(struct DISP_OBJECT *hDispObject);

/*
 *  ======== DISP_Exit ========
 *  Discontinue usage of DISP module.
 *
 *  Parameters:
 *  Returns:
 *  Requires:
 *      DISP_Init(void) previously called.
 *  Ensures:
 *      Any resources acquired in DISP_Init(void) will be freed when last DISP
 *      client calls DISP_Exit(void).
 */
	extern void DISP_Exit(void);

/*
 *  ======== DISP_Init ========
 *  Initialize the DISP module.
 *
 *  Parameters:
 *  Returns:
 *      TRUE if initialization succeeded, FALSE otherwise.
 *  Ensures:
 */
	extern bool DISP_Init(void);

/*
 *  ======== DISP_NodeChangePriority ========
 *  Change the priority of a node currently running on the target.
 *
 *  Parameters:
 *      hDispObject:            Node Dispatcher object.
 *      hNode:                  Node object representing a node currently
 *                              allocated or running on the DSP.
 *      ulFxnAddress:           Address of RMS function for changing priority.
 *      nodeEnv:                Address of node's environment structure.
 *      nPriority:              New priority level to set node's priority to.
 *  Returns:
 *      DSP_SOK:                Success.
 *      DSP_ETIMEOUT:           A timeout occurred before the DSP responded.
 *  Requires:
 *      DISP_Init(void) called.
 *      Valid hDispObject.
 *      hNode != NULL.
 *  Ensures:
 */
	extern DSP_STATUS DISP_NodeChangePriority(struct DISP_OBJECT
						  *hDispObject,
						  struct NODE_OBJECT *hNode,
						  u32 ulFxnAddr,
						  NODE_ENV nodeEnv,
						  s32 nPriority);

/*
 *  ======== DISP_NodeCreate ========
 *  Create a node on the DSP by remotely calling the node's create function.
 *
 *  Parameters:
 *      hDispObject:    Node Dispatcher object.
 *      hNode:          Node handle obtained from NODE_Allocate().
 *      ulFxnAddr:      Address or RMS create node function.
 *      ulCreateFxn:    Address of node's create function.
 *      pArgs:          Arguments to pass to RMS node create function.
 *      pNodeEnv:       Location to store node environment pointer on
 *                      output.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_ETASK:      Unable to create the node's task or process on the DSP.
 *      DSP_ESTREAM:    Stream creation failure on the DSP.
 *      DSP_ETIMEOUT:   A timeout occurred before the DSP responded.
 *      DSP_EUSER:      A user-defined failure occurred.
 *      DSP_EFAIL:      A failure occurred, unable to create node.
 *  Requires:
 *      DISP_Init(void) called.
 *      Valid hDispObject.
 *      pArgs != NULL.
 *      hNode != NULL.
 *      pNodeEnv != NULL.
 *      NODE_GetType(hNode) != NODE_DEVICE.
 *  Ensures:
 */
	extern DSP_STATUS DISP_NodeCreate(struct DISP_OBJECT *hDispObject,
					  struct NODE_OBJECT *hNode,
					  u32 ulFxnAddr,
					  u32 ulCreateFxn,
					  IN CONST struct NODE_CREATEARGS
					  *pArgs,
					  OUT NODE_ENV *pNodeEnv);

/*
 *  ======== DISP_NodeDelete ========
 *  Delete a node on the DSP by remotely calling the node's delete function.
 *
 *  Parameters:
 *      hDispObject:    Node Dispatcher object.
 *      hNode:          Node object representing a node currently
 *                      loaded on the DSP.
 *      ulFxnAddr:      Address or RMS delete node function.
 *      ulDeleteFxn:    Address of node's delete function.
 *      nodeEnv:        Address of node's environment structure.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_ETIMEOUT:   A timeout occurred before the DSP responded.
 *  Requires:
 *      DISP_Init(void) called.
 *      Valid hDispObject.
 *      hNode != NULL.
 *  Ensures:
 */
	extern DSP_STATUS DISP_NodeDelete(struct DISP_OBJECT *hDispObject,
					  struct NODE_OBJECT *hNode,
					  u32 ulFxnAddr,
					  u32 ulDeleteFxn, NODE_ENV nodeEnv);

/*
 *  ======== DISP_NodeRun ========
 *  Start execution of a node's execute phase, or resume execution of a node
 *  that has been suspended (via DISP_NodePause()) on the DSP.
 *
 *  Parameters:
 *      hDispObject:    Node Dispatcher object.
 *      hNode:          Node object representing a node to be executed
 *                      on the DSP.
 *      ulFxnAddr:      Address or RMS node execute function.
 *      ulExecuteFxn:   Address of node's execute function.
 *      nodeEnv:        Address of node's environment structure.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_ETIMEOUT:   A timeout occurred before the DSP responded.
 *  Requires:
 *      DISP_Init(void) called.
 *      Valid hDispObject.
 *      hNode != NULL.
 *  Ensures:
 */
	extern DSP_STATUS DISP_NodeRun(struct DISP_OBJECT *hDispObject,
				       struct NODE_OBJECT *hNode,
				       u32 ulFxnAddr,
				       u32 ulExecuteFxn, NODE_ENV nodeEnv);

#endif				/* DISP_ */
