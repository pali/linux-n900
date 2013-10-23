/*
 * reg.c
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
 *  ======== regce.c ========
 *  Purpose:
 *      Provide registry functions.
 *
 *  Public Functions:
 *      REG_DeleteValue
 *      REG_EnumValue
 *      REG_Exit
 *      REG_GetValue
 *      REG_Init
 *      REG_SetValue
 *
 *! Revision History:
 *! ================
 *
 */

/*  ----------------------------------- Host OS */
#include <dspbridge/host_os.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>
#include <dspbridge/dbdefs.h>
#include <dspbridge/errbase.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>
#include <dspbridge/gt.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/csl.h>
#include <dspbridge/mem.h>

/*  ----------------------------------- Others */
#include <dspbridge/dbreg.h>

/*  ----------------------------------- This */
#include <dspbridge/reg.h>
#include <regsup.h>

#if GT_TRACE
struct GT_Mask REG_debugMask = { NULL, NULL };	/* GT trace var. */
#endif

/*
 *  ======== REG_DeleteValue ========
 *  Deletes a registry entry value.  NOTE:  A registry entry value is not the
 *  same as *  a registry key.
 */
DSP_STATUS REG_DeleteValue(OPTIONAL IN HANDLE *phKey, IN CONST char *pstrSubkey,
			   IN CONST char *pstrValue)
{
	DSP_STATUS status;
	DBC_Require(pstrSubkey && pstrValue);
	DBC_Require(phKey == NULL);
       DBC_Require(strlen(pstrSubkey) < REG_MAXREGPATHLENGTH);
       DBC_Require(strlen(pstrValue) < REG_MAXREGPATHLENGTH);

	GT_0trace(REG_debugMask, GT_ENTER, "REG_DeleteValue: entered\n");

	/*  Note that we don't use phKey */
	if (regsupDeleteValue(pstrSubkey, pstrValue) == DSP_SOK)
		status = DSP_SOK;
	else
		status = DSP_EFAIL;

	return status;
}

/*
 *  ======== REG_EnumValue ========
 *  Enumerates a registry key and retrieve values stored under the key.
 *  We will assume the input pdwValueSize is smaller than
 *  REG_MAXREGPATHLENGTH for implementation purposes.
 */
DSP_STATUS REG_EnumValue(IN HANDLE *phKey, IN u32 dwIndex,
			 IN CONST char *pstrKey, IN OUT char *pstrValue,
			 IN OUT u32 *pdwValueSize, IN OUT char *pstrData,
			 IN OUT u32 *pdwDataSize)
{
	DSP_STATUS status;

	DBC_Require(pstrKey && pstrValue && pdwValueSize && pstrData &&
		    pdwDataSize);
	DBC_Require(*pdwValueSize <= REG_MAXREGPATHLENGTH);
	DBC_Require(phKey == NULL);
       DBC_Require(strlen(pstrKey) < REG_MAXREGPATHLENGTH);

	GT_0trace(REG_debugMask, GT_ENTER, "REG_EnumValue: entered\n");

	status = regsupEnumValue(dwIndex, pstrKey, pstrValue, pdwValueSize,
				 pstrData, pdwDataSize);

	return status;
}

/*
 *  ======== REG_Exit ========
 *  Discontinue usage of the REG module.
 */
void REG_Exit(void)
{
	GT_0trace(REG_debugMask, GT_5CLASS, "REG_Exit\n");

	regsupExit();
}

/*
 *  ======== REG_GetValue ========
 *  Retrieve a value from the registry.
 */
DSP_STATUS REG_GetValue(OPTIONAL IN HANDLE *phKey, IN CONST char *pstrSubkey,
			IN CONST char *pstrValue, OUT u8 *pbData,
			IN OUT u32 *pdwDataSize)
{
	DSP_STATUS status;

	DBC_Require(pstrSubkey && pstrValue && pbData);
	DBC_Require(phKey == NULL);
       DBC_Require(strlen(pstrSubkey) < REG_MAXREGPATHLENGTH);
       DBC_Require(strlen(pstrValue) < REG_MAXREGPATHLENGTH);

	GT_0trace(REG_debugMask, GT_ENTER, "REG_GetValue: entered\n");

	/*  We need to use regsup calls...  */
	/*  ...for now we don't need the key handle or  */
	/*  the subkey, all we need is the value to lookup.  */
	if (regsupGetValue((char *)pstrValue, pbData, pdwDataSize) == DSP_SOK)
		status = DSP_SOK;
	else
		status = DSP_EFAIL;

	return status;
}

/*
 *  ======== REG_Init ========
 *  Initialize the REG module's private state.
 */
bool REG_Init(void)
{
	bool fInit;

	GT_create(&REG_debugMask, "RG");	/* RG for ReG */

	fInit = regsupInit();

	GT_0trace(REG_debugMask, GT_5CLASS, "REG_Init\n");

	return fInit;
}

/*
 *  ======== REG_SetValue ========
 *  Set a value in the registry.
 */
DSP_STATUS REG_SetValue(OPTIONAL IN HANDLE *phKey, IN CONST char *pstrSubkey,
			IN CONST char *pstrValue, IN CONST u32 dwType,
			IN u8 *pbData, IN u32 dwDataSize)
{
	DSP_STATUS status;

	DBC_Require(pstrValue && pbData);
	DBC_Require(phKey == NULL);
	DBC_Require(dwDataSize > 0);
       DBC_Require(strlen(pstrValue) < REG_MAXREGPATHLENGTH);

	/*  We need to use regsup calls...  */
	/*  ...for now we don't need the key handle or  */
	/*  the subkey, all we need is the value to lookup.  */
	if (regsupSetValue((char *)pstrValue, pbData, dwDataSize) == DSP_SOK)
		status = DSP_SOK;
	else
		status = DSP_EFAIL;

	return status;
}

