/*
 * omap iommu wrapper for TI's OMAP3430 Camera ISP
 *
 * Copyright (C) 2008--2009 Nokia.
 *
 * Contributors:
 *	Hiroshi Doyu <hiroshi.doyu@nokia.com>
 *	Sakari Ailus <sakari.ailus@nokia.com>
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef OMAP_ISP_MMU_H
#define OMAP_ISP_MMU_H

#include <linux/err.h>
#include <linux/scatterlist.h>

dma_addr_t ispmmu_vmalloc(size_t bytes);
void ispmmu_vfree(const dma_addr_t da);
dma_addr_t ispmmu_kmap(u32 pa, int size);
void ispmmu_kunmap(dma_addr_t da);
dma_addr_t ispmmu_vmap(const struct scatterlist *sglist, int sglen);
void ispmmu_vunmap(dma_addr_t da);
void ispmmu_save_context(void);
void ispmmu_restore_context(void);
int ispmmu_init(void);
void ispmmu_cleanup(void);

#endif /* OMAP_ISP_MMU_H */
