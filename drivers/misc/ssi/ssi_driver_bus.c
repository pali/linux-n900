/*
 * ssi_driver_bus.c
 *
 * Implements SSI bus, device and driver interface.
 *
 * Copyright (C) 2007-2008 Nokia Corporation. All rights reserved.
 *
 * Contact: Carlos Chinea <carlos.chinea@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */
#include <linux/device.h>
#include "ssi_driver.h"

#define SSI_PREFIX		"ssi:"

struct bus_type ssi_bus_type;

static ssize_t modalias_show(struct device *dev, struct device_attribute *a,
								char *buf)
{
	return snprintf(buf, BUS_ID_SIZE + 1, "%s%s\n", SSI_PREFIX,
								dev->bus_id);
}

static struct device_attribute ssi_dev_attrs[] = {
	__ATTR_RO(modalias),
	__ATTR_NULL,
};

static int ssi_bus_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	add_uevent_var(env, "MODALIAS=%s%s", SSI_PREFIX, dev->bus_id);
	return 0;
}

static int ssi_bus_match(struct device *device, struct device_driver *driver)
{
	struct ssi_device *dev = to_ssi_device(device);
	struct ssi_device_driver *drv = to_ssi_device_driver(driver);

	if (!test_bit(dev->n_ctrl, &drv->ctrl_mask))
		return 0;

	if (!test_bit(dev->n_ch, &drv->ch_mask[dev->n_p]))
		return 0;

	return 1;
}

int ssi_bus_unreg_dev(struct device *device, void *p)
{
	device->release(device);
	device_unregister(device);

	return 0;
}

int __init ssi_bus_init(void)
{
	return bus_register(&ssi_bus_type);
}

void ssi_bus_exit(void)
{
	bus_for_each_dev(&ssi_bus_type, NULL, NULL, ssi_bus_unreg_dev);
	bus_unregister(&ssi_bus_type);
}

static int ssi_driver_probe(struct device *dev)
{
	struct ssi_device_driver *drv = to_ssi_device_driver(dev->driver);

	return 	drv->probe(to_ssi_device(dev));
}

static int ssi_driver_remove(struct device *dev)
{
	struct ssi_device_driver *drv = to_ssi_device_driver(dev->driver);

	return drv->remove(to_ssi_device(dev));
}

static int ssi_driver_suspend(struct device *dev, pm_message_t mesg)
{
	struct ssi_device_driver *drv = to_ssi_device_driver(dev->driver);

	return drv->suspend(to_ssi_device(dev), mesg);
}

static int ssi_driver_resume(struct device *dev)
{
	struct ssi_device_driver *drv = to_ssi_device_driver(dev->driver);

	return drv->resume(to_ssi_device(dev));
}

struct bus_type ssi_bus_type = {
	.name		= "ssi",
	.dev_attrs	= ssi_dev_attrs,
	.match		= ssi_bus_match,
	.uevent		= ssi_bus_uevent,
};

/**
 * register_ssi_driver - Register SSI device driver
 * @driver - reference to the SSI device driver.
 */
int register_ssi_driver(struct ssi_device_driver *driver)
{
	int ret = 0;

	BUG_ON(driver == NULL);

	driver->driver.bus = &ssi_bus_type;
	if (driver->probe)
		driver->driver.probe = ssi_driver_probe;
	if (driver->remove)
		driver->driver.remove = ssi_driver_remove;
	if (driver->suspend)
		driver->driver.suspend = ssi_driver_suspend;
	if (driver->resume)
		driver->driver.resume = ssi_driver_resume;

	ret = driver_register(&driver->driver);

	return ret;
}
EXPORT_SYMBOL(register_ssi_driver);

/**
 * unregister_ssi_driver - Unregister SSI device driver
 * @driver - reference to the SSI device driver.
 */
void unregister_ssi_driver(struct ssi_device_driver *driver)
{
	BUG_ON(driver == NULL);

	driver_unregister(&driver->driver);
}
EXPORT_SYMBOL(unregister_ssi_driver);
