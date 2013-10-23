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

#include <mach/display.h>
#include <mach/clock.h>

#include "dss.h"

static struct {
	struct platform_device *pdev;
	unsigned	ctx_id;

	struct clk      *dss_ick;
	struct clk	*dss1_fck;
	struct clk	*dss2_fck;
	struct clk      *dss_54m_fck;
	struct clk	*dss_96m_fck;
	unsigned	num_clks_enabled;
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
static unsigned dss_get_ctx_id(void)
{
	struct omap_dss_board_info *pdata = core.pdev->dev.platform_data;

	if (!pdata->get_last_off_on_transaction_id)
		return 0;

	return pdata->get_last_off_on_transaction_id(&core.pdev->dev);
}

int dss_need_ctx_restore(void)
{
	int id = dss_get_ctx_id();

	if (id != core.ctx_id) {
		DSSDBG("ctx id %u -> id %u\n",
				core.ctx_id, id);
		core.ctx_id = id;
		return 1;
	} else {
		return 0;
	}
}

static void save_all_ctx(void)
{
	DSSDBG("save context\n");

	dss_clk_enable_no_ctx(DSS_CLK_ICK | DSS_CLK_FCK1);

	dss_save_context();
	dispc_save_context();
#ifdef CONFIG_OMAP2_DSS_DSI
	dsi_save_context();
#endif

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
	return 0;
}

static void dss_uninitialize_debugfs(void)
{
	if (dss_debugfs_dir)
		debugfs_remove_recursive(dss_debugfs_dir);
}
#endif /* CONFIG_DEBUG_FS && CONFIG_OMAP2_DSS_DEBUG_SUPPORT */


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
	r = venc_init();
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
	dss_init_overlays(pdev, def_disp_name);

	dss_clk_disable_all();

	return 0;

	/* XXX fail correctly */
fail0:
	return r;
}

static int omap_dss_remove(struct platform_device *pdev)
{
	int c;

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
	DSSDBG("suspend %d\n", state.event);

	return dss_suspend_all_displays();
}

static int omap_dss_resume(struct platform_device *pdev)
{
	DSSDBG("resume\n");

	return dss_resume_all_displays();
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

subsys_initcall(omap_dss_init);
module_exit(omap_dss_exit);


MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@nokia.com>");
MODULE_DESCRIPTION("OMAP2/3 Display Subsystem");
MODULE_LICENSE("GPL v2");

