/*
 * linux/include/asm-arm/arch-omap/resource.h
 * Structure definitions for Shared resource Framework
 *
 * Copyright (C) 2007-2008 Texas Instruments, Inc.
 * Written by Rajendra Nayak <rnayak@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * History:
 *
 */

#ifndef __ARCH_ARM_OMAP_RESOURCE_H
#define __ARCH_ARM_OMAP_RESOURCE_H

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <mach/cpu.h>

#define RES_DEFAULTLEVEL	0x0

struct shared_resource_ops; /* forward declaration */

/* Used to model a Shared Multilevel Resource */
struct shared_resource {
	/* Resource name */
	char *name;
	/* Used to represent the OMAP chip types containing this res */
	const struct omap_chip_id omap_chip;
	/* Total no of users at any point of this resource */
	u8 no_of_users;
	/* Current level of this resource */
	u32 curr_level;
	/* Used to store any resource specific data */
	void  *resource_data;
	/* List of all the current users for this resource */
	struct list_head users_list;
	/* Shared resource operations */
	struct shared_resource_ops *ops;
	struct list_head node;
};

struct shared_resource_ops {
	/* Init function for the resource */
	void (*init)(struct shared_resource *res);
	/* Function to change the level of the resource */
	int (*change_level)(struct shared_resource *res, u32 target_level);
	/* Function to validate the requested level of the resource */
	int (*validate_level)(struct shared_resource *res, u32 target_level);
};

/* Used to represent a user of a shared resource */
struct users_list {
	/* Device pointer used to uniquely identify the user */
	struct device *dev;
	/* Current level as requested for the resource by the user */
	u32 level;
	struct list_head node;
	u8 usage;
};

extern struct shared_resource *resources_omap[];
/* Shared resource Framework API's */
void resource_init(struct shared_resource **resources);
int resource_refresh(void);
int resource_register(struct shared_resource *res);
int resource_unregister(struct shared_resource *res);
int resource_request(const char *name, struct device *dev,
						 unsigned long level);
int resource_release(const char *name, struct device *dev);
int resource_get_level(const char *name);

#endif /* __ARCH_ARM_OMAP_RESOURCE_H */
