/*
 * _tiomap_pwr.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Definitions and types for the DSP wake/sleep routines.
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

#ifndef _TIOMAP_PWR_
#define _TIOMAP_PWR_

/*
 * ======== wake_dsp =========
 * Wakes up the DSP from DeepSleep
 */
extern dsp_status wake_dsp(struct wmd_dev_context *dev_context, IN void *pargs);

/*
 * ======== sleep_dsp =========
 * Places the DSP in DeepSleep.
 */
extern dsp_status sleep_dsp(struct wmd_dev_context *dev_context,
			    IN u32 dw_cmd, IN void *pargs);
/*
 *  ========interrupt_dsp========
 *  	  Sends an interrupt to DSP unconditionally.
 */
extern void interrupt_dsp(struct wmd_dev_context *dev_context, IN u16 mb_val);

/*
 * ======== wake_dsp =========
 * Wakes up the DSP from DeepSleep
 */
extern dsp_status dsp_peripheral_clk_ctrl(struct wmd_dev_context *dev_context,
					  IN void *pargs);
/*
 *  ======== handle_hibernation_from_dsp ========
 *  	Handle Hibernation requested from DSP
 */
dsp_status handle_hibernation_from_dsp(struct wmd_dev_context *dev_context);
/*
 *  ======== post_scale_dsp ========
 *  	Handle Post Scale notification to DSP
 */
dsp_status post_scale_dsp(struct wmd_dev_context *dev_context, IN void *pargs);
/*
 *  ======== pre_scale_dsp ========
 *  	Handle Pre Scale notification to DSP
 */
dsp_status pre_scale_dsp(struct wmd_dev_context *dev_context, IN void *pargs);
/*
 *  ======== handle_constraints_set ========
 *  	Handle constraints request from DSP
 */
dsp_status handle_constraints_set(struct wmd_dev_context *dev_context,
				  IN void *pargs);
/*
 *  ======== dsp_peripheral_clocks_disable ========
 *  	This function disables all the peripheral clocks that
 *	were enabled by DSP. Call this function only when
 *	DSP is entering Hibernation or when DSP is in
 *	Error state
 */
dsp_status dsp_peripheral_clocks_disable(struct wmd_dev_context *dev_context,
					 IN void *pargs);

/*
 *  ======== dsp_peripheral_clocks_enable ========
 *  	This function enables all the peripheral clocks that
 *	were requested by DSP.
 */
dsp_status dsp_peripheral_clocks_enable(struct wmd_dev_context *dev_context,
					IN void *pargs);

/*
 *  ======== dsp_clk_wakeup_event_ctrl ========
 *     This function sets the group selction bits for while
 *     enabling/disabling.
 */
void dsp_clk_wakeup_event_ctrl(u32 ClkId, bool enable);

#endif /* _TIOMAP_PWR_ */
