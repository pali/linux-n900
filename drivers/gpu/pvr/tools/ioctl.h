/**********************************************************************
 *
 * Copyright(c) 2008 Imagination Technologies Ltd. All rights reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope it will be useful but, except 
 * as otherwise stated in writing, without any warranty; without even the 
 * implied warranty of merchantability or fitness for a particular purpose. 
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * 
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * Imagination Technologies Ltd. <gpl-support@imgtec.com>
 * Home Park Estate, Kings Langley, Herts, WD4 8LZ, UK 
 *
 ******************************************************************************/

#ifndef _IOCTL_
#define _IOCTL_

IMG_UINT32 DBGDIOCDrivCreateStream(IMG_VOID *, IMG_VOID *);
IMG_UINT32 DBGDIOCDrivDestroyStream(IMG_VOID *, IMG_VOID *);
IMG_UINT32 DBGDIOCDrivGetStream(IMG_VOID *, IMG_VOID *);
IMG_UINT32 DBGDIOCDrivWriteString(IMG_VOID *, IMG_VOID *);
IMG_UINT32 DBGDIOCDrivReadString(IMG_VOID *, IMG_VOID *);
IMG_UINT32 DBGDIOCDrivWrite(IMG_VOID *, IMG_VOID *);
IMG_UINT32 DBGDIOCDrivWrite2(IMG_VOID *, IMG_VOID *);
IMG_UINT32 DBGDIOCDrivRead(IMG_VOID *, IMG_VOID *);
IMG_UINT32 DBGDIOCDrivSetCaptureMode(IMG_VOID *, IMG_VOID *);
IMG_UINT32 DBGDIOCDrivSetOutMode(IMG_VOID *, IMG_VOID *);
IMG_UINT32 DBGDIOCDrivSetDebugLevel(IMG_VOID *, IMG_VOID *);
IMG_UINT32 DBGDIOCDrivSetFrame(IMG_VOID *, IMG_VOID *);
IMG_UINT32 DBGDIOCDrivGetFrame(IMG_VOID *, IMG_VOID *);
IMG_UINT32 DBGDIOCDrivOverrideMode(IMG_VOID *, IMG_VOID *);
IMG_UINT32 DBGDIOCDrivDefaultMode(IMG_VOID *, IMG_VOID *);
IMG_UINT32 DBGDIOCDrivGetServiceTable(IMG_VOID *, IMG_VOID *);
IMG_UINT32 DBGDIOCDrivWriteStringCM(IMG_VOID *, IMG_VOID *);
IMG_UINT32 DBGDIOCDrivWriteCM(IMG_VOID *, IMG_VOID *);
IMG_UINT32 DBGDIOCDrivSetMarker(IMG_VOID *, IMG_VOID *);
IMG_UINT32 DBGDIOCDrivGetMarker(IMG_VOID *, IMG_VOID *);
IMG_UINT32 DBGDIOCDrivIsCaptureFrame(IMG_VOID *, IMG_VOID *);
IMG_UINT32 DBGDIOCDrivWriteLF(IMG_VOID *, IMG_VOID *);
IMG_UINT32 DBGDIOCDrivReadLF(IMG_VOID *, IMG_VOID *);
IMG_UINT32 DBGDIOCDrivResetStream(IMG_VOID *, IMG_VOID *);

IMG_UINT32(*g_DBGDrivProc[])(IMG_VOID *, IMG_VOID *) = {
DBGDIOCDrivCreateStream,
	    DBGDIOCDrivDestroyStream,
	    DBGDIOCDrivGetStream,
	    DBGDIOCDrivWriteString,
	    DBGDIOCDrivReadString,
	    DBGDIOCDrivWrite,
	    DBGDIOCDrivRead,
	    DBGDIOCDrivSetCaptureMode,
	    DBGDIOCDrivSetOutMode,
	    DBGDIOCDrivSetDebugLevel,
	    DBGDIOCDrivSetFrame,
	    DBGDIOCDrivGetFrame,
	    DBGDIOCDrivOverrideMode,
	    DBGDIOCDrivDefaultMode,
	    DBGDIOCDrivGetServiceTable,
	    DBGDIOCDrivWrite2,
	    DBGDIOCDrivWriteStringCM,
	    DBGDIOCDrivWriteCM,
	    DBGDIOCDrivSetMarker,
	    DBGDIOCDrivGetMarker,
	    DBGDIOCDrivIsCaptureFrame,
	    DBGDIOCDrivWriteLF, DBGDIOCDrivReadLF, DBGDIOCDrivResetStream,};

#define MAX_DBGVXD_W32_API (sizeof(g_DBGDrivProc)/sizeof(IMG_UINT32))

#endif
