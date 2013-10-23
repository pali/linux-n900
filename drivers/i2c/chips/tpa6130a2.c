/*
 * drivers/i2c/chips/tpa6130a2.c
 *
 * Simple driver to modify TPA6130A2 amplifier chip gain levels trough
 * sysfs interface.
 *
 * Copyright (C) Nokia Corporation
 *
 * Written by Timo Kokkonen <timo.t.kokkonen@nokia.com>
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

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/sysfs.h>
#include <linux/i2c/tpa6130a2.h>
#include <mach/gpio.h>

#define TPA6130A2_REG_ENABLE	0x1
#define TPA6130A2_REG_VOLUME	0x2
#define TPA6130A2_REG_HI_Z	0x3
#define TPA6130A2_REG_VERSION	0x4
#define TPA6130A2_REGS		4

#define TPA6130A2_MASK_CHANNEL	(3 << 6)
#define TPA6130A2_MASK_VOLUME	0x3f
#define TPA6130A2_MASK_HI_Z	0x03
#define TPA6130A2_SWS		0x01

#define TPA6130A2_CHANNEL_LEFT	(1 << 7)
#define TPA6130A2_CHANNEL_RIGHT	(1 << 6)

struct i2c_client *tpa6130a2_client;
static long int initialized;

/* This struct is used to save the context */
struct tpa6130a2_data {
	struct mutex mutex;
	unsigned char regs[TPA6130A2_REGS];
	unsigned char power_state;
	int (*set_power)(int state);
};

static int tpa6130a2_read(int reg)
{
	struct tpa6130a2_data *data;
	int val;

	BUG_ON(tpa6130a2_client == NULL);

	data = i2c_get_clientdata(tpa6130a2_client);

	/* If powered off, return the cached value */
	if (data->power_state) {
		val = i2c_smbus_read_byte_data(tpa6130a2_client, reg);
		if (val < 0)
			dev_err(&tpa6130a2_client->dev, "Read failed\n");
		else
			data->regs[reg - 1] = val;
	} else {
		val = data->regs[reg - 1];
	}

	return val;
}

static int tpa6130a2_write(int reg, u8 value)
{
	struct tpa6130a2_data *data;
	int val = 0;

	BUG_ON(tpa6130a2_client == NULL);

	data = i2c_get_clientdata(tpa6130a2_client);

	if (data->power_state) {
		val = i2c_smbus_write_byte_data(tpa6130a2_client, reg, value);
		if (val < 0)
			dev_err(&tpa6130a2_client->dev, "Write failed\n");
	}

	/* Either powered on or off, we save the context */
	data->regs[reg - 1] = value;

	return val;
}

/* Control interface */
static int tpa6130a2_get_mute(void)
{
	struct tpa6130a2_data *data;
	int ret;

	data = i2c_get_clientdata(tpa6130a2_client);

	mutex_lock(&data->mutex);
	ret = tpa6130a2_read(TPA6130A2_REG_VOLUME) & TPA6130A2_MASK_CHANNEL;
	mutex_unlock(&data->mutex);

	return ret;
}

static void tpa6130a2_set_mute(int channel)
{
	struct tpa6130a2_data *data;
	int val;

	data = i2c_get_clientdata(tpa6130a2_client);

	mutex_lock(&data->mutex);
	val = tpa6130a2_read(TPA6130A2_REG_VOLUME) & ~TPA6130A2_MASK_CHANNEL;
	val |= channel & TPA6130A2_MASK_CHANNEL;

	tpa6130a2_write(TPA6130A2_REG_VOLUME, val);
	mutex_unlock(&data->mutex);
}

static int tpa6130a2_get_hp_en(void)
{
	struct tpa6130a2_data *data;
	int ret;

	data = i2c_get_clientdata(tpa6130a2_client);

	mutex_lock(&data->mutex);
	ret = tpa6130a2_read(TPA6130A2_REG_ENABLE) & TPA6130A2_MASK_CHANNEL;
	mutex_unlock(&data->mutex);

	return ret;
}

static void tpa6130a2_set_hp_en(int channel)
{
	struct tpa6130a2_data *data;
	int val;

	data = i2c_get_clientdata(tpa6130a2_client);

	mutex_lock(&data->mutex);
	val = tpa6130a2_read(TPA6130A2_REG_ENABLE) & ~TPA6130A2_MASK_CHANNEL;
	val |= channel & TPA6130A2_MASK_CHANNEL;

	if (channel)
		val &= ~TPA6130A2_SWS;
	else
		val |= TPA6130A2_SWS;

	tpa6130a2_write(TPA6130A2_REG_ENABLE, val);
	mutex_unlock(&data->mutex);
}

static int tpa6130a2_get_hi_z(void)
{
	struct tpa6130a2_data *data;
	int ret;

	data = i2c_get_clientdata(tpa6130a2_client);

	mutex_lock(&data->mutex);
	ret = (tpa6130a2_read(TPA6130A2_REG_HI_Z) & TPA6130A2_MASK_HI_Z) << 6;
	mutex_unlock(&data->mutex);

	return ret;
}

static void tpa6130a2_set_hi_z(int channel)
{
	struct tpa6130a2_data *data;
	int val;

	data = i2c_get_clientdata(tpa6130a2_client);

	mutex_lock(&data->mutex);
	val = tpa6130a2_read(TPA6130A2_REG_HI_Z) & ~TPA6130A2_MASK_HI_Z;
	val |= (channel & TPA6130A2_MASK_CHANNEL) >> 6;
	tpa6130a2_write(TPA6130A2_REG_HI_Z, val);
	mutex_unlock(&data->mutex);
}

int tpa6130a2_get_volume(void)
{
	struct tpa6130a2_data *data;
	int vol;

	data = i2c_get_clientdata(tpa6130a2_client);

	mutex_lock(&data->mutex);
	vol = tpa6130a2_read(TPA6130A2_REG_VOLUME);
	mutex_unlock(&data->mutex);
	vol &= TPA6130A2_MASK_VOLUME;

	return vol;
}

int tpa6130a2_set_volume(int vol)
{
	struct tpa6130a2_data *data;
	int ret;

	if (vol < 0)
		vol = 0;
	if (vol > 0x3f)
		vol = 0x3f;

	data = i2c_get_clientdata(tpa6130a2_client);

	mutex_lock(&data->mutex);
	vol |= tpa6130a2_read(TPA6130A2_REG_VOLUME) & ~TPA6130A2_MASK_VOLUME;
	ret = tpa6130a2_write(TPA6130A2_REG_VOLUME, vol);
	mutex_unlock(&data->mutex);

	return ret;
}

static void tpa6130a2_power_on(void)
{
	struct tpa6130a2_data *data;
	int i;

	data = i2c_get_clientdata(tpa6130a2_client);

	mutex_lock(&data->mutex);
	data->set_power(1);
	data->power_state = 1;

	/* Rewrite all except the read only register */
	for (i = TPA6130A2_REG_ENABLE; i < TPA6130A2_REGS; i++)
		tpa6130a2_write(i, data->regs[i - 1]);
	mutex_unlock(&data->mutex);
}

static void tpa6130a2_power_off(void)
{
	struct tpa6130a2_data *data;

	data = i2c_get_clientdata(tpa6130a2_client);

	mutex_lock(&data->mutex);
	data->power_state = 0;
	data->set_power(0);
	mutex_unlock(&data->mutex);
}

void tpa6130a2_set_enabled(int enabled)
{
	BUG_ON(tpa6130a2_client == NULL);

	if (enabled) {
		tpa6130a2_set_hp_en(TPA6130A2_CHANNEL_LEFT |
				TPA6130A2_CHANNEL_RIGHT);
		tpa6130a2_power_on();
	} else {
		/* Disable the HPs prior to powering down the chip */
		tpa6130a2_set_hp_en(0);
		tpa6130a2_power_off();
	}
}

/* Sysfs interface */
#define tpa6130a2_sys_property(name)					\
static ssize_t tpa6130a2_##name##_show(struct device *dev,		\
			   struct device_attribute *attr, char *buf)	\
{									\
	int val = tpa6130a2_get_##name();				\
									\
	return snprintf(buf, PAGE_SIZE, "%c%c\n",			\
		(val & TPA6130A2_CHANNEL_LEFT) ? 'l' : ' ',		\
		(val & TPA6130A2_CHANNEL_RIGHT) ? 'r' : ' ');		\
}									\
									\
static ssize_t tpa6130a2_##name##_store(struct device *dev,		\
				    struct device_attribute *attr,	\
				    const char *buf, size_t len)	\
{									\
	int val = 0;							\
									\
	if (strpbrk(buf, "lL") != NULL)					\
		val |= TPA6130A2_CHANNEL_LEFT;				\
	if (strpbrk(buf, "rR") != NULL)					\
		val |= TPA6130A2_CHANNEL_RIGHT;				\
	tpa6130a2_set_##name(val);					\
									\
	return len;							\
}									\
									\
static DEVICE_ATTR(name, S_IRUGO | S_IWUSR, tpa6130a2_##name##_show,	\
					tpa6130a2_##name##_store);

tpa6130a2_sys_property(mute)
tpa6130a2_sys_property(hp_en)
tpa6130a2_sys_property(hi_z)

static ssize_t tpa6130a2_volume_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	int vol;
	vol = tpa6130a2_get_volume();
	vol = snprintf(buf, PAGE_SIZE, "%d\n", vol);
	return vol;
}

static ssize_t tpa6130a2_volume_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t len)
{
	int vol;
	char str[10];

	strncpy(str, buf, min(len, sizeof(str)));
	str[min(len, sizeof(str) - 1)] = 0;

	if (sscanf(str, " %d", &vol) == 1)
		tpa6130a2_set_volume(vol);

	return len;
}

static DEVICE_ATTR(volume, S_IRUGO | S_IWUSR, tpa6130a2_volume_show,
					tpa6130a2_volume_store);

static struct attribute *attrs[] = {
	&dev_attr_volume.attr,
	&dev_attr_mute.attr,
	&dev_attr_hi_z.attr,
	&dev_attr_hp_en.attr,
	NULL,
};

static const struct attribute_group attr_group = {
	.attrs = attrs,
};

static int tpa6130a2_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	int err;
	struct device *dev;
	struct tpa6130a2_data *data;
	struct tpa6130a2_platform_data *pdata;

	dev = &client->dev;
	if (test_and_set_bit(1, &initialized)) {
		dev_info(dev, "Driver already initialized\n");
		return -ENODEV;
	}

	tpa6130a2_client = client;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (data == NULL) {
		err = -ENOMEM;
		goto fail1;
	}

	i2c_set_clientdata(tpa6130a2_client, data);

	if (client->dev.platform_data == NULL) {
		dev_err(dev, "Platform data not set\n");
		dump_stack();
		err = -ENODEV;
		goto fail2;
	}

	pdata = (struct tpa6130a2_platform_data *)client->dev.platform_data;
	data->set_power = pdata->set_power;
	data->set_power(1);
	data->power_state = 1;
	mutex_init(&data->mutex);

	/* Read version */
	err = tpa6130a2_read(TPA6130A2_REG_VERSION) & 0x0f;
	if ((err != 1) && (err != 2)) {
		dev_err(dev, "Unexpected headphone amplifier chip version "
		       "of 0x%02x, was expecting 0x01 or 0x02\n", err);
		err = -ENODEV;

		goto fail2;
	}

	err = sysfs_create_group(&dev->kobj, &attr_group);
	if (err) {
		dev_err(dev, "Sysfs node creation failed\n");
		goto fail2;
	}

	dev_info(dev, "Headphone amplifier initialized successfully\n");

	/* enable both channels */
	tpa6130a2_set_hp_en(TPA6130A2_CHANNEL_LEFT | TPA6130A2_CHANNEL_RIGHT);
	/* Some sort of default volume that doesn't kill your ears.. */
	tpa6130a2_set_volume(20);
	tpa6130a2_set_mute(0); /* Mute off */
	tpa6130a2_set_hp_en(0); /* Disable the chip until we actually need it */

	/* Disable the chip */
	data->power_state = 0;
	data->set_power(0);
	return 0;

fail2:
	kfree(data);
fail1:
	tpa6130a2_client = 0;
	initialized = 0;

	return err;
}

static int tpa6130a2_remove(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct tpa6130a2_data *data = i2c_get_clientdata(client);

	data->set_power(0);
	sysfs_remove_group(&dev->kobj, &attr_group);
	kfree(data);
	tpa6130a2_client = 0;
	initialized = 0;

	return 0;
}

static int tpa6130a2_suspend(struct i2c_client *client, pm_message_t mesg)
{
	tpa6130a2_power_off();

	return 0;
}

static int tpa6130a2_resume(struct i2c_client *client)
{
	tpa6130a2_power_on();

	return 0;
}

static const struct i2c_device_id tpa6130a2_id[] = {
	{
		.name = "tpa6130a2",
		.driver_data = 0,
	},
	{ },
};

static struct i2c_driver tpa6130a2_i2c_driver = {
	.driver = {
		.name = "tpa6130a2",
	},
	.id		= I2C_DRIVERID_MISC,
	.class		= I2C_CLASS_HWMON,
	.probe		= tpa6130a2_probe,
	.remove		= tpa6130a2_remove,
	.suspend	= tpa6130a2_suspend,
	.resume		= tpa6130a2_resume,
	.id_table	= tpa6130a2_id,
};

static int __init tpa6130a2_init(void)
{
	int ret;

	ret = i2c_add_driver(&tpa6130a2_i2c_driver);
	if (ret < 0) {
		printk(KERN_ERR "Unable to register TPA6130A2 I2C driver\n");
		return ret;
	}

	return 0;
}

static void __exit tpa6130a2_exit(void)
{
	i2c_del_driver(&tpa6130a2_i2c_driver);
}

MODULE_AUTHOR("Nokia Corporation");
MODULE_DESCRIPTION("TPA6130A2 Headphone amplifier driver");
MODULE_LICENSE("GPL");

late_initcall(tpa6130a2_init);
module_exit(tpa6130a2_exit);
