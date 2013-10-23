/*
 *  cs-core.c
 *
 * Part of the CMT speech driver, implements the character device
 * interface.
 *
 * Copyright (C) 2008,2009 Nokia Corporation. All rights reserved.
 *
 * Contact: Kai Vehmanen <kai.vehmanen@nokia.com>
 * Original author: Peter Ujfalusi <peter.ujfalusi@nokia.com>
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
#include <linux/poll.h>
#include <asm/mach-types.h>
#include <linux/ioctl.h>
#include <linux/uaccess.h>

#include <linux/cs-protocol.h>

#include "cs-debug.h"
#include "cs-core.h"
#include "cs-ssi.h"

#define CS_MMAP_SIZE	PAGE_SIZE

struct char_queue {
	struct list_head	list;
	u32			msg;
};


struct cs_char {
	unsigned int	opened;

	struct list_head	chardev_queue;

	/* mmap things */
	unsigned long	mmap_base;
	unsigned long	mmap_size;

	spinlock_t 	lock;
	struct fasync_struct *async_queue;
	wait_queue_head_t	wait;
};


static struct cs_char cs_char_data;

void cs_notify(u32 message)
{
	struct char_queue *entry;
	DENTER();

	spin_lock(&cs_char_data.lock);

	if (!cs_char_data.opened) {
		spin_unlock(&cs_char_data.lock);
		goto out;
	}

	entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
	if (!entry) {
		pr_err("CS_SSI: Can't allocate new entry for the queue.\n");
		spin_unlock(&cs_char_data.lock);
		goto out;
	}

	entry->msg = message;
	list_add_tail(&entry->list, &cs_char_data.chardev_queue);

	spin_unlock(&cs_char_data.lock);

	wake_up_interruptible(&cs_char_data.wait);
	kill_fasync(&cs_char_data.async_queue, SIGIO, POLL_IN);

out:
	DLEAVE(0);
}

static void cs_char_vma_open(struct vm_area_struct *vma)
{
	DENTER();
	DLEAVE(0);
}

static void cs_char_vma_close(struct vm_area_struct *vma)
{
	DENTER();
	DLEAVE(0);
}

static int cs_char_vma_fault(struct vm_area_struct *vma,
				struct vm_fault *vmf)
{
	struct page *page;
	DENTER();

	page = virt_to_page(cs_char_data.mmap_base);
	get_page(page);
	vmf->page = page;

	DLEAVE(0);
	return 0;
}

static struct vm_operations_struct cs_char_vm_ops = {
	.open = cs_char_vma_open,
	.close = cs_char_vma_close,
	.fault = cs_char_vma_fault,
};

static int cs_char_fasync(int fd, struct file *file, int on)
{
	if (fasync_helper(fd, file, on, &cs_char_data.async_queue) >= 0)
		return 0;
	else
		return -EIO;
}


static unsigned int cs_char_poll(struct file *file, poll_table *wait)
{
	unsigned int ret = 0;

	poll_wait(file, &cs_char_data.wait, wait);

	spin_lock_bh(&cs_char_data.lock);
	if (!list_empty(&cs_char_data.chardev_queue)) {
		ret = POLLIN | POLLRDNORM;
		DPRINTK("There is something in the queue...\n");
	}
	spin_unlock_bh(&cs_char_data.lock);

	return ret;
}


static ssize_t cs_char_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	u32	data;
	DECLARE_WAITQUEUE(wait, current);
	ssize_t retval;
	struct char_queue	*entry;
	DENTER();

	if (count < sizeof(data))
		return -EINVAL;

	add_wait_queue(&cs_char_data.wait, &wait);

	for ( ; ; ) {
		set_current_state(TASK_INTERRUPTIBLE);

		spin_lock_bh(&cs_char_data.lock);
		if (!list_empty(&cs_char_data.chardev_queue)) {
			entry = list_entry(cs_char_data.chardev_queue.next,
					   struct char_queue, list);
			data = entry->msg;
			list_del(&entry->list);
			kfree(entry);
		} else {
			data = 0;
		}
		spin_unlock_bh(&cs_char_data.lock);

		if (data)
			break;
		else if (file->f_flags & O_NONBLOCK) {
			retval = -EAGAIN;
			goto out;
		} else if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			goto out;
		}
		schedule();
	}

	retval = put_user(data, (u32 __user *)buf);
	if (!retval)
		retval = sizeof(data);
out:
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&cs_char_data.wait, &wait);

	DLEAVE(retval);
	return retval;
}

static ssize_t cs_char_write(struct file *file, const char __user *buf,
			 size_t count, loff_t *ppos)
{
	u32	data;
	int	err;
	ssize_t	retval;
	DENTER();

	if (count < sizeof(data))
		return -EINVAL;

	if (get_user(data, (u32 __user *)buf))
		retval = -EFAULT;
	else
		retval = count;

	err = cs_ssi_command(data);
	if (err < 0)
		retval = err;

	DLEAVE(retval);
	return retval;
}

static int cs_char_ioctl(struct inode *inode, struct file *f,
			   unsigned int cmd, unsigned long arg)
{
	int		r = 0;
	DENTER();

	switch (cmd) {
	case CS_GET_STATE: {
		unsigned int state;

		state = cs_ssi_get_state();
		if (copy_to_user((void __user *)arg, &state, sizeof(state)))
			r = -EFAULT;
	}
		break;
	case CS_CONFIG:
		r = -ENOTTY;
		break;
	case CS_SET_WAKELINE: {
		unsigned int state;

		if (copy_from_user(&state, (void __user *)arg,
				sizeof(state)))
			r = -EFAULT;
		else
			cs_ssi_set_wakeline(state);
	}
		break;
	case CS_CONFIG_BUFS: {
		struct cs_buffer_config buf_cfg;

		if (copy_from_user(&buf_cfg, (void __user *)arg,
				   sizeof(buf_cfg)))
			r = -EFAULT;
		else
			r = cs_ssi_buf_config(&buf_cfg);
		break;
	}
	default:
		r = -ENOTTY;
		break;
	}

	DLEAVE(r);
	return r;
}

static int cs_char_mmap(struct file *file, struct vm_area_struct *vma)
{
	DENTER();

	if (vma->vm_end < vma->vm_start) {
		DLEAVE(1);
		return -EINVAL;
	}

	if (((vma->vm_end - vma->vm_start) >> PAGE_SHIFT) != 1) {
		DLEAVE(2);
		return -EINVAL;
	}

	vma->vm_flags |= VM_RESERVED;
	vma->vm_ops = &cs_char_vm_ops;
	vma->vm_private_data = file->private_data;

	cs_char_vma_open(vma);

	DLEAVE(0);
	return 0;
}

static int cs_char_open(struct inode *inode, struct file *file)
{
	int	ret = 0;
	DENTER();

	spin_lock_bh(&cs_char_data.lock);

	if (cs_char_data.opened) {
		ret = -EBUSY;
		spin_unlock_bh(&cs_char_data.lock);
		goto out;
	}

	cs_char_data.mmap_base = get_zeroed_page(GFP_ATOMIC);
	if (!cs_char_data.mmap_base) {
		pr_err("CS_SSI: Shared memory allocation failed.\n");
		ret = -ENOMEM;
		spin_unlock_bh(&cs_char_data.lock);
		goto out;
	}

	cs_char_data.mmap_size = CS_MMAP_SIZE;
	cs_char_data.opened = 1;
	file->private_data = &cs_char_data;

	spin_unlock_bh(&cs_char_data.lock);

	cs_ssi_start(cs_char_data.mmap_base, cs_char_data.mmap_size);

out:
	DLEAVE(ret);
	return ret;
}

static int cs_char_release(struct inode *inode, struct file *file)
{
	struct char_queue	*entry;
	struct list_head	*cursor, *next;
	DENTER();

	cs_ssi_stop();

	spin_lock_bh(&cs_char_data.lock);

	free_page(cs_char_data.mmap_base);
	cs_char_data.mmap_base = 0;
	cs_char_data.mmap_size = 0;
	cs_char_data.opened = 0;

	if (!list_empty(&cs_char_data.chardev_queue)) {
		list_for_each_safe(cursor, next, &cs_char_data.chardev_queue) {
			entry = list_entry(cursor, struct char_queue, list);
			list_del(&entry->list);
			kfree(entry);
		}
	}

	spin_unlock_bh(&cs_char_data.lock);

	DLEAVE(0);
	return 0;
}

static const struct file_operations cs_char_fops = {
	.owner = THIS_MODULE,
	.read = cs_char_read,
	.write = cs_char_write,
	.poll = cs_char_poll,
	.ioctl = cs_char_ioctl,
	.mmap = cs_char_mmap,
	.open = cs_char_open,
	.release = cs_char_release,
	.fasync = cs_char_fasync,
};

static struct miscdevice cs_char_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "cmt_speech",
	.fops = &cs_char_fops
};

static int __init cs_char_init(void)
{
	int ret;
	DENTER();

	printk(KERN_INFO "CMT speech driver v%d.%d.%d\n",
	       CS_VER_MAJOR, CS_VER_MINOR, CS_VER_EXTRA);

	ret = misc_register(&cs_char_miscdev);
	if (ret) {
		pr_err("CMT speech: Failed to register\n");
		goto out;
	}

	init_waitqueue_head(&cs_char_data.wait);
	spin_lock_init(&cs_char_data.lock);

	/* will be moved to cs_char_open */
	cs_char_data.mmap_base = 0;
	cs_char_data.mmap_size = 0;

	cs_char_data.opened = 0;

	INIT_LIST_HEAD(&cs_char_data.chardev_queue);

	cs_ssi_init();

out:
	DLEAVE(ret);
	return ret;
}

static void __exit cs_char_exit(void)
{
	DENTER();

	misc_deregister(&cs_char_miscdev);

	cs_ssi_exit();

	DLEAVE(0);
}
MODULE_AUTHOR("Peter Ujfalusi <peter.ujfalusi@nokia.com>");
MODULE_AUTHOR("Kai Vehmanen <kai.vehmanen@nokia.com>");
MODULE_DESCRIPTION("CMT speech driver");
MODULE_LICENSE("GPL");

module_init(cs_char_init);
module_exit(cs_char_exit);
