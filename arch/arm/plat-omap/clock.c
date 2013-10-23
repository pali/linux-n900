/*
 *  linux/arch/arm/plat-omap/clock.c
 *
 *  Copyright (C) 2004 - 2008 Nokia corporation
 *  Written by Tuukka Tikkanen <tuukka.tikkanen@elektrobit.com>
 *
 *  Modified for omap shared clock framework by Tony Lindgren <tony@atomide.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/cpufreq.h>
#include <linux/notifier.h>
#include <linux/debugfs.h>
#include <linux/io.h>
#include <linux/bootmem.h>
#include <linux/slab.h>

#include <mach/clock.h>

static LIST_HEAD(clocks);
static DEFINE_MUTEX(clocks_mutex);
static DEFINE_SPINLOCK(clockfw_lock);

static struct clk_functions *arch_clock;

static LIST_HEAD(clk_notifier_list);

/**
 * omap_clk_for_each_child - call callback on each child clock of clk
 * @clk: struct clk * to use as the "parent"
 * @parent_rate: rate of the parent of @clk to pass along
 * @rate_storage: flag indicating whether current or temporary rates are used
 * @cb: pointer to a callback function
 *
 * For each child clock of @clk, call the callback function @cb, passing
 * along the contents of @parent_rate and @rate_storage.  If the callback
 * function returns non-zero, terminate the function and pass along the
 * return value.
 */
static int omap_clk_for_each_child(struct clk *clk, unsigned long parent_rate,
				   u8 rate_storage,
				   int (*cb)(struct clk *clk,
					     unsigned long parent_rate,
					     u8 rate_storage))
{
	struct clk_child *child;
	int ret;

	list_for_each_entry(child, &clk->children, node) {
		ret = (*cb)(child->clk, parent_rate, rate_storage);
		if (ret)
			break;
	}

	return ret;
}

/**
 * omap_clk_has_children - does clk @clk have any child clocks?
 * @clk: struct clk * to test for child clocks
 *
 * If clock @clk has any child clocks, return 1; otherwise, return 0.
 */
static int omap_clk_has_children(struct clk *clk)
{
	return (list_empty(&clk->children)) ? 0 : 1;
}

/**
 * _do_propagate_rate - callback function for rate propagation
 * @clk: struct clk * to recalc and propagate from
 * @parent_rate: rate of the parent of @clk, to use in recalculation
 * @rate_storage: flag indicating whether current or temporary rates are used
 *
 * If @clk has a recalc function, call it.  If @clk has any children,
 * propagate @clk's rate.  Returns 0.
 */
static int _do_propagate_rate(struct clk *clk, unsigned long parent_rate,
			      u8 rate_storage)
{
	if (clk->recalc)
		clk->recalc(clk, parent_rate, rate_storage);
	if (omap_clk_has_children(clk))
		propagate_rate(clk, rate_storage);
	return 0;
}

/**
 * _clk_free_notifier_chain - safely remove struct clk_notifier
 * @cn: struct clk_notifier *
 *
 * Removes the struct clk_notifier @cn from the clk_notifier_list and
 * frees it.
 */
static void _clk_free_notifier_chain(struct clk_notifier *cn)
{
	list_del(&cn->node);
	kfree(cn);
}

/**
 * omap_clk_add_child - add a child clock @clk2 to @clk
 * @clk: parent struct clk *
 * @clk2: new child struct clk *
 *
 * Add a child clock @clk2 to the list of children of parent clock
 * @clk.  Will potentially allocate memory from bootmem or, if
 * available, from slab.  Must only be called with the clock framework
 * spinlock held.  No return value.
 */
void omap_clk_add_child(struct clk *clk, struct clk *clk2)
{
	struct clk_child *child;
	int reuse = 0;

	if (!clk->children.next)
		INIT_LIST_HEAD(&clk->children);

	list_for_each_entry(child, &clk->children, node) {
		if (child->flags & CLK_CHILD_DELETED) {
			reuse = 1;
			child->flags &= ~CLK_CHILD_DELETED;
			break;
		}
	}

	if (!reuse) {
		if (slab_is_available())
			child = kmalloc(sizeof(struct clk_child), GFP_ATOMIC);
		else
			child = alloc_bootmem(sizeof(struct clk_child));

		if (!child) {
			WARN_ON(1);
			return;
		}

		memset(child, 0, sizeof(struct clk_child));

		if (slab_is_available())
			child->flags |= CLK_CHILD_SLAB_ALLOC;
	}

	child->clk = clk2;

	list_add_tail(&child->node, &clk->children);
}

/**
 * omap_clk_del_child - add a child clock @clk2 to @clk
 * @clk: parent struct clk *
 * @clk2: former child struct clk *
 *
 * Remove a child clock @clk2 from the list of children of parent
 * clock @clk.  Must only be called with the clock framework spinlock
 * held.  No return value.
 */
void omap_clk_del_child(struct clk *clk, struct clk *clk2)
{
	struct clk_child *child, *tmp;

	/* Loop over all existing clk_childs, when found, deallocate */
	list_for_each_entry_safe(child, tmp, &clk->children, node) {
		if (child->clk == clk2) {
			list_del(&child->node);
			if (child->flags & CLK_CHILD_SLAB_ALLOC) {
				kfree(child);
			} else {
				child->clk = NULL;
				child->flags |= CLK_CHILD_DELETED;
			}
			break;
		}
	}
}

/**
 * omap_clk_notify - call clk notifier chain
 * @clk: struct clk * that is changing rate
 * @msg: clk notifier type (i.e., CLK_POST_RATE_CHANGE; see mach/clock.h)
 * @old_rate: old rate
 * @new_rate: new rate
 *
 * Triggers a notifier call chain on the post-clk-rate-change notifier
 * for clock 'clk'.  Passes a pointer to the struct clk and the
 * previous and current rates to the notifier callback.  Intended to be
 * called by internal clock code only.  No return value.
 */
static void omap_clk_notify(struct clk *clk, unsigned long msg,
			    unsigned long old_rate, unsigned long new_rate)
{
	struct clk_notifier *cn;
	struct clk_notifier_data cnd;

	cnd.clk = clk;
	cnd.old_rate = old_rate;
	cnd.new_rate = new_rate;

	list_for_each_entry(cn, &clk_notifier_list, node) {
		if (cn->clk == clk) {
			blocking_notifier_call_chain(&cn->notifier_head, msg,
						     &cnd);
			break;
		}
	}
}

/**
 * omap_clk_notify_downstream - trigger clock change notifications
 * @clk: struct clk * to start the notifications with
 * @msg: notifier msg - see "Clk notifier callback types" in mach/clock.h
 * @param2: (not used - any u8 will do)
 *
 * Call clock change notifiers on clocks starting with @clk and including
 * all of @clk's downstream children clocks.  Returns NOTIFY_DONE.
 */
static int omap_clk_notify_downstream(struct clk *clk, unsigned long msg,
				      u8 param2)
{
	if (!clk->notifier_count)
		return NOTIFY_DONE;

	omap_clk_notify(clk, msg, clk->rate, clk->temp_rate);

	if (!omap_clk_has_children(clk))
		return NOTIFY_DONE;

	return omap_clk_for_each_child(clk, msg, 0, omap_clk_notify_downstream);
}


/**
 * _clk_pre_notify_set_parent - handle pre-notification for clk_set_parent()
 * @clk: struct clk * changing parent
 *
 * When @clk is ready to change its parent, handle pre-notification.
 * If the architecture does not have an
 * arch_clock->clk_round_rate_parent() defined, this code will be unable
 * to verify that the selected parent is valid, and also unable to pass the
 * post-parent-change clock rate to the notifier.  Returns any error from
 * clk_round_rate_parent() or 0 upon success.
 */
static int _clk_pre_notify_set_parent(struct clk *clk, struct clk *parent)
{
	long rate;

	if (!clk->notifier_count)
		return 0;

	if (!arch_clock->clk_round_rate_parent) {
		pr_warning("clock: clk_set_parent(): WARNING: "
			   "clk_round_rate_parent() undefined: pre-notifiers "
			   "will get bogus rate\n");

		rate = 0;
	} else {
		rate = arch_clock->clk_round_rate_parent(clk, parent);
	};

	if (IS_ERR_VALUE(rate))
		return rate;

	clk->temp_rate = rate;
	propagate_rate(clk, TEMP_RATE);

	omap_clk_notify_downstream(clk, CLK_PRE_RATE_CHANGE, 0);

	return 0;
}


/*-------------------------------------------------------------------------
 * Standard clock functions defined in include/linux/clk.h
 *-------------------------------------------------------------------------*/

/*
 * Returns a clock. Note that we first try to use device id on the bus
 * and clock name. If this fails, we try to use clock name only.
 */
struct clk * clk_get(struct device *dev, const char *id)
{
	struct clk *p, *clk = ERR_PTR(-ENOENT);
	int idno;

	if (dev == NULL || dev->bus != &platform_bus_type)
		idno = -1;
	else
		idno = to_platform_device(dev)->id;

	mutex_lock(&clocks_mutex);

	list_for_each_entry(p, &clocks, node) {
		if (p->id == idno && strcmp(id, p->name) == 0) {
			clk = p;
			goto found;
		}
	}

	list_for_each_entry(p, &clocks, node) {
		if (strcmp(id, p->name) == 0) {
			clk = p;
			break;
		}
	}

found:
	mutex_unlock(&clocks_mutex);

	return clk;
}
EXPORT_SYMBOL(clk_get);

int clk_enable(struct clk *clk)
{
	unsigned long flags;
	int ret = 0;

	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

	spin_lock_irqsave(&clockfw_lock, flags);
	if (arch_clock->clk_enable) {
		ret = arch_clock->clk_enable(clk);
		if (ret == 0 && clk->flags & RECALC_ON_ENABLE)
			_do_propagate_rate(clk, clk->parent->rate,
					   CURRENT_RATE);
	}

	spin_unlock_irqrestore(&clockfw_lock, flags);

	return ret;
}
EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *clk)
{
	unsigned long flags;

	if (clk == NULL || IS_ERR(clk))
		return;

	spin_lock_irqsave(&clockfw_lock, flags);
	if (clk->usecount == 0) {
		printk(KERN_ERR "Trying disable clock %s with 0 usecount\n",
		       clk->name);
		WARN_ON(1);
		goto out;
	}

	if (arch_clock->clk_disable) {
		arch_clock->clk_disable(clk);
		if (clk->flags & RECALC_ON_ENABLE)
			_do_propagate_rate(clk, clk->parent->rate,
					   CURRENT_RATE);
	}

out:
	spin_unlock_irqrestore(&clockfw_lock, flags);
}
EXPORT_SYMBOL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
	unsigned long flags;
	unsigned long ret = 0;

	if (clk == NULL || IS_ERR(clk))
		return 0;

	spin_lock_irqsave(&clockfw_lock, flags);
	ret = clk->rate;
	spin_unlock_irqrestore(&clockfw_lock, flags);

	return ret;
}
EXPORT_SYMBOL(clk_get_rate);

void clk_put(struct clk *clk)
{
}
EXPORT_SYMBOL(clk_put);

/*-------------------------------------------------------------------------
 * Optional clock functions defined in include/linux/clk.h
 *-------------------------------------------------------------------------*/

long clk_round_rate(struct clk *clk, unsigned long rate)
{
	unsigned long flags;
	long ret = 0;

	if (clk == NULL || IS_ERR(clk))
		return ret;

	spin_lock_irqsave(&clockfw_lock, flags);
	if (arch_clock->clk_round_rate)
		ret = arch_clock->clk_round_rate(clk, rate);
	spin_unlock_irqrestore(&clockfw_lock, flags);

	return ret;
}
EXPORT_SYMBOL(clk_round_rate);

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned long flags;
	int ret = -EINVAL;
	int msg;

	if (clk == NULL || IS_ERR(clk))
		return ret;

	mutex_lock(&clocks_mutex);

	if (clk->notifier_count) {
		clk->temp_rate = rate;
		propagate_rate(clk, TEMP_RATE);

		omap_clk_notify_downstream(clk, CLK_PRE_RATE_CHANGE, 0);
	}

	spin_lock_irqsave(&clockfw_lock, flags);

	if (arch_clock->clk_set_rate) {
		ret = arch_clock->clk_set_rate(clk, rate);
		if (ret == 0)
			_do_propagate_rate(clk, clk->parent->rate,
					   CURRENT_RATE);
	}

	spin_unlock_irqrestore(&clockfw_lock, flags);

	msg = (ret) ? CLK_ABORT_RATE_CHANGE : CLK_POST_RATE_CHANGE;

	omap_clk_notify_downstream(clk, msg, 0);

	mutex_unlock(&clocks_mutex);

	return ret;
}
EXPORT_SYMBOL(clk_set_rate);

int clk_set_parent(struct clk *clk, struct clk *parent)
{
	unsigned long flags;
	struct clk *prev_parent;
	int ret = -EINVAL;
	int msg;

	if (clk == NULL || IS_ERR(clk) || parent == NULL || IS_ERR(parent))
		return ret;

	mutex_lock(&clocks_mutex);

	ret = _clk_pre_notify_set_parent(clk, parent);
	if (IS_ERR_VALUE(ret))
		goto csp_out;

	spin_lock_irqsave(&clockfw_lock, flags);

	if (arch_clock->clk_set_parent) {
		prev_parent = clk->parent;
		ret = arch_clock->clk_set_parent(clk, parent);
		if (ret == 0) {
			omap_clk_del_child(prev_parent, clk);
			omap_clk_add_child(parent, clk);
			_do_propagate_rate(clk, clk->parent->rate,
					   CURRENT_RATE);
		}
	}

	spin_unlock_irqrestore(&clockfw_lock, flags);

	msg = (ret) ? CLK_ABORT_RATE_CHANGE : CLK_POST_RATE_CHANGE;

	omap_clk_notify_downstream(clk, msg, 0);

csp_out:
	mutex_unlock(&clocks_mutex);

	return ret;
}
EXPORT_SYMBOL(clk_set_parent);

struct clk *clk_get_parent(struct clk *clk)
{
	unsigned long flags;
	struct clk * ret = NULL;

	if (clk == NULL || IS_ERR(clk))
		return ret;

	spin_lock_irqsave(&clockfw_lock, flags);
	if (arch_clock->clk_get_parent)
		ret = arch_clock->clk_get_parent(clk);
	spin_unlock_irqrestore(&clockfw_lock, flags);

	return ret;
}
EXPORT_SYMBOL(clk_get_parent);

/*-------------------------------------------------------------------------
 * OMAP specific clock functions shared between omap1 and omap2
 *-------------------------------------------------------------------------*/

unsigned int __initdata mpurate;

/*
 * By default we use the rate set by the bootloader.
 * You can override this with mpurate= cmdline option.
 */
static int __init omap_clk_setup(char *str)
{
	get_option(&str, &mpurate);

	if (!mpurate)
		return 1;

	if (mpurate < 1000)
		mpurate *= 1000000;

	return 1;
}
__setup("mpurate=", omap_clk_setup);

/* Used for clocks that always have same value as the parent clock */
void followparent_recalc(struct clk *clk, unsigned long new_parent_rate,
			 u8 rate_storage)
{
	if (rate_storage == CURRENT_RATE)
		clk->rate = new_parent_rate;
	else if (rate_storage == TEMP_RATE)
		clk->temp_rate = new_parent_rate;
}

/* Propagate rate to children */
void propagate_rate(struct clk *tclk, u8 rate_storage)
{
	unsigned long parent_rate = 0;

	if (tclk == NULL || IS_ERR(tclk))
		return;

	if (rate_storage == CURRENT_RATE)
		parent_rate = tclk->rate;
	else if (rate_storage == TEMP_RATE)
		parent_rate = tclk->temp_rate;

	omap_clk_for_each_child(tclk, parent_rate, rate_storage,
				_do_propagate_rate);
}

/**
 * recalculate_root_clocks - recalculate and propagate all root clocks
 *
 * Recalculates all root clocks (clocks with no parent), which if the
 * clock's .recalc is set correctly, should also propagate their rates.
 * Called at init.
 */
void recalculate_root_clocks(void)
{
	struct clk *clkp;

	list_for_each_entry(clkp, &clocks, node)
		if (unlikely(!clkp->parent))
			_do_propagate_rate(clkp, 0, CURRENT_RATE);
}

int clk_register(struct clk *clk)
{
	int ret;

	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

	mutex_lock(&clocks_mutex);
	if (arch_clock->clk_register) {
		ret = arch_clock->clk_register(clk);
		if (ret)
			goto cr_out;
	}
	list_add(&clk->node, &clocks);
	if (!clk->children.next)
		INIT_LIST_HEAD(&clk->children);
	if (clk->parent)
		omap_clk_add_child(clk->parent, clk);
	if (clk->init)
		clk->init(clk);
	ret = 0;
cr_out:
	mutex_unlock(&clocks_mutex);

	return ret;
}
EXPORT_SYMBOL(clk_register);

void clk_unregister(struct clk *clk)
{
	struct clk_child *child, *tmp;

	if (clk == NULL || IS_ERR(clk))
		return;

	mutex_lock(&clocks_mutex);
	list_del(&clk->node);
	if (clk->parent)
		omap_clk_del_child(clk->parent, clk);
	list_for_each_entry_safe(child, tmp, &clk->children, node)
		if (child->flags & CLK_CHILD_SLAB_ALLOC)
			kfree(child);
	mutex_unlock(&clocks_mutex);
}
EXPORT_SYMBOL(clk_unregister);

void clk_deny_idle(struct clk *clk)
{
	unsigned long flags;

	if (clk == NULL || IS_ERR(clk))
		return;

	spin_lock_irqsave(&clockfw_lock, flags);
	if (arch_clock->clk_deny_idle)
		arch_clock->clk_deny_idle(clk);
	spin_unlock_irqrestore(&clockfw_lock, flags);
}
EXPORT_SYMBOL(clk_deny_idle);

void clk_allow_idle(struct clk *clk)
{
	unsigned long flags;

	if (clk == NULL || IS_ERR(clk))
		return;

	spin_lock_irqsave(&clockfw_lock, flags);
	if (arch_clock->clk_allow_idle)
		arch_clock->clk_allow_idle(clk);
	spin_unlock_irqrestore(&clockfw_lock, flags);
}
EXPORT_SYMBOL(clk_allow_idle);

void clk_enable_init_clocks(void)
{
	struct clk *clkp;

	list_for_each_entry(clkp, &clocks, node) {
		if (clkp->flags & ENABLE_ON_INIT)
			clk_enable(clkp);
	}
}
EXPORT_SYMBOL(clk_enable_init_clocks);

#ifdef CONFIG_CPU_FREQ
void clk_init_cpufreq_table(struct cpufreq_frequency_table **table)
{
	unsigned long flags;

	spin_lock_irqsave(&clockfw_lock, flags);
	if (arch_clock->clk_init_cpufreq_table)
		arch_clock->clk_init_cpufreq_table(table);
	spin_unlock_irqrestore(&clockfw_lock, flags);
}
EXPORT_SYMBOL(clk_init_cpufreq_table);
#endif

/* Clk notifier implementation */

/**
 * clk_notifier_register - add a clock parameter change notifier
 * @clk: struct clk * to watch
 * @nb: struct notifier_block * with callback info
 *
 * Request notification for changes to the clock 'clk'.  This uses a
 * blocking notifier.  Callback code must not call into the clock
 * framework, as clocks_mutex is held.  Pre-notifier callbacks will be
 * passed the previous and new rate of the clock.
 *
 * clk_notifier_register() must be called from process
 * context.  Returns -EINVAL if called with null arguments, -ENOMEM
 * upon allocation failure; otherwise, passes along the return value
 * of blocking_notifier_chain_register().
 */
int clk_notifier_register(struct clk *clk, struct notifier_block *nb)
{
	struct clk_notifier *cn = NULL, *cn_new = NULL;
	int r;
	struct clk *clkp;

	if (!clk || !nb)
		return -EINVAL;

	mutex_lock(&clocks_mutex);

	list_for_each_entry(cn, &clk_notifier_list, node)
		if (cn->clk == clk)
			break;

	if (cn->clk != clk) {
		cn_new = kzalloc(sizeof(struct clk_notifier), GFP_KERNEL);
		if (!cn_new) {
			r = -ENOMEM;
			goto cnr_out;
		};

		cn_new->clk = clk;
		BLOCKING_INIT_NOTIFIER_HEAD(&cn_new->notifier_head);

		list_add(&cn_new->node, &clk_notifier_list);
		cn = cn_new;
	}

	r = blocking_notifier_chain_register(&cn->notifier_head, nb);
	if (!IS_ERR_VALUE(r)) {
		clkp = clk;
		do {
			clkp->notifier_count++;
		} while ((clkp = clkp->parent));
	} else {
		if (cn_new)
			_clk_free_notifier_chain(cn);
	}

cnr_out:
	mutex_unlock(&clocks_mutex);

	return r;
}
EXPORT_SYMBOL(clk_notifier_register);

/**
 * clk_notifier_unregister - remove a clock change notifier
 * @clk: struct clk *
 * @nb: struct notifier_block * with callback info
 *
 * Request no further notification for changes to clock 'clk'.
 * Returns -EINVAL if called with null arguments; otherwise, passes
 * along the return value of blocking_notifier_chain_unregister().
 */
int clk_notifier_unregister(struct clk *clk, struct notifier_block *nb)
{
	struct clk_notifier *cn = NULL;
	struct clk *clkp;
	int r = -EINVAL;

	if (!clk || !nb)
		return -EINVAL;

	mutex_lock(&clocks_mutex);

	list_for_each_entry(cn, &clk_notifier_list, node)
		if (cn->clk == clk)
			break;

	if (cn->clk != clk) {
		r = -ENOENT;
		goto cnu_out;
	};

	r = blocking_notifier_chain_unregister(&cn->notifier_head, nb);
	if (!IS_ERR_VALUE(r)) {
		clkp = clk;
		do {
			clkp->notifier_count--;
		} while ((clkp = clkp->parent));
	}

	/*
	 * XXX ugh, layering violation.  There should be some
	 * support in the notifier code for this.
	 */
	if (!cn->notifier_head.head)
		_clk_free_notifier_chain(cn);

cnu_out:
	mutex_unlock(&clocks_mutex);

	return r;
}
EXPORT_SYMBOL(clk_notifier_unregister);


/*-------------------------------------------------------------------------*/

#ifdef CONFIG_OMAP_RESET_CLOCKS
/*
 * Disable any unused clocks left on by the bootloader
 */
static int __init clk_disable_unused(void)
{
	struct clk *ck;
	unsigned long flags;

	list_for_each_entry(ck, &clocks, node) {
		if (ck->usecount > 0 ||
		    (ck->flags & (ALWAYS_ENABLED | PARENT_CONTROLS_CLOCK)))
			continue;

		if (cpu_class_is_omap1() && ck->enable_reg == 0)
			continue;

		spin_lock_irqsave(&clockfw_lock, flags);
		if (arch_clock->clk_disable_unused)
			arch_clock->clk_disable_unused(ck);
		spin_unlock_irqrestore(&clockfw_lock, flags);
	}

	return 0;
}
late_initcall(clk_disable_unused);
#endif

int __init clk_init(struct clk_functions * custom_clocks)
{
	if (!custom_clocks) {
		printk(KERN_ERR "No custom clock functions registered\n");
		BUG();
	}

	arch_clock = custom_clocks;

	return 0;
}

#if defined(CONFIG_PM_DEBUG) && defined(CONFIG_DEBUG_FS)
/*
 *	debugfs support to trace clock tree hierarchy and attributes
 */
static struct dentry *clk_debugfs_root;

static int clk_debugfs_register_one(struct clk *c)
{
	int err;
	struct dentry *d, *child;
	struct clk *pa = c->parent;
	char s[255];
	char *p = s;

	p += sprintf(p, "%s", c->name);
	if (c->id != 0)
		sprintf(p, ":%d", c->id);
	d = debugfs_create_dir(s, pa ? pa->dent : clk_debugfs_root);
	if (!d)
		return -ENOMEM;
	c->dent = d;

	d = debugfs_create_u8("usecount", S_IRUGO, c->dent, (u8 *)&c->usecount);
	if (!d) {
		err = -ENOMEM;
		goto err_out;
	}
	d = debugfs_create_u32("rate", S_IRUGO, c->dent, (u32 *)&c->rate);
	if (!d) {
		err = -ENOMEM;
		goto err_out;
	}
	d = debugfs_create_x32("flags", S_IRUGO, c->dent, (u32 *)&c->flags);
	if (!d) {
		err = -ENOMEM;
		goto err_out;
	}
	return 0;

err_out:
	d = c->dent;
	list_for_each_entry(child, &d->d_subdirs, d_u.d_child)
		debugfs_remove(child);
	debugfs_remove(c->dent);
	return err;
}

static int clk_debugfs_register(struct clk *c)
{
	int err;
	struct clk *pa = c->parent;

	if (pa && !pa->dent) {
		err = clk_debugfs_register(pa);
		if (err)
			return err;
	}

	if (!c->dent) {
		err = clk_debugfs_register_one(c);
		if (err)
			return err;
	}
	return 0;
}

static int __init clk_debugfs_init(void)
{
	struct clk *c;
	struct dentry *d;
	int err;

	d = debugfs_create_dir("clock", NULL);
	if (!d)
		return -ENOMEM;
	clk_debugfs_root = d;

	list_for_each_entry(c, &clocks, node) {
		err = clk_debugfs_register(c);
		if (err)
			goto err_out;
	}
	return 0;
err_out:
	debugfs_remove(clk_debugfs_root); /* REVISIT: Cleanup correctly */
	return err;
}
late_initcall(clk_debugfs_init);

#endif /* defined(CONFIG_PM_DEBUG) && defined(CONFIG_DEBUG_FS) */
