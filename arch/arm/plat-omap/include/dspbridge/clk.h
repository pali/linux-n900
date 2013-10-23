/*
 * clk.h
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
 *  ======== clk.h ========
 *  Purpose: Provides Clock functions.
 *
 *! Revision History:
 *! ================
 *! 08-May-2007 rg: Moved all clock functions from sync module.
 */

#ifndef _CLK_H
#define _CLK_H

	/* Generic TIMER object: */
	struct TIMER_OBJECT;
	enum SERVICES_ClkId {
		SERVICESCLK_iva2_ck = 0,
		SERVICESCLK_mailbox_ick,
		SERVICESCLK_gpt5_fck,
		SERVICESCLK_gpt5_ick,
		SERVICESCLK_gpt6_fck,
		SERVICESCLK_gpt6_ick,
		SERVICESCLK_gpt7_fck,
		SERVICESCLK_gpt7_ick,
		SERVICESCLK_gpt8_fck,
		SERVICESCLK_gpt8_ick,
		SERVICESCLK_wdt3_fck,
		SERVICESCLK_wdt3_ick,
		SERVICESCLK_mcbsp1_fck,
		SERVICESCLK_mcbsp1_ick,
		SERVICESCLK_mcbsp2_fck,
		SERVICESCLK_mcbsp2_ick,
		SERVICESCLK_mcbsp3_fck,
		SERVICESCLK_mcbsp3_ick,
		SERVICESCLK_mcbsp4_fck,
		SERVICESCLK_mcbsp4_ick,
		SERVICESCLK_mcbsp5_fck,
		SERVICESCLK_mcbsp5_ick,
		SERVICESCLK_ssi_fck,
		SERVICESCLK_ssi_ick,
		SERVICESCLK_sys_32k_ck,
		SERVICESCLK_sys_ck,
		SERVICESCLK_NOT_DEFINED
	} ;

/*
 *  ======== CLK_Exit ========
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
	extern void CLK_Exit(void);

/*
 *  ======== CLK_Init ========
 *  Purpose:
 *      Initializes private state of CLK module.
 *  Parameters:
 *  Returns:
 *      TRUE if initialized; FALSE if error occured.
 *  Requires:
 *  Ensures:
 *      CLK initialized.
 */
	extern bool CLK_Init(void);


/*
 *  ======== CLK_Enable ========
 *  Purpose:
 *      Enables the clock requested.
 *  Parameters:
 *  Returns:
 *      DSP_SOK:	Success.
 *	DSP_EFAIL:	Error occured while enabling the clock.
 *  Requires:
 *  Ensures:
 */
	extern DSP_STATUS CLK_Enable(IN enum SERVICES_ClkId clk_id);

/*
 *  ======== CLK_Disable ========
 *  Purpose:
 *      Disables the clock requested.
 *  Parameters:
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EFAIL:      Error occured while disabling the clock.
 *  Requires:
 *  Ensures:
 */
	extern DSP_STATUS CLK_Disable(IN enum SERVICES_ClkId clk_id);

/*
 *  ======== CLK_GetRate ========
 *  Purpose:
 *      Get the clock rate of requested clock.
 *  Parameters:
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EFAIL:      Error occured while Getting the clock rate.
 *  Requires:
 *  Ensures:
 */
	extern DSP_STATUS CLK_GetRate(IN enum SERVICES_ClkId clk_id,
				     u32 *speedMhz);
/*
 *  ======== CLK_Set_32KHz ========
 *  Purpose:
 *      Set the requested clock to 32KHz.
 *  Parameters:
 *  Returns:
 *      DSP_SOK:        Success.
 *      DSP_EFAIL:      Error occured while setting the clock parent to 32KHz.
 *  Requires:
 *  Ensures:
 */
	extern DSP_STATUS CLK_Set_32KHz(IN enum SERVICES_ClkId clk_id);
	extern void SSI_Clk_Prepare(bool FLAG);

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
	extern s32 CLK_Get_UseCnt(IN enum SERVICES_ClkId clk_id);

#endif				/* _SYNC_H */
