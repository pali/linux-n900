/*
 * Driver for ak8975 (Asahi Kasei EMD Corporation) magnetometer driver
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
#include <linux/i2c/ak8975.h>
#include <linux/regulator/consumer.h>
#include <linux/poll.h>
#include <linux/device.h>
#include <linux/miscdevice.h>

/*
 * 16-bit registers are little-endian. LSB is at the address defined below
 * and MSB is at the next higher address.
 */

/* AK8975 registers */
#define AK8975_WIA		0x00   /* Device ID */
#define AK8975_INFO		0x01   /* Information */
#define AK8975_ST1		0x02   /* Status 1 */
#define AK8975_HXL		0x03   /* Measurement data X */
#define AK8975_HXH		0x04
#define AK8975_HYL		0x05   /* Measurement data Y */
#define AK8975_HYH		0x06
#define AK8975_HZL		0x07   /* Measurement data Z */
#define AK8975_HZH		0x08
#define AK8975_ST2		0x09   /* Status 2 */
#define AK8975_CNTL		0x0A   /* Control */
#define AK8975_RSV		0x0B   /* Reserved */
#define AK8975_ASTC		0x0C   /* Self-test */
#define AK8975_TS1		0x0D   /* Test 1 */
#define AK8975_TS2		0x0E   /* Test 2 */
#define AK8975_I2CDIS		0x0F   /* I2C disable */
#define AK8975_ASAX		0x10
#define AK8975_ASAY		0x11
#define AK8975_ASAZ		0x12

#define AK8975_WHOAMI_VALUE	0x48

#define AK8975_POWERON_DELAY	50
#define AK8975_POWEROFF_DELAY	1

#define AK8975_MEASTIME		8

#define AK8975_PWR_ON		1
#define AK8975_PWR_OFF		0

#define AK8975_MEMORYMODE	0xf
#define AK8975_SELFTESTMODE	0x8
#define AK8975_MEASMODE		0x1
#define AK8975_PWRDOWNMODE	0x0

#define AK8975_SELFTEST		(0x1 << 6)

#define AK8975_ST1_DRDY		0x1
#define AK8975_ST2_HOFL		(0x1 << 3) /* Magnetic overflow */
#define AK8975_MAX_RANGE	4096

struct ak8975_chip {
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

	u8			memory[3];
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

static inline int ak8975_write(struct ak8975_chip *chip, u8 reg, u8 data)
{
	return i2c_smbus_write_byte_data(chip->client, reg, data);
}

static inline int ak8975_read(struct ak8975_chip *chip, u8 reg)
{
	return i2c_smbus_read_byte_data(chip->client, reg);
}

static inline int ak8975_read_block(struct ak8975_chip *chip, u8 reg,
				u8 *data, u8 length)
{
	return i2c_smbus_read_i2c_block_data(chip->client, reg, length, data);
}

static int ak8975_poll_drdy(struct ak8975_chip *chip)
{
	int ret;

	ret = ak8975_read(chip, AK8975_ST1);
	if (ret < 0)
		return ret;
	if (ret & AK8975_ST1_DRDY)
		return 0;
	return ret;
}

static void ak8975_trigmeas(struct ak8975_chip *chip)
{
	mutex_lock(&chip->lock);
	if (chip->meas_ongoing)
		goto out;

	/* Trig new measurement */
	ak8975_write(chip, AK8975_CNTL, AK8975_MEASMODE);

	chip->meas_ongoing = true;
	schedule_delayed_work(&chip->dwork,
			msecs_to_jiffies(AK8975_MEASTIME));

out:
	mutex_unlock(&chip->lock);
}

static int ak8975_reset(struct ak8975_chip *chip)
{
	ak8975_write(chip, AK8975_CNTL, AK8975_PWRDOWNMODE);
	msleep(AK8975_POWEROFF_DELAY);
	ak8975_write(chip, AK8975_ASTC, 0);
	return 0;
}

static int ak8975_getmemory(struct ak8975_chip *chip)
{
	int ret;

	ret = ak8975_write(chip, AK8975_CNTL, AK8975_MEMORYMODE);
	if (ret < 0)
		goto out;
	ret |= ak8975_read_block(chip, AK8975_ASAX, chip->memory, 3);

	ret |= ak8975_write(chip, AK8975_CNTL, AK8975_PWRDOWNMODE);
	msleep(AK8975_POWEROFF_DELAY);
out:
	return ret;
}

static int ak8975_getresult(struct ak8975_chip *chip, s16 *result)
{
	int i;
	int ret;

	ret = ak8975_poll_drdy(chip);
	/* Measurement overflow ? */
	ret |= ak8975_read(chip, AK8975_ST2);
	if (ret < 0)
		return ret;
	if (ret & AK8975_ST2_HOFL)
		ret = -ERANGE;
	else
		ret = 0;

	ak8975_read_block(chip, AK8975_HXL, (u8 *)result, 6);
	for (i = 0; i < 3; i++) {
		/* u16 -> s16 to get sign bit, s16 -> s32 to avoid overflow */
		result[i] = ((s32)((s16)le16_to_cpu(result[i])) *
			(s32)(chip->memory[i] + 128)) / 256;
	}
	return ret;
}

static int ak8975_selftest(struct ak8975_chip *chip, s16 *result)
{
	int ret;
	ret = ak8975_write(chip, AK8975_CNTL, AK8975_PWRDOWNMODE);
	msleep(AK8975_POWEROFF_DELAY);
	ret |= ak8975_write(chip, AK8975_ASTC, AK8975_SELFTEST);
	ret |= ak8975_write(chip, AK8975_CNTL, AK8975_SELFTESTMODE);

	msleep(AK8975_MEASTIME);
	ret |= ak8975_poll_drdy(chip);
	ret |= ak8975_getresult(chip, result);
	ret |= ak8975_write(chip, AK8975_ASTC, 0);

	return ret;
}

static int ak8975_regulators_on(struct ak8975_chip *chip)
{
	int ret;
	ret = regulator_bulk_enable(ARRAY_SIZE(chip->regs), chip->regs);
	if (ret == 0)
		msleep(AK8975_POWERON_DELAY);

	return ret;
}

static inline void ak8975_regulators_off(struct ak8975_chip *chip)
{
	regulator_bulk_disable(ARRAY_SIZE(chip->regs), chip->regs);
}

static int ak8975_add_users(struct ak8975_chip *chip)
{
	int r = 0;
	mutex_lock(&chip->users_lock);

	if (chip->users == 0) {
		r = ak8975_regulators_on(chip);
		if (r < 0)
			goto release_lock;
		r = ak8975_reset(chip);
	}

	if (r < 0)
		ak8975_regulators_off(chip);
	else
		chip->users++;

release_lock:
	mutex_unlock(&chip->users_lock);
	return r;
}

static void ak8975_remove_users(struct ak8975_chip *chip)
{
	mutex_lock(&chip->users_lock);

	if (chip->users != 0)
		chip->users--;

	if (chip->users == 0) {
		cancel_delayed_work_sync(&chip->dwork);
		chip->meas_ongoing = false;
		ak8975_regulators_off(chip);
	}
	mutex_unlock(&chip->users_lock);
}

static int ak8975_get_axis(s8 axis, s16 hw_values[3])
{
	if (axis > 0)
		return hw_values[axis - 1];
	else
		return -hw_values[-axis - 1];
}

static void ak8975_dwork(struct work_struct *work)
{

	struct ak8975_chip *chip = container_of(work, struct ak8975_chip,
						dwork.work);

	s16 hw_values[3];
	int ret;

	mutex_lock(&chip->lock);
	ret = ak8975_getresult(chip, hw_values);
	chip->x = ak8975_get_axis(chip->axis_x, hw_values);
	chip->y = ak8975_get_axis(chip->axis_y, hw_values);
	chip->z = ak8975_get_axis(chip->axis_z, hw_values);
	chip->valid = (ret == 0) ? 1 : 0;
	chip->offset += sizeof(struct ak8975_data);
	chip->meas_ongoing = false;
	mutex_unlock(&chip->lock);
	wake_up_interruptible(&chip->misc_wait);
}

static int ak8975_misc_open(struct inode *inode, struct file *file)
{
	struct ak8975_chip *chip = container_of(file->private_data,
						struct ak8975_chip,
						miscdev);
	file->f_pos = chip->offset;
	return ak8975_add_users(chip);
}

static int ak8975_misc_close(struct inode *inode, struct file *file)
{
	struct ak8975_chip *chip = container_of(file->private_data,
						struct ak8975_chip,
						miscdev);
	ak8975_remove_users(chip);
	return 0;
}

static ssize_t ak8975_misc_read(struct file *file, char __user *buf,
			size_t count, loff_t *offset)
{
	struct ak8975_chip *chip = container_of(file->private_data,
						struct ak8975_chip,
						miscdev);
	struct ak8975_data data;

	if (count < sizeof(data))
		return -EINVAL;

	if (*offset >= chip->offset) {
		ak8975_trigmeas(chip);
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

static ssize_t ak8975_show_selftest(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct ak8975_chip *chip = dev_get_drvdata(dev);
	int ret;
	s16 results[3];

	ak8975_add_users(chip);
	mutex_lock(&chip->lock);
	ret = ak8975_selftest(chip, results);
	mutex_unlock(&chip->lock);
	ak8975_remove_users(chip);

	return sprintf(buf, "%s %d %d %d\n", ret ? "FAIL" : "OK", results[0],
		results[1], results[2]);
}

static DEVICE_ATTR(selftest, S_IRUGO, ak8975_show_selftest, NULL);

static ssize_t ak8975_show_memory(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct ak8975_chip *chip = dev_get_drvdata(dev);
	return sprintf(buf, "%d %d %d\n", chip->memory[0],
		chip->memory[1], chip->memory[2]);
}

static DEVICE_ATTR(memory, S_IRUGO, ak8975_show_memory, NULL);

static ssize_t ak8975_show_chip_id(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct ak8975_chip *chip = dev_get_drvdata(dev);
	return sprintf(buf, "%s rev %d\n", chip->id,
		chip->info[0] | (u16)chip->info[1] << 8);
}

static DEVICE_ATTR(chip_id, S_IRUGO, ak8975_show_chip_id, NULL);

static ssize_t ak8975_show_range(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct ak8975_chip *chip = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", chip->max_range);
}

static DEVICE_ATTR(range, S_IRUGO, ak8975_show_range, NULL);

static struct attribute *sysfs_attrs[] = {
	&dev_attr_selftest.attr,
	&dev_attr_memory.attr,
	&dev_attr_range.attr,
	&dev_attr_chip_id.attr,
	NULL
};

static struct attribute_group ak8975_attribute_group = {
	.attrs = sysfs_attrs
};

static const struct file_operations ak8975_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= ak8975_misc_read,
	.open		= ak8975_misc_open,
	.release	= ak8975_misc_close,
};

static int ak8975_detect(struct ak8975_chip *chip, const char *name)
{
	int whoiam;

	whoiam = ak8975_read(chip, AK8975_WIA);
	if (whoiam != AK8975_WHOAMI_VALUE)
		return -ENODEV;

	chip->id = chip->client->driver->id_table[2].name;

	chip->info[0] = ak8975_read(chip, AK8975_INFO);
	ak8975_getmemory(chip);

	chip->max_range = AK8975_MAX_RANGE;

	return 0;
}

static int __devinit ak8975_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct ak8975_chip *chip;
	struct ak8975_platform_data *pdata;
	int err;
	int x, y, z;

	chip = kzalloc(sizeof *chip, GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	i2c_set_clientdata(client, chip);
	chip->client  = client;

	pdata = client->dev.platform_data;

	x = AK8975_DEV_X;
	y = AK8975_DEV_Y;
	z = AK8975_DEV_Z;

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
	INIT_DELAYED_WORK(&chip->dwork, ak8975_dwork);

	chip->regs[0].supply = reg_avdd;
	chip->regs[1].supply = reg_dvdd;

	err = regulator_bulk_get(&client->dev,
				 ARRAY_SIZE(chip->regs), chip->regs);
	if (err < 0) {
		dev_err(&client->dev, "Cannot get regulators\n");
		goto fail1;
	}

	err = ak8975_regulators_on(chip);
	if (err < 0) {
		dev_err(&client->dev, "Cannot enable regulators\n");
		goto fail2;
	}

	err = ak8975_detect(chip, id->name);
	if (err < 0) {
		dev_err(&client->dev, "Neither AK8975 nor ami305 found \n");
		goto fail3;
	}

	snprintf(chip->name, sizeof(chip->name), "ak8975%d",
		atomic_add_return(1, &device_number) - 1);

	ak8975_reset(chip);
	if (err) {
		dev_err(&client->dev, "Chip reset failed");
		goto fail3;
	}

	ak8975_regulators_off(chip);

	chip->miscdev.minor  = MISC_DYNAMIC_MINOR;
	chip->miscdev.name   = chip->name;
	chip->miscdev.fops   = &ak8975_fops;
	chip->miscdev.parent = &chip->client->dev;
	err = misc_register(&chip->miscdev);
	if (err < 0) {
		dev_err(&chip->client->dev, "Device registration failed\n");
		goto fail3;
	}

	err = sysfs_create_group(&chip->client->dev.kobj,
				&ak8975_attribute_group);
	if (err) {
		dev_err(&client->dev, "Sysfs registration failed\n");
		goto fail4;
	}


	return 0;
fail4:
	misc_deregister(&chip->miscdev);
fail3:
	ak8975_regulators_off(chip);
fail2:
	regulator_bulk_free(ARRAY_SIZE(chip->regs), chip->regs);
fail1:
	cancel_delayed_work_sync(&chip->dwork);
	kfree(chip);
	return err;
}

static int __devexit ak8975_remove(struct i2c_client *client)
{
	struct ak8975_chip *chip = i2c_get_clientdata(client);
	misc_deregister(&chip->miscdev);
	cancel_delayed_work_sync(&chip->dwork);
	sysfs_remove_group(&chip->client->dev.kobj,
			&ak8975_attribute_group);
	/* No users after sysfs removal and unregisterin of input device */
	regulator_bulk_free(ARRAY_SIZE(chip->regs), chip->regs);
	kfree(chip);
	return 0;
}


#ifdef CONFIG_PM
static int ak8975_suspend(struct i2c_client *client, pm_message_t mesg)
{
	return 0;
}

static int ak8975_resume(struct i2c_client *client)
{
	return 0;
}

static void ak8975_shutdown(struct i2c_client *client)
{
}

#else
#define ak8975_suspend	NULL
#define ak8975_shutdown NULL
#define ak8975_resume	NULL
#endif

static const struct i2c_device_id ak8975_id[] = {
	{"ak8975", 0 },
	{}
};

MODULE_DEVICE_TABLE(i2c, ak8975_id);

static struct i2c_driver ak8975_driver = {
	.driver	 = {
		.name	= "ak8975",
		.owner	= THIS_MODULE,
	},
	.suspend  = ak8975_suspend,
	.shutdown = ak8975_shutdown,
	.resume   = ak8975_resume,
	.probe	  = ak8975_probe,
	.remove   = __devexit_p(ak8975_remove),
	.id_table = ak8975_id,
};

static int __init ak8975_init(void)
{
	return i2c_add_driver(&ak8975_driver);
}

static void __exit ak8975_exit(void)
{
	i2c_del_driver(&ak8975_driver);
}

MODULE_DESCRIPTION("AK8975 3-axis magnetometer driver");
MODULE_AUTHOR("Samu Onkalo, Nokia Corporation");
MODULE_LICENSE("GPL v2");

module_init(ak8975_init);
module_exit(ak8975_exit);
