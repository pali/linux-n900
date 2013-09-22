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
	/* OMAP SSI Port 0 */
	/* ssi1_rdy_tx */
	OMAP3_MUX(UART1_CTS, OMAP_MUX_MODE1 | OMAP_PIN_INPUT | OMAP_PULL_UP ),
	/* ssi1_flag_tx */
	OMAP3_MUX(UART1_RTS, OMAP_MUX_MODE1 | OMAP_PIN_OUTPUT),
	/* ssi1_wake_tx - cawake*/
	OMAP3_MUX(UART1_RX, OMAP_MUX_MODE4 | OMAP_PIN_INPUT | OMAP_WAKEUP_EN),
	/* ssi1_dat_tx */
	OMAP3_MUX(UART1_TX, OMAP_MUX_MODE1 | OMAP_PIN_OUTPUT),
	/* ssi1_dat_rx */
	OMAP3_MUX(MCBSP4_CLKX, OMAP_MUX_MODE1 | OMAP_PIN_INPUT),
	/* ssi1_flag_rx */
	OMAP3_MUX(MCBSP4_DR, OMAP_MUX_MODE1 | OMAP_PIN_INPUT),
	/* ssi1_rdy_rx */
	OMAP3_MUX(MCBSP4_DX, OMAP_MUX_MODE1 | OMAP_PIN_OUTPUT),
	/* ssi1_wake */
	OMAP3_MUX(MCBSP4_FSX, OMAP_MUX_MODE1 | OMAP_PIN_OUTPUT),
	

	/* RAPUYAMA/GAZOO */
	/* APESLEEPX */
	OMAP3_MUX(DSS_DATA0,	OMAP_MUX_MODE4 | OMAP_PIN_OUTPUT),
	/* APE_RST_RQ */
	OMAP3_MUX(DSS_DATA2,	OMAP_MUX_MODE4 | OMAP_PIN_INPUT | OMAP_WAKEUP_EN),
	/* CMT_RST_RQ */
	OMAP3_MUX(DSS_DATA3,	OMAP_MUX_MODE4 | OMAP_PIN_OUTPUT),
	/* CMT_EN */
	OMAP3_MUX(DSS_DATA4,	OMAP_MUX_MODE4 | OMAP_PIN_OUTPUT),
	/* CMT_RST */
	OMAP3_MUX(DSS_DATA5,	OMAP_MUX_MODE4 | OMAP_PIN_OUTPUT),
	/* CMT_BSI */
	OMAP3_MUX(MCBSP1_FSR,	OMAP_MUX_MODE7 | OMAP_PIN_OUTPUT),

	/* Camera interface */
	OMAP3_MUX(CAM_HS,	OMAP_MUX_MODE7 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(CAM_VS,	OMAP_MUX_MODE7 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(CAM_XCLKA,	OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(CAM_PCLK,	OMAP_MUX_MODE4 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(CAM_FLD,	OMAP_MUX_MODE4 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(CAM_D0,	OMAP_MUX_MODE4 | OMAP_PIN_INPUT | OMAP_WAKEUP_EN),
	OMAP3_MUX(CAM_D1,	OMAP_MUX_MODE4 | OMAP_PIN_INPUT_PULLUP | OMAP_WAKEUP_EN),
	OMAP3_MUX(CAM_D2,	OMAP_MUX_MODE4 | OMAP_PIN_INPUT | OMAP_WAKEUP_EN),
	OMAP3_MUX(CAM_D3,	OMAP_MUX_MODE4 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(CAM_D4,	OMAP_MUX_MODE7 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(CAM_D5,	OMAP_MUX_MODE4 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(CAM_D6,	OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(CAM_D7,	OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(CAM_D8,	OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(CAM_D9,	OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(CAM_D10,	OMAP_MUX_MODE7 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(CAM_D11,	OMAP_MUX_MODE4 | OMAP_PIN_INPUT | OMAP_WAKEUP_EN),
	OMAP3_MUX(CAM_XCLKB,	OMAP_MUX_MODE7 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(CAM_WEN,	OMAP_MUX_MODE4 | OMAP_PIN_INPUT),
	OMAP3_MUX(CAM_STROBE,	OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	/* cam_focus*/
	OMAP3_MUX(DSS_VSYNC,	OMAP_MUX_MODE4 | OMAP_PIN_INPUT | OMAP_WAKEUP_EN),

	/* uSD cover gpio_160 */
	OMAP3_MUX(MCBSP_CLKS,	OMAP_MUX_MODE4 | OMAP_PIN_INPUT | OMAP_WAKEUP_EN),

	/* ECI */
	OMAP3_MUX(GPMC_NBE1,	OMAP_MUX_MODE4 | OMAP_PIN_INPUT | OMAP_WAKEUP_EN),

	/* LCD RESET gpio_90*/
	OMAP3_MUX(DSS_DATA20,	OMAP_MUX_MODE4 | OMAP_PIN_OUTPUT | OMAP_PULL_UP),

	/* OneNAND IRQ gpio_65*/
	OMAP3_MUX(GPMC_WAIT3,	OMAP_MUX_MODE4 | OMAP_PIN_INPUT_PULLUP | OMAP_WAKEUP_EN),

	{ .reg_offset = OMAP_MUX_TERMINATOR },
};
#endif

static struct omap_musb_board_data musb_board_data = {
	.interface_type		= MUSB_INTERFACE_ULPI,
	.mode			= MUSB_OTG,
	.power			= 0,
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
	.init_machine	= rx51_init,
	.init_late	= omap3430_init_late,
	.init_time	= omap3_sync32k_timer_init,
	.restart	= omap3xxx_restart,
MACHINE_END
