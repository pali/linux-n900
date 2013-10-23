/*
 * This file is part of Aegis Validator
 *
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
 *
 * Contact: Markku Kylänpää <ext-markku.kylanpaa@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * Authors: Markku Kylänpää, 2010
 */

/*
 * This file contains the header file for platform security functions
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/elf.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>
#include <plat/pa.h>
#include <plat/sec.h>
#include "platsec.h"

/* BB5 ROM function index to get R&D certificate */
#define SEC_HAL_DEBUG_CONTROL_GET        37

/* R&D certificate bit mask to check if running R&D SW is allowed */
#define RDC_LEGACY_SEC_ROM_RD_SW_ENABLED 0x20000000UL

/**
 * struct bb5_rom_parameters - parameters to BB5 ROM functions
 * @npar: number of parameters to ROM function call
 * @addr: parameter array base
 */
struct bb5_rom_parameters {
	int npar;
	unsigned long addr;
};

/**
 * check_rd_certificate() - check presence of BB5 R&D certificate
 *
 * Check if the device has BB5 R&D certificate installed which allows
 * running of R&D software.
 *
 * Return 0 if R&D software should be allowed and otherwise negative value
 */
int check_rd_certificate(void)
{
	int r;
	u32 dbg[3];
	struct sec_result out;
	struct bb5_rom_parameters par;

	/* Non-HS device is treated as a device having R&D certificate */
	if (omap_type() != OMAP2_DEVICE_TYPE_SEC)
		return 0;

	/*
	 * Security driver function is called to get debug control bits
	 * from BB5 security hardware. Three slot u32 array is needed
	 * as a parameter to the function.
	 */
	par.npar = 1;
	par.addr = virt_to_phys(dbg);
	r = sec_rom_service(SEC_HAL_DEBUG_CONTROL_GET, &par, &out);
	if (r || out.rom_rv)
		return -EINVAL;
	if (dbg[0] & RDC_LEGACY_SEC_ROM_RD_SW_ENABLED)
		return 0;
	return -EPERM;
}
