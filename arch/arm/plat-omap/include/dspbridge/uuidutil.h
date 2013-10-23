/*
 * uuidutil.h
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
 *  ======== uuidutil.h ========
 *  Description:
 *      This file contains the specification of UUID helper functions.
 *
 *! Revision History
 *! ================
 *! 09-Nov-2000 kc: Modified description of UUID utility functions.
 *! 29-Sep-2000 kc: Appended "UUID_" prefix to UUID helper functions.
 *! 10-Aug-2000 kc: Created.
 *!
 */

#ifndef UUIDUTIL_
#define UUIDUTIL_

#define MAXUUIDLEN  37

/*
 *  ======== UUID_UuidToString ========
 *  Purpose:
 *      Converts a DSP_UUID to an ANSI string.
 *  Parameters:
 *      pUuid:      Pointer to a DSP_UUID object.
 *      pszUuid:    Pointer to a buffer to receive a NULL-terminated UUID
 *                  string.
 *      size:	    Maximum size of the pszUuid string.
 *  Returns:
 *  Requires:
 *      pUuid & pszUuid are non-NULL values.
 *  Ensures:
 *      Lenghth of pszUuid is less than MAXUUIDLEN.
 *  Details:
 *      UUID string limit currently set at MAXUUIDLEN.
 */
	void UUID_UuidToString(IN struct DSP_UUID *pUuid, OUT char *pszUuid,
			       s32 size);

/*
 *  ======== UUID_UuidFromString ========
 *  Purpose:
 *      Converts an ANSI string to a DSP_UUID.
 *  Parameters:
 *      pszUuid:    Pointer to a string that represents a DSP_UUID object.
 *      pUuid:      Pointer to a DSP_UUID object.
 *  Returns:
 *  Requires:
 *      pUuid & pszUuid are non-NULL values.
 *  Ensures:
 *  Details:
 *      We assume the string representation of a UUID has the following format:
 *      "12345678_1234_1234_1234_123456789abc".
 */
	extern void UUID_UuidFromString(IN char *pszUuid,
					OUT struct DSP_UUID *pUuid);

#endif				/* UUIDUTIL_ */
