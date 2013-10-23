/*
 * bcm4751-gps.c - Hardware interface for Broadcom BCM4751 GPS chip.
 *
 * Copyright (C) 2009 Nokia Corporation
 *
 * Written by Andrei Emeltchenko <andrei.emeltchenko@nokia.com>
 * Modified by Yuri Zaporozhets <ext-yuri.zaporozhets@nokia.com>
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
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/poll.h>
#include <linux/miscdevice.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>

#include <linux/i2c/bcm4751-gps.h>

static const char reg_vbat[] = "Vbat";
static const char reg_vddio[] = "Vddio";

/*
 * Part of initialization is done in the board support file.
 */

static inline void bcm4751_gps_enable(struct bcm4751_gps_data *self)
{
	mutex_lock(&self->mutex);
	if (!self->enable) {
		regulator_bulk_enable(ARRAY_SIZE(self->regs), self->regs);
		if (self->pdata->enable)
			self->pdata->enable(self->client);
		self->enable = 1;
	}
	mutex_unlock(&self->mutex);
}

static inline void bcm4751_gps_disable(struct bcm4751_gps_data *self)
{
	mutex_lock(&self->mutex);
	if (self->enable) {
		if (self->pdata->disable)
			self->pdata->disable(self->client);
		self->enable = 0;
		regulator_bulk_disable(ARRAY_SIZE(self->regs), self->regs);
	}
	mutex_unlock(&self->mutex);
}

static inline void bcm4751_gps_wakeup_value(struct bcm4751_gps_data *self,
		int value)
{
	mutex_lock(&self->mutex);
	if (self->pdata->wakeup_ctrl)
		self->pdata->wakeup_ctrl(self->client, value);
	self->wakeup = value;
	mutex_unlock(&self->mutex);
}


/*
 * miscdevice interface
 */

static int bcm4751_gps_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int bcm4751_gps_release(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t bcm4751_gps_read(struct file *file, char __user *buf,
		size_t count, loff_t *offset)
{
	struct  bcm4751_gps_data *data = container_of(file->private_data,
						      struct bcm4751_gps_data,
						      miscdev);
	struct i2c_client *client = data->client;

	int num_read;
	uint8_t	tmp[BCM4751_MAX_BINPKT_RX_LEN];

	/* Adjust for binary packet size */
	if (count > BCM4751_MAX_BINPKT_RX_LEN)
		count = BCM4751_MAX_BINPKT_RX_LEN;

	dev_dbg(&client->dev, "reading %d bytes\n", count);

	num_read = i2c_master_recv(client, tmp, count);

	if (num_read < 0) {
		dev_err(&client->dev, "got %d bytes instead of %d\n",
			num_read, count);
		return num_read;
	} else {
		dev_dbg(&client->dev, "reading %d bytes returns %d",
			count, num_read);
	}

	return copy_to_user(buf, tmp, num_read) ? -EFAULT : num_read;
}

static ssize_t bcm4751_gps_write(struct file *file, const char __user *buf,
		size_t count, loff_t *offset)
{
	struct  bcm4751_gps_data *data = container_of(file->private_data,
						      struct bcm4751_gps_data,
						      miscdev);
	struct i2c_client *client = data->client;
	uint8_t tmp[BCM4751_MAX_BINPKT_TX_LEN];
	int num_sent;

	if (count > BCM4751_MAX_BINPKT_TX_LEN)
		count = BCM4751_MAX_BINPKT_TX_LEN;

	dev_dbg(&client->dev, "writing %d bytes\n", count);

	if (copy_from_user(tmp, buf, count))
		return -EFAULT;

	num_sent = i2c_master_send(client, tmp, count);

	dev_dbg(&client->dev, "writing %d bytes returns %d",
		count, num_sent);

	return num_sent;
}

static int bcm4751_gps_ioctl(struct inode *inode, struct file *file,
		unsigned int cmd, unsigned long arg)
{
	struct  bcm4751_gps_data *data = container_of(file->private_data,
						      struct bcm4751_gps_data,
						      miscdev);
	struct i2c_client *client = data->client;

	dev_dbg(&client->dev, "ioctl: cmd = 0x%02x, arg=0x%02lx\n", cmd, arg);

	switch (cmd) {
	case I2C_SLAVE:
	case I2C_SLAVE_FORCE:
		if ((arg > 0x3ff) ||
				(((client->flags & I2C_M_TEN) == 0) &&
				 arg > 0x7f))
			return -EINVAL;
		client->addr = arg;
		dev_dbg(&client->dev, "ioctl: client->addr = %x", client->addr);
		return 0;
	case I2C_TENBIT:
		if (arg)
			client->flags |= I2C_M_TEN;
		else
			client->flags &= ~I2C_M_TEN;
		return 0;
	default:
		return -ENOTTY;
	}
	return 0;
}

static const struct file_operations bcm4751_gps_fileops = {
	.owner = THIS_MODULE,
	.llseek		= no_llseek,
	.read		= bcm4751_gps_read,
	.write		= bcm4751_gps_write,
	.ioctl		= bcm4751_gps_ioctl,
	.open		= bcm4751_gps_open,
	.release	= bcm4751_gps_release,
};

/*
 * sysfs interface
 */

static ssize_t bcm4751_gps_show_hostreq(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct bcm4751_gps_data *self = dev_get_drvdata(dev);
	int value = -1;

	if (self->pdata->show_irq)
		value = self->pdata->show_irq(self->client);

	return snprintf(buf, PAGE_SIZE, "%d\n", value);
}

static ssize_t bcm4751_gps_show_enable(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct bcm4751_gps_data *self = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", self->enable);
}

static ssize_t bcm4751_gps_set_enable(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct bcm4751_gps_data *self = dev_get_drvdata(dev);
	int value;

	sscanf(buf, "%d", &value);
	dev_dbg(dev, "enable: %d", value);

	switch (value) {
	case 0:
		bcm4751_gps_disable(self);
		break;

	case 1:
		bcm4751_gps_enable(self);
		break;

	default:
		return -EINVAL;
	}
	return len;
}

static ssize_t bcm4751_gps_show_wakeup(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct bcm4751_gps_data *self = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", self->wakeup);
}

static ssize_t bcm4751_gps_set_wakeup(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	unsigned long val;
	int ret;
	struct bcm4751_gps_data *self = dev_get_drvdata(dev);

	ret = strict_strtoul(buf, 0, &val);
	if (ret && val > 1)
		return -EINVAL;
	else {
		bcm4751_gps_wakeup_value(self, val);
		dev_dbg(dev, "new wakeup value = %d", self->wakeup);
	}

	return len;
}

static struct device_attribute bcm4751_gps_attrs[] = {
	__ATTR(enable, S_IRUGO|S_IWUSR,
			bcm4751_gps_show_enable, bcm4751_gps_set_enable),
	__ATTR(hostreq, S_IRUGO|S_IWUSR,
			bcm4751_gps_show_hostreq, NULL),
	__ATTR(wakeup, S_IRUGO|S_IWUSR,
			bcm4751_gps_show_wakeup, bcm4751_gps_set_wakeup),
};

static int bcm4751_gps_register_sysfs(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(bcm4751_gps_attrs); i++) {
		ret = device_create_file(dev, &bcm4751_gps_attrs[i]);
		if (ret)
			goto fail;
	}
	return 0;
fail:
	while (i--)
		device_remove_file(dev, &bcm4751_gps_attrs[i]);

	return ret;
}

static void bcm4751_gps_unregister_sysfs(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	int i;

	for (i = ARRAY_SIZE(bcm4751_gps_attrs) - 1; i >= 0; i--)
		device_remove_file(dev, &bcm4751_gps_attrs[i]);
}

/* IRQ thread */
static irqreturn_t bcm4751_gps_irq_thread(int irq, void *dev_id)
{
	struct bcm4751_gps_data *data = dev_id;

	dev_dbg(&data->client->dev, "irq, HOST_REQ=%d",
			data->pdata->show_irq(data->client));

	/* Update sysfs GPIO line here */
	sysfs_notify(&data->client->dev.kobj, NULL, "hostreq");
	return IRQ_HANDLED;
}

static int bcm4751_gps_probe(struct i2c_client *client,
		const struct i2c_device_id *device_id)
{
	struct bcm4751_gps_data			*data;
	struct bcm4751_gps_platform_data	*pdata;
	int					err;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	pdata = client->dev.platform_data;
	if (!pdata) {
		dev_err(&client->dev, "no platform data\n");
		err = -ENODEV;
		goto clean_data;
	}

	i2c_set_clientdata(client, data);
	data->client = client;
	data->pdata  = pdata;

	data->gpio_irq    = pdata->gps_gpio_irq;
	data->gpio_enable = pdata->gps_gpio_enable;
	data->gpio_wakeup = pdata->gps_gpio_wakeup;

	data->regs[0].supply = reg_vbat;
	data->regs[1].supply = reg_vddio;
	err = regulator_bulk_get(&client->dev,
				ARRAY_SIZE(data->regs), data->regs);
	if (err < 0) {
		dev_err(&client->dev, "Can't get regulators\n");
		goto clean_data;
	}

	if (pdata->setup) {
		err = pdata->setup(client);
		if (err)
			goto clean_reg;
	}

	mutex_init(&data->mutex);
	err = request_threaded_irq(client->irq, NULL,
				bcm4751_gps_irq_thread,
				IRQF_TRIGGER_RISING | IRQF_ONESHOT,
				"bcm4751-gps", data);
	if (err) {
		dev_err(&client->dev, "could not get GPS_IRQ = %d\n",
				client->irq);
		goto clean_setup;
	}

	err = bcm4751_gps_register_sysfs(client);
	if (err) {
		dev_err(&client->dev,
				"sysfs registration failed, error %d\n", err);
		goto clean_irq;
	}

	data->miscdev.minor = MISC_DYNAMIC_MINOR;
	data->miscdev.name = "bcm4751-gps";
	data->miscdev.fops = &bcm4751_gps_fileops;
	data->miscdev.parent = &client->dev;
	err = misc_register(&data->miscdev);
	if (err) {
		dev_err(&client->dev, "Miscdevice register failed\n");
		goto clean_sysfs;
	}

	return 0;

clean_sysfs:
	bcm4751_gps_unregister_sysfs(client);

clean_irq:
	free_irq(client->irq, data);

clean_setup:
	if (pdata->cleanup)
		pdata->cleanup(client);
clean_reg:
	regulator_bulk_free(ARRAY_SIZE(data->regs), data->regs);
clean_data:
	kfree(data);

	return err;
}

static int bcm4751_gps_remove(struct i2c_client *client)
{
	struct bcm4751_gps_data *data = i2c_get_clientdata(client);

	bcm4751_gps_disable(data);

	free_irq(client->irq, data);
	misc_deregister(&data->miscdev);
	bcm4751_gps_unregister_sysfs(client);
	if (data->pdata->cleanup)
		data->pdata->cleanup(client);
	regulator_bulk_free(ARRAY_SIZE(data->regs), data->regs);
	kfree(data);

	return 0;
}

static void bcm4751_gps_shutdown(struct i2c_client *client)
{
	dev_dbg(&client->dev, "BCM4751 shutdown\n");
	bcm4751_gps_disable(i2c_get_clientdata(client));
}

#ifdef CONFIG_PM
static int bcm4751_gps_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct bcm4751_gps_data *data = i2c_get_clientdata(client);
	data->pdata->wakeup_ctrl(data->client, 0);
	dev_dbg(&client->dev, "BCM4751 suspends\n");
	return 0;
}

static int bcm4751_gps_resume(struct i2c_client *client)
{
	struct bcm4751_gps_data *data = i2c_get_clientdata(client);
	data->pdata->wakeup_ctrl(data->client, 1);
	dev_dbg(&client->dev, "BCM4751 resumes\n");
	return 0;
}
#else
#define bcm4751_gps_suspend NULL
#define bcm4751_gps_resume NULL
#endif

static const struct i2c_device_id bcm4751_gps_id[] = {
	{ "bcm4751-gps", 0 },
	{ },
};

MODULE_DEVICE_TABLE(i2c, bcm4751_gps_id);

static struct i2c_driver bcm4751_gps_i2c_driver = {
	.driver = {
		.name	 = "bcm4751-gps",
	},

	.id_table	= bcm4751_gps_id,
	.probe		= bcm4751_gps_probe,
	.remove		= __devexit_p(bcm4751_gps_remove),
	.shutdown	= bcm4751_gps_shutdown,
	.suspend	= bcm4751_gps_suspend,
	.resume		= bcm4751_gps_resume,
};

static int __init bcm4751_gps_init(void)
{
	pr_info("Loading BCM4751 GPS driver\n");

	return i2c_add_driver(&bcm4751_gps_i2c_driver);
}
module_init(bcm4751_gps_init);

static void __exit bcm4751_gps_exit(void)
{
	i2c_del_driver(&bcm4751_gps_i2c_driver);
}
module_exit(bcm4751_gps_exit);

MODULE_AUTHOR("Andrei Emeltchenko, Yuri Zaporozhets");
MODULE_DESCRIPTION("BCM4751 GPS driver");
MODULE_LICENSE("GPL");
