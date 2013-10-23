/*
 * linux/arch/arm/plat-omap/resource.c
 * Shared Resource Framework API implementation
 *
 * Copyright (C) 2007-2008 Texas Instruments, Inc.
 * Rajendra Nayak <rnayak@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 * History:
 *
 */

#include <linux/errno.h>
#include <linux/err.h>
#include <mach/resource.h>
#include <linux/slab.h>

/*
 * This is for statically defining the users pool. This static pool is
 * used early at bootup till kmalloc becomes available.
 */
#define MAX_USERS	10
#define UNUSED		0x0
#define	DYNAMIC_ALLOC	0x1
#define STATIC_ALLOC	0x2

/* res_list contains all registered struct shared_resource */
static LIST_HEAD(res_list);

/* res_mutex protects res_list add and del ops */
static DECLARE_MUTEX(res_mutex);

/* Static Pool of users for a resource used till kmalloc becomes available */
struct  users_list usr_list[MAX_USERS];

/* Private/Internal functions */

/**
 * _resource_lookup - loop up a resource by its name, return a pointer
 * @name: The name of the resource to lookup
 *
 * Looks for a registered resource by its name. Returns a pointer to
 * the struct shared_resource if found, else returns NULL.
 * The function is not lock safe.
 */
static struct shared_resource *_resource_lookup(const char *name)
{
	struct shared_resource *res, *tmp_res;

	if (!name)
		return NULL;

	res = NULL;

	list_for_each_entry(tmp_res, &res_list, node) {
		if (!strcmp(name, tmp_res->name)) {
			res = tmp_res;
			break;
		}
	}
	return res;
}

/**
 * resource_lookup - loop up a resource by its name, return a pointer
 * @name: The name of the resource to lookup
 *
 * Looks for a registered resource by its name. Returns a pointer to
 * the struct shared_resource if found, else returns NULL.
 * The function holds mutex and takes care of atomicity.
 */
static struct shared_resource *resource_lookup(const char *name)
{
	struct shared_resource *res;

	if (!name)
		return NULL;
	down(&res_mutex);
	res = _resource_lookup(name);
	up(&res_mutex);

	return res;
}

/**
 * update_resource_level - Regenerates and updates the curr_level of the res
 * @resp: Pointer to the resource
 *
 * This function looks at all the users of the given resource and the levels
 * requested by each of them, and recomputes a target level for the resource
 * acceptable to all its current usres. It then calls platform specific
 * change_level to change the level of the resource.
 * Returns 0 on success, else a non-zero value returned by the platform
 * specific change_level function.
 **/
static int update_resource_level(struct shared_resource *resp)
{
	struct users_list *user;
	unsigned long target_level;
	int ret;

	/* Regenerate the target_value for the resource */
	target_level = RES_DEFAULTLEVEL;
	list_for_each_entry(user, &resp->users_list, node)
		if (user->level > target_level)
			target_level = user->level;

	pr_debug("SRF: Changing Level for resource %s to %ld\n",
				resp->name, target_level);
	ret = resp->ops->change_level(resp, target_level);
	if (ret) {
		printk(KERN_ERR "Unable to Change"
					"level for resource %s to %ld\n",
		resp->name, target_level);
	}
	return ret;
}

/**
 * get_user - gets a new users_list struct from static pool or dynamically
 *
 * This function initally looks for availability in the static pool and
 * tries to dynamcially allocate only once the static pool is empty.
 * We hope that during bootup by the time we hit a case of dynamic allocation
 * slab initialization would have happened.
 * Returns a pointer users_list struct on success. On dynamic allocation failure
 * returns a ERR_PTR(-ENOMEM).
 */
static struct users_list *get_user(void)
{
	int ind = 0;
	struct users_list *user;

	/* See if something available in the static pool */
	while (ind < MAX_USERS) {
		if (usr_list[ind].usage == UNUSED)
			break;
		else
			ind++;
	}
	if (ind < MAX_USERS) {
		/* Pick from the static pool */
		user = &usr_list[ind];
		user->usage = STATIC_ALLOC;
	} else {
		/* By this time we hope slab is initialized */
		if (slab_is_available()) {
			user = kmalloc(sizeof(struct  users_list), GFP_KERNEL);
			if (!user) {
				printk(KERN_ERR "SRF:FATAL ERROR: kmalloc"
							"failed\n");
				return ERR_PTR(-ENOMEM);
			}
			user->usage = DYNAMIC_ALLOC;
		} else {
			/* Dynamic alloc not available yet */
			printk(KERN_ERR "SRF: FATAL ERROR: users_list"
				"initial POOL EMPTY before slab init\n");
			return ERR_PTR(-ENOMEM);
		}
	}
	return user;
}

/**
 * free_user - frees the dynamic users_list and marks the static one unused
 * @user: The struct users_list to be freed
 *
 * Looks at the usage flag and either frees the users_list if it was
 * dynamically allocated, and if its from the static pool, marks it unused.
 * No return value.
 */
void free_user(struct users_list *user)
{
	if (user->usage == DYNAMIC_ALLOC) {
		kfree(user);
	} else {
		user->usage = UNUSED;
		user->level = RES_DEFAULTLEVEL;
		user->dev = NULL;
	}
}

/**
 * resource_init - Initializes the Shared resource framework.
 * @resources: List of all the resources modelled
 *
 * Loops through the list of resources and registers all that
 * are available for the current CPU.
 * No return value
 */
void resource_init(struct shared_resource **resources)
{
	struct shared_resource **resp;
	int ind;

	pr_debug("Initializing Shared Resource Framework\n");

	if (!cpu_is_omap34xx()) {
		/* This CPU is not supported */
		printk(KERN_WARNING "Shared Resource Framework does not"
			"support this CPU type.\n");
		WARN_ON(1);
	}

	/* Init the users_list POOL */
	for (ind = 0; ind < MAX_USERS; ind++) {
		usr_list[ind].usage = UNUSED;
		usr_list[ind].dev = NULL;
		usr_list[ind].level = RES_DEFAULTLEVEL;
	}

	if (resources)
		for (resp = resources; *resp; resp++)
			resource_register(*resp);
}

/**
 * resource_refresh - Refresh the states of all current resources
 *
 * If a condition in power domains has changed that requires refreshing
 * power domain states, this function can be used to restore correct
 * states according to shared resources.
 * Returns 0 on success, non-zero, if some resource cannot be refreshed.
 */
int resource_refresh(void)
{
	struct shared_resource *resp = NULL;
	int ret = 0;

	list_for_each_entry(resp, &res_list, node) {
		ret = update_resource_level(resp);
		if (ret)
			break;
	}
	return ret;
}

/**
 * resource_register - registers and initializes a resource
 * @res: struct shared_resource * to register
 *
 * Initializes the given resource and adds it to the resource list
 * for the current CPU.
 * Returns 0 on success, -EINVAL if given a NULL pointer, -EEXIST if the
 * resource is already registered.
 */
int resource_register(struct shared_resource *resp)
{
	if (!resp)
		return -EINVAL;

	if (!omap_chip_is(resp->omap_chip))
		return -EINVAL;

	/* Verify that the resource is not already registered */
	if (resource_lookup(resp->name))
		return -EEXIST;

	INIT_LIST_HEAD(&resp->users_list);

	down(&res_mutex);
	/* Add the resource to the resource list */
	list_add(&resp->node, &res_list);

	/* Call the resource specific init*/
	if (resp->ops->init)
		resp->ops->init(resp);

	up(&res_mutex);
	pr_debug("resource: registered %s\n", resp->name);

	return 0;
}
EXPORT_SYMBOL(resource_register);

/**
 * resource_unregister - unregister a resource
 * @res: struct shared_resource * to unregister
 *
 * Removes a resource from the resource list.
 * Returns 0 on success, -EINVAL if passed a NULL pointer.
 */
int resource_unregister(struct shared_resource *resp)
{
	if (!resp)
		return -EINVAL;

	down(&res_mutex);
	/* delete the resource from the resource list */
	list_del(&resp->node);
	up(&res_mutex);

	pr_debug("resource: unregistered %s\n", resp->name);

	return 0;
}
EXPORT_SYMBOL(resource_unregister);

/**
 * resource_request - Request for a required level of a resource
 * @name: The name of the resource requested
 * @dev: Uniquely identifes the caller
 * @level: The requested level for the resource
 *
 * This function recomputes the target level of the resource based on
 * the level requested by the user. The level of the resource is
 * changed to the target level, if it is not the same as the existing level
 * of the resource. Multiple calls to this function by the same device will
 * replace the previous level requested
 * Returns 0 on success, -EINVAL if the resource name passed in invalid.
 * -ENOMEM if no static pool available or dynamic allocations fails.
 * Else returns a non-zero error value returned by one of the failing
 * shared_resource_ops.
 */
int resource_request(const char *name, struct device *dev,
					unsigned long level)
{
	struct shared_resource *resp;
	struct  users_list *user;
	int 	found = 0, ret = 0;

	down(&res_mutex);
	resp = _resource_lookup(name);
	if (!resp) {
		printk(KERN_ERR "resource_request: Invalid resource name\n");
		ret = -EINVAL;
		goto res_unlock;
	}

	/* Call the resource specific validate function */
	if (resp->ops->validate_level) {
		ret = resp->ops->validate_level(resp, level);
		if (ret)
			goto res_unlock;
	}

	list_for_each_entry(user, &resp->users_list, node) {
		if (user->dev == dev) {
			found = 1;
			break;
		}
	}

	if (!found) {
		/* First time user */
		user = get_user();
		if (IS_ERR(user)) {
			ret = -ENOMEM;
			goto res_unlock;
		}
		user->dev = dev;
		list_add(&user->node, &resp->users_list);
		resp->no_of_users++;
	}
	user->level = level;

res_unlock:
	up(&res_mutex);
	/*
	 * Recompute and set the current level for the resource.
	 * NOTE: update_resource level moved out of spin_lock, as it may call
	 * pm_qos_add_requirement, which does a kzmalloc. This won't be allowed
	 * in iterrupt context. The spin_lock still protects add/remove users.
	 */
	if (!ret)
		ret = update_resource_level(resp);
	return ret;
}
EXPORT_SYMBOL(resource_request);

/**
 * resource_release - Release a previously requested level of a resource
 * @name: The name of the resource to be released
 * @dev: Uniquely identifes the caller
 *
 * This function recomputes the target level of the resource after removing
 * the level requested by the user. The level of the resource is
 * changed to the target level, if it is not the same as the existing level
 * of the resource.
 * Returns 0 on success, -EINVAL if the resource name or dev structure
 * is invalid.
 */
int resource_release(const char *name, struct device *dev)
{
	struct shared_resource *resp;
	struct users_list *user;
	int found = 0, ret = 0;

	down(&res_mutex);
	resp = _resource_lookup(name);
	if (!resp) {
		printk(KERN_ERR "resource_release: Invalid resource name\n");
		ret = -EINVAL;
		goto res_unlock;
	}

	list_for_each_entry(user, &resp->users_list, node) {
		if (user->dev == dev) {
			found = 1;
			break;
		}
	}

	if (!found) {
		/* No such user exists */
		ret = -EINVAL;
		goto res_unlock;
	}

	resp->no_of_users--;
	list_del(&user->node);
	free_user(user);

	/* Recompute and set the current level for the resource */
	ret = update_resource_level(resp);
res_unlock:
	up(&res_mutex);
	return ret;
}
EXPORT_SYMBOL(resource_release);

/**
 * resource_get_level - Returns the current level of the resource
 * @name: Name of the resource
 *
 * Returns the current level of the resource if found, else returns
 * -EINVAL if the resource name is invalid.
 */
int resource_get_level(const char *name)
{
	struct shared_resource *resp;
	u32 ret;

	down(&res_mutex);
	resp = _resource_lookup(name);
	if (!resp) {
		printk(KERN_ERR "resource_release: Invalid resource name\n");
		up(&res_mutex);
		return -EINVAL;
	}
	ret = resp->curr_level;
	up(&res_mutex);
	return ret;
}
EXPORT_SYMBOL(resource_get_level);
