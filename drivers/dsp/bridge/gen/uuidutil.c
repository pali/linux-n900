/*
 * uuidutil.c
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
 *  ======== uuidutil.c ========
 *  Description:
 *  This file contains the implementation of UUID helper functions.
 *
 *! Revision History
 *! ================
 *! 23-Feb-2003 vp: Code review updates.
 *! 18-Oct-2003 vp: Ported to Linux platform.
 *! 31-Aug-2000 rr: UUID_UuidFromString bug fixed.
 *! 29-Aug-2000 rr: Modified UUID_UuidFromString.
 *! 09-Nov-2000 kc: Modified UUID_UuidFromString to simplify implementation.
 *! 30-Oct-2000 kc: Modified UUID utility module function prefix.
 *! 10-Aug-2000 kc: Created.
 *!
 */

/*  ----------------------------------- Host OS  */
#include <dspbridge/host_os.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>
#include <dspbridge/dbdefs.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>

/*  ----------------------------------- This */
#include <dspbridge/uuidutil.h>

/*
 *  ======== UUID_UuidToString ========
 *  Purpose:
 *      Converts a struct DSP_UUID to a string.
 *      Note: snprintf format specifier is:
 *      %[flags] [width] [.precision] [{h | l | I64 | L}]type
 */
void UUID_UuidToString(IN struct DSP_UUID *pUuid, OUT char *pszUuid,
		       IN s32 size)
{
	s32 i;			/* return result from snprintf. */

	DBC_Require(pUuid && pszUuid);

	i = snprintf(pszUuid, size,
		     "%.8X_%.4X_%.4X_%.2X%.2X_%.2X%.2X%.2X%.2X%.2X%.2X",
		     pUuid->ulData1, pUuid->usData2, pUuid->usData3,
		     pUuid->ucData4, pUuid->ucData5, pUuid->ucData6[0],
		     pUuid->ucData6[1], pUuid->ucData6[2], pUuid->ucData6[3],
		     pUuid->ucData6[4], pUuid->ucData6[5]);

	DBC_Ensure(i != -1);
}

/*
 *  ======== htoi ========
 *  Purpose:
 *      Converts a hex value to a decimal integer.
 */

static int htoi(char c)
{
	switch (c) {
	case '0':
		return 0;
	case '1':
		return 1;
	case '2':
		return 2;
	case '3':
		return 3;
	case '4':
		return 4;
	case '5':
		return 5;
	case '6':
		return 6;
	case '7':
		return 7;
	case '8':
		return 8;
	case '9':
		return 9;
	case 'A':
		return 10;
	case 'B':
		return 11;
	case 'C':
		return 12;
	case 'D':
		return 13;
	case 'E':
		return 14;
	case 'F':
		return 15;
	case 'a':
		return 10;
	case 'b':
		return 11;
	case 'c':
		return 12;
	case 'd':
		return 13;
	case 'e':
		return 14;
	case 'f':
		return 15;
	}
	return 0;
}

/*
 *  ======== UUID_UuidFromString ========
 *  Purpose:
 *      Converts a string to a struct DSP_UUID.
 */
void UUID_UuidFromString(IN char *pszUuid, OUT struct DSP_UUID *pUuid)
{
	char c;
	s32 i, j;
	s32 result;
	char *temp = pszUuid;

	result = 0;
	for (i = 0; i < 8; i++) {
		/* Get first character in string */
		c = *temp;

		/* Increase the results by new value */
		result *= 16;
		result += htoi(c);

		/* Go to next character in string */
		temp++;
	}
	pUuid->ulData1 = result;

	/* Step over underscore */
	temp++;

	result = 0;
	for (i = 0; i < 4; i++) {
		/* Get first character in string */
		c = *temp;

		/* Increase the results by new value */
		result *= 16;
		result += htoi(c);

		/* Go to next character in string */
		temp++;
	}
	pUuid->usData2 = (u16)result;

	/* Step over underscore */
	temp++;

	result = 0;
	for (i = 0; i < 4; i++) {
		/* Get first character in string */
		c = *temp;

		/* Increase the results by new value */
		result *= 16;
		result += htoi(c);

		/* Go to next character in string */
		temp++;
	}
	pUuid->usData3 = (u16)result;

	/* Step over underscore */
	temp++;

	result = 0;
	for (i = 0; i < 2; i++) {
		/* Get first character in string */
		c = *temp;

		/* Increase the results by new value */
		result *= 16;
		result += htoi(c);

		/* Go to next character in string */
		temp++;
	}
	pUuid->ucData4 = (u8)result;

	result = 0;
	for (i = 0; i < 2; i++) {
		/* Get first character in string */
		c = *temp;

		/* Increase the results by new value */
		result *= 16;
		result += htoi(c);

		/* Go to next character in string */
		temp++;
	}
	pUuid->ucData5 = (u8)result;

	/* Step over underscore */
	temp++;

	for (j = 0; j < 6; j++) {
		result = 0;
		for (i = 0; i < 2; i++) {
			/* Get first character in string */
			c = *temp;

			/* Increase the results by new value */
			result *= 16;
			result += htoi(c);

			/* Go to next character in string */
			temp++;
		}
		pUuid->ucData6[j] = (u8)result;
	}
}
