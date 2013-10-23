/*
 * linux/arch/arm/mach-omap2/board-rx51.c
 *
 * Copyright (C) 2007, 2008 Nokia
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
#include <linux/omapfb.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/mcspi.h>
#include <mach/gpio.h>
#include <mach/mux.h>
#include <mach/board.h>
#include <mach/common.h>
#include <mach/keypad.h>
#include <mach/dma.h>
#include <mach/gpmc.h>
#include <mach/usb-musb.h>
#include <mach/omap-pm.h>
#include <mach/vram.h>

#include <asm/io.h>
#include <asm/delay.h>

#include "omap3-opp.h"
#include "pm.h"

extern int omap_init_fb(void);

static struct omap_uart_config rx51_uart_config = {
	.enabled_uarts	= ((1 << 0) | (1 << 1) | (1 << 2)),
};

#if defined CONFIG_FB_OMAP || defined CONFIG_FB_OMAP_MODULE
static struct omap_lcd_config rx51_lcd_config = {
	.ctrl_name	= "internal",
};

static struct omap_fbmem_config rx51_fbmem0_config = {
	.size = (800 * 480 * 2) * 5,
};

static struct omap_fbmem_config rx51_fbmem1_config = {
	.size = 752 * 1024,
};

static struct omap_fbmem_config rx51_fbmem2_config = {
	.size = 752 * 1024,
};

static struct omap_board_config_kernel rx51_config[] = {
	{ OMAP_TAG_UART,	&rx51_uart_config },
	{ OMAP_TAG_FBMEM,	&rx51_fbmem0_config },
	{ OMAP_TAG_FBMEM,	&rx51_fbmem1_config },
	{ OMAP_TAG_FBMEM,	&rx51_fbmem2_config },
	{ OMAP_TAG_LCD,		&rx51_lcd_config },
};
#else
static struct omap_board_config_kernel rx51_config[] = {
	{ OMAP_TAG_UART,	&rx51_uart_config },
};
#endif	/* CONFIG_FB_OMAP || CONFIG_FB_OMAP_MODULE */

static struct omap_bluetooth_config rx51_bt_config = {
	.chip_type		= BT_CHIP_BCM,
	.bt_wakeup_gpio		= 37,
	.host_wakeup_gpio	= 101,
	.reset_gpio		= 91,
	.bt_uart		= 2,
	.bt_sysclk		= BT_SYSCLK_38_4,
};

static void __init rx51_init_irq(void)
{
	omap2_init_common_hw(rx51_get_sdram_timings(),
				omap3_mpu_rate_table,
				omap3_dsp_rate_table,
				omap3_l3_rate_table);
	omap_init_irq();
	omap_gpio_init();
}

static void __init rx51_pm_init(void)
{
	struct prm_setup_times prm_setup = {
		.clksetup = 0,
		.voltsetup_time1 = 60,
		.voltsetup_time2 = 60,
		.voltoffset = 56,
		.voltsetup2 = 150,
	};

	omap3_set_prm_setup_times(&prm_setup);
}

static void __init rx51_init(void)
{
	usb_musb_init();
	omap_serial_init();
	rx51_pm_init();
	/*
	 * With this early call work around a current clock framework problem
	 * where enabling and then disabling a clock disables a root clock
	 * used by another child clock. In our case this would happen with
	 * hsmmc which is normally initialized before fb.
	 */
	omap_init_fb();
	omap_bt_init(&rx51_bt_config);
}

static void __init rx51_map_io(void)
{
	omap_board_config = rx51_config;
	omap_board_config_size = ARRAY_SIZE(rx51_config);
	omap2_set_globals_343x();
#ifdef CONFIG_OMAP2_DSS
	omap2_set_sdram_vram(1024 * 752 * 2, 0);
#endif
	omap2_map_common_io();
}

MACHINE_START(NOKIA_RX51, "Nokia RX-51 board")
	/* Maintainer: Lauri Leukkunen <lauri.leukkunen@nokia.com> */
	.phys_io	= 0x48000000,
	.io_pg_offst	= ((0xd8000000) >> 18) & 0xfffc,
	.boot_params	= 0x80000100,
	.map_io		= rx51_map_io,
	.init_irq	= rx51_init_irq,
	.init_machine	= rx51_init,
	.timer		= &omap_timer,
MACHINE_END
