/*
 * linux/arch/arm/mach-omap2/board-rx71-peripherals.c
 *
 * Copyright (C) 2008 Nokia
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/spi/spi.h>
#include <linux/i2c.h>
#include <linux/i2c/twl4030.h>
#include <linux/clk.h>
#include <linux/delay.h>

#include <asm/mach-types.h>

#include <mach/mcspi.h>
#include <mach/gpio.h>
#include <mach/mux.h>
#include <mach/board.h>
#include <mach/common.h>
#include <mach/keypad.h>
#include <mach/dma.h>
#include <mach/gpmc.h>

static struct spi_board_info rx71_peripherals_spi_board_info[] = {
};

static int rx71_keymap[] = {
	/* Col, Row, Key */

	KEY(0, 0, KEY_RESERVED),
	KEY(0, 1, KEY_RESERVED),
	KEY(0, 2, KEY_RESERVED),
	KEY(0, 3, KEY_RESERVED),
	KEY(0, 4, KEY_RESERVED),
	KEY(0, 5, KEY_M),
	KEY(0, 6, KEY_KPMINUS),
	KEY(0, 7, KEY_RESERVED),

	KEY(1, 0, KEY_RESERVED),
	KEY(1, 1, KEY_RESERVED),
	KEY(1, 2, KEY_RESERVED),
	KEY(1, 3, KEY_KPPLUS),
	KEY(1, 4, KEY_DELETE),
	KEY(1, 5, KEY_RESERVED),
	KEY(1, 6, KEY_RESERVED),
	KEY(1, 7, KEY_RESERVED),

	KEY(2, 0, KEY_RESERVED),
	KEY(2, 1, KEY_RESERVED),
	KEY(2, 2, KEY_RESERVED),
	KEY(2, 3, KEY_RESERVED),
	KEY(2, 4, KEY_5),
	KEY(2, 5, KEY_9),
	KEY(2, 6, KEY_KPASTERISK),
	KEY(2, 7, KEY_RESERVED),

	KEY(3, 0, KEY_RESERVED),
	KEY(3, 1, KEY_RESERVED),
	KEY(3, 2, KEY_RESERVED),
	KEY(3, 3, KEY_6),
	KEY(3, 4, KEY_7),
	KEY(3, 5, KEY_0),
	KEY(3, 6, KEY_RESERVED),
	KEY(3, 7, KEY_RESERVED),

	KEY(4, 0, KEY_RESERVED),
	KEY(4, 1, KEY_RESERVED),
	KEY(4, 2, KEY_RESERVED),
	KEY(4, 3, KEY_8),
	KEY(4, 4, KEY_ENTER),
	KEY(4, 5, KEY_RESERVED),
	KEY(4, 6, KEY_4),
	KEY(4, 7, KEY_RESERVED),

	KEY(5, 0, KEY_BACKSPACE),
	KEY(5, 1, KEY_RESERVED),
	KEY(5, 2, KEY_F2),
	KEY(5, 3, KEY_F3),
	KEY(5, 4, KEY_F5),
	KEY(5, 5, KEY_F4),
	KEY(5, 6, KEY_RESERVED),
	KEY(5, 7, KEY_RESERVED),

	KEY(6, 0, KEY_RESERVED),
	KEY(6, 1, KEY_RESERVED),
	KEY(6, 2, KEY_RESERVED),
	KEY(6, 3, KEY_RESERVED),
	KEY(6, 4, KEY_RESERVED),
	KEY(6, 5, KEY_RESERVED),
	KEY(6, 6, KEY_RESERVED),
	KEY(6, 7, KEY_RESERVED),

	KEY(7, 0, KEY_RESERVED),
	KEY(7, 1, KEY_RESERVED),
	KEY(7, 2, KEY_RESERVED),
	KEY(7, 3, KEY_RESERVED),
	KEY(7, 4, KEY_RESERVED),
	KEY(7, 5, KEY_RESERVED),
	KEY(7, 6, KEY_RESERVED),
	KEY(7, 7, KEY_RESERVED),
};

static struct twl4030_keypad_data rx71_kp_data = {
	.rows		= 8,
	.cols		= 8,
	.keymap 	= rx71_keymap,
	.keymapsize 	= ARRAY_SIZE(rx71_keymap),
	.rep		= 1,
};

static struct platform_device *rx71_peripherals_devices[] = {
};

static struct twl4030_usb_data rx71_usb_data = {
	.usb_mode		= T2_USB_MODE_ULPI,
};

static struct twl4030_madc_platform_data rx71_madc_data = {
	.irq_line		= 1,
};

static struct twl4030_gpio_platform_data rx71_gpio_data = {
	.gpio_base		= OMAP_MAX_GPIO_LINES,
	.irq_base		= TWL4030_GPIO_IRQ_BASE,
	.irq_end		= TWL4030_GPIO_IRQ_END,
};

static struct twl4030_platform_data rx71_twldata = {
	.irq_base		= TWL4030_IRQ_BASE,
	.irq_end		= TWL4030_IRQ_END,

	/* platform_data for children goes here */
	.gpio			= &rx71_gpio_data,
	.keypad			= &rx71_kp_data,
	.madc			= &rx71_madc_data,
	.usb			= &rx71_usb_data,
};

static struct i2c_board_info __initdata rx71_peripherals_i2c_board_info_1[] = {
	{
		I2C_BOARD_INFO("twl4030", 0x48),
		.flags = I2C_CLIENT_WAKE,
		.irq = INT_34XX_SYS_NIRQ,
		.platform_data = &rx71_twldata,
	},
};

static int __init rx71_i2c_init(void)
{
	omap_register_i2c_bus(1, 2600, rx71_peripherals_i2c_board_info_1,
			ARRAY_SIZE(rx71_peripherals_i2c_board_info_1));
	omap_register_i2c_bus(2, 100, NULL, 0);
	omap_register_i2c_bus(3, 400, NULL, 0);
	return 0;
}


static int __init rx71_peripherals_init(void)
{
	if (!machine_is_nokia_rx71())
		return 0;

	platform_add_devices(rx71_peripherals_devices,
			     ARRAY_SIZE(rx71_peripherals_devices));
	spi_register_board_info(rx71_peripherals_spi_board_info,
				ARRAY_SIZE(rx71_peripherals_spi_board_info));
	rx71_i2c_init();
	return 0;
}

subsys_initcall(rx71_peripherals_init);
