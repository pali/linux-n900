/*
 * omap-pm-srf.c - OMAP power management interface implemented
 * using Shared resource framework
 *
 * This code implements the OMAP power management interface to
 * drivers, CPUIdle, CPUFreq, and DSP Bridge.  It is strictly for
 * debug/demonstration use, as it does nothing but printk() whenever a
 * function is called (when DEBUG is defined, below)
 *
 * Copyright (C) 2008 Texas Instruments, Inc.
 * Copyright (C) 2008 Nokia Corporation
 * Paul Walmsley
 *
 * Interface developed by (in alphabetical order):
 * Karthik Dasu, Tony Lindgren, Rajendra Nayak, Sakari Poussa, Veeramanikandan
 * Raju, Anand Sawant, Igor Stoppa, Paul Walmsley, Richard Woodruff
 */

#undef DEBUG

#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/device.h>
#include <linux/module.h>

#include <mach/omap-pm.h>
#include <mach/powerdomain.h>
#include <mach/resource.h>
#include <mach/omapdev.h>
#include <mach/pm.h>

struct omap_opp *dsp_opps;
struct omap_opp *mpu_opps;
struct omap_opp *l3_opps;

#define LAT_RES_POSTAMBLE "_latency"
#define MAX_LATENCY_RES_NAME 30

/**
 * get_lat_res_name - gets the latency resource name given a power domain name
 * @pwrdm_name: Name of the power domain.
 * @lat_name: Buffer in which latency resource name is populated
 * @size: Max size of the latency resource name
 *
 * Returns the latency resource name populated in lat_name.
 */
void get_lat_res_name(const char *pwrdm_name, char **lat_name, int size)
{
	strcpy(*lat_name, "");
	WARN_ON(strlen(pwrdm_name) + strlen(LAT_RES_POSTAMBLE) > size);
	strcpy(*lat_name, pwrdm_name);
	strcat(*lat_name, LAT_RES_POSTAMBLE);
	return;
}

/*
 * Device-driver-originated constraints (via board-*.c files)
 */

void omap_pm_set_max_mpu_wakeup_lat(struct device *dev, long t)
{
	if (!dev || t < -1) {
		WARN_ON(1);
		return;
	};

	if (t == -1) {
		pr_debug("OMAP PM: remove max MPU wakeup latency constraint: "
			 "dev %s\n", dev_name(dev));
		resource_release("mpu_latency", dev);
	} else {
		pr_debug("OMAP PM: add max MPU wakeup latency constraint: "
			 "dev %s, t = %ld usec\n", dev_name(dev), t);
		resource_request("mpu_latency", dev, t);
	}
}

void omap_pm_set_min_mpu_freq(struct device *dev, unsigned long r)
{
	if (!dev) {
		WARN_ON(1);
		return;
	}

	if (r == 0)
		resource_release("mpu_freq", dev);
	else
		resource_request("mpu_freq", dev, r);
}
EXPORT_SYMBOL(omap_pm_set_min_mpu_freq);

void omap_pm_set_min_bus_tput(struct device *dev, u8 agent_id, unsigned long r)
{
	if (!dev || (agent_id != OCP_INITIATOR_AGENT &&
	    agent_id != OCP_TARGET_AGENT)) {
		WARN_ON(1);
		return;
	};

	if (r == 0) {
		pr_debug("OMAP PM: remove min bus tput constraint: "
			 "dev %s for agent_id %d\n", dev_name(dev), agent_id);
		resource_release("vdd2_opp", dev);
	} else {
		pr_debug("OMAP PM: add min bus tput constraint: "
			 "dev %s for agent_id %d: rate %ld KiB\n",
			 dev_name(dev), agent_id, r);
		resource_request("vdd2_opp", dev, r);
	}
}
EXPORT_SYMBOL(omap_pm_set_min_bus_tput);

void omap_pm_set_max_dev_wakeup_lat(struct device *dev, long t)
{
	struct omapdev *odev;
	struct powerdomain *pwrdm_dev;
	struct platform_device *pdev;
	char *lat_res_name;

	if (!dev || t < -1) {
		WARN_ON(1);
		return;
	};
	/* Look for the devices Power Domain */
	/*
	 * WARNING! If device is not a platform device, container_of will
	 * return a pointer to unknown memory!
	 * TODO: Either change omap-pm interface to support only platform
	 * devices, or change the underlying omapdev implementation to
	 * support normal devices.
	 */
	pdev = container_of(dev, struct platform_device, dev);

	/* Try to catch non platform devices. */
	if (pdev->name == NULL) {
		printk(KERN_ERR "OMAP-PM: Error: platform device not valid\n");
		return;
	}

	odev = omapdev_find_pdev(pdev);
	if (odev) {
		pwrdm_dev = omapdev_get_pwrdm(odev);
	} else {
		printk(KERN_ERR "OMAP-PM: Error: Could not find omapdev "
						"for %s\n", pdev->name);
		return;
	}

	lat_res_name = kmalloc(MAX_LATENCY_RES_NAME, GFP_KERNEL);
	if (!lat_res_name) {
		printk(KERN_ERR "OMAP-PM: FATAL ERROR: kmalloc failed\n");
		return;
	}
	get_lat_res_name(pwrdm_dev->name, &lat_res_name, MAX_LATENCY_RES_NAME);

	if (t == -1) {
		pr_debug("OMAP PM: remove max device latency constraint: "
			 "dev %s\n", dev_name(dev));
		resource_release(lat_res_name, dev);
	} else {
		pr_debug("OMAP PM: add max device latency constraint: "
			 "dev %s, t = %ld usec\n", dev_name(dev), t);
		resource_request(lat_res_name, dev, t);
	}

	kfree(lat_res_name);
	return;
}

void omap_pm_set_max_sdma_lat(struct device *dev, long t)
{
	if (!dev || t < -1) {
		WARN_ON(1);
		return;
	};

	if (t == -1) {
		pr_debug("OMAP PM: remove max DMA latency constraint: "
			 "dev %s\n", dev_name(dev));
		resource_release("core_latency", dev);
	} else {
		pr_debug("OMAP PM: add max DMA latency constraint: "
			 "dev %s, t = %ld usec\n", dev_name(dev), t);
		resource_request("core_latency", dev, t);
	}
}

static struct device dummy_dsp_dev;

/*
 * DSP Bridge-specific constraints
 */
const struct omap_opp *omap_pm_dsp_get_opp_table(void)
{
	pr_debug("OMAP PM: DSP request for OPP table\n");

	/*
	 * Return DSP frequency table here:  The final item in the
	 * array should have .rate = .opp_id = 0.
	 */

	return NULL;
}
EXPORT_SYMBOL(omap_pm_dsp_get_opp_table);

void omap_pm_dsp_set_min_opp(u8 opp_id)
{
	if (opp_id == 0) {
		WARN_ON(1);
		return;
	}

	pr_debug("OMAP PM: DSP requests minimum VDD1 OPP to be %d\n", opp_id);

	/*
	 * For now pass a dummy_dev struct for SRF to identify the caller.
	 * Maybe its good to have DSP pass this as an argument
	 */
	resource_request("vdd1_opp", &dummy_dsp_dev, opp_id);
	return;
}
EXPORT_SYMBOL(omap_pm_dsp_set_min_opp);

u8 omap_pm_dsp_get_opp(void)
{
	pr_debug("OMAP PM: DSP requests current DSP OPP ID\n");
	return resource_get_level("vdd1_opp");
	return 0;
}
EXPORT_SYMBOL(omap_pm_dsp_get_opp);

/*
 * CPUFreq-originated constraint
 *
 * In the future, this should be handled by custom OPP clocktype
 * functions.
 */

struct cpufreq_frequency_table **omap_pm_cpu_get_freq_table(void)
{
	pr_debug("OMAP PM: CPUFreq request for frequency table\n");

	/*
	 * Return CPUFreq frequency table here: loop over
	 * all VDD1 clkrates, pull out the mpu_ck frequencies, build
	 * table
	 */

	return NULL;
}

static struct device dummy_cpufreq_dev;

void omap_pm_cpu_set_freq(unsigned long f)
{
	if (f == 0) {
		WARN_ON(1);
		return;
	}

	pr_debug("OMAP PM: CPUFreq requests CPU frequency to be set to %lu\n",
		 f);

	resource_request("mpu_freq", &dummy_cpufreq_dev, f);
	return;
}
EXPORT_SYMBOL(omap_pm_cpu_set_freq);

unsigned long omap_pm_cpu_get_freq(void)
{
	pr_debug("OMAP PM: CPUFreq requests current CPU frequency\n");
	return resource_get_level("mpu_freq");
}
EXPORT_SYMBOL(omap_pm_cpu_get_freq);

/*
 * Device context loss tracking
 */

int omap_pm_get_dev_context_loss_count(struct device *dev)
{
	if (!dev) {
		WARN_ON(1);
		return -EINVAL;
	};

	pr_debug("OMAP PM: returning context loss count for dev %s\n",
		 dev_name(dev));

	/*
	 * Map the device to the powerdomain.  Return the powerdomain
	 * off counter.
	 */

	return get_last_off_on_transaction_id(dev);
}

/*
 * Powerdomain usecounting hooks
 */

void omap_pm_pwrdm_active(struct powerdomain *pwrdm)
{
	if (!pwrdm) {
		WARN_ON(1);
		return;
	};

	pr_debug("OMAP PM: powerdomain %s is becoming active\n", pwrdm->name);

	/*
	 * CDP code apparently will need these for the enable_power_domain()
	 * and disable_power_domain() functions.
	 */
}

void omap_pm_pwrdm_inactive(struct powerdomain *pwrdm)
{
	if (!pwrdm) {
		WARN_ON(1);
		return;
	};

	pr_debug("OMAP PM: powerdomain %s is becoming inactive\n",
		 pwrdm->name);

	/*
	 * CDP code apparently will need these for the enable_power_domain()
	 * and disable_power_domain() functions.
	 */
}

/*
 * Should be called before clk framework since clk fw will call
 * omap_pm_pwrdm_{in,}active()
 */
int __init omap_pm_if_early_init(struct omap_opp *mpu_opp_table,
				 struct omap_opp *dsp_opp_table,
				 struct omap_opp *l3_opp_table)
{
	mpu_opps = mpu_opp_table;
	dsp_opps = dsp_opp_table;
	l3_opps = l3_opp_table;
	return 0;
}

/* Must be called after clock framework is initialized */
int __init omap_pm_if_init(void)
{
	resource_init(resources_omap);
	return 0;
}

void omap_pm_if_exit(void)
{
	/* Deallocate CPUFreq frequency table here */
}
