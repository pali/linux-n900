/*
 * cfg.c
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
 *  ======== cfgce.c ========
 *  Purpose:
 *      Implementation of platform specific config services.
 *
 *  Private Functions:
 *      CFG_Exit
 *      CFG_GetAutoStart
 *      CFG_GetDevObject
 *      CFG_GetDSPResources
 *      CFG_GetExecFile
 *      CFG_GetHostResources
 *      CFG_GetObject
 *      CFG_Init
 *      CFG_SetDevObject
 *      CFG_SetObject
 *
 *
 *! Revision History:
 *! ================
 *! 26-Arp-2004 hp  Support for handling more than one Device.
 *! 26-Feb-2003 kc  Removed unused CFG fxns.
 *! 10-Nov-2000 rr: CFG_GetBoardName local var initialized.
 *! 30-Oct-2000 kc: Changed local var. names to use Hungarian notation.
 *! 10-Aug-2000 rr: Cosmetic changes.
 *! 26-Jul-2000 rr: Added CFG_GetDCDName. CFG_Get/SetObject(based on a flag)
 *!                  replaces CFG_GetMgrObject & CFG_SetMgrObject.
 *! 17-Jul-2000 rr: Added CFG_GetMgrObject & CFG_SetMgrObject.
 *! 03-Feb-2000 rr: Module init/exit is handled by SERVICES Init/Exit.
 *!		    GT Changes.
 *! 31-Jan-2000 rr: Comments and bugfixes:  modified after code review
 *! 07-Jan-2000 rr: CFG_GetBoardName Ensure class checks strlen of the
 *!                 read value from the registry against the passed in BufSize;
 *!                 CFG_GetZLFile,CFG_GetWMDFileName and
 *!                 CFG_GetExecFile also modified same way.
 *! 06-Jan-2000 rr: CFG_GetSearchPath & CFG_GetWinBRIDGEDir removed.
 *! 09-Dec-1999 rr: CFG_SetDevObject stores the DevNodeString pointer.
 *! 03-Dec-1999 rr: CFG_GetDevObject reads stored DevObject from Registry.
 *!                 CFG_GetDevNode reads the Devnodestring from the registry.
 *!                 CFG_SetDevObject stores the registry path as
 *!                 DevNodestring in the registry.
 *! 02-Dec-1999 rr: CFG_debugMask is declared static now. stdwin.h included
 *! 22-Nov-1999 kc: Added windows.h to remove warnings.
 *! 25-Oct-1999 rr: CFG_GetHostResources reads the HostResource structure
 *!                 from the registry which was set by the DRV Request
 *!                 Resources.
 *! 15-Oct-1999 rr: Changes in CFG_SetPrivateDword & HostResources reflecting
 *!                 changes for  drv.h resource structure and wsxreg.h new
 *!                 entry(DevObject) Hard coded entries removed for those items
 *! 08-Oct-1999 rr: CFG_SetPrivateDword modified. it sets devobject into the
 *!                 registry. CFG_Get HostResources modified for opening up
 *!                 two mem winodws.
 *! 24-Sep-1999 rr: CFG_GetHostResources uses hardcoded Registry calls,uses NT
 *!                 type of Resource Structure.
 *! 19-Jul-1999 a0216266: Stubbed from cfgnt.c.
 */

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>
#include <dspbridge/dbdefs.h>
#include <dspbridge/errbase.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>
#include <dspbridge/gt.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/csl.h>
#include <dspbridge/reg.h>

/*  ----------------------------------- Others */
#include <dspbridge/dbreg.h>

/*  ----------------------------------- This */
#include <dspbridge/cfg.h>
#include <dspbridge/list.h>

struct DRV_EXT {
	struct LST_ELEM link;
	char szString[MAXREGPATHLENGTH];
};

/*  ----------------------------------- Globals */
#if GT_TRACE
static struct GT_Mask CFG_debugMask = { NULL, NULL };	/* CFG debug Mask */
#endif

/*
 *  ======== CFG_Exit ========
 *  Purpose:
 *      Discontinue usage of the CFG module.
 */
void CFG_Exit(void)
{
	GT_0trace(CFG_debugMask, GT_5CLASS, "Entered CFG_Exit\n");
}

/*
 *  ======== CFG_GetAutoStart ========
 *  Purpose:
 *      Retreive the autostart mask, if any, for this board.
 */
DSP_STATUS CFG_GetAutoStart(struct CFG_DEVNODE *hDevNode,
			    OUT u32 *pdwAutoStart)
{
	DSP_STATUS status = DSP_SOK;
	u32 dwBufSize;
	GT_2trace(CFG_debugMask, GT_ENTER,
		  "Entered CFG_GetAutoStart: \n\thDevNode:"
		  "0x%x\n\tpdwAutoStart: 0x%x\n", hDevNode, pdwAutoStart);
	dwBufSize = sizeof(*pdwAutoStart);
	if (!hDevNode)
		status = CFG_E_INVALIDHDEVNODE;
	if (!pdwAutoStart)
		status = CFG_E_INVALIDPOINTER;
	if (DSP_SUCCEEDED(status)) {
		status = REG_GetValue(NULL, (char *)hDevNode, AUTOSTART,
				     (u8 *)pdwAutoStart, &dwBufSize);
		if (DSP_FAILED(status))
			status = CFG_E_RESOURCENOTAVAIL;
	}
#ifdef DEBUG
	if (DSP_SUCCEEDED(status)) {
		GT_0trace(CFG_debugMask, GT_1CLASS,
			 "CFG_GetAutoStart SUCCESS \n");
	} else {
		GT_0trace(CFG_debugMask, GT_6CLASS,
		"CFG_GetAutoStart Failed \n");
	}
#endif
	DBC_Ensure((status == DSP_SOK &&
		(*pdwAutoStart == 0 || *pdwAutoStart == 1))
		|| status != DSP_SOK);
	return status;
}

/*
 *  ======== CFG_GetDevObject ========
 *  Purpose:
 *      Retrieve the Device Object handle for a given devnode.
 */
DSP_STATUS CFG_GetDevObject(struct CFG_DEVNODE *hDevNode, OUT u32 *pdwValue)
{
	DSP_STATUS status = DSP_SOK;
	u32 dwBufSize;
	GT_2trace(CFG_debugMask, GT_ENTER, "Entered CFG_GetDevObject, args: "
		 "\n\thDevNode: 0x%x\n\tpdwValue: 0x%x\n", hDevNode,
		 *pdwValue);
	if (!hDevNode)
		status = CFG_E_INVALIDHDEVNODE;

	if (!pdwValue)
		status = CFG_E_INVALIDHDEVNODE;

	dwBufSize = sizeof(pdwValue);
	if (DSP_SUCCEEDED(status)) {

		/* check the device string and then call the REG_SetValue*/
               if (!(strcmp((char *)((struct DRV_EXT *)hDevNode)->szString,
							"TIOMAP1510"))) {
			GT_0trace(CFG_debugMask, GT_1CLASS,
				  "Fetching DSP Device from "
				  "Registry \n");
			status = REG_GetValue(NULL, (char *)hDevNode,
					      "DEVICE_DSP",
					      (u8 *)pdwValue, &dwBufSize);
		} else {
			GT_0trace(CFG_debugMask, GT_6CLASS,
				  "Failed to Identify the Device to Fetch \n");
		}
	}
#ifdef DEBUG
	if (DSP_SUCCEEDED(status)) {
		GT_1trace(CFG_debugMask, GT_1CLASS,
			  "CFG_GetDevObject SUCCESS DevObject"
			  ": 0x%x\n ", *pdwValue);
	} else {
		GT_0trace(CFG_debugMask, GT_6CLASS,
			  "CFG_GetDevObject Failed \n");
	}
#endif
	return status;
}

/*
 *  ======== CFG_GetDSPResources ========
 *  Purpose:
 *      Get the DSP resources available to a given device.
 */
DSP_STATUS CFG_GetDSPResources(struct CFG_DEVNODE *hDevNode,
			       OUT struct CFG_DSPRES *pDSPResTable)
{
	DSP_STATUS status = DSP_SOK;	/* return value */
	u32 dwResSize;
	GT_2trace(CFG_debugMask, GT_ENTER,
		  "Entered CFG_GetDSPResources, args: "
		  "\n\thDevNode:  0x%x\n\tpDSPResTable:  0x%x\n",
		  hDevNode, pDSPResTable);
	if (!hDevNode) {
		status = CFG_E_INVALIDHDEVNODE;
	} else if (!pDSPResTable) {
		status = CFG_E_INVALIDPOINTER;
	} else {
		status = REG_GetValue(NULL, CONFIG, DSPRESOURCES,
				     (u8 *)pDSPResTable,
				     &dwResSize);
	}
	if (DSP_SUCCEEDED(status)) {
		GT_0trace(CFG_debugMask, GT_1CLASS,
			  "CFG_GetDSPResources SUCCESS\n");
	} else {
		status = CFG_E_RESOURCENOTAVAIL;
		GT_0trace(CFG_debugMask, GT_6CLASS,
			  "CFG_GetDSPResources Failed \n");
	}
#ifdef DEBUG
	/* assert that resource values are reasonable */
	DBC_Assert(pDSPResTable->uChipType < 256);
	DBC_Assert(pDSPResTable->uWordSize > 0);
	DBC_Assert(pDSPResTable->uWordSize < 32);
	DBC_Assert(pDSPResTable->cChips > 0);
	DBC_Assert(pDSPResTable->cChips < 256);
#endif
	return status;
}

/*
 *  ======== CFG_GetExecFile ========
 *  Purpose:
 *      Retreive the default executable, if any, for this board.
 */
DSP_STATUS CFG_GetExecFile(struct CFG_DEVNODE *hDevNode, u32 ulBufSize,
			   OUT char *pstrExecFile)
{
	DSP_STATUS status = DSP_SOK;
	u32 cExecSize = ulBufSize;
	GT_3trace(CFG_debugMask, GT_ENTER,
		  "Entered CFG_GetExecFile:\n\tthDevNode: "
		  "0x%x\n\tulBufSize: 0x%x\n\tpstrExecFile: 0x%x\n", hDevNode,
		  ulBufSize, pstrExecFile);
	if (!hDevNode)
		status = CFG_E_INVALIDHDEVNODE;

	if (!pstrExecFile)
		status = CFG_E_INVALIDPOINTER;

	if (DSP_SUCCEEDED(status)) {
		status = REG_GetValue(NULL, (char *)hDevNode, DEFEXEC,
				     (u8 *)pstrExecFile, &cExecSize);
		if (DSP_FAILED(status))
			status = CFG_E_RESOURCENOTAVAIL;
		else if (cExecSize > ulBufSize)
			status = DSP_ESIZE;

	}
#ifdef DEBUG
	if (DSP_SUCCEEDED(status)) {
		GT_1trace(CFG_debugMask, GT_1CLASS,
			  "CFG_GetExecFile SUCCESS Exec File"
			  "name : %s\n ", pstrExecFile);
	} else {
		GT_0trace(CFG_debugMask, GT_6CLASS,
			  "CFG_GetExecFile Failed \n");
	}
#endif
	DBC_Ensure(((status == DSP_SOK) &&
                 (strlen(pstrExecFile) <= ulBufSize)) || (status != DSP_SOK));
	return status;
}

/*
 *  ======== CFG_GetHostResources ========
 *  Purpose:
 *      Get the Host allocated resources assigned to a given device.
 */
DSP_STATUS CFG_GetHostResources(struct CFG_DEVNODE *hDevNode,
				OUT struct CFG_HOSTRES *pHostResTable)
{
	DSP_STATUS status = DSP_SOK;
	u32 dwBufSize;
	GT_2trace(CFG_debugMask, GT_ENTER,
		  "Entered CFG_GetHostResources, args:\n\t"
		  "pHostResTable:  0x%x\n\thDevNode:  0x%x\n",
		  pHostResTable, hDevNode);
	if (!hDevNode)
		status = CFG_E_INVALIDHDEVNODE;

	if (!pHostResTable)
		status = CFG_E_INVALIDPOINTER;

	if (DSP_SUCCEEDED(status)) {
		dwBufSize = sizeof(struct CFG_HOSTRES);
		if (DSP_FAILED(REG_GetValue(NULL, (char *)hDevNode,
			       CURRENTCONFIG,
			      (u8 *)pHostResTable, &dwBufSize))) {
			status = CFG_E_RESOURCENOTAVAIL;
		}
	}
#ifdef DEBUG
	if (DSP_SUCCEEDED(status)) {
		GT_0trace(CFG_debugMask, GT_1CLASS,
			  "CFG_GetHostResources SUCCESS \n");
	} else {
		GT_0trace(CFG_debugMask, GT_6CLASS,
			  "CFG_GetHostResources Failed \n");
	}
#endif
	return status;
}

/*
 *  ======== CFG_GetObject ========
 *  Purpose:
 *      Retrieve the Object handle from the Registry
 */
DSP_STATUS CFG_GetObject(OUT u32 *pdwValue, u32 dwType)
{
	DSP_STATUS status = DSP_EINVALIDARG;
	u32 dwBufSize;
	DBC_Require(pdwValue != NULL);
	GT_1trace(CFG_debugMask, GT_ENTER,
		 "Entered CFG_GetObject, args:pdwValue: "
		 "0x%x\n", *pdwValue);
	dwBufSize = sizeof(pdwValue);
	switch (dwType) {
	case (REG_DRV_OBJECT):
		status = REG_GetValue(NULL, CONFIG, DRVOBJECT,
				     (u8 *)pdwValue,
				     &dwBufSize);
		break;
	case (REG_MGR_OBJECT):
		status = REG_GetValue(NULL, CONFIG, MGROBJECT,
				     (u8 *)pdwValue,
				     &dwBufSize);
		break;
	default:
		break;
	}
	if (DSP_SUCCEEDED(status)) {
		GT_1trace(CFG_debugMask, GT_1CLASS,
			  "CFG_GetObject SUCCESS DrvObject: "
			  "0x%x\n ", *pdwValue);
	} else {
		status = CFG_E_RESOURCENOTAVAIL;
		*pdwValue = 0;
		GT_0trace(CFG_debugMask, GT_6CLASS, "CFG_GetObject Failed \n");
	}
	DBC_Ensure((DSP_SUCCEEDED(status) && *pdwValue != 0) ||
		   (DSP_FAILED(status) && *pdwValue == 0));
	return status;
}

/*
 *  ======== CFG_Init ========
 *  Purpose:
 *      Initialize the CFG module's private state.
 */
bool CFG_Init(void)
{
	struct CFG_DSPRES dspResources;
	GT_create(&CFG_debugMask, "CF");	/* CF for ConFig */
	GT_0trace(CFG_debugMask, GT_5CLASS, "Entered CFG_Init\n");
	GT_0trace(CFG_debugMask, GT_5CLASS, "Intializing DSP Registry Info \n");

	dspResources.uChipType = DSPTYPE_64;
	dspResources.cChips = 1;
	dspResources.uWordSize = DSPWORDSIZE;
	dspResources.cMemTypes = 0;
	dspResources.aMemDesc[0].uMemType = 0;
	dspResources.aMemDesc[0].ulMin = 0;
	dspResources.aMemDesc[0].ulMax = 0;
	if (DSP_SUCCEEDED(REG_SetValue(NULL, CONFIG, DSPRESOURCES, REG_BINARY,
			 (u8 *)&dspResources, sizeof(struct CFG_DSPRES)))) {
		GT_0trace(CFG_debugMask, GT_5CLASS,
			  "Initialized DSP resources in "
			  "Registry \n");
	} else
		GT_0trace(CFG_debugMask, GT_5CLASS,
			  "Failed to Initialize DSP resources"
			  " in Registry \n");
	return true;
}

/*
 *  ======== CFG_SetDevObject ========
 *  Purpose:
 *      Store the Device Object handle and devNode pointer for a given devnode.
 */
DSP_STATUS CFG_SetDevObject(struct CFG_DEVNODE *hDevNode, u32 dwValue)
{
	DSP_STATUS status = DSP_SOK;
	u32 dwBuffSize;
	GT_2trace(CFG_debugMask, GT_ENTER,
		  "Entered CFG_SetDevObject, args: \n\t"
		  "hDevNode: 0x%x\n\tdwValue: 0x%x\n", hDevNode, dwValue);
	if (!hDevNode)
		status = CFG_E_INVALIDHDEVNODE;

	dwBuffSize = sizeof(dwValue);
	if (DSP_SUCCEEDED(status)) {
		/* Store the WCD device object in the Registry */

               if (!(strcmp((char *)hDevNode, "TIOMAP1510"))) {
			GT_0trace(CFG_debugMask, GT_1CLASS,
				  "Registering the DSP Device \n");
			status = REG_SetValue(NULL, (char *)hDevNode,
				  "DEVICE_DSP", REG_DWORD,\
				  (u8 *)&dwValue, dwBuffSize);
			if (DSP_SUCCEEDED(status)) {
				dwBuffSize = sizeof(hDevNode);
				status = REG_SetValue(NULL,
					  (char *)hDevNode, "DEVNODESTRING_DSP",
					  REG_DWORD, (u8 *)&hDevNode,
					  dwBuffSize);
			}
		} else {
			GT_0trace(CFG_debugMask, GT_6CLASS,
				  "Failed to Register Device \n");
		}
	}
#ifdef DEBUG
	if (DSP_SUCCEEDED(status)) {
		GT_0trace(CFG_debugMask, GT_1CLASS,
			  "CFG_SetDevObject SUCCESS \n");
	} else {
		GT_0trace(CFG_debugMask, GT_6CLASS,
			  "CFG_SetDevObject Failed \n");
	}
#endif
	return status;
}

/*
 *  ======== CFG_SetObject ========
 *  Purpose:
 *      Store the Driver Object handle
 */
DSP_STATUS CFG_SetObject(u32 dwValue, u32 dwType)
{
	DSP_STATUS status = DSP_EINVALIDARG;
	u32 dwBuffSize;
	GT_1trace(CFG_debugMask, GT_ENTER,
		  "Entered CFG_SetObject, args: dwValue: "
		  "0x%x\n", dwValue);
	dwBuffSize = sizeof(dwValue);
	switch (dwType) {
	case (REG_DRV_OBJECT):
		status = REG_SetValue(NULL, CONFIG, DRVOBJECT, REG_DWORD,
			 (u8 *)&dwValue, dwBuffSize);
		break;
	case (REG_MGR_OBJECT):
		status = REG_SetValue(NULL, CONFIG, MGROBJECT, REG_DWORD,
			 (u8 *) &dwValue, dwBuffSize);
		break;
	default:
		break;
	}
#ifdef DEBUG
	if (DSP_SUCCEEDED(status))
		GT_0trace(CFG_debugMask, GT_1CLASS, "CFG_SetObject SUCCESS \n");
	else
		GT_0trace(CFG_debugMask, GT_6CLASS, "CFG_SetObject Failed \n");

#endif
	return status;
}
