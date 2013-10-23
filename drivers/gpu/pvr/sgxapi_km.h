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

#ifndef __SGXAPI_KM_H__
#define __SGXAPI_KM_H__


#include "sgxdefs.h"


#define SGX_GENERAL_HEAP_ID					0
#define SGX_TADATA_HEAP_ID					1
#define SGX_KERNEL_CODE_HEAP_ID				2
#define SGX_VIDEO_CODE_HEAP_ID				3
#define SGX_KERNEL_VIDEO_DATA_HEAP_ID		4
#define SGX_PIXELSHADER_HEAP_ID				5
#define SGX_VERTEXSHADER_HEAP_ID			6
#define SGX_PDSPIXEL_CODEDATA_HEAP_ID		7
#define SGX_PDSVERTEX_CODEDATA_HEAP_ID		8
#define SGX_SYNCINFO_HEAP_ID				9
#define SGX_3DPARAMETERS_HEAP_ID			10
#define SGX_GENERAL_MAPPING_HEAP_ID			11
#define SGX_UNDEFINED_HEAP_ID				(~0LU)

#define SGX_ALT_MAPPING_HEAP_ID                         12
#define SGX_FB_MAPPING_HEAP_ID				13
#define SGX_MAX_HEAP_ID					14

#define SGX_MAX_TA_STATUS_VALS	32
#define SGX_MAX_3D_STATUS_VALS	2

#define SGX_MAX_SRC_SYNCS			4

	typedef struct _SGX_SLAVE_PORT_ {
		IMG_PVOID pvData;
		IMG_UINT32 ui32DataRange;
		IMG_PUINT32 pui32Offset;
		IMG_SYS_PHYADDR sPhysBase;
	} SGX_SLAVE_PORT;


#define PVRSRV_SGX_HWPERF_CBSIZE					0x100

#define PVRSRV_SGX_HWPERF_INVALID					1
#define PVRSRV_SGX_HWPERF_TRANSFER					2
#define PVRSRV_SGX_HWPERF_TA						3
#define PVRSRV_SGX_HWPERF_3D						4

#define PVRSRV_SGX_HWPERF_ON						0x40

	typedef struct _PVRSRV_SGX_HWPERF_CBDATA_ {
		IMG_UINT32 ui32FrameNo;
		IMG_UINT32 ui32Type;
		IMG_UINT32 ui32StartTimeWraps;
		IMG_UINT32 ui32StartTime;
		IMG_UINT32 ui32EndTimeWraps;
		IMG_UINT32 ui32EndTime;
		IMG_UINT32 ui32ClockSpeed;
		IMG_UINT32 ui32TimeMax;
	} PVRSRV_SGX_HWPERF_CBDATA;

	typedef struct _PVRSRV_SGX_HWPERF_CB_ {
		IMG_UINT32 ui32Woff;
		IMG_UINT32 ui32Roff;
		PVRSRV_SGX_HWPERF_CBDATA
		    psHWPerfCBData[PVRSRV_SGX_HWPERF_CBSIZE];
	} PVRSRV_SGX_HWPERF_CB;

	typedef struct _SGX_MISC_INFO_HWPERF_RETRIEVE_CB {
		PVRSRV_SGX_HWPERF_CBDATA *psHWPerfData;
		IMG_UINT32 ui32ArraySize;
		IMG_UINT32 ui32DataCount;
		IMG_UINT32 ui32Time;
	} SGX_MISC_INFO_HWPERF_RETRIEVE_CB;

	typedef enum _SGX_MISC_INFO_REQUEST_ {
		SGX_MISC_INFO_REQUEST_CLOCKSPEED = 0,
		SGX_MISC_INFO_REQUEST_HWPERF_CB_ON,
		SGX_MISC_INFO_REQUEST_HWPERF_CB_OFF,
		SGX_MISC_INFO_REQUEST_HWPERF_RETRIEVE_CB,
		SGX_MISC_INFO_REQUEST_FORCE_I16 = 0x7fff
	} SGX_MISC_INFO_REQUEST;

	typedef struct _SGX_MISC_INFO_ {
		SGX_MISC_INFO_REQUEST eRequest;

		union {
			IMG_UINT32 reserved;
			IMG_UINT32 ui32SGXClockSpeed;
			SGX_MISC_INFO_HWPERF_RETRIEVE_CB sRetrieveCB;
		} uData;
	} SGX_MISC_INFO;


#define PVR3DIF4_KICKTA_DUMPBITMAP_MAX_NAME_LENGTH		256

	typedef struct _PVR3DIF4_KICKTA_DUMPBITMAP_ {
		IMG_DEV_VIRTADDR sDevBaseAddr;
		IMG_UINT32 ui32Flags;
		IMG_UINT32 ui32Width;
		IMG_UINT32 ui32Height;
		IMG_UINT32 ui32Stride;
		IMG_UINT32 ui32PDUMPFormat;
		IMG_UINT32 ui32BytesPP;
		IMG_CHAR pszName[PVR3DIF4_KICKTA_DUMPBITMAP_MAX_NAME_LENGTH];
	} PVR3DIF4_KICKTA_DUMPBITMAP, *PPVR3DIF4_KICKTA_DUMPBITMAP;

#define PVRSRV_SGX_PDUMP_CONTEXT_MAX_BITMAP_ARRAY_SIZE	(16)

	typedef struct _PVRSRV_SGX_PDUMP_CONTEXT_ {

		IMG_UINT32 ui32CacheControl;

	} PVRSRV_SGX_PDUMP_CONTEXT;

	typedef struct _PVR3DIF4_KICKTA_DUMP_ROFF_ {
		IMG_HANDLE hKernelMemInfo;
		IMG_UINT32 uiAllocIndex;
		IMG_UINT32 ui32Offset;
		IMG_UINT32 ui32Value;
		IMG_PCHAR pszName;
	} PVR3DIF4_KICKTA_DUMP_ROFF, *PPVR3DIF4_KICKTA_DUMP_ROFF;

	typedef struct _PVR3DIF4_KICKTA_DUMP_BUFFER_ {
		IMG_UINT32 ui32SpaceUsed;
		IMG_UINT32 ui32Start;
		IMG_UINT32 ui32End;
		IMG_UINT32 ui32BufferSize;
		IMG_UINT32 ui32BackEndLength;
		IMG_UINT32 uiAllocIndex;
		IMG_HANDLE hKernelMemInfo;
		IMG_PCHAR pszName;
	} PVR3DIF4_KICKTA_DUMP_BUFFER, *PPVR3DIF4_KICKTA_DUMP_BUFFER;

#ifdef PDUMP
	typedef struct _PVR3DIF4_KICKTA_PDUMP_ {

		PPVR3DIF4_KICKTA_DUMPBITMAP psPDumpBitmapArray;
		IMG_UINT32 ui32PDumpBitmapSize;

		PPVR3DIF4_KICKTA_DUMP_BUFFER psBufferArray;
		IMG_UINT32 ui32BufferArraySize;

		PPVR3DIF4_KICKTA_DUMP_ROFF psROffArray;
		IMG_UINT32 ui32ROffArraySize;
	} PVR3DIF4_KICKTA_PDUMP, *PPVR3DIF4_KICKTA_PDUMP;
#endif

#define SGX_MAX_TRANSFER_STATUS_VALS	2
#define SGX_MAX_TRANSFER_SYNC_OPS	5

#endif
