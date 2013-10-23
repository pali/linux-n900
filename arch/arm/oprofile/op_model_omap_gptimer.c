/**
 * OMAP gptimer based event monitor driver for oprofile
 *
 * Copyright (C) 2009 Nokia Corporation
 * Contact: Siarhei Siamashka <siarhei.siamashka@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/types.h>
#include <linux/oprofile.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#include <mach/dmtimer.h>

#include "op_counter.h"
#include "op_arm_model.h"

static struct omap_dm_timer *gptimer;

static int gptimer_init(void)
{
	return 0;
}

static int gptimer_setup(void)
{
	return 0;
}

static irqreturn_t gptimer_interrupt(int irq, void *arg)
{
	omap_dm_timer_write_status(gptimer, OMAP_TIMER_INT_OVERFLOW);
	oprofile_add_sample(get_irq_regs(), 0);
	return IRQ_HANDLED;
}

static int gptimer_start(void)
{
	int err;
	u32 count = counter_config[0].count;

	BUG_ON(gptimer != NULL);
	/* First try to request timers from CORE power domain for OMAP3 */
	if (cpu_is_omap34xx()) {
		gptimer = omap_dm_timer_request_specific(10);
		if (gptimer == NULL)
			gptimer = omap_dm_timer_request_specific(11);
	}
	/* Just any timer would be fine */
	if (gptimer == NULL)
		gptimer = omap_dm_timer_request();
	if (gptimer == NULL)
		return -ENODEV;

	omap_dm_timer_set_source(gptimer, OMAP_TIMER_SRC_32_KHZ);
	err = request_irq(omap_dm_timer_get_irq(gptimer), gptimer_interrupt,
				IRQF_DISABLED, "oprofile gptimer", NULL);
	if (err) {
		omap_dm_timer_free(gptimer);
		gptimer = NULL;
		printk(KERN_ERR "oprofile: unable to request gptimer IRQ\n");
		return err;
	}

	if (count < 1)
		count = 1;

	omap_dm_timer_set_load_start(gptimer, 1, 0xffffffff - count);
	omap_dm_timer_set_int_enable(gptimer, OMAP_TIMER_INT_OVERFLOW);
	return 0;
}

static void gptimer_stop(void)
{
	omap_dm_timer_set_int_enable(gptimer, 0);
	free_irq(omap_dm_timer_get_irq(gptimer), NULL);
	omap_dm_timer_free(gptimer);
	gptimer = NULL;
}

struct op_arm_model_spec op_omap_gptimer_spec = {
	.init		= gptimer_init,
	.num_counters	= 1,
	.setup_ctrs	= gptimer_setup,
	.start		= gptimer_start,
	.stop		= gptimer_stop,
	.name		= "arm/omap-gptimer",
};
