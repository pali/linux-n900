/*
 * tiomap_io.h
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
 *  ======== _tiomap_io.h ========
 *  Description:
 *      Definitions, types and function prototypes for the io
 *      (r/w external mem).
 *
 *! Revision History
 *! ================
 *! 08-Oct-2002 rr:  Created.
 */

#ifndef _TIOMAP_IO_
#define _TIOMAP_IO_

/*
 * Symbol that defines beginning of shared memory.
 * For OMAP (Helen) this is the DSP Virtual base address of SDRAM.
 * This will be used to program DSP MMU to map DSP Virt to GPP phys.
 * (see dspMmuTlbEntry()).
 */
#define SHMBASENAME "SHM_BEG"
#define EXTBASE     "EXT_BEG"
#define EXTEND      "_EXT_END"
#define DYNEXTBASE  "_DYNEXT_BEG"
#define DYNEXTEND   "_DYNEXT_END"
#define IVAEXTMEMBASE   "_IVAEXTMEM_BEG"
#define IVAEXTMEMEND   "_IVAEXTMEM_END"


#define DSP_TRACESEC_BEG  "_BRIDGE_TRACE_BEG"
#define DSP_TRACESEC_END  "_BRIDGE_TRACE_END"

#define SYS_PUTCBEG               "_SYS_PUTCBEG"
#define SYS_PUTCEND               "_SYS_PUTCEND"
#define BRIDGE_SYS_PUTC_current   "_BRIDGE_SYS_PUTC_current"


#define WORDSWAP_ENABLE 0x3	/* Enable word swap */

/*
 *  ======== ReadExtDspData ========
 *  Reads it from DSP External memory. The external memory for the DSP
 * is configured by the combination of DSP MMU and SHM Memory manager in the CDB
 */
extern DSP_STATUS ReadExtDspData(struct WMD_DEV_CONTEXT *pDevContext,
				OUT u8 *pbHostBuf, u32 dwDSPAddr,
				u32 ulNumBytes, u32 ulMemType);

/*
 *  ======== WriteDspData ========
 */
extern DSP_STATUS WriteDspData(struct WMD_DEV_CONTEXT *pDevContext,
			       OUT u8 *pbHostBuf, u32 dwDSPAddr,
			       u32 ulNumBytes, u32 ulMemType);

/*
 *  ======== WriteExtDspData ========
 *  Writes to the DSP External memory for external program.
 *  The ext mem for progra is configured by the combination of DSP MMU and
 *  SHM Memory manager in the CDB
 */
extern DSP_STATUS WriteExtDspData(struct WMD_DEV_CONTEXT *pDevContext,
				 IN u8 *pbHostBuf, u32 dwDSPAddr,
				 u32 ulNumBytes, u32 ulMemType,
				 bool bDynamicLoad);

/*
 * ======== WriteExt32BitDspData ========
 * Writes 32 bit data to the external memory
 */
extern inline void WriteExt32BitDspData(IN const
		struct WMD_DEV_CONTEXT *pDevContext, IN u32 dwDSPAddr,
		IN u32 val)
{
	*(u32 *)dwDSPAddr = ((pDevContext->tcWordSwapOn) ? (((val << 16) &
			      0xFFFF0000) | ((val >> 16) & 0x0000FFFF)) : val);
}

/*
 * ======== ReadExt32BitDspData ========
 * Reads 32 bit data from the external memory
 */
extern inline u32 ReadExt32BitDspData(IN const struct WMD_DEV_CONTEXT
				       *pDevContext, IN u32 dwDSPAddr)
{
	u32 retVal;
	retVal = *(u32 *)dwDSPAddr;

	retVal = ((pDevContext->tcWordSwapOn) ? (((retVal << 16)
		 & 0xFFFF0000) | ((retVal >> 16) & 0x0000FFFF)) : retVal);
	return retVal;
}

#endif				/* _TIOMAP_IO_ */

