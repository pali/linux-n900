/*
 * Copyright (C) 2010 Nokia
 * Peter De Schrijver <peter.de-schrijver@nokia.com>
 *
 * Based on twl4030.c Copyright (C) 2010 Texas Instruments, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifdef CONFIG_TWL4030_POWER

#include <linux/i2c/twl.h>
#include <plat/cpu.h>

/*
 * Sequence to control the TRITON Power resources,
 * when the system goes into sleep.
 * Executed upon P1_P2/P3 transition for sleep.
 */
static struct twl4030_ins __initdata sleep_on_seq[] = {
	/* Broadcast message to put res to sleep */
	{MSG_BROADCAST(DEV_GRP_NULL, RES_GRP_ALL, RES_TYPE_ALL, 0, RES_STATE_SLEEP), 2},
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
	{MSG_BROADCAST(DEV_GRP_NULL, RES_GRP_ALL, RES_TYPE_ALL, 0, RES_STATE_ACTIVE), 2},
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
	{MSG_SINGULAR(DEV_GRP_NULL, RES_CLKEN, RES_STATE_ACTIVE), 0x37},
	{MSG_BROADCAST(DEV_GRP_NULL, RES_GRP_ALL, RES_TYPE_ALL, 0, RES_STATE_ACTIVE), 2},
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
	{MSG_SINGULAR(DEV_GRP_NULL, RES_VPLL1, RES_STATE_WRST), 2},
	{MSG_SINGULAR(DEV_GRP_NULL, RES_VDD2, RES_STATE_WRST), 7},
	{MSG_SINGULAR(DEV_GRP_NULL, RES_VDD1, RES_STATE_WRST), 7},
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

static struct twl4030_power_data twl4030_nokia_script __initdata = {
	.scripts	= twl4030_scripts,
	.num		= ARRAY_SIZE(twl4030_scripts),
};

static struct twl4030_resconfig twl4030_rconfig[] = {
	{ .resource = RES_VDD1, .devgroup = DEV_GRP_P1, .type = 4, .type2 = 0, .remap_sleep = 0, },
	{ .resource = RES_VDD2, .devgroup = DEV_GRP_P1, .type = 3, .type2 = 0, .remap_sleep = 0, },
	{ .resource = RES_VPLL1, .devgroup = DEV_GRP_P1, .type = 3, .type2 = 0, .remap_sleep = 0, },
	{ .resource = RES_VPLL2, .devgroup = TWL4030_RESCONFIG_UNDEF, .type = 0, .type2 = 0, .remap_sleep = 0x09, },
	{ .resource = RES_VAUX1, .devgroup = TWL4030_RESCONFIG_UNDEF, .type = 0, .type2 = 0, .remap_sleep = 0x09, },
	{ .resource = RES_VAUX2, .devgroup = TWL4030_RESCONFIG_UNDEF, .type = 0, .type2 = 0, .remap_sleep = 0x09, },
	{ .resource = RES_VAUX3, .devgroup = TWL4030_RESCONFIG_UNDEF, .type = 0, .type2 = 0, .remap_sleep = 0x09, },
	{ .resource = RES_VAUX4, .devgroup = TWL4030_RESCONFIG_UNDEF, .type = 0, .type2 = 0, .remap_sleep = 0x09, },
	{ .resource = RES_VMMC1, .devgroup = TWL4030_RESCONFIG_UNDEF, .type = 0, .type2 = 0, .remap_sleep = 0x09, },
	{ .resource = RES_VMMC2, .devgroup = TWL4030_RESCONFIG_UNDEF, .type = 0, .type2 = 0, .remap_sleep = 0x09, },
	{ .resource = RES_VDAC, .devgroup = TWL4030_RESCONFIG_UNDEF, .type = 0, .type2 = 0, .remap_sleep = 0x09, },
	{ .resource = RES_VUSB_1V5, .devgroup = TWL4030_RESCONFIG_UNDEF, .type = 0, .type2 = 0, .remap_sleep = 0x09, },
	{ .resource = RES_VUSB_1V8, .devgroup = TWL4030_RESCONFIG_UNDEF, .type = 0, .type2 = 0, .remap_sleep = 0x09, },
	{ .resource = RES_VUSB_3V1, .devgroup = TWL4030_RESCONFIG_UNDEF, .type = 0, .type2 = 0, .remap_sleep = 0x09, },
	{ .resource = RES_VINTANA1, .devgroup = DEV_GRP_P1 | DEV_GRP_P2, .type = 1, .type2 = 2, .remap_sleep = 0x08, },
	{ .resource = RES_VINTANA2, .devgroup = DEV_GRP_P1 | DEV_GRP_P2, .type = 0, .type2 = 2, .remap_sleep = 0x08, },
	{ .resource = RES_VINTDIG, .devgroup = DEV_GRP_P1 | DEV_GRP_P2, .type = 1, .type2 = 2, .remap_sleep = 0x08, },
	{ .resource = RES_VIO, .devgroup = DEV_GRP_P1 | DEV_GRP_P2, .type = 2, .type2 = 2, .remap_sleep = 0x08, },
	{ .resource = RES_CLKEN, .devgroup = DEV_GRP_P1 | DEV_GRP_P3, .type = 3, .type2 = 2, .remap_sleep = 0x8, },
	{ .resource = RES_HFCLKOUT, .devgroup = DEV_GRP_P3, .type = 0, .type2 = 1, .remap_sleep = 0x8, },
	{ .resource = RES_NRES_PWRON, .devgroup = DEV_GRP_P1 | DEV_GRP_P3, .type = 0, .type2 = 1, .remap_sleep = 8 },
	{ .resource = RES_REGEN, .devgroup = DEV_GRP_P1 | DEV_GRP_P2 | DEV_GRP_P3, .type = 2, .type2 = 0, .remap_sleep = 9 },
	{ .resource = 0, .devgroup = 0, .type = 0, .type2 = 0, .remap_sleep = 0 }
};

void __init twl4030_get_nokia_powerdata(struct twl4030_power_data *power_data)
{
	/* Work around OMAP 3630 erratum #1.75 by keeping VDD2 on */
	if (omap_rev() == OMAP3630_REV_ES1_0)
		twl4030_rconfig[1].remap_sleep = 0x9;

	power_data->scripts = twl4030_nokia_script.scripts;
	power_data->num = twl4030_nokia_script.num;
	power_data->resource_config = twl4030_rconfig;
}

#endif
