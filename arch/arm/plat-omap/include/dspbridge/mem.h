/*
 * mem.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
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


/*
 *  ======== mem.h ========
 *  Purpose:
 *      Memory management and address mapping services for the DSP/BIOS Bridge
 *      class driver and mini-driver.
 *
 *  Public Functions:
 *      MEM_Alloc
 *      MEM_AllocObject
 *      MEM_AllocPhysMem
 *      MEM_Calloc
 *      MEM_Exit
 *      MEM_FlushCache
 *      MEM_Free
 *      MEM_FreeObject
 *      MEM_FreePhysMem
 *      MEM_GetNumPages
 *      MEM_Init
 *      MEM_IsValidHandle
 *      MEM_LinearAddress
 *      MEM_PageLock
 *      MEM_PageUnlock
 *      MEM_UnMapLinearAddress
 *      MEM_VirtualToPhysical
 *
 *  Notes:
 *
 *! Revision History:
 *! ================
 *! 19-Apr-2004 sb: Added Alloc/Free PhysMem, FlushCache, VirtualToPhysical
 *! 01-Sep-2001 ag: Cleaned up notes for MEM_LinearAddress() does not
 *!                   require phys address to be page aligned!
 *! 02-Dec-1999 rr: stdwin.h included for retail build
 *! 12-Nov-1999 kc: Added warning about use of MEM_LinearAddress.
 *! 29-Oct-1999 kc: Cleaned up for code review.
 *! 10-Aug-1999 kc: Based on wsx-c18.
 *! 07-Jan-1998 gp: Added MEM_AllocUMB and MEM_UMBFree for User Mapped Buffers
 *!                 used by WMD_CHNL.
 *! 23-Dec-1997 cr: Code review cleanup, removed dead Ring 3 code.
 *! 04-Aug-1997 cr: Added explicit CDECL identifiers.
 *! 01-Nov-1996 gp: Updated based on code review.
 *! 04-Sep-1996 gp: Added MEM_PageLock() and MEM_PageUnlock() services.
 *! 14-Aug-1996 mg: Added MEM_GetPhysAddr() and MEM_GetNumPages()
 *! 25-Jul-1996 gp: Added MEM_IsValidHandle() macro.
 *! 10-May-1996 gp: Added MEM_Calloc().
 *! 25-Apr-1996 gp: Added MEM_PhysicalAddress()
 *! 17-Apr-1996 gp: Added MEM_Exit function; updated to latest naming standard.
 *! 08-Apr-1996 gp: Created.
 */

#ifndef MEM_
#define MEM_

#include <dspbridge/host_os.h>
#include <dspbridge/memdefs.h>

/*
 *  ======== MEM_Alloc ========
 *  Purpose:
 *      Allocate memory from the paged or non-paged pools.
 *  Parameters:
 *      cBytes: Number of bytes to allocate.
 *      type:   Type of memory to allocate; one of:
 *              MEM_PAGED: Allocate from pageable memory.
 *              MEM_NONPAGED: Allocate from page locked memory.
 *  Returns:
 *      Pointer to a block of memory;
 *      NULL if memory couldn't be allocated, if cBytes == 0, or if type is
 *      not one of MEM_PAGED or MEM_NONPAGED.
 *  Requires:
 *      MEM initialized.
 *  Ensures:
 *      The returned pointer, if not NULL, points to a valid memory block of
 *      the size requested.
 */
	extern void *MEM_Alloc(IN u32 cBytes, IN enum MEM_POOLATTRS type);

/*
 *  ======== MEM_AllocObject ========
 *  Purpose:
 *      Allocate an object, and set it's signature.
 *  Parameters:
 *      pObj:       Pointer to the new object.
 *      Obj:        Type of the object to allocate.
 *      Signature:  Magic field value.  Must be non-zero.
 *  Returns:
 *  Requires:
 *      Same requirements as MEM_Calloc(); and
 *      The object structure has a dwSignature field.  The compiler ensures
 *      this requirement.
 *  Ensures:
 *      A subsequent call to MEM_IsValidHandle() will succeed for this object.
 */
#define MEM_AllocObject(pObj, Obj, Signature)           \
{                                                       \
    pObj = MEM_Calloc(sizeof(Obj), MEM_NONPAGED);       \
    if (pObj) {                                         \
	pObj->dwSignature = Signature;                  \
    }                                                   \
}

/*  ======== MEM_AllocPhysMem ========
 *  Purpose:
 *      Allocate physically contiguous, uncached memory
 *  Parameters:
 *      cBytes:     Number of bytes to allocate.
 *      ulAlign:    Alignment Mask.
 *      pPhysicalAddress: Physical address of allocated memory.
 *  Returns:
 *      Pointer to a block of memory;
 *      NULL if memory couldn't be allocated, or if cBytes == 0.
 *  Requires:
 *      MEM initialized.
 *  Ensures:
 *      The returned pointer, if not NULL, points to a valid memory block of
 *      the size requested.  Returned physical address refers to physical
 *      location of memory.
 */
	extern void *MEM_AllocPhysMem(IN u32 cBytes,
				      IN u32 ulAlign,
				      OUT u32 *pPhysicalAddress);

/*
 *  ======== MEM_Calloc ========
 *  Purpose:
 *      Allocate zero-initialized memory from the paged or non-paged pools.
 *  Parameters:
 *      cBytes: Number of bytes to allocate.
 *      type:   Type of memory to allocate; one of:
 *              MEM_PAGED:   Allocate from pageable memory.
 *              MEM_NONPAGED: Allocate from page locked memory.
 *  Returns:
 *      Pointer to a block of zeroed memory;
 *      NULL if memory couldn't be allocated, if cBytes == 0, or if type is
 *      not one of MEM_PAGED or MEM_NONPAGED.
 *  Requires:
 *      MEM initialized.
 *  Ensures:
 *      The returned pointer, if not NULL, points to a valid memory block
 *      of the size requested.
 */
	extern void *MEM_Calloc(IN u32 cBytes, IN enum MEM_POOLATTRS type);

/*
 *  ======== MEM_Exit ========
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
	extern void MEM_Exit(void);

/*
 *  ======== MEM_FlushCache ========
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
	extern void MEM_FlushCache(void *pMemBuf, u32 cBytes, s32 FlushType);

/*
 *  ======== MEM_Free ========
 *  Purpose:
 *      Free the given block of system memory.
 *  Parameters:
 *      pMemBuf:    Pointer to memory allocated by MEM_Calloc/Alloc().
 *  Returns:
 *  Requires:
 *      MEM initialized.
 *      pMemBuf is a valid memory address returned by MEM_Calloc/Alloc().
 *  Ensures:
 *      pMemBuf is no longer a valid pointer to memory.
 */
	extern void MEM_Free(IN void *pMemBuf);

/*
 *  ======== MEM_FreePhysMem ========
 *  Purpose:
 *      Free the given block of physically contiguous memory.
 *  Parameters:
 *      pVirtualAddress:  Pointer to virtual memory region allocated
 *      by MEM_AllocPhysMem().
 *      pPhysicalAddress:  Pointer to physical memory region  allocated
 *      by MEM_AllocPhysMem().
 *      cBytes:  Size of the memory region allocated by MEM_AllocPhysMem().
 *  Returns:
 *  Requires:
 *      MEM initialized.
 *      pVirtualAddress is a valid memory address returned by
 *          MEM_AllocPhysMem()
 *  Ensures:
 *      pVirtualAddress is no longer a valid pointer to memory.
 */
	extern void MEM_FreePhysMem(void *pVirtualAddress,
				    u32 pPhysicalAddress, u32 cBytes);

/*
 *  ======== MEM_FreeObject ========
 *  Purpose:
 *      Utility macro to invalidate an object's signature, and deallocate it.
 *  Parameters:
 *      pObj:   Pointer to the object to free.
 *  Returns:
 *  Requires:
 *      Same requirements as MEM_Free().
 *  Ensures:
 *      A subsequent call to MEM_IsValidHandle() will fail for this object.
 */
#define MEM_FreeObject(pObj)    \
{                               \
    pObj->dwSignature = 0x00;   \
    MEM_Free(pObj);             \
}

/*
 *  ======== MEM_GetNumPages ========
 *  Purpose:
 *      Calculate the number of pages corresponding to the supplied buffer.
 *  Parameters:
 *      pAddr:  Linear (virtual) address of the buffer.
 *      cBytes: Number of bytes in the buffer.
 *  Returns:
 *      Number of pages.
 *  Requires:
 *      MEM initialized.
 *  Ensures:
 *      If cBytes > 0, number of pages returned > 0.
 */
	extern s32 MEM_GetNumPages(IN void *pAddr, IN u32 cBytes);

/*
 *  ======== MEM_Init ========
 *  Purpose:
 *      Initializes private state of MEM module.
 *  Parameters:
 *  Returns:
 *      TRUE if initialized; FALSE if error occured.
 *  Requires:
 *  Ensures:
 *      MEM initialized.
 */
	extern bool MEM_Init(void);

/*
 *  ======== MEM_IsValidHandle ========
 *  Purpose:
 *      Validate the object handle.
 *  Parameters:
 *      hObj:   Handle to object created with MEM_AllocObject().
 *      Sig:    Expected signature u32.
 *  Returns:
 *      TRUE if handle is valid; FALSE otherwise.
 *  Requires:
 *      The object structure has a dwSignature field. Ensured by compiler.
 *  Ensures:
 */
#define MEM_IsValidHandle(hObj, Sig)                \
     ((hObj != NULL) && (hObj->dwSignature == Sig))

/*
 *  ======== MEM_LinearAddress ========
 *  Purpose:
 *      Get the linear address corresponding to the given physical address.
 *  Parameters:
 *      pPhysAddr:  Physical address to be mapped.
 *      cBytes:     Number of bytes in physical range to map.
 *  Returns:
 *      The corresponding linear address, or NULL if unsuccessful.
 *  Requires:
 *      MEM initialized.
 *  Ensures:
 *  Notes:
 *      If valid linear address is returned, be sure to call
 *      MEM_UnmapLinearAddress().
 */
#define MEM_LinearAddress(pPhyAddr, cBytes) pPhyAddr

/*
 *  ======== MEM_UnmapLinearAddress ========
 *  Purpose:
 *      Unmap the linear address mapped in MEM_LinearAddress.
 *  Parameters:
 *      pBaseAddr: Ptr to mapped memory (as returned by MEM_LinearAddress()).
 *  Returns:
 *  Requires:
 *      - MEM initialized.
 *      - pBaseAddr is a valid linear address mapped in MEM_LinearAddress.
 *  Ensures:
 *      - pBaseAddr no longer points to a valid linear address.
 */
#define MEM_UnmapLinearAddress(pBaseAddr)

/*
 *  ======== MEM_ExtPhysPoolInit ========
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
	extern void MEM_ExtPhysPoolInit(IN u32 poolPhysBase,
					IN u32 poolSize);

#endif				/* MEM_ */
