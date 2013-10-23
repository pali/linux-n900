/*
 * TI OCP devices present on OMAP243x
 *
 * Copyright (C) 2007-2008 Texas Instruments, Inc.
 * Copyright (C) 2007-2008 Nokia Corporation
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef ARCH_ARM_MACH_OMAP2_OMAPDEV_243X_H
#define ARCH_ARM_MACH_OMAP2_OMAPDEV_243X_H

#include <linux/serial_8250.h>

#include <mach/cpu.h>
#include <mach/omapdev.h>

#ifdef CONFIG_ARCH_OMAP2430

/* 2430 data from 2430 TRM ES2.1 Rev G */

/* XXX add IVA2.1 WUGEN for 2430/3430 ? */


/* MPU */

static struct omapdev mpu_243x_omapdev = {
	.name		= "mpu_omapdev",
	.pwrdm		= { .name = "mpu_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

/* IVA2/DSP */

/* dsp_omapdev is what is used on OMAP243x */
static struct omapdev iva2_243x_omapdev = {
	.name		= "iva2_omapdev",
	.pwrdm		= { .name = "iva2_pwrdm" },
	.pdev_name	= "dsp",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev iva2_mmu_243x_omapdev = {
	.name		= "iva2_mmu_omapdev",
	.pwrdm		= { .name = "iva2_pwrdm" },
	.pdev_name	= "dsp",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};


/* GFX */

/* XXX VGP/MBX split ? */
static struct omapdev gfx_243x_omapdev = {
	.name		= "gfx_omapdev",
	.pwrdm		= { .name = "gfx_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};


/* CORE pwrdm	 */

/* L3 bus configuration: RT, AP, LA, PM blocks */
static struct omapdev l3_243x_omapdev = {
	.name		= "l3_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

/* L4_CORE bus configuration: RT, AP, LA, PM blocks */
static struct omapdev l4_core_243x_omapdev = {
	.name		= "l4_core_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

/* L4_WKUP bus configuration: RT, AP, LA, PM blocks */
static struct omapdev l4_wkup_243x_omapdev = {
	.name		= "l4_wkup_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev dsp_243x_omapdev = {
	.name		= "dsp_omapdev",
	.pwrdm		= { .name = "dsp_pwrdm" },
	.pdev_name	= "dsp",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

/* Wakeup */

/* on CORE pwrdm in OMAP3 */
static struct omapdev control_243x_omapdev = {
	.name		= "control_omapdev",
	.pwrdm		= { .name = "wkup_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev tap_243x_omapdev = {
	.name		= "tap_omapdev",
	.pwrdm		= { .name = "wkup_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

/* GPIO2-4 is on PER_PWRDM on OMAP3 */
static struct omapdev gpio2_243x_omapdev = {
	.name		= "gpio2_omapdev",
	.pwrdm		= { .name = "wkup_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev gpio3_243x_omapdev = {
	.name		= "gpio3_omapdev",
	.pwrdm		= { .name = "wkup_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev gpio4_243x_omapdev = {
	.name		= "gpio4_omapdev",
	.pwrdm		= { .name = "wkup_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev gptimer12_243x_omapdev = {
	.name		= "gptimer12_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev uart3_243x_omapdev = {
	.name		= "uart3_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "serial8250",
	.pdev_id	= PLAT8250_DEV_PLATFORM,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev mcbsp2_243x_omapdev = {
	.name		= "mcbsp2_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omap-mcbsp",
	.pdev_id	= 2,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

/* aka the "IVA2 wdtimer" */
static struct omapdev wdtimer4_243x_omapdev = {
	.name		= "wdtimer4_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "wdt",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev gptimer2_243x_omapdev = {
	.name		= "gptimer2_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev gptimer3_243x_omapdev = {
	.name		= "gptimer3_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev gptimer4_243x_omapdev = {
	.name		= "gptimer4_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev gptimer5_243x_omapdev = {
	.name		= "gptimer5_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev gptimer6_243x_omapdev = {
	.name		= "gptimer6_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev gptimer7_243x_omapdev = {
	.name		= "gptimer7_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev gptimer8_243x_omapdev = {
	.name		= "gptimer8_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev gptimer9_243x_omapdev = {
	.name		= "gptimer9_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev etb_243x_omapdev = {
	.name		= "etb_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev cwt_243x_omapdev = {
	.name		= "cwt_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev xti_243x_omapdev = {
	.name		= "xti_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "sti",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev dap_243x_omapdev = {
	.name		= "dap_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev dsi_243x_omapdev = {
	.name		= "dsi_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omapdss",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev dsi_pll_243x_omapdev = {
	.name		= "dsi_pll_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omapdss",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev dss_243x_omapdev = {
	.name		= "dss_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omapdss",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev dispc_243x_omapdev = {
	.name		= "dispc_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omapdss",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev rfbi_243x_omapdev = {
	.name		= "rfbi_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omapdss",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev venc_243x_omapdev = {
	.name		= "venc_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omapdss",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev fac_243x_omapdev = {
	.name		= "fac_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev cam_243x_omapdev = {
	.name		= "cam_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omap24xxcam",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev cam_core_243x_omapdev = {
	.name		= "cam_core_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omap24xxcam",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev cam_dma_243x_omapdev = {
	.name		= "cam_dma_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omap24xxcam",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev cam_mmu_243x_omapdev = {
	.name		= "cam_mmu_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omap24xxcam",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

/* Connected to the ARM1136 peripheral port, not an L3/L4 interconnect */
static struct omapdev mpu_intc_243x_omapdev = {
	.name		= "mpu_intc_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

/* XXX guessing on this one; TRM does not cover it well */
static struct omapdev modem_intc_243x_omapdev = {
	.name		= "modem_intc_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev sms_243x_omapdev = {
	.name		= "sms_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev gpmc_243x_omapdev = {
	.name		= "gpmc_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev sdrc_243x_omapdev = {
	.name		= "sdrc_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev ocm_ram_243x_omapdev = {
	.name		= "ocm_ram_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev ocm_rom_243x_omapdev = {
	.name		= "ocm_rom_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev sad2d_243x_omapdev = {
	.name		= "sad2d_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev ssi_243x_omapdev = {
	.name		= "ssi_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev ohci_243x_omapdev = {
	.name		= "ohci_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "ohci",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev fsotg_243x_omapdev = {
	.name		= "fsotg_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omap_otg",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev hsotg_243x_omapdev = {
	.name		= "hsotg_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omap_otg",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev sdma_243x_omapdev = {
	.name		= "sdma_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev i2c1_243x_omapdev = {
	.name		= "i2c1_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "i2c_omap",
	.pdev_id	= 1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev i2c2_243x_omapdev = {
	.name		= "i2c2_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "i2c_omap",
	.pdev_id	= 2,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev uart1_243x_omapdev = {
	.name		= "uart1_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "serial8250",
	.pdev_id	= PLAT8250_DEV_PLATFORM,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev uart2_243x_omapdev = {
	.name		= "uart2_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "serial8250",
	.pdev_id	= PLAT8250_DEV_PLATFORM,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev mcbsp1_243x_omapdev = {
	.name		= "mcbsp1_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omap-mcbsp",
	.pdev_id	= 1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev gptimer10_243x_omapdev = {
	.name		= "gptimer10_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev gptimer11_243x_omapdev = {
	.name		= "gptimer11_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev mailbox_243x_omapdev = {
	.name		= "mailbox_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "mailbox",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev mcspi1_243x_omapdev = {
	.name		= "mcspi1_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omap2_mcspi",
	.pdev_id	= 1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev mcspi2_243x_omapdev = {
	.name		= "mcspi2_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omap2_mcspi",
	.pdev_id	= 2,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev mg_243x_omapdev = {
	.name		= "mg_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev hdq_243x_omapdev = {
	.name		= "hdq_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omap_hdq",
	.pdev_id	= 0,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev mspro_243x_omapdev = {
	.name		= "mspro_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev mcbsp5_243x_omapdev = {
	.name		= "mcbsp5_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omap-mcbsp",
	.pdev_id	= 5,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev hsmmc1_243x_omapdev = {
	.name		= "hsmmc1_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "mmci-omap",
	.pdev_id	= 0,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev hsmmc2_243x_omapdev = {
	.name		= "hsmmc2_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "mmci-omap",
	.pdev_id	= 1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev mcspi3_243x_omapdev = {
	.name		= "mcspi3_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omap2_mcspi",
	.pdev_id	= 3,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};



/* WKUP */

static struct omapdev gptimer1_243x_omapdev = {
	.name		= "gptimer1_omapdev",
	.pwrdm		= { .name = "wkup_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev prm_243x_omapdev = {
	.name		= "prm_omapdev",
	.pwrdm		= { .name = "wkup_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev cm_243x_omapdev = {
	.name		= "cm_omapdev",
	.pwrdm		= { .name = "wkup_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev omap_32ksynct_243x_omapdev = {
	.name		= "32ksynct_omapdev",
	.pwrdm		= { .name = "wkup_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev gpio1_243x_omapdev = {
	.name		= "gpio1_omapdev",
	.pwrdm		= { .name = "wkup_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

/* aka the "omap wdtimer" on 2430 or the "mpu wdtimer" on 3430 */
static struct omapdev wdtimer2_243x_omapdev = {
	.name		= "wdtimer2_omapdev",
	.pwrdm		= { .name = "wkup_pwrdm" },
	.pdev_name	= "omap_wdt",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

/* Secure-mode devices */

/* aka the "secure wdtimer" */
static struct omapdev wdtimer1_243x_omapdev = {
	.name		= "wdtimer1_omapdev",
	.pwrdm		= { .name = "wkup_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev rng_243x_omapdev = {
	.name		= "rng_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omap_rng",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev sha1md5_243x_omapdev = {
	.name		= "sha1md5_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "OMAP SHA1/MD5",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev des_243x_omapdev = {
	.name		= "des_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev aes_243x_omapdev = {
	.name		= "aes_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev pka_243x_omapdev = {
	.name		= "pka_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};


static struct omapdev modem_243x_omapdev = {
	.name		= "modem_omapdev",
	.pwrdm		= { .name = "mdm_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev icr_243x_omapdev = {
	.name		= "icr_omapdev",
	.pwrdm		= { .name = "wkup_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

/* These two McBSP instances are in PER on 3430 */
static struct omapdev mcbsp3_243x_omapdev = {
	.name		= "mcbsp3_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omap-mcbsp",
	.pdev_id	= 3,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev mcbsp4_243x_omapdev = {
	.name		= "mcbsp4_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omap-mcbsp",
	.pdev_id	= 4,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

static struct omapdev gpio5_243x_omapdev = {
	.name		= "gpio5_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2430),
};

#endif /* CONFIG_ARCH_OMAP2430 */

#endif
