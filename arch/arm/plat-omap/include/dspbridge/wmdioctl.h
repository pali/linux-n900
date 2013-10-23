/*
 * wmdioctl.h
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
 *  ======== wmdioctl.h ========
 *  Description:
 *    BRIDGE Minidriver BRD_IOCtl reserved command definitions.
 *
 *! Revision History
 *! ================
 *! 19-Apr-2004 sb   Updated HW typedefs
 *! 16-Feb-2004 vp   Added MMU endianness attributes to WMDIOCTL_EXTPROC
 *! 21-Mar-2003 sb   Changed WMDIOCTL_NUMOFMMUTLB from 7 to 32
 *! 14-May-2001 sg   Added codes for PWR.
 *! 10-Aug-2001 ag   Added _SETMMUCONFIG ioctl used for DSP-MMU init.
 *! 16-Nov-1999 rajesh ?
 *! 18-Jun-1998 ag   Moved EMIF, SDRAM_C, & CE space init to ENBLEXTMEM ioctl.
 *!                  Added ENBLEXTMEM, RESETDSP, UNRESETDSP & ASSERTSIG ioctls.
 *! 07-Jun-1998 ag   Added JTAG_SELECT, MAP_TBC, GET_CONFIGURATION ioctls.
 *! 26-Jan-1998 jeh: Added START, RECV, and SEND ioctls.
 *! 07-Nov-1997 nn:  Added command to interrupt DSP for interrupt test.
 *! 20-Oct-1997 nn:  Added commands for getting and resetting interrupt count.
 *! 17-Oct-1997 gp:  Moved to src/wmd. Standardized prefix.
 *! 08-Oct-1997 nn:  Created.
 */

#ifndef WMDIOCTL_
#define WMDIOCTL_

/* ------------------------------------ Hardware Abstraction Layer */
#include <hw_defs.h>
#include <hw_mmu.h>

/* Any IOCTLS at or above this value are reserved for standard WMD interfaces.*/
#define WMDIOCTL_RESERVEDBASE       0x8000

#define WMDIOCTL_CHNLREAD           (WMDIOCTL_RESERVEDBASE + 0x10)
#define WMDIOCTL_CHNLWRITE          (WMDIOCTL_RESERVEDBASE + 0x20)
#define WMDIOCTL_GETINTRCOUNT       (WMDIOCTL_RESERVEDBASE + 0x30)
#define WMDIOCTL_RESETINTRCOUNT     (WMDIOCTL_RESERVEDBASE + 0x40)
#define WMDIOCTL_INTERRUPTDSP       (WMDIOCTL_RESERVEDBASE + 0x50)
#define WMDIOCTL_SETMMUCONFIG       (WMDIOCTL_RESERVEDBASE + 0x60)   /* DMMU */
#define WMDIOCTL_PWRCONTROL         (WMDIOCTL_RESERVEDBASE + 0x70)   /* PWR */

/* attention, modifiers:
 * Some of these control enumerations are made visible to user for power
 * control, so any changes to this list, should also be updated in the user
 * header file 'dbdefs.h' ***/
/* These ioctls are reserved for PWR power commands for the DSP */
#define WMDIOCTL_DEEPSLEEP          (WMDIOCTL_PWRCONTROL + 0x0)
#define WMDIOCTL_EMERGENCYSLEEP     (WMDIOCTL_PWRCONTROL + 0x1)
#define WMDIOCTL_WAKEUP             (WMDIOCTL_PWRCONTROL + 0x2)
#define WMDIOCTL_PWRENABLE          (WMDIOCTL_PWRCONTROL + 0x3)
#define WMDIOCTL_PWRDISABLE         (WMDIOCTL_PWRCONTROL + 0x4)
#define WMDIOCTL_CLK_CTRL		    (WMDIOCTL_PWRCONTROL + 0x7)
#define WMDIOCTL_PWR_HIBERNATE (WMDIOCTL_PWRCONTROL + 0x8) /*DSP Initiated
							    * Hibernate*/
#define WMDIOCTL_PRESCALE_NOTIFY (WMDIOCTL_PWRCONTROL + 0x9)
#define WMDIOCTL_POSTSCALE_NOTIFY (WMDIOCTL_PWRCONTROL + 0xA)
#define WMDIOCTL_CONSTRAINT_REQUEST (WMDIOCTL_PWRCONTROL + 0xB)

/* Number of actual DSP-MMU TLB entrries */
#define WMDIOCTL_NUMOFMMUTLB        32

struct WMDIOCTL_EXTPROC {
	u32 ulDspVa;		/* DSP virtual address */
	u32 ulGppPa;		/* GPP physical address */
	/* GPP virtual address. __va does not work for ioremapped addresses */
	u32 ulGppVa;
	u32 ulSize;		/* Size of the mapped memory in bytes */
	enum HW_Endianism_t endianism;
	enum HW_MMUMixedSize_t mixedMode;
	enum HW_ElementSize_t elemSize;
};

#endif				/* WMDIOCTL_ */

