/*
 * reg.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Provide registry functions.
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

/*  ----------------------------------- Host OS */
#include <dspbridge/host_os.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>
#include <dspbridge/dbdefs.h>
#include <dspbridge/errbase.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/mem.h>

/*  ----------------------------------- This */
#include <dspbridge/reg.h>
#include <regsup.h>

/*
 *  ======== reg_delete_value ========
 *  Deletes a registry entry value.  NOTE:  A registry entry value is not the
 *  same as *  a registry key.
 */
dsp_status reg_delete_value(IN CONST char *pstrValue)
{
	dsp_status status;
	DBC_REQUIRE(strlen(pstrValue) < REG_MAXREGPATHLENGTH);

	status = regsup_delete_value(pstrValue);

	return status;
}

/*
 *  ======== reg_enum_value ========
 *  Enumerates a registry key and retrieve values stored under the key.
 *  We will assume the input pdwValueSize is smaller than
 *  REG_MAXREGPATHLENGTH for implementation purposes.
 */
dsp_status reg_enum_value(IN u32 dw_index,
			  IN CONST char *pstrKey, IN OUT char *pstrValue,
			  IN OUT u32 *pdwValueSize, IN OUT char *pstrData,
			  IN OUT u32 *pdwDataSize)
{
	dsp_status status;

	DBC_REQUIRE(pstrKey && pstrValue && pdwValueSize && pstrData &&
		    pdwDataSize);
	DBC_REQUIRE(*pdwValueSize <= REG_MAXREGPATHLENGTH);
	DBC_REQUIRE(strlen(pstrKey) < REG_MAXREGPATHLENGTH);

	status = regsup_enum_value(dw_index, pstrKey, pstrValue, pdwValueSize,
				   pstrData, pdwDataSize);

	return status;
}

/*
 *  ======== reg_exit ========
 *  Discontinue usage of the REG module.
 */
void reg_exit(void)
{
	regsup_exit();
}

/*
 *  ======== reg_get_value ========
 *  Retrieve a value from the registry.
 */
dsp_status reg_get_value(IN CONST char *pstrValue, OUT u8 * pbData,
			 IN OUT u32 *pdwDataSize)
{
	dsp_status status;

	DBC_REQUIRE(pstrValue && pbData);
	DBC_REQUIRE(strlen(pstrValue) < REG_MAXREGPATHLENGTH);

	/*  We need to use regsup calls... */
	/*  ...for now we don't need the key handle or */
	/*  the subkey, all we need is the value to lookup. */
	if (regsup_get_value((char *)pstrValue, pbData, pdwDataSize) == DSP_SOK)
		status = DSP_SOK;
	else
		status = DSP_EFAIL;

	return status;
}

/*
 *  ======== reg_init ========
 *  Initialize the REG module's private state.
 */
bool reg_init(void)
{
	bool ret;

	ret = regsup_init();

	return ret;
}

/*
 *  ======== reg_set_value ========
 *  Set a value in the registry.
 */
dsp_status reg_set_value(IN CONST char *pstrValue, IN u8 * pbData,
			 IN u32 dw_data_size)
{
	dsp_status status;

	DBC_REQUIRE(pstrValue && pbData);
	DBC_REQUIRE(dw_data_size > 0);
	DBC_REQUIRE(strlen(pstrValue) < REG_MAXREGPATHLENGTH);

	/*
	 * We need to use regsup calls
	 * for now we don't need the key handle or
	 * the subkey, all we need is the value to lookup.
	 */
	status = regsup_set_value((char *)pstrValue, pbData, dw_data_size);

	return status;
}
