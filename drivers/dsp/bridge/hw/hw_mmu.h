/*
 * hw_mmu.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Copyright (C) 2007 Texas Instruments, Inc.
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
 *  ======== hw_mmu.h ========
 *  Description:
 *      MMU types and API declarations
 *
 *! Revision History:
 *! ================
 *! 19-Apr-2004 sb  Moved & renamed endianness, page size, element size
		    TLBAdd takes in MMUMapAttrs instead of separate arguments
 *! 08-Mar-2004 sb  Added the Page Table management APIs
 *! 16 Feb 2003 sb: Initial version
 */
#ifndef __HW_MMU_H
#define __HW_MMU_H

#include <linux/types.h>

/* Bitmasks for interrupt sources */
#define HW_MMU_TRANSLATION_FAULT   0x2
#define HW_MMU_ALL_INTERRUPTS      0x1F

#define HW_MMU_COARSE_PAGE_SIZE 0x400

/* HW_MMUMixedSize_t:  Enumerated Type used to specify whether to follow
			CPU/TLB Element size */
enum HW_MMUMixedSize_t {
	HW_MMU_TLBES,
	HW_MMU_CPUES

} ;

/* HW_MMUMapAttrs_t:  Struct containing MMU mapping attributes */
struct HW_MMUMapAttrs_t {
	enum HW_Endianism_t     endianism;
	enum HW_ElementSize_t   elementSize;
	enum HW_MMUMixedSize_t  mixedSize;
	bool donotlockmpupage;
} ;

extern HW_STATUS HW_MMU_Enable(const void __iomem *baseAddress);

extern HW_STATUS HW_MMU_Disable(const void __iomem *baseAddress);

extern HW_STATUS HW_MMU_NumLockedSet(const void __iomem *baseAddress,
					u32 numLockedEntries);

extern HW_STATUS HW_MMU_VictimNumSet(const void __iomem *baseAddress,
					u32 victimEntryNum);

/* For MMU faults */
extern HW_STATUS HW_MMU_EventAck(const void __iomem *baseAddress,
				    u32 irqMask);

extern HW_STATUS HW_MMU_EventDisable(const void __iomem *baseAddress,
					u32 irqMask);

extern HW_STATUS HW_MMU_EventEnable(const void __iomem *baseAddress,
				       u32 irqMask);

extern HW_STATUS HW_MMU_EventStatus(const void __iomem *baseAddress,
				       u32 *irqMask);

extern HW_STATUS HW_MMU_FaultAddrRead(const void __iomem *baseAddress,
					 u32 *addr);

/* Set the TT base address */
extern HW_STATUS HW_MMU_TTBSet(const void __iomem *baseAddress,
				  u32 TTBPhysAddr);

extern HW_STATUS HW_MMU_TWLEnable(const void __iomem *baseAddress);

extern HW_STATUS HW_MMU_TWLDisable(const void __iomem *baseAddress);

extern HW_STATUS HW_MMU_TLBFlush(const void __iomem *baseAddress,
				    u32 virtualAddr,
				    u32 pageSize);

extern HW_STATUS HW_MMU_TLBAdd(const void __iomem *baseAddress,
				  u32	   physicalAddr,
				  u32	   virtualAddr,
				  u32	   pageSize,
				  u32	    entryNum,
				  struct HW_MMUMapAttrs_t *mapAttrs,
				  enum HW_SetClear_t    preservedBit,
				  enum HW_SetClear_t    validBit);


/* For PTEs */
extern HW_STATUS HW_MMU_PteSet(const u32     pgTblVa,
				  u32	   physicalAddr,
				  u32	   virtualAddr,
				  u32	   pageSize,
				  struct HW_MMUMapAttrs_t *mapAttrs);

extern HW_STATUS HW_MMU_PteClear(const u32   pgTblVa,
				    u32	 pgSize,
				    u32	 virtualAddr);

static inline u32 HW_MMU_PteAddrL1(u32 L1_base, u32 va)
{
	u32 pteAddr;
	u32 VA_31_to_20;

	VA_31_to_20  = va >> (20 - 2); /* Left-shift by 2 here itself */
	VA_31_to_20 &= 0xFFFFFFFCUL;
	pteAddr = L1_base + VA_31_to_20;

	return pteAddr;
}

static inline u32 HW_MMU_PteAddrL2(u32 L2_base, u32 va)
{
	u32 pteAddr;

	pteAddr = (L2_base & 0xFFFFFC00) | ((va >> 10) & 0x3FC);

	return pteAddr;
}

static inline u32 HW_MMU_PteCoarseL1(u32 pteVal)
{
	u32 pteCoarse;

	pteCoarse = pteVal & 0xFFFFFC00;

	return pteCoarse;
}

static inline u32 HW_MMU_PteSizeL1(u32 pteVal)
{
	u32 pteSize = 0;

	if ((pteVal & 0x3) == 0x1) {
		/* Points to L2 PT */
		pteSize = HW_MMU_COARSE_PAGE_SIZE;
	}

	if ((pteVal & 0x3) == 0x2) {
		if (pteVal & (1 << 18))
			pteSize = HW_PAGE_SIZE_16MB;
		else
			pteSize = HW_PAGE_SIZE_1MB;
	}

	return pteSize;
}

static inline u32 HW_MMU_PteSizeL2(u32 pteVal)
{
    u32 pteSize = 0;

    if (pteVal & 0x2)
	pteSize = HW_PAGE_SIZE_4KB;
    else if (pteVal & 0x1)
	pteSize = HW_PAGE_SIZE_64KB;

    return pteSize;
}

#endif  /* __HW_MMU_H */
