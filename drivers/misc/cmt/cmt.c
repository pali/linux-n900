/*
 * cmt.c
 *
 * CMT support.
 *
 * Copyright (C) 2009 Nokia Corporation. All rights reserved.
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

#include <asm/atomic.h>
#include <linux/cmt.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>

/**
 * struct cmt_device - CMT device data
 * @cmt_rst_ind_tasklet: Bottom half for CMT reset line events
 * @cmt_rst_ind_gpio: GPIO number of the CMT reset line
 * @n_head: List of notifiers registered to get CMT events
 * @node: Link on the list of available CMTs
 * @device: Reference to the CMT platform device
 */
struct cmt_device {
	struct tasklet_struct		cmt_rst_ind_tasklet;
	unsigned int			cmt_rst_ind_gpio;
	struct atomic_notifier_head	n_head;
	struct list_head		node;
	struct device			*device;
};

static LIST_HEAD(cmt_list);	/* List of CMT devices */

int cmt_notifier_register(struct cmt_device *cmtdev, struct notifier_block *nb)
{
	struct cmt_device *cmt;
	int err = -ENODEV;

	if ((!cmtdev) || (!nb))
		return -EINVAL;

	list_for_each_entry(cmt, &cmt_list, node)
		if (cmt == cmtdev) {
			err = atomic_notifier_chain_register(&cmt->n_head, nb);
			break;
		}

	return err;
}
EXPORT_SYMBOL_GPL(cmt_notifier_register);

int cmt_notifier_unregister(struct cmt_device *cmtdev,
						struct notifier_block *nb)
{
	struct cmt_device *cmt;
	int err = -ENODEV;

	if ((!cmtdev) || (!nb))
		return -EINVAL;

	list_for_each_entry(cmt, &cmt_list, node)
		if (cmt == cmtdev) {
			err = atomic_notifier_chain_unregister(&cmt->n_head,
									nb);
			break;
		}

	return err;
}
EXPORT_SYMBOL_GPL(cmt_notifier_unregister);

struct cmt_device *cmt_get(const char *name)
{
	struct cmt_device *p, *cmt = ERR_PTR(-ENODEV);

	list_for_each_entry(p, &cmt_list, node)
		if (strcmp(name, dev_name(p->device)) == 0) {
			cmt = p;
			break;
		}

	return cmt;
}
EXPORT_SYMBOL_GPL(cmt_get);

void cmt_put(struct cmt_device *cmtdev)
{
}
EXPORT_SYMBOL_GPL(cmt_put);

static void do_cmt_rst_ind_tasklet(unsigned long cmtdev)
{
	struct cmt_device *cmt = (struct cmt_device *)cmtdev;

	dev_dbg(cmt->device, "*** CMT rst line change detected (%d) ***\n",
					gpio_get_value(cmt->cmt_rst_ind_gpio));
	atomic_notifier_call_chain(&cmt->n_head, CMT_RESET, NULL);
}

static irqreturn_t cmt_rst_ind_isr(int irq, void *cmtdev)
{
	struct cmt_device *cmt = (struct cmt_device *)cmtdev;

	tasklet_schedule(&cmt->cmt_rst_ind_tasklet);

	return IRQ_HANDLED;
}

static int __init cmt_probe(struct platform_device *pd)
{
	struct cmt_platform_data *pdata = pd->dev.platform_data;
	struct cmt_device *cmt;
	int irq;
	int err;
	int pflags;

	if (!pdata) {
		pr_err("CMT: No platform_data found on cmt device\n");
		return -ENXIO;
	}
	cmt = kzalloc(sizeof(*cmt), GFP_KERNEL);
	if (!cmt) {
		dev_err(&pd->dev, "Could not allocate memory for cmtdev\n");
		return -ENOMEM;
	}

	cmt->device = &pd->dev;
	cmt->cmt_rst_ind_gpio = pdata->cmt_rst_ind_gpio;
	err = gpio_request(cmt->cmt_rst_ind_gpio, "cmt_rst_ind");
	if (err < 0) {
		dev_err(&pd->dev, "Request cmt_rst_ind gpio%d failed\n",
							cmt->cmt_rst_ind_gpio);
		goto rback1;
	}
	gpio_direction_input(cmt->cmt_rst_ind_gpio);
	tasklet_init(&cmt->cmt_rst_ind_tasklet, do_cmt_rst_ind_tasklet,
							(unsigned long)cmt);
	if (pdata->cmt_ver == 1)
		pflags = IRQF_TRIGGER_FALLING;
	else
		pflags = IRQF_TRIGGER_RISING;

	irq = gpio_to_irq(cmt->cmt_rst_ind_gpio);
	err = request_irq(irq, cmt_rst_ind_isr,
		IRQF_DISABLED | pflags, "cmt_rst_ind", cmt);
	if (err < 0) {
		dev_err(&pd->dev, "Request cmt_rst_ind irq(%d) failed (flags %d)\n",
			irq, pflags);
		goto rback2;
	}
	enable_irq_wake(irq);
	ATOMIC_INIT_NOTIFIER_HEAD(&cmt->n_head);
	list_add(&cmt->node, &cmt_list);
	platform_set_drvdata(pd, cmt);

	return 0;
rback2:
	gpio_free(cmt->cmt_rst_ind_gpio);
rback1:
	kfree(cmt);

	return err;
}

static int __exit cmt_remove(struct platform_device *pd)
{
	struct cmt_device *cmt = platform_get_drvdata(pd);

	if (!cmt)
		return 0;
	platform_set_drvdata(pd, NULL);
	list_del(&cmt->node);
	disable_irq_wake(gpio_to_irq(cmt->cmt_rst_ind_gpio));
	free_irq(gpio_to_irq(cmt->cmt_rst_ind_gpio), cmt);
	tasklet_kill(&cmt->cmt_rst_ind_tasklet);
	gpio_free(cmt->cmt_rst_ind_gpio);
	kfree(cmt);

	return 0;
}

static struct platform_driver cmt_driver = {
	.remove = __exit_p(cmt_remove),
	.driver = {
		.name	= "cmt",
		.owner	= THIS_MODULE,
	},
};

static int __init cmt_init(void)
{
	pr_notice("CMT driver\n");

	return platform_driver_probe(&cmt_driver, cmt_probe);
}
module_init(cmt_init);

static void __exit cmt_exit(void)
{
	pr_notice("CMT driver exited\n");
	platform_driver_unregister(&cmt_driver);
}
module_exit(cmt_exit);

MODULE_AUTHOR("Carlos Chinea, Nokia");
MODULE_DESCRIPTION("CMT related support");
MODULE_LICENSE("GPL");
