/*
 * Maemo 5 compatibility driver for Nokia RX-51
 *
 * Copyright (C) 2004-2005 Nokia
 * Copyright (C) 2013      Pali Rohár <pali.rohar@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/i2c/twl4030-madc.h>
#include <asm/system_info.h>

/*** /proc/bootreason ***/

static char bootreason_str[16] = "pwr_key";

static int bootreason_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%s\n", bootreason_str);
	return 0;
}

static int bootreason_open(struct inode *inode, struct file *file)
{
	return single_open(file, bootreason_show, NULL);
}

static const struct file_operations bootreason_fops = {
	.open		= bootreason_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int __init bootreason_init(void)
{
	printk(KERN_INFO "Bootup reason: %s\n", bootreason_str);
	proc_create("bootreason", S_IRUGO, NULL, &bootreason_fops);
	return 0;
}

late_initcall(bootreason_init);

/*** /proc/component_version ***/

struct version_config {
	char component[12];
	char version[12];
};

static struct version_config version_configs[] = {
	{"product", "RX-51"},
	{"hw-build", "2101"},
	{"nolo", "1.4.14"},
	{"boot-mode", "normal"},
};

static int component_version_show(struct seq_file *m, void *v)
{
	int i;
	const struct version_config *ver;

	for (i = 0; i < ARRAY_SIZE(version_configs); i++) {
		ver = &version_configs[i];
		seq_printf(m, "%-12s%s\n", ver->component, ver->version);
	}

	return 0;
}

static int component_version_open(struct inode *inode, struct file *file)
{
	return single_open(file, component_version_show, NULL);
}

static const struct file_operations component_version_fops = {
	.open		= component_version_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int __init component_version_init(void)
{
	if (system_rev != 0)
		snprintf(version_configs[1].version, 12, "%04x", system_rev);

	proc_create("component_version", S_IRUGO, NULL, &component_version_fops);
	return 0;
}

late_initcall(component_version_init);

/*** /dev/twl4030-adc ***/

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
#ifdef CONFIG_TWL4030_MADC
	case TWL4030_MADC_IOCX_ADC_RAW_READ: {
		struct twl4030_madc_request req;
		if (par.channel >= TWL4030_MADC_MAX_CHANNELS)
			return -EINVAL;

		req.channels	= (1 << par.channel);
		req.do_avg	= par.average;
		req.method	= TWL4030_MADC_SW1;
		req.func_cb	= NULL;
		req.type	= TWL4030_MADC_WAIT;
		req.raw		= true;

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
#endif
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
	.unlocked_ioctl = twl4030_madc_ioctl,
};

static struct miscdevice twl4030_madc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "twl4030-adc",
	.fops = &twl4030_madc_fileops,
};

static int __init twl4030_madc_init(void) {

	return misc_register(&twl4030_madc_device);

}

late_initcall(twl4030_madc_init);
