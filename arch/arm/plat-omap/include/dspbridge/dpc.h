/*
 * dpc.h
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
 *  ======== dpc.h ========
 *  Purpose:
 *      Deferred Procedure Call(DPC) Services.
 *
 *  Public Functions:
 *      DPC_Cancel
 *      DPC_Create
 *      DPC_Destroy
 *      DPC_Exit
 *      DPC_Init
 *      DPC_Schedule
 *
 *! Revision History:
 *! ================
 *! 31-Jan-2000 rr:  DPC_Destroy ensures Suceess and DPC Object is NULL.
 *! 21-Jan-2000 ag:  Updated comments per code review.
 *! 06-Jan-2000 ag:  Removed DPC_[Lower|Raise]IRQL[From|To]DispatchLevel.
 *! 14-Jan-1998 gp:  Added DPC_[Lower|Raise]IRQL[From|To]DispatchLevel.
 *! 18-Aug-1997 cr:  Added explicit CDECL identifiers.
 *! 28-Jul-1996 gp:  Created.
 */

#ifndef DPC_
#define DPC_

	struct DPC_OBJECT;

/*
 *  ======== DPC_PROC ========
 *  Purpose:
 *      Deferred processing routine.  Typically scheduled from an ISR to
 *      complete I/O processing.
 *  Parameters:
 *      pRefData:   Ptr to user data: passed in via ISR_ScheduleDPC.
 *  Returns:
 *  Requires:
 *      The DPC should not block, or otherwise acquire resources.
 *      Interrupts to the processor are enabled.
 *      DPC_PROC executes in a critical section.
 *  Ensures:
 *      This DPC will not be reenterred on the same thread.
 *      However, the DPC may take hardware interrupts during execution.
 *      Interrupts to the processor are enabled.
 */
       typedef void(*DPC_PROC) (void *pRefData);

/*
 *  ======== DPC_Cancel ========
 *  Purpose:
 *      Cancel a DPC previously scheduled by DPC_Schedule.
 *  Parameters:
 *      hDPC:           A DPC object handle created in DPC_Create().
 *  Returns:
 *      DSP_SOK:        Scheduled DPC, if any, is cancelled.
 *      DSP_SFALSE:     No DPC is currently scheduled for execution.
 *      DSP_EHANDLE:    Invalid hDPC.
 *  Requires:
 *  Ensures:
 *      If the DPC has already executed, is executing, or was not yet
 *      scheduled, this function will have no effect.
 */
       extern DSP_STATUS DPC_Cancel(IN struct DPC_OBJECT *hDPC);

/*
 *  ======== DPC_Create ========
 *  Purpose:
 *      Create a DPC object, allowing a client's own DPC procedure to be
 *      scheduled for a call with client reference data.
 *  Parameters:
 *      phDPC:          Pointer to location to store DPC object.
 *      pfnDPC:         Client's DPC procedure.
 *      pRefData:       Pointer to user-defined reference data.
 *  Returns:
 *      DSP_SOK:        DPC object created.
 *      DSP_EPOINTER:   phDPC == NULL or pfnDPC == NULL.
 *      DSP_EMEMORY:    Insufficient memory.
 *  Requires:
 *      Must not be called at interrupt time.
 *  Ensures:
 *      DSP_SOK: DPC object is created;
 *      else: *phDPC is set to NULL.
 */
       extern DSP_STATUS DPC_Create(OUT struct DPC_OBJECT **phDPC,
					   IN DPC_PROC pfnDPC,
					   IN void *pRefData);

/*
 *  ======== DPC_Destroy ========
 *  Purpose:
 *      Cancel the last scheduled DPC, and deallocate a DPC object previously
 *      allocated with DPC_Create().Frees the Object only if the thread and
 *      the events are terminated successfuly.
 *  Parameters:
 *      hDPC:           A DPC object handle created in DPC_Create().
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    Invalid hDPC.
 *  Requires:
 *      All DPC's scheduled for the DPC object must have completed their
 *      processing.
 *  Ensures:
 *      (SUCCESS && hDPC is NULL) or DSP_EFAILED status
 */
       extern DSP_STATUS DPC_Destroy(IN struct DPC_OBJECT *hDPC);

/*
 *  ======== DPC_Exit ========
 *  Purpose:
 *      Discontinue usage of the DPC module.
 *  Parameters:
 *  Returns:
 *  Requires:
 *      DPC_Init(void) was previously called.
 *  Ensures:
 *      Resources acquired in DPC_Init(void) are freed.
 */
       extern void DPC_Exit(void);

/*
 *  ======== DPC_Init ========
 *  Purpose:
 *      Initialize the DPC module's private state.
 *  Parameters:
 *  Returns:
 *      TRUE if initialized; FALSE if error occured.
 *  Requires:
 *  Ensures:
 *      A requirement for each of the other public DPC functions.
 */
       extern bool DPC_Init(void);

/*
 *  ======== DPC_Schedule ========
 *  Purpose:
 *      Schedule a deferred procedure call to be executed at a later time.
 *      Latency and order of DPC execution is platform specific.
 *  Parameters:
 *      hDPC:           A DPC object handle created in DPC_Create().
 *  Returns:
 *      DSP_SOK:        An event is scheduled for deferred processing.
 *      DSP_EHANDLE:    Invalid hDPC.
 *  Requires:
 *      See requirements for DPC_PROC.
 *  Ensures:
 *      DSP_SOK:        The DPC will not be called before this function returns.
 */
       extern DSP_STATUS DPC_Schedule(IN struct DPC_OBJECT *hDPC);

#endif				/* DPC_ */
