/*
 * ami305.c is driver for AMI305 (Aichi Steel) and
 * AK8974 (Asahi Kasei EMD Corporation) magnetometer chip
 *
 * Copyright (C) 2009 Nokia Corporation and/or its subsidiary(-ies).
 *
 * Contact: Samu Onkalo <samu.p.onkalo@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/i2c/ami305.h>
#include <linux/input-polldev.h>
#include <linux/regulator/consumer.h>

/*
 * 16-bit registers are little-endian. LSB is at the address defined below
 * and MSB is at the next higher address.
 */
#define AMI305_SELFTEST		0x0C
#define AMI305_INFO		0x0D
#define AMI305_WHOAMI		0x0F
#define AMI305_DATA_X		0x10
#define AMI305_DATA_Y		0x12
#define AMI305_DATA_Z		0x14
#define AMI305_INT_SRC		0x16
#define AMI305_STATUS		0x18
#define AMI305_INT_CLEAR	0x1A
#define AMI305_CTRL1		0x1B
#define AMI305_CTRL2		0x1C
#define AMI305_CTRL3		0x1D
#define AMI305_INT_CTRL		0x1E
#define AMI305_OFFSET_X		0x20
#define AMI305_OFFSET_Y		0x22
#define AMI305_OFFSET_Z		0x24
#define AMI305_INT_THRES	0x26  /* absolute any axis value threshold */
#define AMI305_PRESET		0x30
#define AMI305_TEMP		0x31

#define AMI305_SELFTEST_IDLE	0x55
#define AMI305_SELFTEST_OK	0xAA

#define AMI305_WHOAMI_VALUE_AMI305 0x47
#define AMI305_WHOAMI_VALUE_AK8974 0x48

#define AMI305_INT_X_HIGH	0x80  /* Axis over +threshold  */
#define AMI305_INT_Y_HIGH	0x40
#define AMI305_INT_Z_HIGH	0x20
#define AMI305_INT_X_LOW	0x10  /* Axis below -threshold	*/
#define AMI305_INT_Y_LOW	0x08
#define AMI305_INT_Z_LOW	0x04
#define AMI305_INT_RANGE	0x02  /* Range overflow (any axis)   */

#define AMI305_STATUS_DRDY	0x40  /* Data ready	    */
#define AMI305_STATUS_OVERRUN	0x20  /* Data overrun	    */
#define AMI305_STATUS_INT	0x10  /* Interrupt occurred */

#define AMI305_CTRL1_POWER	0x80  /* 0 = standby; 1 = active */
#define AMI305_CTRL1_RATE	0x10  /* 0 = 10 Hz;   1 = 20 Hz	 */
#define AMI305_CTRL1_FORCE_EN	0x02  /* 0 = normal;  1 = force	 */
#define AMI305_CTRL1_MODE2	0x01  /* 0 */

#define AMI305_CTRL2_INT_EN	0x10  /* 1 = enable interrupts	      */
#define AMI305_CTRL2_DRDY_EN	0x08  /* 1 = enable data ready signal */
#define AMI305_CTRL2_DRDY_POL	0x04  /* 1 = data ready active high   */
#define AMI305_CTRL2_RESDEF     (AMI305_CTRL2_DRDY_POL)

#define AMI305_CTRL3_RESET	0x80  /* Software reset		  */
#define AMI305_CTRL3_FORCE	0x40  /* Start forced measurement */
#define AMI305_CTRL3_SELFTEST	0x10  /* Set selftest register	  */
#define AMI305_CTRL3_RESDEF     0x00

#define AMI305_INT_CTRL_XEN	0x80  /* Enable interrupt for this axis */
#define AMI305_INT_CTRL_YEN	0x40
#define AMI305_INT_CTRL_ZEN	0x20
#define AMI305_INT_CTRL_XYZEN	0xE0
#define AMI305_INT_CTRL_POL	0x08  /* 0 = active low; 1 = active high     */
#define AMI305_INT_CTRL_PULSE	0x02  /* 0 = latched;	 1 = pulse (50 usec) */
#define AMI305_INT_CTRL_RESDEF  (AMI305_INT_CTRL_XYZEN | AMI305_INT_CTRL_POL)

#define AMI305_MAX_RANGE	2048
#define AMI305_THRESHOLD_MAX	(AMI305_MAX_RANGE - 1)

#define AMI305_POLL_INTERVAL	1000  /* ms */
#define AMI305_POLL_MIN		0     /* ms */
#define AMI305_POLL_MAX		10000 /* ms */
#define AMI305_ACTIVATE_DELAY	1     /* ms */
#define AMI305_POWERON_DELAY	50    /* ms */
#define AMI305_DEFAULT_FUZZ	3     /* input noise filtering */
#define AMI305_MEAS_DELAY	6     /* one measurement in ms */
#define AMI305_SELFTEST_DELAY	1     /* ms. Spec says 200us min */

#define AMI305_PWR_ON		1
#define AMI305_PWR_OFF		0

#define AMI305_MAX_TRY		2

static const char reg_avdd[] = "AVdd";
static const char reg_dvdd[] = "DVdd";

struct ami305_chip {
	struct mutex		lock;	/* Serialize access to chip */
	struct mutex		users_lock;
	struct i2c_client	*client;
	struct input_polled_dev *idev;	   /* input device */
	struct regulator_bulk_data regs[2];

	int			max_range;
	int			users;

	const char		*id;
	u8			info[2];

	s16			x, y, z; /* Latest measurements */
	s8			axis_x;
	s8			axis_y;
	s8			axis_z;

	char			name[20];
	char			phys[15];
};

static inline int ami305_write(struct ami305_chip *chip, u8 reg, u8 data)
{
	return i2c_smbus_write_byte_data(chip->client, reg, data);
}

static inline int ami305_read(struct ami305_chip *chip, u8 reg)
{
	return i2c_smbus_read_byte_data(chip->client, reg);
}

static inline int ami305_read_block(struct ami305_chip *chip, u8 reg,
			     u8 *data, u8 length)
{
	return i2c_smbus_read_i2c_block_data(chip->client, reg, length, data);
}

static int ami305_regulators_on(struct ami305_chip *chip)
{
	int ret;
	ret = regulator_bulk_enable(ARRAY_SIZE(chip->regs), chip->regs);
	if (ret == 0)
		msleep(AMI305_POWERON_DELAY);

	return ret;
}

static void ami305_regulators_off(struct ami305_chip *chip)
{
	regulator_bulk_disable(ARRAY_SIZE(chip->regs), chip->regs);
}

static int ami305_power(struct ami305_chip *chip, int poweron)
{
	int r, v;

	v = poweron ? AMI305_CTRL1_POWER : 0;
	v = v | AMI305_CTRL1_FORCE_EN;
	r = ami305_write(chip, AMI305_CTRL1, v);
	if (r < 0)
		return r;

	if (poweron)
		msleep(AMI305_ACTIVATE_DELAY);

	return 0;
}

static int ami305_start_measurement(struct ami305_chip *chip)
{
	int ctrl3;
	int ret = 0;

	ctrl3 = ami305_read(chip, AMI305_CTRL3);
	if (ctrl3 < 0)
		return ctrl3;

	ret = ami305_write(chip, AMI305_CTRL3, ctrl3 | AMI305_CTRL3_FORCE);

	return ret;
}

static int ami305_reset(struct ami305_chip *chip)
{
	int r;

	/* Power on to get register access. Sets CTRL1 reg to reset state */
	r = ami305_power(chip, AMI305_PWR_ON);
	if (r < 0)
		goto out;

	r = ami305_write(chip, AMI305_CTRL2, AMI305_CTRL2_RESDEF);
	if (r < 0)
		goto out;

	r = ami305_write(chip, AMI305_CTRL3, AMI305_CTRL3_RESDEF);
	if (r < 0)
		goto out;

	r = ami305_write(chip, AMI305_INT_CTRL, AMI305_INT_CTRL_RESDEF);
	if (r < 0)
		goto out;

	/* After reset, power off is default state */
	r = ami305_power(chip, AMI305_PWR_OFF);

out:
	return r;
}

static int ami305_configure(struct ami305_chip *chip)
{
	int err;

	err = ami305_write(chip, AMI305_CTRL2, AMI305_CTRL2_DRDY_EN);
	if (err)
		goto fail;

	err = ami305_write(chip, AMI305_CTRL3, 0);
	if (err)
		goto fail;

	err = ami305_write(chip, AMI305_INT_CTRL, AMI305_INT_CTRL_POL);
	if (err)
		goto fail;

	err = ami305_write(chip, AMI305_PRESET, 0);
fail:
	return err;
}

static int ami305_add_users(struct ami305_chip *chip)
{
	int r = 0;

	mutex_lock(&chip->users_lock);

	if (chip->users == 0) {
		r = ami305_regulators_on(chip);
		if (r < 0)
			goto fail1;
		r = ami305_power(chip, AMI305_PWR_ON);
		if (r < 0)
			goto fail2;
		r = ami305_configure(chip);
		if (r < 0)
			goto fail3;
	}
	chip->users++;
	mutex_unlock(&chip->users_lock);
	return 0;
fail3:
	ami305_power(chip, AMI305_PWR_OFF);
fail2:
	ami305_regulators_off(chip);
fail1:
	mutex_unlock(&chip->users_lock);
	return r;
}

static void ami305_remove_users(struct ami305_chip *chip)
{
	mutex_lock(&chip->users_lock);

	if (chip->users != 0)
		chip->users--;

	if (chip->users == 0) {
		ami305_power(chip, AMI305_PWR_OFF);
		ami305_regulators_off(chip);
	}
	mutex_unlock(&chip->users_lock);
}

static int ami305_get_axis(s8 axis, s16 hw_values[3])
{
	if (axis > 0)
		return hw_values[axis - 1];
	else
		return -hw_values[-axis - 1];
}

static int ami305_read_values(struct ami305_chip *chip)
{
	s16 hw_values[3];
	int i;

	ami305_start_measurement(chip);

	i = AMI305_MAX_TRY;
	do {
		msleep(AMI305_MEAS_DELAY);
		if (ami305_read(chip, AMI305_STATUS) & AMI305_STATUS_DRDY)
			break;
		i--;
	} while (i > 0);

	if (i == 0)
		return -ENODEV;

	/* X, Y, Z are in conscutive addresses. 2 * 3 bytes */
	ami305_read_block(chip, AMI305_DATA_X, (u8 *)hw_values, 6);

	for (i = 0; i < 3; i++)
		hw_values[i] = le16_to_cpu(hw_values[i]);

	chip->x = ami305_get_axis(chip->axis_x, hw_values);
	chip->y = ami305_get_axis(chip->axis_y, hw_values);
	chip->z = ami305_get_axis(chip->axis_z, hw_values);

	return 0;
}

static int ami305_selftest(struct ami305_chip *chip)
{
	int r;
	int success = 0;

	ami305_add_users(chip);

	r = ami305_read(chip, AMI305_SELFTEST);
	if (r != AMI305_SELFTEST_IDLE)
		goto out;

	r = ami305_read(chip, AMI305_CTRL3);
	if (r < 0)
		goto out;

	r = ami305_write(chip, AMI305_CTRL3, r | AMI305_CTRL3_SELFTEST);
	if (r < 0)
		goto out;

	msleep(AMI305_SELFTEST_DELAY);

	r = ami305_read(chip, AMI305_SELFTEST);
	if (r != AMI305_SELFTEST_OK)
		goto out;

	r = ami305_read(chip, AMI305_SELFTEST);
	if (r == AMI305_SELFTEST_IDLE)
		success = 1;

 out:
	ami305_remove_users(chip);

	return success;
}

/*
 * SYSFS interface
 */

static ssize_t ami305_show_selftest(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct ami305_chip *chip = dev_get_drvdata(dev);
	char *status;

	mutex_lock(&chip->lock);
	status = ami305_selftest(chip) ? "OK" : "FAIL";
	mutex_unlock(&chip->lock);

	return sprintf(buf, "%s\n", status);
}

static DEVICE_ATTR(selftest, S_IRUGO, ami305_show_selftest, NULL);

static ssize_t ami305_show_active(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct ami305_chip *chip = dev_get_drvdata(dev);
	int status;

	/* Read from the chip to reflect real state of the HW */
	status = ami305_read(chip, AMI305_CTRL1) & AMI305_CTRL1_POWER;
	return sprintf(buf, "%s\n", status ? "ON" : "OFF");
}

static DEVICE_ATTR(active, S_IRUGO, ami305_show_active, NULL);

static ssize_t ami305_show_chip_id(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct ami305_chip *chip = dev_get_drvdata(dev);
	return sprintf(buf, "%s rev %d\n", chip->id,
		chip->info[0] | (u16)chip->info[1] << 8);
}

static DEVICE_ATTR(chip_id, S_IRUGO, ami305_show_chip_id, NULL);

static struct attribute *sysfs_attrs[] = {
	&dev_attr_selftest.attr,
	&dev_attr_active.attr,
	&dev_attr_chip_id.attr,
	NULL
};

static struct attribute_group ami305_attribute_group = {
	.attrs = sysfs_attrs
};

/*
 * Polled input device interface
 */

static void ami305_poll(struct input_polled_dev *pidev)
{
	struct ami305_chip *chip = pidev->private;

	mutex_lock(&chip->lock);

	ami305_read_values(chip);
	input_report_abs(pidev->input, ABS_X, chip->x);
	input_report_abs(pidev->input, ABS_Y, chip->y);
	input_report_abs(pidev->input, ABS_Z, chip->z);
	input_sync(pidev->input);

	mutex_unlock(&chip->lock);
}

static void ami305_open(struct input_polled_dev *pidev)
{
	struct ami305_chip *chip = pidev->private;
	ami305_add_users(chip);
	ami305_poll(pidev);
}

static void ami305_close(struct input_polled_dev *pidev)
{
	struct ami305_chip *chip = pidev->private;
	ami305_remove_users(chip);
}

static int ami305_inputdev_enable(struct ami305_chip *chip,
		const struct i2c_device_id *id)
{
	struct input_dev *input_dev;
	int range, fuzz;
	int err;

	if (chip->idev)
		return -EINVAL;

	chip->idev = input_allocate_polled_device();
	if (!chip->idev)
		return -ENOMEM;

	chip->idev->private	  = chip;
	chip->idev->poll	  = ami305_poll;
	chip->idev->close	  = ami305_close;
	chip->idev->open	  = ami305_open;

	chip->idev->poll_interval     = AMI305_POLL_INTERVAL;
	chip->idev->poll_interval_max = AMI305_POLL_MAX;
	chip->idev->poll_interval_min = AMI305_POLL_MIN;

	snprintf(chip->name, sizeof(chip->name), "%s magnetometer", id->name);
	snprintf(chip->phys, sizeof(chip->phys), "%s/input0", id->name);

	input_dev	      = chip->idev->input;
	input_dev->name	      = chip->name;
	input_dev->phys	      = chip->phys;
	input_dev->id.bustype = BUS_HOST;
	input_dev->id.vendor  = 0;
	input_dev->dev.parent = &chip->client->dev;

	set_bit(EV_ABS, input_dev->evbit);

	range = chip->max_range;
	fuzz = AMI305_DEFAULT_FUZZ;
	input_set_abs_params(input_dev, ABS_X, -range, range, fuzz, 0);
	input_set_abs_params(input_dev, ABS_Y, -range, range, fuzz, 0);
	input_set_abs_params(input_dev, ABS_Z, -range, range, fuzz, 0);

	err = input_register_polled_device(chip->idev);
	if (err) {
		input_free_polled_device(chip->idev);
		chip->idev = NULL;
	}

	return err;
}

static int __devinit ami305_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct ami305_chip *chip;
	struct ami305_platform_data *pdata;
	int err;
	int whoami;
	int x, y, z;

	chip = kzalloc(sizeof *chip, GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	i2c_set_clientdata(client, chip);
	chip->client  = client;

	pdata = client->dev.platform_data;

	x = AMI305_DEV_X;
	y = AMI305_DEV_Y;
	z = AMI305_DEV_Z;

	if (pdata) {
		/* Remap axes */
		x = pdata->axis_x ? pdata->axis_x : x;
		y = pdata->axis_y ? pdata->axis_y : y;
		z = pdata->axis_z ? pdata->axis_z : z;
	}

	if ((abs(x) > 3) | (abs(y) > 3) | (abs(z) > 3)) {
		dev_err(&client->dev, "Incorrect platform data\n");
		err = -EINVAL;
		goto fail1;
	}

	chip->axis_x = x;
	chip->axis_y = y;
	chip->axis_z = z;

	chip->max_range = AMI305_MAX_RANGE;

	mutex_init(&chip->lock);
	mutex_init(&chip->users_lock);

	chip->regs[0].supply = reg_avdd;
	chip->regs[1].supply = reg_dvdd;

	err = regulator_bulk_get(&client->dev,
				 ARRAY_SIZE(chip->regs), chip->regs);
	if (err < 0) {
		dev_err(&client->dev, "Cannot get regulators\n");
		goto fail1;
	}

	err = ami305_regulators_on(chip);
	if (err < 0) {
		dev_err(&client->dev, "Cannot enable regulators\n");
		goto fail2;
	}

	whoami = ami305_read(chip, AMI305_WHOAMI);
	if (whoami < 0) {
		dev_err(&client->dev, "device not found\n");
		err = -ENODEV;
		goto fail3;
	}

	ami305_read_block(chip, AMI305_INFO, chip->info, 2);

	switch (whoami) {
	case AMI305_WHOAMI_VALUE_AMI305:
		chip->id = client->driver->id_table[0].name;
		break;
	case AMI305_WHOAMI_VALUE_AK8974:
		chip->id = client->driver->id_table[1].name;
		break;
	default:
		dev_dbg(&client->dev, "unsupported device\n");
		err = -ENODEV;
		goto fail3;
	}

	err = ami305_reset(chip);
	if (err) {
		dev_err(&client->dev, "Chip reset failed");
		goto fail3;
	}

	err = ami305_inputdev_enable(chip, id);
	if (err) {
		dev_err(&client->dev, "Cannot setup input device\n");
		goto fail3;
	}

	err = sysfs_create_group(&chip->client->dev.kobj,
				&ami305_attribute_group);
	if (err) {
		dev_err(&client->dev, "Sysfs registration failed\n");
		goto fail4;
	}

	ami305_regulators_off(chip);

	return 0;
fail4:
	input_unregister_polled_device(chip->idev);
	input_free_polled_device(chip->idev);
fail3:
	ami305_regulators_off(chip);
fail2:
	regulator_bulk_free(ARRAY_SIZE(chip->regs), chip->regs);
fail1:
	kfree(chip);
	return err;
}

static int __devexit ami305_remove(struct i2c_client *client)
{
	struct ami305_chip *chip = i2c_get_clientdata(client);

	sysfs_remove_group(&chip->client->dev.kobj,
			&ami305_attribute_group);
	input_unregister_polled_device(chip->idev);
	input_free_polled_device(chip->idev);
	/* No users after sysfs removal and unregisterin of input device */
	regulator_bulk_free(ARRAY_SIZE(chip->regs), chip->regs);
	kfree(chip);
	return 0;
}

#ifdef CONFIG_PM
static int ami305_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct ami305_chip *chip = i2c_get_clientdata(client);
	ami305_power(chip, AMI305_PWR_OFF);
	return 0;
}

static int ami305_resume(struct i2c_client *client)
{
	struct ami305_chip *chip = i2c_get_clientdata(client);
	ami305_power(chip, AMI305_PWR_ON);
	return 0;
}

static void ami305_shutdown(struct i2c_client *client)
{
	struct ami305_chip *chip = i2c_get_clientdata(client);
	ami305_power(chip, AMI305_PWR_OFF);
}

#else
#define ami305_suspend NULL
#define ami305_shutdown NULL
#define ami305_resume NULL
#endif

static const struct i2c_device_id ami305_id[] = {
	{"ami305", 0 },
	{"ak8974", 0 },
	{}
};

MODULE_DEVICE_TABLE(i2c, ami305_id);

static struct i2c_driver ami305_driver = {
	.driver	 = {
		.name	= "ami305",
		.owner	= THIS_MODULE,
	},
	.suspend = ami305_suspend,
	.shutdown = ami305_shutdown,
	.resume = ami305_resume,
	.probe	= ami305_probe,
	.remove = __devexit_p(ami305_remove),
	.id_table = ami305_id,
};

static int __init ami305_init(void)
{
	return i2c_add_driver(&ami305_driver);
}

static void __exit ami305_exit(void)
{
	i2c_del_driver(&ami305_driver);
}

MODULE_DESCRIPTION("AMI305 3-axis magnetometer driver");
MODULE_AUTHOR("Samu Onkalo, Nokia Corporation");
MODULE_LICENSE("GPL v2");

module_init(ami305_init);
module_exit(ami305_exit);
