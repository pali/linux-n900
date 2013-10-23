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
#include <linux/usb/musb.h>

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

#include <mach/pm.h>
#include <mach/omap-pm.h>
#include <mach/prcm.h>
#include "cm.h"

#define RX51_USB_TRANSCEIVER_RST_GPIO	67

extern int omap_init_fb(void);
extern void rx51_video_mem_init(void);

static struct omap_uart_config rx51_uart_config = {
	.enabled_uarts	= ((1 << 0) | (1 << 1) | (1 << 2)),
};

static struct omap_board_config_kernel rx51_config[] = {
	{ OMAP_TAG_UART,	&rx51_uart_config },
};

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
	struct omap_sdrc_params *sdrc_params;

	sdrc_params = rx51_get_sdram_timings();

	omap2_init_common_hw(sdrc_params, sdrc_params,
			     omap3_mpu_rate_table,
			     omap3_dsp_rate_table,
			     omap3_l3_rate_table);
	omap_init_irq();
	omap_gpio_init();
}

static void __init rx51_pm_init(void)
{
	struct prm_setup_times prm_setup = {
		.clksetup = 111, /* must equal Volt offset + voltsetup2 */
		.voltsetup_time1 = 270,
		.voltsetup_time2 = 150,
		/* Time between wakeup event to when the sysoff goes high */
		.voltoffset = 16,
		 /* The following is for a 2.25ms ramp time of the oscillator
		  * Documented 2ms, added .25 as margin. NOTE: scripts
		  * change as oscillator changes
		  */
		.voltsetup2 = 95,
	};

	omap3_set_prm_setup_times(&prm_setup);
}

static void __init rx51_xceiv_init(void)
{
	if (gpio_request(RX51_USB_TRANSCEIVER_RST_GPIO, NULL) < 0)
		BUG();
	gpio_direction_output(RX51_USB_TRANSCEIVER_RST_GPIO, 1);
}

static int rx51_xceiv_reset(void)
{
	/* make sure the transceiver is awake */
	msleep(15);
	/* only reset powered transceivers */
	if (!gpio_get_value(RX51_USB_TRANSCEIVER_RST_GPIO))
		return 0;
	gpio_set_value(RX51_USB_TRANSCEIVER_RST_GPIO, 0);
	msleep(1);
	gpio_set_value(RX51_USB_TRANSCEIVER_RST_GPIO, 1);
	msleep(15);

	return 0;
}

static int rx51_xceiv_power(bool power)
{
	unsigned long	timeout;

	if (!power) {
		/* Let musb go stdby before powering down the transceiver */
		timeout = jiffies + msecs_to_jiffies(100);
		while (!time_after(jiffies, timeout))
			if (cm_read_mod_reg(CORE_MOD, CM_IDLEST1)
				& OMAP3430ES2_ST_HSOTGUSB_STDBY_MASK)
				break;
		if (!(cm_read_mod_reg(CORE_MOD, CM_IDLEST1)
			& OMAP3430ES2_ST_HSOTGUSB_STDBY_MASK))
			WARN(1, "could not put musb to sleep\n");
	}
	gpio_set_value(RX51_USB_TRANSCEIVER_RST_GPIO, power);

	return 0;
}

/**
 * rx51_usb_set_pm_limits - sets omap3-related pm constraints
 * @dev:	musb's device pointer
 * @set:	set or clear constraints
 *
 * For now we only need mpu wakeup latency mpu frequency, if we
 * need anything else we just add the logic here and the driver
 * is already handling what needs to be handled.
 */
static void rx51_usb_set_pm_limits(struct device *dev, bool set)
{
	omap_pm_set_max_mpu_wakeup_lat(dev, set ? 10 : -1);
	omap_pm_set_min_mpu_freq(dev, set ? 500000000 : 0);
}

static struct musb_board_data rx51_musb_data = {
	.xceiv_reset	= rx51_xceiv_reset,
	.xceiv_power	= rx51_xceiv_power,
	.set_pm_limits	= rx51_usb_set_pm_limits,
};

static void __init rx51_init(void)
{
	rx51_xceiv_init();
	usb_musb_init(&rx51_musb_data);
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
	omap2_map_common_io();
	rx51_video_mem_init();
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
