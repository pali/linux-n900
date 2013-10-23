/*
 * clk.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Clock and Timer services.
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

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>
#include <dspbridge/dbdefs.h>
#include <dspbridge/errbase.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/mem.h>

/*  ----------------------------------- This */
#include <dspbridge/clk.h>

/*  ----------------------------------- Defines, Data Structures, Typedefs */

typedef volatile unsigned long reg_uword32;

#define OMAP_SSI_OFFSET			0x58000
#define OMAP_SSI_SIZE			0x1000
#define OMAP_SSI_SYSCONFIG_OFFSET	0x10

#define SSI_AUTOIDLE			(1 << 0)
#define SSI_SIDLE_SMARTIDLE		(2 << 3)
#define SSI_MIDLE_NOIDLE		(1 << 12)

struct services_clk_t {
	struct clk *clk_handle;
	const char *clk_name;
	int id;
};

/* The row order of the below array needs to match with the clock enumerations
 * 'services_clk_id' provided in the header file.. any changes in the
 * enumerations needs to be fixed in the array as well */
static struct services_clk_t services_clks[] = {
	{NULL, "iva2_ck", -1},
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
struct timer_object {
	struct timer_list timer;
};

/*
 *  ======== clk_exit ========
 *  Purpose:
 *      Cleanup CLK module.
 */
void clk_exit(void)
{
	int i = 0;

	/* Relinquish the clock handles */
	while (i < SERVICESCLK_NOT_DEFINED) {
		if (services_clks[i].clk_handle)
			clk_put(services_clks[i].clk_handle);

		services_clks[i].clk_handle = NULL;
		i++;
	}

}

/*
 *  ======== services_clk_init ========
 *  Purpose:
 *      Initialize CLK module.
 */
bool services_clk_init(void)
{
	static struct platform_device dspbridge_device;
	struct clk *clk_handle;
	int i = 0;

	dspbridge_device.dev.bus = &platform_bus_type;

	/* Get the clock handles from base port and store locally */
	while (i < SERVICESCLK_NOT_DEFINED) {
		/* get the handle from BP */
		dspbridge_device.id = services_clks[i].id;

		clk_handle = clk_get(&dspbridge_device.dev,
				     services_clks[i].clk_name);

		if (!clk_handle) {
			pr_err("%s: failed to get clk handle %s, dev id = %d\n",
			       __func__, services_clks[i].clk_name,
			       services_clks[i].id);
			/* should we fail here?? */
		}
		services_clks[i].clk_handle = clk_handle;
		i++;
	}

	return true;
}

/*
 *  ======== services_clk_enable ========
 *  Purpose:
 *      Enable Clock .
 *
 */
dsp_status services_clk_enable(IN enum services_clk_id clk_id)
{
	dsp_status status = DSP_SOK;
	struct clk *clk_handle;

	DBC_REQUIRE(clk_id < SERVICESCLK_NOT_DEFINED);

	clk_handle = services_clks[clk_id].clk_handle;
	if (clk_handle) {
		if (clk_enable(clk_handle)) {
			pr_err("services_clk_enable: failed to Enable CLK %s, "
			       "CLK dev id = %d\n",
			       services_clks[clk_id].clk_name,
			       services_clks[clk_id].id);
			status = DSP_EFAIL;
		}
	} else {
		pr_err("%s: failed to get CLK %s, CLK dev id = %d\n", __func__,
		     services_clks[clk_id].clk_name, services_clks[clk_id].id);
		status = DSP_EFAIL;
	}
	/* The SSI module need to configured not to have the Forced idle for
	 * master interface. If it is set to forced idle, the SSI module is
	 * transitioning to standby thereby causing the client in the DSP hang
	 * waiting for the SSI module to be active after enabling the clocks
	 */
	if (clk_id == SERVICESCLK_SSI_FCK)
		ssi_clk_prepare(true);

	return status;
}

/*
 *  ======== clk_set32k_hz ========
 *  Purpose:
 *      To Set parent of a clock to 32KHz.
 */

dsp_status clk_set32k_hz(IN enum services_clk_id clk_id)
{
	dsp_status status = DSP_SOK;
	struct clk *clk_handle;
	struct clk *clk_parent;
	clk_parent = services_clks[SERVICESCLK_SYS32K_CK].clk_handle;

	DBC_REQUIRE(clk_id < SERVICESCLK_NOT_DEFINED);

	clk_handle = services_clks[clk_id].clk_handle;
	if (clk_handle) {
		if (!(clk_set_parent(clk_handle, clk_parent) == 0x0)) {
			pr_err("%s: failed for %s, dev id = %d\n", __func__,
			       services_clks[clk_id].clk_name,
			       services_clks[clk_id].id);
			status = DSP_EFAIL;
		}
	}
	return status;
}

/*
 *  ======== services_clk_disable ========
 *  Purpose:
 *      Disable the clock.
 *
 */
dsp_status services_clk_disable(IN enum services_clk_id clk_id)
{
	dsp_status status = DSP_SOK;
	struct clk *clk_handle;
	s32 clk_use_cnt;

	DBC_REQUIRE(clk_id < SERVICESCLK_NOT_DEFINED);

	clk_handle = services_clks[clk_id].clk_handle;

	clk_use_cnt = clk_get_use_cnt(clk_id);
	if (clk_use_cnt == -1) {
		pr_err("%s: failed to get CLK Use count for CLK %s, CLK dev id"
		       " = %d\n", __func__, services_clks[clk_id].clk_name,
		       services_clks[clk_id].id);
	} else if (clk_use_cnt == 0) {
		return status;
	}
	if (clk_id == SERVICESCLK_SSI_ICK)
		ssi_clk_prepare(false);

	if (clk_handle) {
		clk_disable(clk_handle);
	} else {
		pr_err("services_clk_disable: failed to get CLK %s,"
		       "CLK dev id = %d\n",
		       services_clks[clk_id].clk_name,
		       services_clks[clk_id].id);
		status = DSP_EFAIL;
	}
	return status;
}

/*
 *  ======== services_clk_get_rate ========
 *  Purpose:
 *      GetClock Speed.
 *
 */

dsp_status services_clk_get_rate(IN enum services_clk_id clk_id, u32 *speedKhz)
{
	dsp_status status = DSP_SOK;
	struct clk *clk_handle;
	u32 clk_speed_hz;

	DBC_REQUIRE(clk_id < SERVICESCLK_NOT_DEFINED);
	*speedKhz = 0x0;

	clk_handle = services_clks[clk_id].clk_handle;
	if (clk_handle) {
		clk_speed_hz = clk_get_rate(clk_handle);
		*speedKhz = clk_speed_hz / 1000;
		dev_dbg(bridge, "%s: clk_speed_hz = %d, speedinKhz = %d\n",
			__func__, clk_speed_hz, *speedKhz);
	} else {
		pr_err("%s: failed to get %s, dev Id = %d\n", __func__,
		       services_clks[clk_id].clk_name,
		       services_clks[clk_id].id);
		status = DSP_EFAIL;
	}
	return status;
}

s32 clk_get_use_cnt(IN enum services_clk_id clk_id)
{
	dsp_status status = DSP_SOK;
	struct clk *clk_handle;
	s32 use_count = -1;
	DBC_REQUIRE(clk_id < SERVICESCLK_NOT_DEFINED);

	clk_handle = services_clks[clk_id].clk_handle;

	if (clk_handle) {
		/* FIXME: usecount shouldn't be used */
		use_count = clk_handle->usecount;
	} else {
		pr_err("%s: failed to get %s, dev Id = %d\n", __func__,
		       services_clks[clk_id].clk_name,
		       services_clks[clk_id].id);
		status = DSP_EFAIL;
	}
	return use_count;
}

void ssi_clk_prepare(bool FLAG)
{
	void __iomem *ssi_base;
	unsigned int value;

	ssi_base = ioremap(L4_34XX_BASE + OMAP_SSI_OFFSET, OMAP_SSI_SIZE);
	if (!ssi_base) {
		pr_err("%s: error, SSI not configured\n", __func__);
		return;
	}

	if (FLAG) {
		/* Set Autoidle, SIDLEMode to smart idle, and MIDLEmode to
		 * no idle
		 */
		value = SSI_AUTOIDLE | SSI_SIDLE_SMARTIDLE | SSI_MIDLE_NOIDLE;
	} else {
		/* Set Autoidle, SIDLEMode to forced idle, and MIDLEmode to
		 * forced idle
		 */
		value = SSI_AUTOIDLE;
	}

	__raw_writel(value, ssi_base + OMAP_SSI_SYSCONFIG_OFFSET);
	iounmap(ssi_base);
}
