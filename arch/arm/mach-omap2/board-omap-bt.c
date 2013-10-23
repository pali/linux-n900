/*
 * Nokia RX-51 platform-specific data for Bluetooth
 *
 * Copyright (C) 2005, 2006 Nokia Corporation
 * Contact: Ville Tervo <ville.tervo@nokia.com>
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

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <mach/board.h>

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <mach/board.h>

static struct platform_device omap_bt_device = {
	.name		= "hci_h4p",
	.id		= -1,
	.num_resources	= 0,
};

static ssize_t hci_h4p_store_bdaddr(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct omap_bluetooth_config *bt_config =
		omap_bt_device.dev.platform_data;
	unsigned int bdaddr[6];
	int ret, i;

	ret = sscanf(buf, "%2x:%2x:%2x:%2x:%2x:%2x\n",
			&bdaddr[0], &bdaddr[1], &bdaddr[2],
			&bdaddr[3], &bdaddr[4], &bdaddr[5]);

	if (ret != 6)
		return -EINVAL;

	for (i = 0; i < 6; i++)
		bt_config->bd_addr[i] = bdaddr[i] & 0xff;

	return count;
}

static ssize_t hci_h4p_show_bdaddr(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct omap_bluetooth_config *bt_config =
		omap_bt_device.dev.platform_data;

	return sprintf(buf, "%.2x:%.2x:%.2x:%.2x:%.2x:%.2x\n",
		       bt_config->bd_addr[0],
		       bt_config->bd_addr[1],
		       bt_config->bd_addr[2],
		       bt_config->bd_addr[3],
		       bt_config->bd_addr[4],
		       bt_config->bd_addr[5]);
}

static DEVICE_ATTR(bdaddr, S_IRUGO | S_IWUSR, hci_h4p_show_bdaddr,
		   hci_h4p_store_bdaddr);
int hci_h4p_sysfs_create_files(struct device *dev)
{
	return device_create_file(dev, &dev_attr_bdaddr);
}

void __init omap_bt_init(struct omap_bluetooth_config *bt_config)
{
	int err;
	omap_bt_device.dev.platform_data = bt_config;

	err = platform_device_register(&omap_bt_device);
	if (err < 0) {
		printk(KERN_ERR "Omap bluetooth device registration failed\n");
		return;
	}

	err = hci_h4p_sysfs_create_files(&omap_bt_device.dev);
	if (err < 0) {
		dev_err(&omap_bt_device.dev,
			"Omap bluetooth sysfs entry registration failed\n");
		platform_device_unregister(&omap_bt_device);
		return;
	}
}
