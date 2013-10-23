/*
 * dbof.h
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
 *  ======== dbof.h ========
 *  Description:
 *      Defines and typedefs for DSP/BIOS Bridge Object File Format (DBOF).
 *
 *! Revision History
 *! ================
 *! 12-Jul-2002 jeh     Added defines for DBOF_SectHdr page.
 *! 12-Oct-2001 jeh     Converted to std.h format.
 *! 07-Sep-2001 jeh     Added overlay support.
 *! 06-Jul-2001 jeh     Created.
 */

#ifndef DBOF_
#define DBOF_

/* Enough to hold DCD section names: 32 digit ID + underscores */
#define DBOF_DCDSECTNAMELEN     40

/* Values for DBOF_SectHdr page field. */
#define         DBOF_PROGRAM    0
#define         DBOF_DATA       1
#define         DBOF_CINIT      2

/*
 *  ======== DBOF_FileHdr ========
 */
	struct DBOF_FileHdr {
		u32 magic;	/* COFF magic number */
		u32 entry;	/* Program entry point */
		u16 numSymbols;	/* Number of bridge symbols */
		u16 numDCDSects;	/* Number of DCD sections */
		u16 numSects;	/* Number of sections to load */
		u16 numOvlySects;	/* Number of overlay sections */
		u32 symOffset;	/* Offset in file to symbols */
		u32 dcdSectOffset;	/* Offset to DCD sections */
		u32 loadSectOffset;	/* Offset to loadable sections */
		u32 ovlySectOffset;	/* Offset to overlay data */
		u16 version;	/* DBOF version number */
		u16 resvd;	/* Reserved for future use */
	} ;

/*
 *  ======== DBOF_DCDSectHdr ========
 */
	struct DBOF_DCDSectHdr {
		u32 size;	/* Sect size (target MAUs) */
		char name[DBOF_DCDSECTNAMELEN];	/* DCD section name */
	} ;

/*
 *  ======== DBOF_OvlySectHdr ========
 */
	struct DBOF_OvlySectHdr {
		u16 nameLen;	/* Length of section name */
		u16 numCreateSects;	/* # of sects loaded for create phase */
		u16 numDeleteSects;	/* # of sects loaded for delete phase */
		u16 numExecuteSects; /* # of sects loaded for execute phase */

		/*
		 *  Number of sections where load/unload phase is not specified.
		 *  These sections will be loaded when create phase sects are
		 *  loaded, and unloaded when the delete phase is unloaded.
		 */
		u16 numOtherSects;
		u16 resvd;	/* Reserved for future use */
	};

/*
 *  ======== DBOF_OvlySectData ========
 */
	struct DBOF_OvlySectData {
		u32 loadAddr;	/* Section load address */
		u32 runAddr;	/* Section run address */
		u32 size;	/* Section size (target MAUs) */
		u16 page;	/* Memory page number */
		u16 resvd;	/* Reserved */
	} ;

/*
 *  ======== DBOF_SectHdr ========
 */
	struct DBOF_SectHdr {
		u32 addr;	/* Section address */
		u32 size;	/* Section size (target MAUs) */
		u16 page;	/* Page number */
		u16 resvd;	/* Reserved for future use */
	} ;

/*
 *  ======== DBOF_SymbolHdr ========
 */
	struct DBOF_SymbolHdr {
		u32 value;	/* Symbol value */
		u16 nameLen;	/* Length of symbol name */
		u16 resvd;	/* Reserved for future use */
	} ;

#endif				/* DBOF_ */

