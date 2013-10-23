/*
 * dmm.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * The Dynamic Memory Manager (DMM) module manages the DSP Virtual address
 * space that can be directly mapped to any MPU buffer or memory region
 *
 * Notes:
 *   Region: Generic memory entitiy having a start address and a size
 *   Chunk:  Reserved region
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

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>
#include <dspbridge/errbase.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/mem.h>
#include <dspbridge/sync.h>

/*  ----------------------------------- Platform Manager */
#include <dspbridge/dev.h>
#include <dspbridge/proc.h>

/*  ----------------------------------- This */
#include <dspbridge/dmm.h>

/*  ----------------------------------- Defines, Data Structures, Typedefs */
/* Object signatures */
#define DMMSIGNATURE       0x004d4d44	/* "DMM"   (in reverse) */

#define DMM_ADDR_VIRTUAL(a) \
	(((struct map_page *)(a) - virtual_mapping_table) * PG_SIZE4K +\
	dyn_mem_map_beg)
#define DMM_ADDR_TO_INDEX(a) (((a) - dyn_mem_map_beg) / PG_SIZE4K)

/* DMM Mgr */
struct dmm_object {
	u32 dw_signature;	/* Used for object validation */
	/* Dmm Lock is used to serialize access mem manager for
	 * multi-threads. */
	struct sync_csobject *dmm_lock;	/* Lock to access dmm mgr */
};

/*  ----------------------------------- Globals */
static u32 refs;		/* module reference count */
struct map_page {
	u32 region_size:31;
	u32 reserved:1;
	u32 mapped_size:31;
	u32 mapped:1;
};

/*  Create the free list */
static struct map_page *virtual_mapping_table;
static u32 free_region;		/* The index of free region */
static u32 free_size;
static u32 dyn_mem_map_beg;	/* The Beginning of dynamic memory mapping */
static u32 table_size;		/* The size of virt and phys pages tables */

/*  ----------------------------------- Function Prototypes */
static struct map_page *get_region(u32 addr);
static struct map_page *get_free_region(u32 aSize);
static struct map_page *get_mapped_region(u32 aAddr);
#ifdef DSP_DMM_DEBUG
u32 dmm_mem_map_dump(struct dmm_object *dmm_mgr);
#endif

/*  ======== dmm_create_tables ========
 *  Purpose:
 *      Create table to hold the information of physical address
 *      the buffer pages that is passed by the user, and the table
 *      to hold the information of the virtual memory that is reserved
 *      for DSP.
 */
dsp_status dmm_create_tables(struct dmm_object *dmm_mgr, u32 addr, u32 size)
{
	struct dmm_object *dmm_obj = (struct dmm_object *)dmm_mgr;
	dsp_status status = DSP_SOK;

	status = dmm_delete_tables(dmm_obj);
	if (DSP_SUCCEEDED(status)) {
		sync_enter_cs(dmm_obj->dmm_lock);
		dyn_mem_map_beg = addr;
		table_size = PG_ALIGN_HIGH(size, PG_SIZE4K) / PG_SIZE4K;
		/*  Create the free list */
		virtual_mapping_table = (struct map_page *)mem_calloc
		    (table_size * sizeof(struct map_page), MEM_LARGEVIRTMEM);
		if (virtual_mapping_table == NULL)
			status = DSP_EMEMORY;
		else {
			/* On successful allocation,
			 * all entries are zero ('free') */
			free_region = 0;
			free_size = table_size * PG_SIZE4K;
			virtual_mapping_table[0].region_size = table_size;
		}
		sync_leave_cs(dmm_obj->dmm_lock);
	}

	if (DSP_FAILED(status))
		pr_err("%s: failure, status 0x%x\n", __func__, status);

	return status;
}

/*
 *  ======== dmm_create ========
 *  Purpose:
 *      Create a dynamic memory manager object.
 */
dsp_status dmm_create(OUT struct dmm_object **phDmmMgr,
		      struct dev_object *hdev_obj,
		      IN CONST struct dmm_mgrattrs *pMgrAttrs)
{
	struct dmm_object *dmm_obj = NULL;
	dsp_status status = DSP_SOK;
	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(phDmmMgr != NULL);

	*phDmmMgr = NULL;
	/* create, zero, and tag a cmm mgr object */
	MEM_ALLOC_OBJECT(dmm_obj, struct dmm_object, DMMSIGNATURE);
	if (dmm_obj != NULL) {
		status = sync_initialize_cs(&dmm_obj->dmm_lock);
		if (DSP_SUCCEEDED(status))
			*phDmmMgr = dmm_obj;
		else
			dmm_destroy(dmm_obj);
	} else {
		status = DSP_EMEMORY;
	}

	return status;
}

/*
 *  ======== dmm_destroy ========
 *  Purpose:
 *      Release the communication memory manager resources.
 */
dsp_status dmm_destroy(struct dmm_object *dmm_mgr)
{
	struct dmm_object *dmm_obj = (struct dmm_object *)dmm_mgr;
	dsp_status status = DSP_SOK;

	DBC_REQUIRE(refs > 0);
	if (MEM_IS_VALID_HANDLE(dmm_mgr, DMMSIGNATURE)) {
		status = dmm_delete_tables(dmm_obj);
		if (DSP_SUCCEEDED(status)) {
			/* Delete CS & dmm mgr object */
			sync_delete_cs(dmm_obj->dmm_lock);
			MEM_FREE_OBJECT(dmm_obj);
		}
	} else
		status = DSP_EHANDLE;

	return status;
}

/*
 *  ======== dmm_delete_tables ========
 *  Purpose:
 *      Delete DMM Tables.
 */
dsp_status dmm_delete_tables(struct dmm_object *dmm_mgr)
{
	struct dmm_object *dmm_obj = (struct dmm_object *)dmm_mgr;
	dsp_status status = DSP_SOK;

	DBC_REQUIRE(refs > 0);
	if (MEM_IS_VALID_HANDLE(dmm_mgr, DMMSIGNATURE)) {
		/* Delete all DMM tables */
		sync_enter_cs(dmm_obj->dmm_lock);

		vfree(virtual_mapping_table);

		sync_leave_cs(dmm_obj->dmm_lock);
	} else
		status = DSP_EHANDLE;
	return status;
}

/*
 *  ======== dmm_exit ========
 *  Purpose:
 *      Discontinue usage of module; free resources when reference count
 *      reaches 0.
 */
void dmm_exit(void)
{
	DBC_REQUIRE(refs > 0);

	refs--;
}

/*
 *  ======== dmm_get_handle ========
 *  Purpose:
 *      Return the dynamic memory manager object for this device.
 *      This is typically called from the client process.
 */
dsp_status dmm_get_handle(void *hprocessor, OUT struct dmm_object **phDmmMgr)
{
	dsp_status status = DSP_SOK;
	struct dev_object *hdev_obj;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(phDmmMgr != NULL);
	if (hprocessor != NULL)
		status = proc_get_dev_object(hprocessor, &hdev_obj);
	else
		hdev_obj = dev_get_first();	/* default */

	if (DSP_SUCCEEDED(status))
		status = dev_get_dmm_mgr(hdev_obj, phDmmMgr);

	return status;
}

/*
 *  ======== dmm_init ========
 *  Purpose:
 *      Initializes private state of DMM module.
 */
bool dmm_init(void)
{
	bool ret = true;

	DBC_REQUIRE(refs >= 0);

	if (ret)
		refs++;

	DBC_ENSURE((ret && (refs > 0)) || (!ret && (refs >= 0)));

	virtual_mapping_table = NULL;
	table_size = 0;

	return ret;
}

/*
 *  ======== dmm_map_memory ========
 *  Purpose:
 *      Add a mapping block to the reserved chunk. DMM assumes that this block
 *  will be mapped in the DSP/IVA's address space. DMM returns an error if a
 *  mapping overlaps another one. This function stores the info that will be
 *  required later while unmapping the block.
 */
dsp_status dmm_map_memory(struct dmm_object *dmm_mgr, u32 addr, u32 size)
{
	struct dmm_object *dmm_obj = (struct dmm_object *)dmm_mgr;
	struct map_page *chunk;
	dsp_status status = DSP_SOK;

	sync_enter_cs(dmm_obj->dmm_lock);
	/* Find the Reserved memory chunk containing the DSP block to
	 * be mapped */
	chunk = (struct map_page *)get_region(addr);
	if (chunk != NULL) {
		/* Mark the region 'mapped', leave the 'reserved' info as-is */
		chunk->mapped = true;
		chunk->mapped_size = (size / PG_SIZE4K);
	} else
		status = DSP_ENOTFOUND;
	sync_leave_cs(dmm_obj->dmm_lock);

	dev_dbg(bridge, "%s dmm_mgr %p, addr %x, size %x\n\tstatus %x, "
		"chunk %p", __func__, dmm_mgr, addr, size, status, chunk);

	return status;
}

/*
 *  ======== dmm_reserve_memory ========
 *  Purpose:
 *      Reserve a chunk of virtually contiguous DSP/IVA address space.
 */
dsp_status dmm_reserve_memory(struct dmm_object *dmm_mgr, u32 size,
			      u32 *prsv_addr)
{
	dsp_status status = DSP_SOK;
	struct dmm_object *dmm_obj = (struct dmm_object *)dmm_mgr;
	struct map_page *node;
	u32 rsv_addr = 0;
	u32 rsv_size = 0;

	sync_enter_cs(dmm_obj->dmm_lock);

	/* Try to get a DSP chunk from the free list */
	node = get_free_region(size);
	if (node != NULL) {
		/*  DSP chunk of given size is available. */
		rsv_addr = DMM_ADDR_VIRTUAL(node);
		/* Calculate the number entries to use */
		rsv_size = size / PG_SIZE4K;
		if (rsv_size < node->region_size) {
			/* Mark remainder of free region */
			node[rsv_size].mapped = false;
			node[rsv_size].reserved = false;
			node[rsv_size].region_size =
			    node->region_size - rsv_size;
			node[rsv_size].mapped_size = 0;
		}
		/*  get_region will return first fit chunk. But we only use what
		   is requested. */
		node->mapped = false;
		node->reserved = true;
		node->region_size = rsv_size;
		node->mapped_size = 0;
		/* Return the chunk's starting address */
		*prsv_addr = rsv_addr;
	} else
		/*dSP chunk of given size is not available */
		status = DSP_EMEMORY;

	sync_leave_cs(dmm_obj->dmm_lock);

	dev_dbg(bridge, "%s dmm_mgr %p, size %x, prsv_addr %p\n\tstatus %x, "
		"rsv_addr %x, rsv_size %x\n", __func__, dmm_mgr, size,
		prsv_addr, status, rsv_addr, rsv_size);

	return status;
}

/*
 *  ======== dmm_un_map_memory ========
 *  Purpose:
 *      Remove the mapped block from the reserved chunk.
 */
dsp_status dmm_un_map_memory(struct dmm_object *dmm_mgr, u32 addr, u32 *psize)
{
	struct dmm_object *dmm_obj = (struct dmm_object *)dmm_mgr;
	struct map_page *chunk;
	dsp_status status = DSP_SOK;

	sync_enter_cs(dmm_obj->dmm_lock);
	chunk = get_mapped_region(addr);
	if (chunk == NULL)
		status = DSP_ENOTFOUND;

	if (DSP_SUCCEEDED(status)) {
		/* Unmap the region */
		*psize = chunk->mapped_size * PG_SIZE4K;
		chunk->mapped = false;
		chunk->mapped_size = 0;
	}
	sync_leave_cs(dmm_obj->dmm_lock);

	dev_dbg(bridge, "%s: dmm_mgr %p, addr %x, psize %p\n\tstatus %x, "
		"chunk %p\n", __func__, dmm_mgr, addr, psize, status, chunk);

	return status;
}

/*
 *  ======== dmm_un_reserve_memory ========
 *  Purpose:
 *      Free a chunk of reserved DSP/IVA address space.
 */
dsp_status dmm_un_reserve_memory(struct dmm_object *dmm_mgr, u32 rsv_addr)
{
	struct dmm_object *dmm_obj = (struct dmm_object *)dmm_mgr;
	struct map_page *chunk;
	u32 i;
	dsp_status status = DSP_SOK;
	u32 chunk_size;

	sync_enter_cs(dmm_obj->dmm_lock);

	/* Find the chunk containing the reserved address */
	chunk = get_mapped_region(rsv_addr);
	if (chunk == NULL)
		status = DSP_ENOTFOUND;

	if (DSP_SUCCEEDED(status)) {
		/* Free all the mapped pages for this reserved region */
		i = 0;
		while (i < chunk->region_size) {
			if (chunk[i].mapped) {
				/* Remove mapping from the page tables. */
				chunk_size = chunk[i].mapped_size;
				/* Clear the mapping flags */
				chunk[i].mapped = false;
				chunk[i].mapped_size = 0;
				i += chunk_size;
			} else
				i++;
		}
		/* Clear the flags (mark the region 'free') */
		chunk->reserved = false;
		/* NOTE: We do NOT coalesce free regions here.
		 * Free regions are coalesced in get_region(), as it traverses
		 *the whole mapping table
		 */
	}
	sync_leave_cs(dmm_obj->dmm_lock);

	dev_dbg(bridge, "%s: dmm_mgr %p, rsv_addr %x\n\tstatus %x chunk %p",
		__func__, dmm_mgr, rsv_addr, status, chunk);

	return status;
}

/*
 *  ======== get_region ========
 *  Purpose:
 *      Returns a region containing the specified memory region
 */
static struct map_page *get_region(u32 aAddr)
{
	struct map_page *curr_region = NULL;
	u32 i = 0;

	if (virtual_mapping_table != NULL) {
		/* find page mapped by this address */
		i = DMM_ADDR_TO_INDEX(aAddr);
		if (i < table_size)
			curr_region = virtual_mapping_table + i;
	}

	dev_dbg(bridge, "%s: curr_region %p, free_region %d, free_size %d\n",
		__func__, curr_region, free_region, free_size);
	return curr_region;
}

/*
 *  ======== get_free_region ========
 *  Purpose:
 *  Returns the requested free region
 */
static struct map_page *get_free_region(u32 aSize)
{
	struct map_page *curr_region = NULL;
	u32 i = 0;
	u32 region_size = 0;
	u32 next_i = 0;

	if (virtual_mapping_table == NULL)
		return curr_region;

		/* Find the largest free region
		 * (coalesce during the traversal) */
		while (i < table_size) {
			region_size = virtual_mapping_table[i].region_size;
			next_i = i + region_size;
			if (virtual_mapping_table[i].reserved == false) {
				/* Coalesce, if possible */
				if (next_i < table_size &&
				    virtual_mapping_table[next_i].reserved
				    == false) {
					virtual_mapping_table[i].region_size +=
					    virtual_mapping_table
					    [next_i].region_size;
					continue;
				}
				region_size *= PG_SIZE4K;
				if (region_size >= aSize) {
					free_region = i;
					free_size = region_size;
					break;
				}
			}
			i = next_i;
		}
	if (aSize <= free_size) {
		curr_region = virtual_mapping_table + free_region;
		free_region += (aSize / PG_SIZE4K);
		free_size -= aSize;
	}
	return curr_region;
}

/*
 *  ======== get_mapped_region ========
 *  Purpose:
 *  Returns the requestedmapped region
 */
static struct map_page *get_mapped_region(u32 aAddr)
{
	u32 i = 0;
	struct map_page *curr_region = NULL;

	if (virtual_mapping_table == NULL)
		return curr_region;

	i = DMM_ADDR_TO_INDEX(aAddr);
	if (i < table_size && (virtual_mapping_table[i].mapped ||
			       virtual_mapping_table[i].reserved))
		curr_region = virtual_mapping_table + i;
	return curr_region;
}

#ifdef DSP_DMM_DEBUG
u32 dmm_mem_map_dump(struct dmm_object *dmm_mgr)
{
	struct map_page *curr_node = NULL;
	u32 i;
	u32 freemem = 0;
	u32 bigsize = 0;

	sync_enter_cs(dmm_mgr->dmm_lock);

	if (virtual_mapping_table != NULL) {
		for (i = 0; i < table_size; i +=
		     virtual_mapping_table[i].region_size) {
			curr_node = virtual_mapping_table + i;
			if (curr_node->reserved == TRUE) {
				/*printk("RESERVED size = 0x%x, "
				   "Map size = 0x%x\n",
				   (curr_node->region_size * PG_SIZE4K),
				   (curr_node->mapped == false) ? 0 :
				   (curr_node->mapped_size * PG_SIZE4K));
				 */
			} else {
/*				printk("UNRESERVED size = 0x%x\n",
					(curr_node->region_size * PG_SIZE4K));
 */
				freemem += (curr_node->region_size * PG_SIZE4K);
				if (curr_node->region_size > bigsize)
					bigsize = curr_node->region_size;
			}
		}
	}
	printk(KERN_INFO "Total DSP VA FREE memory = %d Mbytes\n",
	       freemem / (1024 * 1024));
	printk(KERN_INFO "Total DSP VA USED memory= %d Mbytes \n",
	       (((table_size * PG_SIZE4K) - freemem)) / (1024 * 1024));
	printk(KERN_INFO "DSP VA - Biggest FREE block = %d Mbytes \n\n",
	       (bigsize * PG_SIZE4K / (1024 * 1024)));
	sync_leave_cs(dmm_mgr->dmm_lock);

	return 0;
}
#endif
