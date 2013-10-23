/*
 * services.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Provide SERVICES loading.
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

#include <dspbridge/host_os.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>
#include <dspbridge/dbdefs.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/cfg.h>
#include <dspbridge/mem.h>
#include <dspbridge/ntfy.h>
#include <dspbridge/reg.h>
#include <dspbridge/sync.h>
#include <dspbridge/clk.h>

/*  ----------------------------------- This */
#include <dspbridge/services.h>

/*
 *  ======== services_exit ========
 *  Purpose:
 *      Discontinue usage of module; free resources when reference count
 *      reaches 0.
 */
void services_exit(void)
{
	/* Uninitialize all SERVICES modules here */
	ntfy_exit();
	sync_exit();
	clk_exit();
	reg_exit();
	cfg_exit();
	mem_exit();
}

/*
 *  ======== services_init ========
 *  Purpose:
 *      Initializes SERVICES modules.
 */
bool services_init(void)
{
	bool ret = true;
	bool fcfg, fmem;
	bool freg, fsync, fclk, fntfy;

	/* Perform required initialization of SERVICES modules. */
	fmem = services_mem_init();
	freg = reg_init();
	fcfg = cfg_init();
	fsync = sync_init();
	fclk = services_clk_init();
	fntfy = ntfy_init();

	ret = fcfg && fmem && freg && fsync && fclk;

	if (!ret) {
		if (fntfy)
			ntfy_exit();

		if (fsync)
			sync_exit();

		if (fclk)
			clk_exit();

		if (freg)
			reg_exit();

		if (fcfg)
			cfg_exit();

		if (fmem)
			mem_exit();

	}

	return ret;
}
