/*
 * linux/arch/arm/mach-omap2/pm.c
 *
 * OMAP Power Management Common Routines
 *
 * Copyright (C) 2005 Texas Instruments, Inc.
 * Copyright (C) 2006-2008 Nokia Corporation
 *
 * Written by:
 * Richard Woodruff <r-woodruff2@ti.com>
 * Tony Lindgren
 * Juha Yrjola
 * Amit Kucheria <amit.kucheria@nokia.com>
 * Igor Stoppa <igor.stoppa@nokia.com>
 * Jouni Hogander
 *
 * Based on pm.c for omap1
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/device.h>

#include <plat/resource.h>
#include <plat/omap34xx.h>
#include <plat/opp.h>

#include "pm.h"

#ifdef CONFIG_OMAP_PM_SRF
static ssize_t vdd_opp_show(struct kobject *, struct kobj_attribute *, char *);
static ssize_t vdd_opp_store(struct kobject *k, struct kobj_attribute *,
			  const char *buf, size_t n);
static struct kobj_attribute vdd1_opp_attr =
	__ATTR(vdd1_opp, 0644, vdd_opp_show, vdd_opp_store);

static struct kobj_attribute vdd2_opp_attr =
	__ATTR(vdd2_opp, 0644, vdd_opp_show, vdd_opp_store);
static struct kobj_attribute vdd1_lock_attr =
	__ATTR(vdd1_lock, 0644, vdd_opp_show, vdd_opp_store);
static struct kobj_attribute vdd2_lock_attr =
	__ATTR(vdd2_lock, 0644, vdd_opp_show, vdd_opp_store);

#endif

#ifdef CONFIG_OMAP_PM_SRF
#include <plat/omap34xx.h>
static int vdd1_locked;
static int vdd2_locked;

static ssize_t vdd_opp_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	if (attr == &vdd1_opp_attr)
		return sprintf(buf, "%hu\n", resource_get_level("vdd1_opp"));
	else if (attr == &vdd2_opp_attr)
		return sprintf(buf, "%hu\n", resource_get_level("vdd2_opp"));
	else if (attr == &vdd1_lock_attr)
		return sprintf(buf, "%hu\n", resource_get_opp_lock(VDD1_OPP));
	else if (attr == &vdd2_lock_attr)
		return sprintf(buf, "%hu\n", resource_get_opp_lock(VDD2_OPP));
	else
		return -EINVAL;
}

static ssize_t vdd_opp_store(struct kobject *kobj, struct kobj_attribute *attr,
			  const char *buf, size_t n)
{
	unsigned short value;
	struct omap_opp *opp;
	int flags = 0;

	if (sscanf(buf, "%hu", &value) != 1)
		return -EINVAL;

	/* Check locks */
	if (attr == &vdd1_lock_attr) {
		flags = OPP_IGNORE_LOCK;
		attr = &vdd1_opp_attr;
		if (vdd1_locked && value == 0) {
			resource_unlock_opp(VDD1_OPP);
			resource_refresh();
			vdd1_locked = 0;
			return n;
		}
		if (vdd1_locked == 0 && value != 0) {
			resource_lock_opp(VDD1_OPP);
			vdd1_locked = 1;
		}
	} else if (attr == &vdd2_lock_attr) {
		flags = OPP_IGNORE_LOCK;
		attr = &vdd2_opp_attr;
		if (vdd2_locked && value == 0) {
			resource_unlock_opp(VDD2_OPP);
			resource_refresh();
			vdd2_locked = 0;
			return n;
		}
		if (vdd2_locked == 0 && value != 0) {
			resource_lock_opp(VDD2_OPP);
			vdd2_locked = 1;
		}
	}

	if (attr == &vdd1_opp_attr) {
		opp = opp_find_by_opp_id(OPP_MPU, value);
		if (!opp) {
			printk(KERN_ERR "vdd_opp_store: Invalid value\n");
			return -EINVAL;
		}
		resource_set_opp_level(VDD1_OPP, value, flags);
	} else if (attr == &vdd2_opp_attr) {
		opp = opp_find_by_opp_id(OPP_L3, value);
		if (!opp) {
			printk(KERN_ERR "vdd_opp_store: Invalid value\n");
			return -EINVAL;
		}
		resource_set_opp_level(VDD2_OPP, value, flags);
	} else {
		return -EINVAL;
	}
	return n;
}
#endif

static int __init omap_pm_init(void)
{
	int error = -EINVAL;

#ifdef CONFIG_OMAP_PM_SRF
	error = sysfs_create_file(power_kobj,
				  &vdd1_opp_attr.attr);
	if (error) {
		printk(KERN_ERR "sysfs_create_file failed: %d\n", error);
		return error;
	}
	error = sysfs_create_file(power_kobj,
				  &vdd2_opp_attr.attr);
	if (error) {
		printk(KERN_ERR "sysfs_create_file failed: %d\n", error);
		return error;
	}

	error = sysfs_create_file(power_kobj, &vdd1_lock_attr.attr);
	if (error) {
		printk(KERN_ERR "sysfs_create_file failed: %d\n", error);
		return error;
	}

	error = sysfs_create_file(power_kobj, &vdd2_lock_attr.attr);
	if (error) {
		printk(KERN_ERR "sysfs_create_file failed: %d\n", error);
		return error;
	}
#endif

	return error;
}
late_initcall(omap_pm_init);
