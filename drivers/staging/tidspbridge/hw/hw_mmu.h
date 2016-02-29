/*
 * hw_mmu.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * MMU types and API declarations
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

#ifndef _HW_MMU_H
#define _HW_MMU_H

#include <linux/types.h>

/* Bitmasks for interrupt sources */
#define HW_MMU_TRANSLATION_FAULT   0x2
#define HW_MMU_ALL_INTERRUPTS      0x1F

#define HW_MMU_COARSE_PAGE_SIZE 0x400

/* hw_mmu_mixed_size_t:  Enumerated Type used to specify whether to follow
			CPU/TLB Element size */
enum hw_mmu_mixed_size_t {
	HW_MMU_TLBES,
	HW_MMU_CPUES
};

/* hw_mmu_map_attrs_t:  Struct containing MMU mapping attributes */
struct hw_mmu_map_attrs_t {
	enum hw_endianism_t endianism;
	enum hw_element_size_t element_size;
	enum hw_mmu_mixed_size_t mixed_size;
	bool donotlockmpupage;
};
#endif /* _HW_MMU_H */
