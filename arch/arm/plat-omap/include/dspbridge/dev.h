/*
 * dev.h
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
 *  ======== dev.h ========
 *  Description:
 *      'Bridge Mini-driver device operations.
 *
 *  Public Functions:
 *      DEV_BrdWriteFxn
 *      DEV_CreateDevice
 *      DEV_Create2
 *      DEV_Destroy2
 *      DEV_DestroyDevice
 *      DEV_GetChnlMgr
 *      DEV_GetCmmMgr
 *      DEV_GetCodMgr
 *      DEV_GetDehMgr
 *      DEV_GetDevNode
 *      DEV_GetDSPWordSize
 *      DEV_GetFirst
 *      DEV_GetIntfFxns
 *      DEV_GetIOMgr
 *      DEV_GetMsgMgr
 *      DEV_GetNext
 *      DEV_GetNodeManager
 *      DEV_GetSymbol
 *      DEV_GetWMDContext
 *      DEV_Exit
 *      DEV_Init
 *      DEV_InsertProcObject
 *      DEV_IsLocked
 *      DEV_NotifyClient
 *      DEV_RegisterNotify
 *      DEV_ReleaseCodMgr
 *      DEV_RemoveDevice
 *      DEV_RemoveProcObject
 *      DEV_SetChnlMgr
 *      DEV_SetMsgMgr
 *      DEV_SetLockOwner
 *      DEV_StartDevice
 *
 *! Revision History:
 *! ================
 *! 08-Mar-2004 sb  Added the Dynamic Memory Mapping feature - Dev_GetDmmMgr
 *! 09-Feb-2004 vp  Added functions required for IVA
 *! 25-Feb-2003 swa PMGR Code Review changes incorporated
 *! 05-Nov-2001 kc: Added DEV_GetDehMgr.
 *! 05-Dec-2000 jeh Added DEV_SetMsgMgr.
 *! 29-Nov-2000 rr: Incorporated code review changes.
 *! 17-Nov-2000 jeh Added DEV_GetMsgMgr.
 *! 05-Oct-2000 rr: DEV_Create2 & DEV_Destroy2 Added.
 *! 02-Oct-2000 rr: Added DEV_GetNodeManager.
 *! 11-Aug-2000 ag: Added DEV_GetCmmMgr() for shared memory management.
 *! 10-Aug-2000 rr: DEV_InsertProcObject/RemoveProcObject added.
 *! 06-Jun-2000 jeh Added DEV_GetSymbol().
 *! 05-Nov-1999 kc: Updated function prototypes.
 *! 08-Oct-1997 cr: Added explicit CDECL function identifiers.
 *! 07-Nov-1996 gp: Updated for code review.
 *! 22-Oct-1996 gp: Added DEV_CleanupProcessState().
 *! 29-May-1996 gp: Changed DEV_HDEVNODE --> CFG_HDEVNODE.
 *! 18-May-1996 gp: Created.
 */

#ifndef DEV_
#define DEV_

/*  ----------------------------------- Module Dependent Headers */
#include <dspbridge/chnldefs.h>
#include <dspbridge/cmm.h>
#include <dspbridge/cod.h>
#include <dspbridge/dehdefs.h>
#include <dspbridge/nodedefs.h>
#include <dspbridge/dispdefs.h>
#include <dspbridge/wmd.h>
#include <dspbridge/dmm.h>
#include <dspbridge/host_os.h>

/*  ----------------------------------- This */
#include <dspbridge/devdefs.h>


/*
 *  ======== DEV_BrdWriteFxn ========
 *  Purpose:
 *      Exported function to be used as the COD write function.  This function
 *      is passed a handle to a DEV_hObject by ZL in pArb, then calls the
 *      device's WMD_BRD_Write() function.
 *  Parameters:
 *      pArb:           Handle to a Device Object.
 *      hDevContext:    Handle to mini-driver defined device info.
 *      dwDSPAddr:      Address on DSP board (Destination).
 *      pHostBuf:       Pointer to host buffer (Source).
 *      ulNumBytes:     Number of bytes to transfer.
 *      ulMemType:      Memory space on DSP to which to transfer.
 *  Returns:
 *      Number of bytes written.  Returns 0 if the DEV_hObject passed in via
 *      pArb is invalid.
 *  Requires:
 *      DEV Initialized.
 *      pHostBuf != NULL
 *  Ensures:
 */
       extern u32 DEV_BrdWriteFxn(void *pArb,
					   u32 ulDspAddr,
					   void *pHostBuf,
					   u32 ulNumBytes, u32 nMemSpace);

/*
 *  ======== DEV_CreateDevice ========
 *  Purpose:
 *      Called by the operating system to load the 'Bridge Mini Driver for a
 *      'Bridge device.
 *  Parameters:
 *      phDevObject:    Ptr to location to receive the device object handle.
 *      pstrWMDFileName: Name of WMD PE DLL file to load.  If the absolute
 *                      path is not provided, the file is loaded through
 *                      'Bridge's module search path.
 *      pHostConfig:    Host configuration information, to be passed down
 *                      to the WMD when WMD_DEV_Create() is called.
 *      pDspConfig:     DSP resources, to be passed down to the WMD when
 *                      WMD_DEV_Create() is called.
 *      hDevNode:       Platform (Windows) specific device node.
 *  Returns:
 *      DSP_SOK:            Module is loaded, device object has been created
 *      DSP_EMEMORY:        Insufficient memory to create needed resources.
 *      DEV_E_NEWWMD:       The WMD was compiled for a newer version of WCD.
 *      DEV_E_NULLWMDINTF:  WMD passed back a NULL Fxn Interface Struct Ptr
 *      DEV_E_NOCODMODULE:  No ZL file name was specified in the registry
 *                          for this hDevNode.
 *      LDR_E_FILEUNABLETOOPEN: Unable to open the specified WMD.
 *      LDR_E_NOMEMORY:         PELDR is out of resources.
 *      DSP_EFAIL:              Unable to find WMD entry point function.
 *      COD_E_NOZLFUNCTIONS:    One or more ZL functions exports not found.
 *      COD_E_ZLCREATEFAILED:   Unable to load ZL DLL.
 *  Requires:
 *      DEV Initialized.
 *      phDevObject != NULL.
 *      pstrWMDFileName != NULL.
 *      pHostConfig != NULL.
 *      pDspConfig != NULL.
 *  Ensures:
 *      DSP_SOK:  *phDevObject will contain handle to the new device object.
 *      Otherwise, does not create the device object, ensures the WMD module is
 *      unloaded, and sets *phDevObject to NULL.
 */
       extern DSP_STATUS DEV_CreateDevice(OUT struct DEV_OBJECT
						 **phDevObject,
						 IN CONST char *pstrWMDFileName,
						 IN CONST struct CFG_HOSTRES
						 *pHostConfig,
						 IN CONST struct CFG_DSPRES
						 *pDspConfig,
						 struct CFG_DEVNODE *hDevNode);

/*
 *  ======== DEV_CreateIVADevice ========
 *  Purpose:
 *      Called by the operating system to load the 'Bridge Mini Driver for IVA.
 *  Parameters:
 *      phDevObject:    Ptr to location to receive the device object handle.
 *      pstrWMDFileName: Name of WMD PE DLL file to load.  If the absolute
 *                      path is not provided, the file is loaded through
 *                      'Bridge's module search path.
 *      pHostConfig:    Host configuration information, to be passed down
 *                      to the WMD when WMD_DEV_Create() is called.
 *      pDspConfig:     DSP resources, to be passed down to the WMD when
 *                      WMD_DEV_Create() is called.
 *      hDevNode:       Platform (Windows) specific device node.
 *  Returns:
 *      DSP_SOK:            Module is loaded, device object has been created
 *      DSP_EMEMORY:        Insufficient memory to create needed resources.
 *      DEV_E_NEWWMD:       The WMD was compiled for a newer version of WCD.
 *      DEV_E_NULLWMDINTF:  WMD passed back a NULL Fxn Interface Struct Ptr
 *      DEV_E_NOCODMODULE:  No ZL file name was specified in the registry
 *                          for this hDevNode.
 *      LDR_E_FILEUNABLETOOPEN: Unable to open the specified WMD.
 *      LDR_E_NOMEMORY:         PELDR is out of resources.
 *      DSP_EFAIL:              Unable to find WMD entry point function.
 *      COD_E_NOZLFUNCTIONS:    One or more ZL functions exports not found.
 *      COD_E_ZLCREATEFAILED:   Unable to load ZL DLL.
 *  Requires:
 *      DEV Initialized.
 *      phDevObject != NULL.
 *      pstrWMDFileName != NULL.
 *      pHostConfig != NULL.
 *      pDspConfig != NULL.
 *  Ensures:
 *      DSP_SOK:  *phDevObject will contain handle to the new device object.
 *      Otherwise, does not create the device object, ensures the WMD module is
 *      unloaded, and sets *phDevObject to NULL.
 */
       extern DSP_STATUS DEV_CreateIVADevice(OUT struct DEV_OBJECT
				**phDevObject,
				IN CONST char *pstrWMDFileName,
				IN CONST struct CFG_HOSTRES *pHostConfig,
				IN CONST struct CFG_DSPRES *pDspConfig,
				struct CFG_DEVNODE *hDevNode);

/*
 *  ======== DEV_Create2 ========
 *  Purpose:
 *      After successful loading of the image from WCD_InitComplete2
 *      (PROC Auto_Start) or PROC_Load this fxn is called. This creates
 *      the Node Manager and updates the DEV Object.
 *  Parameters:
 *      hDevObject: Handle to device object created with DEV_CreateDevice().
 *  Returns:
 *      DSP_SOK:    Successful Creation of Node Manager
 *      DSP_EFAIL:  Some Error Occurred.
 *  Requires:
 *      DEV Initialized
 *      Valid hDevObject
 *  Ensures:
 *      DSP_SOK and hDevObject->hNodeMgr != NULL
 *      else    hDevObject->hNodeMgr == NULL
 */
       extern DSP_STATUS DEV_Create2(IN struct DEV_OBJECT *hDevObject);

/*
 *  ======== DEV_Destroy2 ========
 *  Purpose:
 *      Destroys the Node manager for this device.
 *  Parameters:
 *      hDevObject: Handle to device object created with DEV_CreateDevice().
 *  Returns:
 *      DSP_SOK:    Successful Creation of Node Manager
 *      DSP_EFAIL:  Some Error Occurred.
 *  Requires:
 *      DEV Initialized
 *      Valid hDevObject
 *  Ensures:
 *      DSP_SOK and hDevObject->hNodeMgr == NULL
 *      else    DSP_EFAIL.
 */
       extern DSP_STATUS DEV_Destroy2(IN struct DEV_OBJECT *hDevObject);

/*
 *  ======== DEV_DestroyDevice ========
 *  Purpose:
 *      Destroys the channel manager for this device, if any, calls
 *      WMD_DEV_Destroy(), and then attempts to unload the WMD module.
 *  Parameters:
 *      hDevObject:     Handle to device object created with
 *                      DEV_CreateDevice().
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hDevObject.
 *      DSP_EFAIL:      The WMD failed it's WMD_DEV_Destroy() function.
 *  Requires:
 *      DEV Initialized.
 *  Ensures:
 */
       extern DSP_STATUS DEV_DestroyDevice(struct DEV_OBJECT
						  *hDevObject);

/*
 *  ======== DEV_GetChnlMgr ========
 *  Purpose:
 *      Retrieve the handle to the channel manager created for this device.
 *  Parameters:
 *      hDevObject:     Handle to device object created with
 *                      DEV_CreateDevice().
 *      *phMgr:         Ptr to location to store handle.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hDevObject.
 *  Requires:
 *      phMgr != NULL.
 *      DEV Initialized.
 *  Ensures:
 *      DSP_SOK:        *phMgr contains a handle to a channel manager object,
 *                      or NULL.
 *      else:           *phMgr is NULL.
 */
       extern DSP_STATUS DEV_GetChnlMgr(struct DEV_OBJECT *hDevObject,
					       OUT struct CHNL_MGR **phMgr);

/*
 *  ======== DEV_GetCmmMgr ========
 *  Purpose:
 *      Retrieve the handle to the shared memory manager created for this
 *      device.
 *  Parameters:
 *      hDevObject:     Handle to device object created with
 *                      DEV_CreateDevice().
 *      *phMgr:         Ptr to location to store handle.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hDevObject.
 *  Requires:
 *      phMgr != NULL.
 *      DEV Initialized.
 *  Ensures:
 *      DSP_SOK:        *phMgr contains a handle to a channel manager object,
 *                      or NULL.
 *      else:           *phMgr is NULL.
 */
       extern DSP_STATUS DEV_GetCmmMgr(struct DEV_OBJECT *hDevObject,
					      OUT struct CMM_OBJECT **phMgr);

/*
 *  ======== DEV_GetDmmMgr ========
 *  Purpose:
 *      Retrieve the handle to the dynamic memory manager created for this
 *      device.
 *  Parameters:
 *      hDevObject:     Handle to device object created with
 *                      DEV_CreateDevice().
 *      *phMgr:         Ptr to location to store handle.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hDevObject.
 *  Requires:
 *      phMgr != NULL.
 *      DEV Initialized.
 *  Ensures:
 *      DSP_SOK:        *phMgr contains a handle to a channel manager object,
 *                      or NULL.
 *      else:           *phMgr is NULL.
 */
       extern DSP_STATUS DEV_GetDmmMgr(struct DEV_OBJECT *hDevObject,
					      OUT struct DMM_OBJECT **phMgr);

/*
 *  ======== DEV_GetCodMgr ========
 *  Purpose:
 *      Retrieve the COD manager create for this device.
 *  Parameters:
 *      hDevObject:     Handle to device object created with
 *                      DEV_CreateDevice().
 *      *phCodMgr:      Ptr to location to store handle.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hDevObject.
 *  Requires:
 *      phCodMgr != NULL.
 *      DEV Initialized.
 *  Ensures:
 *      DSP_SOK:        *phCodMgr contains a handle to a COD manager object.
 *      else:           *phCodMgr is NULL.
 */
       extern DSP_STATUS DEV_GetCodMgr(struct DEV_OBJECT *hDevObject,
					     OUT struct COD_MANAGER **phCodMgr);

/*
 *  ======== DEV_GetDehMgr ========
 *  Purpose:
 *      Retrieve the DEH manager created for this device.
 *  Parameters:
 *      hDevObject: Handle to device object created with DEV_CreateDevice().
 *      *phDehMgr:  Ptr to location to store handle.
 *  Returns:
 *      DSP_SOK:    Success.
 *      DSP_EHANDLE:   Invalid hDevObject.
 *  Requires:
 *      phDehMgr != NULL.
 *      DEH Initialized.
 *  Ensures:
 *      DSP_SOK:    *phDehMgr contains a handle to a DEH manager object.
 *      else:       *phDehMgr is NULL.
 */
       extern DSP_STATUS DEV_GetDehMgr(struct DEV_OBJECT *hDevObject,
					      OUT struct DEH_MGR **phDehMgr);

/*
 *  ======== DEV_GetDevNode ========
 *  Purpose:
 *      Retrieve the platform specific device ID for this device.
 *  Parameters:
 *      hDevObject:     Handle to device object created with
 *                      DEV_CreateDevice().
 *      phDevNode:      Ptr to location to get the device node handle.
 *  Returns:
 *      DSP_SOK:        In Win95, returns a DEVNODE in *hDevNode; In NT, ???
 *      DSP_EHANDLE:    Invalid hDevObject.
 *  Requires:
 *      phDevNode != NULL.
 *      DEV Initialized.
 *  Ensures:
 *      DSP_SOK:        *phDevNode contains a platform specific device ID;
 *      else:           *phDevNode is NULL.
 */
       extern DSP_STATUS DEV_GetDevNode(struct DEV_OBJECT *hDevObject,
					OUT struct CFG_DEVNODE **phDevNode);

/*
 *  ======== DEV_GetDevType ========
 *  Purpose:
 *      Retrieve the platform specific device ID for this device.
 *  Parameters:
 *      hDevObject:     Handle to device object created with
 *                      DEV_CreateDevice().
 *      phDevNode:      Ptr to location to get the device node handle.
 *  Returns:
 *      DSP_SOK:        Success
 *      DSP_EHANDLE:    Invalid hDevObject.
 *  Requires:
 *      phDevNode != NULL.
 *      DEV Initialized.
 *  Ensures:
 *      DSP_SOK:        *phDevNode contains a platform specific device ID;
 *      else:           *phDevNode is NULL.
 */
       extern DSP_STATUS DEV_GetDevType(struct DEV_OBJECT *hdevObject,
					       u32 *devType);

/*
 *  ======== DEV_GetFirst ========
 *  Purpose:
 *      Retrieve the first Device Object handle from an internal linked list of
 *      of DEV_OBJECTs maintained by DEV.
 *  Parameters:
 *  Returns:
 *      NULL if there are no device objects stored; else
 *      a valid DEV_HOBJECT.
 *  Requires:
 *      No calls to DEV_CreateDevice or DEV_DestroyDevice (which my modify the
 *      internal device object list) may occur between calls to DEV_GetFirst
 *      and DEV_GetNext.
 *  Ensures:
 *      The DEV_HOBJECT returned is valid.
 *      A subsequent call to DEV_GetNext will return the next device object in
 *      the list.
 */
       extern struct DEV_OBJECT *DEV_GetFirst(void);

/*
 *  ======== DEV_GetIntfFxns ========
 *  Purpose:
 *      Retrieve the WMD interface function structure for the loaded WMD.
 *  Parameters:
 *      hDevObject:     Handle to device object created with
 *                      DEV_CreateDevice().
 *      *ppIntfFxns:    Ptr to location to store fxn interface.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hDevObject.
 *  Requires:
 *      ppIntfFxns != NULL.
 *      DEV Initialized.
 *  Ensures:
 *      DSP_SOK:        *ppIntfFxns contains a pointer to the WMD interface;
 *      else:           *ppIntfFxns is NULL.
 */
       extern DSP_STATUS DEV_GetIntfFxns(struct DEV_OBJECT *hDevObject,
				OUT struct WMD_DRV_INTERFACE **ppIntfFxns);

/*
 *  ======== DEV_GetIOMgr ========
 *  Purpose:
 *      Retrieve the handle to the IO manager created for this device.
 *  Parameters:
 *      hDevObject:     Handle to device object created with
 *                      DEV_CreateDevice().
 *      *phMgr:         Ptr to location to store handle.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hDevObject.
 *  Requires:
 *      phMgr != NULL.
 *      DEV Initialized.
 *  Ensures:
 *      DSP_SOK:        *phMgr contains a handle to an IO manager object.
 *      else:           *phMgr is NULL.
 */
       extern DSP_STATUS DEV_GetIOMgr(struct DEV_OBJECT *hDevObject,
					     OUT struct IO_MGR **phMgr);

/*
 *  ======== DEV_GetNext ========
 *  Purpose:
 *      Retrieve the next Device Object handle from an internal linked list of
 *      of DEV_OBJECTs maintained by DEV, after having previously called
 *      DEV_GetFirst() and zero or more DEV_GetNext
 *  Parameters:
 *      hDevObject: Handle to the device object returned from a previous
 *                  call to DEV_GetFirst() or DEV_GetNext().
 *  Returns:
 *      NULL if there are no further device objects on the list or hDevObject
 *      was invalid;
 *      else the next valid DEV_HOBJECT in the list.
 *  Requires:
 *      No calls to DEV_CreateDevice or DEV_DestroyDevice (which my modify the
 *      internal device object list) may occur between calls to DEV_GetFirst
 *      and DEV_GetNext.
 *  Ensures:
 *      The DEV_HOBJECT returned is valid.
 *      A subsequent call to DEV_GetNext will return the next device object in
 *      the list.
 */
       extern struct DEV_OBJECT *DEV_GetNext(struct DEV_OBJECT
						    *hDevObject);

/*
 *  ========= DEV_GetMsgMgr ========
 *  Purpose:
 *      Retrieve the MSG Manager Handle from the DevObject.
 *  Parameters:
 *      hDevObject: Handle to the Dev Object
 *      phMsgMgr:   Location where MSG Manager handle will be returned.
 *  Returns:
 *  Requires:
 *      DEV Initialized.
 *      Valid hDevObject.
 *      phNodeMgr != NULL.
 *  Ensures:
 */
       extern void DEV_GetMsgMgr(struct DEV_OBJECT *hDevObject,
					OUT struct MSG_MGR **phMsgMgr);

/*
 *  ========= DEV_GetNodeManager ========
 *  Purpose:
 *      Retrieve the Node Manager Handle from the DevObject. It is an
 *      accessor function
 *  Parameters:
 *      hDevObject:     Handle to the Dev Object
 *      phNodeMgr:      Location where Handle to the Node Manager will be
 *                      returned..
 *  Returns:
 *      DSP_SOK:        Success
 *      DSP_EHANDLE:    Invalid Dev Object handle.
 *  Requires:
 *      DEV Initialized.
 *      phNodeMgr is not null
 *  Ensures:
 *      DSP_SOK:        *phNodeMgr contains a handle to a Node manager object.
 *      else:           *phNodeMgr is NULL.
 */
       extern DSP_STATUS DEV_GetNodeManager(struct DEV_OBJECT
					*hDevObject,
					OUT struct NODE_MGR **phNodeMgr);

/*
 *  ======== DEV_GetSymbol ========
 *  Purpose:
 *      Get the value of a symbol in the currently loaded program.
 *  Parameters:
 *      hDevObject:     Handle to device object created with
 *                      DEV_CreateDevice().
 *      pstrSym:        Name of symbol to look up.
 *      pulValue:       Ptr to symbol value.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hDevObject.
 *      COD_E_NOSYMBOLSLOADED:  Symbols have not been loaded onto the board.
 *      COD_E_SYMBOLNOTFOUND:   The symbol could not be found.
 *  Requires:
 *      pstrSym != NULL.
 *      pulValue != NULL.
 *      DEV Initialized.
 *  Ensures:
 *      DSP_SOK:        *pulValue contains the symbol value;
 */
       extern DSP_STATUS DEV_GetSymbol(struct DEV_OBJECT *hDevObject,
					      IN CONST char *pstrSym,
					      OUT u32 *pulValue);

/*
 *  ======== DEV_GetWMDContext ========
 *  Purpose:
 *      Retrieve the WMD Context handle, as returned by the WMD_Create fxn.
 *  Parameters:
 *      hDevObject:     Handle to device object created with DEV_CreateDevice()
 *      *phWmdContext:  Ptr to location to store context handle.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hDevObject.
 *  Requires:
 *      phWmdContext != NULL.
 *      DEV Initialized.
 *  Ensures:
 *      DSP_SOK:        *phWmdContext contains context handle;
 *      else:           *phWmdContext is NULL;
 */
       extern DSP_STATUS DEV_GetWMDContext(struct DEV_OBJECT *hDevObject,
				OUT struct WMD_DEV_CONTEXT **phWmdContext);

/*
 *  ======== DEV_Exit ========
 *  Purpose:
 *      Decrement reference count, and free resources when reference count is
 *      0.
 *  Parameters:
 *  Returns:
 *  Requires:
 *      DEV is initialized.
 *  Ensures:
 *      When reference count == 0, DEV's private resources are freed.
 */
       extern void DEV_Exit(void);

/*
 *  ======== DEV_Init ========
 *  Purpose:
 *      Initialize DEV's private state, keeping a reference count on each call.
 *  Parameters:
 *  Returns:
 *      TRUE if initialized; FALSE if error occured.
 *  Requires:
 *  Ensures:
 *      TRUE: A requirement for the other public DEV functions.
 */
       extern bool DEV_Init(void);

/*
 *  ======== DEV_IsLocked ========
 *  Purpose:
 *      Predicate function to determine if the device has been
 *      locked by a client for exclusive access.
 *  Parameters:
 *      hDevObject:     Handle to device object created with
 *                      DEV_CreateDevice().
 *  Returns:
 *      DSP_SOK:        TRUE: device has been locked.
 *      DSP_SFALSE:     FALSE: device not locked.
 *      DSP_EHANDLE:    hDevObject was invalid.
 *  Requires:
 *      DEV Initialized.
 *  Ensures:
 */
       extern DSP_STATUS DEV_IsLocked(IN struct DEV_OBJECT *hDevObject);

/*
 *  ======== DEV_InsertProcObject ========
 *  Purpose:
 *      Inserts the Processor Object into the List of PROC Objects
 *      kept in the DEV Object
 *  Parameters:
 *      hProcObject:    Handle to the Proc Object
 *      hDevObject      Handle to the Dev Object
 *      bAttachedNew    Specifies if there are already processors attached
 *  Returns:
 *      DSP_SOK:        Successfully inserted into the list
 *  Requires:
 *      hProcObject is not NULL
 *      hDevObject is a valid handle to the DEV.
 *      DEV Initialized.
 *      List(of Proc object in Dev) Exists.
 *  Ensures:
 *      DSP_SOK & the PROC Object is inserted and the list is not empty
 *  Details:
 *      If the List of Proc Object is empty bAttachedNew is TRUE, it indicated
 *      this is the first Processor attaching.
 *      If it is False, there are already processors attached.
 */
       extern DSP_STATUS DEV_InsertProcObject(IN struct DEV_OBJECT
						     *hDevObject,
						     IN u32 hProcObject,
						     OUT bool *
						     pbAlreadyAttached);

/*
 *  ======== DEV_RemoveProcObject ========
 *  Purpose:
 *      Search for and remove a Proc object from the given list maintained
 *      by the DEV
 *  Parameters:
 *      pProcObject:        Ptr to ProcObject to insert.
 *      pDevObject:         Ptr to Dev Object where the list is.
 *      pbAlreadyAttached:  Ptr to return the bool
 *  Returns:
 *      DSP_SOK:            If successful.
 *      DSP_EFAIL           Failure to Remove the PROC Object from the list
 *  Requires:
 *      DevObject is Valid
 *      hProcObject != 0
 *      pDevObject->procList != NULL
 *      !LST_IsEmpty(pDevObject->procList)
 *      pbAlreadyAttached !=NULL
 *  Ensures:
 *  Details:
 *      List will be deleted when the DEV is destroyed.
 *
 */
       extern DSP_STATUS DEV_RemoveProcObject(struct DEV_OBJECT
						     *hDevObject,
						     u32 hProcObject);

/*
 *  ======== DEV_NotifyClients ========
 *  Purpose:
 *      Notify all clients of this device of a change in device status.
 *      Clients may include multiple users of BRD, as well as CHNL.
 *      This function is asychronous, and may be called by a timer event
 *      set up by a watchdog timer.
 *  Parameters:
 *      hDevObject:  Handle to device object created with DEV_CreateDevice().
 *      ulStatus:    A status word, most likely a BRD_STATUS.
 *  Returns:
 *      DSP_SOK:     All registered clients were asynchronously notified.
 *      DSP_EINVALIDARG:   Invalid hDevObject.
 *  Requires:
 *      DEV Initialized.
 *  Ensures:
 *      DSP_SOK: Notifications are queued by the operating system to be
 *      delivered to clients.  This function does not ensure that
 *      the notifications will ever be delivered.
 */
       extern DSP_STATUS DEV_NotifyClients(struct DEV_OBJECT *hDevObject,
						  u32 ulStatus);



/*
 *  ======== DEV_RemoveDevice ========
 *  Purpose:
 *      Destroys the Device Object created by DEV_StartDevice.
 *  Parameters:
 *      hDevNode:       Device node as it is know to OS.
 *  Returns:
 *      DSP_SOK:        If success;
 *      <error code>    Otherwise.
 *  Requires:
 *  Ensures:
 */
       extern DSP_STATUS DEV_RemoveDevice(struct CFG_DEVNODE *hDevNode);

/*
 *  ======== DEV_SetChnlMgr ========
 *  Purpose:
 *      Set the channel manager for this device.
 *  Parameters:
 *      hDevObject:     Handle to device object created with
 *                      DEV_CreateDevice().
 *      hMgr:           Handle to a channel manager, or NULL.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hDevObject.
 *  Requires:
 *      DEV Initialized.
 *  Ensures:
 */
       extern DSP_STATUS DEV_SetChnlMgr(struct DEV_OBJECT *hDevObject,
					       struct CHNL_MGR *hMgr);

/*
 *  ======== DEV_SetMsgMgr ========
 *  Purpose:
 *      Set the Message manager for this device.
 *  Parameters:
 *      hDevObject: Handle to device object created with DEV_CreateDevice().
 *      hMgr:       Handle to a message manager, or NULL.
 *  Returns:
 *  Requires:
 *      DEV Initialized.
 *  Ensures:
 */
       extern void DEV_SetMsgMgr(struct DEV_OBJECT *hDevObject,
					struct MSG_MGR *hMgr);

/*
 *  ======== DEV_StartDevice ========
 *  Purpose:
 *      Initializes the new device with the WinBRIDGE environment.  This
 *      involves querying CM for allocated resources, querying the registry
 *      for necessary dsp resources (requested in the INF file), and using
 *      this information to create a WinBRIDGE device object.
 *  Parameters:
 *      hDevNode:       Device node as it is know to OS.
 *  Returns:
 *      DSP_SOK:        If success;
 *      <error code>    Otherwise.
 *  Requires:
 *      DEV initialized.
 *  Ensures:
 */
       extern DSP_STATUS DEV_StartDevice(struct CFG_DEVNODE *hDevNode);

#endif				/* DEV_ */
