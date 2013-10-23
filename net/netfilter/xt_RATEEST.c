/*
 * (C) 2007 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/gen_stats.h>
#include <linux/jhash.h>
#include <linux/rtnetlink.h>
#include <linux/random.h>
#include <net/gen_stats.h>
#include <net/netlink.h>

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_RATEEST.h>
#include <net/netfilter/xt_rateest.h>

static DEFINE_MUTEX(xt_rateest_mutex);

#define RATEEST_HSIZE	16
static struct hlist_head rateest_hash[RATEEST_HSIZE] __read_mostly;
static unsigned int jhash_rnd __read_mostly;

static unsigned int xt_rateest_hash(const char *name)
{
	return jhash(name, FIELD_SIZEOF(struct xt_rateest, name), jhash_rnd) &
	       (RATEEST_HSIZE - 1);
}

static void xt_rateest_hash_insert(struct xt_rateest *est)
{
	unsigned int h;

	h = xt_rateest_hash(est->name);
	hlist_add_head(&est->list, &rateest_hash[h]);
}

struct xt_rateest *xt_rateest_lookup(const char *name)
{
	struct xt_rateest *est;
	struct hlist_node *n;
	unsigned int h;

	h = xt_rateest_hash(name);
	mutex_lock(&xt_rateest_mutex);
	hlist_for_each_entry(est, n, &rateest_hash[h], list) {
		if (strcmp(est->name, name) == 0) {
			est->refcnt++;
			mutex_unlock(&xt_rateest_mutex);
			return est;
		}
	}
	mutex_unlock(&xt_rateest_mutex);
	return NULL;
}
EXPORT_SYMBOL_GPL(xt_rateest_lookup);

void xt_rateest_put(struct xt_rateest *est)
{
	mutex_lock(&xt_rateest_mutex);
	if (--est->refcnt == 0) {
		hlist_del(&est->list);
		kfree(est);
	}
	mutex_unlock(&xt_rateest_mutex);
}
EXPORT_SYMBOL_GPL(xt_rateest_put);

static unsigned int
xt_rateest_tg(struct sk_buff *skb, const struct xt_target_param *par)
{
	const struct xt_rateest_target_info *info = par->targinfo;
	struct gnet_stats_basic_packed *stats = &info->est->bstats;
	struct gnet_stats_rate_est *rstats = &info->est->rstats;
	struct xt_rateest_stats *istats = &info->est->istats;
	unsigned long msecs;
	int oldest;

	spin_lock_bh(&info->est->lock);

	/* update statistics */
	stats->bytes += skb->len;
	stats->packets++;

	/* update reference data list */
	istats->idx = (istats->idx + 1) & XT_RATEEST_MOD_MASK;
	istats->totalsize -= istats->pkt_size[istats->idx];
	istats->totalsize += skb->len;

	istats->pkt_jiffies[istats->idx] = jiffies;
	istats->pkt_size[istats->idx] = skb->len;

	/* calculate current rates */
	oldest = (istats->idx - istats->count) & XT_RATEEST_MOD_MASK;
	if (unlikely(istats->count < XT_RATEEST_MAX_PACKETS - 1))
		istats->count++;

	spin_unlock_bh(&info->est->lock);

	msecs = jiffies_to_msecs(jiffies - istats->pkt_jiffies[oldest]);
	if (msecs) {
		rstats->bps = ((istats->totalsize << 8) / msecs) << 2;
		rstats->pps = (((istats->count + 1) << 8) / msecs) << 2;
	}

	return XT_CONTINUE;
}

static bool xt_rateest_tg_checkentry(const struct xt_tgchk_param *par)
{
	struct xt_rateest_target_info *info = par->targinfo;
	struct xt_rateest *est;

	est = xt_rateest_lookup(info->name);
	if (est) {
		info->est = est;
		return true;
	}

	est = kzalloc(sizeof(*est), GFP_KERNEL);
	if (!est)
		goto err1;

	strlcpy(est->name, info->name, sizeof(est->name));
	spin_lock_init(&est->lock);
	est->refcnt		= 1;

	info->est = est;
	xt_rateest_hash_insert(est);

	return true;

err1:
	return false;
}

static void xt_rateest_tg_destroy(const struct xt_tgdtor_param *par)
{
	struct xt_rateest_target_info *info = par->targinfo;

	xt_rateest_put(info->est);
}

static struct xt_target xt_rateest_tg_reg __read_mostly = {
	.name       = "RATEEST",
	.revision   = 0,
	.family     = NFPROTO_UNSPEC,
	.target     = xt_rateest_tg,
	.checkentry = xt_rateest_tg_checkentry,
	.destroy    = xt_rateest_tg_destroy,
	.targetsize = sizeof(struct xt_rateest_target_info),
	.me         = THIS_MODULE,
};

static int __init xt_rateest_tg_init(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(rateest_hash); i++)
		INIT_HLIST_HEAD(&rateest_hash[i]);

	get_random_bytes(&jhash_rnd, sizeof(jhash_rnd));
	return xt_register_target(&xt_rateest_tg_reg);
}

static void __exit xt_rateest_tg_fini(void)
{
	xt_unregister_target(&xt_rateest_tg_reg);
}


MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Xtables: packet rate estimator");
MODULE_ALIAS("ipt_RATEEST");
MODULE_ALIAS("ip6t_RATEEST");
module_init(xt_rateest_tg_init);
module_exit(xt_rateest_tg_fini);
