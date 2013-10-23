/*
 * hw_prcm.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * PRCM types and API declarations
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

#ifndef _HW_PRCM_H
#define _HW_PRCM_H

/* HW_ClkModule:  Enumerated Type used to specify the clock domain */

enum hw_clk_module_t {
/* DSP Domain */
	HW_CLK_DSP_CPU,
	HW_CLK_DSP_IPI_MMU,
	HW_CLK_IVA_ARM,
	HW_CLK_IVA_COP,		/* IVA Coprocessor */

/* Core Domain */
	HW_CLK_FN_WDT4,		/* Functional Clock */
	HW_CLK_FN_WDT3,
	HW_CLK_FN_UART2,
	HW_CLK_FN_UART1,
	HW_CLK_GPT5,
	HW_CLK_GPT6,
	HW_CLK_GPT7,
	HW_CLK_GPT8,

	HW_CLK_IF_WDT4,		/* Interface Clock */
	HW_CLK_IF_WDT3,
	HW_CLK_IF_UART2,
	HW_CLK_IF_UART1,
	HW_CLK_IF_MBOX
};

enum hw_clk_subsys_t {
	HW_CLK_DSPSS,
	HW_CLK_IVASS
};

/* HW_GPtimers:  General purpose timers */
enum hw_g_ptimer_t {
	HW_GPT5 = 5,
	HW_GPT6 = 6,
	HW_GPT7 = 7,
	HW_GPT8 = 8
};

/* GP timers Input clock type:  General purpose timers */
enum hw_clocktype_t {
	HW_CLK32K_HZ = 0,
	HW_CLK_SYS = 1,
	HW_CLK_EXT = 2
};

/* HW_ClkDiv:  Clock divisors */
enum hw_clk_div_t {
	HW_CLK_DIV1 = 0x1,
	HW_CLK_DIV2 = 0x2,
	HW_CLK_DIV3 = 0x3,
	HW_CLK_DIV4 = 0x4,
	HW_CLK_DIV6 = 0x6,
	HW_CLK_DIV8 = 0x8,
	HW_CLK_DIV12 = 0xC
};

/* HW_RstModule:  Enumerated Type used to specify the module to be reset */
enum hw_rst_module_t {
	HW_RST1_IVA2,		/* Reset the DSP */
	HW_RST2_IVA2,		/* Reset MMU and LEON HWa */
	HW_RST3_IVA2		/* Reset LEON sequencer */
};

/* HW_PwrModule:  Enumerated Type used to specify the power domain */
enum hw_pwr_module_t {
/* Domains */
	HW_PWR_DOMAIN_CORE,
	HW_PWR_DOMAIN_MPU,
	HW_PWR_DOMAIN_WAKEUP,
	HW_PWR_DOMAIN_DSP,

/* Sub-domains */
	HW_PWR_DSP_IPI,		/* IPI = Intrusive Port Interface */
	HW_PWR_IVA_ISP		/* ISP = Intrusive Slave Port */
};

enum hw_pwr_state_t {
	HW_PWR_STATE_OFF,
	HW_PWR_STATE_RET,
	HW_PWR_STATE_INACT,
	HW_PWR_STATE_ON = 3
};

enum hw_force_state_t {
	HW_FORCE_OFF,
	HW_FORCE_ON
};

enum hw_idle_state_t {
	HW_ACTIVE,
	HW_STANDBY
};

enum hw_transition_state_t {
	HW_AUTOTRANS_DIS,
	HW_SW_SUP_SLEEP,
	HW_SW_SUP_WAKEUP,
	HW_AUTOTRANS_EN
};

extern hw_status hw_rst_reset(const void __iomem *baseAddress,
			      enum hw_rst_module_t r);

extern hw_status hw_rst_un_reset(const void __iomem *baseAddress,
				 enum hw_rst_module_t r);

extern hw_status hw_rstctrl_reg_get(const void __iomem *baseAddress,
				    enum hw_rst_module_t p, u32 *value);
extern hw_status hw_rstst_reg_get(const void __iomem *baseAddress,
				  enum hw_rst_module_t p, u32 *value);

extern hw_status hw_pwr_power_state_set(const u32 baseAddress,
					enum hw_pwr_module_t p,
					enum hw_pwr_state_t value);

extern hw_status hw_clk_set_input_clock(const u32 baseAddress,
					enum hw_g_ptimer_t gpt,
					enum hw_clocktype_t c);

extern hw_status hw_pwr_iva2_state_get(const void __iomem *baseAddress,
				       enum hw_pwr_module_t p,
				       enum hw_pwr_state_t *value);

extern hw_status hw_pwrst_iva2_reg_get(const void __iomem *baseAddress,
				       u32 *value);

extern hw_status hw_pwr_iva2_power_state_set(const void __iomem *baseAddress,
					     enum hw_pwr_module_t p,
					     enum hw_pwr_state_t value);

extern hw_status hw_pwr_clkctrl_iva2_reg_set(const void __iomem *baseAddress,
					     enum hw_transition_state_t val);

#endif /* _HW_PRCM_H */
