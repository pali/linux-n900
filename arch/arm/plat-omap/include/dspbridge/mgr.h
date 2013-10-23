/*
 * mgr.h
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
 *  ======== mgr.h ========
 *  Description:
 *      This is the Class driver RM module interface.
 *
 *  Public Functions:
 *      MGR_Create
 *      MGR_Destroy
 *      MGR_EnumNodeInfo
 *      MGR_EnumProcessorInfo
 *      MGR_Exit
 *      MGR_GetDCDHandle
 *      MGR_Init
 *
 *  Notes:
 *
 *! Revision History:
 *! ================
 *! 15-Oct-2002 kc: Removed legacy PERF definitions.
 *! 11-Jul-2001 jeh Added CFG_HDEVNODE parameter to MGR_Create().
 *! 22-Nov-2000 kc: Added MGR_GetPerfData for acquiring PERF stats.
 *! 03-Nov-2000 rr: Added MGR_GetDCDHandle. Modified after code review.
 *! 25-Sep-2000 rr: Updated to Version 0.9
 *! 14-Aug-2000 rr: Cleaned up.
 *! 07-Aug-2000 rr: MGR_Create does the job of Loading DCD Dll.
 *! 27-Jul-2000 rr: Updated to ver 0.8 of DSPAPI(types).
 *! 20-Jun-2000 rr: Created.
 */

#ifndef MGR_
#define MGR_

#include <dspbridge/mgrpriv.h>

#define MAX_EVENTS 32

/*
 *  ======== MGR_WaitForBridgeEvents ========
 *  Purpose:
 *      Block on any Bridge event(s)
 *  Parameters:
 *      aNotifications  : array of pointers to notification objects.
 *      uCount          : number of elements in above array
 *      puIndex         : index of signaled event object
 *      uTimeout        : timeout interval in milliseocnds
 *  Returns:
 *      DSP_SOK         : Success.
 *      DSP_ETIMEOUT    : Wait timed out. *puIndex is undetermined.
 *  Details:
 */

	DSP_STATUS MGR_WaitForBridgeEvents(struct DSP_NOTIFICATION
					   **aNotifications,
					   u32 uCount, OUT u32 *puIndex,
					   u32 uTimeout);

/*
 *  ======== MGR_Create ========
 *  Purpose:
 *      Creates the Manager Object. This is done during the driver loading.
 *      There is only one Manager Object in the DSP/BIOS Bridge.
 *  Parameters:
 *      phMgrObject:    Location to store created MGR Object handle.
 *      hDevNode:       Device object as known to Windows system.
 *  Returns:
 *      DSP_SOK:        Success
 *      DSP_EMEMORY:    Failed to Create the Object
 *      DSP_EFAIL:      General Failure
 *  Requires:
 *      MGR Initialized (cRefs > 0 )
 *      phMgrObject != NULL.
 *  Ensures:
 *      DSP_SOK:        *phMgrObject is a valid MGR interface to the device.
 *                      MGR Object stores the DCD Manager Handle.
 *                      MGR Object stored in the Regsitry.
 *      !DSP_SOK:       MGR Object not created
 *  Details:
 *      DCD Dll is loaded and MGR Object stores the handle of the DLL.
 */
	extern DSP_STATUS MGR_Create(OUT struct MGR_OBJECT **hMgrObject,
				     struct CFG_DEVNODE *hDevNode);

/*
 *  ======== MGR_Destroy ========
 *  Purpose:
 *      Destroys the MGR object. Called upon driver unloading.
 *  Parameters:
 *      hMgrObject:     Handle to Manager object .
 *  Returns:
 *      DSP_SOK:        Success.
 *                      DCD Manager freed; MGR Object destroyed;
 *                      MGR Object deleted from the Registry.
 *      DSP_EFAIL:      Failed to destroy MGR Object
 *  Requires:
 *      MGR Initialized (cRefs > 0 )
 *      hMgrObject is a valid MGR handle .
 *  Ensures:
 *      DSP_SOK:        MGR Object destroyed and hMgrObject is Invalid MGR
 *                      Handle.
 */
	extern DSP_STATUS MGR_Destroy(struct MGR_OBJECT *hMgrObject);

/*
 *  ======== MGR_EnumNodeInfo ========
 *  Purpose:
 *      Enumerate and get configuration information about nodes configured
 *      in the node database.
 *  Parameters:
 *      uNode:              The node index (base 0).
 *      pNDBProps:          Ptr to the DSP_NDBPROPS structure for output.
 *      uNDBPropsSize:      Size of the DSP_NDBPROPS structure.
 *      puNumNodes:         Location where the number of nodes configured
 *                          in the database will be returned.
 *  Returns:
 *      DSP_SOK:            Success.
 *      DSP_EINVALIDARG:    Parameter uNode is > than the number of nodes.
 *                          configutred in the system
 *      DSP_ECHANGEDURINGENUM:  During Enumeration there has been a change in
 *                              the number of nodes configured or in the
 *                              the properties of the enumerated nodes.
 *      DSP_EFAIL:          Failed to querry the Node Data Base
 *  Requires:
 *      pNDBPROPS is not null
 *      uNDBPropsSize >= sizeof(DSP_NDBPROPS)
 *      puNumNodes is not null
 *      MGR Initialized (cRefs > 0 )
 *  Ensures:
 *      SUCCESS on successful retreival of data and *puNumNodes > 0 OR
 *      DSP_FAILED  && *puNumNodes == 0.
 *  Details:
 */
	extern DSP_STATUS MGR_EnumNodeInfo(u32 uNode,
					   OUT struct DSP_NDBPROPS *pNDBProps,
					   u32 uNDBPropsSize,
					   OUT u32 *puNumNodes);

/*
 *  ======== MGR_EnumProcessorInfo ========
 *  Purpose:
 *      Enumerate and get configuration information about available DSP
 *      processors
 *  Parameters:
 *      uProcessor:         The processor index (zero-based).
 *      pProcessorInfo:     Ptr to the DSP_PROCESSORINFO structure .
 *      uProcessorInfoSize: Size of DSP_PROCESSORINFO structure.
 *      puNumProcs:         Location where the number of DSPs configured
 *                          in the database will be returned
 *  Returns:
 *      DSP_SOK:            Success.
 *      DSP_EINVALIDARG:    Parameter uProcessor is > than the number of
 *                          DSP Processors in the system.
 *      DSP_EFAIL:          Failed to querry the Node Data Base
 *  Requires:
 *      pProcessorInfo is not null
 *      puNumProcs is not null
 *      uProcessorInfoSize >= sizeof(DSP_PROCESSORINFO)
 *      MGR Initialized (cRefs > 0 )
 *  Ensures:
 *      SUCCESS on successful retreival of data and *puNumProcs > 0 OR
 *      DSP_FAILED && *puNumProcs == 0.
 *  Details:
 */
	extern DSP_STATUS MGR_EnumProcessorInfo(u32 uProcessor,
						OUT struct DSP_PROCESSORINFO *
						pProcessorInfo,
						u32 uProcessorInfoSize,
						OUT u32 *puNumProcs);
/*
 *  ======== MGR_Exit ========
 *  Purpose:
 *      Decrement reference count, and free resources when reference count is
 *      0.
 *  Parameters:
 *  Returns:
 *  Requires:
 *      MGR is initialized.
 *  Ensures:
 *      When reference count == 0, MGR's private resources are freed.
 */
       extern void MGR_Exit(void);

/*
 *  ======== MGR_GetDCDHandle ========
 *  Purpose:
 *      Retrieves the MGR handle. Accessor Function
 *  Parameters:
 *      hMGRHandle:     Handle to the Manager Object
 *      phDCDHandle:    Ptr to receive the DCD Handle.
 *  Returns:
 *      DSP_SOK:        Sucess
 *      DSP_EFAIL:      Failure to get the Handle
 *  Requires:
 *      MGR is initialized.
 *      phDCDHandle != NULL
 *  Ensures:
 *      DSP_SOK and *phDCDHandle != NULL ||
 *      DSP_EFAIL and *phDCDHandle == NULL
 */
       extern DSP_STATUS MGR_GetDCDHandle(IN struct MGR_OBJECT
						 *hMGRHandle,
						 OUT u32 *phDCDHandle);

/*
 *  ======== MGR_Init ========
 *  Purpose:
 *      Initialize MGR's private state, keeping a reference count on each
 *      call. Intializes the DCD.
 *  Parameters:
 *  Returns:
 *      TRUE if initialized; FALSE if error occured.
 *  Requires:
 *  Ensures:
 *      TRUE: A requirement for the other public MGR functions.
 */
       extern bool MGR_Init(void);

#endif				/* MGR_ */
