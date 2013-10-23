/*
 * io.h
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
 *  ======== io.h ========
 *  Description:
 *      The io module manages IO between CHNL and MSG.
 *
 *  Public Functions:
 *      IO_Create
 *      IO_Destroy
 *      IO_Exit
 *      IO_Init
 *      IO_OnLoaded
 *
 *
 *! Revision History:
 *! ================
 *! 07-Nov-2000 jeh     Created.
 */

#ifndef IO_
#define IO_

#include <dspbridge/cfgdefs.h>
#include <dspbridge/devdefs.h>

#include <dspbridge/iodefs.h>

/*
 *  ======== IO_Create ========
 *  Purpose:
 *      Create an IO manager object, responsible for managing IO between
 *      CHNL and MSG.
 *  Parameters:
 *      phChnlMgr:              Location to store a channel manager object on
 *                              output.
 *      hDevObject:             Handle to a device object.
 *      pMgrAttrs:              IO manager attributes.
 *      pMgrAttrs->bIRQ:        I/O IRQ number.
 *      pMgrAttrs->fShared:     TRUE if the IRQ is shareable.
 *      pMgrAttrs->uWordSize:   DSP Word size in equivalent PC bytes..
 *  Returns:
 *      DSP_SOK:                Success;
 *      DSP_EMEMORY:            Insufficient memory for requested resources.
 *      CHNL_E_ISR:             Unable to plug channel ISR for configured IRQ.
 *      CHNL_E_INVALIDIRQ:      Invalid IRQ number. Must be 0 <= bIRQ <= 15.
 *      CHNL_E_INVALIDWORDSIZE: Invalid DSP word size.  Must be > 0.
 *      CHNL_E_INVALIDMEMBASE:  Invalid base address for DSP communications.
 *  Requires:
 *      IO_Init(void) called.
 *      phIOMgr != NULL.
 *      pMgrAttrs != NULL.
 *  Ensures:
 */
	extern DSP_STATUS IO_Create(OUT struct IO_MGR **phIOMgr,
				    struct DEV_OBJECT *hDevObject,
				    IN CONST struct IO_ATTRS *pMgrAttrs);

/*
 *  ======== IO_Destroy ========
 *  Purpose:
 *      Destroy the IO manager.
 *  Parameters:
 *      hIOMgr:         IOmanager object.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    hIOMgr was invalid.
 *  Requires:
 *      IO_Init(void) called.
 *  Ensures:
 */
	extern DSP_STATUS IO_Destroy(struct IO_MGR *hIOMgr);

/*
 *  ======== IO_Exit ========
 *  Purpose:
 *      Discontinue usage of the IO module.
 *  Parameters:
 *  Returns:
 *  Requires:
 *      IO_Init(void) previously called.
 *  Ensures:
 *      Resources, if any acquired in IO_Init(void), are freed when the last
 *      client of IO calls IO_Exit(void).
 */
	extern void IO_Exit(void);

/*
 *  ======== IO_Init ========
 *  Purpose:
 *      Initialize the IO module's private state.
 *  Parameters:
 *  Returns:
 *      TRUE if initialized; FALSE if error occurred.
 *  Requires:
 *  Ensures:
 *      A requirement for each of the other public CHNL functions.
 */
	extern bool IO_Init(void);

/*
 *  ======== IO_OnLoaded ========
 *  Purpose:
 *      Called when a program is loaded so IO manager can update its
 *      internal state.
 *  Parameters:
 *      hIOMgr:         IOmanager object.
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EHANDLE:    hIOMgr was invalid.
 *  Requires:
 *      IO_Init(void) called.
 *  Ensures:
 */
	extern DSP_STATUS IO_OnLoaded(struct IO_MGR *hIOMgr);

#endif				/* CHNL_ */
