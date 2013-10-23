/*
 * Copyright (C) Nokia Corporation
 *
 * Written by Timo Kokkonen <timo.t.kokkonen at nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/watchdog.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/i2c/twl4030.h>

#define TWL4030_WDT_ENABLE_POWEROFF_ON_SUSPEND 1


#ifdef TWL4030_WDT_ENABLE_POWEROFF_ON_SUSPEND
#include <linux/debugfs.h>
#endif

#define TWL4030_WATCHDOG_CFG_REG_OFFS	0x3

static int nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started "
	"(default=" __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");



#define	TWL4030_WDT_STATE_OPEN_BIT	0x0
#define TWL4030_WDT_STATE_OPEN		(1 << TWL4030_WDT_STATE_OPEN_BIT)
#define TWL4030_WDT_STATE_ACTIVE	0x8

static struct platform_device *twl4030_wdt_dev;

struct twl4030_wdt {
	struct miscdevice	miscdev;
	int			timer_margin;
	unsigned long		state;
#ifdef TWL4030_WDT_ENABLE_POWEROFF_ON_SUSPEND
	u8			poweroff_on_suspend;
#endif
};

static int twl4030_wdt_write(unsigned char val)
{
	return twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER, val,
			   TWL4030_WATCHDOG_CFG_REG_OFFS);
}

static int twl4030_wdt_ping(struct twl4030_wdt *wdt)
{
	return twl4030_wdt_write(wdt->timer_margin + 1);
}

static int twl4030_wdt_enable(struct twl4030_wdt *wdt)
{
	return twl4030_wdt_ping(wdt);
}

static int twl4030_wdt_disable(struct twl4030_wdt *wdt)
{
	return twl4030_wdt_write(0);
}

static int twl4030_wdt_set_timeout(struct twl4030_wdt *wdt, int timeout)
{
	if (timeout < 0 || timeout > 30) {
		dev_warn(wdt->miscdev.parent,
			"Timeout can only be in the range [0-30] seconds");
		return -EINVAL;
	}
	wdt->timer_margin = timeout;
	return twl4030_wdt_ping(wdt);
}

#ifdef TWL4030_WDT_ENABLE_POWEROFF_ON_SUSPEND
static struct dentry *twl4030_wdt_debugfs;
#endif

static ssize_t twl4030_wdt_write_fop(struct file *file,
		const char __user *data, size_t len, loff_t *ppos)
{
	struct twl4030_wdt *wdt = file->private_data;
	if (twl4030_wdt_ping(wdt))
		return -EAGAIN;
	return len;
}

static long twl4030_wdt_ioctl(struct file *file,
		unsigned int cmd, unsigned long arg)
{
	int new_margin;
	struct twl4030_wdt *wdt = file->private_data;

	static const struct watchdog_info twl4030_wd_ident = {
		.identity = "TWL4030 Watchdog",
		.options = WDIOF_SETTIMEOUT,
		.firmware_version = 0
	};

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		return copy_to_user((struct watchdog_info __user *) arg,
				&twl4030_wd_ident, sizeof(twl4030_wd_ident)) ?
				-EFAULT : 0;
	case WDIOC_SETTIMEOUT:
		if (get_user(new_margin, (int __user *) arg))
			return -EFAULT;
		if (twl4030_wdt_set_timeout(wdt, new_margin))
			return -EINVAL;
		if (put_user(wdt->timer_margin, (int __user *) arg))
			return -EFAULT;
		break;

	case WDIOC_GETTIMEOUT:
		return put_user(wdt->timer_margin, (int __user *) arg);

	case WDIOC_KEEPALIVE:
		if (twl4030_wdt_ping(wdt))
			return -EFAULT;
		break;

	default:
		return -ENOTTY;
	}

	return 0;
}

static int twl4030_wdt_open(struct inode *inode, struct file *file)
{
	struct twl4030_wdt *wdt = platform_get_drvdata(twl4030_wdt_dev);

	if (test_and_set_bit(TWL4030_WDT_STATE_OPEN_BIT, &wdt->state))
		return -EBUSY;

	wdt->state |= TWL4030_WDT_STATE_ACTIVE;

	if (twl4030_wdt_ping(wdt)) {
		wdt->state &= ~(TWL4030_WDT_STATE_ACTIVE |
				TWL4030_WDT_STATE_OPEN);
		return -EAGAIN;
	}
	file->private_data = wdt;
	return nonseekable_open(inode, file);
}

static int twl4030_wdt_release(struct inode *inode, struct file *file)
{
	struct twl4030_wdt *wdt = file->private_data;
	if (nowayout) {
		dev_alert(wdt->miscdev.parent,
		       "Unexpected close, watchdog still running!\n");
	} else {
		if (twl4030_wdt_disable(wdt))
			return -EAGAIN;
		wdt->state &= ~TWL4030_WDT_STATE_ACTIVE;
	}

	wdt->state &= ~TWL4030_WDT_STATE_OPEN;
	return 0;
}

static int twl4030_wdt_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct twl4030_wdt *wdt = platform_get_drvdata(pdev);
#ifdef TWL4030_WDT_ENABLE_POWEROFF_ON_SUSPEND
	if (wdt->poweroff_on_suspend > 0 && wdt->poweroff_on_suspend < 31)
		return twl4030_wdt_write(wdt->poweroff_on_suspend + 1);
#endif

	return (wdt->state & TWL4030_WDT_STATE_ACTIVE) ?
		twl4030_wdt_disable(wdt) : 0;
}

static int twl4030_wdt_resume(struct platform_device *pdev)
{
	struct twl4030_wdt *wdt = platform_get_drvdata(pdev);
	return (wdt->state & TWL4030_WDT_STATE_ACTIVE) ?
		twl4030_wdt_enable(wdt) : 0;
}

static const struct file_operations twl4030_wdt_fops = {
	.owner		= THIS_MODULE,
	.open		= twl4030_wdt_open,
	.release	= twl4030_wdt_release,
	.unlocked_ioctl	= twl4030_wdt_ioctl,
	.write		= twl4030_wdt_write_fop,
};

static int __devinit twl4030_wdt_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct twl4030_wdt *wdt;

	wdt = kzalloc(sizeof(struct twl4030_wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	wdt->state		= 0;
	wdt->timer_margin	= 30;
	wdt->miscdev.parent	= &pdev->dev;
	wdt->miscdev.fops	= &twl4030_wdt_fops;

	/* RX51 HACK: Make it register as yet another misc device,
	*  as we do already have the omap_wdt registered as watchdog */
#ifdef CONFIG_MACH_NOKIA_RX51
	wdt->miscdev.minor	= 142;
	wdt->miscdev.name	= "twl4030_wdt";
#else
	wdt->miscdev.minor	= WATCHDOG_MINOR;
	wdt->miscdev.name	= "watchdog";
#endif

	ret = misc_register(&wdt->miscdev);
	if (ret) {
		dev_err(wdt->miscdev.parent,
			"Failed to register misc device\n");
		kfree(wdt);
		return ret;
	}

	platform_set_drvdata(pdev, wdt);

#ifdef TWL4030_WDT_ENABLE_POWEROFF_ON_SUSPEND
	twl4030_wdt_debugfs = debugfs_create_dir("twl4030_wdt", NULL);
	if (!IS_ERR(twl4030_wdt_debugfs) && twl4030_wdt_debugfs)
		debugfs_create_u8("poweroff_on_suspend", 0644,
			twl4030_wdt_debugfs, &wdt->poweroff_on_suspend);
#endif
	twl4030_wdt_dev = pdev;
	return 0;
}

static int __devexit twl4030_wdt_remove(struct platform_device *pdev)
{
	struct twl4030_wdt *wdt = platform_get_drvdata(pdev);

	if (wdt->state & TWL4030_WDT_STATE_ACTIVE)
		if (twl4030_wdt_disable(wdt))
			return -EFAULT;
#ifdef TWL4030_WDT_ENABLE_POWEROFF_ON_SUSPEND
	if (!IS_ERR(twl4030_wdt_debugfs) && twl4030_wdt_debugfs)
		debugfs_remove_recursive(twl4030_wdt_debugfs);
#endif


	misc_deregister(&wdt->miscdev);

	platform_set_drvdata(pdev, NULL);
	kfree(wdt);
	twl4030_wdt_dev = NULL;

	return 0;
}

static struct platform_driver twl4030_wdt_driver = {
	.probe		= twl4030_wdt_probe,
	.remove		= __devexit_p(twl4030_wdt_remove),
	.suspend	= twl4030_wdt_suspend,
	.resume		= twl4030_wdt_resume,
	.driver		= {
		.name	= "twl4030_wdt",
	},
};

static int __devinit twl4030_wdt_init(void)
{
	return platform_driver_register(&twl4030_wdt_driver);
}
module_init(twl4030_wdt_init);

static void __devexit twl4030_wdt_exit(void)
{
	platform_driver_unregister(&twl4030_wdt_driver);
}
module_exit(twl4030_wdt_exit);

MODULE_AUTHOR("Nokia Corporation");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:twl4030_wdt");
