/*
 * linux/arch/arm/mach-omap2/board-rx71.c
 *
 * Copyright (C) 2007 Nokia
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

#include <asm/io.h>
#include <asm/delay.h>

/* MPU speeds */
#define S600M   600000000
#define S550M   550000000
#define S500M   500000000
#define S250M   250000000
#define S125M   125000000

/* DSP speeds */
#define S430M   430000000
#define S400M   400000000
#define S360M   360000000
#define S180M   180000000
#define S90M    90000000

/* L3 speeds */
#define S41M	41500000
#define S83M    83000000
#define S166M   166000000

static struct omap_opp rx71_mpu_rate_table[] = {
	{0, 0, 0},
	/*OPP1*/
	{S125M, VDD1_OPP1, 0x20},
	/*OPP2*/
	{S250M, VDD1_OPP2, 0x2c},
	/*OPP3*/
	{S500M, VDD1_OPP3, 0x31},
	/*OPP4*/
	{S550M, VDD1_OPP4, 0x37},
	/*OPP5*/
	{S600M, VDD1_OPP5, 0x42},
};

static struct omap_opp rx71_l3_rate_table[] = {
	{0, 0, 0},
	/*OPP1*/
	{S41M, VDD2_OPP1, 0x1e},
	/*OPP2*/
	{S83M, VDD2_OPP2, 0x20},
	/*OPP3*/
	{S166M, VDD2_OPP3, 0x2C},
};

struct omap_opp rx71_dsp_rate_table[] = {
	{0, 0, 0},
	/*OPP1*/
	{S90M, VDD1_OPP1, 0x1c},
	/*OPP2*/
	{S180M, VDD1_OPP2, 0x20},
	/*OPP3*/
	{S360M, VDD1_OPP3, 0x30},
	/*OPP4*/
	{S400M, VDD1_OPP4, 0x36},
	/*OPP5*/
	{S430M, VDD1_OPP5, 0x3C},
};

static struct omap_uart_config rx71_uart_config = {
	.enabled_uarts	= ((1 << 0) | (1 << 1) | (1 << 2)),
};

static struct omap_board_config_kernel rx71_config[] = {
	{ OMAP_TAG_UART,	&rx71_uart_config },
};

static void __init rx71_init_irq(void)
{
	struct omap_sdrc_params *sdrc_params;

	sdrc_params = rx51_get_sdram_timings();

	omap2_init_common_hw(sdrc_params, sdrc_params,
				rx71_mpu_rate_table,
				rx71_dsp_rate_table,
				rx71_l3_rate_table);

	omap_init_irq();
	omap_gpio_init();
}

static void __init rx71_init(void)
{
	omap_board_config = rx71_config;
	omap_board_config_size = ARRAY_SIZE(rx71_config);
	usb_musb_init(NULL);
	omap_serial_init();
}

static void __init rx71_map_io(void)
{
	omap2_set_globals_343x();
	omap2_map_common_io();
}

MACHINE_START(NOKIA_RX71, "Nokia RX-71 board")
	/* Maintainer: Ilkka Koskinen <ilkka.koskinen@nokia.com> */
	.phys_io	= 0x48000000,
	.io_pg_offst	= ((0xd8000000) >> 18) & 0xfffc,
	.boot_params	= 0x80000100,
	.map_io		= rx71_map_io,
	.init_irq	= rx71_init_irq,
	.init_machine	= rx71_init,
	.timer		= &omap_timer,
MACHINE_END
