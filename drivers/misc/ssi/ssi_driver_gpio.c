/*
 * ssi_driver_gpio.c
 *
 * Implements SSI GPIO related functionality. (i.e: wake lines management)
 *
 * Copyright (C) 2007-2008 Nokia Corporation. All rights reserved.
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
#include <linux/gpio.h>
#include "ssi_driver.h"

static void do_ssi_cawake_tasklet(unsigned long ssi_p)
{
	struct ssi_port *port = (struct ssi_port *)ssi_p;
	struct ssi_dev *ssi_ctrl = port->ssi_controller;

	if (ssi_cawake(port)) {
		if (!ssi_ctrl->cawake_clk_enable) {
			ssi_ctrl->cawake_clk_enable = 1;
			clk_enable(ssi_ctrl->ssi_clk);
		}
		ssi_port_event_handler(port, SSI_EVENT_CAWAKE_UP, NULL);
	} else {
		ssi_port_event_handler(port, SSI_EVENT_CAWAKE_DOWN, NULL);
		if (ssi_ctrl->cawake_clk_enable) {
			ssi_ctrl->cawake_clk_enable = 0;
			clk_disable(ssi_ctrl->ssi_clk);
		}
	}
}

static irqreturn_t ssi_cawake_isr(int irq, void *ssi_p)
{
	struct ssi_port *port = ssi_p;

	tasklet_hi_schedule(&port->cawake_tasklet);

	return IRQ_HANDLED;
}

int __init ssi_cawake_init(struct ssi_port *port, const char *irq_name)
{
	tasklet_init(&port->cawake_tasklet, do_ssi_cawake_tasklet,
							(unsigned long)port);
	if (request_irq(port->cawake_gpio_irq, ssi_cawake_isr,
		IRQF_DISABLED | IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
							irq_name, port) < 0) {
		dev_err(port->ssi_controller->dev,
			"FAILED to request %s GPIO IRQ %d on port %d\n",
			irq_name, port->cawake_gpio_irq, port->port_number);
		return -EBUSY;
	}
	enable_irq_wake(port->cawake_gpio_irq);

	return 0;
}

void ssi_cawake_exit(struct ssi_port *port)
{
	if (port->cawake_gpio < 0)
		return;	/* Nothing to do */

	disable_irq_wake(port->cawake_gpio_irq);
	tasklet_kill(&port->cawake_tasklet);
	free_irq(port->cawake_gpio_irq, port);
}
