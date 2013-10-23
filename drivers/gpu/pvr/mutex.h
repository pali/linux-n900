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

#ifndef __INCLUDED_LINUX_MUTEX_H_
#define __INCLUDED_LINUX_MUTEX_H_

#include <linux/version.h>

#include <linux/mutex.h>


typedef struct mutex PVRSRV_LINUX_MUTEX;


extern IMG_VOID LinuxInitMutex(PVRSRV_LINUX_MUTEX * psPVRSRVMutex);

extern IMG_VOID LinuxLockMutex(PVRSRV_LINUX_MUTEX * psPVRSRVMutex);

extern PVRSRV_ERROR LinuxLockMutexInterruptible(PVRSRV_LINUX_MUTEX *
						psPVRSRVMutex);

extern IMG_INT32 LinuxTryLockMutex(PVRSRV_LINUX_MUTEX * psPVRSRVMutex);

extern IMG_VOID LinuxUnLockMutex(PVRSRV_LINUX_MUTEX * psPVRSRVMutex);

extern IMG_BOOL LinuxIsLockedMutex(PVRSRV_LINUX_MUTEX * psPVRSRVMutex);

#endif
