/*
 * linux/arch/arm/mach-omap2/cpuidle34xx.c
 *
 * OMAP3 CPU IDLE Routines
 *
 * Copyright (C) 2008 Texas Instruments, Inc.
 * Rajendra Nayak <rnayak@ti.com>
 *
 * Copyright (C) 2007 Texas Instruments, Inc.
 * Karthik Dasu <karthik-dp@ti.com>
 *
 * Copyright (C) 2006 Nokia Corporation
 * Tony Lindgren <tony@atomide.com>
 *
 * Copyright (C) 2005 Texas Instruments, Inc.
 * Richard Woodruff <r-woodruff2@ti.com>
 *
 * Based on pm.c for omap2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/sched.h>
#include <linux/cpuidle.h>

#include <plat/prcm.h>
#include <plat/irqs.h>
#include <plat/powerdomain.h>
#include <plat/clockdomain.h>
#include <plat/control.h>
#include <plat/serial.h>
#include <plat/sram.h>

#include "pm.h"
#include "pm-optimizer.h"

#ifdef CONFIG_CPU_IDLE

#define OMAP3_MAX_STATES 8
#define OMAP3_STATE_C1 0 /* C1 - MPU WFI + Core active */
#define OMAP3_STATE_C2 1 /* C2 - MPU WFI + Core inactive */
#define OMAP3_STATE_C3 2 /* C3 - MPU CSWR + Core inactive */
#define OMAP3_STATE_C4 3 /* C4 - MPU OFF + Core iactive */
#define OMAP3_STATE_C5 4 /* C5 - MPU RET + Core RET */
#define OMAP3_STATE_C6 5 /* C6 - MPU OFF + Core RET */
#define OMAP3_STATE_C7 6 /* C7 - MPU OFF + Core OFF */
#define OMAP3_STATE_C8 7 /* C8 - MPU OFF + Core OFF + save secure RAM */

struct omap3_processor_cx {
	u8 valid;
	u8 type;
	u32 sleep_latency;
	u32 wakeup_latency;
	u32 mpu_state;
	u32 core_state;
	u32 threshold;
	u32 flags;
};

static struct cpuidle_state *cpuidle_forced_state;
struct omap3_processor_cx omap3_power_states[OMAP3_MAX_STATES];
struct omap3_processor_cx current_cx_state;
static struct powerdomain *mpu_pd, *core_pd, *per_pd, *iva2_pd;
static struct powerdomain *sgx_pd, *usb_pd, *cam_pd, *dss_pd;

/*
 * The latencies/thresholds for various C states have
 * to be configured from the respective board files.
 * These are some default values (which might not provide
 * the best power savings) used on boards which do not
 * pass these details from the board file.
 */
static struct cpuidle_params cpuidle_params_table[] = {
	/* C1 */
	{1, 2, 2, 5},
	/* C2 */
	{1, 10, 10, 30},
	/* C3 */
	{1, 50, 50, 300},
	/* C4 */
	{1, 1500, 1800, 4000},
	/* C5 */
	{1, 2500, 7500, 12000},
	/* C6 */
	{1, 3000, 8500, 15000},
	/* C7 */
	{1, 10000, 30000, 300000},
	/* C8 */
	{1, 10000, 30000, 300000},
};

static int omap3_idle_bm_check(void)
{
	if (!omap3_can_sleep())
		return 1;
	return 0;
}

int cpuidle_chip_is_idle(void)
{
	if (pwrdm_read_pwrst(dss_pd) == PWRDM_POWER_ON)
		return 0;
	if (pwrdm_read_pwrst(cam_pd) == PWRDM_POWER_ON)
		return 0;
	if (pwrdm_read_pwrst(sgx_pd) == PWRDM_POWER_ON)
		return 0;
	if (!pwrdm_can_idle(per_pd))
		return 0;
	if (!pwrdm_can_idle(core_pd))
		return 0;
	return 1;
}

/**
 * omap3_enter_idle - Programs OMAP3 to enter the specified state
 * @dev: cpuidle device
 * @state: The target state to be programmed
 *
 * Called from the CPUidle framework to program the device to the
 * specified target state selected by the governor.
 */
static int omap3_enter_idle(struct cpuidle_device *dev,
			struct cpuidle_state *state)
{
	struct omap3_processor_cx *cx = cpuidle_get_statedata(state);
	struct timespec ts_preidle, ts_postidle, ts_idle;
	u32 mpu_state = cx->mpu_state, core_state = cx->core_state;
	u32 per_state = 0, saved_per_state = 0;

	current_cx_state = *cx;

	/* Used to keep track of the total time in idle */
	getnstimeofday(&ts_preidle);

	local_irq_disable();
	local_fiq_disable();

	omap3_pwrdm_set_next_pwrst(mpu_pd, mpu_state);
	omap3_pwrdm_set_next_pwrst(core_pd, core_state);

	if (omap_irq_pending() || need_resched())
		goto return_sleep_time;

	/* If we are entering safe state, prevent PER off */
	if (state == dev->safe_state) {
		saved_per_state = omap3_pwrdm_read_next_pwrst(per_pd);
		if (saved_per_state == PWRDM_POWER_OFF) {
			per_state = PWRDM_POWER_RET;
			omap3_pwrdm_set_next_pwrst(per_pd, per_state);
		} else
			per_state = saved_per_state;
	}

	/* Execute ARM wfi */
	omap_sram_idle();

	/* Restore potentially tampered PER state */
	if (per_state != saved_per_state)
		omap3_pwrdm_set_next_pwrst(per_pd, saved_per_state);

return_sleep_time:
	getnstimeofday(&ts_postidle);
	ts_idle = timespec_sub(ts_postidle, ts_preidle);

	local_irq_enable();
	local_fiq_enable();

	return ts_idle.tv_nsec / NSEC_PER_USEC + ts_idle.tv_sec * USEC_PER_SEC;
}

/**
 * omap3_enter_idle_bm - Checks for any bus activity
 * @dev: cpuidle device
 * @state: The target state to be programmed
 *
 * Called from the CPUidle framework for C states with CPUIDLE_FLAG_CHECK_BM
 * flag set. This function checks for any pending bus activity and then
 * programs the device to the specified or a lower possible state
 */
static int omap3_enter_idle_bm(struct cpuidle_device *dev,
			       struct cpuidle_state *state)
{
	struct cpuidle_state *new_state = state;
	u32 per_state = 0, saved_per_state = 0, cam_state, usb_state;
	u32 iva2_state, sgx_state, dss_state, new_core_state, new_mpu_state;
	struct omap3_processor_cx *cx;
	int ret;
	int limit;

	if (state->flags & CPUIDLE_FLAG_CHECK_BM) {
		if (omap3_idle_bm_check()) {
			BUG_ON(!dev->safe_state);
			new_state = dev->safe_state;
			goto select_state;
		}
		if (cpuidle_forced_state)
			state = cpuidle_forced_state;

		limit = pm_optimizer_cpuidle_limit();
		while (state->exit_latency > limit && state != dev->safe_state)
				state--;
		cx = cpuidle_get_statedata(state);
		new_core_state = cx->core_state;
		new_mpu_state = cx->mpu_state;

		if (!enable_off_mode) {
			if (new_mpu_state < PWRDM_POWER_RET)
				new_mpu_state = PWRDM_POWER_RET;
			if (new_core_state < PWRDM_POWER_RET)
				new_core_state = PWRDM_POWER_RET;
		}

		/* Check if CORE is active, if yes, fallback to inactive */
		if (!pwrdm_can_idle(core_pd))
			new_core_state = PWRDM_POWER_INACTIVE;

		/*
		 * Prevent idle completely if CAM is active.
		 * CAM does not have wakeup capability in OMAP3.
		 */
		cam_state = pwrdm_read_pwrst(cam_pd);
		if (cam_state == PWRDM_POWER_ON) {
			new_state = dev->safe_state;
			goto select_state;
		}

		dss_state = pwrdm_read_pwrst(dss_pd);
		if (dss_state == PWRDM_POWER_ON)
			new_core_state = PWRDM_POWER_INACTIVE;

		if (new_core_state == PWRDM_POWER_OFF && disable_core_off)
			new_core_state = PWRDM_POWER_RET;

		/*
		 * Check if PER can idle or not. If we are not likely
		 * to idle, deny PER off. This prevents unnecessary
		 * context save/restore.
		 */
		saved_per_state = omap3_pwrdm_read_next_pwrst(per_pd);
		if (pwrdm_can_idle(per_pd)) {
			per_state = saved_per_state;
			/*
			 * Prevent PER off if CORE is active as this
			 * would disable PER wakeups completely
			 */
			if (per_state == PWRDM_POWER_OFF &&
			    new_core_state > PWRDM_POWER_OFF)
				per_state = PWRDM_POWER_RET;

		} else if (saved_per_state == PWRDM_POWER_OFF)
			per_state = PWRDM_POWER_RET;
		else
			per_state = saved_per_state;

		/*
		 * If we are attempting CORE off, check if any other
		 * powerdomains are at retention or higher. CORE off causes
		 * chipwide reset which would reset these domains also.
		 */
		if (new_core_state == PWRDM_POWER_OFF) {
			iva2_state = pwrdm_read_pwrst(iva2_pd);
			sgx_state = pwrdm_read_pwrst(sgx_pd);
			usb_state = pwrdm_read_pwrst(usb_pd);

			if (cam_state > PWRDM_POWER_OFF ||
			    dss_state > PWRDM_POWER_OFF ||
			    iva2_state > PWRDM_POWER_OFF ||
			    per_state > PWRDM_POWER_OFF ||
			    sgx_state > PWRDM_POWER_OFF ||
			    usb_state > PWRDM_POWER_OFF)
				new_core_state = PWRDM_POWER_RET;
		}

		/* Fallback to new target core/mpu state */
		while (cx->core_state < new_core_state ||
		       cx->mpu_state < new_mpu_state) {
			state--;
			cx = cpuidle_get_statedata(state);
		}

		/*
		 * Check if we are able to enter the forced state, if not
		 * fallback to the safe state
		 */
		if (cpuidle_forced_state &&
		    cpuidle_forced_state != state) {
			new_state = dev->safe_state;
			goto select_state;
		}

		new_state = state;

		/* Are we changing PER target state? */
		if (per_state != saved_per_state)
			omap3_pwrdm_set_next_pwrst(per_pd, per_state);
	}

select_state:
	dev->last_state = new_state;
	ret = omap3_enter_idle(dev, new_state);

	/* Restore potentially tampered PER state */
	if (per_state != saved_per_state)
		omap3_pwrdm_set_next_pwrst(per_pd, saved_per_state);

	return ret;
}

static void omap3_pm_cpuidle_check_secure_ram_save(struct cpuidle_device *dev)
{
	if (omap3_check_secure_ram_dirty()) {
		dev->states[OMAP3_STATE_C7].flags |= CPUIDLE_FLAG_IGNORE;
		dev->states[OMAP3_STATE_C8].flags &= ~CPUIDLE_FLAG_IGNORE;
	} else {
		dev->states[OMAP3_STATE_C7].flags &= ~CPUIDLE_FLAG_IGNORE;
		dev->states[OMAP3_STATE_C8].flags |= CPUIDLE_FLAG_IGNORE;
	}

	return;
}

DEFINE_PER_CPU(struct cpuidle_device, omap3_idle_dev);

void omap3_pm_init_cpuidle(struct cpuidle_params *cpuidle_board_params)
{
	int i;

	if (!cpuidle_board_params)
		return;

	for (i = OMAP3_STATE_C1; i < OMAP3_MAX_STATES; i++) {
		cpuidle_params_table[i].valid =
			cpuidle_board_params[i].valid;
		cpuidle_params_table[i].sleep_latency =
			cpuidle_board_params[i].sleep_latency;
		cpuidle_params_table[i].wake_latency =
			cpuidle_board_params[i].wake_latency;
		cpuidle_params_table[i].threshold =
			cpuidle_board_params[i].threshold;
	}
	return;
}

int omap3_pm_force_cpuidle_state(int state)
{
	struct cpuidle_device *dev;
	struct cpuidle_state *st;
	int i;

	dev = &per_cpu(omap3_idle_dev, smp_processor_id());

	if (state == 0) {
		cpuidle_forced_state = NULL;
		return 0;
	} else {
		for (i = 0; i < dev->state_count; i++) {
			st = &dev->states[i];
			if (st->name[1] == '0' + state) {
				cpuidle_forced_state = st;
				return 0;
			}
		}
	}

	return -EINVAL;
}

/* omap3_init_power_states - Initialises the OMAP3 specific C states.
 *
 * Below is the desciption of each C state.
 * 	C1 . MPU WFI + Core active
 *	C2 . MPU WFI + Core inactive
 *	C3 . MPU CSWR + Core inactive
 *	C4 . MPU OFF + Core inactive
 *	C5 . MPU CSWR + Core CSWR
 *	C6 . MPU OFF + Core CSWR
 *	C7 . MPU OFF + Core OFF
 */
void omap_init_power_states(void)
{
	/* C1 . MPU WFI + Core active */
	omap3_power_states[OMAP3_STATE_C1].valid =
			cpuidle_params_table[OMAP3_STATE_C1].valid;
	omap3_power_states[OMAP3_STATE_C1].type = OMAP3_STATE_C1;
	omap3_power_states[OMAP3_STATE_C1].sleep_latency =
			cpuidle_params_table[OMAP3_STATE_C1].sleep_latency;
	omap3_power_states[OMAP3_STATE_C1].wakeup_latency =
			cpuidle_params_table[OMAP3_STATE_C1].wake_latency;
	omap3_power_states[OMAP3_STATE_C1].threshold =
			cpuidle_params_table[OMAP3_STATE_C1].threshold;
	omap3_power_states[OMAP3_STATE_C1].mpu_state = PWRDM_POWER_ON;
	omap3_power_states[OMAP3_STATE_C1].core_state = PWRDM_POWER_ON;
	omap3_power_states[OMAP3_STATE_C1].flags = CPUIDLE_FLAG_TIME_VALID;

	/* C2 . MPU WFI + Core inactive */
	omap3_power_states[OMAP3_STATE_C2].valid =
			cpuidle_params_table[OMAP3_STATE_C2].valid;
	omap3_power_states[OMAP3_STATE_C2].type = OMAP3_STATE_C2;
	omap3_power_states[OMAP3_STATE_C2].sleep_latency =
			cpuidle_params_table[OMAP3_STATE_C2].sleep_latency;
	omap3_power_states[OMAP3_STATE_C2].wakeup_latency =
			cpuidle_params_table[OMAP3_STATE_C2].wake_latency;
	omap3_power_states[OMAP3_STATE_C2].threshold =
			cpuidle_params_table[OMAP3_STATE_C2].threshold;
	omap3_power_states[OMAP3_STATE_C2].mpu_state = PWRDM_POWER_INACTIVE;
	omap3_power_states[OMAP3_STATE_C2].core_state = PWRDM_POWER_INACTIVE;
	omap3_power_states[OMAP3_STATE_C2].flags = CPUIDLE_FLAG_TIME_VALID |
				CPUIDLE_FLAG_CHECK_BM;

	/* C3 . MPU CSWR + Core inactive */
	omap3_power_states[OMAP3_STATE_C3].valid =
			cpuidle_params_table[OMAP3_STATE_C3].valid;
	omap3_power_states[OMAP3_STATE_C3].type = OMAP3_STATE_C3;
	omap3_power_states[OMAP3_STATE_C3].sleep_latency =
			cpuidle_params_table[OMAP3_STATE_C3].sleep_latency;
	omap3_power_states[OMAP3_STATE_C3].wakeup_latency =
			cpuidle_params_table[OMAP3_STATE_C3].wake_latency;
	omap3_power_states[OMAP3_STATE_C3].threshold =
			cpuidle_params_table[OMAP3_STATE_C3].threshold;
	omap3_power_states[OMAP3_STATE_C3].mpu_state = PWRDM_POWER_RET;
	omap3_power_states[OMAP3_STATE_C3].core_state = PWRDM_POWER_INACTIVE;
	omap3_power_states[OMAP3_STATE_C3].flags = CPUIDLE_FLAG_TIME_VALID |
				CPUIDLE_FLAG_CHECK_BM;

	/* C4 . MPU OFF + Core inactive */
	omap3_power_states[OMAP3_STATE_C4].valid =
			cpuidle_params_table[OMAP3_STATE_C4].valid;
	omap3_power_states[OMAP3_STATE_C4].type = OMAP3_STATE_C4;
	omap3_power_states[OMAP3_STATE_C4].sleep_latency =
			cpuidle_params_table[OMAP3_STATE_C4].sleep_latency;
	omap3_power_states[OMAP3_STATE_C4].wakeup_latency =
			cpuidle_params_table[OMAP3_STATE_C4].wake_latency;
	omap3_power_states[OMAP3_STATE_C4].threshold =
			cpuidle_params_table[OMAP3_STATE_C4].threshold;
	omap3_power_states[OMAP3_STATE_C4].mpu_state = PWRDM_POWER_OFF;
	omap3_power_states[OMAP3_STATE_C4].core_state = PWRDM_POWER_INACTIVE;
	omap3_power_states[OMAP3_STATE_C4].flags = CPUIDLE_FLAG_TIME_VALID |
				CPUIDLE_FLAG_CHECK_BM;

	/* C5 . MPU CSWR + Core CSWR*/
	omap3_power_states[OMAP3_STATE_C5].valid =
			cpuidle_params_table[OMAP3_STATE_C5].valid;
	omap3_power_states[OMAP3_STATE_C5].type = OMAP3_STATE_C5;
	omap3_power_states[OMAP3_STATE_C5].sleep_latency =
			cpuidle_params_table[OMAP3_STATE_C5].sleep_latency;
	omap3_power_states[OMAP3_STATE_C5].wakeup_latency =
			cpuidle_params_table[OMAP3_STATE_C5].wake_latency;
	omap3_power_states[OMAP3_STATE_C5].threshold =
			cpuidle_params_table[OMAP3_STATE_C5].threshold;
	omap3_power_states[OMAP3_STATE_C5].mpu_state = PWRDM_POWER_RET;
	omap3_power_states[OMAP3_STATE_C5].core_state = PWRDM_POWER_RET;
	omap3_power_states[OMAP3_STATE_C5].flags = CPUIDLE_FLAG_TIME_VALID |
				CPUIDLE_FLAG_CHECK_BM;

	/* C6 . MPU OFF + Core CSWR */
	omap3_power_states[OMAP3_STATE_C6].valid =
			cpuidle_params_table[OMAP3_STATE_C6].valid;
	omap3_power_states[OMAP3_STATE_C6].type = OMAP3_STATE_C6;
	omap3_power_states[OMAP3_STATE_C6].sleep_latency =
			cpuidle_params_table[OMAP3_STATE_C6].sleep_latency;
	omap3_power_states[OMAP3_STATE_C6].wakeup_latency =
			cpuidle_params_table[OMAP3_STATE_C6].wake_latency;
	omap3_power_states[OMAP3_STATE_C6].threshold =
			cpuidle_params_table[OMAP3_STATE_C6].threshold;
	omap3_power_states[OMAP3_STATE_C6].mpu_state = PWRDM_POWER_OFF;
	omap3_power_states[OMAP3_STATE_C6].core_state = PWRDM_POWER_RET;
	omap3_power_states[OMAP3_STATE_C6].flags = CPUIDLE_FLAG_TIME_VALID |
				CPUIDLE_FLAG_CHECK_BM;

	/* C7 . MPU OFF + Core OFF */
	omap3_power_states[OMAP3_STATE_C7].valid =
			cpuidle_params_table[OMAP3_STATE_C7].valid;
	omap3_power_states[OMAP3_STATE_C7].type = OMAP3_STATE_C7;
	omap3_power_states[OMAP3_STATE_C7].sleep_latency =
			cpuidle_params_table[OMAP3_STATE_C7].sleep_latency;
	omap3_power_states[OMAP3_STATE_C7].wakeup_latency =
			cpuidle_params_table[OMAP3_STATE_C7].wake_latency;
	omap3_power_states[OMAP3_STATE_C7].threshold =
			cpuidle_params_table[OMAP3_STATE_C7].threshold;
	omap3_power_states[OMAP3_STATE_C7].mpu_state = PWRDM_POWER_OFF;
	omap3_power_states[OMAP3_STATE_C7].core_state = PWRDM_POWER_OFF;
	omap3_power_states[OMAP3_STATE_C7].flags = CPUIDLE_FLAG_TIME_VALID |
				CPUIDLE_FLAG_CHECK_BM;

	/* C8 . MPU OFF + Core OFF */
	omap3_power_states[OMAP3_STATE_C8].valid =
			cpuidle_params_table[OMAP3_STATE_C8].valid;
	omap3_power_states[OMAP3_STATE_C8].type = OMAP3_STATE_C8;
	omap3_power_states[OMAP3_STATE_C8].sleep_latency =
			cpuidle_params_table[OMAP3_STATE_C8].sleep_latency;
	omap3_power_states[OMAP3_STATE_C8].wakeup_latency =
			cpuidle_params_table[OMAP3_STATE_C8].wake_latency;
	omap3_power_states[OMAP3_STATE_C8].threshold =
			cpuidle_params_table[OMAP3_STATE_C8].threshold;
	omap3_power_states[OMAP3_STATE_C8].mpu_state = PWRDM_POWER_OFF;
	omap3_power_states[OMAP3_STATE_C8].core_state = PWRDM_POWER_OFF;
	omap3_power_states[OMAP3_STATE_C8].flags = CPUIDLE_FLAG_TIME_VALID |
				CPUIDLE_FLAG_CHECK_BM;
}

struct cpuidle_driver omap3_idle_driver = {
	.name = 	"omap3_idle",
	.owner = 	THIS_MODULE,
};

/**
 * omap3_idle_init - Init routine for OMAP3 idle
 *
 * Registers the OMAP3 specific cpuidle driver with the cpuidle
 * framework with the valid set of states.
 */
int __init omap3_idle_init(void)
{
	int i, count = 0;
	struct omap3_processor_cx *cx;
	struct cpuidle_state *state;
	struct cpuidle_device *dev;

	mpu_pd = pwrdm_lookup("mpu_pwrdm");
	core_pd = pwrdm_lookup("core_pwrdm");
	per_pd = pwrdm_lookup("per_pwrdm");
	iva2_pd = pwrdm_lookup("iva2_pwrdm");
	sgx_pd = pwrdm_lookup("sgx_pwrdm");
	usb_pd = pwrdm_lookup("usbhost_pwrdm");
	cam_pd = pwrdm_lookup("cam_pwrdm");
	dss_pd = pwrdm_lookup("dss_pwrdm");

	omap_init_power_states();
	cpuidle_register_driver(&omap3_idle_driver);

	dev = &per_cpu(omap3_idle_dev, smp_processor_id());

	for (i = OMAP3_STATE_C1; i < OMAP3_MAX_STATES; i++) {
		cx = &omap3_power_states[i];
		state = &dev->states[count];

		if (!cx->valid)
			continue;
		cpuidle_set_statedata(state, cx);
		state->exit_latency = cx->sleep_latency + cx->wakeup_latency;
		state->target_residency = cx->threshold;
		state->flags = cx->flags;
		state->enter = (state->flags & CPUIDLE_FLAG_CHECK_BM) ?
			omap3_enter_idle_bm : omap3_enter_idle;
		if (cx->type == OMAP3_STATE_C1)
			dev->safe_state = state;
		sprintf(state->name, "C%d", cx->type + 1);
		count++;
	}

	if (!count)
		return -EINVAL;
	dev->state_count = count;

	dev->prepare = omap3_pm_cpuidle_check_secure_ram_save;

	if (cpuidle_register_device(dev)) {
		printk(KERN_ERR "%s: CPUidle register device failed\n",
		       __func__);
		return -EIO;
	}

	return 0;
}
#else
int __init omap3_idle_init(void)
{
	return 0;
}
#endif /* CONFIG_CPU_IDLE */
