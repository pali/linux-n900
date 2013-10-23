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
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/sysfs.h>
#include <linux/i2c/tpa6130a2.h>
#include <mach/gpio.h>

#define TPA6130A2_I2C_ADDRESS	0x60

#define TPA6130A2_REG_ENABLE	0x1
#define TPA6130A2_REG_VOLUME	0x2
#define TPA6130A2_REG_HI_Z	0x3
#define TPA6130A2_REG_VERSION	0x4
#define TPA6130A2_REGS		4

#define TPA6130A2_BIT_HI_Z_R	(1 << 0)
#define TPA6130A2_BIT_HI_Z_L	(1 << 1)
#define TPA6130A2_MASK_CHANNEL	(3 << 6)
#define TPA6130A2_MASK_VOLUME	0x3f
#define TPA6130A2_MASK_HI_Z	0x03

#define TPA6130A2_CHANNEL_LEFT	(1 << 7)
#define TPA6130A2_CHANNEL_RIGHT	(1 << 6)

struct i2c_client *tpa6130a2_client;
static long int initialized;

/* This struct is used to save the context */
struct tpa6130a2_data {
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
			data->regs[reg] = val;
	} else {
		val = data->regs[reg];
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
	data->regs[reg] = value;

	return val;
}

static int tpa6130a2_print_channel(char *buf, int channel)
{
	int i = 0;
	buf[0] = buf[1] = ' ';
	buf[2] = '\n';
	buf[3] = 0;

	if (channel & TPA6130A2_CHANNEL_LEFT)
		buf[i++] = 'l';
	if (channel & TPA6130A2_CHANNEL_RIGHT)
		buf[i] = 'r';
	return 4;
}

static int tpa6130a2_parse_channel(const char *buf, int len)
{
	int val = 0, i;
	for (i = 0; (i < 2) && (i < len); i++) {
		if ((buf[i] == 'l') || (buf[i] == 'L'))
			val |= TPA6130A2_CHANNEL_LEFT;
		if ((buf[i] == 'r') || (buf[i] == 'R'))
			val |= TPA6130A2_CHANNEL_RIGHT;
	}
	return val;
}

static struct i2c_driver tpa6130a2_i2c_driver;

/*
 * Control interface
 */

static int tpa6130a2_get_mute(void)
{
	return tpa6130a2_read(TPA6130A2_REG_VOLUME) & TPA6130A2_MASK_CHANNEL;
}

static int tpa6130a2_set_mute(int channel)
{
	int val;
	val = tpa6130a2_read(TPA6130A2_REG_VOLUME) & ~TPA6130A2_MASK_CHANNEL;
	val |= channel & TPA6130A2_MASK_CHANNEL;

	return tpa6130a2_write(TPA6130A2_REG_VOLUME, val);
}

static int tpa6130a2_get_hpen(void)
{
	return tpa6130a2_read(TPA6130A2_REG_ENABLE) & TPA6130A2_MASK_CHANNEL;
}

static int tpa6130a2_set_hpen(int channel)
{
	int val;
	val = tpa6130a2_read(TPA6130A2_REG_ENABLE) & ~TPA6130A2_MASK_CHANNEL;
	val |= channel & TPA6130A2_MASK_CHANNEL;

	return tpa6130a2_write(TPA6130A2_REG_ENABLE, val);
}

static int tpa6130a2_get_hi_z(void)
{
	return (tpa6130a2_read(TPA6130A2_REG_HI_Z) & TPA6130A2_MASK_HI_Z) << 6;
}

static int tpa6130a2_set_hi_z(int channel)
{
	int val;
	val = tpa6130a2_read(TPA6130A2_REG_HI_Z) & ~TPA6130A2_MASK_HI_Z;
	val |= (channel & TPA6130A2_MASK_CHANNEL) >> 6;

	return tpa6130a2_write(TPA6130A2_REG_HI_Z, val);
}

int tpa6130a2_get_volume(void)
{
	int vol = tpa6130a2_read(TPA6130A2_REG_VOLUME);
	vol &= TPA6130A2_MASK_VOLUME;
	return vol;
}

int tpa6130a2_set_volume(int vol)
{
	if (vol < 0)
		vol = 0;
	if (vol > 0x3f)
		vol = 0x3f;

	vol |= tpa6130a2_read(TPA6130A2_REG_VOLUME) & ~TPA6130A2_MASK_VOLUME;
	return tpa6130a2_write(TPA6130A2_REG_VOLUME, vol);
}

static void tpa6130a2_power_on(void)
{
	struct tpa6130a2_data *data;
	int i;

	BUG_ON(tpa6130a2_client == NULL);
	data = i2c_get_clientdata(tpa6130a2_client);

	data->set_power(1);
	data->power_state = 1;

	/* Rewrite all except the read only register */
	for (i = 0; i < TPA6130A2_REGS - 1; i++)
		tpa6130a2_write(i, data->regs[i]);
}

static void tpa6130a2_power_off(void)
{
	struct tpa6130a2_data *data;

	BUG_ON(tpa6130a2_client == NULL);
	data = i2c_get_clientdata(tpa6130a2_client);

	data->set_power(0);
	data->power_state = 0;
}

void tpa6130a2_set_enabled(int enabled)
{
	if (enabled) {
		tpa6130a2_set_hpen(TPA6130A2_CHANNEL_LEFT |
				TPA6130A2_CHANNEL_RIGHT);
		tpa6130a2_power_on();
	} else {
		/* Disable the HPs prior to powering down the chip */
		tpa6130a2_set_hpen(0);
		tpa6130a2_power_off();
	}
}

/*
 * Sysfs interface
 */

static ssize_t tpa6130a2_mute_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	int val = tpa6130a2_get_mute();

	return tpa6130a2_print_channel(buf, val);
}

static ssize_t tpa6130a2_mute_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t len)
{
	int val;
	val = tpa6130a2_parse_channel(buf, len);
	tpa6130a2_set_mute(val);

	return len;
}

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

static ssize_t tpa6130a2_hp_en_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	int val = tpa6130a2_get_hpen();

	return tpa6130a2_print_channel(buf, val);
}

static ssize_t tpa6130a2_hp_en_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t len)
{
	int val = 0;

	val = tpa6130a2_parse_channel(buf, len);
	tpa6130a2_set_hpen(val);

	return len;
}

static ssize_t tpa6130a2_hi_z_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	int val = tpa6130a2_get_hi_z();

	return tpa6130a2_print_channel(buf, val);
}

static ssize_t tpa6130a2_hi_z_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t len)
{
	int val = 0;

	val = tpa6130a2_parse_channel(buf, len);
	tpa6130a2_set_hi_z(val);

	return len;
}

static struct device_attribute tpa6130a2_attrs[] = {
	__ATTR(volume, S_IRUGO | S_IWUSR, tpa6130a2_volume_show,
	       tpa6130a2_volume_store),
	__ATTR(mute, S_IRUGO | S_IWUSR, tpa6130a2_mute_show,
	       tpa6130a2_mute_store),
	__ATTR(hi_z, S_IRUGO | S_IWUSR, tpa6130a2_hi_z_show,
	       tpa6130a2_hi_z_store),
	__ATTR(hp_en, S_IRUGO | S_IWUSR, tpa6130a2_hp_en_show,
	       tpa6130a2_hp_en_store),
};

static int tpa6130a2_register_sysfs(struct device *dev)
{
	int err, i;

	for (i = 0; i < ARRAY_SIZE(tpa6130a2_attrs); i++) {
		err = device_create_file(dev, &tpa6130a2_attrs[i]);
		if (err)
			goto fail;
	}
	return 0;

fail:
	while (i--)
		device_remove_file(dev, &tpa6130a2_attrs[i]);
	return err;
}

static void tpa6130a2_unregister_sysfs(struct device *dev)
{
	int i = ARRAY_SIZE(tpa6130a2_attrs);

	while (i--)
		device_remove_file(dev, &tpa6130a2_attrs[i]);
}

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

	/* Read version */
	err = tpa6130a2_read(TPA6130A2_REG_VERSION);
	if ((err & 0xf) != 2) {
		dev_err(dev, "Unexpected headphone amplifier chip version "
		       "of 0x%02x, was expecting 0x02\n", err);
		err = -ENODEV;

		goto fail2;
	}

	err = tpa6130a2_register_sysfs(dev);
	if (err) {
		dev_err(dev, "Sysfs node creation failed\n");
		goto fail2;
	}

	dev_info(dev, "Headphone amplifier initialized successfully\n");

	/* enable both channels */
	tpa6130a2_set_hpen(TPA6130A2_CHANNEL_LEFT | TPA6130A2_CHANNEL_RIGHT);
	/* Some sort of default volume that doesn't kill your ears.. */
	tpa6130a2_set_volume(20);
	tpa6130a2_set_mute(0); /* Mute off */
	tpa6130a2_set_hpen(0); /* Disable the chip until we actually need it */

	/* Disable the chip */
	data->set_power(0);
	data->power_state = 0;
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
	tpa6130a2_unregister_sysfs(dev);
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

module_init(tpa6130a2_init);
module_exit(tpa6130a2_exit);
