/*
 * hw_prcm.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * API definitions to configure PRCM (Power, Reset & Clocks Manager)
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

#include <GlobalTypes.h>
#include "PRCMRegAcM.h"
#include <hw_defs.h>
#include <hw_prcm.h>

static hw_status hw_rst_write_val(const void __iomem *baseAddress,
				  enum hw_rst_module_t r, s8 val);

hw_status hw_rst_reset(const void __iomem *baseAddress, enum hw_rst_module_t r)
{
	return hw_rst_write_val(baseAddress, r, HW_SET);
}

hw_status hw_rst_un_reset(const void __iomem *baseAddress,
			  enum hw_rst_module_t r)
{
	return hw_rst_write_val(baseAddress, r, HW_CLEAR);
}

static hw_status hw_rst_write_val(const void __iomem *baseAddress,
				  enum hw_rst_module_t r, s8 val)
{
	hw_status status = RET_OK;

	switch (r) {
	case HW_RST1_IVA2:
		PRM_RSTCTRL_IVA2RST1_DSP_WRITE32(baseAddress, val);
		break;
	case HW_RST2_IVA2:
		PRM_RSTCTRL_IVA2RST2_DSP_WRITE32(baseAddress, val);
		break;
	case HW_RST3_IVA2:
		PRM_RSTCTRL_IVA2RST3_DSP_WRITE32(baseAddress, val);
		break;
	default:
		status = RET_FAIL;
		break;
	}
	return status;
}

hw_status hw_pwr_iva2_state_get(const void __iomem *baseAddress,
				enum hw_pwr_module_t p,
				enum hw_pwr_state_t *value)
{
	hw_status status = RET_OK;
	u32 temp;

	switch (p) {
	case HW_PWR_DOMAIN_DSP:
		/* wait until Transition is complete */
		do {
			/* mdelay(1); */
			temp = PRCMPM_PWSTST_IVA2_IN_TRANSITION_READ32
			    (baseAddress);

		} while (temp);

		temp = PRCMPM_PWSTST_IVA2_READ_REGISTER32(baseAddress);
		*value = PRCMPM_PWSTST_IVA2_POWER_STATE_ST_GET32(temp);
		break;

	default:
		status = RET_FAIL;
		break;
	}
	return status;
}

hw_status hw_pwrst_iva2_reg_get(const void __iomem *baseAddress, u32 *value)
{
	hw_status status = RET_OK;

	*value = PRCMPM_PWSTST_IVA2_READ_REGISTER32(baseAddress);

	return status;
}

hw_status hw_pwr_iva2_power_state_set(const void __iomem *baseAddress,
				      enum hw_pwr_module_t p,
				      enum hw_pwr_state_t value)
{
	hw_status status = RET_OK;

	switch (p) {
	case HW_PWR_DOMAIN_DSP:
		switch (value) {
		case HW_PWR_STATE_ON:
			PRCMPM_PWSTCTRL_IVA2_POWER_STATE_WRITE_ON32
			    (baseAddress);
			break;
		case HW_PWR_STATE_RET:
			PRCMPM_PWSTCTRL_DSP_POWER_STATE_WRITE_RET32
			    (baseAddress);
			break;
		case HW_PWR_STATE_OFF:
			PRCMPM_PWSTCTRL_IVA2_POWER_STATE_WRITE_OFF32
			    (baseAddress);
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

hw_status hw_pwr_clkctrl_iva2_reg_set(const void __iomem *baseAddress,
				      enum hw_transition_state_t val)
{
	hw_status status = RET_OK;

	PRCMCM_CLKSTCTRL_IVA2_WRITE_REGISTER32(baseAddress, val);

	return status;

}

hw_status hw_rstst_reg_get(const void __iomem *baseAddress,
			   enum hw_rst_module_t m, u32 *value)
{
	hw_status status = RET_OK;

	*value = PRCMRM_RSTST_DSP_READ_REGISTER32(baseAddress);

	return status;
}

hw_status hw_rstctrl_reg_get(const void __iomem *baseAddress,
			     enum hw_rst_module_t m, u32 *value)
{
	hw_status status = RET_OK;

	*value = PRCMRM_RSTCTRL_DSP_READ_REGISTER32(baseAddress);

	return status;
}
