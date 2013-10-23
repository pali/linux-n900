/*
 *	"condition" match extension for Xtables
 *
 *	Description: This module allows firewall rules to match using
 *	condition variables available through procfs.  It also allows
 *	target rules to set the condition variable.
 *
 *	Authors:
 *	Stephane Ouellette <ouellettes [at] videotron ca>, 2002-10-22
 *	Massimiliano Hofer <max [at] nucleus it>, 2006-05-15
 *	Luciano Coelho <luciano.coelho@nokia.com>, 2010-08-11
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License; either version 2
 *	or 3 of the License, as published by the Free Software Foundation.
 *
 *	Portion Copyright 2010 Nokia Corporation and/or its subsidiary(-ies).
 *	File modified on 2010-08-11.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/version.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_condition.h>
#include <net/netns/generic.h>
#include <linux/uaccess.h>
#include <linux/capability.h>

/* Defaults, these can be overridden on the module command-line. */
static unsigned int condition_list_perms = S_IRUGO | S_IWUSR;
static unsigned int condition_uid_perms;
static unsigned int condition_gid_perms;

MODULE_AUTHOR("Stephane Ouellette <ouellettes@videotron.ca>");
MODULE_AUTHOR("Massimiliano Hofer <max@nucleus.it>");
MODULE_AUTHOR("Jan Engelhardt <jengelh@medozas.de>");
MODULE_AUTHOR("Luciano Coelho <luciano.coelho@nokia.com>");
MODULE_DESCRIPTION("Allows rules to set and match condition variables");
MODULE_LICENSE("GPL");
module_param(condition_list_perms, uint, S_IRUSR | S_IWUSR);
MODULE_PARM_DESC(condition_list_perms, "default permissions on /proc/net/nf_condition/* files");
module_param(condition_uid_perms, uint, S_IRUSR | S_IWUSR);
MODULE_PARM_DESC(condition_uid_perms, "default user owner of /proc/net/nf_condition/* files");
module_param(condition_gid_perms, uint, S_IRUSR | S_IWUSR);
MODULE_PARM_DESC(condition_gid_perms, "default group owner of /proc/net/nf_condition/* files");
MODULE_ALIAS("ipt_condition");
MODULE_ALIAS("ip6t_condition");

struct condition_variable {
	struct list_head list;
	struct proc_dir_entry *status_proc;
	unsigned int refcount;
	u32 value;
};

struct condition_net {
	struct list_head list;
	struct proc_dir_entry *proc_dir;
};

static int condition_net_id;
static inline struct condition_net *condition_pernet(struct net *net)
{
	return net_generic(net, condition_net_id);
}

/* proc_lock is a user context only semaphore used for write access */
/*           to the conditions' list.                               */
static DEFINE_MUTEX(proc_lock);

static int condition_proc_read(char *buffer, char **start, off_t offset,
			       int length, int *eof, void *data)
{
	const struct condition_variable *var = data;

	return snprintf(buffer, length, "%u\n", var->value);
}

static int condition_proc_write(struct file *file, const char __user *input,
				unsigned long length, void *data)
{
	struct condition_variable *var = data;
	/* the longest possible string is MAX_UINT in octal */
	char buf[sizeof("+037777777777")];
	unsigned long long value;

	if (!capable(CAP_NET_ADMIN)) {
		pr_debug("task doesn't have CAP_NET_ADMIN capability\n");
		return -EPERM;
	}

	if (length == 0)
		return 0;

	if (length > sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, input, length) != 0)
		return -EFAULT;

	buf[length - 1] = '\0';

	if (strict_strtoull(buf, 0, &value) != 0)
		return -EINVAL;

	if (value > (u32) value)
		return -ERANGE;

	var->value = value;

	return length;
}

static struct
condition_variable *xt_condition_insert(const char *name,
					struct condition_net *cond_net)
{
	struct condition_variable *var;

	/*
	 * Let's acquire the lock, check for the condition and add it
	 * or increase the reference counter.
	 */
	mutex_lock(&proc_lock);
	list_for_each_entry(var, &cond_net->list, list) {
		if (strcmp(name, var->status_proc->name) == 0) {
			++var->refcount;
			goto out;
		}
	}

	/* At this point, we need to allocate a new condition variable. */
	var = kmalloc(sizeof(struct condition_variable), GFP_KERNEL);
	if (var == NULL)
		goto out;

	/* Create the condition variable's proc file entry. */
	var->status_proc = create_proc_entry(name, condition_list_perms,
					     cond_net->proc_dir);
	if (var->status_proc == NULL) {
		kfree(var);
		var = NULL;
		goto out;
	}

	var->refcount = 1;
	var->value = 0;
	var->status_proc->data       = var;
	var->status_proc->read_proc  = condition_proc_read;
	var->status_proc->write_proc = condition_proc_write;
	var->status_proc->uid        = condition_uid_perms;
	var->status_proc->gid        = condition_gid_perms;
	list_add(&var->list, &cond_net->list);
out:
	mutex_unlock(&proc_lock);
	return var;
}

static void xt_condition_put(struct condition_variable *var,
			     struct condition_net *cond_net)
{
	mutex_lock(&proc_lock);
	if (--var->refcount == 0) {
		list_del(&var->list);
		/* status_proc may be null in case of ns exit */
		if (var->status_proc)
			remove_proc_entry(var->status_proc->name,
					  cond_net->proc_dir);
		mutex_unlock(&proc_lock);
		kfree(var);
		return;
	}
	mutex_unlock(&proc_lock);
}

static bool
condition_mt(const struct sk_buff *skb, const struct xt_match_param *par)
{
	const struct xt_condition_mtinfo *info = par->matchinfo;
	const struct condition_variable *var   = info->condvar;

	return (var->value == info->value) ^ info->invert;
}

static bool condition_mt_check(const struct xt_mtchk_param *par)
{
	struct xt_condition_mtinfo *info = par->matchinfo;
	struct condition_variable *var;
	struct condition_net *cond_net = condition_pernet(&init_net);

	/* Forbid certain names */
	if (*info->name == '\0' || *info->name == '.' ||
	    info->name[sizeof(info->name)-1] != '\0' ||
	    memchr(info->name, '/', sizeof(info->name)) != NULL) {
		pr_info("name not allowed or too long: \"%.*s\"\n",
			(unsigned int)sizeof(info->name), info->name);
		return false;
	}

	var = xt_condition_insert(info->name, cond_net);
	if (var == NULL)
		return false;

	info->condvar = var;
	return true;
}

static void condition_mt_destroy(const struct xt_mtdtor_param *par)
{
	const struct xt_condition_mtinfo *info = par->matchinfo;
	struct condition_net *cond_net = condition_pernet(&init_net);

	xt_condition_put(info->condvar, cond_net);
}

static unsigned int condition_tg_target(struct sk_buff *skb,
					const struct xt_target_param *par)
{
	const struct condition_tginfo *info = par->targinfo;
	struct condition_variable *var = info->condvar;

	pr_debug("setting condition %s, value %d\n",
		 info->name, info->value);

	var->value = info->value;

	return XT_CONTINUE;
}

static bool condition_tg_checkentry(const struct xt_tgchk_param *par)
{
	struct condition_tginfo *info = par->targinfo;
	struct condition_variable *var;
	struct condition_net *cond_net = condition_pernet(&init_net);

	pr_debug("checkentry %s\n", info->name);

	/* Forbid certain names */
	if (*info->name == '\0' || *info->name == '.' ||
	    info->name[sizeof(info->name)-1] != '\0' ||
	    memchr(info->name, '/', sizeof(info->name)) != NULL) {
		pr_info("name not allowed or too long: \"%.*s\"\n",
			(unsigned int)sizeof(info->name), info->name);
		return false;
	}

	var = xt_condition_insert(info->name, cond_net);
	if (var == NULL)
		return false;

	info->condvar = var;
	return true;
}

static void condition_tg_destroy(const struct xt_tgdtor_param *par)
{
	const struct condition_tginfo *info = par->targinfo;
	struct condition_net *cond_net = condition_pernet(&init_net);

	pr_debug("destroy %s\n", info->name);

	xt_condition_put(info->condvar, cond_net);
}

static struct xt_target condition_tg_reg __read_mostly = {
	.name           = "CONDITION",
	.revision       = 0,
	.family         = NFPROTO_UNSPEC,
	.target         = condition_tg_target,
	.targetsize     = sizeof(struct condition_tginfo),
	.checkentry     = condition_tg_checkentry,
	.destroy        = condition_tg_destroy,
	.me             = THIS_MODULE,
};

static struct xt_match condition_mt_reg __read_mostly = {
	.name       = "condition",
	.revision   = 2,
	.family     = NFPROTO_UNSPEC,
	.matchsize  = sizeof(struct xt_condition_mtinfo),
	.match      = condition_mt,
	.checkentry = condition_mt_check,
	.destroy    = condition_mt_destroy,
	.me         = THIS_MODULE,
};

static const char *const dir_name = "nf_condition";

static int __net_init condnet_init(struct net *net)
{
	struct condition_net *cond_net;
	int err;

	cond_net = kzalloc(sizeof(*cond_net), GFP_KERNEL);
	if (cond_net == NULL)
		return -ENOMEM;

	INIT_LIST_HEAD(&cond_net->list);

	cond_net->proc_dir = proc_mkdir(dir_name, net->proc_net);
	if (cond_net->proc_dir == NULL)
		return -EACCES;

	err = net_assign_generic(net, condition_net_id, cond_net);
	if (err < 0)
		return err;

	return 0;
}

static void __net_exit condnet_exit(struct net *net)
{
	struct condition_net *cond_net = condition_pernet(net);
	struct condition_variable *var, *tmp;

	mutex_lock(&proc_lock);
	list_for_each_entry_safe(var, tmp, &cond_net->list, list) {
		remove_proc_entry(var->status_proc->name,
				  cond_net->proc_dir);
		/* set to null so we don't double remove in mt_destroy */
		var->status_proc = NULL;
	}

	mutex_unlock(&proc_lock);

	remove_proc_entry(dir_name, net->proc_net);

	kfree(cond_net);

	net_assign_generic(net, condition_net_id, NULL);
}

static struct pernet_operations condition_mt_netops = {
	.init = condnet_init,
	.exit = condnet_exit,
};

static int __init condition_init(void)
{
	int ret;

	mutex_init(&proc_lock);
	ret = xt_register_match(&condition_mt_reg);
	if (ret < 0)
		return ret;

	ret =  xt_register_target(&condition_tg_reg);
	if (ret < 0) {
		xt_unregister_match(&condition_mt_reg);
		return ret;
	}

	ret = register_pernet_gen_subsys(&condition_net_id,
					 &condition_mt_netops);
	if (ret < 0) {
		xt_unregister_target(&condition_tg_reg);
		xt_unregister_match(&condition_mt_reg);
		return ret;
	}

	return 0;
}

static void __exit condition_exit(void)
{
	unregister_pernet_gen_subsys(condition_net_id,
				     &condition_mt_netops);
	xt_unregister_match(&condition_mt_reg);
	xt_unregister_target(&condition_tg_reg);
}

module_init(condition_init);
module_exit(condition_exit);
