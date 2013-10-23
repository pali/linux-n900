/*
 * pwr.c
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
 *  ======== PWR.c ========
 *  PWR API for controlling DSP power states.
 *
 *  Public Functions:
 *      PWR_SleepDSP
 *      PWR_WakeDSP
 *
 *! Revision History
 *! ================
 *! 18-Feb-2003 vp  Code review updates.
 *! 18-Oct-2002 vp  Ported to Linux platform.
 *! 22-May-2002 sg  Do PWR-to-IOCTL code mapping in PWR_SleepDSP.
 *! 29-Apr-2002 sg  Initial.
 */

/*  ----------------------------------- Host OS */
#include <dspbridge/host_os.h>

/*  ----------------------------------- This */
#include <dspbridge/pwr.h>

/*  ----------------------------------- Resource Manager */
#include <dspbridge/devdefs.h>
#include <dspbridge/drv.h>

/*  ----------------------------------- Platform Manager */
#include <dspbridge/dev.h>

/*  ----------------------------------- Link Driver */
#include <dspbridge/wmdioctl.h>

/*
 *  ======== PWR_SleepDSP ========
 *    Send command to DSP to enter sleep state.
 */
DSP_STATUS PWR_SleepDSP(IN CONST u32 sleepCode, IN CONST u32 timeout)
{
	struct WMD_DRV_INTERFACE *pIntfFxns;
	struct WMD_DEV_CONTEXT *dwContext;
	DSP_STATUS status = DSP_EFAIL;
	struct DEV_OBJECT *hDevObject = NULL;
	u32 ioctlcode = 0;
	u32 arg = timeout;

	for (hDevObject = (struct DEV_OBJECT *)DRV_GetFirstDevObject();
			  hDevObject != NULL;
			hDevObject =
				(struct DEV_OBJECT *)DRV_GetNextDevObject
				((u32)hDevObject)) {
		if (DSP_FAILED(DEV_GetWMDContext(hDevObject,
		   (struct WMD_DEV_CONTEXT **)&dwContext))) {
			continue;
		}
		if (DSP_FAILED(DEV_GetIntfFxns(hDevObject,
		   (struct WMD_DRV_INTERFACE **)&pIntfFxns))) {
			continue;
		}
		if (sleepCode == PWR_DEEPSLEEP)
			ioctlcode = WMDIOCTL_DEEPSLEEP;
		else if (sleepCode == PWR_EMERGENCYDEEPSLEEP)
			ioctlcode = WMDIOCTL_EMERGENCYSLEEP;
		else
			status = DSP_EINVALIDARG;

		if (status != DSP_EINVALIDARG) {
			status = (*pIntfFxns->pfnDevCntrl)(dwContext,
				 ioctlcode, (void *)&arg);
		}
	}
	return status;
}

/*
 *  ======== PWR_WakeDSP ========
 *    Send command to DSP to wake it from sleep.
 */
DSP_STATUS PWR_WakeDSP(IN CONST u32 timeout)
{
	struct WMD_DRV_INTERFACE *pIntfFxns;
	struct WMD_DEV_CONTEXT *dwContext;
	DSP_STATUS status = DSP_EFAIL;
	struct DEV_OBJECT *hDevObject = NULL;
	u32 arg = timeout;

	for (hDevObject = (struct DEV_OBJECT *)DRV_GetFirstDevObject();
	     hDevObject != NULL;
	     hDevObject = (struct DEV_OBJECT *)DRV_GetNextDevObject
			  ((u32)hDevObject)) {
		if (DSP_SUCCEEDED(DEV_GetWMDContext(hDevObject,
		   (struct WMD_DEV_CONTEXT **)&dwContext))) {
			if (DSP_SUCCEEDED(DEV_GetIntfFxns(hDevObject,
			   (struct WMD_DRV_INTERFACE **)&pIntfFxns))) {
				status = (*pIntfFxns->pfnDevCntrl)(dwContext,
					 WMDIOCTL_WAKEUP, (void *)&arg);
			}
		}
	}
	return status;
}

/*
 *  ======== PWR_PM_PreScale========
 *    Sends pre-notification message to DSP.
 */
DSP_STATUS PWR_PM_PreScale(IN u16 voltage_domain, u32 level)
{
	struct WMD_DRV_INTERFACE *pIntfFxns;
	struct WMD_DEV_CONTEXT *dwContext;
	DSP_STATUS status = DSP_EFAIL;
	struct DEV_OBJECT *hDevObject = NULL;
	u32 arg[2];

	arg[0] = voltage_domain;
	arg[1] = level;

	for (hDevObject = (struct DEV_OBJECT *)DRV_GetFirstDevObject();
	    hDevObject != NULL;
	    hDevObject = (struct DEV_OBJECT *)DRV_GetNextDevObject
			 ((u32)hDevObject)) {
		if (DSP_SUCCEEDED(DEV_GetWMDContext(hDevObject,
		   (struct WMD_DEV_CONTEXT **)&dwContext))) {
			if (DSP_SUCCEEDED(DEV_GetIntfFxns(hDevObject,
			   (struct WMD_DRV_INTERFACE **)&pIntfFxns))) {
				status = (*pIntfFxns->pfnDevCntrl)(dwContext,
					 WMDIOCTL_PRESCALE_NOTIFY,
					 (void *)&arg);
			}
		}
	}
	return status;
}

/*
 *  ======== PWR_PM_PostScale========
 *    Sends post-notification message to DSP.
 */
DSP_STATUS PWR_PM_PostScale(IN u16 voltage_domain, u32 level)
{
	struct WMD_DRV_INTERFACE *pIntfFxns;
	struct WMD_DEV_CONTEXT *dwContext;
	DSP_STATUS status = DSP_EFAIL;
	struct DEV_OBJECT *hDevObject = NULL;
	u32 arg[2];

	arg[0] = voltage_domain;
	arg[1] = level;

	for (hDevObject = (struct DEV_OBJECT *)DRV_GetFirstDevObject();
	     hDevObject != NULL;
	     hDevObject = (struct DEV_OBJECT *)DRV_GetNextDevObject
			  ((u32)hDevObject)) {
		if (DSP_SUCCEEDED(DEV_GetWMDContext(hDevObject,
		   (struct WMD_DEV_CONTEXT **)&dwContext))) {
			if (DSP_SUCCEEDED(DEV_GetIntfFxns(hDevObject,
			   (struct WMD_DRV_INTERFACE **)&pIntfFxns))) {
				status = (*pIntfFxns->pfnDevCntrl)(dwContext,
					WMDIOCTL_POSTSCALE_NOTIFY,
					(void *)&arg);
			}
		}
	}
	return status;

}


