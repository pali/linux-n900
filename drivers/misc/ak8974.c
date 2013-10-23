/*
 * Driver for ak8974 (Asahi Kasei EMD Corporation)
 * and ami305 (Aichi Steel) magnetometer chip.
 *
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
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
#include <asm/atomic.h>
#include <linux/delay.h>
#include <linux/i2c/ak8974.h>
#include <linux/regulator/consumer.h>
#include <linux/poll.h>
#include <linux/device.h>
#include <linux/miscdevice.h>

/*
 * 16-bit registers are little-endian. LSB is at the address defined below
 * and MSB is at the next higher address.
 */

/* for AK8974 and AMI305 registers */
#define AK8974_SELFTEST		0x0C
#define AK8974_INFO		0x0D
#define AK8974_WHOAMI		0x0F
#define AK8974_DATA_X		0x10
#define AK8974_DATA_Y		0x12
#define AK8974_DATA_Z		0x14
#define AK8974_INT_SRC		0x16
#define AK8974_STATUS		0x18
#define AK8974_INT_CLEAR	0x1A
#define AK8974_CTRL1		0x1B
#define AK8974_CTRL2		0x1C
#define AK8974_CTRL3		0x1D
#define AK8974_INT_CTRL		0x1E
#define AK8974_OFFSET_X		0x20
#define AK8974_OFFSET_Y		0x22
#define AK8974_OFFSET_Z		0x24
#define AK8974_INT_THRES	0x26  /* absolute any axis value threshold */
#define AK8974_PRESET		0x30
#define AK8974_TEMP		0x31

#define AK8974_SELFTEST_IDLE	0x55
#define AK8974_SELFTEST_OK	0xAA

#define AK8974_WHOAMI_VALUE_AMI305 0x47
#define AK8974_WHOAMI_VALUE_AK8974 0x48
#define AK8974_INT_X_HIGH	0x80  /* Axis over +threshold  */
#define AK8974_INT_Y_HIGH	0x40
#define AK8974_INT_Z_HIGH	0x20
#define AK8974_INT_X_LOW	0x10  /* Axis below -threshold	*/
#define AK8974_INT_Y_LOW	0x08
#define AK8974_INT_Z_LOW	0x04
#define AK8974_INT_RANGE	0x02  /* Range overflow (any axis)   */

#define AK8974_STATUS_DRDY	0x40  /* Data ready	    */
#define AK8974_STATUS_OVERRUN	0x20  /* Data overrun	    */
#define AK8974_STATUS_INT	0x10  /* Interrupt occurred */

#define AK8974_CTRL1_POWER	0x80  /* 0 = standby; 1 = active */
#define AK8974_CTRL1_RATE	0x10  /* 0 = 10 Hz;   1 = 20 Hz	 */
#define AK8974_CTRL1_FORCE_EN	0x02  /* 0 = normal;  1 = force	 */
#define AK8974_CTRL1_MODE2	0x01  /* 0 */

#define AK8974_CTRL2_INT_EN	0x10  /* 1 = enable interrupts	      */
#define AK8974_CTRL2_DRDY_EN	0x08  /* 1 = enable data ready signal */
#define AK8974_CTRL2_DRDY_POL	0x04  /* 1 = data ready active high   */
#define AK8974_CTRL2_RESDEF	(AK8974_CTRL2_DRDY_POL)

#define AK8974_CTRL3_RESET	0x80  /* Software reset		  */
#define AK8974_CTRL3_FORCE	0x40  /* Start forced measurement */
#define AK8974_CTRL3_SELFTEST	0x10  /* Set selftest register	  */
#define AK8974_CTRL3_RESDEF	0x00

#define AK8974_INT_CTRL_XEN	0x80  /* Enable interrupt for this axis */
#define AK8974_INT_CTRL_YEN	0x40
#define AK8974_INT_CTRL_ZEN	0x20
#define AK8974_INT_CTRL_XYZEN	0xE0
#define AK8974_INT_CTRL_POL	0x08  /* 0 = active low; 1 = active high     */
#define AK8974_INT_CTRL_PULSE	0x02  /* 0 = latched;	 1 = pulse (50 usec) */
#define AK8974_INT_CTRL_RESDEF	(AK8974_INT_CTRL_XYZEN | AK8974_INT_CTRL_POL)

#define AK8974_INT_SCR_MROI	0x02

#define AK8974_MAX_RANGE	2048

#define AK8974_POWERON_DELAY	50
#define AK8974_ACTIVATE_DELAY	1
#define AK8974_SELFTEST_DELAY	1

#define AK8974_MEASTIME		3

#define AK8974_PWR_ON		1
#define AK8974_PWR_OFF		0

struct ak8974_chip {
	struct miscdevice	miscdev;
	struct mutex		lock;	/* Serialize access to chip */
	struct mutex		users_lock;
	struct i2c_client	*client;
	struct regulator_bulk_data regs[2];
	struct delayed_work	dwork;
	wait_queue_head_t	misc_wait;
	loff_t			offset;

	int			max_range;
	int			users;

	const char		*id;
	u8			info[2];

	s16			x, y, z; /* Latest measurements */
	s8			axis_x;
	s8			axis_y;
	s8			axis_z;
	bool			valid;
	bool			meas_ongoing;

	char			name[20];
};

static const char reg_avdd[] = "AVdd";
static const char reg_dvdd[] = "DVdd";
static atomic_t device_number = ATOMIC_INIT(0);

static inline int ak8974_write(struct ak8974_chip *chip, u8 reg, u8 data)
{
	return i2c_smbus_write_byte_data(chip->client, reg, data);
}

static inline int ak8974_read(struct ak8974_chip *chip, u8 reg)
{
	return i2c_smbus_read_byte_data(chip->client, reg);
}

static inline int ak8974_read_block(struct ak8974_chip *chip, u8 reg,
				u8 *data, u8 length)
{
	return i2c_smbus_read_i2c_block_data(chip->client, reg, length, data);
}

static int ak8974_enable(struct ak8974_chip *chip, bool mode)
{
	int r, v;

	v = mode ? AK8974_CTRL1_POWER : 0;
	v |= AK8974_CTRL1_FORCE_EN;
	r = ak8974_write(chip, AK8974_CTRL1, v);
	if (r < 0)
		return r;

	if (mode)
		msleep(AK8974_ACTIVATE_DELAY);

	return 0;
}

static int ak8974_reset(struct ak8974_chip *chip)
{
	int r;

	/* Power on to get register access. Sets CTRL1 reg to reset state */
	r = ak8974_enable(chip, AK8974_PWR_ON);
	r |= ak8974_write(chip, AK8974_CTRL2, AK8974_CTRL2_RESDEF);
	r |= ak8974_write(chip, AK8974_CTRL3, AK8974_CTRL3_RESDEF);
	r |= ak8974_write(chip, AK8974_INT_CTRL, AK8974_INT_CTRL_RESDEF);

	/* After reset, power off is default state */
	r |= ak8974_enable(chip, AK8974_PWR_OFF);

	return r;
}

static int ak8974_configure(struct ak8974_chip *chip)
{
	int err;

	err = ak8974_write(chip, AK8974_CTRL2, AK8974_CTRL2_DRDY_EN |
			AK8974_CTRL2_INT_EN);
	err |= ak8974_write(chip, AK8974_CTRL3, 0);
	err |= ak8974_write(chip, AK8974_INT_CTRL, AK8974_INT_CTRL_POL);
	err |= ak8974_write(chip, AK8974_PRESET, 0);

	return err;
}

static int ak8974_poll_drdy(struct ak8974_chip *chip)
{
	int ret;

	ret = ak8974_read(chip, AK8974_STATUS);
	if (ret < 0)
		return ret;
	if (ret & AK8974_STATUS_DRDY)
		return 0;
	return ret;
}

static int ak8974_trigmeas(struct ak8974_chip *chip)
{
	int ret = 0;

	mutex_lock(&chip->lock);
	if (chip->meas_ongoing)
		goto out;

	/* Clear previous measurement overflow status */
	ak8974_read(chip, AK8974_INT_CLEAR);

	ret = ak8974_read(chip, AK8974_CTRL3);
	if (ret < 0)
		goto out;

	ret = ak8974_write(chip, AK8974_CTRL3, ret | AK8974_CTRL3_FORCE);
	if (ret < 0)
		goto out;

	chip->meas_ongoing = true;
	schedule_delayed_work(&chip->dwork,
			msecs_to_jiffies(AK8974_MEASTIME));
out:
	mutex_unlock(&chip->lock);
	return ret;
}

static int ak8974_getresult(struct ak8974_chip *chip, s16 *result)
{
	int ret;
	int i;

	ret = ak8974_poll_drdy(chip);
	/* Measurement overflow ? */
	ret |= ak8974_read(chip, AK8974_INT_SRC);
	if (ret < 0)
		return ret;
	if (ret & AK8974_INT_SCR_MROI)
		ret = -ERANGE;
	else
		ret = 0;

	ak8974_read_block(chip, AK8974_DATA_X, (u8 *)result, 6);
	for (i = 0; i < 3; i++)
		result[i] = le16_to_cpu(result[i]);

	return ret;
}

static int ak8974_selftest(struct ak8974_chip *chip)
{
	int r;
	int ret = -EIO;

	r = ak8974_read(chip, AK8974_SELFTEST);
	if (r != AK8974_SELFTEST_IDLE)
		goto out;

	r = ak8974_read(chip, AK8974_CTRL3);
	if (r < 0)
		goto out;

	r = ak8974_write(chip, AK8974_CTRL3, r | AK8974_CTRL3_SELFTEST);
	if (r < 0)
		goto out;

	msleep(AK8974_SELFTEST_DELAY);

	r = ak8974_read(chip, AK8974_SELFTEST);
	if (r != AK8974_SELFTEST_OK)
		goto out;

	r = ak8974_read(chip, AK8974_SELFTEST);
	if (r == AK8974_SELFTEST_IDLE)
		ret = 0;

 out:
	return ret;
}

static int ak8974_regulators_on(struct ak8974_chip *chip)
{
	int ret;
	ret = regulator_bulk_enable(ARRAY_SIZE(chip->regs), chip->regs);
	if (ret == 0)
		msleep(AK8974_POWERON_DELAY);

	return ret;
}

static inline void ak8974_regulators_off(struct ak8974_chip *chip)
{
	regulator_bulk_disable(ARRAY_SIZE(chip->regs), chip->regs);
}

static int ak8974_add_users(struct ak8974_chip *chip)
{
	int r = 0;
	mutex_lock(&chip->users_lock);

	if (chip->users == 0) {
		r = ak8974_regulators_on(chip);
		if (r < 0)
			goto release_lock;
		r = ak8974_enable(chip, AK8974_PWR_ON);
		if (r < 0)
			goto fail2;
		r = ak8974_configure(chip);
		if (r < 0)
			goto fail3;
	}
	chip->users++;
	goto release_lock;
fail3:
	ak8974_enable(chip, AK8974_PWR_OFF);
fail2:
	ak8974_regulators_off(chip);
release_lock:
	mutex_unlock(&chip->users_lock);
	return r;
}

static void ak8974_remove_users(struct ak8974_chip *chip)
{
	mutex_lock(&chip->users_lock);

	if (chip->users != 0)
		chip->users--;

	if (chip->users == 0) {
		cancel_delayed_work_sync(&chip->dwork);
		chip->meas_ongoing = false;
		ak8974_enable(chip, AK8974_PWR_OFF);
		ak8974_regulators_off(chip);
	}
	mutex_unlock(&chip->users_lock);
}

static int ak8974_get_axis(s8 axis, s16 hw_values[3])
{
	if (axis > 0)
		return hw_values[axis - 1];
	else
		return -hw_values[-axis - 1];
}

static void ak8974_dwork(struct work_struct *work)
{
	struct ak8974_chip *chip = container_of(work, struct ak8974_chip,
						dwork.work);
	s16 hw_values[3];
	int ret;

	mutex_lock(&chip->lock);
	ret = ak8974_getresult(chip, hw_values);
	chip->x = ak8974_get_axis(chip->axis_x, hw_values);
	chip->y = ak8974_get_axis(chip->axis_y, hw_values);
	chip->z = ak8974_get_axis(chip->axis_z, hw_values);
	chip->valid = (ret == 0) ? 1 : 0;
	chip->offset += sizeof(struct ak8974_data);
	chip->meas_ongoing = false;
	mutex_unlock(&chip->lock);
	wake_up_interruptible(&chip->misc_wait);
}

static int ak8974_misc_open(struct inode *inode, struct file *file)
{
	struct ak8974_chip *chip = container_of(file->private_data,
						struct ak8974_chip,
						miscdev);
	file->f_pos = chip->offset;
	return ak8974_add_users(chip);
}

static int ak8974_misc_close(struct inode *inode, struct file *file)
{
	struct ak8974_chip *chip = container_of(file->private_data,
						struct ak8974_chip,
						miscdev);
	ak8974_remove_users(chip);
	return 0;
}

static ssize_t ak8974_misc_read(struct file *file, char __user *buf,
			size_t count, loff_t *offset)
{
	struct ak8974_chip *chip = container_of(file->private_data,
						struct ak8974_chip,
						miscdev);
	struct ak8974_data data;

	if (count < sizeof(data))
		return -EINVAL;

	if (*offset >= chip->offset) {
		ak8974_trigmeas(chip);
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(chip->misc_wait,
						(*offset < chip->offset)))
			return -ERESTARTSYS;
	}

	mutex_lock(&chip->lock);
	data.x = chip->x;
	data.y = chip->y;
	data.z = chip->z;
	data.valid = chip->valid;
	*offset = chip->offset;
	mutex_unlock(&chip->lock);

	return copy_to_user(buf, &data, sizeof(data)) ? -EFAULT : sizeof(data);
}

static ssize_t ak8974_show_selftest(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct ak8974_chip *chip = dev_get_drvdata(dev);
	int ret;

	ak8974_add_users(chip);
	mutex_lock(&chip->lock);
	ret = ak8974_selftest(chip);
	mutex_unlock(&chip->lock);
	ak8974_remove_users(chip);

	return sprintf(buf, "%s\n", ret ? "FAIL" : "OK");
}

static DEVICE_ATTR(selftest, S_IRUGO, ak8974_show_selftest, NULL);

static ssize_t ak8974_show_chip_id(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct ak8974_chip *chip = dev_get_drvdata(dev);
	return sprintf(buf, "%s rev %d\n", chip->id,
		chip->info[0] | (u16)chip->info[1] << 8);
}

static DEVICE_ATTR(chip_id, S_IRUGO, ak8974_show_chip_id, NULL);

static ssize_t ak8974_show_range(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct ak8974_chip *chip = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", chip->max_range);
}

static DEVICE_ATTR(range, S_IRUGO, ak8974_show_range, NULL);

static struct attribute *sysfs_attrs[] = {
	&dev_attr_selftest.attr,
	&dev_attr_range.attr,
	&dev_attr_chip_id.attr,
	NULL
};

static struct attribute_group ak8974_attribute_group = {
	.attrs = sysfs_attrs
};

static const struct file_operations ak8974_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= ak8974_misc_read,
	.open		= ak8974_misc_open,
	.release	= ak8974_misc_close,
};

static int ak8974_detect(struct ak8974_chip *chip, const char *name)
{
	int whoiam;
	whoiam = ak8974_read(chip, AK8974_WHOAMI);

	switch (whoiam) {
	case AK8974_WHOAMI_VALUE_AMI305:
		chip->id = chip->client->driver->id_table[0].name;
		break;
	case AK8974_WHOAMI_VALUE_AK8974:
		chip->id = chip->client->driver->id_table[1].name;
		break;
	default:
		dev_dbg(&chip->client->dev, "unsupported device\n");
		return -ENODEV;
	}

	ak8974_read_block(chip, AK8974_INFO, chip->info, 2);

	chip->max_range = AK8974_MAX_RANGE;

	return 0;
}

static int __devinit ak8974_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct ak8974_chip *chip;
	struct ak8974_platform_data *pdata;
	int err;
	int x, y, z;

	chip = kzalloc(sizeof *chip, GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	i2c_set_clientdata(client, chip);
	chip->client  = client;

	pdata = client->dev.platform_data;

	x = AK8974_DEV_X;
	y = AK8974_DEV_Y;
	z = AK8974_DEV_Z;

	if (pdata) {
		/* Remap axes */
		x = pdata->axis_x ? pdata->axis_x : x;
		y = pdata->axis_y ? pdata->axis_y : y;
		z = pdata->axis_z ? pdata->axis_z : z;
	}

	if ((abs(x) > 3) || (abs(y) > 3) || (abs(z) > 3)) {
		dev_err(&client->dev, "Incorrect platform data\n");
		err = -EINVAL;
		goto fail1;
	}

	chip->axis_x = x;
	chip->axis_y = y;
	chip->axis_z = z;

	mutex_init(&chip->lock);
	mutex_init(&chip->users_lock);
	init_waitqueue_head(&chip->misc_wait);
	INIT_DELAYED_WORK(&chip->dwork, ak8974_dwork);

	chip->regs[0].supply = reg_avdd;
	chip->regs[1].supply = reg_dvdd;

	err = regulator_bulk_get(&client->dev,
				 ARRAY_SIZE(chip->regs), chip->regs);
	if (err < 0) {
		dev_err(&client->dev, "Cannot get regulators\n");
		goto fail1;
	}

	err = ak8974_regulators_on(chip);
	if (err < 0) {
		dev_err(&client->dev, "Cannot enable regulators\n");
		goto fail2;
	}

	err = ak8974_detect(chip, id->name);
	if (err < 0) {
		dev_err(&client->dev, "Neither AK8974 nor ami305 found \n");
		goto fail3;
	}

	snprintf(chip->name, sizeof(chip->name), "ak8974%d",
		atomic_add_return(1, &device_number) - 1);

	err = ak8974_reset(chip);
	if (err) {
		dev_err(&client->dev, "Chip reset failed");
		goto fail3;
	}

	ak8974_regulators_off(chip);

	chip->miscdev.minor  = MISC_DYNAMIC_MINOR;
	chip->miscdev.name   = chip->name;
	chip->miscdev.fops   = &ak8974_fops;
	chip->miscdev.parent = &chip->client->dev;
	err = misc_register(&chip->miscdev);
	if (err < 0) {
		dev_err(&chip->client->dev, "Device registration failed\n");
		goto fail3;
	}

	err = sysfs_create_group(&chip->client->dev.kobj,
				&ak8974_attribute_group);
	if (err) {
		dev_err(&client->dev, "Sysfs registration failed\n");
		goto fail4;
	}


	return 0;
fail4:
	misc_deregister(&chip->miscdev);
fail3:
	ak8974_regulators_off(chip);
fail2:
	regulator_bulk_free(ARRAY_SIZE(chip->regs), chip->regs);
fail1:
	cancel_delayed_work_sync(&chip->dwork);
	kfree(chip);
	return err;
}

static int __devexit ak8974_remove(struct i2c_client *client)
{
	struct ak8974_chip *chip = i2c_get_clientdata(client);
	misc_deregister(&chip->miscdev);
	cancel_delayed_work_sync(&chip->dwork);
	sysfs_remove_group(&chip->client->dev.kobj,
			&ak8974_attribute_group);
	/* No users after sysfs removal and unregisterin of input device */
	regulator_bulk_free(ARRAY_SIZE(chip->regs), chip->regs);
	kfree(chip);
	return 0;
}


#ifdef CONFIG_PM
static int ak8974_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct ak8974_chip *chip = i2c_get_clientdata(client);
	mutex_lock(&chip->users_lock);
	if (chip->users > 0)
		ak8974_enable(chip, AK8974_PWR_OFF);
	mutex_unlock(&chip->users_lock);
	return 0;
}

static int ak8974_resume(struct i2c_client *client)
{
	struct ak8974_chip *chip = i2c_get_clientdata(client);
	mutex_lock(&chip->users_lock);
	if (chip->users > 0)
		ak8974_enable(chip, AK8974_PWR_ON);
	mutex_unlock(&chip->users_lock);
	return 0;
}

static void ak8974_shutdown(struct i2c_client *client)
{
	struct ak8974_chip *chip = i2c_get_clientdata(client);

	mutex_lock(&chip->users_lock);
	if (chip->users > 0)
		ak8974_enable(chip, AK8974_PWR_OFF);

	mutex_unlock(&chip->users_lock);
}

#else
#define ak8974_suspend	NULL
#define ak8974_shutdown NULL
#define ak8974_resume	NULL
#endif

static const struct i2c_device_id ak8974_id[] = {
	{"ami305", 0 },
	{"ak8974", 0 },
	{}
};

MODULE_DEVICE_TABLE(i2c, ak8974_id);

static struct i2c_driver ak8974_driver = {
	.driver	 = {
		.name	= "ak8974",
		.owner	= THIS_MODULE,
	},
	.suspend  = ak8974_suspend,
	.shutdown = ak8974_shutdown,
	.resume	  = ak8974_resume,
	.probe	  = ak8974_probe,
	.remove	  = __devexit_p(ak8974_remove),
	.id_table = ak8974_id,
};

static int __init ak8974_init(void)
{
	return i2c_add_driver(&ak8974_driver);
}

static void __exit ak8974_exit(void)
{
	i2c_del_driver(&ak8974_driver);
}


MODULE_DESCRIPTION("AK8974/5 and AMI305 3-axis magnetometer driver");
MODULE_AUTHOR("Samu Onkalo, Nokia Corporation");
MODULE_LICENSE("GPL v2");

module_init(ak8974_init);
module_exit(ak8974_exit);
