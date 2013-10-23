/*
 * linux/arch/arm/mach-omap2/board-rx51-flash.c
 *
 * Copyright (C) 2008 Nokia
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mmc/host.h>

#include <asm/mach/flash.h>
#include <asm/mach-types.h>

#include <mach/onenand.h>

#include "mmc-twl4030.h"

#define	RX51_FLASH_CS	0
#define VAUX3_DEV_GRP		0x1F
#define SYSTEM_REV_B_USES_VAUX3	0x1699
#define SYSTEM_REV_S_USES_VAUX3	0x7

extern struct mtd_partition n800_partitions[ONENAND_MAX_PARTITIONS];
extern int n800_onenand_setup(void __iomem *onenand_base, int freq);
extern void __init n800_flash_init(void);

static struct flash_platform_data rx51_flash_data = {
	.map_name	= "cfi_probe",
	.width		= 2,
	.parts		= n800_partitions,
	.nr_parts	= ARRAY_SIZE(n800_partitions),
};

static struct resource rx51_flash_resource = {
	.flags		= IORESOURCE_MEM,
};

static struct platform_device rx51_flash_device = {
	.name		= "omapflash",
	.id		= 0,
	.dev		= {
		.platform_data	= &rx51_flash_data,
	},
	.num_resources	= 1,
	.resource	= &rx51_flash_resource,
};

static struct platform_device *rx51_flash_devices[] = {
	&rx51_flash_device,
};

static struct twl4030_hsmmc_info mmc[] __initdata = {
	{
		.name		= "external",
		.mmc		= 1,
		.wires		= 4,
		.cover_only	= true,
		.gpio_cd	= 160,
		.gpio_wp	= -EINVAL,
		.power_saving	= true,
		.caps		= MMC_CAP_SD_ONLY,
	},
	{
		.name		= "internal",
		.mmc		= 2,
		.wires		= 8,
		.gpio_cd	= -EINVAL,
		.gpio_wp	= -EINVAL,
		.vsim_18v	= true,
		.power_saving	= true,
		.caps		= MMC_CAP_MMC_ONLY | MMC_CAP_NONREMOVABLE,
	},
	{}	/* Terminator */
};

static int __init rx51_flash_init(void)
{
	if (!(machine_is_nokia_rx51() || machine_is_nokia_rx71()))
		return 0;

	if ((system_rev >= SYSTEM_REV_S_USES_VAUX3 && system_rev < 0x100) ||
	    system_rev >= SYSTEM_REV_B_USES_VAUX3)
		mmc[1].vmmc_dev_grp = VAUX3_DEV_GRP;
	else
		mmc[1].power_saving = false;

	platform_add_devices(rx51_flash_devices, ARRAY_SIZE(rx51_flash_devices));
	n800_flash_init();
	twl4030_mmc_init(mmc);
	return 0;
}

subsys_initcall(rx51_flash_init);
