/*
 * linux/arch/arm/mach-omap2/board-rx51-network.c
 *
 * Copyright (C) 2008 Nokia
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/mach-types.h>

static int __init rx51_network_init(void)
{
	if (!(machine_is_nokia_rx51() || machine_is_nokia_rx71()))
		return 0;

	return 0;
}

subsys_initcall(rx51_network_init);
