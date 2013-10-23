/*
 * services.c
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
 *  ======== services.c ========
 *  Purpose:
 *      Provide SERVICES loading.
 *
 *  Public Functions:
 *      SERVICES_Exit
 *      SERVICES_Init
 *
 *
 *! Revision History
 *! ================
 *! 20-Nov-2000 rr: NTFY_Init/Exit added.
 *! 06-Jul-2000 rr: PROC prefix changed to PRCS to accomodate RM.
 *! 01-Feb-2000 kc: Created.
 */

#include <dspbridge/host_os.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>
#include <dspbridge/dbdefs.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>
#include <dspbridge/gt.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/cfg.h>
#include <dspbridge/csl.h>
#include <dspbridge/dbg.h>
#include <dspbridge/dpc.h>
#include <dspbridge/kfile.h>
#include <dspbridge/list.h>
#include <dspbridge/mem.h>
#include <dspbridge/ntfy.h>
#include <dspbridge/reg.h>
#include <dspbridge/sync.h>
#include <dspbridge/clk.h>
#include <dspbridge/util.h>

/*  ----------------------------------- This */
#include <dspbridge/services.h>

/*  ----------------------------------- Globals */
#if GT_TRACE
static struct GT_Mask SERVICES_debugMask = { NULL, NULL };  /* GT trace var. */
#endif

static u32 cRefs;		/* SERVICES module reference count */

/*
 *  ======== SERVICES_Exit ========
 *  Purpose:
 *      Discontinue usage of module; free resources when reference count
 *      reaches 0.
 */
void SERVICES_Exit(void)
{
	DBC_Require(cRefs > 0);

	GT_1trace(SERVICES_debugMask, GT_5CLASS, "SERVICES_Exit: cRefs 0x%x\n",
		 cRefs);

	cRefs--;
	if (cRefs == 0) {
		/* Uninitialize all SERVICES modules here */
		NTFY_Exit();
		UTIL_Exit();
		SYNC_Exit();
		CLK_Exit();
		REG_Exit();
		LST_Exit();
		KFILE_Exit();
		DPC_Exit();
		DBG_Exit();
		CSL_Exit();
		CFG_Exit();
		MEM_Exit();

		GT_exit();
	}

	DBC_Ensure(cRefs >= 0);
}

/*
 *  ======== SERVICES_Init ========
 *  Purpose:
 *      Initializes SERVICES modules.
 */
bool SERVICES_Init(void)
{
	bool fInit = true;
       bool fCFG, fCSL, fDBG, fDPC, fKFILE, fLST, fMEM;
       bool fREG, fSYNC, fCLK, fUTIL, fNTFY;

	DBC_Require(cRefs >= 0);

	if (cRefs == 0) {

		GT_init();
		GT_create(&SERVICES_debugMask, "OS");	/* OS for OSal */

		GT_0trace(SERVICES_debugMask, GT_ENTER,
			 "SERVICES_Init: entered\n");

		/* Perform required initialization of SERVICES modules. */
		fMEM = MEM_Init();
		fREG = REG_Init();
		fCFG = CFG_Init();
		fCSL = CSL_Init();
		fDBG = DBG_Init();
		fDPC = DPC_Init();
		fKFILE = KFILE_Init();
		fLST = LST_Init();
		/* fREG = REG_Init(); */
		fSYNC = SYNC_Init();
		fCLK  = CLK_Init();
		fUTIL = UTIL_Init();
		fNTFY = NTFY_Init();

               fInit = fCFG && fCSL && fDBG && fDPC && fKFILE &&
                       fLST && fMEM && fREG && fSYNC && fCLK && fUTIL;

		if (!fInit) {
			if (fNTFY)
				NTFY_Exit();

			if (fUTIL)
				UTIL_Exit();

			if (fSYNC)
				SYNC_Exit();

			if (fCLK)
				CLK_Exit();

			if (fREG)
				REG_Exit();

			if (fLST)
				LST_Exit();

			if (fKFILE)
				KFILE_Exit();

			if (fDPC)
				DPC_Exit();

			if (fDBG)
				DBG_Exit();

			if (fCSL)
				CSL_Exit();

			if (fCFG)
				CFG_Exit();

			if (fMEM)
				MEM_Exit();

		}
	}

	if (fInit)
		cRefs++;

	GT_1trace(SERVICES_debugMask, GT_5CLASS, "SERVICES_Init: cRefs 0x%x\n",
		 cRefs);

	DBC_Ensure((fInit && (cRefs > 0)) || (!fInit && (cRefs >= 0)));

	return fInit;
}

