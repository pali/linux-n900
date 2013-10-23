/*
 * drivers/hwmon/lis3lv02d_i2c.c
 *
 * Implements I2C interface for lis3lv02d (STMicroelectronics) accelerometer.
 * Driver is based on corresponding SPI driver written by Daniel Mack
 * (lis3lv02d_spi.c (C) 2009 Daniel Mack <daniel@caiaq.de> ).
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include "lis3lv02d.h"

#define DRV_NAME 	"lis3lv02d_i2c"

static const char reg_vdd[]	= "Vdd";
static const char reg_vdd_io[]	= "Vdd_IO";

static int lis3_reg_ctrl(struct lis3lv02d *lis3, bool state)
{
	int ret;
	if (state == LIS3_REG_OFF) {
		ret = regulator_bulk_disable(ARRAY_SIZE(lis3->regulators),
					lis3->regulators);
	} else {
		ret = regulator_bulk_enable(ARRAY_SIZE(lis3->regulators),
					lis3->regulators);
		/* Chip needs time to wakeup. Not mentioned in datasheet */
		msleep(5);
	}
	return ret;
}

static inline s32 lis3_i2c_write(struct lis3lv02d *lis3, int reg, u8 value)
{
	struct i2c_client *c = lis3->bus_priv;
	return i2c_smbus_write_byte_data(c, reg, value);
}

static inline s32 lis3_i2c_read(struct lis3lv02d *lis3, int reg, u8 *v)
{
	struct i2c_client *c = lis3->bus_priv;
	*v = i2c_smbus_read_byte_data(c, reg);
	return 0;
}

static inline s32 lis3_i2c_blockread(struct lis3lv02d *lis3, int reg, int len,
				u8 *v)
{
	struct i2c_client *c = lis3->bus_priv;
	reg |= (1 << 7); /* 7th bit enables address auto incrementation */
	return i2c_smbus_read_i2c_block_data(c, reg, len, v);
}

static int lis3_i2c_init(struct lis3lv02d *lis3)
{
	u8 reg;
	int ret;

	lis3_reg_ctrl(lis3, LIS3_REG_ON);

	lis3->read(lis3, WHO_AM_I, &reg);
	if (lis3->whoami != 0 && reg != lis3->whoami)
		printk(KERN_ERR "lis3: power on failure\n");

	/* power up the device */
	ret = lis3->read(lis3, CTRL_REG1, &reg);
	if (ret < 0)
		return ret;

	/* Channel enable bits are needed for selftest */
	reg |= CTRL1_PD0 | CTRL1_Xen | CTRL1_Yen | CTRL1_Zen;
	return lis3lv02d_write(lis3, CTRL_REG1, reg);
}

/* Default axis mapping but it can be overwritten by platform data */
static struct axis_conversion lis3lv02d_axis_map = { LIS3_DEV_X,
						     LIS3_DEV_Y,
						     LIS3_DEV_Z };

static int __devinit lis3lv02d_i2c_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	int ret = 0;
	struct lis3lv02d_platform_data *pdata = client->dev.platform_data;

	if (pdata) {
		if (pdata->axis_x)
			lis3lv02d_axis_map.x = pdata->axis_x;

		if (pdata->axis_y)
			lis3lv02d_axis_map.y = pdata->axis_y;

		if (pdata->axis_z)
			lis3lv02d_axis_map.z = pdata->axis_z;

		if (pdata->setup_resources)
			ret = pdata->setup_resources();

		if (ret)
			goto fail;
	}

	lis3_dev.regulators[0].supply = reg_vdd;
	lis3_dev.regulators[1].supply = reg_vdd_io;
	ret = regulator_bulk_get(&client->dev, ARRAY_SIZE(lis3_dev.regulators),
				lis3_dev.regulators);
	if (ret < 0)
		goto fail;

	lis3_dev.pdata	  = pdata;
	lis3_dev.bus_priv = client;
	lis3_dev.init	  = lis3_i2c_init;
	lis3_dev.read	  = lis3_i2c_read;
	lis3_dev.blkread  = lis3_i2c_blockread;
	lis3_dev.write	  = lis3_i2c_write;
	lis3_dev.reg_ctrl = lis3_reg_ctrl;
	lis3_dev.irq	  = client->irq;
	lis3_dev.ac	  = lis3lv02d_axis_map;
	lis3_dev.owner	  = THIS_MODULE;

	i2c_set_clientdata(client, &lis3_dev);

	/* Provide power over the init call */
	lis3_reg_ctrl(&lis3_dev, LIS3_REG_ON);
	ret = lis3lv02d_init_device(&lis3_dev);
	lis3_reg_ctrl(&lis3_dev, LIS3_REG_OFF);
fail:
	return ret;
}

static int __devexit lis3lv02d_i2c_remove(struct i2c_client *client)
{
	struct lis3lv02d_platform_data *pdata = client->dev.platform_data;

	if (pdata && pdata->release_resources)
		pdata->release_resources();

	lis3lv02d_joystick_disable();
	lis3lv02d_remove_fs(&lis3_dev);

	regulator_bulk_free(ARRAY_SIZE(lis3_dev.regulators),
			lis3_dev.regulators);
	return 0;
}

#ifdef CONFIG_PM
static int lis3lv02d_i2c_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct lis3lv02d *lis3 = i2c_get_clientdata(client);

	mutex_lock(&lis3->mutex);
	if (!lis3->pdata->wakeup_flags && lis3->users)
		lis3lv02d_poweroff(lis3);
	mutex_unlock(&lis3->mutex);
	return 0;
}

static int lis3lv02d_i2c_resume(struct i2c_client *client)
{
	struct lis3lv02d *lis3 = i2c_get_clientdata(client);

	mutex_lock(&lis3->mutex);
	if (!lis3->pdata->wakeup_flags && lis3->users)
		lis3lv02d_poweron(lis3);
	mutex_unlock(&lis3->mutex);
	return 0;
}

static void lis3lv02d_i2c_shutdown(struct i2c_client *client)
{
	lis3lv02d_i2c_suspend(client, PMSG_SUSPEND);
}
#else
#define lis3lv02d_i2c_suspend	NULL
#define lis3lv02d_i2c_resume	NULL
#define lis3lv02d_i2c_shutdown	NULL
#endif

static const struct i2c_device_id lis3lv02d_id[] = {
	{"lis3lv02d", 0 },
	{}
};

MODULE_DEVICE_TABLE(i2c, lis3lv02d_id);

static struct i2c_driver lis3lv02d_i2c_driver = {
	.driver	 = {
		.name   = DRV_NAME,
		.owner  = THIS_MODULE,
	},
	.suspend = lis3lv02d_i2c_suspend,
	.shutdown = lis3lv02d_i2c_shutdown,
	.resume = lis3lv02d_i2c_resume,
	.probe	= lis3lv02d_i2c_probe,
	.remove	= __devexit_p(lis3lv02d_i2c_remove),
	.id_table = lis3lv02d_id,
};

static int __init lis3lv02d_init(void)
{
	return i2c_add_driver(&lis3lv02d_i2c_driver);
}

static void __exit lis3lv02d_exit(void)
{
	i2c_del_driver(&lis3lv02d_i2c_driver);
}

MODULE_AUTHOR("Nokia Corporation");
MODULE_DESCRIPTION("lis3lv02d I2C interface");
MODULE_LICENSE("GPL");
MODULE_ALIAS("i2c:" DRV_NAME);

module_init(lis3lv02d_init);
module_exit(lis3lv02d_exit);
