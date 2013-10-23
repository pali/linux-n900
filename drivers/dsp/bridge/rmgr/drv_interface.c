/*
 * drv_interface.c
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
 *  ======== linux_driver.c ========
 *  Description:
 *      DSP/BIOS Bridge driver interface.
 *
 *  Public Functions:
 *      driver_init
 *      driver_exit
 *      driver_open
 *      driver_release
 *      driver_ioctl
 *      driver_mmap
 *
 *! Revision History
 *! ================
 *! 21-Apr-2004 map   Deprecated use of MODULE_PARM for kernel versions
 *!		   greater than 2.5, use module_param.
 *! 08-Mar-2004 sb    Added the dsp_debug argument, which keeps the DSP in self
 *!		   loop after image load and waits in a loop for DSP to start
 *! 16-Feb-2004 vp    Deprecated the usage of MOD_INC_USE_COUNT and
 *! 						MOD_DEC_USE_COUNT
 *!		   for kernel versions greater than 2.5
 *! 20-May-2003 vp    Added unregister functions for the DPM.
 *! 24-Mar-2003 sb    Pass pid instead of driverContext to DSP_Close
 *! 24-Mar-2003 vp    Added Power Management support.
 *! 21-Mar-2003 sb    Configure SHM size using insmod argument shm_size
 *! 10-Feb-2003 vp    Updated based on code review comments
 *! 18-Oct-2002 sb    Created initial version
 */

/*  ----------------------------------- Host OS */

#include <dspbridge/host_os.h>
#include <linux/platform_device.h>
#include <linux/pm.h>

#ifdef MODULE
#include <linux/module.h>
#endif

#include <linux/device.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/cdev.h>

#include <mach/board-3430sdp.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>
#include <dspbridge/dbdefs.h>
#include <dspbridge/errbase.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/gt.h>
#include <dspbridge/dbc.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/services.h>
#include <dspbridge/sync.h>
#include <dspbridge/reg.h>
#include <dspbridge/csl.h>

/*  ----------------------------------- Platform Manager */
#include <dspbridge/wcdioctl.h>
#include <dspbridge/_dcd.h>
#include <dspbridge/dspdrv.h>
#include <dspbridge/dbreg.h>

/*  ----------------------------------- Resource Manager */
#include <dspbridge/pwr.h>

/*  ----------------------------------- This */
#include <drv_interface.h>

#ifndef RES_CLEANUP_DISABLE
#include <dspbridge/cfg.h>
#include <dspbridge/resourcecleanup.h>
#include <dspbridge/chnl.h>
#include <dspbridge/proc.h>
#include <dspbridge/cfg.h>
#include <dspbridge/dev.h>
#include <dspbridge/drvdefs.h>
#include <dspbridge/drv.h>
#include <dspbridge/dbreg.h>
#endif

#include <mach/omap-pm.h>
#include <mach-omap2/omap3-opp.h>

#define BRIDGE_NAME "C6410"
/*  ----------------------------------- Globals */
#define DRIVER_NAME  "DspBridge"
#define DRIVER_MAJOR 0		/* Linux assigns our Major device number */
#define DRIVER_MINOR 0		/* Linux assigns our Major device number */
s32 dsp_debug;

struct platform_device *omap_dspbridge_dev;

struct bridge_dev {
	struct cdev cdev;
};

static struct bridge_dev *bridge_device;

static struct class *bridge_class;

static u32 driverContext;
#ifdef CONFIG_BRIDGE_DEBUG
static char *GT_str;
#endif /* CONFIG_BRIDGE_DEBUG */
static s32 driver_major = DRIVER_MAJOR;
static s32 driver_minor = DRIVER_MINOR;
static char *base_img;
char *iva_img;
static char *num_procs = "C55=1";
static s32 shm_size = 0x400000;	/* 4 MB */
static u32 phys_mempool_base;
static u32 phys_mempool_size;
static int tc_wordswapon;	/* Default value is always false */

/* Minimum ACTIVE VDD1 OPP level for reliable DSP operation */
unsigned short min_active_opp = 3;

#ifdef CONFIG_PM
struct omap34xx_bridge_suspend_data {
	int suspended;
	wait_queue_head_t suspend_wq;
};

static struct omap34xx_bridge_suspend_data bridge_suspend_data;

static int omap34xxbridge_suspend_lockout(
		struct omap34xx_bridge_suspend_data *s, struct file *f)
{
	if ((s)->suspended) {
		if ((f)->f_flags & O_NONBLOCK)
			return DSP_EDPMSUSPEND;
		wait_event_interruptible((s)->suspend_wq, (s)->suspended == 0);
	}
	return 0;
}

#endif

#ifdef DEBUG
module_param(GT_str, charp, 0);
MODULE_PARM_DESC(GT_str, "GT string, default = NULL");

module_param(dsp_debug, int, 0);
MODULE_PARM_DESC(dsp_debug, "Wait after loading DSP image. default = false");
#endif

module_param(driver_major, int, 0);	/* Driver's major number */
MODULE_PARM_DESC(driver_major, "Major device number, default = 0 (auto)");

module_param(driver_minor, int, 0);	/* Driver's major number */
MODULE_PARM_DESC(driver_minor, "Minor device number, default = 0 (auto)");

module_param(base_img, charp, 0);
MODULE_PARM_DESC(base_img, "DSP base image, default = NULL");

module_param(shm_size, int, 0);
MODULE_PARM_DESC(shm_size, "SHM size, default = 4 MB, minimum = 64 KB");

module_param(phys_mempool_base, uint, 0);
MODULE_PARM_DESC(phys_mempool_base,
		"Physical memory pool base passed to driver");

module_param(phys_mempool_size, uint, 0);
MODULE_PARM_DESC(phys_mempool_size,
		"Physical memory pool size passed to driver");
module_param(tc_wordswapon, int, 0);
MODULE_PARM_DESC(tc_wordswapon, "TC Word Swap Option. default = 0");

module_param(min_active_opp, ushort, S_IRUSR | S_IWUSR);
MODULE_PARM_DESC(min_active_opp, "Minimum ACTIVE VDD1 OPP Level, default = 3");

MODULE_AUTHOR("Texas Instruments");
MODULE_LICENSE("GPL");

static char *driver_name = DRIVER_NAME;

#ifdef CONFIG_BRIDGE_DEBUG
static struct GT_Mask driverTrace;
#endif /* CONFIG_BRIDGE_DEBUG */

static struct file_operations bridge_fops = {
	.open		= bridge_open,
	.release	= bridge_release,
	.unlocked_ioctl	= bridge_ioctl,
	.mmap		= bridge_mmap,
};

#ifdef CONFIG_PM
static u32 timeOut = 1000;
#ifdef CONFIG_BRIDGE_DVFS
static struct clk *clk_handle;
s32 dsp_max_opps = VDD1_OPP5;
#endif

/* Maximum Opps that can be requested by IVA*/
/*vdd1 rate table*/
#ifdef CONFIG_BRIDGE_DVFS
const struct omap_opp  vdd1_rate_table_bridge[] = {
	{0, 0, 0},
	/*OPP1*/
	{S125M, VDD1_OPP1, 0},
	/*OPP2*/
	{S250M, VDD1_OPP2, 0},
	/*OPP3*/
	{S500M, VDD1_OPP3, 0},
	/*OPP4*/
	{S550M, VDD1_OPP4, 0},
	/*OPP5*/
	{S600M, VDD1_OPP5, 0},
};
#endif
#endif

struct dspbridge_platform_data *omap_dspbridge_pdata;

u32 vdd1_dsp_freq[6][4] = {
	{0, 0, 0, 0},
	/*OPP1*/
	{0, 90000, 0, 86000},
	/*OPP2*/
	{0, 180000, 80000, 170000},
	/*OPP3*/
	{0, 360000, 160000, 340000},
	/*OPP4*/
	{0, 396000, 325000, 376000},
	/*OPP5*/
	{0, 430000, 355000, 430000},
};

#ifdef CONFIG_BRIDGE_DVFS
static int dspbridge_post_scale(struct notifier_block *op, unsigned long level,
				void *ptr)
{
	PWR_PM_PostScale(PRCM_VDD1, level);
	return 0;
}

static struct notifier_block iva_clk_notifier = {
	.notifier_call = dspbridge_post_scale,
	NULL,
};
#endif

static int __devinit omap34xx_bridge_probe(struct platform_device *pdev)
{
	int status;
	u32 initStatus;
	u32 temp;
	dev_t   dev = 0 ;
	int     result;
#ifdef CONFIG_BRIDGE_DVFS
	int i = 0;
#endif
	struct dspbridge_platform_data *pdata = pdev->dev.platform_data;

	omap_dspbridge_dev = pdev;

	/* use 2.6 device model */
	if (driver_major) {
		dev = MKDEV(driver_major, driver_minor);
		result = register_chrdev_region(dev, 1, driver_name);
	} else {
		result = alloc_chrdev_region(&dev, driver_minor, 1,
					    driver_name);
		driver_major = MAJOR(dev);
	}

	if (result < 0) {
		GT_1trace(driverTrace, GT_7CLASS, "bridge_init: "
				"Can't get Major %d \n", driver_major);
		return result;
	}

	bridge_device = kmalloc(sizeof(struct bridge_dev), GFP_KERNEL);
	if (!bridge_device) {
		result = -ENOMEM;
		unregister_chrdev_region(dev, 1);
		return result;
	}
	memset(bridge_device, 0, sizeof(struct bridge_dev));
	cdev_init(&bridge_device->cdev, &bridge_fops);
	bridge_device->cdev.owner = THIS_MODULE;
	bridge_device->cdev.ops = &bridge_fops;

	status = cdev_add(&bridge_device->cdev, dev, 1);

	if (status) {
		GT_0trace(driverTrace, GT_7CLASS,
				"Failed to add the bridge device \n");
		return status;
	}

	/* udev support */
	bridge_class = class_create(THIS_MODULE, "ti_bridge");

	if (IS_ERR(bridge_class))
		GT_0trace(driverTrace, GT_7CLASS,
				"Error creating bridge class \n");

	device_create(bridge_class, NULL, MKDEV(driver_major, driver_minor),
			NULL, "DspBridge");

	GT_init();
	GT_create(&driverTrace, "LD");

#ifdef DEBUG
	if (GT_str)
		GT_set(GT_str);
#elif defined(DDSP_DEBUG_PRODUCT) && GT_TRACE
	GT_set("**=67");
#endif

	GT_0trace(driverTrace, GT_ENTER, "-> driver_init\n");

#ifdef CONFIG_PM
	/* Initialize the wait queue */
	if (!status) {
		bridge_suspend_data.suspended = 0;
		init_waitqueue_head(&bridge_suspend_data.suspend_wq);
	}
#endif

	SERVICES_Init();

	/*  Autostart flag.  This should be set to true if the DSP image should
	 *  be loaded and run during bridge module initialization  */

	if (base_img) {
		temp = true;
		REG_SetValue(NULL, NULL, AUTOSTART, REG_DWORD, (u8 *)&temp,
			    sizeof(temp));
		REG_SetValue(NULL, NULL, DEFEXEC, REG_SZ, (u8 *)base_img,
						strlen(base_img) + 1);
	} else {
		temp = false;
		REG_SetValue(NULL, NULL, AUTOSTART, REG_DWORD, (u8 *)&temp,
			    sizeof(temp));
		REG_SetValue(NULL, NULL, DEFEXEC, REG_SZ, (u8 *) "\0", (u32)2);
	}
	REG_SetValue(NULL, NULL, NUMPROCS, REG_SZ, (u8 *) num_procs,
						strlen(num_procs) + 1);

	if (shm_size >= 0x10000) {	/* 64 KB */
		initStatus = REG_SetValue(NULL, NULL, SHMSIZE, REG_DWORD,
					  (u8 *)&shm_size, sizeof(shm_size));
	} else {
		initStatus = DSP_EINVALIDARG;
		status = -1;
		GT_0trace(driverTrace, GT_7CLASS,
			  "SHM size must be at least 64 KB\n");
	}
	GT_1trace(driverTrace, GT_7CLASS,
		 "requested shm_size = 0x%x\n", shm_size);

	if (pdata->phys_mempool_base && pdata->phys_mempool_size) {
		phys_mempool_base = pdata->phys_mempool_base;
		phys_mempool_size = pdata->phys_mempool_size;
	}

	if (phys_mempool_base > 0x0) {
		initStatus = REG_SetValue(NULL, NULL, PHYSMEMPOOLBASE,
					 REG_DWORD, (u8 *)&phys_mempool_base,
					 sizeof(phys_mempool_base));
	}
	GT_1trace(driverTrace, GT_7CLASS, "phys_mempool_base = 0x%x \n",
		 phys_mempool_base);

	if (phys_mempool_size > 0x0) {
		initStatus = REG_SetValue(NULL, NULL, PHYSMEMPOOLSIZE,
					 REG_DWORD, (u8 *)&phys_mempool_size,
					 sizeof(phys_mempool_size));
	}
	GT_1trace(driverTrace, GT_7CLASS, "phys_mempool_size = 0x%x\n",
		 phys_mempool_base);
	if ((phys_mempool_base > 0x0) && (phys_mempool_size > 0x0))
		MEM_ExtPhysPoolInit(phys_mempool_base, phys_mempool_size);
	if (tc_wordswapon) {
		GT_0trace(driverTrace, GT_7CLASS, "TC Word Swap is enabled\n");
		REG_SetValue(NULL, NULL, TCWORDSWAP, REG_DWORD,
			    (u8 *)&tc_wordswapon, sizeof(tc_wordswapon));
	} else {
		GT_0trace(driverTrace, GT_7CLASS, "TC Word Swap is disabled\n");
		REG_SetValue(NULL, NULL, TCWORDSWAP,
			    REG_DWORD, (u8 *)&tc_wordswapon,
			    sizeof(tc_wordswapon));
	}
	if (DSP_SUCCEEDED(initStatus)) {
#ifdef CONFIG_BRIDGE_DVFS
		for (i = 0; i < 6; i++)
			pdata->mpu_speed[i] = vdd1_rate_table_bridge[i].rate;

		clk_handle = clk_get(NULL, "iva2_ck");
		if (!clk_handle) {
			GT_0trace(driverTrace, GT_7CLASS,
			"clk_get failed to get iva2_ck \n");
		} else {
			GT_0trace(driverTrace, GT_7CLASS,
			"clk_get PASS to get iva2_ck \n");
		}
		if (!clk_notifier_register(clk_handle, &iva_clk_notifier)) {
			GT_0trace(driverTrace, GT_7CLASS,
			"clk_notifier_register PASS for iva2_ck \n");
		} else {
			GT_0trace(driverTrace, GT_7CLASS,
			"clk_notifier_register FAIL for iva2_ck \n");
		}

		/*
		 * When Smartreflex is ON, DSP requires at least OPP level 3
		 * to operate reliably. So boost lower OPP levels to OPP3.
		 */
		if (pdata->dsp_set_min_opp)
			(*pdata->dsp_set_min_opp)(min_active_opp);
#endif
		driverContext = DSP_Init(&initStatus);
		if (DSP_FAILED(initStatus)) {
			status = -1;
			GT_0trace(driverTrace, GT_7CLASS,
				 "DSP/BIOS Bridge initialization Failed\n");
		} else {
			GT_0trace(driverTrace, GT_5CLASS,
					"DSP/BIOS Bridge driver loaded\n");
		}
	}

	DBC_Assert(status == 0);
	DBC_Assert(DSP_SUCCEEDED(initStatus));
	GT_0trace(driverTrace, GT_ENTER, " <- driver_init\n");
	return status;
}

static int __devexit omap34xx_bridge_remove(struct platform_device *pdev)
{
	dev_t devno;
	bool ret;
	DSP_STATUS dsp_status = DSP_SOK;
	HANDLE	     hDrvObject = NULL;
	struct PROCESS_CONTEXT	*pTmp = NULL;
	struct PROCESS_CONTEXT    *pCtxtclosed = NULL;
	struct PROC_OBJECT *proc_obj_ptr, *temp;

	GT_0trace(driverTrace, GT_ENTER, "-> driver_exit\n");

	dsp_status = CFG_GetObject((u32 *)&hDrvObject, REG_DRV_OBJECT);
	if (DSP_FAILED(dsp_status))
		goto func_cont;

#ifdef CONFIG_BRIDGE_DVFS
	if (!clk_notifier_unregister(clk_handle, &iva_clk_notifier)) {
		GT_0trace(driverTrace, GT_7CLASS,
		"clk_notifier_unregister PASS for iva2_ck \n");
	} else {
		GT_0trace(driverTrace, GT_7CLASS,
		"clk_notifier_unregister FAILED for iva2_ck \n");
	}
#endif /* #ifdef CONFIG_BRIDGE_DVFS */

	DRV_GetProcCtxtList(&pCtxtclosed, (struct DRV_OBJECT *)hDrvObject);
	while (pCtxtclosed != NULL) {
		GT_1trace(driverTrace, GT_5CLASS, "***Cleanup of "
			 "process***%d\n", pCtxtclosed->pid);
		DRV_RemoveAllResources(pCtxtclosed);
		list_for_each_entry_safe(proc_obj_ptr, temp,
				&pCtxtclosed->processor_list, proc_object) {
			PROC_Detach(proc_obj_ptr, pCtxtclosed);
		}
		pTmp = pCtxtclosed->next;
		DRV_RemoveProcContext((struct DRV_OBJECT *)hDrvObject,
				pCtxtclosed);
		pCtxtclosed = pTmp;
	}

	if (driverContext) {
		/* Put the DSP in reset state */
		ret = DSP_Deinit(driverContext);
		driverContext = 0;
		DBC_Assert(ret == true);
	}

	clk_put(clk_handle);
	clk_handle = NULL;

func_cont:
	SERVICES_Exit();
	GT_exit();

	devno = MKDEV(driver_major, driver_minor);
	if (bridge_device) {
		cdev_del(&bridge_device->cdev);
		kfree(bridge_device);
	}
	unregister_chrdev_region(devno, 1);
	if (bridge_class) {
		/* remove the device from sysfs */
		device_destroy(bridge_class, MKDEV(driver_major, driver_minor));
		class_destroy(bridge_class);

	}
	return 0;
}


#ifdef CONFIG_PM
static int bridge_suspend(struct platform_device *pdev, pm_message_t state)
{
	u32 status;
	u32 command = PWR_EMERGENCYDEEPSLEEP;

	status = PWR_SleepDSP(command, timeOut);
	if (DSP_FAILED(status))
		return -1;

	bridge_suspend_data.suspended = 1;
	return 0;
}

static int bridge_resume(struct platform_device *pdev)
{
	u32 status;

	status = PWR_WakeDSP(timeOut);
	if (DSP_FAILED(status))
		return -1;

	bridge_suspend_data.suspended = 0;
	wake_up(&bridge_suspend_data.suspend_wq);
	return 0;
}
#else
#define bridge_suspend NULL
#define bridge_resume NULL
#endif

static struct platform_driver bridge_driver = {
	.driver = {
		.name = BRIDGE_NAME,
	},
	.probe	 = omap34xx_bridge_probe,
	.remove	 = __devexit_p(omap34xx_bridge_remove),
	.suspend = bridge_suspend,
	.resume	 = bridge_resume,
};

static int __init bridge_init(void)
{
	return platform_driver_register(&bridge_driver);
}

static void __exit bridge_exit(void)
{
	platform_driver_unregister(&bridge_driver);
}

/*
 * This function is called when an application opens handle to the
 * bridge driver.
 */
static int bridge_open(struct inode *ip, struct file *filp)
{
	int status = 0;
	DSP_STATUS dsp_status;
	HANDLE hDrvObject;
	struct PROCESS_CONTEXT *pr_ctxt = NULL;

	GT_0trace(driverTrace, GT_ENTER, "-> bridge_open\n");

	dsp_status = CFG_GetObject((u32 *)&hDrvObject, REG_DRV_OBJECT);
	if (DSP_SUCCEEDED(dsp_status)) {
		/*
		 * Allocate a new process context and insert it into global
		 * process context list.
		 */
		DRV_InsertProcContext(hDrvObject, &pr_ctxt);
		if (pr_ctxt) {
			DRV_ProcUpdatestate(pr_ctxt, PROC_RES_ALLOCATED);
			DRV_ProcSetPID(pr_ctxt, current->tgid);
		} else {
			status = -ENOMEM;
		}
	} else {
		status = -EIO;
	}

	filp->private_data = pr_ctxt;

	GT_0trace(driverTrace, GT_ENTER, "<- bridge_open\n");
	return status;
}

/*
 * This function is called when an application closes handle to the bridge
 * driver.
 */
static int bridge_release(struct inode *ip, struct file *filp)
{
	int status = 0;
	DSP_STATUS dsp_status;
	HANDLE hDrvObject;
	struct PROCESS_CONTEXT *pr_ctxt;
	struct PROC_OBJECT *proc_obj_ptr, *temp;

	GT_0trace(driverTrace, GT_ENTER, "-> bridge_release\n");

	if (!filp->private_data) {
		status = -EIO;
	} else {
		pr_ctxt = filp->private_data;
		dsp_status = CFG_GetObject((u32 *)&hDrvObject, REG_DRV_OBJECT);
		if (DSP_SUCCEEDED(dsp_status)) {
			flush_signals(current);
			DRV_RemoveAllResources(pr_ctxt);
			list_for_each_entry_safe(proc_obj_ptr, temp,
					&pr_ctxt->processor_list,
					proc_object) {
				PROC_Detach(proc_obj_ptr, pr_ctxt);
			}
			DRV_RemoveProcContext((struct DRV_OBJECT *)hDrvObject,
					pr_ctxt);
		} else {
			status = -EIO;
		}
		filp->private_data = NULL;
	}

	GT_0trace(driverTrace, GT_ENTER, "<- bridge_release\n");
	return status;
}

/* This function provides IO interface to the bridge driver. */
static long bridge_ioctl(struct file *filp, unsigned int code,
		unsigned long args)
{
	int status;
	u32 retval = DSP_SOK;
	union Trapped_Args pBufIn;

	DBC_Require(filp != NULL);
#ifdef CONFIG_PM
	status = omap34xxbridge_suspend_lockout(&bridge_suspend_data, filp);
	if (status != 0)
		return status;
#endif

	GT_0trace(driverTrace, GT_ENTER, " -> driver_ioctl\n");

	/* Deduct one for the CMD_BASE. */
	code = (code - 1);

	status = copy_from_user(&pBufIn, (union Trapped_Args *)args,
				sizeof(union Trapped_Args));

	if (status >= 0) {
		status = WCD_CallDevIOCtl(code, &pBufIn, &retval,
				filp->private_data);

		if (DSP_SUCCEEDED(status)) {
			status = retval;
		} else {
			GT_1trace(driverTrace, GT_7CLASS,
				 "IOCTL Failed, code : 0x%x\n", code);
			status = -1;
		}

	}

	GT_0trace(driverTrace, GT_ENTER, " <- driver_ioctl\n");

	return status;
}

/* This function maps kernel space memory to user space memory. */
static int bridge_mmap(struct file *filp, struct vm_area_struct *vma)
{
#if GT_TRACE
	u32 offset = vma->vm_pgoff << PAGE_SHIFT;
#endif
	u32 status;

	DBC_Assert(vma->vm_start < vma->vm_end);

	vma->vm_flags |= VM_RESERVED | VM_IO;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	GT_6trace(driverTrace, GT_3CLASS,
		 "vm filp %p offset %lx start %lx end %lx"
		 " page_prot %lx flags %lx\n", filp, offset, vma->vm_start,
		 vma->vm_end, vma->vm_page_prot, vma->vm_flags);

	status = remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
				vma->vm_end - vma->vm_start, vma->vm_page_prot);
	if (status != 0)
		status = -EAGAIN;

	return status;
}

#ifndef RES_CLEANUP_DISABLE
/* To remove all process resources before removing the process from the
 * process context list*/
DSP_STATUS DRV_RemoveAllResources(HANDLE hPCtxt)
{
	DSP_STATUS status = DSP_SOK;
	struct PROCESS_CONTEXT *pCtxt = (struct PROCESS_CONTEXT *)hPCtxt;
	if (pCtxt != NULL) {
		DRV_RemoveAllSTRMResElements(pCtxt);
		DRV_RemoveAllNodeResElements(pCtxt);
		DRV_RemoveAllDMMResElements(pCtxt);
		DRV_ProcUpdatestate(pCtxt, PROC_RES_FREED);
	}
	return status;
}
#endif

/* Bridge driver initialization and de-initialization functions */
module_init(bridge_init);
module_exit(bridge_exit);

