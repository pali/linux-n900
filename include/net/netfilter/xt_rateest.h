#ifndef _XT_RATEEST_H
#define _XT_RATEEST_H

#define XT_RATEEST_MAX_PACKETS 8
#define XT_RATEEST_MOD_MASK    0x7

struct xt_rateest_stats {
	unsigned long pkt_jiffies[XT_RATEEST_MAX_PACKETS];
	u32 pkt_size[XT_RATEEST_MAX_PACKETS];
	u32 totalsize;
	u8 idx;
	u8 count;
};

struct xt_rateest {
	struct hlist_node		list;
	char				name[IFNAMSIZ];
	unsigned int			refcnt;
	spinlock_t			lock;
	struct xt_rateest_stats		istats;
	struct gnet_stats_rate_est	rstats;
	struct gnet_stats_basic_packed	bstats;
};

extern struct xt_rateest *xt_rateest_lookup(const char *name);
extern void xt_rateest_put(struct xt_rateest *est);

#endif /* _XT_RATEEST_H */
