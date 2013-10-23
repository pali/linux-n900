/*
 *  arch/arm/plat-omap/include/mach/clock.h
 *
 *  Copyright (C) 2004 - 2005 Nokia corporation
 *  Written by Tuukka Tikkanen <tuukka.tikkanen@elektrobit.com>
 *  Based on clocks.h by Tony Lindgren, Gordon McNutt and RidgeRun, Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARCH_ARM_OMAP_CLOCK_H
#define __ARCH_ARM_OMAP_CLOCK_H

#include <linux/notifier.h>

#include <linux/list.h>

struct module;
struct clk;
struct clockdomain;

struct clkops {
	int			(*enable)(struct clk *);
	void			(*disable)(struct clk *);
	void			(*find_idlest)(struct clk *, void __iomem **, u8 *);
	void			(*find_companion)(struct clk *, void __iomem **, u8 *);
};

#if defined(CONFIG_ARCH_OMAP2) || defined(CONFIG_ARCH_OMAP3) || \
		defined(CONFIG_ARCH_OMAP4)

struct clksel_rate {
	u32			val;
	u8			div;
	u8			flags;
};

struct clksel {
	struct clk		 *parent;
	const struct clksel_rate *rates;
};

/* A new flag called flag has been added which indiciates what is the type
  * of dpll (like j_type, no_dco_sel)
  */

struct dpll_data {
	void __iomem		*mult_div1_reg;
	u32			mult_mask;
	u32			div1_mask;
	struct clk		*clk_bypass;
	struct clk		*clk_ref;
	void __iomem		*control_reg;
	u32			enable_mask;
	unsigned int		rate_tolerance;
	unsigned long		last_rounded_rate;
	u16			last_rounded_m;
	u8			last_rounded_n;
	u8			min_divider;
	u8			max_divider;
	u32			max_tolerance;
	u16			max_multiplier;
#if defined(CONFIG_ARCH_OMAP3) || defined(CONFIG_ARCH_OMAP4)
	u8			modes;
	void __iomem		*autoidle_reg;
	void __iomem		*idlest_reg;
	u32			autoidle_mask;
	u32			freqsel_mask;
	u8			flags;
	u32			idlest_mask;
	u8			auto_recal_bit;
	u8			recal_en_bit;
	u8			recal_st_bit;
#  endif
};

#endif

/*
 * struct clk_notifier - associate a clk with a notifier
 * @clk: struct clk * to associate the notifier with
 * @notifier_head: a blocking_notifier_head for this clk
 * @node: linked list pointers
 *
 * A list of struct clk_notifier is maintained by the notifier code.
 * An entry is created whenever code registers the first notifier on a
 * particular @clk.  Future notifiers on that @clk are added to the
 * @notifier_head.
 */
struct clk_notifier {
	struct clk			*clk;
	struct blocking_notifier_head	notifier_head;
	struct list_head		node;
};

/*
 * struct clk_notifier_data - rate data to pass to the notifier callback
 * @clk: struct clk * being changed
 * @rate: current rate of this clock
 *
 * This struct is passed as parameter to the clock notifier callbacks when
 * a clock is changed. Current rate of the clock is passed along with the
 * call in pre-notifier, and the new rate in post-notifier.
 */
struct clk_notifier_data {
	struct clk			*clk;
	unsigned long			rate;
};

struct clk {
	struct list_head	node;
	const struct clkops	*ops;
	const char		*name;
	int			id;
	struct clk		*parent;
	struct list_head	children;
	struct list_head	sibling;	/* node for children */
	unsigned long		rate;
	__u32			flags;
	void __iomem		*enable_reg;
	unsigned long		(*recalc)(struct clk *);
	int			(*set_rate)(struct clk *, unsigned long);
	long			(*round_rate)(struct clk *, unsigned long);
	void			(*init)(struct clk *);
	u16			notifier_count;
	__u8			enable_bit;
	__s8			usecount;
#if defined(CONFIG_ARCH_OMAP2) || defined(CONFIG_ARCH_OMAP3) || \
		defined(CONFIG_ARCH_OMAP4)
	u8			fixed_div;
	u8			clksel_shift;
	void __iomem		*clksel_reg;
	u32			clksel_mask;
	const struct clksel	*clksel;
	struct dpll_data	*dpll_data;
	const char		*clkdm_name;
	struct clockdomain	*clkdm;
#else
	__u8			rate_offset;
	__u8			src_offset;
#endif
	void			*data;
#if defined(CONFIG_PM_DEBUG) && defined(CONFIG_DEBUG_FS)
	struct dentry		*dent;	/* For visible tree hierarchy */
#endif
};

struct cpufreq_frequency_table;

struct clk_functions {
	int		(*clk_enable)(struct clk *clk);
	void		(*clk_disable)(struct clk *clk);
	long		(*clk_round_rate)(struct clk *clk, unsigned long rate);
	int		(*clk_set_rate)(struct clk *clk, unsigned long rate);
	int		(*clk_set_parent)(struct clk *clk, struct clk *parent);
	void		(*clk_allow_idle)(struct clk *clk);
	void		(*clk_deny_idle)(struct clk *clk);
	void		(*clk_disable_unused)(struct clk *clk);
#ifdef CONFIG_CPU_FREQ
	void		(*clk_init_cpufreq_table)(struct cpufreq_frequency_table **);
	void		(*clk_exit_cpufreq_table)(struct cpufreq_frequency_table **);
#endif
};

extern unsigned int mpurate;

extern int clk_init(struct clk_functions *custom_clocks);
extern void clk_preinit(struct clk *clk);
extern int clk_register(struct clk *clk);
extern void clk_reparent(struct clk *child, struct clk *parent);
extern void clk_unregister(struct clk *clk);
extern void propagate_rate(struct clk *clk);
extern void recalculate_root_clocks(void);
extern unsigned long followparent_recalc(struct clk *clk);
extern void clk_enable_init_clocks(void);
extern int clk_notifier_register(struct clk *clk, struct notifier_block *nb);
extern int clk_notifier_unregister(struct clk *clk, struct notifier_block *nb);
#ifdef CONFIG_CPU_FREQ
extern void clk_init_cpufreq_table(struct cpufreq_frequency_table **table);
extern void clk_exit_cpufreq_table(struct cpufreq_frequency_table **table);
#endif
extern struct clk *omap_clk_get_by_name(const char *name);

extern const struct clkops clkops_null;

/* Clock flags */
/* bit 0 is free */
#define RATE_FIXED		(1 << 1)	/* Fixed clock rate */
/* bits 2-4 are free */
#define ENABLE_REG_32BIT	(1 << 5)	/* Use 32-bit access */
#define CLOCK_IDLE_CONTROL	(1 << 7)
#define CLOCK_NO_IDLE_PARENT	(1 << 8)
#define DELAYED_APP		(1 << 9)	/* Delay application of clock */
#define CONFIG_PARTICIPANT	(1 << 10)	/* Fundamental clock */
#define ENABLE_ON_INIT		(1 << 11)	/* Enable upon framework init */
#define INVERT_ENABLE           (1 << 12)       /* 0 enables, 1 disables */
#define CLOCK_IN_OMAP4430	(1 << 13)
#define ALWAYS_ENABLED		(1 << 14)
/* bits 13-31 are currently free */

/* Clksel_rate flags */
#define DEFAULT_RATE		(1 << 0)
#define RATE_IN_242X		(1 << 1)
#define RATE_IN_243X		(1 << 2)
#define RATE_IN_343X		(1 << 3)	/* rates common to all 343X */
#define RATE_IN_3430ES2	(1 << 4)	/* 3430ES2 rates only */
#define RATE_IN_36XX		(1 << 5)
#define RATE_IN_4430		(1 << 6)

#define RATE_IN_24XX		(RATE_IN_242X | RATE_IN_243X)

/*
 * Clk notifier callback types
 *
 * Since the notifier is called with interrupts disabled, any actions
 * taken by callbacks must be extremely fast and lightweight.
 *
 * CLK_PRE_RATE_CHANGE - called immediately before the clock rate is
 *     changed. Drivers must immediately terminate any operations that
 *     will be affected by the rate change.  Callbacks must always
 *     return NOTIFY_DONE.
 *
 * CLK_ABORT_RATE_CHANGE: called if the rate change failed for some
 *     reason after CLK_PRE_RATE_CHANGE.  In this case, all registered
 *     notifiers on the clock will be called with
 *     CLK_ABORT_RATE_CHANGE. Callbacks must always return
 *     NOTIFY_DONE.
 *
 * CLK_POST_RATE_CHANGE - called after the clock rate change has
 *     successfully completed.  Callbacks must always return
 *     NOTIFY_DONE.
 *
 */
#define CLK_PRE_RATE_CHANGE		1
#define CLK_ABORT_RATE_CHANGE		2
#define CLK_POST_RATE_CHANGE		3

#endif
