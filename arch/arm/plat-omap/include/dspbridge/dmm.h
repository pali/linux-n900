/*
 * dmm.h
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
 *  ======== dmm.h ========
 *  Purpose:
 *      The Dynamic Memory Mapping(DMM) module manages the DSP Virtual address
 *      space that can be directly mapped to any MPU buffer or memory region
 *
 *  Public Functions:
 *
 *! Revision History:
 *! ================
 *! 20-Feb-2004 sb: Created.
 *!
 */

#ifndef DMM_
#define DMM_

#include <dspbridge/dbdefs.h>

	struct DMM_OBJECT;

/* DMM attributes used in DMM_Create() */
	struct DMM_MGRATTRS {
		u32 reserved;
	} ;

#define DMMPOOLSIZE      0x4000000

/*
 *  ======== DMM_GetHandle ========
 *  Purpose:
 *      Return the dynamic memory manager object for this device.
 *      This is typically called from the client process.
 */

	extern DSP_STATUS DMM_GetHandle(DSP_HPROCESSOR hProcessor,
					OUT struct DMM_OBJECT **phDmmMgr);

	extern DSP_STATUS DMM_ReserveMemory(struct DMM_OBJECT *hDmmMgr,
					    u32 size,
					    u32 *pRsvAddr);

	extern DSP_STATUS DMM_UnReserveMemory(struct DMM_OBJECT *hDmmMgr,
					      u32 rsvAddr);

	extern DSP_STATUS DMM_MapMemory(struct DMM_OBJECT *hDmmMgr, u32 addr,
					u32 size);

	extern DSP_STATUS DMM_UnMapMemory(struct DMM_OBJECT *hDmmMgr,
					  u32 addr,
					  u32 *pSize);

	extern DSP_STATUS DMM_Destroy(struct DMM_OBJECT *hDmmMgr);

	extern DSP_STATUS DMM_DeleteTables(struct DMM_OBJECT *hDmmMgr);

	extern DSP_STATUS DMM_Create(OUT struct DMM_OBJECT **phDmmMgr,
				     struct DEV_OBJECT *hDevObject,
				     IN CONST struct DMM_MGRATTRS *pMgrAttrs);

	extern bool DMM_Init(void);

	extern void DMM_Exit(void);

	extern DSP_STATUS DMM_CreateTables(struct DMM_OBJECT *hDmmMgr,
						u32 addr, u32 size);
	extern u32 *DMM_GetPhysicalAddrTable(void);
#endif				/* DMM_ */
