/*
 * msg.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * DSP/BIOS Bridge msg_ctrl Module.
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

/*  ----------------------------------- Mini Driver */
#include <dspbridge/wmd.h>

/*  ----------------------------------- Platform Manager */
#include <dspbridge/dev.h>

/*  ----------------------------------- This */
#include <msgobj.h>
#include <dspbridge/msg.h>

/*  ----------------------------------- Globals */
static u32 refs;		/* module reference count */

/*
 *  ======== msg_create ========
 *  Purpose:
 *      Create an object to manage message queues. Only one of these objects
 *      can exist per device object.
 */
dsp_status msg_create(OUT struct msg_mgr **phMsgMgr,
		      struct dev_object *hdev_obj, msg_onexit msgCallback)
{
	struct bridge_drv_interface *intf_fxns;
	struct msg_mgr_ *msg_mgr_obj;
	struct msg_mgr *hmsg_mgr;
	dsp_status status = DSP_SOK;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(phMsgMgr != NULL);
	DBC_REQUIRE(msgCallback != NULL);
	DBC_REQUIRE(hdev_obj != NULL);

	*phMsgMgr = NULL;

	dev_get_intf_fxns(hdev_obj, &intf_fxns);

	/* Let WMD message module finish the create: */
	status =
	    (*intf_fxns->pfn_msg_create) (&hmsg_mgr, hdev_obj, msgCallback);

	if (DSP_SUCCEEDED(status)) {
		/* Fill in WCD message module's fields of the msg_mgr
		 * structure */
		msg_mgr_obj = (struct msg_mgr_ *)hmsg_mgr;
		msg_mgr_obj->intf_fxns = intf_fxns;

		/* Finally, return the new message manager handle: */
		*phMsgMgr = hmsg_mgr;
	} else {
		status = DSP_EFAIL;
	}
	return status;
}

/*
 *  ======== msg_delete ========
 *  Purpose:
 *      Delete a msg_ctrl manager allocated in msg_create().
 */
void msg_delete(struct msg_mgr *hmsg_mgr)
{
	struct msg_mgr_ *msg_mgr_obj = (struct msg_mgr_ *)hmsg_mgr;
	struct bridge_drv_interface *intf_fxns;

	DBC_REQUIRE(refs > 0);

	if (MEM_IS_VALID_HANDLE(msg_mgr_obj, MSGMGR_SIGNATURE)) {
		intf_fxns = msg_mgr_obj->intf_fxns;

		/* Let WMD message module destroy the msg_mgr: */
		(*intf_fxns->pfn_msg_delete) (hmsg_mgr);
	} else {
		dev_dbg(bridge, "%s: Error hmsg_mgr handle: %p\n",
			__func__, hmsg_mgr);
	}
}

/*
 *  ======== msg_exit ========
 */
void msg_exit(void)
{
	DBC_REQUIRE(refs > 0);
	refs--;

	DBC_ENSURE(refs >= 0);
}

/*
 *  ======== msg_mod_init ========
 */
bool msg_mod_init(void)
{
	DBC_REQUIRE(refs >= 0);

	refs++;

	DBC_ENSURE(refs >= 0);

	return true;
}
