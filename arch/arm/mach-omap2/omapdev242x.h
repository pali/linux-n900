/*
 * TI OCP devices present on OMAP242x
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

#ifndef ARCH_ARM_MACH_OMAP2_OMAPDEV_242X_H
#define ARCH_ARM_MACH_OMAP2_OMAPDEV_242X_H

#include <linux/serial_8250.h>

#include <mach/cpu.h>
#include <mach/omapdev.h>

#ifdef CONFIG_ARCH_OMAP2420

/* 242x data from 2420 TRM ES2.1.1 ES2.2 Rev Q */

/* MPU */

static struct omapdev mpu_242x_omapdev = {
	.name		= "mpu_omapdev",
	.pwrdm		= { .name = "mpu_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};


/* DSP/IVA pwrdm */

/* This is IVA1, the ARM7 core on 2420 */
static struct omapdev iva_242x_omapdev = {
	.name		= "iva_omapdev",
	.pwrdm		= { .name = "dsp_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

/* GFX */

/* XXX VGP/MBX split ? */
static struct omapdev gfx_242x_omapdev = {
	.name		= "gfx_omapdev",
	.pwrdm		= { .name = "gfx_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

/* WKUP */

static struct omapdev prcm_242x_omapdev = {
	.name		= "prcm_omapdev",
	.pwrdm		= { .name = "wkup_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

/* CORE */

/* L3 bus configuration: RT, AP, LA, PM blocks */
static struct omapdev l3_242x_omapdev = {
	.name		= "l3_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

/* L4_CORE bus configuration: RT, AP, LA, PM blocks */
static struct omapdev l4_core_242x_omapdev = {
	.name		= "l4_core_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev dsp_242x_omapdev = {
	.name		= "dsp_omapdev",
	.pwrdm		= { .name = "dsp_pwrdm" },
	.pdev_name	= "dsp",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev dsp_mmu_242x_omapdev = {
	.name		= "dsp_mmu_omapdev",
	.pwrdm		= { .name = "dsp_pwrdm" },
	.pdev_name	= "dsp",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

/* Wakeup */

/* on CORE pwrdm in OMAP3 */
static struct omapdev control_242x_omapdev = {
	.name		= "control_omapdev",
	.pwrdm		= { .name = "wkup_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev tap_242x_omapdev = {
	.name		= "tap_omapdev",
	.pwrdm		= { .name = "wkup_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

/* GPIO2-4 is on PER_PWRDM on OMAP3 */
static struct omapdev gpio2_242x_omapdev = {
	.name		= "gpio2_omapdev",
	.pwrdm		= { .name = "wkup_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev gpio3_242x_omapdev = {
	.name		= "gpio3_omapdev",
	.pwrdm		= { .name = "wkup_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev gpio4_242x_omapdev = {
	.name		= "gpio4_omapdev",
	.pwrdm		= { .name = "wkup_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev gptimer12_242x_omapdev = {
	.name		= "gptimer12_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev uart3_242x_omapdev = {
	.name		= "uart3_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "serial8250",
	.pdev_id	= PLAT8250_DEV_PLATFORM,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev mcbsp2_242x_omapdev = {
	.name		= "mcbsp2_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omap-mcbsp",
	.pdev_id	= 2,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

/* aka the "IVA2 wdtimer" */
static struct omapdev wdtimer4_242x_omapdev = {
	.name		= "wdtimer4_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev gptimer2_242x_omapdev = {
	.name		= "gptimer2_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev gptimer3_242x_omapdev = {
	.name		= "gptimer3_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev gptimer4_242x_omapdev = {
	.name		= "gptimer4_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev gptimer5_242x_omapdev = {
	.name		= "gptimer5_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev gptimer6_242x_omapdev = {
	.name		= "gptimer6_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev gptimer7_242x_omapdev = {
	.name		= "gptimer7_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev gptimer8_242x_omapdev = {
	.name		= "gptimer8_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev gptimer9_242x_omapdev = {
	.name		= "gptimer9_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev etb_242x_omapdev = {
	.name		= "etb_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev cwt_242x_omapdev = {
	.name		= "cwt_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev xti_242x_omapdev = {
	.name		= "xti_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "sti",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev dap_242x_omapdev = {
	.name		= "dap_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev dsi_242x_omapdev = {
	.name		= "dsi_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omapdss",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev dsi_pll_242x_omapdev = {
	.name		= "dsi_pll_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omapdss",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev dss_242x_omapdev = {
	.name		= "dss_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omapdss",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev dispc_242x_omapdev = {
	.name		= "dispc_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omapdss",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev rfbi_242x_omapdev = {
	.name		= "rfbi_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omapdss",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev venc_242x_omapdev = {
	.name		= "venc_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omapdss",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev fac_242x_omapdev = {
	.name		= "fac_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev cam_242x_omapdev = {
	.name		= "cam_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omap24xxcam",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev cam_core_242x_omapdev = {
	.name		= "cam_core_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omap24xxcam",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev cam_dma_242x_omapdev = {
	.name		= "cam_dma_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omap24xxcam",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev cam_mmu_242x_omapdev = {
	.name		= "cam_mmu_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omap24xxcam",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

/* Connected to the ARM1136 peripheral port, not an L3/L4 interconnect */
static struct omapdev mpu_intc_242x_omapdev = {
	.name		= "mpu_intc_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev sms_242x_omapdev = {
	.name		= "sms_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev gpmc_242x_omapdev = {
	.name		= "gpmc_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev sdrc_242x_omapdev = {
	.name		= "sdrc_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev ocm_ram_242x_omapdev = {
	.name		= "ocm_ram_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev ocm_rom_242x_omapdev = {
	.name		= "ocm_rom_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev ssi_242x_omapdev = {
	.name		= "ssi_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev ohci_242x_omapdev = {
	.name		= "ohci_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "ohci",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev otg_242x_omapdev = {
	.name		= "otg_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omap_otg",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev sdma_242x_omapdev = {
	.name		= "sdma_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev i2c1_242x_omapdev = {
	.name		= "i2c1_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "i2c_omap",
	.pdev_id	= 1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev i2c2_242x_omapdev = {
	.name		= "i2c2_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "i2c_omap",
	.pdev_id	= 2,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev uart1_242x_omapdev = {
	.name		= "uart1_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "serial8250",
	.pdev_id	= PLAT8250_DEV_PLATFORM,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev uart2_242x_omapdev = {
	.name		= "uart2_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "serial8250",
	.pdev_id	= PLAT8250_DEV_PLATFORM,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev mcbsp1_242x_omapdev = {
	.name		= "mcbsp1_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omap-mcbsp",
	.pdev_id	= 1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev gptimer10_242x_omapdev = {
	.name		= "gptimer10_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev gptimer11_242x_omapdev = {
	.name		= "gptimer11_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev mailbox_242x_omapdev = {
	.name		= "mailbox_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "mailbox",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev mcspi1_242x_omapdev = {
	.name		= "mcspi1_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omap2_mcspi",
	.pdev_id	= 1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev mcspi2_242x_omapdev = {
	.name		= "mcspi2_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omap2_mcspi",
	.pdev_id	= 2,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev mg_242x_omapdev = {
	.name		= "mg_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev hdq_242x_omapdev = {
	.name		= "hdq_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omap_hdq",
	.pdev_id	= 0,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev mspro_242x_omapdev = {
	.name		= "mspro_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

/* Not present on 2430 - present on 3430 in PER pwrdm */
static struct omapdev wdtimer3_242x_omapdev = {
	.name		= "wdtimer3_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev vlynq_242x_omapdev = {
	.name		= "vlynq_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev eac_242x_omapdev = {
	.name		= "eac_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omap24xx-eac",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev mmc_242x_omapdev = {
	.name		= "mmc_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "mmci-omap",
	.pdev_id	= 0,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev gptimer1_242x_omapdev = {
	.name		= "gptimer1_omapdev",
	.pwrdm		= { .name = "wkup_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev omap_32ksynct_242x_omapdev = {
	.name		= "32ksynct_omapdev",
	.pwrdm		= { .name = "wkup_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev gpio1_242x_omapdev = {
	.name		= "gpio1_omapdev",
	.pwrdm		= { .name = "wkup_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

/* aka the "omap wdtimer" on 2430 or the "mpu wdtimer" on 3430 */
static struct omapdev wdtimer2_242x_omapdev = {
	.name		= "wdtimer2_omapdev",
	.pwrdm		= { .name = "wkup_pwrdm" },
	.pdev_name	= "omap_wdt",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

/* Secure-mode devices */

/* aka the "secure wdtimer" */
static struct omapdev wdtimer1_242x_omapdev = {
	.name		= "wdtimer1_omapdev",
	.pwrdm		= { .name = "wkup_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev rng_242x_omapdev = {
	.name		= "rng_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omap_rng",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

/* XXX is the slash in this pdev_name going to wreck sysfs? */
static struct omapdev sha1md5_242x_omapdev = {
	.name		= "sha1md5_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "OMAP SHA1/MD5",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev des_242x_omapdev = {
	.name		= "des_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev aes_242x_omapdev = {
	.name		= "aes_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

static struct omapdev pka_242x_omapdev = {
	.name		= "pka_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP2420),
};

#endif	 /* CONFIG_ARCH_OMAP2420 */


#endif

