/*
 * omap_hwmod_3xxx_data.c - hardware modules present on the OMAP3xxx chips
 *
 * Copyright (C) 2009-2010 Nokia Corporation
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * The data in this file should be completely autogeneratable from
 * the TI hardware database or other technical documentation.
 *
 * XXX these should be marked initdata for multi-OMAP kernels
 */
#include <plat/omap_hwmod.h>
#include <mach/irqs.h>
#include <plat/cpu.h>
#include <plat/dma.h>
#include <plat/control.h>

#include "omap_hwmod_common_data.h"

#include "prm-regbits-34xx.h"
#include "smartreflex.h"
#include "voltage.h"

/*
 * OMAP3xxx hardware module integration data
 *
 * ALl of the data in this section should be autogeneratable from the
 * TI hardware database or other technical documentation.  Data that
 * is driver-specific or driver-kernel integration-specific belongs
 * elsewhere.
 */

static struct omap_hwmod omap3xxx_mpu_hwmod;
static struct omap_hwmod omap3xxx_l3_hwmod;
static struct omap_hwmod omap3xxx_l4_core_hwmod;
static struct omap_hwmod omap3xxx_l4_per_hwmod;
static struct omap_hwmod omap34xx_sr1_hwmod;
static struct omap_hwmod omap34xx_sr2_hwmod;

/* L3 -> L4_CORE interface */
static struct omap_hwmod_ocp_if omap3xxx_l3__l4_core = {
	.master	= &omap3xxx_l3_hwmod,
	.slave	= &omap3xxx_l4_core_hwmod,
	.user	= OCP_USER_MPU | OCP_USER_SDMA,
};

/* L3 -> L4_PER interface */
static struct omap_hwmod_ocp_if omap3xxx_l3__l4_per = {
	.master = &omap3xxx_l3_hwmod,
	.slave	= &omap3xxx_l4_per_hwmod,
	.user	= OCP_USER_MPU | OCP_USER_SDMA,
};

/* MPU -> L3 interface */
static struct omap_hwmod_ocp_if omap3xxx_mpu__l3 = {
	.master = &omap3xxx_mpu_hwmod,
	.slave	= &omap3xxx_l3_hwmod,
	.user	= OCP_USER_MPU,
};

/* Slave interfaces on the L3 interconnect */
static struct omap_hwmod_ocp_if *omap3xxx_l3_slaves[] = {
	&omap3xxx_mpu__l3,
};

/* Master interfaces on the L3 interconnect */
static struct omap_hwmod_ocp_if *omap3xxx_l3_masters[] = {
	&omap3xxx_l3__l4_core,
	&omap3xxx_l3__l4_per,
};

/* L3 */
static struct omap_hwmod omap3xxx_l3_hwmod = {
	.name		= "l3_hwmod",
	.class		= &l3_hwmod_class,
	.masters	= omap3xxx_l3_masters,
	.masters_cnt	= ARRAY_SIZE(omap3xxx_l3_masters),
	.slaves		= omap3xxx_l3_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_l3_slaves),
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430)
};

static struct omap_hwmod omap3xxx_l4_wkup_hwmod;

/* L4_CORE -> L4_WKUP interface */
static struct omap_hwmod_ocp_if omap3xxx_l4_core__l4_wkup = {
	.master	= &omap3xxx_l4_core_hwmod,
	.slave	= &omap3xxx_l4_wkup_hwmod,
	.user	= OCP_USER_MPU | OCP_USER_SDMA,
};


static struct omap_hwmod_irq_info omap3_sr1_irq = {
	.name = "sr1_irq",
	.irq = 18,
};

static struct omap_hwmod_irq_info omap3_sr2_irq = {
	.name = "sr2_irq",
	.irq = 19,
};

/* L4 CORE -> SR1 interface */
static struct omap_hwmod_addr_space omap34xx_sr1_addr_space[] = {
	{
		.pa_start	= OMAP34XX_SR1_BASE,
		.pa_end		= OMAP34XX_SR1_BASE + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT,
	},
};

static struct omap_hwmod_ocp_if omap3_l4_core__sr1 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap34xx_sr1_hwmod,
	.clk		= "sr_l4_ick",
	.addr		= omap34xx_sr1_addr_space,
	.addr_cnt	= ARRAY_SIZE(omap34xx_sr1_addr_space),
	.user		= OCP_USER_MPU,
};

/* L4 CORE -> SR1 interface */
static struct omap_hwmod_addr_space omap34xx_sr2_addr_space[] = {
	{
		.pa_start	= OMAP34XX_SR2_BASE,
		.pa_end		= OMAP34XX_SR2_BASE + SZ_1K - 1,
		.flags		= ADDR_TYPE_RT,
	},
};

static struct omap_hwmod_ocp_if omap3_l4_core__sr2 = {
	.master		= &omap3xxx_l4_core_hwmod,
	.slave		= &omap34xx_sr2_hwmod,
	.clk		= "sr_l4_ick",
	.addr		= omap34xx_sr2_addr_space,
	.addr_cnt	= ARRAY_SIZE(omap34xx_sr2_addr_space),
	.user		= OCP_USER_MPU,
};

/* Slave interfaces on the L4_CORE interconnect */
static struct omap_hwmod_ocp_if *omap3xxx_l4_core_slaves[] = {
	&omap3xxx_l3__l4_core,
	&omap3_l4_core__sr1,
	&omap3_l4_core__sr2,
};

/* Master interfaces on the L4_CORE interconnect */
static struct omap_hwmod_ocp_if *omap3xxx_l4_core_masters[] = {
	&omap3xxx_l4_core__l4_wkup,
};

/* L4 CORE */
static struct omap_hwmod omap3xxx_l4_core_hwmod = {
	.name		= "l4_core_hwmod",
	.class		= &l4_hwmod_class,
	.masters	= omap3xxx_l4_core_masters,
	.masters_cnt	= ARRAY_SIZE(omap3xxx_l4_core_masters),
	.slaves		= omap3xxx_l4_core_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_l4_core_slaves),
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430)
};

/* Slave interfaces on the L4_PER interconnect */
static struct omap_hwmod_ocp_if *omap3xxx_l4_per_slaves[] = {
	&omap3xxx_l3__l4_per,
};

/* Master interfaces on the L4_PER interconnect */
static struct omap_hwmod_ocp_if *omap3xxx_l4_per_masters[] = {
};

/* L4 PER */
static struct omap_hwmod omap3xxx_l4_per_hwmod = {
	.name		= "l4_per_hwmod",
	.class		= &l4_hwmod_class,
	.masters	= omap3xxx_l4_per_masters,
	.masters_cnt	= ARRAY_SIZE(omap3xxx_l4_per_masters),
	.slaves		= omap3xxx_l4_per_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_l4_per_slaves),
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430)
};

/* Slave interfaces on the L4_WKUP interconnect */
static struct omap_hwmod_ocp_if *omap3xxx_l4_wkup_slaves[] = {
	&omap3xxx_l4_core__l4_wkup,
};

/* Master interfaces on the L4_WKUP interconnect */
static struct omap_hwmod_ocp_if *omap3xxx_l4_wkup_masters[] = {
};

/* L4 WKUP */
static struct omap_hwmod omap3xxx_l4_wkup_hwmod = {
	.name		= "l4_wkup_hwmod",
	.class		= &l4_hwmod_class,
	.masters	= omap3xxx_l4_wkup_masters,
	.masters_cnt	= ARRAY_SIZE(omap3xxx_l4_wkup_masters),
	.slaves		= omap3xxx_l4_wkup_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap3xxx_l4_wkup_slaves),
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430)
};

/* Master interfaces on the MPU device */
static struct omap_hwmod_ocp_if *omap3xxx_mpu_masters[] = {
	&omap3xxx_mpu__l3,
};

/* MPU */
static struct omap_hwmod omap3xxx_mpu_hwmod = {
	.name		= "mpu_hwmod",
	.class		= &mpu_hwmod_class,
	.main_clk	= "arm_fck",
	.masters	= omap3xxx_mpu_masters,
	.masters_cnt	= ARRAY_SIZE(omap3xxx_mpu_masters),
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

/* SR common */
static struct omap_hwmod_sysc_fields omap34xx_sr_sysc_fields = {
	.clkact_shift	= 20,
};

static struct omap_hwmod_class_sysconfig omap34xx_sr_sysc = {
	.sysc_offs	= 0x24,
	.sysc_flags	= (SYSC_HAS_CLOCKACTIVITY | SYSC_NO_CACHE),
	.clockact	= CLOCKACT_TEST_ICLK,
	.sysc_fields	= &omap34xx_sr_sysc_fields,
};

static struct omap_hwmod_class omap34xx_smartreflex_hwmod_class = {
	.name = "smartreflex",
	.sysc = &omap34xx_sr_sysc,
	.rev  = 1,
};

static struct omap_hwmod_sysc_fields omap36xx_sr_sysc_fields = {
	.sidle_shift	= 24,
	.enwkup_shift	= 26
};

static struct omap_hwmod_class_sysconfig omap36xx_sr_sysc = {
	.sysc_offs	= 0x38,
	.idlemodes	= (SIDLE_FORCE | SIDLE_NO | SIDLE_SMART),
	.sysc_flags	= (SYSC_HAS_SIDLEMODE | SYSC_HAS_ENAWAKEUP |
			SYSC_NO_CACHE),
	.sysc_fields	= &omap36xx_sr_sysc_fields,
};

static struct omap_hwmod_class omap36xx_smartreflex_hwmod_class = {
	.name = "smartreflex",
	.sysc = &omap36xx_sr_sysc,
	.rev  = 2,
};

/* SR1 */
static struct omap_hwmod_ocp_if *omap34xx_sr1_slaves[] = {
	&omap3_l4_core__sr1,
};

static u32 omap34xx_sr1_efuse_offs[] = {
	OMAP343X_CONTROL_FUSE_OPP1_VDD1, OMAP343X_CONTROL_FUSE_OPP2_VDD1,
	OMAP343X_CONTROL_FUSE_OPP3_VDD1, OMAP343X_CONTROL_FUSE_OPP4_VDD1,
	OMAP343X_CONTROL_FUSE_OPP5_VDD1,
};

static u32 omap34xx_sr1_test_nvalues[] = {
	0x9A90E6, 0xAABE9A, 0xBBF5C5, 0xBBB292, 0xBBF5C5,
};

static struct omap_smartreflex_dev_data omap34xx_sr1_dev_attr = {
	.efuse_sr_control	= OMAP343X_CONTROL_FUSE_SR,
	.sennenable_shift	= OMAP343X_SR1_SENNENABLE_SHIFT,
	.senpenable_shift	= OMAP343X_SR1_SENPENABLE_SHIFT,
	.efuse_nvalues_offs	= omap34xx_sr1_efuse_offs,
	.test_sennenable	= 0x3,
	.test_senpenable	= 0x3,
	.test_nvalues		= omap34xx_sr1_test_nvalues,
};

static struct omap_hwmod omap34xx_sr1_hwmod = {
	.name		= "sr1_hwmod",
	.class		= &omap34xx_smartreflex_hwmod_class,
	.main_clk	= "sr1_fck",
	.slaves		= omap34xx_sr1_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap34xx_sr1_slaves),
	.dev_attr	= &omap34xx_sr1_dev_attr,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430ES2 |
					CHIP_IS_OMAP3430ES3_0 |
					CHIP_IS_OMAP3430ES3_1),
	.flags		= HWMOD_SET_DEFAULT_CLOCKACT,
	.mpu_irqs	= &omap3_sr1_irq,
	.mpu_irqs_cnt	= 1,
};

static u32 omap36xx_sr1_efuse_offs[] = {
	OMAP3630_CONTROL_FUSE_OPP50_VDD1, OMAP3630_CONTROL_FUSE_OPP100_VDD1,
	OMAP3630_CONTROL_FUSE_OPP120_VDD1, OMAP3630_CONTROL_FUSE_OPPTM_VDD1,
};

static u32 omap36xx_sr1_test_nvalues[] = {
	0x898beb, 0x999b83, 0xaac5a8, 0xaab197,
};

static struct omap_smartreflex_dev_data omap36xx_sr1_dev_attr = {
	.efuse_sr_control	= OMAP343X_CONTROL_FUSE_SR,
	.sennenable_shift	= OMAP343X_SR1_SENNENABLE_SHIFT,
	.senpenable_shift	= OMAP343X_SR1_SENPENABLE_SHIFT,
	.efuse_nvalues_offs	= omap36xx_sr1_efuse_offs,
	.test_sennenable	= 0x1,
	.test_senpenable	= 0x1,
	.test_nvalues		= omap36xx_sr1_test_nvalues,
};

static struct omap_hwmod omap36xx_sr1_hwmod = {
	.name		= "sr1_hwmod",
	.class		= &omap36xx_smartreflex_hwmod_class,
	.main_clk	= "sr1_fck",
	.slaves		= omap34xx_sr1_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap34xx_sr1_slaves),
	.dev_attr	= &omap36xx_sr1_dev_attr,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3630ES1 |
					CHIP_IS_OMAP3630ES1_1 |
					CHIP_IS_OMAP3630ES1_2),
	.mpu_irqs	= &omap3_sr1_irq,
	.mpu_irqs_cnt	= 1,
};

/* SR2 */
static struct omap_hwmod_ocp_if *omap34xx_sr2_slaves[] = {
	&omap3_l4_core__sr2,
};

static u32 omap34xx_sr2_efuse_offs[] = {
	OMAP343X_CONTROL_FUSE_OPP1_VDD2, OMAP343X_CONTROL_FUSE_OPP2_VDD2,
	OMAP343X_CONTROL_FUSE_OPP3_VDD2,
};

static u32 omap34xx_sr2_test_nvalues[] = {
	0x0, 0xAAC098, 0xAB89D9
};

static struct omap_smartreflex_dev_data omap34xx_sr2_dev_attr = {
	.efuse_sr_control	= OMAP343X_CONTROL_FUSE_SR,
	.sennenable_shift	= OMAP343X_SR2_SENNENABLE_SHIFT,
	.senpenable_shift	= OMAP343X_SR2_SENPENABLE_SHIFT,
	.efuse_nvalues_offs	= omap34xx_sr2_efuse_offs,
	.test_sennenable	= 0x3,
	.test_senpenable	= 0x3,
	.test_nvalues		= omap34xx_sr2_test_nvalues,
};

static struct omap_hwmod omap34xx_sr2_hwmod = {
	.name		= "sr2_hwmod",
	.class		= &omap34xx_smartreflex_hwmod_class,
	.main_clk	= "sr2_fck",
	.slaves		= omap34xx_sr2_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap34xx_sr2_slaves),
	.dev_attr	= &omap34xx_sr2_dev_attr,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430ES2 |
					CHIP_IS_OMAP3430ES3_0 |
					CHIP_IS_OMAP3430ES3_1),
	.flags		= HWMOD_SET_DEFAULT_CLOCKACT,
	.mpu_irqs	= &omap3_sr2_irq,
	.mpu_irqs_cnt	= 1,
};

static u32 omap36xx_sr2_efuse_offs[] = {
	OMAP3630_CONTROL_FUSE_OPP50_VDD2, OMAP3630_CONTROL_FUSE_OPP100_VDD2,
};

static u32 omap36xx_sr2_test_nvalues[] = {
	0x898beb, 0x9a8cee,
};

static struct omap_smartreflex_dev_data omap36xx_sr2_dev_attr = {
	.efuse_sr_control	= OMAP343X_CONTROL_FUSE_SR,
	.sennenable_shift	= OMAP343X_SR2_SENNENABLE_SHIFT,
	.senpenable_shift	= OMAP343X_SR2_SENPENABLE_SHIFT,
	.efuse_nvalues_offs	= omap36xx_sr2_efuse_offs,
	.test_sennenable	= 0x1,
	.test_senpenable	= 0x1,
	.test_nvalues		= omap36xx_sr2_test_nvalues,
};

static struct omap_hwmod omap36xx_sr2_hwmod = {
	.name		= "sr2_hwmod",
	.class		= &omap36xx_smartreflex_hwmod_class,
	.main_clk	= "sr2_fck",
	.slaves		= omap34xx_sr2_slaves,
	.slaves_cnt	= ARRAY_SIZE(omap34xx_sr2_slaves),
	.dev_attr	= &omap36xx_sr2_dev_attr,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3630ES1 |
					CHIP_IS_OMAP3630ES1_1 |
					CHIP_IS_OMAP3630ES1_2),
	.mpu_irqs	= &omap3_sr2_irq,
	.mpu_irqs_cnt	= 1,
};

static __initdata struct omap_hwmod *omap3xxx_hwmods[] = {
	&omap34xx_sr1_hwmod,
	&omap34xx_sr2_hwmod,
	&omap36xx_sr1_hwmod,
	&omap36xx_sr2_hwmod,
	NULL,
};

int __init omap3xxx_hwmod_init(void)
{
	return omap_hwmod_init(omap3xxx_hwmods);
}


