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

#include <linux/version.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/module.h>

#include "img_defs.h"
#include "services.h"

#include "mutex.h"

void LinuxInitMutex(struct mutex *psPVRSRVMutex)
{
	mutex_init(psPVRSRVMutex);
}

void LinuxLockMutex(struct mutex *psPVRSRVMutex)
{
	mutex_lock(psPVRSRVMutex);
}

enum PVRSRV_ERROR LinuxLockMutexInterruptible(struct mutex *psPVRSRVMutex)
{
	if (mutex_lock_interruptible(psPVRSRVMutex) == -EINTR)
		return PVRSRV_ERROR_GENERIC;
	else
		return PVRSRV_OK;
}

s32 LinuxTryLockMutex(struct mutex *psPVRSRVMutex)
{
	return mutex_trylock(psPVRSRVMutex);
}

void LinuxUnLockMutex(struct mutex *psPVRSRVMutex)
{
	mutex_unlock(psPVRSRVMutex);
}

IMG_BOOL LinuxIsLockedMutex(struct mutex *psPVRSRVMutex)
{
	return mutex_is_locked(psPVRSRVMutex);
}

