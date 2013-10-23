/*
 * linux/arch/arm/mach-omap2/board-rm696.c
 *
 * Copyright (C) 2009 Nokia
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/usb/musb.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <plat/mux.h>
#include <plat/board.h>
#include <plat/board-nokia.h>
#include <plat/common.h>
#include <plat/usb.h>

extern void rm696_video_mem_init(void);
extern void rm696_camera_init(void);

#include "omap3-opp.h"
#include "sdram-nokia.h"
#include "voltage.h"
#include "smartreflex-class1p5.h"
#include "pm-optimizer.h"
#include "pm.h"

static struct omap_board_config_kernel rm696_config[] = {
};

struct omap_opp {
	bool enabled;
	unsigned long rate;
	unsigned long u_volt;
	u8 opp_id;
};

#define REG_SLEW_RATE			4
#define RET_VOLTAGE			830000
#define RAMP_UV_CYCLES(tuv, rkhz)		\
		(((((tuv) - RET_VOLTAGE) / REG_SLEW_RATE) * (rkhz)) / 1000000)
static struct prm_setup_vc rm696_vc_config = {
	.ret = {
		.clksetup = 113, /* 32kHz, 3ms osc, 200us reg, 250us margin */
		.voltsetup1_vdd1 = 0, /* This value is computed at run time */
		.voltsetup1_vdd2 = 0, /* This value is computed at run time */
		.voltsetup2 = 0x0,
		.voltoffset = 16,
	},
	.off = {
		.clksetup = 113, /* 32kHz, 3ms osc, 200us reg, 250us margin */
		.voltsetup1_vdd1 = 0, /* ready when clk is active */
		.voltsetup1_vdd2 = 0, /* ready when clk is active */
		.voltsetup2 = 97, /* clksetup - voltoffset */
		.voltoffset = 16, /* 488us according to recommendation */
	},
};

static struct pm_optimizer_element rm696_pm_opt_elems[] = {
	{
		.type = PM_OPTIMIZER_TYPE_CLK,
		.name = "mmci-omap-hs.1",
		.data = { .conn = "fck" },
		.cpufreq = {
			.threshold = 100,
			.min = 600000,
		},
	},
	{
		.type = PM_OPTIMIZER_TYPE_DISPLAY,
		.name = "display",
		.cpuidle = {
			.threshold = 100,
			.max = 6000,
		},
	},
	{
		.type = PM_OPTIMIZER_TYPE_INPUT,
		.name = "input",
		.timeout = 1 * HZ,
		.cpuidle = {
			.threshold = 100,
			.max = 6000,
		},
		.cpufreq = {
			.threshold = 100,
			.min = 1000000,
		},
	},
	{
		.type = PM_OPTIMIZER_TYPE_USERSPACE,
		.name = "rotation",
		.timeout = 1 * HZ,
		.cpufreq = {
			.threshold = 100,
			.min = 1000000,
		},
	},
};

static const char *rm696_pm_opt_ignore_list[] = {
	"ST LIS3LV02DL Accelerometer",
};

static struct pm_optimizer_data rm696_pm_opt_data = {
	.elems = rm696_pm_opt_elems,
	.num_elements = ARRAY_SIZE(rm696_pm_opt_elems),
	.ignore_list = rm696_pm_opt_ignore_list,
	.num_ignores = ARRAY_SIZE(rm696_pm_opt_ignore_list),
};

/*
 * CPUIDLE parameters, measured at OPP1. Threshold values should be
 * calculated when proper consumption numbers are available
 */
static struct cpuidle_params rm696_cpuidle_params[] = {
	/* C1 */ {1, 60,	42,	101,},
	/* C2 */ {1, 87,	59,	183,},
	/* C3 */ {1, 185,	67,	29177,},
	/* C4 */ {1, 1928,	381,	42648,},
	/* C5 */ {1, 4624,	987,	56119,},
	/* C6 */ {1, 6391,	1294,	77616,},
	/* C7 */ {1, 14643,	1175,	99114,},
	/* C8 */ {1, 14643,	7615,	158105,},
};

static void __init rm696_setup_vc_timings(struct omap_opp *vdd1opp4,
						struct omap_opp *vdd2opp2)
{
	struct clk *sys_ck;

	sys_ck = clk_get(NULL, "sys_ck");
	if (sys_ck != NULL && !IS_ERR(vdd1opp4) && !IS_ERR(vdd2opp2)) {
		unsigned long rate = clk_get_rate(sys_ck) / 1000;

		 /* register value is number of sys_ck cycles / 8 */
		rm696_vc_config.ret.voltsetup1_vdd1 =
			RAMP_UV_CYCLES(opp_get_voltage(vdd1opp4), rate) / 8;

		 /* register value is number of sys_ck cycles / 8 */
		rm696_vc_config.ret.voltsetup1_vdd2 =
			RAMP_UV_CYCLES(opp_get_voltage(vdd2opp2), rate) / 8;
		clk_put(sys_ck);
	}
}

static void __init rm696_init_irq(void)
{
	struct omap_opp *vdd1 = ERR_PTR(-ENOENT), *vdd2 = ERR_PTR(-ENOENT);
	struct omap_opp *opp1, *opp2;
	struct clk *sdrc_ck;
	u32 sdrc_rate;
	struct omap_sdrc_params *sdrc_params;

	omap_board_config = rm696_config;
	omap_board_config_size = ARRAY_SIZE(rm696_config);
	omap3_pm_init_opp_table();
#ifdef CONFIG_PM
	omap3_pm_init_cpuidle(rm696_cpuidle_params);
#endif
	/*
	 * It doesn't matter the memory clock as this function
	 * sets up all clocks we use. clockfw should take care
	 * of grabbing the right one.
	 */
	sdrc_params = nokia_get_sdram_timings();
	omap2_init_common_hw(sdrc_params, sdrc_params);

	/* Enable 800MHz MPU OPP */
	opp1 = opp_find_freq_exact(OPP_MPU, 800000000,
			false);
	opp2 = opp_find_freq_exact(OPP_DSP, 660000000,
			false);
	if (!IS_ERR(opp1) && !IS_ERR(opp2)) {
		int ret;
		ret = opp_enable(opp1);
		BUG_ON(ret);
		ret = opp_enable(opp2);
		BUG_ON(ret);
	}

	opp1 = opp_find_freq_exact(OPP_MPU, 1000000000, false);
	opp2 = opp_find_freq_exact(OPP_DSP, 800000000, false);
	if (!IS_ERR(opp1) && !IS_ERR(opp2)) {
		int ret;
		if (system_rev >= 0x201) {
			ret = opp_enable(opp1);
			BUG_ON(ret);
			vdd1 = opp1;
			ret = opp_enable(opp2);
			BUG_ON(ret);
		} else {
			ret = opp_disable(opp1);
			BUG_ON(ret);
			ret = opp_disable(opp2);
			BUG_ON(ret);
		}
	}

	/*
	 * Adjust L3 OPPs according to the SDRC frequency initialized
	 * by the boot loader
	 */
	sdrc_ck = clk_get(NULL, "sdrc_ick");
	if (sdrc_ck) {
		opp1 = opp_find_freq_exact(OPP_L3, 100000000, true);
		opp2 = opp_find_freq_exact(OPP_L3, 200000000, true);

		if (!IS_ERR(opp1) && !IS_ERR(opp2)) {
			vdd2 = opp2;
			sdrc_rate = clk_get_rate(sdrc_ck);
			switch (sdrc_rate) {
			case 185000000:
				opp1->rate = 92500000;
				opp2->rate = 185000000;
				break;
			case 195200000:
				opp1->rate = 97600000;
				opp2->rate = 195200000;
				break;
			case 200000000:
				pr_warning("Running device under out of "
					"spec clocking on L3\n");
				break;
			default:
				pr_warning("Booting with unknown L3 clock\n");
			}
		}
		clk_put(sdrc_ck);
	}

	if (!IS_ERR(vdd1) && !IS_ERR(vdd2)) {
		/*
		 * Now that OPPs are setup, we can compute some
		 * voltage timings based on system configuration we get
		 */
		rm696_setup_vc_timings(vdd1, vdd2);

		omap_voltage_init_vc(&rm696_vc_config);
	}

	omap_init_irq();
	pm_optimizer_register(&rm696_pm_opt_data);
	omap_gpio_init();
}

/**
 * rm696_usb_set_pm_limits - sets omap3-related pm constraints
 * @dev:        musb's device pointer
 * @set:        set or clear constraints
 *
 * For now we only need mpu wakeup latency mpu frequency, if we
 * need anything else we just add the logic here and the driver
 * is already handling what needs to be handled.
 */
static void rm696_usb_set_pm_limits(struct device *dev, bool set)
{
	omap_pm_set_max_mpu_wakeup_lat(dev, set ? 10 : -1);
	omap_pm_set_min_bus_tput(dev, OCP_INITIATOR_AGENT, set ? 780800 : 0);
}

static struct musb_board_data rm696_musb_data = {
	.set_pm_limits  = rm696_usb_set_pm_limits,
};

static void __init rm696_init(void)
{
	printk(KERN_INFO "RM-696 board, rev %04x\n", system_rev);
	omap_serial_init_port(2);
	usb_musb_init(&rm696_musb_data, MUSB_PERIPHERAL, 100);
	rm696_peripherals_init();
	rm696_camera_init();

	/* Ensure SDRC pins are mux'd for self-refresh */
	omap_cfg_reg(H16_34XX_SDRC_CKE0);
	omap_cfg_reg(H17_34XX_SDRC_CKE1);

	sr_class1p5_init();
}

static void __init rm696_map_io(void)
{
	omap2_set_globals_343x();
	omap2_map_common_io();
}

static void __init rm696_reserve(void)
{
	rm696_video_mem_init();
	omap_reserve();
	pm_alloc_secure_ram();
}

MACHINE_START(NOKIA_RM696, "Nokia RM-696 board")
	/* Maintainer: Aaro Koskinen <aaro.koskinen@nokia.com> */
	.phys_io	= 0x48000000,
	.io_pg_offst	= ((0xfa000000) >> 18) & 0xfffc,
	.boot_params	= PHYS_OFFSET + 0x100,
	.map_io		= rm696_map_io,
	.reserve	= rm696_reserve,
	.init_irq	= rm696_init_irq,
	.init_machine	= rm696_init,
	.timer		= &omap_timer,
MACHINE_END
