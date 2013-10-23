/*
 * OMAP on-chip device: structure and function call definitions
 *
 * Copyright (C) 2007-2008 Texas Instruments, Inc.
 * Copyright (C) 2007-2008 Nokia Corporation
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#ifndef ARCH_ARM_PLAT_OMAP_OMAPDEV_H
#define ARCH_ARM_PLAT_OMAP_OMAPDEV_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/platform_device.h>

#include <mach/cpu.h>
#include <mach/powerdomain.h>

/**
 * struct omapdev - OMAP on-chip hardware devices
 * @name: name of the device - should match TRM
 * @pwrdm: powerdomain that the device resides in
 * @omap_chip: OMAP chips this omapdev is valid for
 * @pdev_name: platform_device name associated with this omapdev (if any)
 * @pdev_id: platform_device id associated with this omapdev (if any)
 *
 */
struct omapdev {

	const char *name;

	union {
		const char *name;
		struct powerdomain *ptr;
	} pwrdm;

	const struct omap_chip_id omap_chip;

	const char *pdev_name;

	const int pdev_id;

	struct list_head node;
};


void omapdev_init(struct omapdev **odev_list);

struct powerdomain *omapdev_get_pwrdm(struct omapdev *odev);

struct omapdev *omapdev_find_pdev(struct platform_device *pdev);


#endif
