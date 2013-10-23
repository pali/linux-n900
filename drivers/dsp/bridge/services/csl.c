/*
 * csl.c
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
 *  ======== cslce.c ========
 *  Purpose:
 *      Provides platform independent C Standard library functions.
 *
 *  Public Functions:
 *      CSL_Atoi
 *      CSL_Exit
 *      CSL_Init
 *      CSL_NumToAscii
 *      CSL_Strtokr
 *
 *! Revision History:
 *! ================
 *! 07-Aug-2002 jeh: Added CSL_Strtokr().
 *! 21-Sep-2001 jeh: Added CSL_Strncmp(). Alphabetized functions.
 *! 22-Nov-2000 map: Added CSL_Atoi and CSL_Strtok
 *! 19-Nov-2000 kc: Added CSL_ByteSwap.
 *! 09-Nov-2000 kc: Added CSL_Strncat.
 *! 03-Feb-2000 rr: Module init/exit is handled by SERVICES Init/Exit.
 *!		 GT Changes.
 *! 15-Dec-1999 ag: Removed incorrect assertion CSL_NumToAscii()
 *! 29-Oct-1999 kc: Added CSL_Wstrlen for UNICODE strings.
 *! 30-Sep-1999 ag: Removed DBC assertion (!CSL_DebugMask.flags) in
 *		  CSP_Init().
 *! 20-Sep-1999 ag: Added CSL_WcharToAnsi().
 *!		 Removed call to GT_set().
 *! 19-Jan-1998 cr: Code review cleanup.
 *! 29-Dec-1997 cr: Made platform independant, using MS CRT code, and
 *!		 combined csl32.c csl95.c and cslnt.c into csl.c.  Also
 *!		 changed CSL_lowercase to CSL_Uppercase.
 *! 21-Aug-1997 gp: Fix to CSL_strcpyn to initialize Source string, the NT way.
 *! 25-Jun-1997 cr: Created from csl95, added CSL_strcmp.
 */

/* ----------------------------------- Host OS */
#include <dspbridge/host_os.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>
#include <dspbridge/dbdefs.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>
#include <dspbridge/gt.h>

/*  ----------------------------------- This */
#include <dspbridge/csl.h>

/* Is character c in the string pstrDelim? */
#define IsDelimiter(c, pstrDelim) ((c != '\0') && \
				   (strchr(pstrDelim, c) != NULL))

/*  ----------------------------------- Globals */
#if GT_TRACE
static struct GT_Mask CSL_DebugMask = { NULL, NULL };	/* GT trace var. */
#endif

/*
 *  ======== CSL_Exit ========
 *  Purpose:
 *      Discontinue usage of the CSL module.
 */
void CSL_Exit(void)
{
	GT_0trace(CSL_DebugMask, GT_5CLASS, "CSL_Exit\n");
}

/*
 *  ======== CSL_Init ========
 *  Purpose:
 *      Initialize the CSL module's private state.
 */
bool CSL_Init(void)
{
	GT_create(&CSL_DebugMask, "CS");

	GT_0trace(CSL_DebugMask, GT_5CLASS, "CSL_Init\n");

	return true;
}

/*
 *  ======== CSL_NumToAscii ========
 *  Purpose:
 *      Convert a 1 or 2 digit number to a 2 digit string.
 */
void CSL_NumToAscii(OUT char *pstrNumber, u32 dwNum)
{
	char tens;

	DBC_Require(dwNum < 100);

	if (dwNum < 100) {
		tens = (char) dwNum / 10;
		dwNum = dwNum % 10;

		if (tens) {
			pstrNumber[0] = tens + '0';
			pstrNumber[1] = (char) dwNum + '0';
			pstrNumber[2] = '\0';
		} else {
			pstrNumber[0] = (char) dwNum + '0';
			pstrNumber[1] = '\0';
		}
	} else {
		pstrNumber[0] = '\0';
	}
}




/*
 *  ======= CSL_Strtokr =======
 *  Purpose:
 *      Re-entrant version of strtok.
 */
char *CSL_Strtokr(IN char *pstrSrc, IN CONST char *szSeparators,
		  OUT char **ppstrLast)
{
	char *pstrTemp;
	char *pstrToken;

	DBC_Require(szSeparators != NULL);
	DBC_Require(ppstrLast != NULL);
	DBC_Require(pstrSrc != NULL || *ppstrLast != NULL);

	/*
	 *  Set string location to beginning (pstrSrc != NULL) or to the
	 *  beginning of the next token.
	 */
	pstrTemp = (pstrSrc != NULL) ? pstrSrc : *ppstrLast;
	if (*pstrTemp == '\0') {
		pstrToken = NULL;
	} else {
		pstrToken = pstrTemp;
		while (*pstrTemp != '\0' && !IsDelimiter(*pstrTemp,
		      szSeparators)) {
			pstrTemp++;
		}
		if (*pstrTemp != '\0') {
			while (IsDelimiter(*pstrTemp, szSeparators)) {
				/* TODO: Shouldn't we do this for
				 * only 1 char?? */
				*pstrTemp = '\0';
				pstrTemp++;
			}
		}

		/* Location in string for next call */
		*ppstrLast = pstrTemp;
	}

	return pstrToken;
}
