/*
 * wcdioctl.h
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
 *  ======== wcdioctl.h ========
 *  Purpose:
 *      Contains structures and commands that are used for interaction
 *      between the DDSP API and class driver.
 *
 *! Revision History
 *! ================
 *! 19-Apr-2004 sb  Aligned DMM definitions with Symbian
 *! 08-Mar-2004 sb  Added the Dynamic Memory Mapping structs & offsets
 *! 15-Oct-2002 kc  Updated definitions for private PERF module.
 *! 16-Aug-2002 map Added ARGS_MGR_REGISTEROBJECT & ARGS_MGR_UNREGISTEROBJECT
 *!		 Added CMD_MGR_REGISTEROBJECT_OFFSET &
 *!		 CMD_MGR_UNREGISTEROBJECT_OFFSET
 *! 15-Jan-2002 ag  Added actaul bufSize to ARGS_STRM_[RECLAIM][ISSUE].
 *! 15-Nov-2001 ag  change to STRMINFO in ARGS_STRM_GETINFO.
 *! 11-Sep-2001 ag  ARGS_CMM_GETHANDLE defn uses DSP_HPROCESSOR.
 *! 23-Apr-2001 jeh Added pStatus to NODE_TERMINATE args.
 *! 13-Feb-2001 kc  DSP/BIOS Bridge name updates.
 *! 22-Nov-2000 kc: Added CMD_MGR_GETPERF_DATA_OFFSET for acquiring PERF stats.
 *! 27-Oct-2000 jeh Added timeouts to NODE_GETMESSAGE, NODE_PUTMESSAGE args.
 *!		 Removed NODE_GETMESSAGESTRM args.
 *! 11-Oct-2000 ag: Added SM mgr(CMM) args.
 *! 27-Sep-2000 jeh Removed struct DSP_BUFFERATTR param from
 *!		    ARGS_STRM_ALLOCATEBUFFER.
 *! 25-Sep-2000 rr: Updated to Version 0.9
 *! 07-Sep-2000 jeh Changed HANDLE to DSP_HNOTIFICATION in RegisterNotify args.
 *!		 Added DSP_STRMATTR to DSPNode_Connect args.
 *! 04-Aug-2000 rr: MEM and UTIL added to RM.
 *! 27-Jul-2000 rr: NODE, MGR,STRM and PROC added
 *! 27-Jun-2000 rr: Modifed to Use either PM or DSP/BIOS Bridge
 *!		 IFDEF to build for PM or DSP/BIOS Bridge
 *! 28-Jan-2000 rr: NT_CMD_FROM_OFFSET moved out to dsptrap.h
 *! 24-Jan-2000 rr: Merged with Scott's code.
 *! 21-Jan-2000 sg: In ARGS_CHNL_GETMODE changed mode to be u32 to be
 *!		 consistent with chnldefs.h.
 *! 11-Jan-2000 rr: CMD_CFG_GETCDVERSION_OFFSET added.
 *! 12-Nov-1999 rr: CMD_BRD_MONITOR_OFFSET added
 *! 09-Nov-1999 kc: Added MEMRY and enabled CMD_BRD_IOCTL_OFFSET.
 *! 05-Nov-1999 ag: Added CHNL.
 *! 02-Nov-1999 kc: Removed field from ARGS_UTIL_TESTDLL.
 *! 29-Oct-1999 kc: Cleaned up for code review.
 *! 08-Oct-1999 rr: Util control offsets added.
 *! 13-Sep-1999 kc: Added ARGS_UTIL_TESTDLL for PM test infrastructure.
 *! 19-Aug-1999 rr: Created from WSX. Minimal Implementaion of BRD_Start and BRD
 *!		 and BRD_Stop. IOCTL Offsets and CTRL Code.
 */

#ifndef WCDIOCTL_
#define WCDIOCTL_

#include <dspbridge/mem.h>
#include <dspbridge/cmm.h>
#include <dspbridge/strmdefs.h>
#include <dspbridge/dbdcd.h>

union Trapped_Args {

	/* MGR Module */
	struct {
		u32 uNode;
		struct DSP_NDBPROPS __user *pNDBProps;
		u32 uNDBPropsSize;
		u32 __user *puNumNodes;
	} ARGS_MGR_ENUMNODE_INFO;

	struct {
		u32 uProcessor;
		struct DSP_PROCESSORINFO __user *pProcessorInfo;
		u32 uProcessorInfoSize;
		u32 __user *puNumProcs;
	} ARGS_MGR_ENUMPROC_INFO;

	struct {
		struct DSP_UUID *pUuid;
		enum DSP_DCDOBJTYPE objType;
		char *pszPathName;
	} ARGS_MGR_REGISTEROBJECT;

	struct {
		struct DSP_UUID *pUuid;
		enum DSP_DCDOBJTYPE objType;
	} ARGS_MGR_UNREGISTEROBJECT;

	struct {
		struct DSP_NOTIFICATION  __user*__user *aNotifications;
		u32 uCount;
		u32 __user *puIndex;
		u32 uTimeout;
	} ARGS_MGR_WAIT;

	/* PROC Module */
	struct {
		u32 uProcessor;
		struct DSP_PROCESSORATTRIN __user *pAttrIn;
		DSP_HPROCESSOR __user *phProcessor;
	} ARGS_PROC_ATTACH;

	struct {
		DSP_HPROCESSOR hProcessor;
		u32 dwCmd;
		struct DSP_CBDATA __user *pArgs;
	} ARGS_PROC_CTRL;

	struct {
		DSP_HPROCESSOR hProcessor;
	} ARGS_PROC_DETACH;

	struct {
		DSP_HPROCESSOR hProcessor;
		DSP_HNODE __user *aNodeTab;
		u32 uNodeTabSize;
		u32 __user *puNumNodes;
		u32 __user *puAllocated;
	} ARGS_PROC_ENUMNODE_INFO;

	struct {
		DSP_HPROCESSOR hProcessor;
		u32 uResourceType;
		struct DSP_RESOURCEINFO *pResourceInfo;
		u32 uResourceInfoSize;
	} ARGS_PROC_ENUMRESOURCES;

	struct {
		DSP_HPROCESSOR hProcessor;
		struct DSP_PROCESSORSTATE __user *pProcStatus;
		u32 uStateInfoSize;
	} ARGS_PROC_GETSTATE;

	struct {
		DSP_HPROCESSOR hProcessor;
		u8 __user *pBuf;

	#ifndef RES_CLEANUP_DISABLE
	    u8 __user *pSize;
    #endif
		u32 uMaxSize;
	} ARGS_PROC_GETTRACE;

	struct {
		DSP_HPROCESSOR hProcessor;
		s32 iArgc;
		char __user*__user *aArgv;
		char *__user *aEnvp;
	} ARGS_PROC_LOAD;

	struct {
		DSP_HPROCESSOR hProcessor;
		u32 uEventMask;
		u32 uNotifyType;
		struct DSP_NOTIFICATION __user *hNotification;
	} ARGS_PROC_REGISTER_NOTIFY;

	struct {
		DSP_HPROCESSOR hProcessor;
	} ARGS_PROC_START;

	struct {
		DSP_HPROCESSOR hProcessor;
		u32 ulSize;
		void *__user *ppRsvAddr;
	} ARGS_PROC_RSVMEM;

	struct {
		DSP_HPROCESSOR hProcessor;
		u32 ulSize;
		void *pRsvAddr;
	} ARGS_PROC_UNRSVMEM;

	struct {
		DSP_HPROCESSOR hProcessor;
		void *pMpuAddr;
		u32 ulSize;
		void *pReqAddr;
		void *__user *ppMapAddr;
		u32 ulMapAttr;
	} ARGS_PROC_MAPMEM;

	struct {
		DSP_HPROCESSOR hProcessor;
		u32 ulSize;
		void *pMapAddr;
	} ARGS_PROC_UNMAPMEM;

	struct {
		DSP_HPROCESSOR hProcessor;
		void *pMpuAddr;
		u32 ulSize;
		u32 ulFlags;
	} ARGS_PROC_FLUSHMEMORY;

	struct {
		DSP_HPROCESSOR hProcessor;
	} ARGS_PROC_STOP;

	struct {
		DSP_HPROCESSOR hProcessor;
		void *pMpuAddr;
		u32 ulSize;
	} ARGS_PROC_INVALIDATEMEMORY;


	/* NODE Module */
	struct {
		DSP_HPROCESSOR hProcessor;
		struct DSP_UUID __user *pNodeID;
		struct DSP_CBDATA __user *pArgs;
		struct DSP_NODEATTRIN __user *pAttrIn;
		DSP_HNODE __user *phNode;
	} ARGS_NODE_ALLOCATE;

	struct {
		DSP_HNODE hNode;
		u32 uSize;
		struct DSP_BUFFERATTR __user *pAttr;
		u8 *__user *pBuffer;
	} ARGS_NODE_ALLOCMSGBUF;

	struct {
		DSP_HNODE hNode;
		s32 iPriority;
	} ARGS_NODE_CHANGEPRIORITY;

	struct {
		DSP_HNODE hNode;
		u32 uStream;
		DSP_HNODE hOtherNode;
		u32 uOtherStream;
		struct DSP_STRMATTR __user *pAttrs;
		struct DSP_CBDATA __user *pConnParam;
	} ARGS_NODE_CONNECT;

	struct {
		DSP_HNODE hNode;
	} ARGS_NODE_CREATE;

	struct {
		DSP_HNODE hNode;
	} ARGS_NODE_DELETE;

	struct {
		DSP_HNODE hNode;
		struct DSP_BUFFERATTR __user *pAttr;
		u8 *pBuffer;
	} ARGS_NODE_FREEMSGBUF;

	struct {
		DSP_HNODE hNode;
		struct DSP_NODEATTR __user *pAttr;
		u32 uAttrSize;
	} ARGS_NODE_GETATTR;

	struct {
		DSP_HNODE hNode;
		struct DSP_MSG __user *pMessage;
		u32 uTimeout;
	} ARGS_NODE_GETMESSAGE;

	struct {
		DSP_HNODE hNode;
	} ARGS_NODE_PAUSE;

	struct {
		DSP_HNODE hNode;
		struct DSP_MSG __user *pMessage;
		u32 uTimeout;
	} ARGS_NODE_PUTMESSAGE;

	struct {
		DSP_HNODE hNode;
		u32 uEventMask;
		u32 uNotifyType;
		struct DSP_NOTIFICATION __user *hNotification;
	} ARGS_NODE_REGISTERNOTIFY;

	struct {
		DSP_HNODE hNode;
	} ARGS_NODE_RUN;

	struct {
		DSP_HNODE hNode;
		DSP_STATUS __user *pStatus;
	} ARGS_NODE_TERMINATE;

	struct {
		DSP_HPROCESSOR hProcessor;
		struct DSP_UUID __user *pNodeID;
		struct DSP_NDBPROPS __user *pNodeProps;
	} ARGS_NODE_GETUUIDPROPS;

	/* STRM module */

	struct {
		DSP_HSTREAM hStream;
		u32 uSize;
		u8 *__user *apBuffer;
		u32 uNumBufs;
	} ARGS_STRM_ALLOCATEBUFFER;

	struct {
		DSP_HSTREAM hStream;
	} ARGS_STRM_CLOSE;

	struct {
		DSP_HSTREAM hStream;
		u8 *__user *apBuffer;
		u32 uNumBufs;
	} ARGS_STRM_FREEBUFFER;

	struct {
		DSP_HSTREAM hStream;
		HANDLE *phEvent;
	} ARGS_STRM_GETEVENTHANDLE;

	struct {
		DSP_HSTREAM hStream;
		struct STRM_INFO __user *pStreamInfo;
		u32 uStreamInfoSize;
	} ARGS_STRM_GETINFO;

	struct {
		DSP_HSTREAM hStream;
		bool bFlush;
	} ARGS_STRM_IDLE;

	struct {
		DSP_HSTREAM hStream;
		u8 *pBuffer;
		u32 dwBytes;
		u32 dwBufSize;
		u32 dwArg;
	} ARGS_STRM_ISSUE;

	struct {
		DSP_HNODE hNode;
		u32 uDirection;
		u32 uIndex;
		struct STRM_ATTR __user *pAttrIn;
		DSP_HSTREAM __user *phStream;
	} ARGS_STRM_OPEN;

	struct {
		DSP_HSTREAM hStream;
		u8 *__user *pBufPtr;
		u32 __user *pBytes;
		u32 __user *pBufSize;
		u32 __user *pdwArg;
	} ARGS_STRM_RECLAIM;

	struct {
		DSP_HSTREAM hStream;
		u32 uEventMask;
		u32 uNotifyType;
		struct DSP_NOTIFICATION __user *hNotification;
	} ARGS_STRM_REGISTERNOTIFY;

	struct {
		DSP_HSTREAM __user *aStreamTab;
		u32 nStreams;
		u32 __user *pMask;
		u32 uTimeout;
	} ARGS_STRM_SELECT;

	/* CMM Module */
	struct {
		struct CMM_OBJECT *hCmmMgr;
		u32 uSize;
		struct CMM_ATTRS *pAttrs;
		OUT void **ppBufVA;
	} ARGS_CMM_ALLOCBUF;

	struct {
		struct CMM_OBJECT *hCmmMgr;
		void *pBufPA;
		u32 ulSegId;
	} ARGS_CMM_FREEBUF;

	struct {
		DSP_HPROCESSOR hProcessor;
		struct CMM_OBJECT *__user *phCmmMgr;
	} ARGS_CMM_GETHANDLE;

	struct {
		struct CMM_OBJECT *hCmmMgr;
		struct CMM_INFO __user *pCmmInfo;
	} ARGS_CMM_GETINFO;

	/* MEM Module */
	struct {
		u32 cBytes;
		enum MEM_POOLATTRS type;
		void *pMem;
	} ARGS_MEM_ALLOC;

	struct {
		u32 cBytes;
		enum MEM_POOLATTRS type;
		void *pMem;
	} ARGS_MEM_CALLOC;

	struct {
		void *pMem;
	} ARGS_MEM_FREE;

	struct {
		void *pBuffer;
		u32 cSize;
		void *pLockedBuffer;
	} ARGS_MEM_PAGELOCK;

	struct {
		void *pBuffer;
		u32 cSize;
	} ARGS_MEM_PAGEUNLOCK;

	/* UTIL module */
	struct {
		s32 cArgc;
		char **ppArgv;
	} ARGS_UTIL_TESTDLL;
} ;

#define CMD_BASE		    1

/* MGR module offsets */
#define CMD_MGR_BASE_OFFSET	     CMD_BASE
#define CMD_MGR_ENUMNODE_INFO_OFFSET    (CMD_MGR_BASE_OFFSET + 0)
#define CMD_MGR_ENUMPROC_INFO_OFFSET    (CMD_MGR_BASE_OFFSET + 1)
#define CMD_MGR_REGISTEROBJECT_OFFSET   (CMD_MGR_BASE_OFFSET + 2)
#define CMD_MGR_UNREGISTEROBJECT_OFFSET (CMD_MGR_BASE_OFFSET + 3)
#define CMD_MGR_WAIT_OFFSET	     (CMD_MGR_BASE_OFFSET + 4)

#ifndef RES_CLEANUP_DISABLE
#define CMD_MGR_RESOUCES_OFFSET	 (CMD_MGR_BASE_OFFSET + 5)
#define CMD_MGR_END_OFFSET	      CMD_MGR_RESOUCES_OFFSET
#else
#define CMD_MGR_END_OFFSET	      CMD_MGR_WAIT_OFFSET
#endif

#define CMD_PROC_BASE_OFFSET	    (CMD_MGR_END_OFFSET + 1)
#define CMD_PROC_ATTACH_OFFSET	  (CMD_PROC_BASE_OFFSET + 0)
#define CMD_PROC_CTRL_OFFSET	    (CMD_PROC_BASE_OFFSET + 1)
#define CMD_PROC_DETACH_OFFSET	  (CMD_PROC_BASE_OFFSET + 2)
#define CMD_PROC_ENUMNODE_OFFSET	(CMD_PROC_BASE_OFFSET + 3)
#define CMD_PROC_ENUMRESOURCES_OFFSET   (CMD_PROC_BASE_OFFSET + 4)
#define CMD_PROC_GETSTATE_OFFSET	(CMD_PROC_BASE_OFFSET + 5)
#define CMD_PROC_GETTRACE_OFFSET	(CMD_PROC_BASE_OFFSET + 6)
#define CMD_PROC_LOAD_OFFSET	    (CMD_PROC_BASE_OFFSET + 7)
#define CMD_PROC_REGISTERNOTIFY_OFFSET  (CMD_PROC_BASE_OFFSET + 8)
#define CMD_PROC_START_OFFSET	   (CMD_PROC_BASE_OFFSET + 9)
#define CMD_PROC_RSVMEM_OFFSET	  (CMD_PROC_BASE_OFFSET + 10)
#define CMD_PROC_UNRSVMEM_OFFSET	(CMD_PROC_BASE_OFFSET + 11)
#define CMD_PROC_MAPMEM_OFFSET	  (CMD_PROC_BASE_OFFSET + 12)
#define CMD_PROC_UNMAPMEM_OFFSET	(CMD_PROC_BASE_OFFSET + 13)
#define CMD_PROC_FLUSHMEMORY_OFFSET      (CMD_PROC_BASE_OFFSET + 14)
#define CMD_PROC_STOP_OFFSET	    (CMD_PROC_BASE_OFFSET + 15)
#define CMD_PROC_INVALIDATEMEMORY_OFFSET (CMD_PROC_BASE_OFFSET + 16)
#define CMD_PROC_END_OFFSET	     CMD_PROC_INVALIDATEMEMORY_OFFSET


#define CMD_NODE_BASE_OFFSET	    (CMD_PROC_END_OFFSET + 1)
#define CMD_NODE_ALLOCATE_OFFSET	(CMD_NODE_BASE_OFFSET + 0)
#define CMD_NODE_ALLOCMSGBUF_OFFSET     (CMD_NODE_BASE_OFFSET + 1)
#define CMD_NODE_CHANGEPRIORITY_OFFSET  (CMD_NODE_BASE_OFFSET + 2)
#define CMD_NODE_CONNECT_OFFSET	 (CMD_NODE_BASE_OFFSET + 3)
#define CMD_NODE_CREATE_OFFSET	  (CMD_NODE_BASE_OFFSET + 4)
#define CMD_NODE_DELETE_OFFSET	  (CMD_NODE_BASE_OFFSET + 5)
#define CMD_NODE_FREEMSGBUF_OFFSET      (CMD_NODE_BASE_OFFSET + 6)
#define CMD_NODE_GETATTR_OFFSET	 (CMD_NODE_BASE_OFFSET + 7)
#define CMD_NODE_GETMESSAGE_OFFSET      (CMD_NODE_BASE_OFFSET + 8)
#define CMD_NODE_PAUSE_OFFSET	   (CMD_NODE_BASE_OFFSET + 9)
#define CMD_NODE_PUTMESSAGE_OFFSET      (CMD_NODE_BASE_OFFSET + 10)
#define CMD_NODE_REGISTERNOTIFY_OFFSET  (CMD_NODE_BASE_OFFSET + 11)
#define CMD_NODE_RUN_OFFSET	     (CMD_NODE_BASE_OFFSET + 12)
#define CMD_NODE_TERMINATE_OFFSET       (CMD_NODE_BASE_OFFSET + 13)
#define CMD_NODE_GETUUIDPROPS_OFFSET    (CMD_NODE_BASE_OFFSET + 14)
#define CMD_NODE_END_OFFSET	     CMD_NODE_GETUUIDPROPS_OFFSET

#define CMD_STRM_BASE_OFFSET	    (CMD_NODE_END_OFFSET + 1)
#define CMD_STRM_ALLOCATEBUFFER_OFFSET  (CMD_STRM_BASE_OFFSET + 0)
#define CMD_STRM_CLOSE_OFFSET	   (CMD_STRM_BASE_OFFSET + 1)
#define CMD_STRM_FREEBUFFER_OFFSET      (CMD_STRM_BASE_OFFSET + 2)
#define CMD_STRM_GETEVENTHANDLE_OFFSET  (CMD_STRM_BASE_OFFSET + 3)
#define CMD_STRM_GETINFO_OFFSET	 (CMD_STRM_BASE_OFFSET + 4)
#define CMD_STRM_IDLE_OFFSET	    (CMD_STRM_BASE_OFFSET + 5)
#define CMD_STRM_ISSUE_OFFSET	   (CMD_STRM_BASE_OFFSET + 6)
#define CMD_STRM_OPEN_OFFSET	    (CMD_STRM_BASE_OFFSET + 7)
#define CMD_STRM_RECLAIM_OFFSET	 (CMD_STRM_BASE_OFFSET + 8)
#define CMD_STRM_REGISTERNOTIFY_OFFSET  (CMD_STRM_BASE_OFFSET + 9)
#define CMD_STRM_SELECT_OFFSET	  (CMD_STRM_BASE_OFFSET + 10)
#define CMD_STRM_END_OFFSET	     CMD_STRM_SELECT_OFFSET

/* Communication Memory Manager (UCMM) */
#define CMD_CMM_BASE_OFFSET	     (CMD_STRM_END_OFFSET + 1)
#define CMD_CMM_ALLOCBUF_OFFSET	 (CMD_CMM_BASE_OFFSET + 0)
#define CMD_CMM_FREEBUF_OFFSET	  (CMD_CMM_BASE_OFFSET + 1)
#define CMD_CMM_GETHANDLE_OFFSET	(CMD_CMM_BASE_OFFSET + 2)
#define CMD_CMM_GETINFO_OFFSET	  (CMD_CMM_BASE_OFFSET + 3)
#define CMD_CMM_END_OFFSET	      CMD_CMM_GETINFO_OFFSET

#define CMD_BASE_END_OFFSET	CMD_CMM_END_OFFSET
#endif				/* WCDIOCTL_ */
