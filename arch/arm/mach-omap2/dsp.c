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

#include <linux/dma-mapping.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>


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

RESERVEDMEM_OF_DECLARE(dsp, "ti,dsp-memsize", rmem_dsp_setup);
