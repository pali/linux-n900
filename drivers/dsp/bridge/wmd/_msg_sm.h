/*
 * _msg_sm.h
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
 *  ======== _msg_sm.h ========
 *  Description:
 *      Private header file defining MSG manager objects and defines needed
 *      by IO manager.
 *
 *  Public Functions:
 *      None.
 *
 *  Notes:
 *
 *! Revision History:
 *! ================
 *! 09-May-2001 jeh     Code Review cleanup.
 *! 08-Nov-2000 jeh     Created.
 */

#ifndef _MSG_SM_
#define _MSG_SM_

#include <dspbridge/list.h>
#include <dspbridge/msgdefs.h>

/*
 *  These target side symbols define the beginning and ending addresses
 *  of the section of shared memory used for messages. They are
 *  defined in the *cfg.cmd file by cdb code.
 */
#define MSG_SHARED_BUFFER_BASE_SYM      "_MSG_BEG"
#define MSG_SHARED_BUFFER_LIMIT_SYM     "_MSG_END"

#ifndef _CHNL_WORDSIZE
#define _CHNL_WORDSIZE 4	/* default _CHNL_WORDSIZE is 2 bytes/word */
#endif

/*
 *  ======== MSG ========
 *  There is a control structure for messages to the DSP, and a control
 *  structure for messages from the DSP. The shared memory region for
 *  transferring messages is partitioned as follows:
 *
 *  ----------------------------------------------------------
 *  |Control | Messages from DSP | Control | Messages to DSP |
 *  ----------------------------------------------------------
 *
 *  MSG control structure for messages to the DSP is used in the following
 *  way:
 *
 *  bufEmpty -      This flag is set to FALSE by the GPP after it has output
 *                  messages for the DSP. The DSP host driver sets it to
 *                  TRUE after it has copied the messages.
 *  postSWI -       Set to 1 by the GPP after it has written the messages,
 *                  set the size, and set bufEmpty to FALSE.
 *                  The DSP Host driver uses SWI_andn of the postSWI field
 *                  when a host interrupt occurs. The host driver clears
 *                  this after posting the SWI.
 *  size -          Number of messages to be read by the DSP.
 *
 *  For messages from the DSP:
 *  bufEmpty -      This flag is set to FALSE by the DSP after it has output
 *                  messages for the GPP. The DPC on the GPP sets it to
 *                  TRUE after it has copied the messages.
 *  postSWI -       Set to 1 the DPC on the GPP after copying the messages.
 *  size -          Number of messages to be read by the GPP.
 */
struct MSG {
	u32 bufEmpty;	/* to/from DSP buffer is empty */
	u32 postSWI;	/* Set to "1" to post MSG SWI */
	u32 size;	/* Number of messages to/from the DSP */
	u32 resvd;
} ;

/*
 *  ======== MSG_MGR ========
 *  The MSG_MGR maintains a list of all MSG_QUEUEs. Each NODE object can
 *  have MSG_QUEUE to hold all messages that come up from the corresponding
 *  node on the DSP. The MSG_MGR also has a shared queue of messages
 *  ready to go to the DSP.
 */
struct MSG_MGR {
	/* The first two fields must match those in msgobj.h */
	u32 dwSignature;
	struct WMD_DRV_INTERFACE *pIntfFxns;	/* Function interface to WMD. */

	struct IO_MGR *hIOMgr;	/* IO manager */
	struct LST_LIST *queueList;	/* List of MSG_QUEUEs */
	struct SYNC_CSOBJECT *hSyncCS;	/* For critical sections */
	/* Signalled when MsgFrame is available */
	struct SYNC_OBJECT *hSyncEvent;
	struct LST_LIST *msgFreeList;	/* Free MsgFrames ready to be filled */
	struct LST_LIST *msgUsedList;	/* MsgFrames ready to go to DSP */
	u32 uMsgsPending;	/* # of queued messages to go to DSP */
	u32 uMaxMsgs;	/* Max # of msgs that fit in buffer */
	MSG_ONEXIT onExit;	/* called when RMS_EXIT is received */
} ;

/*
 *  ======== MSG_QUEUE ========
 *  Each NODE has a MSG_QUEUE for receiving messages from the
 *  corresponding node on the DSP. The MSG_QUEUE object maintains a list
 *  of messages that have been sent to the host, but not yet read (MSG_Get),
 *  and a list of free frames that can be filled when new messages arrive
 *  from the DSP.
 *  The MSG_QUEUE's hSynEvent gets posted when a message is ready.
 */
struct MSG_QUEUE {
	struct LST_ELEM listElem;
	u32 dwSignature;
	struct MSG_MGR *hMsgMgr;
	u32 uMaxMsgs;	/* Node message depth */
	u32 dwId;	/* Node environment pointer */
	struct LST_LIST *msgFreeList;	/* Free MsgFrames ready to be filled */
	/* Filled MsgFramess waiting to be read */
	struct LST_LIST *msgUsedList;
	HANDLE hArg;	/* Handle passed to mgr onExit callback */
	struct SYNC_OBJECT *hSyncEvent;	/* Signalled when message is ready */
	struct SYNC_OBJECT *hSyncDone;	/* For synchronizing cleanup */
	struct SYNC_OBJECT *hSyncDoneAck;	/* For synchronizing cleanup */
	struct NTFY_OBJECT *hNtfy;	/* For notification of message ready */
	bool fDone;	/* TRUE <==> deleting the object */
	u32 refCount;	/* Number of pending MSG_get/put calls */
};

/*
 *  ======== MSG_DSPMSG ========
 */
struct MSG_DSPMSG {
	struct DSP_MSG msg;
	u32 dwId;	/* Identifies the node the message goes to */
} ;

/*
 *  ======== MSG_FRAME ========
 */
struct MSG_FRAME {
	struct LST_ELEM listElem;
	struct MSG_DSPMSG msgData;
} ;

#endif				/* _MSG_SM_ */

