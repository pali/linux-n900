/*
 * rmm.c
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
 *  ======== rmm.c ========
 *  Description:
 *
 *  This memory manager provides general heap management and arbitrary
 *  alignment for any number of memory segments.
 *
 *  Notes:
 *
 *  Memory blocks are allocated from the end of the first free memory
 *  block large enough to satisfy the request.  Alignment requirements
 *  are satisfied by "sliding" the block forward until its base satisfies
 *  the alignment specification; if this is not possible then the next
 *  free block large enough to hold the request is tried.
 *
 *  Since alignment can cause the creation of a new free block - the
 *  unused memory formed between the start of the original free block
 *  and the start of the allocated block - the memory manager must free
 *  this memory to prevent a memory leak.
 *
 *  Overlay memory is managed by reserving through RMM_alloc, and freeing
 *  it through RMM_free. The memory manager prevents DSP code/data that is
 *  overlayed from being overwritten as long as the memory it runs at has
 *  been allocated, and not yet freed.
 *
 *! Revision History
 *! ================
 *! 18-Feb-2003 vp  Code review updates.
 *! 18-Oct-2002 vp  Ported to Linux Platform.
 *! 24-Sep-2002 map Updated from Code Review
 *! 25-Jun-2002 jeh     Free from segid passed to RMM_free().
 *! 24-Apr-2002 jeh     Determine segid based on address in RMM_free(). (No way
 *!                     to keep track of segid with dynamic loader library.)
 *! 16-Oct-2001 jeh     Based on gen tree rm.c. Added support for overlays.
 */

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>
#include <dspbridge/dbdefs.h>
#include <dspbridge/errbase.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>
#include <dspbridge/gt.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/list.h>
#include <dspbridge/mem.h>

/*  ----------------------------------- This */
#include <dspbridge/rmm.h>

#define RMM_TARGSIGNATURE   0x544d4d52	/* "TMMR" */

/*
 *  ======== RMM_Header ========
 *  This header is used to maintain a list of free memory blocks.
 */
struct RMM_Header {
	struct RMM_Header *next;	/* form a free memory link list */
	u32 size;		/* size of the free memory */
	u32 addr;		/* DSP address of memory block */
} ;

/*
 *  ======== RMM_OvlySect ========
 *  Keeps track of memory occupied by overlay section.
 */
struct RMM_OvlySect {
	struct LST_ELEM listElem;
	u32 addr;		/* Start of memory section */
	u32 size;		/* Length (target MAUs) of section */
	s32 page;		/* Memory page */
};

/*
 *  ======== RMM_TargetObj ========
 */
struct RMM_TargetObj {
	u32 dwSignature;
	struct RMM_Segment *segTab;
	struct RMM_Header **freeList;
	u32 numSegs;
	struct LST_LIST *ovlyList;	/* List of overlay memory in use */
};

#if GT_TRACE
static struct GT_Mask RMM_debugMask = { NULL, NULL };	/* GT trace variable */
#endif

static u32 cRefs;		/* module reference count */

static bool allocBlock(struct RMM_TargetObj *target, u32 segid, u32 size,
		      u32 align, u32 *dspAddr);
static bool freeBlock(struct RMM_TargetObj *target, u32 segid, u32 addr,
		     u32 size);

/*
 *  ======== RMM_alloc ========
 */
DSP_STATUS RMM_alloc(struct RMM_TargetObj *target, u32 segid, u32 size,
		    u32 align, u32 *dspAddr, bool reserve)
{
	struct RMM_OvlySect *sect;
	struct RMM_OvlySect *prevSect = NULL;
	struct RMM_OvlySect *newSect;
	u32 addr;
	DSP_STATUS status = DSP_SOK;

	DBC_Require(MEM_IsValidHandle(target, RMM_TARGSIGNATURE));
	DBC_Require(dspAddr != NULL);
	DBC_Require(size > 0);
	DBC_Require(reserve || (target->numSegs > 0));
	DBC_Require(cRefs > 0);

	GT_6trace(RMM_debugMask, GT_ENTER,
		 "RMM_alloc(0x%lx, 0x%lx, 0x%lx, 0x%lx, "
		 "0x%lx, 0x%lx)\n", target, segid, size, align, dspAddr,
		 reserve);
	if (!reserve) {
		if (!allocBlock(target, segid, size, align, dspAddr)) {
			status = DSP_EMEMORY;
		} else {
			/* Increment the number of allocated blocks in this
			 * segment */
			target->segTab[segid].number++;
		}
		goto func_end;
	}
	/* An overlay section - See if block is already in use. If not,
	 * insert into the list in ascending address size.  */
	addr = *dspAddr;
	sect = (struct RMM_OvlySect *)LST_First(target->ovlyList);
	/*  Find place to insert new list element. List is sorted from
	 *  smallest to largest address. */
	while (sect != NULL) {
		if (addr <= sect->addr) {
			/* Check for overlap with sect */
			if ((addr + size > sect->addr) || (prevSect &&
			   (prevSect->addr + prevSect->size > addr))) {
				status = DSP_EOVERLAYMEMORY;
			}
			break;
		}
		prevSect = sect;
		sect = (struct RMM_OvlySect *)LST_Next(target->ovlyList,
			(struct LST_ELEM *)sect);
	}
	if (DSP_SUCCEEDED(status)) {
		/* No overlap - allocate list element for new section. */
		newSect = MEM_Calloc(sizeof(struct RMM_OvlySect), MEM_PAGED);
		if (newSect == NULL) {
			status = DSP_EMEMORY;
		} else {
			LST_InitElem((struct LST_ELEM *)newSect);
			newSect->addr = addr;
			newSect->size = size;
			newSect->page = segid;
			if (sect == NULL) {
				/* Put new section at the end of the list */
				LST_PutTail(target->ovlyList,
					   (struct LST_ELEM *)newSect);
			} else {
				/* Put new section just before sect */
				LST_InsertBefore(target->ovlyList,
						(struct LST_ELEM *)newSect,
						(struct LST_ELEM *)sect);
			}
		}
	}
func_end:
	return status;
}

/*
 *  ======== RMM_create ========
 */
DSP_STATUS RMM_create(struct RMM_TargetObj **pTarget,
		     struct RMM_Segment segTab[], u32 numSegs)
{
	struct RMM_Header *hptr;
	struct RMM_Segment *sptr, *tmp;
	struct RMM_TargetObj *target;
	s32 i;
	DSP_STATUS status = DSP_SOK;

	DBC_Require(pTarget != NULL);
	DBC_Require(numSegs == 0 || segTab != NULL);

	GT_3trace(RMM_debugMask, GT_ENTER,
		 "RMM_create(0x%lx, 0x%lx, 0x%lx)\n",
		 pTarget, segTab, numSegs);

	/* Allocate DBL target object */
	MEM_AllocObject(target, struct RMM_TargetObj, RMM_TARGSIGNATURE);

	if (target == NULL) {
		GT_0trace(RMM_debugMask, GT_6CLASS,
			 "RMM_create: Memory allocation failed\n");
		status = DSP_EMEMORY;
	}
	if (DSP_FAILED(status))
		goto func_cont;

	target->numSegs = numSegs;
	if (!(numSegs > 0))
		goto func_cont;

	/* Allocate the memory for freelist from host's memory */
	target->freeList = MEM_Calloc(numSegs * sizeof(struct RMM_Header *),
				     MEM_PAGED);
	if (target->freeList == NULL) {
		GT_0trace(RMM_debugMask, GT_6CLASS,
			 "RMM_create: Memory allocation failed\n");
		status = DSP_EMEMORY;
	} else {
		/* Allocate headers for each element on the free list */
		for (i = 0; i < (s32) numSegs; i++) {
			target->freeList[i] =
					MEM_Calloc(sizeof(struct RMM_Header),
					MEM_PAGED);
			if (target->freeList[i] == NULL) {
				GT_0trace(RMM_debugMask, GT_6CLASS,
					 "RMM_create: Memory "
					 "allocation failed\n");
				status = DSP_EMEMORY;
				break;
			}
		}
		/* Allocate memory for initial segment table */
		target->segTab = MEM_Calloc(numSegs *
				 sizeof(struct RMM_Segment), MEM_PAGED);
		if (target->segTab == NULL) {
			GT_0trace(RMM_debugMask, GT_6CLASS,
				 "RMM_create: Memory allocation failed\n");
			status = DSP_EMEMORY;
		} else {
			/* Initialize segment table and free list */
			sptr = target->segTab;
			for (i = 0, tmp = segTab; numSegs > 0; numSegs--, i++) {
				*sptr = *tmp;
				hptr = target->freeList[i];
				hptr->addr = tmp->base;
				hptr->size = tmp->length;
				hptr->next = NULL;
				tmp++;
				sptr++;
			}
		}
	}
func_cont:
	/* Initialize overlay memory list */
	if (DSP_SUCCEEDED(status)) {
		target->ovlyList = LST_Create();
		if (target->ovlyList == NULL) {
			GT_0trace(RMM_debugMask, GT_6CLASS,
				 "RMM_create: Memory allocation failed\n");
			status = DSP_EMEMORY;
		}
	}

	if (DSP_SUCCEEDED(status)) {
		*pTarget = target;
	} else {
		*pTarget = NULL;
		if (target)
			RMM_delete(target);

	}

	DBC_Ensure((DSP_SUCCEEDED(status) && MEM_IsValidHandle((*pTarget),
		  RMM_TARGSIGNATURE)) || (DSP_FAILED(status) && *pTarget ==
		  NULL));

	return status;
}

/*
 *  ======== RMM_delete ========
 */
void RMM_delete(struct RMM_TargetObj *target)
{
	struct RMM_OvlySect *pSect;
	struct RMM_Header *hptr;
	struct RMM_Header *next;
	u32 i;

	DBC_Require(MEM_IsValidHandle(target, RMM_TARGSIGNATURE));

	GT_1trace(RMM_debugMask, GT_ENTER, "RMM_delete(0x%lx)\n", target);

	if (target->segTab != NULL)
		MEM_Free(target->segTab);

	if (target->ovlyList) {
		while ((pSect = (struct RMM_OvlySect *)LST_GetHead
		      (target->ovlyList))) {
			MEM_Free(pSect);
		}
		DBC_Assert(LST_IsEmpty(target->ovlyList));
		LST_Delete(target->ovlyList);
	}

	if (target->freeList != NULL) {
		/* Free elements on freelist */
		for (i = 0; i < target->numSegs; i++) {
			hptr = next = target->freeList[i];
			while (next) {
				hptr = next;
				next = hptr->next;
				MEM_Free(hptr);
			}
		}
		MEM_Free(target->freeList);
	}

	MEM_FreeObject(target);
}

/*
 *  ======== RMM_exit ========
 */
void RMM_exit(void)
{
	DBC_Require(cRefs > 0);

	cRefs--;

	GT_1trace(RMM_debugMask, GT_5CLASS, "RMM_exit() ref count: 0x%x\n",
		 cRefs);

	if (cRefs == 0)
		MEM_Exit();

	DBC_Ensure(cRefs >= 0);
}

/*
 *  ======== RMM_free ========
 */
bool RMM_free(struct RMM_TargetObj *target, u32 segid, u32 addr, u32 size,
	bool reserved)

{
	struct RMM_OvlySect *sect;
	bool retVal = true;

	DBC_Require(MEM_IsValidHandle(target, RMM_TARGSIGNATURE));

	DBC_Require(reserved || segid < target->numSegs);
	DBC_Require(reserved || (addr >= target->segTab[segid].base &&
		   (addr + size) <= (target->segTab[segid].base +
		   target->segTab[segid].length)));

	GT_5trace(RMM_debugMask, GT_ENTER,
		 "RMM_free(0x%lx, 0x%lx, 0x%lx, 0x%lx, "
		 "0x%lx)\n", target, segid, addr, size, reserved);
	/*
	 *  Free or unreserve memory.
	 */
	if (!reserved) {
		retVal = freeBlock(target, segid, addr, size);
		if (retVal)
			target->segTab[segid].number--;

	} else {
		/* Unreserve memory */
		sect = (struct RMM_OvlySect *)LST_First(target->ovlyList);
		while (sect != NULL) {
			if (addr == sect->addr) {
				DBC_Assert(size == sect->size);
				/* Remove from list */
				LST_RemoveElem(target->ovlyList,
					      (struct LST_ELEM *)sect);
				MEM_Free(sect);
				break;
			}
			sect = (struct RMM_OvlySect *)LST_Next(target->ovlyList,
			       (struct LST_ELEM *)sect);
		}
		if (sect == NULL)
			retVal = false;

	}
	return retVal;
}

/*
 *  ======== RMM_init ========
 */
bool RMM_init(void)
{
	bool retVal = true;

	DBC_Require(cRefs >= 0);

	if (cRefs == 0) {
		DBC_Assert(!RMM_debugMask.flags);
		GT_create(&RMM_debugMask, "RM");	/* "RM" for RMm */

		retVal = MEM_Init();

		if (!retVal)
			MEM_Exit();

	}

	if (retVal)
		cRefs++;

	GT_1trace(RMM_debugMask, GT_5CLASS,
		 "RMM_init(), ref count:  0x%x\n",
		 cRefs);

	DBC_Ensure((retVal && (cRefs > 0)) || (!retVal && (cRefs >= 0)));

	return retVal;
}

/*
 *  ======== RMM_stat ========
 */
bool RMM_stat(struct RMM_TargetObj *target, enum DSP_MEMTYPE segid,
	     struct DSP_MEMSTAT *pMemStatBuf)
{
	struct RMM_Header *head;
	bool retVal = false;
	u32 maxFreeSize = 0;
	u32 totalFreeSize = 0;
	u32 freeBlocks = 0;

	DBC_Require(pMemStatBuf != NULL);
	DBC_Assert(target != NULL);

	if ((u32) segid < target->numSegs) {
		head = target->freeList[segid];

		/* Collect data from freeList */
		while (head != NULL) {
			maxFreeSize = max(maxFreeSize, head->size);
			totalFreeSize += head->size;
			freeBlocks++;
			head = head->next;
		}

		/* ulSize */
		pMemStatBuf->ulSize = target->segTab[segid].length;

		/* ulNumFreeBlocks */
		pMemStatBuf->ulNumFreeBlocks = freeBlocks;

		/* ulTotalFreeSize */
		pMemStatBuf->ulTotalFreeSize = totalFreeSize;

		/* ulLenMaxFreeBlock */
		pMemStatBuf->ulLenMaxFreeBlock = maxFreeSize;

		/* ulNumAllocBlocks */
		pMemStatBuf->ulNumAllocBlocks = target->segTab[segid].number;

		retVal = true;
	}

	return retVal;
}

/*
 *  ======== balloc ========
 *  This allocation function allocates memory from the lowest addresses
 *  first.
 */
static bool allocBlock(struct RMM_TargetObj *target, u32 segid, u32 size,
		      u32 align, u32 *dspAddr)
{
	struct RMM_Header *head;
	struct RMM_Header *prevhead = NULL;
	struct RMM_Header *next;
	u32 tmpalign;
	u32 alignbytes;
	u32 hsize;
	u32 allocsize;
	u32 addr;

	alignbytes = (align == 0) ? 1 : align;
	prevhead = NULL;
	head = target->freeList[segid];

	do {
		hsize = head->size;
		next = head->next;

		addr = head->addr;	/* alloc from the bottom */

		/* align allocation */
		(tmpalign = (u32) addr % alignbytes);
		if (tmpalign != 0)
			tmpalign = alignbytes - tmpalign;

		allocsize = size + tmpalign;

		if (hsize >= allocsize) {	/* big enough */
			if (hsize == allocsize && prevhead != NULL) {
				prevhead->next = next;
				MEM_Free(head);
			} else {
				head->size = hsize - allocsize;
				head->addr += allocsize;
			}

			/* free up any hole created by alignment */
			if (tmpalign)
				freeBlock(target, segid, addr, tmpalign);

			*dspAddr = addr + tmpalign;
			return true;
		}

		prevhead = head;
		head = next;

	} while (head != NULL);

	return false;
}

/*
 *  ======== freeBlock ========
 *  TO DO: freeBlock() allocates memory, which could result in failure.
 *  Could allocate an RMM_Header in RMM_alloc(), to be kept in a pool.
 *  freeBlock() could use an RMM_Header from the pool, freeing as blocks
 *  are coalesced.
 */
static bool freeBlock(struct RMM_TargetObj *target, u32 segid, u32 addr,
		     u32 size)
{
	struct RMM_Header *head;
	struct RMM_Header *thead;
	struct RMM_Header *rhead;
	bool retVal = true;

	/* Create a memory header to hold the newly free'd block. */
	rhead = MEM_Calloc(sizeof(struct RMM_Header), MEM_PAGED);
	if (rhead == NULL) {
		retVal = false;
	} else {
		/* search down the free list to find the right place for addr */
		head = target->freeList[segid];

		if (addr >= head->addr) {
			while (head->next != NULL && addr > head->next->addr)
				head = head->next;

			thead = head->next;

			head->next = rhead;
			rhead->next = thead;
			rhead->addr = addr;
			rhead->size = size;
		} else {
			*rhead = *head;
			head->next = rhead;
			head->addr = addr;
			head->size = size;
			thead = rhead->next;
		}

		/* join with upper block, if possible */
		if (thead != NULL && (rhead->addr + rhead->size) ==
		   thead->addr) {
			head->next = rhead->next;
			thead->size = size + thead->size;
			thead->addr = addr;
			MEM_Free(rhead);
			rhead = thead;
		}

		/* join with the lower block, if possible */
		if ((head->addr + head->size) == rhead->addr) {
			head->next = rhead->next;
			head->size = head->size + rhead->size;
			MEM_Free(rhead);
		}
	}

	return retVal;
}

