/*
 * _dcd.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Includes the wrapper functions called directly by the
 * DeviceIOControl interface.
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

#ifndef _WCD_
#define _WCD_

#include <dspbridge/wcdioctl.h>

/*
 *  ======== wcd_call_dev_io_ctl ========
 *  Purpose:
 *      Call the (wrapper) function for the corresponding WCD IOCTL.
 *  Parameters:
 *      cmd:        IOCTL id, base 0.
 *      args:       Argument structure.
 *      pResult:
 *  Returns:
 *      DSP_SOK if command called; DSP_EINVALIDARG if command not in IOCTL
 *      table.
 *  Requires:
 *  Ensures:
 */
extern dsp_status wcd_call_dev_io_ctl(unsigned int cmd,
				      union Trapped_Args *args,
				      u32 *pResult, void *pr_ctxt);

/*
 *  ======== wcd_init ========
 *  Purpose:
 *      Initialize WCD modules, and export WCD services to WMD's.
 *      This procedure is called when the class driver is loaded.
 *  Parameters:
 *  Returns:
 *      TRUE if success; FALSE otherwise.
 *  Requires:
 *  Ensures:
 */
extern bool wcd_init(void);

/*
 *  ======== wcd_init_complete2 ========
 *  Purpose:
 *      Perform any required WCD, and WMD initialization which
 *      cannot not be performed in wcd_init(void) or dev_start_device() due
 *      to the fact that some services are not yet
 *      completely initialized.
 *  Parameters:
 *  Returns:
 *      DSP_SOK:        Allow this device to load
 *      DSP_EFAIL:      Failure.
 *  Requires:
 *      WCD initialized.
 *  Ensures:
 */
extern dsp_status wcd_init_complete2(void);

/*
 *  ======== wcd_exit ========
 *  Purpose:
 *      Exit all modules initialized in wcd_init(void).
 *      This procedure is called when the class driver is unloaded.
 *  Parameters:
 *  Returns:
 *  Requires:
 *      wcd_init(void) was previously called.
 *  Ensures:
 *      Resources acquired in wcd_init(void) are freed.
 */
extern void wcd_exit(void);

/* MGR wrapper functions */
extern u32 mgrwrap_enum_node_info(union Trapped_Args *args, void *pr_ctxt);
extern u32 mgrwrap_enum_proc_info(union Trapped_Args *args, void *pr_ctxt);
extern u32 mgrwrap_register_object(union Trapped_Args *args, void *pr_ctxt);
extern u32 mgrwrap_unregister_object(union Trapped_Args *args, void *pr_ctxt);
extern u32 mgrwrap_wait_for_bridge_events(union Trapped_Args *args,
					  void *pr_ctxt);

extern u32 mgrwrap_get_process_resources_info(union Trapped_Args *args,
					      void *pr_ctxt);

/* CPRC (Processor) wrapper Functions */
extern u32 procwrap_attach(union Trapped_Args *args, void *pr_ctxt);
extern u32 procwrap_ctrl(union Trapped_Args *args, void *pr_ctxt);
extern u32 procwrap_detach(union Trapped_Args *args, void *pr_ctxt);
extern u32 procwrap_enum_node_info(union Trapped_Args *args, void *pr_ctxt);
extern u32 procwrap_enum_resources(union Trapped_Args *args, void *pr_ctxt);
extern u32 procwrap_get_state(union Trapped_Args *args, void *pr_ctxt);
extern u32 procwrap_get_trace(union Trapped_Args *args, void *pr_ctxt);
extern u32 procwrap_load(union Trapped_Args *args, void *pr_ctxt);
extern u32 procwrap_register_notify(union Trapped_Args *args, void *pr_ctxt);
extern u32 procwrap_start(union Trapped_Args *args, void *pr_ctxt);
extern u32 procwrap_reserve_memory(union Trapped_Args *args, void *pr_ctxt);
extern u32 procwrap_un_reserve_memory(union Trapped_Args *args, void *pr_ctxt);
extern u32 procwrap_map(union Trapped_Args *args, void *pr_ctxt);
extern u32 procwrap_un_map(union Trapped_Args *args, void *pr_ctxt);
extern u32 procwrap_flush_memory(union Trapped_Args *args, void *pr_ctxt);
extern u32 procwrap_stop(union Trapped_Args *args, void *pr_ctxt);
extern u32 procwrap_invalidate_memory(union Trapped_Args *args, void *pr_ctxt);

/* NODE wrapper functions */
extern u32 nodewrap_allocate(union Trapped_Args *args, void *pr_ctxt);
extern u32 nodewrap_alloc_msg_buf(union Trapped_Args *args, void *pr_ctxt);
extern u32 nodewrap_change_priority(union Trapped_Args *args, void *pr_ctxt);
extern u32 nodewrap_connect(union Trapped_Args *args, void *pr_ctxt);
extern u32 nodewrap_create(union Trapped_Args *args, void *pr_ctxt);
extern u32 nodewrap_delete(union Trapped_Args *args, void *pr_ctxt);
extern u32 nodewrap_free_msg_buf(union Trapped_Args *args, void *pr_ctxt);
extern u32 nodewrap_get_attr(union Trapped_Args *args, void *pr_ctxt);
extern u32 nodewrap_get_message(union Trapped_Args *args, void *pr_ctxt);
extern u32 nodewrap_pause(union Trapped_Args *args, void *pr_ctxt);
extern u32 nodewrap_put_message(union Trapped_Args *args, void *pr_ctxt);
extern u32 nodewrap_register_notify(union Trapped_Args *args, void *pr_ctxt);
extern u32 nodewrap_run(union Trapped_Args *args, void *pr_ctxt);
extern u32 nodewrap_terminate(union Trapped_Args *args, void *pr_ctxt);
extern u32 nodewrap_get_uuid_props(union Trapped_Args *args, void *pr_ctxt);

/* STRM wrapper functions */
extern u32 strmwrap_allocate_buffer(union Trapped_Args *args, void *pr_ctxt);
extern u32 strmwrap_close(union Trapped_Args *args, void *pr_ctxt);
extern u32 strmwrap_free_buffer(union Trapped_Args *args, void *pr_ctxt);
extern u32 strmwrap_get_event_handle(union Trapped_Args *args, void *pr_ctxt);
extern u32 strmwrap_get_info(union Trapped_Args *args, void *pr_ctxt);
extern u32 strmwrap_idle(union Trapped_Args *args, void *pr_ctxt);
extern u32 strmwrap_issue(union Trapped_Args *args, void *pr_ctxt);
extern u32 strmwrap_open(union Trapped_Args *args, void *pr_ctxt);
extern u32 strmwrap_reclaim(union Trapped_Args *args, void *pr_ctxt);
extern u32 strmwrap_register_notify(union Trapped_Args *args, void *pr_ctxt);
extern u32 strmwrap_select(union Trapped_Args *args, void *pr_ctxt);

extern u32 cmmwrap_calloc_buf(union Trapped_Args *args, void *pr_ctxt);
extern u32 cmmwrap_free_buf(union Trapped_Args *args, void *pr_ctxt);
extern u32 cmmwrap_get_handle(union Trapped_Args *args, void *pr_ctxt);
extern u32 cmmwrap_get_info(union Trapped_Args *args, void *pr_ctxt);

#endif /* _WCD_ */
