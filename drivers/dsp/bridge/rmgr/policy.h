/*
 * policy.h - header file for policy enforcement point for TI DSP bridge access
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Copyright (C) 2010-2011 Nokia Corporation
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/fs.h>
#include <linux/device.h>

int policy_init(struct class *class);
void policy_remove(void);

int policy_open_hook(struct inode *ip, struct file *filp);
int policy_ioctl_pre_hook(struct file *filp, unsigned int code,
			  unsigned long args);
int policy_ioctl_post_hook(struct file *filp, unsigned int code,
			   unsigned long args, int status);
int policy_mmap_hook(struct file *filp);
int policy_release_hook(struct inode *ip, struct file *filp);
