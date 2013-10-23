/*
 * linux/arch/arm/mach-omap2/pm34xx.c
 *
 * OMAP3 Power Management Routines
 *
 * Copyright (C) 2006-2008 Nokia Corporation
 * Tony Lindgren <tony@atomide.com>
 * Jouni Hogander
 *
 * Copyright (C) 2007 Texas Instruments, Inc.
 * Rajendra Nayak <rnayak@ti.com>
 *
 * Copyright (C) 2005 Texas Instruments, Inc.
 * Richard Woodruff <r-woodruff2@ti.com>
 *
 * Based on pm.c for omap1
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/pm.h>
#include <linux/suspend.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/err.h>
#include <linux/delay.h>

#include <mach/gpio.h>
#include <mach/sram.h>
#include <mach/pm.h>
#include <mach/prcm.h>
#include <mach/clockdomain.h>
#include <mach/powerdomain.h>
#include <mach/resource.h>
#include <mach/serial.h>
#include <mach/control.h>
#include <mach/serial.h>
#include <mach/gpio.h>
#include <mach/sdrc.h>
#include <mach/dma.h>
#include <mach/gpmc.h>
#include <mach/dma.h>
#include <mach/vrfb.h>
#include <mach/ssi.h>
#include <mach/omap-pm.h>

#include <asm/tlbflush.h>

#include "cm.h"
#include "cm-regbits-34xx.h"
#include "prm-regbits-34xx.h"

#include "prm.h"
#include "pm.h"
#include "smartreflex.h"
#include "sdrc.h"

/* Scratchpad offsets */
#define OMAP343X_TABLE_ADDRESS_OFFSET	   0x31
#define OMAP343X_TABLE_VALUE_OFFSET	   0x30
#define OMAP343X_CONTROL_REG_VALUE_OFFSET  0x32

/* IDLEST bitmasks for core status checks */
#define CORE_IDLEST1_ALL		(\
		OMAP3430ES2_ST_MMC3_MASK|OMAP3430_ST_ICR_MASK|\
		OMAP3430_ST_AES2_MASK|OMAP3430_ST_SHA12_MASK|\
		OMAP3430_ST_DES2_MASK|OMAP3430_ST_MMC2_MASK|\
		OMAP3430_ST_MMC1_MASK|OMAP3430_ST_MSPRO_MASK|\
		OMAP3430_ST_HDQ_MASK|OMAP3430_ST_MCSPI4_MASK|\
		OMAP3430_ST_MCSPI3_MASK|OMAP3430_ST_MCSPI2_MASK|\
		OMAP3430_ST_MCSPI1_MASK|OMAP3430_ST_I2C3_MASK|\
		OMAP3430_ST_I2C2_MASK|OMAP3430_ST_I2C1_MASK|\
		OMAP3430_ST_UART2_MASK|OMAP3430_ST_UART1_MASK|\
		OMAP3430_ST_GPT11_MASK|OMAP3430_ST_GPT10_MASK|\
		OMAP3430_ST_MCBSP5_MASK|OMAP3430_ST_MCBSP1_MASK|\
		OMAP3430ES2_ST_HSOTGUSB_STDBY_MASK|\
		OMAP3430ES2_ST_SSI_IDLE_MASK|OMAP3430_ST_SDMA_MASK|\
		OMAP3430_ST_SSI_STDBY_MASK|OMAP3430_ST_D2D_MASK)
#define CORE_IDLEST2_ALL		(\
		OMAP3430_ST_PKA_MASK|OMAP3430_ST_AES1_MASK|\
		OMAP3430_ST_RNG_MASK|OMAP3430_ST_SHA11_MASK|\
		OMAP3430_ST_DES1_MASK)
#define CORE_IDLEST3_ALL		(\
		OMAP3430ES2_ST_USBTLL_MASK|OMAP3430ES2_ST_CPEFUSE_MASK)
#define PER_IDLEST_ALL			(\
		OMAP3430_ST_WDT3_MASK|OMAP3430_ST_MCBSP4_MASK|\
		OMAP3430_ST_MCBSP3_MASK|OMAP3430_ST_MCBSP2_MASK|\
		OMAP3430_ST_GPT9_MASK|OMAP3430_ST_GPT8_MASK|\
		OMAP3430_ST_GPT7_MASK|OMAP3430_ST_GPT6_MASK|\
		OMAP3430_ST_GPT5_MASK|OMAP3430_ST_GPT4_MASK|\
		OMAP3430_ST_GPT3_MASK|OMAP3430_ST_GPT2_MASK)

#define SGX_IDLEST_ALL			OMAP_ST_GFX
#define DSS_IDLEST_ALL			(\
		OMAP3430ES2_ST_DSS_STDBY_MASK|\
		OMAP3430ES2_ST_DSS_IDLE_MASK)
#define CAM_IDLEST_ALL			OMAP3430_ST_CAM

#define OMAP343X_SSI_PORT1_BASE		0x48058000
#define CONTROL_PADCONF_MCBSP4_DX	0x158
#define CONTROL_PADCONF_UART1_TX	0x14c

#define VSEL_1200	0x30

static u16 ssi_rx_rdy;
static u16 ssi_tx_dat;
static u16 ssi_tx_flag;
static int ssi_pads_saved;

/* Interrupt controller control register offset */
#define INTC_CONTROL	0x48

struct power_state {
	struct powerdomain *pwrdm;
	u32 next_state;
#ifdef CONFIG_SUSPEND
	u32 saved_state;
#endif
	struct list_head node;
};

static LIST_HEAD(pwrst_list);

static void (*_omap_sram_idle)(u32 *addr, int save_state);

static int (*_omap_save_secure_sram)(u32 *addr);

static void (*saved_idle)(void);

static unsigned int *_sdrc_counters;
static unsigned int save_sdrc_counters[2];

static struct powerdomain *mpu_pwrdm, *neon_pwrdm;
static struct powerdomain *core_pwrdm, *per_pwrdm;
static struct powerdomain *cam_pwrdm, *iva2_pwrdm, *dss_pwrdm, *usb_pwrdm;

static struct prm_setup_times prm_setup = {
	.clksetup = 0xff,
	.voltsetup_time1 = 0xfff,
	.voltsetup_time2 = 0xfff,
	.voltoffset = 0xff,
	.voltsetup2 = 0xff,
};

static inline void omap3_per_save_context(void)
{
	omap3_gpio_save_context();
}

static inline void omap3_per_restore_context(void)
{
	omap3_gpio_restore_context();
}

static void omap3_enable_io_chain(void)
{
	int timeout = 0;

	if (omap_rev() >= OMAP3430_REV_ES3_1) {
		prm_set_mod_reg_bits(OMAP3430_EN_IO_CHAIN, WKUP_MOD, PM_WKEN);
		/* Do a readback to assure write has been done */
		prm_read_mod_reg(WKUP_MOD, PM_WKEN);

		while (!(prm_read_mod_reg(WKUP_MOD, PM_WKST) &
						OMAP3430_ST_IO_CHAIN)) {
			timeout++;
			if (timeout > 1000) {
				printk(KERN_ERR "Wake up daisy chain "
						"activation failed.\n");
				return;
			}
		prm_set_mod_reg_bits(OMAP3430_ST_IO_CHAIN, WKUP_MOD, PM_WKST);
		}
	}
}

static void omap3_disable_io_chain(void)
{
	if (omap_rev() >= OMAP3430_REV_ES3_1)
		prm_clear_mod_reg_bits(OMAP3430_EN_IO_CHAIN, WKUP_MOD, PM_WKEN);
}

/*
 * The following 4 helper functions are a workaround for a hardware bug
 * which causes SSI_RX_RDY, SSI_TX_DAT and SSI_TX_FLAG to be raised
 * erronously after resume from off mode. We work around this issue by
 * putting these pads into tristate mode and enable their internal pull
 * down before we enter off * mode. After resuming off mode, we reset
 * the SSI module and then restore the configuration of these pads to their
 * original state.
 */

static void save_ssi_padconf(void)
{

	ssi_rx_rdy = omap_ctrl_readw(OMAP2_CONTROL_PADCONFS +
					CONTROL_PADCONF_MCBSP4_DX);
	ssi_tx_dat = omap_ctrl_readw(OMAP2_CONTROL_PADCONFS +
					CONTROL_PADCONF_UART1_TX);
	ssi_tx_flag = omap_ctrl_readw(OMAP2_CONTROL_PADCONFS +
					CONTROL_PADCONF_UART1_TX + 2);
}

static void ssi_padconf_save_mode(void)
{
	u32 fck_core1;

	ssi_pads_saved = 0;

	fck_core1 = cm_read_mod_reg(CORE_MOD, CM_FCLKEN1);

	if (fck_core1 & 1)
		return ;

	/* Set pad to save mode and enable pulldown */
	omap_ctrl_writew(0x10f, OMAP2_CONTROL_PADCONFS +
					CONTROL_PADCONF_MCBSP4_DX);
	omap_ctrl_writew(0x10f, OMAP2_CONTROL_PADCONFS +
					CONTROL_PADCONF_UART1_TX);
	omap_ctrl_writew(0x10f, OMAP2_CONTROL_PADCONFS +
					CONTROL_PADCONF_UART1_TX + 2);

	ssi_pads_saved = 1;
}

static void reset_ssi(void)
{
	int timeout = 0;

	/* reset the SSI module */

	cm_set_mod_reg_bits(0x1, CORE_MOD, CM_ICLKEN);
	cm_set_mod_reg_bits(0x1, CORE_MOD, CM_FCLKEN);

	while (cm_read_mod_reg(CORE_MOD, CM_IDLEST1) &
		        OMAP3430ES2_ST_SSI_IDLE_MASK) {
		timeout++;
		if (timeout > 1000)
			break;
		}

	omap_writel(SSI_SOFTRESET, OMAP343X_SSI_PORT1_BASE +
			SSI_SYS_SYSCONFIG_REG);
	dsb();

	timeout = 0;
	while (!(omap_readl(SSI_SYS_SYSSTATUS_REG + OMAP343X_SSI_PORT1_BASE)
			& SSI_RESETDONE)) {
		timeout++;
		if (timeout > 1000)
			break;
		}

	cm_clear_mod_reg_bits(0x1, CORE_MOD, CM_ICLKEN);
	cm_clear_mod_reg_bits(0x1, CORE_MOD, CM_FCLKEN);
}

static void restore_ssi_padconf(void)
{
	if (ssi_pads_saved) {
		/* restore the SSI pads configuration */
		omap_ctrl_writew(ssi_rx_rdy, OMAP2_CONTROL_PADCONFS +
				CONTROL_PADCONF_MCBSP4_DX);
		omap_ctrl_writew(ssi_tx_dat, OMAP2_CONTROL_PADCONFS +
				CONTROL_PADCONF_UART1_TX);
		omap_ctrl_writew(ssi_tx_flag, OMAP2_CONTROL_PADCONFS +
				CONTROL_PADCONF_UART1_TX + 2);
		ssi_pads_saved = 0;
	}
}

int pm_check_idle(void)
{
	if ((cm_read_mod_reg(CORE_MOD, CM_IDLEST1) & CORE_IDLEST1_ALL)
			!= CORE_IDLEST1_ALL)
		return 0;
	if ((cm_read_mod_reg(CORE_MOD, CM_IDLEST2) & CORE_IDLEST2_ALL)
			!= CORE_IDLEST2_ALL)
		return 0;
	if ((cm_read_mod_reg(CORE_MOD, OMAP3430_CM_IDLEST3) & CORE_IDLEST3_ALL)
			!= CORE_IDLEST3_ALL)
		return 0;
	if ((cm_read_mod_reg(OMAP3430_PER_MOD, CM_IDLEST1) & PER_IDLEST_ALL)
			!= PER_IDLEST_ALL)
		return 0;
	if (cm_read_mod_reg(OMAP3430ES2_SGX_MOD, CM_IDLEST1) != SGX_IDLEST_ALL)
		return 0;
	if (cm_read_mod_reg(OMAP3430_CAM_MOD, CM_IDLEST1) != CAM_IDLEST_ALL)
		return 0;
	if (cm_read_mod_reg(OMAP3430_DSS_MOD, CM_IDLEST1) != DSS_IDLEST_ALL)
		return 0;
	return 1;
}

static void omap3_core_save_context(void)
{
	u32 control_padconf_off;

	/* Save the padconf registers */
	control_padconf_off =
	omap_ctrl_readl(OMAP343X_CONTROL_PADCONF_OFF);
	control_padconf_off |= START_PADCONF_SAVE;
	omap_ctrl_writel(control_padconf_off, OMAP343X_CONTROL_PADCONF_OFF);
	/* wait for the save to complete */
	while (!(omap_ctrl_readl(OMAP343X_CONTROL_GENERAL_PURPOSE_STATUS)
			& PADCONF_SAVE_DONE))
		udelay(1);

	/*
	 * Force write last pad into memory, as this can fail in some
	 * cases according to errata XYZ
	 */
	omap_ctrl_writel(omap_ctrl_readl(OMAP343X_PADCONF_ETK_D14),
		OMAP343X_CONTROL_MEM_WKUP + 0x2a0);

	/* Save the Interrupt controller context */
	omap3_intc_save_context();
	/* Save the GPMC context */
	omap3_gpmc_save_context();
	/* The VRFB context is saved while it's being configured */
	/* Save the system control module context, padconf already save above*/
	omap3_control_save_context();
	omap_dma_global_context_save();
}

static void omap3_core_restore_context(void)
{
	/* Restore the control module context, padconf restored by h/w */
	omap3_control_restore_context();
	/* Restore the GPMC context */
	omap3_gpmc_restore_context();
	/* Restore the VRFB context */
	omap_vrfb_restore_context();
	/* Restore the interrupt controller context */
	omap3_intc_restore_context();
	omap_dma_global_context_restore();
}

/*
 * FIXME: This function should be called before entering off-mode after
 * OMAP3 secure services have been accessed. Currently it is only called
 * once during boot sequence, but this works as we are not using secure
 * services.
 */
static void omap3_save_secure_ram_context(u32 target_mpu_state)
{
	u32 ret;

	if (omap_type() != OMAP2_DEVICE_TYPE_GP) {
		/*
		 * MPU next state must be set to POWER_ON temporarily,
		 * otherwise the WFI executed inside the ROM code
		 * will hang the system.
		 */
		pwrdm_set_next_pwrst(mpu_pwrdm, PWRDM_POWER_ON);
		ret = _omap_save_secure_sram((u32 *)
				__pa(omap3_secure_ram_storage));
		pwrdm_set_next_pwrst(mpu_pwrdm, target_mpu_state);
		/* Following is for error tracking, it should not happen */
		if (ret) {
			printk(KERN_ERR "save_secure_sram() returns %08x\n",
				ret);
			while (1)
				;
		}
	}
}

/* PRCM Interrupt Handler for wakeups */
static irqreturn_t prcm_interrupt_handler (int irq, void *dev_id)
{
	u32 wkst, irqstatus_mpu;
	u32 fclk, iclk, clken_pll;

	/* Ensure that DPLL4_M2X2_CLK path is powered up */
	clken_pll = cm_read_mod_reg(OMAP3430_CCR_MOD, CM_CLKEN);
	cm_clear_mod_reg_bits(1 << OMAP3430_PWRDN_96M_SHIFT,
			      OMAP3430_CCR_MOD, CM_CLKEN);

	/* WKUP */
	wkst = prm_read_mod_reg(WKUP_MOD, PM_WKST);
	if (wkst) {
		iclk = cm_read_mod_reg(WKUP_MOD, CM_ICLKEN);
		fclk = cm_read_mod_reg(WKUP_MOD, CM_FCLKEN);
		cm_set_mod_reg_bits(wkst, WKUP_MOD, CM_ICLKEN);
		cm_set_mod_reg_bits(wkst, WKUP_MOD, CM_FCLKEN);
		prm_write_mod_reg(wkst, WKUP_MOD, PM_WKST);
		prm_read_mod_reg(WKUP_MOD, PM_WKST);
		cm_write_mod_reg(iclk, WKUP_MOD, CM_ICLKEN);
		cm_write_mod_reg(fclk, WKUP_MOD, CM_FCLKEN);
	}

	/* CORE */
	wkst = prm_read_mod_reg(CORE_MOD, PM_WKST1);
	if (wkst) {
		iclk = cm_read_mod_reg(CORE_MOD, CM_ICLKEN1);
		fclk = cm_read_mod_reg(CORE_MOD, CM_FCLKEN1);
		cm_set_mod_reg_bits(wkst, CORE_MOD, CM_ICLKEN1);
		cm_set_mod_reg_bits(wkst, CORE_MOD, CM_FCLKEN1);
		prm_write_mod_reg(wkst, CORE_MOD, PM_WKST1);
		prm_read_mod_reg(CORE_MOD, PM_WKST1);
		cm_write_mod_reg(iclk, CORE_MOD, CM_ICLKEN1);
		cm_write_mod_reg(fclk, CORE_MOD, CM_FCLKEN1);
	}
	wkst = prm_read_mod_reg(CORE_MOD, OMAP3430ES2_PM_WKST3);
	if (wkst) {
		iclk = cm_read_mod_reg(CORE_MOD, CM_ICLKEN3);
		fclk = cm_read_mod_reg(CORE_MOD, OMAP3430ES2_CM_FCLKEN3);
		cm_set_mod_reg_bits(wkst, CORE_MOD, CM_ICLKEN3);
		cm_set_mod_reg_bits(wkst, CORE_MOD, OMAP3430ES2_CM_FCLKEN3);
		prm_write_mod_reg(wkst, CORE_MOD, OMAP3430ES2_PM_WKST3);
		prm_read_mod_reg(CORE_MOD, OMAP3430ES2_PM_WKST3);
		cm_write_mod_reg(iclk, CORE_MOD, CM_ICLKEN3);
		cm_write_mod_reg(fclk, CORE_MOD, OMAP3430ES2_CM_FCLKEN3);
	}

	/* PER */
	wkst = prm_read_mod_reg(OMAP3430_PER_MOD, PM_WKST);
	if (wkst) {
		iclk = cm_read_mod_reg(OMAP3430_PER_MOD, CM_ICLKEN);
		fclk = cm_read_mod_reg(OMAP3430_PER_MOD, CM_FCLKEN);
		cm_set_mod_reg_bits(wkst, OMAP3430_PER_MOD, CM_ICLKEN);
		cm_set_mod_reg_bits(wkst, OMAP3430_PER_MOD, CM_FCLKEN);
		prm_write_mod_reg(wkst, OMAP3430_PER_MOD, PM_WKST);
		prm_read_mod_reg(OMAP3430_PER_MOD, PM_WKST);
		cm_write_mod_reg(iclk, OMAP3430_PER_MOD, CM_ICLKEN);
		cm_write_mod_reg(fclk, OMAP3430_PER_MOD, CM_FCLKEN);
	}

	if (omap_rev() > OMAP3430_REV_ES1_0) {
		/* USBHOST */
		wkst = prm_read_mod_reg(OMAP3430ES2_USBHOST_MOD, PM_WKST);
		if (wkst) {
			iclk = cm_read_mod_reg(OMAP3430ES2_USBHOST_MOD,
					       CM_ICLKEN);
			fclk = cm_read_mod_reg(OMAP3430ES2_USBHOST_MOD,
					       CM_FCLKEN);
			cm_set_mod_reg_bits(wkst, OMAP3430ES2_USBHOST_MOD,
					 CM_ICLKEN);
			cm_set_mod_reg_bits(wkst, OMAP3430ES2_USBHOST_MOD,
					 CM_FCLKEN);
			prm_write_mod_reg(wkst, OMAP3430ES2_USBHOST_MOD,
					  PM_WKST);
			prm_read_mod_reg(OMAP3430ES2_USBHOST_MOD, PM_WKST);
			cm_write_mod_reg(iclk, OMAP3430ES2_USBHOST_MOD,
					 CM_ICLKEN);
			cm_write_mod_reg(fclk, OMAP3430ES2_USBHOST_MOD,
					 CM_FCLKEN);
		}
	}

	cm_write_mod_reg(clken_pll, OMAP3430_CCR_MOD, CM_CLKEN);

	irqstatus_mpu = prm_read_mod_reg(OCP_MOD,
					OMAP2_PRM_IRQSTATUS_MPU_OFFSET);
	prm_write_mod_reg(irqstatus_mpu, OCP_MOD,
					OMAP2_PRM_IRQSTATUS_MPU_OFFSET);

	prm_read_mod_reg(OCP_MOD, OMAP2_PRM_IRQSTATUS_MPU_OFFSET);

	return IRQ_HANDLED;
}

static void restore_control_register(u32 val)
{
	__asm__ __volatile__ ("mcr p15, 0, %0, c1, c0, 0" : : "r" (val));
}

void omap3_save_neon_context(void)
{
#ifdef CONFIG_VFP
	vfp_pm_save_context();
#endif
}

void omap3_restore_neon_context(void)
{
	return;
}

/* Function to restore the table entry that was modified for enabling MMU */
static void restore_table_entry(void)
{
	u32 *scratchpad_address;
	u32 previous_value, control_reg_value;
	u32 *address;
	scratchpad_address = OMAP2_IO_ADDRESS(OMAP343X_SCRATCHPAD);
	/* Get address of entry that was modified */
	address = (u32 *)__raw_readl(scratchpad_address
					+ OMAP343X_TABLE_ADDRESS_OFFSET);
	/* Get the previous value which needs to be restored */
	previous_value = __raw_readl(scratchpad_address
					+ OMAP343X_TABLE_VALUE_OFFSET);
	address = __va(address);
	*address = previous_value;
	flush_tlb_all();
	control_reg_value = __raw_readl(scratchpad_address
					+ OMAP343X_CONTROL_REG_VALUE_OFFSET);
	/* This will enable caches and prediction */
	restore_control_register(control_reg_value);
}

void omap_sram_idle(void)
{
	/* Variable to tell what needs to be saved and restored
	 * in omap_sram_idle*/
	/* save_state = 0 => Nothing to save and restored */
	/* save_state = 1 => Only L1 and logic lost */
	/* save_state = 2 => Only L2 lost */
	/* save_state = 3 => L1, L2 and logic lost */
	int save_state = 0;
	int mpu_next_state = PWRDM_POWER_ON;
	int neon_next_state = PWRDM_POWER_ON;
	int per_next_state = PWRDM_POWER_ON;
	int core_next_state = PWRDM_POWER_ON;
	int dss_state = PWRDM_POWER_ON;
	int iva2_state = PWRDM_POWER_ON;
	int usb_state = PWRDM_POWER_ON;
	int core_prev_state, per_prev_state;
	u32 sdrc_pwr = 0;
	int per_state_modified = 0;
	int core_saved_state = PWRDM_POWER_ON;
	static int prev_dpll3_div = 0;

	if (!_omap_sram_idle)
		return;

	pwrdm_clear_all_prev_pwrst(mpu_pwrdm);
	pwrdm_clear_all_prev_pwrst(neon_pwrdm);
	pwrdm_clear_all_prev_pwrst(core_pwrdm);
	pwrdm_clear_all_prev_pwrst(per_pwrdm);

	mpu_next_state = pwrdm_read_next_pwrst(mpu_pwrdm);
	switch (mpu_next_state) {
	case PWRDM_POWER_ON:
	case PWRDM_POWER_RET:
		/* No need to save context */
		save_state = 0;
		break;
	case PWRDM_POWER_OFF:
		save_state = 3;
		break;
	default:
		/* Invalid state */
		printk(KERN_ERR "Invalid mpu state in sram_idle\n");
		return;
	}

	pwrdm_pre_transition();

	/* NEON control */
	if (pwrdm_read_pwrst(neon_pwrdm) == PWRDM_POWER_ON) {
		pwrdm_set_next_pwrst(neon_pwrdm, mpu_next_state);
		neon_next_state = mpu_next_state;
		if (neon_next_state == PWRDM_POWER_OFF) 
			omap3_save_neon_context();
	}

	/* Get powerdomain state data */
	core_next_state = pwrdm_read_next_pwrst(core_pwrdm);
	dss_state = pwrdm_read_pwrst(dss_pwrdm);
	iva2_state = pwrdm_read_pwrst(iva2_pwrdm);
	usb_state = pwrdm_read_pwrst(usb_pwrdm);
	per_next_state = pwrdm_read_next_pwrst(per_pwrdm);

	if (dss_state == PWRDM_POWER_ON &&
		core_next_state < PWRDM_POWER_INACTIVE) {
		core_next_state = PWRDM_POWER_INACTIVE;
		pwrdm_set_next_pwrst(core_pwrdm, PWRDM_POWER_ON);
	}

	/* Check if PER domain can enter OFF or not */
	if (per_next_state == PWRDM_POWER_OFF) {
		if ((cm_read_mod_reg(OMAP3430_PER_MOD, CM_IDLEST) &
				PER_IDLEST_ALL) != PER_IDLEST_ALL) {
			per_next_state = PWRDM_POWER_RET;
			pwrdm_set_next_pwrst(per_pwrdm, per_next_state);
			per_state_modified = 1;
		}
	}
	/*
	 * Check whether core will enter idle or not. This is needed
	 * because I/O pad wakeup will fail if core stays on and PER
	 * enters off. This will also prevent unnecessary core context
	 * save / restore.
	 */
	if (core_next_state < PWRDM_POWER_ON) {
		core_saved_state = core_next_state;
		if ((cm_read_mod_reg(CORE_MOD, CM_IDLEST1) & CORE_IDLEST1_ALL)
				!= CORE_IDLEST1_ALL ||
		    (cm_read_mod_reg(CORE_MOD, CM_IDLEST2) & CORE_IDLEST2_ALL)
				!= CORE_IDLEST2_ALL ||
		    (cm_read_mod_reg(CORE_MOD, OMAP3430_CM_IDLEST3) &
				CORE_IDLEST3_ALL) != CORE_IDLEST3_ALL) {
			core_next_state = PWRDM_POWER_ON;
			pwrdm_set_next_pwrst(core_pwrdm, PWRDM_POWER_ON);
		} else if (core_next_state == PWRDM_POWER_OFF &&
				 (dss_state == PWRDM_POWER_ON ||
				  iva2_state >= PWRDM_POWER_RET ||
				  usb_state >= PWRDM_POWER_RET ||
				  per_next_state >= PWRDM_POWER_RET)) {
			core_next_state = PWRDM_POWER_RET;
			pwrdm_set_next_pwrst(core_pwrdm, PWRDM_POWER_RET);
		}
	}

	/* PER */
	if (per_next_state < PWRDM_POWER_ON) {
		omap_uart_prepare_idle(2);
		omap2_gpio_prepare_for_idle(per_next_state);
		if (per_next_state == PWRDM_POWER_OFF) {
			if (core_next_state == PWRDM_POWER_ON) {
				per_next_state = PWRDM_POWER_RET;
				pwrdm_set_next_pwrst(per_pwrdm, per_next_state);
				per_state_modified = 1;
			} else
				omap3_per_save_context();
		}
	}

	if (pwrdm_read_pwrst(cam_pwrdm) == PWRDM_POWER_ON)
		omap2_clkdm_deny_idle(mpu_pwrdm->pwrdm_clkdms[0]);

	/*
	 * Disable smartreflex before entering WFI.
	 * Only needed if we are going to enter retention.
	 */
	if (mpu_next_state < PWRDM_POWER_ON)
		disable_smartreflex(SR1);
	if (core_next_state < PWRDM_POWER_ON)
		disable_smartreflex(SR2);

	/* CORE */
	if (core_next_state < PWRDM_POWER_ON) {
		omap_uart_prepare_idle(0);
		omap_uart_prepare_idle(1);
		if (core_next_state == PWRDM_POWER_OFF) {
			prm_set_mod_reg_bits(OMAP3430_AUTO_OFF,
					     OMAP3430_GR_MOD,
					     OMAP3_PRM_VOLTCTRL_OFFSET);
			ssi_padconf_save_mode();
			omap3_core_save_context();
			omap3_prcm_save_context();
		}
		/* Enable IO-PAD and IO-CHAIN wakeups */
		prm_set_mod_reg_bits(OMAP3430_EN_IO, WKUP_MOD, PM_WKEN);
		omap3_enable_io_chain();
	}

	/*
	 * Force SDRAM controller to self-refresh mode after timeout on
	 * autocount. This is needed on ES3.0 to avoid SDRAM controller
	 * hang-ups.
	 */
	if (omap_rev() >= OMAP3430_REV_ES3_0 &&
	    omap_type() != OMAP2_DEVICE_TYPE_GP &&
	    core_next_state == PWRDM_POWER_OFF) {
		sdrc_pwr = sdrc_read_reg(SDRC_POWER);
		sdrc_write_reg((sdrc_pwr &
			~(SDRC_POWER_AUTOCOUNT_MASK|SDRC_POWER_CLKCTRL_MASK)) |
			(1 << SDRC_POWER_AUTOCOUNT_SHIFT) |
			SDRC_SELF_REFRESH_ON_AUTOCOUNT, SDRC_POWER);
	}

	/* Write voltage setup times which are changed dynamically */
	if (core_next_state == PWRDM_POWER_OFF) {
		prm_write_mod_reg(0, OMAP3430_GR_MOD,
				OMAP3_PRM_VOLTSETUP1_OFFSET);
		prm_write_mod_reg(prm_setup.voltsetup2, OMAP3430_GR_MOD,
				  OMAP3_PRM_VOLTSETUP2_OFFSET);
		prm_write_mod_reg(prm_setup.clksetup, OMAP3430_GR_MOD,
				  OMAP3_PRM_CLKSETUP_OFFSET);
	} else {
		prm_write_mod_reg((prm_setup.voltsetup_time2 <<
				   OMAP3430_SETUP_TIME2_SHIFT) |
				  (prm_setup.voltsetup_time1 <<
				   OMAP3430_SETUP_TIME1_SHIFT),
				  OMAP3430_GR_MOD, OMAP3_PRM_VOLTSETUP1_OFFSET);
		prm_write_mod_reg(0, OMAP3430_GR_MOD,
				  OMAP3_PRM_VOLTSETUP2_OFFSET);
		/*
		 * Use static 1 as only HF_CLKOUT is turned off.
		 * Value taken from application note SWPA152
		 */
		prm_write_mod_reg(0x1, OMAP3430_GR_MOD,
				  OMAP3_PRM_CLKSETUP_OFFSET);
	}

	if (core_next_state < PWRDM_POWER_INACTIVE) {
		u32 clksel1_pll, v;

		clksel1_pll = cm_read_mod_reg(PLL_MOD, OMAP3430_CM_CLKSEL1_PLL);
		prev_dpll3_div = clksel1_pll >> 28;
		if (prev_dpll3_div == 1) {
			/* L3 @ 166Mhz */
			struct omap_sdrc_params *sdrc_cs0;
			struct omap_sdrc_params *sdrc_cs1;

			omap2_sdrc_get_params(83*1000*1000, &sdrc_cs0, &sdrc_cs1);
			/* scale down to 83Mhz, use worst case delay for clock stabilization */
			omap3_configure_core_dpll(4, 0, 28, 0, sdrc_cs0->rfr_ctrl, sdrc_cs0->mr, 0, 0);

			/* increase voltage to 1.2V */
			sr_voltagescale_vcbypass(PRCM_VDD2_OPP3, PRCM_VDD2_OPP2, VSEL_1200, l3_opps[3].vsel);
		} else {
			/* L3 @ 83Mhz, increase voltage to 1.2V  */
			sr_voltagescale_vcbypass(PRCM_VDD2_OPP3, PRCM_VDD2_OPP2, VSEL_1200, l3_opps[2].vsel);
		}

		/* enable DPLL3 autoidle */
		v = cm_read_mod_reg(PLL_MOD, CM_AUTOIDLE);
		v |= 1;
		cm_write_mod_reg(v, PLL_MOD, CM_AUTOIDLE);
	}	

	memcpy(save_sdrc_counters, _sdrc_counters, sizeof(save_sdrc_counters));

	/*
	 * omap3_arm_context is the location where ARM registers
	 * get saved. The restore path then reads from this
	 * location and restores them back.
	 */
	_omap_sram_idle(omap3_arm_context, save_state);

	/* Restore normal SDRAM settings */
	if (omap_rev() >= OMAP3430_REV_ES3_0 &&
	    omap_type() != OMAP2_DEVICE_TYPE_GP &&
	    core_next_state == PWRDM_POWER_OFF)
		sdrc_write_reg(sdrc_pwr, SDRC_POWER);

	/* Restore table entry modified during MMU restoration */
	if (pwrdm_read_prev_pwrst(mpu_pwrdm) == PWRDM_POWER_OFF)
		restore_table_entry();

	if (neon_next_state == PWRDM_POWER_OFF)
		omap3_restore_neon_context();

	if (core_next_state < PWRDM_POWER_INACTIVE) {
		if (pwrdm_read_prev_pwrst(core_pwrdm) == PWRDM_POWER_OFF) {
			u32 clksel1_pll;

			/* ROM code restored the scratchpad settings. So DPLL3 autoidle is
			 * disabled and L3 clock is back to the value before entering this function.
			 * This means we only have to lower the voltage if L3 runs at 83Mhz
			 */
			clksel1_pll = cm_read_mod_reg(PLL_MOD, OMAP3430_CM_CLKSEL1_PLL);
			if ((clksel1_pll >> 28) == 2) {
				/* restore VDD2 OPP2 voltage */
				sr_voltagescale_vcbypass(PRCM_VDD2_OPP2, PRCM_VDD2_OPP3, l3_opps[2].vsel, VSEL_1200);
			}
			else {
				/* restore VDD2 OPP3 voltage */
				sr_voltagescale_vcbypass(PRCM_VDD2_OPP2, PRCM_VDD2_OPP3, l3_opps[3].vsel, VSEL_1200);
			}
		}
		else {
			u32 v;

			/* disable DPLL3 autoidle */
			v = cm_read_mod_reg(PLL_MOD, CM_AUTOIDLE);
			v &= ~0x7;
			cm_write_mod_reg(v, PLL_MOD, CM_AUTOIDLE);

			if (prev_dpll3_div == 1) {
				/* restore L3 to 166Mhz */
				struct omap_sdrc_params *sdrc_cs0;
				struct omap_sdrc_params *sdrc_cs1;

				omap2_sdrc_get_params(166*1000*1000, &sdrc_cs0, &sdrc_cs1);
				/* scale up to 166Mhz, use worst case delay for clock stabilization */
				omap3_configure_core_dpll(2, 0, 28, 1, sdrc_cs0->rfr_ctrl, sdrc_cs0->mr, 0, 0);

				/* restore VDD2 OPP3 voltage */
				sr_voltagescale_vcbypass(PRCM_VDD2_OPP2, PRCM_VDD2_OPP3, l3_opps[3].vsel, VSEL_1200);
			}
			else {
				/* restore VDD2 OPP2 voltage */
				sr_voltagescale_vcbypass(PRCM_VDD2_OPP2, PRCM_VDD2_OPP3, l3_opps[2].vsel, VSEL_1200);
			}
		}
	}

	/* CORE */
	if (core_next_state < PWRDM_POWER_ON) {
		core_prev_state = pwrdm_read_prev_pwrst(core_pwrdm);
		if (core_prev_state == PWRDM_POWER_OFF) {
			omap3_core_restore_context();
			omap3_prcm_restore_context();
			omap3_sram_restore_context();
			omap2_sms_restore_context();
			reset_ssi();
			memcpy(_sdrc_counters, save_sdrc_counters, sizeof(save_sdrc_counters));
		}
		omap_uart_resume_idle(0);
		omap_uart_resume_idle(1);
		if (core_next_state == PWRDM_POWER_OFF) {
			prm_clear_mod_reg_bits(OMAP3430_AUTO_OFF,
					       OMAP3430_GR_MOD,
					       OMAP3_PRM_VOLTCTRL_OFFSET);
			restore_ssi_padconf();
		}
	}

	/*
	 * Enable smartreflex after WFI. Only needed if we
	 * entered retention.
	 */
	if (mpu_next_state < PWRDM_POWER_ON)
		enable_smartreflex(SR1);
	if (core_next_state < PWRDM_POWER_ON)
		enable_smartreflex(SR2);

	if (core_saved_state != core_next_state)
		pwrdm_set_next_pwrst(core_pwrdm, core_saved_state);

	/* PER */
	if (per_next_state < PWRDM_POWER_ON) {
		per_prev_state = pwrdm_read_prev_pwrst(per_pwrdm);
		if (per_prev_state == PWRDM_POWER_OFF) {
			omap3_per_restore_context();
			omap3_gpio_restore_pad_context(0);
		} else if (per_next_state == PWRDM_POWER_OFF)
			omap3_gpio_restore_pad_context(1);
		omap2_gpio_resume_after_idle();
		omap_uart_resume_idle(2);
		if (per_state_modified)
			pwrdm_set_next_pwrst(per_pwrdm, PWRDM_POWER_OFF);
	}

	/* Disable IO-PAD and IO-CHAIN wakeup */
	if (core_next_state < PWRDM_POWER_ON) {
		prm_clear_mod_reg_bits(OMAP3430_EN_IO, WKUP_MOD, PM_WKEN);
		omap3_disable_io_chain();
	}


	pwrdm_post_transition();

	omap2_clkdm_allow_idle(mpu_pwrdm->pwrdm_clkdms[0]);
}

int omap3_can_sleep(void)
{
	if (!enable_dyn_sleep)
		return 0;
	if (!omap_uart_can_sleep())
		return 0;
	if (atomic_read(&sleep_block) > 0)
		return 0;
	return 1;
}

/* This sets pwrdm state (other than mpu & core. Currently only ON &
 * RET are supported. Function is assuming that clkdm doesn't have
 * hw_sup mode enabled. */
int set_pwrdm_state(struct powerdomain *pwrdm, u32 state)
{
	u32 cur_state;
	int sleep_switch = 0;
	int ret = 0;

	if (pwrdm == NULL || IS_ERR(pwrdm))
		return -EINVAL;

	while (!(pwrdm->pwrsts & (1 << state))) {
		if (state == PWRDM_POWER_OFF)
			return ret;
		state--;
	}

	cur_state = pwrdm_read_next_pwrst(pwrdm);
	if (cur_state == state)
		return ret;

	if (pwrdm_read_pwrst(pwrdm) < PWRDM_POWER_ON) {
		omap2_clkdm_wakeup(pwrdm->pwrdm_clkdms[0]);
		sleep_switch = 1;
		pwrdm_wait_transition(pwrdm);
	}

	ret = pwrdm_set_next_pwrst(pwrdm, state);
	if (ret) {
		printk(KERN_ERR "Unable to set state of powerdomain: %s\n",
		       pwrdm->name);
		goto err;
	}

	if (sleep_switch) {
		omap2_clkdm_allow_idle(pwrdm->pwrdm_clkdms[0]);
		pwrdm_wait_transition(pwrdm);
		pwrdm_state_switch(pwrdm);
	}

err:
	return ret;
}

/* return a pointer to the sdrc counters */
unsigned int *omap3_get_sdrc_counters(void)
{
	return _sdrc_counters;
}

static void omap3_pm_idle(void)
{
	local_irq_disable();
	local_fiq_disable();

	if (!omap3_can_sleep())
		goto out;

	if (omap_irq_pending() || need_resched())
		goto out;

	omap_sram_idle();

out:
	local_fiq_enable();
	local_irq_enable();
}

#ifdef CONFIG_SUSPEND
static int omap3_pm_prepare(void)
{
	saved_idle = pm_idle;
	pm_idle = NULL;
	return 0;
}

static int omap3_pm_suspend(void)
{
	struct power_state *pwrst;
	int state, ret = 0;

	/* Read current next_pwrsts */
	list_for_each_entry(pwrst, &pwrst_list, node)
		pwrst->saved_state = pwrdm_read_next_pwrst(pwrst->pwrdm);
	/* Set ones wanted by suspend */
	list_for_each_entry(pwrst, &pwrst_list, node) {
		/* Special handling for IVA2, just use current sleep state */
		if (pwrst->pwrdm == iva2_pwrdm) {
			state = pwrdm_read_pwrst(pwrst->pwrdm);
			if (state < PWRDM_POWER_ON)
				pwrst->next_state = state;
		}
		if (set_pwrdm_state(pwrst->pwrdm, pwrst->next_state))
			goto restore;
		if (pwrdm_clear_all_prev_pwrst(pwrst->pwrdm))
			goto restore;
	}

	omap_uart_prepare_suspend();
	/* ACK pending interrupts */
	omap_writel(1, OMAP34XX_IC_BASE + INTC_CONTROL);
	omap_sram_idle();

restore:
	/* Restore next_pwrsts */
	list_for_each_entry(pwrst, &pwrst_list, node) {
		state = pwrdm_read_prev_pwrst(pwrst->pwrdm);
		if (state > pwrst->next_state) {
			printk(KERN_INFO "Powerdomain (%s) didn't enter "
			       "target state %d\n",
			       pwrst->pwrdm->name, pwrst->next_state);
			ret = -1;
		}
		set_pwrdm_state(pwrst->pwrdm, pwrst->saved_state);
	}
	if (ret)
		printk(KERN_ERR "Could not enter target state in pm_suspend\n");
	else
		printk(KERN_INFO "Successfully put all powerdomains "
		       "to target state\n");

	return ret;
}

static int omap3_pm_enter(suspend_state_t state)
{
	int ret = 0;

	switch (state) {
	case PM_SUSPEND_STANDBY:
	case PM_SUSPEND_MEM:
		ret = omap3_pm_suspend();
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static void omap3_pm_finish(void)
{
	pm_idle = saved_idle;
}

/* Hooks to enable / disable UART interrupts during suspend */
static int omap3_pm_begin(suspend_state_t state)
{
	omap_uart_enable_irqs(0);
	return 0;
}

static void omap3_pm_end(void)
{
	omap_uart_enable_irqs(1);
	return;
}

static struct platform_suspend_ops omap_pm_ops = {
	.begin		= omap3_pm_begin,
	.end		= omap3_pm_end,
	.prepare	= omap3_pm_prepare,
	.enter		= omap3_pm_enter,
	.finish		= omap3_pm_finish,
	.valid		= suspend_valid_only_mem,
};
#endif /* CONFIG_SUSPEND */


/**
 * omap3_iva_idle(): ensure IVA is in idle so it can be put into
 *                   retention
 *
 * In cases where IVA2 is activated by bootcode, it may prevent
 * full-chip retention or off-mode because it is not idle.  This
 * function forces the IVA2 into idle state so it can go
 * into retention/off and thus allow full-chip retention/off.
 *
 **/
static void __init omap3_iva_idle(void)
{
	/* ensure IVA2 clock is disabled */
	cm_write_mod_reg(0, OMAP3430_IVA2_MOD, CM_FCLKEN);

	/* Reset IVA2 */
	prm_write_mod_reg(OMAP3430_RST1_IVA2 |
			  OMAP3430_RST2_IVA2 |
			  OMAP3430_RST3_IVA2,
			  OMAP3430_IVA2_MOD, RM_RSTCTRL);

	/* Enable IVA2 clock */
	cm_write_mod_reg(OMAP3430_CM_FCLKEN_IVA2_EN_IVA2, 
			 OMAP3430_IVA2_MOD, CM_FCLKEN);

	/* Set IVA2 boot mode to 'idle' */
	omap_ctrl_writel(OMAP3_IVA2_BOOTMOD_IDLE,
			 OMAP343X_CONTROL_IVA2_BOOTMOD);

	/* Un-reset IVA2 */
	prm_write_mod_reg(0, OMAP3430_IVA2_MOD, RM_RSTCTRL);

	/* Disable IVA2 clock */
	cm_write_mod_reg(0, OMAP3430_IVA2_MOD, CM_FCLKEN);

	/* Reset IVA2 */
	prm_write_mod_reg(OMAP3430_RST1_IVA2 |
			  OMAP3430_RST2_IVA2 |
			  OMAP3430_RST3_IVA2,
			  OMAP3430_IVA2_MOD, RM_RSTCTRL);
}

static void __init prcm_setup_regs(void)
{
	struct clk *clk;

	/* reset modem */
	prm_write_mod_reg(OMAP3430_RM_RSTCTRL_CORE_MODEM_SW_RSTPWRON |
			  OMAP3430_RM_RSTCTRL_CORE_MODEM_SW_RST,
			  CORE_MOD, RM_RSTCTRL);
	prm_write_mod_reg(0, CORE_MOD, RM_RSTCTRL);

	/* XXX Reset all wkdeps. This should be done when initializing
	 * powerdomains */
	prm_write_mod_reg(0, OMAP3430_IVA2_MOD, PM_WKDEP);
	prm_write_mod_reg(0, MPU_MOD, PM_WKDEP);
	prm_write_mod_reg(0, OMAP3430_DSS_MOD, PM_WKDEP);
	prm_write_mod_reg(0, OMAP3430_NEON_MOD, PM_WKDEP);
	prm_write_mod_reg(0, OMAP3430_CAM_MOD, PM_WKDEP);
	prm_write_mod_reg(0, OMAP3430_PER_MOD, PM_WKDEP);
	if (omap_rev() > OMAP3430_REV_ES1_0) {

		/*
		 * This workaround is needed to prevent SGX and USBHOST from
		 * failing to transition to RET/OFF after a warm reset in OFF
		 * mode. Workaround sets a sleepdep of each of these domains
		 * with MPU, waits for a min 2 sysclk cycles and clears the sleepdep.
		 */
		cm_write_mod_reg(OMAP3430_CM_SLEEPDEP_PER_EN_MPU,
				OMAP3430ES2_USBHOST_MOD, OMAP3430_CM_SLEEPDEP);
		cm_write_mod_reg(OMAP3430_CM_SLEEPDEP_PER_EN_MPU,
				OMAP3430ES2_SGX_MOD, OMAP3430_CM_SLEEPDEP);
		udelay(100);
		cm_write_mod_reg(0, OMAP3430ES2_USBHOST_MOD,
				OMAP3430_CM_SLEEPDEP);
		cm_write_mod_reg(0, OMAP3430ES2_SGX_MOD,
				OMAP3430_CM_SLEEPDEP);

		prm_write_mod_reg(0, OMAP3430ES2_SGX_MOD, PM_WKDEP);
		prm_write_mod_reg(0, OMAP3430ES2_USBHOST_MOD, PM_WKDEP);
	} else
		prm_write_mod_reg(0, GFX_MOD, PM_WKDEP);

	/*
	 * Enable interface clock autoidle for all modules.
	 * Note that in the long run this should be done by clockfw
	 */
	cm_write_mod_reg(
		OMAP3430_AUTO_MODEM |
		OMAP3430ES2_AUTO_MMC3 |
		OMAP3430ES2_AUTO_ICR |
		OMAP3430_AUTO_AES2 |
		OMAP3430_AUTO_SHA12 |
		OMAP3430_AUTO_DES2 |
		OMAP3430_AUTO_MMC2 |
		OMAP3430_AUTO_MMC1 |
		OMAP3430_AUTO_MSPRO |
		OMAP3430_AUTO_HDQ |
		OMAP3430_AUTO_MCSPI4 |
		OMAP3430_AUTO_MCSPI3 |
		OMAP3430_AUTO_MCSPI2 |
		OMAP3430_AUTO_MCSPI1 |
		OMAP3430_AUTO_I2C3 |
		OMAP3430_AUTO_I2C2 |
		OMAP3430_AUTO_I2C1 |
		OMAP3430_AUTO_UART2 |
		OMAP3430_AUTO_UART1 |
		OMAP3430_AUTO_GPT11 |
		OMAP3430_AUTO_GPT10 |
		OMAP3430_AUTO_MCBSP5 |
		OMAP3430_AUTO_MCBSP1 |
		OMAP3430ES1_AUTO_FAC | /* This is es1 only */
		OMAP3430_AUTO_MAILBOXES |
		OMAP3430_AUTO_OMAPCTRL |
		OMAP3430ES1_AUTO_FSHOSTUSB |
		OMAP3430_AUTO_HSOTGUSB |
		OMAP3430_AUTO_SAD2D |
		OMAP3430_AUTO_SSI,
		CORE_MOD, CM_AUTOIDLE1);

	cm_write_mod_reg(
		OMAP3430_AUTO_PKA |
		OMAP3430_AUTO_AES1 |
		OMAP3430_AUTO_RNG |
		OMAP3430_AUTO_SHA11 |
		OMAP3430_AUTO_DES1,
		CORE_MOD, CM_AUTOIDLE2);

	if (omap_rev() > OMAP3430_REV_ES1_0) {
		cm_write_mod_reg(
			OMAP3430_AUTO_MAD2D |
			OMAP3430ES2_AUTO_USBTLL,
			CORE_MOD, CM_AUTOIDLE3);
	}

	cm_write_mod_reg(
		OMAP3430_AUTO_WDT2 |
		OMAP3430_AUTO_WDT1 |
		OMAP3430_AUTO_GPIO1 |
		OMAP3430_AUTO_32KSYNC |
		OMAP3430_AUTO_GPT12 |
		OMAP3430_AUTO_GPT1 ,
		WKUP_MOD, CM_AUTOIDLE);

	cm_write_mod_reg(
		OMAP3430_AUTO_DSS,
		OMAP3430_DSS_MOD,
		CM_AUTOIDLE);

	cm_write_mod_reg(
		OMAP3430_AUTO_CAM,
		OMAP3430_CAM_MOD,
		CM_AUTOIDLE);

	cm_write_mod_reg(
		OMAP3430_AUTO_GPIO6 |
		OMAP3430_AUTO_GPIO5 |
		OMAP3430_AUTO_GPIO4 |
		OMAP3430_AUTO_GPIO3 |
		OMAP3430_AUTO_GPIO2 |
		OMAP3430_AUTO_WDT3 |
		OMAP3430_AUTO_UART3 |
		OMAP3430_AUTO_GPT9 |
		OMAP3430_AUTO_GPT8 |
		OMAP3430_AUTO_GPT7 |
		OMAP3430_AUTO_GPT6 |
		OMAP3430_AUTO_GPT5 |
		OMAP3430_AUTO_GPT4 |
		OMAP3430_AUTO_GPT3 |
		OMAP3430_AUTO_GPT2 |
		OMAP3430_AUTO_MCBSP4 |
		OMAP3430_AUTO_MCBSP3 |
		OMAP3430_AUTO_MCBSP2,
		OMAP3430_PER_MOD,
		CM_AUTOIDLE);

	if (omap_rev() > OMAP3430_REV_ES1_0) {
		cm_write_mod_reg(
			OMAP3430ES2_AUTO_USBHOST,
			OMAP3430ES2_USBHOST_MOD,
			CM_AUTOIDLE);
	}

	omap_ctrl_writel(OMAP3430_AUTOIDLE, OMAP2_CONTROL_SYSCONFIG);

	/*
	 * Set all plls to autoidle. This is needed until autoidle is
	 * enabled by clockfw
	 */
	cm_write_mod_reg(1 << OMAP3430_AUTO_IVA2_DPLL_SHIFT,
			 OMAP3430_IVA2_MOD, CM_AUTOIDLE2);
	cm_write_mod_reg(1 << OMAP3430_AUTO_MPU_DPLL_SHIFT,
			 MPU_MOD,
			 CM_AUTOIDLE2);
	cm_write_mod_reg((1 << OMAP3430_AUTO_PERIPH_DPLL_SHIFT) |
			 (0 << OMAP3430_AUTO_CORE_DPLL_SHIFT),
			 PLL_MOD,
			 CM_AUTOIDLE);
	cm_write_mod_reg(1 << OMAP3430ES2_AUTO_PERIPH2_DPLL_SHIFT,
			 PLL_MOD,
			 CM_AUTOIDLE2);

	/*
	 * Enable control of expternal oscillator through
	 * sys_clkreq. In the long run clock framework should
	 * take care of this.
	 */
	prm_rmw_mod_reg_bits(OMAP_AUTOEXTCLKMODE_MASK,
			     1 << OMAP_AUTOEXTCLKMODE_SHIFT,
			     OMAP3430_GR_MOD,
			     OMAP3_PRM_CLKSRC_CTRL_OFFSET);

	/* setup wakup source */
	prm_write_mod_reg(OMAP3430_EN_GPIO1 | OMAP3430_EN_GPT1 |
			  OMAP3430_EN_GPT12, WKUP_MOD, PM_WKEN);
	/* No need to write EN_IO, that is always enabled */
	prm_write_mod_reg(OMAP3430_EN_GPIO1 | OMAP3430_EN_GPT1 |
			  OMAP3430_EN_GPT12,
			  WKUP_MOD, OMAP3430_PM_MPUGRPSEL);
	/* For some reason IO doesn't generate wakeup event even if
	 * it is selected to mpu wakeup goup */
	prm_write_mod_reg(OMAP3430_IO_EN | OMAP3430_WKUP_EN,
			OCP_MOD, OMAP2_PRM_IRQENABLE_MPU_OFFSET);

	omap3_iva_idle();

	/*
	 * Permanently enable USB interface clock, needed for the
	 * OTG_SYSCONFIG save / restore hack
	 */
	clk = clk_get(NULL, "hsotgusb_ick");
	clk_enable(clk);
}

void omap3_pm_off_mode_enable(int enable)
{
	struct power_state *pwrst;
	u32 state;

	if (enable)
		state = PWRDM_POWER_OFF;
	else
		state = PWRDM_POWER_RET;

#ifdef CONFIG_OMAP_PM_SRF
	resource_lock_opp(PRCM_VDD1);
	resource_lock_opp(PRCM_VDD2);
	if (resource_refresh())
		printk(KERN_ERR "Error: could not refresh resources\n");
	resource_unlock_opp(PRCM_VDD1);
	resource_unlock_opp(PRCM_VDD2);
#endif
	list_for_each_entry(pwrst, &pwrst_list, node) {
		pwrst->next_state = state;
		set_pwrdm_state(pwrst->pwrdm, state);
	}
}

int omap3_pm_get_suspend_state(struct powerdomain *pwrdm)
{
	struct power_state *pwrst;

	list_for_each_entry(pwrst, &pwrst_list, node) {
		if (pwrst->pwrdm == pwrdm)
			return pwrst->next_state;
	}
	return -EINVAL;
}

int omap3_pm_set_suspend_state(struct powerdomain *pwrdm, int state)
{
	struct power_state *pwrst;

	list_for_each_entry(pwrst, &pwrst_list, node) {
		if (pwrst->pwrdm == pwrdm) {
			pwrst->next_state = state;
			return 0;
		}
	}
	return -EINVAL;
}

void omap3_set_prm_setup_times(struct prm_setup_times *setup_times)
{
	prm_setup.clksetup = setup_times->clksetup;
	prm_setup.voltsetup_time1 = setup_times->voltsetup_time1;
	prm_setup.voltsetup_time2 = setup_times->voltsetup_time2;
	prm_setup.voltoffset = setup_times->voltoffset;
	prm_setup.voltsetup2 = setup_times->voltsetup2;
}

static int __init pwrdms_setup(struct powerdomain *pwrdm, void *unused)
{
	struct power_state *pwrst;

	if (!pwrdm->pwrsts)
		return 0;

	pwrst = kmalloc(sizeof(struct power_state), GFP_KERNEL);
	if (!pwrst)
		return -ENOMEM;
	pwrst->pwrdm = pwrdm;
	pwrst->next_state = PWRDM_POWER_RET;
	list_add(&pwrst->node, &pwrst_list);

	if (pwrdm_has_hdwr_sar(pwrdm))
		pwrdm_enable_hdwr_sar(pwrdm);

	return set_pwrdm_state(pwrst->pwrdm, pwrst->next_state);
}

/*
 * Enable hw supervised mode for all clockdomains if it's
 * supported. Initiate sleep transition for other clockdomains, if
 * they are not used
 */
static int __init clkdms_setup(struct clockdomain *clkdm, void *unused)
{
	if (clkdm->flags & CLKDM_CAN_ENABLE_AUTO)
		omap2_clkdm_allow_idle(clkdm);
	else if (clkdm->flags & CLKDM_CAN_FORCE_SLEEP &&
		 atomic_read(&clkdm->usecount) == 0)
		omap2_clkdm_sleep(clkdm);
	return 0;
}

void omap_push_sram_idle(void)
{
	_omap_sram_idle = omap_sram_push(omap34xx_cpu_suspend,
					omap34xx_cpu_suspend_sz);
	/* the sdrc counters are always at the end of the omap34xx_cpu_suspend
	 * block */
	_sdrc_counters = (unsigned *)((u8 *)_omap_sram_idle + omap34xx_cpu_suspend_sz - 8);

	if (omap_type() != OMAP2_DEVICE_TYPE_GP)
		_omap_save_secure_sram = omap_sram_push(save_secure_ram_context,
				save_secure_ram_context_sz);
}

int __init omap3_pm_init(void)
{
	struct power_state *pwrst, *tmp;
	int ret;

	printk(KERN_ERR "Power Management for TI OMAP3.\n");

	/* XXX prcm_setup_regs needs to be before enabling hw
	 * supervised mode for powerdomains */
	prcm_setup_regs();

	ret = request_irq(INT_34XX_PRCM_MPU_IRQ,
			  (irq_handler_t)prcm_interrupt_handler,
			  IRQF_DISABLED, "prcm", NULL);
	if (ret) {
		printk(KERN_ERR "request_irq failed to register for 0x%x\n",
		       INT_34XX_PRCM_MPU_IRQ);
		goto err1;
	}

	ret = pwrdm_for_each(pwrdms_setup, NULL);
	if (ret) {
		printk(KERN_ERR "Failed to setup powerdomains\n");
		goto err2;
	}

	(void) clkdm_for_each(clkdms_setup, NULL);

	mpu_pwrdm = pwrdm_lookup("mpu_pwrdm");
	if (mpu_pwrdm == NULL) {
		printk(KERN_ERR "Failed to get mpu_pwrdm\n");
		goto err2;
	}

	neon_pwrdm = pwrdm_lookup("neon_pwrdm");
	per_pwrdm = pwrdm_lookup("per_pwrdm");
	core_pwrdm = pwrdm_lookup("core_pwrdm");
	cam_pwrdm = pwrdm_lookup("cam_pwrdm");
	iva2_pwrdm = pwrdm_lookup("iva2_pwrdm");
	dss_pwrdm = pwrdm_lookup("dss_pwrdm");
	usb_pwrdm = pwrdm_lookup("usbhost_pwrdm");

	omap_push_sram_idle();

#ifdef CONFIG_SUSPEND
	suspend_set_ops(&omap_pm_ops);
#endif /* CONFIG_SUSPEND */

	pm_idle = omap3_pm_idle;
	omap3_idle_init();

	pwrdm_add_wkdep(neon_pwrdm, mpu_pwrdm);
	/*
	 * REVISIT: This wkdep is only necessary when GPIO2-6 are enabled for
	 * IO-pad wakeup.  Otherwise it will unnecessarily waste power
	 * waking up PER with every CORE wakeup - see
	 * http://marc.info/?l=linux-omap&m=121852150710062&w=2
	*/
	pwrdm_add_wkdep(per_pwrdm, core_pwrdm);

	if (omap_type() != OMAP2_DEVICE_TYPE_GP) {
		omap3_secure_ram_storage =
			kmalloc(0x803F, GFP_KERNEL);
		if (!omap3_secure_ram_storage)
			printk(KERN_ERR "Memory allocation failed when"
					"allocating for secure sram context\n");
	}
	omap3_save_scratchpad_contents();

	save_ssi_padconf();

	if (omap_type() != OMAP2_DEVICE_TYPE_GP) {
		local_irq_disable();
		local_fiq_disable();

		omap_dma_global_context_save();
		omap3_save_secure_ram_context(PWRDM_POWER_ON);
		omap_dma_global_context_restore();

		local_irq_enable();
		local_fiq_enable();
	}

err1:
	return ret;
err2:
	free_irq(INT_34XX_PRCM_MPU_IRQ, NULL);
	list_for_each_entry_safe(pwrst, tmp, &pwrst_list, node) {
		list_del(&pwrst->node);
		kfree(pwrst);
	}
	return ret;
}

/* PRM_VC_CMD_VAL_0 specific bits */
#define OMAP3430_VC_CMD_VAL0_ON		0x30
#define OMAP3430_VC_CMD_VAL0_ONLP	0x1E
#define OMAP3430_VC_CMD_VAL0_RET	0x1E
#define OMAP3430_VC_CMD_VAL0_OFF	0x30

/* PRM_VC_CMD_VAL_1 specific bits */
#define OMAP3430_VC_CMD_VAL1_ON		0x2C
#define OMAP3430_VC_CMD_VAL1_ONLP	0x1E
#define OMAP3430_VC_CMD_VAL1_RET	0x1E
#define OMAP3430_VC_CMD_VAL1_OFF	0x2C

static void __init configure_vc(void)
{

	prm_write_mod_reg((R_SRI2C_SLAVE_ADDR << OMAP3430_SMPS_SA1_SHIFT) |
			(R_SRI2C_SLAVE_ADDR << OMAP3430_SMPS_SA0_SHIFT),
			OMAP3430_GR_MOD, OMAP3_PRM_VC_SMPS_SA_OFFSET);
	prm_write_mod_reg((R_VDD2_SR_CONTROL << OMAP3430_VOLRA1_SHIFT) |
			(R_VDD1_SR_CONTROL << OMAP3430_VOLRA0_SHIFT),
			OMAP3430_GR_MOD, OMAP3_PRM_VC_SMPS_VOL_RA_OFFSET);

	prm_write_mod_reg((OMAP3430_VC_CMD_VAL0_ON <<
		OMAP3430_VC_CMD_ON_SHIFT) |
		(OMAP3430_VC_CMD_VAL0_ONLP << OMAP3430_VC_CMD_ONLP_SHIFT) |
		(OMAP3430_VC_CMD_VAL0_RET << OMAP3430_VC_CMD_RET_SHIFT) |
		(OMAP3430_VC_CMD_VAL0_OFF << OMAP3430_VC_CMD_OFF_SHIFT),
		OMAP3430_GR_MOD, OMAP3_PRM_VC_CMD_VAL_0_OFFSET);

	prm_write_mod_reg((OMAP3430_VC_CMD_VAL1_ON <<
		OMAP3430_VC_CMD_ON_SHIFT) |
		(OMAP3430_VC_CMD_VAL1_ONLP << OMAP3430_VC_CMD_ONLP_SHIFT) |
		(OMAP3430_VC_CMD_VAL1_RET << OMAP3430_VC_CMD_RET_SHIFT) |
		(OMAP3430_VC_CMD_VAL1_OFF << OMAP3430_VC_CMD_OFF_SHIFT),
		OMAP3430_GR_MOD, OMAP3_PRM_VC_CMD_VAL_1_OFFSET);

	prm_write_mod_reg(OMAP3430_CMD1 | OMAP3430_RAV1,
				OMAP3430_GR_MOD,
				OMAP3_PRM_VC_CH_CONF_OFFSET);

	prm_write_mod_reg(OMAP3430_MCODE_SHIFT | OMAP3430_HSEN,
				OMAP3430_GR_MOD,
				OMAP3_PRM_VC_I2C_CFG_OFFSET);

	/* Setup value for voltctrl */
	prm_write_mod_reg(OMAP3430_AUTO_RET,
			  OMAP3430_GR_MOD, OMAP3_PRM_VOLTCTRL_OFFSET);

	/* Write static setup times */
	prm_write_mod_reg(prm_setup.voltoffset, OMAP3430_GR_MOD,
			OMAP3_PRM_VOLTOFFSET_OFFSET);
}

static int __init omap3_pm_early_init(void)
{
	prm_clear_mod_reg_bits(OMAP3430_OFFMODE_POL, OMAP3430_GR_MOD,
				OMAP3_PRM_POLCTRL_OFFSET);

	configure_vc();

	return 0;
}

arch_initcall(omap3_pm_early_init);
