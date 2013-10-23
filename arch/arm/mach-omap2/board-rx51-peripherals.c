/*
 * linux/arch/arm/mach-omap2/board-rx51-flash.c
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
#include <linux/spi/tsc2005.h>
#include <linux/spi/wl12xx.h>
#include <linux/i2c.h>
#include <linux/i2c/twl4030.h>
#include <linux/i2c/tsl2563.h>
#include <linux/camera_button.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/lis302dl.h>
#include <linux/leds-lp5523.h>

#include <asm/mach-types.h>

#include <mach/mcspi.h>
#include <mach/gpio.h>
#include <mach/mux.h>
#include <mach/board.h>
#include <mach/common.h>
#include <mach/keypad.h>
#include <mach/dma.h>
#include <mach/gpmc.h>
#include <mach/ssi.h>
#include <mach/omap-pm.h>

#include "../../../drivers/input/lirc/lirc_rx51.h"

#define RX51_DEBUG_BASE			0x08000000  /* debug board */
#define RX51_ETHR_START			RX51_DEBUG_BASE
#define RX51_ETHR_GPIO_IRQ		54

#define RX51_TSC2005_RESET_GPIO		104
#define RX51_TSC2005_IRQ_GPIO		100
#define RX51_LP5523_IRQ_GPIO		55
#define RX51_LP5523_CHIP_EN_GPIO	41

#define	RX51_SMC91X_CS			1

#define RX51_WL12XX_POWER_GPIO		87
#define RX51_WL12XX_IRQ_GPIO		42

static void rx51_wl12xx_set_power(bool enable);
static void rx51_tsc2005_set_reset(bool enable);

static struct resource rx51_smc91x_resources[] = {
	[0] = {
		.start	= RX51_ETHR_START,
		.end	= RX51_ETHR_START + SZ_4K,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= OMAP_GPIO_IRQ(RX51_ETHR_GPIO_IRQ),
		.end	= 0,
		.flags		= IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE,
	},
};

static struct platform_device rx51_smc91x_device = {
	.name		= "smc91x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(rx51_smc91x_resources),
	.resource	= rx51_smc91x_resources,
};

static struct tsc2005_platform_data tsc2005_config = {
	.ts_x_plate_ohm		= 280,
	.ts_hw_avg		= 0,
	.ts_touch_pressure	= 1500,
	.ts_stab_time		= 1000,
	.ts_pressure_max	= 2048,
	.ts_pressure_fudge	= 2,
	.ts_x_max		= 4096,
	.ts_x_fudge		= 4,
	.ts_y_max		= 4096,
	.ts_y_fudge		= 7,

	.esd_timeout		= 8*1000, /* ms of inactivity before we check */

	.set_reset		= NULL,
};

static struct lis302dl_platform_data rx51_lis302dl_data = {
	.int1_gpio = 181,
	.int2_gpio = 180,
};

static struct lp5523_led_config rx51_lp5523_led_config[] = {
	{
		.name           = "kb1",
		.led_nr		= 0,
		.led_current    = 50,
	}, {
		.name           = "kb2",
		.led_nr		= 1,
		.led_current    = 50,
	}, {
		.name           = "kb3",
		.led_nr		= 2,
		.led_current    = 50,
	}, {
		.name           = "kb4",
		.led_nr		= 3,
		.led_current    = 50,
	}, {
		.name           = "b",
		.led_nr		= 4,
		.led_current    = 50,
	}, {
		.name           = "g",
		.led_nr		= 5,
		.led_current    = 50,
	}, {
		.name           = "r",
		.led_nr		= 6,
		.led_current    = 50,
	}, {
		.name           = "kb5",
		.led_nr		= 7,
		.led_current    = 50,
	}, {
		.name           = "kb6",
		.led_nr		= 8,
		.led_current    = 50,
	}
};

static struct lp5523_platform_data rx51_lp5523_platform_data = {
	.led_config	= rx51_lp5523_led_config,
	.num_leds	= 9,
	.irq		= OMAP_GPIO_IRQ(RX51_LP5523_IRQ_GPIO),
	.chip_en     	= RX51_LP5523_CHIP_EN_GPIO,
};

static struct tsl2563_platform_data rx51_tsl2563_platform_data = {
	.cover_comp_gain = 16,
};

static struct wl12xx_platform_data wl12xx_pdata = {
	.set_power = rx51_wl12xx_set_power,
};

static struct omap2_mcspi_device_config tsc2005_mcspi_config = {
	.turbo_mode	= 0,
	.single_channel = 1,
};

static struct omap2_mcspi_device_config wl12xx_mcspi_config = {
	.turbo_mode	= 0,
	.single_channel	= 1,
};

static struct spi_board_info rx51_peripherals_spi_board_info[] = {
	[0] = {
		.modalias		= "tsc2005",
		.bus_num		= 1,
		.chip_select		= 0,
		.irq	 		= OMAP_GPIO_IRQ(RX51_TSC2005_IRQ_GPIO),
		.max_speed_hz   	= 6000000,
		.controller_data	= &tsc2005_mcspi_config,
		.platform_data		= &tsc2005_config,
	},
	[1] = {
		.modalias		= "wl12xx",
		.bus_num		= 4,
		.chip_select		= 0,
		.max_speed_hz   	= 48000000,
		.mode                   = SPI_MODE_3,
		.controller_data	= &wl12xx_mcspi_config,
		.platform_data		= &wl12xx_pdata,
	},
};

static int rx51_keymap[] = {
	KEY(0, 0, KEY_Q),
	KEY(0, 1, KEY_W),
	KEY(0, 2, KEY_E),
	KEY(0, 3, KEY_R),
	KEY(0, 4, KEY_T),
	KEY(0, 5, KEY_Y),
	KEY(0, 6, KEY_U),
	KEY(0, 7, KEY_I),
	KEY(1, 0, KEY_O),
	KEY(1, 1, KEY_D),
	KEY(1, 2, KEY_DOT),
	KEY(1, 3, KEY_V),
	KEY(1, 4, KEY_DOWN),
	KEY(1, 7, KEY_F7),
	KEY(2, 0, KEY_P),
	KEY(2, 1, KEY_F),
	KEY(2, 2, KEY_UP),
	KEY(2, 3, KEY_B),
	KEY(2, 4, KEY_RIGHT),
	KEY(2, 7, KEY_F8),
	KEY(3, 0, KEY_COMMA),
	KEY(3, 1, KEY_G),
	KEY(3, 2, KEY_ENTER),
	KEY(3, 3, KEY_N),
	KEY(4, 0, KEY_BACKSPACE),
	KEY(4, 1, KEY_H),
	KEY(4, 3, KEY_M),
	KEY(4, 4, KEY_LEFTCTRL),
	KEY(5, 1, KEY_J),
	KEY(5, 2, KEY_Z),
	KEY(5, 3, KEY_SPACE),
	KEY(5, 4, KEY_RIGHTALT),
	KEY(6, 0, KEY_A),
	KEY(6, 1, KEY_K),
	KEY(6, 2, KEY_X),
	KEY(6, 3, KEY_SPACE),
	KEY(6, 4, KEY_LEFTSHIFT),
	KEY(7, 0, KEY_S),
	KEY(7, 1, KEY_L),
	KEY(7, 2, KEY_C),
	KEY(7, 3, KEY_LEFT),
	KEY(0xff, 2, KEY_F9),
	KEY(0xff, 4, KEY_F10),
	KEY(0xff, 5, KEY_F11),
};

static struct twl4030_keypad_data rx51_kp_data = {
	.rows		= 8,
	.cols		= 8,
	.keymap 	= rx51_keymap,
	.keymapsize 	= ARRAY_SIZE(rx51_keymap),
	.rep		= 1,
};

static struct camera_button_platform_data rx51_cam_button_data = {
	.shutter 	= 69,
	.focus		= 68,
};

static struct platform_device rx51_camera_button_device = {
	.name		= "camera_button",
	.id		= -1,
	.dev		= {
		.platform_data = &rx51_cam_button_data,
	},
};

static struct lirc_rx51_platform_data rx51_lirc_data = {
	.set_max_mpu_wakeup_lat = omap_pm_set_max_mpu_wakeup_lat,
	.pwm_timer = 9, /* Use GPT 9 for CIR */
};

static struct platform_device rx51_lirc_device = {
	.name		= "lirc_rx51",
	.id		= -1,
	.dev		= {
		.platform_data = &rx51_lirc_data,
	},
};

static struct platform_device *rx51_peripherals_devices[] = {
	&rx51_smc91x_device,
	&rx51_camera_button_device,
	&rx51_lirc_device,
};

static void __init rx51_init_smc91x(void)
{
	int eth_cs;
	unsigned long cs_mem_base;
	unsigned int rate;
	struct clk *l3ck;

	eth_cs	= RX51_SMC91X_CS;

	l3ck = clk_get(NULL, "core_l3_ck");
	if (IS_ERR(l3ck))
		rate = 100000000;
	else
		rate = clk_get_rate(l3ck);

	if (gpmc_cs_request(eth_cs, SZ_16M, &cs_mem_base) < 0) {
		printk(KERN_ERR "Failed to request GPMC mem for smc91x\n");
		return;
	}

	rx51_smc91x_resources[0].start = cs_mem_base + 0x0;
	rx51_smc91x_resources[0].end   = cs_mem_base + 0xf;
	udelay(100);

	if (gpio_request(RX51_ETHR_GPIO_IRQ, "SMC91X irq") < 0) {
		printk(KERN_ERR "Failed to request GPIO%d for smc91x IRQ\n",
			RX51_ETHR_GPIO_IRQ);
		return;
	}
	gpio_direction_input(RX51_ETHR_GPIO_IRQ);
}

static void __init rx51_init_lp5523(void)
{
	int r;

	r = gpio_request(RX51_LP5523_IRQ_GPIO, "lp5523 IRQ");
	if (r >= 0)
		gpio_direction_input(RX51_LP5523_IRQ_GPIO);
	else
		printk(KERN_ERR "unable to get lp5523 IRQ GPIO\n");

	r = gpio_request(RX51_LP5523_CHIP_EN_GPIO, "lp5523 CHIP EN");

	if (r >= 0) {
		r = gpio_direction_output(RX51_LP5523_CHIP_EN_GPIO, 1);
		if (r < 0)
			printk(KERN_ERR "unable to set lp5523 CHIP EN GPIO to output\n");
	} else {
		printk(KERN_ERR "unable to get lp5523 CHIP EN GPIO\n");
	}
}

static void __init rx51_init_tsc2005(void)
{
	int r;

	r = gpio_request(RX51_TSC2005_IRQ_GPIO, "tsc2005 DAV IRQ");
	if (r >= 0)
		gpio_direction_input(RX51_TSC2005_IRQ_GPIO);
	else
		printk(KERN_ERR "unable to get DAV GPIO\n");

	r = gpio_request(RX51_TSC2005_RESET_GPIO, "tsc2005 reset");
	if (r >= 0) {
		gpio_direction_output(RX51_TSC2005_RESET_GPIO, 1);
		tsc2005_config.set_reset = rx51_tsc2005_set_reset;
	} else {
		printk(KERN_ERR "unable to get tsc2005 reset GPIO\n");
		tsc2005_config.esd_timeout = 0;
	}
}

static void rx51_tsc2005_set_reset(bool enable)
{
	gpio_set_value(RX51_TSC2005_RESET_GPIO, enable);
}

static void rx51_wl12xx_set_power(bool enable)
{
	gpio_set_value(RX51_WL12XX_POWER_GPIO, enable);
}

static void __init rx51_init_wl12xx(void)
{
	int irq, ret;

	ret = gpio_request(RX51_WL12XX_POWER_GPIO, "wl12xx power");
	if (ret < 0)
		goto error;

	ret = gpio_direction_output(RX51_WL12XX_POWER_GPIO, 0);
	if (ret < 0)
		goto err_power;

	ret = gpio_request(RX51_WL12XX_IRQ_GPIO, "wl12xx irq");
	if (ret < 0)
		goto err_power;

	ret = gpio_direction_input(RX51_WL12XX_IRQ_GPIO);
	if (ret < 0)
		goto err_irq;

	irq = gpio_to_irq(RX51_WL12XX_IRQ_GPIO);
	if (irq < 0)
		goto err_irq;

	rx51_peripherals_spi_board_info[1].irq = irq;

	return;

err_irq:
	gpio_free(RX51_WL12XX_IRQ_GPIO);

err_power:
	gpio_free(RX51_WL12XX_POWER_GPIO);

error:
	printk(KERN_ERR "wl12xx board initialisation failed\n");
	wl12xx_pdata.set_power = NULL;

	/*
	 * Now rx51_peripherals_spi_board_info[1].irq is zero and
	 * set_power is null, and wl12xx_probe() will fail.
	 */
}

static struct twl4030_usb_data rx51_usb_data = {
	.usb_mode		= T2_USB_MODE_ULPI,
};

static struct twl4030_madc_platform_data rx51_madc_data = {
	.irq_line		= 1,
};

static struct twl4030_gpio_platform_data rx51_gpio_data = {
	.gpio_base		= OMAP_MAX_GPIO_LINES,
	.irq_base		= TWL4030_GPIO_IRQ_BASE,
	.irq_end		= TWL4030_GPIO_IRQ_END,
};

static struct twl4030_ins sleep_on_seq[] = {
/*
 * Turn off everything.
#define MSG_BROADCAST(devgrp, grp, type, type2, state) \
	( (devgrp) << 13 | 1 << 12 | (grp) << 9 | (type2) << 7 \
	| (type) << 4 | (state))
#define MSG_SINGULAR(devgrp, id, state) \
	((devgrp) << 13 | 0 << 12 | (id) << 4 | (state))
	0x14 - Corresponds to 500uSec
 */
	{MSG_SINGULAR(DEV_GRP_NULL, RES_HFCLKOUT, RES_STATE_SLEEP), 0x14},
	{MSG_BROADCAST(DEV_GRP_NULL, RES_GRP_ALL, 4, 1, RES_STATE_SLEEP), 2},
	{MSG_BROADCAST(DEV_GRP_NULL, RES_GRP_ALL, 4, 2, RES_STATE_SLEEP), 2},
};

static struct twl4030_script sleep_on_script = {
	.script		  = sleep_on_seq,
	.size		  = ARRAY_SIZE(sleep_on_seq),
	.number_of_events = 1,
	.events[0] = {
		.offset = 0,
		.event = TRITON_SLEEP,
	},
};

static struct twl4030_ins wakeup_seq[] = {
/*
 * Reenable everything.
 */
	/* 0x32= 2.25 max(32khz) delay */
	{MSG_BROADCAST(DEV_GRP_NULL, RES_GRP_ALL, 1, 2, RES_STATE_ACTIVE), 0x32},
	{MSG_SINGULAR(DEV_GRP_NULL, RES_HFCLKOUT, RES_STATE_ACTIVE), 1},
	{MSG_BROADCAST(DEV_GRP_NULL, RES_GRP_ALL, 1, 1, RES_STATE_ACTIVE), 2},
};

static struct twl4030_script wakeup_script = {
	.script		  = wakeup_seq,
	.size		  = ARRAY_SIZE(wakeup_seq),
	.number_of_events = 2,
	.events = {
		[0] = {
			.offset = 0,
			.event = TRITON_WAKEUP12,
		},
		[1] = {
			.offset = 0,
			.event = TRITON_WAKEUP3,
		},
	},
};

static struct twl4030_ins wrst_seq[] = {
/*
 * Reset twl4030.
 * Reset VDD1 regulator.
 * Reset VDD2 regulator.
 * Reset VPLL1 regulator.
 * Enable sysclk output.
 * Reenable twl4030.
 */
	{MSG_SINGULAR(DEV_GRP_NULL, RES_RESET, RES_STATE_OFF), 1},
	{MSG_BROADCAST(DEV_GRP_NULL, RES_GRP_ALL, 0, 2, RES_STATE_WRST), 1},
	{MSG_BROADCAST(DEV_GRP_NULL, RES_GRP_PP, 0, 3, RES_STATE_OFF), 0x34},
	{MSG_BROADCAST(DEV_GRP_NULL, RES_GRP_ALL, 0, 1, RES_STATE_WRST), 1},
	{MSG_SINGULAR(DEV_GRP_NULL, RES_RESET, RES_STATE_ACTIVE), 2},
};

static struct twl4030_script wrst_script = {
	.script 	  = wrst_seq,
	.size   	  = ARRAY_SIZE(wrst_seq),
	.number_of_events = 1,
	.events[0] = {
		.offset = 0,
		.event = TRITON_WRST,
	},
};

static struct twl4030_script *twl4030_scripts[] = {
	&sleep_on_script,
	&wakeup_script,
	&wrst_script,
};

static struct twl4030_resconfig twl4030_rconfig[] = {

	/* Default p1*/
	{ .resource = RES_VDD1,		.devgroup = DEV_GRP_P1 | DEV_GRP_P3,	.type = 4, .type2 =  1, .remap =  0 },
	{ .resource = RES_VDD2,		.devgroup = DEV_GRP_P1 | DEV_GRP_P3,	.type = 3, .type2 =  1, .remap =  0 },
	{ .resource = RES_VPLL1,	.devgroup = DEV_GRP_P1 | DEV_GRP_P3,	.type = 3, .type2 =  1, .remap =  0 },
	{ .resource = RES_VPLL2,	.devgroup = -1,				.type = 0, .type2 =  3, .remap =  8 },
	{ .resource = RES_VAUX1,	.devgroup = -1,				.type = 0, .type2 =  3, .remap =  8 },
	{ .resource = RES_VAUX2,	.devgroup = -1,				.type = 0, .type2 =  3, .remap =  8 },
	{ .resource = RES_VAUX3,	.devgroup = -1,				.type = 0, .type2 =  3, .remap =  8 },
	{ .resource = RES_VAUX4,	.devgroup = -1,				.type = 0, .type2 =  3, .remap =  8 },
	{ .resource = RES_VMMC1,	.devgroup = -1,				.type = 0, .type2 =  3, .remap =  8 },
	{ .resource = RES_VMMC2,	.devgroup = -1,				.type = 0, .type2 =  3, .remap =  8 },
	{ .resource = RES_VDAC,		.devgroup = -1,				.type = 0, .type2 =  3, .remap =  8 },
	{ .resource = RES_VSIM,		.devgroup = -1,				.type = 0, .type2 =  3, .remap =  8 },
	{ .resource = RES_VINTANA1,	.devgroup = DEV_GRP_P1 | DEV_GRP_P3,	.type = 1, .type2 =  2, .remap =  8 },
	{ .resource = RES_VINTANA2,	.devgroup = DEV_GRP_P1 | DEV_GRP_P3,	.type = 0, .type2 =  2, .remap =  8 },
	{ .resource = RES_VINTDIG,	.devgroup = DEV_GRP_P1 | DEV_GRP_P3,	.type = 1, .type2 =  2, .remap =  8 },
	{ .resource = RES_VIO,		.devgroup = DEV_GRP_P1 | DEV_GRP_P3,	.type = 2, .type2 =  2, .remap =  8 },
	{ .resource = RES_CLKEN,	.devgroup = DEV_GRP_P1 | DEV_GRP_P3,	.type = 3, .type2 =  2, .remap =  8 },
	{ .resource = RES_REGEN,	.devgroup = DEV_GRP_P1 | DEV_GRP_P3,	.type = 2, .type2 =  1, .remap =  8 },
	{ .resource = RES_NRES_PWRON,	.devgroup = DEV_GRP_P1 | DEV_GRP_P3,	.type = 0, .type2 =  1, .remap =  8 },
	{ .resource = RES_SYSEN,	.devgroup = DEV_GRP_P1 | DEV_GRP_P3,	.type = 6, .type2 =  1, .remap =  8 },
	{ .resource = RES_HFCLKOUT,	.devgroup = DEV_GRP_P1 | DEV_GRP_P3,	.type = 0, .type2 =  1, .remap =  8 },
	{ .resource = RES_32KCLKOUT,	.devgroup = DEV_GRP_P1 | DEV_GRP_P2 | DEV_GRP_P3,	.type = 0, .type2 =  0, .remap =  8 },
	{ .resource = RES_RESET,	.devgroup = DEV_GRP_P1 | DEV_GRP_P2 | DEV_GRP_P3,	.type = 6, .type2 =  0, .remap =  8 },
	{ .resource = RES_Main_Ref,	.devgroup = DEV_GRP_P1 | DEV_GRP_P2 | DEV_GRP_P3,	.type = 0, .type2 =  0, .remap =  8 },
	{ 0, 0},
};

static struct twl4030_power_data rx51_t2scripts_data = {
	.scripts	= twl4030_scripts,
	.scripts_size	= ARRAY_SIZE(twl4030_scripts),
	.resource_config = twl4030_rconfig,
};


extern struct regulator_init_data rx51_vdac_data;

static struct twl4030_platform_data rx51_twldata = {
	.irq_base		= TWL4030_IRQ_BASE,
	.irq_end		= TWL4030_IRQ_END,

	/* platform_data for children goes here */
	.gpio			= &rx51_gpio_data,
	.keypad			= &rx51_kp_data,
	.madc			= &rx51_madc_data,
	.power			= &rx51_t2scripts_data,
	.usb			= &rx51_usb_data,

	/* LDOs */
	.vdac			= &rx51_vdac_data,
};

static struct omap_ssi_board_config ssi_board_config = {
	.num_ports = 1,
	.cawake_gpio = { 151 },
};

static struct i2c_board_info __initdata rx51_peripherals_i2c_board_info_1[] = {
	{
		I2C_BOARD_INFO("twl4030", 0x48),
		.flags = I2C_CLIENT_WAKE,
		.irq = INT_34XX_SYS_NIRQ,
		.platform_data = &rx51_twldata,
	},
};

static struct i2c_board_info __initdata rx51_peripherals_i2c_board_info_2[] = {
	{
		I2C_BOARD_INFO("lp5523", 0x32),
		.platform_data  = &rx51_lp5523_platform_data,
	},
	{
		I2C_BOARD_INFO("tsl2563", 0x29),
		.platform_data = &rx51_tsl2563_platform_data,
	},
};

static struct i2c_board_info __initdata rx51_peripherals_i2c_board_info_3[] = {
	{
		I2C_BOARD_INFO("lis302dl", 0x1d),
		.platform_data = &rx51_lis302dl_data,
	},
};

static int __init rx51_i2c_init(void)
{
	omap_register_i2c_bus(1, 2200, rx51_peripherals_i2c_board_info_1,
			ARRAY_SIZE(rx51_peripherals_i2c_board_info_1));
	omap_register_i2c_bus(2, 100, rx51_peripherals_i2c_board_info_2,
			      ARRAY_SIZE(rx51_peripherals_i2c_board_info_2));
	omap_register_i2c_bus(3, 400, rx51_peripherals_i2c_board_info_3,
			      ARRAY_SIZE(rx51_peripherals_i2c_board_info_3));

	return 0;
}


static int __init rx51_peripherals_init(void)
{
	if (!machine_is_nokia_rx51())
		return 0;

	rx51_init_wl12xx();

	platform_add_devices(rx51_peripherals_devices,
				ARRAY_SIZE(rx51_peripherals_devices));
	spi_register_board_info(rx51_peripherals_spi_board_info,
				ARRAY_SIZE(rx51_peripherals_spi_board_info));
	rx51_i2c_init();
	rx51_init_smc91x();
	rx51_init_tsc2005();
	rx51_init_lp5523();
	omap_ssi_config(&ssi_board_config);
	return 0;
}

subsys_initcall(rx51_peripherals_init);
