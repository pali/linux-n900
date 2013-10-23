/*
 * OMAP3 Clock load tracer
 *
 * Copyright (C) 2010 Nokia Corporation
 * Tero Kristo
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/input.h>

#include <plat/clock.h>
#include <plat/display.h>

#include "clock.h"
#include "clk-tracer.h"

#define CLKOPS_SLOT_AMT 4

struct clock_data {
	int load;
	u32 idle;
	u32 active;
	u32 idle_save;
	u32 active_save;
	u32 timestamp;
	u32 last_switch;
	const struct clkops *saved_ops;
};

static const struct clkops *clk_tracer_saved_ops[CLKOPS_SLOT_AMT];
static struct clkops clk_tracer_ops[CLKOPS_SLOT_AMT];
static int clk_tracer_used_ops;

static int clk_tracer_clk_enable(struct clk *clk)
{
	struct clock_data *d;
	if (clk->data) {
		d = (struct clock_data *)clk->data;
		d->idle += jiffies - d->last_switch;
		d->last_switch = jiffies;
		return d->saved_ops->enable(clk);
	}
	return 0;
}

static void clk_tracer_clk_disable(struct clk *clk)
{
	struct clock_data *d;
	if (clk->data) {
		d = (struct clock_data *)clk->data;
		d->active += jiffies - d->last_switch;
		d->last_switch = jiffies;
		d->saved_ops->disable(clk);
	}
}

void clk_tracer_register_clk(struct clk *clk)
{
	int i;
	struct clock_data *d;

	if (!clk)
		return;

	d = kzalloc(sizeof(struct clock_data), GFP_KERNEL);
	clk->data = d;

	/* find clkops slot */
	for (i = 0; i < clk_tracer_used_ops; i++) {
		if (clk->ops == clk_tracer_saved_ops[i])
			break;
	}
	if (i == clk_tracer_used_ops) {
		if (clk_tracer_used_ops == CLKOPS_SLOT_AMT) {
			WARN(1, "clk-tracer: out of clkops slots\n");
			return;
		}
		clk_tracer_saved_ops[i] = clk->ops;
		memcpy(clk_tracer_ops + i, clk->ops, sizeof(struct clkops));
		clk_tracer_ops[i].enable = clk_tracer_clk_enable;
		clk_tracer_ops[i].disable = clk_tracer_clk_disable;
		clk_tracer_used_ops++;
	}
	clk->ops = clk_tracer_ops + i;
	d->saved_ops = clk_tracer_saved_ops[i];
}

int clk_tracer_get_load(struct clk *clk)
{
	u32 t;
	u32 j_act;
	struct clock_data *d;

	d = (struct clock_data *)clk->data;

	if (!d)
		return 0;

	if (jiffies - d->timestamp >= HZ / 10) {
		t = jiffies - d->timestamp;
		j_act = d->active - d->active_save;

		if (clk->usecount)
			j_act += jiffies - d->last_switch;

		d->idle_save = d->idle;
		d->active_save = d->active;
		d->timestamp = jiffies;
		if (j_act > t)
			d->load = 100;
		else
			d->load = j_act * 100 / t;
	}
	return d->load;
}
