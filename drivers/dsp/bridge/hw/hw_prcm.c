/*
 * hw_prcm.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Copyright (C) 2007 Texas Instruments, Inc.
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
 *  ======== hw_prcm.c ========
 *  Description:
 *      API definitions to configure PRCM (Power, Reset & Clocks Manager)
 *
 *! Revision History:
 *! ================
 *! 16 Feb 2003 sb: Initial version
 */

#include <GlobalTypes.h>
#include "PRCMRegAcM.h"
#include <hw_defs.h>
#include <hw_prcm.h>

static HW_STATUS HW_RST_WriteVal(const void __iomem *baseAddress,
				    enum HW_RstModule_t r,
				    enum HW_SetClear_t val);

HW_STATUS HW_RST_Reset(const void __iomem *baseAddress, enum HW_RstModule_t r)
{
	return HW_RST_WriteVal(baseAddress, r, HW_SET);
}

HW_STATUS HW_RST_UnReset(const void __iomem *baseAddress, enum HW_RstModule_t r)
{
	return HW_RST_WriteVal(baseAddress, r, HW_CLEAR);
}

static HW_STATUS HW_RST_WriteVal(const void __iomem *baseAddress,
				    enum HW_RstModule_t r,
				    enum HW_SetClear_t val)
{
	HW_STATUS status = RET_OK;

	switch (r) {
	case HW_RST1_IVA2:
	    PRM_RSTCTRL_IVA2RST1_DSPWrite32(baseAddress, val);
	    break;
	case HW_RST2_IVA2:
	    PRM_RSTCTRL_IVA2RST2_DSPWrite32(baseAddress, val);
	    break;
	case HW_RST3_IVA2:
	    PRM_RSTCTRL_IVA2RST3_DSPWrite32(baseAddress, val);
	    break;
	default:
	    status = RET_FAIL;
	    break;
	}
	return status;
}

HW_STATUS HW_PWR_IVA2StateGet(const void __iomem *baseAddress,
		enum HW_PwrModule_t p, enum HW_PwrState_t *value)
{
	HW_STATUS status = RET_OK;
	u32 temp;

	switch (p) {
	case HW_PWR_DOMAIN_DSP:
		/* wait until Transition is complete */
		do {
			/* mdelay(1); */
			temp = PRCMPM_PWSTST_IVA2InTransitionRead32
				(baseAddress);

		} while (temp);

		temp = PRCMPM_PWSTST_IVA2ReadRegister32(baseAddress);
		*value = PRCMPM_PWSTST_IVA2PowerStateStGet32(temp);
		break;

	default:
		status = RET_FAIL;
		break;
	}
	return status;
}

HW_STATUS HW_PWRST_IVA2RegGet(const void __iomem *baseAddress, u32 *value)
{
	HW_STATUS status = RET_OK;

	*value = PRCMPM_PWSTST_IVA2ReadRegister32(baseAddress);

	return status;
}


HW_STATUS HW_PWR_IVA2PowerStateSet(const void __iomem *baseAddress,
				     enum HW_PwrModule_t p,
				     enum HW_PwrState_t value)
{
	HW_STATUS status = RET_OK;

	switch (p) {
	case HW_PWR_DOMAIN_DSP:
		switch (value) {
		case HW_PWR_STATE_ON:
			PRCMPM_PWSTCTRL_IVA2PowerStateWriteON32(baseAddress);
			break;
		case HW_PWR_STATE_RET:
			PRCMPM_PWSTCTRL_DSPPowerStateWriteRET32(baseAddress);
			break;
		case HW_PWR_STATE_OFF:
			PRCMPM_PWSTCTRL_IVA2PowerStateWriteOFF32(baseAddress);
			break;
		default:
			status = RET_FAIL;
			break;
		}
		break;

	default:
		status = RET_FAIL;
		break;
	}

	return status;
}

HW_STATUS HW_PWR_CLKCTRL_IVA2RegSet(const void __iomem *baseAddress,
				      enum HW_TransitionState_t val)
{
	HW_STATUS status = RET_OK;

	PRCMCM_CLKSTCTRL_IVA2WriteRegister32(baseAddress, val);

	return status;

}

HW_STATUS HW_RSTST_RegGet(const void __iomem *baseAddress,
		enum HW_RstModule_t m, u32 *value)
{
	HW_STATUS status = RET_OK;

	*value = PRCMRM_RSTST_DSPReadRegister32(baseAddress);

	return status;
}

HW_STATUS HW_RSTCTRL_RegGet(const void __iomem *baseAddress,
		enum HW_RstModule_t m, u32 *value)
{
	HW_STATUS status = RET_OK;

	*value = PRCMRM_RSTCTRL_DSPReadRegister32(baseAddress);

	return status;
}
