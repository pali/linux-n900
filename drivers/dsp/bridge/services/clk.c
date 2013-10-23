/*
 * clk.c
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
 *  ======== clk.c ========
 *  Purpose:
 *      Clock and Timer services.
 *
 *  Public Functions:
 *      CLK_Exit
 *      CLK_Init
 *	CLK_Enable
 *	CLK_Disable
 *	CLK_GetRate
 *	CLK_Set_32KHz
 *! Revision History:
 *! ================
 *! 08-May-2007 rg: moved all clock functions from sync module.
 *		    And added CLK_Set_32KHz, CLK_Set_SysClk.
 */

/*  ----------------------------------- Host OS */
#include <dspbridge/host_os.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>
#include <dspbridge/dbdefs.h>
#include <dspbridge/errbase.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>
#include <dspbridge/gt.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/csl.h>
#include <dspbridge/mem.h>

/*  ----------------------------------- This */
#include <dspbridge/clk.h>
#include <dspbridge/util.h>


/*  ----------------------------------- Defines, Data Structures, Typedefs */

typedef volatile unsigned long  REG_UWORD32;

#define SSI_Base        0x48058000

#define SSI_BASE                     IO_ADDRESS(SSI_Base)


struct SERVICES_Clk_t {
	struct clk *clk_handle;
	const char *clk_name;
	int id;
};

/* The row order of the below array needs to match with the clock enumerations
 * 'SERVICES_ClkId' provided in the header file.. any changes in the
 * enumerations needs to be fixed in the array as well */
static struct SERVICES_Clk_t SERVICES_Clks[] = {
	{NULL, "iva2_ck", -1},
	{NULL, "mailboxes_ick", -1},
	{NULL, "gpt5_fck", -1},
	{NULL, "gpt5_ick", -1},
	{NULL, "gpt6_fck", -1},
	{NULL, "gpt6_ick", -1},
	{NULL, "gpt7_fck", -1},
	{NULL, "gpt7_ick", -1},
	{NULL, "gpt8_fck", -1},
	{NULL, "gpt8_ick", -1},
	{NULL, "wdt_fck", 3},
	{NULL, "wdt_ick", 3},
	{NULL, "mcbsp_fck", 1},
	{NULL, "mcbsp_ick", 1},
	{NULL, "mcbsp_fck", 2},
	{NULL, "mcbsp_ick", 2},
	{NULL, "mcbsp_fck", 3},
	{NULL, "mcbsp_ick", 3},
	{NULL, "mcbsp_fck", 4},
	{NULL, "mcbsp_ick", 4},
	{NULL, "mcbsp_fck", 5},
	{NULL, "mcbsp_ick", 5},
	{NULL, "ssi_ssr_sst_fck", -1},
	{NULL, "ssi_ick", -1},
	{NULL, "omap_32k_fck", -1},
	{NULL, "sys_ck", -1},
	{NULL, ""}
};

/* Generic TIMER object: */
struct TIMER_OBJECT {
	struct timer_list timer;
};

/*  ----------------------------------- Globals */
#if GT_TRACE
static struct GT_Mask CLK_debugMask = { NULL, NULL };	/* GT trace variable */
#endif

/*
 *  ======== CLK_Exit ========
 *  Purpose:
 *      Cleanup CLK module.
 */
void CLK_Exit(void)
{
	int i = 0;

	GT_0trace(CLK_debugMask, GT_5CLASS, "CLK_Exit\n");
	/* Relinquish the clock handles */
	while (i < SERVICESCLK_NOT_DEFINED) {
		if (SERVICES_Clks[i].clk_handle)
			clk_put(SERVICES_Clks[i].clk_handle);

		SERVICES_Clks[i].clk_handle = NULL;
		i++;
	}

}

/*
 *  ======== CLK_Init ========
 *  Purpose:
 *      Initialize CLK module.
 */
bool CLK_Init(void)
{
	static struct platform_device dspbridge_device;
	struct clk *clk_handle;
	int i = 0;
	GT_create(&CLK_debugMask, "CK");	/* CK for CLK */
	GT_0trace(CLK_debugMask, GT_5CLASS, "CLK_Init\n");

	dspbridge_device.dev.bus = &platform_bus_type;

	/* Get the clock handles from base port and store locally */
	while (i < SERVICESCLK_NOT_DEFINED) {
		/* get the handle from BP */
		dspbridge_device.id = SERVICES_Clks[i].id;

		clk_handle = clk_get(&dspbridge_device.dev,
			     SERVICES_Clks[i].clk_name);

		if (!clk_handle) {
			GT_2trace(CLK_debugMask, GT_7CLASS,
				  "CLK_Init: failed to get Clk handle %s, "
				  "CLK dev id = %d\n",
				  SERVICES_Clks[i].clk_name,
				  SERVICES_Clks[i].id);
			/* should we fail here?? */
		} else {
			GT_2trace(CLK_debugMask, GT_7CLASS,
				  "CLK_Init: PASS and Clk handle %s, "
				  "CLK dev id = %d\n",
				  SERVICES_Clks[i].clk_name,
				  SERVICES_Clks[i].id);
		}
		SERVICES_Clks[i].clk_handle = clk_handle;
		i++;
	}

	return true;
}

/*
 *  ======== CLK_Enable ========
 *  Purpose:
 *      Enable Clock .
 *
*/
DSP_STATUS CLK_Enable(IN enum SERVICES_ClkId clk_id)
{
	DSP_STATUS status = DSP_SOK;
	struct clk *pClk;

	DBC_Require(clk_id < SERVICESCLK_NOT_DEFINED);
	GT_2trace(CLK_debugMask, GT_6CLASS, "CLK_Enable: CLK %s, "
		"CLK dev id = %d\n", SERVICES_Clks[clk_id].clk_name,
		SERVICES_Clks[clk_id].id);

	pClk = SERVICES_Clks[clk_id].clk_handle;
	if (pClk) {
		if (clk_enable(pClk) == 0x0) {
			/* Success ? */
		} else {
			pr_err("CLK_Enable: failed to Enable CLK %s, "
					"CLK dev id = %d\n",
					SERVICES_Clks[clk_id].clk_name,
					SERVICES_Clks[clk_id].id);
			status = DSP_EFAIL;
		}
	} else {
		pr_err("CLK_Enable: failed to get CLK %s, CLK dev id = %d\n",
					SERVICES_Clks[clk_id].clk_name,
					SERVICES_Clks[clk_id].id);
		status = DSP_EFAIL;
	}
	/* The SSI module need to configured not to have the Forced idle for
	 * master interface. If it is set to forced idle, the SSI module is
	 * transitioning to standby thereby causing the client in the DSP hang
	 * waiting for the SSI module to be active after enabling the clocks
	 */
	if (clk_id == SERVICESCLK_ssi_fck)
		SSI_Clk_Prepare(true);

	return status;
}
/*
 *  ======== CLK_Set_32KHz ========
 *  Purpose:
 *      To Set parent of a clock to 32KHz.
 */

DSP_STATUS CLK_Set_32KHz(IN enum SERVICES_ClkId clk_id)
{
	DSP_STATUS status = DSP_SOK;
	struct clk *pClk;
	struct clk *pClkParent;
	enum SERVICES_ClkId sys_32k_id = SERVICESCLK_sys_32k_ck;
	pClkParent =  SERVICES_Clks[sys_32k_id].clk_handle;

	DBC_Require(clk_id < SERVICESCLK_NOT_DEFINED);
	GT_2trace(CLK_debugMask, GT_6CLASS, "CLK_Set_32KHz: CLK %s, "
		"CLK dev id = %d is setting to 32KHz \n",
		SERVICES_Clks[clk_id].clk_name,
		SERVICES_Clks[clk_id].id);
	pClk = SERVICES_Clks[clk_id].clk_handle;
	if (pClk) {
		if (!(clk_set_parent(pClk, pClkParent) == 0x0)) {
		       GT_2trace(CLK_debugMask, GT_7CLASS, "CLK_Set_32KHz: "
				"Failed to set to 32KHz %s, CLK dev id = %d\n",
				SERVICES_Clks[clk_id].clk_name,
				SERVICES_Clks[clk_id].id);
			status = DSP_EFAIL;
		}
	}
	return status;
}

/*
 *  ======== CLK_Disable ========
 *  Purpose:
 *      Disable the clock.
 *
*/
DSP_STATUS CLK_Disable(IN enum SERVICES_ClkId clk_id)
{
	DSP_STATUS status = DSP_SOK;
	struct clk *pClk;
	s32 clkUseCnt;

	DBC_Require(clk_id < SERVICESCLK_NOT_DEFINED);
	GT_2trace(CLK_debugMask, GT_6CLASS, "CLK_Disable: CLK %s, "
		"CLK dev id = %d\n", SERVICES_Clks[clk_id].clk_name,
		SERVICES_Clks[clk_id].id);

	pClk = SERVICES_Clks[clk_id].clk_handle;

	clkUseCnt = CLK_Get_UseCnt(clk_id);
	if (clkUseCnt == -1) {
		pr_err("CLK_Disable: failed to get CLK Use count for CLK %s,"
				"CLK dev id = %d\n",
				SERVICES_Clks[clk_id].clk_name,
				SERVICES_Clks[clk_id].id);
	} else if (clkUseCnt == 0) {
		GT_2trace(CLK_debugMask, GT_4CLASS, "CLK_Disable: CLK %s,"
				"CLK dev id= %d is already disabled\n",
				SERVICES_Clks[clk_id].clk_name,
				SERVICES_Clks[clk_id].id);
		return status;
	}
	if (clk_id == SERVICESCLK_ssi_ick)
		SSI_Clk_Prepare(false);

		if (pClk) {
			clk_disable(pClk);
		} else {
			pr_err("CLK_Disable: failed to get CLK %s,"
					"CLK dev id = %d\n",
					SERVICES_Clks[clk_id].clk_name,
					SERVICES_Clks[clk_id].id);
			status = DSP_EFAIL;
		}
	return status;
}

/*
 *  ======== CLK_GetRate ========
 *  Purpose:
 *      GetClock Speed.
 *
 */

DSP_STATUS CLK_GetRate(IN enum SERVICES_ClkId clk_id, u32 *speedKhz)
{
	DSP_STATUS status = DSP_SOK;
	struct clk *pClk;
	u32 clkSpeedHz;

	DBC_Require(clk_id < SERVICESCLK_NOT_DEFINED);
	*speedKhz = 0x0;

	GT_2trace(CLK_debugMask, GT_7CLASS, "CLK_GetRate: CLK %s, "
		"CLK dev Id = %d \n", SERVICES_Clks[clk_id].clk_name,
		SERVICES_Clks[clk_id].id);
	pClk = SERVICES_Clks[clk_id].clk_handle;
	if (pClk) {
		clkSpeedHz = clk_get_rate(pClk);
		*speedKhz = clkSpeedHz / 1000;
		GT_2trace(CLK_debugMask, GT_6CLASS,
			  "CLK_GetRate: clkSpeedHz = %d , "
			 "speedinKhz=%d\n", clkSpeedHz, *speedKhz);
	} else {
		GT_2trace(CLK_debugMask, GT_7CLASS,
			 "CLK_GetRate: failed to get CLK %s, "
			 "CLK dev Id = %d\n", SERVICES_Clks[clk_id].clk_name,
			 SERVICES_Clks[clk_id].id);
		status = DSP_EFAIL;
	}
	return status;
}

s32 CLK_Get_UseCnt(IN enum SERVICES_ClkId clk_id)
{
	DSP_STATUS status = DSP_SOK;
	struct clk *pClk;
	s32 useCount = -1;
	DBC_Require(clk_id < SERVICESCLK_NOT_DEFINED);

	pClk = SERVICES_Clks[clk_id].clk_handle;

	if (pClk) {
		useCount =  pClk->usecount; /* FIXME: usecount shouldn't be used */
	} else {
		GT_2trace(CLK_debugMask, GT_7CLASS,
			 "CLK_GetRate: failed to get CLK %s, "
			 "CLK dev Id = %d\n", SERVICES_Clks[clk_id].clk_name,
			 SERVICES_Clks[clk_id].id);
		status = DSP_EFAIL;
	}
	return useCount;
}

void SSI_Clk_Prepare(bool FLAG)
{
	u32 ssi_sysconfig;
	ssi_sysconfig = __raw_readl((SSI_BASE) + 0x10);

	if (FLAG) {
		/* Set Autoidle, SIDLEMode to smart idle, and MIDLEmode to
		 * no idle
		 */
		ssi_sysconfig = 0x1011;
	} else {
		/* Set Autoidle, SIDLEMode to forced idle, and MIDLEmode to
		 * forced idle
		 */
		ssi_sysconfig = 0x1;
	}
	__raw_writel((u32)ssi_sysconfig, SSI_BASE + 0x10);
}
