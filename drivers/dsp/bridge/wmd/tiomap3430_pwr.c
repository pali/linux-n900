/*
 * tiomap_pwr.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
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

/*
 *  ======== _tiomap_pwr.c ========
 *  Description:
 *      Implementation of DSP wake/sleep routines.
 *
 *! Revision History
 *! ================
 *! 01-Nov-2007 HK: Added Off mode(Hibernation) support and DVFS support
 *! 05-Jan-2004 vp: Moved the file to platform specific folder and commented the
 *!		    code.
 *! 27-Mar-2003 vp: Added support for DSP boot idle mode.
 *! 06-Dec-2002 cring:  Added Palm support.
 *! 08-Oct-2002 rr:  Created.
 */

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/dbdefs.h>
#include <dspbridge/errbase.h>
#include <dspbridge/cfg.h>
#include <dspbridge/drv.h>
#include <dspbridge/io_sm.h>
#include <dspbridge/chnl_sm.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbg.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/mem.h>
#include <dspbridge/util.h>

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

/*  ----------------------------------- specific to this file */
#include "_tiomap.h"
#include "_tiomap_pwr.h"
#include "_tiomap_util.h"
#include <mach-omap2/prm-regbits-34xx.h>
#include <mach-omap2/cm-regbits-34xx.h>

#ifdef CONFIG_PM
#include <mach/board-3430sdp.h>
#endif
extern struct MAILBOX_CONTEXT mboxsetting;
extern unsigned short enable_off_mode;
extern unsigned short min_active_opp;
/*
 *  ======== handle_constraints_set ========
 *  	Sets new DSP constraint
 */
DSP_STATUS handle_constraints_set(struct WMD_DEV_CONTEXT *pDevContext,
				  IN void *pArgs)
{
#ifdef CONFIG_BRIDGE_DVFS
	u32 pConstraintVal;
	DSP_STATUS status = DSP_SOK;
	struct CFG_HOSTRES resources;
	struct dspbridge_platform_data *pdata =
		omap_dspbridge_dev->dev.platform_data;
	status = CFG_GetHostResources(
		(struct CFG_DEVNODE *)DRV_GetFirstDevExtension(), &resources);

	pConstraintVal = *(((u32 *)pArgs) + 1);
	/* Read the target value requested by DSP  */
	DBG_Trace(DBG_LEVEL7, "handle_constraints_set:"
		"opp requested = 0x%x\n", pConstraintVal);
	status = HW_MBOX_saveSettings(resources.dwMboxBase);

	/* Set the new opp value */
	if (pdata->dsp_set_min_opp) {
		/*
		 * When Smartreflex is ON, DSP requires at least OPP level 3
		 * to operate reliably. So boost lower OPP levels to OPP3.
		 */
		if (pConstraintVal < min_active_opp) {
			pr_debug("DSPBRIDGE: VDD1 OPP%x elevated to OPP%x\n",
					pConstraintVal, min_active_opp);
			(*pdata->dsp_set_min_opp)(min_active_opp);
		} else
			(*pdata->dsp_set_min_opp)(pConstraintVal);
	}
#endif /* #ifdef CONFIG_BRIDGE_DVFS */
	return DSP_SOK;
}

/*
 *  ======== handle_hibernation_fromDSP ========
 *  	Handle Hibernation requested from DSP
 */
DSP_STATUS handle_hibernation_fromDSP(struct WMD_DEV_CONTEXT *pDevContext)
{
	DSP_STATUS status = DSP_SOK;
#ifdef CONFIG_PM
	u16 usCount = TIHELEN_ACKTIMEOUT;
	struct CFG_HOSTRES resources;
	enum HW_PwrState_t pwrState;
#ifdef CONFIG_BRIDGE_DVFS
	u32 opplevel;
	struct IO_MGR *hIOMgr;
	struct dspbridge_platform_data *pdata =
				omap_dspbridge_dev->dev.platform_data;
#endif

	status = CFG_GetHostResources(
		 (struct CFG_DEVNODE *)DRV_GetFirstDevExtension(), &resources);
	if (DSP_FAILED(status))
		return status;

	HW_PWR_IVA2StateGet(resources.dwPrmBase, HW_PWR_DOMAIN_DSP,
			    &pwrState);
	/* Wait for DSP to move into Off state,  how much time should
	 * we wait? */
	while ((pwrState != HW_PWR_STATE_OFF) && --usCount) {
		udelay(500);
		HW_PWR_IVA2StateGet(resources.dwPrmBase, HW_PWR_DOMAIN_DSP,
				    &pwrState);
	}
	if (usCount == 0) {
		DBG_Trace(DBG_LEVEL7, "Timed out Waiting for DSP Off mode \n");
		status = WMD_E_TIMEOUT;
		return status;
	} else {

		/* Save mailbox settings */
		status = HW_MBOX_saveSettings(resources.dwMboxBase);
		DBG_Trace(DBG_LEVEL6, "MailBoxSettings: SYSCONFIG = 0x%x\n",
			 mboxsetting.sysconfig);
		DBG_Trace(DBG_LEVEL6, "MailBoxSettings: IRQENABLE0 = 0x%x\n",
			 mboxsetting.irqEnable0);
		DBG_Trace(DBG_LEVEL6, "MailBoxSettings: IRQENABLE1 = 0x%x\n",
			 mboxsetting.irqEnable1);
		/* Turn off DSP Peripheral clocks and DSP Load monitor timer */
		status = DSP_PeripheralClocks_Disable(pDevContext, NULL);

		if (DSP_SUCCEEDED(status)) {
			/* Update the Bridger Driver state */
			pDevContext->dwBrdState = BRD_DSP_HIBERNATION;
#ifdef CONFIG_BRIDGE_DVFS
			status = DEV_GetIOMgr(pDevContext->hDevObject, &hIOMgr);
			if (DSP_FAILED(status))
				return status;
			IO_SHMsetting(hIOMgr, SHM_GETOPP, &opplevel);
			if (opplevel != VDD1_OPP1) {
				DBG_Trace(DBG_LEVEL5,
					" DSP requested OPP = %d, MPU"
					" requesting low OPP %d instead\n",
					opplevel, VDD1_OPP1);
			}
			/*
			 * Set the OPP to low level before moving to OFF
			 * mode
			 */
			if (pdata->dsp_set_min_opp)
				(*pdata->dsp_set_min_opp)(VDD1_OPP1);
			status = DSP_SOK;
#endif /* CONFIG_BRIDGE_DVFS */
		} else {
			DBG_Trace(DBG_LEVEL7,
				 "handle_hibernation_fromDSP- FAILED\n");
		}
	}
#endif
	return status;
}

/*
 *  ======== SleepDSP ========
 *  	Put DSP in low power consuming state.
 */
DSP_STATUS SleepDSP(struct WMD_DEV_CONTEXT *pDevContext, IN u32 dwCmd,
		   IN void *pArgs)
{
	DSP_STATUS status = DSP_SOK;
#ifdef CONFIG_PM
	struct CFG_HOSTRES resources;
	struct DEH_MGR *hDehMgr;
	u16 usCount = TIHELEN_ACKTIMEOUT;
	enum HW_PwrState_t pwrState, targetPwrState;

	DBG_Trace(DBG_LEVEL7, "SleepDSP- Enter function \n");

	/* Check if sleep code is valid */
	if ((dwCmd != PWR_DEEPSLEEP) && (dwCmd != PWR_EMERGENCYDEEPSLEEP)) {
		DBG_Trace(DBG_LEVEL7, "SleepDSP- Illegal sleep command\n");
		return DSP_EINVALIDARG;
	}

	status = CFG_GetHostResources(
		 (struct CFG_DEVNODE *)DRV_GetFirstDevExtension(), &resources);
	if (DSP_FAILED(status))
		return status;

	switch (pDevContext->dwBrdState) {
	case BRD_RUNNING:
		status = HW_MBOX_saveSettings(resources.dwMboxBase);
		if (enable_off_mode) {
			CHNLSM_InterruptDSP2(pDevContext,
					     MBX_PM_DSPHIBERNATE);
			DBG_Trace(DBG_LEVEL7,
				 "SleepDSP - Sent hibernate "
				 "command to DSP\n");
			targetPwrState = HW_PWR_STATE_OFF;
		} else {
			CHNLSM_InterruptDSP2(pDevContext,
					     MBX_PM_DSPRETENTION);
			targetPwrState = HW_PWR_STATE_RET;
		}
		break;
	case BRD_RETENTION:
		status = HW_MBOX_saveSettings(resources.dwMboxBase);
		if (enable_off_mode) {
			CHNLSM_InterruptDSP2(pDevContext,
					     MBX_PM_DSPHIBERNATE);
			targetPwrState = HW_PWR_STATE_OFF;
		} else
			return DSP_SOK;
		break;
	case BRD_HIBERNATION:
	case BRD_DSP_HIBERNATION:
		/* Already in Hibernation, so just return */
		DBG_Trace(DBG_LEVEL7, "SleepDSP- DSP already in "
			 "hibernation\n");
		return DSP_SOK;
	case BRD_STOPPED:
		DBG_Trace(DBG_LEVEL7,
			 "SleepDSP- Board in STOP state \n");
		return DSP_SALREADYASLEEP;
	default:
		DBG_Trace(DBG_LEVEL7,
			 "SleepDSP- Bridge in Illegal state\n");
			return DSP_EFAIL;
	}

	/* Get the PRCM DSP power domain status */
	HW_PWR_IVA2StateGet(resources.dwPrmBase, HW_PWR_DOMAIN_DSP,
			&pwrState);

	/*
	 * Wait for DSP to move into Standby state,  how much time
	 * should we wait?
	 */
	while ((pwrState != targetPwrState) && --usCount) {
		udelay(500);
		HW_PWR_IVA2StateGet(resources.dwPrmBase, HW_PWR_DOMAIN_DSP,
				    &pwrState);
	}

	if (!usCount) {
		DBG_Trace(DBG_LEVEL7, "SleepDSP: Timed out Waiting for DSP"
			 " STANDBY %x \n", pwrState);
		DEV_GetDehMgr(pDevContext->hDevObject, &hDehMgr);
		WMD_DEH_Notify(hDehMgr, DSP_PWRERROR, 0);
		return WMD_E_TIMEOUT;
	} else {
		DBG_Trace(DBG_LEVEL7, "SleepDSP: DSP STANDBY Pwr state %x \n",
			 pwrState);

		/* Update the Bridger Driver state */
		if (enable_off_mode)
			pDevContext->dwBrdState = BRD_HIBERNATION;
		else
			pDevContext->dwBrdState = BRD_RETENTION;

		/* Turn off DSP Peripheral clocks  */
		status = DSP_PeripheralClocks_Disable(pDevContext, NULL);
		if (DSP_FAILED(status)) {
			DBG_Trace(DBG_LEVEL7, "SleepDSP- FAILED\n");
			return status;
		}
#ifdef CONFIG_BRIDGE_DVFS
		else if (targetPwrState == HW_PWR_STATE_OFF) {
			struct dspbridge_platform_data *pdata =
				omap_dspbridge_dev->dev.platform_data;
			/*
			 * Set the OPP to low level before moving to OFF mode
			 */
			if (pdata->dsp_set_min_opp)
				(*pdata->dsp_set_min_opp)(VDD1_OPP1);
		}
#endif /* CONFIG_BRIDGE_DVFS */
	}
#endif /* CONFIG_PM */
	return status;
}


/*
 *  ======== WakeDSP ========
 *  	Wake up DSP from sleep.
 */
DSP_STATUS WakeDSP(struct WMD_DEV_CONTEXT *pDevContext, IN void *pArgs)
{
#ifdef CONFIG_PM
	DSP_STATUS status = DSP_SOK;
#ifdef CONFIG_BRIDGE_DEBUG
	enum HW_PwrState_t pwrState;
	struct CFG_HOSTRES resources;

	status = CFG_GetHostResources(
		 (struct CFG_DEVNODE *)DRV_GetFirstDevExtension(), &resources);
	if (DSP_FAILED(status))
		return status;
#endif /* CONFIG_BRIDGE_DEBUG */

	/* Check the BRD/WMD state, if it is not 'SLEEP' then return failure */
	if (pDevContext->dwBrdState == BRD_RUNNING ||
	    pDevContext->dwBrdState == BRD_STOPPED) {
		/* The Device is in 'RET' or 'OFF' state and WMD state is not
		 * 'SLEEP', this means state inconsistency, so return  */
		return DSP_SOK;
	}

	/* Send a wakeup message to DSP */
	CHNLSM_InterruptDSP2(pDevContext, MBX_PM_DSPWAKEUP);

#ifdef CONFIG_BRIDGE_DEBUG
	HW_PWR_IVA2StateGet(resources.dwPrmBase, HW_PWR_DOMAIN_DSP,
			&pwrState);
	DBG_Trace(DBG_LEVEL7,
			"\nWakeDSP: Power State After sending Interrupt "
			"to DSP %x\n", pwrState);
#endif /* CONFIG_BRIDGE_DEBUG */

	/* Set the device state to RUNNIG */
	pDevContext->dwBrdState = BRD_RUNNING;
#endif /* CONFIG_PM */
	return status;
}

/*
 *  ======== DSPPeripheralClkCtrl ========
 *  	Enable/Disable the DSP peripheral clocks as needed..
 */
DSP_STATUS DSPPeripheralClkCtrl(struct WMD_DEV_CONTEXT *pDevContext,
				IN void *pArgs)
{
	u32 extClk = 0;
	u32 extClkId = 0;
	u32 extClkCmd = 0;
	u32 clkIdIndex = MBX_PM_MAX_RESOURCES;
	u32 tmpIndex;
	u32 dspPerClksBefore;
	DSP_STATUS status = DSP_SOK;
	DSP_STATUS status1 = DSP_SOK;

	DBG_Trace(DBG_ENTER, "Entering DSPPeripheralClkCtrl \n");
	dspPerClksBefore = pDevContext->uDspPerClks;
	DBG_Trace(DBG_ENTER, "DSPPeripheralClkCtrl : uDspPerClks = 0x%x \n",
		  dspPerClksBefore);

	extClk = (u32)*((u32 *)pArgs);

	DBG_Trace(DBG_LEVEL3, "DSPPeripheralClkCtrl : extClk+Cmd = 0x%x \n",
		 extClk);

	extClkId = extClk & MBX_PM_CLK_IDMASK;

	/* process the power message -- TODO, keep it in a separate function */
	for (tmpIndex = 0; tmpIndex < MBX_PM_MAX_RESOURCES; tmpIndex++) {
		if (extClkId == BPWR_CLKID[tmpIndex]) {
			clkIdIndex = tmpIndex;
			break;
		}
	}
	/* TODO -- Assert may be a too hard restriction here.. May be we should
	 * just return with failure when the CLK ID does not match */
	/* DBC_Assert(clkIdIndex < MBX_PM_MAX_RESOURCES);*/
	if (clkIdIndex == MBX_PM_MAX_RESOURCES) {
		DBG_Trace(DBG_LEVEL7,
			 "DSPPeripheralClkCtrl : Could n't get clock Id for"
			 "clkid 0x%x \n", clkIdIndex);
		/* return with a more meaningfull error code */
		return DSP_EFAIL;
	}
	extClkCmd = (extClk >> MBX_PM_CLK_CMDSHIFT) & MBX_PM_CLK_CMDMASK;
	switch (extClkCmd) {
	case BPWR_DisableClock:
		/* Call BP to disable the needed clock */
		DBG_Trace(DBG_LEVEL3,
			 "DSPPeripheralClkCtrl : Disable CLK for \n");
		status1 = CLK_Disable(BPWR_Clks[clkIdIndex].intClk);
		status = CLK_Disable(BPWR_Clks[clkIdIndex].funClk);
		DSPClkWakeupEventCtrl(BPWR_Clks[clkIdIndex].clkId, false);
		if ((DSP_SUCCEEDED(status)) && (DSP_SUCCEEDED(status1))) {
			(pDevContext->uDspPerClks) &=
				(~((u32) (1 << clkIdIndex)));
		} else {
			DBG_Trace(DBG_LEVEL7, "DSPPeripheralClkCtrl : Failed "
				 "to disable clk\n");
		}
		break;
	case BPWR_EnableClock:
		DBG_Trace(DBG_LEVEL3,
			 "DSPPeripheralClkCtrl : Enable CLK for \n");
		status1 = CLK_Enable(BPWR_Clks[clkIdIndex].intClk);
		status = CLK_Enable(BPWR_Clks[clkIdIndex].funClk);
		DSPClkWakeupEventCtrl(BPWR_Clks[clkIdIndex].clkId, true);
		if ((DSP_SUCCEEDED(status)) && (DSP_SUCCEEDED(status1))) {
			(pDevContext->uDspPerClks) |= (1 << clkIdIndex);
		} else {
			DBG_Trace(DBG_LEVEL7,
				 "DSPPeripheralClkCtrl:Failed to Enable clk\n");
		}
		break;
	default:
		DBG_Trace(DBG_LEVEL3,
			 "DSPPeripheralClkCtrl : Unsupported CMD \n");
		/* unsupported cmd */
		/* TODO -- provide support for AUTOIDLE Enable/Disable
		 * commands */
	}
	return status;
}

/*
 *  ========PreScale_DSP========
 *  Sends prescale notification to DSP
 *
 */
DSP_STATUS PreScale_DSP(struct WMD_DEV_CONTEXT *pDevContext, IN void *pArgs)
{
#ifdef CONFIG_BRIDGE_DVFS
	u32 level;
	u32 voltage_domain;

	voltage_domain = *((u32 *)pArgs);
	level = *((u32 *)pArgs + 1);

	DBG_Trace(DBG_LEVEL7, "PreScale_DSP: voltage_domain = %x, level = "
		 "0x%x\n", voltage_domain, level);
	if ((pDevContext->dwBrdState == BRD_HIBERNATION) ||
			(pDevContext->dwBrdState == BRD_RETENTION) ||
			(pDevContext->dwBrdState == BRD_DSP_HIBERNATION)) {
		DBG_Trace(DBG_LEVEL7, "PreScale_DSP: IVA in sleep. "
			 "No notification to DSP\n");
		return DSP_SOK;
	} else if ((pDevContext->dwBrdState == BRD_RUNNING)) {
		/* Send a prenotificatio to DSP */
		DBG_Trace(DBG_LEVEL7,
			 "PreScale_DSP: Sent notification to DSP\n");
		CHNLSM_InterruptDSP2(pDevContext, MBX_PM_SETPOINT_PRENOTIFY);
		return DSP_SOK;
	} else {
		DBG_Trace(DBG_LEVEL7, "PreScale_DSP: Failed - DSP BRD"
			  " state in wrong state");
		return DSP_EFAIL;
	}
#endif /* #ifdef CONFIG_BRIDGE_DVFS */
	return DSP_SOK;
}

/*
 *  ========PostScale_DSP========
 *  Sends postscale notification to DSP
 *
 */
DSP_STATUS PostScale_DSP(struct WMD_DEV_CONTEXT *pDevContext, IN void *pArgs)
{
#ifdef CONFIG_BRIDGE_DVFS
	u32 level;
	u32 voltage_domain;
	struct IO_MGR *hIOMgr;
	DSP_STATUS status = DSP_SOK;

	status = DEV_GetIOMgr(pDevContext->hDevObject, &hIOMgr);

	voltage_domain = *((u32 *)pArgs);
	level = *((u32 *)pArgs + 1);
	DBG_Trace(DBG_LEVEL7,
		"PostScale_DSP: voltage_domain = %x, level = 0x%x\n",
		voltage_domain, level);
	if ((pDevContext->dwBrdState == BRD_HIBERNATION) ||
			(pDevContext->dwBrdState == BRD_RETENTION) ||
			(pDevContext->dwBrdState == BRD_DSP_HIBERNATION)) {
		/* Update the OPP value in shared memory */
		IO_SHMsetting(hIOMgr, SHM_CURROPP, &level);
		DBG_Trace(DBG_LEVEL7,
			 "PostScale_DSP: IVA in sleep. Wrote to shared "
			 "memory \n");
		return DSP_SOK;
	} else  if ((pDevContext->dwBrdState == BRD_RUNNING)) {
		/* Update the OPP value in shared memory */
		IO_SHMsetting(hIOMgr, SHM_CURROPP, &level);
		/* Send a post notification to DSP */
		CHNLSM_InterruptDSP2(pDevContext, MBX_PM_SETPOINT_POSTNOTIFY);
		DBG_Trace(DBG_LEVEL7,
			"PostScale_DSP: Wrote to shared memory Sent post"
			" notification to DSP\n");
		return DSP_SOK;
	} else {
		DBG_Trace(DBG_LEVEL7, "PostScale_DSP: Failed - DSP BRD state "
			"in wrong state");
		return DSP_EFAIL;
	}
#endif /* #ifdef CONFIG_BRIDGE_DVFS */
	return DSP_SOK;
}

/*
 *  ========DSP_PeripheralClocks_Disable========
 *  Disables all the peripheral clocks that were requested by DSP
 */
DSP_STATUS DSP_PeripheralClocks_Disable(struct WMD_DEV_CONTEXT *pDevContext,
					IN void *pArgs)
{

	u32 clkIdx;
	DSP_STATUS status = DSP_SOK;

	for (clkIdx = 0; clkIdx < MBX_PM_MAX_RESOURCES; clkIdx++) {
		if (((pDevContext->uDspPerClks) >> clkIdx) & 0x01) {
			/* Disables the interface clock of the peripheral */
			status = CLK_Disable(BPWR_Clks[clkIdx].intClk);
			if (DSP_FAILED(status)) {
				DBG_Trace(DBG_LEVEL7,
					 "Failed to Enable the DSP Peripheral"
					 "Clk 0x%x \n", BPWR_Clks[clkIdx]);
			}
			/* Disables the functional clock of the periphearl */
			status = CLK_Disable(BPWR_Clks[clkIdx].funClk);
			if (DSP_FAILED(status)) {
				DBG_Trace(DBG_LEVEL7,
					 "Failed to Enable the DSP Peripheral"
					 "Clk 0x%x \n", BPWR_Clks[clkIdx]);
			}
		}
	}
	return status;
}

/*
 *  ========DSP_PeripheralClocks_Enable========
 *  Enables all the peripheral clocks that were requested by DSP
 */
DSP_STATUS DSP_PeripheralClocks_Enable(struct WMD_DEV_CONTEXT *pDevContext,
				      IN void *pArgs)
{
	u32 clkIdx;
	DSP_STATUS int_clk_status = DSP_EFAIL, fun_clk_status = DSP_EFAIL;

	for (clkIdx = 0; clkIdx < MBX_PM_MAX_RESOURCES; clkIdx++) {
		if (((pDevContext->uDspPerClks) >> clkIdx) & 0x01) {
			/* Enable the interface clock of the peripheral */
			int_clk_status = CLK_Enable(BPWR_Clks[clkIdx].intClk);
			/* Enable the functional clock of the periphearl */
			fun_clk_status = CLK_Enable(BPWR_Clks[clkIdx].funClk);
		}
	}
	if ((int_clk_status | fun_clk_status) != DSP_SOK)
		return DSP_EFAIL;
	return DSP_SOK;
}

void DSPClkWakeupEventCtrl(u32 ClkId, bool enable)
{
	struct CFG_HOSTRES resources;
	DSP_STATUS status = DSP_SOK;
	u32 iva2_grpsel;
	u32 mpu_grpsel;

	status = CFG_GetHostResources(
		(struct CFG_DEVNODE *)DRV_GetFirstDevExtension(), &resources);
	if (DSP_FAILED(status))
		return;

	switch (ClkId) {
	case BPWR_GPTimer5:
		iva2_grpsel = (u32) *((REG_UWORD32 *)
				((u32) (resources.dwPerPmBase) + 0xA8));
		mpu_grpsel = (u32) *((REG_UWORD32 *)
				((u32) (resources.dwPerPmBase) + 0xA4));
		if (enable) {
			iva2_grpsel |= OMAP3430_GRPSEL_GPT5;
			mpu_grpsel &= ~OMAP3430_GRPSEL_GPT5;
		} else {
			mpu_grpsel |= OMAP3430_GRPSEL_GPT5;
			iva2_grpsel &= ~OMAP3430_GRPSEL_GPT5;
		}
		*((REG_UWORD32 *) ((u32) (resources.dwPerPmBase) + 0xA8))
				= iva2_grpsel;
		*((REG_UWORD32 *) ((u32) (resources.dwPerPmBase) + 0xA4))
				= mpu_grpsel;
	break;
	case BPWR_GPTimer6:
		iva2_grpsel = (u32) *((REG_UWORD32 *)
				((u32) (resources.dwPerPmBase) + 0xA8));
		mpu_grpsel = (u32) *((REG_UWORD32 *)
				((u32) (resources.dwPerPmBase) + 0xA4));
		if (enable) {
			iva2_grpsel |= OMAP3430_GRPSEL_GPT6;
			mpu_grpsel &= ~OMAP3430_GRPSEL_GPT6;
		} else {
			mpu_grpsel |= OMAP3430_GRPSEL_GPT6;
			iva2_grpsel &= ~OMAP3430_GRPSEL_GPT6;
		}
		*((REG_UWORD32 *) ((u32) (resources.dwPerPmBase) + 0xA8))
							= iva2_grpsel;
		*((REG_UWORD32 *) ((u32) (resources.dwPerPmBase) + 0xA4))
							= mpu_grpsel;
	break;
	case BPWR_GPTimer7:
		iva2_grpsel = (u32) *((REG_UWORD32 *)
				((u32) (resources.dwPerPmBase) + 0xA8));
		mpu_grpsel = (u32) *((REG_UWORD32 *)
				((u32) (resources.dwPerPmBase) + 0xA4));
		if (enable) {
			iva2_grpsel |= OMAP3430_GRPSEL_GPT7;
			mpu_grpsel &= ~OMAP3430_GRPSEL_GPT7;
		} else {
			mpu_grpsel |= OMAP3430_GRPSEL_GPT7;
			iva2_grpsel &= ~OMAP3430_GRPSEL_GPT7;
		}
		*((REG_UWORD32 *) ((u32) (resources.dwPerPmBase) + 0xA8))
							= iva2_grpsel;
		*((REG_UWORD32 *) ((u32) (resources.dwPerPmBase) + 0xA4))
							= mpu_grpsel;
	break;
	case BPWR_GPTimer8:
		iva2_grpsel = (u32) *((REG_UWORD32 *)
				((u32) (resources.dwPerPmBase) + 0xA8));
		mpu_grpsel = (u32) *((REG_UWORD32 *)
				((u32) (resources.dwPerPmBase) + 0xA4));
		if (enable) {
			iva2_grpsel |= OMAP3430_GRPSEL_GPT8;
			mpu_grpsel &= ~OMAP3430_GRPSEL_GPT8;
		} else {
			mpu_grpsel |= OMAP3430_GRPSEL_GPT8;
			iva2_grpsel &= ~OMAP3430_GRPSEL_GPT8;
		}
		*((REG_UWORD32 *) ((u32) (resources.dwPerPmBase) + 0xA8))
							= iva2_grpsel;
		*((REG_UWORD32 *) ((u32) (resources.dwPerPmBase) + 0xA4))
							= mpu_grpsel;
	break;
	case BPWR_MCBSP1:
		iva2_grpsel = (u32) *((REG_UWORD32 *)
				((u32) (resources.dwCorePmBase) + 0xA8));
		mpu_grpsel = (u32) *((REG_UWORD32 *)
				((u32) (resources.dwCorePmBase) + 0xA4));
		if (enable) {
			iva2_grpsel |= OMAP3430_GRPSEL_MCBSP1;
			mpu_grpsel &= ~OMAP3430_GRPSEL_MCBSP1;
		} else {
			mpu_grpsel |= OMAP3430_GRPSEL_MCBSP1;
			iva2_grpsel &= ~OMAP3430_GRPSEL_MCBSP1;
		}
		*((REG_UWORD32 *) ((u32) (resources.dwCorePmBase) + 0xA8))
							= iva2_grpsel;
		*((REG_UWORD32 *) ((u32) (resources.dwCorePmBase) + 0xA4))
							= mpu_grpsel;
	break;
	case BPWR_MCBSP2:
		iva2_grpsel = (u32) *((REG_UWORD32 *)
				((u32) (resources.dwPerPmBase) + 0xA8));
		mpu_grpsel = (u32) *((REG_UWORD32 *)
				((u32) (resources.dwPerPmBase) + 0xA4));
		if (enable) {
			iva2_grpsel |= OMAP3430_GRPSEL_MCBSP2;
			mpu_grpsel &= ~OMAP3430_GRPSEL_MCBSP2;
		} else {
			mpu_grpsel |= OMAP3430_GRPSEL_MCBSP2;
			iva2_grpsel &= ~OMAP3430_GRPSEL_MCBSP2;
		}
		*((REG_UWORD32 *) ((u32) (resources.dwPerPmBase) + 0xA8))
							= iva2_grpsel;
		*((REG_UWORD32 *) ((u32) (resources.dwPerPmBase) + 0xA4))
							= mpu_grpsel;
	break;
	case BPWR_MCBSP3:
		iva2_grpsel = (u32) *((REG_UWORD32 *)
				((u32) (resources.dwPerPmBase) + 0xA8));
		mpu_grpsel = (u32) *((REG_UWORD32 *)
				((u32) (resources.dwPerPmBase) + 0xA4));
		if (enable) {
			iva2_grpsel |= OMAP3430_GRPSEL_MCBSP3;
			mpu_grpsel &= ~OMAP3430_GRPSEL_MCBSP3;
		} else {
			mpu_grpsel |= OMAP3430_GRPSEL_MCBSP3;
			iva2_grpsel &= ~OMAP3430_GRPSEL_MCBSP3;
		}
		*((REG_UWORD32 *) ((u32) (resources.dwPerPmBase) + 0xA8))
							= iva2_grpsel;
		*((REG_UWORD32 *) ((u32) (resources.dwPerPmBase) + 0xA4))
							= mpu_grpsel;
	break;
	case BPWR_MCBSP4:
		iva2_grpsel = (u32) *((REG_UWORD32 *)
				((u32) (resources.dwPerPmBase) + 0xA8));
		mpu_grpsel = (u32) *((REG_UWORD32 *)
				((u32) (resources.dwPerPmBase) + 0xA4));
		if (enable) {
			iva2_grpsel |= OMAP3430_GRPSEL_MCBSP4;
			mpu_grpsel &= ~OMAP3430_GRPSEL_MCBSP4;
		} else {
			mpu_grpsel |= OMAP3430_GRPSEL_MCBSP4;
			iva2_grpsel &= ~OMAP3430_GRPSEL_MCBSP4;
		}
		*((REG_UWORD32 *) ((u32) (resources.dwPerPmBase) + 0xA8))
							= iva2_grpsel;
		*((REG_UWORD32 *) ((u32) (resources.dwPerPmBase) + 0xA4))
							= mpu_grpsel;
	break;
	case BPWR_MCBSP5:
		iva2_grpsel = (u32) *((REG_UWORD32 *)
				((u32) (resources.dwCorePmBase) + 0xA8));
		mpu_grpsel = (u32) *((REG_UWORD32 *)
				((u32) (resources.dwCorePmBase) + 0xA4));
		if (enable) {
			iva2_grpsel |= OMAP3430_GRPSEL_MCBSP5;
			mpu_grpsel &= ~OMAP3430_GRPSEL_MCBSP5;
		} else {
			mpu_grpsel |= OMAP3430_GRPSEL_MCBSP5;
			iva2_grpsel &= ~OMAP3430_GRPSEL_MCBSP5;
		}
		*((REG_UWORD32 *) ((u32) (resources.dwCorePmBase) + 0xA8))
							= iva2_grpsel;
		*((REG_UWORD32 *) ((u32) (resources.dwCorePmBase) + 0xA4))
							= mpu_grpsel;
	break;
	}
}
