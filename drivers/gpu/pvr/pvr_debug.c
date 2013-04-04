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

#ifndef AUTOCONF_INCLUDED
#include <linux/config.h>
#endif

#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/tty.h>
#include <stdarg.h>
#include "img_types.h"
#include "pvr_debug.h"
#include "proc.h"

#if defined(DEBUG) || defined(TIMING)

u32 gPVRDebugLevel = DBGPRIV_WARNING;

#define PVR_STRING_TERMINATOR		'\0'
#define PVR_IS_FILE_SEPARATOR(character) \
	(((character) == '\\') || ((character) == '/'))

void PVRSRVDebugPrintf(u32 ui32DebugLevel,
		       const char *pszFileName,
		       u32 ui32Line, const char *pszFormat, ...
    )
{
	IMG_BOOL bTrace, bDebug;
	char *pszLeafName;

	pszLeafName = (char *)strrchr(pszFileName, '\\');

	if (pszLeafName)
		pszFileName = pszLeafName;

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

		if (!bTrace)
			sprintf(&szBuffer[strlen(szBuffer)], " [%d, %s]",
				(int)ui32Line, pszFileName);

		printk(KERN_INFO "%s\n", szBuffer);

		va_end(vaArgs);
	}
}

void PVRSRVDebugAssertFail(const char *pszFile, u32 uLine)
{
	PVRSRVDebugPrintf(DBGPRIV_FATAL, pszFile, uLine,
			  "Debug assertion failed!");
	BUG();
}

void PVRSRVTrace(const char *pszFormat, ...)
{
	static char szMessage[PVR_MAX_DEBUG_MESSAGE_LEN + 1];
	char *pszEndOfMessage = NULL;
	va_list ArgList;

	strncpy(szMessage, "PVR: ", PVR_MAX_DEBUG_MESSAGE_LEN);

	pszEndOfMessage = &szMessage[strlen(szMessage)];

	va_start(ArgList, pszFormat);
	vsprintf(pszEndOfMessage, pszFormat, ArgList);
	va_end(ArgList);

	strcat(szMessage, "\n");

	printk(KERN_INFO "%s", szMessage);
}

void PVRDebugSetLevel(u32 uDebugLevel)
{
	printk(KERN_INFO "PVR: Setting Debug Level = 0x%x\n",
	       (unsigned int)uDebugLevel);

	gPVRDebugLevel = uDebugLevel;
}

int PVRDebugProcSetLevel(struct file *file, const char __user *buffer,
			 unsigned long count, void *data)
{
#define	_PROC_SET_BUFFER_SZ		2
	char data_buffer[_PROC_SET_BUFFER_SZ];

	if (count != _PROC_SET_BUFFER_SZ) {
		return -EINVAL;
	} else {
		if (copy_from_user(data_buffer, buffer, count))
			return -EINVAL;
		if (data_buffer[count - 1] != '\n')
			return -EINVAL;
		PVRDebugSetLevel(data_buffer[0] - '0');
	}
	return count;
}

int PVRDebugProcGetLevel(char *page, char **start, off_t off, int count,
			 int *eof, void *data)
{
	if (off == 0) {
		*start = (char *)1;
		return printAppend(page, count, 0, "%u\n", gPVRDebugLevel);
	}
	*eof = 1;
	return 0;
}

#endif
