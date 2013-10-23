/*
 * linux/arch/arm/mach-omap2/resource34xx.c
 * OMAP3 resource init/change_level/validate_level functions
 *
 * Copyright (C) 2007-2008 Texas Instruments, Inc.
 * Rajendra Nayak <rnayak@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 * History:
 *
 */

#include <linux/pm_qos_params.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <mach/powerdomain.h>
#include <mach/clockdomain.h>
#include <mach/control.h>
#include "smartreflex.h"
#include "resource34xx.h"
#include "pm.h"
#include "cm.h"
#include "cm-regbits-34xx.h"

/**
 * init_latency - Initializes the mpu/core latency resource.
 * @resp: Latency resource to be initalized
 *
 * No return value.
 */
void init_latency(struct shared_resource *resp)
{
	resp->no_of_users = 0;
	resp->curr_level = RES_DEFAULTLEVEL;
	*((u8 *)resp->resource_data) = 0;
	return;
}

/**
 * set_latency - Adds/Updates and removes the CPU_DMA_LATENCY in *pm_qos_params.
 * @resp: resource pointer
 * @latency: target latency to be set
 *
 * Returns 0 on success, or error values as returned by
 * pm_qos_update_requirement/pm_qos_add_requirement.
 */
int set_latency(struct shared_resource *resp, u32 latency)
{
	u8 *pm_qos_req_added;

	if (resp->curr_level == latency)
		return 0;
	else
		/* Update the resources current level */
		resp->curr_level = latency;

	pm_qos_req_added = resp->resource_data;
	if (latency == RES_DEFAULTLEVEL)
		/* No more users left, remove the pm_qos_req if present */
		if (*pm_qos_req_added) {
			pm_qos_remove_requirement(PM_QOS_CPU_DMA_LATENCY,
							resp->name);
			*pm_qos_req_added = 0;
			return 0;
		}

	if (*pm_qos_req_added) {
		return pm_qos_update_requirement(PM_QOS_CPU_DMA_LATENCY,
						resp->name, latency);
	} else {
		*pm_qos_req_added = 1;
		return pm_qos_add_requirement(PM_QOS_CPU_DMA_LATENCY,
						resp->name, latency);
	}
}

/**
 * init_pd_latency - Initializes the power domain latency resource.
 * @resp: Power Domain Latency resource to be initialized.
 *
 * No return value.
 */
void init_pd_latency(struct shared_resource *resp)
{
	struct pd_latency_db *pd_lat_db;

	resp->no_of_users = 0;
	if (enable_off_mode)
		resp->curr_level = PD_LATENCY_OFF;
	else
		resp->curr_level = PD_LATENCY_RET;
	pd_lat_db = resp->resource_data;
	/* Populate the power domain associated with the latency resource */
	pd_lat_db->pd = pwrdm_lookup(pd_lat_db->pwrdm_name);
	set_pwrdm_state(pd_lat_db->pd, resp->curr_level);
	return;
}

/**
 * set_pd_latency - Updates the curr_level of the power domain resource.
 * @resp: Power domain latency resource.
 * @latency: New latency value acceptable.
 *
 * This function maps the latency in microsecs to the acceptable
 * Power domain state using the latency DB.
 * It then programs the power domain to enter the target state.
 * Always returns 0.
 */
int set_pd_latency(struct shared_resource *resp, u32 latency)
{
	u32 pd_lat_level, ind;
	struct pd_latency_db *pd_lat_db;
	struct powerdomain *pwrdm;

	pd_lat_db = resp->resource_data;
	pwrdm = pd_lat_db->pd;
	pd_lat_level = PD_LATENCY_OFF;
	/* using the latency db map to the appropriate PD state */
	for (ind = 0; ind < PD_LATENCY_MAXLEVEL; ind++) {
		if (pd_lat_db->latency[ind] < latency) {
			pd_lat_level = ind;
			break;
		}
	}

	if (!enable_off_mode && pd_lat_level == PD_LATENCY_OFF)
		pd_lat_level = PD_LATENCY_RET;

	resp->curr_level = pd_lat_level;
	set_pwrdm_state(pwrdm, pd_lat_level);
	return 0;
}

static struct shared_resource *vdd1_resp;
static struct shared_resource *vdd2_resp;
static struct device dummy_mpu_dev;
static struct device dummy_dsp_dev;
static struct device vdd2_dev;
static int vdd1_lock;
static int vdd2_lock;
static struct clk *dpll1_clk, *dpll2_clk, *dpll3_clk;
static int curr_vdd1_opp;
static int curr_vdd2_opp;

DEFINE_MUTEX(dvfs_mutex);

static unsigned short get_opp(struct omap_opp *opp_freq_table,
		unsigned long freq)
{
	struct omap_opp *prcm_config;
	prcm_config = opp_freq_table;

	if (prcm_config->rate <= freq)
		return prcm_config->opp_id; /* Return the Highest OPP */
	for (; prcm_config->rate; prcm_config--)
		if (prcm_config->rate < freq)
			return (prcm_config+1)->opp_id;
		else if (prcm_config->rate == freq)
			return prcm_config->opp_id;
	/* Return the least OPP */
	return (prcm_config+1)->opp_id;
}

/**
 * init_opp - Initialize the OPP resource
 */
void init_opp(struct shared_resource *resp)
{
	resp->no_of_users = 0;

	if (!mpu_opps || !dsp_opps || !l3_opps)
		return;

	/* Initialize the current level of the OPP resource
	* to the  opp set by u-boot.
	*/
	if (strcmp(resp->name, "vdd1_opp") == 0) {
		vdd1_resp = resp;
		dpll1_clk = clk_get(NULL, "dpll1_ck");
		dpll2_clk = clk_get(NULL, "dpll2_ck");
		resp->curr_level = get_opp(mpu_opps + MAX_VDD1_OPP,
				dpll1_clk->rate);
		curr_vdd1_opp = resp->curr_level;
	} else if (strcmp(resp->name, "vdd2_opp") == 0) {
		vdd2_resp = resp;
		dpll3_clk = clk_get(NULL, "dpll3_m2_ck");
		resp->curr_level = get_opp(l3_opps + MAX_VDD2_OPP,
				dpll2_clk->rate);
		curr_vdd2_opp = resp->curr_level;
	}
	return;
}

int resource_access_opp_lock(int res, int delta)
{
	if (res == PRCM_VDD1) {
		vdd1_lock += delta;
		return vdd1_lock;
	} else if (res == PRCM_VDD2) {
		vdd2_lock += delta;
		return vdd2_lock;
	}
	return -EINVAL;
}

#ifndef CONFIG_CPU_FREQ
static unsigned long compute_lpj(unsigned long ref, u_int div, u_int mult)
{
	unsigned long new_jiffy_l, new_jiffy_h;

	/*
	 * Recalculate loops_per_jiffy.  We do it this way to
	 * avoid math overflow on 32-bit machines.  Maybe we
	 * should make this architecture dependent?  If you have
	 * a better way of doing this, please replace!
	 *
	 *    new = old * mult / div
	 */
	new_jiffy_h = ref / div;
	new_jiffy_l = (ref % div) / 100;
	new_jiffy_h *= mult;
	new_jiffy_l = new_jiffy_l * mult / div;

	return new_jiffy_h + new_jiffy_l * 100;
}
#endif

static int program_opp_freq(int res, int target_level, int current_level)
{
	int ret = 0, l3_div;
	int *curr_opp;

	if (res == PRCM_VDD1) {
		curr_opp = &curr_vdd1_opp;
		clk_set_rate(dpll1_clk, mpu_opps[target_level].rate);
		clk_set_rate(dpll2_clk, dsp_opps[target_level].rate);
#ifndef CONFIG_CPU_FREQ
		/*Update loops_per_jiffy if processor speed is being changed*/
		loops_per_jiffy = compute_lpj(loops_per_jiffy,
			mpu_opps[current_level].rate/1000,
			mpu_opps[target_level].rate/1000);
#endif
	} else {
		curr_opp = &curr_vdd2_opp;
		l3_div = cm_read_mod_reg(CORE_MOD, CM_CLKSEL) &
			OMAP3430_CLKSEL_L3_MASK;
		ret = clk_set_rate(dpll3_clk,
				l3_opps[target_level].rate * l3_div);
	}
	if (ret)
		return current_level;
#ifdef CONFIG_PM
	omap3_save_scratchpad_contents();
#endif
	*curr_opp = target_level;
	return target_level;
}

static int program_opp(int res, struct omap_opp *opp, int target_level,
		int current_level)
{
	int i, ret = 0, raise, sr_status;
#ifdef CONFIG_OMAP_SMARTREFLEX
	unsigned long t_opp, c_opp;

	t_opp = ID_VDD(res) | ID_OPP_NO(opp[target_level].opp_id);
	c_opp = ID_VDD(res) | ID_OPP_NO(opp[current_level].opp_id);
#endif
	if (target_level > current_level)
		raise = 1;
	else
		raise = 0;

#ifdef CONFIG_OMAP_SMARTREFLEX
	sr_status = sr_stop_vddautocomap((get_vdd(t_opp) == PRCM_VDD1) ?
			SR1 : SR2);
#endif
	for (i = 0; i < 2; i++) {
		if (i == raise)
			ret = program_opp_freq(res, target_level,
					current_level);
#ifdef CONFIG_OMAP_SMARTREFLEX
		else
			sr_voltagescale_vcbypass(t_opp, c_opp,
				opp[target_level].vsel,
				opp[current_level].vsel);
#endif
	}
#ifdef CONFIG_OMAP_SMARTREFLEX
	if (sr_status)
		sr_start_vddautocomap((get_vdd(t_opp) == PRCM_VDD1) ? SR1 : SR2,
				opp[target_level].opp_id);
#endif

	return ret;
}

int resource_set_opp_level(int res, u32 target_level, int flags)
{
	unsigned long mpu_freq, mpu_old_freq;
#ifdef CONFIG_CPU_FREQ
	struct cpufreq_freqs freqs_notify;
#endif
	struct shared_resource *resp;

	if (res == PRCM_VDD1)
		resp = vdd1_resp;
	else if (res == PRCM_VDD2)
		resp = vdd2_resp;
	else
		return 0;

	if (resp->curr_level == target_level)
		return 0;

	if (!mpu_opps || !dsp_opps || !l3_opps)
		return 0;

	mutex_lock(&dvfs_mutex);

	if (res == PRCM_VDD1) {
		if (!(flags & OPP_IGNORE_LOCK) && vdd1_lock) {
			mutex_unlock(&dvfs_mutex);
			return 0;
		}
		mpu_old_freq = mpu_opps[resp->curr_level].rate;
		mpu_freq = mpu_opps[target_level].rate;
#ifdef CONFIG_CPU_FREQ
		freqs_notify.old = mpu_old_freq/1000;
		freqs_notify.new = mpu_freq/1000;
		freqs_notify.cpu = 0;
		/* Send pre notification to CPUFreq */
		cpufreq_notify_transition(&freqs_notify, CPUFREQ_PRECHANGE);
#endif

		resp->curr_level = program_opp(res, mpu_opps, target_level,
			resp->curr_level);

#ifdef CONFIG_CPU_FREQ
		/* Send a post notification to CPUFreq */
		cpufreq_notify_transition(&freqs_notify, CPUFREQ_POSTCHANGE);
#endif
	} else {
		if (!(flags & OPP_IGNORE_LOCK) && vdd2_lock) {
			mutex_unlock(&dvfs_mutex);
			return 0;
		}

		resp->curr_level = program_opp(res, l3_opps, target_level,
			resp->curr_level);
	}
	mutex_unlock(&dvfs_mutex);
	return 0;
}

int set_opp(struct shared_resource *resp, u32 target_level)
{
	unsigned long tput;
	unsigned long req_l3_freq;
	int ind;

	if (resp == vdd1_resp) {
		if (target_level < 3)
			resource_release_locked("vdd2_opp", &vdd2_dev);

		resource_set_opp_level(PRCM_VDD1, target_level, 0);
		/*
		 * For VDD1 OPP3 and above, make sure the interconnect
		 * is at 100Mhz or above.
		 * throughput in KiB/s for 100 Mhz = 100 * 1000 * 4.
		 */
		if (target_level >= 3)
			resource_request_locked("vdd2_opp", &vdd2_dev, 400000);
	} else if (resp == vdd2_resp) {
		tput = target_level;

		/* Convert the tput in KiB/s to Bus frequency in MHz */
		req_l3_freq = (tput * 1000)/4;

		for (ind = 2; ind <= MAX_VDD2_OPP; ind++)
			if ((l3_opps + ind)->rate >= req_l3_freq) {
				target_level = ind;
				break;
			}

		/* Set the highest OPP possible */
		if (ind > MAX_VDD2_OPP)
			target_level = ind-1;
		resource_set_opp_level(PRCM_VDD2, target_level, 0);
	}
	return 0;
}

/**
 * validate_opp - Validates if valid VDD1 OPP's are passed as the
 * target_level.
 * VDD2 OPP levels are passed as L3 throughput, which are then mapped
 * to an appropriate OPP.
 */
int validate_opp(struct shared_resource *resp, u32 target_level)
{
	return 0;
}

/**
 * init_freq - Initialize the frequency resource.
 */
void init_freq(struct shared_resource *resp)
{
	char *linked_res_name;
	resp->no_of_users = 0;

	if (!mpu_opps || !dsp_opps)
		return;

	linked_res_name = (char *)resp->resource_data;
	/* Initialize the current level of the Freq resource
	* to the frequency set by u-boot.
	*/
	if (strcmp(resp->name, "mpu_freq") == 0)
		/* MPU freq in Mhz */
		resp->curr_level = mpu_opps[curr_vdd1_opp].rate;
	else if (strcmp(resp->name, "dsp_freq") == 0)
		/* DSP freq in Mhz */
		resp->curr_level = dsp_opps[curr_vdd1_opp].rate;
	return;
}

int set_freq(struct shared_resource *resp, u32 target_level)
{
	unsigned int vdd1_opp;

	if (!mpu_opps || !dsp_opps)
		return 0;

	if (strcmp(resp->name, "mpu_freq") == 0) {
		vdd1_opp = get_opp(mpu_opps + MAX_VDD1_OPP, target_level);
		resource_request_locked("vdd1_opp", &dummy_mpu_dev, vdd1_opp);
	} else if (strcmp(resp->name, "dsp_freq") == 0) {
		vdd1_opp = get_opp(dsp_opps + MAX_VDD1_OPP, target_level);
		resource_request_locked("vdd1_opp", &dummy_dsp_dev, vdd1_opp);
	}
	resp->curr_level = target_level;
	return 0;
}

int validate_freq(struct shared_resource *resp, u32 target_level)
{
	return 0;
}
