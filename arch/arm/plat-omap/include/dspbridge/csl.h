/*
 * csl.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Copyright (C) 2008 Texas Instruments, Inc.
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
 *  ======== csl.h ========
 *  Purpose:
 *      Platform independent C Standard library functions.
 *
 *  Public Functions:
 *      CSL_AnsiToWchar
 *      CSL_ByteSwap
 *      CSL_Exit
 *      CSL_Init
 *      CSL_NumToAscii
 *      CSL_Strtok
 *      CSL_Strtokr
 *      CSL_WcharToAnsi
 *
 *! Revision History:
 *! ================
 *! 07-Aug-2002 jeh: Added CSL_Strtokr().
 *! 21-Sep-2001 jeh: Added CSL_Strncmp.
 *! 22-Nov-2000 map: Added CSL_Atoi and CSL_Strtok
 *! 19-Nov-2000 kc:  Added CSL_ByteSwap().
 *! 09-Nov-2000 kc:  Added CSL_Strncat.
 *! 29-Oct-1999 kc:  Added CSL_Wstrlen().
 *! 20-Sep-1999 ag:  Added CSL_Wchar2Ansi().
 *! 19-Jan-1998 cr:  Code review cleanup (mostly documentation fixes).
 *! 29-Dec-1997 cr:  Changed CSL_lowercase to CSL_Uppercase, added
 *!                  CSL_AnsiToWchar.
 *! 30-Sep-1997 cr:  Added explicit cdecl descriptors to fxn definitions.
 *! 25-Jun-1997 cr:  Added CSL_strcmp.
 *! 12-Jun-1996 gp:  Created.
 */

#ifndef CSL_
#define CSL_

#include <dspbridge/host_os.h>

/*
 *  ======== CSL_Exit ========
 *  Purpose:
 *      Discontinue usage of the CSL module.
 *  Parameters:
 *  Returns:
 *  Requires:
 *      CSL initialized.
 *  Ensures:
 *      Resources acquired in CSL_Init(void) are freed.
 */
	extern void CSL_Exit(void);

/*
 *  ======== CSL_Init ========
 *  Purpose:
 *      Initialize the CSL module's private state.
 *  Parameters:
 *  Returns:
 *      TRUE if initialized; FALSE if error occured.
 *  Requires:
 *  Ensures:
 *      A requirement for each of the other public CSL functions.
 */
	extern bool CSL_Init(void);

/*
 *  ======== CSL_NumToAscii ========
 *  Purpose:
 *      Convert a 1 or 2 digit number to a 2 digit string.
 *  Parameters:
 *      pstrNumber: Buffer to store converted string.
 *      dwNum:      Number to convert.
 *  Returns:
 *  Requires:
 *      pstrNumber must be able to hold at least three characters.
 *  Ensures:
 *      pstrNumber will be null terminated.
 */
	extern void CSL_NumToAscii(OUT char *pstrNumber, IN u32 dwNum);


/*
 *  ======== CSL_Strtok ========
 *  Purpose:
 *      Tokenize a NULL terminated string
 *  Parameters:
 *      ptstrSrc:       pointer to string.
 *      szSeparators:   pointer to a string of seperators
 *  Returns:
 *      char *
 *  Requires:
 *      CSL initialized.
 *      ptstrSrc is a valid string pointer.
 *      szSeparators is a valid string pointer.
 *  Ensures:
 */
	extern char *CSL_Strtok(IN char *ptstrSrc,
				IN CONST char *szSeparators);

/*
 *  ======== CSL_Strtokr ========
 *  Purpose:
 *      Re-entrant version of strtok.
 *  Parameters:
 *      pstrSrc:        Pointer to string. May be NULL on subsequent calls.
 *      szSeparators:   Pointer to a string of seperators
 *      ppstrCur:       Location to store start of string for next call to
 *                      to CSL_Strtokr.
 *  Returns:
 *      char * (the token)
 *  Requires:
 *      CSL initialized.
 *      szSeparators != NULL
 *      ppstrCur != NULL
 *  Ensures:
 */
	extern char *CSL_Strtokr(IN char *pstrSrc,
				 IN CONST char *szSeparators,
				 OUT char **ppstrCur);

#endif				/* CSL_ */
