/*
 * linux/arch/arm/mach-omap2/board-rx51-audio.c
 *
 * Copyright (C) 2008 Nokia
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/i2c/twl4030.h>
#include <linux/i2c/tpa6130a2.h>
#include <media/radio-bcm2048.h>
#include "../drivers/media/radio/radio-si4713.h"
#include <linux/platform_device.h>
#include <linux/nokia-av.h>
#include <asm/mach-types.h>

#define RX51_FMTX_RESET_GPIO  	163
#define RX51_FMTX_IRQ		53
#define RX51_FMRX_IRQ		43
#define RX51_HEADPHN_EN_GPIO	98
#define RX51_ECI0_GPIO		61
#define RX51_ECI1_GPIO		62
#define RX51_HEADPHONE_GPIO	177

static int si4713_set_power(int power)
{
	/* Make sure VAUX1 is enabled before we rise reset line */
	if (power)
		twl4030_enable_regulator(RES_VAUX1);

	if (!power)
		udelay(1);
	gpio_set_value(RX51_FMTX_RESET_GPIO, power);
	udelay(50);

	/* As reset line is down, no need to keep VAUX1 */
	if (!power)
		twl4030_disable_regulator(RES_VAUX1);

	return 0;
}

static struct si4713_platform_data rx51_si4713_platform_data = {
	.set_power	= si4713_set_power,
};

static void __init rx51_init_si4713(void)
{
	int r;

	r = gpio_request(RX51_FMTX_RESET_GPIO, "si4713");
	if (r < 0) {
		printk(KERN_ERR "Failed to request gpio for FMTx rst\n");
		return;
	}

	gpio_direction_output(RX51_FMTX_RESET_GPIO, 0);
}

static void __init rx51_init_bcm2048(void)
{
	int gpio_irq;

	gpio_irq = gpio_request(RX51_FMRX_IRQ, "BCM2048");
	if (gpio_irq < 0) {
		printk(KERN_ERR "Failed to request gpio for FMRX IRQ\n");
		return;
	}

	gpio_direction_input(RX51_FMRX_IRQ);
}

static int tpa6130a2_set_power(int state)
{
	gpio_set_value(RX51_HEADPHN_EN_GPIO, !!state);
	return 0;
}

static struct tpa6130a2_platform_data rx51_tpa6130a2_platform_data = {
	.set_power = tpa6130a2_set_power,
};

static void __init rx51_init_tpa6130a2(void)
{
	int r;

	r = gpio_request(RX51_HEADPHN_EN_GPIO, "tpa6130a2");
	if (r < 0) {
		printk(KERN_ERR "Failed to request shutdown gpio "
		       "for TPA6130a2 chip\n");
	}

	gpio_direction_output(RX51_HEADPHN_EN_GPIO, 0);

	return;
}

static struct nokia_av_platform_data rx51_nokia_av_platform_data = {
	.eci0_gpio		= RX51_ECI0_GPIO,
	.eci1_gpio		= RX51_ECI1_GPIO,
	.headph_gpio		= RX51_HEADPHONE_GPIO,
};

static struct platform_device rx51_nokia_av_device = {
	.name		= "nokia-av",
	.id		= -1,
	.dev		= {
		.platform_data = &rx51_nokia_av_platform_data,
	},
};

static struct platform_device *rx51_audio_devices[] = {
	&rx51_nokia_av_device,
};

static struct i2c_board_info __initdata rx51_audio_i2c_board_info_2[] = {
	{
		I2C_BOARD_INFO(SI4713_NAME, SI4713_I2C_ADDR_BUSEN_HIGH),
		.type		= "si4713",
		.irq		= OMAP_GPIO_IRQ(RX51_FMTX_IRQ),
		.platform_data 	= &rx51_si4713_platform_data,
	},
	{
		I2C_BOARD_INFO("aic34b_dummy", 0x19),
	},
	{
		I2C_BOARD_INFO("tpa6130a2", 0x60),
		.platform_data	= &rx51_tpa6130a2_platform_data,
	},
};

static struct i2c_board_info __initdata rx51_audio_i2c_board_info_3[] = {
	{
		I2C_BOARD_INFO(BCM2048_NAME, BCM2048_I2C_ADDR),
		.irq		= OMAP_GPIO_IRQ(RX51_FMRX_IRQ),
	},
};

static int __init rx51_audio_init(void)
{
	if (!(machine_is_nokia_rx51() || machine_is_nokia_rx71()))
		return 0;

	platform_add_devices(rx51_audio_devices,
			ARRAY_SIZE(rx51_audio_devices));

	rx51_init_tpa6130a2();
	rx51_init_si4713();
	rx51_init_bcm2048();
	i2c_register_board_info(2, rx51_audio_i2c_board_info_2,
			      ARRAY_SIZE(rx51_audio_i2c_board_info_2));
	i2c_register_board_info(3, rx51_audio_i2c_board_info_3,
			      ARRAY_SIZE(rx51_audio_i2c_board_info_3));

	return 0;
}

subsys_initcall(rx51_audio_init);
