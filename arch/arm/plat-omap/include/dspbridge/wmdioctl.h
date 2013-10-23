/*
 * wmdioctl.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * BRIDGE Minidriver BRD_IOCtl reserved command definitions.
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

#ifndef WMDIOCTL_
#define WMDIOCTL_

/* ------------------------------------ Hardware Abstraction Layer */
#include <hw_defs.h>
#include <hw_mmu.h>

/*
 * Any IOCTLS at or above this value are reserved for standard WMD
 * interfaces.
 */
#define WMDIOCTL_RESERVEDBASE       0x8000

#define WMDIOCTL_CHNLREAD           (WMDIOCTL_RESERVEDBASE + 0x10)
#define WMDIOCTL_CHNLWRITE          (WMDIOCTL_RESERVEDBASE + 0x20)
#define WMDIOCTL_GETINTRCOUNT       (WMDIOCTL_RESERVEDBASE + 0x30)
#define WMDIOCTL_RESETINTRCOUNT     (WMDIOCTL_RESERVEDBASE + 0x40)
#define WMDIOCTL_INTERRUPTDSP       (WMDIOCTL_RESERVEDBASE + 0x50)
/* DMMU */
#define WMDIOCTL_SETMMUCONFIG       (WMDIOCTL_RESERVEDBASE + 0x60)
/* PWR */
#define WMDIOCTL_PWRCONTROL         (WMDIOCTL_RESERVEDBASE + 0x70)

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
/* DSP Initiated Hibernate */
#define WMDIOCTL_PWR_HIBERNATE	(WMDIOCTL_PWRCONTROL + 0x8)
#define WMDIOCTL_PRESCALE_NOTIFY (WMDIOCTL_PWRCONTROL + 0x9)
#define WMDIOCTL_POSTSCALE_NOTIFY (WMDIOCTL_PWRCONTROL + 0xA)
#define WMDIOCTL_CONSTRAINT_REQUEST (WMDIOCTL_PWRCONTROL + 0xB)

/* Number of actual DSP-MMU TLB entrries */
#define WMDIOCTL_NUMOFMMUTLB        32

struct wmdioctl_extproc {
	u32 ul_dsp_va;		/* DSP virtual address */
	u32 ul_gpp_pa;		/* GPP physical address */
	/* GPP virtual address. __va does not work for ioremapped addresses */
	u32 ul_gpp_va;
	u32 ul_size;		/* Size of the mapped memory in bytes */
	enum hw_endianism_t endianism;
	enum hw_mmu_mixed_size_t mixed_mode;
	enum hw_element_size_t elem_size;
};

#endif /* WMDIOCTL_ */
