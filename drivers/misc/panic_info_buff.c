/*
 * Copyright (C) Nokia Corporation
 *
 * Contact: Atal Shargorodsky <ext-atal.shargorodsky@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <linux/notifier.h>

#define PANIC_BUFFER_MAX_LEN  1024
static char panic_info_buff[PANIC_BUFFER_MAX_LEN];
static struct dentry *panic_info_buff_debugfs;

static int panic_info_buff_open(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t panic_info_buff_write(struct file *file,
		const char __user *buf, size_t len, loff_t *off)
{
	if (len >= PANIC_BUFFER_MAX_LEN)
		return -EINVAL;
	if (copy_from_user(panic_info_buff, buf, len))
		return -EFAULT;
	panic_info_buff[len] = '\0';
	return len;
}

static struct file_operations panic_info_buff_fops = {
	.open   = panic_info_buff_open,
	.write  = panic_info_buff_write,
	.llseek = no_llseek,
	.owner  = THIS_MODULE,
};

static int panic_info_buff_event(struct notifier_block *this,
	unsigned long event, void *ptr)
{
	if (panic_info_buff[0] == '\0') {
		printk(KERN_EMERG "Panic info buffer is empty.\n");
	} else {
		printk(KERN_EMERG "Panic info buffer:\n");
		printk(KERN_EMERG "%s\n", panic_info_buff);
	}
	return NOTIFY_OK;
}

static struct notifier_block panic_info_buff_block = {
	.notifier_call  = panic_info_buff_event,
	.priority       = 1,
};

static int __devinit panic_info_buff_init(void)
{
	panic_info_buff_debugfs = debugfs_create_file("panic_info_buff",
		S_IFREG | S_IWUSR | S_IWGRP,
		NULL, NULL, &panic_info_buff_fops);
	atomic_notifier_chain_register(&panic_notifier_list,
		&panic_info_buff_block);
	return 0;
}
module_init(panic_info_buff_init);

static void __devexit panic_info_buff_exit(void)
{
	debugfs_remove(panic_info_buff_debugfs);
	atomic_notifier_chain_unregister(&panic_notifier_list,
		&panic_info_buff_block);

}
module_exit(panic_info_buff_exit);

MODULE_AUTHOR("Nokia Corporation");
MODULE_LICENSE("GPL");
MODULE_ALIAS("panic_info_buff");
