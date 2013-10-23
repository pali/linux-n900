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

#ifndef __ARCH_ARM_MACH_OMAP2_PM_OPTIMIZER_H
#define __ARCH_ARM_MACH_OMAP2_PM_OPTIMIZER_H

struct pm_optimizer_limits {
	int threshold;
	int min;
	int max;
};

struct pm_optimizer_element {
	const char *name;
	union {
		const char *conn;
		struct clk *clk;
		const char *last;
	} data;
	int timeout;
	int load;
	int type;
	struct pm_optimizer_limits cpufreq;
	struct pm_optimizer_limits cpuidle;
};

struct pm_optimizer_data {
	struct pm_optimizer_element *elems;
	int num_elements;
	const char **ignore_list;
	int num_ignores;
};

enum {
	PM_OPTIMIZER_TYPE_CLK,
	PM_OPTIMIZER_TYPE_DISPLAY,
	PM_OPTIMIZER_TYPE_INPUT,
	PM_OPTIMIZER_TYPE_USERSPACE,
};

enum {
	PM_OPTIMIZER_CPUIDLE,
	PM_OPTIMIZER_CPUFREQ,
};

extern void pm_optimizer_register(struct pm_optimizer_data *data);
extern int pm_optimizer_cpuidle_limit(void);
extern struct cpufreq_policy *pm_optimizer_cpufreq_limits(
		struct cpufreq_policy *policy);
extern void pm_optimizer_limits(int type, unsigned int *min, unsigned int *max,
		struct seq_file *trace);
extern void pm_optimizer_populate_debugfs(struct dentry *d, const void *fops,
		void *status_param, const void *seq_ops);

extern u32 pm_optimizer_enable;

#endif
