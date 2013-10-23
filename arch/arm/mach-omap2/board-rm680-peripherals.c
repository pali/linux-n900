/*
 * linux/arch/arm/mach-omap2/board-rm680-peripherals.c
 *
 * Copyright (C) 2009 Nokia
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/input/matrix_keypad.h>
#include <linux/spi/spi.h>
#include <linux/wl12xx.h>
#include <linux/i2c.h>
#include <linux/i2c/twl4030.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/consumer.h>
#include <linux/lis3lv02d.h>
#include <linux/leds-lp5523.h>
#include <linux/i2c/ak8974.h>
#include <linux/i2c/bcm4751-gps.h>
#include <linux/input/syntm12xx-plat.h>
#include <linux/i2c/bhsfh.h>
#include <linux/hsi/hsi.h>
#include <linux/cmt.h>
#include <linux/irq.h>
#include <sound/tpa6130a2-plat.h>
#include <sound/tlv320dac33-plat.h>
#include <linux/mfd/wl1273-core.h>
#include <linux/input/atmel_mxt.h>
#include <linux/input/eci.h>

#include <asm/mach-types.h>

#include <plat/mcspi.h>
#include <plat/gpio.h>
#include <plat/mux.h>
#include <plat/board.h>
#include <plat/common.h>
#include <plat/control.h>
#include <plat/dma.h>
#include <plat/gpmc.h>
#include <plat/onenand.h>
#include <plat/board-nokia.h>
#include <plat/mmc.h>
#include <plat/ssi.h>
#include <plat/omap-pm.h>
#include "hsmmc.h"
#include "board-gpio-export.h"
#include "board-rm680-types.h"
#include "twl4030.h"
#include "nokia-twl4030.h"
#include "atmel_mxt_config.h"

#define RM680_WL1271_POWER_GPIO		35
#define RM680_WL1271_IRQ_GPIO		42
#define RM680_WL1271_REF_CLOCK		2

static void rm680_wl1271_set_power(bool enable);
static void rm680_wl1271_request_perf(struct device *dev,
				      enum wl12xx_perf perf);

static struct wl12xx_platform_data wl1271_pdata = {
	.set_power = rm680_wl1271_set_power,
	.request_perf = rm680_wl1271_request_perf,
	.board_ref_clock = RM680_WL1271_REF_CLOCK,
};

static struct omap2_mcspi_device_config wl1271_mcspi_config = {
	.turbo_mode	= 1,
	.single_channel	= 1,
};

#define LIS302_IRQ1_GPIO 180
#define LIS302_IRQ2_GPIO 181  /* Not yet in use */
#define RM680_FM_RESET_GPIO1	26
#define RM680_FM_RESET_GPIO2	179
#define RM680_FMRX_IRQ_GPIO	43

#define RM680_DAC33_RESET_GPIO	60
#define RM680_DAC33_IRQ_GPIO	53

#define BHSFH_GPIO			83

#define RM680_BCM4751_GPS_IRQ_GPIO	95
#define RM680_BCM4751_GPS_ENABLE_GPIO	94
#define RM680_BCM4751_GPS_WAKEUP_GPIO	91

#define RM680_LP5523_CHIP_EN_GPIO	41

#define ATMEL_MXT_IRQ_GPIO		61
#define ATMEL_MXT_RESET_GPIO		81

static struct spi_board_info rm680_peripherals_spi_board_info[] = {
	[0] = {
		.modalias		= "wl1271_spi",
		.bus_num		= 4,
		.chip_select		= 0,
		.max_speed_hz   	= 48000000,
		.mode                   = SPI_MODE_0,
		.controller_data	= &wl1271_mcspi_config,
		.platform_data		= &wl1271_pdata,
	},
};

static struct twl4030_clock_init_data rm680_clock_init_data = {
	.ck32k_lowpwr_enable = 1,
};

#define RM680_TVOUT_EN_GPIO	40
#define RM680_JACK_GPIO		(OMAP_MAX_GPIO_LINES + 0)

static uint32_t rm680_keymap[] = {
	/* Col, Row, Key */
	KEY(0, 0, KEY_BACKSLASH),
	KEY(1, 0, KEY_LEFTSHIFT),
	KEY(2, 0, KEY_COMPOSE),
	KEY(3, 0, KEY_LEFTMETA),
	KEY(4, 0, KEY_RIGHTCTRL),
	KEY(5, 0, KEY_BACKSPACE),
	KEY(6, 0, KEY_VOLUMEDOWN),
	KEY(7, 0, KEY_VOLUMEUP),

	KEY(0, 1, KEY_RESERVED),
	KEY(1, 1, KEY_RESERVED),
	KEY(2, 1, KEY_RESERVED),
	KEY(3, 1, KEY_Z),
	KEY(4, 1, KEY_A),
	KEY(5, 1, KEY_Q),
	KEY(6, 1, KEY_W),
	KEY(7, 1, KEY_E),

	KEY(0, 2, KEY_RESERVED),
	KEY(1, 2, KEY_RESERVED),
	KEY(2, 2, KEY_RESERVED),
	KEY(3, 2, KEY_X),
	KEY(4, 2, KEY_S),
	KEY(5, 2, KEY_D),
	KEY(6, 2, KEY_C),
	KEY(7, 2, KEY_V),

	KEY(0, 3, KEY_RESERVED),
	KEY(1, 3, KEY_RESERVED),
	KEY(2, 3, KEY_RESERVED),
	KEY(3, 3, KEY_O),
	KEY(4, 3, KEY_I),
	KEY(5, 3, KEY_U),
	KEY(6, 3, KEY_L),
	KEY(7, 3, KEY_APOSTROPHE),

	KEY(0, 4, KEY_RESERVED),
	KEY(1, 4, KEY_RESERVED),
	KEY(2, 4, KEY_RESERVED),
	KEY(3, 4, KEY_Y),
	KEY(4, 4, KEY_K),
	KEY(5, 4, KEY_J),
	KEY(6, 4, KEY_H),
	KEY(7, 4, KEY_G),

	KEY(0, 5, KEY_RESERVED),
	KEY(1, 5, KEY_RESERVED),
	KEY(2, 5, KEY_RESERVED),
	KEY(3, 5, KEY_B),
	KEY(4, 5, KEY_COMMA),
	KEY(5, 5, KEY_M),
	KEY(6, 5, KEY_N),
	KEY(7, 5, KEY_DOT),

	KEY(0, 6, KEY_SPACE),
	KEY(1, 6, KEY_RESERVED),
	KEY(2, 6, KEY_RESERVED),
	KEY(3, 6, KEY_T),
	KEY(4, 6, KEY_UP),
	KEY(5, 6, KEY_LEFT),
	KEY(6, 6, KEY_RIGHT),
	KEY(7, 6, KEY_DOWN),

	KEY(0, 7, KEY_RESERVED),
	KEY(1, 7, KEY_RESERVED),
	KEY(2, 7, KEY_RESERVED),
	KEY(3, 7, KEY_P),
	KEY(4, 7, KEY_ENTER),
	KEY(5, 7, KEY_SLASH),
	KEY(6, 7, KEY_F),
	KEY(7, 7, KEY_R),
};

static struct matrix_keymap_data rm680_keymap_data = {
	.keymap			= rm680_keymap,
	.keymap_size		= ARRAY_SIZE(rm680_keymap),
};

static struct twl4030_keypad_data rm680_kp_data = {
	.keymap_data 	= &rm680_keymap_data,
	.rows		= 8,
	.cols		= 8,
	.rep		= 1,
};

static struct twl4030_usb_data rm680_usb_data = {
	.usb_mode		= T2_USB_MODE_ULPI,
};

static struct twl4030_madc_platform_data rm680_madc_data = {
	.irq_line		= 1,
};

static struct omap2_hsmmc_info mmc[] __initdata = {
	{
		.name           = "internal",
		.mmc            = 2,
		.wires          = 8,
		.nomux		= 1,
		.gpio_cd        = -EINVAL,
		.gpio_wp        = -EINVAL,
		.nonremovable	= true,
		.mmc_only	= true,
		.power_saving	= true,
		.no_off		= true,
		.vcc_aux_disable_is_sleep = true,
	},
	{}      /* Terminator */
};

/* Regulator definitions and their consumer supplies list */

#define REGULATOR_INIT_DATA(_name, _min, _max, _apply, _ops_mask) \
	static struct regulator_init_data _name##_data = { \
		.constraints = { \
			.name                   = #_name, \
			.min_uV                 = _min, \
			.max_uV                 = _max, \
			.apply_uV               = _apply, \
			.valid_modes_mask       = REGULATOR_MODE_NORMAL | \
						REGULATOR_MODE_STANDBY, \
			.valid_ops_mask         = _ops_mask, \
		}, \
		.num_consumer_supplies  = ARRAY_SIZE(_name##_consumers), \
		.consumer_supplies      = _name##_consumers, \
}

/* Same _voltage value for minimum and maximum uV */
#define REGULATOR_INIT_DATA_FIXED(_name, _voltage) \
	REGULATOR_INIT_DATA(_name, _voltage, _voltage, true, \
				REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_MODE)
/* different _min and _max values for minimum and maximum uV */
#define REGULATOR_INIT_DATA_VARIABLE(_name, _min, _max) \
	REGULATOR_INIT_DATA(_name, _min, _max, false, \
			REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | \
			REGULATOR_CHANGE_MODE)

extern struct platform_device rm680_dss_device;

static struct regulator_consumer_supply rm680_vdac_18_consumers[] = {
	{
		.supply         = "vdda_dac",
		.dev            = &rm680_dss_device.dev,
	},
	{
		.supply		= "vdac",
	},
};
REGULATOR_INIT_DATA_FIXED(rm680_vdac_18, 1800000);

static struct regulator_consumer_supply rm680_vpll2_consumers[] = {
	{
		.supply         = "vdds_dsi",
		.dev            = &rm680_dss_device.dev,
	},
	{
		.supply		= "vpll2",
	},
};
REGULATOR_INIT_DATA_FIXED(rm680_vpll2, 1800000);

static struct regulator_consumer_supply rm680_vaux1_consumers[] = {
	REGULATOR_SUPPLY("AVdd", "3-000f"),	/* AK8974 */
	REGULATOR_SUPPLY("Vdd", "3-001d"),	/* LIS302 */
	REGULATOR_SUPPLY("Vcc", "2-0038"),	/* BHSFH */
	REGULATOR_SUPPLY("AVdd", "2-004b"),	/* Atmel mxt */
	REGULATOR_SUPPLY("v28", "twl5031_aci"),
	{
		.supply		= "vaux1",
	},
};
REGULATOR_INIT_DATA_FIXED(rm680_vaux1, 2800000);

static struct regulator_consumer_supply rm680_vaux2_consumers[] = {
	REGULATOR_SUPPLY("VDD_CSIPHY1", "omap3isp"),	/* OMAP ISP */
	REGULATOR_SUPPLY("VDD_CSIPHY2", "omap3isp"),	/* OMAP ISP */
	{
		.supply		= "vaux2",
	},
};
REGULATOR_INIT_DATA_FIXED(rm680_vaux2, 1800000);

static struct regulator_consumer_supply rm680_vaux3_consumers[] = {
	REGULATOR_SUPPLY("VANA", "2-0010"),	/* Main Camera Sensor */
	REGULATOR_SUPPLY("VANA", "2-0037"),	/* Main Camera Sensor */
	REGULATOR_SUPPLY("VANA", "2-000e"),	/* Main Camera Lens */
	REGULATOR_SUPPLY("VANA", "3-0010"),	/* Front Camera */
	{
		.supply		= "vaux3",
	},
};
REGULATOR_INIT_DATA_FIXED(rm680_vaux3, 2800000);

static struct regulator_consumer_supply rm680_vaux4_consumers[] = {
	REGULATOR_SUPPLY("AVDD", "2-0019"),	/* TLV320DAC33 */
	{
		.supply		= "vaux4",
	},
};
REGULATOR_INIT_DATA_FIXED(rm680_vaux4, 2800000);

static struct regulator_consumer_supply rm680_vmmc1_consumers[] = {
	{
		.supply		= "vmmc1",
	},
};
REGULATOR_INIT_DATA_VARIABLE(rm680_vmmc1, 1850000, 3150000);

static struct regulator_consumer_supply rm680_vmmc2_consumers[] = {
	REGULATOR_SUPPLY("VPNL", "display0"),	/* Himalaya */
	{
		.supply		= "vmmc2",
	},
};
REGULATOR_INIT_DATA_FIXED(rm680_vmmc2, 3000000);

static struct regulator_consumer_supply rm680_vpll1_consumers[] = {
	{
		.supply		= "vpll1",
	},
};
REGULATOR_INIT_DATA_FIXED(rm680_vpll1, 1800000);

static struct regulator_consumer_supply rm680_vio_consumers[] = {
	REGULATOR_SUPPLY("DVDD", "2-0019"),	/* TLV320DAC33 */
	REGULATOR_SUPPLY("IOVDD", "2-0019"),	/* TLV320DAC33 */
	REGULATOR_SUPPLY("DVdd", "3-000f"),	/* AK8974 */
	REGULATOR_SUPPLY("Vdd_IO", "3-001d"),	/* LIS302 */
	REGULATOR_SUPPLY("vmmc_aux", "mmci-omap-hs.1"),
	REGULATOR_SUPPLY("VDDI", "display0"),	/* Himalaya */
	REGULATOR_SUPPLY("Vdd", "2-004b"),	/* Atmel mxt */
	REGULATOR_SUPPLY("vonenand", "omap2-onenand"), /* OneNAND flash */
	REGULATOR_SUPPLY("Vbat", "3-01fa"),	/* BCM4751_GPS */
	REGULATOR_SUPPLY("Vddio", "3-01fa"),	/* BCM4751_GPS */
	{
		.supply		= "vio",
	},
};
REGULATOR_INIT_DATA_FIXED(rm680_vio, 1800000);

static struct regulator_consumer_supply rm680_vintana1_consumers[] = {
	{
		.supply		= "vintana1",
	},
};
REGULATOR_INIT_DATA_FIXED(rm680_vintana1, 1500000);

static struct regulator_consumer_supply rm680_vintana2_consumers[] = {
	{
		.supply		= "vintana2",
	},
};
REGULATOR_INIT_DATA_FIXED(rm680_vintana2, 2750000);

static struct regulator_consumer_supply rm680_vrrtc_consumers[] = {
	{
		.supply		= "vrrtc",
	},
};
REGULATOR_INIT_DATA_FIXED(rm680_vrrtc, 1500000);

static struct regulator_consumer_supply rm680_vintdig_consumers[] = {
	{
		.supply		= "vintdig",
	},
};
REGULATOR_INIT_DATA_FIXED(rm680_vintdig, 1500000);

static struct regulator_consumer_supply rm680_vsim_consumers[] = {
	{
		.supply		= "vsim",
	},
};
REGULATOR_INIT_DATA_FIXED(rm680_vsim, 1800000);

static struct regulator_consumer_supply rm680_vbat_consumers[] = {
	REGULATOR_SUPPLY("Vleds", "2-0038"),	/* BHSFH */
	REGULATOR_SUPPLY("AVdd", "2-0060"),	/* TPA6140A2 */
	{
		.supply		= "vbat",
	},
};

static struct regulator_init_data rm680_vbat_data = {
	.num_consumer_supplies	= ARRAY_SIZE(rm680_vbat_consumers),
	.consumer_supplies	= rm680_vbat_consumers,
	.constraints		= {
		.always_on	= 1,
	},
};

static struct fixed_voltage_config rm680_vbat_config = {
	.supply_name = "vbat",
	.microvolts = 3700000,
	.gpio = -1,
	.init_data = &rm680_vbat_data,
};

static struct platform_device rm680_vbat = {
	.name			= "reg-fixed-voltage",
	.id			= -1,
	.dev			= {
		.platform_data	= &rm680_vbat_config,
	},
};

static struct regulator_consumer_supply rm680_vemmc_consumers[] = {
	REGULATOR_SUPPLY("vmmc", "mmci-omap-hs.1"),
};
REGULATOR_INIT_DATA_FIXED(rm680_vemmc, 2900000);

static struct fixed_voltage_config rm680_vemmc_config = {
	.supply_name		= "VEMMC",
	.microvolts		= 2900000,
	.gpio			= 157,
	.startup_delay		= 500,
	.enable_high		= 1,
	.enabled_at_boot	= 0,
	.init_data		= &rm680_vemmc_data,
};

static struct platform_device rm680_vemmc_device = {
	.name			= "reg-fixed-voltage",
	.dev			= {
		.platform_data	= &rm680_vemmc_config,
	},
};

static struct cmt_platform_data rm680_cmt_pdata = {
	.cmt_rst_ind_gpio = 34,
	.cmt_rst_ind_flags = IRQF_TRIGGER_RISING,
};

static struct platform_device rm680_cmt_device = {
	.name = "cmt",
	.id = -1,
	.dev =  {
		.platform_data = &rm680_cmt_pdata,
	},
};

static struct eci_platform_data rm680_eci_platform_data = {
#if	defined(CONFIG_SND_OMAP_SOC_DFL61_TWL4030) || \
	defined(CONFIG_SND_OMAP_SOC_DFL61_TWL4030_MODULE)
	.register_hsmic_event_cb	= dfl61_register_hsmic_event_cb,
	.jack_report			= dfl61_jack_report,
#endif
};

static struct platform_device rm680_eci_device = {
	.name			= "ECI_accessory",
	.dev			= {
		.platform_data = &rm680_eci_platform_data,
	},
};

static struct platform_device *rm680_peripherals_devices[] = {
	&rm680_vbat,
	&rm680_vemmc_device,
	&rm680_eci_device,
	&rm680_cmt_device,
};

/* GPIO0 AvPlugDet */
#define TWL_GPIOS_HIGH	BIT(0)
#define TWL_GPIOS_LOW	(BIT(2) | BIT(6) | BIT(8) | BIT(13) | BIT(15))

static int twlgpio_setup(struct device *dev, unsigned gpio, unsigned n)
{
	int err;

	err = gpio_request(gpio + 1, "TMP303_SOH");
	if (err) {
		printk(KERN_ERR "twl4030_gpio: gpio request failed\n");
		goto out;
	}

	err = gpio_direction_output(gpio + 1, 0);
	if (err)
		printk(KERN_ERR "twl4030_gpio: set gpio direction failed\n");

out:
	return 0;
}

static int twlgpio_teardown(struct device *dev, unsigned gpio, unsigned n)
{
	gpio_free(gpio + 1);
	return 0;
}

static struct twl4030_gpio_platform_data rm680_gpio_data = {
	.gpio_base		= OMAP_MAX_GPIO_LINES,
	.irq_base		= TWL4030_GPIO_IRQ_BASE,
	.irq_end		= TWL4030_GPIO_IRQ_END,
	.pullups		= TWL_GPIOS_HIGH,
	.pulldowns		= TWL_GPIOS_LOW,
	.setup			= twlgpio_setup,
	.teardown		= twlgpio_teardown,
};

static struct twl4030_codec_audio_data rm680_audio_data = {
	.audio_mclk = 38400000,
};

static struct twl4030_codec_vibra_data rm680_vibra_data = {
	.coexist = 1,
};

static struct twl4030_codec_data rm680_codec_data = {
	.audio_mclk = 38400000,
	.audio = &rm680_audio_data,
	.vibra = &rm680_vibra_data,
};

/* ACI */
static struct regulator *rm680plug_regulator;
static bool rm680plug_regulator_enabled;
static int rm680_plug_resource_reserve(struct device *dev)
{
	rm680plug_regulator = regulator_get(dev, "v28");
	if (IS_ERR(rm680plug_regulator)) {
		dev_err(dev, "Unable to get v28 regulator for plug switch");
		return PTR_ERR(rm680plug_regulator);
	}

	rm680plug_regulator_enabled = 0;
	return 0;
}

static int rm680_plug_set_state(struct device *dev, bool plugged)
{
	int ret;

	if (rm680plug_regulator_enabled == plugged)
		return 0;

	if (plugged) {
		ret = regulator_enable(rm680plug_regulator);
		if (ret)
			dev_err(dev, "Failed to enable v28 regulator");
		else
			rm680plug_regulator_enabled = 1;
	} else {
		ret = regulator_disable(rm680plug_regulator);
		if (ret)
			dev_err(dev, "Failed to disable v28 regulator");
		else
			rm680plug_regulator_enabled = 0;
	}

	return ret;
}

static void rm680_plug_resource_release(void)
{
	if (rm680plug_regulator_enabled)
		regulator_disable(rm680plug_regulator);

	regulator_put(rm680plug_regulator);
	rm680plug_regulator = NULL;
}

static struct twl5031_aci_platform_data rm680_aci_data = {
	.tvout_gpio			= RM680_TVOUT_EN_GPIO,
	.jack_gpio			= RM680_JACK_GPIO,
	.avplugdet_plugged		= AVPLUGDET_WHEN_PLUGGED_LOW,
	.hw_plug_set_state		= rm680_plug_set_state,
	.hw_plug_resource_reserve	= rm680_plug_resource_reserve,
	.hw_plug_resource_release	= rm680_plug_resource_release,
};

static struct twl4030_power_data rm680_power_data;

static struct twl4030_platform_data rm680_twldata = {
	.irq_base		= TWL4030_IRQ_BASE,
	.irq_end		= TWL4030_IRQ_END,
	.clock			= &rm680_clock_init_data,

	/* platform_data for children goes here */
	.gpio			= &rm680_gpio_data,
	.keypad			= &rm680_kp_data,
	.madc			= &rm680_madc_data,
	.usb			= &rm680_usb_data,
	.codec			= &rm680_codec_data,
	.aci			= &rm680_aci_data,
	.power			= &rm680_power_data,
	/* LDOs */
	.vio			= &rm680_vio_data,
	.vdac			= &rm680_vdac_18_data,
	.vpll1			= &rm680_vpll1_data,
	.vpll2			= &rm680_vpll2_data,
	.vmmc1			= &rm680_vmmc1_data,
	.vmmc2			= &rm680_vmmc2_data,
	.vsim			= &rm680_vsim_data,
	.vaux1			= &rm680_vaux1_data,
	.vaux2			= &rm680_vaux2_data,
	.vaux3			= &rm680_vaux3_data,
	.vaux4			= &rm680_vaux4_data,
	.vintana1		= &rm680_vintana1_data,
	.vintana2		= &rm680_vintana2_data,
	.vintdig		= &rm680_vintdig_data,
	.vrrtc			= &rm680_vrrtc_data,
};

#define WL1273_HAS_SHARED_RESET(x) (board_is_rm680() && (x) <= 0x0324)

#if defined(CONFIG_RADIO_WL1273) || defined(CONFIG_RADIO_WL1273_MODULE)

static unsigned int wl1273_fm_reset_gpio;

static int rm680_wl1273_fm_request_resources(struct i2c_client *client)
{
	if (gpio_request(RM680_FMRX_IRQ_GPIO, "wl1273_fm irq gpio") < 0) {
		dev_err(&client->dev, "Request IRQ GPIO fails.\n");
		return -1;
	}

	if (WL1273_HAS_SHARED_RESET(system_rev))
		wl1273_fm_reset_gpio = RM680_FM_RESET_GPIO1;
	else
		wl1273_fm_reset_gpio = RM680_FM_RESET_GPIO2;

	/*
	 * In RM680 S0.5 - S0.7 there is no dedicated reset line for the radio.
	 * On those devices the BT driver may have already have reserved the
	 * GPIO or it may try to get it after the radio. So we don't try to
	 * reserve it here.
	 */
	if (!WL1273_HAS_SHARED_RESET(system_rev))
		if (gpio_request(wl1273_fm_reset_gpio,
				 "wl1273_fm reset gpio") < 0) {
			dev_err(&client->dev, "Request for GPIO %d fails.\n",
				wl1273_fm_reset_gpio);
			return -1;
	}

	if (gpio_direction_output(wl1273_fm_reset_gpio, 0)) {
		dev_err(&client->dev, "Set GPIO Direction fails.\n");
		return -1;
	}

	client->irq = gpio_to_irq(RM680_FMRX_IRQ_GPIO);

	return 0;
}

static void rm680_wl1273_fm_free_resources(void)
{
	gpio_free(RM680_FMRX_IRQ_GPIO);
	if (!WL1273_HAS_SHARED_RESET(system_rev))
		gpio_free(wl1273_fm_reset_gpio);
}

static void rm680_wl1273_fm_enable(void)
{
	gpio_set_value(wl1273_fm_reset_gpio, 1);
}

static void rm680_wl1273_fm_disable(void)
{
/*
 * For devices with shared reset line we cannot disable the radio because
 * BT would get disabled at the same time.
 */
	if (!WL1273_HAS_SHARED_RESET(system_rev))
		gpio_set_value(wl1273_fm_reset_gpio, 0);
}

static struct wl1273_fm_platform_data rm680_fm_data = {
	.request_resources = rm680_wl1273_fm_request_resources,
	.free_resources = rm680_wl1273_fm_free_resources,
	.enable = rm680_wl1273_fm_enable,
	.disable = rm680_wl1273_fm_disable,
#if defined(CONFIG_SND_SOC_WL1273) || defined(CONFIG_SND_SOC_WL1273_MODULE)
	.children = WL1273_CODEC_CHILD | WL1273_RADIO_CHILD,
#else
	.children = WL1273_RADIO_CHILD,
#endif
	.modes = WL1273_RX_ALLOWED,
};

static struct platform_device rm680_wl1273_core_device = {
	.name		= "wl1273-core-fm",
	.id		= -1,
	.dev		= {
		.platform_data = &rm680_fm_data,
	},
};

static void __init rm680_wl1273_init(void)
{
	platform_device_register(&rm680_wl1273_core_device);
}
#else

static void __init rm680_wl1273_init(void)
{
}
#endif

static struct i2c_board_info __initdata rm680_twl5031_i2c_board_info[] = {
	{
		I2C_BOARD_INFO("twl5031", 0x48),
		.flags = I2C_CLIENT_WAKE,
		.irq = INT_34XX_SYS_NIRQ,
		.platform_data = &rm680_twldata,
	},
};

#if	defined(CONFIG_SND_SOC_TLV320DAC33) || \
	defined(CONFIG_SND_SOC_TLV320DAC33_MODULE)
static void __init rm680_init_tlv320dac33(void)
{
	int r;

	r = gpio_request(RM680_DAC33_IRQ_GPIO, "tlv320dac33 IRQ");
	if (r < 0) {
		printk(KERN_ERR "Failed to request IRQ gpio "
			"for tlv320dac33 chip\n");
	}
	gpio_direction_input(RM680_DAC33_IRQ_GPIO);

	return;
}

static struct tlv320dac33_platform_data rm680_dac33_platform_data = {
	.power_gpio = RM680_DAC33_RESET_GPIO,
	.mode1_latency = 10000, /* 10ms */
	.mode7lp_latency = 10000, /* 10ms */
	.fallback_to_bypass_time = 40000, /* 40ms */
	.auto_fifo_config = 1,
	.keep_bclk = 1,
	.burst_bclkdiv = 3,
};
#else
static inline void __init rm680_init_tlv320dac33(void)
{
}
#endif

#if	defined(CONFIG_SND_SOC_TPA6130A2) || \
	defined(CONFIG_SND_SOC_TPA6130A2_MODULE)
/* We don't have GPIO allocated for the TPA6130A2 amplifier */
static struct tpa6130a2_platform_data rm680_tpa6130a2_platform_data = {
	.power_gpio = -1,
	.id = TPA6140A2,
};
#endif

#if defined(CONFIG_BHSFH) || defined(CONFIG_BHSFH_MODULE)
static int bhsfh_setup(void)
{
	int err;
	int irq = BHSFH_GPIO;

	/* gpio for interrupt pin */
	err = gpio_request(irq, "bhsfh_irq");
	if (err) {
		printk(KERN_ERR "bhsfh: gpio request failed\n");
		goto fail;
	}

	gpio_direction_input(irq);

fail:
	return err;
}

static int bhsfh_release(void)
{
	gpio_free(BHSFH_GPIO);
	return 0;
}

static struct bhsfh_platform_data rm680_bhsfh_data = {
	.leds		   = BHSFH_LED1,
	.led_max_curr	   = BHSFH_LED_100mA,
	.led_def_curr	   = BHSFH_LED_50mA,
	.glass_attenuation = (16384 * 385) / 100, /* about 3.85x filtering */
	.setup_resources   = bhsfh_setup,
	.release_resources = bhsfh_release,
};
#endif

#if defined(CONFIG_LEDS_LP5523) || defined(CONFIG_LEDS_LP5523_MODULE)
#define RM680_LED_MAX_CURR 130 /* 13 mA */
#define RM680_LED_DEF_CURR 50  /* 5.0 mA */
static struct lp5523_led_config rm680_lp5523_led_config[] = {
	{
		.chan_nr	= 0,
		.led_current    = RM680_LED_DEF_CURR,
		.max_current    = RM680_LED_MAX_CURR,
	}, {
		.chan_nr	= 1,
		.led_current    = RM680_LED_DEF_CURR,
		.max_current    = RM680_LED_MAX_CURR,
	}, {
		.chan_nr	= 2,
		.led_current    = RM680_LED_DEF_CURR,
		.max_current    = RM680_LED_MAX_CURR,
	}, {
		.chan_nr	= 3,
		.led_current    = RM680_LED_DEF_CURR,
		.max_current    = RM680_LED_MAX_CURR,
	}, {
		.chan_nr	= 4,
		.led_current    = RM680_LED_DEF_CURR,
		.max_current    = RM680_LED_MAX_CURR,
	}, {
		.chan_nr	= 5,
		.led_current    = RM680_LED_DEF_CURR,
		.max_current    = RM680_LED_MAX_CURR,
	}, {
		.chan_nr	= 6,
		.led_current    = 0,
	}, {
		.chan_nr	= 7,
		.led_current    = 0,
	}, {
		.chan_nr	= 8,
		.led_current    = RM680_LED_DEF_CURR,
		.max_current    = RM680_LED_MAX_CURR,
	}
};

static int lp5523_setup(void)
{
	int err;
	int gpio = RM680_LP5523_CHIP_EN_GPIO;
	err = gpio_request(gpio, "lp5523_enable");
	if (err) {
		printk(KERN_ERR "lp5523: gpio request failed\n");
		return err;
	}
	gpio_direction_output(gpio, 0);
	return 0;
}

static void lp5523_release(void)
{
	gpio_free(RM680_LP5523_CHIP_EN_GPIO);
}

static void lp5523_enable(bool state)
{
	gpio_set_value(RM680_LP5523_CHIP_EN_GPIO, !!state);
}

static struct lp5523_platform_data rm680_lp5523_platform_data = {
	.led_config	= rm680_lp5523_led_config,
	.num_channels	= ARRAY_SIZE(rm680_lp5523_led_config),
	.clock_mode	= LP5523_CLOCK_EXT,
	.setup_resources   = lp5523_setup,
	.release_resources = lp5523_release,
	.enable		   = lp5523_enable,
};
#endif

#if defined(CONFIG_TOUCHSCREEN_ATMEL_MXT) || \
	defined(CONFIG_TOUCHSCREEN_ATMEL_MXT_MODULE)

static struct mxt_platform_data atmel_mxt_platform_data = {
	.reset_gpio = ATMEL_MXT_RESET_GPIO,
	.int_gpio = ATMEL_MXT_IRQ_GPIO,
	.rlimit_min_interval_us = 7000,
	.rlimit_bypass_time_us = 25000,
	.wakeup_interval_ms = 50,
	.config = &atmel_mxt_himalaya_config,
};

static int __init rm680_init_atmel_mxt(void)
{
	int r;

	r = gpio_request(ATMEL_MXT_IRQ_GPIO, "Atmel mxt interrupt");
	if (r < 0) {
		printk(KERN_ERR "unable to get INT GPIO for atmel_mxt\n");
		goto out;
	}

	gpio_direction_input(ATMEL_MXT_IRQ_GPIO);

	if (board_is_rm680() && system_rev < 0x0400)
		atmel_mxt_platform_data.reset_gpio = -1;

	if (atmel_mxt_platform_data.reset_gpio != -1) {
		r = gpio_request(atmel_mxt_platform_data.reset_gpio,
				 "Atmel mxt reset");
		if (r < 0) {
			printk(KERN_ERR
			       "unable to get RST GPIO for atmel_mxt\n");
			goto out;
		}

		gpio_direction_output(atmel_mxt_platform_data.reset_gpio, 1);
	}

	r = 0;
out:
	return r;
}
#else
static int __init rm680_init_atmel_mxt(void) { return 0; }
#endif

static struct i2c_board_info __initdata rm680_peripherals_i2c_board_info_2[] = {
#if defined(CONFIG_BHSFH) || defined(CONFIG_BHSFH_MODULE)
	{
		I2C_BOARD_INFO("bh1770glc", 0x38),
		.platform_data = &rm680_bhsfh_data,
		.irq = OMAP_GPIO_IRQ(BHSFH_GPIO),
	},
#endif

#if	defined(CONFIG_SND_SOC_TPA6130A2) || \
	defined(CONFIG_SND_SOC_TPA6130A2_MODULE)
	{
		I2C_BOARD_INFO("tpa6130a2", 0x60),
		.platform_data	= &rm680_tpa6130a2_platform_data,
	},
#endif

#if	defined(CONFIG_SND_SOC_TLV320DAC33) || \
	defined(CONFIG_SND_SOC_TLV320DAC33_MODULE)
	{
		I2C_BOARD_INFO("tlv320dac33", 0x19),
		.irq = OMAP_GPIO_IRQ(RM680_DAC33_IRQ_GPIO),
		.platform_data = &rm680_dac33_platform_data,
	},
#endif

#if defined(CONFIG_LEDS_LP5523) || defined(CONFIG_LEDS_LP5523_MODULE)
	{
		I2C_BOARD_INFO("lp5523", 0x32),
		.platform_data  = &rm680_lp5523_platform_data,
	},
#endif

#if     defined(CONFIG_TOUCHSCREEN_ATMEL_MXT) || \
	defined(CONFIG_TOUCHSCREEN_ATMEL_MXT_MODULE)
	{
		I2C_BOARD_INFO("atmel_mxt", 0x4b),
		.irq = OMAP_GPIO_IRQ(ATMEL_MXT_IRQ_GPIO),
		.platform_data = &atmel_mxt_platform_data,
	},
#endif

#if     defined(CONFIG_TOUCHSCREEN_ATMEL_MXT_BL) || \
	defined(CONFIG_TOUCHSCREEN_ATMEL_MXT_BL_MODULE)
	{
		I2C_BOARD_INFO("atmel_mxt_bl", 0x25),
		.irq = OMAP_GPIO_IRQ(ATMEL_MXT_IRQ_GPIO),
	},
#endif
};

#if defined(CONFIG_SENSORS_LIS3_I2C) || defined(CONFIG_SENSORS_LIS3_I2C_MODULE)
static int lis302_setup(void)
{
	int err;
	int irq1 = LIS302_IRQ1_GPIO;
	int irq2 = LIS302_IRQ2_GPIO;

	/* gpio for interrupt pin 1 */
	err = gpio_request(irq1, "lis3lv02dl_irq1");
	if (err) {
		printk(KERN_ERR "lis3lv02dl: gpio request failed\n");
		goto out;
	}

	/* gpio for interrupt pin 2 */
	err = gpio_request(irq2, "lis3lv02dl_irq2");
	if (err) {
		gpio_free(irq1);
		printk(KERN_ERR "lis3lv02dl: gpio request failed\n");
		goto out;
	}

	gpio_direction_input(irq1);
	gpio_direction_input(irq2);

out:
	return err;
}

static int lis302_release(void)
{
	gpio_free(LIS302_IRQ1_GPIO);
	gpio_free(LIS302_IRQ2_GPIO);

	return 0;
}

static struct lis3lv02d_platform_data rm680_lis302dl_data = {
	.click_flags	= LIS3_CLICK_SINGLE_X | LIS3_CLICK_SINGLE_Y |
			  LIS3_CLICK_SINGLE_Z,
	/* Limits are 0.5g * value */
	.click_thresh_x = 8,
	.click_thresh_y = 8,
	.click_thresh_z = 10,
	/* Click must be longer than time limit */
	.click_time_limit = 9,
	/* Kind of debounce filter */
	.click_latency	  = 50,

	/* Limits for all axis. millig-value / 18 to get HW values */
	.wakeup_flags = LIS3_WAKEUP_X_HI | LIS3_WAKEUP_Y_HI,
	.wakeup_thresh = 8,
	.wakeup_flags2 =  LIS3_WAKEUP_Z_HI,
	.wakeup_thresh2 = 10,

	.hipass_ctrl = LIS3_HIPASS_CUTFF_2HZ,

	/* Interrupt line 2 for click detection, line 1 for thresholds */
	.irq_cfg = LIS3_IRQ2_CLICK | LIS3_IRQ1_FF_WU_12,
	.irq_flags = LIS3_IRQ1_USE_BOTH_EDGES, /* Both edges trigs WU irq */
	.duration1 = 8,
	.duration2 = 8,

	.axis_x = LIS3_INV_DEV_Y,
	.axis_y = LIS3_INV_DEV_X,
	.axis_z = LIS3_INV_DEV_Z,
	.setup_resources = lis302_setup,
	.release_resources = lis302_release,
	.st_min_limits = {-32, 3, 3},
	.st_max_limits = {-3, 32, 32},
	.irq2 = OMAP_GPIO_IRQ(LIS302_IRQ2_GPIO),
};

#endif

#if defined(CONFIG_AK8974) || defined(CONFIG_AK8974_MODULE)
static struct ak8974_platform_data rm680_ak8974_data = {
	.axis_x = AK8974_DEV_X,
	.axis_y = AK8974_INV_DEV_Y,
	.axis_z = AK8974_INV_DEV_Z,
};

static inline void __init rm680_init_ak8974(void)
{
	if (board_is_rm680() && system_rev < 0x0400) {
		rm680_ak8974_data.axis_x = AK8974_DEV_Y;
		rm680_ak8974_data.axis_y = AK8974_DEV_X;
		rm680_ak8974_data.axis_z = AK8974_INV_DEV_Z;
	}
}
#else
static inline void __init rm680_init_ak8974(void)
{
}
#endif

#if defined(CONFIG_BCM4751_GPS) || defined(CONFIG_BCM4751_GPS_MODULE)
static int bcm4751_gps_setup(struct i2c_client *client)
{
	struct bcm4751_gps_data *data = i2c_get_clientdata(client);
	int err;

	/* GPS IRQ */
	err = gpio_request(data->gpio_irq, "GPS_IRQ");
	if (err) {
		dev_err(&client->dev,
				"Failed to request GPIO%d (HOST_REQ)\n",
				data->gpio_irq);
		return err;
	}
	err = gpio_direction_input(data->gpio_irq);
	if (err) {
		dev_err(&client->dev, "Failed to change direction\n");
		goto clean_gpio_irq;
	}

	client->irq = gpio_to_irq(data->gpio_irq);

	/* Request GPIO for NSHUTDOWN == GPS_ENABLE */
	err = gpio_request(data->gpio_enable, "GPS Enable");
	if (err < 0) {
		dev_err(&client->dev,
				"Failed to request GPIO%d (GPS_ENABLE)\n",
				data->gpio_enable);
		goto clean_gpio_irq;
	}
	err = gpio_direction_output(data->gpio_enable, 0);
	if (err) {
		dev_err(&client->dev, "Failed to change direction\n");
		goto clean_gpio_en;
	}

	/* Request GPIO for GPS WAKEUP */
	err = gpio_request(data->gpio_wakeup, "GPS Wakeup");
	if (err < 0) {
		dev_err(&client->dev,
				"Failed to request GPIO%d (GPS_WAKEUP)\n",
				data->gpio_wakeup);
		goto clean_gpio_en;
	}
	err = gpio_direction_output(data->gpio_wakeup, 0);
	if (err) {
		dev_err(&client->dev, "Failed to change direction\n");
		goto clean_gpio_wakeup;
	}

	return 0;

clean_gpio_wakeup:
	gpio_free(data->gpio_wakeup);

clean_gpio_en:
	gpio_free(data->gpio_enable);

clean_gpio_irq:
	gpio_free(data->gpio_irq);

	return err;
}

static void bcm4751_gps_cleanup(struct i2c_client *client)
{
	struct bcm4751_gps_data *data = i2c_get_clientdata(client);

	gpio_free(data->gpio_irq);
	gpio_free(data->gpio_wakeup);
	gpio_free(data->gpio_enable);
}

static int bcm4751_gps_show_gpio_irq(struct i2c_client *client)
{
	struct bcm4751_gps_data *data = i2c_get_clientdata(client);

	return gpio_get_value(data->gpio_irq);
}

static void bcm4751_gps_enable(struct i2c_client *client)
{
	struct bcm4751_gps_data *data = i2c_get_clientdata(client);

	gpio_set_value(data->gpio_enable, 1);
}

static void bcm4751_gps_disable(struct i2c_client *client)
{
	struct bcm4751_gps_data *data = i2c_get_clientdata(client);

	gpio_set_value(data->gpio_enable, 0);
}

static void bcm4751_gps_wakeup_ctrl(struct i2c_client *client, int value)
{
	struct bcm4751_gps_data *data = i2c_get_clientdata(client);

	gpio_set_value(data->gpio_wakeup, value);
}

static struct bcm4751_gps_platform_data rm680_bcm4751_gps_platform_data = {
	.gps_gpio_irq		= RM680_BCM4751_GPS_IRQ_GPIO,
	.gps_gpio_enable	= RM680_BCM4751_GPS_ENABLE_GPIO,
	.gps_gpio_wakeup	= RM680_BCM4751_GPS_WAKEUP_GPIO,
	.setup			= bcm4751_gps_setup,
	.cleanup		= bcm4751_gps_cleanup,
	.enable			= bcm4751_gps_enable,
	.disable		= bcm4751_gps_disable,
	.wakeup_ctrl		= bcm4751_gps_wakeup_ctrl,
	.show_irq		= bcm4751_gps_show_gpio_irq
};
#endif

static struct i2c_board_info __initdata rm680_peripherals_i2c_board_info_3[] = {
#if defined(CONFIG_SENSORS_LIS3_I2C) || defined(CONFIG_SENSORS_LIS3_I2C_MODULE)
	{
		I2C_BOARD_INFO("lis3lv02d", 0x1d),
		.platform_data = &rm680_lis302dl_data,
		.irq = OMAP_GPIO_IRQ(LIS302_IRQ1_GPIO),
	},
#endif

#if defined(CONFIG_AK8974) || defined(CONFIG_AK8974_MODULE)
	{
		I2C_BOARD_INFO("ak8974", 0x0f),
		.platform_data = &rm680_ak8974_data,
	},
#endif

#if defined(CONFIG_BCM4751_GPS) || defined(CONFIG_BCM4751_GPS_MODULE)
	{
		I2C_BOARD_INFO("bcm4751-gps", 0x1fa),
		.platform_data = &rm680_bcm4751_gps_platform_data,
	},
#endif

#if defined(CONFIG_RADIO_WL1273) || defined(CONFIG_RADIO_WL1273_MODULE)
	{
		I2C_BOARD_INFO(WL1273_FM_DRIVER_NAME, RX71_FM_I2C_ADDR),
		.platform_data = &rm680_fm_data,
	},
#endif
};

/* OneNAND */
#if defined(CONFIG_MTD_ONENAND_OMAP2) || \
	defined(CONFIG_MTD_ONENAND_OMAP2_MODULE)

static struct omap_onenand_platform_data board_onenand_data = {
	.cs			= 0,
	.gpio_irq		= 65,
	.flags			= ONENAND_SYNC_READWRITE,
	.regulator_can_sleep    = 1,
	.skip_initial_unlocking = 1,
};

static void __init board_onenand_init(void)
{
	gpmc_onenand_init(&board_onenand_data);
}

#else

static inline void board_onenand_init(void)
{
}

#endif /* CONFIG_MTD_ONENAND_OMAP2 || CONFIG_MTD_ONENAND_OMAP2_MODULE */

static void rm680_wl1271_set_power(bool enable)
{
	gpio_set_value(RM680_WL1271_POWER_GPIO, enable);
}

static void rm680_wl1271_request_perf(struct device *dev, enum wl12xx_perf perf)
{
	switch (perf) {
	case WL12XX_PERF_DONT_CARE:
		omap_pm_set_max_mpu_wakeup_lat(dev, -1);
		omap_pm_set_min_bus_tput(dev, OCP_INITIATOR_AGENT, 0);
		omap_pm_set_max_sdma_lat(dev, -1);
		break;
	case WL12XX_PERF_HIGH_THROUGHPUT:
		omap_pm_set_max_mpu_wakeup_lat(dev, 0);
		omap_pm_set_min_bus_tput(dev, OCP_INITIATOR_AGENT, 780800);
		omap_pm_set_max_sdma_lat(dev, 0);
		break;
	}
}

static void __init rm680_init_wl1271(void)
{
	int irq, ret;

	ret = gpio_request(RM680_WL1271_POWER_GPIO, "wl1271 power");
	if (ret < 0)
		goto error;

	ret = gpio_direction_output(RM680_WL1271_POWER_GPIO, 0);
	if (ret < 0)
		goto err_power;

	ret = gpio_request(RM680_WL1271_IRQ_GPIO, "wl1271 irq");
	if (ret < 0)
		goto err_power;

	ret = gpio_direction_input(RM680_WL1271_IRQ_GPIO);
	if (ret < 0)
		goto err_irq;

	irq = gpio_to_irq(RM680_WL1271_IRQ_GPIO);
	if (irq < 0)
		goto err_irq;

	rm680_peripherals_spi_board_info[0].irq = irq;

	return;

err_irq:
	gpio_free(RM680_WL1271_IRQ_GPIO);

err_power:
	gpio_free(RM680_WL1271_POWER_GPIO);

error:
	printk(KERN_ERR "wl1271 board initialisation failed\n");
	wl1271_pdata.set_power = NULL;

	/*
	 * Now rm680_peripherals_spi_board_info[1].irq is zero and
	 * set_power is null, and wl1271_probe() will fail.
	 */

}

static int __init rm680_i2c_init(void)
{
	/* Configure OMAP 3630 internal pull-ups for I2Ci */
	if (cpu_is_omap3630()) {
		u32 prog_io;
		prog_io = omap_ctrl_readl(OMAP343X_CONTROL_PROG_IO1);
		/* Program (bit 19)=0 to enable internal pull-up on I2C1 */
		prog_io &= (~OMAP3630_PRG_I2C1_PULLUPRESX);
		/* Program (bit 0)=0 to enable internal pull-up on I2C2 */
		prog_io &= (~OMAP3630_PRG_I2C2_PULLUPRESX);
		omap_ctrl_writel(prog_io, OMAP343X_CONTROL_PROG_IO1);

		prog_io = omap_ctrl_readl(OMAP36XX_CONTROL_PROG_IO2);
		/* Program (bit 7)=1 to disable internal pull-up on I2C3 */
		prog_io |= OMAP3630_PRG_I2C3_PULLUPRESX;
		/* Program (bits 13:12) to set I2C1 pull-up resistance */
		prog_io &= (~OMAP3630_PRG_I2C1_HS_MASK);
		prog_io |= (OMAP3630_PRG_I2C_LB_HS_RES_1K66 << OMAP3630_PRG_I2C1_HS_SHIFT);
		/* Program (bits 11:10) to set I2C2 pull-up resistance */
		prog_io &= (~OMAP3630_PRG_I2C2_FS_MASK);
		prog_io |= (OMAP3630_PRG_I2C_LB_FS_RES_2K1 << OMAP3630_PRG_I2C2_FS_SHIFT);
		omap_ctrl_writel(prog_io, OMAP36XX_CONTROL_PROG_IO2);
	}

	omap_register_i2c_bus(1, 2900, rm680_twl5031_i2c_board_info,
			      ARRAY_SIZE(rm680_twl5031_i2c_board_info));
	omap_register_i2c_bus(2, 400, rm680_peripherals_i2c_board_info_2,
			ARRAY_SIZE(rm680_peripherals_i2c_board_info_2));
	omap_register_i2c_bus(3, 400, rm680_peripherals_i2c_board_info_3,
			ARRAY_SIZE(rm680_peripherals_i2c_board_info_3));

	return 0;
}

/* Use wakeup latency only for now */
static void rm680_bt_set_pm_limits(struct device *dev, bool set)
{
	omap_pm_set_max_mpu_wakeup_lat(dev, set ? H4P_WAKEUP_LATENCY : -1);
}

static struct omap_bluetooth_config rm680_bt_config = {
	.chip_type		= BT_CHIP_TI,
	.bt_wakeup_gpio		= 37,
	.host_wakeup_gpio	= 101,
	.reset_gpio		= 26,
	.bt_uart		= 2,
	.bt_sysclk		= BT_SYSCLK_38_4,
	.set_pm_limits		= rm680_bt_set_pm_limits,
};

static struct omap_ssi_port_config __initdata rm680_ssi_port_config[] = {
	[0] =	{
		.cawake_gpio = 151,
		.ready_rx_gpio = 154,
		},
};

static struct omap_ssi_board_config __initdata rm680_ssi_config = {
	.num_ports = ARRAY_SIZE(rm680_ssi_port_config),
	.port_config = rm680_ssi_port_config,
};

static struct hsi_board_info __initdata rm680_ssi_cl[] = {
	[0] = 	{
		.name = "hsi_char",
		.hsi_id = 0,
		.port = 0,
		},
	[1] = 	{
		.name = "ssi_protocol",
		.hsi_id = 0,
		.port = 0,
		.tx_cfg = {
			.mode = HSI_MODE_FRAME,
			.channels = 4,
			.speed = 96000,
			.arb_mode = HSI_ARB_RR,
			},
		.rx_cfg = {
			.mode = HSI_MODE_FRAME,
			.channels = 4,
			},
		},
	[2] =	{
		.name = "cmt_speech",
		.hsi_id = 0,
		.port = 0,
		},
};

static void __init rm680_ssi_init(void)
{
	omap_ssi_config(&rm680_ssi_config);
	hsi_register_board_info(rm680_ssi_cl, ARRAY_SIZE(rm680_ssi_cl));
}

static __init void rm680_shared_bt_fm_reset(struct omap_bluetooth_config *bt)
{
	int err;
	if (WL1273_HAS_SHARED_RESET(system_rev)) {
		printk(KERN_INFO "Allocated shared FM/BT reset\n");
		err = gpio_request(26, "wl1273_fmbt_reset");
		if (err < 0)
			printk(KERN_ERR "Can't allocate shared FM/BT reset\n");

		bt->reset_gpio_shared = 1;
	}
}

static void __init rm680_avplugdet_init(void)
{
	if (system_rev == 0x1029)
		rm680_aci_data.avplugdet_plugged = AVPLUGDET_WHEN_PLUGGED_HIGH;
}

void __init rm680_peripherals_init(void)
{
	rm680_init_wl1271();

	platform_add_devices(rm680_peripherals_devices,
			     ARRAY_SIZE(rm680_peripherals_devices));
	spi_register_board_info(rm680_peripherals_spi_board_info,
				ARRAY_SIZE(rm680_peripherals_spi_board_info));
	rm680_init_tlv320dac33();
	rm680_init_ak8974();
	rm680_init_atmel_mxt();
	twl4030_get_nokia_powerdata(&rm680_power_data);
	rm680_avplugdet_init();
	rm680_i2c_init();
	board_onenand_init();
	rm680_ssi_init();
	rm680_wl1273_init();
	rm680_shared_bt_fm_reset(&rm680_bt_config);
	omap_bt_init(&rm680_bt_config);
	omap2_hsmmc_init(mmc);
	regulator_has_full_constraints();
}
