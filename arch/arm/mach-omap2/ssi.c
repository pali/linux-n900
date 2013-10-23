/*
 * arch/arm/mach-omap2/ssi.c
 *
 * Copyright (C) 2007-2008 Nokia Corporation. All rights reserved.
 *
 * Contact: Carlos Chinea <carlos.chinea@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/jiffies.h>
#include <linux/notifier.h>
#include <mach/clock.h>
#include <mach/ssi.h>
#include <linux/ssi_driver_if.h>
#include "clock.h"
#include <mach/omap-pm.h>
#include "cm.h"
#include "cm-regbits-34xx.h"

#define SSI_RATE_CHANGE		1

/**
 *	struct ssi_internal_clk - Generic virtual ssi clock
 *	@clk: clock data
 *	@nb: notfier block for the DVFS notification chain
 *	@childs: Array of SSI FCK and ICK clocks
 *	@n_childs: Number of clocks in childs array
 *	@rate_change: Tracks if we are in the middle of a clock rate change
 *	@pdev: Reference to the SSI platform device associated to the clock
 *	@drv_nb: Reference to driver nb, use to propagate the DVFS notification
 */
struct ssi_internal_clk {
	struct clk clk;
	struct notifier_block nb;

	struct clk **childs;
	int n_childs;

	unsigned int rate_change:1;

	struct platform_device *pdev;
	struct notifier_block *drv_nb;
};

static struct ssi_internal_clk ssi_clock;

static void ssi_set_mode(struct platform_device *pdev, u32 mode)
{
	struct ssi_platform_data *pdata = pdev->dev.platform_data;
	void __iomem *base = OMAP2_IO_ADDRESS(pdev->resource[0].start);
	int port;

	for (port = 1; port <= pdata->num_ports; port++) {
		outl(mode, base + SSI_SST_MODE_REG(port));
		outl(mode, base + SSI_SSR_MODE_REG(port));
	}
}

static void ssi_save_mode(struct platform_device *pdev)
{
	struct ssi_platform_data *pdata = pdev->dev.platform_data;
	void __iomem *base = OMAP2_IO_ADDRESS(pdev->resource[0].start);
	struct port_ctx *p;
	int port;

	for (port = 1; port <= pdata->num_ports; port++) {
		p = &pdata->ctx.pctx[port - 1];
		p->sst.mode = inl(base + SSI_SST_MODE_REG(port));
		p->ssr.mode = inl(base + SSI_SSR_MODE_REG(port));
	}
}

static void ssi_restore_mode(struct platform_device *pdev)
{
	struct ssi_platform_data *pdata = pdev->dev.platform_data;
	void __iomem *base = OMAP2_IO_ADDRESS(pdev->resource[0].start);
	struct port_ctx *p;
	int port;

	for (port = 1; port <= pdata->num_ports; port++) {
		p = &pdata->ctx.pctx[port - 1];
		outl(p->sst.mode, base + SSI_SST_MODE_REG(port));
		outl(p->ssr.mode, base + SSI_SSR_MODE_REG(port));
	}
}

static int ssi_clk_event(struct notifier_block *nb, unsigned long event,
								void *data)
{
	struct ssi_internal_clk *ssi_clk =
				container_of(nb, struct ssi_internal_clk, nb);
	switch (event) {
	case CLK_PRE_RATE_CHANGE:
		ssi_clk->drv_nb->notifier_call(ssi_clk->drv_nb, event, data);
		ssi_clk->rate_change = 1;
		if (ssi_clk->clk.usecount > 0) {
			ssi_save_mode(ssi_clk->pdev);
			ssi_set_mode(ssi_clk->pdev, SSI_MODE_SLEEP);
		}
		break;
	case CLK_ABORT_RATE_CHANGE:
	case CLK_POST_RATE_CHANGE:
		if ((ssi_clk->clk.usecount > 0) && (ssi_clk->rate_change))
			ssi_restore_mode(ssi_clk->pdev);

		ssi_clk->rate_change = 0;
		ssi_clk->drv_nb->notifier_call(ssi_clk->drv_nb, event, data);
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}

static int ssi_clk_notifier_register(struct clk *clk, struct notifier_block *nb)
{
	struct ssi_internal_clk *ssi_clk;

	if (!clk || !nb)
		return -EINVAL;

	ssi_clk = container_of(clk, struct ssi_internal_clk, clk);
	ssi_clk->drv_nb = nb;
	ssi_clk->nb.priority = nb->priority;
	/* NOTE: We only want notifications from the functional clock */
	return clk_notifier_register(ssi_clk->childs[1], &ssi_clk->nb);
}

static int ssi_clk_notifier_unregister(struct clk *clk,
						struct notifier_block *nb)
{
	struct ssi_internal_clk *ssi_clk;

	if (!clk || !nb)
		return -EINVAL;

	ssi_clk = container_of(clk, struct ssi_internal_clk, clk);
	ssi_clk->drv_nb = NULL;
	return clk_notifier_unregister(ssi_clk->childs[1], &ssi_clk->nb);
}

static void ssi_save_ctx(struct platform_device *pdev)
{
	struct ssi_platform_data *pdata = pdev->dev.platform_data;
	void __iomem *base = OMAP2_IO_ADDRESS(pdev->resource[0].start);
	struct port_ctx *p;
	int port;

	pdata->ctx.loss_count =
			omap_pm_get_dev_context_loss_count(&pdev->dev);

	pdata->ctx.sysconfig = inl(base + SSI_SYS_SYSCONFIG_REG);
	pdata->ctx.gdd_gcr = inl(base + SSI_GDD_GCR_REG);
	for (port = 1; port <= pdata->num_ports; port++) {
		p = &pdata->ctx.pctx[port - 1];
		p->sys_mpu_enable[0] = inl(base +
					SSI_SYS_MPU_ENABLE_REG(port, 0));
		p->sys_mpu_enable[1] = inl(base +
					SSI_SYS_MPU_ENABLE_REG(port, 1));
		p->sst.frame_size = inl(base +
						SSI_SST_FRAMESIZE_REG(port));
		p->sst.divisor = inl(base + SSI_SST_DIVISOR_REG(port));
		p->sst.channels = inl(base + SSI_SST_CHANNELS_REG(port));
		p->sst.arb_mode = inl(base + SSI_SST_ARBMODE_REG(port));
		p->ssr.frame_size = inl(base +
						SSI_SSR_FRAMESIZE_REG(port));
		p->ssr.timeout = inl(base + SSI_SSR_TIMEOUT_REG(port));
		p->ssr.channels = inl(base + SSI_SSR_CHANNELS_REG(port));
	}
}

static void ssi_restore_ctx(struct platform_device *pdev)
{
	struct ssi_platform_data *pdata = pdev->dev.platform_data;
	void __iomem *base = OMAP2_IO_ADDRESS(pdev->resource[0].start);
	struct port_ctx *p;
	int port;
	int loss_count;


	loss_count = omap_pm_get_dev_context_loss_count(&pdev->dev);
#if 0
	if (loss_count == pdata->ctx.loss_count)
		return;
#endif
	outl(pdata->ctx.sysconfig, base + SSI_SYS_SYSCONFIG_REG);
	outl(pdata->ctx.gdd_gcr, base + SSI_GDD_GCR_REG);
	for (port = 1; port <= pdata->num_ports; port++) {
		p = &pdata->ctx.pctx[port - 1];
		outl(p->sys_mpu_enable[0], base +
					SSI_SYS_MPU_ENABLE_REG(port, 0));
		outl(p->sys_mpu_enable[1], base +
					SSI_SYS_MPU_ENABLE_REG(port, 1));
		outl(p->sst.frame_size, base +
						SSI_SST_FRAMESIZE_REG(port));
		outl(p->sst.divisor, base + SSI_SST_DIVISOR_REG(port));
		outl(p->sst.channels, base + SSI_SST_CHANNELS_REG(port));
		outl(p->sst.arb_mode, base + SSI_SST_ARBMODE_REG(port));
		outl(p->ssr.frame_size, base +
						SSI_SSR_FRAMESIZE_REG(port));
		outl(p->ssr.timeout, base + SSI_SSR_TIMEOUT_REG(port));
		outl(p->ssr.channels, base + SSI_SSR_CHANNELS_REG(port));
	}
}

static void ssi_pdev_release(struct device *dev)
{
}

/*
 * NOTE: We abuse a little bit the struct port_ctx to use it also for
 * initialization.
 */
static struct port_ctx ssi_port_ctx[] = {
	[0] = {
		.sst.mode = SSI_MODE_FRAME,
		.sst.frame_size = SSI_FRAMESIZE_DEFAULT,
		.sst.divisor = 1,
		.sst.channels = SSI_CHANNELS_DEFAULT,
		.sst.arb_mode = SSI_ARBMODE_ROUNDROBIN,
		.ssr.mode = SSI_MODE_FRAME,
		.ssr.frame_size = SSI_FRAMESIZE_DEFAULT,
		.ssr.channels = SSI_CHANNELS_DEFAULT,
		.ssr.timeout = SSI_TIMEOUT_DEFAULT,
		},
};

static struct ssi_platform_data ssi_pdata = {
	.num_ports = ARRAY_SIZE(ssi_port_ctx),
	.ctx.pctx = ssi_port_ctx,
	.clk_notifier_register = ssi_clk_notifier_register,
	.clk_notifier_unregister = ssi_clk_notifier_unregister,
};

static struct resource ssi_resources[] = {
	[0] =	{
		.start = 0x48058000,
		.end = 0x4805bbff,
		.name = "omap_ssi_iomem",
		.flags = IORESOURCE_MEM,
		},
	[1] =	{
		.start = 67,
		.end = 67,
		.name = "ssi_p1_mpu_irq0",
		.flags = IORESOURCE_IRQ,
		},
	[2] =	{
		.start = 69,
		.end = 69,
		.name = "ssi_p1_mpu_irq1",
		.flags = IORESOURCE_IRQ,
		},
	[3] =	{
		.start = 68,
		.end = 68,
		.name = "ssi_p2_mpu_irq0",
		.flags = IORESOURCE_IRQ,
		},
	[4] =	{
		.start = 70,
		.end = 70,
		.name = "ssi_p2_mpu_irq1",
		.flags = IORESOURCE_IRQ,
		},
	[5] =	{
		.start = 71,
		.end = 71,
		.name = "ssi_gdd",
		.flags = IORESOURCE_IRQ,
		},
	[6] =	{
		.start = 151,
		.end = 0,
		.name = "ssi_p1_cawake_gpio",
		.flags = IORESOURCE_IRQ | IORESOURCE_UNSET,
		},
	[7] =	{
		.start = 0,
		.end = 0,
		.name = "ssi_p2_cawake_gpio",
		.flags = IORESOURCE_IRQ | IORESOURCE_UNSET,
		},
};

static struct platform_device ssi_pdev = {
	.name = "omap_ssi",
	.id = -1,
	.num_resources = ARRAY_SIZE(ssi_resources),
	.resource = ssi_resources,
	.dev =	{
		.release = ssi_pdev_release,
		.platform_data = &ssi_pdata,
		},
};

#define __SSI_CLK_FIX__
#ifdef __SSI_CLK_FIX__
/*
 * FIXME: TO BE REMOVED.
 * This hack allows us to ensure that clocks are stable before accessing
 * SSI controller registers. To be removed when PM functionalty is in place.
 */
static int check_ssi_active(void)
{
	u32 reg;
	unsigned long dl = jiffies + msecs_to_jiffies(500);
	void __iomem *cm_idlest1 = OMAP2_IO_ADDRESS(0x48004a20);

	reg = inl(cm_idlest1);
	while ((!(reg & 0x01)) && (time_before(jiffies, dl)))
		reg = inl(cm_idlest1);

	if (!(reg & 0x01)) { /* ST_SSI */
		pr_err("SSI is still in STANDBY ! (BUG !?)\n");
		return -1;
	}

	return 0;
}
#endif /* __SSI_CLK_FIX__ */

static int ssi_clk_init(struct ssi_internal_clk *ssi_clk)
{
	const char *clk_names[] = { "ssi_ick", "ssi_ssr_fck" };
	int i;
	int j;

	ssi_clk->n_childs = ARRAY_SIZE(clk_names);
	ssi_clk->childs = kzalloc(ssi_clk->n_childs * sizeof(*ssi_clk->childs),
								GFP_KERNEL);
	if (!ssi_clk->childs)
		return -ENOMEM;

	for (i = 0; i < ssi_clk->n_childs; i++) {
		ssi_clk->childs[i] = clk_get(&ssi_clk->pdev->dev, clk_names[i]);
		if (IS_ERR(ssi_clk->childs[i])) {
			pr_err("Unable to get SSI clock: %s", clk_names[i]);
			for (j = i - 1; j >= 0; j--)
				clk_put(ssi_clk->childs[j]);
			return -ENODEV;
		}
	}

	return 0;
}

static void disable_dpll3_autoidle(void)
{
	u32 v;

	v = cm_read_mod_reg(PLL_MOD, CM_AUTOIDLE);
	v &= ~0x7;
	cm_write_mod_reg(v, PLL_MOD, CM_AUTOIDLE);
}

static void enable_dpll3_autoidle(void)
{
	u32 v;

	v = cm_read_mod_reg(PLL_MOD, CM_AUTOIDLE);
	v |= 0;
	cm_write_mod_reg(v, PLL_MOD, CM_AUTOIDLE);
}

static int ssi_clk_enable(struct clk *clk)
{
	struct ssi_internal_clk *ssi_clk =
				container_of(clk, struct ssi_internal_clk, clk);
	int err;
	int i;

	disable_dpll3_autoidle();

	for (i = 0; i < ssi_clk->n_childs; i++) {
		err = omap2_clk_enable(ssi_clk->childs[i]);
		if (unlikely(err < 0))
			goto rollback;
	}
#ifdef __SSI_CLK_FIX__
	/*
	 * FIXME: To be removed
	 * Wait until the SSI controller has the clocks stable
	 */
	check_ssi_active();
#endif
	ssi_restore_ctx(ssi_clk->pdev);
	if (!ssi_clk->rate_change)
		ssi_restore_mode(ssi_clk->pdev);

	return 0;
rollback:
	pr_err("Error on SSI clk child %d\n", i);
	for (i = i - 1; i >= 0; i--)
		omap2_clk_disable(ssi_clk->childs[i]);

	enable_dpll3_autoidle();

	return err;
}

static void ssi_clk_disable(struct clk *clk)
{
	struct ssi_internal_clk *ssi_clk =
				container_of(clk, struct ssi_internal_clk, clk);
	int i;

	if (!ssi_clk->rate_change) {
		ssi_save_mode(ssi_clk->pdev);
		ssi_set_mode(ssi_clk->pdev, SSI_MODE_SLEEP);
	}
	/* Save ctx in all ports */
	ssi_save_ctx(ssi_clk->pdev);

	for (i = 0; i < ssi_clk->n_childs; i++)
		omap2_clk_disable(ssi_clk->childs[i]);

	enable_dpll3_autoidle();

}

int omap_ssi_config(struct omap_ssi_board_config *ssi_config)
{
	int port;
	int cawake_gpio;

	ssi_pdata.num_ports = ssi_config->num_ports;
	for (port = 0; port < ssi_config->num_ports; port++) {
		cawake_gpio = ssi_config->cawake_gpio[port];
		if (cawake_gpio < 0)
			continue;	/* Nothing to do */

		if (gpio_request(cawake_gpio, "CAWAKE") < 0) {
			dev_err(&ssi_pdev.dev, "FAILED to request CAWAKE"
						" GPIO %d\n", cawake_gpio);
			return -EBUSY;
		}

		gpio_direction_input(cawake_gpio);
		ssi_resources[6 + port].start = gpio_to_irq(cawake_gpio);
		ssi_resources[6 + port].flags &= ~IORESOURCE_UNSET;
		ssi_resources[6 + port].flags |= IORESOURCE_IRQ_HIGHEDGE |
							IORESOURCE_IRQ_LOWEDGE;
	}
	return 0;
}

static struct ssi_internal_clk ssi_clock = {
	.clk = {
		.name = "ssi_clk",
		.id = -1,
		.enable = ssi_clk_enable,
		.disable = ssi_clk_disable,
		.clkdm = { .name = "core_l4_clkdm", },
	},
	.nb = {
		.notifier_call = ssi_clk_event,
		.priority = INT_MAX,
	},
	.pdev = &ssi_pdev,
};

static int __init omap_ssi_init(void)
{
	int err;

	ssi_clk_init(&ssi_clock);
	clk_register(&ssi_clock.clk);

	err = platform_device_register(&ssi_pdev);
	if (err < 0) {
		pr_err("Unable to register SSI platform device: %d\n", err);
		return err;
	}

	return 0;
}
subsys_initcall(omap_ssi_init);
