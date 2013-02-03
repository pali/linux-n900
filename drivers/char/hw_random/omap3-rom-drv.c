/*
 * omap3-rom-drv.c - RNG driver for TI OMAP3 CPU family
 *
 * Copyright (C) 2009 Nokia Corporation
 * Author: Juha Yrjola <juha.yrjola@solidboot.com>
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/random.h>
#include <linux/hw_random.h>
#include <linux/timer.h>
#include <linux/clk.h>
#include <linux/err.h>

#include <mach/cpu.h>

#define SEC_HAL_RNG_GENERATE		29
#define RNG_RESET			0x01
#define RNG_GEN_PRNG_HW_INIT		0x02
#define RNG_GEN_HW			0x08

static const char *omap3_rom_rng_name = "OMAP3 ROM RNG";

extern u32 omap3_rng_call_rom_asm(u32 id, u32 proc, u32 flags, u32 va_ptr);

static int call_sec_rom(u32 appl_id, u32 proc_id, u32 flag, ...)
{
	va_list ap;
	u32 ret;
	u32 val;

	va_start(ap, flag);
	val = *(u32 *) &ap;
	local_irq_disable();
	local_fiq_disable();
	ret = omap3_rng_call_rom_asm(appl_id, proc_id, flag,
				     (u32) virt_to_phys((void *) val));
	local_fiq_enable();
	local_irq_enable();
	va_end(ap);

	return ret;
}

static struct timer_list idle_timer;
static int rng_idle;
static struct clk *rng_clk;

static void omap3_rom_idle_rng(unsigned long data)
{
	int r;

	r = call_sec_rom(SEC_HAL_RNG_GENERATE, 0, 0, 3, NULL, 0,
			 RNG_RESET);
	if (r != 0) {
		printk(KERN_ERR "%s: reset failed: %d\n",
		       omap3_rom_rng_name, r);
		return;
	}
	clk_disable(rng_clk);
	rng_idle = 1;
}

static int omap3_rom_get_random(void *buf, unsigned int count)
{
	u32 r;
	u32 ptr;

	del_timer_sync(&idle_timer);
	if (rng_idle) {
		clk_enable(rng_clk);
		r = call_sec_rom(SEC_HAL_RNG_GENERATE, 0, 0, 3, NULL, 0,
				 RNG_GEN_PRNG_HW_INIT);
		if (r != 0) {
			clk_disable(rng_clk);
			printk(KERN_ERR "%s: HW init failed: %d\n",
			       omap3_rom_rng_name, r);
			return -EIO;
		}
		rng_idle = 0;
	}

	ptr = virt_to_phys(buf);
	r = call_sec_rom(SEC_HAL_RNG_GENERATE, 0, 0, 3, ptr,
			 count, RNG_GEN_HW);
	mod_timer(&idle_timer, jiffies + msecs_to_jiffies(500));
	if (r != 0)
		return -EINVAL;
	return 0;
}

static int omap3_rom_rng_data_present(struct hwrng *rng, int wait)
{
	return 1;
}

static int omap3_rom_rng_data_read(struct hwrng *rng, u32 *data)
{
	int r;

	r = omap3_rom_get_random(data, 4);
	if (r < 0)
		return r;
	return 4;
}

static struct hwrng omap3_rom_rng_ops = {
	.name		= "omap3-rom",
	.data_present	= omap3_rom_rng_data_present,
	.data_read	= omap3_rom_rng_data_read,
};

static int __init omap3_rom_rng_init(void)
{
	printk(KERN_INFO "%s: initializing\n", omap3_rom_rng_name);
	if (!cpu_is_omap34xx()) {
		printk(KERN_ERR "%s: currently supports only OMAP34xx CPUs\n",
		       omap3_rom_rng_name);
		return -ENODEV;
	}
	if (omap_type() == OMAP2_DEVICE_TYPE_GP) {
		printk(KERN_ERR "%s: GP OMAPs not supported\n",
		       omap3_rom_rng_name);
		return -ENODEV;
	}

	setup_timer(&idle_timer, omap3_rom_idle_rng, 0);
	rng_clk = clk_get(NULL, "rng_ick");
	if (IS_ERR(rng_clk)) {
		printk(KERN_ERR "%s: unable to get RNG clock\n",
		       omap3_rom_rng_name);
		return IS_ERR(rng_clk);
	}

	/* Leave the RNG in reset state. */
	clk_enable(rng_clk);
	omap3_rom_idle_rng(0);

	return hwrng_register(&omap3_rom_rng_ops);
}

static void __exit omap3_rom_rng_exit(void)
{
	hwrng_unregister(&omap3_rom_rng_ops);
}

module_init(omap3_rom_rng_init);
module_exit(omap3_rom_rng_exit);

MODULE_AUTHOR("Juha Yrjola");
MODULE_LICENSE("GPL");
