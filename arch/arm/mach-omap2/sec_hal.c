/*
 * linux/arch/arm/mach-omap2/sec_hal.c
 *
 * Copyright (C) 2008 - 2010 Nokia Corporation
 * Author: Urpo Pietikäinen <urpo.pietikainen@offcode.fi>
 *         Dmitry Kasatkin <dmitry.kasatkin@nokia.com>
 *
 * OMAP3 HS HAL API
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/mutex.h>
#include <linux/random.h>
#include <linux/io.h>
#include <asm/cacheflush.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <plat/cpu.h>
#include <plat/sec.h>
#include <plat/pa.h>
#include "sec_hal.h"

static struct sec_hal_operations *sec_hal_ops;

/**
 * sec_hal_mark_dirty() - Mark secure RAM as dirty.
 *
 * This function is called from functions that are entering to secure side
 * when it is considered that secure RAM has been modified and needs to have
 * expensive save operation done when powering down the CPU.
 *
 * Note: This function will be called with interrupts disabled.
 */
static inline void sec_hal_mark_dirty(void)
{
	if (sec_hal_ops && sec_hal_ops->mark_dirty)
		sec_hal_ops->mark_dirty();
}

void sec_hal_flush_helper(void)
{
	flush_cache_all();
}

u32 sec_hal_dispatch_raw(u32 appl_id, u32 proc_id, u32 flag, dma_addr_t params)
{
	u32 res;

	local_irq_disable();
	local_fiq_disable();

	res = asm_sec_hal_entry(appl_id, proc_id, flag, params);

	sec_hal_mark_dirty();

	local_fiq_enable();
	local_irq_enable();
	return res;
}

u32 sec_hal_dispatch(u32 appl_id, u32 proc_id, u32 flag, ...)
{
	va_list ap;
	u32 res;
	u32 val;

	va_start(ap, flag);
	val = *(u32 *)&ap;
	local_irq_disable();
	local_fiq_disable();

	res = asm_sec_hal_entry(appl_id, proc_id, flag,
				(u32)virt_to_phys((void *)val));

	sec_hal_mark_dirty();

	local_fiq_enable();
	local_irq_enable();
	va_end(ap);
	return res;
}

u32 sec_hal_read_pub_id(u8 *public_id)
{
	return  sec_hal_dispatch(SEC_HAL_KM_PUBLIC_ID, 0, OMAP_DEFAULT_SEC_FLAG,
				  1, public_id);
}

u32 sec_hal_read_rootkey_hash(u8 *rkh)
{
	return sec_hal_dispatch(SEC_HAL_KM_ROOTKEYHASH_READ, 0,
				OMAP_DEFAULT_SEC_FLAG, 1, rkh);
}

u32 sec_hal_rpc_init(void *rpc_fn)
{
	return sec_hal_dispatch(SEC_HAL_SEC_RPC_INIT, 0,
				OMAP_DEFAULT_SEC_FLAG, 1, (u32)rpc_fn);
}

#ifdef SEC_OMAP_HAL_MODE
#define __SECS_INIT_FUNC	SEC_HAL_SEC_STORAGE_INIT
#else
#define __SECS_INIT_FUNC	SEC_HAL_SEC_STORAGE_INIT_NOKIA
#endif

u32 sec_hal_stor_init(dma_addr_t secs, int len)
{
	return sec_hal_dispatch(__SECS_INIT_FUNC, 0, OMAP_DEFAULT_SEC_FLAG,
				2, secs, len);
}

u32 sec_hal_rng(u32 cmd, dma_addr_t p, u32 p_len)
{
	return sec_hal_dispatch(SEC_HAL_RNG_GENERATE, 0, OMAP_DEFAULT_SEC_FLAG,
				 3, p, p_len, cmd);
}

u32 sec_hal_pa_key_import(dma_addr_t cert)
{
	return sec_hal_dispatch(SEC_HAL_PA_KEY_IMPORT, 0,
				OMAP_DEFAULT_SEC_FLAG, 1, cert);
}

#ifdef SEC_OMAP_HAL_MODE
u32 sec_hal_pa_load(dma_addr_t pa_addr, int sub_appl,
		    const dma_addr_t params, dma_addr_t results)
{
	struct  {
		dma_addr_t cert;
		dma_addr_t params;
		dma_addr_t results;
	} info = { pa_addr, params, results };

	return sec_hal_dispatch(SEC_HAL_PA_LOAD, 0, OMAP_DEFAULT_SEC_FLAG, 1,
			       virt_to_phys(&info));
}
#else
u32 sec_hal_pa_load(u32 pa_addr, int sub_appl,
		    const dma_addr_t params, dma_addr_t results)
{
	return sec_hal_dispatch(SEC_HAL_PA_LOAD_WITH_SUBAPPS, 0,
				OMAP_DEFAULT_SEC_FLAG, 4, pa_addr, params,
				results, sub_appl);
}
#endif

u32 sec_hal_ram_resize(u32 size)
{
	return sec_hal_dispatch(SEC_HAL_SEC_RAM_RESIZE, 0,
				OMAP_DEFAULT_SEC_FLAG, 1, size);
}

u32 sec_ext_code_check(dma_addr_t hdr)
{
	return sec_hal_dispatch(SEC_HAL_EXT_CODE_CHECK, 0,
				OMAP_DEFAULT_SEC_FLAG, 2, hdr, 0);
}

/**
 * sec_hal_register() - Register security HAL callbacks
 *
 * @ops: Callback definitions.
 *
 * Note: During registration operation interrupts will be disabled for a
 * short while and enabled afterwards.
 *
 * Returns 0 when all is ok, -EBUSY if there was already registered handler,
 * -EINVAL if input arguments were incorrect.
 */
int sec_hal_register(struct sec_hal_operations *ops)
{
	int rv = -EBUSY;

	/* Make sure input is valid */
	if (!ops)
		return -EINVAL;

	local_irq_disable();
	local_fiq_disable();

	/* Only one registered callback handler is allowed. */
	if (!sec_hal_ops) {
		sec_hal_ops = ops;
		rv = 0;
	}

	local_fiq_enable();
	local_irq_enable();

	return rv;
}

/**
 * sec_hal_unregister() - Unregister security HAL callbacks
 *
 * @ops: Previously registered callback definitions.
 *
 * Note: During unregistration operation interrupts will be disabled for a
 * short while and enabled afterwards.
 *
 * Returns 0 when all is ok, -ENODEV if there was different handler
 * registered, -EINVAL if input arguments were incorrect.
 */
int sec_hal_unregister(struct sec_hal_operations *ops)
{
	int rv = -ENODEV;

	/* Make sure input is valid */
	if (!ops)
		return -EINVAL;

	local_irq_disable();
	local_fiq_disable();

	if (sec_hal_ops == ops) {
		sec_hal_ops = NULL;
		rv = 0;
	}

	local_fiq_enable();
	local_irq_enable();

	return rv;
}
