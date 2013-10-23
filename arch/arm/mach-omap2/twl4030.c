/*
 * Copyright (C) 2010 Texas Instruments, Inc.
 * Lesly A M <x0080970@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifdef CONFIG_TWL4030_POWER

#include <linux/i2c/twl.h>

#include "twl4030.h"
#include "smartreflex.h"

static struct prm_setup_vc twl4030_voltsetup_time = {
	/* VOLT SETUPTIME for RET */
	.ret = {
		.voltsetup1_vdd1 = 0x005B,
		.voltsetup1_vdd2 = 0x0055,
		.voltsetup2 = 0x0,
		.voltoffset = 0x0,
	},
	/* VOLT SETUPTIME for OFF */
	.off = {
		.voltsetup1_vdd1 = 0x00B3,
		.voltsetup1_vdd2 = 0x00A0,
		.voltsetup2 = 0x118,
		.voltoffset = 0x32,
	},
};

/*
 * Sequence to control the TRITON Power resources,
 * when the system goes into sleep.
 * Executed upon P1_P2/P3 transition for sleep.
 */
static struct twl4030_ins __initdata sleep_on_seq[] = {
	/* Broadcast message to put res to sleep */
	{MSG_BROADCAST(DEV_GRP_NULL, RES_GRP_ALL, 1, 0, RES_STATE_SLEEP), 2},
};

static struct twl4030_script sleep_on_script __initdata = {
	.script	= sleep_on_seq,
	.size	= ARRAY_SIZE(sleep_on_seq),
	.flags	= TWL4030_SLEEP_SCRIPT,
};

/*
 * Sequence to control the TRITON Power resources,
 * when the system wakeup from sleep.
 * Executed upon P1_P2 transition for wakeup.
 */
static struct twl4030_ins wakeup_p12_seq[] __initdata = {
	/* Broadcast message to put res to active */
	{MSG_BROADCAST(DEV_GRP_NULL, RES_GRP_ALL, 1, 0, RES_STATE_ACTIVE), 2},
};

static struct twl4030_script wakeup_p12_script __initdata = {
	.script	= wakeup_p12_seq,
	.size	= ARRAY_SIZE(wakeup_p12_seq),
	.flags	= TWL4030_WAKEUP12_SCRIPT,
};

/*
 * Sequence to control the TRITON Power resources,
 * when the system wakeup from sleep.
 * Executed upon P3 transition for wakeup.
 */
static struct twl4030_ins wakeup_p3_seq[] __initdata = {
	/* Broadcast message to put res to active */
	{MSG_BROADCAST(DEV_GRP_NULL, RES_GRP_ALL, 1, 0, RES_STATE_ACTIVE), 2},
};

static struct twl4030_script wakeup_p3_script __initdata = {
	.script = wakeup_p3_seq,
	.size   = ARRAY_SIZE(wakeup_p3_seq),
	.flags  = TWL4030_WAKEUP3_SCRIPT,
};

/*
 * Sequence to reset the TRITON Power resources,
 * when the system gets warm reset.
 * Executed upon warm reset signal.
 */
static struct twl4030_ins wrst_seq[] __initdata = {
	{MSG_SINGULAR(DEV_GRP_NULL, RES_RESET, RES_STATE_OFF), 2},
	{MSG_BROADCAST(DEV_GRP_NULL, RES_GRP_ALL, 0, 2, RES_STATE_WRST), 1},
	{MSG_BROADCAST(DEV_GRP_NULL, RES_GRP_ALL, 0, 1, RES_STATE_WRST), 1},
	{MSG_SINGULAR(DEV_GRP_NULL, RES_RESET, RES_STATE_ACTIVE), 2},
};

static struct twl4030_script wrst_script __initdata = {
	.script = wrst_seq,
	.size   = ARRAY_SIZE(wrst_seq),
	.flags  = TWL4030_WRST_SCRIPT,
};

/* TRITON script for sleep, wakeup & warm_reset */
static struct twl4030_script *twl4030_scripts[] __initdata = {
	&sleep_on_script,
	&wakeup_p12_script,
	&wakeup_p3_script,
	&wrst_script,
};

static struct twl4030_power_data twl4030_generic_script __initdata = {
	.scripts	= twl4030_scripts,
	.num		= ARRAY_SIZE(twl4030_scripts),
};

void twl4030_get_scripts(struct twl4030_power_data *t2scripts_data)
{
	t2scripts_data->scripts = twl4030_generic_script.scripts;
	t2scripts_data->num = twl4030_generic_script.num;
}

void twl4030_get_vc_timings(struct prm_setup_vc *setup_vc)
{
	/* copies new voltsetup time for RERT */
	setup_vc->ret.voltsetup1_vdd1 =
				twl4030_voltsetup_time.ret.voltsetup1_vdd1;
	setup_vc->ret.voltsetup1_vdd2 =
				twl4030_voltsetup_time.ret.voltsetup1_vdd2;
	setup_vc->ret.voltsetup2 = twl4030_voltsetup_time.ret.voltsetup2;
	setup_vc->ret.voltoffset = twl4030_voltsetup_time.ret.voltoffset;

	/* copies new voltsetup time for OFF */
	setup_vc->off.voltsetup1_vdd1 =
				twl4030_voltsetup_time.off.voltsetup1_vdd1;
	setup_vc->off.voltsetup1_vdd2 =
				twl4030_voltsetup_time.off.voltsetup1_vdd2;
	setup_vc->off.voltsetup2 = twl4030_voltsetup_time.off.voltsetup2;
	setup_vc->off.voltoffset = twl4030_voltsetup_time.off.voltoffset;
}

static void twl4030_smartreflex_init(void)
{
	int ret = 0;
	u8 read_val;

	ret = twl_i2c_read_u8(TWL4030_MODULE_PM_RECEIVER, &read_val,
			      R_DCDC_GLOBAL_CFG);
	read_val |= DCDC_GLOBAL_CFG_ENABLE_SRFLX;
	ret |= twl_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER, read_val,
				R_DCDC_GLOBAL_CFG);
}

static struct omap_smartreflex_pmic_data twl4030_sr_data = {
	.sr_pmic_init	= twl4030_smartreflex_init,
};

static int __init twl4030_init(void)
{
	omap_sr_register_pmic(&twl4030_sr_data);
	return 0;
}
arch_initcall(twl4030_init);
#endif
