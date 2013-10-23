/*
 * linux/arch/arm/mach-omap2/sr_device.c
 *
 * OMAP3/OMAP4 smartreflex device file
 *
 * Author: Thara Gopinath	<thara@ti.com>
 *
 * Based originally on code from smartreflex.c
 * Copyright (C) 2010 Texas Instruments, Inc.
 * Thara Gopinath <thara@ti.com>
 *
 * Copyright (C) 2008 Nokia Corporation
 * Kalle Jokiniemi
 *
 * Copyright (C) 2007 Texas Instruments, Inc.
 * Lesly A M <x0080970@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/err.h>

#include <plat/control.h>
#include <plat/omap_hwmod.h>
#include <plat/omap_device.h>
#include <plat/opp.h>

#include "smartreflex.h"
#include "voltage.h"

#define MAX_HWMOD_NAME_LEN	16

static struct omap_device_pm_latency omap_sr_latency[] = {
	{
		.deactivate_func = omap_device_idle_hwmods,
		.activate_func	 = omap_device_enable_hwmods,
		.flags = OMAP_DEVICE_LATENCY_AUTO_ADJUST
	},
};

/* Read EFUSE values from control registers for OMAP3430 */
static void __init sr_read_efuse(
				struct omap_smartreflex_dev_data *dev_data,
				struct omap_smartreflex_data *sr_data)
{
	int i;
	if (!dev_data) {
		pr_warning("%s: ouch bad parameter.. dev_data is null",
				__func__);
		return;
	} else if (!dev_data->volts_supported ||
			!dev_data->volt_data || !dev_data->efuse_sr_control ||
			!dev_data->efuse_nvalues_offs) {
		pr_warning("%s: ouch bad parameter.. dev_data = %p,"
				"dev_data->volts_supported= %d, "
				"dev_data->volt_data=%p, "
				"dev_data->efuse_sr_control=%d "
				"dev_data->efuse_nvalues_offs=%p\n",
				__func__, dev_data,
				dev_data->volts_supported,
				dev_data->volt_data,
				dev_data->efuse_sr_control,
				dev_data->efuse_nvalues_offs);
		return;
	}

	if (cpu_is_omap3630()) {
		sr_data->senn_mod = 0x1;
		sr_data->senp_mod = 0x1;

	} else {
		sr_data->senn_mod =
			(omap_ctrl_readl(dev_data->efuse_sr_control) &
				(0x3 << dev_data->sennenable_shift) >>
				dev_data->sennenable_shift);
		sr_data->senp_mod =
			(omap_ctrl_readl(dev_data->efuse_sr_control) &
				(0x3 << dev_data->senpenable_shift) >>
				dev_data->senpenable_shift);
	}


	for (i = 0; i < dev_data->volts_supported; i++)
		dev_data->volt_data[i].sr_nvalue = omap_ctrl_readl(
				dev_data->efuse_nvalues_offs[i]);
}

/*
 * Hard coded nvalues for testing purposes for OMAP3430,
 * may cause device to hang!
 */
static void __init sr_set_testing_nvalues(
				struct omap_smartreflex_dev_data *dev_data,
				struct omap_smartreflex_data *sr_data)
{
	int i;

	if (!dev_data) {
		pr_warning("%s: ouch bad parameter.. dev_data is null",
				 __func__);
			return;
	} else if (!dev_data->volts_supported ||
			!dev_data->volt_data || !dev_data->test_nvalues) {
		pr_warning("%s: ouch bad parameter.. dev_data = %p,"
				"dev_data->volts_supported= %d, "
				"dev_data->volt_data=%p, "
				"dev_data->test_nvalues=%p\n",
				__func__, dev_data,
				dev_data->volts_supported,
				dev_data->volt_data,
				dev_data->test_nvalues);
		return;
	}

	sr_data->senn_mod = dev_data->test_sennenable;
	sr_data->senp_mod = dev_data->test_senpenable;
	for (i = 0; i < dev_data->volts_supported; i++)
		dev_data->volt_data[i].sr_nvalue = dev_data->test_nvalues[i];
}

static void __init sr_set_nvalues(struct omap_smartreflex_dev_data *dev_data,
		struct omap_smartreflex_data *sr_data)
{
	if (cpu_is_omap34xx()) {
		if (SR_TESTING_NVALUES)
			sr_set_testing_nvalues(dev_data, sr_data);
		else
			sr_read_efuse(dev_data, sr_data);
	}
}

static int __init sr_dev_init(struct omap_hwmod *oh, void *user)
{
	static int i;
	char *name = "smartreflex";
	struct omap_smartreflex_data sr_data = {0};
	struct omap_smartreflex_dev_data *sr_dev_data;
	struct omap_device *od;

	sr_dev_data = (struct omap_smartreflex_dev_data *)oh->dev_attr;

	sr_data.enable_on_init = false;

	sr_data.device_enable = omap_device_enable;
	sr_data.device_shutdown = omap_device_shutdown;
	sr_data.device_idle = omap_device_idle;
	omap_get_voltage_table(i, &sr_dev_data->volt_data,
				&sr_dev_data->volts_supported);
	sr_set_nvalues(sr_dev_data, &sr_data);

	od = omap_device_build(name, i, oh, &sr_data, sizeof(sr_data),
			       omap_sr_latency,
			       ARRAY_SIZE(omap_sr_latency), 0);
	if (IS_ERR(od)) {
		pr_warning("%s: Could not build omap_device %s:%s\n",
			__func__, name, oh->name);
	}
	i++;

	return 0;
}

static int __init omap_devinit_smartreflex(void)
{
	return omap_hwmod_for_each_by_class("smartreflex", sr_dev_init, NULL);
}
device_initcall(omap_devinit_smartreflex);
