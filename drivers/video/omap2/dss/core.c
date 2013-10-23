/*
 * linux/drivers/video/omap2/dss/core.c
 *
 * Copyright (C) 2009 Nokia Corporation
 * Author: Tomi Valkeinen <tomi.valkeinen@nokia.com>
 *
 * Some code and ideas taken from drivers/video/omap/ driver
 * by Imre Deak.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define DSS_SUBSYS_NAME "CORE"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/io.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>

#include <mach/display.h>
#include <mach/clock.h>
#include <mach/omap-pm.h>

#include "dss.h"

static struct {
	struct platform_device *pdev;
	int		ctx_id;

	struct clk      *dss_ick;
	struct clk	*dss1_fck;
	struct clk	*dss2_fck;
	struct clk      *dss_54m_fck;
	struct clk	*dss_96m_fck;
	unsigned	num_clks_enabled;

	struct delayed_work bus_tput_work;
	unsigned int bus_tput;

	struct mutex dss_lock;
} core;

static void dss_clk_enable_all_no_ctx(void);
static void dss_clk_disable_all_no_ctx(void);
static void dss_clk_enable_no_ctx(enum dss_clock clks);
static void dss_clk_disable_no_ctx(enum dss_clock clks);

static char *def_disp_name;
module_param_named(def_disp, def_disp_name, charp, 0);
MODULE_PARM_DESC(def_disp_name, "default display name");

#ifdef DEBUG
unsigned int dss_debug;
module_param_named(debug, dss_debug, bool, 0644);
#endif

/* CONTEXT */
static int dss_get_ctx_id(void)
{
	struct omap_dss_board_info *pdata = core.pdev->dev.platform_data;
	int r;

	if (!pdata->get_last_off_on_transaction_id)
		return 0;
	r = pdata->get_last_off_on_transaction_id(&core.pdev->dev);
	if (r < 0) {
		dev_err(&core.pdev->dev,
			"getting transaction ID failed, will force context restore\n");
		r = -1;
	}
	return r;
}

int dss_need_ctx_restore(void)
{
	int id = dss_get_ctx_id();

	if (id < 0 || id != core.ctx_id) {
		DSSDBG("ctx id %d -> id %d\n",
				core.ctx_id, id);
		core.ctx_id = id;
		return 1;
	} else {
		/* Hack to workaround context loss */
		if (dss_check_context()) {
			DSSERR("unexpected HW context loss, will force context restore (id=%d)\n",
			       id);
			return 1;
		}

		return 0;
	}
}

static void save_all_ctx(void)
{
	DSSDBG("save context\n");

	dss_clk_enable_no_ctx(DSS_CLK_ICK | DSS_CLK_FCK1);

	/* Hack to workaround context loss */
	if (dss_check_context()) {
		DSSERR("HW context corrupted, skipping save\n");
		goto out;
	}

	dss_save_context();
	dispc_save_context();
#ifdef CONFIG_OMAP2_DSS_DSI
	dsi_save_context();
#endif

 out:
	dss_clk_disable_no_ctx(DSS_CLK_ICK | DSS_CLK_FCK1);
}

static void restore_all_ctx(void)
{
	DSSDBG("restore context\n");

	dss_clk_enable_all_no_ctx();

	dss_restore_context();
	dispc_restore_context();
#ifdef CONFIG_OMAP2_DSS_DSI
	dsi_restore_context();
#endif

	dss_clk_disable_all_no_ctx();
}

/* CLOCKS */
void dss_dump_clocks(struct seq_file *s)
{
	int i;
	struct clk *clocks[5] = {
		core.dss_ick,
		core.dss1_fck,
		core.dss2_fck,
		core.dss_54m_fck,
		core.dss_96m_fck
	};

	seq_printf(s, "- dss -\n");

	seq_printf(s, "internal clk count\t%u\n", core.num_clks_enabled);

	for (i = 0; i < 5; i++) {
		if (!clocks[i])
			continue;
		seq_printf(s, "%-15s\t%lu\t%d\n",
				clocks[i]->name,
				clk_get_rate(clocks[i]),
				clocks[i]->usecount);
	}
}

static int dss_get_clocks(void)
{
	const struct {
		struct clk **clock;
		char *omap2_name;
		char *omap3_name;
	} clocks[5] = {
		{ &core.dss_ick, "dss_ick", "dss_ick" },	/* L3 & L4 ick */
		{ &core.dss1_fck, "dss1_fck", "dss1_alwon_fck" },
		{ &core.dss2_fck, "dss2_fck", "dss2_alwon_fck" },
		{ &core.dss_54m_fck, "dss_54m_fck", "dss_tv_fck" },
		{ &core.dss_96m_fck, NULL, "dss_96m_fck" },
	};

	int r = 0;
	int i;
	const int num_clocks = 5;

	for (i = 0; i < num_clocks; i++)
		*clocks[i].clock = NULL;

	for (i = 0; i < num_clocks; i++) {
		struct clk *clk;
		const char *clk_name;

		clk_name = cpu_is_omap34xx() ? clocks[i].omap3_name
			: clocks[i].omap2_name;

		if (!clk_name)
			continue;

		clk = clk_get(NULL, clk_name);

		if (IS_ERR(clk)) {
			DSSERR("can't get clock %s", clk_name);
			r = PTR_ERR(clk);
			goto err;
		}

		DSSDBG("clk %s, rate %ld\n",
				clk_name, clk_get_rate(clk));

		*clocks[i].clock = clk;
	}

	return 0;

err:
	for (i = 0; i < num_clocks; i++) {
		if (!IS_ERR(*clocks[i].clock))
			clk_put(*clocks[i].clock);
	}

	return r;
}

static void dss_put_clocks(void)
{
	if (core.dss_96m_fck)
		clk_put(core.dss_96m_fck);
	clk_put(core.dss_54m_fck);
	clk_put(core.dss1_fck);
	clk_put(core.dss2_fck);
	clk_put(core.dss_ick);
}

unsigned long dss_clk_get_rate(enum dss_clock clk)
{
	switch (clk) {
	case DSS_CLK_ICK:
		return clk_get_rate(core.dss_ick);
	case DSS_CLK_FCK1:
		return clk_get_rate(core.dss1_fck);
	case DSS_CLK_FCK2:
		return clk_get_rate(core.dss2_fck);
	case DSS_CLK_54M:
		return clk_get_rate(core.dss_54m_fck);
	case DSS_CLK_96M:
		return clk_get_rate(core.dss_96m_fck);
	}

	BUG();
	return 0;
}

static unsigned count_clk_bits(enum dss_clock clks)
{
	unsigned num_clks = 0;

	if (clks & DSS_CLK_ICK)
		++num_clks;
	if (clks & DSS_CLK_FCK1)
		++num_clks;
	if (clks & DSS_CLK_FCK2)
		++num_clks;
	if (clks & DSS_CLK_54M)
		++num_clks;
	if (clks & DSS_CLK_96M)
		++num_clks;

	return num_clks;
}

static void dss_clk_enable_no_ctx(enum dss_clock clks)
{
	unsigned num_clks = count_clk_bits(clks);

	if (clks & DSS_CLK_ICK)
		clk_enable(core.dss_ick);
	if (clks & DSS_CLK_FCK1)
		clk_enable(core.dss1_fck);
	if (clks & DSS_CLK_FCK2)
		clk_enable(core.dss2_fck);
	if (clks & DSS_CLK_54M)
		clk_enable(core.dss_54m_fck);
	if (clks & DSS_CLK_96M)
		clk_enable(core.dss_96m_fck);

	core.num_clks_enabled += num_clks;
}

void dss_clk_enable(enum dss_clock clks)
{
	dss_clk_enable_no_ctx(clks);

	if (cpu_is_omap34xx() && dss_need_ctx_restore())
		restore_all_ctx();
}

static void dss_clk_disable_no_ctx(enum dss_clock clks)
{
	unsigned num_clks = count_clk_bits(clks);

	if (clks & DSS_CLK_ICK)
		clk_disable(core.dss_ick);
	if (clks & DSS_CLK_FCK1)
		clk_disable(core.dss1_fck);
	if (clks & DSS_CLK_FCK2)
		clk_disable(core.dss2_fck);
	if (clks & DSS_CLK_54M)
		clk_disable(core.dss_54m_fck);
	if (clks & DSS_CLK_96M)
		clk_disable(core.dss_96m_fck);

	core.num_clks_enabled -= num_clks;
}

void dss_clk_disable(enum dss_clock clks)
{
	if (cpu_is_omap34xx()) {
		unsigned num_clks = count_clk_bits(clks);

		BUG_ON(core.num_clks_enabled < num_clks);

		if (core.num_clks_enabled == num_clks)
			save_all_ctx();
	}

	dss_clk_disable_no_ctx(clks);
}

static void dss_clk_enable_all_no_ctx(void)
{
	enum dss_clock clks;

	clks = DSS_CLK_ICK | DSS_CLK_FCK1 | DSS_CLK_FCK2 | DSS_CLK_54M;
	if (cpu_is_omap34xx())
		clks |= DSS_CLK_96M;
	dss_clk_enable_no_ctx(clks);
}

static void dss_clk_disable_all_no_ctx(void)
{
	enum dss_clock clks;

	clks = DSS_CLK_ICK | DSS_CLK_FCK1 | DSS_CLK_FCK2 | DSS_CLK_54M;
	if (cpu_is_omap34xx())
		clks |= DSS_CLK_96M;
	dss_clk_disable_no_ctx(clks);
}

static void dss_clk_enable_all(void)
{
	enum dss_clock clks;

	clks = DSS_CLK_ICK | DSS_CLK_FCK1 | DSS_CLK_FCK2 | DSS_CLK_54M;
	if (cpu_is_omap34xx())
		clks |= DSS_CLK_96M;
	dss_clk_enable(clks);
}

static void dss_clk_disable_all(void)
{
	enum dss_clock clks;

	clks = DSS_CLK_ICK | DSS_CLK_FCK1 | DSS_CLK_FCK2 | DSS_CLK_54M;
	if (cpu_is_omap34xx())
		clks |= DSS_CLK_96M;
	dss_clk_disable(clks);
}

/* DEBUGFS */
#if defined(CONFIG_DEBUG_FS) && defined(CONFIG_OMAP2_DSS_DEBUG_SUPPORT)
static void dss_debug_dump_clocks(struct seq_file *s)
{
	dss_dump_clocks(s);
	dispc_dump_clocks(s);
#ifdef CONFIG_OMAP2_DSS_DSI
	dsi_dump_clocks(s);
#endif
}

static int dss_debug_show(struct seq_file *s, void *unused)
{
	void (*func)(struct seq_file *) = s->private;
	func(s);
	return 0;
}

static int dss_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, dss_debug_show, inode->i_private);
}

static const struct file_operations dss_debug_fops = {
	.open           = dss_debug_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = single_release,
};

static struct dentry *dss_debugfs_dir;

static int dss_initialize_debugfs(void)
{
	dss_debugfs_dir = debugfs_create_dir("omapdss", NULL);
	if (IS_ERR(dss_debugfs_dir)) {
		int err = PTR_ERR(dss_debugfs_dir);
		dss_debugfs_dir = NULL;
		return err;
	}

	debugfs_create_file("clk", S_IRUGO, dss_debugfs_dir,
			&dss_debug_dump_clocks, &dss_debug_fops);

	debugfs_create_file("dss", S_IRUGO, dss_debugfs_dir,
			&dss_dump_regs, &dss_debug_fops);
	debugfs_create_file("dispc", S_IRUGO, dss_debugfs_dir,
			&dispc_dump_regs, &dss_debug_fops);
#ifdef CONFIG_OMAP2_DSS_RFBI
	debugfs_create_file("rfbi", S_IRUGO, dss_debugfs_dir,
			&rfbi_dump_regs, &dss_debug_fops);
#endif
#ifdef CONFIG_OMAP2_DSS_DSI
	debugfs_create_file("dsi", S_IRUGO, dss_debugfs_dir,
			&dsi_dump_regs, &dss_debug_fops);
#endif
#ifdef CONFIG_OMAP2_DSS_VENC
	debugfs_create_file("venc", S_IRUGO, dss_debugfs_dir,
			&venc_dump_regs, &dss_debug_fops);
#endif
	return 0;
}

static void dss_uninitialize_debugfs(void)
{
	if (dss_debugfs_dir)
		debugfs_remove_recursive(dss_debugfs_dir);
}
#endif /* CONFIG_DEBUG_FS && CONFIG_OMAP2_DSS_DEBUG_SUPPORT */

void omap_dss_lock(void)
{
	mutex_lock(&core.dss_lock);
}
EXPORT_SYMBOL(omap_dss_lock);

void omap_dss_unlock(void)
{
	mutex_unlock(&core.dss_lock);
}
EXPORT_SYMBOL(omap_dss_unlock);

/* RESET */

void dss_soft_reset(void)
{
	DSSDBG("performing soft reset\n");

	omap_dss_lock();
	dss_clk_enable_all();
	dss_suspend_all_displays();
	save_all_ctx();

	dss_reset();

	restore_all_ctx();
	dss_resume_all_displays();
	dss_clk_disable_all();
	omap_dss_unlock();

	DSSERR("done with soft reset\n");
}

/* DVFS */

static void bus_tput_work_func(struct work_struct *work)
{
	struct omap_dss_board_info *pdata = core.pdev->dev.platform_data;

	DSSDBG("setting bus throughput to %d KiB/s\n", core.bus_tput);
	pdata->set_min_bus_tput(&core.pdev->dev,
			OCP_INITIATOR_AGENT, core.bus_tput);
}

static void set_min_bus_tput(unsigned int num_overlays)
{
	struct omap_dss_board_info *pdata = core.pdev->dev.platform_data;
	/*
	 * Magic value 400000 chosen so that on OMAP3 OPP3 is used.
	 */
	unsigned int tput_max = 400000;
	unsigned int tput = num_overlays ? 400000 : 0;

	if (!pdata->set_min_bus_tput || tput == core.bus_tput)
		return;

	cancel_delayed_work_sync(&core.bus_tput_work);

	core.bus_tput = tput;

	/* Switch to the maximum when the FIFOs are empty. */
	DSSDBG("setting bus throughput to %d KiB/s\n", tput_max);
	pdata->set_min_bus_tput(&core.pdev->dev, OCP_INITIATOR_AGENT, tput_max);

	if (tput == tput_max)
		return;

	/* Switch to whatever was requested after things have stabilized. */
	schedule_delayed_work(&core.bus_tput_work, msecs_to_jiffies(2000));
}

void omap_dss_maximize_min_bus_tput(void)
{
	set_min_bus_tput(omap_dss_get_num_overlays());
}

void omap_dss_update_min_bus_tput(void)
{
	int i;
	struct omap_display *display;
	struct omap_overlay *ovl;
	int num_overlays = 0;

	DSSDBG("dss_update_min_bus_tput()\n");

	/* Determine how many overlays are actually fetching data */
	for (i = 0; i < omap_dss_get_num_overlays(); ++i) {
		ovl = omap_dss_get_overlay(i);

		if (!(ovl->caps & OMAP_DSS_OVL_CAP_DISPC))
			continue;

		if (!ovl->info.enabled || !ovl->manager)
			continue;

		display = ovl->manager->display;
		if (!display || display->state != OMAP_DSS_DISPLAY_ACTIVE)
			continue;

		num_overlays++;
	}

	set_min_bus_tput(num_overlays);
}

/* DSI powers */
int dss_dsi_power_up(void)
{
	struct omap_dss_board_info *pdata = core.pdev->dev.platform_data;

	if (!pdata->dsi_power_up)
		return 0; /* presume power is always on then */

	return pdata->dsi_power_up();
}

void dss_dsi_power_down(void)
{
	struct omap_dss_board_info *pdata = core.pdev->dev.platform_data;

	if (!pdata->dsi_power_down)
		return;

	pdata->dsi_power_down();
}

const char *dss_get_def_disp_name(void)
{
	return def_disp_name ? def_disp_name : "";
}



/* PLATFORM DEVICE */
static int omap_dss_probe(struct platform_device *pdev)
{
	int skip_init = 0;
	int r;

	core.pdev = pdev;

	r = dss_get_clocks();
	if (r)
		goto fail0;

	dss_clk_enable_all_no_ctx();

	core.ctx_id = dss_get_ctx_id();
	DSSDBG("initial ctx id %u\n", core.ctx_id);

#ifdef CONFIG_FB_OMAP_BOOTLOADER_INIT
	/* DISPC_CONTROL */
	if (omap_readl(0x48050440) & 1)	/* LCD enabled? */
		skip_init = 1;
#endif

	r = dss_init(skip_init);
	if (r) {
		DSSERR("Failed to initialize DSS\n");
		goto fail0;
	}

#ifdef CONFIG_OMAP2_DSS_RFBI
	r = rfbi_init();
	if (r) {
		DSSERR("Failed to initialize rfbi\n");
		goto fail0;
	}
#endif

	r = dpi_init();
	if (r) {
		DSSERR("Failed to initialize dpi\n");
		goto fail0;
	}

	r = dispc_init();
	if (r) {
		DSSERR("Failed to initialize dispc\n");
		goto fail0;
	}
#ifdef CONFIG_OMAP2_DSS_VENC
	r = venc_init(core.pdev);
	if (r) {
		DSSERR("Failed to initialize venc\n");
		goto fail0;
	}
#endif
	if (cpu_is_omap34xx()) {
#ifdef CONFIG_OMAP2_DSS_SDI
		r = sdi_init(skip_init);
		if (r) {
			DSSERR("Failed to initialize SDI\n");
			goto fail0;
		}
#endif
#ifdef CONFIG_OMAP2_DSS_DSI
		r = dsi_init();
		if (r) {
			DSSERR("Failed to initialize DSI\n");
			goto fail0;
		}
#endif
	}

#if defined(CONFIG_DEBUG_FS) && defined(CONFIG_OMAP2_DSS_DEBUG_SUPPORT)
	r = dss_initialize_debugfs();
	if (r)
		goto fail0;
#endif

	dss_init_displays(pdev);
	dss_init_overlay_managers(pdev);
	dss_init_overlays(pdev);

	dss_clk_disable_all();

	INIT_DELAYED_WORK(&core.bus_tput_work, bus_tput_work_func);

	mutex_init(&core.dss_lock);

	return 0;

	/* XXX fail correctly */
fail0:
	return r;
}

static int omap_dss_remove(struct platform_device *pdev)
{
	struct omap_dss_board_info *pdata = pdev->dev.platform_data;
	int c;

	cancel_delayed_work_sync(&core.bus_tput_work);
	if (pdata->set_min_bus_tput)
		pdata->set_min_bus_tput(&core.pdev->dev, OCP_INITIATOR_AGENT, 0);

	dss_uninit_overlays(pdev);
	dss_uninit_overlay_managers(pdev);
	dss_uninit_displays(pdev);

#if defined(CONFIG_DEBUG_FS) && defined(CONFIG_OMAP2_DSS_DEBUG_SUPPORT)
	dss_uninitialize_debugfs();
#endif

#ifdef CONFIG_OMAP2_DSS_VENC
	venc_exit();
#endif
	dispc_exit();
	dpi_exit();
#ifdef CONFIG_OMAP2_DSS_RFBI
	rfbi_exit();
#endif
	if (cpu_is_omap34xx()) {
#ifdef CONFIG_OMAP2_DSS_DSI
		dsi_exit();
#endif
#ifdef CONFIG_OMAP2_DSS_SDI
		sdi_exit();
#endif
	}

	dss_exit();

	/* these should be removed at some point */
	c = core.dss_ick->usecount;
	if (c > 0) {
		DSSERR("warning: dss_ick usecount %d, disabling\n", c);
		while (c-- > 0)
			clk_disable(core.dss_ick);
	}

	c = core.dss1_fck->usecount;
	if (c > 0) {
		DSSERR("warning: dss1_fck usecount %d, disabling\n", c);
		while (c-- > 0)
			clk_disable(core.dss1_fck);
	}

	c = core.dss2_fck->usecount;
	if (c > 0) {
		DSSERR("warning: dss2_fck usecount %d, disabling\n", c);
		while (c-- > 0)
			clk_disable(core.dss2_fck);
	}

	c = core.dss_54m_fck->usecount;
	if (c > 0) {
		DSSERR("warning: dss_54m_fck usecount %d, disabling\n", c);
		while (c-- > 0)
			clk_disable(core.dss_54m_fck);
	}

	if (core.dss_96m_fck) {
		c = core.dss_96m_fck->usecount;
		if (c > 0) {
			DSSERR("warning: dss_96m_fck usecount %d, disabling\n",
					c);
			while (c-- > 0)
				clk_disable(core.dss_96m_fck);
		}
	}

	dss_put_clocks();

	return 0;
}

static void omap_dss_shutdown(struct platform_device *pdev)
{
	DSSDBG("shutdown\n");
}

static int omap_dss_suspend(struct platform_device *pdev, pm_message_t state)
{
	int ret;

	DSSDBG("suspend %d\n", state.event);

	omap_dss_lock();
	ret = dss_suspend_all_displays();
	omap_dss_unlock();

	return ret;

}

static int omap_dss_resume(struct platform_device *pdev)
{
	int ret;

	DSSDBG("resume\n");

	omap_dss_lock();
	ret =  dss_resume_all_displays();
	omap_dss_unlock();

	return ret;
}

static struct platform_driver omap_dss_driver = {
	.probe          = omap_dss_probe,
	.remove         = omap_dss_remove,
	.shutdown	= omap_dss_shutdown,
	.suspend	= omap_dss_suspend,
	.resume		= omap_dss_resume,
	.driver         = {
		.name   = "omapdss",
		.owner  = THIS_MODULE,
	},
};

static int __init omap_dss_init(void)
{
	return platform_driver_register(&omap_dss_driver);
}

static void __exit omap_dss_exit(void)
{
	platform_driver_unregister(&omap_dss_driver);
}

device_initcall(omap_dss_init);
module_exit(omap_dss_exit);


MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@nokia.com>");
MODULE_DESCRIPTION("OMAP2/3 Display Subsystem");
MODULE_LICENSE("GPL v2");

