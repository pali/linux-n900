/*
 * pwr.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * PWR API for controlling DSP power states.
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
 *  ======== pwr_sleep_dsp ========
 *    Send command to DSP to enter sleep state.
 */
dsp_status pwr_sleep_dsp(IN CONST u32 sleepCode, IN CONST u32 timeout)
{
	struct bridge_drv_interface *intf_fxns;
	struct wmd_dev_context *dw_context;
	dsp_status status = DSP_EFAIL;
	struct dev_object *hdev_obj = NULL;
	u32 ioctlcode = 0;
	u32 arg = timeout;

	for (hdev_obj = (struct dev_object *)drv_get_first_dev_object();
	     hdev_obj != NULL;
	     hdev_obj =
	     (struct dev_object *)drv_get_next_dev_object((u32) hdev_obj)) {
		if (DSP_FAILED(dev_get_wmd_context(hdev_obj,
						   (struct wmd_dev_context **)
						   &dw_context))) {
			continue;
		}
		if (DSP_FAILED(dev_get_intf_fxns(hdev_obj,
						 (struct bridge_drv_interface **)
						 &intf_fxns))) {
			continue;
		}
		if (sleepCode == PWR_DEEPSLEEP)
			ioctlcode = WMDIOCTL_DEEPSLEEP;
		else if (sleepCode == PWR_EMERGENCYDEEPSLEEP)
			ioctlcode = WMDIOCTL_EMERGENCYSLEEP;
		else
			status = DSP_EINVALIDARG;

		if (status != DSP_EINVALIDARG) {
			status = (*intf_fxns->pfn_dev_cntrl) (dw_context,
							      ioctlcode,
							      (void *)&arg);
		}
	}
	return status;
}

/*
 *  ======== pwr_wake_dsp ========
 *    Send command to DSP to wake it from sleep.
 */
dsp_status pwr_wake_dsp(IN CONST u32 timeout)
{
	struct bridge_drv_interface *intf_fxns;
	struct wmd_dev_context *dw_context;
	dsp_status status = DSP_EFAIL;
	struct dev_object *hdev_obj = NULL;
	u32 arg = timeout;

	for (hdev_obj = (struct dev_object *)drv_get_first_dev_object();
	     hdev_obj != NULL;
	     hdev_obj = (struct dev_object *)drv_get_next_dev_object
	     ((u32) hdev_obj)) {
		if (DSP_SUCCEEDED(dev_get_wmd_context(hdev_obj,
						      (struct wmd_dev_context
						       **)&dw_context))) {
			if (DSP_SUCCEEDED
			    (dev_get_intf_fxns
			     (hdev_obj,
			      (struct bridge_drv_interface **)&intf_fxns))) {
				status =
				    (*intf_fxns->pfn_dev_cntrl) (dw_context,
							WMDIOCTL_WAKEUP,
							(void *)&arg);
			}
		}
	}
	return status;
}

/*
 *  ======== pwr_pm_pre_scale========
 *    Sends pre-notification message to DSP.
 */
dsp_status pwr_pm_pre_scale(IN u16 voltage_domain, u32 level)
{
	struct bridge_drv_interface *intf_fxns;
	struct wmd_dev_context *dw_context;
	dsp_status status = DSP_EFAIL;
	struct dev_object *hdev_obj = NULL;
	u32 arg[2];

	arg[0] = voltage_domain;
	arg[1] = level;

	for (hdev_obj = (struct dev_object *)drv_get_first_dev_object();
	     hdev_obj != NULL;
	     hdev_obj = (struct dev_object *)drv_get_next_dev_object
	     ((u32) hdev_obj)) {
		if (DSP_SUCCEEDED(dev_get_wmd_context(hdev_obj,
						      (struct wmd_dev_context
						       **)&dw_context))) {
			if (DSP_SUCCEEDED
			    (dev_get_intf_fxns
			     (hdev_obj,
			      (struct bridge_drv_interface **)&intf_fxns))) {
				status =
				    (*intf_fxns->pfn_dev_cntrl) (dw_context,
						WMDIOCTL_PRESCALE_NOTIFY,
						(void *)&arg);
			}
		}
	}
	return status;
}

/*
 *  ======== pwr_pm_post_scale========
 *    Sends post-notification message to DSP.
 */
dsp_status pwr_pm_post_scale(IN u16 voltage_domain, u32 level)
{
	struct bridge_drv_interface *intf_fxns;
	struct wmd_dev_context *dw_context;
	dsp_status status = DSP_EFAIL;
	struct dev_object *hdev_obj = NULL;
	u32 arg[2];

	arg[0] = voltage_domain;
	arg[1] = level;

	for (hdev_obj = (struct dev_object *)drv_get_first_dev_object();
	     hdev_obj != NULL;
	     hdev_obj = (struct dev_object *)drv_get_next_dev_object
	     ((u32) hdev_obj)) {
		if (DSP_SUCCEEDED(dev_get_wmd_context(hdev_obj,
						      (struct wmd_dev_context
						       **)&dw_context))) {
			if (DSP_SUCCEEDED
			    (dev_get_intf_fxns
			     (hdev_obj,
			      (struct bridge_drv_interface **)&intf_fxns))) {
				status =
				    (*intf_fxns->pfn_dev_cntrl) (dw_context,
						WMDIOCTL_POSTSCALE_NOTIFY,
						(void *)&arg);
			}
		}
	}
	return status;

}
