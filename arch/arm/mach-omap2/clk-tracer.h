/*
 * OMAP3 Clock tracer
 *
 * Copyright (C) 2011 Nokia Corporation
 * Tero Kristo
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARCH_ARM_MACH_OMAP2_CLK_TRACER_H
#define __ARCH_ARM_MACH_OMAP2_CLK_TRACER_H

extern void clk_tracer_register_clk(struct clk *clk);
extern int clk_tracer_get_load(struct clk *clk);

#endif
