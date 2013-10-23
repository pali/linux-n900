/*
 * omapdev device registration and handling code
 *
 * Copyright (C) 2007-2008 Texas Instruments, Inc.
 * Copyright (C) 2007-2008 Nokia Corporation
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#undef DEBUG

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/platform_device.h>

#include <mach/cpu.h>
#include <mach/powerdomain.h>
#include <mach/omapdev.h>

/* odev_list contains all registered struct omapdevs */
static LIST_HEAD(odev_list);


/* Private functions */

/*
 * _omapdev_lookup - look up an OMAP module pointer by its name
 * @name: name of the OMAP module to look up
 *
 * Finds a registered OMAP module by its name, returning a pointer to
 * it.	Returns a pointer to the struct omapdev if found, or NULL
 * otherwise.
 */
static struct omapdev *_omapdev_lookup(const char *name)
{
	struct omapdev *odev, *temp_odev;

	if (!name)
		return NULL;

	odev = NULL;

	list_for_each_entry(temp_odev, &odev_list, node) {
		if (!strcmp(name, temp_odev->name)) {
			odev = temp_odev;
			break;
		}
	}

	return odev;
}

/*
 * _omapdev_register - register an OMAP module
 * @odev: struct omapdev * to register
 *
 * Adds a OMAP module to the internal OMAP module list.  Returns
 * -EINVAL if given a null pointer, -EEXIST if a OMAP module is
 * already registered by the provided name, or 0 upon success.
 */
static int _omapdev_register(struct omapdev *odev)
{
	struct powerdomain *pwrdm;

	if (!odev)
		return -EINVAL;

	if (!omap_chip_is(odev->omap_chip))
		return -EINVAL;

	if (_omapdev_lookup(odev->name))
		return -EEXIST;

	pwrdm = pwrdm_lookup(odev->pwrdm.name);
	if (!pwrdm) {
		pr_debug("omapdev: cannot register %s: bad powerdomain\n",
			 odev->name);
		return -EINVAL;
	}
	odev->pwrdm.ptr = pwrdm;

	list_add(&odev->node, &odev_list);

	pr_debug("omapdev: registered %s\n", odev->name);

	return 0;
}



/* Public functions */


/**
 * omapdev_init - set up the OMAP module layer
 * @odevs: ptr to a array of omapdev ptrs to register
 *
 * Loop through the list of OMAP modules, registering them all.  No
 * return value.
 */
void omapdev_init(struct omapdev **odevs)
{
	struct omapdev **d = NULL;

	if (!list_empty(&odev_list)) {
		pr_debug("omapdev: init already called\n");
		return;
	}

	for (d = odevs; *d; d++) {
		int v;

		if (!omap_chip_is((*d)->omap_chip))
			continue;

		v = _omapdev_register(*d);
		if (ERR_PTR(v))
			pr_err("omapdev: could not register %s\n",
			       (*d)->name);
	}
}


/**
 * omapdev_get_pwrdm - return pwrdm pointer associated with the device
 * @omapdev: omapdev *
 *
 */
struct powerdomain *omapdev_get_pwrdm(struct omapdev *odev)
{
	if (!odev)
		return NULL;

	return odev->pwrdm.ptr;
}


/**
 * omapdev_find_pdev - look up an OMAP module by platform_device
 * @pdev_name: platform_device name to find
 * @pdev_id: platform_device id to find
 *
 * Finds a registered OMAP module by the platform_device name and ID
 * that is associated with it in the omapdev structure. If multiple
 * records exist, simply returns the 'first' record that it finds -
 * this is probably not optimal behavior, but should work for current
 * purposes.  Returns a pointer to the struct omapdev if found, or
 * NULL otherwise.
 */
struct omapdev *omapdev_find_pdev(struct platform_device *pdev)
{
	struct omapdev *odev, *temp_odev;

	if (!pdev)
		return NULL;

	odev = NULL;

	list_for_each_entry(temp_odev, &odev_list, node) {
		if (temp_odev->pdev_name &&
		    !strcmp(pdev->name, temp_odev->pdev_name) &&
		    pdev->id == temp_odev->pdev_id) {
			odev = temp_odev;
			break;
		}
	}

	return odev;
}

