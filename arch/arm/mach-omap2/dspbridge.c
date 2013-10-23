/*
 * TI's dspbridge platform device registration
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 * Copyright (C) 2009 Nokia Corporation
 *
 * Written by Hiroshi DOYU <Hiroshi.DOYU@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/platform_device.h>

#ifdef CONFIG_BRIDGE_DVFS
#include <plat/omap-pm.h>
#include <plat/opp.h>
/*
 * The DSP load balancer works on the following logic:
 * Opp frequencies:
 * 0 <---------> Freq 1 <------------> Freq 2 <---------> Freq 3
 * DSP Thresholds for the frequencies:
 * 0M<-------X-> Freq 1 <-------M--X-> Freq 2 <----M--X-> Freq 3
 * Where, M is the minimal threshold and X is maximum threshold
 *
 * if from Freq x to Freq y; where x > y, transition happens on M
 * if from Freq x to Freq y; where x < y, transition happens on X
 */
#define BRIDGE_THRESH_HIGH_PERCENT	95
#define BRIDGE_THRESH_LOW_PERCENT	88
#endif

#include <dspbridge/host_os.h>

static struct platform_device *dspbridge_pdev;

static struct dspbridge_platform_data dspbridge_pdata = {
#ifdef CONFIG_BRIDGE_DVFS
	.dsp_set_min_opp = omap_pm_dsp_set_min_opp,
	.dsp_get_opp	 = omap_pm_dsp_get_opp,
	.cpu_set_freq	 = omap_pm_cpu_set_freq,
	.cpu_get_freq	 = omap_pm_cpu_get_freq,
#endif
};

/**
 * get_opp_table() - populate the pdata with opp info
 * @pdata: pointer to pdata
 *
 * OPP table implementation is a variant b/w platforms.
 * the platform file now incorporates this into the build
 * itself and uses the interface to talk to platform specific
 * functions
 */
static int __init get_opp_table(struct dspbridge_platform_data *pdata)
{
#ifdef CONFIG_BRIDGE_DVFS
	int mpu_freqs;
	int dsp_freqs;
	int i;
	struct omap_opp *opp;
	unsigned long freq, old_rate;

	mpu_freqs = opp_get_opp_count(OPP_MPU);
	dsp_freqs = opp_get_opp_count(OPP_DSP);
	if (mpu_freqs < 0 || dsp_freqs < 0 || mpu_freqs != dsp_freqs) {
		pr_err("%s:mpu and dsp frequencies are inconsistent! "
			"mpu_freqs=%d dsp_freqs=%d\n", __func__, mpu_freqs,
			dsp_freqs);
		return -EINVAL;
	}
	/* allocate memory if we have opps initialized */
	pdata->mpu_speeds = kzalloc(sizeof(u32) * mpu_freqs,
			GFP_KERNEL);
	if (!pdata->mpu_speeds) {
		pr_err("%s:unable to allocate memory for the mpu"
			"frequencies\n", __func__);
		return -ENOMEM;
	}
	i = 0;
	freq = 0;
	while (!IS_ERR(opp = opp_find_freq_ceil(OPP_MPU, &freq))) {
		pdata->mpu_speeds[i] = freq;
		freq++;
		i++;
	}
	pdata->mpu_num_speeds = mpu_freqs;
	pdata->mpu_min_speed = pdata->mpu_speeds[0];
	pdata->mpu_max_speed = pdata->mpu_speeds[mpu_freqs - 1];
	/* need an initial terminator */
	pdata->dsp_freq_table = kzalloc(
			sizeof(struct dsp_shm_freq_table) *
			(dsp_freqs + 1), GFP_KERNEL);
	if (!pdata->dsp_freq_table) {
		pr_err("%s: unable to allocate memory for the dsp"
			"frequencies\n", __func__);
		return -ENOMEM;
	}

	i = 1;
	freq = 0;
	old_rate = 0;
	/*
	 * the freq table is in terms of khz.. so we need to
	 * divide by 1000
	 */
	while (!IS_ERR(opp = opp_find_freq_ceil(OPP_DSP, &freq))) {
		/* dsp frequencies are in khz */
		u32 rate = freq / 1000;

		/*
		 * On certain 34xx silicons, certain OPPs are duplicated
		 * for DSP - handle those by copying previous opp value
		 */
		if (rate == old_rate) {
			memcpy(&pdata->dsp_freq_table[i],
				&pdata->dsp_freq_table[i-1],
				sizeof(struct dsp_shm_freq_table));
		} else {
			pdata->dsp_freq_table[i].dsp_freq = rate;
			pdata->dsp_freq_table[i].u_volts =
				opp_get_voltage(opp);
			/*
			 * min threshold:
			 * NOTE: index 1 needs a min of 0! else no
			 * scaling happens at DSP!
			 */
			pdata->dsp_freq_table[i].thresh_min_freq =
				((old_rate * BRIDGE_THRESH_LOW_PERCENT) / 100);

			/* max threshold */
			pdata->dsp_freq_table[i].thresh_max_freq =
				((rate * BRIDGE_THRESH_HIGH_PERCENT) / 100);
		}
		old_rate = rate;
		freq++;
		i++;
	}
	/* the last entry should map with maximum rate */
	pdata->dsp_freq_table[i - 1].thresh_max_freq = old_rate;
	pdata->dsp_num_speeds = dsp_freqs;
#endif
	return 0;
}

static int __init dspbridge_init(void)
{
	struct platform_device *pdev;
	int err = -ENOMEM;
	struct dspbridge_platform_data *pdata = &dspbridge_pdata;

	pdata->phys_mempool_base = dspbridge_get_mempool_base();

	if (pdata->phys_mempool_base) {
		pdata->phys_mempool_size = CONFIG_BRIDGE_MEMPOOL_SIZE;
		pr_info("%s: %x bytes @ %x\n", __func__,
			pdata->phys_mempool_size, pdata->phys_mempool_base);
	}

	pdev = platform_device_alloc("C6410", -1);
	if (!pdev)
		goto err_out;
	err = get_opp_table(pdata);
	if (err)
		goto err_out;


	err = platform_device_add_data(pdev, pdata, sizeof(*pdata));
	if (err)
		goto err_out;

	err = platform_device_add(pdev);
	if (err)
		goto err_out;

	dspbridge_pdev = pdev;
	return 0;

err_out:
	kfree(pdata->mpu_speeds);
	kfree(pdata->dsp_freq_table);
	pdata->mpu_speeds = NULL;
	pdata->dsp_freq_table = NULL;
	platform_device_put(pdev);
	return err;
}
module_init(dspbridge_init);

static void __exit dspbridge_exit(void)
{
	struct dspbridge_platform_data *pdata = &dspbridge_pdata;
	kfree(pdata->mpu_speeds);
	kfree(pdata->dsp_freq_table);
	pdata->mpu_speeds = NULL;
	pdata->dsp_freq_table = NULL;
	platform_device_unregister(dspbridge_pdev);
}
module_exit(dspbridge_exit);

MODULE_AUTHOR("Hiroshi DOYU");
MODULE_DESCRIPTION("TI's dspbridge platform device registration");
MODULE_LICENSE("GPL v2");
