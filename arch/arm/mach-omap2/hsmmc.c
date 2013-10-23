/*
 * linux/arch/arm/mach-omap2/hsmmc.c
 *
 * Copyright (C) 2007-2008 Texas Instruments
 * Copyright (C) 2008 Nokia Corporation
 * Author: Texas Instruments
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/clk.h>

#include <linux/mmc/card.h>

#include <mach/hardware.h>

#include <plat/control.h>
#include <plat/clock.h>
#include <plat/mmc.h>
#include <plat/omap-pm.h>

#include "hsmmc.h"

#define HSMMC_MAX_FREQ	48000000

#if defined(CONFIG_MMC_OMAP_HS) || defined(CONFIG_MMC_OMAP_HS_MODULE)

static u16 control_pbias_offset;
static u16 control_devconf1_offset;

#define HSMMC_NAME_LEN	9

static struct hsmmc_controller {
	char				name[HSMMC_NAME_LEN + 1];
} hsmmc[OMAP34XX_NR_MMC];

#if defined(CONFIG_ARCH_OMAP3) && defined(CONFIG_PM)

static int hsmmc_get_context_loss(struct device *dev)
{
	return omap_pm_get_dev_context_loss_count(dev);
}

/*
 * This handler can be used for setting other DVFS/PM constraints:
 * intr latency, wakeup latency, DMA start latency, bus throughput
 * according to API in mach/omap-pm.h
 */
static void hsmmc_set_pm_constraints(struct device *dev, int on)
{
	pr_debug("HSMMC: Set pm constraints: %s\n", on ? "on" : "off");
	omap_pm_set_max_mpu_wakeup_lat(dev, on ? 100 : -1);
	omap_pm_set_max_sdma_lat(dev, on ? 100 : -1);
}
#else
#define hsmmc_get_context_loss NULL
#define hsmmc_set_pm_constraints NULL
#endif

static void hsmmc1_before_set_reg(struct device *dev, int slot,
				  int power_on, int vdd)
{
	u32 reg, prog_io;
	struct omap_mmc_platform_data *mmc = dev->platform_data;

	if (mmc->slots[0].remux)
		mmc->slots[0].remux(dev, slot, power_on);

	/*
	 * Assume we power both OMAP VMMC1 (for CMD, CLK, DAT0..3) and the
	 * card with Vcc regulator (from twl4030 or whatever).  OMAP has both
	 * 1.8V and 3.0V modes, controlled by the PBIAS register.
	 *
	 * In 8-bit modes, OMAP VMMC1A (for DAT4..7) needs a supply, which
	 * is most naturally TWL VSIM; those pins also use PBIAS.
	 *
	 * FIXME handle VMMC1A as needed ...
	 */
	if (power_on) {
		if (cpu_is_omap2430()) {
			reg = omap_ctrl_readl(OMAP243X_CONTROL_DEVCONF1);
			if ((1 << vdd) >= MMC_VDD_30_31)
				reg |= OMAP243X_MMC1_ACTIVE_OVERWRITE;
			else
				reg &= ~OMAP243X_MMC1_ACTIVE_OVERWRITE;
			omap_ctrl_writel(reg, OMAP243X_CONTROL_DEVCONF1);
		}

		if (mmc->slots[0].internal_clock) {
			reg = omap_ctrl_readl(OMAP2_CONTROL_DEVCONF0);
			reg |= OMAP2_MMCSDIO1ADPCLKISEL;
			omap_ctrl_writel(reg, OMAP2_CONTROL_DEVCONF0);
		}

		reg = omap_ctrl_readl(control_pbias_offset);
		if (cpu_is_omap3630()) {
			/* Set MMC I/O to 52Mhz */
			prog_io = omap_ctrl_readl(OMAP343X_CONTROL_PROG_IO1);
			prog_io |= OMAP3630_PRG_SDMMC1_SPEEDCTRL;
			omap_ctrl_writel(prog_io, OMAP343X_CONTROL_PROG_IO1);
		} else {
			reg |= OMAP2_PBIASSPEEDCTRL0;
		}
		reg &= ~OMAP2_PBIASLITEPWRDNZ0;
		omap_ctrl_writel(reg, control_pbias_offset);
	} else {
		reg = omap_ctrl_readl(control_pbias_offset);
		reg &= ~OMAP2_PBIASLITEPWRDNZ0;
		omap_ctrl_writel(reg, control_pbias_offset);
	}
}

static void hsmmc1_after_set_reg(struct device *dev, int slot,
				 int power_on, int vdd)
{
	u32 reg;

	/* 100ms delay required for PBIAS configuration */
	msleep(100);

	if (power_on) {
		reg = omap_ctrl_readl(control_pbias_offset);
		reg |= (OMAP2_PBIASLITEPWRDNZ0 | OMAP2_PBIASSPEEDCTRL0);
		if ((1 << vdd) <= MMC_VDD_165_195)
			reg &= ~OMAP2_PBIASLITEVMODE0;
		else
			reg |= OMAP2_PBIASLITEVMODE0;
		omap_ctrl_writel(reg, control_pbias_offset);
	} else {
		reg = omap_ctrl_readl(control_pbias_offset);
		reg |= (OMAP2_PBIASSPEEDCTRL0 | OMAP2_PBIASLITEPWRDNZ0 |
			OMAP2_PBIASLITEVMODE0);
		omap_ctrl_writel(reg, control_pbias_offset);
	}
}

static void hsmmc23_before_set_reg(struct device *dev, int slot,
				   int power_on, int vdd)
{
	struct omap_mmc_platform_data *mmc = dev->platform_data;

	if (mmc->slots[0].remux)
		mmc->slots[0].remux(dev, slot, power_on);

	if (power_on) {
		/* Only MMC2 supports a CLKIN */
		if (mmc->slots[0].internal_clock) {
			u32 reg;

			reg = omap_ctrl_readl(control_devconf1_offset);
			reg |= OMAP2_MMCSDIO2ADPCLKISEL;
			omap_ctrl_writel(reg, control_devconf1_offset);
		}
	}
}

#ifdef CONFIG_ARCH_OMAP3430
static struct hsmmc_max_freq_info {
	struct device *dev;
	int freq;
	int high_speed;
} hsmmc_max_freq_info[OMAP34XX_NR_MMC];

static unsigned int hsmmc_max_freq = HSMMC_MAX_FREQ;
static DEFINE_SPINLOCK(hsmmc_max_freq_lock);

static DECLARE_WAIT_QUEUE_HEAD(hsmmc_max_freq_wq);

static int hsmmc_high_speed(struct device *dev)
{
	void *drvdata = platform_get_drvdata(to_platform_device(dev));
	struct mmc_host *mmc = container_of(drvdata, struct mmc_host, private);

	return mmc->card ? mmc_card_highspeed(mmc->card) : 0;
}

static unsigned int hsmmc_get_max_freq_hs(struct device *dev, int high_speed)
{
	return high_speed ? hsmmc_max_freq : hsmmc_max_freq >> 1;
}

static unsigned int hsmmc_get_max_freq(struct device *dev)
{
	return hsmmc_get_max_freq_hs(dev, hsmmc_high_speed(dev));
}

static unsigned int hsmmc_active(struct device *dev, unsigned int target_freq)
{
	int high_speed = hsmmc_high_speed(dev);
	int i;
	unsigned int max_freq, freq;
	unsigned long flags;

	spin_lock_irqsave(&hsmmc_max_freq_lock, flags);
	max_freq = hsmmc_get_max_freq_hs(dev, high_speed);
	freq = min(target_freq, max_freq);
	for (i = 0; i < ARRAY_SIZE(hsmmc_max_freq_info); i++) {
		if (!hsmmc_max_freq_info[i].dev) {
			hsmmc_max_freq_info[i].dev = dev;
			hsmmc_max_freq_info[i].freq = freq;
			hsmmc_max_freq_info[i].high_speed = high_speed;
			break;
		}
	}
	spin_unlock_irqrestore(&hsmmc_max_freq_lock, flags);
	return freq;
}

static void hsmmc_inactive(struct device *dev)
{
	int i;
	unsigned long flags;

	spin_lock_irqsave(&hsmmc_max_freq_lock, flags);
	for (i = 0; i < ARRAY_SIZE(hsmmc_max_freq_info); i++) {
		if (hsmmc_max_freq_info[i].dev == dev) {
			hsmmc_max_freq_info[i].dev = NULL;
			spin_unlock_irqrestore(&hsmmc_max_freq_lock, flags);
			/*
			 * Wake up the queue only in case we deactivated a
			 * device.
			 */
			wake_up(&hsmmc_max_freq_wq);
			return;
		}
	}
	spin_unlock_irqrestore(&hsmmc_max_freq_lock, flags);
}

static bool hsmmc_max_freq_ok(void)
{
	int i;
	bool ret = true;
	unsigned long flags;

	spin_lock_irqsave(&hsmmc_max_freq_lock, flags);
	for (i = 0; i < ARRAY_SIZE(hsmmc_max_freq_info); i++) {
		if (hsmmc_max_freq_info[i].dev) {
			unsigned int max_freq;

			if (hsmmc_max_freq_info[i].high_speed)
				max_freq = HSMMC_MAX_FREQ >> 1;
			else
				max_freq = HSMMC_MAX_FREQ >> 2;

			if (hsmmc_max_freq_info[i].freq > max_freq) {
				ret = false;
				break;
			}
		}
	}
	spin_unlock_irqrestore(&hsmmc_max_freq_lock, flags);
	return ret;
}

static int hsmmc_clk_notifier(struct notifier_block *nb, unsigned long event,
			      void *data)
{
	struct clk_notifier_data *clk_data = data;
	unsigned int threshold = 75000000;	/* L4 clock */

	switch (event) {
	case CLK_PRE_RATE_CHANGE:
		pr_debug("pre rate change\n");
		if (clk_data->rate > threshold) {	/* opp100 -> opp50 */
			hsmmc_max_freq = HSMMC_MAX_FREQ >> 1;

			/* Timeout is 1 sec */
			if (!wait_event_timeout(hsmmc_max_freq_wq,
						hsmmc_max_freq_ok(),
						msecs_to_jiffies(1000)))
				pr_err("MMC violates maximum frequency "
				       "constraint\n");
		}
		break;
	case CLK_ABORT_RATE_CHANGE:
		pr_debug("abort rate change\n");
		if (clk_data->rate < threshold)		/* opp100 -> opp50 */
			hsmmc_max_freq = HSMMC_MAX_FREQ;
		break;
	case CLK_POST_RATE_CHANGE:
		pr_debug("post rate change\n");
		if (clk_data->rate > threshold)		/* opp50 -> opp100 */
			hsmmc_max_freq = HSMMC_MAX_FREQ;
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block hsmmc_notifier_block = {
	.notifier_call	= hsmmc_clk_notifier,
	.priority	= INT_MAX,
};

static struct clk *hsmmc_core_l4_ick;

static int __init hsmmc_init_notifier(struct omap_mmc_platform_data **mmc_data,
				      int nr_controllers)
{
	int err, i;

	/* Note the clk and registered notifier are kept forever */
	hsmmc_core_l4_ick = clk_get(NULL, "core_l4_ick");
	if (IS_ERR(hsmmc_core_l4_ick))
		return PTR_ERR(hsmmc_core_l4_ick);

	err = clk_notifier_register(hsmmc_core_l4_ick, &hsmmc_notifier_block);
	if (err)
		return err;

	for (i = 0; i < nr_controllers; i++) {
		struct omap_mmc_platform_data *mmc = mmc_data[i];

		if (!mmc)
			continue;

		mmc->get_max_freq = hsmmc_get_max_freq;
		mmc->active = hsmmc_active;
		mmc->inactive = hsmmc_inactive;
	}

	return 0;
}
#else
static inline int hsmmc_init_notifier(struct omap_mmc_platform_data **mmc_data,
				      int nr_controllers)
{
	return 0;
}
#endif

static struct omap_mmc_platform_data *hsmmc_data[OMAP34XX_NR_MMC] __initdata;

void __init omap2_hsmmc_init(struct omap2_hsmmc_info *controllers)
{
	struct omap2_hsmmc_info *c;
	int nr_hsmmc = ARRAY_SIZE(hsmmc_data);
	int i;

	if (cpu_is_omap2430()) {
		control_pbias_offset = OMAP243X_CONTROL_PBIAS_LITE;
		control_devconf1_offset = OMAP243X_CONTROL_DEVCONF1;
	} else {
		control_pbias_offset = OMAP343X_CONTROL_PBIAS_LITE;
		control_devconf1_offset = OMAP343X_CONTROL_DEVCONF1;
	}

	for (c = controllers; c->mmc; c++) {
		struct hsmmc_controller *hc = hsmmc + c->mmc - 1;
		struct omap_mmc_platform_data *mmc = hsmmc_data[c->mmc - 1];

		if (!c->mmc || c->mmc > nr_hsmmc) {
			pr_debug("MMC%d: no such controller\n", c->mmc);
			continue;
		}
		if (mmc) {
			pr_debug("MMC%d: already configured\n", c->mmc);
			continue;
		}

		mmc = kzalloc(sizeof(struct omap_mmc_platform_data),
			      GFP_KERNEL);
		if (!mmc) {
			pr_err("Cannot allocate memory for mmc device!\n");
			goto done;
		}

		if (c->name)
			strncpy(hc->name, c->name, HSMMC_NAME_LEN);
		else
			snprintf(hc->name, ARRAY_SIZE(hc->name),
				"mmc%islot%i", c->mmc, 1);
		mmc->slots[0].name = hc->name;
		mmc->nr_slots = 1;
		mmc->slots[0].wires = c->wires;
		mmc->slots[0].internal_clock = !c->ext_clock;
		mmc->dma_mask = 0xffffffff;

		mmc->get_context_loss_count = hsmmc_get_context_loss;
		mmc->set_pm_constraints = hsmmc_set_pm_constraints;

		mmc->slots[0].switch_pin = c->gpio_cd;
		mmc->slots[0].gpio_wp = c->gpio_wp;

		if (c->hw_reset_connected)
			mmc->slots[0].gpio_hw_reset = c->gpio_hw_reset;
		else
			mmc->slots[0].gpio_hw_reset = -EINVAL;

		mmc->slots[0].nomux = c->nomux;
		mmc->slots[0].remux = c->remux;

		if (c->cover_only)
			mmc->slots[0].cover = 1;

		if (c->nonremovable)
			mmc->slots[0].nonremovable = 1;

		if (c->mmc_only)
			mmc->slots[0].mmc_only = 1;

		if (c->power_saving)
			mmc->slots[0].power_saving = 1;

		if (c->no_off)
			mmc->slots[0].no_off = 1;

		if (c->vcc_aux_disable_is_sleep)
			mmc->slots[0].vcc_aux_disable_is_sleep = 1;

		/* NOTE:  MMC slots should have a Vcc regulator set up.
		 * This may be from a TWL4030-family chip, another
		 * controllable regulator, or a fixed supply.
		 *
		 * temporary HACK: ocr_mask instead of fixed supply
		 */
		mmc->slots[0].ocr_mask = c->ocr_mask;

		switch (c->mmc) {
		case 1:
			/* on-chip level shifting via PBIAS0/PBIAS1 */
			mmc->slots[0].before_set_reg = hsmmc1_before_set_reg;
			mmc->slots[0].after_set_reg = hsmmc1_after_set_reg;

			/* Omap3630 HSMMC1 supports only 4-bit */
			if (cpu_is_omap3630() && c->wires > 4) {
				c->wires = 4;
				mmc->slots[0].wires = c->wires;
			}
			break;
		case 2:
			if (c->ext_clock)
				c->transceiver = 1;
			if (c->transceiver && c->wires > 4)
				c->wires = 4;
			/* FALLTHROUGH */
		case 3:
			/* off-chip level shifting, or none */
			mmc->slots[0].before_set_reg = hsmmc23_before_set_reg;
			mmc->slots[0].after_set_reg = NULL;
			break;
		default:
			pr_err("MMC%d configuration not supported!\n", c->mmc);
			kfree(mmc);
			continue;
		}
		hsmmc_data[c->mmc - 1] = mmc;
	}

	/* The HW bug happens only on the following CPU type */
	if (cpu_is_omap3630()) {
		int err;

		err = hsmmc_init_notifier(hsmmc_data, OMAP34XX_NR_MMC);
		if (err) {
			pr_err("Can't setup clock notifier for mmc driver!\n");
			goto done;
		}
	}

	omap2_init_mmc(hsmmc_data, OMAP34XX_NR_MMC);

	/* pass the device nodes back to board setup code */
	for (c = controllers; c->mmc; c++) {
		struct omap_mmc_platform_data *mmc = hsmmc_data[c->mmc - 1];

		if (!c->mmc || c->mmc > nr_hsmmc)
			continue;
		c->dev = mmc->dev;
	}

done:
	for (i = 0; i < nr_hsmmc; i++)
		kfree(hsmmc_data[i]);
}

#endif
