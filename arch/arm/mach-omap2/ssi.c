/*
 * linux/arch/arm/mach-omap2/ssi.c
 *
 * Copyright (C) 2010 Nokia Corporation. All rights reserved.
 *
 * Contact: Carlos Chinea <carlos.chinea@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/platform_data/ssi.h>

#include "common.h"
#include "omap-pm.h"

static struct omap_ssi_platform_data ssi_pdata = {
	.num_ports			= SSI_NUM_PORTS,
	.get_dev_context_loss_count	= omap_pm_get_dev_context_loss_count,
};

static struct resource ssi_resources[] = {
	/* SSI controller */
	[0] = {
		.start	= 0x48058000,
		.end	= 0x48058fff,
		.name	= "omap_ssi_sys",
		.flags	= IORESOURCE_MEM,
	},
	/* GDD */
	[1] = {
		.start	= 0x48059000,
		.end	= 0x48059fff,
		.name	= "omap_ssi_gdd",
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start	= 71 + OMAP_INTC_START,
		.end	= 71 + OMAP_INTC_START,
		.name	= "ssi_gdd",
		.flags	= IORESOURCE_IRQ,
	},
	/* SSI port 1 */
	[3] = {
		.start	= 0x4805a000,
		.end	= 0x4805a7ff,
		.name	= "omap_ssi_sst1",
		.flags	= IORESOURCE_MEM,
	},
	[4] = {
		.start	= 0x4805a800,
		.end	= 0x4805afff,
		.name	= "omap_ssi_ssr1",
		.flags	= IORESOURCE_MEM,
	},
	[5] = {
		.start	= 67 + OMAP_INTC_START,
		.end	= 67 + OMAP_INTC_START,
		.name	= "ssi_p1_mpu_irq0",
		.flags	= IORESOURCE_IRQ,
	},
	[6] = {
		.start	= 68 + OMAP_INTC_START,
		.end	= 68 + OMAP_INTC_START,
		.name	= "ssi_p1_mpu_irq1",
		.flags	= IORESOURCE_IRQ,
	},
	[7] = {
		.start	= 0,
		.end	= 0,
		.name	= "ssi_p1_cawake",
		.flags	= IORESOURCE_IRQ | IORESOURCE_UNSET,
	},
};

static struct platform_device ssi_pdev = {
	.name		= "omap_ssi",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(ssi_resources),
	.resource	= ssi_resources,
	.dev		= {
				.platform_data	= &ssi_pdata,
	},
};

int __init omap_ssi_config(struct omap_ssi_board_config *ssi_config)
{
	unsigned int port, offset, cawake_gpio;
	int err;

	ssi_pdata.num_ports = ssi_config->num_ports;
	for (port = 0, offset = 7; port < ssi_config->num_ports;
							port++, offset += 5) {
		cawake_gpio = ssi_config->cawake_gpio[port];
		if (!cawake_gpio)
			continue; /* Nothing to do */
		err = gpio_request(cawake_gpio, "cawake");
		if (err < 0)
			goto rback;
		gpio_direction_input(cawake_gpio);
		ssi_resources[offset].start = gpio_to_irq(cawake_gpio);
		ssi_resources[offset].flags &= ~IORESOURCE_UNSET;
		ssi_resources[offset].flags |= IORESOURCE_IRQ_HIGHEDGE |
							IORESOURCE_IRQ_LOWEDGE;
	}

	return 0;
rback:
	dev_err(&ssi_pdev.dev, "Request cawake (gpio%d) failed\n", cawake_gpio);
	while (port > 0)
		gpio_free(ssi_config->cawake_gpio[--port]);

	return err;
}

static int __init ssi_init(void)
{
	return platform_device_register(&ssi_pdev);
}
subsys_initcall(ssi_init);
