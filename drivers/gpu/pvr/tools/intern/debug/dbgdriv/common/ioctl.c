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



#ifdef LINUX
#include <asm/uaccess.h>
#endif 

#include "img_types.h"
#include "dbgdrvif.h"
#include "dbgdriv.h"
#include "hotkey.h"


IMG_UINT32 DBGDIOCDrivCreateStream(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	PDBG_IN_CREATESTREAM psIn;
	IMG_VOID * *ppvOut;
	#ifdef LINUX
	static char name[32];
	#endif

	psIn = (PDBG_IN_CREATESTREAM) pvInBuffer;
	ppvOut = (IMG_VOID * *) pvOutBuffer;

	#ifdef LINUX

	if(copy_from_user(name, psIn->pszName, 32) != 0)
		return IMG_FALSE;
	*ppvOut = ExtDBGDrivCreateStream(name, psIn->ui32CapMode, psIn->ui32OutMode, 0, psIn->ui32Pages);

	#else
	*ppvOut = ExtDBGDrivCreateStream(psIn->pszName, psIn->ui32CapMode, psIn->ui32OutMode, DEBUG_FLAGS_NO_BUF_EXPANDSION, psIn->ui32Pages);
	#endif


	return(IMG_TRUE);
}

IMG_UINT32 DBGDIOCDrivDestroyStream(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	IMG_UINT32 *		pStream;
	PDBG_STREAM	psStream;

	pStream = (IMG_UINT32 *) pvInBuffer;
	psStream = (PDBG_STREAM) *pStream;

	PVR_UNREFERENCED_PARAMETER(	pvOutBuffer);

	ExtDBGDrivDestroyStream(psStream);

	return(IMG_TRUE);
}

IMG_UINT32 DBGDIOCDrivGetStream(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	PDBG_IN_FINDSTREAM psParams;
	IMG_UINT32 *	pui32Stream;

	psParams		= (PDBG_IN_FINDSTREAM)pvInBuffer;
	pui32Stream	= (IMG_UINT32 *)pvOutBuffer;

	*pui32Stream = (IMG_UINT32)ExtDBGDrivFindStream(psParams->pszName, psParams->bResetStream);

	return(IMG_TRUE);
}

IMG_UINT32 DBGDIOCDrivWriteString(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	PDBG_IN_WRITESTRING psParams;
	IMG_UINT32 *				pui32OutLen;

	psParams = (PDBG_IN_WRITESTRING) pvInBuffer;
	pui32OutLen = (IMG_UINT32 *) pvOutBuffer;

	*pui32OutLen = ExtDBGDrivWriteString((PDBG_STREAM) psParams->pvStream,psParams->pszString,psParams->ui32Level);

	return(IMG_TRUE);
}

IMG_UINT32 DBGDIOCDrivWriteStringCM(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	PDBG_IN_WRITESTRING psParams;
	IMG_UINT32 *				pui32OutLen;

	psParams = (PDBG_IN_WRITESTRING) pvInBuffer;
	pui32OutLen = (IMG_UINT32 *) pvOutBuffer;

	*pui32OutLen = ExtDBGDrivWriteStringCM((PDBG_STREAM) psParams->pvStream,psParams->pszString,psParams->ui32Level);

	return(IMG_TRUE);
}

IMG_UINT32 DBGDIOCDrivReadString(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	IMG_UINT32 *				pui32OutLen;
	PDBG_IN_READSTRING	psParams;

	psParams = (PDBG_IN_READSTRING) pvInBuffer;
	pui32OutLen = (IMG_UINT32 *) pvOutBuffer;

	*pui32OutLen = ExtDBGDrivReadString(psParams->pvStream,psParams->pszString,psParams->ui32StringLen);

	return(IMG_TRUE);
}

IMG_UINT32 DBGDIOCDrivWrite(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	IMG_UINT32 *				pui32BytesCopied;
	PDBG_IN_WRITE		psInParams;

	psInParams = (PDBG_IN_WRITE) pvInBuffer;
	pui32BytesCopied = (IMG_UINT32 *) pvOutBuffer;

	*pui32BytesCopied = ExtDBGDrivWrite((PDBG_STREAM) psInParams->pvStream,psInParams->pui8InBuffer,psInParams->ui32TransferSize,psInParams->ui32Level);

	return(IMG_TRUE);
}

IMG_UINT32 DBGDIOCDrivWrite2(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	IMG_UINT32 *				pui32BytesCopied;
	PDBG_IN_WRITE		psInParams;

	psInParams = (PDBG_IN_WRITE) pvInBuffer;
	pui32BytesCopied = (IMG_UINT32 *) pvOutBuffer;

	*pui32BytesCopied = ExtDBGDrivWrite2((PDBG_STREAM) psInParams->pvStream,psInParams->pui8InBuffer,psInParams->ui32TransferSize,psInParams->ui32Level);

	return(IMG_TRUE);
}

IMG_UINT32 DBGDIOCDrivWriteCM(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	IMG_UINT32 *				pui32BytesCopied;
	PDBG_IN_WRITE		psInParams;

	psInParams = (PDBG_IN_WRITE) pvInBuffer;
	pui32BytesCopied = (IMG_UINT32 *) pvOutBuffer;

	*pui32BytesCopied = ExtDBGDrivWriteCM((PDBG_STREAM) psInParams->pvStream,psInParams->pui8InBuffer,psInParams->ui32TransferSize,psInParams->ui32Level);

	return(IMG_TRUE);
}

IMG_UINT32 DBGDIOCDrivRead(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	IMG_UINT32 *				pui32BytesCopied;
	PDBG_IN_READ		psInParams;

	psInParams = (PDBG_IN_READ) pvInBuffer;
	pui32BytesCopied = (IMG_UINT32 *) pvOutBuffer;

	*pui32BytesCopied = ExtDBGDrivRead((PDBG_STREAM) psInParams->pvStream,psInParams->bReadInitBuffer, psInParams->ui32OutBufferSize,psInParams->pui8OutBuffer);

	return(IMG_TRUE);
}

IMG_UINT32 DBGDIOCDrivSetCaptureMode(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	PDBG_IN_SETDEBUGMODE 	psParams;

	psParams = (PDBG_IN_SETDEBUGMODE) pvInBuffer;
	PVR_UNREFERENCED_PARAMETER(pvOutBuffer);

	ExtDBGDrivSetCaptureMode((PDBG_STREAM) psParams->pvStream,
						  psParams->ui32Mode,
						  psParams->ui32Start,
						  psParams->ui32End,
						  psParams->ui32SampleRate);

	return(IMG_TRUE);
}

IMG_UINT32 DBGDIOCDrivSetOutMode(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	PDBG_IN_SETDEBUGOUTMODE psParams;

	psParams = (PDBG_IN_SETDEBUGOUTMODE) pvInBuffer;
	PVR_UNREFERENCED_PARAMETER(pvOutBuffer);

	ExtDBGDrivSetOutputMode((PDBG_STREAM) psParams->pvStream,psParams->ui32Mode);

	return(IMG_TRUE);
}

IMG_UINT32 DBGDIOCDrivSetDebugLevel(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	PDBG_IN_SETDEBUGLEVEL psParams;

	psParams = (PDBG_IN_SETDEBUGLEVEL) pvInBuffer;
	PVR_UNREFERENCED_PARAMETER(pvOutBuffer);

	ExtDBGDrivSetDebugLevel((PDBG_STREAM) psParams->pvStream,psParams->ui32Level);

	return(IMG_TRUE);
}

IMG_UINT32 DBGDIOCDrivSetFrame(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	PDBG_IN_SETFRAME	psParams;

	psParams = (PDBG_IN_SETFRAME) pvInBuffer;
	PVR_UNREFERENCED_PARAMETER(pvOutBuffer);

	ExtDBGDrivSetFrame((PDBG_STREAM) psParams->pvStream,psParams->ui32Frame);

	return(IMG_TRUE);
}

IMG_UINT32 DBGDIOCDrivGetFrame(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	IMG_UINT32 *		pStream;
	PDBG_STREAM	psStream;
	IMG_UINT32 *		pui32Current;

	pStream = (IMG_UINT32 *) pvInBuffer;
	psStream = (PDBG_STREAM) *pStream;
	pui32Current = (IMG_UINT32 *) pvOutBuffer;

	*pui32Current = ExtDBGDrivGetFrame(psStream);

	return(IMG_TRUE);
}

IMG_UINT32 DBGDIOCDrivIsCaptureFrame(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	PDBG_IN_ISCAPTUREFRAME psParams;
	IMG_UINT32 *		pui32Current;

	psParams = (PDBG_IN_ISCAPTUREFRAME) pvInBuffer;
	pui32Current = (IMG_UINT32 *) pvOutBuffer;

	*pui32Current = ExtDBGDrivIsCaptureFrame((PDBG_STREAM) psParams->pvStream, psParams->bCheckPreviousFrame);

	return(IMG_TRUE);
}

IMG_UINT32 DBGDIOCDrivOverrideMode(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	PDBG_IN_OVERRIDEMODE	psParams;

	psParams = (PDBG_IN_OVERRIDEMODE) pvInBuffer;
	PVR_UNREFERENCED_PARAMETER(	pvOutBuffer);

	ExtDBGDrivOverrideMode((PDBG_STREAM) psParams->pvStream,psParams->ui32Mode);

	return(IMG_TRUE);
}

IMG_UINT32 DBGDIOCDrivDefaultMode(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	IMG_UINT32 *		pStream;
	PDBG_STREAM	psStream;

	pStream = (IMG_UINT32 *) pvInBuffer;
	psStream = (PDBG_STREAM) *pStream;

	PVR_UNREFERENCED_PARAMETER(pvOutBuffer);

	ExtDBGDrivDefaultMode(psStream);

	return(IMG_TRUE);
}

IMG_UINT32 DBGDIOCDrivSetMarker(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	PDBG_IN_SETMARKER	psParams;

	psParams = (PDBG_IN_SETMARKER) pvInBuffer;
	PVR_UNREFERENCED_PARAMETER(pvOutBuffer);

	ExtDBGDrivSetMarker((PDBG_STREAM) psParams->pvStream, psParams->ui32Marker);

	return(IMG_TRUE);
}

IMG_UINT32 DBGDIOCDrivGetMarker(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	IMG_UINT32 *		pStream;
	PDBG_STREAM	psStream;
	IMG_UINT32 *		pui32Current;

	pStream = (IMG_UINT32 *) pvInBuffer;
	psStream = (PDBG_STREAM) *pStream;
	pui32Current = (IMG_UINT32 *) pvOutBuffer;

	*pui32Current = ExtDBGDrivGetMarker(psStream);

	return(IMG_TRUE);
}

IMG_UINT32 DBGDIOCDrivGetServiceTable(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	IMG_UINT32 *	pui32Out;

	PVR_UNREFERENCED_PARAMETER(pvInBuffer);
	pui32Out = (IMG_UINT32 *) pvOutBuffer;

	*pui32Out = DBGDrivGetServiceTable();

    return(IMG_TRUE);
}

IMG_UINT32 DBGDIOCDrivWriteLF(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	PDBG_IN_WRITE_LF	psInParams;
	IMG_UINT32 *				pui32BytesCopied;

	psInParams = (PDBG_IN_WRITE_LF) pvInBuffer;
	pui32BytesCopied = (IMG_UINT32 *) pvOutBuffer;

	*pui32BytesCopied = ExtDBGDrivWriteLF(psInParams->pvStream,
										psInParams->pui8InBuffer,
										psInParams->ui32BufferSize,
										psInParams->ui32Level,
										psInParams->ui32Flags);

	return IMG_TRUE;
}

IMG_UINT32 DBGDIOCDrivReadLF(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	IMG_UINT32 *				pui32BytesCopied;
	PDBG_IN_READ		psInParams;

	psInParams = (PDBG_IN_READ) pvInBuffer;
	pui32BytesCopied = (IMG_UINT32 *) pvOutBuffer;

	*pui32BytesCopied = ExtDBGDrivReadLF((PDBG_STREAM) psInParams->pvStream,psInParams->ui32OutBufferSize,psInParams->pui8OutBuffer);

	return(IMG_TRUE);
}

IMG_UINT32 DBGDIOCDrivResetStream(IMG_VOID * pvInBuffer, IMG_VOID * pvOutBuffer)
{
	IMG_UINT32 *				pui32BytesCopied;
	PDBG_IN_READ		psInParams;

	psInParams = (PDBG_IN_READ) pvInBuffer;
	pui32BytesCopied = (IMG_UINT32 *) pvOutBuffer;

	*pui32BytesCopied = ExtDBGDrivReadLF((PDBG_STREAM) psInParams->pvStream,psInParams->ui32OutBufferSize,psInParams->pui8OutBuffer);

	return(IMG_TRUE);
}
