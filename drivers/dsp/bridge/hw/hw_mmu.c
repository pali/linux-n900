/*
 * hw_mmu.c
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
 *  ======== hw_mmu.c ========
 *  Description:
 *      API definitions to setup MMU TLB and PTE
 *
 *! Revision History:
 *! ================
 *! 19-Apr-2004 sb  TLBAdd and TLBFlush input the page size in bytes instead
		    of an enum. TLBAdd inputs mapping attributes struct instead
		    of individual arguments.
		    Removed MMU.h and other cosmetic updates.
 *! 08-Mar-2004 sb  Added the Page Table Management APIs
 *! 16 Feb 2003 sb: Initial version
 */

#include <GlobalTypes.h>
#include <linux/io.h>
#include "MMURegAcM.h"
#include <hw_defs.h>
#include <hw_mmu.h>
#include <linux/types.h>

#define MMU_BASE_VAL_MASK	0xFC00
#define MMU_PAGE_MAX	     3
#define MMU_ELEMENTSIZE_MAX      3
#define MMU_ADDR_MASK	    0xFFFFF000
#define MMU_TTB_MASK	     0xFFFFC000
#define MMU_SECTION_ADDR_MASK    0xFFF00000
#define MMU_SSECTION_ADDR_MASK   0xFF000000
#define MMU_PAGE_TABLE_MASK      0xFFFFFC00
#define MMU_LARGE_PAGE_MASK      0xFFFF0000
#define MMU_SMALL_PAGE_MASK      0xFFFFF000

#define MMU_LOAD_TLB	0x00000001

/* HW_MMUPageSize_t:  Enumerated Type used to specify the MMU Page Size(SLSS) */
enum HW_MMUPageSize_t {
    HW_MMU_SECTION,
    HW_MMU_LARGE_PAGE,
    HW_MMU_SMALL_PAGE,
    HW_MMU_SUPERSECTION
} ;

/*
* FUNCTION	      : MMU_FlushEntry
*
* INPUTS:
*
*       Identifier      : baseAddress
*       Type		: const u32
*       Description     : Base Address of instance of MMU module
*
* RETURNS:
*
*       Type		: HW_STATUS
*       Description     : RET_OK		 -- No errors occured
*			 RET_BAD_NULL_PARAM     -- A Pointer
*						Paramater was set to NULL
*
* PURPOSE:	      : Flush the TLB entry pointed by the
*			lock counter register
*			even if this entry is set protected
*
* METHOD:	       : Check the Input parameter and Flush a
*			 single entry in the TLB.
*/
static HW_STATUS MMU_FlushEntry(const void __iomem *baseAddress);

/*
* FUNCTION	      : MMU_SetCAMEntry
*
* INPUTS:
*
*       Identifier      : baseAddress
*       TypE		: const u32
*       Description     : Base Address of instance of MMU module
*
*       Identifier      : pageSize
*       TypE		: const u32
*       Description     : It indicates the page size
*
*       Identifier      : preservedBit
*       Type		: const u32
*       Description     : It indicates the TLB entry is preserved entry
*							or not
*
*       Identifier      : validBit
*       Type		: const u32
*       Description     : It indicates the TLB entry is valid entry or not
*
*
*       Identifier      : virtualAddrTag
*       Type	    	: const u32
*       Description     : virtual Address
*
* RETURNS:
*
*       Type	    	: HW_STATUS
*       Description     : RET_OK		 -- No errors occured
*			 RET_BAD_NULL_PARAM     -- A Pointer Paramater
*						   was set to NULL
*			 RET_PARAM_OUT_OF_RANGE -- Input Parameter out
*						   of Range
*
* PURPOSE:	      	: Set MMU_CAM reg
*
* METHOD:	       	: Check the Input parameters and set the CAM entry.
*/
static HW_STATUS MMU_SetCAMEntry(const void __iomem *baseAddress,
				   const u32    pageSize,
				   const u32    preservedBit,
				   const u32    validBit,
				   const u32    virtualAddrTag);

/*
* FUNCTION	      : MMU_SetRAMEntry
*
* INPUTS:
*
*       Identifier      : baseAddress
*       Type	    	: const u32
*       Description     : Base Address of instance of MMU module
*
*       Identifier      : physicalAddr
*       Type	    	: const u32
*       Description     : Physical Address to which the corresponding
*			 virtual   Address shouldpoint
*
*       Identifier      : endianism
*       Type	    	: HW_Endianism_t
*       Description     : endianism for the given page
*
*       Identifier      : elementSize
*       Type	    	: HW_ElementSize_t
*       Description     : The element size ( 8,16, 32 or 64 bit)
*
*       Identifier      : mixedSize
*       Type	    	: HW_MMUMixedSize_t
*       Description     : Element Size to follow CPU or TLB
*
* RETURNS:
*
*       Type	    	: HW_STATUS
*       Description     : RET_OK		 -- No errors occured
*			 RET_BAD_NULL_PARAM     -- A Pointer Paramater
*							was set to NULL
*			 RET_PARAM_OUT_OF_RANGE -- Input Parameter
*							out of Range
*
* PURPOSE:	      : Set MMU_CAM reg
*
* METHOD:	       : Check the Input parameters and set the RAM entry.
*/
static HW_STATUS MMU_SetRAMEntry(const void __iomem *baseAddress,
				   const u32	physicalAddr,
				   enum HW_Endianism_t      endianism,
				   enum HW_ElementSize_t    elementSize,
				   enum HW_MMUMixedSize_t   mixedSize);

/* HW FUNCTIONS */

HW_STATUS HW_MMU_Enable(const void __iomem *baseAddress)
{
    HW_STATUS status = RET_OK;

    MMUMMU_CNTLMMUEnableWrite32(baseAddress, HW_SET);

    return status;
}

HW_STATUS HW_MMU_Disable(const void __iomem *baseAddress)
{
    HW_STATUS status = RET_OK;

    MMUMMU_CNTLMMUEnableWrite32(baseAddress, HW_CLEAR);

    return status;
}

HW_STATUS HW_MMU_NumLockedSet(const void __iomem *baseAddress,
				u32 numLockedEntries)
{
    HW_STATUS status = RET_OK;

    MMUMMU_LOCKBaseValueWrite32(baseAddress, numLockedEntries);

    return status;
}

HW_STATUS HW_MMU_VictimNumSet(const void __iomem *baseAddress,
				u32 victimEntryNum)
{
    HW_STATUS status = RET_OK;

    MMUMMU_LOCKCurrentVictimWrite32(baseAddress, victimEntryNum);

    return status;
}

HW_STATUS HW_MMU_EventAck(const void __iomem *baseAddress, u32 irqMask)
{
    HW_STATUS status = RET_OK;

    MMUMMU_IRQSTATUSWriteRegister32(baseAddress, irqMask);

    return status;
}

HW_STATUS HW_MMU_EventDisable(const void __iomem *baseAddress,
				u32 irqMask)
{
    HW_STATUS status = RET_OK;
    u32 irqReg;

    irqReg = MMUMMU_IRQENABLEReadRegister32(baseAddress);

    MMUMMU_IRQENABLEWriteRegister32(baseAddress, irqReg & ~irqMask);

    return status;
}

HW_STATUS HW_MMU_EventEnable(const void __iomem *baseAddress, u32 irqMask)
{
    HW_STATUS status = RET_OK;
    u32 irqReg;

    irqReg = MMUMMU_IRQENABLEReadRegister32(baseAddress);

    MMUMMU_IRQENABLEWriteRegister32(baseAddress, irqReg | irqMask);

    return status;
}


HW_STATUS HW_MMU_EventStatus(const void __iomem *baseAddress, u32 *irqMask)
{
    HW_STATUS status = RET_OK;

    *irqMask = MMUMMU_IRQSTATUSReadRegister32(baseAddress);

    return status;
}


HW_STATUS HW_MMU_FaultAddrRead(const void __iomem *baseAddress, u32 *addr)
{
    HW_STATUS status = RET_OK;

    /*Check the input Parameters*/
    CHECK_INPUT_PARAM(baseAddress, 0, RET_BAD_NULL_PARAM,
		      RES_MMU_BASE + RES_INVALID_INPUT_PARAM);

    /* read values from register */
    *addr = MMUMMU_FAULT_ADReadRegister32(baseAddress);

    return status;
}

HW_STATUS HW_MMU_TTBSet(const void __iomem *baseAddress, u32 TTBPhysAddr)
{
    HW_STATUS status = RET_OK;
    u32 loadTTB;

   /*Check the input Parameters*/
   CHECK_INPUT_PARAM(baseAddress, 0, RET_BAD_NULL_PARAM,
		     RES_MMU_BASE + RES_INVALID_INPUT_PARAM);

   loadTTB = TTBPhysAddr & ~0x7FUL;
   /* write values to register */
   MMUMMU_TTBWriteRegister32(baseAddress, loadTTB);

   return status;
}

HW_STATUS HW_MMU_TWLEnable(const void __iomem *baseAddress)
{
    HW_STATUS status = RET_OK;

    MMUMMU_CNTLTWLEnableWrite32(baseAddress, HW_SET);

    return status;
}

HW_STATUS HW_MMU_TWLDisable(const void __iomem *baseAddress)
{
    HW_STATUS status = RET_OK;

    MMUMMU_CNTLTWLEnableWrite32(baseAddress, HW_CLEAR);

    return status;
}

HW_STATUS HW_MMU_TLBFlush(const void __iomem *baseAddress, u32 virtualAddr,
			     u32 pageSize)
{
    HW_STATUS status = RET_OK;
    u32 virtualAddrTag;
    enum HW_MMUPageSize_t pgSizeBits;

    switch (pageSize) {
    case HW_PAGE_SIZE_4KB:
	pgSizeBits = HW_MMU_SMALL_PAGE;
	break;

    case HW_PAGE_SIZE_64KB:
	pgSizeBits = HW_MMU_LARGE_PAGE;
	break;

    case HW_PAGE_SIZE_1MB:
	pgSizeBits = HW_MMU_SECTION;
	break;

    case HW_PAGE_SIZE_16MB:
	pgSizeBits = HW_MMU_SUPERSECTION;
	break;

    default:
	return RET_FAIL;
    }

    /* Generate the 20-bit tag from virtual address */
    virtualAddrTag = ((virtualAddr & MMU_ADDR_MASK) >> 12);

    MMU_SetCAMEntry(baseAddress, pgSizeBits, 0, 0, virtualAddrTag);

    MMU_FlushEntry(baseAddress);

    return status;
}

HW_STATUS HW_MMU_TLBAdd(const void __iomem *baseAddress,
			   u32	      physicalAddr,
			   u32	      virtualAddr,
			   u32	      pageSize,
			   u32	      entryNum,
			   struct HW_MMUMapAttrs_t    *mapAttrs,
			   enum HW_SetClear_t       preservedBit,
			   enum HW_SetClear_t       validBit)
{
    HW_STATUS  status = RET_OK;
    u32 lockReg;
    u32 virtualAddrTag;
    enum HW_MMUPageSize_t mmuPgSize;

    /*Check the input Parameters*/
    CHECK_INPUT_PARAM(baseAddress, 0, RET_BAD_NULL_PARAM,
		      RES_MMU_BASE + RES_INVALID_INPUT_PARAM);
    CHECK_INPUT_RANGE_MIN0(pageSize, MMU_PAGE_MAX, RET_PARAM_OUT_OF_RANGE,
			   RES_MMU_BASE + RES_INVALID_INPUT_PARAM);
    CHECK_INPUT_RANGE_MIN0(mapAttrs->elementSize, MMU_ELEMENTSIZE_MAX,
			RET_PARAM_OUT_OF_RANGE, RES_MMU_BASE +
			RES_INVALID_INPUT_PARAM);

    switch (pageSize) {
    case HW_PAGE_SIZE_4KB:
	mmuPgSize = HW_MMU_SMALL_PAGE;
	break;

    case HW_PAGE_SIZE_64KB:
	mmuPgSize = HW_MMU_LARGE_PAGE;
	break;

    case HW_PAGE_SIZE_1MB:
	mmuPgSize = HW_MMU_SECTION;
	break;

    case HW_PAGE_SIZE_16MB:
	mmuPgSize = HW_MMU_SUPERSECTION;
	break;

    default:
	return RET_FAIL;
    }

    lockReg = MMUMMU_LOCKReadRegister32(baseAddress);

    /* Generate the 20-bit tag from virtual address */
    virtualAddrTag = ((virtualAddr & MMU_ADDR_MASK) >> 12);

    /* Write the fields in the CAM Entry Register */
    MMU_SetCAMEntry(baseAddress,  mmuPgSize, preservedBit, validBit,
		    virtualAddrTag);

    /* Write the different fields of the RAM Entry Register */
    /* endianism of the page,Element Size of the page (8, 16, 32, 64 bit)*/
    MMU_SetRAMEntry(baseAddress, physicalAddr, mapAttrs->endianism,
		    mapAttrs->elementSize, mapAttrs->mixedSize);

    /* Update the MMU Lock Register */
    /* currentVictim between lockedBaseValue and (MMU_Entries_Number - 1)*/
    MMUMMU_LOCKCurrentVictimWrite32(baseAddress, entryNum);

    /* Enable loading of an entry in TLB by writing 1
	   into LD_TLB_REG register */
    MMUMMU_LD_TLBWriteRegister32(baseAddress, MMU_LOAD_TLB);


    MMUMMU_LOCKWriteRegister32(baseAddress, lockReg);

    return status;
}

HW_STATUS HW_MMU_PteSet(const u32	pgTblVa,
			   u32	      physicalAddr,
			   u32	      virtualAddr,
			   u32	      pageSize,
			   struct HW_MMUMapAttrs_t    *mapAttrs)
{
    HW_STATUS status = RET_OK;
    u32 pteAddr, pteVal;
    s32 numEntries = 1;

    switch (pageSize) {
    case HW_PAGE_SIZE_4KB:
	pteAddr = HW_MMU_PteAddrL2(pgTblVa,
				    virtualAddr & MMU_SMALL_PAGE_MASK);
	pteVal = ((physicalAddr & MMU_SMALL_PAGE_MASK) |
		    (mapAttrs->endianism << 9) |
		    (mapAttrs->elementSize << 4) |
		    (mapAttrs->mixedSize << 11) | 2
		  );
	break;

    case HW_PAGE_SIZE_64KB:
	numEntries = 16;
	pteAddr = HW_MMU_PteAddrL2(pgTblVa,
				    virtualAddr & MMU_LARGE_PAGE_MASK);
	pteVal = ((physicalAddr & MMU_LARGE_PAGE_MASK) |
		    (mapAttrs->endianism << 9) |
		    (mapAttrs->elementSize << 4) |
		    (mapAttrs->mixedSize << 11) | 1
		  );
	break;

    case HW_PAGE_SIZE_1MB:
	pteAddr = HW_MMU_PteAddrL1(pgTblVa,
				    virtualAddr & MMU_SECTION_ADDR_MASK);
	pteVal = ((((physicalAddr & MMU_SECTION_ADDR_MASK) |
		     (mapAttrs->endianism << 15) |
		     (mapAttrs->elementSize << 10) |
		     (mapAttrs->mixedSize << 17)) &
		     ~0x40000) | 0x2
		 );
	break;

    case HW_PAGE_SIZE_16MB:
	numEntries = 16;
	pteAddr = HW_MMU_PteAddrL1(pgTblVa,
				    virtualAddr & MMU_SSECTION_ADDR_MASK);
	pteVal = (((physicalAddr & MMU_SSECTION_ADDR_MASK) |
		      (mapAttrs->endianism << 15) |
		      (mapAttrs->elementSize << 10) |
		      (mapAttrs->mixedSize << 17)
		    ) | 0x40000 | 0x2
		  );
	break;

    case HW_MMU_COARSE_PAGE_SIZE:
	pteAddr = HW_MMU_PteAddrL1(pgTblVa,
				    virtualAddr & MMU_SECTION_ADDR_MASK);
	pteVal = (physicalAddr & MMU_PAGE_TABLE_MASK) | 1;
	break;

    default:
	return RET_FAIL;
    }

    while (--numEntries >= 0)
	((u32 *)pteAddr)[numEntries] = pteVal;

    return status;
}

HW_STATUS HW_MMU_PteClear(const u32  pgTblVa,
			     u32	virtualAddr,
			     u32	pgSize)
{
    HW_STATUS status = RET_OK;
    u32 pteAddr;
    s32 numEntries = 1;

    switch (pgSize) {
    case HW_PAGE_SIZE_4KB:
	pteAddr = HW_MMU_PteAddrL2(pgTblVa,
				    virtualAddr & MMU_SMALL_PAGE_MASK);
	break;

    case HW_PAGE_SIZE_64KB:
	numEntries = 16;
	pteAddr = HW_MMU_PteAddrL2(pgTblVa,
				    virtualAddr & MMU_LARGE_PAGE_MASK);
	break;

    case HW_PAGE_SIZE_1MB:
    case HW_MMU_COARSE_PAGE_SIZE:
	pteAddr = HW_MMU_PteAddrL1(pgTblVa,
				    virtualAddr & MMU_SECTION_ADDR_MASK);
	break;

    case HW_PAGE_SIZE_16MB:
	numEntries = 16;
	pteAddr = HW_MMU_PteAddrL1(pgTblVa,
				    virtualAddr & MMU_SSECTION_ADDR_MASK);
	break;

    default:
	return RET_FAIL;
    }

    while (--numEntries >= 0)
	((u32 *)pteAddr)[numEntries] = 0;

    return status;
}

/* MMU_FlushEntry */
static HW_STATUS MMU_FlushEntry(const void __iomem *baseAddress)
{
   HW_STATUS status = RET_OK;
   u32 flushEntryData = 0x1;

   /*Check the input Parameters*/
   CHECK_INPUT_PARAM(baseAddress, 0, RET_BAD_NULL_PARAM,
		     RES_MMU_BASE + RES_INVALID_INPUT_PARAM);

   /* write values to register */
   MMUMMU_FLUSH_ENTRYWriteRegister32(baseAddress, flushEntryData);

   return status;
}

/* MMU_SetCAMEntry */
static HW_STATUS MMU_SetCAMEntry(const void __iomem *baseAddress,
				   const u32    pageSize,
				   const u32    preservedBit,
				   const u32    validBit,
				   const u32    virtualAddrTag)
{
   HW_STATUS status = RET_OK;
   u32 mmuCamReg;

   /*Check the input Parameters*/
   CHECK_INPUT_PARAM(baseAddress, 0, RET_BAD_NULL_PARAM,
		     RES_MMU_BASE + RES_INVALID_INPUT_PARAM);

   mmuCamReg = (virtualAddrTag << 12);
   mmuCamReg = (mmuCamReg) | (pageSize) |  (validBit << 2) |
	       (preservedBit << 3) ;

   /* write values to register */
   MMUMMU_CAMWriteRegister32(baseAddress, mmuCamReg);

   return status;
}

/* MMU_SetRAMEntry */
static HW_STATUS MMU_SetRAMEntry(const void __iomem *baseAddress,
				   const u32       physicalAddr,
				   enum HW_Endianism_t     endianism,
				   enum HW_ElementSize_t   elementSize,
				   enum HW_MMUMixedSize_t  mixedSize)
{
   HW_STATUS status = RET_OK;
   u32 mmuRamReg;

   /*Check the input Parameters*/
   CHECK_INPUT_PARAM(baseAddress, 0, RET_BAD_NULL_PARAM,
		     RES_MMU_BASE + RES_INVALID_INPUT_PARAM);
   CHECK_INPUT_RANGE_MIN0(elementSize, MMU_ELEMENTSIZE_MAX,
		   RET_PARAM_OUT_OF_RANGE, RES_MMU_BASE +
		   RES_INVALID_INPUT_PARAM);


   mmuRamReg = (physicalAddr & MMU_ADDR_MASK);
   mmuRamReg = (mmuRamReg) | ((endianism << 9) |  (elementSize << 7) |
	       (mixedSize << 6));

   /* write values to register */
   MMUMMU_RAMWriteRegister32(baseAddress, mmuRamReg);

   return status;

}
