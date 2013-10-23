/*
 * opp_twl_tps.c - TWL/TPS-specific functions for the OPP code
 *
 * Copyright (C) 2009 Texas Instruments Incorporated.
 * Nishanth Menon
 * Copyright (C) 2009 Nokia Corporation
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * XXX This code should be part of some other TWL/TPS code.
 */

#include <plat/opp_twl_tps.h>

/**
 * omap_twl_vsel_to_vdc - convert TWL/TPS VSEL value to microvolts DC
 * @vsel: TWL/TPS VSEL value to convert
 *
 * Returns the microvolts DC that the TWL/TPS family of PMICs should
 * generate when programmed with @vsel.
 */
unsigned long omap_twl_vsel_to_uv(const u8 vsel)
{
	return (((vsel * 125) + 6000)) * 100;
}

/**
 * omap_twl_uv_to_vsel - convert microvolts DC to TWL/TPS VSEL value
 * @uv: microvolts DC to convert
 *
 * Returns the VSEL value necessary for the TWL/TPS family of PMICs to
 * generate an output voltage equal to or greater than @uv microvolts DC.
 */
u8 omap_twl_uv_to_vsel(unsigned long uv)
{
	return DIV_ROUND_UP(uv - 600000, 12500);
}
