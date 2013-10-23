/*
 * mem.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Implementation of platform specific memory services.
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

/*  ----------------------------------- Host OS */
#include <dspbridge/host_os.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>
#include <dspbridge/dbdefs.h>
#include <dspbridge/errbase.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>

/*  ----------------------------------- This */
#include <dspbridge/mem.h>
#include <dspbridge/list.h>

/*  ----------------------------------- Defines */
#define MEM512MB   0x1fffffff

/*  ----------------------------------- Globals */
static bool ext_phys_mem_pool_enabled;

struct ext_phys_mem_pool {
	u32 phys_mem_base;
	u32 phys_mem_size;
	u32 virt_mem_base;
	u32 next_phys_alloc_ptr;
};

static struct ext_phys_mem_pool ext_mem_pool;

void mem_ext_phys_pool_init(u32 poolPhysBase, u32 poolSize)
{
	u32 pool_virt_base;

	/* get the virtual address for the physical memory pool passed */
	pool_virt_base = (u32) ioremap(poolPhysBase, poolSize);

	if ((void **)pool_virt_base == NULL) {
		pr_err("%s: external physical memory map failed\n", __func__);
		ext_phys_mem_pool_enabled = false;
	} else {
		ext_mem_pool.phys_mem_base = poolPhysBase;
		ext_mem_pool.phys_mem_size = poolSize;
		ext_mem_pool.virt_mem_base = pool_virt_base;
		ext_mem_pool.next_phys_alloc_ptr = poolPhysBase;
		ext_phys_mem_pool_enabled = true;
	}
}

void mem_ext_phys_pool_release(void)
{
	if (ext_phys_mem_pool_enabled) {
		iounmap((void *)(ext_mem_pool.virt_mem_base));
		ext_phys_mem_pool_enabled = false;
	}
}

/*
 *  ======== mem_ext_phys_mem_alloc ========
 *  Purpose:
 *     Allocate physically contiguous, uncached memory from external memory pool
 */

static void *mem_ext_phys_mem_alloc(u32 bytes, u32 align, OUT u32 * pPhysAddr)
{
	u32 new_alloc_ptr;
	u32 offset;
	u32 virt_addr;

	if (align == 0)
		align = 1;

	if (bytes > ((ext_mem_pool.phys_mem_base + ext_mem_pool.phys_mem_size)
		     - ext_mem_pool.next_phys_alloc_ptr)) {
		pPhysAddr = NULL;
		return NULL;
	} else {
		offset = (ext_mem_pool.next_phys_alloc_ptr & (align - 1));
		if (offset == 0)
			new_alloc_ptr = ext_mem_pool.next_phys_alloc_ptr;
		else
			new_alloc_ptr = (ext_mem_pool.next_phys_alloc_ptr) +
			    (align - offset);
		if ((new_alloc_ptr + bytes) <=
		    (ext_mem_pool.phys_mem_base + ext_mem_pool.phys_mem_size)) {
			/* we can allocate */
			*pPhysAddr = new_alloc_ptr;
			ext_mem_pool.next_phys_alloc_ptr =
			    new_alloc_ptr + bytes;
			virt_addr =
			    ext_mem_pool.virt_mem_base + (new_alloc_ptr -
							  ext_mem_pool.
							  phys_mem_base);
			return (void *)virt_addr;
		} else {
			*pPhysAddr = 0;
			return NULL;
		}
	}
}

/*
 *  ======== mem_alloc ========
 *  Purpose:
 *      Allocate memory from the paged or non-paged pools.
 */
void *mem_alloc(u32 byte_size, enum mem_poolattrs type)
{
	void *mem = NULL;

	if (byte_size > 0) {
		switch (type) {
		case MEM_NONPAGED:
			/* Fall through */
		case MEM_PAGED:
			mem = kmalloc(byte_size,
				      (in_atomic()) ? GFP_ATOMIC : GFP_KERNEL);
			break;
		case MEM_LARGEVIRTMEM:
			mem = vmalloc(byte_size);
			break;

		default:
			break;
		}
	}

	return mem;
}

/*
 *  ======== mem_alloc_phys_mem ========
 *  Purpose:
 *      Allocate physically contiguous, uncached memory
 */
void *mem_alloc_phys_mem(u32 byte_size, u32 ulAlign, OUT u32 * pPhysicalAddress)
{
	void *va_mem = NULL;
	dma_addr_t pa_mem;

	if (byte_size > 0) {
		if (ext_phys_mem_pool_enabled) {
			va_mem = mem_ext_phys_mem_alloc(byte_size, ulAlign,
							(u32 *) &pa_mem);
		} else
			va_mem = dma_alloc_coherent(NULL, byte_size, &pa_mem,
						    (in_atomic()) ? GFP_ATOMIC :
						    GFP_KERNEL);
		if (va_mem == NULL)
			*pPhysicalAddress = 0;
		else
			*pPhysicalAddress = pa_mem;
	}
	return va_mem;
}

/*
 *  ======== mem_calloc ========
 *  Purpose:
 *      Allocate zero-initialized memory from the paged or non-paged pools.
 */
void *mem_calloc(u32 byte_size, enum mem_poolattrs type)
{
	void *mem = NULL;

	if (byte_size > 0) {
		switch (type) {
		case MEM_NONPAGED:
			/*  Fall through */
		case MEM_PAGED:
			mem = kzalloc(byte_size,
				      (in_atomic()) ? GFP_ATOMIC : GFP_KERNEL);
			break;
		case MEM_LARGEVIRTMEM:
			mem = __vmalloc(byte_size,
					GFP_KERNEL | __GFP_HIGHMEM | __GFP_ZERO,
					PAGE_KERNEL);
			break;
		default:
			break;
		}
	}

	return mem;
}

/*
 *  ======== mem_exit ========
 *  Purpose:
 *      Discontinue usage of the MEM module.
 */
void mem_exit(void)
{
}

/*
 *  ======== mem_flush_cache ========
 *  Purpose:
 *      Flush cache
 */
void mem_flush_cache(void *pMemBuf, u32 byte_size, s32 FlushType)
{
	if (!pMemBuf)
		return;

	switch (FlushType) {
		/* invalidate only */
	case PROC_INVALIDATE_MEM:
		dmac_inv_range(pMemBuf, pMemBuf + byte_size);
		outer_inv_range(__pa((u32) pMemBuf), __pa((u32) pMemBuf +
							  byte_size));
		break;
		/* writeback only */
	case PROC_WRITEBACK_MEM:
		dmac_clean_range(pMemBuf, pMemBuf + byte_size);
		outer_clean_range(__pa((u32) pMemBuf), __pa((u32) pMemBuf +
							    byte_size));
		break;
		/* writeback and invalidate */
	case PROC_WRITEBACK_INVALIDATE_MEM:
		dmac_flush_range(pMemBuf, pMemBuf + byte_size);
		outer_flush_range(__pa((u32) pMemBuf), __pa((u32) pMemBuf +
							    byte_size));
		break;
	}

}

/*
 *  ======== mem_free_phys_mem ========
 *  Purpose:
 *      Free the given block of physically contiguous memory.
 */
void mem_free_phys_mem(void *pVirtualAddress, u32 pPhysicalAddress,
		       u32 byte_size)
{
	DBC_REQUIRE(pVirtualAddress != NULL);

	if (!ext_phys_mem_pool_enabled)
		dma_free_coherent(NULL, byte_size, pVirtualAddress,
				  pPhysicalAddress);
}

/*
 *  ======== services_mem_init ========
 *  Purpose:
 *      Initialize MEM module private state.
 */
bool services_mem_init(void)
{
	return true;
}
