/*
 * mgrpriv.h
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
 *  ======== mgrpriv.h ========
 *  Description:
 *      Global MGR constants and types, shared by PROC, MGR, and WCD.
 *
 *! Revision History:
 *! ================
 *! 29-July-2001 ag: added MGR_PROCESSOREXTINFO.
 *! 05-July-2000 rr: Created
 */

#ifndef MGRPRIV_
#define MGRPRIV_

/*
 * OMAP1510 specific
 */
#define MGR_MAXTLBENTRIES  32

/* RM MGR Object */
	struct MGR_OBJECT;

	struct MGR_TLBENTRY {
		u32 ulDspVirt;	/* DSP virtual address */
		u32 ulGppPhys;	/* GPP physical address */
	} ;

/*
 *  The DSP_PROCESSOREXTINFO structure describes additional extended
 *  capabilities of a DSP processor not exposed to user.
 */
	struct MGR_PROCESSOREXTINFO {
		struct DSP_PROCESSORINFO tyBasic;    /* user processor info */
		/* private dsp mmu entries */
		struct MGR_TLBENTRY tyTlb[MGR_MAXTLBENTRIES];
	} ;

#endif				/* MGRPRIV_ */
