/*
 * omap34xx_temp.c - Linux kernel module for the temperature sensor inside
 * OMAP34xx and OMAP36xx.
 *
 * Copyright (C) 2008, 2009, 2010 Nokia Corporation
 *
 * Written by Peter De Schrijver <peter.de-schrijver@nokia.com>
 *
 * Inspired by k8temp.c
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file "COPYING" in the main directory of this
 * archive for more details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/clk.h>
#include <linux/hrtimer.h>
#include <linux/module.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/sched.h>
#include <plat/omap34xx.h>
#include <plat/control.h>

/* minimum delay for EOCZ rise after SOC rise is
 * 11 cycles of the 32.768Khz clock */
#define EOCZ_MIN_RISING_DELAY (11 * 30518)

/* From docs, maximum delay for EOCZ rise after SOC rise is
 * 14 cycles of the 32.768Khz clock. But after some experiments,
 * 24 cycles as maximum is safer. */
#define EOCZ_MAX_RISING_DELAY (24 * 30518)

/* minimum delay for EOCZ falling is
 * 36 cycles of the 32.768Khz clock */
#define EOCZ_MIN_FALLING_DELAY (36 * 30518)

/* maximum delay for EOCZ falling is
 * 40 cycles of the 32.768Khz clock */
#define EOCZ_MAX_FALLING_DELAY (40 * 30518)

struct omap34xx_data {
	struct device *hwmon_dev;
	struct clk *clk_32k;
	/* mutex to protect the update procedure while reading from sensor */
	struct mutex update_lock;
	const char *name;
	unsigned long last_updated;
	u32 temp;
	int *adc_to_temp;
	u8 soc_bit, eocz_bit;
	char valid;
};

static int adc_to_temp_3430[] = {
	-400, -400, -400, -400, -400, -390, -380, -360, -340, -320, -310,
	-290, -280, -260, -250, -240, -220, -210, -190, -180, -170, -150,
	-140, -120, -110, -90, -80, -70, -50, -40, -20, -10, 00, 10, 30,
	40, 50, 70, 80, 100, 110, 130, 140, 150, 170, 180, 200, 210, 220,
	240, 250, 270, 280, 300, 310, 320, 340, 350, 370, 380, 390, 410, 420,
	440, 450, 470, 480, 490, 510, 520, 530, 550, 560, 580, 590, 600, 620,
	630, 650, 660, 670, 690, 700, 720, 730, 740, 760, 770, 790, 800, 810,
	830, 840, 850, 870, 880, 890, 910, 920, 940, 950, 960, 980, 990, 1000,
	1020, 1030, 1050, 1060, 1070, 1090, 1100, 1110, 1130, 1140, 1160,
	1170, 1180, 1200, 1210, 1220, 1240, 1240, 1250, 1250, 1250, 1250,
	1250};

static int adc_to_temp_3630[] = {
	-400, -400, -400, -400, -400, -400, -400, -400, -400, -400, -400,
	-400, -400, -400, -380, -350, -340, -320, -300, -280, -260, -240,
	-220, -200, -185, -170, -150, -135, -120, -100, -80, -65, -50, -35,
	-15, 0, 20, 35, 50, 65, 85, 100, 120, 135, 150, 170, 190, 210, 230,
	250, 270, 285, 300, 320, 335, 350, 370, 385, 400, 420, 435, 450, 470,
	485, 500, 520, 535, 550, 570, 585, 600, 620, 640, 660, 680, 700, 715,
	735, 750, 770, 785, 800, 820, 835, 850, 870, 885, 900, 920, 935, 950,
	970, 985, 1000, 1020, 1035, 1050, 1070, 1090, 1110, 1130, 1150, 1170,
	1185, 1200, 1220, 1235, 1250, 1250, 1250, 1250, 1250, 1250, 1250,
	1250, 1250, 1250, 1250, 1250, 1250, 1250, 1250, 1250, 1250, 1250,
	1250, 1250, 1250};

static inline u32 wait_for_eocz(int min_delay, int max_delay, u32 level,
				struct omap34xx_data *data)
{
	ktime_t timeout, expire;
	u32 temp_sensor_reg, eocz_mask;

	eocz_mask = BIT(data->eocz_bit);
	level &= 1;
	level *= eocz_mask;

	expire = ktime_add_ns(ktime_get(), max_delay);
	timeout = ktime_set(0, min_delay);
	__set_current_state(TASK_INTERRUPTIBLE);
	schedule_hrtimeout(&timeout, HRTIMER_MODE_REL);
	do {
		temp_sensor_reg = omap_ctrl_readl(OMAP343X_CONTROL_TEMP_SENSOR);
		if ((temp_sensor_reg & eocz_mask) == level)
			break;
	} while (ktime_us_delta(expire, ktime_get()) > 0);

	return (temp_sensor_reg & eocz_mask) == level;
}

static int omap34xx_update(struct omap34xx_data *data)
{
	int e = 0;
	u32 temp_sensor_reg;

	mutex_lock(&data->update_lock);

	if (!data->valid
	    || time_after(jiffies, data->last_updated + HZ)) {

		clk_enable(data->clk_32k);

		temp_sensor_reg = omap_ctrl_readl(OMAP343X_CONTROL_TEMP_SENSOR);
		temp_sensor_reg |= BIT(data->soc_bit);
		omap_ctrl_writel(temp_sensor_reg, OMAP343X_CONTROL_TEMP_SENSOR);

		if (!wait_for_eocz(EOCZ_MIN_RISING_DELAY,
					EOCZ_MAX_RISING_DELAY, 1, data)) {
			e = -EIO;
			goto err;
		}

		temp_sensor_reg = omap_ctrl_readl(OMAP343X_CONTROL_TEMP_SENSOR);
		temp_sensor_reg &= ~(BIT(data->soc_bit));
		omap_ctrl_writel(temp_sensor_reg, OMAP343X_CONTROL_TEMP_SENSOR);

		if (!wait_for_eocz(EOCZ_MIN_FALLING_DELAY,
					EOCZ_MAX_FALLING_DELAY, 0, data)) {
			e = -EIO;
			goto err;
		}

		data->temp = omap_ctrl_readl(OMAP343X_CONTROL_TEMP_SENSOR) &
						((1<<7) - 1);
		data->last_updated = jiffies;
		data->valid = 1;

err:
		clk_disable(data->clk_32k);
	}

	mutex_unlock(&data->update_lock);
	return e;
}

static ssize_t show_name(struct device *dev,
			struct device_attribute *devattr, char *buf)
{
	struct omap34xx_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", data->name);
}

static ssize_t show_temp_raw(struct device *dev,
			 struct device_attribute *devattr, char *buf)
{
	struct omap34xx_data *data = dev_get_drvdata(dev);
	int e;

	e = omap34xx_update(data);
	if (e < 0)
		return e;

	return sprintf(buf, "%d\n", data->temp);
}

static ssize_t show_temp(struct device *dev,
			 struct device_attribute *devattr, char *buf)
{
	struct omap34xx_data *data = dev_get_drvdata(dev);
	int temp;
	int e;

	e = omap34xx_update(data);
	if (e < 0)
		return e;

	temp = data->adc_to_temp[data->temp];

	return sprintf(buf, "%d.%d\n", temp / 10, temp % 10);
}

static SENSOR_DEVICE_ATTR_2(temp1_input, S_IRUGO, show_temp, NULL, 0, 0);
static SENSOR_DEVICE_ATTR_2(temp1_input_raw, S_IRUGO, show_temp_raw,
				NULL, 0, 0);
static DEVICE_ATTR(name, S_IRUGO, show_name, NULL);

static int __init omap34xx_temp_probe(struct platform_device *pdev)
{
	int err;
	struct omap34xx_data *data;

	data = kzalloc(sizeof(struct omap34xx_data), GFP_KERNEL);
	if (!data) {
		err = -ENOMEM;
		goto exit;
	}

	platform_set_drvdata(pdev, data);
	mutex_init(&data->update_lock);
	data->name = "omap34xx_temp";

	data->clk_32k = clk_get(&pdev->dev, "ts_fck");
	if (IS_ERR(data->clk_32k)) {
		err = PTR_ERR(data->clk_32k);
		goto exit_free;
	}

	err = device_create_file(&pdev->dev,
				 &sensor_dev_attr_temp1_input.dev_attr);
	if (err)
		goto clock_free;

	err = device_create_file(&pdev->dev,
				 &sensor_dev_attr_temp1_input_raw.dev_attr);
	if (err)
		goto exit_remove;

	err = device_create_file(&pdev->dev, &dev_attr_name);
	if (err)
		goto exit_remove_raw;

	data->hwmon_dev = hwmon_device_register(&pdev->dev);

	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto exit_remove_all;
	}

	if (cpu_is_omap3630()) {
		data->eocz_bit = 8;
		data->soc_bit = 9;
		data->adc_to_temp = adc_to_temp_3630;
	} else {
		data->eocz_bit = 7;
		data->soc_bit = 8;
		data->adc_to_temp = adc_to_temp_3430;
	}

	return 0;

exit_remove_all:
	device_remove_file(&pdev->dev, &dev_attr_name);
exit_remove_raw:
	device_remove_file(&pdev->dev,
			   &sensor_dev_attr_temp1_input_raw.dev_attr);
exit_remove:
	device_remove_file(&pdev->dev,
			   &sensor_dev_attr_temp1_input.dev_attr);
clock_free:
	clk_put(data->clk_32k);

exit_free:
	kfree(data);
	platform_set_drvdata(pdev, NULL);
exit:
	return err;
}

static int  __exit omap34xx_temp_remove(struct platform_device *pdev)
{
	struct omap34xx_data *data = platform_get_drvdata(pdev);

	if (!data)
		return 0;

	clk_put(data->clk_32k);
	hwmon_device_unregister(data->hwmon_dev);
	device_remove_file(&pdev->dev,
				&sensor_dev_attr_temp1_input_raw.dev_attr);
	device_remove_file(&pdev->dev, &sensor_dev_attr_temp1_input.dev_attr);
	device_remove_file(&pdev->dev, &dev_attr_name);
	kfree(data);

	return 0;
}

static struct platform_device *omap34xx_temp_device;

static struct platform_driver omap34xx_temp_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "omap34xx_temp",
	},
	.remove		= __exit_p(omap34xx_temp_remove),
};

static int __init omap34xx_temp_init(void)
{
	int err;

	omap34xx_temp_device = platform_device_register_simple("omap34xx_temp",
								-1, NULL, 0);
	if (IS_ERR(omap34xx_temp_device)) {
		err = PTR_ERR(omap34xx_temp_device);
		printk(KERN_ERR
			"Unable to register omap34xx temperature device\n");
		goto exit;
	}

	err = platform_driver_probe(&omap34xx_temp_driver, omap34xx_temp_probe);
	if (err)
		goto exit_pdevice;

	return 0;

exit_pdevice:
	platform_device_unregister(omap34xx_temp_device);
exit:
	return err;
}

static void __exit omap34xx_temp_exit(void)
{
	platform_device_unregister(omap34xx_temp_device);
	platform_driver_unregister(&omap34xx_temp_driver);
}

MODULE_AUTHOR("Peter De Schrijver");
MODULE_DESCRIPTION("OMAP34xx/OMAP36xx temperature sensor");
MODULE_LICENSE("GPL");

module_init(omap34xx_temp_init)
module_exit(omap34xx_temp_exit)

