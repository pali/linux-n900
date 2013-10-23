/*
 * list.c
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
 *  ======== listce.c ========
 *  Purpose
 *      Provides standard circular list handling functions.
 *
 *  Public Functions:
 *      LST_Create
 *      LST_Delete
 *      LST_Exit
 *      LST_First
 *      LST_GetHead
 *      LST_Init
 *      LST_InitElem
 *      LST_InsertBefore
 *      LST_Next
 *      LST_PutTail
 *      LST_RemoveElem
 *
 *! Revision History
 *! ================
 *! 06-Mar-2002 jeh Don't set element self to NULL in LST_RemoveElem().
 *! 10-Aug-2000 ag: Added LST_InsertBefore().
 *! 03-Feb-2000 rr: Module init/exit is handled by SERVICES Init/Exit.
 *!		 GT Changes.
 *! 22-Nov-1999 kc: Added changes from code review.
 *! 10-Aug-1999 kc: Based on wsx-c18.
 *! 16-Jun-1997 gp: Removed unnecessary enabling/disabling of interrupts around
 *!                 list manipulation code.
 *! 22-Oct-1996 gp: Added LST_RemoveElem, and LST_First/LST_Next iterators.
 *! 10-Aug-1996 gp: Acquired from SMM for WinSPOX v. 1.1; renamed identifiers.
 */

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>
#include <dspbridge/dbdefs.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>
#include <dspbridge/gt.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/mem.h>

/*  ----------------------------------- This */
#include <dspbridge/list.h>

/*  ----------------------------------- Globals */
#if GT_TRACE
static struct GT_Mask LST_debugMask = { NULL, NULL };	/* GT trace var. */
#endif

/*
 *  ======== LST_Create ========
 *  Purpose:
 *      Allocates and initializes a circular list.
 */
struct LST_LIST *LST_Create(void)
{
	struct LST_LIST *pList;

	GT_0trace(LST_debugMask, GT_ENTER, "LST_Create: entered\n");

	pList = (struct LST_LIST *) MEM_Calloc(sizeof(struct LST_LIST),
		MEM_NONPAGED);
	if (pList != NULL) {
		pList->head.next = &pList->head;
		pList->head.prev = &pList->head;
		pList->head.self = NULL;
	}

	return pList;
}

/*
 *  ======== LST_Delete ========
 *  Purpose:
 *      Removes a list by freeing its control structure's memory space.
 */
void LST_Delete(struct LST_LIST *pList)
{
	DBC_Require(pList != NULL);

	GT_1trace(LST_debugMask, GT_ENTER, "LST_Delete: pList 0x%x\n", pList);

	MEM_Free(pList);
}

/*
 *  ======== LST_Exit ========
 *  Purpose:
 *      Discontinue usage of the LST module.
 */
void LST_Exit(void)
{
	GT_0trace(LST_debugMask, GT_5CLASS, "LST_Exit\n");
}

/*
 *  ======== LST_First ========
 *  Purpose:
 *      Returns a pointer to the first element of the list, or NULL if the
 *      list is empty.
 */
struct LST_ELEM *LST_First(struct LST_LIST *pList)
{
	struct LST_ELEM *pElem = NULL;

	DBC_Require(pList != NULL);

	GT_1trace(LST_debugMask, GT_ENTER, "LST_First: pList 0x%x\n", pList);

	if (!LST_IsEmpty(pList))
		pElem = pList->head.next;

	return pElem;
}

/*
 *  ======== LST_GetHead ========
 *  Purpose:
 *      "Pops" the head off the list and returns a pointer to it.
 */
struct LST_ELEM *LST_GetHead(struct LST_LIST *pList)
{
	struct LST_ELEM *pElem;

	DBC_Require(pList != NULL);

	GT_1trace(LST_debugMask, GT_ENTER, "LST_GetHead: pList 0x%x\n", pList);

	if (LST_IsEmpty(pList))
		return NULL;

	/* pElem is always valid because the list cannot be empty
	 * at this point */
	pElem = pList->head.next;
	pList->head.next = pElem->next;
	pElem->next->prev = &pList->head;

	return pElem->self;
}

/*
 *  ======== LST_Init ========
 *  Purpose:
 *      Initialize LST module private state.
 */
bool LST_Init(void)
{
	GT_create(&LST_debugMask, "LS");	/* LS for LSt module */

	GT_0trace(LST_debugMask, GT_5CLASS, "LST_Init\n");

	return true;
}

/*
 *  ======== LST_InitElem ========
 *  Purpose:
 *      Initializes a list element to default (cleared) values
 */
void LST_InitElem(struct LST_ELEM *pElem)
{
	DBC_Require(pElem != NULL);

	GT_1trace(LST_debugMask, GT_ENTER, "LST_InitElem: pElem 0x%x\n", pElem);

	if (pElem) {
		pElem->next = NULL;
		pElem->prev = NULL;
		pElem->self = pElem;
	}
}

/*
 *  ======== LST_InsertBefore ========
 *  Purpose:
 *      Insert the element before the existing element.
 */
void LST_InsertBefore(struct LST_LIST *pList, struct LST_ELEM *pElem,
		      struct LST_ELEM *pElemExisting)
{
	DBC_Require(pList != NULL);
	DBC_Require(pElem != NULL);
	DBC_Require(pElemExisting != NULL);

	GT_3trace(LST_debugMask, GT_ENTER, "LST_InsertBefore: pList 0x%x, "
		  "pElem 0x%x pElemExisting 0x%x\n", pList, pElem,
		  pElemExisting);

	pElemExisting->prev->next = pElem;
	pElem->prev = pElemExisting->prev;
	pElem->next = pElemExisting;
	pElemExisting->prev = pElem;
}

/*
 *  ======== LST_Next ========
 *  Purpose:
 *      Returns a pointer to the next element of the list, or NULL if the
 *      next element is the head of the list or the list is empty.
 */
struct LST_ELEM *LST_Next(struct LST_LIST *pList, struct LST_ELEM *pCurElem)
{
	struct LST_ELEM *pNextElem = NULL;

	DBC_Require(pList != NULL);
	DBC_Require(pCurElem != NULL);

	GT_2trace(LST_debugMask, GT_ENTER,
		  "LST_Next: pList 0x%x, pCurElem 0x%x\n",
		  pList, pCurElem);

	if (!LST_IsEmpty(pList)) {
		if (pCurElem->next != &pList->head)
			pNextElem = pCurElem->next;
	}

	return pNextElem;
}

/*
 *  ======== LST_PutTail ========
 *  Purpose:
 *      Adds the specified element to the tail of the list
 */
void LST_PutTail(struct LST_LIST *pList, struct LST_ELEM *pElem)
{
	DBC_Require(pList != NULL);
	DBC_Require(pElem != NULL);

	GT_2trace(LST_debugMask, GT_ENTER,
		  "LST_PutTail: pList 0x%x, pElem 0x%x\n",
		  pList, pElem);

	pElem->prev = pList->head.prev;
	pElem->next = &pList->head;
	pList->head.prev = pElem;
	pElem->prev->next = pElem;

	DBC_Ensure(!LST_IsEmpty(pList));
}

/*
 *  ======== LST_RemoveElem ========
 *  Purpose:
 *      Removes (unlinks) the given element from the list, if the list is not
 *      empty.  Does not free the list element.
 */
void LST_RemoveElem(struct LST_LIST *pList, struct LST_ELEM *pCurElem)
{
	DBC_Require(pList != NULL);
	DBC_Require(pCurElem != NULL);

	GT_2trace(LST_debugMask, GT_ENTER,
		  "LST_RemoveElem: pList 0x%x, pCurElem "
		  "0x%x\n", pList, pCurElem);

	if (!LST_IsEmpty(pList)) {
		pCurElem->prev->next = pCurElem->next;
		pCurElem->next->prev = pCurElem->prev;

		/* set elem fields to NULL to prevent illegal references */
		pCurElem->next = NULL;
		pCurElem->prev = NULL;
	}
}

