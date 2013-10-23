/*
 * dmm.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * The Dynamic Memory Mapping(DMM) module manages the DSP Virtual address
 * space that can be directly mapped to any MPU buffer or memory region.
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

#ifndef DMM_
#define DMM_

#include <dspbridge/dbdefs.h>

struct dmm_object;

/* DMM attributes used in dmm_create() */
struct dmm_mgrattrs {
	u32 reserved;
};

#define DMMPOOLSIZE      0x10000000

/*
 *  ======== dmm_get_handle ========
 *  Purpose:
 *      Return the dynamic memory manager object for this device.
 *      This is typically called from the client process.
 */

extern dsp_status dmm_get_handle(void *hprocessor,
				 OUT struct dmm_object **phDmmMgr);

extern dsp_status dmm_reserve_memory(struct dmm_object *dmm_mgr,
				     u32 size, u32 *prsv_addr);

extern dsp_status dmm_un_reserve_memory(struct dmm_object *dmm_mgr,
					u32 rsv_addr);

extern dsp_status dmm_map_memory(struct dmm_object *dmm_mgr, u32 addr,
				 u32 size);

extern dsp_status dmm_un_map_memory(struct dmm_object *dmm_mgr,
				    u32 addr, u32 *psize);

extern dsp_status dmm_destroy(struct dmm_object *dmm_mgr);

extern dsp_status dmm_delete_tables(struct dmm_object *dmm_mgr);

extern dsp_status dmm_create(OUT struct dmm_object **phDmmMgr,
			     struct dev_object *hdev_obj,
			     IN CONST struct dmm_mgrattrs *pMgrAttrs);

extern bool dmm_init(void);

extern void dmm_exit(void);

extern dsp_status dmm_create_tables(struct dmm_object *dmm_mgr,
				    u32 addr, u32 size);
#endif /* DMM_ */
