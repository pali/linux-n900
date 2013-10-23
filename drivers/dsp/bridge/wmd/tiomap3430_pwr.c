/*
 * tiomap_pwr.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Implementation of DSP wake/sleep routines.
 *
 * Copyright (C) 2007-2008 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/dbdefs.h>
#include <dspbridge/errbase.h>
#include <dspbridge/cfg.h>
#include <dspbridge/drv.h>
#include <dspbridge/io_sm.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/mem.h>

/*  ----------------------------------- Platform Manager */
#include <dspbridge/brddefs.h>
#include <dspbridge/dev.h>
#include <dspbridge/iodefs.h>

/* ------------------------------------ Hardware Abstraction Layer */
#include <hw_defs.h>
#include <hw_dspssC64P.h>
#include <hw_prcm.h>
#include <hw_mmu.h>

#include <dspbridge/pwr_sh.h>

/*  ----------------------------------- Mini Driver */
#include <dspbridge/wmddeh.h>
#include <dspbridge/wdt.h>

/*  ----------------------------------- specific to this file */
#include "_tiomap.h"
#include "_tiomap_pwr.h"
#include "_tiomap_util.h"
#include <mach-omap2/prm-regbits-34xx.h>
#include <mach-omap2/cm-regbits-34xx.h>

#ifdef CONFIG_PM
extern s32 dsp_test_sleepstate;
#endif
extern struct mailbox_context mboxsetting;

DEFINE_SPINLOCK(pwr_lock);

/*
 *  ======== handle_constraints_set ========
 *  	Sets new DSP constraint
 */
dsp_status handle_constraints_set(struct wmd_dev_context *dev_context,
				  IN void *pargs)
{
#ifdef CONFIG_BRIDGE_DVFS
	u32 *constraint_val;
	struct dspbridge_platform_data *pdata =
	    omap_dspbridge_dev->dev.platform_data;

	constraint_val = (u32 *) (pargs);
	/* Read the target value requested by DSP */
	dev_dbg(bridge, "OPP: %s opp requested = 0x%x\n", __func__,
		(u32) *(constraint_val + 1));

	/* Set the new opp value */
	if (pdata->dsp_set_min_opp)
		(*pdata->dsp_set_min_opp) ((u32) *(constraint_val + 1));
#endif /* #ifdef CONFIG_BRIDGE_DVFS */
	return DSP_SOK;
}

/*
 *  ======== handle_hibernation_from_dsp ========
 *	Handle Hibernation requested from DSP
 */
dsp_status handle_hibernation_from_dsp(struct wmd_dev_context *dev_context)
{
	dsp_status status = DSP_SOK;
#ifdef CONFIG_PM
	u8 t;
	unsigned long v;
	struct cfg_hostres resources;
	enum hw_pwr_state_t pwr_state;
#ifdef CONFIG_BRIDGE_DVFS
	u32 opplevel;
	struct io_mgr *hio_mgr;
	struct dspbridge_platform_data *pdata =
	    omap_dspbridge_dev->dev.platform_data;
#endif

	status = cfg_get_host_resources((struct cfg_devnode *)
					drv_get_first_dev_extension(),
					&resources);
	if (DSP_FAILED(status))
		return status;

	spin_lock_bh(&pwr_lock);

	/* Wait for DSP to move into OFF state */
	v = msecs_to_jiffies(PWRSTST_TIMEOUT) + jiffies;
	do {
		t = time_is_after_jiffies(v);
		hw_pwr_iva2_state_get(resources.dw_prm_base, HW_PWR_DOMAIN_DSP,
				      &pwr_state);
		if (pwr_state == HW_PWR_STATE_OFF)
			break;
	} while (t);

	if (!t) {
		dev_dbg(bridge, "Timed out waiting for DSP off mode\n");
		spin_unlock_bh(&pwr_lock);
		return WMD_E_TIMEOUT;
	}

	/* Save mailbox settings */
	omap_mbox_save_ctx(dev_context->mbox);

	/* Turn off DSP Peripheral clocks and DSP Load monitor timer */
	status = dsp_peripheral_clocks_disable(dev_context, NULL);

	if (DSP_FAILED(status)) {
		spin_unlock_bh(&pwr_lock);
		return status;
	}

	status = services_clk_disable(SERVICESCLK_IVA2_CK);

	/* Disable wdt on hibernation. */
	dsp_wdt_enable(false);

	/* Update the Bridger Driver state */
	dev_context->dw_brd_state = BRD_DSP_HIBERNATION;

	spin_unlock_bh(&pwr_lock);

#ifdef CONFIG_BRIDGE_DVFS
	status = dev_get_io_mgr(dev_context->hdev_obj, &hio_mgr);
	if (!hio_mgr)
		return DSP_EHANDLE;
	io_sh_msetting(hio_mgr, SHM_GETOPP, &opplevel);

	/*Set the OPP to low level before moving to OFF mode */
	if (pdata->dsp_set_min_opp)
		(*pdata->dsp_set_min_opp) (VDD1_OPP1);
			status = DSP_SOK;
#endif /* CONFIG_BRIDGE_DVFS */
#endif
	return status;
}

/*
 *  ======== sleep_dsp ========
 *  	Put DSP in low power consuming state.
 */
dsp_status sleep_dsp(struct wmd_dev_context *dev_context, IN u32 dw_cmd,
		     IN void *pargs)
{
	dsp_status status = DSP_SOK;
#ifdef CONFIG_PM
	struct cfg_hostres resources;
#ifdef CONFIG_BRIDGE_NTFY_PWRERR
	struct deh_mgr *hdeh_mgr;
#endif /* CONFIG_BRIDGE_NTFY_PWRERR */
	u8 t;
	unsigned long v;
	u32 mbx_msg;
	enum hw_pwr_state_t pwr_state, target_pwr_state;

	/* Check if sleep code is valid */
	if ((dw_cmd != PWR_DEEPSLEEP) && (dw_cmd != PWR_EMERGENCYDEEPSLEEP))
		return DSP_EINVALIDARG;

	status = cfg_get_host_resources((struct cfg_devnode *)
					drv_get_first_dev_extension(),
					&resources);
	if (DSP_FAILED(status))
		return status;

	switch (dev_context->dw_brd_state) {
	case BRD_RUNNING:
		if (dsp_test_sleepstate == HW_PWR_STATE_OFF) {
			mbx_msg = MBX_PM_DSPHIBERNATE;
			dev_dbg(bridge, "PM: %s - sent hibernate cmd to DSP\n",
				__func__);
			target_pwr_state = HW_PWR_STATE_OFF;
		} else {
			mbx_msg = MBX_PM_DSPRETENTION;
			target_pwr_state = HW_PWR_STATE_RET;
		}
		break;
	case BRD_RETENTION:
		if (dsp_test_sleepstate == HW_PWR_STATE_OFF) {
			mbx_msg = MBX_PM_DSPHIBERNATE;
			target_pwr_state = HW_PWR_STATE_OFF;
		} else
			return DSP_SOK;
		break;
	case BRD_HIBERNATION:
	case BRD_DSP_HIBERNATION:
		/* Already in Hibernation, so just return */
		dev_dbg(bridge, "PM: %s - DSP already in hibernation\n",
			__func__);
		return DSP_SOK;
	case BRD_STOPPED:
		dev_dbg(bridge, "PM: %s - Board in STOP state\n", __func__);
		return DSP_SALREADYASLEEP;
	default:
		dev_dbg(bridge, "PM: %s - Bridge in Illegal state\n", __func__);
		return DSP_EFAIL;
	}

	spin_lock_bh(&pwr_lock);

	omap_mbox_save_ctx(dev_context->mbox);

	status = omap_mbox_msg_send(dev_context->mbox, mbx_msg);
	if (DSP_FAILED(status)) {
		spin_unlock_bh(&pwr_lock);
		return status;
	}

	/* Wait for DSP to move into target power state */
	v = msecs_to_jiffies(PWRSTST_TIMEOUT) + jiffies;
	do {
		t = time_is_after_jiffies(v);
		hw_pwr_iva2_state_get(resources.dw_prm_base, HW_PWR_DOMAIN_DSP,
				      &pwr_state);
		if (pwr_state == target_pwr_state)
			break;
	} while (t);
	if (!t) {
		pr_err("%s: Timed out waiting for DSP off mode, state %x\n",
		       __func__, pwr_state);
#ifdef CONFIG_BRIDGE_NTFY_PWRERR
		dev_get_deh_mgr(dev_context->hdev_obj, &hdeh_mgr);
		bridge_deh_notify(hdeh_mgr, DSP_PWRERROR, 0);
#endif /* CONFIG_BRIDGE_NTFY_PWRERR */
		spin_unlock_bh(&pwr_lock);
		return WMD_E_TIMEOUT;
	}

	/* Update the Bridger Driver state */
	if (dsp_test_sleepstate == HW_PWR_STATE_OFF)
		dev_context->dw_brd_state = BRD_HIBERNATION;
	else
		dev_context->dw_brd_state = BRD_RETENTION;

	/* Disable wdt on hibernation. */
	dsp_wdt_enable(false);

	/* Turn off DSP Peripheral clocks */
	status = dsp_peripheral_clocks_disable(dev_context, NULL);
	status = services_clk_disable(SERVICESCLK_IVA2_CK);
	if (DSP_FAILED(status)) {
		spin_unlock_bh(&pwr_lock);
		return status;
	}

	spin_unlock_bh(&pwr_lock);

#ifdef CONFIG_BRIDGE_DVFS
	if (target_pwr_state == HW_PWR_STATE_OFF) {
		struct dspbridge_platform_data *pdata =
		    omap_dspbridge_dev->dev.platform_data;
		/*
		 * Set the OPP to low level before moving to OFF mode
		 */
		if (pdata->dsp_set_min_opp)
			(*pdata->dsp_set_min_opp) (VDD1_OPP1);
	}
#endif /* CONFIG_BRIDGE_DVFS */
#endif /* CONFIG_PM */
	return status;
}

/*
 *  ======== wake_dsp ========
 *  	Wake up DSP from sleep.
 */
dsp_status wake_dsp(struct wmd_dev_context *dev_context, IN void *pargs)
{
	dsp_status status = DSP_SOK;
#ifdef CONFIG_PM
	struct cfg_hostres resources;
	u32 temp;

	status = cfg_get_host_resources((struct cfg_devnode *)
					drv_get_first_dev_extension(),
					&resources);
	if (!dev_context->mbox || DSP_FAILED(status))
		return DSP_EFAIL;

	spin_lock_bh(&pwr_lock);

	switch (dev_context->dw_brd_state) {
	case BRD_STOPPED:
		spin_unlock_bh(&pwr_lock);
		return 0;
	case BRD_RUNNING:
		break;
	case BRD_RETENTION:
		services_clk_enable(SERVICESCLK_IVA2_CK);
		/* Restart the peripheral clocks */
		dsp_peripheral_clocks_enable(dev_context, NULL);

		dsp_wdt_enable(true);

		dev_context->dw_brd_state = BRD_RUNNING;

		break;
	case BRD_HIBERNATION:
	case BRD_DSP_HIBERNATION:
		services_clk_enable(SERVICESCLK_IVA2_CK);
		/* Restart the peripheral clocks */
		dsp_peripheral_clocks_enable(dev_context, NULL);
		dsp_wdt_enable(true);

		/*
		 * 2:0 AUTO_IVA2_DPLL - Enabling IVA2 DPLL auto control
		 *     in CM_AUTOIDLE_PLL_IVA2 register
		 */
		*(reg_uword32 *) (resources.dw_cm_base + 0x34) = 0x1;

		/*
		 * 7:4 IVA2_DPLL_FREQSEL - IVA2 internal frq set to
		 *     0.75 MHz - 1.0 MHz
		 * 2:0 EN_IVA2_DPLL - Enable IVA2 DPLL in lock mode
		 */
		temp = *(reg_uword32 *) (resources.dw_cm_base + 0x4);
		temp = (temp & 0xFFFFFF08) | 0x37;
		*(reg_uword32 *) (resources.dw_cm_base + 0x4) = temp;

		/* Restore mailbox settings */
		omap_mbox_restore_ctx(dev_context->mbox);

		/* Access MMU SYS CONFIG register to generate a short wakeup */
		temp = *(reg_uword32 *) (resources.dw_dmmu_base + 0x10);

		dev_context->dw_brd_state = BRD_RUNNING;

		break;
	default:
		pr_err("%s: unexpected state %x\n", __func__,
						dev_context->dw_brd_state);
		spin_unlock_bh(&pwr_lock);
		return DSP_EVALUE;
	}

	/* Send a wakeup message to DSP */
	status = omap_mbox_msg_send(dev_context->mbox, MBX_PM_DSPWAKEUP);

	spin_unlock_bh(&pwr_lock);

#endif /* CONFIG_PM */
	return status;
}

/*
 *  ======== dsp_peripheral_clk_ctrl ========
 *  	Enable/Disable the DSP peripheral clocks as needed..
 */
dsp_status dsp_peripheral_clk_ctrl(struct wmd_dev_context *dev_context,
				   IN void *pargs)
{
	u32 ext_clk = 0;
	u32 ext_clk_id = 0;
	u32 ext_clk_cmd = 0;
	u32 clk_id_index = MBX_PM_MAX_RESOURCES;
	u32 tmp_index;
	u32 dsp_per_clks_before;
	dsp_status status = DSP_SOK;
	dsp_status status1 = DSP_SOK;
	struct cfg_hostres resources;
	u32 value;

	dsp_per_clks_before = dev_context->dsp_per_clks;

	ext_clk = (u32) *((u32 *) pargs);

	status = cfg_get_host_resources((struct cfg_devnode *)
					drv_get_first_dev_extension(),
					&resources);

	if (DSP_FAILED(status))
		return DSP_EFAIL;

	ext_clk_id = ext_clk & MBX_PM_CLK_IDMASK;

	/* process the power message -- TODO, keep it in a separate function */
	for (tmp_index = 0; tmp_index < MBX_PM_MAX_RESOURCES; tmp_index++) {
		if (ext_clk_id == bpwr_clkid[tmp_index]) {
			clk_id_index = tmp_index;
			break;
		}
	}
	/* TODO -- Assert may be a too hard restriction here.. May be we should
	 * just return with failure when the CLK ID does not match */
	/* DBC_ASSERT(clk_id_index < MBX_PM_MAX_RESOURCES); */
	if (clk_id_index == MBX_PM_MAX_RESOURCES) {
		/* return with a more meaningfull error code */
		return DSP_EFAIL;
	}
	ext_clk_cmd = (ext_clk >> MBX_PM_CLK_CMDSHIFT) & MBX_PM_CLK_CMDMASK;
	switch (ext_clk_cmd) {
	case BPWR_DISABLE_CLOCK:
		/* Call BP to disable the needed clock */
		status1 = services_clk_disable(bpwr_clks[clk_id_index].int_clk);
		status = services_clk_disable(bpwr_clks[clk_id_index].fun_clk);
		if (bpwr_clkid[clk_id_index] == BPWR_MCBSP1) {
			/* clear MCBSP1_CLKS, on McBSP1 OFF */
			value = __raw_readl(resources.dw_sys_ctrl_base + 0x274);
			value &= ~(1 << 2);
			__raw_writel(value, resources.dw_sys_ctrl_base + 0x274);
		} else if (bpwr_clkid[clk_id_index] == BPWR_MCBSP2) {
			/* clear MCBSP2_CLKS, on McBSP2 OFF */
			value = __raw_readl(resources.dw_sys_ctrl_base + 0x274);
			value &= ~(1 << 6);
			__raw_writel(value, resources.dw_sys_ctrl_base + 0x274);
		}
		dsp_clk_wakeup_event_ctrl(bpwr_clks[clk_id_index].clk_id,
					  false);
		if ((DSP_SUCCEEDED(status)) && (DSP_SUCCEEDED(status1))) {
			(dev_context->dsp_per_clks) &=
			    (~((u32) (1 << clk_id_index)));
		}
		break;
	case BPWR_ENABLE_CLOCK:
		status1 = services_clk_enable(bpwr_clks[clk_id_index].int_clk);
		status = services_clk_enable(bpwr_clks[clk_id_index].fun_clk);
		if (bpwr_clkid[clk_id_index] == BPWR_MCBSP1) {
			/* set MCBSP1_CLKS, on McBSP1 ON */
			value = __raw_readl(resources.dw_sys_ctrl_base + 0x274);
			value |= 1 << 2;
			__raw_writel(value, resources.dw_sys_ctrl_base + 0x274);
		} else if (bpwr_clkid[clk_id_index] == BPWR_MCBSP2) {
			/* set MCBSP2_CLKS, on McBSP2 ON */
			value = __raw_readl(resources.dw_sys_ctrl_base + 0x274);
			value |= 1 << 6;
			__raw_writel(value, resources.dw_sys_ctrl_base + 0x274);
		}
		dsp_clk_wakeup_event_ctrl(bpwr_clks[clk_id_index].clk_id, true);
		if ((DSP_SUCCEEDED(status)) && (DSP_SUCCEEDED(status1))) {
			(dev_context->dsp_per_clks) |= (1 << clk_id_index);
		}
		break;
	default:
		dev_dbg(bridge, "%s: Unsupported CMD\n", __func__);
		/* unsupported cmd */
		/* TODO -- provide support for AUTOIDLE Enable/Disable
		 * commands */
	}
	return status;
}

/*
 *  ========pre_scale_dsp========
 *  Sends prescale notification to DSP
 *
 */
dsp_status pre_scale_dsp(struct wmd_dev_context *dev_context, IN void *pargs)
{
#ifdef CONFIG_BRIDGE_DVFS
	u32 level;
	u32 voltage_domain;

	voltage_domain = *((u32 *) pargs);
	level = *((u32 *) pargs + 1);

	dev_dbg(bridge, "OPP: %s voltage_domain = %x, level = 0x%x\n",
		__func__, voltage_domain, level);
	if ((dev_context->dw_brd_state == BRD_HIBERNATION) ||
	    (dev_context->dw_brd_state == BRD_RETENTION) ||
	    (dev_context->dw_brd_state == BRD_DSP_HIBERNATION)) {
		dev_dbg(bridge, "OPP: %s IVA in sleep. No message to DSP\n",
			__func__);
		return DSP_SOK;
	} else if ((dev_context->dw_brd_state == BRD_RUNNING)) {
		/* Send a prenotificatio to DSP */
		dev_dbg(bridge, "OPP: %s sent notification to DSP\n", __func__);
		sm_interrupt_dsp(dev_context, MBX_PM_SETPOINT_PRENOTIFY);
		return DSP_SOK;
	} else {
		return DSP_EFAIL;
	}
#endif /* #ifdef CONFIG_BRIDGE_DVFS */
	return DSP_SOK;
}

/*
 *  ========post_scale_dsp========
 *  Sends postscale notification to DSP
 *
 */
dsp_status post_scale_dsp(struct wmd_dev_context *dev_context, IN void *pargs)
{
	dsp_status status = DSP_SOK;
#ifdef CONFIG_BRIDGE_DVFS
	u32 level;
	u32 voltage_domain;
	struct io_mgr *hio_mgr;

	status = dev_get_io_mgr(dev_context->hdev_obj, &hio_mgr);
	if (!hio_mgr)
		return DSP_EHANDLE;

	voltage_domain = *((u32 *) pargs);
	level = *((u32 *) pargs + 1);
	dev_dbg(bridge, "OPP: %s voltage_domain = %x, level = 0x%x\n",
		__func__, voltage_domain, level);
	if ((dev_context->dw_brd_state == BRD_HIBERNATION) ||
	    (dev_context->dw_brd_state == BRD_RETENTION) ||
	    (dev_context->dw_brd_state == BRD_DSP_HIBERNATION)) {
		/* Update the OPP value in shared memory */
		io_sh_msetting(hio_mgr, SHM_CURROPP, &level);
		dev_dbg(bridge, "OPP: %s IVA in sleep. Wrote to shm\n",
			__func__);
	} else if ((dev_context->dw_brd_state == BRD_RUNNING)) {
		/* Update the OPP value in shared memory */
		io_sh_msetting(hio_mgr, SHM_CURROPP, &level);
		/* Send a post notification to DSP */
		sm_interrupt_dsp(dev_context, MBX_PM_SETPOINT_POSTNOTIFY);
		dev_dbg(bridge, "OPP: %s wrote to shm. Sent post notification "
			"to DSP\n", __func__);
	} else {
		status = DSP_EFAIL;
	}
#endif /* #ifdef CONFIG_BRIDGE_DVFS */
	return status;
}

/*
 *  ========dsp_peripheral_clocks_disable========
 *  Disables all the peripheral clocks that were requested by DSP
 */
dsp_status dsp_peripheral_clocks_disable(struct wmd_dev_context *dev_context,
					 IN void *pargs)
{
	u32 clk_idx;
	dsp_status status = DSP_SOK;
	struct cfg_hostres resources;
	u32 value;

	status = cfg_get_host_resources((struct cfg_devnode *)
					drv_get_first_dev_extension(),
					&resources);

	for (clk_idx = 0; clk_idx < MBX_PM_MAX_RESOURCES; clk_idx++) {
		if (((dev_context->dsp_per_clks) >> clk_idx) & 0x01) {
			/* Disables the interface clock of the peripheral */
			status =
			    services_clk_disable(bpwr_clks[clk_idx].int_clk);
			if (bpwr_clkid[clk_idx] == BPWR_MCBSP1) {
				/* clear MCBSP1_CLKS, on McBSP1 OFF */
				value = __raw_readl(resources.dw_sys_ctrl_base
						    + 0x274);
				value &= ~(1 << 2);
				__raw_writel(value, resources.dw_sys_ctrl_base
					     + 0x274);
			} else if (bpwr_clkid[clk_idx] == BPWR_MCBSP2) {
				/* clear MCBSP2_CLKS, on McBSP2 OFF */
				value = __raw_readl(resources.dw_sys_ctrl_base
						    + 0x274);
				value &= ~(1 << 6);
				__raw_writel(value, resources.dw_sys_ctrl_base
					     + 0x274);
			}

			/* Disables the functional clock of the periphearl */
			status =
			    services_clk_disable(bpwr_clks[clk_idx].fun_clk);
		}
	}
	return status;
}

/*
 *  ========dsp_peripheral_clocks_enable========
 *  Enables all the peripheral clocks that were requested by DSP
 */
dsp_status dsp_peripheral_clocks_enable(struct wmd_dev_context *dev_context,
					IN void *pargs)
{
	u32 clk_idx;
	dsp_status int_clk_status = DSP_EFAIL, fun_clk_status = DSP_EFAIL;
	struct cfg_hostres resources;
	u32 value;

	cfg_get_host_resources((struct cfg_devnode *)
			       drv_get_first_dev_extension(), &resources);

	for (clk_idx = 0; clk_idx < MBX_PM_MAX_RESOURCES; clk_idx++) {
		if (((dev_context->dsp_per_clks) >> clk_idx) & 0x01) {
			/* Enable the interface clock of the peripheral */
			int_clk_status =
			    services_clk_enable(bpwr_clks[clk_idx].int_clk);
			if (bpwr_clkid[clk_idx] == BPWR_MCBSP1) {
				/* set MCBSP1_CLKS, on McBSP1 ON */
				value = __raw_readl(resources.dw_sys_ctrl_base
						    + 0x274);
				value |= 1 << 2;
				__raw_writel(value, resources.dw_sys_ctrl_base
					     + 0x274);
			} else if (bpwr_clkid[clk_idx] == BPWR_MCBSP2) {
				/* set MCBSP2_CLKS, on McBSP2 ON */
				value = __raw_readl(resources.dw_sys_ctrl_base
						    + 0x274);
				value |= 1 << 6;
				__raw_writel(value, resources.dw_sys_ctrl_base
					     + 0x274);
			}
			/* Enable the functional clock of the periphearl */
			fun_clk_status =
			    services_clk_enable(bpwr_clks[clk_idx].fun_clk);
		}
	}
	if ((int_clk_status | fun_clk_status) != DSP_SOK)
		return DSP_EFAIL;
	return DSP_SOK;
}

void dsp_clk_wakeup_event_ctrl(u32 ClkId, bool enable)
{
	struct cfg_hostres resources;
	dsp_status status = DSP_SOK;
	u32 iva2_grpsel;
	u32 mpu_grpsel;

	status = cfg_get_host_resources((struct cfg_devnode *)
					drv_get_first_dev_extension(),
					&resources);
	if (DSP_FAILED(status))
		return;

	switch (ClkId) {
	case BPWR_GP_TIMER5:
		iva2_grpsel = (u32) *((reg_uword32 *)
				       ((u32) (resources.dw_per_pm_base) +
					0xA8));
		mpu_grpsel = (u32) *((reg_uword32 *)
				      ((u32) (resources.dw_per_pm_base) +
				       0xA4));
		if (enable) {
			iva2_grpsel |= OMAP3430_GRPSEL_GPT5;
			mpu_grpsel &= ~OMAP3430_GRPSEL_GPT5;
		} else {
			mpu_grpsel |= OMAP3430_GRPSEL_GPT5;
			iva2_grpsel &= ~OMAP3430_GRPSEL_GPT5;
		}
		*((reg_uword32 *) ((u32) (resources.dw_per_pm_base) + 0xA8))
		    = iva2_grpsel;
		*((reg_uword32 *) ((u32) (resources.dw_per_pm_base) + 0xA4))
		    = mpu_grpsel;
		break;
	case BPWR_GP_TIMER6:
		iva2_grpsel = (u32) *((reg_uword32 *)
				       ((u32) (resources.dw_per_pm_base) +
					0xA8));
		mpu_grpsel = (u32) *((reg_uword32 *)
				      ((u32) (resources.dw_per_pm_base) +
				       0xA4));
		if (enable) {
			iva2_grpsel |= OMAP3430_GRPSEL_GPT6;
			mpu_grpsel &= ~OMAP3430_GRPSEL_GPT6;
		} else {
			mpu_grpsel |= OMAP3430_GRPSEL_GPT6;
			iva2_grpsel &= ~OMAP3430_GRPSEL_GPT6;
		}
		*((reg_uword32 *) ((u32) (resources.dw_per_pm_base) + 0xA8))
		    = iva2_grpsel;
		*((reg_uword32 *) ((u32) (resources.dw_per_pm_base) + 0xA4))
		    = mpu_grpsel;
		break;
	case BPWR_GP_TIMER7:
		iva2_grpsel = (u32) *((reg_uword32 *)
				       ((u32) (resources.dw_per_pm_base) +
					0xA8));
		mpu_grpsel = (u32) *((reg_uword32 *)
				      ((u32) (resources.dw_per_pm_base) +
				       0xA4));
		if (enable) {
			iva2_grpsel |= OMAP3430_GRPSEL_GPT7;
			mpu_grpsel &= ~OMAP3430_GRPSEL_GPT7;
		} else {
			mpu_grpsel |= OMAP3430_GRPSEL_GPT7;
			iva2_grpsel &= ~OMAP3430_GRPSEL_GPT7;
		}
		*((reg_uword32 *) ((u32) (resources.dw_per_pm_base) + 0xA8))
		    = iva2_grpsel;
		*((reg_uword32 *) ((u32) (resources.dw_per_pm_base) + 0xA4))
		    = mpu_grpsel;
		break;
	case BPWR_GP_TIMER8:
		iva2_grpsel = (u32) *((reg_uword32 *)
				       ((u32) (resources.dw_per_pm_base) +
					0xA8));
		mpu_grpsel = (u32) *((reg_uword32 *)
				      ((u32) (resources.dw_per_pm_base) +
				       0xA4));
		if (enable) {
			iva2_grpsel |= OMAP3430_GRPSEL_GPT8;
			mpu_grpsel &= ~OMAP3430_GRPSEL_GPT8;
		} else {
			mpu_grpsel |= OMAP3430_GRPSEL_GPT8;
			iva2_grpsel &= ~OMAP3430_GRPSEL_GPT8;
		}
		*((reg_uword32 *) ((u32) (resources.dw_per_pm_base) + 0xA8))
		    = iva2_grpsel;
		*((reg_uword32 *) ((u32) (resources.dw_per_pm_base) + 0xA4))
		    = mpu_grpsel;
		break;
	case BPWR_MCBSP1:
		iva2_grpsel = (u32) *((reg_uword32 *)
				       ((u32) (resources.dw_core_pm_base) +
					0xA8));
		mpu_grpsel = (u32) *((reg_uword32 *)
				      ((u32) (resources.dw_core_pm_base) +
				       0xA4));
		if (enable) {
			iva2_grpsel |= OMAP3430_GRPSEL_MCBSP1;
			mpu_grpsel &= ~OMAP3430_GRPSEL_MCBSP1;
		} else {
			mpu_grpsel |= OMAP3430_GRPSEL_MCBSP1;
			iva2_grpsel &= ~OMAP3430_GRPSEL_MCBSP1;
		}
		*((reg_uword32 *) ((u32) (resources.dw_core_pm_base) + 0xA8))
		    = iva2_grpsel;
		*((reg_uword32 *) ((u32) (resources.dw_core_pm_base) + 0xA4))
		    = mpu_grpsel;
		break;
	case BPWR_MCBSP2:
		iva2_grpsel = (u32) *((reg_uword32 *)
				       ((u32) (resources.dw_per_pm_base) +
					0xA8));
		mpu_grpsel = (u32) *((reg_uword32 *)
				      ((u32) (resources.dw_per_pm_base) +
				       0xA4));
		if (enable) {
			iva2_grpsel |= OMAP3430_GRPSEL_MCBSP2;
			mpu_grpsel &= ~OMAP3430_GRPSEL_MCBSP2;
		} else {
			mpu_grpsel |= OMAP3430_GRPSEL_MCBSP2;
			iva2_grpsel &= ~OMAP3430_GRPSEL_MCBSP2;
		}
		*((reg_uword32 *) ((u32) (resources.dw_per_pm_base) + 0xA8))
		    = iva2_grpsel;
		*((reg_uword32 *) ((u32) (resources.dw_per_pm_base) + 0xA4))
		    = mpu_grpsel;
		break;
	case BPWR_MCBSP3:
		iva2_grpsel = (u32) *((reg_uword32 *)
				       ((u32) (resources.dw_per_pm_base) +
					0xA8));
		mpu_grpsel = (u32) *((reg_uword32 *)
				      ((u32) (resources.dw_per_pm_base) +
				       0xA4));
		if (enable) {
			iva2_grpsel |= OMAP3430_GRPSEL_MCBSP3;
			mpu_grpsel &= ~OMAP3430_GRPSEL_MCBSP3;
		} else {
			mpu_grpsel |= OMAP3430_GRPSEL_MCBSP3;
			iva2_grpsel &= ~OMAP3430_GRPSEL_MCBSP3;
		}
		*((reg_uword32 *) ((u32) (resources.dw_per_pm_base) + 0xA8))
		    = iva2_grpsel;
		*((reg_uword32 *) ((u32) (resources.dw_per_pm_base) + 0xA4))
		    = mpu_grpsel;
		break;
	case BPWR_MCBSP4:
		iva2_grpsel = (u32) *((reg_uword32 *)
				       ((u32) (resources.dw_per_pm_base) +
					0xA8));
		mpu_grpsel = (u32) *((reg_uword32 *)
				      ((u32) (resources.dw_per_pm_base) +
				       0xA4));
		if (enable) {
			iva2_grpsel |= OMAP3430_GRPSEL_MCBSP4;
			mpu_grpsel &= ~OMAP3430_GRPSEL_MCBSP4;
		} else {
			mpu_grpsel |= OMAP3430_GRPSEL_MCBSP4;
			iva2_grpsel &= ~OMAP3430_GRPSEL_MCBSP4;
		}
		*((reg_uword32 *) ((u32) (resources.dw_per_pm_base) + 0xA8))
		    = iva2_grpsel;
		*((reg_uword32 *) ((u32) (resources.dw_per_pm_base) + 0xA4))
		    = mpu_grpsel;
		break;
	case BPWR_MCBSP5:
		iva2_grpsel = (u32) *((reg_uword32 *)
				       ((u32) (resources.dw_core_pm_base) +
					0xA8));
		mpu_grpsel = (u32) *((reg_uword32 *)
				      ((u32) (resources.dw_core_pm_base) +
				       0xA4));
		if (enable) {
			iva2_grpsel |= OMAP3430_GRPSEL_MCBSP5;
			mpu_grpsel &= ~OMAP3430_GRPSEL_MCBSP5;
		} else {
			mpu_grpsel |= OMAP3430_GRPSEL_MCBSP5;
			iva2_grpsel &= ~OMAP3430_GRPSEL_MCBSP5;
		}
		*((reg_uword32 *) ((u32) (resources.dw_core_pm_base) + 0xA8))
		    = iva2_grpsel;
		*((reg_uword32 *) ((u32) (resources.dw_core_pm_base) + 0xA4))
		    = mpu_grpsel;
		break;
	}
}
