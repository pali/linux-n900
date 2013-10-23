/*
 * OMAP3 PM optimizer
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
#include <linux/cpufreq.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>

#include <plat/clock.h>
#include <plat/display.h>

#include "clock.h"
#include "pm-optimizer.h"
#include "clk-tracer.h"

static struct pm_optimizer_data *pm_opt_data;
static struct pm_optimizer_element *pm_opt_input_data;
static struct pm_optimizer_element *pm_opt_display_data;
static struct pm_optimizer_element *pm_opt_userspace_data;
static struct timer_list pm_opt_input_timer;
static struct timer_list pm_opt_userspace_timer;
static int pm_opt_input_active;
u32 pm_optimizer_enable = 1;
static struct cpufreq_policy pm_opt_cpufreq_policy;
static struct cpufreq_policy *pm_opt_saved_policy;
static void pm_opt_userspace_event(void);

static void pm_opt_userspace_callback(struct work_struct *unused)
{
	if (!pm_opt_saved_policy || !pm_opt_userspace_data)
			return;
	__cpufreq_driver_target(pm_opt_saved_policy,
		pm_opt_userspace_data->cpufreq.min, CPUFREQ_RELATION_L);
}

static DECLARE_WORK(pm_opt_userspace_work, pm_opt_userspace_callback);
static void pm_opt_userspace_event(void)
{
	if (!pm_opt_userspace_data)
		return;
	mod_timer(&pm_opt_userspace_timer,
			jiffies + pm_opt_userspace_data->timeout);
	if (pm_opt_userspace_data->load != 100)
		pm_opt_userspace_data->load = 100;
		schedule_work(&pm_opt_userspace_work);
}

static void pm_opt_userspace_timeout(unsigned long data)
{
	if (!pm_opt_userspace_data)
		return;
	pm_opt_userspace_data->load = 0;
}

static void pm_opt_input_callback(struct work_struct *unused)
{
	if (!pm_opt_saved_policy || !pm_opt_input_data)
			return;
	__cpufreq_driver_target(pm_opt_saved_policy,
		pm_opt_input_data->cpufreq.min, CPUFREQ_RELATION_L);
}

static DECLARE_WORK(pm_opt_input_work, pm_opt_input_callback);

static void pm_opt_input_event(struct input_handle *handle,
		unsigned int type, unsigned int code, int value)
{
	if (!pm_opt_input_data)
		return;
	mod_timer(&pm_opt_input_timer,
			jiffies + pm_opt_input_data->timeout);
	pm_opt_input_data->data.last = handle->dev->name;
	if (pm_opt_input_data->load != 100) {
		pm_opt_input_data->load = 100;
		schedule_work(&pm_opt_input_work);
	}
}

static void pm_opt_input_timeout(unsigned long data)
{
	if (!pm_opt_input_data)
		return;
	pm_opt_input_data->load = 0;
}

static void pm_opt_input_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static int pm_opt_input_connect(struct input_handler *handler,
		struct input_dev *dev, const struct input_device_id *id)
{
	struct input_handle *handle;
	int error;
	int i;

	for (i = 0; i < pm_opt_data->num_ignores; i++)
		if (!strcmp(dev->name, pm_opt_data->ignore_list[i]))
			return 0;

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "pm-optimizer";

	error = input_register_handle(handle);
	if (error)
		goto err1;

	error = input_open_device(handle);
	if (error)
		goto err2;

	return 0;
err1:
	input_unregister_handle(handle);
err2:
	kfree(handle);
	return error;
}

#ifdef CONFIG_OMAP_PM_SRF
static ssize_t pm_optimizer_rotation_show(struct kobject *kobj,
						struct kobj_attribute *attr,
						char *buf)
{
	return sprintf(buf, "load:%u\n", pm_opt_userspace_data->load);
}

static ssize_t pm_optimizer_rotation_store(struct kobject *kobj,
						struct kobj_attribute *attr,
						const char *buf, size_t n)
{
	unsigned short value;

	if (sscanf(buf, "%hu", &value) != 1)
		return -EINVAL;

	/* handle userspace rotation event */
	pm_opt_userspace_event();

	return n;
}

static struct kobj_attribute pm_optimizer_rotation_attr =
	__ATTR(pm_optimizer_rotation, 0644, pm_optimizer_rotation_show,
						pm_optimizer_rotation_store);

#endif

static const struct input_device_id pm_opt_input_ids[] = {
	{ .driver_info = 1 },
	{ },
};

static struct input_handler pm_opt_input_handler = {
	.event = pm_opt_input_event,
	.connect = pm_opt_input_connect,
	.disconnect = pm_opt_input_disconnect,
	.name = "pm_optimizer_handler",
	.id_table = pm_opt_input_ids,
};

void pm_optimizer_register(struct pm_optimizer_data *data)
{
	WARN(pm_opt_data, "pm_optimizer: already registered.\n");
	pm_opt_data = data;
}

static int __init pm_optimizer_init(void)
{
	struct pm_optimizer_element *p;
	int i;
	struct clk *clk;
#ifdef CONFIG_OMAP_PM_SRF
	int error = -EINVAL;
#endif

	if (!pm_opt_data)
		return 0;

	for (i = 0; i < pm_opt_data->num_elements; i++) {
		p = pm_opt_data->elems + i;
		switch (p->type) {
		case PM_OPTIMIZER_TYPE_CLK:
			if (p->data.conn != NULL)
				clk = clk_get_sys(p->name, p->data.conn);
			else
				clk = clk_get_sys(NULL, p->name);
			p->data.clk = clk;
			if (clk)
				clk_tracer_register_clk(clk);
			else
				WARN(1, "pm_optimizer_init unknown clock %s\n",
						p->name);
			break;
		case PM_OPTIMIZER_TYPE_INPUT:
			pm_opt_input_data = p;
			break;
		case PM_OPTIMIZER_TYPE_DISPLAY:
			pm_opt_display_data = p;
			break;
		case PM_OPTIMIZER_TYPE_USERSPACE:
#ifdef CONFIG_OMAP_PM_SRF
			error = sysfs_create_file(power_kobj,
					&pm_optimizer_rotation_attr.attr);
			if (error) {
				printk(KERN_ERR
					"sysfs_create_file failed: %d\n",
					 error);
				return error;
			}
#endif
			pm_opt_userspace_data = p;
			break;
		}
	}
	setup_timer(&pm_opt_input_timer, pm_opt_input_timeout, 0);
	setup_timer(&pm_opt_userspace_timer, pm_opt_userspace_timeout, 0);

	return 0;
}
late_initcall(pm_optimizer_init);

#ifdef CONFIG_OMAP2_DSS
static struct omap_dss_device *pm_opt_dss_dev;
static bool pm_opt_dss_lpm_enabled;

static int pm_optimizer_get_dss_load(void)
{
	int dss_load;

	if (pm_opt_dss_lpm_enabled)
		return 0;
	if (pm_opt_dss_dev == NULL)
		pm_opt_dss_dev = omap_dss_get_next_device(NULL);
	if (pm_opt_dss_dev)
		dss_load = pm_opt_dss_dev->state == OMAP_DSS_DISPLAY_ACTIVE ?
			100 : 0;
	else
		dss_load = 0;
	return dss_load;
}

void pm_optimizer_enable_dss_lpm(bool ena)
{
	pm_opt_dss_lpm_enabled = ena;
}
#else
static inline int pm_optimizer_get_dss_load(void)
{
	return 0;
}
#endif

void pm_optimizer_limits(int type, unsigned int *min, unsigned int *max,
			struct seq_file *trace)
{
	struct pm_optimizer_element *p;
	struct pm_optimizer_limits *l;
	int i;
	int load;
	int ret;
	int dss_load = pm_optimizer_get_dss_load();

	if (!pm_opt_data || !pm_optimizer_enable)
		return;

	if (type == PM_OPTIMIZER_CPUFREQ) {
		if (dss_load && !pm_opt_input_active) {
			ret = input_register_handler(&pm_opt_input_handler);
			pm_opt_input_active = 1;
		}
		if (!dss_load && pm_opt_input_active) {
			input_unregister_handler(&pm_opt_input_handler);
			pm_opt_input_active = 0;
		}
	}

	for (i = 0; i < pm_opt_data->num_elements; i++) {
		p = pm_opt_data->elems + i;
		load = 0;
		if (type == PM_OPTIMIZER_CPUIDLE)
			l = &p->cpuidle;
		else
			l = &p->cpufreq;

		if (!l->threshold)
			continue;

		switch (p->type) {
		case PM_OPTIMIZER_TYPE_CLK:
			load = clk_tracer_get_load(p->data.clk);
			break;
		case PM_OPTIMIZER_TYPE_INPUT:
			load = p->load;
			break;
		case PM_OPTIMIZER_TYPE_DISPLAY:
			load = dss_load;
			break;
		case PM_OPTIMIZER_TYPE_USERSPACE:
			load = p->load;
			break;
		}
		if (load >= l->threshold) {
			if (l->min && *min < l->min)
				*min = l->min;
			if (l->max && *max > l->max)
				*max = l->max;
		}

		if (trace)
			seq_printf(trace, "%d:%s:l=%d,t=%d,cl=%d-%d,l=%d-%d\n",
				type, p->name, load, l->threshold,
				*min, *max, l->min, l->max);
	}
}

int pm_optimizer_cpuidle_limit(void)
{
	unsigned int max = INT_MAX;
	unsigned int min = 0;

	pm_optimizer_limits(PM_OPTIMIZER_CPUIDLE, &min, &max, NULL);
	return max;
}

struct cpufreq_policy *pm_optimizer_cpufreq_limits(
		struct cpufreq_policy *policy)
{
	memcpy(&pm_opt_cpufreq_policy, policy, sizeof(struct cpufreq_policy));
	pm_opt_saved_policy = policy;
	pm_optimizer_limits(PM_OPTIMIZER_CPUFREQ, &pm_opt_cpufreq_policy.min,
			&pm_opt_cpufreq_policy.max, NULL);
	if (pm_opt_cpufreq_policy.min > policy->max)
		pm_opt_cpufreq_policy.min = policy->max;
	if (pm_opt_cpufreq_policy.max < policy->min)
		pm_opt_cpufreq_policy.max = policy->min;
	if (pm_opt_cpufreq_policy.max < pm_opt_cpufreq_policy.min)
		pm_opt_cpufreq_policy.max = pm_opt_cpufreq_policy.min;
	return &pm_opt_cpufreq_policy;
}

#ifdef CONFIG_DEBUG_FS

static int pm_optimizer_load_get(void *data, u64 *val)
{
	struct pm_optimizer_element *p = data;

	switch (p->type) {
	case PM_OPTIMIZER_TYPE_CLK:
		*val = clk_tracer_get_load(p->data.clk);
		break;
	case PM_OPTIMIZER_TYPE_INPUT:
		*val = p->load;
		break;
	case PM_OPTIMIZER_TYPE_DISPLAY:
		*val = pm_optimizer_get_dss_load();
		break;
	case PM_OPTIMIZER_TYPE_USERSPACE:
		*val = p->load;
		break;
	}
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(pm_opt_load_fops, pm_optimizer_load_get, NULL,
		"%llu\n");

static int pm_optimizer_type_get(void *data, u64 *val)
{
	struct pm_optimizer_element *p = data;

	*val = p->type;
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(pm_opt_type_fops, pm_optimizer_type_get, NULL,
		"%llu\n");

static int pm_optimizer_str_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t pm_optimizer_str_read(struct file *file, char __user *user_buf,
			size_t count, loff_t *ppos)
{
	const char *str = *(char **)file->private_data;
	if (!str)
		return 0;
	return simple_read_from_buffer(user_buf, count, ppos, str, strlen(str));
}

static const struct file_operations pm_opt_str_fops = {
	.open   = pm_optimizer_str_open,
	.read   = pm_optimizer_str_read,
};

void pm_optimizer_populate_debugfs(struct dentry *d, const void *fops,
		void *status_param, const void *seq_ops)
{
	int i, j;
	struct dentry *d2, *d3;
	struct pm_optimizer_limits *l;
	struct pm_optimizer_element *p;

	if (!pm_opt_data)
		return;

	d = debugfs_create_dir("pm_optimizer", d);
	(void) debugfs_create_file("enable", S_IRUGO | S_IWUGO, d,
				   &pm_optimizer_enable, fops);
	(void) debugfs_create_file("status", S_IRUGO, d,
				   status_param, seq_ops);
	for (i = 0; i < pm_opt_data->num_elements; i++) {
		p = pm_opt_data->elems + i;
		d2 = debugfs_create_dir(p->name, d);
		(void) debugfs_create_file("load", S_IRUGO, d2, p,
					   &pm_opt_load_fops);
		(void) debugfs_create_file("type", S_IRUGO, d2, p,
					   &pm_opt_type_fops);
		if (p->type == PM_OPTIMIZER_TYPE_INPUT) {
			(void) debugfs_create_file("timeout",
				S_IRUGO | S_IWUGO, d2, &p->timeout, fops);
			(void) debugfs_create_file("last",
				S_IRUGO, d2, &p->data.last, &pm_opt_str_fops);
		}
		if (p->type == PM_OPTIMIZER_TYPE_USERSPACE) {
			(void) debugfs_create_file("timeout",
				S_IRUGO | S_IWUGO, d2, &p->timeout, fops);
		}
		for (j = 0; j < 2; j++) {
			if (j == 0) {
				d3 = debugfs_create_dir("cpuidle", d2);
				l = &p->cpuidle;
			} else {
				d3 = debugfs_create_dir("cpufreq", d2);
				l = &p->cpufreq;
			}
			(void) debugfs_create_file("threshold",
				S_IRUGO | S_IWUGO, d3, &l->threshold, fops);
			(void) debugfs_create_file("min",
				S_IRUGO | S_IWUGO, d3, &l->min, fops);
			(void) debugfs_create_file("max",
				S_IRUGO | S_IWUGO, d3, &l->max, fops);
		}
	}
}
#endif
