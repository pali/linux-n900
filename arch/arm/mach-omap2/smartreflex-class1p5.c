/*
 * Smart reflex Class 1.5 specific implementations
 *
 * Copyright (C) 2010 Texas Instruments, Inc.
 * Nishanth Menon <nm@ti.com>
 *
 * Smart reflex class 1.5 is also called periodic SW Calibration
 * Some of the highlights are as follows:
 * – Host CPU triggers OPP calibration when transitioning to non calibrated
 *   OPP
 * – SR-AVS + VP modules are used to perform calibration
 * – Once completed, the SmartReflex-AVS module can be disabled
 * – Enables savings based on process, supply DC accuracy and aging
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/kobject.h>
#include <linux/workqueue.h>
#include <plat/opp.h>
#include <plat/opp_twl_tps.h>

#include "smartreflex.h"
#include "smartreflex-class1p5.h"
#include "voltage.h"
#include "resource34xx_mutex.h"

#define MAX_VDDS	2
#define SR1P5_SAMPLING_DELAY_MS	1
#define SR1P5_STABLE_SAMPLES	5
#define SR1P5_MAX_TRIGGERS	5

#define RECALIBRATION_TIMEOUT_MS	(24 * 3600 * 1000)

/*
 * we expect events in 10uS, if we dont get 2wice times as much,
 * we could kind of ignore this as a missed event.
 */
#define MAX_CHECK_VPTRANS_US	20

/**
 * struct sr_class1p5_work_data - data meant to be used by calibration work
 * @work:	calibration work
 * @id:		id for which we are triggering
 * @vdata:	voltage data we are calibrating
 * @u_volt_samples:	voltage samples collected by isr
 * @num_triggers:	number of triggers from calibration loop
 * @num_samples:	number of samples collected by isr
 * @work_active:	have we scheduled a work item?
 * @recal_jtimeout:	timeout for recalibration
 */
struct sr_class1p5_work_data {
	struct delayed_work work;
	int id;
	struct omap_volt_data *vdata;
	unsigned long u_volt_samples[SR1P5_STABLE_SAMPLES];
	u8 num_triggers;
	u8 num_samples;
	bool work_active;
	unsigned long recal_jtimeout;
};

/**
 * struct sr_class1p5_data - private data for class 1p5
 * @debugfs_file:	 created debugfs entry
 * @debugfs_users:	number of users for debugfs (essentially num vdds)
 * @work_data:		work item data per vdd
 */
struct sr_class1p5_data {
#ifdef CONFIG_PM_DEBUG
	struct dentry *debugfs_file;
	int debugfs_users;
#endif
	struct sr_class1p5_work_data work_data[MAX_VDDS];
};

#ifdef CONFIG_PM_DEBUG
static int sr_class1p5_open(struct inode *inode, struct file *file);
static int sr_class1p5_calib_show(struct seq_file *s, void *unused);
static ssize_t sr_class1p5_calib_store(struct file *file,
		const char __user *buf, size_t len, loff_t *ppos);

static const struct file_operations sr_class1p5_calib_fops = {
	.open		= sr_class1p5_open,
	.write		= sr_class1p5_calib_store,
	.read           = seq_read,
	.release        = single_release,
};
#endif

static void sr_class1p5_reset_calib(int vdd, bool reset, bool recal);

/* our instance of class 1p5 private data */
static struct sr_class1p5_data class_1p5_data;

static struct sr_class1p5_work_data *get_sr1p5_work(int id)
{
	if (unlikely(id >= MAX_VDDS)) {
		pr_err("%s: bad data!\n", __func__);
		return NULL;
	}
	return &class_1p5_data.work_data[id];
}

/**
 * sr_class1p5_notify() - isr notifier for status events
 * @sr_id:	srid for which we were triggered
 * @status:	notifier event to use
 *
 * This basically collects data for the work to use.
 */
static int sr_class1p5_notify(int sr_id, u32 status)
{
	struct sr_class1p5_work_data *work_data;
	int idx = 0;
	work_data = get_sr1p5_work(sr_id);

	if (unlikely(!work_data)) {
		pr_err("%s:%d no work data!!\n", __func__, sr_id);
		return -EINVAL;
	}

	/* Wait for transdone so that we know the voltage to read */
	do {
		if (vp_is_transdone(sr_id))
			break;
		idx++;
		/* get some constant delay */
		udelay(1);
	} while (idx < MAX_CHECK_VPTRANS_US);

	/*
	 * If we timeout, we still read the data,
	 * if we are oscillating+irq latencies are too high, we could
	 * have scenarios where we miss transdone event. since
	 * we waited long enough, it is still safe to read the voltage
	 * as we would have waited long enough - still flag it..
	 */
	if (idx >= MAX_CHECK_VPTRANS_US)
		pr_warning("%s: timed out waiting for transdone!!\n",
			__func__);

	vp_clear_transdone(sr_id);

	idx = (work_data->num_samples) % SR1P5_STABLE_SAMPLES;
	work_data->u_volt_samples[idx] =
		omap_voltageprocessor_get_voltage(sr_id);
	work_data->num_samples++;

	return 0;
}

/**
 * do_calibrate() - work which actually does the calibration
 * @work: pointer to the work
 *
 * calibration routine uses the following logic:
 * on the first trigger, we start the isr to collect sr voltages
 * wait for stabilization delay (reschdule self instead of sleeping)
 * after the delay, see if we collected any isr events
 * if none, we have calibrated voltage.
 * if there are any, we retry untill we giveup.
 * on retry timeout, select a voltage to use as safe voltage.
 */
static void do_calibrate(struct work_struct *work)
{
	struct sr_class1p5_work_data *work_data =
		container_of(work, struct sr_class1p5_work_data, work.work);
	unsigned long u_volt_safe = 0, u_volt_current = 0;
	struct omap_volt_data *volt_data;
	int id;
	int i, r;

	if (unlikely(!work_data)) {
		pr_err("%s: ooops.. null work_data?\n", __func__);
		return;
	}

	/*
	 * Handle the case where we might have just been scheduled AND
	 * 1.5 disable was called. Paranoid check - dvfs_mutex could be held
	 * for the other vdd too.. play safe instead, we will collect more
	 * sample data instead, reschedule.
	 */
	if (!mutex_trylock(&dvfs_mutex)) {
		schedule_delayed_work(&work_data->work,
				msecs_to_jiffies(SR1P5_SAMPLING_DELAY_MS *
				SR1P5_STABLE_SAMPLES));
		return;
	}
	id = work_data->id;
	/*
	 * In the unlikely case that we did get through when unplanned,
	 * flag and return.
	 */
	if (unlikely(!work_data->work_active)) {
		pr_err("%s:%d unplanned work invocation!\n", __func__, id);
		mutex_unlock(&dvfs_mutex);
		return;
	}

	work_data->num_triggers++;
	/* if we are triggered first time, we need to start isr to sample */
	if (work_data->num_triggers == 1)
		goto start_sampling;

	/* Stop isr from interrupting our measurements :) */
	sr_notifier_control(id, false);

	volt_data = work_data->vdata;

	/* if there are no samples captured.. SR is silent, aka stability! */
	if (!work_data->num_samples) {
		u_volt_safe = omap_voltageprocessor_get_voltage(id);
		u_volt_current = u_volt_safe;
		goto done_calib;
	}
	if (work_data->num_triggers == SR1P5_MAX_TRIGGERS) {
		pr_warning("%s: %d recalib timeout!\n", __func__,
			   work_data->id);
		goto oscillating_calib;
	}

	/* ok.. so we have potential oscillations, lets sample again.. */
start_sampling:
	work_data->num_samples = 0;
	/* Clear pending events */
	sr_notifier_control(id, false);
	/* Clear all transdones */
	while (vp_is_transdone(id))
		vp_clear_transdone(id);
	/* trigger sampling */
	sr_notifier_control(id, true);
	schedule_delayed_work(&work_data->work,
		msecs_to_jiffies(SR1P5_SAMPLING_DELAY_MS *
		SR1P5_STABLE_SAMPLES));
	mutex_unlock(&dvfs_mutex);
	return;

oscillating_calib:
	/* just in case we got more interrupts than our poor tiny buffer */
	i = ((work_data->num_samples > SR1P5_STABLE_SAMPLES) ?
		SR1P5_STABLE_SAMPLES : work_data->num_samples) - 1;
	/* Grab the max of the samples as the stable voltage */
	for (; i >= 0; i--)
		u_volt_safe = (work_data->u_volt_samples[i] >
			u_volt_safe) ? work_data->u_volt_samples[i] :
			u_volt_safe;
	/* pick up current voltage to switch if needed */
	u_volt_current = omap_voltageprocessor_get_voltage(id);

	/* Fall through to close up common stuff */
done_calib:
	r = omap_sr_get_count(id, &volt_data->sr_error, &volt_data->sr_val);
	if (r) {
		pr_err("%s: %d: invalid configuration?? calib stopped\n",
			__func__, id);
		goto out;
	}

	omap_voltageprocessor_disable(id);
	sr_disable(id);

	volt_data->u_volt_calib = u_volt_safe;
	/* Setup my dynamic voltage for the next calibration for this opp */
	volt_data->u_volt_dyn_nominal = omap_get_dyn_nominal(volt_data);

	/*
	 * if the voltage we decided as safe is not the current voltage,
	 * switch
	 */
	if (volt_data->u_volt_calib != u_volt_current) {
		struct omap_volt_data vdata_current;
		pr_debug("%s:%d reconfiguring to voltage %ld from %ld\n",
				__func__, id, volt_data->u_volt_calib,
				u_volt_current);
		memcpy(&vdata_current, volt_data, sizeof(vdata_current));
		vdata_current.u_volt_calib = u_volt_current;
		omap_voltage_scale(id, volt_data, &vdata_current);
	}

	/* Setup my wakeup voltage */
	vc_setup_on_voltage(id, volt_data->u_volt_calib);
	work_data->work_active = false;
out:
	mutex_unlock(&dvfs_mutex);
}

/**
 * sr_class1p5_enable() - class 1.5 mode of enable
 * @id:		sr id to enable this for
 * @volt_data:	voltdata to the voltage transition taking place
 *
 * when this gets called, we use the h/w loop to setup our voltages
 * to an calibrated voltage, detect any oscillations, recover from the same
 * and finally store the optimized voltage as the calibrated voltage in the
 * system
 */
static int sr_class1p5_enable(int id, struct omap_volt_data *volt_data)
{
	int r;
	struct sr_class1p5_work_data *work_data;
	/* if already calibrated, nothing to do here.. */
	if (volt_data->u_volt_calib)
		return 0;

	work_data = get_sr1p5_work(id);
	if (unlikely(!work_data)) {
		pr_err("%s: aieeee.. bad work data??\n", __func__);
		return -EINVAL;
	}

	if (work_data->work_active)
		return 0;

	omap_voltageprocessor_enable(id, volt_data);
	r = sr_enable(id, volt_data);
	if (r) {
		pr_err("%s: sr[%d] failed\n", __func__, id + 1);
		omap_voltageprocessor_disable(id);
		return r;
	}
	work_data->vdata = volt_data;
	work_data->work_active = true;
	work_data->num_triggers = 0;
	/* program the workqueue and leave it to calibrate offline.. */
	schedule_delayed_work(&work_data->work,
		msecs_to_jiffies(SR1P5_SAMPLING_DELAY_MS *
			SR1P5_STABLE_SAMPLES));

	return 0;
}

/**
 * sr_class1p5_disable() - disable for class 1p5
 * @id: id for the sr which needs disabling
 * @volt_data:	voltagedata to disable
 * @is_volt_reset: reset the voltage?
 *
 * we dont do anything if the class 1p5 is being used. this is because we
 * already disable sr at the end of calibration and no h/w loop is actually
 * active when this is called.
 */
static int sr_class1p5_disable(int id, struct omap_volt_data *volt_data,
		int is_volt_reset)
{
	unsigned long u_volt_current;
	struct sr_class1p5_work_data *work_data;

	work_data = get_sr1p5_work(id);
	u_volt_current = omap_voltageprocessor_get_voltage(id);
	if (work_data->work_active) {
		/* if volt reset and work is active, we dont allow this */
		if (is_volt_reset)
			return -EBUSY;
		/* flag work is dead and remove the old work */
		work_data->work_active = false;
		cancel_delayed_work_sync(&work_data->work);
		sr_notifier_control(id, false);
		omap_voltageprocessor_disable(id);
		sr_disable(id);
	}

	/* if already calibrated, see if we need to reset calib.. */
	if (volt_data->u_volt_calib) {
		/*
		 * Skip the recal check if we are in ret path,
		 * do it later
		 */
		if (is_volt_reset)
			return 0;
		/*
		 * we now do a check if time is appropriate to do a
		 * recalibration for this vdd or not
		 * NOTE: i dont need to reset the voltage OR enable sr here,
		 * because I will be doing this anyway further down the flow
		 * forceupdate, enable sr.
		 */
		if (!time_is_after_jiffies(work_data->recal_jtimeout))
			sr_class1p5_reset_calib(id, false, false);

		return 0;
	}

	if (is_volt_reset) {
		struct omap_volt_data vdata_current;
		memcpy(&vdata_current, volt_data, sizeof(vdata_current));
		vdata_current.u_volt_calib = u_volt_current;
		omap_voltage_scale(id, volt_data, &vdata_current);
	}
	return 0;
}

/**
 * sr_class1p5_configure() - configuration function
 * @id:	configure for which class
 *
 * we dont do much here other than setup some registers for
 * the sr module involved.
 */
static void sr_class1p5_configure(int id)
{
	sr_configure_errgen(id);
}

#ifdef CONFIG_PM_DEBUG
/**
 * sr_class1p5_open() - open for debugfs
 * @inode: inode
 * @file: file pointer
 *
 * we use single open to sequence our data ops
 */
static int sr_class1p5_open(struct inode *inode, struct file *file)
{
	return single_open(file, sr_class1p5_calib_show, inode->i_private);
}

/**
 * sr_class1p5_calib_show() - show calibrated voltages to userspace
 * @kobj:	sysfs obj
 * @attr:	file attribute
 * @buf:	buffer to dump the data to
 */
static int sr_class1p5_calib_show(struct seq_file *s, void *unused)
{
	/* FIXME: COMPATIBILITY BREAK WITH OMAP4!!!!!! */
	enum opp_t omap3_opps[] = {
		OPP_MPU,
		OPP_L3,
	};
	unsigned long freq;
	struct omap_volt_data *vdata;
	struct omap_opp *opp;
	enum opp_t oppt;
	int i;
	int vdd;
	int idx;
	char *name;

	seq_printf(s, "%5s  [%10s] %10s[ hex]: %9s[ hex] "
			"%9s [ hex]:%10s[ hex]: "
			"%10s %10s %10s%10s %10s %3s\n", "oppid", "freq",
			"nomV(uV)", "dnomV(uV)", "dynM(uV)",
			"calibV(uV)", "ntarget", "errminlimit", "errorgain",
			"sr error", "sr val", "abb");
	for (i = 0; i < ARRAY_SIZE(omap3_opps); i++) {
		freq = 0;
		idx = 1;
		oppt = omap3_opps[i];
		/*
		 * ok, I admit, the following code is crappy as hell
		 * TODO: modify voltage layer to use domain IDs and not to use
		 * VDD1 and VDD2 ids.
		 */
		vdd = (oppt == OPP_MPU) ? VDD1 : VDD2;
		name = (oppt == OPP_MPU) ? "mpu" : "l3";
		while (!IS_ERR(opp = opp_find_freq_ceil(oppt, &freq))) {
			vdata = omap_get_volt_data(vdd, opp_get_voltage(opp));
			seq_printf(s, "%5s %1d[%10ld] %10ld[0x%02x]:"
				"%10ld[0x%02x] %10ld[0x%02x]:"
				"%10ld[0x%02x]: 0x%08x 0x%08x 0x%08x 0x%08x "
				"0x%08x %2s\n",
				name, idx, freq, vdata->u_volt_nominal,
				omap_twl_uv_to_vsel(vdata->u_volt_nominal),
				vdata->u_volt_dyn_nominal,
				(vdata->u_volt_dyn_nominal) ?
				omap_twl_uv_to_vsel(vdata->u_volt_dyn_nominal) :
						0,
				vdata->u_volt_dyn_margin,
				(vdata->u_volt_dyn_margin) ?
				omap_twl_uv_to_vsel(vdata->u_volt_dyn_margin +
					600000) : 0,
				vdata->u_volt_calib,
				(vdata->u_volt_calib) ?
				omap_twl_uv_to_vsel(vdata->u_volt_calib) : 0,
				vdata->sr_nvalue, vdata->sr_errminlimit,
				vdata->vp_errorgain,
				vdata->sr_error,
				vdata->sr_val,
				(vdata->abb) ? "yes" : "no");
			freq++;
			idx++;
		}
	}
	return 0;
}

/**
 * sr_class1p5_calib_store() - reset calibration for periodic recalibration
 * @kobj:	sysfs object
 * @attr:	attribute to the file
 * @buf:	buffer containing the value
 * @n:		size of the buffer
 *
 * we will engage only if the value is 0, else we return back saying invalid
 */
static ssize_t sr_class1p5_calib_store(struct file *file,
		const char __user *buf, size_t len, loff_t *ppos)
{
	char lbuf[10];
	size_t size;
	char *s;
	unsigned short value;

	size = min(sizeof(lbuf) - 1, len);
	if (copy_from_user(lbuf, buf, size))
		return -EFAULT;
	lbuf[size] = '\0';
	s = strstrip(lbuf);

	/* I take nothin but 0 */
	if ((sscanf(s, "%hu", &value) > 1) || value) {
		pr_err("%s: Invalid value %d\n", __func__, value);
		return -EINVAL;
	}

	mutex_lock(&dvfs_mutex);
	sr_class1p5_reset_calib(VDD1, true, true);
	sr_class1p5_reset_calib(VDD2, true, true);
	mutex_unlock(&dvfs_mutex);

	return len;
}
#endif		/* CONFIG_PM_DEBUG */

/**
 * sr_class1p5_reset_calib() - reset all calibrated voltages
 * @srid:	srid to reset the calibration for
 * @reset:	reset voltage before we recal?
 * @recal:	should I recalibrate my current opp?
 *
 * if we call this, it means either periodic calibration trigger was
 * fired(either from sysfs or other mechanisms) or we have disabled class 1p5,
 * meaning we cant trust the calib voltages anymore, it is better to use
 * nominal in the system
 */
static void sr_class1p5_reset_calib(int vdd, bool reset, bool recal)
{
	/* FIXME: COMPATIBILITY BREAK WITH OMAP4!!!!!! */
	enum opp_t omap3_opps[] = {
		OPP_MPU,
		OPP_L3,
	};
	unsigned long freq;
	struct omap_volt_data *vdata;
	struct omap_opp *opp;
	enum opp_t oppt;
	struct sr_class1p5_work_data *work_data;

	freq = 0;
	oppt = omap3_opps[vdd];
	/* I dont need to go further if sr is not present */
	if (!is_sr_enabled(vdd))
		return;

	work_data = get_sr1p5_work(vdd);
	/* setup a new timeout value */
	work_data->recal_jtimeout = jiffies +
		msecs_to_jiffies(RECALIBRATION_TIMEOUT_MS);

	if (work_data->work_active)
		sr_class1p5_disable(vdd, work_data->vdata, 0);

	while (!IS_ERR(opp = opp_find_freq_ceil(oppt, &freq))) {
		vdata = omap_get_volt_data(vdd, opp_get_voltage(opp));
		/* reset it to 0 */
		vdata->u_volt_calib = 0;
		freq++;
	}
	/*
	 * I should now reset the voltages to my nominal to be safe
	 */
	if (reset)
		vdata = omap_reset_voltage(vdd);

	/*
	 * I should fire a recalibration for current opp if needed
	 * Note: i have just reset my calibrated voltages, and if
	 * i call sr_enable equivalent, I will cause a recalibration
	 * loop, even though the function is called sr_enable.. we
	 * are in class 1.5 ;)
	 */
	if (reset && recal)
		sr_class1p5_enable(vdd, vdata);
}


/**
 * sr_class1p5_cinit() - class 1p5 init
 * @sr_id:		sr id
 * @class_priv_data:	private data for the class
 *
 * we do class specific initialization like creating sysfs/debugfs entries
 * needed, spawning of a kthread if needed etc.
 */
static int sr_class1p5_cinit(int sr_id, void *class_priv_data)
{
	struct sr_class1p5_data *class_data;
	struct sr_class1p5_work_data *work_data;
	char name[] = "sr_calib";

	if (!class_priv_data) {
		pr_err("%s: bad param? no priv data!\n", __func__);
		return -EINVAL;
	}
	class_data = (struct sr_class1p5_data *) class_priv_data;

#ifdef CONFIG_PM_DEBUG
	if (!class_data->debugfs_file) {
		class_data->debugfs_file = debugfs_create_file(name, S_IRUGO,
			pm_dbg_main_dir,
			NULL,
			&sr_class1p5_calib_fops);
		if (!class_data->debugfs_file)
			pr_err("%s: creating debugfs of %s failed\n", __func__,
				name);
	}
	class_data->debugfs_users++;
#endif

	/* setup our work params */
	work_data = get_sr1p5_work(sr_id);
	if (unlikely(!work_data)) {
		pr_err("%s: ooopps.. no work data for %d!!! bug??\n",
		__func__, sr_id);
#ifdef CONFIG_PM_DEBUG
		class_data->debugfs_users--;
		if (!class_data->debugfs_users) {
			debugfs_remove(class_data->debugfs_file);
			class_data->debugfs_file = NULL;
		}
#endif

		return -EINVAL;
	}
	work_data->id = sr_id;
	work_data->recal_jtimeout = jiffies +
		msecs_to_jiffies(RECALIBRATION_TIMEOUT_MS);
	INIT_DELAYED_WORK_DEFERRABLE(&work_data->work, do_calibrate);

	return 0;
}

/**
 * sr_class1p5_cdeinit() - class 1p5 deinitialization
 * @sr_id:	sr id for which to do this.
 * @class_priv_data: class private data for deinitialiation
 *
 * currently only resets the calibrated voltage forcing dvfs voltages
 * to be used in the system
 */
static int sr_class1p5_cdeinit(int sr_id, void *class_priv_data)
{
	struct sr_class1p5_data *class_data;
	if (!class_priv_data) {
		pr_err("%s: bad param? no priv data!\n", __func__);
		return -EINVAL;
	}

	/*
	 * we dont have SR periodic calib anymore.. so reset calibs
	 * we are already protected by sr debugfs lock, so no lock needed
	 * here.
	 */
	sr_class1p5_reset_calib(sr_id, true, false);

#ifdef CONFIG_PM_DEBUG
	class_data = (struct sr_class1p5_data *) class_priv_data;
	class_data->debugfs_users--;
	if (class_data->debugfs_file && !class_data->debugfs_users) {
		debugfs_remove(class_data->debugfs_file);
		class_data->debugfs_file = NULL;
	}
#endif
	return 0;
}

/* SR class1p5 structure */
static struct omap_smartreflex_class_data class1p5_data = {
	.enable = sr_class1p5_enable,
	.disable = sr_class1p5_disable,
	.configure = sr_class1p5_configure,
	.class_type = SR_CLASS1P5,
	.class_init = sr_class1p5_cinit,
	.class_deinit = sr_class1p5_cdeinit,
	.notify = sr_class1p5_notify,
	/*
	 * trigger for bound - this tells VP that SR has a voltage
	 * change. we should ensure transdone is set before reading
	 * vp voltage.
	 */
	.notify_flags = SR_NOTIFY_MCUBOUND,
	.class_priv_data = (void *)&class_1p5_data,
};

/**
 * sr_class1p5_init() - register class 1p5 as default
 *
 * board files call this function to use class 1p5, we register with the
 * smartreflex subsystem
 */
int __init sr_class1p5_init(void)
{
	int r;
	r = omap_sr_register_class(&class1p5_data);
	if (r)
		pr_err("SmartReflex class 1.5 driver: "
				"failed to register with %d\n", r);
	else
		pr_info("SmartReflex class 1.5 driver: initialized\n");
	return r;
}
