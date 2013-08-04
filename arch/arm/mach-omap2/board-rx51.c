/*
 * Board support file for Nokia N900 (aka RX-51).
 *
 * Copyright (C) 2007, 2008 Nokia
 * Copyright (C) 2012 Ivaylo Dimitrov <freemangordon@abv.bg>
 * Copyright (C) 2013 Pali Rohár <pali.rohar@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/leds.h>
#include <linux/usb/phy.h>
#include <linux/usb/musb.h>
#include <linux/platform_data/spi-omap2-mcspi.h>

#include <asm/cacheflush.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <linux/omap-dma.h>

#include "common.h"
#include "mux.h"
#include "gpmc.h"
#include "pm.h"
#include "soc.h"
#include "sdram-nokia.h"
#include "omap-secure.h"

/* Secure PPA (Primary Protected Application) APIs */
#define RX51_PPA_HWRNG			29
#define RX51_PPA_L2_INVAL		40
#define RX51_PPA_WRITE_ACR		42

#define RX51_GPIO_SLEEP_IND 162

static struct gpio_led gpio_leds[] = {
	{
		.name	= "sleep_ind",
		.gpio	= RX51_GPIO_SLEEP_IND,
	},
};

static struct gpio_led_platform_data gpio_led_info = {
	.leds		= gpio_leds,
	.num_leds	= ARRAY_SIZE(gpio_leds),
};

static struct platform_device leds_gpio = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data	= &gpio_led_info,
	},
};

/*
 * cpuidle C-states definition for rx51.
 *
 * The 'exit_latency' field is the sum of sleep
 * and wake-up latencies.

    ---------------------------------------------
   | state |  exit_latency  |  target_residency  |
    ---------------------------------------------
   |  C1   |    110 + 162   |            5       |
   |  C2   |    106 + 180   |          309       |
   |  C3   |    107 + 410   |        46057       |
   |  C4   |    121 + 3374  |        46057       |
   |  C5   |    855 + 1146  |        46057       |
   |  C6   |   7580 + 4134  |       484329       |
   |  C7   |   7505 + 15274 |       484329       |
    ---------------------------------------------

*/

extern void __init rx51_peripherals_init(void);
extern void __init rx51_camera_init(void);

#ifdef CONFIG_OMAP_MUX
static struct omap_board_mux board_mux[] __initdata = {
	{ .reg_offset = OMAP_MUX_TERMINATOR },
};
#endif

static struct omap_musb_board_data musb_board_data = {
	.interface_type		= MUSB_INTERFACE_ULPI,
	.mode			= MUSB_PERIPHERAL,
	.power			= 0,
};

/**
 * rx51_secure_dispatcher: Routine to dispatch secure PPA API calls
 * @idx: The PPA API index
 * @process: Process ID
 * @flag: The flag indicating criticality of operation
 * @nargs: Number of valid arguments out of four.
 * @arg1, arg2, arg3 args4: Parameters passed to secure API
 *
 * Return the non-zero error value on failure.
 */
static u32 rx51_secure_dispatcher(u32 idx, u32 process, u32 flag, u32 nargs,
			   u32 arg1, u32 arg2, u32 arg3, u32 arg4)
{
	u32 ret;
	u32 param[5];

	param[0] = nargs+1; /* RX-51 needs number of arguments + 1 */
	param[1] = arg1;
	param[2] = arg2;
	param[3] = arg3;
	param[4] = arg4;

	/*
	 * Secure API needs physical address
	 * pointer for the parameters
	 */
	local_irq_disable();
	local_fiq_disable();
	flush_cache_all();
	outer_clean_range(__pa(param), __pa(param + 5));
	ret = omap_smc3(idx, process, flag, __pa(param));
	flush_cache_all();
	local_fiq_enable();
	local_irq_enable();

	return ret;
}

/**
 * rx51_secure_update_aux_cr: Routine to modify the contents of Auxiliary Control Register
 *  @set_bits: bits to set in ACR
 *  @clr_bits: bits to clear in ACR
 *
 * Return the non-zero error value on failure.
*/
static u32 rx51_secure_update_aux_cr(u32 set_bits, u32 clear_bits)
{
	u32 acr;

	/* Read ACR */
	asm volatile ("mrc p15, 0, %0, c1, c0, 1" : "=r" (acr));
	acr &= ~clear_bits;
	acr |= set_bits;

	return rx51_secure_dispatcher(RX51_PPA_WRITE_ACR,
				      0,
				      FLAG_START_CRITICAL,
				      1, acr, 0, 0, 0);
}

/**
 * rx51_secure_rng_call: Routine for HW random generator
 */
static u32 rx51_secure_rng_call(u32 ptr, u32 count, u32 flag)
{
	return rx51_secure_dispatcher(RX51_PPA_HWRNG,
				      0,
				      NO_FLAG,
				      3, ptr, count, flag, 0);
}

static struct platform_device omap3_rom_rng_device = {
	.name		= "omap_rng",
	.id		= -1,
	.dev	= {
		.platform_data	= rx51_secure_rng_call,
	},
};

static void __init rx51_init(void)
{
	struct omap_sdrc_params *sdrc_params;

	omap3_mux_init(board_mux, OMAP_PACKAGE_CBB);
	omap_serial_init();

	sdrc_params = nokia_get_sdram_timings();
	omap_sdrc_init(sdrc_params, sdrc_params);

	usb_bind_phy("musb-hdrc.0.auto", 0, "twl4030_usb");
	usb_musb_init(&musb_board_data);
	rx51_peripherals_init();
	rx51_camera_init();

	if (omap_type() == OMAP2_DEVICE_TYPE_SEC) {
#ifdef CONFIG_ARM_ERRATA_430973
		pr_info("RX-51: Enabling ARM errata 430973 workaround\n");
		/* set IBE to 1 */
		rx51_secure_update_aux_cr(BIT(6), 0);
#endif
		pr_info("RX-51: Registring OMAP3 HWRNG device\n");
		platform_device_register(&omap3_rom_rng_device);
	}

	/* Ensure SDRC pins are mux'd for self-refresh */
	omap_mux_init_signal("sdrc_cke0", OMAP_PIN_OUTPUT);
	omap_mux_init_signal("sdrc_cke1", OMAP_PIN_OUTPUT);

	platform_device_register(&leds_gpio);
}

static void __init rx51_reserve(void)
{
	omap_reserve();
}

MACHINE_START(NOKIA_RX51, "Nokia RX-51 board")
	/* Maintainer: Lauri Leukkunen <lauri.leukkunen@nokia.com> */
	.atag_offset	= 0x100,
	.reserve	= rx51_reserve,
	.map_io		= omap3_map_io,
	.init_early	= omap3430_init_early,
	.init_irq	= omap3_init_irq,
	.handle_irq	= omap3_intc_handle_irq,
	.init_machine	= rx51_init,
	.init_late	= omap3430_init_late,
	.init_time	= omap3_sync32k_timer_init,
	.restart	= omap3xxx_restart,
MACHINE_END
