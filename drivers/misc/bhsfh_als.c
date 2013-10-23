/*
 * This file is part of the ROHM BH1770GLC / OSRAM SFH7770 sensor driver.
 * Chip is combined proximity and ambient light sensor.
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

#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/miscdevice.h>
#include <linux/poll.h>
#include <linux/uaccess.h>
#include <linux/regulator/consumer.h>
#include <linux/i2c/bhsfh.h>
#include "bhsfh.h"

struct bhsfh_als_dev {
	struct miscdevice		miscdev;
	struct bhsfh_chip		*chip;
	wait_queue_head_t		als_misc_wait;
};

static ssize_t bhsfh_als_read(struct file *file, char __user *buf,
			size_t count, loff_t *offset)
{
	struct bhsfh_als_dev *als_dev = container_of(file->private_data,
						struct bhsfh_als_dev,
						miscdev);
	struct bhsfh_chip *chip = als_dev->chip;

	struct bhsfh_als als;

	if (count < sizeof(als))
		return -EINVAL;

	if (*offset >= chip->als_offset) {
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(als_dev->als_misc_wait,
						(*offset < chip->als_offset)))
			return -ERESTARTSYS;
	}

	als.lux = chip->als_data;
	*offset = chip->als_offset;
	return copy_to_user(buf, &als, sizeof(als)) ? -EFAULT : sizeof(als);
}

static int bhsfh_als_open(struct inode *inode, struct file *file)
{
	struct bhsfh_als_dev *als_dev = container_of(file->private_data,
						struct bhsfh_als_dev,
						miscdev);
	struct bhsfh_chip *chip = als_dev->chip;
	int ret;

	mutex_lock(&chip->mutex);
	ret = bhsfh_als_on(chip);
	if (ret < 0)
		goto release_lock;

	file->f_pos = chip->als_offset;
release_lock:
	mutex_unlock(&chip->mutex);

	/* Kick buggy version handling */
	if (chip->broken_chip && chip->als_users == 1)
		bhsfh_handle_buggy_version(chip);

	return ret;
}

static unsigned int bhsfh_als_poll(struct file *file, poll_table *wait)
{
	struct bhsfh_als_dev *als_dev = container_of(file->private_data,
						struct bhsfh_als_dev,
						miscdev);
	struct bhsfh_chip *chip = als_dev->chip;

	poll_wait(file, &als_dev->als_misc_wait, wait);
	if (file->f_pos < chip->als_offset)
		return POLLIN | POLLRDNORM;
	return 0;
}

static int bhsfh_als_close(struct inode *inode, struct file *file)
{
	struct bhsfh_als_dev *als_dev = container_of(file->private_data,
						struct bhsfh_als_dev,
						miscdev);
	struct bhsfh_chip *chip = als_dev->chip;

	mutex_lock(&chip->mutex);
	bhsfh_als_off(chip);
	mutex_unlock(&chip->mutex);
	return 0;
}

static const struct file_operations bhsfh_als_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= bhsfh_als_read,
	.poll		= bhsfh_als_poll,
	.open		= bhsfh_als_open,
	.release	= bhsfh_als_close,
};

int bhsfh_als_init(struct bhsfh_chip *chip)
{
	struct bhsfh_als_dev *als_device;
	int err;

	als_device = kzalloc(sizeof *als_device, GFP_KERNEL);
	if (!als_device)
		return -ENOMEM;

	init_waitqueue_head(&als_device->als_misc_wait);

	chip->als_dev_private = als_device;
	chip->als_dev_wait = &als_device->als_misc_wait;

	als_device->chip = chip;
	als_device->miscdev.minor = MISC_DYNAMIC_MINOR;
	als_device->miscdev.name  = "bh1770glc_als";
	als_device->miscdev.fops  = &bhsfh_als_fops;
	als_device->miscdev.parent = &chip->client->dev;
	err = misc_register(&als_device->miscdev);
	return err;
}

int bhsfh_als_destroy(struct bhsfh_chip *chip)
{
	struct bhsfh_als_dev *als_device = chip->als_dev_private;
	misc_deregister(&als_device->miscdev);
	kfree(als_device);
	chip->als_dev_private = NULL;
	chip->als_dev_wait = NULL;
	return 0;
}
