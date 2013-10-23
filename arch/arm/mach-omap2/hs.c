/*
 * linux/arch/arm/mach-omap2/hs.c
 *
 * Copyright (C) 2008 - 2010 Nokia Corporation
 * Author: Urpo Pietikäinen <urpo.pietikainen@offcode.fi>
 *	   Dmitry Kasatkin <dmitry.kasatkin@nokia.com>
 *
 * OMAP3 HS secure side interface module
 *
 * used by omap_sec security driver to access secure environment
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/random.h>
#include <linux/io.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <plat/cpu.h>
#include <plat/sec.h>
#include "sec_hal.h"

enum {
	RPC_SETTINGS,
	RPC_ALLOC,
	RPC_DEALLOC,
	RPC_SECS_WRITE,
};

struct hs_status {
	struct mutex 		lock;
	/* allocation counter to discover secure environment allocation bugs */
	unsigned long resource;
};

static struct hs_status hs;
static struct sec_operations omap3_ops;

/* I am not sure if it is really needed. Original code had it */
static dma_addr_t __omap_hal_vtp(const void *virt)
{
	return virt_to_phys((void *)virt);
}

static void *__omap_hal_ptv(dma_addr_t phys)
{
	return phys_to_virt(phys);
}

static u32 rpc_alloc(u32 size)
{
	void *p;

	if (!size)
		return 0;

	p = kzalloc(size, GFP_KERNEL);
	if (!p)
		return 0;

	hs.resource++;

	return __omap_hal_vtp(p);
}

static void rpc_dealloc(u32 phys)
{
	hs.resource--;
	kfree(__omap_hal_ptv(phys));
}

u32 sec_hal_rpc_handler(u32 service, u32 a1, u32 a2, u32 a3)
{
	u32 rv = 0;

	pr_debug("service=%x a1=%x a2=%x a3=%x\n", service, a1, a2, a3);

	switch (service) {
	case RPC_SETTINGS:
		break;
	case RPC_ALLOC:
		rv = rpc_alloc(a1);
		break;
	case RPC_DEALLOC:
		rpc_dealloc(a1);
		break;
	case RPC_SECS_WRITE:
		omap3_ops.flags |= SEC_FLAGS_SECS_CHANGED;
		break;
	default:
		pr_debug("No RPC service %x\n", service);
	}
	pr_debug("exit\n");
	return rv;
}

static int init_secs(const void *secs, int secs_len, struct sec_result *out)
{
	pr_debug("enter, secs: %p\n", secs);

	/* Setup secure storage */
	out->rom_rv = sec_hal_stor_init(__omap_hal_vtp(secs), secs_len);
	if ((out->rom_rv != SEC_OK) &&
		(out->rom_rv != SEC_STORAGE_ALREADY_DONE)) {
		pr_err("sec_hal_stor_init() failed %x !\n", out->rom_rv);
		return 0;
	}
	out->rom_rv = 0;
	return 0;
}

static int import_papub(const void *papub, struct sec_result *out)
{
	out->rom_rv = sec_hal_pa_key_import(__omap_hal_vtp(papub));
	if (out->rom_rv)
		pr_err("Failed to import PA pub keys: %x\n", out->rom_rv);
	/* ignore it for now since PPA returns err if already initialized */
	out->rom_rv = 0;
	return 0;
}

static int random_get(void *buf, int len, struct sec_result *out)
{
	out->rom_rv = sec_hal_rng(RNG_GEN_HW, __omap_hal_vtp(buf), len);
	return out->rom_rv != SEC_OK ? -EIO : 0;
}

static int pa_service(const void *pa_addr, int pa_sub,
		const void *params, void *results, struct sec_result *output)
{
	mutex_lock(&hs.lock);
	output->rom_rv = sec_hal_pa_load(__omap_hal_vtp(pa_addr), pa_sub,
					__omap_hal_vtp(params),
					__omap_hal_vtp(results));
	if (output->rom_rv & 0x80000000) {
		/* to see if it happens */
		pr_info("Secure storage change indicated by 0x80000000\n");
		omap3_ops.flags |= SEC_FLAGS_SECS_CHANGED;
		output->rom_rv &= ~0x80000000;
	}
	mutex_unlock(&hs.lock);
	WARN(hs.resource != 0, "resource counter is not 0\n");
	return 0;
}

static int rom_service(int cmd, const void *params, struct sec_result *output)
{
	output->pa_rv = 0;
	mutex_lock(&hs.lock);
	output->rom_rv = sec_hal_dispatch_raw(cmd, 0, OMAP_DEFAULT_SEC_FLAG,
				  __omap_hal_vtp(params));
	mutex_unlock(&hs.lock);
	WARN(hs.resource != 0, "resource counter is not 0\n");
	return 0;
}

static int rom_verify(const void *img, size_t size, size_t *startup)
{
	int rv;
	u32 magic;

	*startup = 0;

	if (!img || size < BB5_COMMON_HEADER_SIZE)
		return -EINVAL;

	magic = *(u32 *)img;

	if (magic != BB5_COMMON_HEADER_MAGIC)
		return -EINVAL;

	rv = sec_ext_code_check(virt_to_phys((void *)img));
	if (!rv)
		*startup = *(uint32_t *)(img + BB5_STARTUP_OFFSET);

	return rv;
}

static struct sec_operations omap3_ops = {
	.owner		= THIS_MODULE,
	.init_secs	= init_secs,
	.import_papub	= import_papub,
	.random_get	= random_get,
	.pa_service	= pa_service,
	.rom_service	= rom_service,
	.rom_verify	= rom_verify,
	.vtp		= __omap_hal_vtp,
	.ptv		= __omap_hal_ptv
};

static int __init hs_init(void)
{
	pr_info("omap_hs driver\n");

	if (!cpu_is_omap34xx() && !cpu_is_omap3630()) {
		pr_err("Unsupported cpu\n");
		return -ENODEV;
	}

	if (omap_type() != OMAP2_DEVICE_TYPE_SEC &&
		   omap_type() != OMAP2_DEVICE_TYPE_EMU) {
		pr_err("OMAP is not HS/EMU\n");
		return -ENODEV;
	}

	mutex_init(&hs.lock);

	if (sec_hal_ram_resize(48 * 1024)) {
		pr_err("Unable to resize Secure RAM\n");
		return -EIO;
	}

	if (sec_hal_rpc_init(asm_sec_hal_rpc_handler)) {
		pr_err("Unable to allocate RPC handler\n");
		return -EIO;
	}

	return sec_register(&omap3_ops);
}

static void __exit hs_exit(void)
{
	sec_unregister(&omap3_ops);
}

module_init(hs_init);
module_exit(hs_exit);

MODULE_DESCRIPTION("OMAP3 HS secure side interface module");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Urpo Pietikäinen, Dmitry Kasatkin");
