/*
 * pwr.h
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
 *  ======== pwr.h ========
 *
 *  Public Functions:
 *
 *      PWR_SleepDSP
 *      PWR_WakeDSP
 *
 *  Notes:
 *
 *! Revision History:
 *! ================
 *! 06-Jun-2002 sg  Replaced dspdefs.h with includes of dbdefs.h and errbase.h.
 *! 13-May-2002 sg  Added DSP_SAREADYASLEEP and DSP_SALREADYAWAKE.
 *! 09-May-2002 sg  Updated, added timeouts.
 *! 02-May-2002 sg  Initial.
 */

#ifndef PWR_
#define PWR_

#include <dspbridge/dbdefs.h>
#include <dspbridge/errbase.h>
#include <dspbridge/pwr_sh.h>

/*
 *  ======== PWR_SleepDSP ========
 *      Signal the DSP to go to sleep.
 *
 *  Parameters:
 *      sleepCode:          New sleep state for DSP.  (Initially, valid codes
 *                          are PWR_DEEPSLEEP or PWR_EMERGENCYDEEPSLEEP; both of
 *                          these codes will simply put the DSP in deep sleep.)
 *
 *	timeout:            Maximum time (msec) that PWR should wait for
 *                          confirmation that the DSP sleep state has been
 *                          reached.  If PWR should simply send the command to
 *                          the DSP to go to sleep and then return (i.e.,
 *                          asynchrounous sleep), the timeout should be
 *                          specified as zero.
 *
 *  Returns:
 *      DSP_SOK:            Success.
 *      DSP_SALREADYASLEEP: Success, but the DSP was already asleep.
 *      DSP_EINVALIDARG:    The specified sleepCode is not supported.
 *      DSP_ETIMEOUT:       A timeout occured while waiting for DSP sleep
 *                          confirmation.
 *      DSP_EFAIL:          General failure, unable to send sleep command to
 *                          the DSP.
 */
	extern DSP_STATUS PWR_SleepDSP(IN CONST u32 sleepCode,
				       IN CONST u32 timeout);

/*
 *  ======== PWR_WakeDSP ========
 *    Signal the DSP to wake from sleep.
 *
 *  Parameters:
 *	timeout:            Maximum time (msec) that PWR should wait for
 *                          confirmation that the DSP is awake.  If PWR should
 *                          simply send a command to the DSP to wake and then
 *                          return (i.e., asynchrounous wake), timeout should
 *                          be specified as zero.
 *
 *  Returns:
 *      DSP_SOK:            Success.
 *      DSP_SALREADYAWAKE:  Success, but the DSP was already awake.
 *      DSP_ETIMEOUT:       A timeout occured while waiting for wake
 *                          confirmation.
 *      DSP_EFAIL:          General failure, unable to send wake command to
 *                          the DSP.
 */
	extern DSP_STATUS PWR_WakeDSP(IN CONST u32 timeout);

/*
 *  ======== PWR_PM_PreScale ========
 *    Prescale notification to DSP.
 *
 *  Parameters:
 *	voltage_domain:   The voltage domain for which notification is sent
 *    level:			The level of voltage domain
 *
 *  Returns:
 *      DSP_SOK:            Success.
 *      DSP_SALREADYAWAKE:  Success, but the DSP was already awake.
 *      DSP_ETIMEOUT:       A timeout occured while waiting for wake
 *                          confirmation.
 *      DSP_EFAIL:          General failure, unable to send wake command to
 *                          the DSP.
 */
	extern DSP_STATUS PWR_PM_PreScale(IN u16 voltage_domain, u32 level);

/*
 *  ======== PWR_PM_PostScale ========
 *    PostScale notification to DSP.
 *
 *  Parameters:
 *	voltage_domain:   The voltage domain for which notification is sent
 *    level:			The level of voltage domain
 *
 *  Returns:
 *      DSP_SOK:            Success.
 *      DSP_SALREADYAWAKE:  Success, but the DSP was already awake.
 *      DSP_ETIMEOUT:       A timeout occured while waiting for wake
 *                          confirmation.
 *      DSP_EFAIL:          General failure, unable to send wake command to
 *                          the DSP.
 */
	extern DSP_STATUS PWR_PM_PostScale(IN u16 voltage_domain,
					   u32 level);

#endif				/* PWR_ */
