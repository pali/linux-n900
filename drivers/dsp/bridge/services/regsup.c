/*
 * regsup.c
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
 *  ======== regsup.c ========
 *  Purpose:
 *      Provide registry support functions.
 *
 *! Revision History:
 *! ================
 *! 28-May-2002  map: Integrated PSI's dspimage update mechanism
 *! 11-May-2002  gp:  Turned PERF "on".
 *! 21-May-2002  map: Fixed bug in SetValue - if resizing datasize, set
 *!		      new size too
 */

/*  ----------------------------------- Host OS */
#include <dspbridge/host_os.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>
#include <dspbridge/dbdefs.h>
#include <dspbridge/errbase.h>
#include <dspbridge/dbreg.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>
#include <dspbridge/gt.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/mem.h>
#include <dspbridge/csl.h>

/*  ----------------------------------- This */
#include <regsup.h>

struct RegValueStruct {
	char name[BRIDGE_MAX_NAME_SIZE];   /*  Name of a given value entry  */
	u32 dataSize;		/*  Size of the data  */
	void *pData;		/*  Pointer to the actual data  */
};

struct RegKeyStruct {
	/*The current number of value entries this key has*/
	u32 numValueEntries;
	/* Array of value entries */
	struct RegValueStruct values[BRIDGE_MAX_NUM_REG_ENTRIES];
};


/*  Pointer to the registry support key  */
static struct RegKeyStruct *pRegKey;

#if GT_TRACE
extern struct GT_Mask REG_debugMask;	/* GT trace var. */
/*
 *  ======== printS ========
 *  Purpose:
 *      Displays printable characters in pBuf, if any.
 */
static inline void printS(void *pBuf)
{
	int pos = 0;
	if (*(REG_debugMask).flags & (GT_2CLASS)) {
		while (*(u8 *)((pBuf)+pos) >= ' ' &&
		       *(u8 *)((pBuf)+pos) <= '~') {
			GT_1trace(REG_debugMask, GT_2CLASS, "%c",
					*(u8 *)((pBuf) + pos++));
		}

		GT_0trace(REG_debugMask, GT_2CLASS, "\n");
	}
}
#else
#define printS(pBuf)
#endif

/*
 *  ======== regsupInit ========
 *  Purpose:
 *      Initialize the Registry Support module's private state.
 */
bool regsupInit(void)
{
	if (pRegKey != NULL)
		return true;

	/*  Need to allocate and setup our registry.  */
	pRegKey = MEM_Calloc(sizeof(struct RegKeyStruct), MEM_NONPAGED);
	if (pRegKey == NULL)
		return false;

	return true;
}

/*
 *  ======== regsupExit ========
 *  Purpose:
 *      Release all registry support allocations.
 */
void regsupExit(void)
{
	u32 i;

	/*  Make sure data has actually been allocated.  */
	if (pRegKey == NULL) {
		/*  Nothing initialized.return!  */
		return;
	}

	GT_1trace(REG_debugMask, GT_2CLASS, "pRegKey->numValueEntries %d\n",
		  pRegKey->numValueEntries);

	/*  Now go through each entry and free all resources.  */
	for (i = 0; ((i < BRIDGE_MAX_NUM_REG_ENTRIES) &&
	    (i < pRegKey->numValueEntries)); i++) {
		if (pRegKey->values[i].name[0] != '\0') {
			/*  We have a valid entry.free it up!  */
			if (pRegKey->values[i].pData != NULL) {
				GT_3trace(REG_debugMask, GT_2CLASS,
					  "E %d\t %s DATA %x ", i,
					  pRegKey->values[i].name,
					  *(u32 *)pRegKey->values[i].pData);
				printS((u8 *)(pRegKey->values[i].pData));
				MEM_Free(pRegKey->values[i].pData);
			}
			pRegKey->values[i].pData = NULL;
			pRegKey->values[i].dataSize = 0;
			pRegKey->values[i].name[0] = '\0';
		}
	}

	/*  Now that all of the resources are freed up, free the main one!  */
	MEM_Free(pRegKey);

	/*  Don't forget to NULL out the global entry!  */
	pRegKey = NULL;
}

/*
 *  ======== regsupGetValue ========
 *  Purpose:
 *      Get the value of the entry having the given name.
 */
DSP_STATUS regsupGetValue(char *valName, void *pBuf, u32 *dataSize)
{
	DSP_STATUS retVal = DSP_EFAIL;
	u32 i;

	/*  Need to search through the entries looking for the right one.  */
	for (i = 0; i < pRegKey->numValueEntries; i++) {
		/*  See if the name matches.  */
               if (strncmp(pRegKey->values[i].name, valName,
		    BRIDGE_MAX_NAME_SIZE) == 0) {

			/*  We have a match!  Copy out the data.  */
			memcpy(pBuf, pRegKey->values[i].pData,
			       pRegKey->values[i].dataSize);

			/*  Get the size for the caller.  */
			*dataSize = pRegKey->values[i].dataSize;

			/*  Set our status to good and exit.  */
			retVal = DSP_SOK;
			break;
		}
	}

	if (DSP_SUCCEEDED(retVal)) {
		GT_2trace(REG_debugMask, GT_2CLASS, "G %s DATA %x ", valName,
			  *(u32 *)pBuf);
		printS((u8 *)pBuf);
	} else {
		GT_1trace(REG_debugMask, GT_3CLASS, "G %s FAILED\n", valName);
	}

	return retVal;
}

/*
 *  ======== regsupSetValue ========
 *  Purpose:
 *      Sets the value of the entry having the given name.
 */
DSP_STATUS regsupSetValue(char *valName, void *pBuf, u32 dataSize)
{
	DSP_STATUS retVal = DSP_EFAIL;
	u32 i;

	GT_2trace(REG_debugMask, GT_2CLASS, "S %s DATA %x ", valName,
		  *(u32 *)pBuf);
	printS((u8 *)pBuf);

	/*  Need to search through the entries looking for the right one.  */
	for (i = 0; i < pRegKey->numValueEntries; i++) {
		/*  See if the name matches.  */
               if (strncmp(pRegKey->values[i].name, valName,
		    BRIDGE_MAX_NAME_SIZE) == 0) {
			/*  Make sure the new data size is the same.  */
			if (dataSize != pRegKey->values[i].dataSize) {
				/*  The caller needs a different data size!  */
				MEM_Free(pRegKey->values[i].pData);
				pRegKey->values[i].pData = MEM_Alloc(dataSize,
							   MEM_NONPAGED);
				if (pRegKey->values[i].pData == NULL)
					break;

			}

			/*  We have a match!  Copy out the data.  */
			memcpy(pRegKey->values[i].pData, pBuf, dataSize);

			/* Reset datasize - overwrite if new or same */
			pRegKey->values[i].dataSize = dataSize;

			/*  Set our status to good and exit.  */
			retVal = DSP_SOK;
			break;
		}
	}

	/*  See if we found a match or if this is a new entry  */
	if (i == pRegKey->numValueEntries) {
		/*  No match, need to make a new entry  */
		/*  First check to see if we can make any more entries.  */
		if (pRegKey->numValueEntries < BRIDGE_MAX_NUM_REG_ENTRIES) {
			char *tmp_name =
				pRegKey->values[pRegKey->numValueEntries].name;
			strncpy(tmp_name, valName, BRIDGE_MAX_NAME_SIZE - 1);
			tmp_name[BRIDGE_MAX_NAME_SIZE - 1] = '\0';
			pRegKey->values[pRegKey->numValueEntries].pData =
					MEM_Alloc(dataSize, MEM_NONPAGED);
			if (pRegKey->values[pRegKey->numValueEntries].pData !=
									NULL) {
				memcpy(pRegKey->
					values[pRegKey->numValueEntries].pData,
					pBuf, dataSize);
				pRegKey->
				    values[pRegKey->numValueEntries].dataSize =
				    dataSize;
				pRegKey->numValueEntries++;
				retVal = DSP_SOK;
			}
		} else {
			GT_0trace(REG_debugMask, GT_7CLASS,
				  "MAX NUM REG ENTRIES REACHED\n");
		}
	}

	return retVal;
}

/*
 *  ======== regsupEnumValue ========
 *  Purpose:
 *      Returns registry "values" and their "data" under a (sub)key.
 */
DSP_STATUS regsupEnumValue(IN u32 dwIndex, IN CONST char *pstrKey,
			   IN OUT char *pstrValue, IN OUT u32 *pdwValueSize,
			   IN OUT char *pstrData, IN OUT u32 *pdwDataSize)
{
	DSP_STATUS retVal = REG_E_INVALIDSUBKEY;
	u32 i;
       u32 dwKeyLen;
	u32 count = 0;

       DBC_Require(pstrKey);
       dwKeyLen = strlen(pstrKey);

	/*  Need to search through the entries looking for the right one.  */
	for (i = 0; i < pRegKey->numValueEntries; i++) {
		/*  See if the name matches.  */
               if ((strncmp(pRegKey->values[i].name, pstrKey,
		    dwKeyLen) == 0) && count++ == dwIndex) {
			/*  We have a match!  Copy out the data.  */
			memcpy(pstrData, pRegKey->values[i].pData,
				pRegKey->values[i].dataSize);
			/*  Get the size for the caller.  */
			*pdwDataSize = pRegKey->values[i].dataSize;
                       *pdwValueSize = strlen(&(pRegKey->
						values[i].name[dwKeyLen]));
                       strncpy(pstrValue,
				    &(pRegKey->values[i].name[dwKeyLen]),
				    *pdwValueSize + 1);
			GT_3trace(REG_debugMask, GT_2CLASS,
				  "E Key %s, Value %s, Data %x ",
				  pstrKey, pstrValue, *(u32 *)pstrData);
			printS((u8 *)pstrData);
			/*  Set our status to good and exit.  */
			retVal = DSP_SOK;
			break;
		}
	}

	if (count && DSP_FAILED(retVal))
		retVal = REG_E_NOMOREITEMS;

	return retVal;
}

/*
 *  ======== regsupDeleteValue ========
 */
DSP_STATUS regsupDeleteValue(IN CONST char *pstrSubkey,
			    IN CONST char *pstrValue)
{
	DSP_STATUS retVal = DSP_EFAIL;
	u32 i;

	for (i = 0; ((i < BRIDGE_MAX_NUM_REG_ENTRIES) &&
	    (i < pRegKey->numValueEntries)); i++) {
		/*  See if the name matches...  */
               if (strncmp(pRegKey->values[i].name, pstrValue,
		    BRIDGE_MAX_NAME_SIZE) == 0) {
			/* We have a match!  Delete this key.  To delete a
			 * key, we free all resources associated with this
			 * key and, if we're not already the last entry in
			 * the array, we copy that entry into this deleted
			 * key.
			 */
			MEM_Free(pRegKey->values[i].pData);
			if ((pRegKey->numValueEntries - 1) == i) {
				/* we're deleting the last one */
				pRegKey->values[i].name[0] = '\0';
				pRegKey->values[i].dataSize = 0;
				pRegKey->values[i].pData = NULL;
			} else {
				/* move the last one here */
                               strncpy(pRegKey->values[i].name, pRegKey->
				    values[pRegKey->numValueEntries - 1].name,
				    BRIDGE_MAX_NAME_SIZE);
				pRegKey->values[i].dataSize =
				    pRegKey->
				    values[pRegKey->numValueEntries-1].dataSize;
				pRegKey->values[i].pData =
				    pRegKey->
				    values[pRegKey->numValueEntries-1].pData;
				/* don't have to do this, but for
				 * the paranoid... */
				pRegKey->
				    values[pRegKey->numValueEntries-1].name[0] =
				    '\0';
			}

			/* another one bites the dust. */
			pRegKey->numValueEntries--;

			/*  Set our status to good and exit...  */
			retVal = DSP_SOK;
			break;
		}
	}
	return retVal;

}

