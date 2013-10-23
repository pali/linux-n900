/*
 * mem.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Memory management and address mapping services for the DSP/BIOS Bridge
 * class driver and mini-driver.
 *
 * Copyright (C) 2008 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef MEM_
#define MEM_

#include <dspbridge/host_os.h>
#include <dspbridge/memdefs.h>

/*
 *  ======== mem_alloc ========
 *  Purpose:
 *      Allocate memory from the paged or non-paged pools.
 *  Parameters:
 *      byte_size: Number of bytes to allocate.
 *      type:   Type of memory to allocate; one of:
 *              MEM_PAGED: Allocate from pageable memory.
 *              MEM_NONPAGED: Allocate from page locked memory.
 *  Returns:
 *      Pointer to a block of memory;
 *      NULL if memory couldn't be allocated, if byte_size == 0, or if type is
 *      not one of MEM_PAGED or MEM_NONPAGED.
 *  Requires:
 *      MEM initialized.
 *  Ensures:
 *      The returned pointer, if not NULL, points to a valid memory block of
 *      the size requested.
 */
extern void *mem_alloc(IN u32 byte_size, IN enum mem_poolattrs type);

/*
 *  ======== MEM_ALLOC_OBJECT ========
 *  Purpose:
 *      Allocate an object, and set it's signature.
 *  Parameters:
 *      pObj:       Pointer to the new object.
 *      Obj:        Type of the object to allocate.
 *      Signature:  Magic field value.  Must be non-zero.
 *  Returns:
 *  Requires:
 *      Same requirements as mem_calloc(); and
 *      The object structure has a dw_signature field.  The compiler ensures
 *      this requirement.
 *  Ensures:
 *      A subsequent call to MEM_IS_VALID_HANDLE() will succeed for this object.
 */
#define MEM_ALLOC_OBJECT(pObj, Obj, Signature)           \
{                                                       \
    pObj = mem_calloc(sizeof(Obj), MEM_NONPAGED);       \
    if (pObj) {                                         \
	pObj->dw_signature = Signature;                  \
    }                                                   \
}

/*  ======== mem_alloc_phys_mem ========
 *  Purpose:
 *      Allocate physically contiguous, uncached memory
 *  Parameters:
 *      byte_size:     Number of bytes to allocate.
 *      ulAlign:    Alignment Mask.
 *      pPhysicalAddress: Physical address of allocated memory.
 *  Returns:
 *      Pointer to a block of memory;
 *      NULL if memory couldn't be allocated, or if byte_size == 0.
 *  Requires:
 *      MEM initialized.
 *  Ensures:
 *      The returned pointer, if not NULL, points to a valid memory block of
 *      the size requested.  Returned physical address refers to physical
 *      location of memory.
 */
extern void *mem_alloc_phys_mem(IN u32 byte_size,
				IN u32 ulAlign, OUT u32 *pPhysicalAddress);

/*
 *  ======== mem_calloc ========
 *  Purpose:
 *      Allocate zero-initialized memory from the paged or non-paged pools.
 *  Parameters:
 *      byte_size: Number of bytes to allocate.
 *      type:   Type of memory to allocate; one of:
 *              MEM_PAGED:   Allocate from pageable memory.
 *              MEM_NONPAGED: Allocate from page locked memory.
 *  Returns:
 *      Pointer to a block of zeroed memory;
 *      NULL if memory couldn't be allocated, if byte_size == 0, or if type is
 *      not one of MEM_PAGED or MEM_NONPAGED.
 *  Requires:
 *      MEM initialized.
 *  Ensures:
 *      The returned pointer, if not NULL, points to a valid memory block
 *      of the size requested.
 */
extern void *mem_calloc(IN u32 byte_size, IN enum mem_poolattrs type);

/*
 *  ======== mem_exit ========
 *  Purpose:
 *      Discontinue usage of module; free resources when reference count
 *      reaches 0.
 *  Parameters:
 *  Returns:
 *  Requires:
 *      MEM is initialized.
 *  Ensures:
 *      Resources used by module are freed when cRef reaches zero.
 */
extern void mem_exit(void);

/*
 *  ======== mem_flush_cache ========
 *  Purpose:
 *      Performs system cache sync with discard
 *  Parameters:
 *      pMemBuf:    Pointer to memory region to be flushed.
 *      pMemBuf:    Size of the memory region to be flushed.
 *  Returns:
 *  Requires:
 *      MEM is initialized.
 *  Ensures:
 *      Cache is synchronized
 */
extern void mem_flush_cache(void *pMemBuf, u32 byte_size, s32 FlushType);

/*
 *  ======== mem_free_phys_mem ========
 *  Purpose:
 *      Free the given block of physically contiguous memory.
 *  Parameters:
 *      pVirtualAddress:  Pointer to virtual memory region allocated
 *      by mem_alloc_phys_mem().
 *      pPhysicalAddress:  Pointer to physical memory region  allocated
 *      by mem_alloc_phys_mem().
 *      byte_size:  Size of the memory region allocated by mem_alloc_phys_mem().
 *  Returns:
 *  Requires:
 *      MEM initialized.
 *      pVirtualAddress is a valid memory address returned by
 *          mem_alloc_phys_mem()
 *  Ensures:
 *      pVirtualAddress is no longer a valid pointer to memory.
 */
extern void mem_free_phys_mem(void *pVirtualAddress,
			      u32 pPhysicalAddress, u32 byte_size);

/*
 *  ======== MEM_FREE_OBJECT ========
 *  Purpose:
 *      Utility macro to invalidate an object's signature, and deallocate it.
 *  Parameters:
 *      pObj:   Pointer to the object to free.
 *  Returns:
 *  Requires:
 *      Same requirements as kfree().
 *  Ensures:
 *      A subsequent call to MEM_IS_VALID_HANDLE() will fail for this object.
 */
#define MEM_FREE_OBJECT(pObj)    \
{                               \
    pObj->dw_signature = 0x00;   \
    kfree(pObj);                \
}

/*
 *  ======== mem_get_num_pages ========
 *  Purpose:
 *      Calculate the number of pages corresponding to the supplied buffer.
 *  Parameters:
 *      paddr:  Linear (virtual) address of the buffer.
 *      byte_size: Number of bytes in the buffer.
 *  Returns:
 *      Number of pages.
 *  Requires:
 *      MEM initialized.
 *  Ensures:
 *      If byte_size > 0, number of pages returned > 0.
 */
extern s32 mem_get_num_pages(IN void *paddr, IN u32 byte_size);

/*
 *  ======== services_mem_init ========
 *  Purpose:
 *      Initializes private state of MEM module.
 *  Parameters:
 *  Returns:
 *      TRUE if initialized; FALSE if error occured.
 *  Requires:
 *  Ensures:
 *      MEM initialized.
 */
extern bool services_mem_init(void);

/*
 *  ======== MEM_IS_VALID_HANDLE ========
 *  Purpose:
 *      Validate the object handle.
 *  Parameters:
 *      hObj:   Handle to object created with MEM_ALLOC_OBJECT().
 *      Sig:    Expected signature u32.
 *  Returns:
 *      TRUE if handle is valid; FALSE otherwise.
 *  Requires:
 *      The object structure has a dw_signature field. Ensured by compiler.
 *  Ensures:
 */
#define MEM_IS_VALID_HANDLE(hObj, Sig)                \
     ((hObj != NULL) && (hObj->dw_signature == Sig))

/*
 *  ======== MEM_LINEAR_ADDRESS ========
 *  Purpose:
 *      Get the linear address corresponding to the given physical address.
 *  Parameters:
 *      pPhysAddr:  Physical address to be mapped.
 *      byte_size:     Number of bytes in physical range to map.
 *  Returns:
 *      The corresponding linear address, or NULL if unsuccessful.
 *  Requires:
 *      MEM initialized.
 *  Ensures:
 *  Notes:
 *      If valid linear address is returned, be sure to call
 *      MEM_UNMAP_LINEAR_ADDRESS().
 */
#define MEM_LINEAR_ADDRESS(pPhyAddr, byte_size) pPhyAddr

/*
 *  ======== MEM_UNMAP_LINEAR_ADDRESS ========
 *  Purpose:
 *      Unmap the linear address mapped in MEM_LINEAR_ADDRESS.
 *  Parameters:
 *      pBaseAddr: Ptr to mapped memory (as returned by MEM_LINEAR_ADDRESS()).
 *  Returns:
 *  Requires:
 *      - MEM initialized.
 *      - pBaseAddr is a valid linear address mapped in MEM_LINEAR_ADDRESS.
 *  Ensures:
 *      - pBaseAddr no longer points to a valid linear address.
 */
#define MEM_UNMAP_LINEAR_ADDRESS(pBaseAddr) {}

/*
 *  ======== mem_ext_phys_pool_init ========
 *  Purpose:
 *      Uses the physical memory chunk passed for internal consitent memory
 *      allocations.
 *      physical address based on the page frame address.
 *  Parameters:
 *      poolPhysBase  starting address of the physical memory pool.
 *      poolSize      size of the physical memory pool.
 *  Returns:
 *      none.
 *  Requires:
 *      - MEM initialized.
 *      - valid physical address for the base and size > 0
 */
extern void mem_ext_phys_pool_init(IN u32 poolPhysBase, IN u32 poolSize);

/*
 *  ======== mem_ext_phys_pool_release ========
 */
extern void mem_ext_phys_pool_release(void);

#endif /* MEM_ */
