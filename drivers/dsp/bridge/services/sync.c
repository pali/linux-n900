/*
 * sync.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Synchronization services.
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
#include <dspbridge/sync.h>

/*  ----------------------------------- Defines, Data Structures, Typedefs */
#define SIGNATURE       0x434e5953	/* "SYNC" (in reverse) */

enum wait_state {
	WO_WAITING,
	WO_SIGNALLED
};

enum sync_state {
	SO_RESET,
	SO_SIGNALLED
};

struct wait_object {
	enum wait_state state;
	struct sync_object *signalling_event;
	struct semaphore sem;
};

/* Generic SYNC object: */
struct sync_object {
	u32 dw_signature;	/* Used for object validation. */
	enum sync_state state;
	spinlock_t sync_lock;
	struct wait_object *wait_obj;
};

struct sync_dpccsobject {
	u32 dw_signature;	/* used for object validation */
	spinlock_t sync_dpccs_lock;
	s32 count;
};

static int test_and_set(volatile void *ptr, int val)
{
	int ret = val;
	asm volatile (" swp %0, %0, [%1]" : "+r" (ret) : "r"(ptr) : "memory");
	return ret;
}

static void timeout_callback(unsigned long hWaitObj);

/*
 *  ======== sync_close_event ========
 *  Purpose:
 *      Close an existing SYNC event object.
 */
dsp_status sync_close_event(struct sync_object *event_obj)
{
	dsp_status status = DSP_SOK;
	struct sync_object *event = (struct sync_object *)event_obj;

	DBC_REQUIRE(event != NULL && event->wait_obj == NULL);

	if (MEM_IS_VALID_HANDLE(event_obj, SIGNATURE)) {
		if (event->wait_obj)
			status = DSP_EFAIL;

		MEM_FREE_OBJECT(event);

	} else {
		status = DSP_EHANDLE;
	}

	return status;
}

/*
 *  ======== sync_exit ========
 *  Purpose:
 *      Cleanup SYNC module.
 */
void sync_exit(void)
{
	/* Do nothing */
}

/*
 *  ======== sync_init ========
 *  Purpose:
 *      Initialize SYNC module.
 */
bool sync_init(void)
{
	return true;
}

/*
 *  ======== sync_open_event ========
 *  Purpose:
 *      Open a new synchronization event object.
 */
dsp_status sync_open_event(OUT struct sync_object **ph_event,
			   IN OPTIONAL struct sync_attrs *pattrs)
{
	struct sync_object *event = NULL;
	dsp_status status = DSP_SOK;

	DBC_REQUIRE(ph_event != NULL);

	/* Allocate memory for sync object */
	MEM_ALLOC_OBJECT(event, struct sync_object, SIGNATURE);
	if (event != NULL) {
		event->state = SO_RESET;
		event->wait_obj = NULL;
		spin_lock_init(&event->sync_lock);
	} else {
		status = DSP_EMEMORY;
	}

	*ph_event = event;

	return status;
}

/*
 *  ======== sync_reset_event ========
 *  Purpose:
 *      Reset an event to non-signalled.
 */
dsp_status sync_reset_event(struct sync_object *event_obj)
{
	dsp_status status = DSP_SOK;
	struct sync_object *event = (struct sync_object *)event_obj;

	if (MEM_IS_VALID_HANDLE(event_obj, SIGNATURE))
		event->state = SO_RESET;
	else
		status = DSP_EHANDLE;

	return status;
}

/*
 *  ======== sync_set_event ========
 *  Purpose:
 *      Set an event to signaled and unblock one waiting thread.
 *
 *  This function is called from ISR, DPC and user context. Hence interrupts
 *  are disabled to ensure atomicity.
 */

dsp_status sync_set_event(struct sync_object *event_obj)
{
	dsp_status status = DSP_SOK;
	struct sync_object *event = (struct sync_object *)event_obj;
	unsigned long flags;

	if (MEM_IS_VALID_HANDLE(event_obj, SIGNATURE)) {
		spin_lock_irqsave(&event_obj->sync_lock, flags);

		if (event->wait_obj != NULL &&
		    test_and_set(&event->wait_obj->state,
				 WO_SIGNALLED) == WO_WAITING) {
			event->state = SO_RESET;
			event->wait_obj->signalling_event = event;
			up(&event->wait_obj->sem);
		} else {
			event->state = SO_SIGNALLED;
		}
		spin_unlock_irqrestore(&event_obj->sync_lock, flags);
	} else {
		status = DSP_EHANDLE;
	}
	return status;
}

/*
 *  ======== sync_wait_on_event ========
 *  Purpose:
 *      Wait for an event to be signalled, up to the specified timeout.
 *      Note: dwTimeOut must be 0xffffffff to signal infinite wait.
 */
dsp_status sync_wait_on_event(struct sync_object *event_obj, u32 dwTimeout)
{
	dsp_status status = DSP_SOK;
	struct sync_object *event = (struct sync_object *)event_obj;
	u32 temp;

	if (MEM_IS_VALID_HANDLE(event_obj, SIGNATURE))
		status = sync_wait_on_multiple_events(&event, 1, dwTimeout,
						      &temp);
	else
		status = DSP_EHANDLE;

	return status;
}

/*
 *  ======== sync_wait_on_multiple_events ========
 *  Purpose:
 *      Wait for any of an array of events to be signalled, up to the
 *      specified timeout.
 */
dsp_status sync_wait_on_multiple_events(struct sync_object **sync_events,
					u32 count, u32 dwTimeout,
					OUT u32 *pu_index)
{
	u32 i;
	dsp_status status = DSP_SOK;
	u32 curr;
	struct wait_object *wp;

	DBC_REQUIRE(count > 0);
	DBC_REQUIRE(sync_events != NULL);
	DBC_REQUIRE(pu_index != NULL);

	for (i = 0; i < count; i++)
		DBC_REQUIRE(MEM_IS_VALID_HANDLE(sync_events[i], SIGNATURE));

	wp = mem_calloc(sizeof(struct wait_object), MEM_NONPAGED);
	if (wp == NULL)
		return DSP_EMEMORY;

	wp->state = WO_WAITING;
	wp->signalling_event = NULL;
	init_MUTEX_LOCKED(&(wp->sem));

	for (curr = 0; curr < count; curr++) {
		sync_events[curr]->wait_obj = wp;
		if (sync_events[curr]->state == SO_SIGNALLED) {
			if (test_and_set(&(wp->state), WO_SIGNALLED) ==
			    WO_WAITING) {
				sync_events[curr]->state = SO_RESET;
				wp->signalling_event = sync_events[curr];
			}
			curr++;	/* Will try optimizing later */
			break;
		}
	}

	curr--;			/* Will try optimizing later */
	if (wp->state != WO_SIGNALLED && dwTimeout > 0) {
		struct timer_list timeout;
		if (dwTimeout != SYNC_INFINITE) {
			init_timer_on_stack(&timeout);
			timeout.function = timeout_callback;
			timeout.data = (unsigned long)wp;
			timeout.expires = jiffies + dwTimeout * HZ / 1000;
			add_timer(&timeout);
		}
		if (down_interruptible(&(wp->sem))) {
			/*
			 * Most probably we are interrupted by a fake signal
			 * from freezer. Return -ERESTARTSYS so that this
			 * ioctl is restarted, and user space doesn't notice
			 * it.
			 */
			status = -ERESTARTSYS;
		}
		if (dwTimeout != SYNC_INFINITE) {
			if (in_interrupt()) {
				del_timer(&timeout);
			} else {
				del_timer_sync(&timeout);
			}
		}
	}
	for (i = 0; i <= curr; i++) {
		if (MEM_IS_VALID_HANDLE(sync_events[i], SIGNATURE)) {
			/*  Memory corruption here if sync_events[i] is
			 *  freed before following statememt. */
			sync_events[i]->wait_obj = NULL;
		}
		if (sync_events[i] == wp->signalling_event)
			*pu_index = i;

	}
	if (wp->signalling_event == NULL && DSP_SUCCEEDED(status))
		status = DSP_ETIMEOUT;
	kfree(wp);
	return status;
}

static void timeout_callback(unsigned long hWaitObj)
{
	struct wait_object *wait_obj = (struct wait_object *)hWaitObj;
	if (test_and_set(&wait_obj->state, WO_SIGNALLED) == WO_WAITING)
		up(&wait_obj->sem);

}

/*
 *  ======== sync_delete_cs ========
 */
dsp_status sync_delete_cs(struct sync_csobject *hcs_obj)
{
	dsp_status status = DSP_SOK;
	struct sync_csobject *pcs_obj = (struct sync_csobject *)hcs_obj;

	if (MEM_IS_VALID_HANDLE(hcs_obj, SIGNATURECS)) {
		if (down_trylock(&pcs_obj->sem) != 0)
			DBC_ASSERT(0);

		MEM_FREE_OBJECT(hcs_obj);
	} else if (MEM_IS_VALID_HANDLE(hcs_obj, SIGNATUREDPCCS)) {
		struct sync_dpccsobject *pdpccs_obj =
		    (struct sync_dpccsobject *)hcs_obj;
		if (pdpccs_obj->count != 1)
			DBC_ASSERT(0);

		MEM_FREE_OBJECT(pdpccs_obj);
	} else {
		status = DSP_EHANDLE;
	}

	return status;
}

/*
 *  ======== sync_enter_cs ========
 */
dsp_status sync_enter_cs(struct sync_csobject *hcs_obj)
{
	dsp_status status = DSP_SOK;
	struct sync_csobject *pcs_obj = (struct sync_csobject *)hcs_obj;

	if (MEM_IS_VALID_HANDLE(hcs_obj, SIGNATURECS)) {
		if (in_interrupt()) {
			status = DSP_EFAIL;
			DBC_ASSERT(0);
		} else if (down_interruptible(&pcs_obj->sem)) {
			status = DSP_EFAIL;
		}
	} else if (MEM_IS_VALID_HANDLE(hcs_obj, SIGNATUREDPCCS)) {
		struct sync_dpccsobject *pdpccs_obj =
		    (struct sync_dpccsobject *)hcs_obj;
		spin_lock_bh(&pdpccs_obj->sync_dpccs_lock);
		pdpccs_obj->count--;
		if (pdpccs_obj->count != 0) {
			/* FATAL ERROR : Failed to acquire DPC CS */
			spin_unlock_bh(&pdpccs_obj->sync_dpccs_lock);
			DBC_ASSERT(0);
		}
	} else {
		status = DSP_EHANDLE;
	}

	return status;
}

/*
 *  ======== sync_initialize_cs ========
 */
dsp_status sync_initialize_cs(OUT struct sync_csobject **phCSObj)
{
	dsp_status status = DSP_SOK;
	struct sync_csobject *pcs_obj = NULL;

	/* Allocate memory for sync CS object */
	MEM_ALLOC_OBJECT(pcs_obj, struct sync_csobject, SIGNATURECS);
	if (pcs_obj != NULL)
		init_MUTEX(&pcs_obj->sem);
	else
		status = DSP_EMEMORY;

	/* return CS object */
	*phCSObj = pcs_obj;
	DBC_ASSERT(DSP_FAILED(status) || (pcs_obj));
	return status;
}

dsp_status sync_initialize_dpccs(OUT struct sync_csobject **phCSObj)
{
	dsp_status status = DSP_SOK;
	struct sync_dpccsobject *pcs_obj = NULL;

	DBC_REQUIRE(phCSObj);

	if (phCSObj) {
		/* Allocate memory for sync CS object */
		MEM_ALLOC_OBJECT(pcs_obj, struct sync_dpccsobject,
				 SIGNATUREDPCCS);
		if (pcs_obj != NULL) {
			pcs_obj->count = 1;
			spin_lock_init(&pcs_obj->sync_dpccs_lock);
		} else {
			status = DSP_EMEMORY;
		}

		/* return CS object */
		*phCSObj = (struct sync_csobject *)pcs_obj;
	} else {
		status = DSP_EPOINTER;
	}

	DBC_ASSERT(DSP_FAILED(status) || (pcs_obj));

	return status;
}

/*
 *  ======== sync_leave_cs ========
 */
dsp_status sync_leave_cs(struct sync_csobject *hcs_obj)
{
	dsp_status status = DSP_SOK;
	struct sync_csobject *pcs_obj = (struct sync_csobject *)hcs_obj;

	if (MEM_IS_VALID_HANDLE(hcs_obj, SIGNATURECS)) {
		up(&pcs_obj->sem);
	} else if (MEM_IS_VALID_HANDLE(hcs_obj, SIGNATUREDPCCS)) {
		struct sync_dpccsobject *pdpccs_obj =
		    (struct sync_dpccsobject *)hcs_obj;
		pdpccs_obj->count++;
		if (pdpccs_obj->count != 1) {
			/* FATAL ERROR : Invalid DPC CS count */
			spin_unlock_bh(&pdpccs_obj->sync_dpccs_lock);
			DBC_ASSERT(0);
			spin_lock_bh(&pdpccs_obj->sync_dpccs_lock);
		}
		spin_unlock_bh(&pdpccs_obj->sync_dpccs_lock);
	} else {
		status = DSP_EHANDLE;
	}

	return status;
}
