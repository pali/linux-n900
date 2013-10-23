/*
 * drv_interface.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * DSP/BIOS Bridge driver interface.
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

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>
#include <dspbridge/dbdefs.h>
#include <dspbridge/errbase.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/services.h>
#include <dspbridge/sync.h>
#include <dspbridge/reg.h>

/*  ----------------------------------- Platform Manager */
#include <dspbridge/wcdioctl.h>
#include <dspbridge/_dcd.h>
#include <dspbridge/dspdrv.h>

/*  ----------------------------------- Resource Manager */
#include <dspbridge/pwr.h>

/*  ----------------------------------- This */
#include <drv_interface.h>
#include <policy.h>

#include <dspbridge/cfg.h>
#include <dspbridge/resourcecleanup.h>
#include <dspbridge/chnl.h>
#include <dspbridge/proc.h>
#include <dspbridge/dev.h>
#include <dspbridge/drvdefs.h>
#include <dspbridge/drv.h>

#ifdef CONFIG_BRIDGE_DVFS
#include <mach-omap2/omap3-opp.h>
#endif

#define BRIDGE_NAME "C6410"
/*  ----------------------------------- Globals */
#define DRIVER_NAME  "DspBridge"
#define DSPBRIDGE_VERSION	"0.2"
s32 dsp_debug;

struct platform_device *omap_dspbridge_dev;
struct device *bridge;

/* This is a test variable used by Bridge to test different sleep states */
s32 dsp_test_sleepstate;

static struct cdev bridge_cdev;

static struct class *bridge_class;

static u32 driver_context;
static s32 driver_major;
static char *base_img;
char *iva_img;
static s32 shm_size = 0x500000;	/* 5 MB */
static u32 phys_mempool_base;
static u32 phys_mempool_size;
static int tc_wordswapon;	/* Default value is always false */
#ifdef CONFIG_BRIDGE_RECOVERY
#define REC_TIMEOUT 5000	/*recovery timeout in msecs */
static atomic_t bridge_cref;	/* number of bridge open handles */
static struct workqueue_struct *bridge_rec_queue;
static struct work_struct bridge_recovery_work;
static DECLARE_COMPLETION(bridge_comp);
static DECLARE_COMPLETION(bridge_open_comp);
static bool recover;
#endif

#ifdef CONFIG_PM
struct omap34_xx_bridge_suspend_data {
	int suspended;
	wait_queue_head_t suspend_wq;
};

static struct omap34_xx_bridge_suspend_data bridge_suspend_data;

static int omap34_xxbridge_suspend_lockout(struct omap34_xx_bridge_suspend_data
					   *s, struct file *f)
{
	if ((s)->suspended) {
		if ((f)->f_flags & O_NONBLOCK)
			return DSP_EDPMSUSPEND;
		wait_event_interruptible((s)->suspend_wq, (s)->suspended == 0);
	}
	return 0;
}
#endif

module_param(dsp_debug, int, 0);
MODULE_PARM_DESC(dsp_debug, "Wait after loading DSP image. default = false");

module_param(dsp_test_sleepstate, int, 0);
MODULE_PARM_DESC(dsp_test_sleepstate, "DSP Sleep state = 0");

module_param(base_img, charp, 0);
MODULE_PARM_DESC(base_img, "DSP base image, default = NULL");

module_param(shm_size, int, 0);
MODULE_PARM_DESC(shm_size, "shm size, default = 4 MB, minimum = 64 KB");

module_param(phys_mempool_base, uint, 0);
MODULE_PARM_DESC(phys_mempool_base,
		 "Physical memory pool base passed to driver");

module_param(phys_mempool_size, uint, 0);
MODULE_PARM_DESC(phys_mempool_size,
		 "Physical memory pool size passed to driver");
module_param(tc_wordswapon, int, 0);
MODULE_PARM_DESC(tc_wordswapon, "TC Word Swap Option. default = 0");

MODULE_AUTHOR("Texas Instruments");
MODULE_LICENSE("GPL");
MODULE_VERSION(DSPBRIDGE_VERSION);

static char *driver_name = DRIVER_NAME;

static struct file_operations bridge_fops = {
	.open = bridge_open,
	.release = bridge_release,
	.unlocked_ioctl = bridge_ioctl,
	.mmap = bridge_mmap,
};

#ifdef CONFIG_PM
static u32 time_out = 1000;
#ifdef CONFIG_BRIDGE_DVFS
static struct clk *clk_handle;
#endif
#endif

struct dspbridge_platform_data *omap_dspbridge_pdata;

#ifdef CONFIG_BRIDGE_RECOVERY
static void bridge_recover(struct work_struct *work)
{
	struct dev_object *dev;
	struct cfg_devnode *dev_node;
	if (atomic_read(&bridge_cref)) {
		INIT_COMPLETION(bridge_comp);
		while (!wait_for_completion_timeout(&bridge_comp,
						msecs_to_jiffies(REC_TIMEOUT)))
			pr_info("%s:%d handle(s) still opened\n",
					__func__, atomic_read(&bridge_cref));
	}
	dev = dev_get_first();
	dev_get_dev_node(dev, &dev_node);
	if (!dev_node || DSP_FAILED(proc_auto_start(dev_node, dev)))
		pr_err("DSP could not be restarted\n");
	recover = false;
	complete_all(&bridge_open_comp);
}

void bridge_recover_schedule(void)
{
	INIT_COMPLETION(bridge_open_comp);
	recover = true;
	queue_work(bridge_rec_queue, &bridge_recovery_work);
}
#endif
#ifdef CONFIG_BRIDGE_DVFS
static int dspbridge_scale_notification(struct notifier_block *op,
		unsigned long val, void *ptr)
{
	struct dspbridge_platform_data *pdata =
					omap_dspbridge_dev->dev.platform_data;
	struct cpufreq_freqs *freqs = ptr;
	int opp = 0;
	if (CPUFREQ_POSTCHANGE == val && pdata->dsp_get_opp) {
		switch (freqs->new) {
		case 1000000:
			opp++;
		case 800000:
			opp++;
		case 600000:
			opp++;
		default:
			opp++;
		}
		pwr_pm_post_scale(PRCM_VDD1, opp);
	}
	return 0;
}

static struct notifier_block iva_clk_notifier = {
	.notifier_call = dspbridge_scale_notification,
	NULL,
};
#endif

static int __devinit omap34_xx_bridge_probe(struct platform_device *pdev)
{
	int status;
	u32 init_status;
	u32 temp;
	dev_t dev = 0;
	int result;
	struct dspbridge_platform_data *pdata = pdev->dev.platform_data;

	omap_dspbridge_dev = pdev;

	/* Global bridge device */
	bridge = &omap_dspbridge_dev->dev;

	/* use 2.6 device model */
	result = alloc_chrdev_region(&dev, 0, 1, driver_name);
	if (result < 0) {
		pr_err("%s: Can't get major %d\n", __func__, driver_major);
		goto err1;
	}

	driver_major = MAJOR(dev);

	cdev_init(&bridge_cdev, &bridge_fops);
	bridge_cdev.owner = THIS_MODULE;

	status = cdev_add(&bridge_cdev, dev, 1);
	if (status) {
		pr_err("%s: Failed to add bridge device\n", __func__);
		goto err2;
	}

	/* udev support */
	bridge_class = class_create(THIS_MODULE, "ti_bridge");

	if (IS_ERR(bridge_class))
		pr_err("%s: Error creating bridge class\n", __func__);

	device_create(bridge_class, NULL, MKDEV(driver_major, 0),
		      NULL, "DspBridge");

#ifdef CONFIG_PM
	/* Initialize the wait queue */
	if (!status) {
		bridge_suspend_data.suspended = 0;
		init_waitqueue_head(&bridge_suspend_data.suspend_wq);
	}
#endif

	services_init();

	/*  Autostart flag.  This should be set to true if the DSP image should
	 *  be loaded and run during bridge module initialization */

	if (base_img) {
		temp = true;
		reg_set_value(AUTOSTART, (u8 *) &temp, sizeof(temp));
		reg_set_value(DEFEXEC, (u8 *) base_img, strlen(base_img) + 1);
	} else {
		temp = false;
		reg_set_value(AUTOSTART, (u8 *) &temp, sizeof(temp));
		reg_set_value(DEFEXEC, (u8 *) "\0", (u32) 2);
	}

	if (shm_size >= 0x10000) {	/* 64 KB */
		init_status = reg_set_value(SHMSIZE, (u8 *) &shm_size,
					    sizeof(shm_size));
	} else {
		init_status = DSP_EINVALIDARG;
		status = -1;
		pr_err("%s: shm size must be at least 64 KB\n", __func__);
	}
	dev_dbg(bridge, "%s: requested shm_size = 0x%x\n", __func__, shm_size);

	if (pdata->phys_mempool_base && pdata->phys_mempool_size) {
		phys_mempool_base = pdata->phys_mempool_base;
		phys_mempool_size = pdata->phys_mempool_size;
	}

	dev_dbg(bridge, "%s: phys_mempool_base = 0x%x \n", __func__,
		phys_mempool_base);

	dev_dbg(bridge, "%s: phys_mempool_size = 0x%x\n", __func__,
		phys_mempool_base);
	if ((phys_mempool_base > 0x0) && (phys_mempool_size > 0x0))
		mem_ext_phys_pool_init(phys_mempool_base, phys_mempool_size);
	if (tc_wordswapon) {
		dev_dbg(bridge, "%s: TC Word Swap is enabled\n", __func__);
		reg_set_value(TCWORDSWAP, (u8 *) &tc_wordswapon,
			      sizeof(tc_wordswapon));
	} else {
		dev_dbg(bridge, "%s: TC Word Swap is disabled\n", __func__);
		reg_set_value(TCWORDSWAP, (u8 *) &tc_wordswapon,
			      sizeof(tc_wordswapon));
	}
	if (DSP_SUCCEEDED(init_status)) {
#ifdef CONFIG_BRIDGE_DVFS
		clk_handle = clk_get(NULL, "iva2_ck");
		if (!clk_handle)
			pr_err("%s: clk_get failed to get iva2_ck\n", __func__);

		if (cpufreq_register_notifier(&iva_clk_notifier,
						CPUFREQ_TRANSITION_NOTIFIER))
		pr_err("%s: cpufreq_register_notifier failed for "
			"iva2_ck\n", __func__);
#endif
		driver_context = dsp_init(&init_status);
		if (DSP_FAILED(init_status)) {
			status = -1;
			pr_err("DSP Bridge driver initialization failed\n");
		} else {
			pr_info("DSP Bridge driver loaded\n");
		}
	}

#ifdef CONFIG_BRIDGE_RECOVERY
	bridge_rec_queue = create_workqueue("bridge_rec_queue");
	INIT_WORK(&bridge_recovery_work, bridge_recover);
	INIT_COMPLETION(bridge_comp);
#endif
	DBC_ASSERT(status == 0);
	DBC_ASSERT(DSP_SUCCEEDED(init_status));

	policy_init(bridge_class);

	return 0;

err2:
	unregister_chrdev_region(dev, 1);
err1:
	return result;
}

static int __devexit omap34_xx_bridge_remove(struct platform_device *pdev)
{
	dev_t devno;
	bool ret;
	dsp_status status = DSP_SOK;
	bhandle hdrv_obj = NULL;

	policy_remove();

	status = cfg_get_object((u32 *) &hdrv_obj, REG_DRV_OBJECT);
	if (DSP_FAILED(status))
		goto func_cont;

#ifdef CONFIG_BRIDGE_DVFS
	if (cpufreq_unregister_notifier(&iva_clk_notifier,
						CPUFREQ_TRANSITION_NOTIFIER))
		pr_err("%s: cpufreq_unregister_notifier failed for iva2_ck\n",
			__func__);
#endif /* #ifdef CONFIG_BRIDGE_DVFS */

	if (driver_context) {
		/* Put the DSP in reset state */
		ret = dsp_deinit(driver_context);
		driver_context = 0;
		DBC_ASSERT(ret == true);
	}
#ifdef CONFIG_BRIDGE_DVFS
	clk_put(clk_handle);
	clk_handle = NULL;
#endif /* #ifdef CONFIG_BRIDGE_DVFS */

func_cont:
	mem_ext_phys_pool_release();

	services_exit();

	devno = MKDEV(driver_major, 0);
	cdev_del(&bridge_cdev);
	unregister_chrdev_region(devno, 1);
	if (bridge_class) {
		/* remove the device from sysfs */
		device_destroy(bridge_class, MKDEV(driver_major, 0));
		class_destroy(bridge_class);

	}
	return 0;
}

#ifdef CONFIG_PM
static int BRIDGE_SUSPEND(struct platform_device *pdev, pm_message_t state)
{
	u32 status;
	u32 command = PWR_EMERGENCYDEEPSLEEP;

	status = pwr_sleep_dsp(command, time_out);
	if (DSP_FAILED(status))
		return -1;

	bridge_suspend_data.suspended = 1;
	return 0;
}

static int BRIDGE_RESUME(struct platform_device *pdev)
{
	u32 status;

	status = pwr_wake_dsp(time_out);
	if (DSP_FAILED(status))
		return -1;

	bridge_suspend_data.suspended = 0;
	wake_up(&bridge_suspend_data.suspend_wq);
	return 0;
}
#else
#define BRIDGE_SUSPEND NULL
#define BRIDGE_RESUME NULL
#endif

static struct platform_driver bridge_driver = {
	.driver = {
		   .name = BRIDGE_NAME,
		   },
	.probe = omap34_xx_bridge_probe,
	.remove = __devexit_p(omap34_xx_bridge_remove),
	.suspend = BRIDGE_SUSPEND,
	.resume = BRIDGE_RESUME,
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
	struct process_context *pr_ctxt = NULL;

#ifdef CONFIG_BRIDGE_RECOVERY
	if (recover) {
		if (filp->f_flags & O_NONBLOCK ||
			wait_for_completion_interruptible(&bridge_open_comp))
			return -EBUSY;
	}
#endif

	/*
	 * Allocate a new process context and insert it into global
	 * process context list.
	 */
	pr_ctxt = mem_calloc(sizeof(struct process_context), MEM_PAGED);
	if (pr_ctxt) {
		pr_ctxt->res_state = PROC_RES_ALLOCATED;
		spin_lock_init(&pr_ctxt->dmm_map_lock);
		INIT_LIST_HEAD(&pr_ctxt->dmm_map_list);
		spin_lock_init(&pr_ctxt->dmm_rsv_lock);
		INIT_LIST_HEAD(&pr_ctxt->dmm_rsv_list);
		mutex_init(&pr_ctxt->node_mutex);
		mutex_init(&pr_ctxt->strm_mutex);
	} else {
		return -ENOMEM;
	}

	filp->private_data = pr_ctxt;

	status = policy_open_hook(ip, filp);
	if (status) {
		kfree(pr_ctxt);
		filp->private_data = NULL;
		return status;
	}

#ifdef CONFIG_BRIDGE_RECOVERY
	atomic_inc(&bridge_cref);
#endif
	return status;
}

/*
 * This function is called when an application closes handle to the bridge
 * driver.
 */
static int bridge_release(struct inode *ip, struct file *filp)
{
	int status = 0;
	struct process_context *pr_ctxt;

	status = policy_release_hook(ip, filp);
	if (status)
		return status;

	if (!filp->private_data) {
		status = -EIO;
		goto err;
	}

	pr_ctxt = filp->private_data;
	flush_signals(current);
	drv_remove_all_resources(pr_ctxt);
	proc_detach(pr_ctxt);
	kfree(pr_ctxt);

	filp->private_data = NULL;

err:
#ifdef CONFIG_BRIDGE_RECOVERY
	if (!atomic_dec_return(&bridge_cref))
		complete(&bridge_comp);
#endif
	return status;
}

/* This function provides IO interface to the bridge driver. */
static long bridge_ioctl(struct file *filp, unsigned int code,
			 unsigned long args)
{
	int status;
	u32 retval = DSP_SOK;
	union Trapped_Args buf_in;

	status = policy_ioctl_pre_hook(filp, code, args);
	if (status)
		return status;

	DBC_REQUIRE(filp != NULL);
#ifdef CONFIG_BRIDGE_RECOVERY
	if (recover) {
		status = -EIO;
		goto err;
	}
#endif
#ifdef CONFIG_PM
	status = omap34_xxbridge_suspend_lockout(&bridge_suspend_data, filp);
	if (status != 0)
		return status;
#endif

	if (!filp->private_data) {
		status = -EIO;
		goto err;
	}

	status = copy_from_user(&buf_in, (union Trapped_Args *)args,
				sizeof(union Trapped_Args));

	if (!status) {
		status = wcd_call_dev_io_ctl(code, &buf_in, &retval,
					     filp->private_data);

		if (DSP_SUCCEEDED(status)) {
			status = retval;
		} else {
			dev_dbg(bridge, "%s: IOCTL Failed, code: 0x%x "
				"status 0x%x\n", __func__, code, status);
			status = -1;
		}

	}

	status = policy_ioctl_post_hook(filp, code, args, status);

err:
	return status;
}

/* This function maps kernel space memory to user space memory. */
static int bridge_mmap(struct file *filp, struct vm_area_struct *vma)
{
	u32 offset = vma->vm_pgoff << PAGE_SHIFT;
	u32 status;

	status = policy_mmap_hook(filp);
	if (status)
		return status;

	DBC_ASSERT(vma->vm_start < vma->vm_end);

	vma->vm_flags |= VM_RESERVED | VM_IO;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	dev_dbg(bridge, "%s: vm filp %p offset %x start %lx end %lx page_prot "
		"%lx flags %lx\n", __func__, filp, offset,
		vma->vm_start, vma->vm_end, vma->vm_page_prot, vma->vm_flags);

	status = remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
				 vma->vm_end - vma->vm_start,
				 vma->vm_page_prot);
	if (status != 0)
		status = -EAGAIN;

	return status;
}

/* To remove all process resources before removing the process from the
 * process context list */
dsp_status drv_remove_all_resources(bhandle hPCtxt)
{
	dsp_status status = DSP_SOK;
	struct process_context *ctxt = (struct process_context *)hPCtxt;
	drv_remove_all_strm_res_elements(ctxt);
	drv_remove_all_node_res_elements(ctxt);
	drv_remove_all_dmm_res_elements(ctxt);
	ctxt->res_state = PROC_RES_FREED;
	return status;
}

/* Bridge driver initialization and de-initialization functions */
module_init(bridge_init);
module_exit(bridge_exit);
