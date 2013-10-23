/*
 * gh.c
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
 *  ======== gh.c ========
 */

#include <dspbridge/std.h>

#include <dspbridge/host_os.h>

#include <dspbridge/gs.h>

#include <dspbridge/gh.h>

struct Elem {
	struct Elem *next;
	u8 data[1];
};

struct GH_THashTab {
	u16 maxBucket;
	u16 valSize;
	struct Elem **buckets;
	 u16(*hash) (void *, u16);
	 bool(*match) (void *, void *);
	 void(*delete) (void *);
};

static void Nop(void *p);
static s32 curInit;
static void myfree(void *ptr, s32 size);

/*
 *  ======== GH_create ========
 */

struct GH_THashTab *GH_create(u16 maxBucket, u16 valSize,
		u16(*hash)(void *, u16), bool(*match)(void *, void *),
		void(*delete)(void *))
{
	struct GH_THashTab *hashTab;
	u16 i;
	hashTab = (struct GH_THashTab *)GS_alloc(sizeof(struct GH_THashTab));
	if (hashTab == NULL)
		return NULL;
	hashTab->maxBucket = maxBucket;
	hashTab->valSize = valSize;
	hashTab->hash = hash;
	hashTab->match = match;
	hashTab->delete = delete == NULL ? Nop : delete;

	hashTab->buckets = (struct Elem **)
			   GS_alloc(sizeof(struct Elem *) * maxBucket);
	if (hashTab->buckets == NULL) {
		GH_delete(hashTab);
		return NULL;
	}

	for (i = 0; i < maxBucket; i++)
		hashTab->buckets[i] = NULL;

	return hashTab;
}

/*
 *  ======== GH_delete ========
 */
void GH_delete(struct GH_THashTab *hashTab)
{
	struct Elem *elem, *next;
	u16 i;

	if (hashTab != NULL) {
		if (hashTab->buckets != NULL) {
			for (i = 0; i < hashTab->maxBucket; i++) {
				for (elem = hashTab->buckets[i]; elem != NULL;
				    elem = next) {
					next = elem->next;
					(*hashTab->delete) (elem->data);
					myfree(elem, sizeof(struct Elem) - 1 +
					      hashTab->valSize);
				}
			}

			myfree(hashTab->buckets, sizeof(struct Elem *)
			      * hashTab->maxBucket);
		}

		myfree(hashTab, sizeof(struct GH_THashTab));
	}
}

/*
 *  ======== GH_exit ========
 */

void GH_exit(void)
{
	if (curInit-- == 1)
		GS_exit();

}

/*
 *  ======== GH_find ========
 */

void *GH_find(struct GH_THashTab *hashTab, void *key)
{
	struct Elem *elem;

	elem = hashTab->buckets[(*hashTab->hash)(key, hashTab->maxBucket)];

	for (; elem; elem = elem->next) {
		if ((*hashTab->match)(key, elem->data))
			return elem->data;
	}

	return NULL;
}

/*
 *  ======== GH_init ========
 */

void GH_init(void)
{
	if (curInit++ == 0)
		GS_init();
}

/*
 *  ======== GH_insert ========
 */

void *GH_insert(struct GH_THashTab *hashTab, void *key, void *value)
{
	struct Elem *elem;
	u16 i;
	char *src, *dst;

	elem = (struct Elem *)GS_alloc(sizeof(struct Elem) - 1 +
		hashTab->valSize);
	if (elem != NULL) {

		dst = (char *)elem->data;
		src = (char *)value;
		for (i = 0; i < hashTab->valSize; i++)
			*dst++ = *src++;

		i = (*hashTab->hash)(key, hashTab->maxBucket);
		elem->next = hashTab->buckets[i];
		hashTab->buckets[i] = elem;

		return elem->data;
	}

	return NULL;
}

/*
 *  ======== Nop ========
 */
/* ARGSUSED */
static void Nop(void *p)
{
	p = p;			/* stifle compiler warning */
}

/*
 *  ======== myfree ========
 */
static void myfree(void *ptr, s32 size)
{
	GS_free(ptr);
}
