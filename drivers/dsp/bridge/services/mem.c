/*
 * mem.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
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


/*
 *  ======== mem.c ========
 *  Purpose:
 *      Implementation of platform specific memory services.
 *
 *  Public Functions:
 *      MEM_Alloc
 *      MEM_AllocPhysMem
 *      MEM_Calloc
 *      MEM_Exit
 *      MEM_FlushCache
 *      MEM_Free
 *      MEM_FreePhysMem
 *      MEM_Init
 *      MEM_ExtPhysPoolInit
 *
 *! Revision History:
 *! =================
 *! 18-Jan-2004 hp: Added support for External physical memory pool
 *! 19-Apr-2004 sb: Added Alloc/Free PhysMem, FlushCache, VirtualToPhysical
 *! 01-Sep-2001 ag: Code cleanup.
 *! 02-May-2001 ag: MEM_[UnMap]LinearAddress revamped to align Phys to Virt.
 *!		 Set PAGE_PHYSICAL if phy addr <= 512MB. Opposite uSoft doc!
 *! 29-Aug-2000 rr: MEM_LinearAddress does not check for 512MB for non-x86.
 *! 28-Mar-2000 rr: MEM_LinearAddress changed.Handles address larger than 512MB
 *! 03-Feb-2000 rr: Module init/exit is handled by SERVICES Init/Exit.
 *!		 GT Changes.
 *! 22-Nov-1999 kc: Added changes from code review.
 *! 16-Aug-1999 kc: modified for WinCE.
 *! 20-Mar-1999 ag: SP 4 fix in MEM_UMBCalloc().
 *!		 Mdl offset now ORed not added to userBuf.
 *! 23-Dec-1997 cr: Code review changes.
 *! 08-Dec-1997 cr: Prepared for code review.
 *! 24-Jun-1997 cr: Created.
 */

/*  ----------------------------------- Host OS */
#include <dspbridge/host_os.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>
#include <dspbridge/dbdefs.h>
#include <dspbridge/errbase.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>
#include <dspbridge/gt.h>

/*  ----------------------------------- This */
#include <dspbridge/mem.h>
#include <dspbridge/list.h>

/*  ----------------------------------- Defines */
#define MEM_512MB   0x1fffffff
#define memInfoSign 0x464E494D	/* "MINF" (in reverse). */

#ifdef DEBUG
#define MEM_CHECK		/* Use to detect source of memory leaks */
#endif

/*  ----------------------------------- Globals */
#if GT_TRACE
static struct GT_Mask MEM_debugMask = { NULL, NULL };	/* GT trace variable */
#endif

static u32 cRefs;		/* module reference count */

static bool extPhysMemPoolEnabled;

struct extPhysMemPool {
	u32 physMemBase;
	u32 physMemSize;
	u32 virtMemBase;
	u32 nextPhysAllocPtr;
};

static struct extPhysMemPool extMemPool;

/*  Information about each element allocated on heap */
struct memInfo {
	struct LST_ELEM link;		/* Must be first */
	size_t size;
	void *caller;
	u32 dwSignature;	/* Should be last */
};

#ifdef MEM_CHECK

/*
 *  This structure holds a linked list to all memory elements allocated on
 *  heap by DSP/BIOS Bridge. This is used to report memory leaks and free
 *  such elements while removing the DSP/BIOS Bridge driver
 */
struct memMan {
	struct LST_LIST lst;
	spinlock_t lock;
};

static struct memMan mMan;

/*
 *  These functions are similar to LST_PutTail and LST_RemoveElem and are
 *  duplicated here to make MEM independent of LST
 */
static inline void MLST_PutTail(struct LST_LIST *pList, struct LST_ELEM *pElem)
{
	pElem->prev = pList->head.prev;
	pElem->next = &pList->head;
	pList->head.prev = pElem;
	pElem->prev->next = pElem;
	pElem->self = pElem;
}

static inline void MLST_RemoveElem(struct LST_LIST *pList,
				   struct LST_ELEM *pCurElem)
{
	pCurElem->prev->next = pCurElem->next;
	pCurElem->next->prev = pCurElem->prev;
	pCurElem->next = NULL;
	pCurElem->prev = NULL;
}

static void MEM_Check(void)
{
	struct memInfo *pMem;
	struct LST_ELEM *last = &mMan.lst.head;
	struct LST_ELEM *curr = mMan.lst.head.next;

	if (!LST_IsEmpty(&mMan.lst)) {
		GT_0trace(MEM_debugMask, GT_7CLASS, "*** MEMORY LEAK ***\n");
		GT_0trace(MEM_debugMask, GT_7CLASS,
			  "Addr      Size      Caller\n");
		while (curr != last) {
			pMem = (struct memInfo *)curr;
			curr = curr->next;
			if ((u32)pMem > PAGE_OFFSET &&
			    MEM_IsValidHandle(pMem, memInfoSign)) {
				GT_3trace(MEM_debugMask, GT_7CLASS,
					"%lx  %d\t [<%p>]\n",
					(u32) pMem + sizeof(struct memInfo),
					pMem->size, pMem->caller);
				MLST_RemoveElem(&mMan.lst,
						(struct LST_ELEM *) pMem);
				kfree(pMem);
			} else {
				GT_1trace(MEM_debugMask, GT_7CLASS,
					  "Invalid allocation or "
					  "Buffer underflow at %x\n",
					  (u32)pMem +	sizeof(struct memInfo));
				break;
			}
		}
	}
	DBC_Ensure(LST_IsEmpty(&mMan.lst));
}

#endif

void MEM_ExtPhysPoolInit(u32 poolPhysBase, u32 poolSize)
{
	u32 poolVirtBase;

	/* get the virtual address for the physical memory pool passed */
	poolVirtBase = (u32)ioremap(poolPhysBase, poolSize);

	if ((void **)poolVirtBase == NULL) {
		GT_0trace(MEM_debugMask, GT_7CLASS,
			  "[PHYS_POOL]Mapping External "
			  "physical memory to virt failed \n");
		extPhysMemPoolEnabled = false;
	} else {
		extMemPool.physMemBase = poolPhysBase;
		extMemPool.physMemSize = poolSize;
		extMemPool.virtMemBase = poolVirtBase;
		extMemPool.nextPhysAllocPtr = poolPhysBase;
		extPhysMemPoolEnabled = true;
		GT_3trace(MEM_debugMask, GT_1CLASS,
			  "ExtMemory Pool details " "Pool"
			  "Physical mem base = %0x " "Pool Physical mem size "
			  "= %0x" "Pool Virtual mem base = %0x \n",
			  poolPhysBase, poolSize, poolVirtBase);
	}
}

static void MEM_ExtPhysPoolRelease(void)
{
	GT_0trace(MEM_debugMask, GT_1CLASS,
		  "Releasing External memory pool \n");
	if (extPhysMemPoolEnabled) {
		iounmap((void *)(extMemPool.virtMemBase));
		extPhysMemPoolEnabled = false;
	}
}

/*
 *  ======== MEM_ExtPhysMemAlloc ========
 *  Purpose:
 *     Allocate physically contiguous, uncached memory from external memory pool
 */

static void *MEM_ExtPhysMemAlloc(u32 bytes, u32 align, OUT u32 *pPhysAddr)
{
	u32 newAllocPtr;
	u32 offset;
	u32 virtAddr;

	GT_2trace(MEM_debugMask, GT_1CLASS,
		  "Ext Memory Allocation" "bytes=0x%x , "
		  "align=0x%x \n", bytes, align);
	if (align == 0) {
		GT_0trace(MEM_debugMask, GT_7CLASS,
			  "ExtPhysical Memory Allocation "
			  "No alignment request in allocation call !! \n");
		align = 1;
	}
	if (bytes > ((extMemPool.physMemBase + extMemPool.physMemSize)
	    - extMemPool.nextPhysAllocPtr)) {
		GT_1trace(MEM_debugMask, GT_7CLASS,
			  "ExtPhysical Memory Allocation "
			  "unable to allocate memory for bytes = 0x%x \n",
			  bytes);
		pPhysAddr = NULL;
		return NULL;
	} else {
		offset = (extMemPool.nextPhysAllocPtr & (align - 1));
		if (offset == 0)
			newAllocPtr = extMemPool.nextPhysAllocPtr;
		else
			newAllocPtr = (extMemPool.nextPhysAllocPtr) +
				      (align - offset);
		if ((newAllocPtr + bytes) <=
		    (extMemPool.physMemBase + extMemPool.physMemSize)) {
			/* we can allocate */
			*pPhysAddr = newAllocPtr;
			extMemPool.nextPhysAllocPtr = newAllocPtr + bytes;
			virtAddr = extMemPool.virtMemBase + (newAllocPtr -
				   extMemPool.physMemBase);
			GT_2trace(MEM_debugMask, GT_1CLASS,
				  "Ext Memory Allocation succedded "
				  "phys address=0x%x , virtaddress=0x%x \n",
				  newAllocPtr, virtAddr);
			return (void *)virtAddr;
		} else {
			*pPhysAddr = 0;
			return NULL;
		}
	}
}

/*
 *  ======== MEM_Alloc ========
 *  Purpose:
 *      Allocate memory from the paged or non-paged pools.
 */
void *MEM_Alloc(u32 cBytes, enum MEM_POOLATTRS type)
{
	struct memInfo *pMem = NULL;

	GT_2trace(MEM_debugMask, GT_ENTER,
		  "MEM_Alloc: cBytes 0x%x\ttype 0x%x\n", cBytes, type);
	if (cBytes > 0) {
		switch (type) {
		case MEM_NONPAGED:
		/* If non-paged memory required, see note at top of file. */
		case MEM_PAGED:
#ifndef MEM_CHECK
			pMem = kmalloc(cBytes,
				(in_atomic()) ? GFP_ATOMIC : GFP_KERNEL);
#else
			pMem = kmalloc(cBytes + sizeof(struct memInfo),
				(in_atomic()) ? GFP_ATOMIC : GFP_KERNEL);
			if (pMem) {
				pMem->size = cBytes;
				pMem->caller = __builtin_return_address(0);
				pMem->dwSignature = memInfoSign;

				spin_lock(&mMan.lock);
				MLST_PutTail(&mMan.lst,
					    (struct LST_ELEM *)pMem);
				spin_unlock(&mMan.lock);

				pMem = (void *)((u32)pMem +
					sizeof(struct memInfo));
			}
#endif
			break;
		case MEM_LARGEVIRTMEM:
#ifndef MEM_CHECK
			pMem = vmalloc(cBytes);
#else
			pMem = vmalloc(cBytes + sizeof(struct memInfo));
			if (pMem) {
				pMem->size = cBytes;
				pMem->caller = __builtin_return_address(0);
				pMem->dwSignature = memInfoSign;

				spin_lock(&mMan.lock);
				MLST_PutTail(&mMan.lst,
					    (struct LST_ELEM *) pMem);
				spin_unlock(&mMan.lock);

				pMem = (void *)((u32)pMem +
					sizeof(struct memInfo));
			}
#endif
			break;

		default:
			GT_0trace(MEM_debugMask, GT_6CLASS,
				  "MEM_Alloc: unexpected "
				  "MEM_POOLATTRS value\n");
			break;
		}
	}

	return pMem;
}

/*
 *  ======== MEM_AllocPhysMem ========
 *  Purpose:
 *      Allocate physically contiguous, uncached memory
 */
void *MEM_AllocPhysMem(u32 cBytes, u32 ulAlign, OUT u32 *pPhysicalAddress)
{
	void *pVaMem = NULL;
	dma_addr_t paMem;

	DBC_Require(cRefs > 0);

	GT_2trace(MEM_debugMask, GT_ENTER,
		  "MEM_AllocPhysMem: cBytes 0x%x\tulAlign"
		  "0x%x\n", cBytes, ulAlign);

	if (cBytes > 0) {
		if (extPhysMemPoolEnabled) {
			pVaMem = MEM_ExtPhysMemAlloc(cBytes, ulAlign,
						    (u32 *)&paMem);
		} else
			pVaMem = dma_alloc_coherent(NULL, cBytes, &paMem,
				(in_atomic()) ? GFP_ATOMIC : GFP_KERNEL);
		if (pVaMem == NULL) {
			*pPhysicalAddress = 0;
			GT_1trace(MEM_debugMask, GT_6CLASS,
				  "MEM_AllocPhysMem failed: "
				  "0x%x\n", pVaMem);
		} else {
			*pPhysicalAddress = paMem;
		}
	}
	return pVaMem;
}

/*
 *  ======== MEM_Calloc ========
 *  Purpose:
 *      Allocate zero-initialized memory from the paged or non-paged pools.
 */
void *MEM_Calloc(u32 cBytes, enum MEM_POOLATTRS type)
{
	struct memInfo *pMem = NULL;

	GT_2trace(MEM_debugMask, GT_ENTER,
		  "MEM_Calloc: cBytes 0x%x\ttype 0x%x\n",
		  cBytes, type);

	if (cBytes > 0) {
		switch (type) {
		case MEM_NONPAGED:
		/* If non-paged memory required, see note at top of file. */
		case MEM_PAGED:
#ifndef MEM_CHECK
			pMem = kmalloc(cBytes,
				(in_atomic()) ? GFP_ATOMIC : GFP_KERNEL);
			if (pMem)
				memset(pMem, 0, cBytes);

#else
			pMem = kmalloc(cBytes + sizeof(struct memInfo),
				(in_atomic()) ? GFP_ATOMIC : GFP_KERNEL);
			if (pMem) {
				memset((void *)((u32)pMem +
					sizeof(struct memInfo)), 0, cBytes);
				pMem->size = cBytes;
				pMem->caller = __builtin_return_address(0);
				pMem->dwSignature = memInfoSign;
				spin_lock(&mMan.lock);
				MLST_PutTail(&mMan.lst,
					(struct LST_ELEM *) pMem);
				spin_unlock(&mMan.lock);
				pMem = (void *)((u32)pMem +
					sizeof(struct memInfo));
			}
#endif
			break;
		case MEM_LARGEVIRTMEM:
#ifndef MEM_CHECK
			pMem = vmalloc(cBytes);
			if (pMem)
				memset(pMem, 0, cBytes);
#else
			pMem = vmalloc(cBytes + sizeof(struct memInfo));
			if (pMem) {
				memset((void *)((u32)pMem +
					sizeof(struct memInfo)), 0, cBytes);
				pMem->size = cBytes;
				pMem->caller = __builtin_return_address(0);
				pMem->dwSignature = memInfoSign;
				spin_lock(&mMan.lock);
				MLST_PutTail(&mMan.lst, (struct LST_ELEM *)
					pMem);
				spin_unlock(&mMan.lock);
				pMem = (void *)((u32)pMem +
					sizeof(struct memInfo));
			}
#endif
			break;
		default:
			GT_1trace(MEM_debugMask, GT_6CLASS,
				  "MEM_Calloc: unexpected "
				  "MEM_POOLATTRS value 0x%x\n", type);
			break;
		}
	}

	return pMem;
}

/*
 *  ======== MEM_Exit ========
 *  Purpose:
 *      Discontinue usage of the MEM module.
 */
void MEM_Exit(void)
{
	DBC_Require(cRefs > 0);

	GT_1trace(MEM_debugMask, GT_5CLASS, "MEM_Exit: cRefs 0x%x\n", cRefs);

	cRefs--;
#ifdef MEM_CHECK
	if (cRefs == 0)
		MEM_Check();

#endif
	MEM_ExtPhysPoolRelease();
	DBC_Ensure(cRefs >= 0);
}

/*
 *  ======== MEM_FlushCache ========
 *  Purpose:
 *      Flush cache
 */
void MEM_FlushCache(void *pMemBuf, u32 cBytes, s32 FlushType)
{
	DBC_Require(cRefs > 0);

	switch (FlushType) {
	/* invalidate only */
	case PROC_INVALIDATE_MEM:
		dmac_inv_range(pMemBuf, pMemBuf + cBytes);
		outer_inv_range(__pa((u32)pMemBuf), __pa((u32)pMemBuf +
				cBytes));
	break;
	/* writeback only */
	case PROC_WRITEBACK_MEM:
		dmac_clean_range(pMemBuf, pMemBuf + cBytes);
		outer_clean_range(__pa((u32)pMemBuf), __pa((u32)pMemBuf +
				  cBytes));
	break;
	/* writeback and invalidate */
	case PROC_WRITEBACK_INVALIDATE_MEM:
		dmac_flush_range(pMemBuf, pMemBuf + cBytes);
		outer_flush_range(__pa((u32)pMemBuf), __pa((u32)pMemBuf +
				  cBytes));
	break;
	default:
		GT_1trace(MEM_debugMask, GT_6CLASS, "MEM_FlushCache: invalid "
			  "FlushMemType 0x%x\n", FlushType);
	break;
	}

}

/*
 *  ======== MEM_VFree ========
 *  Purpose:
 *      Free the given block of system memory in virtual space.
 */
void MEM_VFree(IN void *pMemBuf)
{
#ifdef MEM_CHECK
	struct memInfo *pMem = (void *)((u32)pMemBuf - sizeof(struct memInfo));
#endif

	DBC_Require(pMemBuf != NULL);

	GT_1trace(MEM_debugMask, GT_ENTER, "MEM_VFree: pMemBufs 0x%x\n",
		  pMemBuf);

	if (pMemBuf) {
#ifndef MEM_CHECK
		vfree(pMemBuf);
#else
		if (pMem) {
			if (pMem->dwSignature == memInfoSign) {
				spin_lock(&mMan.lock);
				MLST_RemoveElem(&mMan.lst,
						(struct LST_ELEM *) pMem);
				spin_unlock(&mMan.lock);
				pMem->dwSignature = 0;
				vfree(pMem);
			} else {
				GT_1trace(MEM_debugMask, GT_7CLASS,
					"Invalid allocation or "
					"Buffer underflow at %x\n",
					(u32) pMem + sizeof(struct memInfo));
			}
		}
#endif
	}
}

/*
 *  ======== MEM_Free ========
 *  Purpose:
 *      Free the given block of system memory.
 */
void MEM_Free(IN void *pMemBuf)
{
#ifdef MEM_CHECK
	struct memInfo *pMem = (void *)((u32)pMemBuf - sizeof(struct memInfo));
#endif

	DBC_Require(pMemBuf != NULL);

	GT_1trace(MEM_debugMask, GT_ENTER, "MEM_Free: pMemBufs 0x%x\n",
		  pMemBuf);

	if (pMemBuf) {
#ifndef MEM_CHECK
		kfree(pMemBuf);
#else
		if (pMem) {
			if (pMem->dwSignature == memInfoSign) {
				spin_lock(&mMan.lock);
				MLST_RemoveElem(&mMan.lst,
						(struct LST_ELEM *) pMem);
				spin_unlock(&mMan.lock);
				pMem->dwSignature = 0;
				kfree(pMem);
			} else {
				GT_1trace(MEM_debugMask, GT_7CLASS,
					"Invalid allocation or "
					"Buffer underflow at %x\n",
					(u32) pMem + sizeof(struct memInfo));
			}
		}
#endif
	}
}

/*
 *  ======== MEM_FreePhysMem ========
 *  Purpose:
 *      Free the given block of physically contiguous memory.
 */
void MEM_FreePhysMem(void *pVirtualAddress, u32 pPhysicalAddress,
		     u32 cBytes)
{
	DBC_Require(cRefs > 0);
	DBC_Require(pVirtualAddress != NULL);

	GT_1trace(MEM_debugMask, GT_ENTER, "MEM_FreePhysMem: pVirtualAddress "
		  "0x%x\n", pVirtualAddress);

	if (!extPhysMemPoolEnabled)
		dma_free_coherent(NULL, cBytes, pVirtualAddress,
				 pPhysicalAddress);
}

/*
 *  ======== MEM_Init ========
 *  Purpose:
 *      Initialize MEM module private state.
 */
bool MEM_Init(void)
{
	DBC_Require(cRefs >= 0);

	if (cRefs == 0) {
		GT_create(&MEM_debugMask, "MM");	/* MM for MeM module */

#ifdef MEM_CHECK
		mMan.lst.head.next = &mMan.lst.head;
		mMan.lst.head.prev = &mMan.lst.head;
		mMan.lst.head.self = NULL;
		spin_lock_init(&mMan.lock);
#endif

	}

	cRefs++;

	GT_1trace(MEM_debugMask, GT_5CLASS, "MEM_Init: cRefs 0x%x\n", cRefs);

	DBC_Ensure(cRefs > 0);

	return true;
}
