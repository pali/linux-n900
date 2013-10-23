/*
 *  nokia-av.c - Nokia AV accessory detection
 *
 *  Copyright (C) 2008 Nokia Corporation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/i2c/twl4030-madc.h>
#include <linux/nokia-av.h>

#include <sound/jack.h>

#include <mach/mux.h>
#include <mach/gpio-switch.h>

#include <asm/system.h>

/* FIXME */
#include "../../sound/soc/omap/rx51.h"

#define DRIVER_NAME "nokia-av"

#define HS_BTN_KEY		KEY_PHONE
#define HS_BTN_IRQ_FLAGS	(IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING)
#define HS_BTN_DEBOUNCE_PRESS	100
#define HS_BTN_DEBOUNCE_RELEASE	100
#define HS_BTN_REPORT_DELAY	1000

#define HEADPH_IRQ_FLAGS	(IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING |\
				IRQF_SHARED)
#define HEADPH_DEBOUNCE		300

#define DET_OPEN_CABLE_DELAY	100
#define DET_REPEAT_DELAY	50
#define DET_PLUG_DELAY		10
#define DET_PROBE_DELAY		10
#define DET_ECI_RESET_DELAY	600
#define DET_REPEAT_COUNT	5
#define DET_COUNT_MAX		10

enum {
	UNKNOWN,
	HEADPHONES, /* or line input cable or external mic */
	VIDEO_CABLE,
	OPEN_CABLE,
	BASIC_HEADSET,
};

struct nokia_av_drvdata {
	struct device *dev;
	struct mutex lock;
	struct mutex detection_lock;

	struct workqueue_struct *workqueue;
	struct work_struct headph_work;
	struct timer_list headph_timer;
	struct delayed_work detection_work;

	struct input_dev *input;
	struct work_struct hs_btn_work;
	struct timer_list hs_btn_timer;
	struct delayed_work hs_btn_report_work;
	int hs_btn_pressed;

	int autodetect;
	int type;
	int dettype;
	int detcount;
	int dettotal;

	int eci0_gpio;
	int eci1_gpio;
	int headph_gpio;
	int headph_plugged;
};

/* Delayed reporting of button press-release cycle */
static void hs_btn_report(struct work_struct *work)
{
	struct nokia_av_drvdata *drvdata = container_of(work,
		struct nokia_av_drvdata, hs_btn_report_work.work);

	/* Don't report if unplugged */
	if (drvdata->input && drvdata->headph_plugged) {
		input_report_key(drvdata->input, HS_BTN_KEY, 1);
		input_sync(drvdata->input);
		input_report_key(drvdata->input, HS_BTN_KEY, 0);
		input_sync(drvdata->input);
	}
}

/* Timer for debouncing */
static void hs_btn_timer(unsigned long arg)
{
	struct nokia_av_drvdata *drvdata = (struct nokia_av_drvdata *) arg;

	schedule_work(&drvdata->hs_btn_work);
}

/* Handle debounced button press/release */
static void hs_btn_handler(struct work_struct *work)
{
	struct nokia_av_drvdata *drvdata =
		container_of(work, struct nokia_av_drvdata, hs_btn_work);
	int pressed;

	if (!allow_button_press())
		return;

	pressed = !gpio_get_value(drvdata->eci0_gpio);
	if (drvdata->hs_btn_pressed == pressed)
		return;

	drvdata->hs_btn_pressed = pressed;

	/* Only report on key release */
	if (drvdata->type == BASIC_HEADSET && !pressed) {
		/* Delay reporting to avoid false events on unplug */
		queue_delayed_work(drvdata->workqueue,
				&drvdata->hs_btn_report_work,
				msecs_to_jiffies(HS_BTN_REPORT_DELAY));
	}
}

/* Button press/release */
static irqreturn_t hs_btn_irq(int irq, void *_drvdata)
{
	struct nokia_av_drvdata *drvdata = _drvdata;
	int pressed, timeout;

	pressed = !gpio_get_value(drvdata->eci0_gpio);
	if (drvdata->hs_btn_pressed == pressed)
		return IRQ_HANDLED;

	if (pressed)
		timeout = HS_BTN_DEBOUNCE_PRESS;
	else
		timeout = HS_BTN_DEBOUNCE_RELEASE;

	if (!timeout)
		schedule_work(&drvdata->hs_btn_work);
	else
		mod_timer(&drvdata->hs_btn_timer,
			jiffies + msecs_to_jiffies(timeout));

	return IRQ_HANDLED;
}

static int hs_btn_input_init(struct nokia_av_drvdata *drvdata)
{
	int ret;

	if (drvdata->input)
		return -EEXIST;

	drvdata->hs_btn_pressed = 0;

	drvdata->input = input_allocate_device();
	if (!drvdata->input) {
		dev_err(drvdata->dev, "Could not allocate input device\n");
		ret = -ENOMEM;
		return ret;
	}

	input_set_capability(drvdata->input, EV_KEY, HS_BTN_KEY);
	drvdata->input->name = "headset button";

	ret = input_register_device(drvdata->input);
	if (ret) {
		dev_err(drvdata->dev, "Could not register input device\n");
		input_free_device(drvdata->input);
		drvdata->input = NULL;
		return ret;
	}

	ret = request_irq(gpio_to_irq(drvdata->eci0_gpio), hs_btn_irq,
			HS_BTN_IRQ_FLAGS, "hs_btn", drvdata);
	if (ret) {
		dev_err(drvdata->dev, "Could not request irq %d\n",
			gpio_to_irq(drvdata->eci0_gpio));
		input_unregister_device(drvdata->input);
		drvdata->input = NULL;
		return ret;
	}

	return 0;
}

static void hs_btn_input_free(struct nokia_av_drvdata *drvdata)
{
	if (!drvdata->input)
		return;

	free_irq(gpio_to_irq(drvdata->eci0_gpio), drvdata);

	del_timer(&drvdata->hs_btn_timer);
	cancel_delayed_work(&drvdata->hs_btn_report_work);

	input_unregister_device(drvdata->input);
	drvdata->input = NULL;
}

static int madc(void)
{
	struct twl4030_madc_request req;

	req.channels = (1 << 2);
	req.do_avg = 0;
	req.method = TWL4030_MADC_SW1;
	req.active = 0;
	req.func_cb = NULL;
	twl4030_madc_conversion(&req);

	return req.rbuf[2];
}

/* Get voltage in mV */
static inline int madc_voltage(void)
{
	return madc()*147/60;
}

/* Get voltage in mV, wait for voltage to settle to smaller than d/t
 * mV/ms slope, wait at most tmax ms (not accurate, but just to make
 * sure it's less than infinity) */
static int madc_stable_voltage(int d, int t, int tmax)
{
	int mv1, mv2, dmv;

	mv1 = madc_voltage();
	do {
		if (t) {
			msleep(t);
			tmax -= t;
		} else {
			tmax--;
		}
		mv2 = madc_voltage();
		dmv = abs(mv2 - mv1);
		mv1 = mv2;
	} while (dmv > d && tmax > 0);

	return mv2;
}

/* < pre-B3 */
#define NEED_BIAS_CORRECTION(hwid)	(((hwid) > 0x0013 && \
					(hwid) <= 0x1700) || ((hwid) < 0x0008))
#define BIAS_CORRECTION		80

#define THRESHOLD_GROUNDED	40
#define THRESHOLD_VIDEO_HI	150
#define THRESHOLD_HEADSET_HI	1000
#define THRESHOLD_ECI_LO	1950
#define THRESHOLD_ECI_HI	2200

static int detect(struct nokia_av_drvdata *drvdata)
{
	int mv;
	int type = UNKNOWN;

	mutex_lock(&drvdata->detection_lock);

	rx51_set_eci_mode(4);
	msleep(20);

	/* Detection point 1 */
	if (gpio_get_value(drvdata->eci0_gpio)) {
		rx51_set_eci_mode(3);
		msleep(20);

		/* Detection point 3 */
		if (gpio_get_value(drvdata->eci1_gpio)) {
			type = OPEN_CABLE;
			goto done;
		}

		rx51_set_eci_mode(0);

		/* Detection point 4 */
		mv = madc_stable_voltage(50, 5, 100);
		if (mv < THRESHOLD_HEADSET_HI) {
			type = BASIC_HEADSET;
			goto done;
		}
	} else {
		/* Detection point 2 */
		mv = madc_voltage();

		/* Measurements made with mic bias need to be
		 * corrected on old hardware revisions */
		if (NEED_BIAS_CORRECTION(system_rev))
			mv -= BIAS_CORRECTION;

		if (mv < THRESHOLD_GROUNDED) {
			type = HEADPHONES;
			goto done;
		}

		if (mv < THRESHOLD_VIDEO_HI) {
			type = VIDEO_CABLE;
			goto done;
		}
	}
done:
	rx51_set_eci_mode(1);
	mutex_unlock(&drvdata->detection_lock);

	return type;
}

/* HACK: Try to detect ECI headsets */
static int detect_eci(struct nokia_av_drvdata *drvdata)
{
	int t = DET_ECI_RESET_DELAY;
	int mv;
	int type = UNKNOWN;

	mutex_lock(&drvdata->detection_lock);

	rx51_set_eci_mode(4);

	/* Give the ECI headset sufficient time (more than 500 ms) to
	 * reset and stabilize the mic line, bail out on unplug */
	while (t > 0) {
		if (!drvdata->headph_plugged) {
			type = -1;
			goto out;
		}

		msleep(t < 100 ? t : 100);
		t -= 100;
	}

	mv = madc_stable_voltage(50, 5, 100);
	if (mv > THRESHOLD_ECI_LO && mv < THRESHOLD_ECI_HI)
		type = BASIC_HEADSET;

	rx51_set_eci_mode(1);
out:
	mutex_unlock(&drvdata->detection_lock);

	return type;
}

/* Main accessory detection routine. */
static void detection_handler(struct work_struct *work)
{
	struct nokia_av_drvdata *drvdata = container_of(work,
		struct nokia_av_drvdata, detection_work.work);
	int type;

	/* This is a shortcut detection for connecting open cable */
	if (drvdata->type == OPEN_CABLE && gpio_get_value(drvdata->eci1_gpio)) {
		queue_delayed_work(drvdata->workqueue,
				&drvdata->detection_work,
				msecs_to_jiffies(DET_OPEN_CABLE_DELAY));
		return;
	}
	drvdata->type = UNKNOWN;

	type = detect(drvdata);

	mutex_lock(&drvdata->lock);

	/* Unplug in the middle of detection */
	if (!drvdata->headph_plugged)
		goto out;

	if (type == drvdata->dettype) {
		drvdata->detcount++;
	} else {
		drvdata->detcount = 1;
		drvdata->dettype = type;
	}

	drvdata->dettotal++;

	if (drvdata->detcount >= DET_REPEAT_COUNT ||
		drvdata->dettotal >= DET_COUNT_MAX) {
		int status = 0;

		/* HACK: Try to detect the accessory as an ECI headset
		 * only if unable to detect it as anything else. */
		if (type == UNKNOWN || drvdata->dettotal >= DET_COUNT_MAX) {
			/* Unlock to allow headph_handler to work */
			mutex_unlock(&drvdata->lock);
			type = detect_eci(drvdata);
			mutex_lock(&drvdata->lock);

			/* Unplug in the middle of ECI detection. */
			if (type < 0 || !drvdata->headph_plugged)
				goto out;
		}

		drvdata->type = type;
		drvdata->dettype = UNKNOWN;
		drvdata->detcount = 0;
		drvdata->dettotal = 0;

		switch (type) {
		case BASIC_HEADSET:
			status = SND_JACK_HEADSET;
			hs_btn_input_init(drvdata);
			break;
		case HEADPHONES:
			status = SND_JACK_HEADPHONE;
			break;
		case VIDEO_CABLE:
			status = SND_JACK_AVOUT;
			break;
		case OPEN_CABLE:
			rx51_set_eci_mode(3); /* Detect connection */
			queue_delayed_work(drvdata->workqueue,
					&drvdata->detection_work,
					msecs_to_jiffies(DET_OPEN_CABLE_DELAY));
			break;
		}
		status |= SND_JACK_MECHANICAL;

		rx51_jack_report(status);

	} else {
		queue_delayed_work(drvdata->workqueue,
				&drvdata->detection_work,
				msecs_to_jiffies(DET_REPEAT_DELAY));
	}
out:
	mutex_unlock(&drvdata->lock);
}

/* Debounced headphone plug handler */
static void headph_handler(struct work_struct *work)
{
	struct nokia_av_drvdata *drvdata =
		container_of(work, struct nokia_av_drvdata, headph_work);
	int plugged;

	if (!drvdata->autodetect) {
		return;
	}

	plugged = !gpio_get_value(drvdata->headph_gpio);
	if (drvdata->headph_plugged == plugged)
		return;

	mutex_lock(&drvdata->lock);

	drvdata->headph_plugged = plugged;

	drvdata->type = UNKNOWN;
	drvdata->dettype = UNKNOWN;
	drvdata->detcount = 0;
	drvdata->dettotal = 0;

	hs_btn_input_free(drvdata);

	mutex_unlock(&drvdata->lock);

	if (drvdata->headph_plugged) {
		queue_delayed_work(drvdata->workqueue,
				&drvdata->detection_work,
				msecs_to_jiffies(DET_PLUG_DELAY));
	} else {
		cancel_delayed_work_sync(&drvdata->detection_work);

		rx51_set_eci_mode(1);
		rx51_jack_report(0);
	}
}

/* Headphone plug debounce timer */
static void headph_timer(unsigned long arg)
{
	struct nokia_av_drvdata *drvdata = (struct nokia_av_drvdata *) arg;

	schedule_work(&drvdata->headph_work);
}

/* Headphone plug irq */
static irqreturn_t headph_irq(int irq, void *_drvdata)
{
	struct nokia_av_drvdata *drvdata = _drvdata;

	mod_timer(&drvdata->headph_timer,
		jiffies + msecs_to_jiffies(HEADPH_DEBOUNCE));

	return IRQ_HANDLED;
}

static ssize_t detect_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct nokia_av_drvdata *drvdata = dev_get_drvdata(dev);
	int type;

	type = detect(drvdata);

	return snprintf(buf, PAGE_SIZE, "%d\n", type);
}

static ssize_t eci0_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct nokia_av_drvdata *drvdata = dev_get_drvdata(dev);
	int val = gpio_get_value(drvdata->eci0_gpio);

	return snprintf(buf, PAGE_SIZE, "%d\n", val);
}

static ssize_t eci1_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct nokia_av_drvdata *drvdata = dev_get_drvdata(dev);
	int val = gpio_get_value(drvdata->eci1_gpio);

	return snprintf(buf, PAGE_SIZE, "%d\n", val);
}

static ssize_t autodetect_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct nokia_av_drvdata *drvdata = dev_get_drvdata(dev);
	int val;

	val = drvdata->autodetect;

	return snprintf(buf, PAGE_SIZE, "%d\n", val);
}

static ssize_t autodetect_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t len)
{
	struct nokia_av_drvdata *drvdata = dev_get_drvdata(dev);
	int val;

	if (sscanf(buf, "%d", &val) != 1 || val < 0 || val > 1)
		return -EINVAL;

	drvdata->autodetect = val;

	return len;
}

static ssize_t type_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct nokia_av_drvdata *drvdata = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", drvdata->type);
}

static ssize_t madc_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", madc());
}

static DEVICE_ATTR(detect, S_IRUGO, detect_show, NULL);
static DEVICE_ATTR(type, S_IRUGO, type_show, NULL);
static DEVICE_ATTR(autodetect, S_IRUGO|S_IWUGO, autodetect_show,
		autodetect_store);

static DEVICE_ATTR(eci0, S_IRUGO, eci0_show, NULL);
static DEVICE_ATTR(eci1, S_IRUGO, eci1_show, NULL);

static DEVICE_ATTR(madc, S_IRUGO, madc_show, NULL);

static struct attribute *nokia_av_attributes[] = {
	&dev_attr_detect.attr,
	&dev_attr_type.attr,
	&dev_attr_autodetect.attr,
	&dev_attr_eci0.attr,
	&dev_attr_eci1.attr,
	&dev_attr_madc.attr,
	NULL
};

static const struct attribute_group nokia_av_group = {
	.attrs = nokia_av_attributes,
};

static int nokia_av_register_sysfs(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	return sysfs_create_group(&dev->kobj, &nokia_av_group);
}

static void nokia_av_unregister_sysfs(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	sysfs_remove_group(&dev->kobj, &nokia_av_group);
}

static int __init nokia_av_probe(struct platform_device *pdev)
{
	struct nokia_av_platform_data *pdata;
	struct nokia_av_drvdata *drvdata;
	int ret;

	drvdata = kzalloc(sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata) {
		dev_err(&pdev->dev, "could not allocate memory\n");
		ret = -ENOMEM;
		goto err_alloc;
	}

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev, "no platform data?\n");
		ret = -EINVAL;
		goto err_pdata;
	}

	drvdata->workqueue = create_singlethread_workqueue(DRIVER_NAME);
	if (!drvdata->workqueue) {
		dev_err(&pdev->dev, "couldn't create workqueue\n");
		ret = -ENOMEM;
		goto err_workqueue;
	}

	drvdata->eci0_gpio = pdata->eci0_gpio;
	drvdata->eci1_gpio = pdata->eci1_gpio;
	drvdata->headph_gpio = pdata->headph_gpio;
	drvdata->autodetect = 1;

	drvdata->dev = &pdev->dev;

	mutex_init(&drvdata->lock);
	mutex_init(&drvdata->detection_lock);
	INIT_DELAYED_WORK(&drvdata->detection_work, detection_handler);

	INIT_WORK(&drvdata->hs_btn_work, hs_btn_handler);
	init_timer(&drvdata->hs_btn_timer);
	drvdata->hs_btn_timer.function = hs_btn_timer;
	drvdata->hs_btn_timer.data = (unsigned long)drvdata;
	INIT_DELAYED_WORK(&drvdata->hs_btn_report_work, hs_btn_report);

	platform_set_drvdata(pdev, drvdata);

	ret = nokia_av_register_sysfs(pdev);
	if (ret) {
		dev_err(&pdev->dev, "sysfs registration failed, %d\n", ret);
		goto err_sysfs;
	}

	ret = gpio_request(drvdata->eci0_gpio, "eci0");
	if (ret) {
		dev_err(&pdev->dev, "gpio %d request failed, %d\n",
			drvdata->eci0_gpio, ret);
		goto err_eci0;
	}


	ret = gpio_request(drvdata->eci1_gpio, "eci1");
	if (ret) {
		dev_err(&pdev->dev, "gpio %d request failed, %d\n",
			drvdata->eci1_gpio, ret);
		goto err_eci1;
	}

	gpio_direction_input(drvdata->eci0_gpio);
	gpio_direction_input(drvdata->eci1_gpio);

	/* Plug/unplug detection */
	drvdata->headph_plugged = !gpio_get_value(drvdata->headph_gpio);

	INIT_WORK(&drvdata->headph_work, headph_handler);
	init_timer(&drvdata->headph_timer);
	drvdata->headph_timer.function = headph_timer;
	drvdata->headph_timer.data = (unsigned long)drvdata;

	ret = request_irq(gpio_to_irq(drvdata->headph_gpio), headph_irq,
			HEADPH_IRQ_FLAGS, "headph", drvdata);
	if (ret) {
		dev_err(&pdev->dev, "gpio %d irq request failed, %d\n",
			drvdata->headph_gpio, ret);
		goto err_headph;
	}

	dev_info(&pdev->dev, "accessory detect module initialized\n");

	if (drvdata->headph_plugged)
		queue_delayed_work(drvdata->workqueue,
				&drvdata->detection_work,
				msecs_to_jiffies(DET_PROBE_DELAY));

	return 0;

err_headph:
	gpio_free(drvdata->eci1_gpio);

err_eci1:
	gpio_free(drvdata->eci0_gpio);

err_eci0:
	nokia_av_unregister_sysfs(pdev);

err_sysfs:
	destroy_workqueue(drvdata->workqueue);

err_workqueue:
	platform_set_drvdata(pdev, NULL);

err_pdata:
	kfree(drvdata);

err_alloc:

	return ret;
}

static int __exit nokia_av_remove(struct platform_device *pdev)
{
	struct nokia_av_drvdata *drvdata = platform_get_drvdata(pdev);

	free_irq(gpio_to_irq(drvdata->headph_gpio), drvdata);

	hs_btn_input_free(drvdata);

	nokia_av_unregister_sysfs(pdev);

	gpio_free(drvdata->eci0_gpio);
	gpio_free(drvdata->eci1_gpio);

	cancel_delayed_work_sync(&drvdata->detection_work);

	destroy_workqueue(drvdata->workqueue);

	platform_set_drvdata(pdev, NULL);
	kfree(drvdata);

	return 0;
}

static struct platform_driver nokia_av_driver = {
	.driver		= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
	},
	.probe		= nokia_av_probe,
	.remove		= __exit_p(nokia_av_remove),
};

static int __init nokia_av_init(void)
{
	return platform_driver_register(&nokia_av_driver);
}
module_init(nokia_av_init);

static void __exit nokia_av_exit(void)
{
	platform_driver_unregister(&nokia_av_driver);
}
module_exit(nokia_av_exit);

MODULE_ALIAS("platform:" DRIVER_NAME);
MODULE_AUTHOR("Nokia");
MODULE_DESCRIPTION("Nokia AV accessory detection");
MODULE_LICENSE("GPL");
