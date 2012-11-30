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

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>

#include <linux/platform_device.h>

#include "img_defs.h"
#include "services.h"
#include "kerneldisplay.h"
#include "kernelbuffer.h"
#include "syscommon.h"
#include "pvrmmap.h"
#include "mm.h"
#include "mmap.h"
#include "mutex.h"
#include "pvr_debug.h"
#include "srvkm.h"
#include "perproc.h"
#include "handle.h"
#include "pvr_bridge_km.h"
#include "proc.h"
#include "pvrmodule.h"
#include "omaplfb.h"

/* omaplfb.h defines these, but we use our own */
#undef DRVNAME
#undef DEVNAME

#define DRVNAME		"pvrsrvkm"
#define DEVNAME		"pvrsrvkm"

MODULE_SUPPORTED_DEVICE(DEVNAME);
#ifdef DEBUG
static int debug = DBGPRIV_WARNING;
#include <linux/moduleparam.h>
module_param(debug, int, 0);
#endif

static int AssignedMajorNumber;

static int PVRSRVOpen(struct inode *pInode, struct file *pFile);
static int PVRSRVRelease(struct inode *pInode, struct file *pFile);

struct mutex gPVRSRVLock;

const static struct file_operations pvrsrv_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= PVRSRV_BridgeDispatchKM,
	.open		= PVRSRVOpen,
	.release	= PVRSRVRelease,
	.mmap		= PVRMMap,
};

#define	LDM_DEV	struct platform_device
#define	LDM_DRV	struct platform_driver

static int PVRSRVDriverRemove(LDM_DEV *device);
static int PVRSRVDriverProbe(LDM_DEV *device);
static int PVRSRVDriverSuspend(LDM_DEV *device, pm_message_t state);
static void PVRSRVDriverShutdown(LDM_DEV *device);
static int PVRSRVDriverResume(LDM_DEV *device);


static LDM_DRV powervr_driver = {
	.driver = {
		   .name = DRVNAME,
		   },
	.probe		= PVRSRVDriverProbe,
	.remove		= PVRSRVDriverRemove,
	.suspend	= PVRSRVDriverSuspend,
	.resume		= PVRSRVDriverResume,
	.shutdown	= PVRSRVDriverShutdown,
};

static LDM_DEV *gpsPVRLDMDev;

static void PVRSRVDeviceRelease(struct device *device);

static struct platform_device powervr_device = {
	.name = DEVNAME,
	.id = -1,
	.dev = {
		.release = PVRSRVDeviceRelease}
};

static int PVRSRVDriverProbe(LDM_DEV *pDevice)
{
	struct SYS_DATA *psSysData;

	PVR_TRACE("PVRSRVDriverProbe(pDevice=%p)", pDevice);

	dev_set_drvdata(&pDevice->dev, NULL);


	if (SysAcquireData(&psSysData) != PVRSRV_OK) {
		gpsPVRLDMDev = pDevice;

		if (SysInitialise() != PVRSRV_OK)
			return -ENODEV;
	}

	return 0;
}

static int PVRSRVDriverRemove(LDM_DEV *pDevice)
{
	struct SYS_DATA *psSysData;

	PVR_TRACE("PVRSRVDriverRemove(pDevice=%p)", pDevice);

	if (SysAcquireData(&psSysData) == PVRSRV_OK) {
		SysDeinitialise(psSysData);

		gpsPVRLDMDev = NULL;
	}

	return 0;
}

static void PVRSRVDriverShutdown(LDM_DEV *pDevice)
{
	PVR_TRACE("PVRSRVDriverShutdown(pDevice=%p)", pDevice);

	(void)PVRSRVSetPowerStateKM(PVRSRV_POWER_STATE_D3);
}

static int PVRSRVDriverSuspend(LDM_DEV *pDevice, pm_message_t state)
{
	PVR_TRACE("PVRSRVDriverSuspend(pDevice=%p)", pDevice);

	if (PVRSRVSetPowerStateKM(PVRSRV_POWER_STATE_D3) != PVRSRV_OK)
		return -EINVAL;
	return 0;
}

static int PVRSRVDriverResume(LDM_DEV *pDevice)
{
	PVR_TRACE("PVRSRVDriverResume(pDevice=%p)", pDevice);

	if (PVRSRVSetPowerStateKM(PVRSRV_POWER_STATE_D0) != PVRSRV_OK)
		return -EINVAL;
	return 0;
}

static void PVRSRVDeviceRelease(struct device *pDevice)
{
	PVR_DPF(PVR_DBG_WARNING, "PVRSRVDeviceRelease(pDevice=%p)", pDevice);
}

static int PVRSRVOpen(struct inode unref__ * pInode,
		      struct file unref__ * pFile)
{
	int Ret = 0;

	LinuxLockMutex(&gPVRSRVLock);

	if (PVRSRVProcessConnect(OSGetCurrentProcessIDKM()) != PVRSRV_OK)
		Ret = -ENOMEM;

	LinuxUnLockMutex(&gPVRSRVLock);

	return Ret;
}

static int PVRSRVRelease(struct inode unref__ * pInode,
			 struct file unref__ * pFile)
{
	int Ret = 0;

	LinuxLockMutex(&gPVRSRVLock);

	PVRSRVProcessDisconnect(OSGetCurrentProcessIDKM());

	LinuxUnLockMutex(&gPVRSRVLock);

	return Ret;
}

static int __init PVRCore_Init(void)
{
	int error;

	PVR_TRACE("PVRCore_Init");

	AssignedMajorNumber = register_chrdev(0, DEVNAME, &pvrsrv_fops);

	if (AssignedMajorNumber <= 0) {
		PVR_DPF(PVR_DBG_ERROR,
			 "PVRCore_Init: unable to get major number");

		return -EBUSY;
	}

	PVR_TRACE("PVRCore_Init: major device %d", AssignedMajorNumber);

	if (CreateProcEntries()) {
		unregister_chrdev(AssignedMajorNumber, DRVNAME);

		return -ENOMEM;
	}

	LinuxInitMutex(&gPVRSRVLock);

#ifdef DEBUG
	PVRDebugSetLevel(debug);
#endif

	if (LinuxMMInit() != PVRSRV_OK) {
		error = -ENOMEM;
		goto init_failed;
	}

	LinuxBridgeInit();

	PVRMMapInit();

	error = platform_driver_register(&powervr_driver);
	if (error != 0) {
		PVR_DPF(PVR_DBG_ERROR, "PVRCore_Init: "
			"unable to register platform driver (%d)", error);

		goto init_failed;
	}

	powervr_device.dev.devt = MKDEV(AssignedMajorNumber, 0);

	error = platform_device_register(&powervr_device);
	if (error != 0) {
		platform_driver_unregister(&powervr_driver);

		PVR_DPF(PVR_DBG_ERROR, "PVRCore_Init: "
			"unable to register platform device (%d)", error);

		goto init_failed;
	}


	return 0;

init_failed:

	PVRMMapCleanup();
	LinuxMMCleanup();
	RemoveProcEntries();
	unregister_chrdev(AssignedMajorNumber, DRVNAME);

	return error;

}

static void __exit PVRCore_Cleanup(void)
{
	struct SYS_DATA *psSysData;

	PVR_TRACE("PVRCore_Cleanup");

	SysAcquireData(&psSysData);

	unregister_chrdev(AssignedMajorNumber, DRVNAME);

	platform_device_unregister(&powervr_device);
	platform_driver_unregister(&powervr_driver);

	PVRMMapCleanup();
	LinuxMMCleanup();
	LinuxBridgeDeInit();
	RemoveProcEntries();

	PVR_TRACE("PVRCore_Cleanup: unloading");
}

module_init(PVRCore_Init);
module_exit(PVRCore_Cleanup);
