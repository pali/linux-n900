/*
 * IPIAccInt.h
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

#ifndef _IPI_ACC_INT_H
#define _IPI_ACC_INT_H

/* Bitfield mask and offset declarations */
#define SYSC_IVA2BOOTMOD_OFFSET                   0x404
#define SYSC_IVA2BOOTADDR_OFFSET                0x400
#define SYSC_IVA2BOOTADDR_MASK                 0xfffffc00


/* The following represent the enumerated values for each bitfield */

enum IPIIPI_SYSCONFIGAutoIdleE {
	IPIIPI_SYSCONFIGAutoIdleclkfree = 0x0000,
	IPIIPI_SYSCONFIGAutoIdleautoclkgate = 0x0001
} ;

enum IPIIPI_ENTRYElemSizeValueE {
	IPIIPI_ENTRYElemSizeValueElemSz8b = 0x0000,
	IPIIPI_ENTRYElemSizeValueElemSz16b = 0x0001,
	IPIIPI_ENTRYElemSizeValueElemSz32b = 0x0002,
	IPIIPI_ENTRYElemSizeValueReserved = 0x0003
} ;

#endif				/* _IPI_ACC_INT_H */
/* EOF */
