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


#define SGX_GENERAL_HEAP_ID			0
#define SGX_TADATA_HEAP_ID			1
#define SGX_KERNEL_CODE_HEAP_ID			2
#define SGX_VIDEO_CODE_HEAP_ID			3
#define SGX_KERNEL_VIDEO_DATA_HEAP_ID		4
#define SGX_PIXELSHADER_HEAP_ID			5
#define SGX_VERTEXSHADER_HEAP_ID		6
#define SGX_PDSPIXEL_CODEDATA_HEAP_ID		7
#define SGX_PDSVERTEX_CODEDATA_HEAP_ID		8
#define SGX_SYNCINFO_HEAP_ID			9
#define SGX_3DPARAMETERS_HEAP_ID		10
#define SGX_GENERAL_MAPPING_HEAP_ID		11
#define SGX_UNDEFINED_HEAP_ID			(~0LU)

#define SGX_ALT_MAPPING_HEAP_ID			12
#define SGX_FB_MAPPING_HEAP_ID			13
#define SGX_MAX_HEAP_ID				14

#define SGX_MAX_TA_STATUS_VALS			32
#define SGX_MAX_3D_STATUS_VALS			2

#define SGX_MAX_SRC_SYNCS			4

struct SGX_SLAVE_PORT {
	void *pvData;
	u32 ui32DataRange;
	u32 *pui32Offset;
	struct IMG_SYS_PHYADDR sPhysBase;
};

#define PVRSRV_SGX_HWPERF_CBSIZE		0x100

#define PVRSRV_SGX_HWPERF_INVALID		1
#define PVRSRV_SGX_HWPERF_TRANSFER		2
#define PVRSRV_SGX_HWPERF_TA			3
#define PVRSRV_SGX_HWPERF_3D			4

#define PVRSRV_SGX_HWPERF_ON			0x40

struct PVRSRV_SGX_HWPERF_CBDATA {
	u32 ui32FrameNo;
	u32 ui32Type;
	u32 ui32StartTimeWraps;
	u32 ui32StartTime;
	u32 ui32EndTimeWraps;
	u32 ui32EndTime;
	u32 ui32ClockSpeed;
	u32 ui32TimeMax;
};

struct PVRSRV_SGX_HWPERF_CB {
	u32 ui32Woff;
	u32 ui32Roff;
	struct PVRSRV_SGX_HWPERF_CBDATA
			    psHWPerfCBData[PVRSRV_SGX_HWPERF_CBSIZE];
};

struct SGX_MISC_INFO_HWPERF_RETRIEVE_CB {
	struct PVRSRV_SGX_HWPERF_CBDATA *psHWPerfData;
	u32 ui32ArraySize;
	u32 ui32DataCount;
	u32 ui32Time;
};

enum SGX_MISC_INFO_REQUEST {
	SGX_MISC_INFO_REQUEST_CLOCKSPEED = 0,
	SGX_MISC_INFO_REQUEST_HWPERF_CB_ON,
	SGX_MISC_INFO_REQUEST_HWPERF_CB_OFF,
	SGX_MISC_INFO_REQUEST_HWPERF_RETRIEVE_CB,
	SGX_MISC_INFO_REQUEST_FORCE_I16 = 0x7fff
};

struct SGX_MISC_INFO {
	enum SGX_MISC_INFO_REQUEST eRequest;

	union {
		u32 reserved;
		u32 ui32SGXClockSpeed;
		struct SGX_MISC_INFO_HWPERF_RETRIEVE_CB sRetrieveCB;
	} uData;
};


#define PVR3DIF4_KICKTA_DUMPBITMAP_MAX_NAME_LENGTH		256

struct PVR3DIF4_KICKTA_DUMPBITMAP {
	struct IMG_DEV_VIRTADDR sDevBaseAddr;
	u32 ui32Flags;
	u32 ui32Width;
	u32 ui32Height;
	u32 ui32Stride;
	u32 ui32PDUMPFormat;
	u32 ui32BytesPP;
	char pszName[PVR3DIF4_KICKTA_DUMPBITMAP_MAX_NAME_LENGTH];
};

#define PVRSRV_SGX_PDUMP_CONTEXT_MAX_BITMAP_ARRAY_SIZE	(16)

struct PVRSRV_SGX_PDUMP_CONTEXT {
	u32 ui32CacheControl;
};

struct PVR3DIF4_KICKTA_DUMP_ROFF {
	void *hKernelMemInfo;
	u32 uiAllocIndex;
	u32 ui32Offset;
	u32 ui32Value;
	char *pszName;
};

struct PVR3DIF4_KICKTA_DUMP_BUFFER {
	u32 ui32SpaceUsed;
	u32 ui32Start;
	u32 ui32End;
	u32 ui32BufferSize;
	u32 ui32BackEndLength;
	u32 uiAllocIndex;
	void *hKernelMemInfo;
	char *pszName;
};

#ifdef PDUMP
struct PVR3DIF4_KICKTA_PDUMP {

	struct PVR3DIF4_KICKTA_DUMPBITMAP *psPDumpBitmapArray;
	u32 ui32PDumpBitmapSize;

	struct PVR3DIF4_KICKTA_DUMP_BUFFER *psBufferArray;
	u32 ui32BufferArraySize;

	struct PVR3DIF4_KICKTA_DUMP_ROFF *psROffArray;
	u32 ui32ROffArraySize;
};
#endif

#define SGX_MAX_TRANSFER_STATUS_VALS	2
#define SGX_MAX_TRANSFER_SYNC_OPS	5

#endif
