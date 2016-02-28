/*
 * TI's OMAP DSP platform device registration
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 * Copyright (C) 2009 Nokia Corporation
 *
 * Written by Hiroshi DOYU <Hiroshi.DOYU@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * XXX The function pointers to the PRM/CM functions are incorrect and
 * should be removed.  No device driver should be changing PRM/CM bits
 * directly; that's a layering violation -- those bits are the responsibility
 * of the OMAP PM core code.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/of_reserved_mem.h>

#include "control.h"
#include "cm2xxx_3xxx.h"
#include "prm2xxx_3xxx.h"
#ifdef CONFIG_TIDSPBRIDGE_DVFS
#include "omap-pm.h"
#endif
#include "soc.h"

#include <linux/platform_data/dsp-omap.h>

static void omap_pm_dsp_set_min_opp(u8 opp_id)
{
	return;
}
static u8 omap_pm_dsp_get_opp(void)
{
	return 2;
}

static void omap_pm_cpu_set_freq(unsigned long f)
{
	return;
}

static unsigned long omap_pm_cpu_get_freq(void)
{
	return 250000000;
}

struct omap_dsp_platform_data omap_dsp_pdata = {
#ifdef CONFIG_TIDSPBRIDGE_DVFS
	.dsp_set_min_opp = omap_pm_dsp_set_min_opp,
	.dsp_get_opp = omap_pm_dsp_get_opp,
	.cpu_set_freq = omap_pm_cpu_set_freq,
	.cpu_get_freq = omap_pm_cpu_get_freq,
#endif
	.dsp_prm_read = omap2_prm_read_mod_reg,
	.dsp_prm_write = omap2_prm_write_mod_reg,
	.dsp_prm_rmw_bits = omap2_prm_rmw_mod_reg_bits,
	.dsp_cm_read = omap2_cm_read_mod_reg,
	.dsp_cm_write = omap2_cm_write_mod_reg,
	.dsp_cm_rmw_bits = omap2_cm_rmw_mod_reg_bits,

	.set_bootaddr = omap_ctrl_write_dsp_boot_addr,
	.set_bootmode = omap_ctrl_write_dsp_boot_mode,
};

static int rmem_dsp_device_init(struct reserved_mem *rmem, struct device *dev)
{
	int dma;

	if (rmem->priv)
		return 0;

	dma = dma_declare_coherent_memory(dev, rmem->base,
					  rmem->base, rmem->size,
					  DMA_MEMORY_MAP |
					  DMA_MEMORY_EXCLUSIVE);
	if (!(dma & DMA_MEMORY_MAP)) {
			pr_err("dsp: dma_declare_coherent_memory failed\n");
			return -ENOMEM;
	}
	else
		rmem->priv = dev->dma_mem;

	return 0;
}

static void rmem_dsp_device_release(struct reserved_mem *rmem,
				    struct device *dev)
{
	dma_release_declared_memory(dev);
	rmem->priv = 0;
}

static const struct reserved_mem_ops rmem_dsp_ops = {
	.device_init    = rmem_dsp_device_init,
	.device_release = rmem_dsp_device_release,
};

static int __init rmem_dsp_setup(struct reserved_mem *rmem)
{
	pr_info("dsp: reserved %d bytes at %pa\n", rmem->size, &rmem->base);
	rmem->ops = &rmem_dsp_ops;

	return 0;
}

RESERVEDMEM_OF_DECLARE(dss, "ti,dsp-memsize", rmem_dsp_setup);

MODULE_AUTHOR("Hiroshi DOYU");
MODULE_DESCRIPTION("TI's OMAP DSP platform device registration");
MODULE_LICENSE("GPL");
