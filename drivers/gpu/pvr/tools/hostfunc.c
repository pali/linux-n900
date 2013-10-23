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

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <asm/page.h>
#include <linux/vmalloc.h>

#include "img_types.h"
#include "pvr_debug.h"

IMG_UINT32 gPVRDebugLevel = DBGPRIV_WARNING;

#define PVR_STRING_TERMINATOR		'\0'
#define PVR_IS_FILE_SEPARATOR(character) ( ((character) == '\\') || ((character) == '/') )

void PVRSRVDebugPrintf(IMG_UINT32 ui32DebugLevel,
		       const IMG_CHAR * pszFileName,
		       IMG_UINT32 ui32Line, const IMG_CHAR * pszFormat, ...
    )
{
	IMG_BOOL bTrace, bDebug;
	IMG_CHAR *pszLeafName;

	pszLeafName = (char *)strrchr(pszFileName, '\\');

	if (pszLeafName) {
		pszFileName = pszLeafName;
	}

	bTrace = gPVRDebugLevel & ui32DebugLevel & DBGPRIV_CALLTRACE;
	bDebug = ((gPVRDebugLevel & DBGPRIV_ALLLEVELS) >= ui32DebugLevel);

	if (bTrace || bDebug) {
		va_list vaArgs;
		static char szBuffer[256];

		va_start(vaArgs, pszFormat);

		if (bDebug) {
			switch (ui32DebugLevel) {
			case DBGPRIV_FATAL:
				{
					strcpy(szBuffer, "PVR_K:(Fatal): ");
					break;
				}
			case DBGPRIV_ERROR:
				{
					strcpy(szBuffer, "PVR_K:(Error): ");
					break;
				}
			case DBGPRIV_WARNING:
				{
					strcpy(szBuffer, "PVR_K:(Warning): ");
					break;
				}
			case DBGPRIV_MESSAGE:
				{
					strcpy(szBuffer, "PVR_K:(Message): ");
					break;
				}
			case DBGPRIV_VERBOSE:
				{
					strcpy(szBuffer, "PVR_K:(Verbose): ");
					break;
				}
			default:
				{
					strcpy(szBuffer,
					       "PVR_K:(Unknown message level)");
					break;
				}
			}
		} else {
			strcpy(szBuffer, "PVR_K: ");
		}

		vsprintf(&szBuffer[strlen(szBuffer)], pszFormat, vaArgs);

		if (!bTrace) {
			sprintf(&szBuffer[strlen(szBuffer)], " [%d, %s]",
				(int)ui32Line, pszFileName);
		}

		printk(KERN_INFO "%s\r\n", szBuffer);

		va_end(vaArgs);
	}
}

IMG_VOID HostMemSet(IMG_VOID * pvDest, IMG_UINT8 ui8Value, IMG_UINT32 ui32Size)
{
	memset(pvDest, (int)ui8Value, (size_t) ui32Size);
}

IMG_VOID HostMemCopy(IMG_VOID * pvDst, IMG_VOID * pvSrc, IMG_UINT32 ui32Size)
{
	memcpy(pvDst, pvSrc, ui32Size);
}

IMG_UINT32 HostReadRegistryDWORDFromString(char *pcKey, char *pcValueName,
					   IMG_UINT32 * pui32Data)
{

	return 0;
}

IMG_VOID *HostPageablePageAlloc(IMG_UINT32 ui32Pages)
{
	return (void *)vmalloc(ui32Pages * PAGE_SIZE);
}

IMG_VOID HostPageablePageFree(IMG_VOID * pvBase)
{
	vfree(pvBase);
}

IMG_VOID *HostNonPageablePageAlloc(IMG_UINT32 ui32Pages)
{
	return (void *)vmalloc(ui32Pages * PAGE_SIZE);
}

IMG_VOID HostNonPageablePageFree(IMG_VOID * pvBase)
{
	vfree(pvBase);
}

IMG_VOID *HostMapKrnBufIntoUser(IMG_VOID * pvKrnAddr, IMG_UINT32 ui32Size,
				IMG_VOID ** ppvMdl)
{

	return IMG_NULL;
}

IMG_VOID HostUnMapKrnBufFromUser(IMG_VOID * pvUserAddr, IMG_VOID * pvMdl,
				 IMG_VOID * pvProcess)
{

}

IMG_VOID HostCreateRegDeclStreams(IMG_VOID)
{

}

IMG_VOID *HostCreateMutex(IMG_VOID)
{

	return IMG_NULL;
}

IMG_VOID HostAquireMutex(IMG_VOID * pvMutex)
{

}

IMG_VOID HostReleaseMutex(IMG_VOID * pvMutex)
{

}

IMG_VOID HostDestroyMutex(IMG_VOID * pvMutex)
{

}
