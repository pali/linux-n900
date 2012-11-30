/*
 *
 * TWL4030 MADC Hwmon driver-This driver monitors the real time
 * conversion of analog signals like battery temperature,
 * battery type, battery level etc. User can ask for the conversion on a
 * particular channel using the sysfs nodes.
 *
 * Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com/
 * J Keerthy <j-keerthy@ti.com>
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
 *
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c/twl.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/i2c/twl4030-madc.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/stddef.h>
#include <linux/sysfs.h>
#include <linux/err.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>

/*
 * sysfs hook function
 */
static ssize_t madc_read(struct device *dev,
			 struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct twl4030_madc_request req = {
		.channels = 1 << attr->index,
		.method = TWL4030_MADC_SW2,
		.type = TWL4030_MADC_WAIT,
	};
	long val;

	val = twl4030_madc_conversion(&req);
	if (val < 0)
		return val;

	return sprintf(buf, "%d\n", req.rbuf[attr->index]);
}

/* sysfs nodes to read individual channels from user side */
static SENSOR_DEVICE_ATTR(in0_input, S_IRUGO, madc_read, NULL, 0);
static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, madc_read, NULL, 1);
static SENSOR_DEVICE_ATTR(in2_input, S_IRUGO, madc_read, NULL, 2);
static SENSOR_DEVICE_ATTR(in3_input, S_IRUGO, madc_read, NULL, 3);
static SENSOR_DEVICE_ATTR(in4_input, S_IRUGO, madc_read, NULL, 4);
static SENSOR_DEVICE_ATTR(in5_input, S_IRUGO, madc_read, NULL, 5);
static SENSOR_DEVICE_ATTR(in6_input, S_IRUGO, madc_read, NULL, 6);
static SENSOR_DEVICE_ATTR(in7_input, S_IRUGO, madc_read, NULL, 7);
static SENSOR_DEVICE_ATTR(in8_input, S_IRUGO, madc_read, NULL, 8);
static SENSOR_DEVICE_ATTR(in9_input, S_IRUGO, madc_read, NULL, 9);
static SENSOR_DEVICE_ATTR(curr10_input, S_IRUGO, madc_read, NULL, 10);
static SENSOR_DEVICE_ATTR(in11_input, S_IRUGO, madc_read, NULL, 11);
static SENSOR_DEVICE_ATTR(in12_input, S_IRUGO, madc_read, NULL, 12);
static SENSOR_DEVICE_ATTR(in15_input, S_IRUGO, madc_read, NULL, 15);

static struct attribute *twl4030_madc_attributes[] = {
	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in3_input.dev_attr.attr,
	&sensor_dev_attr_in4_input.dev_attr.attr,
	&sensor_dev_attr_in5_input.dev_attr.attr,
	&sensor_dev_attr_in6_input.dev_attr.attr,
	&sensor_dev_attr_in7_input.dev_attr.attr,
	&sensor_dev_attr_in8_input.dev_attr.attr,
	&sensor_dev_attr_in9_input.dev_attr.attr,
	&sensor_dev_attr_curr10_input.dev_attr.attr,
	&sensor_dev_attr_in11_input.dev_attr.attr,
	&sensor_dev_attr_in12_input.dev_attr.attr,
	&sensor_dev_attr_in15_input.dev_attr.attr,
	NULL
};

static const struct attribute_group twl4030_madc_group = {
	.attrs = twl4030_madc_attributes,
};

#define TWL4030_MADC_IOC_MAGIC '`'
#define TWL4030_MADC_IOCX_ADC_RAW_READ	  _IO(TWL4030_MADC_IOC_MAGIC, 0)

static long twl4030_madc_ioctl(struct file *filp, unsigned int cmd,
				unsigned long arg)
{
	struct twl4030_madc_user_parms par;
	int val, ret;

	ret = copy_from_user(&par, (void __user *) arg, sizeof(par));
	if (ret) {
		return -EACCES;
	}

	switch (cmd) {
	case TWL4030_MADC_IOCX_ADC_RAW_READ: {
		struct twl4030_madc_request req;
		if (par.channel >= TWL4030_MADC_MAX_CHANNELS)
			return -EINVAL;

		req.channels	= (1 << par.channel);
		req.do_avg	= par.average;
		req.method	= TWL4030_MADC_SW1;
		req.func_cb	= NULL;
		req.type	= TWL4030_MADC_WAIT;

		val = twl4030_madc_conversion(&req);
		if (likely(val > 0)) {
			par.status = 0;
			par.result = (u16)req.rbuf[par.channel];
		} else if (val == 0) {
			par.status = -ENODATA;
		} else {
			par.status = val;
		}
		break;
		}
	default:
		return -EINVAL;
	}

	ret = copy_to_user((void __user *) arg, &par, sizeof(par));
	if (ret) {
		return -EACCES;
	}

	return 0;
}

static struct file_operations twl4030_madc_fileops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = twl4030_madc_ioctl
};

static struct miscdevice twl4030_madc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "twl4030-adc",
	.fops = &twl4030_madc_fileops
};

static int twl4030_madc_hwmon_probe(struct platform_device *pdev)
{
	int ret;
	struct device *hwmon;

	ret = sysfs_create_group(&pdev->dev.kobj, &twl4030_madc_group);
	if (ret)
		goto err_sysfs;
	hwmon = hwmon_device_register(&pdev->dev);
	if (IS_ERR(hwmon)) {
		dev_err(&pdev->dev, "hwmon_device_register failed.\n");
		ret = PTR_ERR(hwmon);
		goto err_reg;
	}
	ret = misc_register(&twl4030_madc_device);
	if (ret) {
		dev_err(&pdev->dev, "could not register misc_device\n");
		goto err_misc;
	}

	return 0;

err_misc:
	hwmon_device_unregister(&pdev->dev);
err_reg:
	sysfs_remove_group(&pdev->dev.kobj, &twl4030_madc_group);
err_sysfs:

	return ret;
}

static int twl4030_madc_hwmon_remove(struct platform_device *pdev)
{
	misc_deregister(&twl4030_madc_device);
	hwmon_device_unregister(&pdev->dev);
	sysfs_remove_group(&pdev->dev.kobj, &twl4030_madc_group);

	return 0;
}

static struct platform_driver twl4030_madc_hwmon_driver = {
	.probe = twl4030_madc_hwmon_probe,
	.remove = twl4030_madc_hwmon_remove,
	.driver = {
		   .name = "twl4030_madc_hwmon",
		   .owner = THIS_MODULE,
		   },
};

module_platform_driver(twl4030_madc_hwmon_driver);

MODULE_DESCRIPTION("TWL4030 ADC Hwmon driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("J Keerthy");
MODULE_ALIAS("platform:twl4030_madc_hwmon");
