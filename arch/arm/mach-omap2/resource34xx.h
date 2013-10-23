/*
 * linux/arch/arm/mach-omap2/resource34xx.h
 *
 * OMAP3 resource definitions
 *
 * Copyright (C) 2007-2008 Texas Instruments, Inc.
 * Rajendra Nayak <rnayak@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * History:
 *
 */

#ifndef __ARCH_ARM_MACH_OMAP2_RESOURCE_H
#define __ARCH_ARM_MACH_OMAP2_RESOURCE_H

#include <mach/resource.h>
#include <linux/clk.h>
#include <mach/clock.h>
#include <mach/powerdomain.h>
#include <mach/omap-pm.h>
#include "resource34xx_mutex.h"

extern int sr_voltagescale_vcbypass(u32 t_opp, u32 c_opp, u8 t_vsel, u8 c_vsel);

/*
 * mpu_latency/core_latency are used to control the cpuidle C state.
 */
void init_latency(struct shared_resource *resp);
int set_latency(struct shared_resource *resp, u32 target_level);

static u8 mpu_qos_req_added;
static u8 core_qos_req_added;

static struct shared_resource_ops lat_res_ops = {
	.init 		= init_latency,
	.change_level   = set_latency,
};

static struct shared_resource mpu_latency = {
	.name 		= "mpu_latency",
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
	.resource_data  = &mpu_qos_req_added,
	.ops 		= &lat_res_ops,
};

static struct shared_resource core_latency = {
	.name 		= "core_latency",
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
	.resource_data	= &core_qos_req_added,
	.ops 		= &lat_res_ops,
};

/*
 * Power domain Latencies are used to control the target Power
 * domain state once all clocks for the power domain
 * are released.
 */
void init_pd_latency(struct shared_resource *resp);
int set_pd_latency(struct shared_resource *resp, u32 target_level);

/* Power Domain Latency levels */
#define PD_LATENCY_OFF		0x0
#define PD_LATENCY_RET		0x1
#define PD_LATENCY_INACT	0x2
#define PD_LATENCY_ON		0x3

#define PD_LATENCY_MAXLEVEL 	0x4

struct pd_latency_db {
	char *pwrdm_name;
	struct powerdomain *pd;
	/* Latencies for each state transition, stored in us */
	unsigned long latency[PD_LATENCY_MAXLEVEL];
};

static struct shared_resource_ops pd_lat_res_ops = {
	.init		= init_pd_latency,
	.change_level 	= set_pd_latency,
};

static struct shared_resource core_pwrdm_latency = {
	.name		= "core_pwrdm_latency",
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
	.resource_data	= &core_qos_req_added,
	.ops		= &lat_res_ops,
};

#if !defined(CONFIG_MPU_BRIDGE) && !defined(CONFIG_MPU_BRIDGE_MODULE)
static struct pd_latency_db iva2_pwrdm_lat_db = {
	.pwrdm_name = "iva2_pwrdm",
	.latency[PD_LATENCY_OFF] = 1100,
	.latency[PD_LATENCY_RET] = 350,
	.latency[PD_LATENCY_INACT] = -1,
	.latency[PD_LATENCY_ON] = 0
};

static struct shared_resource iva2_pwrdm_latency = {
	.name		= "iva2_pwrdm_latency",
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
	.resource_data	= &iva2_pwrdm_lat_db,
	.ops		= &pd_lat_res_ops,
};
#endif

static struct pd_latency_db gfx_pwrdm_lat_db = {
	.pwrdm_name = "gfx_pwrdm",
	.latency[PD_LATENCY_OFF] = 1000,
	.latency[PD_LATENCY_RET] = 100,
	.latency[PD_LATENCY_INACT] = -1,
	.latency[PD_LATENCY_ON]	 = 0
};

static struct pd_latency_db sgx_pwrdm_lat_db = {
	.pwrdm_name = "sgx_pwrdm",
	.latency[PD_LATENCY_OFF] = 1000,
	.latency[PD_LATENCY_RET] = 100,
	.latency[PD_LATENCY_INACT] = -1,
	.latency[PD_LATENCY_ON]	 = 0
};

static struct shared_resource gfx_pwrdm_latency = {
	.name		= "gfx_pwrdm_latency",
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430ES1),
	.resource_data	= &gfx_pwrdm_lat_db,
	.ops		= &pd_lat_res_ops,
};

static struct shared_resource sgx_pwrdm_latency = {
	.name 		= "sgx_pwrdm_latency",
	.omap_chip	= OMAP_CHIP_INIT(CHIP_GE_OMAP3430ES2),
	.resource_data  = &sgx_pwrdm_lat_db,
	.ops		= &pd_lat_res_ops,
};

static struct pd_latency_db dss_pwrdm_lat_db = {
	.pwrdm_name = "dss_pwrdm",
	.latency[PD_LATENCY_OFF] = 70,
	.latency[PD_LATENCY_RET] = 20,
	.latency[PD_LATENCY_INACT] = -1,
	.latency[PD_LATENCY_ON]	 = 0
};

static struct shared_resource dss_pwrdm_latency = {
	.name		= "dss_pwrdm_latency",
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
	.resource_data	= &dss_pwrdm_lat_db,
	.ops		= &pd_lat_res_ops,
};

static struct pd_latency_db cam_pwrdm_lat_db = {
	.pwrdm_name = "cam_pwrdm",
	.latency[PD_LATENCY_OFF] = 850,
	.latency[PD_LATENCY_RET] = 35,
	.latency[PD_LATENCY_INACT] = -1,
	.latency[PD_LATENCY_ON]	 = 0
};

static struct shared_resource cam_pwrdm_latency = {
	.name		= "cam_pwrdm_latency",
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
	.resource_data	= &cam_pwrdm_lat_db,
	.ops		= &pd_lat_res_ops,
};

static struct pd_latency_db per_pwrdm_lat_db = {
	.pwrdm_name = "per_pwrdm",
	.latency[PD_LATENCY_OFF] = 200,
	.latency[PD_LATENCY_RET] = 110,
	.latency[PD_LATENCY_INACT] = -1,
	.latency[PD_LATENCY_ON]	 = 0
};

static struct shared_resource per_pwrdm_latency = {
	.name		= "per_pwrdm_latency",
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
	.resource_data	= &per_pwrdm_lat_db,
	.ops		= &pd_lat_res_ops,
};

static struct pd_latency_db neon_pwrdm_lat_db = {
	.pwrdm_name = "neon_pwrdm",
	.latency[PD_LATENCY_OFF] = 200,
	.latency[PD_LATENCY_RET] = 35,
	.latency[PD_LATENCY_INACT] = -1,
	.latency[PD_LATENCY_ON]	 = 0
};

static struct shared_resource neon_pwrdm_latency = {
	.name		= "neon_pwrdm_latency",
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
	.resource_data	= &neon_pwrdm_lat_db,
	.ops		= &pd_lat_res_ops,
};

static struct pd_latency_db usbhost_pwrdm_lat_db = {
	.pwrdm_name = "usbhost_pwrdm",
	.latency[PD_LATENCY_OFF] = 800,
	.latency[PD_LATENCY_RET] = 150,
	.latency[PD_LATENCY_INACT] = -1,
	.latency[PD_LATENCY_ON]	 = 0
};

static struct shared_resource usbhost_pwrdm_latency = {
	.name		= "usbhost_pwrdm_latency",
	.omap_chip	= OMAP_CHIP_INIT(CHIP_GE_OMAP3430ES2),
	.resource_data  = &usbhost_pwrdm_lat_db,
	.ops		= &pd_lat_res_ops,
};

static struct pd_latency_db emu_pwrdm_lat_db = {
	.pwrdm_name = "emu_pwrdm",
	.latency[PD_LATENCY_OFF] = 1000,
	.latency[PD_LATENCY_RET] = 100,
	.latency[PD_LATENCY_INACT] = -1,
	.latency[PD_LATENCY_ON]  = 0
};

static struct shared_resource emu_pwrdm_latency = {
	.name           = "emu_pwrdm",
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
	.resource_data  = &emu_pwrdm_lat_db,
	.ops		= &pd_lat_res_ops,
};

void init_opp(struct shared_resource *resp);
int set_opp(struct shared_resource *resp, u32 target_level);
int validate_opp(struct shared_resource *resp, u32 target_level);
void init_freq(struct shared_resource *resp);
int set_freq(struct shared_resource *resp, u32 target_level);
int validate_freq(struct shared_resource *resp, u32 target_level);

static struct shared_resource_ops opp_res_ops = {
	.init           = init_opp,
	.change_level   = set_opp,
	.validate_level = validate_opp,
};

static struct shared_resource vdd1_opp = {
	.name           = "vdd1_opp",
	.omap_chip      = OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
	.ops            = &opp_res_ops,
};

static struct shared_resource vdd2_opp = {
	.name           = "vdd2_opp",
	.omap_chip      = OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
	.ops            = &opp_res_ops,
};

static char linked_res[] = "vdd1_opp";

static struct shared_resource_ops freq_res_ops = {
	.init           = init_freq,
	.change_level   = set_freq,
	.validate_level = validate_freq,
};

static struct shared_resource mpu_freq = {
	.name           = "mpu_freq",
	.omap_chip      = OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
	.resource_data  = &linked_res,
	.ops            = &freq_res_ops,
};

static struct shared_resource dsp_freq = {
	.name           = "dsp_freq",
	.omap_chip      = OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
	.resource_data  = &linked_res,
	.ops            = &freq_res_ops,
};

struct shared_resource *resources_omap[] __initdata = {
	&mpu_latency,
	&core_latency,
	/* Power Domain Latency resources */
	&core_pwrdm_latency,
#if !defined(CONFIG_MPU_BRIDGE) && !defined(CONFIG_MPU_BRIDGE_MODULE)
	&iva2_pwrdm_latency,
#endif
	&gfx_pwrdm_latency,
	&sgx_pwrdm_latency,
	&dss_pwrdm_latency,
	&cam_pwrdm_latency,
	&per_pwrdm_latency,
	&neon_pwrdm_latency,
	&usbhost_pwrdm_latency,
	&emu_pwrdm_latency,
	/* OPP/frequency resources */
	&vdd1_opp,
	&vdd2_opp,
	&mpu_freq,
	&dsp_freq,
	NULL
};

#endif /* __ARCH_ARM_MACH_OMAP2_RESOURCE_H */
