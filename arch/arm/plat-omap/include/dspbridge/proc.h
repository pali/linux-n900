/*
 * proc.h
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
 *  ======== proc.h ========
 *  Description:
 *	This is the Class driver RM module interface.
 *
 *  Public Functions:
 *      PROC_Attach
 *      PROC_Create
 *      PROC_Ctrl	       (OEM-function)
 *      PROC_Destroy
 *      PROC_Detach
 *      PROC_EnumNodes
 *      PROC_Exit
 *      PROC_FlushMemory
 *      PROC_GetDevObject       (OEM-function)
 *      PROC_GetResourceInfo
 *      PROC_GetState
 *      PROC_GetProcessorId
 *      PROC_GetTrace	   (OEM-function)
 *      PROC_Init
 *      PROC_Load	       (OEM-function)
 *      PROC_Map
 *      PROC_NotifyAllclients
 *      PROC_NotifyClients      (OEM-function)
 *      PROC_RegisterNotify
 *      PROC_ReserveMemory
 *      PROC_Start	      (OEM-function)
 *      PROC_UnMap
 *      PROC_UnReserveMemory
 *
 *  Notes:
 *
 *! Revision History:
 *! ================
 *! 19-Apr-2004 sb  Aligned DMM definitions with Symbian
 *! 08-Mar-2004 sb  Added the Dynamic Memory Mapping APIs
 *! 09-Feb-2003 vp: Added PROC_GetProcessorID function
 *! 29-Nov-2000 rr: Incorporated code review changes.
 *! 28-Sep-2000 rr: Updated to Version 0.9.
 *! 10-Aug-2000 rr: PROC_NotifyClients, PROC_GetProcessorHandle Added
 *! 27-Jul-2000 rr: Updated to ver 0.8 of DSPAPI(types). GetTrace added.
 *! 27-Jun-2000 rr: Created from dspapi.h
 */

#ifndef PROC_
#define PROC_

#include <dspbridge/cfgdefs.h>
#include <dspbridge/devdefs.h>

/*
 *  ======== PROC_Attach ========
 *  Purpose:
 *      Prepare for communication with a particular DSP processor, and return
 *      a handle to the processor object. The PROC Object gets created
 *  Parameters:
 *      uProcessor  :	   The processor index (zero-based).
 *      hMgrObject  :	   Handle to the Manager Object
 *      pAttrIn     :	   Ptr to the DSP_PROCESSORATTRIN structure.
 *			      A NULL value means use default values.
 *      phProcessor :	   Ptr to location to store processor handle.
 *  Returns:
 *      DSP_SOK     :	   Success.
 *      DSP_EFAIL   :	   General failure.
 *      DSP_EHANDLE :	   Invalid processor handle.
 *      DSP_SALREADYATTACHED:   Success; Processor already attached.
 *  Requires:
 *      phProcessor != NULL.
 *      PROC Initialized.
 *  Ensures:
 *      DSP_EFAIL, and *phProcessor == NULL, OR
 *      Success and *phProcessor is a Valid Processor handle OR
 *      DSP_SALREADYATTACHED and *phProcessor is a Valid Processor.
 *  Details:
 *      When pAttrIn is NULL, the default timeout value is 10 seconds.
 */
	extern DSP_STATUS PROC_Attach(u32 uProcessor,
				      OPTIONAL CONST struct DSP_PROCESSORATTRIN
				      *pAttrIn,
				      OUT DSP_HPROCESSOR *phProcessor);

/*
 *  ======== PROC_AutoStart =========
 *  Purpose:
 *      A Particular device gets loaded with the default image
 *      if the AutoStart flag is set.
 *  Parameters:
 *      hDevObject  :   Handle to the Device
 *  Returns:
 *      DSP_SOK     :   On Successful Loading
 *      DSP_EFILE   :   No DSP exec file found.
 *      DSP_EFAIL   :   General Failure
 *  Requires:
 *      hDevObject != NULL.
 *      hDevNode != NULL.
 *      PROC Initialized.
 *  Ensures:
 */
	extern DSP_STATUS PROC_AutoStart(struct CFG_DEVNODE *hDevNode,
					 struct DEV_OBJECT *hDevObject);

/*
 *  ======== PROC_Ctrl ========
 *  Purpose:
 *      Pass control information to the GPP device driver managing the DSP
 *      processor. This will be an OEM-only function, and not part of the
 *      'Bridge application developer's API.
 *  Parameters:
 *      hProcessor  :       The processor handle.
 *      dwCmd       :       Private driver IOCTL cmd ID.
 *      pArgs       :       Ptr to an driver defined argument structure.
 *  Returns:
 *      DSP_SOK     :       SUCCESS
 *      DSP_EHANDLE :       Invalid processor handle.
 *      DSP_ETIMEOUT:       A Timeout Occured before the Control information
 *			  could be sent.
 *      DSP_EACCESSDENIED:  Client does not have the access rights required
 *			  to call this function.
 *      DSP_ERESTART:       A Critical error has occured and the DSP is being
 *			  restarted.
 *      DSP_EFAIL   :       General Failure.
 *  Requires:
 *      PROC Initialized.
 *  Ensures
 *  Details:
 *      This function Calls WMD_BRD_Ioctl.
 */
	extern DSP_STATUS PROC_Ctrl(DSP_HPROCESSOR hProcessor,
				    u32 dwCmd, IN struct DSP_CBDATA *pArgs);

/*
 *  ======== PROC_Detach ========
 *  Purpose:
 *      Close a DSP processor and de-allocate all (GPP) resources reserved
 *      for it. The Processor Object is deleted.
 *  Parameters:
 *      hProcessor  :   The processor handle.
 *  Returns:
 *      DSP_SOK     :   Success.
 *      DSP_EHANDLE :   InValid Handle.
 *      DSP_EFAIL   :   General failure.
 *  Requires:
 *      PROC Initialized.
 *  Ensures:
 *      PROC Object is destroyed.
 */
	extern DSP_STATUS PROC_Detach(DSP_HPROCESSOR hProcessor);

/*
 *  ======== PROC_EnumNodes ========
 *  Purpose:
 *      Enumerate the nodes currently allocated on a processor.
 *  Parameters:
 *      hProcessor  :   The processor handle.
 *      aNodeTab    :   The first Location of an array allocated for node
 *		      handles.
 *      uNodeTabSize:   The number of (DSP_HNODE) handles that can be held
 *		      to the memory the client has allocated for aNodeTab
 *      puNumNodes  :   Location where DSPProcessor_EnumNodes will return
 *		      the number of valid handles written to aNodeTab
 *      puAllocated :   Location where DSPProcessor_EnumNodes will return
 *		      the number of nodes that are allocated on the DSP.
 *  Returns:
 *      DSP_SOK     :   Success.
 *      DSP_EHANDLE :   Invalid processor handle.
 *      DSP_ESIZE   :   The amount of memory allocated for aNodeTab is
 *		      insufficent. That is the number of nodes actually
 *		      allocated on the DSP is greater than the value
 *		      specified for uNodeTabSize.
 *      DSP_EFAIL   :   Unable to get Resource Information.
 *  Details:
 *  Requires
 *      puNumNodes is not NULL.
 *      puAllocated is not NULL.
 *      aNodeTab is not NULL.
 *      PROC Initialized.
 *  Ensures:
 *  Details:
 */
	extern DSP_STATUS PROC_EnumNodes(DSP_HPROCESSOR hProcessor,
					 IN DSP_HNODE *aNodeTab,
					 IN u32 uNodeTabSize,
					 OUT u32 *puNumNodes,
					 OUT u32 *puAllocated);

/*
 *  ======== PROC_GetResourceInfo ========
 *  Purpose:
 *      Enumerate the resources currently available on a processor.
 *  Parameters:
 *      hProcessor  :       The processor handle.
 *      uResourceType:      Type of resource .
 *      pResourceInfo:      Ptr to the DSP_RESOURCEINFO structure.
 *      uResourceInfoSize:  Size of the structure.
 *  Returns:
 *      DSP_SOK     :       Success.
 *      DSP_EHANDLE :       Invalid processor handle.
 *      DSP_EWRONGSTATE:    The processor is not in the PROC_RUNNING state.
 *      DSP_ETIMEOUT:       A timeout occured before the DSP responded to the
 *			  querry.
 *      DSP_ERESTART:       A Critical error has occured and the DSP is being
 *			  restarted.
 *      DSP_EFAIL   :       Unable to get Resource Information
 *  Requires:
 *      pResourceInfo is not NULL.
 *      Parameter uResourceType is Valid.[TBD]
 *      uResourceInfoSize is >= sizeof DSP_RESOURCEINFO struct.
 *      PROC Initialized.
 *  Ensures:
 *  Details:
 *      This function currently returns
 *      DSP_ENOTIMPL, and does not write any data to the pResourceInfo struct.
 */
	extern DSP_STATUS PROC_GetResourceInfo(DSP_HPROCESSOR hProcessor,
					       u32 uResourceType,
					       OUT struct DSP_RESOURCEINFO *
					       pResourceInfo,
					       u32 uResourceInfoSize);

/*
 *  ======== PROC_Exit ========
 *  Purpose:
 *      Decrement reference count, and free resources when reference count is
 *      0.
 *  Parameters:
 *  Returns:
 *  Requires:
 *      PROC is initialized.
 *  Ensures:
 *      When reference count == 0, PROC's private resources are freed.
 */
       extern void PROC_Exit(void);

/*
 * ======== PROC_GetDevObject =========
 *  Purpose:
 *      Returns the DEV Hanlde for a given Processor handle
 *  Parameters:
 *      hProcessor  :   Processor Handle
 *      phDevObject :   Location to store the DEV Handle.
 *  Returns:
 *      DSP_SOK     :   Success; *phDevObject has Dev handle
 *      DSP_EFAIL   :   Failure; *phDevObject is zero.
 *  Requires:
 *      phDevObject is not NULL
 *      PROC Initialized.
 *  Ensures:
 *      DSP_SOK     :   *phDevObject is not NULL
 *      DSP_EFAIL   :   *phDevObject is NULL.
 */
	extern DSP_STATUS PROC_GetDevObject(DSP_HPROCESSOR hProcessor,
					    struct DEV_OBJECT **phDevObject);

/*
 *  ======== PROC_Init ========
 *  Purpose:
 *      Initialize PROC's private state, keeping a reference count on each
 *      call.
 *  Parameters:
 *  Returns:
 *      TRUE if initialized; FALSE if error occured.
 *  Requires:
 *  Ensures:
 *      TRUE: A requirement for the other public PROC functions.
 */
       extern bool PROC_Init(void);

/*
 *  ======== PROC_GetState ========
 *  Purpose:
 *      Report the state of the specified DSP processor.
 *  Parameters:
 *      hProcessor  :   The processor handle.
 *      pProcStatus :   Ptr to location to store the DSP_PROCESSORSTATE
 *		      structure.
 *      uStateInfoSize: Size of DSP_PROCESSORSTATE.
 *  Returns:
 *      DSP_SOK     :   Success.
 *      DSP_EHANDLE :   Invalid processor handle.
 *      DSP_EFAIL   :   General failure while querying processor state.
 *  Requires:
 *      pProcStatus is not NULL
 *      uStateInfoSize is >= than the size of DSP_PROCESSORSTATE structure.
 *      PROC Initialized.
 *  Ensures:
 *  Details:
 */
	extern DSP_STATUS PROC_GetState(DSP_HPROCESSOR hProcessor,
					OUT struct DSP_PROCESSORSTATE
					*pProcStatus,
					u32 uStateInfoSize);

/*
 *  ======== PROC_GetProcessorID ========
 *  Purpose:
 *      Report the state of the specified DSP processor.
 *  Parameters:
 *      hProcessor  :   The processor handle.
 *      procID      :   Processor ID
 *
 *  Returns:
 *      DSP_SOK     :   Success.
 *      DSP_EHANDLE :   Invalid processor handle.
 *      DSP_EFAIL   :   General failure while querying processor state.
 *  Requires:
 *      pProcStatus is not NULL
 *      uStateInfoSize is >= than the size of DSP_PROCESSORSTATE structure.
 *      PROC Initialized.
 *  Ensures:
 *  Details:
 */
	extern DSP_STATUS PROC_GetProcessorId(DSP_HPROCESSOR hProcessor,
					      u32 *procID);

/*
 *  ======== PROC_GetTrace ========
 *  Purpose:
 *      Retrieve the trace buffer from the specified DSP processor.
 *  Parameters:
 *      hProcessor  :   The processor handle.
 *      pBuf	:   Ptr to buffer to hold trace output.
 *      uMaxSize    :   Maximum size of the output buffer.
 *  Returns:
 *      DSP_SOK     :   Success.
 *      DSP_EHANDLE :   Invalid processor handle.
 *      DSP_EFAIL   :   General failure while retireving processor trace
 *		      Buffer.
 *  Requires:
 *      pBuf is not NULL
 *      uMaxSize is > 0.
 *      PROC Initialized.
 *  Ensures:
 *  Details:
 */
	extern DSP_STATUS PROC_GetTrace(DSP_HPROCESSOR hProcessor, u8 *pBuf,
					u32 uMaxSize);

/*
 *  ======== PROC_Load ========
 *  Purpose:
 *      Reset a processor and load a new base program image.
 *      This will be an OEM-only function.
 *  Parameters:
 *      hProcessor  :       The processor handle.
 *      iArgc       :       The number of Arguments(strings)in the aArgV[]
 *      aArgv       :       An Array of Arguments(Unicode Strings)
 *      aEnvp       :       An Array of Environment settings(Unicode Strings)
 *  Returns:
 *      DSP_SOK     :       Success.
 *      DSP_EFILE   :       The DSP Execuetable was not found.
 *      DSP_EHANDLE :       Invalid processor handle.
 *      DSP_ECORRUTFILE:    Unable to Parse the DSP Execuetable
 *      DSP_EATTACHED:      Abort because a GPP Client is attached to the
 *			  specified Processor
 *      DSP_EACCESSDENIED:  Client does not have the required access rights
 *			  to reset and load the Processor
 *      DSP_EFAIL   :       Unable to Load the Processor
 *  Requires:
 *      aArgv is not NULL
 *      iArgc is > 0
 *      PROC Initialized.
 *  Ensures:
 *      Success and ProcState == PROC_LOADED
 *      or DSP_FAILED status.
 *  Details:
 *      Does not implement access rights to control which GPP application
 *      can load the processor.
 */
	extern DSP_STATUS PROC_Load(DSP_HPROCESSOR hProcessor,
				    IN CONST s32 iArgc, IN CONST char **aArgv,
				    IN CONST char **aEnvp);

/*
 *  ======== PROC_RegisterNotify ========
 *  Purpose:
 *      Register to be notified of specific processor events
 *  Parameters:
 *      hProcessor  :   The processor handle.
 *      uEventMask  :   Mask of types of events to be notified about.
 *      uNotifyType :   Type of notification to be sent.
 *      hNotification:  Handle to be used for notification.
 *  Returns:
 *      DSP_SOK     :   Success.
 *      DSP_EHANDLE :   Invalid processor handle or hNotification.
 *      DSP_EVALUE  :   Parameter uEventMask is Invalid
 *      DSP_ENOTIMP :   The notification type specified in uNotifyMask
 *		      is not supported.
 *      DSP_EFAIL   :   Unable to register for notification.
 *  Requires:
 *      hNotification is not NULL
 *      PROC Initialized.
 *  Ensures:
 *  Details:
 */
	extern DSP_STATUS PROC_RegisterNotify(DSP_HPROCESSOR hProcessor,
					      u32 uEventMask, u32 uNotifyType,
					      struct DSP_NOTIFICATION
					      *hNotification);

/*
 *  ======== PROC_NotifyClients ========
 *  Purpose:
 *      Notify the Processor Clients
 *  Parameters:
 *      hProc       :   The processor handle.
 *      uEvents     :   Event to be notified about.
 *  Returns:
 *      DSP_SOK     :   Success.
 *      DSP_EHANDLE :   Invalid processor handle.
 *      DSP_EFAIL   :   Failure to Set or Reset the Event
 *  Requires:
 *      uEvents is Supported or Valid type of Event
 *      hProc is a valid handle
 *      PROC Initialized.
 *  Ensures:
 */
	extern DSP_STATUS PROC_NotifyClients(DSP_HPROCESSOR hProc,
					     u32 uEvents);

/*
 *  ======== PROC_NotifyAllClients ========
 *  Purpose:
 *      Notify the Processor Clients
 *  Parameters:
 *      hProc       :   The processor handle.
 *      uEvents     :   Event to be notified about.
 *  Returns:
 *      DSP_SOK     :   Success.
 *      DSP_EHANDLE :   Invalid processor handle.
 *      DSP_EFAIL   :   Failure to Set or Reset the Event
 *  Requires:
 *      uEvents is Supported or Valid type of Event
 *      hProc is a valid handle
 *      PROC Initialized.
 *  Ensures:
 *  Details:
 *      NODE And STRM would use this function to notify their clients
 *      about the state changes in NODE or STRM.
 */
	extern DSP_STATUS PROC_NotifyAllClients(DSP_HPROCESSOR hProc,
						u32 uEvents);

/*
 *  ======== PROC_Start ========
 *  Purpose:
 *      Start a processor running.
 *      Processor must be in PROC_LOADED state.
 *      This will be an OEM-only function, and not part of the 'Bridge
 *      application developer's API.
 *  Parameters:
 *      hProcessor  :       The processor handle.
 *  Returns:
 *      DSP_SOK     :       Success.
 *      DSP_EHANDLE :       Invalid processor handle.
 *      DSP_EWRONGSTATE:    Processor is not in PROC_LOADED state.
 *      DSP_EFAIL   :       Unable to start the processor.
 *  Requires:
 *      PROC Initialized.
 *  Ensures:
 *      Success and ProcState == PROC_RUNNING or DSP_FAILED status.
 *  Details:
 */
	extern DSP_STATUS PROC_Start(DSP_HPROCESSOR hProcessor);

/*
 *  ======== PROC_Stop ========
 *  Purpose:
 *      Start a processor running.
 *      Processor must be in PROC_LOADED state.
 *      This will be an OEM-only function, and not part of the 'Bridge
 *      application developer's API.
 *  Parameters:
 *      hProcessor  :       The processor handle.
 *  Returns:
 *      DSP_SOK     :       Success.
 *      DSP_EHANDLE :       Invalid processor handle.
 *      DSP_EWRONGSTATE:    Processor is not in PROC_LOADED state.
 *      DSP_EFAIL   :       Unable to start the processor.
 *  Requires:
 *      PROC Initialized.
 *  Ensures:
 *      Success and ProcState == PROC_RUNNING or DSP_FAILED status.
 *  Details:
 */
	extern DSP_STATUS PROC_Stop(DSP_HPROCESSOR hProcessor);

/*
 *  ======== PROC_FlushMemory ========
 *  Purpose:
 *      Flushes a buffer from the MPU data cache.
 *  Parameters:
 *      hProcessor      :   The processor handle.
 *      pMpuAddr	:   Buffer start address
 *      ulSize	  :   Buffer size
 *      ulFlags	 :   Reserved.
 *  Returns:
 *      DSP_SOK	 :   Success.
 *      DSP_EHANDLE     :   Invalid processor handle.
 *      DSP_EFAIL       :   General failure.
 *  Requires:
 *      PROC Initialized.
 *  Ensures:
 *  Details:
 *      All the arguments are currently ignored.
 */
	extern DSP_STATUS PROC_FlushMemory(DSP_HPROCESSOR hProcessor,
					   void *pMpuAddr,
					   u32 ulSize, u32 ulFlags);


/*
 *  ======== PROC_InvalidateMemory ========
 *  Purpose:
 *      Invalidates a buffer from the MPU data cache.
 *  Parameters:
 *      hProcessor      :   The processor handle.
 *      pMpuAddr	:   Buffer start address
 *      ulSize	  :   Buffer size
 *  Returns:
 *      DSP_SOK	 :   Success.
 *      DSP_EHANDLE     :   Invalid processor handle.
 *      DSP_EFAIL       :   General failure.
 *  Requires:
 *      PROC Initialized.
 *  Ensures:
 *  Details:
 *      All the arguments are currently ignored.
 */
	extern DSP_STATUS PROC_InvalidateMemory(DSP_HPROCESSOR hProcessor,
					   void *pMpuAddr,
					   u32 ulSize);

/*
 *  ======== PROC_Map ========
 *  Purpose:
 *      Maps a MPU buffer to DSP address space.
 *  Parameters:
 *      hProcessor      :   The processor handle.
 *      pMpuAddr	:   Starting address of the memory region to map.
 *      ulSize	  :   Size of the memory region to map.
 *      pReqAddr	:   Requested DSP start address. Offset-adjusted actual
 *			  mapped address is in the last argument.
 *      ppMapAddr       :   Ptr to DSP side mapped u8 address.
 *      ulMapAttr       :   Optional endianness attributes, virt to phys flag.
 *  Returns:
 *      DSP_SOK	 :   Success.
 *      DSP_EHANDLE     :   Invalid processor handle.
 *      DSP_EFAIL       :   General failure.
 *      DSP_EMEMORY     :   MPU side memory allocation error.
 *      DSP_ENOTFOUND   :   Cannot find a reserved region starting with this
 *		      :   address.
 *  Requires:
 *      pMpuAddr is not NULL
 *      ulSize is not zero
 *      ppMapAddr is not NULL
 *      PROC Initialized.
 *  Ensures:
 *  Details:
 */
	extern DSP_STATUS PROC_Map(DSP_HPROCESSOR hProcessor,
				   void *pMpuAddr,
				   u32 ulSize,
				   void *pReqAddr,
				   void **ppMapAddr, u32 ulMapAttr);

/*
 *  ======== PROC_ReserveMemory ========
 *  Purpose:
 *      Reserve a virtually contiguous region of DSP address space.
 *  Parameters:
 *      hProcessor      :   The processor handle.
 *      ulSize	  :   Size of the address space to reserve.
 *      ppRsvAddr       :   Ptr to DSP side reserved u8 address.
 *  Returns:
 *      DSP_SOK	 :   Success.
 *      DSP_EHANDLE     :   Invalid processor handle.
 *      DSP_EFAIL       :   General failure.
 *      DSP_EMEMORY     :   Cannot reserve chunk of this size.
 *  Requires:
 *      ppRsvAddr is not NULL
 *      PROC Initialized.
 *  Ensures:
 *  Details:
 */
	extern DSP_STATUS PROC_ReserveMemory(DSP_HPROCESSOR hProcessor,
					     u32 ulSize, void **ppRsvAddr);

/*
 *  ======== PROC_UnMap ========
 *  Purpose:
 *      Removes a MPU buffer mapping from the DSP address space.
 *  Parameters:
 *      hProcessor      :   The processor handle.
 *      pMapAddr	:   Starting address of the mapped memory region.
 *  Returns:
 *      DSP_SOK	 :   Success.
 *      DSP_EHANDLE     :   Invalid processor handle.
 *      DSP_EFAIL       :   General failure.
 *      DSP_ENOTFOUND   :   Cannot find a mapped region starting with this
 *		      :   address.
 *  Requires:
 *      pMapAddr is not NULL
 *      PROC Initialized.
 *  Ensures:
 *  Details:
 */
	extern DSP_STATUS PROC_UnMap(DSP_HPROCESSOR hProcessor, void *pMapAddr);

/*
 *  ======== PROC_UnReserveMemory ========
 *  Purpose:
 *      Frees a previously reserved region of DSP address space.
 *  Parameters:
 *      hProcessor      :   The processor handle.
 *      pRsvAddr	:   Ptr to DSP side reservedBYTE address.
 *  Returns:
 *      DSP_SOK	 :   Success.
 *      DSP_EHANDLE     :   Invalid processor handle.
 *      DSP_EFAIL       :   General failure.
 *      DSP_ENOTFOUND   :   Cannot find a reserved region starting with this
 *		      :   address.
 *  Requires:
 *      pRsvAddr is not NULL
 *      PROC Initialized.
 *  Ensures:
 *  Details:
 */
	extern DSP_STATUS PROC_UnReserveMemory(DSP_HPROCESSOR hProcessor,
					       void *pRsvAddr);

#endif				/* PROC_ */
