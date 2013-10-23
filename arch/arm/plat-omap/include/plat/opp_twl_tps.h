/*
 * opp_twl_tps.h - TWL/TPS-specific headers for the OPP code
 *
 * Copyright (C) 2009 Texas Instruments Incorporated.
 *	Nishanth Menon
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * XXX This code belongs as part of some other TWL/TPS code.
 */
#ifndef _ARCH_ARM_PLAT_OMAP_OPP_TWL_TPS_H
#define _ARCH_ARM_PLAT_OMAP_OPP_TWL_TPS_H

#include <linux/kernel.h>

#ifdef CONFIG_CPU_FREQ
unsigned long omap_twl_vsel_to_uv(const u8 vsel);
u8 omap_twl_uv_to_vsel(unsigned long uV);
#else
static inline unsigned long omap_twl_vsel_to_uv(const u8 vsel)
{
	return 0;
}
static inline u8 omap_twl_uv_to_vsel(unsigned long uV)
{
	return 0;
}
#endif

#endif
