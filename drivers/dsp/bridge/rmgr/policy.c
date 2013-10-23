/*
 * policy.c - policy enforcement point for TI DSP bridge access
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

#include <linux/types.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/timer.h>
#include <linux/sysfs.h>

#include <dspbridge/drv.h>

#include <policy.h>

/* TI DSP ioctl interface specific defines, here we need two of them */
#define is_wait_for_events(code)      ((code & 0xff) == 0x04)
#define is_node_get_message(code)     ((code & 0xff) == 0x48)

struct policy_t {
	struct class       *bridge_class;

	pid_t               tgid;
	pid_t               guest;
	unsigned int        enabled;
	unsigned int        guest_mode;
	int                 preempt;

	struct list_head    tasks;
	struct list_head    pending_tasks;

	struct timer_list   timer;
	int                 pending_count;
	/* mutex to access the list */
	struct mutex        lock;
};

struct dsp_handler_t {
	pid_t               tgid;
	struct pid         *pid;

	struct list_head    task_entry;
	struct list_head    pending_entry;

	int                 pending_count;

	struct completion   pending_comp;
};

static struct policy_t policy;

static inline bool is_handler_guest(struct dsp_handler_t *handler)
{
	return (policy.guest == handler->tgid || !policy.guest);
}

static inline bool is_guest(void)
{
	return (policy.guest == current->tgid || !policy.guest);
}

static inline bool is_handler_guest_restricted(struct dsp_handler_t *handler)
{
	return !policy.guest_mode || policy.tgid || !is_handler_guest(handler);
}

static inline bool is_guest_restricted(void)
{
	return !policy.guest_mode || policy.tgid || !is_guest();
}

static inline bool is_handler_restricted(struct dsp_handler_t *handler)
{
	return (policy.enabled && policy.tgid != handler->tgid &&
		is_handler_guest_restricted(handler));
}

static inline bool is_restricted(void)
{
	return (policy.enabled && policy.tgid != current->tgid &&
		is_guest_restricted());
}

static inline unsigned int count_pendings(pid_t tgid)
{
	unsigned int count = 0;
	struct dsp_handler_t *handler;

	list_for_each_entry(handler, &policy.tasks, task_entry)
		if (tgid != handler->tgid &&
		    !(tgid == policy.guest && policy.guest_mode))
			count++;

	return count;
}

static inline void unblock_pendings(void)
{
	struct dsp_handler_t *handler;

	list_for_each_entry(handler, &policy.pending_tasks, pending_entry)
		complete(&handler->pending_comp);
}

static void set_new_guest(void)
{
	struct dsp_handler_t *handler;

	if (list_empty(&policy.tasks))
		policy.guest = 0;
	else {
		handler = list_first_entry(&policy.tasks,
					   struct dsp_handler_t, task_entry);
		policy.guest = handler->tgid;
	}
}

static int policy_preempt(pid_t tgid, int sig)
{
	struct dsp_handler_t *handler;

	/* Send a signal to all policy restricted tasks */
	list_for_each_entry(handler, &policy.tasks, task_entry)
		if (tgid != handler->tgid) {
			pr_info("%s: send %d signal to %d\n",
				__func__, sig, handler->tgid);
			kill_pid(handler->pid, sig, 0);
		}

	return 0;
}

static void policy_timer_preempt(unsigned long data)
{
	pid_t tgid = (pid_t)data;

	mutex_lock(&policy.lock);
	policy_preempt(tgid, SIGKILL);
	mutex_unlock(&policy.lock);
}

static void policy_enable_preempt(pid_t tgid)
{
	if (policy.preempt > 0) {
		/*
		 * SIGURG signal can be handled by an application,
		 * but it's not a destructive one
		 */
		policy_preempt(tgid, SIGURG);

		policy.timer.data = tgid;
		mod_timer(&policy.timer, jiffies + HZ * policy.preempt);
	} else if (!policy.preempt) {
		policy_preempt(tgid, SIGKILL);
	}
}

static int wait_for_releasing(struct dsp_handler_t *handler)
{
	int status;

	status = wait_for_completion_interruptible(&handler->pending_comp);

	mutex_lock(&policy.lock);
	list_del(&handler->pending_entry);
	if (status)
		status = -EINTR;

	/* policy rules might have been changed while we're blocked */
	if (is_restricted()) {
		if (!status)
			status = -EBUSY;
		policy.pending_count--;
		BUG_ON(policy.pending_count < 0);
	}

	if (status) {
		list_del(&handler->task_entry);
		if (is_guest())
			set_new_guest();
		kfree(handler);
	}
	mutex_unlock(&policy.lock);

	return status;
}

static inline struct dsp_handler_t *handler_by_tgid(pid_t tgid)
{
	struct dsp_handler_t *handler;

	list_for_each_entry(handler, &policy.tasks, task_entry)
		if (tgid == handler->tgid)
			return handler;

	return NULL;
}

static inline struct dsp_handler_t *handler_by_pid(struct pid *pid)
{
	struct dsp_handler_t *handler;

	list_for_each_entry(handler, &policy.tasks, task_entry)
		if (pid == handler->pid)
			return handler;

	return NULL;
}

static void policy_enable(pid_t tgid)
{
	policy.enabled        = 1;
	policy.tgid           = tgid;
	policy.pending_count  = count_pendings(tgid);

	/* blocked on open tasks are still in pendings list, remove them */
	unblock_pendings();

	/* preempt the current users of DSP if any */
	policy_enable_preempt(tgid);

	pr_debug("DSP policy enabled for %d process group, %d pendings\n",
		 tgid, policy.pending_count);
}

static void policy_disable(void)
{
	/* do not preempt all current users of DSP */
	policy.enabled        = 0;
	policy.pending_count  = 0;

	/* blocked on open tasks are still in pending list, remove them */
	unblock_pendings();

	del_timer_sync(&policy.timer);
}

static void policy_state_pid_update(int tgid)
{
	if (tgid != policy.tgid) {
		policy.tgid = tgid;
		if (policy.enabled)
			policy_enable(tgid);
	}
}

static void policy_state_enabled_update(int enabled)
{
	if (enabled > 0 && !policy.enabled)
		policy_enable(policy.tgid);
	else if (!enabled && policy.enabled)
		policy_disable();
}

static void policy_state_guest_update(int guest_mode)
{
	if (guest_mode != policy.guest_mode) {
		policy.guest_mode = guest_mode;
		if (!guest_mode && policy.enabled && !policy.tgid)
			policy_enable(0);
	}
}

static ssize_t policy_show(struct class *class, char *buf)
{
	ssize_t len = 0;

	if (mutex_lock_interruptible(&policy.lock))
		return -EINTR;

	len += snprintf(&buf[len], PAGE_SIZE, "%d\n", policy.tgid);

	mutex_unlock(&policy.lock);

	return len;
}

static ssize_t policy_store(struct class *class, const char *buf, size_t count)
{
	long tgid;
	int  status;

	if (mutex_lock_interruptible(&policy.lock))
		return -EINTR;

	status = strict_strtol(buf, 0, &tgid);
	if (status < 0)
		goto done;

	if (tgid < 0 || tgid > PID_MAX_DEFAULT) {
		status = -EINVAL;
		goto done;
	}

	policy_state_pid_update(tgid);

 done:
	mutex_unlock(&policy.lock);

	return status ? : count;
}

static ssize_t enabled_show(struct class *class, char *buf)
{
	ssize_t len = 0;

	if (mutex_lock_interruptible(&policy.lock))
		return -EINTR;

	len += snprintf(&buf[len], PAGE_SIZE, "%u\n", policy.enabled);

	mutex_unlock(&policy.lock);

	return len;
}

static ssize_t enabled_store(struct class *class, const char *buf, size_t count)
{
	long enabled;
	int  status;

	if (mutex_lock_interruptible(&policy.lock))
		return -EINTR;

	status = strict_strtol(buf, 0, &enabled);
	if (status < 0)
		goto done;

	if (enabled < 0) {
		status = -EINVAL;
		goto done;
	}

	policy_state_enabled_update(!!enabled);

 done:
	mutex_unlock(&policy.lock);

	return status ? : count;
}

static ssize_t guest_mode_show(struct class *class, char *buf)
{
	ssize_t len = 0;

	if (mutex_lock_interruptible(&policy.lock))
		return -EINTR;

	len += snprintf(&buf[len], PAGE_SIZE, "%u\n", policy.guest_mode);

	mutex_unlock(&policy.lock);

	return len;
}

static ssize_t guest_mode_store(struct class *class,
			       const char *buf, size_t count)
{
	long guest_mode;
	int  status;

	if (mutex_lock_interruptible(&policy.lock))
		return -EINTR;

	status = strict_strtol(buf, 0, &guest_mode);
	if (status < 0)
		goto done;

	if (guest_mode < 0) {
		status = -EINVAL;
		goto done;
	}

	policy_state_guest_update(!!guest_mode);

 done:
	mutex_unlock(&policy.lock);

	return status ? : count;
}

static ssize_t preempt_show(struct class *class, char *buf)
{
	ssize_t len = 0;

	if (mutex_lock_interruptible(&policy.lock))
		return -EINTR;

	len += snprintf(&buf[len], PAGE_SIZE, "%d\n", policy.preempt);

	mutex_unlock(&policy.lock);

	return len;
}

static ssize_t preempt_store(struct class *class, const char *buf, size_t count)
{
	long preempt;
	int  status;

	if (mutex_lock_interruptible(&policy.lock))
		return -EINTR;

	status = strict_strtol(buf, 0, &preempt);
	if (status < 0)
		goto done;

	if (preempt < 0)
		preempt = -1;

	if (preempt > 60)
		preempt = 60;

	if (policy.preempt != preempt) {
		policy.preempt = preempt;
		if (policy.enabled)
			policy_enable_preempt(policy.tgid);
	}

 done:
	mutex_unlock(&policy.lock);

	return status ? : count;
}

static struct class_attribute policy_attrs[] = {
	__ATTR(policy,     S_IRUSR|S_IWUSR, policy_show,     policy_store),
	__ATTR(enabled,    S_IRUSR|S_IWUSR, enabled_show,    enabled_store),
	__ATTR(guest_mode, S_IRUSR|S_IWUSR, guest_mode_show, guest_mode_store),
	__ATTR(preempt,    S_IRUSR|S_IWUSR, preempt_show,    preempt_store),
};

int policy_init(struct class *class)
{
	int i;
	int status;

	policy.bridge_class   = class;

	policy.tgid           = 0;
	policy.guest          = 0;
	policy.enabled        = 0;
	policy.guest_mode     = 0;
	policy.preempt        = -1;
	policy.pending_count  = 0;

	INIT_LIST_HEAD(&policy.tasks);
	INIT_LIST_HEAD(&policy.pending_tasks);

	mutex_init(&policy.lock);

	init_timer(&policy.timer);
	policy.timer.data = 0;
	policy.timer.function = policy_timer_preempt;

	for (i = 0; i < ARRAY_SIZE(policy_attrs); i++) {
		status = class_create_file(class, &policy_attrs[i]);
		if (status != 0)
			goto err_policy;
	}

	if (policy.enabled)
		policy_enable(policy.tgid);

	return status;

 err_policy:
	while (--i >= 0)
		class_remove_file(policy.bridge_class, &policy_attrs[i]);

	policy.bridge_class = NULL;
	pr_err("dspbridge: policy for DSP is not initialized\n");

	return status;
}

void policy_remove(void)
{
	int i;

	if (!policy.bridge_class)
		return;

	for (i = 0; i < ARRAY_SIZE(policy_attrs); i++)
		class_remove_file(policy.bridge_class, &policy_attrs[i]);

	del_timer_sync(&policy.timer);

	mutex_destroy(&policy.lock);

	policy.bridge_class = NULL;
	policy.tgid = 0;
}


/* Policy enforcement operations */
int policy_open_hook(struct inode *ip, struct file *filp)
{
	int status = 0;
	struct dsp_handler_t   *handler;
	struct process_context *context = filp->private_data;

	if (!context)
		return -EIO;

	if (mutex_lock_interruptible(&policy.lock))
		return -EINTR;

	if (is_restricted()) {
		status = -EBUSY;
		goto no_blocking;
	}

	handler = handler_by_tgid(current->tgid);
	if (handler) {
		context->policy = handler;
		handler->pending_count++;
		pr_debug("%s: handler for %d is incremented\n",
			 __func__, current->tgid);
		goto no_blocking;
	}

	if (policy.pending_count && (filp->f_flags & O_NONBLOCK)) {
		status = -EAGAIN;
		goto no_blocking;
	}

	handler = kzalloc(sizeof(struct dsp_handler_t), GFP_KERNEL);
	if (!handler) {
		status = -ENOMEM;
		goto no_blocking;
	}

	context->policy = handler;

	handler->tgid = current->tgid;
	handler->pid  = get_pid(task_tgid(current));
	handler->pending_count  = 1;

	INIT_LIST_HEAD(&handler->task_entry);
	INIT_LIST_HEAD(&handler->pending_entry);
	init_completion(&handler->pending_comp);

	list_add(&handler->task_entry, &policy.tasks);
	pr_debug("%s: handler for %d is added\n", __func__, handler->tgid);

	/*
	 * Any process could become a guest,
	 * that's a matter of future applied policy restrictions
	 */
	if (!policy.guest)
		policy.guest = current->tgid;

	/*
	 * If policy is enabled, there might be tasks in the process of
	 * releasing the resource, we need to wait for them.
	 */
	if (policy.pending_count) {
		pr_debug("%s: %d pending tasks\n",
			 __func__, policy.pending_count);
		list_add(&handler->pending_entry, &policy.pending_tasks);
		mutex_unlock(&policy.lock);
		status = wait_for_releasing(handler);
		return status;
	}

 no_blocking:
	mutex_unlock(&policy.lock);

	return status;
}

int policy_ioctl_pre_hook(struct file *filp, unsigned int code,
			  unsigned long args)
{
	int status = 0;

	mutex_lock(&policy.lock);

	if (is_restricted()) {
		if (is_wait_for_events(code)) {
			/*
			 * It is wanted to filter out wait_for_bridge_events to
			 * decrease a preemption timeout before the clean up
			 * procedure
			 */
			pr_debug("%s: ioctl restricted wait_for_events by %d\n",
				 __func__, current->tgid);
			status = -EBUSY;
		} else if (is_node_get_message(code)) {
			/*
			 * Need to filter out get_message, due to a loop in
			 * user space, which blocks a call of wait_for_events
			 */
			pr_debug("%s: ioctl restricted get_message by %d\n",
				 __func__, current->tgid);
			status = -EBUSY;
		}

		/*
		 * Bypass all other ioctl commands from restricted processes,
		 * they're needed for proper resource deinitialization
		 */
		goto exit;
	}

	if (is_wait_for_events(code))
		pr_debug("%s: enter wait_for_events by %d\n",
			 __func__, current->tgid);

 exit:
	mutex_unlock(&policy.lock);

	return status;
}

int policy_ioctl_post_hook(struct file *filp, unsigned int code,
			   unsigned long args, int status)
{
	if (is_wait_for_events(code))
		pr_debug("%s: exit wait_for_events by %d\n",
			 __func__, current->tgid);

	return status;
}

int policy_mmap_hook(struct file *filp)
{
	int status = 0;

	mutex_lock(&policy.lock);
	if (is_restricted())
		status = -EACCES;
	mutex_unlock(&policy.lock);

	return status;
}

int policy_release_hook(struct inode *ip, struct file *filp)
{
	struct process_context *context = filp->private_data;
	struct dsp_handler_t   *handler;

	if (!context || !context->policy)
		return -EIO;

	handler = context->policy;

	mutex_lock(&policy.lock);

	handler->pending_count--;
	if (handler->pending_count) {
		mutex_unlock(&policy.lock);
		pr_debug("%s: handler for %d is decremented\n",
			 __func__, current->tgid);
		return 0;
	}

	put_pid(handler->pid);

	list_del(&handler->task_entry);

	if (is_handler_guest(handler))
		set_new_guest();

	/*
	 * If the last policy prohibited task released the node,
	 * we can unblock all policy allowed tasks opening it in their turn
	 */
	if (is_handler_restricted(handler)) {
		policy.pending_count--;
		pr_debug("%s: %d pending tasks\n",
			 __func__, policy.pending_count);
		BUG_ON(policy.pending_count < 0);
		if (!policy.pending_count)
			unblock_pendings();
	}

	mutex_unlock(&policy.lock);

	kfree(handler);
	pr_debug("%s: handler for %d is removed\n", __func__, current->tgid);

	return 0;
}
