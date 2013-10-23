/*
 * Smart reflex Class 3 specific implementations
 *
 * Copyright (C) 2010 Texas Instruments, Inc.
 * Thara Gopinath <thara@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "smartreflex.h"
#include "smartreflex-class3.h"
#include "voltage.h"

static int sr_class3_enable(int id, struct omap_volt_data *volt_data)
{
	omap_voltageprocessor_enable(id, volt_data);
	return sr_enable(id, volt_data);
}

static int sr_class3_disable(int id, struct omap_volt_data *volt_data,
		int is_volt_reset)
{
	omap_voltageprocessor_disable(id);
	sr_disable(id);
	if (is_volt_reset)
		omap_reset_voltage(id);

	return 0;
}

static void sr_class3_configure(int id)
{
	sr_configure_errgen(id);
}

/* SR class3 structure */
static struct omap_smartreflex_class_data class3_data = {
	.enable = sr_class3_enable,
	.disable = sr_class3_disable,
	.configure = sr_class3_configure,
	.class_type = SR_CLASS3,
};

int __init sr_class3_init(void)
{
	pr_info("SmartReflex class 3 initialized\n");
	return omap_sr_register_class(&class3_data);
}
