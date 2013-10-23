/*
 * linux/arch/arm/mach-omap3/smartreflex.c
 *
 * OMAP34XX SmartReflex Voltage Control
 *
 * Copyright (C) 2008 Nokia Corporation
 * Kalle Jokiniemi
 *
 * Copyright (C) 2007 Texas Instruments, Inc.
 * Lesly A M <x0080970@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/i2c/twl4030.h>
#include <linux/io.h>
#include "resource34xx_mutex.h"

#include <mach/omap34xx.h>
#include <mach/control.h>
#include <mach/clock.h>
#include <mach/omap-pm.h>
#include <mach/resource.h>

#include "prm.h"
#include "smartreflex.h"
#include "prm-regbits-34xx.h"

/*
 * VP_TRANXDONE_TIMEOUT: maximum microseconds to wait for the VP to
 * indicate that any pending transactions are complete.  [The current
 * 62 microsecond timeout was measured empirically by Nishanth Menon
 * during an overnight run; its granularity is ~ 30.5 microseconds, since
 * it was measured with the 32KiHz sync timer; see bug 133793]
 */
#define VP_TRANXDONE_TIMEOUT	62

/*
 * VP_IDLE_TIMEOUT: maximum microseconds to wait for the VP to enter
 * IDLE.  [The current 3.472 millisecond timeout was measured
 * empirically by Nishanth Menon during an overnight run; its
 * granularity is ~ 30.5 microseconds, since it was measured with the
 * 32KiHz sync timer; see bug 133793]
 */
#define VP_IDLE_TIMEOUT		3472

/*
 * SR_DISABLE_TIMEOUT: maximum microseconds to wait for the SR to
 * disable.  [The current 3.472 millisecond timeout was measured
 * empirically by Nishanth Menon during an overnight run; its
 * granularity is ~ 30.5 microseconds, since it was measured with the
 * 32KiHz sync timer; see bug 133793]
 */
#define SR_DISABLE_TIMEOUT	3472

/*
 * SR_DISABLE_MAX_ATTEMPTS: arbitrary value intended to avoid system
 * crashes if the SR disable process fails the first few times.  The
 * kernel will WARN() for every timeout, but will BUG() after
 * SR_DISABLE_MAX_ATTEMPTS.
 */
#define SR_DISABLE_MAX_ATTEMPTS 4

struct omap_sr {
	int		srid;
	int		is_sr_reset;
	int		is_autocomp_active;
	struct clk	*clk;
	u32		clk_length;
	u32		req_opp_no;
	u32		opp1_nvalue, opp2_nvalue, opp3_nvalue, opp4_nvalue;
	u32		opp5_nvalue;
	u32		senp_mod, senn_mod;
	void __iomem	*srbase_addr;
	void __iomem	*vpbase_addr;
};

#define SR_REGADDR(offs)	(sr->srbase_addr + offset)

static inline void sr_write_reg(struct omap_sr *sr, unsigned offset, u32 value)
{
	__raw_writel(value, SR_REGADDR(offset));
}

static inline void sr_modify_reg(struct omap_sr *sr, unsigned offset, u32 mask,
					u32 value)
{
	u32 reg_val;

	reg_val = __raw_readl(SR_REGADDR(offset));
	reg_val &= ~mask;
	reg_val |= value;

	__raw_writel(reg_val, SR_REGADDR(offset));
}

static inline u32 sr_read_reg(struct omap_sr *sr, unsigned offset)
{
	return __raw_readl(SR_REGADDR(offset));
}

static int sr_clk_enable(struct omap_sr *sr)
{
	if (clk_enable(sr->clk) != 0) {
		printk(KERN_ERR "Could not enable %s\n", sr->clk->name);
		return -1;
	}

	/* set fclk- active , iclk- idle */
	sr_modify_reg(sr, ERRCONFIG, SR_CLKACTIVITY_MASK |
		ERRCONFIG_INTERRUPT_STATUS_MASK, SR_CLKACTIVITY_IOFF_FON);

	return 0;
}

static void sr_clk_disable(struct omap_sr *sr)
{
	/* set fclk, iclk- idle */
	sr_modify_reg(sr, ERRCONFIG, SR_CLKACTIVITY_MASK |
		ERRCONFIG_INTERRUPT_STATUS_MASK, SR_CLKACTIVITY_IOFF_FOFF);

	clk_disable(sr->clk);
	sr->is_sr_reset = 1;
}

static struct omap_sr sr1 = {
	.srid			= SR1,
	.is_sr_reset		= 1,
	.is_autocomp_active	= 0,
	.clk_length		= 0,
	.srbase_addr		= OMAP2_IO_ADDRESS(OMAP34XX_SR1_BASE),
};

static struct omap_sr sr2 = {
	.srid			= SR2,
	.is_sr_reset		= 1,
	.is_autocomp_active	= 0,
	.clk_length		= 0,
	.srbase_addr		= OMAP2_IO_ADDRESS(OMAP34XX_SR2_BASE),
};

static void cal_reciprocal(u32 sensor, u32 *sengain, u32 *rnsen)
{
	u32 gn, rn, mul;

	for (gn = 0; gn < GAIN_MAXLIMIT; gn++) {
		mul = 1 << (gn + 8);
		rn = mul / sensor;
		if (rn < R_MAXLIMIT) {
			*sengain = gn;
			*rnsen = rn;
		}
	}
}

static u32 cal_test_nvalue(u32 sennval, u32 senpval)
{
	u32 senpgain, senngain;
	u32 rnsenp, rnsenn;

	/* Calculating the gain and reciprocal of the SenN and SenP values */
	cal_reciprocal(senpval, &senpgain, &rnsenp);
	cal_reciprocal(sennval, &senngain, &rnsenn);

	return ((senpgain << NVALUERECIPROCAL_SENPGAIN_SHIFT) |
		(senngain << NVALUERECIPROCAL_SENNGAIN_SHIFT) |
		(rnsenp << NVALUERECIPROCAL_RNSENP_SHIFT) |
		(rnsenn << NVALUERECIPROCAL_RNSENN_SHIFT));
}

static void sr_set_clk_length(struct omap_sr *sr)
{
	struct clk *sys_ck;
	u32 sys_clk_speed;

	sys_ck = clk_get(NULL, "sys_ck");
	sys_clk_speed = clk_get_rate(sys_ck);
	clk_put(sys_ck);

	switch (sys_clk_speed) {
	case 12000000:
		sr->clk_length = SRCLKLENGTH_12MHZ_SYSCLK;
		break;
	case 13000000:
		sr->clk_length = SRCLKLENGTH_13MHZ_SYSCLK;
		break;
	case 19200000:
		sr->clk_length = SRCLKLENGTH_19MHZ_SYSCLK;
		break;
	case 26000000:
		sr->clk_length = SRCLKLENGTH_26MHZ_SYSCLK;
		break;
	case 38400000:
		sr->clk_length = SRCLKLENGTH_38MHZ_SYSCLK;
		break;
	default :
		printk(KERN_ERR "Invalid sysclk value: %d\n", sys_clk_speed);
		break;
	}
}

static void sr_set_efuse_nvalues(struct omap_sr *sr)
{
	if (sr->srid == SR1) {
		sr->senn_mod = (omap_ctrl_readl(OMAP343X_CONTROL_FUSE_SR) &
					OMAP343X_SR1_SENNENABLE_MASK) >>
					OMAP343X_SR1_SENNENABLE_SHIFT;
		sr->senp_mod = (omap_ctrl_readl(OMAP343X_CONTROL_FUSE_SR) &
					OMAP343X_SR1_SENPENABLE_MASK) >>
					OMAP343X_SR1_SENPENABLE_SHIFT;

		sr->opp5_nvalue = omap_ctrl_readl(
					OMAP343X_CONTROL_FUSE_OPP5_VDD1);
		sr->opp4_nvalue = omap_ctrl_readl(
					OMAP343X_CONTROL_FUSE_OPP4_VDD1);
		sr->opp3_nvalue = omap_ctrl_readl(
					OMAP343X_CONTROL_FUSE_OPP3_VDD1);
		sr->opp2_nvalue = omap_ctrl_readl(
					OMAP343X_CONTROL_FUSE_OPP2_VDD1);
		sr->opp1_nvalue = omap_ctrl_readl(
					OMAP343X_CONTROL_FUSE_OPP1_VDD1);
	} else if (sr->srid == SR2) {
		sr->senn_mod = (omap_ctrl_readl(OMAP343X_CONTROL_FUSE_SR) &
					OMAP343X_SR2_SENNENABLE_MASK) >>
					OMAP343X_SR2_SENNENABLE_SHIFT;

		sr->senp_mod = (omap_ctrl_readl(OMAP343X_CONTROL_FUSE_SR) &
					OMAP343X_SR2_SENPENABLE_MASK) >>
					OMAP343X_SR2_SENPENABLE_SHIFT;

		sr->opp3_nvalue = omap_ctrl_readl(
					OMAP343X_CONTROL_FUSE_OPP3_VDD2);
		sr->opp2_nvalue = omap_ctrl_readl(
					OMAP343X_CONTROL_FUSE_OPP2_VDD2);
		sr->opp1_nvalue = omap_ctrl_readl(
					OMAP343X_CONTROL_FUSE_OPP1_VDD2);
	}
}

/* Hard coded nvalues for testing purposes, may cause device to hang! */
static void sr_set_testing_nvalues(struct omap_sr *sr)
{
	if (sr->srid == SR1) {
		sr->senp_mod = 0x03;	/* SenN-M5 enabled */
		sr->senn_mod = 0x03;

		/* calculate nvalues for each opp */
		sr->opp5_nvalue = cal_test_nvalue(0xacd + 0x330, 0x848 + 0x330);
		sr->opp4_nvalue = cal_test_nvalue(0x964 + 0x2a0, 0x727 + 0x2a0);
		sr->opp3_nvalue = cal_test_nvalue(0x85b + 0x200, 0x655 + 0x200);
		sr->opp2_nvalue = cal_test_nvalue(0x506 + 0x1a0, 0x3be + 0x1a0);
		sr->opp1_nvalue = cal_test_nvalue(0x373 + 0x100, 0x28c + 0x100);
	} else if (sr->srid == SR2) {
		sr->senp_mod = 0x03;
		sr->senn_mod = 0x03;

		sr->opp3_nvalue = cal_test_nvalue(0x76f + 0x200, 0x579 + 0x200);
		sr->opp2_nvalue = cal_test_nvalue(0x4f5 + 0x1c0, 0x390 + 0x1c0);
		sr->opp1_nvalue = cal_test_nvalue(0x359, 0x25d);
	}

}

static void sr_set_nvalues(struct omap_sr *sr)
{
	if (SR_TESTING_NVALUES)
		sr_set_testing_nvalues(sr);
	else
		sr_set_efuse_nvalues(sr);
}

static void sr_configure_vp(int srid)
{
	u32 vpconfig;
	u8 curr_opp_no;

	if (srid == SR1) {
		curr_opp_no = resource_get_level("vdd1_opp");

		vpconfig = PRM_VP1_CONFIG_ERROROFFSET |
			PRM_VP1_CONFIG_TIMEOUTEN |
			mpu_opps[curr_opp_no].vsel <<
			OMAP3430_INITVOLTAGE_SHIFT;

		vpconfig |= (curr_opp_no > SR_MAX_LOW_OPP) ?
			PRM_VP1_CONFIG_ERRORGAIN_HIGHOPP :
			PRM_VP1_CONFIG_ERRORGAIN_LOWOPP;

		prm_write_mod_reg(vpconfig, OMAP3430_GR_MOD,
					OMAP3_PRM_VP1_CONFIG_OFFSET);
		prm_write_mod_reg(PRM_VP1_VSTEPMIN_SMPSWAITTIMEMIN |
					PRM_VP1_VSTEPMIN_VSTEPMIN,
					OMAP3430_GR_MOD,
					OMAP3_PRM_VP1_VSTEPMIN_OFFSET);

		prm_write_mod_reg(PRM_VP1_VSTEPMAX_SMPSWAITTIMEMAX |
					PRM_VP1_VSTEPMAX_VSTEPMAX,
					OMAP3430_GR_MOD,
					OMAP3_PRM_VP1_VSTEPMAX_OFFSET);

		prm_write_mod_reg(PRM_VP1_VLIMITTO_VDDMAX |
					PRM_VP1_VLIMITTO_VDDMIN |
					PRM_VP1_VLIMITTO_TIMEOUT,
					OMAP3430_GR_MOD,
					OMAP3_PRM_VP1_VLIMITTO_OFFSET);

		/* Trigger initVDD value copy to voltage processor */
		prm_set_mod_reg_bits(PRM_VP1_CONFIG_INITVDD, OMAP3430_GR_MOD,
				     OMAP3_PRM_VP1_CONFIG_OFFSET);

		/* Clear initVDD copy trigger bit */
		prm_clear_mod_reg_bits(PRM_VP1_CONFIG_INITVDD, OMAP3430_GR_MOD,
				       OMAP3_PRM_VP1_CONFIG_OFFSET);

		/* Force update of voltage */
		prm_set_mod_reg_bits(OMAP3430_FORCEUPDATE, OMAP3430_GR_MOD,
				     OMAP3_PRM_VP1_CONFIG_OFFSET);
		/* Clear force bit */
		prm_clear_mod_reg_bits(OMAP3430_FORCEUPDATE, OMAP3430_GR_MOD,
				       OMAP3_PRM_VP1_CONFIG_OFFSET);

	} else if (srid == SR2) {
		curr_opp_no = resource_get_level("vdd2_opp");

		vpconfig = PRM_VP2_CONFIG_ERROROFFSET |
			PRM_VP2_CONFIG_TIMEOUTEN |
			l3_opps[curr_opp_no].vsel <<
			OMAP3430_INITVOLTAGE_SHIFT;

		vpconfig |= (curr_opp_no > SR_MAX_LOW_OPP) ?
			PRM_VP2_CONFIG_ERRORGAIN_HIGHOPP :
			PRM_VP2_CONFIG_ERRORGAIN_LOWOPP;

		prm_write_mod_reg(vpconfig, OMAP3430_GR_MOD,
					OMAP3_PRM_VP2_CONFIG_OFFSET);
		prm_write_mod_reg(PRM_VP2_VSTEPMIN_SMPSWAITTIMEMIN |
					PRM_VP2_VSTEPMIN_VSTEPMIN,
					OMAP3430_GR_MOD,
					OMAP3_PRM_VP2_VSTEPMIN_OFFSET);

		prm_write_mod_reg(PRM_VP2_VSTEPMAX_SMPSWAITTIMEMAX |
					PRM_VP2_VSTEPMAX_VSTEPMAX,
					OMAP3430_GR_MOD,
					OMAP3_PRM_VP2_VSTEPMAX_OFFSET);

		prm_write_mod_reg(PRM_VP2_VLIMITTO_VDDMAX |
					PRM_VP2_VLIMITTO_VDDMIN |
					PRM_VP2_VLIMITTO_TIMEOUT,
					OMAP3430_GR_MOD,
					OMAP3_PRM_VP2_VLIMITTO_OFFSET);

		/* Trigger initVDD value copy to voltage processor */
		prm_set_mod_reg_bits(PRM_VP1_CONFIG_INITVDD, OMAP3430_GR_MOD,
				     OMAP3_PRM_VP2_CONFIG_OFFSET);

		/* Clear initVDD copy trigger bit */
		prm_clear_mod_reg_bits(PRM_VP1_CONFIG_INITVDD, OMAP3430_GR_MOD,
				       OMAP3_PRM_VP2_CONFIG_OFFSET);

		/* Force update of voltage */
		prm_set_mod_reg_bits(OMAP3430_FORCEUPDATE, OMAP3430_GR_MOD,
				     OMAP3_PRM_VP2_CONFIG_OFFSET);
		/* Clear force bit */
		prm_clear_mod_reg_bits(OMAP3430_FORCEUPDATE, OMAP3430_GR_MOD,
				       OMAP3_PRM_VP2_CONFIG_OFFSET);

	}
}

static void sr_configure(struct omap_sr *sr)
{
	u32 sr_config;
	u32 senp_en , senn_en;

	if (sr->clk_length == 0)
		sr_set_clk_length(sr);

	senp_en = sr->senp_mod;
	senn_en = sr->senn_mod;
	if (sr->srid == SR1) {
		sr_config = SR1_SRCONFIG_ACCUMDATA |
			(sr->clk_length << SRCONFIG_SRCLKLENGTH_SHIFT) |
			SRCONFIG_SENENABLE | SRCONFIG_ERRGEN_EN |
			SRCONFIG_MINMAXAVG_EN |
			(senn_en << SRCONFIG_SENNENABLE_SHIFT) |
			(senp_en << SRCONFIG_SENPENABLE_SHIFT) |
			SRCONFIG_DELAYCTRL;

		sr_write_reg(sr, SRCONFIG, sr_config);
		sr_write_reg(sr, AVGWEIGHT, SR1_AVGWEIGHT_SENPAVGWEIGHT |
					SR1_AVGWEIGHT_SENNAVGWEIGHT);

		sr_modify_reg(sr, ERRCONFIG, (SR_ERRWEIGHT_MASK |
			SR_ERRMAXLIMIT_MASK) | ERRCONFIG_INTERRUPT_STATUS_MASK,
			(SR1_ERRWEIGHT | SR1_ERRMAXLIMIT));

	} else if (sr->srid == SR2) {
		sr_config = SR2_SRCONFIG_ACCUMDATA |
			(sr->clk_length << SRCONFIG_SRCLKLENGTH_SHIFT) |
			SRCONFIG_SENENABLE | SRCONFIG_ERRGEN_EN |
			SRCONFIG_MINMAXAVG_EN |
			(senn_en << SRCONFIG_SENNENABLE_SHIFT) |
			(senp_en << SRCONFIG_SENPENABLE_SHIFT) |
			SRCONFIG_DELAYCTRL;

		sr_write_reg(sr, SRCONFIG, sr_config);
		sr_write_reg(sr, AVGWEIGHT, SR2_AVGWEIGHT_SENPAVGWEIGHT |
					SR2_AVGWEIGHT_SENNAVGWEIGHT);
		sr_modify_reg(sr, ERRCONFIG, (SR_ERRWEIGHT_MASK |
			SR_ERRMAXLIMIT_MASK) | ERRCONFIG_INTERRUPT_STATUS_MASK,
			(SR2_ERRWEIGHT | SR2_ERRMAXLIMIT));

	}
	sr->is_sr_reset = 0;
}

static int sr_reset_voltage(int srid)
{
	u32 target_opp_no, vsel = 0;
	u32 reg_addr = 0;
	u32 loop_cnt = 0, retries_cnt = 0;
	u32 vc_bypass_value;
	u32 t2_smps_steps = 0;
	u32 t2_smps_delay = 0;
	u32 prm_vp1_voltage, prm_vp2_voltage, vp_config_offs;
	u32 errorgain;

	if (srid == SR1) {
		target_opp_no = sr1.req_opp_no;
		vsel = mpu_opps[target_opp_no].vsel;
		reg_addr = R_VDD1_SR_CONTROL;
		prm_vp1_voltage = prm_read_mod_reg(OMAP3430_GR_MOD,
						OMAP3_PRM_VP1_VOLTAGE_OFFSET);
		t2_smps_steps = abs(vsel - prm_vp1_voltage);
		errorgain = (target_opp_no > SR_MAX_LOW_OPP) ?
			PRM_VP1_CONFIG_ERRORGAIN_HIGHOPP :
			PRM_VP1_CONFIG_ERRORGAIN_LOWOPP;
		vp_config_offs = OMAP3_PRM_VP1_CONFIG_OFFSET;
	} else if (srid == SR2) {
		target_opp_no = sr2.req_opp_no;
		vsel = l3_opps[target_opp_no].vsel;
		reg_addr = R_VDD2_SR_CONTROL;
		prm_vp2_voltage = prm_read_mod_reg(OMAP3430_GR_MOD,
						OMAP3_PRM_VP2_VOLTAGE_OFFSET);
		t2_smps_steps = abs(vsel - prm_vp2_voltage);
		errorgain = (target_opp_no > SR_MAX_LOW_OPP) ?
			PRM_VP2_CONFIG_ERRORGAIN_HIGHOPP :
			PRM_VP2_CONFIG_ERRORGAIN_LOWOPP;
		vp_config_offs = OMAP3_PRM_VP2_CONFIG_OFFSET;
	} else {
		WARN(1, "Bad SR ID %d", srid);
		return SR_FAIL;
	}

	prm_rmw_mod_reg_bits(OMAP3430_ERRORGAIN_MASK, errorgain,
			     OMAP3430_GR_MOD, vp_config_offs);

	vc_bypass_value = (vsel << OMAP3430_DATA_SHIFT) |
			(reg_addr << OMAP3430_REGADDR_SHIFT) |
			(R_SRI2C_SLAVE_ADDR << OMAP3430_SLAVEADDR_SHIFT);

	prm_write_mod_reg(vc_bypass_value, OMAP3430_GR_MOD,
			OMAP3_PRM_VC_BYPASS_VAL_OFFSET);

	vc_bypass_value = prm_set_mod_reg_bits(OMAP3430_VALID, OMAP3430_GR_MOD,
					OMAP3_PRM_VC_BYPASS_VAL_OFFSET);

	while ((vc_bypass_value & OMAP3430_VALID) != 0x0) {
		loop_cnt++;
		if (retries_cnt > 10) {
			printk(KERN_INFO "Loop count exceeded in check SR I2C"
								"write\n");
			return SR_FAIL;
		}
		if (loop_cnt > 50) {
			retries_cnt++;
			loop_cnt = 0;
			udelay(10);
		}
		vc_bypass_value = prm_read_mod_reg(OMAP3430_GR_MOD,
					OMAP3_PRM_VC_BYPASS_VAL_OFFSET);
	}

	/*
	 *  T2 SMPS slew rate (min) 4mV/uS, step size 12.5mV,
	 *  2us added as buffer.
	 */
	t2_smps_delay = ((t2_smps_steps * 125) / 40) + 2;
	udelay(t2_smps_delay);

	return SR_PASS;
}

static int sr_enable(struct omap_sr *sr, u32 target_opp_no)
{
	u32 nvalue_reciprocal, v;
	u8 errminlimit;

	BUG_ON(!(mpu_opps && l3_opps));

	sr->req_opp_no = target_opp_no;

	if (sr->srid == SR1) {
		switch (target_opp_no) {
		case 5:
			nvalue_reciprocal = sr->opp5_nvalue;
			break;
		case 4:
			nvalue_reciprocal = sr->opp4_nvalue;
			break;
		case 3:
			nvalue_reciprocal = sr->opp3_nvalue;
			break;
		case 2:
			nvalue_reciprocal = sr->opp2_nvalue;
			break;
		case 1:
			nvalue_reciprocal = sr->opp1_nvalue;
			break;
		default:
			nvalue_reciprocal = sr->opp3_nvalue;
			break;
		}
	} else {
		switch (target_opp_no) {
		case 3:
			nvalue_reciprocal = sr->opp3_nvalue;
			break;
		case 2:
			nvalue_reciprocal = sr->opp2_nvalue;
			break;
		case 1:
			nvalue_reciprocal = sr->opp1_nvalue;
			break;
		default:
			nvalue_reciprocal = sr->opp3_nvalue;
			break;
		}
	}

	if (nvalue_reciprocal == 0) {
		printk(KERN_NOTICE "OPP%d doesn't support SmartReflex\n",
								target_opp_no);
		return SR_FALSE;
	}

	sr_write_reg(sr, NVALUERECIPROCAL, nvalue_reciprocal);

	/* Enable the interrupt */
	sr_modify_reg(sr, ERRCONFIG, (ERRCONFIG_VPBOUNDINTEN |
				ERRCONFIG_INTERRUPT_STATUS_MASK),
			(ERRCONFIG_VPBOUNDINTEN | ERRCONFIG_VPBOUNDINTST));

	if (sr->srid == SR1) {
		errminlimit = (target_opp_no > SR_MAX_LOW_OPP) ?
			SR1_ERRMINLIMIT_HIGHOPP : SR1_ERRMINLIMIT_LOWOPP;

		/* set/latch init voltage */
		v = prm_read_mod_reg(OMAP3430_GR_MOD,
				     OMAP3_PRM_VP1_CONFIG_OFFSET);
		v &= ~(OMAP3430_INITVOLTAGE_MASK | OMAP3430_INITVDD);
		v |= mpu_opps[target_opp_no].vsel <<
			OMAP3430_INITVOLTAGE_SHIFT;
		prm_write_mod_reg(v, OMAP3430_GR_MOD,
				  OMAP3_PRM_VP1_CONFIG_OFFSET);
		/* write1 to latch */
		prm_set_mod_reg_bits(OMAP3430_INITVDD, OMAP3430_GR_MOD,
				     OMAP3_PRM_VP1_CONFIG_OFFSET);
		/* write2 clear */
		prm_clear_mod_reg_bits(OMAP3430_INITVDD, OMAP3430_GR_MOD,
				       OMAP3_PRM_VP1_CONFIG_OFFSET);
		/* Enable VP1 */
		prm_set_mod_reg_bits(PRM_VP1_CONFIG_VPENABLE, OMAP3430_GR_MOD,
				     OMAP3_PRM_VP1_CONFIG_OFFSET);
	} else if (sr->srid == SR2) {
		errminlimit = (target_opp_no > SR_MAX_LOW_OPP) ?
			SR2_ERRMINLIMIT_HIGHOPP : SR2_ERRMINLIMIT_LOWOPP;

		/* set/latch init voltage */
		v = prm_read_mod_reg(OMAP3430_GR_MOD,
				     OMAP3_PRM_VP2_CONFIG_OFFSET);
		v &= ~(OMAP3430_INITVOLTAGE_MASK | OMAP3430_INITVDD);
		v |= l3_opps[target_opp_no].vsel <<
			OMAP3430_INITVOLTAGE_SHIFT;
		prm_write_mod_reg(v, OMAP3430_GR_MOD,
				  OMAP3_PRM_VP2_CONFIG_OFFSET);
		/* write1 to latch */
		prm_set_mod_reg_bits(OMAP3430_INITVDD, OMAP3430_GR_MOD,
				     OMAP3_PRM_VP2_CONFIG_OFFSET);
		/* write2 clear */
		prm_clear_mod_reg_bits(OMAP3430_INITVDD, OMAP3430_GR_MOD,
				       OMAP3_PRM_VP2_CONFIG_OFFSET);
		/* Enable VP2 */
		prm_set_mod_reg_bits(PRM_VP2_CONFIG_VPENABLE, OMAP3430_GR_MOD,
				     OMAP3_PRM_VP2_CONFIG_OFFSET);
	} else {
		WARN(1, "Bad SR ID %d", sr->srid);
		return SR_FAIL;
	}

	sr_modify_reg(sr, ERRCONFIG, SR_ERRMINLIMIT_MASK |
			ERRCONFIG_INTERRUPT_STATUS_MASK, errminlimit);

	/* SRCONFIG - enable SR */
	sr_modify_reg(sr, SRCONFIG, SRCONFIG_SRENABLE, SRCONFIG_SRENABLE);
	return SR_TRUE;
}

static void vp_disable(struct omap_sr *sr)
{
	u32 vp_config_offs, vp_status_offs;
	u32 vp_tranxdone_st;
	int c = 0, v;

	if (sr->srid == SR1) {
		vp_config_offs = OMAP3_PRM_VP1_CONFIG_OFFSET;
		vp_status_offs = OMAP3_PRM_VP1_STATUS_OFFSET;
		vp_tranxdone_st = OMAP3430_VP1_TRANXDONE_ST;
	} else if (sr->srid == SR2) {
		vp_config_offs = OMAP3_PRM_VP2_CONFIG_OFFSET;
		vp_status_offs = OMAP3_PRM_VP2_STATUS_OFFSET;
		vp_tranxdone_st = OMAP3430_VP2_TRANXDONE_ST;
	} else {
		WARN(1, "Bad SR ID");
		return;
	}

	/*
	 * Clear all pending TransactionDone int/st here
	 * XXX Do we need to make sure this INTEN bit is masked so the
	 * PRCM ISR isn't called?
	 */
	do {
		prm_write_mod_reg(vp_tranxdone_st, OCP_MOD,
				  OMAP2_PRM_IRQSTATUS_MPU_OFFSET);
		v = prm_read_mod_reg(OCP_MOD, OMAP2_PRM_IRQSTATUS_MPU_OFFSET);
		v &= vp_tranxdone_st;
		/*
		 * XXX This udelay(1) will wait for longer than 1
		 * microsecond when switching to a lower OPP, since
		 * loops_per_jiffy is not yet updated at this point
		 */
		if (v)
			udelay(1);
		c++;
	} while (v && c < VP_TRANXDONE_TIMEOUT);

	/* XXX Need clarity from TI on what to do if the timeout is reached */
	WARN(c == VP_TRANXDONE_TIMEOUT, "VP: TRANXDONE timeout exceeded");

	/* Disable VP */
	prm_clear_mod_reg_bits(OMAP3430_VPENABLE, OMAP3430_GR_MOD,
			       vp_config_offs);

	/* Wait for VP to be in IDLE - typical latency < 1 microsecond */
	c = 0;
	while (c < VP_IDLE_TIMEOUT &&
	       !(prm_read_mod_reg(OMAP3430_GR_MOD, vp_status_offs) &
		 OMAP3430_VPINIDLE)) {
		/*
		 * XXX This udelay(1) will wait for longer than 1
		 * microsecond when switching to a lower OPP, since
		 * loops_per_jiffy is not yet updated at this point
		 */
		udelay(1);
		c++;
	}

	/* XXX Need clarity from TI on what to do if the timeout is reached */
	WARN(c == VP_IDLE_TIMEOUT, "VP: IDLE timeout exceeded");
}

static void sr_disable(struct omap_sr *sr)
{
	u32 srconfig;
	int c;
	u8 retries = 0;

	/* Check to see if SR is already disabled.  If so, skip */
	srconfig = sr_read_reg(sr, SRCONFIG);
	if (!(srconfig & SRCONFIG_SRENABLE)) {
		/* XXX In callers, add disable VP after sr_clk_disable() etc */
		sr->is_sr_reset = 1;
		return;
	}

	/* Enable MCUDisableAcknowledge interrupt */
	sr_modify_reg(sr, ERRCONFIG, ERRCONFIG_MCUDISACKINTEN |
			ERRCONFIG_INTERRUPT_STATUS_MASK,
		      ERRCONFIG_MCUDISACKINTEN);

	/* Clear SREnable */
	srconfig &= ~SRCONFIG_SRENABLE;
	sr_write_reg(sr, SRCONFIG, srconfig);

	/* Disable VPBOUND interrupt enable and status */
	sr_modify_reg(sr, ERRCONFIG, ERRCONFIG_VPBOUNDINTEN |
			ERRCONFIG_INTERRUPT_STATUS_MASK,
		      ERRCONFIG_VPBOUNDINTST);

	do {
		c = 0;
		/* Wait for SR to be disabled - typical time < 1 microsecond */
		while (c < SR_DISABLE_TIMEOUT &&
		       !(sr_read_reg(sr, ERRCONFIG) & ERRCONFIG_MCUDISACKINTST)) {
			/*
			 * XXX This udelay(1) will wait for longer than 1
			 * microsecond when switching to a lower OPP, since
			 * loops_per_jiffy is not yet updated at this point
			 */
			udelay(1);
			c++;
		}

		/* Could be due to a board-level I2C4 problem */
		WARN(c == SR_DISABLE_TIMEOUT, "SR disable timed out - "
		     "should never happen");

	} while ((c == SR_DISABLE_TIMEOUT) &&
		 (++retries < SR_DISABLE_MAX_ATTEMPTS));

	WARN(retries == SR_DISABLE_MAX_ATTEMPTS, "SR voltage change failed "
	     "despite %d retries - should never happen - system will likely "
	     "crash soon", SR_DISABLE_MAX_ATTEMPTS);

	/* Disable MCUDisableAck interrupt and clear pending */
	sr_modify_reg(sr, ERRCONFIG, (ERRCONFIG_MCUDISACKINTEN |
				ERRCONFIG_INTERRUPT_STATUS_MASK),
			ERRCONFIG_MCUDISACKINTST);

	/* Disable SR func clk - done by sr_clk_disable() */

	sr->is_sr_reset = 1;
}


void sr_start_vddautocomap(int srid, u32 target_opp_no)
{
	struct omap_sr *sr = NULL;

	if (srid == SR1)
		sr = &sr1;
	else if (srid == SR2)
		sr = &sr2;
	else
		return;

	if (sr->is_sr_reset == 1) {
		sr_clk_enable(sr);
		sr_configure(sr);
	}

	sr->is_autocomp_active = 1;
	if (!sr_enable(sr, target_opp_no)) {
		sr->is_autocomp_active = 0;
		if (sr->is_sr_reset == 1)
			sr_clk_disable(sr);
	}
}
EXPORT_SYMBOL(sr_start_vddautocomap);

int sr_stop_vddautocomap(int srid)
{
	struct omap_sr *sr = NULL;

	if (srid == SR1)
		sr = &sr1;
	else if (srid == SR2)
		sr = &sr2;
	else
		return -EINVAL;

	if (sr->is_autocomp_active == 1) {
		vp_disable(sr);
		sr_disable(sr);
		sr_clk_disable(sr);
		sr->is_autocomp_active = 0;
		/* Reset the volatage for current OPP */
		sr_reset_voltage(srid);
		return SR_TRUE;
	} else
		return SR_FALSE;
}
EXPORT_SYMBOL(sr_stop_vddautocomap);

void enable_smartreflex(int srid)
{
	u32 target_opp_no = 0;
	struct omap_sr *sr = NULL;

	if (srid == SR1)
		sr = &sr1;
	else if (srid == SR2)
		sr = &sr2;
	else
		return;

	if (sr->is_autocomp_active == 1) {
		if (sr->is_sr_reset == 1) {
			/* Enable SR clks */
			sr_clk_enable(sr);

			target_opp_no = sr->req_opp_no;

			sr_configure(sr);

			if (!sr_enable(sr, target_opp_no))
				sr_clk_disable(sr);
		}
	}
}

void disable_smartreflex(int srid)
{
	struct omap_sr *sr = NULL;

	if (srid == SR1)
		sr = &sr1;
	else if (srid == SR2)
		sr = &sr2;
	else
		return;

	if (sr->is_autocomp_active == 1) {
		if (sr->is_sr_reset == 0) {

			sr->is_sr_reset = 1;
			vp_disable(sr);
			sr_disable(sr);
			/* Disable SR clk */
			sr_clk_disable(sr);
			/* Reset the volatage for current OPP */
			sr_reset_voltage(srid);
		}
	}
}

/* Voltage Scaling using SR VCBYPASS */
int sr_voltagescale_vcbypass(u32 target_opp, u32 current_opp,
					u8 target_vsel, u8 current_vsel)
{
	u32 vdd, target_opp_no, current_opp_no;
	u32 vc_bypass_value;
	u32 reg_addr = 0;
	u32 loop_cnt = 0, retries_cnt = 0;
	u32 t2_smps_steps = 0;
	u32 t2_smps_delay = 0;
	u32 vc_cmd_val_offs, vp_config_offs;
	u32 errorgain;
	struct omap_sr *sr;

	vdd = get_vdd(target_opp);
	target_opp_no = get_opp_no(target_opp);
	current_opp_no = get_opp_no(current_opp);

	if (vdd == PRCM_VDD1) {
		t2_smps_steps = abs(target_vsel - current_vsel);
		errorgain = (target_opp_no > SR_MAX_LOW_OPP) ?
			PRM_VP1_CONFIG_ERRORGAIN_HIGHOPP :
			PRM_VP1_CONFIG_ERRORGAIN_LOWOPP;

		vc_cmd_val_offs = OMAP3_PRM_VC_CMD_VAL_0_OFFSET;
		vp_config_offs = OMAP3_PRM_VP1_CONFIG_OFFSET;
		reg_addr = R_VDD1_SR_CONTROL;
		sr = &sr1;
	} else if (vdd == PRCM_VDD2) {
		t2_smps_steps = abs(target_vsel - current_vsel);
		errorgain = (target_opp_no > SR_MAX_LOW_OPP) ?
			PRM_VP2_CONFIG_ERRORGAIN_HIGHOPP :
			PRM_VP2_CONFIG_ERRORGAIN_LOWOPP;

		vc_cmd_val_offs = OMAP3_PRM_VC_CMD_VAL_1_OFFSET;
		vp_config_offs = OMAP3_PRM_VP2_CONFIG_OFFSET;
		reg_addr = R_VDD2_SR_CONTROL;
		sr = &sr2;
	} else {
		WARN(1, "SR: invalid VDD in vcbypass scale");
		return SR_FAIL;
	}

	if (sr->is_autocomp_active) {
		WARN(1, "SR: Must not transmit VCBYPASS command while SR is "
		     "active");
		return SR_FAIL;
	}

	prm_rmw_mod_reg_bits(OMAP3430_ERRORGAIN_MASK, errorgain,
			     OMAP3430_GR_MOD, vp_config_offs);

	prm_rmw_mod_reg_bits(OMAP3430_VC_CMD_ON_MASK,
			     (target_vsel << OMAP3430_VC_CMD_ON_SHIFT),
			     OMAP3430_GR_MOD, vc_cmd_val_offs);

	vc_bypass_value = (target_vsel << OMAP3430_DATA_SHIFT) |
			(reg_addr << OMAP3430_REGADDR_SHIFT) |
			(R_SRI2C_SLAVE_ADDR << OMAP3430_SLAVEADDR_SHIFT);

	prm_write_mod_reg(vc_bypass_value, OMAP3430_GR_MOD,
			OMAP3_PRM_VC_BYPASS_VAL_OFFSET);

	vc_bypass_value = prm_set_mod_reg_bits(OMAP3430_VALID, OMAP3430_GR_MOD,
					OMAP3_PRM_VC_BYPASS_VAL_OFFSET);

	while ((vc_bypass_value & OMAP3430_VALID) != 0x0) {
		loop_cnt++;
		if (retries_cnt > 10) {
			printk(KERN_INFO "Loop count exceeded in check SR I2C"
								"write\n");
			return SR_FAIL;
		}
		if (loop_cnt > 50) {
			retries_cnt++;
			loop_cnt = 0;
			udelay(10);
		}
		vc_bypass_value = prm_read_mod_reg(OMAP3430_GR_MOD,
					OMAP3_PRM_VC_BYPASS_VAL_OFFSET);
	}

	/*
	 *  T2 SMPS slew rate (min) 4mV/uS, step size 12.5mV,
	 *  2us added as buffer.
	 */
	t2_smps_delay = ((t2_smps_steps * 125) / 40) + 2;
	udelay(t2_smps_delay);


	return SR_PASS;
}

/* Sysfs interface to select SR VDD1 auto compensation */
static ssize_t omap_sr_vdd1_autocomp_show(struct kobject *kobj,
					struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", sr1.is_autocomp_active);
}

static ssize_t omap_sr_vdd1_autocomp_store(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t n)
{
	unsigned short value;

	if (sscanf(buf, "%hu", &value) != 1 || (value > 1)) {
		printk(KERN_ERR "sr_vdd1_autocomp: Invalid value\n");
		return -EINVAL;
	}

	mutex_lock(&dvfs_mutex);

	if (value == 0) {
		sr_stop_vddautocomap(SR1);
	} else {
		u32 current_vdd1opp_no = resource_get_level("vdd1_opp");
		if (IS_ERR_VALUE(current_vdd1opp_no)) {
			mutex_unlock(&dvfs_mutex);
			return -ENODEV;
		}
		sr_start_vddautocomap(SR1, current_vdd1opp_no);
	}

	mutex_unlock(&dvfs_mutex);

	return n;
}

static struct kobj_attribute sr_vdd1_autocomp = {
	.attr = {
	.name = __stringify(sr_vdd1_autocomp),
	.mode = 0644,
	},
	.show = omap_sr_vdd1_autocomp_show,
	.store = omap_sr_vdd1_autocomp_store,
};

/* Sysfs interface to select SR VDD2 auto compensation */
static ssize_t omap_sr_vdd2_autocomp_show(struct kobject *kobj,
					struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", sr2.is_autocomp_active);
}

static ssize_t omap_sr_vdd2_autocomp_store(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t n)
{
	u32 current_vdd2opp_no;
	unsigned short value;

	if (sscanf(buf, "%hu", &value) != 1 || (value > 1)) {
		printk(KERN_ERR "sr_vdd2_autocomp: Invalid value\n");
		return -EINVAL;
	}

	if (value != 0) {
		pr_warning("VDD2 smartreflex is broken\n");
		return -EINVAL;
	}

	mutex_lock(&dvfs_mutex);

	current_vdd2opp_no = resource_get_level("vdd2_opp");

	if (value == 0)
		sr_stop_vddautocomap(SR2);
	else
		sr_start_vddautocomap(SR2, current_vdd2opp_no);

	mutex_unlock(&dvfs_mutex);

	return n;
}

static struct kobj_attribute sr_vdd2_autocomp = {
	.attr = {
	.name = __stringify(sr_vdd2_autocomp),
	.mode = 0644,
	},
	.show = omap_sr_vdd2_autocomp_show,
	.store = omap_sr_vdd2_autocomp_store,
};

static ssize_t omap_sr_opp1_efuse_show(struct kobject *kobj,
					struct kobj_attribute *attr,
					char *buf)
{
	return sprintf(buf, "%08x\n%08x\n%08x\n%08x\n%08x\n", sr1.opp1_nvalue,
							sr1.opp2_nvalue,
							sr1.opp3_nvalue,
							sr1.opp4_nvalue,
							sr1.opp5_nvalue);
}

static struct kobj_attribute sr_efuse = {
	.attr = {
	.name = "Efuse",
	.mode = 0444,
	},
	.show = omap_sr_opp1_efuse_show,
};

static int __init omap3_sr_init(void)
{
	int ret = 0;
	u8 RdReg;

	/* Enable SR on T2 */
	ret = twl4030_i2c_read_u8(TWL4030_MODULE_PM_RECEIVER, &RdReg,
					R_DCDC_GLOBAL_CFG);

	RdReg |= DCDC_GLOBAL_CFG_ENABLE_SRFLX;
	ret |= twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER, RdReg,
					R_DCDC_GLOBAL_CFG);

	if (cpu_is_omap34xx()) {
		sr1.clk = clk_get(NULL, "sr1_fck");
		sr2.clk = clk_get(NULL, "sr2_fck");
	}
	sr_set_clk_length(&sr1);
	sr_set_clk_length(&sr2);

	/* Call the VPConfig, VCConfig, set N Values. */
	sr_set_nvalues(&sr1);
	sr_configure_vp(SR1);

	sr_set_nvalues(&sr2);
	sr_configure_vp(SR2);

	printk(KERN_INFO "SmartReflex driver initialized\n");

	ret = sysfs_create_file(power_kobj, &sr_vdd1_autocomp.attr);
	if (ret)
		printk(KERN_ERR "sysfs_create_file failed: %d\n", ret);

	ret = sysfs_create_file(power_kobj, &sr_vdd2_autocomp.attr);
	if (ret)
		printk(KERN_ERR "sysfs_create_file failed: %d\n", ret);

	ret = sysfs_create_file(power_kobj, &sr_efuse.attr);
	if (ret)
		printk(KERN_ERR "sysfs_create_file failed for OPP data: %d\n", ret);

	return 0;
}

late_initcall(omap3_sr_init);
