/*
 * arch/arm/mach-omap2/cpufreq34xx.c
 * OMAP3 resource init/change_level/validate_level functions
 *
 * Copyright (C) 2007-2008 Texas Instruments, Inc.
 * Rajendra Nayak <rnayak@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 * History:
 *
 */

#include <linux/module.h>
#include <linux/err.h>

#include <plat/opp.h>
#include <plat/cpu.h>
#include "omap3-opp.h"

#include "omap3-opp.h"

static struct omap_opp_def __initdata omap34xx_mpu_rate_table[] = {
	/* OPP1 */
	OMAP_OPP_DEF(true, 125000000, 975000),
	/* OPP2 */
	OMAP_OPP_DEF(true, 250000000, 1075000),
	/* OPP3 */
	OMAP_OPP_DEF(true, 500000000, 1200000),
	/* OPP4 */
	OMAP_OPP_DEF(true, 550000000, 1270000),
	/* OPP5 */
	OMAP_OPP_DEF(true, 600000000, 1350000),
	/* Terminator */
	OMAP_OPP_DEF(0, 0, 0)
};

static struct omap_opp_def __initdata omap34xx_l3_rate_table[] = {
	/*
	 * OPP1 - 41.5 MHz is disabled because: The voltage for that OPP is
	 * almost the same than the one at 83MHz thus providing very little
	 * gain for the power point of view. In term of energy it will even
	 * increase the consumption due to the very negative performance
	 * impact that frequency will do to the MPU and the whole system in
	 * general.
	 */
	OMAP_OPP_DEF(false, 41500000, 975000),
	/* OPP2 */
	OMAP_OPP_DEF(true, 83000000, 1050000),
	/* OPP3 */
	OMAP_OPP_DEF(true, 166000000, 1150000),
	/* Terminator */
	OMAP_OPP_DEF(0, 0, 0)
};

static struct omap_opp_def __initdata omap34xx_dsp_rate_table[] = {
	/* OPP1 */
	OMAP_OPP_DEF(true, 90000000, 975000),
	/* OPP2 */
	OMAP_OPP_DEF(true, 180000000, 1075000),
	/* OPP3 */
	OMAP_OPP_DEF(true, 360000000, 1200000),
	/* OPP4 */
	OMAP_OPP_DEF(true, 400000000, 1270000),
	/* OPP5 */
	OMAP_OPP_DEF(true, 430000000, 1350000),
	/* Terminator */
	OMAP_OPP_DEF(0, 0, 0)
};

static struct omap_opp_def __initdata omap36xx_mpu_rate_table[] = {
	/* OPP1 - OPP50 */
	OMAP_OPP_DEF(true,  300000000, 1012500),
	/* OPP2 - OPP100 */
	OMAP_OPP_DEF(true,  600000000, 1200000),
	/* OPP3 - OPP-Turbo */
	OMAP_OPP_DEF(false, 800000000, 1325000),
	/* OPP4 - OPP-SB */
	OMAP_OPP_DEF(false, 1000000000, 1375000),
	/* Terminator */
	OMAP_OPP_DEF(0, 0, 0)
};

static struct omap_opp_def __initdata omap36xx_l3_rate_table[] = {
	/* OPP1 - OPP50 */
	OMAP_OPP_DEF(true, 100000000, 1000000),
	/* OPP2 - OPP100, OPP-Turbo, OPP-SB */
	OMAP_OPP_DEF(true, 200000000, 1200000),
	/* Terminator */
	OMAP_OPP_DEF(0, 0, 0)
};

static struct omap_opp_def __initdata omap36xx_dsp_rate_table[] = {
	/* OPP1 - OPP50 */
	OMAP_OPP_DEF(true,  260000000, 1012500),
	/* OPP2 - OPP100 */
	OMAP_OPP_DEF(true,  520000000, 1200000),
	/* OPP3 - OPP-Turbo */
	OMAP_OPP_DEF(false, 660000000, 1325000),
	/* OPP4 - OPP-SB */
	OMAP_OPP_DEF(false, 800000000, 1375000),
	/* Terminator */
	OMAP_OPP_DEF(0, 0, 0)
};

int __init omap3_pm_init_opp_table(void)
{
	int i, r;
	struct omap_opp_def **omap3_opp_def_list;
	struct omap_opp_def *omap34xx_opp_def_list[] = {
		omap34xx_mpu_rate_table,
		omap34xx_l3_rate_table,
		omap34xx_dsp_rate_table
	};
	struct omap_opp_def *omap36xx_opp_def_list[] = {
		omap36xx_mpu_rate_table,
		omap36xx_l3_rate_table,
		omap36xx_dsp_rate_table
	};
	enum opp_t omap3_opps[] = {
		OPP_MPU,
		OPP_L3,
		OPP_DSP
	};

	omap3_opp_def_list = cpu_is_omap3630() ? omap36xx_opp_def_list :
				omap34xx_opp_def_list;

	for (i = 0; i < ARRAY_SIZE(omap3_opps); i++) {
		r = opp_init_list(omap3_opps[i], omap3_opp_def_list[i]);
		if (r)
			break;
	}
	if (!r)
		return 0;

	/* Cascading error handling - disable all enabled OPPs */
	pr_err("%s: Failed to register %d OPP type\n", __func__,
		omap3_opps[i]);
	i--;
	while (i != -1) {
		struct omap_opp *opp;
		unsigned long freq = 0;

		do {
			opp = opp_find_freq_ceil(omap3_opps[i], &freq);
			if (IS_ERR(opp))
				break;
			opp_disable(opp);
			freq++;
		} while (1);
		i--;
	}

	return r;
}

