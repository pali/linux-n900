/*
 * linux/arch/arm/mach-omap2/usb-musb.c
 *
 * This file will contain the board specific details for the
 * MENTOR USB OTG controller on OMAP3430
 *
 * Copyright (C) 2007-2008 Texas Instruments
 * Copyright (C) 2008 Nokia Corporation
 * Author: Vikram Pandita
 *
 * Generalization by:
 * Felipe Balbi <felipe.balbi@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>

#include <linux/usb/musb.h>

#include <asm/sizes.h>

#include <mach/hardware.h>
#include <mach/irqs.h>
#include <plat/mux.h>
#include <plat/usb.h>

#define OTG_SYSCONFIG	   0x404
#define OTG_SYSC_SOFTRESET BIT(1)
#define OTG_SYSSTATUS     0x408
#define OTG_SYSS_RESETDONE BIT(0)

static struct platform_device dummy_pdev = {
	.dev = {
		.bus = &platform_bus_type,
	},
};

static void __iomem *otg_base;
static struct clk *otg_clk;

static void __init usb_musb_pm_init(void)
{
	struct device *dev = &dummy_pdev.dev;

	if (!cpu_is_omap34xx())
		return;

	otg_base = ioremap(OMAP34XX_HSUSB_OTG_BASE, SZ_4K);
	if (WARN_ON(!otg_base))
		return;

	/* Do not use SOFTRESET with 3630. See OMAP3630 Errata ID i445. */
	if (cpu_is_omap3630())
		return;

	dev_set_name(dev, "musb_hdrc");
	otg_clk = clk_get(dev, "ick");

	if (otg_clk && clk_enable(otg_clk)) {
		printk(KERN_WARNING
			"%s: Unable to enable clocks for MUSB, "
			"cannot reset.\n",  __func__);
	} else {
		/* Reset OTG controller. After reset, it will be in
		 * force-idle, force-standby mode. */
		__raw_writel(OTG_SYSC_SOFTRESET, otg_base + OTG_SYSCONFIG);

		while (!(OTG_SYSS_RESETDONE &
					__raw_readl(otg_base + OTG_SYSSTATUS)))
			cpu_relax();
	}

	if (otg_clk)
		clk_disable(otg_clk);
}

void usb_musb_disable_autoidle(void)
{
	if (otg_clk) {
		unsigned long reg;

		clk_enable(otg_clk);
		reg = __raw_readl(otg_base + OTG_SYSCONFIG);
		__raw_writel(reg & ~1, otg_base + OTG_SYSCONFIG);
		clk_disable(otg_clk);
	}
}

#ifdef CONFIG_USB_MUSB_SOC

static struct resource musb_resources[] = {
	[0] = { /* start and end set dynamically */
		.flags	= IORESOURCE_MEM,
	},
	[1] = {	/* general IRQ */
		.start	= INT_243X_HS_USB_MC,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {	/* DMA IRQ */
		.start	= INT_243X_HS_USB_DMA,
		.flags	= IORESOURCE_IRQ,
	},
};

/*
 * REVISIT these are optimized for g_nokia and
 * g_file_storage and should be coming from
 * board file. Other patches should come
 * to fix this problem
 */
static struct musb_fifo_cfg musb_fifo_cfg[] = {

	/* f_mtp or mass storage */
	MUSB_EP_FIFO_DOUBLE(1, FIFO_TX, 512),
	MUSB_EP_FIFO_DOUBLE(1, FIFO_RX, 512),
	MUSB_EP_FIFO_SINGLE(2, FIFO_TX, 64),

	/* phonet */
	MUSB_EP_FIFO_DOUBLE(3, FIFO_TX, 512),
	MUSB_EP_FIFO_DOUBLE(2, FIFO_RX, 512),

	/* obex 0 */
	MUSB_EP_FIFO_DOUBLE(4, FIFO_TX, 512),
	MUSB_EP_FIFO_DOUBLE(3, FIFO_RX, 512),

	/* obex 1 */
	MUSB_EP_FIFO_DOUBLE(5, FIFO_TX, 512),
	MUSB_EP_FIFO_DOUBLE(4, FIFO_RX, 512),

	/* acm */
	MUSB_EP_FIFO_DOUBLE(6, FIFO_TX, 512),
	MUSB_EP_FIFO_DOUBLE(5, FIFO_RX, 512),
	MUSB_EP_FIFO_SINGLE(7, FIFO_TX, 64),

	/* ecm */
	MUSB_EP_FIFO_DOUBLE(8, FIFO_TX, 512),
	MUSB_EP_FIFO_DOUBLE(6, FIFO_RX, 512),
	MUSB_EP_FIFO_SINGLE(9, FIFO_TX, 64),

	/* CD-ROM */
	MUSB_EP_FIFO_DOUBLE(10, FIFO_TX, 512),
	MUSB_EP_FIFO_DOUBLE(7, FIFO_RX, 512),

};

static struct musb_hdrc_config musb_config = {
	.fifo_cfg	= musb_fifo_cfg,
	.fifo_cfg_size	= ARRAY_SIZE(musb_fifo_cfg),
	.multipoint	= 1,
	.num_eps	= 16,
	.ram_bits	= 12,
};

static struct musb_hdrc_platform_data musb_plat = {
	.config		= &musb_config,
	/* .power	= DYNAMIC, */
	/* .mode	= DYNAMIC, */
};

static u64 musb_dmamask = DMA_BIT_MASK(32);

static struct platform_device musb_device = {
	.name		= "musb_hdrc",
	.id		= -1,
	.dev = {
		.dma_mask		= &musb_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= &musb_plat,
	},
	.num_resources	= ARRAY_SIZE(musb_resources),
	.resource	= musb_resources,
};

void __init usb_musb_init(struct musb_board_data *board,
				enum musb_mode mode, unsigned power)
{
	if (cpu_is_omap243x())
		musb_resources[0].start = OMAP243X_HS_BASE;
	else
		musb_resources[0].start = OMAP34XX_HSUSB_OTG_BASE;
	musb_resources[0].end = musb_resources[0].start + SZ_4K - 1;

	musb_plat.board = board;
	musb_plat.power = power >> 1;
	musb_plat.mode = mode;

	if (platform_device_register(&musb_device) < 0)
		printk(KERN_ERR "Unable to register HS-USB (MUSB) device\n");

	usb_musb_pm_init();
}

#else
void __init usb_musb_init(struct musb_board_data *board,
				enum musb_mode mode, unsigned power)
{
	usb_musb_pm_init();
}
#endif /* CONFIG_USB_MUSB_SOC */
