/*
 * This file is part of the ROHM BH1770GLC / OSRAM SFH7770 sensor driver.
 * Chip is combined proximity and ambient light sensor.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
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
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/i2c/bhsfh.h>
#include "bhsfh.h"

#define BHSFH_PS_INIT_DELAY	15

struct bhsfh_ps_dev {
	struct miscdevice		miscdev;
	struct bhsfh_chip		*chip;
	wait_queue_head_t		ps_misc_wait;
};

static unsigned int bhsfh_ps_poll(struct file *file, poll_table *wait)
{
	struct bhsfh_ps_dev *ps_dev = container_of(file->private_data,
						struct bhsfh_ps_dev,
						miscdev);
	struct bhsfh_chip *chip = ps_dev->chip;

	poll_wait(file, &ps_dev->ps_misc_wait, wait);
	if (file->f_pos < chip->ps_offset)
		return POLLIN | POLLRDNORM;
	return 0;
}

static int bhsfh_ps_open(struct inode *inode, struct file *file)
{
	struct bhsfh_ps_dev *ps_dev = container_of(file->private_data,
						struct bhsfh_ps_dev,
						miscdev);
	struct bhsfh_chip *chip = ps_dev->chip;
	int ret;

	mutex_lock(&chip->mutex);
	ret = bhsfh_proximity_on(chip);
	file->f_pos = chip->ps_offset;
	mutex_unlock(&chip->mutex);

	if (ret == 0)
		schedule_delayed_work(&chip->ps_work,
				msecs_to_jiffies(BHSFH_PS_INIT_DELAY));
	return ret;
}

static int bhsfh_ps_close(struct inode *inode, struct file *file)
{
	struct bhsfh_ps_dev *ps_dev = container_of(file->private_data,
						struct bhsfh_ps_dev,
						miscdev);
	struct bhsfh_chip *chip = ps_dev->chip;
	int users;

	mutex_lock(&chip->mutex);
	bhsfh_proximity_off(chip);
	users = chip->ps_users;
	mutex_unlock(&chip->mutex);
	if (!users)
		cancel_delayed_work_sync(&chip->ps_work);
	return 0;
}

static ssize_t bhsfh_ps_read(struct file *file, char __user *buf,
			size_t count, loff_t *offset)
{
	struct bhsfh_ps_dev *ps_dev = container_of(file->private_data,
						struct bhsfh_ps_dev,
						miscdev);
	struct bhsfh_chip *chip = ps_dev->chip;

	struct bhsfh_ps ps;

	if (count < sizeof(ps))
		return -EINVAL;

	if (*offset >= chip->ps_offset) {
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(ps_dev->ps_misc_wait,
						(*offset < chip->ps_offset)))
			return -ERESTARTSYS;
	}

	ps.led1 = chip->ps_data;
	ps.led2 = 0;
	ps.led3 = 0;

	*offset = chip->ps_offset;

	return copy_to_user(buf, &ps, sizeof(ps)) ? -EFAULT : sizeof(ps);
}

static const struct file_operations bhsfh_ps_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= bhsfh_ps_read,
	.poll		= bhsfh_ps_poll,
	.open		= bhsfh_ps_open,
	.release	= bhsfh_ps_close,
};

int bhsfh_ps_init(struct bhsfh_chip *chip)
{
	struct bhsfh_ps_dev *ps_device;

	ps_device = kzalloc(sizeof *ps_device, GFP_KERNEL);
	if (!ps_device)
		return -ENOMEM;

	init_waitqueue_head(&ps_device->ps_misc_wait);

	chip->ps_dev_private = ps_device;
	chip->ps_dev_wait = &ps_device->ps_misc_wait;

	ps_device->chip = chip;
	ps_device->miscdev.minor = MISC_DYNAMIC_MINOR,
	ps_device->miscdev.name = "bh1770glc_ps";
	ps_device->miscdev.fops = &bhsfh_ps_fops;
	ps_device->miscdev.parent = &chip->client->dev;

	return misc_register(&ps_device->miscdev);
}

int bhsfh_ps_destroy(struct bhsfh_chip *chip)
{
	struct bhsfh_ps_dev *ps_device = chip->ps_dev_private;
	misc_deregister(&ps_device->miscdev);
	kfree(ps_device);
	chip->ps_dev_private = NULL;
	chip->ps_dev_wait = NULL;
	return 0;
}
