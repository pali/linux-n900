/*
 * clk.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Provides Clock functions.
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

#ifndef _CLK_H
#define _CLK_H

	/* Generic TIMER object: */
struct timer_object;
enum services_clk_id {
	SERVICESCLK_IVA2_CK = 0,
	SERVICESCLK_GPT5_FCK,
	SERVICESCLK_GPT5_ICK,
	SERVICESCLK_GPT6_FCK,
	SERVICESCLK_GPT6_ICK,
	SERVICESCLK_GPT7_FCK,
	SERVICESCLK_GPT7_ICK,
	SERVICESCLK_GPT8_FCK,
	SERVICESCLK_GPT8_ICK,
	SERVICESCLK_WDT3_FCK,
	SERVICESCLK_WDT3_ICK,
	SERVICESCLK_MCBSP1_FCK,
	SERVICESCLK_MCBSP1_ICK,
	SERVICESCLK_MCBSP2_FCK,
	SERVICESCLK_MCBSP2_ICK,
	SERVICESCLK_MCBSP3_FCK,
	SERVICESCLK_MCBSP3_ICK,
	SERVICESCLK_MCBSP4_FCK,
	SERVICESCLK_MCBSP4_ICK,
	SERVICESCLK_MCBSP5_FCK,
	SERVICESCLK_MCBSP5_ICK,
	SERVICESCLK_SSI_FCK,
	SERVICESCLK_SSI_ICK,
	SERVICESCLK_SYS32K_CK,
	SERVICESCLK_SYS_CK,
	SERVICESCLK_NOT_DEFINED
};

/*
 *  ======== clk_exit ========
 *  Purpose:
 *      Discontinue usage of module; free resources when reference count
 *      reaches 0.
 *  Parameters:
 *  Returns:
 *  Requires:
 *      CLK initialized.
 *  Ensures:
 *      Resources used by module are freed when cRef reaches zero.
 */
extern void clk_exit(void);

/*
 *  ======== services_clk_init ========
 *  Purpose:
 *      Initializes private state of CLK module.
 *  Parameters:
 *  Returns:
 *      TRUE if initialized; FALSE if error occured.
 *  Requires:
 *  Ensures:
 *      CLK initialized.
 */
extern bool services_clk_init(void);

/*
 *  ======== services_clk_enable ========
 *  Purpose:
 *      Enables the clock requested.
 *  Parameters:
 *  Returns:
 *      DSP_SOK:	Success.
 *	DSP_EFAIL:	Error occured while enabling the clock.
 *  Requires:
 *  Ensures:
 */
extern dsp_status services_clk_enable(IN enum services_clk_id clk_id);

/*
 *  ======== services_clk_disable ========
 *  Purpose:
 *      Disables the clock requested.
 *  Parameters:
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EFAIL:      Error occured while disabling the clock.
 *  Requires:
 *  Ensures:
 */
extern dsp_status services_clk_disable(IN enum services_clk_id clk_id);

/*
 *  ======== services_clk_get_rate ========
 *  Purpose:
 *      Get the clock rate of requested clock.
 *  Parameters:
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EFAIL:      Error occured while Getting the clock rate.
 *  Requires:
 *  Ensures:
 */
extern dsp_status services_clk_get_rate(IN enum services_clk_id clk_id,
					u32 *speedMhz);
/*
 *  ======== clk_set32k_hz ========
 *  Purpose:
 *      Set the requested clock to 32KHz.
 *  Parameters:
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EFAIL:      Error occured while setting the clock parent to 32KHz.
 *  Requires:
 *  Ensures:
 */
extern dsp_status clk_set32k_hz(IN enum services_clk_id clk_id);
extern void ssi_clk_prepare(bool FLAG);

/*
 *  ======== CLK_Get_RefCnt ========
 *  Purpose:
 *      get the reference count for the clock.
 *  Parameters:
 *  Returns:
 *      s32:        Reference Count for the clock.
 *      DSP_EFAIL:  Error occured while getting the reference count of a clock.
 *  Requires:
 *  Ensures:
 */
extern s32 clk_get_use_cnt(IN enum services_clk_id clk_id);

#endif /* _SYNC_H */
