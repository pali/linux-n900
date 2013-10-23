/*
 * _tiomap_pwr.h
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
 *  ======== _tiomap_pwr.h ========
 *  Description:
 *      Definitions and types for the DSP wake/sleep routines.
 *
 *! Revision History
 *! ================
 *! 08-Oct-2002 rr:  Created.
 */

#ifndef _TIOMAP_PWR_
#define _TIOMAP_PWR_

/*
 * ======== WakeDSP =========
 * Wakes up the DSP from DeepSleep
 */
extern DSP_STATUS WakeDSP(struct WMD_DEV_CONTEXT *pDevContext, IN void *pArgs);

/*
 * ======== SleepDSP =========
 * Places the DSP in DeepSleep.
 */
extern DSP_STATUS SleepDSP(struct WMD_DEV_CONTEXT *pDevContext,
			   IN u32 dwCmd, IN void *pArgs);
/*
 *  ========InterruptDSP========
 *  	  Sends an interrupt to DSP unconditionally.
 */
extern void InterruptDSP(struct WMD_DEV_CONTEXT *pDevContext, IN u16 wMbVal);

/*
 * ======== WakeDSP =========
 * Wakes up the DSP from DeepSleep
 */
extern DSP_STATUS DSPPeripheralClkCtrl(struct WMD_DEV_CONTEXT *pDevContext,
				       IN void *pArgs);
/*
 *  ======== handle_hibernation_fromDSP ========
 *  	Handle Hibernation requested from DSP
 */
DSP_STATUS handle_hibernation_fromDSP(struct WMD_DEV_CONTEXT *pDevContext);
/*
 *  ======== PostScale_DSP ========
 *  	Handle Post Scale notification to DSP
 */
DSP_STATUS PostScale_DSP(struct WMD_DEV_CONTEXT *pDevContext, IN void *pArgs);
/*
 *  ======== PreScale_DSP ========
 *  	Handle Pre Scale notification to DSP
 */
DSP_STATUS PreScale_DSP(struct WMD_DEV_CONTEXT *pDevContext, IN void *pArgs);
/*
 *  ======== handle_constraints_set ========
 *  	Handle constraints request from DSP
 */
DSP_STATUS handle_constraints_set(struct WMD_DEV_CONTEXT *pDevContext,
				 IN void *pArgs);
/*
 *  ======== DSP_PeripheralClocks_Disable ========
 *  	This function disables all the peripheral clocks that
 *	were enabled by DSP. Call this function only when
 *	DSP is entering Hibernation or when DSP is in
 *	Error state
 */
DSP_STATUS DSP_PeripheralClocks_Disable(struct WMD_DEV_CONTEXT *pDevContext,
					IN void *pArgs);

/*
 *  ======== DSP_PeripheralClocks_Enable ========
 *  	This function enables all the peripheral clocks that
 *	were requested by DSP.
 */
DSP_STATUS DSP_PeripheralClocks_Enable(struct WMD_DEV_CONTEXT *pDevContext,
				       IN void *pArgs);

/*
 *  ======== DSPClkWakeupEventCtrl ========
 *     This function sets the group selction bits for while
 *     enabling/disabling.
 */
void DSPClkWakeupEventCtrl(u32 ClkId, bool enable);

#endif				/* _TIOMAP_PWR_ */

