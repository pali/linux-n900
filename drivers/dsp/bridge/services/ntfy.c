/*
 * ntfy.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Manage lists of notification events.
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
#include <dspbridge/list.h>
#include <dspbridge/mem.h>
#include <dspbridge/sync.h>

/*  ----------------------------------- This */
#include <dspbridge/ntfy.h>

/*  ----------------------------------- Defines, Data Structures, Typedefs */
#define NTFY_SIGNATURE      0x5946544e	/* "YFTN" */

/*
 *  ======== ntfy_object ========
 */
struct ntfy_object {
	u32 dw_signature;	/* For object validation */
	struct lst_list *notify_list;	/* List of notifier objects */
	struct sync_csobject *sync_obj;	/* For critical sections */
};

/*
 *  ======== notifier ========
 *  This object will be created when a client registers for events.
 */
struct notifier {
	struct list_head list_elem;
	u32 event_mask;		/* Events to be notified about */
	u32 notify_type;	/* Type of notification to be sent */

	/*
	 *  We keep a copy of the event name to check if the event has
	 *  already been registered. (SYNC also keeps a copy of the name).
	 */
	char *pstr_name;	/* Name of event */
	bhandle event_obj;	/* Handle for notification */
	struct sync_object *sync_obj;
};

/*  ----------------------------------- Function Prototypes */
static void delete_notify(struct notifier *notifier_obj);

/*
 *  ======== ntfy_create ========
 *  Purpose:
 *      Create an empty list of notifications.
 */
dsp_status ntfy_create(struct ntfy_object **phNtfy)
{
	struct ntfy_object *notify_obj;
	dsp_status status = DSP_SOK;

	DBC_REQUIRE(phNtfy != NULL);

	*phNtfy = NULL;
	MEM_ALLOC_OBJECT(notify_obj, struct ntfy_object, NTFY_SIGNATURE);

	if (notify_obj) {

		status = sync_initialize_dpccs(&notify_obj->sync_obj);
		if (DSP_SUCCEEDED(status)) {
			notify_obj->notify_list =
			    mem_calloc(sizeof(struct lst_list), MEM_NONPAGED);
			if (notify_obj->notify_list == NULL) {
				(void)sync_delete_cs(notify_obj->sync_obj);
				MEM_FREE_OBJECT(notify_obj);
				status = DSP_EMEMORY;
			} else {
				INIT_LIST_HEAD(&notify_obj->notify_list->head);
				*phNtfy = notify_obj;
			}
		}
	} else {
		status = DSP_EMEMORY;
	}

	DBC_ENSURE((DSP_FAILED(status) && *phNtfy == NULL) ||
		   (DSP_SUCCEEDED(status) && MEM_IS_VALID_HANDLE((*phNtfy),
							 NTFY_SIGNATURE)));

	return status;
}

/*
 *  ======== ntfy_delete ========
 *  Purpose:
 *      Free resources allocated in ntfy_create.
 */
void ntfy_delete(struct ntfy_object *ntfy_obj)
{
	struct notifier *notifier_obj;

	DBC_REQUIRE(MEM_IS_VALID_HANDLE(ntfy_obj, NTFY_SIGNATURE));

	/* Remove any elements remaining in list */
	if (ntfy_obj->notify_list) {
		while ((notifier_obj =
		      (struct notifier *)lst_get_head(ntfy_obj->notify_list))) {
			delete_notify(notifier_obj);
		}
		DBC_ASSERT(LST_IS_EMPTY(ntfy_obj->notify_list));
		kfree(ntfy_obj->notify_list);
	}
	if (ntfy_obj->sync_obj)
		(void)sync_delete_cs(ntfy_obj->sync_obj);

	MEM_FREE_OBJECT(ntfy_obj);
}

/*
 *  ======== ntfy_exit ========
 *  Purpose:
 *      Discontinue usage of NTFY module.
 */
void ntfy_exit(void)
{
	/* Do nothing */
}

/*
 *  ======== ntfy_init ========
 *  Purpose:
 *      Initialize the NTFY module.
 */
bool ntfy_init(void)
{
	return true;
}

/*
 *  ======== ntfy_notify ========
 *  Purpose:
 *      Execute notify function (signal event) for every
 *      element in the notification list that is to be notified about the
 *      event specified in event_mask.
 */
void ntfy_notify(struct ntfy_object *ntfy_obj, u32 event_mask)
{
	struct notifier *notifier_obj;

	DBC_REQUIRE(MEM_IS_VALID_HANDLE(ntfy_obj, NTFY_SIGNATURE));

	/*
	 *  Go through notify_list and notify all clients registered for
	 *  event_mask events.
	 */

	(void)sync_enter_cs(ntfy_obj->sync_obj);

	notifier_obj = (struct notifier *)lst_first(ntfy_obj->notify_list);
	while (notifier_obj != NULL) {
		if (notifier_obj->event_mask & event_mask) {
			/* Notify */
			if (notifier_obj->notify_type == DSP_SIGNALEVENT)
				(void)sync_set_event(notifier_obj->sync_obj);

		}
		notifier_obj =
		    (struct notifier *)lst_next(ntfy_obj->notify_list,
						(struct list_head *)
						notifier_obj);
	}

	(void)sync_leave_cs(ntfy_obj->sync_obj);
}

/*
 *  ======== ntfy_register ========
 *  Purpose:
 *      Add a notification element to the list. If the notification is already
 *      registered, and event_mask != 0, the notification will get posted for
 *      events specified in the new event mask. If the notification is already
 *      registered and event_mask == 0, the notification will be unregistered.
 */
dsp_status ntfy_register(struct ntfy_object *ntfy_obj,
			 struct dsp_notification *hnotification,
			 u32 event_mask, u32 notify_type)
{
	struct notifier *notifier_obj;
	struct sync_attrs sync_attrs_obj;
	dsp_status status = DSP_SOK;

	DBC_REQUIRE(MEM_IS_VALID_HANDLE(ntfy_obj, NTFY_SIGNATURE));

	if (hnotification == NULL)
		status = DSP_EHANDLE;

	/* Return DSP_ENOTIMPL if notify_type is not supported */
	if (DSP_SUCCEEDED(status)) {
		if (!IS_VALID_NOTIFY_MASK(notify_type))
			status = DSP_ENOTIMPL;

	}

	if (DSP_FAILED(status))
		return status;

	(void)sync_enter_cs(ntfy_obj->sync_obj);

	notifier_obj = (struct notifier *)lst_first(ntfy_obj->notify_list);
	while (notifier_obj != NULL) {
		/* If there is more than one notification type, each
		 * type may require its own handler code. */

		if (hnotification->handle == notifier_obj->sync_obj) {
			/* found */
			break;
		}
		notifier_obj =
		    (struct notifier *)lst_next(ntfy_obj->notify_list,
						(struct list_head *)
						notifier_obj);
	}
	if (notifier_obj == NULL) {
		/* Not registered */
		if (event_mask == 0) {
			status = DSP_EVALUE;
		} else {
			/* Allocate notifier object, add to list */
			notifier_obj = mem_calloc(sizeof(struct notifier),
						  MEM_PAGED);
			if (notifier_obj == NULL)
				status = DSP_EMEMORY;

		}
		if (DSP_SUCCEEDED(status)) {
			lst_init_elem((struct list_head *)notifier_obj);
			/* If there is more than one notification type, each
			 * type may require its own handler code. */
			status =
			    sync_open_event(&notifier_obj->sync_obj,
					    &sync_attrs_obj);
			hnotification->handle = notifier_obj->sync_obj;

			if (DSP_SUCCEEDED(status)) {
				notifier_obj->event_mask = event_mask;
				notifier_obj->notify_type = notify_type;
				lst_put_tail(ntfy_obj->notify_list,
					     (struct list_head *)notifier_obj);
			} else {
				delete_notify(notifier_obj);
			}
		}
	} else {
		/* Found in list */
		if (event_mask == 0) {
			/* Remove from list and free */
			lst_remove_elem(ntfy_obj->notify_list,
					(struct list_head *)notifier_obj);
			delete_notify(notifier_obj);
		} else {
			/* Update notification mask (type shouldn't change) */
			notifier_obj->event_mask = event_mask;
		}
	}
	(void)sync_leave_cs(ntfy_obj->sync_obj);
	return status;
}

/*
 *  ======== delete_notify ========
 *  Purpose:
 *      Free the notification object.
 */
static void delete_notify(struct notifier *notifier_obj)
{
	if (notifier_obj->sync_obj)
		(void)sync_close_event(notifier_obj->sync_obj);

	kfree(notifier_obj->pstr_name);

	kfree(notifier_obj);
}
