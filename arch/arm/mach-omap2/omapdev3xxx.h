/*
 * TI OCP devices present on OMAP3xxx
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

#ifndef ARCH_ARM_MACH_OMAP2_OMAPDEV_3XXX_H
#define ARCH_ARM_MACH_OMAP2_OMAPDEV_3XXX_H

#include <linux/serial_8250.h>

#include <mach/cpu.h>
#include <mach/omapdev.h>

#ifdef CONFIG_ARCH_OMAP3

/* 3xxx data from the 34xx ES2 TRM Rev F */

/* MPU */

static struct omapdev mpu_3xxx_omapdev = {
	.name		= "mpu_3xxx_omapdev",
	.pwrdm		= { .name = "mpu_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

/* IVA2/DSP */

/* dsp_omapdev is what is used on OMAP242x */
static struct omapdev iva2_3xxx_omapdev = {
	.name		= "iva2_3xxx_omapdev",
	.pwrdm		= { .name = "iva2_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev iva2_mmu_3xxx_omapdev = {
	.name		= "iva2_mmu_3xxx_omapdev",
	.pwrdm		= { .name = "iva2_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};


/* GFX */

/* XXX VGP/MBX split ? */
static struct omapdev gfx_3xxx_omapdev = {
	.name		= "gfx_omapdev",
	.pwrdm		= { .name = "gfx_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430ES1),
};


/* CORE pwrdm	 */

/* L3 bus configuration: RT, AP, LA, PM blocks */
static struct omapdev l3_3xxx_omapdev = {
	.name		= "l3_3xxx_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

/* L4_CORE bus configuration: RT, AP, LA, PM blocks */
static struct omapdev l4_core_3xxx_omapdev = {
	.name		= "l4_core_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

/* L4_WKUP bus configuration: RT, AP, LA, PM blocks */
static struct omapdev l4_wkup_3xxx_omapdev = {
	.name		= "l4_wkup_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev mpu_intc_3xxx_omapdev = {
	.name		= "mpu_intc_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

/* XXX guessing on this one; TRM does not cover it well */
static struct omapdev modem_intc_3xxx_omapdev = {
	.name		= "modem_intc_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev sms_3xxx_omapdev = {
	.name		= "sms_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev gpmc_3xxx_omapdev = {
	.name		= "gpmc_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev sdrc_3xxx_omapdev = {
	.name		= "sdrc_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev ocm_ram_3xxx_omapdev = {
	.name		= "ocm_ram_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev ocm_rom_3xxx_omapdev = {
	.name		= "ocm_rom_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev sad2d_3xxx_omapdev = {
	.name		= "sad2d_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev ssi_3xxx_omapdev = {
	.name		= "ssi_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omap_ssi",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev sdma_3xxx_omapdev = {
	.name		= "sdma_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev i2c1_3xxx_omapdev = {
	.name		= "i2c1_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "i2c_omap",
	.pdev_id	= 1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev i2c2_3xxx_omapdev = {
	.name		= "i2c2_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "i2c_omap",
	.pdev_id	= 2,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev uart1_3xxx_omapdev = {
	.name		= "uart1_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "serial8250",
	.pdev_id	= PLAT8250_DEV_PLATFORM,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev uart2_3xxx_omapdev = {
	.name		= "uart2_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "serial8250",
	.pdev_id	= PLAT8250_DEV_PLATFORM,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev mcbsp1_3xxx_omapdev = {
	.name		= "mcbsp1_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omap-mcbsp",
	.pdev_id	= 1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev gptimer10_3xxx_omapdev = {
	.name		= "gptimer10_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev gptimer11_3xxx_omapdev = {
	.name		= "gptimer11_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev mailbox_3xxx_omapdev = {
	.name		= "mailbox_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "mailbox",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev mcspi1_3xxx_omapdev = {
	.name		= "mcspi1_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omap2_mcspi",
	.pdev_id	= 1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev mcspi2_3xxx_omapdev = {
	.name		= "mcspi2_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omap2_mcspi",
	.pdev_id	= 2,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev mg_3xxx_omapdev = {
	.name		= "mg_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev hdq_3xxx_omapdev = {
	.name		= "hdq_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omap_hdq",
	.pdev_id	= 0,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev mspro_3xxx_omapdev = {
	.name		= "mspro_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev mcbsp5_3xxx_omapdev = {
	.name		= "mcbsp5_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omap-mcbsp",
	.pdev_id	= 5,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev hsmmc1_3xxx_omapdev = {
	.name		= "hsmmc1_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "mmci-omap-hs",
	.pdev_id	= 0,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev hsmmc2_3xxx_omapdev = {
	.name		= "hsmmc2_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "mmci-omap-hs",
	.pdev_id	= 1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev mcspi3_3xxx_omapdev = {
	.name		= "mcspi3_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omap2_mcspi",
	.pdev_id	= 3,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};



/* WKUP */

static struct omapdev gptimer1_3xxx_omapdev = {
	.name		= "gptimer1_omapdev",
	.pwrdm		= { .name = "wkup_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev prm_3xxx_omapdev = {
	.name		= "prm_omapdev",
	.pwrdm		= { .name = "wkup_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev cm_3xxx_omapdev = {
	.name		= "cm_omapdev",
	.pwrdm		= { .name = "wkup_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev omap_32ksynct_3xxx_omapdev = {
	.name		= "32ksynct_omapdev",
	.pwrdm		= { .name = "wkup_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev gpio1_3xxx_omapdev = {
	.name		= "gpio1_omapdev",
	.pwrdm		= { .name = "wkup_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

/* aka the "omap wdtimer" on 2430 or the "mpu wdtimer" on 3430 */
static struct omapdev wdtimer2_3xxx_omapdev = {
	.name		= "wdtimer2_omapdev",
	.pwrdm		= { .name = "wkup_pwrdm" },
	.pdev_name	= "omap_wdt",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

/* Secure-mode devices */

/* aka the "secure wdtimer" */
static struct omapdev wdtimer1_3xxx_omapdev = {
	.name		= "wdtimer1_omapdev",
	.pwrdm		= { .name = "wkup_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev rng_3xxx_omapdev = {
	.name		= "rng_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
	.pdev_name	= "omap_rng",
	.pdev_id	= -1,
};

static struct omapdev sha1md5_3xxx_omapdev = {
	.name		= "sha1md5_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
	.pdev_name	= "OMAP SHA1/MD5",
	.pdev_id	= -1,
};

static struct omapdev des_3xxx_omapdev = {
	.name		= "des_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev aes_3xxx_omapdev = {
	.name		= "aes_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev pka_3xxx_omapdev = {
	.name		= "pka_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

/* NEON */

static struct omapdev neon_3xxx_omapdev = {
	.name		= "neon_omapdev",
	.pwrdm		= { .name = "neon_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

/* IVA2 */

/* XXX include IVA2 async bridges ? */

/* SGX/GFX */

static struct omapdev sgx_3xxx_omapdev = {
	.name		= "sgx_omapdev",
	.pwrdm		= { .name = "sgx_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_GE_OMAP3430ES2),
};

/* CORE */

static struct omapdev l4_per_3xxx_omapdev = {
	.name		= "l4_per_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev l4_emu_3xxx_omapdev = {
	.name		= "l4_emu_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

/* XXX ICR is present on 2430 & 3430, but is in WKUP on 2430 */
static struct omapdev icr_3xxx_omapdev = {
	.name		= "icr_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

/* IVA2 interrupt controller - XXX 2430 also ? */
static struct omapdev wugen_3xxx_omapdev = {
	.name		= "wugen_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

/* XXX temperature sensor (what is the second one for ?) */

/* XXX CWT is on 2430 at least, what about 2420? */

static struct omapdev mad2d_3xxx_omapdev = {
	.name		= "mad2d_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

/* CONTROL/SCM moved into CORE pwrdm on 3430 */
static struct omapdev control_3xxx_omapdev = {
	.name		= "control_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev i2c3_3xxx_omapdev = {
	.name		= "i2c3_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "i2c_omap",
	.pdev_id	= 3,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev hsmmc3_3xxx_omapdev = {
	.name		= "hsmmc3_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "mmci-omap-hs",
	.pdev_id	= 2,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_GE_OMAP3430ES2),
};

static struct omapdev mcspi4_3xxx_omapdev = {
	.name		= "mcspi4_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.pdev_name	= "omap2_mcspi",
	.pdev_id	= 4,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev sr1_3xxx_omapdev = {
	.name		= "sr1_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev sr2_3xxx_omapdev = {
	.name		= "sr2_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev usbhost_es1_3xxx_omapdev = {
	.name		= "usbhost_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430ES1),
};

static struct omapdev usbotg_es1_3xxx_omapdev = {
	.name		= "usbotg_omapdev",
	.pwrdm		= { .name = "core_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430ES1),
};

/* L4-PER */

static struct omapdev uart3_3xxx_omapdev = {
	.name		= "uart3_omapdev",
	.pwrdm		= { .name = "per_pwrdm" },
	.pdev_name	= "serial8250",
	.pdev_id	= PLAT8250_DEV_PLATFORM,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev mcbsp2_3xxx_omapdev = {
	.name		= "mcbsp2_omapdev",
	.pwrdm		= { .name = "per_pwrdm" },
	.pdev_name	= "omap-mcbsp",
	.pdev_id	= 2,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev mcbsp3_3xxx_omapdev = {
	.name		= "mcbsp3_omapdev",
	.pwrdm		= { .name = "per_pwrdm" },
	.pdev_name	= "omap-mcbsp",
	.pdev_id	= 3,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev mcbsp4_3xxx_omapdev = {
	.name		= "mcbsp4_omapdev",
	.pwrdm		= { .name = "per_pwrdm" },
	.pdev_name	= "omap-mcbsp",
	.pdev_id	= 3,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev mcbsp2_sidetone_3xxx_omapdev = {
	.name		= "mcbsp2_sidetone_omapdev",
	.pwrdm		= { .name = "per_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev mcbsp3_sidetone_3xxx_omapdev = {
	.name		= "mcbsp3_sidetone_omapdev",
	.pwrdm		= { .name = "per_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

/* On 2420 also, but in CORE pwrdm */
/* aka the "iva2" wdtimer */
static struct omapdev wdtimer3_3xxx_omapdev = {
	.name		= "wdtimer3_omapdev",
	.pwrdm		= { .name = "per_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev gptimer2_3xxx_omapdev = {
	.name		= "gptimer2_omapdev",
	.pwrdm		= { .name = "per_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev gptimer3_3xxx_omapdev = {
	.name		= "gptimer3_omapdev",
	.pwrdm		= { .name = "per_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev gptimer4_3xxx_omapdev = {
	.name		= "gptimer4_omapdev",
	.pwrdm		= { .name = "per_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev gptimer5_3xxx_omapdev = {
	.name		= "gptimer5_omapdev",
	.pwrdm		= { .name = "per_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev gptimer6_3xxx_omapdev = {
	.name		= "gptimer6_omapdev",
	.pwrdm		= { .name = "per_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev gptimer7_3xxx_omapdev = {
	.name		= "gptimer7_omapdev",
	.pwrdm		= { .name = "per_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev gptimer8_3xxx_omapdev = {
	.name		= "gptimer8_omapdev",
	.pwrdm		= { .name = "per_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev gptimer9_3xxx_omapdev = {
	.name		= "gptimer9_omapdev",
	.pwrdm		= { .name = "per_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev gpio2_3xxx_omapdev = {
	.name		= "gpio2_omapdev",
	.pwrdm		= { .name = "per_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev gpio3_3xxx_omapdev = {
	.name		= "gpio3_omapdev",
	.pwrdm		= { .name = "per_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev gpio4_3xxx_omapdev = {
	.name		= "gpio4_omapdev",
	.pwrdm		= { .name = "per_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev gpio5_3xxx_omapdev = {
	.name		= "gpio5_omapdev",
	.pwrdm		= { .name = "per_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev gpio6_3xxx_omapdev = {
	.name		= "gpio6_omapdev",
	.pwrdm		= { .name = "per_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

/* L4-WAKEUP */

static struct omapdev tap_3xxx_omapdev = {
	.name		= "tap_omapdev",
	.pwrdm		= { .name = "wkup_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};


/* L4-EMU */

static struct omapdev mpuemu_3xxx_omapdev = {
	.name		= "mpuemu_omapdev",
	.pwrdm		= { .name = "emu_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev tpiu_3xxx_omapdev = {
	.name		= "tpiu_omapdev",
	.pwrdm		= { .name = "emu_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev etb_3xxx_omapdev = {
	.name		= "etb_omapdev",
	.pwrdm		= { .name = "emu_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev dapctl_3xxx_omapdev = {
	.name		= "dapctl_omapdev",
	.pwrdm		= { .name = "emu_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev sdti_3xxx_omapdev = {
	.name		= "sdti_omapdev",
	.pwrdm		= { .name = "emu_pwrdm" },
	.pdev_name	= "sti",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev dap_3xxx_omapdev = {
	.name		= "dap_omapdev",
	.pwrdm		= { .name = "emu_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

/* USBHOST */

static struct omapdev usbhost_3xxx_omapdev = {
	.name		= "usbhost_omapdev",
	.pwrdm		= { .name = "usbhost_pwrdm" },
	.pdev_name	= "ehci-omap",
	.pdev_id	= 0,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_GE_OMAP3430ES2),
};

static struct omapdev usbotg_3xxx_omapdev = {
	.name		= "usbotg_omapdev",
	.pwrdm		= { .name = "usbhost_pwrdm" },
	.pdev_name	= "musb_hdrc",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_GE_OMAP3430ES2),
};

static struct omapdev usbtll_3xxx_omapdev = {
	.name		= "usbtll_omapdev",
	.pwrdm		= { .name = "usbhost_pwrdm" },
	.pdev_name	= "ehci-omap",
	.pdev_id	= 0,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

/* DSS */

static struct omapdev dsi_3xxx_omapdev = {
	.name		= "dsi_omapdev",
	.pwrdm		= { .name = "dss_pwrdm" },
	.pdev_name	= "omapdss",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev dsi_phy_3xxx_omapdev = {
	.name		= "dsi_phy_omapdev",
	.pwrdm		= { .name = "dss_pwrdm" },
	.pdev_name	= "omapdss",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev dsi_pll_3xxx_omapdev = {
	.name		= "dsi_pll_omapdev",
	.pwrdm		= { .name = "dss_pwrdm" },
	.pdev_name	= "omapdss",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev dss_3xxx_omapdev = {
	.name		= "dss_omapdev",
	.pwrdm		= { .name = "dss_pwrdm" },
	.pdev_name	= "omapdss",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev dispc_3xxx_omapdev = {
	.name		= "dispc_omapdev",
	.pwrdm		= { .name = "dss_pwrdm" },
	.pdev_name	= "omapdss",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev rfbi_3xxx_omapdev = {
	.name		= "rfbi_omapdev",
	.pwrdm		= { .name = "dss_pwrdm" },
	.pdev_name	= "omapdss",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev venc_3xxx_omapdev = {
	.name		= "venc_omapdev",
	.pwrdm		= { .name = "dss_pwrdm" },
	.pdev_name	= "omapdss",
	.pdev_id	= -1,
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};


/* CAM */


static struct omapdev isp_3xxx_omapdev = {
	.name		= "isp_omapdev",
	.pwrdm		= { .name = "cam_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev isp_cbuff_3xxx_omapdev = {
	.name		= "isp_cbuff_omapdev",
	.pwrdm		= { .name = "cam_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev ccp2_3xxx_omapdev = {
	.name		= "ccp2_omapdev",
	.pwrdm		= { .name = "cam_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev ccdc_3xxx_omapdev = {
	.name		= "ccdc_omapdev",
	.pwrdm		= { .name = "cam_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev hist_3xxx_omapdev = {
	.name		= "hist_omapdev",
	.pwrdm		= { .name = "cam_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev h3a_3xxx_omapdev = {
	.name		= "h3a_omapdev",
	.pwrdm		= { .name = "cam_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev prv_3xxx_omapdev = {
	.name		= "prv_omapdev",
	.pwrdm		= { .name = "cam_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev rsz_3xxx_omapdev = {
	.name		= "rsz_omapdev",
	.pwrdm		= { .name = "cam_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev sbl_3xxx_omapdev = {
	.name		= "sbl_omapdev",
	.pwrdm		= { .name = "cam_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

static struct omapdev csi2_3xxx_omapdev = {
	.name		= "csi2_omapdev",
	.pwrdm		= { .name = "cam_pwrdm" },
	.omap_chip	= OMAP_CHIP_INIT(CHIP_IS_OMAP3430),
};

#endif	   /* CONFIG_ARCH_OMAP3 */

#endif
