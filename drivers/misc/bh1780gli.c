/*
 * bh1780gli.c - BH1780GLI Ambient Light Sensor Driver
 *
 * Copyright (C) 2009 Nokia Corporation
 *
 * Written by Timo O. Karjalainen <timo.o.karjalainen@nokia.com>
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
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/regulator/consumer.h>

#define BH1780_CMD		0x80

#define BH1780_REG_CTRL		0x00
#define BH1780_REG_REV		0x0a
#define BH1780_REG_MANUF	0x0b
#define BH1780_REG_LUX		0x0c /* 2 bytes, LSB first */

#define BH1780_POWER_ON		0x03
#define BH1780_POWER_MASK	0x03

struct bh1780_chip {
	struct mutex		lock;		/* Serialize access to chip */
	struct i2c_client	*client;
	struct regulator        *regulator_vcc;
	unsigned int		suspended:1;
	u32			calib;		/* 16.16 fixed point */
	u16			lux;
};

static int bh1780_write(struct bh1780_chip *chip, u8 reg, u8 val)
{
	return i2c_smbus_write_byte_data(chip->client, BH1780_CMD | reg, val);
}

static int bh1780_read8(struct bh1780_chip *chip, u8 reg)
{
	return i2c_smbus_read_byte_data(chip->client, BH1780_CMD | reg);
}

static int bh1780_read16(struct bh1780_chip *chip, u8 reg)
{
	return i2c_smbus_read_word_data(chip->client, BH1780_CMD | reg);
}

static int bh1780_set_power(struct bh1780_chip *chip, int on)
{
	return bh1780_write(chip, BH1780_REG_CTRL, on ? BH1780_POWER_ON : 0);
}

static int bh1780_get_power(struct bh1780_chip *chip)
{
	int val;

	val = bh1780_read8(chip, BH1780_REG_CTRL);
	if (val < 0)
		return val;

	return (val & BH1780_POWER_MASK) == BH1780_POWER_ON;
}

static int bh1780_detect(struct bh1780_chip *chip)
{
	int ret;

	ret = bh1780_set_power(chip, 1);
	if (ret)
		return ret;

	ret = bh1780_get_power(chip);
	if (ret < 0)
		return ret;

	return ret ? 0 : -ENODEV;
}

static int bh1780_read_id(struct bh1780_chip *chip)
{
	return bh1780_read8(chip, BH1780_REG_REV);
}

/**
 * calib_adc() - Apply calibration coefficient to raw lux measurement
 * @raw: Raw measurement from chip
 * @calib: Calibration coefficient
 *
 * Multiply the raw value with the calibration coefficient, which is stored
 * in 16.16 fixed point format.
 */
static u16 calib_adc(u16 raw, u32 calib)
{
	unsigned long scaled = raw;

	scaled *= calib;
	scaled >>= 16;

	return (u16) scaled;
}

static int bh1780_read_lux(struct bh1780_chip *chip)
{
	s32 ret;

	ret = bh1780_read16(chip, BH1780_REG_LUX);
	if (ret < 0)
		return ret;

	chip->lux = le16_to_cpu(ret);
	return 0;
}

/*
 * Sysfs representation of calibration coefficient is an integer
 * number of 1/1000's (one-thousandths). Thus 1.0 is 1000 and
 * 1.023 is 1023, etc.
 *
 * Internally the driver uses 16.16 fixed point arithmetic.
 *
 * These functions convert between these two formats, with rounding.
 */
static inline unsigned int calib_to_sysfs(u32 calib)
{
	return (unsigned int) (((calib * 1000) + 500) >> 16);
}

static inline u32 calib_from_sysfs(unsigned int value)
{
	return (((u32) value) << 16) / 1000;
}

/*
 * Sysfs interface
 */

static ssize_t bh1780_lux_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct bh1780_chip *chip = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&chip->lock);

	if (!chip->suspended) {
		ret = bh1780_read_lux(chip);
		if (ret)
			goto out;
	}

	ret = snprintf(buf, PAGE_SIZE, "%u\n",
		       calib_adc(chip->lux, chip->calib));

out:
	mutex_unlock(&chip->lock);

	return ret;
}

static ssize_t bh1780_raw_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct bh1780_chip *chip = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&chip->lock);

	if (!chip->suspended) {
		ret = bh1780_read_lux(chip);
		if (ret)
			goto out;
	}

	ret = snprintf(buf, PAGE_SIZE, "%u\n", chip->lux);

out:
	mutex_unlock(&chip->lock);

	return ret;
}

static ssize_t bh1780_calib_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct bh1780_chip *chip = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&chip->lock);
	ret = snprintf(buf, PAGE_SIZE, "%u\n", calib_to_sysfs(chip->calib));
	mutex_unlock(&chip->lock);

	return ret;
}

static ssize_t bh1780_calib_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len)
{
	struct bh1780_chip *chip = dev_get_drvdata(dev);
	unsigned long value;

	if (strict_strtoul(buf, 0, &value))
		return -EINVAL;

	chip->calib = calib_from_sysfs(value);

	return len;
}

static DEVICE_ATTR(lux,   S_IRUGO, bh1780_lux_show, NULL);
static DEVICE_ATTR(raw,   S_IRUGO, bh1780_raw_show, NULL);
static DEVICE_ATTR(calib, S_IRUGO | S_IWUSR,
		   bh1780_calib_show, bh1780_calib_store);

static struct attribute *bh1780_attributes[] = {
	&dev_attr_lux.attr,
	&dev_attr_raw.attr,
	&dev_attr_calib.attr,
	NULL
};

static const struct attribute_group bh1780_group = {
	.attrs = bh1780_attributes,
};

static int bh1780_register_sysfs(struct i2c_client *client)
{
	struct device *dev = &client->dev;

	return sysfs_create_group(&dev->kobj, &bh1780_group);
}

static void bh1780_unregister_sysfs(struct i2c_client *client)
{
	struct device *dev = &client->dev;

	sysfs_remove_group(&dev->kobj, &bh1780_group);
}

/*
 * Probe, Attach, Remove
 */

static int bh1780_probe(struct i2c_client *client,
			const struct i2c_device_id *device_id)
{
	struct bh1780_chip *chip;
	int err;
	int id;

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->regulator_vcc = regulator_get(&client->dev, "Vcc");
	if (IS_ERR(chip->regulator_vcc)) {
		dev_err(&client->dev, "Cannot get Vcc regulator\n");
		err = PTR_ERR(chip->regulator_vcc);
		goto fail1;
	}

	err = regulator_enable(chip->regulator_vcc);
	if (err < 0) {
		dev_err(&client->dev, "Cannot enable Vcc regulator\n");
		goto fail2;
	}

	i2c_set_clientdata(client, chip);
	chip->client = client;

	err = bh1780_detect(chip);
	if (err) {
		dev_err(&client->dev, "device not found, error %d\n", err);
		goto fail3;
	}

	id = bh1780_read_id(chip);
	if (id < 0) {
		err = id;
		goto fail3;
	}

	dev_dbg(&client->dev, "model %d, rev %d\n", (id >> 4) & 0xf, id & 0xf);

	mutex_init(&chip->lock);
	chip->calib = calib_from_sysfs(1000);

	err = bh1780_register_sysfs(client);
	if (err) {
		dev_err(&client->dev, "sysfs registration failed, %d\n", err);
		goto fail3;
	}

	return 0;
fail3:
	regulator_disable(chip->regulator_vcc);
fail2:
	regulator_put(chip->regulator_vcc);
fail1:
	kfree(chip);
	return err;
}

static int bh1780_remove(struct i2c_client *client)
{
	struct bh1780_chip *chip = i2c_get_clientdata(client);

	bh1780_unregister_sysfs(client);
	regulator_disable(chip->regulator_vcc);
	regulator_put(chip->regulator_vcc);
	kfree(chip);

	return 0;
}

#ifdef CONFIG_PM
static int bh1780_suspend(struct i2c_client *client, pm_message_t state)
{
	struct bh1780_chip *chip = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&chip->lock);

	ret = bh1780_set_power(chip, 0);
	if (ret)
		goto out;

	chip->suspended = 1;

out:
	mutex_unlock(&chip->lock);
	return ret;
}

static int bh1780_resume(struct i2c_client *client)
{
	struct bh1780_chip *chip = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&chip->lock);

	ret = bh1780_set_power(chip, 1);
	if (ret)
		goto out;

	chip->suspended = 0;

out:
	mutex_unlock(&chip->lock);
	return ret;
}
#else
#define bh1780_suspend NULL
#define bh1780_resume NULL
#endif

static const struct i2c_device_id bh1780_id[] = {
	{ "bh1780gli", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, bh1780_id);

static struct i2c_driver bh1780_i2c_driver = {
	.driver = {
		.name	 = "bh1780gli",
	},
	.suspend	= bh1780_suspend,
	.resume		= bh1780_resume,
	.probe		= bh1780_probe,
	.remove		= bh1780_remove,
	.id_table	= bh1780_id,
};

static int __init bh1780_init(void)
{
	return i2c_add_driver(&bh1780_i2c_driver);
}
module_init(bh1780_init);

static void __exit bh1780_exit(void)
{
	i2c_del_driver(&bh1780_i2c_driver);
}
module_exit(bh1780_exit);

MODULE_AUTHOR("Timo O. Karjalainen <timo.o.karjalainen@nokia.com>");
MODULE_DESCRIPTION("BH1780GLI light sensor driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("i2c:bh1780gli");
