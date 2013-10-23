/*
 * OMAP Power Management debug routines
 *
 * Copyright (C) 2005 Texas Instruments, Inc.
 * Copyright (C) 2006-2008 Nokia Corporation
 *
 * Written by:
 * Richard Woodruff <r-woodruff2@ti.com>
 * Tony Lindgren
 * Juha Yrjola
 * Amit Kucheria <amit.kucheria@nokia.com>
 * Igor Stoppa <igor.stoppa@nokia.com>
 * Jouni Hogander
 *
 * Based on pm.c for omap2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/vmalloc.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/pm_qos_params.h>

#include <plat/clock.h>
#include <plat/board.h>
#include <plat/powerdomain.h>
#include <plat/clockdomain.h>
#include <plat/dma.h>
#include <plat/omap-pm.h>
#include <plat/resource.h>
#include <plat/mux.h>
#include <plat/control.h>

#include "prm.h"
#include "cm.h"
#include "pm.h"
#include "prm-regbits-34xx.h"
#include "smartreflex.h"
#include "pm-optimizer.h"

int omap2_pm_debug;

#define DUMP_PRM_MOD_REG(mod, reg)    \
	regs[reg_count].name = #mod "." #reg; \
	regs[reg_count++].val = prm_read_mod_reg(mod, reg)
#define DUMP_CM_MOD_REG(mod, reg)     \
	regs[reg_count].name = #mod "." #reg; \
	regs[reg_count++].val = cm_read_mod_reg(mod, reg)
#define DUMP_PRM_REG(reg) \
	regs[reg_count].name = #reg; \
	regs[reg_count++].val = __raw_readl(reg)
#define DUMP_CM_REG(reg) \
	regs[reg_count].name = #reg; \
	regs[reg_count++].val = __raw_readl(reg)
#define DUMP_INTC_REG(reg, off) \
	regs[reg_count].name = #reg; \
	regs[reg_count++].val = \
			 __raw_readl(OMAP2_L4_IO_ADDRESS(0x480fe000 + (off)))

void omap2_pm_dump(int mode, int resume, unsigned int us)
{
	struct reg {
		const char *name;
		u32 val;
	} regs[32];
	int reg_count = 0, i;
	const char *s1 = NULL, *s2 = NULL;

	if (!resume) {
#if 0
		/* MPU */
		DUMP_PRM_MOD_REG(OCP_MOD, OMAP2_PRM_IRQENABLE_MPU_OFFSET);
		DUMP_CM_MOD_REG(MPU_MOD, CM_CLKSTCTRL);
		DUMP_PRM_MOD_REG(MPU_MOD, PM_PWSTCTRL);
		DUMP_PRM_MOD_REG(MPU_MOD, PM_PWSTST);
		DUMP_PRM_MOD_REG(MPU_MOD, PM_WKDEP);
#endif
#if 0
		/* INTC */
		DUMP_INTC_REG(INTC_MIR0, 0x0084);
		DUMP_INTC_REG(INTC_MIR1, 0x00a4);
		DUMP_INTC_REG(INTC_MIR2, 0x00c4);
#endif
#if 0
		DUMP_CM_MOD_REG(CORE_MOD, CM_FCLKEN1);
		if (cpu_is_omap24xx()) {
			DUMP_CM_MOD_REG(CORE_MOD, OMAP24XX_CM_FCLKEN2);
			DUMP_PRM_MOD_REG(OMAP24XX_GR_MOD,
					OMAP2_PRCM_CLKEMUL_CTRL_OFFSET);
			DUMP_PRM_MOD_REG(OMAP24XX_GR_MOD,
					OMAP2_PRCM_CLKSRC_CTRL_OFFSET);
		}
		DUMP_CM_MOD_REG(WKUP_MOD, CM_FCLKEN);
		DUMP_CM_MOD_REG(CORE_MOD, CM_ICLKEN1);
		DUMP_CM_MOD_REG(CORE_MOD, CM_ICLKEN2);
		DUMP_CM_MOD_REG(WKUP_MOD, CM_ICLKEN);
		DUMP_CM_MOD_REG(PLL_MOD, CM_CLKEN);
		DUMP_CM_MOD_REG(PLL_MOD, CM_AUTOIDLE);
		DUMP_PRM_MOD_REG(CORE_MOD, PM_PWSTST);
#endif
#if 0
		/* DSP */
		if (cpu_is_omap24xx()) {
			DUMP_CM_MOD_REG(OMAP24XX_DSP_MOD, CM_FCLKEN);
			DUMP_CM_MOD_REG(OMAP24XX_DSP_MOD, CM_ICLKEN);
			DUMP_CM_MOD_REG(OMAP24XX_DSP_MOD, CM_IDLEST);
			DUMP_CM_MOD_REG(OMAP24XX_DSP_MOD, CM_AUTOIDLE);
			DUMP_CM_MOD_REG(OMAP24XX_DSP_MOD, CM_CLKSEL);
			DUMP_CM_MOD_REG(OMAP24XX_DSP_MOD, CM_CLKSTCTRL);
			DUMP_PRM_MOD_REG(OMAP24XX_DSP_MOD, RM_RSTCTRL);
			DUMP_PRM_MOD_REG(OMAP24XX_DSP_MOD, RM_RSTST);
			DUMP_PRM_MOD_REG(OMAP24XX_DSP_MOD, PM_PWSTCTRL);
			DUMP_PRM_MOD_REG(OMAP24XX_DSP_MOD, PM_PWSTST);
		}
#endif
	} else {
		DUMP_PRM_MOD_REG(CORE_MOD, PM_WKST1);
		if (cpu_is_omap24xx())
			DUMP_PRM_MOD_REG(CORE_MOD, OMAP24XX_PM_WKST2);
		DUMP_PRM_MOD_REG(WKUP_MOD, PM_WKST);
		DUMP_PRM_MOD_REG(OCP_MOD, OMAP2_PRCM_IRQSTATUS_MPU_OFFSET);
#if 1
		DUMP_INTC_REG(INTC_PENDING_IRQ0, 0x0098);
		DUMP_INTC_REG(INTC_PENDING_IRQ1, 0x00b8);
		DUMP_INTC_REG(INTC_PENDING_IRQ2, 0x00d8);
#endif
	}

	switch (mode) {
	case 0:
		s1 = "full";
		s2 = "retention";
		break;
	case 1:
		s1 = "MPU";
		s2 = "retention";
		break;
	case 2:
		s1 = "MPU";
		s2 = "idle";
		break;
	}

	if (!resume)
#ifdef CONFIG_NO_HZ
		printk(KERN_INFO
		       "--- Going to %s %s (next timer after %u ms)\n", s1, s2,
		       jiffies_to_msecs(get_next_timer_interrupt(jiffies) -
					jiffies));
#else
		printk(KERN_INFO "--- Going to %s %s\n", s1, s2);
#endif
	else
		printk(KERN_INFO "--- Woke up (slept for %u.%03u ms)\n",
			us / 1000, us % 1000);

	for (i = 0; i < reg_count; i++)
		printk(KERN_INFO "%-20s: 0x%08x\n", regs[i].name, regs[i].val);
}

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#include <linux/seq_file.h>

static void pm_dbg_regset_store(u32 *ptr);

struct dentry *pm_dbg_dir, *pm_dbg_main_dir;

static int pm_dbg_init_done;

static int __init pm_dbg_init(void);

enum {
	DEBUG_FILE_COUNTERS = 0,
	DEBUG_FILE_TIMERS,
	DEBUG_FILE_WAIT_SDRC_COUNT,
	DEBUG_FILE_SECURE_RAM_STATE,
	DEBUG_FILE_PM_OPT,
	DEBUG_FILE_RESOURCES,
	DEBUG_FILE_PM_QOS,
};

struct pm_module_def {
	char name[8]; /* Name of the module */
	short type; /* CM or PRM */
	unsigned short offset;
	int low; /* First register address on this module */
	int high; /* Last register address on this module */
};

#define MOD_CM 0
#define MOD_PRM 1

static const struct pm_module_def *pm_dbg_reg_modules;
static const struct pm_module_def omap3_pm_reg_modules[] = {
	{ "IVA2", MOD_CM, OMAP3430_IVA2_MOD, 0, 0x4c },
	{ "OCP", MOD_CM, OCP_MOD, 0, 0x10 },
	{ "MPU", MOD_CM, MPU_MOD, 4, 0x4c },
	{ "CORE", MOD_CM, CORE_MOD, 0, 0x4c },
	{ "SGX", MOD_CM, OMAP3430ES2_SGX_MOD, 0, 0x4c },
	{ "WKUP", MOD_CM, WKUP_MOD, 0, 0x40 },
	{ "CCR", MOD_CM, PLL_MOD, 0, 0x70 },
	{ "DSS", MOD_CM, OMAP3430_DSS_MOD, 0, 0x4c },
	{ "CAM", MOD_CM, OMAP3430_CAM_MOD, 0, 0x4c },
	{ "PER", MOD_CM, OMAP3430_PER_MOD, 0, 0x4c },
	{ "EMU", MOD_CM, OMAP3430_EMU_MOD, 0x40, 0x54 },
	{ "NEON", MOD_CM, OMAP3430_NEON_MOD, 0x20, 0x48 },
	{ "USB", MOD_CM, OMAP3430ES2_USBHOST_MOD, 0, 0x4c },

	{ "IVA2", MOD_PRM, OMAP3430_IVA2_MOD, 0x50, 0xfc },
	{ "OCP", MOD_PRM, OCP_MOD, 4, 0x1c },
	{ "MPU", MOD_PRM, MPU_MOD, 0x58, 0xe8 },
	{ "CORE", MOD_PRM, CORE_MOD, 0x58, 0xf8 },
	{ "SGX", MOD_PRM, OMAP3430ES2_SGX_MOD, 0x58, 0xe8 },
	{ "WKUP", MOD_PRM, WKUP_MOD, 0xa0, 0xb0 },
	{ "CCR", MOD_PRM, PLL_MOD, 0x40, 0x70 },
	{ "DSS", MOD_PRM, OMAP3430_DSS_MOD, 0x58, 0xe8 },
	{ "CAM", MOD_PRM, OMAP3430_CAM_MOD, 0x58, 0xe8 },
	{ "PER", MOD_PRM, OMAP3430_PER_MOD, 0x58, 0xe8 },
	{ "EMU", MOD_PRM, OMAP3430_EMU_MOD, 0x58, 0xe4 },
	{ "GLBL", MOD_PRM, OMAP3430_GR_MOD, 0x20, 0xe4 },
	{ "NEON", MOD_PRM, OMAP3430_NEON_MOD, 0x58, 0xe8 },
	{ "USB", MOD_PRM, OMAP3430ES2_USBHOST_MOD, 0x58, 0xe8 },
	{ "", 0, 0, 0, 0 },
};

#define PM_DBG_MAX_REG_SETS 4

static void *pm_dbg_reg_set[PM_DBG_MAX_REG_SETS];
#ifdef CONFIG_CPU_IDLE
static int cpuidle_force;
#endif

/* Debugfs interface to track clock notifier events */
struct clkn_event {
	struct list_head event_list;
	unsigned long long time;
	unsigned long rate;
	int msg;
};

struct clkn_tracker_info {
	/* Clock notifier data */
	struct clk *clock;
	struct notifier_block nb;
	/* polling data */
	wait_queue_head_t wq;
	struct work_struct ws;
	struct dentry *file;
	struct list_head tracker_list;
	/* mutex to access the buf with the events */
	struct mutex rmutex;
	struct list_head event_list;
	char buf[PAGE_SIZE];
};

struct clkn_tracker_info_head {
	/* mutext o access the list */
	struct mutex mutex;
	struct list_head tracker_list;
};

static struct clkn_tracker_info_head tracker_head;

static struct dentry *clkn_tracker_root;

static void tracker_print_events(struct clkn_tracker_info *tracker)
{
	unsigned long nanosec_rem;
	struct clkn_event *event, *rem;
	const char *ename;

	mutex_lock(&tracker->rmutex);
	list_for_each_entry_safe(event, rem, &tracker->event_list, event_list) {
		nanosec_rem = do_div(event->time, 1000000000) / 1000;

		switch (event->msg) {
		case CLK_PRE_RATE_CHANGE:
			ename = "PRE";
			break;
		case CLK_ABORT_RATE_CHANGE:
			ename = "ABORT";
			break;
		case CLK_POST_RATE_CHANGE:
			ename = "POST";
			break;
		default:
			ename = "UNKNOWN";
		}

		if ((PAGE_SIZE - strlen(tracker->buf)) < 50)
			tracker->buf[0] = '\0';
		snprintf(tracker->buf + strlen(tracker->buf), PAGE_SIZE,
				"[%5lu.%06lu] %s NOTIFIER %lu Hz\n",
				(unsigned long) event->time,
				nanosec_rem,
				ename,
				event->rate);
		list_del(&event->event_list);
		kfree(event);
	}
	mutex_unlock(&tracker->rmutex);
}

static void tracker_flush_data(struct work_struct *ws)
{
	struct clkn_tracker_info *tracker =
				container_of(ws, struct clkn_tracker_info, ws);

	tracker_print_events(tracker);

	wake_up_interruptible(&tracker->wq);
}

static int tracker_notifier_call(struct notifier_block *nblk, unsigned long msg,
								void *data)
{
	struct clkn_tracker_info *tracker;
	struct clk_notifier_data *cnd = data;
	struct clkn_event *event;

	tracker = container_of(nblk, struct clkn_tracker_info, nb);
	/* we are in interrupt context */
	event = kzalloc(sizeof(*event), GFP_ATOMIC);
	if (!event) {
		pr_err("%s: No mem for this event\n", __func__);
		return -ENOMEM;
	}
	event->time = (unsigned long long)(jiffies - INITIAL_JIFFIES)
			* (NSEC_PER_SEC / HZ);
	event->msg = msg;
	event->rate = cnd->rate;
	INIT_LIST_HEAD(&event->event_list);
	list_add_tail(&event->event_list, &tracker->event_list);

	schedule_work(&tracker->ws);

	return 0;
}

static unsigned int tracker_poll(struct file *file, poll_table *wait)
{
	struct clkn_tracker_info *priv = file->private_data;

	poll_wait(file, &priv->wq, wait);

	if (!list_empty(&priv->event_list))
		tracker_print_events(priv);

	return POLLIN | POLLRDNORM;
}

ssize_t tracker_read(struct file *file, char __user *buf, size_t size,
								loff_t *pos)
{
	struct clkn_tracker_info *priv = file->private_data;
	int ret;

	if (!priv)
		return 0;

	mutex_lock(&priv->rmutex);
	ret = simple_read_from_buffer(buf, size, pos, priv->buf, PAGE_SIZE);
	mutex_unlock(&priv->rmutex);

	return ret;
}

int tracker_open(struct inode *inode, struct file *filp)
{
	filp->private_data = inode->i_private;

	return 0;
}

static const struct file_operations clock_file_fops = {
	.open	= tracker_open,
	.read	= tracker_read,
	.poll	= tracker_poll,
};

static struct clkn_tracker_info *find_tracker(char *clk_name)
{
	struct clkn_tracker_info *tracker, *find = NULL;

	list_for_each_entry(tracker, &tracker_head.tracker_list, tracker_list) {
		if (!strcmp(tracker->clock->name, clk_name)) {
			find = tracker;
			break;
		}
	}

	return find;
}

static int register_tracker(char *clk_name)
{
	struct dentry *file;
	struct clkn_tracker_info *tracker;
	int err;

	tracker = find_tracker(clk_name);
	if (tracker)
		return 0;

	tracker = vmalloc(sizeof(*tracker));
	if (!tracker)
		return -ENOMEM;

	memset(tracker, 0, sizeof(*tracker));
	tracker->clock = clk_get(NULL, clk_name);
	if (IS_ERR(tracker->clock)) {
		pr_err("Wrong clock name: %s\n", clk_name);
		err = PTR_ERR(tracker->clock);
		goto free;
	}

	file = debugfs_create_file(clk_name, 0400, clkn_tracker_root, tracker,
						&clock_file_fops);
	if (!file) {
		err = -ENODEV;
		goto put_clock;
	}

	tracker->nb.notifier_call = tracker_notifier_call;

	err = clk_notifier_register(tracker->clock, &tracker->nb);
	if (err)
		goto remove_file;

	tracker->file = file;
	init_waitqueue_head(&tracker->wq);
	INIT_WORK(&tracker->ws, tracker_flush_data);
	INIT_LIST_HEAD(&tracker->tracker_list);
	INIT_LIST_HEAD(&tracker->event_list);
	mutex_init(&tracker->rmutex);
	list_add(&tracker->tracker_list, &tracker_head.tracker_list);

	return 0;

remove_file:
	debugfs_remove(file);
put_clock:
	clk_put(tracker->clock);
free:
	vfree(tracker);
	return err;
}

static void clean_tracker(struct clkn_tracker_info *tracker)
{
	struct clkn_event *event, *rem;

	cancel_work_sync(&tracker->ws);
	mutex_lock(&tracker->rmutex);
	list_for_each_entry_safe(event, rem, &tracker->event_list, event_list) {
		list_del(&event->event_list);
		kfree(event);
	}
	mutex_unlock(&tracker->rmutex);

	clk_notifier_unregister(tracker->clock, &tracker->nb);
	clk_put(tracker->clock);
	debugfs_remove(tracker->file);
	list_del(&tracker->tracker_list);
	vfree(tracker);
}

static int unregister_tracker(char *clk_name)
{
	struct clkn_tracker_info *remove = NULL;

	remove = find_tracker(clk_name);
	if (!remove) {
		pr_err("Wrong clock name: %s\n", clk_name);
		return -EINVAL;
	}

	clean_tracker(remove);

	return 0;
}

static ssize_t register_write(struct file *file, const char __user *buf,
		size_t len, loff_t *ppos)
{
	size_t size;
	ssize_t ret;
	char lbuf[128];

	ret = mutex_lock_interruptible(&tracker_head.mutex);
	if (ret)
		return ret;

	ret = -EFAULT;
	size = min(sizeof(lbuf) - 1, len);
	if (copy_from_user(lbuf, buf, size))
		goto out;

	lbuf[size] = '\0';
	strstrip(lbuf);
	ret = register_tracker(lbuf);
	if (ret == 0)
		ret = len;
out:
	mutex_unlock(&tracker_head.mutex);
	return ret;
}

static ssize_t unregister_write(struct file *file, const char __user *buf,
		size_t len, loff_t *ppos)
{
	size_t size;
	ssize_t ret;
	char lbuf[128];

	ret = mutex_lock_interruptible(&tracker_head.mutex);
	if (ret)
		return ret;

	ret = -EFAULT;
	size = min(sizeof(lbuf) - 1, len);
	if (copy_from_user(lbuf, buf, size))
		goto out;

	lbuf[size] = '\0';
	strstrip(lbuf);
	ret = unregister_tracker(lbuf);
	if (ret == 0)
		ret = len;
out:
	mutex_unlock(&tracker_head.mutex);
	return ret;

}

static const struct file_operations register_fops = {
	.open		= nonseekable_open,
	.write		= register_write,
};

static const struct file_operations unregister_fops = {
	.open		= nonseekable_open,
	.write		= unregister_write,
};

static int __init clkn_tracker_register_debugfs(struct dentry *root)
{
	struct dentry *file;

	clkn_tracker_root = debugfs_create_dir("clkn_tracker", root);
	if (!clkn_tracker_root)
		return -ENODEV;

	file = debugfs_create_file("register", 0200, clkn_tracker_root, NULL,
						&register_fops);
	if (!file)
		goto remove_dir;

	file = debugfs_create_file("unregister", 0200, clkn_tracker_root, NULL,
						&unregister_fops);
	if (!file)
		goto remove_dir;

	return 0;

remove_dir:
	debugfs_remove_recursive(clkn_tracker_root);
	return -ENODEV;
}

static int pm_dbg_get_regset_size(void)
{
	static int regset_size;

	if (regset_size == 0) {
		int i = 0;

		while (pm_dbg_reg_modules[i].name[0] != 0) {
			regset_size += pm_dbg_reg_modules[i].high +
				4 - pm_dbg_reg_modules[i].low;
			i++;
		}
	}
	return regset_size;
}

static int pm_dbg_show_regs(struct seq_file *s, void *unused)
{
	int i, j;
	unsigned long val;
	int reg_set = (int)s->private;
	u32 *ptr;
	void *store = NULL;
	int regs;
	int linefeed;

	if (reg_set == 0) {
		store = kmalloc(pm_dbg_get_regset_size(), GFP_KERNEL);
		ptr = store;
		pm_dbg_regset_store(ptr);
	} else {
		ptr = pm_dbg_reg_set[reg_set - 1];
	}

	i = 0;

	while (pm_dbg_reg_modules[i].name[0] != 0) {
		regs = 0;
		linefeed = 0;
		if (pm_dbg_reg_modules[i].type == MOD_CM)
			seq_printf(s, "MOD: CM_%s (%08x)\n",
				pm_dbg_reg_modules[i].name,
				(u32)(OMAP3430_CM_BASE +
				pm_dbg_reg_modules[i].offset));
		else
			seq_printf(s, "MOD: PRM_%s (%08x)\n",
				pm_dbg_reg_modules[i].name,
				(u32)(OMAP3430_PRM_BASE +
				pm_dbg_reg_modules[i].offset));

		for (j = pm_dbg_reg_modules[i].low;
			j <= pm_dbg_reg_modules[i].high; j += 4) {
			val = *(ptr++);
			if (val != 0) {
				regs++;
				if (linefeed) {
					seq_printf(s, "\n");
					linefeed = 0;
				}
				seq_printf(s, "  %02x => %08lx", j, val);
				if (regs % 4 == 0)
					linefeed = 1;
			}
		}
		seq_printf(s, "\n");
		i++;
	}

	if (store != NULL)
		kfree(store);

	return 0;
}

static void pm_dbg_regset_store(u32 *ptr)
{
	int i, j;
	u32 val;

	i = 0;

	while (pm_dbg_reg_modules[i].name[0] != 0) {
		for (j = pm_dbg_reg_modules[i].low;
			j <= pm_dbg_reg_modules[i].high; j += 4) {
			if (pm_dbg_reg_modules[i].type == MOD_CM)
				val = cm_read_mod_reg(
					pm_dbg_reg_modules[i].offset, j);
			else
				val = prm_read_mod_reg(
					pm_dbg_reg_modules[i].offset, j);
			*(ptr++) = val;
		}
		i++;
	}
}

int pm_dbg_regset_save(int reg_set)
{
	if (pm_dbg_reg_set[reg_set-1] == NULL)
		return -EINVAL;

	pm_dbg_regset_store(pm_dbg_reg_set[reg_set-1]);

	return 0;
}

static const char pwrdm_state_names[][PWRDM_MAX_PWRSTS] = {
	"OFF",
	"RET",
	"INA",
	"ON"
};

void pm_dbg_update_time(struct powerdomain *pwrdm, int prev)
{
	s64 t;

	if (!pm_dbg_init_done)
		return ;

	/* Update timer for previous state */
	t = sched_clock();

	pwrdm->state_timer[prev] += t - pwrdm->timer;

	pwrdm->timer = t;
}

static int clkdm_dbg_show_counter(struct clockdomain *clkdm, void *user)
{
	struct seq_file *s = (struct seq_file *)user;

	if (strcmp(clkdm->name, "emu_clkdm") == 0 ||
		strcmp(clkdm->name, "wkup_clkdm") == 0 ||
		strncmp(clkdm->name, "dpll", 4) == 0)
		return 0;

	seq_printf(s, "%s->%s (%d)", clkdm->name,
			clkdm->pwrdm.ptr->name,
			atomic_read(&clkdm->usecount));
	seq_printf(s, "\n");

	return 0;
}

static int pwrdm_dbg_show_counter(struct powerdomain *pwrdm, void *user)
{
	struct seq_file *s = (struct seq_file *)user;
	int i;

	if (strcmp(pwrdm->name, "emu_pwrdm") == 0 ||
		strcmp(pwrdm->name, "wkup_pwrdm") == 0 ||
		strncmp(pwrdm->name, "dpll", 4) == 0)
		return 0;

	if (pwrdm->state != pwrdm_read_pwrst(pwrdm))
		printk(KERN_ERR "pwrdm state mismatch(%s) %d != %d\n",
			pwrdm->name, pwrdm->state, pwrdm_read_pwrst(pwrdm));

	seq_printf(s, "%s (%s)", pwrdm->name,
			pwrdm_state_names[pwrdm->state]);
	for (i = 0; i < PWRDM_MAX_PWRSTS; i++)
		seq_printf(s, ",%s:%d", pwrdm_state_names[i],
			pwrdm->state_counter[i]);

	seq_printf(s, "\n");

	return 0;
}

static int pwrdm_dbg_show_timer(struct powerdomain *pwrdm, void *user)
{
	struct seq_file *s = (struct seq_file *)user;
	int i;

	if (strcmp(pwrdm->name, "emu_pwrdm") == 0 ||
		strcmp(pwrdm->name, "wkup_pwrdm") == 0 ||
		strncmp(pwrdm->name, "dpll", 4) == 0)
		return 0;

	pwrdm_state_switch(pwrdm);

	seq_printf(s, "%s (%s)", pwrdm->name,
		pwrdm_state_names[pwrdm->state]);

	for (i = 0; i < 4; i++)
		seq_printf(s, ",%s:%lld", pwrdm_state_names[i],
			pwrdm->state_timer[i]);

	seq_printf(s, "\n");
	return 0;
}

static int pm_dbg_show_counters(struct seq_file *s, void *unused)
{
	pwrdm_for_each(pwrdm_dbg_show_counter, s);
	clkdm_for_each(clkdm_dbg_show_counter, s);

	return 0;
}

static int pm_dbg_show_timers(struct seq_file *s, void *unused)
{
	pwrdm_for_each(pwrdm_dbg_show_timer, s);
	return 0;
}

static int pm_dbg_show_optimizer(struct seq_file *s, void *unused)
{
	int min, max;

	min = 0; max = INT_MAX;
	pm_optimizer_limits(PM_OPTIMIZER_CPUIDLE, &min, &max, s);
	min = 0; max = INT_MAX;
	pm_optimizer_limits(PM_OPTIMIZER_CPUFREQ, &min, &max, s);
	return 0;
}

static int pm_dbg_show_sdrc_wait_count(struct seq_file *s, void *unused)
{
	unsigned int *sdrc_counters = omap3_get_sdrc_counters();

	seq_printf(s, "dll kick count: %u\n", sdrc_counters[0]);
	seq_printf(s, "wait dll lock count: %u\n", sdrc_counters[1]);

	return 0;
}

static int pm_dbg_show_secure_ram_state(struct seq_file *s, void *unused)
{
	seq_printf(s, "secure ram %s to be saved\n",
		omap3_check_secure_ram_dirty() ? "needs" : "doesn't need");

	return 0;
}

static int pm_dbg_open(struct inode *inode, struct file *file)
{
	switch ((int)inode->i_private) {
	case DEBUG_FILE_PM_OPT:
		return single_open(file, pm_dbg_show_optimizer,
			&inode->i_private);
	case DEBUG_FILE_WAIT_SDRC_COUNT:
		return single_open(file, pm_dbg_show_sdrc_wait_count,
			&inode->i_private);
	case DEBUG_FILE_COUNTERS:
		return single_open(file, pm_dbg_show_counters,
			&inode->i_private);
	case DEBUG_FILE_RESOURCES:
		return single_open(file, resource_dump_reqs,
			&inode->i_private);
	case DEBUG_FILE_PM_QOS:
		return single_open(file, pm_qos_dump_reqs,
			&inode->i_private);
	case DEBUG_FILE_TIMERS:
		return single_open(file, pm_dbg_show_timers,
			&inode->i_private);
	default:
	case DEBUG_FILE_SECURE_RAM_STATE:
		return single_open(file, pm_dbg_show_secure_ram_state,
			&inode->i_private);
	};
}

static int pm_dbg_reg_open(struct inode *inode, struct file *file)
{
	return single_open(file, pm_dbg_show_regs, inode->i_private);
}

static const struct file_operations debug_fops = {
	.open           = pm_dbg_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = single_release,
};

static const struct file_operations debug_reg_fops = {
	.open           = pm_dbg_reg_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = single_release,
};

int pm_dbg_regset_init(int reg_set)
{
	char name[2];

	if (!pm_dbg_init_done)
		pm_dbg_init();

	if (reg_set < 1 || reg_set > PM_DBG_MAX_REG_SETS ||
		pm_dbg_reg_set[reg_set-1] != NULL)
		return -EINVAL;

	pm_dbg_reg_set[reg_set-1] =
		kmalloc(pm_dbg_get_regset_size(), GFP_KERNEL);

	if (pm_dbg_reg_set[reg_set-1] == NULL)
		return -ENOMEM;

	if (pm_dbg_dir != NULL) {
		sprintf(name, "%d", reg_set);

		(void) debugfs_create_file(name, S_IRUGO,
			pm_dbg_dir, (void *)reg_set, &debug_reg_fops);
	}

	return 0;
}

static int pwrdm_suspend_get(void *data, u64 *val)
{
	int ret;
	ret = omap3_pm_get_suspend_state((struct powerdomain *)data);
	*val = ret;

	if (ret >= 0)
		return 0;
	return *val;
}

static int pwrdm_suspend_set(void *data, u64 val)
{
	return omap3_pm_set_suspend_state((struct powerdomain *)data, (int)val);
}

DEFINE_SIMPLE_ATTRIBUTE(pwrdm_suspend_fops, pwrdm_suspend_get,
			pwrdm_suspend_set, "%llu\n");

static u32 dma_state;
static u32 dma_status;
static int dma_ch;
static u8 *dmasrcbuf;
static u8 *dmadstbuf;
static int dmabufsiz;
static dma_addr_t dma_src, dma_dst;

static void start_dma_xfer(void)
{
	dma_addr_t dma_tmp_addr;

	omap_set_dma_src_params(dma_ch, 0, OMAP_DMA_AMODE_POST_INC,
		dma_src, 0, 0);
	omap_set_dma_dest_params(dma_ch, 0, OMAP_DMA_AMODE_POST_INC,
		dma_dst, 0, 0);
	omap_set_dma_dest_data_pack(dma_ch, 1);
	omap_set_dma_dest_burst_mode(dma_ch, OMAP_DMA_DATA_BURST_4);
	omap_set_dma_transfer_params(dma_ch, OMAP_DMA_DATA_TYPE_S32,
		dmabufsiz >> 2, 1, 0, 0, 0);
	omap_start_dma(dma_ch);

	dma_tmp_addr = dma_src;
	dma_src = dma_dst;
	dma_dst = dma_tmp_addr;
	dma_status = 2;
}

static void dma_cb(int lch, u16 ch_status, void *data)
{
	int i;

	if (dma_state == 0) {
		omap_free_dma(dma_ch);
		dma_status = 1;
		for (i = 0; i < dmabufsiz; i++) {
			if (dmasrcbuf[i] != (i & 0xff) ||
			    dmadstbuf[i] != (i & 0xff)) {
				pr_info("compare error at %i, expected %02x, "
					"got %02x %02x\n", i, i & 255,
					dmadstbuf[i], dmasrcbuf[i]);
				dma_status = 0;
				break;
			}
		}
		if (i == dmabufsiz)
			pr_info("compare ok\n");

		kfree(dmasrcbuf);
		kfree(dmadstbuf);
		dmasrcbuf = NULL;
		dmadstbuf = NULL;
		pr_info("dma stopped\n");
	} else
		start_dma_xfer();
}

static void setup_dma(int size)
{
	int i;

	if (size == 0)
		return;

	dmabufsiz = size;
	dmasrcbuf = kmalloc(size, GFP_KERNEL);
	dmadstbuf = kmalloc(size, GFP_KERNEL);

	if (!dmasrcbuf || !dmadstbuf) {
		pr_info("Buffer alloc failed.\n");
		kfree(dmasrcbuf);
		kfree(dmadstbuf);
		return;
	}

	for (i = 0; i < size; i++)
		dmasrcbuf[i] = i & 0xff;

	if (omap_request_dma(0, "test_dma", dma_cb, NULL, &dma_ch)) {
		pr_info("Failed to get DMA channel.\n");
		return;
	}

	dma_src = dma_map_single(NULL, dmasrcbuf, dmabufsiz,
			DMA_BIDIRECTIONAL);
	dma_dst = dma_map_single(NULL, dmadstbuf, dmabufsiz,
			DMA_BIDIRECTIONAL);

	pr_info("starting DMA xfer...\n");
	start_dma_xfer();
}

static const char *pwrdm_names[] = {
	"core_pwrdm",
	"neon_pwrdm",
	"usbhost_pwrdm",
#if !defined(CONFIG_MPU_BRIDGE) && !defined(CONFIG_MPU_BRIDGE_MODULE)
	"iva2_pwrdm",
#endif
	"sgx_pwrdm",
	"dss_pwrdm",
	"per_pwrdm",
	"cam_pwrdm",
	"mpu_pwrdm",
};

#define NUM_PWR_DOMAINS		ARRAY_SIZE(pwrdm_names)

static u32 madc_testing;
static u32 min_bus_tput;
static u32 max_mpu_wakeup_lat;
static u32 max_sdma_lat;
static u32 max_dev_wakeup_lat[NUM_PWR_DOMAINS];

static struct device dummy_devs[NUM_PWR_DOMAINS];
static struct device bus_test_dev;

static struct platform_device pwr_testdevs[] = {
	{
		.name = "core_test",
		.dev = { &dummy_devs[0] },
	},
	{
		.name = "neon_test",
		.dev = { &dummy_devs[1] },
	},
	{
		.name = "usbhost_test",
		.dev = { &dummy_devs[2] },
	},
#if !defined(CONFIG_MPU_BRIDGE) && !defined(CONFIG_MPU_BRIDGE_MODULE)
	{
		.name = "iva2_test",
		.dev = { &dummy_devs[3] },
	},
#endif
	{
		.name = "sgx_test",
		.dev = { &dummy_devs[4] },
	},
	{
		.name = "dss_test",
		.dev = { &dummy_devs[5] },
	},
	{
		.name = "per_test",
		.dev = { &dummy_devs[6] },
	},
	{
		.name = "cam_test",
		.dev = { &dummy_devs[7] },
	},
	{
		.name = "mpu_test",
		.dev = { &dummy_devs[8] },
	},
};

static int pwr_domain_to_index(const char *name)
{
	int i;

	for (i = 0; i < NUM_PWR_DOMAINS; i++) {
		if (!strcmp(name, pwrdm_names[i]))
			return i;
	}

	return -1;
}

static int dev_wakeup_option_get(void *data, u64 *val)
{
	struct powerdomain *option = data;
	int i;

	i = pwr_domain_to_index(option->name);

	if (i < 0)
		return -ENOENT;

	*val = max_dev_wakeup_lat[i];

	return 0;
}

static int dev_wakeup_option_set(void *data, u64 val)
{
	struct powerdomain *option = data;
	int i;
	long value;

	if ((long)val < 0)
		return -EINVAL;

	i = pwr_domain_to_index(option->name);

	if (i < 0)
		return -ENOENT;

	max_dev_wakeup_lat[i] = (u32) val;
	value = val ? ((long)val) : -1;
	omap_pm_set_max_dev_wakeup_lat(&pwr_testdevs[i].dev, value);

	return 0;
}

static int omap_pm_set_madc_testing(u64 val)
{
	static u32 initval;
	static u8 saved;

	if (!saved) {
		initval = omap_ctrl_readw(OMAP343X_PADCONF_DSS_DAT16);
		saved++;
	}

	if (val)
		omap_ctrl_writew(OMAP34XX_MUX_MODE4,
				 OMAP343X_PADCONF_DSS_DAT16);
	else
		omap_ctrl_writew(initval, OMAP343X_PADCONF_DSS_DAT16);

	return 0;
}

static int omap_pm_test_option_set(void *data, u64 val)
{
	u32 *option = data;
	long value;

	if (((long)val < 0) &&
		(option == &max_mpu_wakeup_lat || option == &max_sdma_lat))
		return -EINVAL;

	*option = val;

	if (option == &madc_testing)
		omap_pm_set_madc_testing(val);

	if (option == &min_bus_tput)
		omap_pm_set_min_bus_tput(&bus_test_dev, OCP_INITIATOR_AGENT,
					(unsigned long)val);
	if (option == &max_mpu_wakeup_lat) {
		value = val ? ((long)val) : -1;
		omap_pm_set_max_mpu_wakeup_lat(&bus_test_dev, value);
	}
	if (option == &max_sdma_lat) {
		value = val ? ((long)val) : -1;
		omap_pm_set_max_sdma_lat(&bus_test_dev, value);
	}

	return 0;
}

static int context_option_get(void *data, u64 *val)
{
	struct powerdomain *option = data;
	int i;

	i = pwr_domain_to_index(option->name);

	if (i < 0)
		return -ENOENT;

	*val = omap_pm_get_dev_context_loss_count(&pwr_testdevs[i].dev);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(pwrdm_dev_wakeup_fops, dev_wakeup_option_get,
			dev_wakeup_option_set, "%llu\n");
DEFINE_SIMPLE_ATTRIBUTE(context_test_fops, context_option_get, NULL, "%llu\n");

static int __init pwrdms_setup(struct powerdomain *pwrdm, void *dir)
{
	int i;
	s64 t;
	struct dentry *d;

	t = sched_clock();

	for (i = 0; i < 4; i++)
		pwrdm->state_timer[i] = 0;

	pwrdm->timer = t;

	if (strncmp(pwrdm->name, "dpll", 4) == 0)
		return 0;

	d = debugfs_create_dir(pwrdm->name, (struct dentry *)dir);

	(void) debugfs_create_file("suspend", S_IRUGO|S_IWUSR, d,
			(void *)pwrdm, &pwrdm_suspend_fops);

	if (strcmp(pwrdm->name, "mpu_pwrdm") != 0 &&
#if defined(CONFIG_MPU_BRIDGE) || defined(CONFIG_MPU_BRIDGE_MODULE)
	    strcmp(pwrdm->name, "iva2_pwrdm") != 0 &&
#endif
	    strcmp(pwrdm->name, "emu_pwrdm") != 0 &&
	    strcmp(pwrdm->name, "wkup_pwrdm") != 0) {
		(void) debugfs_create_file("max_dev_wakeup_lat",
					   S_IRUGO|S_IWUGO, d, (void *)pwrdm,
					   &pwrdm_dev_wakeup_fops);
	}

	if (strcmp(pwrdm->name, "emu_pwrdm") != 0 &&
#if defined(CONFIG_MPU_BRIDGE) || defined(CONFIG_MPU_BRIDGE_MODULE)
	    strcmp(pwrdm->name, "iva2_pwrdm") != 0 &&
#endif
	    strcmp(pwrdm->name, "wkup_pwrdm") != 0) {
		(void) debugfs_create_file("context_loss_count", S_IRUGO, d,
					(void *)pwrdm, &context_test_fops);
	}

	return 0;
}

static int option_get(void *data, u64 *val)
{
	u32 *option = data;

	*val = *option;

	return 0;
}

static int option_set(void *data, u64 val)
{
	u32 *option = data;

	if (option == &wakeup_timer_milliseconds && val >= 1000)
		return -EINVAL;

	*option = val;

	if (option == &disable_core_off)
		omap3_pm_off_mode_enable(enable_off_mode);
	if (option == &enable_off_mode)
		omap3_pm_off_mode_enable(val);
	if (option == &voltage_off_while_idle) {
		if (voltage_off_while_idle)
			prm_set_mod_reg_bits(OMAP3430_SEL_OFF, OMAP3430_GR_MOD,
					     OMAP3_PRM_VOLTCTRL_OFFSET);
		else
			prm_clear_mod_reg_bits(OMAP3430_SEL_OFF,
					       OMAP3430_GR_MOD,
					       OMAP3_PRM_VOLTCTRL_OFFSET);
	}
	if (option == &dma_state)
		setup_dma(val);
#ifdef CONFIG_CPU_IDLE
	if (option == &cpuidle_force)
		if (omap3_pm_force_cpuidle_state((int)val)) {
			pr_info("Illegal C state\n");
			omap3_pm_force_cpuidle_state(0);
			*option = 0;
		}
#endif

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(omap_pm_test_fops, option_get,
			omap_pm_test_option_set, "%llu\n");
DEFINE_SIMPLE_ATTRIBUTE(pm_dbg_option_fops, option_get, option_set, "%llu\n");

static int __init pm_dbg_init(void)
{
	int i, ret;
	struct dentry *d;
	char name[2];

	if (pm_dbg_init_done)
		return 0;

	if (cpu_is_omap34xx())
		pm_dbg_reg_modules = omap3_pm_reg_modules;
	else {
		printk(KERN_ERR "%s: only OMAP3 supported\n", __func__);
		return -ENODEV;
	}

	d = debugfs_create_dir("pm_debug", NULL);
	if (IS_ERR(d))
		return PTR_ERR(d);

	(void) debugfs_create_file("count", S_IRUGO,
		d, (void *)DEBUG_FILE_COUNTERS, &debug_fops);
	(void) debugfs_create_file("time", S_IRUGO,
		d, (void *)DEBUG_FILE_TIMERS, &debug_fops);
	(void) debugfs_create_file("wait_sdrc_count", S_IRUGO,
		d, (void *)DEBUG_FILE_WAIT_SDRC_COUNT, &debug_fops);
	(void) debugfs_create_file("secure_ram",  S_IRUGO,
		d, (void *)DEBUG_FILE_SECURE_RAM_STATE, &debug_fops);
	(void) debugfs_create_file("resources", S_IRUGO,
		d, (void *)DEBUG_FILE_RESOURCES, &debug_fops);
	(void) debugfs_create_file("pm_qos", S_IRUGO,
		d, (void *)DEBUG_FILE_PM_QOS, &debug_fops);

	pwrdm_for_each_nolock(pwrdms_setup, (void *)d);

	pm_dbg_dir = debugfs_create_dir("registers", d);
	if (IS_ERR(pm_dbg_dir))
		return PTR_ERR(pm_dbg_dir);

	(void) debugfs_create_file("current", S_IRUGO,
		pm_dbg_dir, (void *)0, &debug_reg_fops);

	for (i = 0; i < PM_DBG_MAX_REG_SETS; i++)
		if (pm_dbg_reg_set[i] != NULL) {
			sprintf(name, "%d", i+1);
			(void) debugfs_create_file(name, S_IRUGO,
				pm_dbg_dir, (void *)(i+1), &debug_reg_fops);

		}

	(void) debugfs_create_file("madc_testing", S_IRUGO | S_IWUGO, d,
				   &madc_testing, &omap_pm_test_fops);

	(void) debugfs_create_file("min_bus_tput", S_IRUGO | S_IWUGO, d,
				   &min_bus_tput, &omap_pm_test_fops);
	(void) debugfs_create_file("max_mpu_wakeup_lat", S_IRUGO | S_IWUGO, d,
				   &max_mpu_wakeup_lat, &omap_pm_test_fops);
	(void) debugfs_create_file("max_sdma_lat", S_IRUGO | S_IWUGO, d,
				   &max_sdma_lat, &omap_pm_test_fops);

	(void) debugfs_create_file("enable_off_mode", S_IRUGO | S_IWUGO, d,
				   &enable_off_mode, &pm_dbg_option_fops);
	(void) debugfs_create_file("disable_core_off", S_IRUGO | S_IWUGO, d,
				   &disable_core_off, &pm_dbg_option_fops);
	(void) debugfs_create_file("sleep_while_idle", S_IRUGO | S_IWUGO, d,
				   &sleep_while_idle, &pm_dbg_option_fops);
	(void) debugfs_create_file("wakeup_timer_seconds", S_IRUGO | S_IWUGO, d,
				   &wakeup_timer_seconds, &pm_dbg_option_fops);
	(void) debugfs_create_file("wakeup_timer_milliseconds",
				   S_IRUGO | S_IWUGO, d,
				   &wakeup_timer_milliseconds,
				   &pm_dbg_option_fops);
	(void) debugfs_create_file("resume_action", S_IRUGO | S_IWUGO, d,
				   &resume_action, &pm_dbg_option_fops);
	(void) debugfs_create_file("dma", S_IRUGO | S_IWUGO, d,
				   &dma_state, &pm_dbg_option_fops);
	(void) debugfs_create_file("dma_status", S_IRUGO, d,
				   &dma_status, &pm_dbg_option_fops);
#ifdef CONFIG_CPU_IDLE
	(void) debugfs_create_file("cpuidle_forced_state", S_IRUGO | S_IWUGO, d,
				   &cpuidle_force, &pm_dbg_option_fops);
#endif

	/* Only enable for >= ES2.1 . Going to 0V on anything under
	 * ES2.1 will eventually cause a crash */
	if (omap_rev() > OMAP3430_REV_ES2_0)
		(void) debugfs_create_file("voltage_off_while_idle",
					   S_IRUGO | S_IWUGO, d,
					   &voltage_off_while_idle,
					   &pm_dbg_option_fops);
	pm_dbg_main_dir = d;
	pm_optimizer_populate_debugfs(d, &pm_dbg_option_fops,
			(void *)DEBUG_FILE_PM_OPT, &debug_fops);
	mutex_init(&tracker_head.mutex);
	INIT_LIST_HEAD(&tracker_head.tracker_list);

	ret = clkn_tracker_register_debugfs(d);
	if (ret)
		return ret;

	pm_dbg_init_done = 1;

	return 0;
}
arch_initcall(pm_dbg_init);

#endif
