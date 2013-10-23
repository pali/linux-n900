/*
 * ssi-char.c
 *
 * SSI character device driver, implements the character device
 * interface.
 *
 * Copyright (C) 2009 Nokia Corporation. All rights reserved.
 *
 * Contact: Andras Domokos <andras.domokos@nokia.com>
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

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/miscdevice.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/poll.h>
#include <asm/mach-types.h>
#include <linux/ioctl.h>
#include <linux/uaccess.h>

#include <mach/ssi.h>
#include <linux/ssi_driver_if.h>
#include <linux/ssi_char.h>

#include "ssi-char-debug.h"
#include "ssi-char.h"

#define DRIVER_VERSION  "0.1.0"

static unsigned int port = 1;
module_param(port, uint, 1);
MODULE_PARM_DESC(port, "SSI port to be probed");

static unsigned int channels_map[SSI_MAX_CHAR_DEVS] = {1};
module_param_array(channels_map, uint, NULL, 0);
MODULE_PARM_DESC(channels_map, "SSI channels to be probed");

dev_t ssi_char_dev;

struct char_queue {
	struct list_head list;
	u32 *data;
	unsigned int count;
};

struct ssi_char {
	unsigned int opened;
	int poll_event;
	struct list_head rx_queue;
	struct list_head tx_queue;
	spinlock_t lock;
	struct fasync_struct *async_queue;
	wait_queue_head_t rx_wait;
	wait_queue_head_t tx_wait;
	wait_queue_head_t poll_wait;
};

static struct ssi_char ssi_char_data[SSI_MAX_CHAR_DEVS];

void if_notify(int ch, struct ssi_event *ev)
{
	struct char_queue *entry;

	spin_lock(&ssi_char_data[ch].lock);

	if (!ssi_char_data[ch].opened) {
		printk(KERN_DEBUG "device not opened\n!");
		spin_unlock(&ssi_char_data[ch].lock);
		return;
	}

	switch (SSI_EV_TYPE(ev->event)) {
	case SSI_EV_IN:
		entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
		if (!entry) {
			pr_err("SSI-CHAR: entry allocation failed.\n");
			spin_unlock(&ssi_char_data[ch].lock);
			return;
		}
		entry->data = ev->data;
		entry->count = ev->count;
		list_add_tail(&entry->list, &ssi_char_data[ch].rx_queue);
		spin_unlock(&ssi_char_data[ch].lock);
		wake_up_interruptible(&ssi_char_data[ch].rx_wait);
		break;
	case SSI_EV_OUT:
		entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
		if (!entry) {
			pr_err("SSI-CHAR: entry allocation failed.\n");
			spin_unlock(&ssi_char_data[ch].lock);
			return;
		}
		entry->data = ev->data;
		entry->count = ev->count;
		ssi_char_data[ch].poll_event |= (POLLOUT | POLLWRNORM);
		list_add_tail(&entry->list, &ssi_char_data[ch].tx_queue);
		spin_unlock(&ssi_char_data[ch].lock);
		wake_up_interruptible(&ssi_char_data[ch].tx_wait);
		break;
	case SSI_EV_EXCEP:
		ssi_char_data[ch].poll_event |= POLLPRI;
		spin_unlock(&ssi_char_data[ch].lock);
		wake_up_interruptible(&ssi_char_data[ch].poll_wait);
		break;
	case SSI_EV_AVAIL:
		ssi_char_data[ch].poll_event |= (POLLIN | POLLRDNORM);
		spin_unlock(&ssi_char_data[ch].lock);
		wake_up_interruptible(&ssi_char_data[ch].poll_wait);
		break;
	default:
		spin_unlock(&ssi_char_data[ch].lock);
		break;
	}
}


static int ssi_char_fasync(int fd, struct file *file, int on)
{
	int ch = (int)file->private_data;
	if (fasync_helper(fd, file, on, &ssi_char_data[ch].async_queue) >= 0)
		return 0;
	else
		return -EIO;
}


static unsigned int ssi_char_poll(struct file *file, poll_table *wait)
{
	int ch = (int)file->private_data;
	unsigned int ret = 0;

	poll_wait(file, &ssi_char_data[ch].poll_wait, wait);
	poll_wait(file, &ssi_char_data[ch].tx_wait, wait);
	spin_lock_bh(&ssi_char_data[ch].lock);
	ret = ssi_char_data[ch].poll_event;
	spin_unlock_bh(&ssi_char_data[ch].lock);

	return ret;
}


static ssize_t ssi_char_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	int ch = (int)file->private_data;
	DECLARE_WAITQUEUE(wait, current);
	u32 *data;
	unsigned int data_len;
	struct char_queue *entry;
	ssize_t ret;

	/* only 32bit data is supported for now */
	if ((count < 4) || (count & 3) || (count > 0x10000))
		return -EINVAL;

	data = kmalloc(count, GFP_ATOMIC);
	if (!data) {
		pr_err("SSI-CHAR: memory allocation failed.\n");
		return -ENOMEM;
	}

	ret = if_ssi_read(ch, data, count);
	if (ret < 0) {
		kfree(data);
		goto out2;
	}

	add_wait_queue(&ssi_char_data[ch].rx_wait, &wait);

	for ( ; ; ) {
		data = NULL;
		data_len = 0;

		set_current_state(TASK_INTERRUPTIBLE);

		spin_lock_bh(&ssi_char_data[ch].lock);
		if (!list_empty(&ssi_char_data[ch].rx_queue)) {
			entry = list_entry(ssi_char_data[ch].rx_queue.next,
					struct char_queue, list);
			data = entry->data;
			data_len = entry->count;
			list_del(&entry->list);
			kfree(entry);
		}
		spin_unlock_bh(&ssi_char_data[ch].lock);

		if (data_len) {
			spin_lock_bh(&ssi_char_data[ch].lock);
			ssi_char_data[ch].poll_event &= ~(POLLIN | POLLRDNORM |
								POLLPRI);
			if_ssi_poll(ch);
			spin_unlock_bh(&ssi_char_data[ch].lock);
			break;
		} else if (file->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			goto out;
		} else if (signal_pending(current)) {
			ret = -EINTR;
			if_ssi_cancel_read(ch);
			break;
		}

		schedule();
	}

	if (data_len) {
		ret = copy_to_user((void __user *)buf, data, data_len);
		if (!ret)
			ret = data_len;
	}

	kfree(data);

out:
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&ssi_char_data[ch].rx_wait, &wait);

out2:
	return ret;
}

static ssize_t ssi_char_write(struct file *file, const char __user *buf,
			 size_t count, loff_t *ppos)
{
	int ch = (int)file->private_data;
	DECLARE_WAITQUEUE(wait, current);
	u32 *data;
	unsigned int data_len = 0;
	struct char_queue *entry;
	ssize_t ret;

	/* only 32bit data is supported for now */
	if ((count < 4) || (count & 3) || (count > 0x10000))
		return -EINVAL;

	data = kmalloc(count, GFP_ATOMIC);
	if (!data) {
		pr_err("SSI-CHAR: memory allocation failed.\n");
		return -ENOMEM;
	}

	if (copy_from_user(data, (void __user *)buf, count)) {
		ret = -EFAULT;
		kfree(data);
	} else {
		ret = count;
	}

	spin_lock_bh(&ssi_char_data[ch].lock);
	ret = if_ssi_write(ch, data, count);
	if (ret < 0) {
		spin_unlock_bh(&ssi_char_data[ch].lock);
		kfree(data);
		goto out2;
	}
	ssi_char_data[ch].poll_event &= ~(POLLOUT | POLLWRNORM);
	spin_unlock_bh(&ssi_char_data[ch].lock);

	add_wait_queue(&ssi_char_data[ch].tx_wait, &wait);

	for ( ; ; ) {
		data = NULL;
		data_len = 0;

		set_current_state(TASK_INTERRUPTIBLE);

		spin_lock_bh(&ssi_char_data[ch].lock);
		if (!list_empty(&ssi_char_data[ch].tx_queue)) {
			entry = list_entry(ssi_char_data[ch].tx_queue.next,
					struct char_queue, list);
			data = entry->data;
			data_len = entry->count;
			list_del(&entry->list);
			kfree(entry);
		}
		spin_unlock_bh(&ssi_char_data[ch].lock);

		if (data_len) {
			ret = data_len;
			break;
		} else if (file->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			goto out;
		} else if (signal_pending(current)) {
			ret = -EINTR;
			if_ssi_cancel_write(ch);
			break;
		}

		schedule();
	}

    kfree(data);

out:
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&ssi_char_data[ch].tx_wait, &wait);

out2:
	return ret;
}

static int ssi_char_ioctl(struct inode *inode, struct file *file,
				unsigned int cmd, unsigned long arg)
{
	int ch = (int)file->private_data;
	unsigned int state;
	struct ssi_rx_config rx_cfg;
	struct ssi_tx_config tx_cfg;
	int ret = 0;

	switch (cmd) {
	case CS_SEND_BREAK:
		if_ssi_send_break(ch);
	break;
	case CS_FLUSH_RX:
		if_ssi_flush_rx(ch);
	break;
	case CS_FLUSH_TX:
		if_ssi_flush_tx(ch);
	break;
	case CS_SET_WAKELINE:
		if (copy_from_user(&state, (void __user *)arg,
				sizeof(state)))
			ret = -EFAULT;
		else
			if_ssi_set_wakeline(ch, state);
	break;
	case CS_GET_WAKELINE:
		if_ssi_get_wakeline(ch, &state);
		if (copy_to_user((void __user *)arg, &state, sizeof(state)))
			ret = -EFAULT;
	break;
	case CS_SET_RX: {
		if (copy_from_user(&rx_cfg, (void __user *)arg,
				sizeof(rx_cfg)))
			ret = -EFAULT;
		else
			ret = if_ssi_set_rx(ch, &rx_cfg);
	}
		break;
	case CS_GET_RX:
		if_ssi_get_rx(ch, &rx_cfg);
		if (copy_to_user((void __user *)arg, &rx_cfg, sizeof(rx_cfg)))
			ret = -EFAULT;
	break;
	case CS_SET_TX:
		if (copy_from_user(&tx_cfg, (void __user *)arg,
				sizeof(tx_cfg)))
			ret = -EFAULT;
		else
			ret = if_ssi_set_tx(ch, &tx_cfg);
	break;
	case CS_GET_TX:
		if_ssi_get_tx(ch, &tx_cfg);
		if (copy_to_user((void __user *)arg, &tx_cfg, sizeof(tx_cfg)))
			ret = -EFAULT;
	break;
	default:
		return -ENOIOCTLCMD;
	break;
	}

	return ret;
}

static int ssi_char_open(struct inode *inode, struct file *file)
{
	int ret = 0, ch = iminor(inode);

	if (!channels_map[ch])
		return -ENODEV;

	spin_lock_bh(&ssi_char_data[ch].lock);
#if 0
	if (ssi_char_data[ch].opened) {
		spin_unlock_bh(&ssi_char_data[ch].lock);
		return -EBUSY;
	}
#endif
	file->private_data = (void *)ch;
	ssi_char_data[ch].opened++;
	ssi_char_data[ch].poll_event = (POLLOUT | POLLWRNORM);
	spin_unlock_bh(&ssi_char_data[ch].lock);

	ret = if_ssi_start(ch);

	return ret;
}

static int ssi_char_release(struct inode *inode, struct file *file)
{
	int ch = (int)file->private_data;
	struct char_queue	*entry;
	struct list_head	*cursor, *next;

	if_ssi_stop(ch);
	spin_lock_bh(&ssi_char_data[ch].lock);
	ssi_char_data[ch].opened--;

	if (!list_empty(&ssi_char_data[ch].rx_queue)) {
		list_for_each_safe(cursor, next, &ssi_char_data[ch].rx_queue) {
			entry = list_entry(cursor, struct char_queue, list);
			list_del(&entry->list);
			kfree(entry);
		}
	}

	if (!list_empty(&ssi_char_data[ch].tx_queue)) {
		list_for_each_safe(cursor, next, &ssi_char_data[ch].tx_queue) {
			entry = list_entry(cursor, struct char_queue, list);
			list_del(&entry->list);
			kfree(entry);
		}
	}

	spin_unlock_bh(&ssi_char_data[ch].lock);

	return 0;
}

static const struct file_operations ssi_char_fops = {
	.owner = THIS_MODULE,
	.read = ssi_char_read,
	.write = ssi_char_write,
	.poll = ssi_char_poll,
	.ioctl = ssi_char_ioctl,
	.open = ssi_char_open,
	.release = ssi_char_release,
	.fasync = ssi_char_fasync,
};

static struct cdev ssi_char_cdev;

static int __init ssi_char_init(void)
{
	char devname[] = "ssi_char";
	int ret, i;

	pr_info("SSI character device version " DRIVER_VERSION "\n");

	for (i = 0; i < SSI_MAX_CHAR_DEVS; i++) {
		init_waitqueue_head(&ssi_char_data[i].rx_wait);
		init_waitqueue_head(&ssi_char_data[i].tx_wait);
		init_waitqueue_head(&ssi_char_data[i].poll_wait);
		spin_lock_init(&ssi_char_data[i].lock);
		ssi_char_data[i].opened = 0;
		INIT_LIST_HEAD(&ssi_char_data[i].rx_queue);
		INIT_LIST_HEAD(&ssi_char_data[i].tx_queue);
	}

	ret = if_ssi_init(port, channels_map);
	if (ret)
		return ret;

	ret = alloc_chrdev_region(&ssi_char_dev, 0, SSI_MAX_CHAR_DEVS, devname);
	if (ret < 0) {
		pr_err("SSI character driver: Failed to register\n");
		return ret;
	}

	cdev_init(&ssi_char_cdev, &ssi_char_fops);
	cdev_add(&ssi_char_cdev, ssi_char_dev, SSI_MAX_CHAR_DEVS);

	return 0;
}

static void __exit ssi_char_exit(void)
{
	cdev_del(&ssi_char_cdev);
	unregister_chrdev_region(ssi_char_dev, SSI_MAX_CHAR_DEVS);
	if_ssi_exit();
}

MODULE_AUTHOR("Andras Domokos <andras.domokos@nokia.com>");
MODULE_DESCRIPTION("SSI character device");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRIVER_VERSION);

module_init(ssi_char_init);
module_exit(ssi_char_exit);
