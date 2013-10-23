/*
 * _chnl_sm.h
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
 *  ======== _chnl_sm.h ========
 *  Description:
 *      Private header file defining channel manager and channel objects for
 *      a shared memory channel driver.
 *
 *  Public Functions:
 *      None.
 *
 *  Notes:
 *      Shared between the modules implementing the shared memory channel class
 *      library.
 *
 *! Revision History:
 *! ================
 *! 15-Oct-2002 kc  Removed legacy PERF code.
 *! 12-Jan-2002 ag  Removed unused gppReqIO & ddmaChnlId DDMA fields.
 *!                 Added zero-copy chnl descriptor array: zchnldesc.
 *! 21-Dec-2001 ag  Moved descPaGpp to private chnl obj from chnl descriptor.
 *! 20-May-2001 ag/jeh Removed fShmSyms field from CHNL_MGR.
 *! 04-Feb-2001 ag  DSP-DMA support added.
 *! 26-Oct-2000 jeh Added arg and resvd to SHM control structure. Added dwArg
 *!                 to CHNL_IRP.
 *! 16-Oct-2000 jeh Removed #ifdef DEBUG from around channel object's cIOCs
 *!                 field, added cIOReqs.
 *! 20-Jan-2000 ag: Incorporated code review comments.
 *! 05-Jan-2000 ag: Text format cleanup.
 *! 03-Nov-1999 ag: Added szEventName[] to CHNL object for name event support.
 *! 02-Nov-1999 ag: _SHM_BEG & _END Syms from COFF now used for IO and SM CLASS.
 *! 27-Oct-1999 jeh Define SHM structure to work for 16-bit targets.
 *! 25-May-1999 jg: Added target side symbol names for share memory buffer
 *! 03-Jan-1997 gp: Added fSharedIRQ field.
 *! 22-Oct-1996 gp: Made dwProcessID a handle.
 *! 09-Sep-1996 gp: Added dwProcessID field to CHNL_OBJECT.
 *! 13-Aug-1996 gp: Created.
 */

#ifndef _CHNL_SM_
#define _CHNL_SM_

#include <dspbridge/wcd.h>
#include <dspbridge/wmd.h>
#include <dspbridge/dpc.h>

#include <dspbridge/list.h>
#include <dspbridge/ntfy.h>

/*
 *  These target side symbols define the beginning and ending addresses
 *  of shared memory buffer. They are defined in the *cfg.cmd file by
 *  cdb code.
 */
#define CHNL_SHARED_BUFFER_BASE_SYM "_SHM_BEG"
#define CHNL_SHARED_BUFFER_LIMIT_SYM "_SHM_END"
#define BRIDGEINIT_BIOSGPTIMER "_BRIDGEINIT_BIOSGPTIMER"
#define BRIDGEINIT_LOADMON_GPTIMER "_BRIDGEINIT_LOADMON_GPTIMER"

#ifndef _CHNL_WORDSIZE
#define _CHNL_WORDSIZE 4	/* default _CHNL_WORDSIZE is 2 bytes/word */
#endif

#define MAXOPPS 16

struct oppTableEntry {
    u32 voltage;
    u32 frequency;
    u32 minFreq;
    u32 maxFreq;
} ;

struct oppStruct {
    u32 currOppPt;
    u32 numOppPts;
    struct oppTableEntry oppPoint[MAXOPPS];
} ;

/* Request to MPU */
struct oppRqstStruct {
    u32 rqstDspFreq;
    u32 rqstOppPt;
};

/* Info to MPU */
struct loadMonStruct {
    u32 currDspLoad;
    u32 currDspFreq;
    u32 predDspLoad;
    u32 predDspFreq;
};

	enum SHM_DESCTYPE {
		SHM_CURROPP = 0,
		SHM_OPPINFO = 1,
		SHM_GETOPP = 2,		/* Get DSP requested OPP info */
	} ;

/* Structure in shared between DSP and PC for communication.*/
	struct SHM {
		u32 dspFreeMask;	/* Written by DSP, read by PC. */
		u32 hostFreeMask;	/* Written by PC, read by DSP */

		u32 inputFull;	/* Input channel has unread data. */
		u32 inputId;	/* Channel for which input is available. */
		u32 inputSize;	/* Size of data block (in DSP words). */

		u32 outputFull;	/* Output channel has unread data. */
		u32 outputId;	/* Channel for which output is available. */
		u32 outputSize;	/* Size of data block (in DSP words). */

		u32 arg;	/* Arg for Issue/Reclaim (23 bits for 55x). */
		u32 resvd;	/* Keep structure size even for 32-bit DSPs */

		/* Operating Point structure */
		struct oppStruct  oppTableStruct;
		/* Operating Point Request structure */
		struct oppRqstStruct oppRequest;
		/* load monitor information structure*/
		struct loadMonStruct loadMonInfo;
		char dummy[184];             /* padding to 256 byte boundary */
		u32 shm_dbg_var[64];         /* shared memory debug variables */
	} ;

	/* Channel Manager: only one created per board: */
	struct CHNL_MGR {
		u32 dwSignature;	/* Used for object validation */
		/* Function interface to WMD */
		struct WMD_DRV_INTERFACE *pIntfFxns;
		struct IO_MGR *hIOMgr;	/* IO manager */
		/* Device this board represents */
		struct DEV_OBJECT *hDevObject;

		/* These fields initialized in WMD_CHNL_Create():    */
		u32 dwOutputMask; /* Host output channels w/ full buffers */
		u32 dwLastOutput;	/* Last output channel fired from DPC */
		/* Critical section object handle */
		struct SYNC_CSOBJECT *hCSObj;
		u32 uWordSize;	/* Size in bytes of DSP word */
		u32 cChannels;	/* Total number of channels */
		u32 cOpenChannels;	/* Total number of open channels */
		struct CHNL_OBJECT **apChannel;	/* Array of channels */
		u32 dwType;	/* Type of channel class library */
		/* If no SHM syms, return for CHNL_Open */
		DSP_STATUS chnlOpenStatus;
	} ;

/*
 *  Channel: up to CHNL_MAXCHANNELS per board or if DSP-DMA supported then
 *     up to CHNL_MAXCHANNELS + CHNL_MAXDDMACHNLS per board.
 */
	struct CHNL_OBJECT {
		u32 dwSignature;	/* Used for object validation */
		/* Pointer back to channel manager */
		struct CHNL_MGR *pChnlMgr;
		u32 uId;	/* Channel id */
		u32 dwState;	/* Current channel state */
		u32 uMode;	/* Chnl mode and attributes */
		/* Chnl I/O completion event (user mode) */
		HANDLE hUserEvent;
		/* Abstract syncronization object */
		struct SYNC_OBJECT *hSyncEvent;
		/* Name of Sync event */
		char szEventName[SYNC_MAXNAMELENGTH + 1];
               u32 hProcess;   /* Process which created this channel */
		u32 pCBArg;	/* Argument to use with callback */
		struct LST_LIST *pIORequests;	/* List of IOR's to driver */
		s32 cIOCs;	/* Number of IOC's in queue */
		s32 cIOReqs;	/* Number of IORequests in queue */
		s32 cChirps;	/* Initial number of free Irps */
		/* List of IOC's from driver */
		struct LST_LIST *pIOCompletions;
		struct LST_LIST *pFreeList;	/* List of free Irps */
		struct NTFY_OBJECT *hNtfy;
		u32 cBytesMoved;	/* Total number of bytes transfered */

		/* For DSP-DMA */

		/* Type of chnl transport:CHNL_[PCPY][DDMA] */
		u32 uChnlType;
	} ;

/* I/O Request/completion packet: */
	struct CHNL_IRP {
		struct LST_ELEM link;	/* Link to next CHIRP in queue. */
		/* Buffer to be filled/emptied. (User)   */
		u8 *pHostUserBuf;
		/* Buffer to be filled/emptied. (System) */
		u8 *pHostSysBuf;
		u32 dwArg;	/* Issue/Reclaim argument.               */
		u32 uDspAddr;	/* Transfer address on DSP side.         */
		u32 cBytes;	/* Bytes transferred.                    */
		u32 cBufSize;	/* Actual buffer size when allocated.    */
		u32 status;	/* Status of IO completion.              */
	} ;

#endif				/* _CHNL_SM_ */
