/*
 * OMAP3/OMAP4 Voltage Management Routines
 *
 * Author: Thara Gopinath	<thara@ti.com>
 *
 * Copyright (C) 2007 Texas Instruments, Inc.
 * Rajendra Nayak <rnayak@ti.com>
 * Lesly A M <x0080970@ti.com>
 *
 * Copyright (C) 2008 Nokia Corporation
 * Kalle Jokiniemi
 *
 * Copyright (C) 2010 Texas Instruments, Inc.
 * Thara Gopinath <thara@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/pm.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/err.h>

#include <plat/omap-pm.h>
#include <plat/omap34xx.h>
#include <plat/opp.h>
#include <plat/opp_twl_tps.h>
#include <plat/clock.h>
#include <plat/common.h>

#include "prm-regbits-34xx.h"
#include "voltage.h"

#define VP_IDLE_TIMEOUT		200
#define VP_TRANXDONE_TIMEOUT	300

#define ABB_MAX_SETTLING_TIME	30
#define ABB_FAST_OPP		1
#define ABB_NOMINAL_OPP		2
#define ABB_SLOW_OPP		3

/**
 * OMAP3 Voltage controller SR parameters. TODO: Pass this info as part of
 * board data or PMIC data
 */
#define R_SRI2C_SLAVE_ADDR              0x12
#define R_VDD1_SR_CONTROL               0x00
#define R_VDD2_SR_CONTROL		0x01

/* PRM voltage module */
static u32 volt_mod;

static struct clk *dpll1_clk, *l3_clk;
static struct clk *sys_ck;

static int sr2_wtcnt_value;

/* Voltage processor register offsets */
struct vp_reg_offs {
	u8 vpconfig_reg;
	u8 vstepmin_reg;
	u8 vstepmax_reg;
	u8 vlimitto_reg;
	u8 status_reg;
	u8 voltage_reg;
};

/*
 * Voltage processor structure containing info about
 * the various register offsets, the bit field values
 * and the info on various supported volatges and dependent
 * data for a particular instance of voltage processor.
 */
struct vp_info {
	struct omap_volt_data *volt_data;
	struct vp_reg_offs vp_offs;
	int volt_data_count;
	/* Bit fields for VPx_VPCONFIG */
	u32 vp_erroroffset;
	u32 vp_errorgain;
	/* Bit fields for VPx_VSTEPMIN */
	u32 vp_stepmin;
	u32 vp_smpswaittimemin;
	/* Bit fields for VPx_VSTEPMAX */
	u32 vp_stepmax;
	u32 vp_smpswaittimemax;
	/* Bit fields for VPx_VLIMITTO */
	u32 vp_vddmin;
	u32 vp_vddmax;
	u32 vp_timeout;
	u32 vp_tranxdone_status;
};
static struct vp_info *vp_reg;
/*
 * Number of scalable voltage domains that has an independent
 * Voltage processor
 */
static int no_scalable_vdd;

/* OMAP3 VP register offsets and other definitions */
static struct __init vp_reg_offs omap3_vp_offs[] = {
	/* VP1 */
	{
		.vpconfig_reg = OMAP3_PRM_VP1_CONFIG_OFFSET,
		.vstepmin_reg = OMAP3_PRM_VP1_VSTEPMIN_OFFSET,
		.vstepmax_reg = OMAP3_PRM_VP1_VSTEPMAX_OFFSET,
		.vlimitto_reg = OMAP3_PRM_VP1_VLIMITTO_OFFSET,
		.status_reg = OMAP3_PRM_VP1_STATUS_OFFSET,
		.voltage_reg = OMAP3_PRM_VP1_VOLTAGE_OFFSET,
	},
	/* VP2 */
	{	.vpconfig_reg = OMAP3_PRM_VP2_CONFIG_OFFSET,
		.vstepmin_reg = OMAP3_PRM_VP2_VSTEPMIN_OFFSET,
		.vstepmax_reg = OMAP3_PRM_VP2_VSTEPMAX_OFFSET,
		.vlimitto_reg = OMAP3_PRM_VP2_VLIMITTO_OFFSET,
		.status_reg = OMAP3_PRM_VP2_STATUS_OFFSET,
		.voltage_reg = OMAP3_PRM_VP2_VOLTAGE_OFFSET,
	},
};

#define OMAP3_NO_SCALABLE_VDD ARRAY_SIZE(omap3_vp_offs)
static struct vp_info omap3_vp_reg[OMAP3_NO_SCALABLE_VDD];


/* TODO: OMAP4 register offsets */

/*
 * Voltage controller register offsets
 */
static struct vc_reg_info {
	u8 cmdval0_reg;
	u8 cmdval1_reg;
	u8 bypass_val_reg;
} vc_reg;

/*
 * Default voltage controller settings for OMAP3430
 */
static struct __initdata prm_setup_vc omap3430_vc_config = {
	/* CLK & VOLT SETUPTIME for RET */
	.ret = {
		.clksetup = 0x1,
		.voltsetup1_vdd1 = 0x005B,
		.voltsetup1_vdd2 = 0x0055,
		.voltsetup2 = 0x0,
		.voltoffset = 0x0,
	},
	/* CLK & VOLT SETUPTIME for OFF */
	.off = {
		.clksetup = 0x14A,
		.voltsetup1_vdd1 = 0x00B3,
		.voltsetup1_vdd2 = 0x00A0,
		.voltsetup2 = 0x118,
		.voltoffset = 0x32,
	},
	/* VC COMMAND VALUES for VDD1/VDD2 */
	.vdd1_on = 0x30,        /* 1.2v */
	.vdd1_onlp = 0x20,      /* 1.0v */
	.vdd1_ret = 0x1e,       /* 0.975v */
	.vdd1_off = 0x00,       /* 0.6v */
	.vdd2_on = 0x2c,        /* 1.15v */
	.vdd2_onlp = 0x20,      /* 1.0v */
	.vdd2_ret = 0x1e,       /* 0.975v */
	.vdd2_off = 0x00,       /* 0.6v */
};

/*
 * Default voltage controller settings for OMAP3630
 */
static struct __initdata prm_setup_vc omap3630_vc_config = {
	/* CLK & VOLT SETUPTIME for RET */
	.ret = {
		.clksetup = 0xff,
		.voltsetup1_vdd1 = 0x0fff,
		.voltsetup1_vdd2 = 0x0fff,
		.voltsetup2 = 0xff,
		.voltoffset = 0xff,
	},
	/* CLK & VOLT SETUPTIME for OFF */
	.off = {
		.clksetup = 0xff,
		.voltsetup1_vdd1 = 0x0fff,
		.voltsetup1_vdd2 = 0x0fff,
		.voltsetup2 = 0xff,
		.voltoffset = 0xff,
	},
	/* VC COMMAND VALUES for VDD1/VDD2 */
	.vdd1_on = 0x28,	/* 1.1v */
	.vdd1_onlp = 0x20,	/* 1.0v */
	.vdd1_ret = 0x13,	/* 0.83v */
	.vdd1_off = 0x00,	/* 0.6v */
	.vdd2_on = 0x2B,	/* 1.1375v */
	.vdd2_onlp = 0x20,	/* 1.0v */
	.vdd2_ret = 0x13,	/* 0.83v */
	.vdd2_off = 0x00,	/* 0.6v */
};

static struct prm_setup_vc vc_config;

/*
 * Structures containing OMAP3430 voltage supported and various data
 * associated with it per voltage domain basis. Smartreflex Ntarget
 * vales are left as 0 as they have to be populated by smartreflex
 * driver after reading the efuse.
 */

/* VDD1 */
static struct omap_volt_data omap34xx_vdd1_volt_data[] = {
	{.u_volt_nominal = 975000, .sr_errminlimit = 0xF4,
		.vp_errorgain = 0x0C},
	{.u_volt_nominal = 1075000, .sr_errminlimit = 0xF4,
		.vp_errorgain = 0x0C},
	{.u_volt_nominal = 1200000, .sr_errminlimit = 0xF9,
		.vp_errorgain = 0x18},
	{.u_volt_nominal = 1270000, .sr_errminlimit = 0xF9,
		.vp_errorgain = 0x18},
	{.u_volt_nominal = 1350000, .sr_errminlimit = 0xF9,
		.vp_errorgain = 0x18},
};

static struct omap_volt_data omap36xx_vdd1_volt_data[] = {
	{.u_volt_nominal = 1012500, .sr_errminlimit = 0xF4,
		.u_volt_dyn_margin = 50000, .vp_errorgain = 0x0C},
	{.u_volt_nominal = 1200000, .sr_errminlimit = 0xF9,
		.u_volt_dyn_margin = 50000, .vp_errorgain = 0x16},
	{.u_volt_nominal = 1325000, .sr_errminlimit = 0xFA,
		.u_volt_dyn_margin = 50000, .vp_errorgain = 0x23},
	{.u_volt_nominal = 1375000, .sr_errminlimit = 0xFA,
		.u_volt_dyn_margin = 50000, .vp_errorgain = 0x27, .abb = true},
};

/* VDD2 */
static struct omap_volt_data omap34xx_vdd2_volt_data[] = {
	{.u_volt_nominal = 975000, .sr_errminlimit = 0xF4,
		.vp_errorgain = 0x0C},
	{.u_volt_nominal = 1050000, .sr_errminlimit = 0xF4,
		.vp_errorgain = 0x0C},
	{.u_volt_nominal = 1150000, .sr_errminlimit = 0xF9,
		.vp_errorgain = 0x18},
	{.u_volt_nominal = 1200000, .sr_errminlimit = 0xF9,
		.vp_errorgain = 0x18},
};

static struct omap_volt_data omap36xx_vdd2_volt_data[] = {
	{.u_volt_nominal = 1000000, .sr_errminlimit = 0xF4,
		.u_volt_dyn_margin = 50000, .vp_errorgain = 0x0C},
	{.u_volt_nominal = 1200000, .sr_errminlimit = 0xF9,
		.u_volt_dyn_margin = 50000, .vp_errorgain = 0x16},
};


/* By default VPFORCEUPDATE is the chosen method of voltage scaling */
static bool voltscale_vpforceupdate = true;

static inline u32 voltage_read_reg(u8 offset)
{
	return prm_read_mod_reg(volt_mod, offset);
}

static inline void voltage_write_reg(u8 offset, u32 value)
{
	prm_write_mod_reg(value, volt_mod, offset);
}

/**
 * voltagecontroller_init - initializes the voltage controller.
 *
 * Intializes the voltage controller registers with the PMIC and board
 * specific parameters and voltage setup times. If the board file does not
 * populate the voltage controller parameters through omap3_pm_init_vc,
 * default values specified in vc_config is used.
 */
static void __init init_voltagecontroller(void)
{

	u8 vc_ch_conf_reg, vc_i2c_cfg_reg, vc_smps_sa_reg, vc_smps_vol_ra_reg;
	u8 prm_clksetup_reg, prm_voltsetup1_reg;
	u8 prm_voltsetup2_reg, prm_voltoffset_reg;

	if (cpu_is_omap34xx()) {
		vc_reg.cmdval0_reg = OMAP3_PRM_VC_CMD_VAL_0_OFFSET;
		vc_reg.cmdval1_reg = OMAP3_PRM_VC_CMD_VAL_1_OFFSET;
		vc_reg.bypass_val_reg = OMAP3_PRM_VC_BYPASS_VAL_OFFSET;
		vc_ch_conf_reg = OMAP3_PRM_VC_CH_CONF_OFFSET;
		vc_i2c_cfg_reg = OMAP3_PRM_VC_I2C_CFG_OFFSET;
		vc_smps_sa_reg = OMAP3_PRM_VC_SMPS_SA_OFFSET;
		vc_smps_vol_ra_reg = OMAP3_PRM_VC_SMPS_VOL_RA_OFFSET;
		prm_clksetup_reg = OMAP3_PRM_CLKSETUP_OFFSET;
		prm_voltoffset_reg = OMAP3_PRM_VOLTOFFSET_OFFSET;
		prm_voltsetup1_reg = OMAP3_PRM_VOLTSETUP1_OFFSET;
		prm_voltsetup2_reg = OMAP3_PRM_VOLTSETUP2_OFFSET;
	} else {
		pr_warning("%s: support for voltage controller not added\n",
				__func__);
		return;
	}
	voltage_write_reg(vc_smps_sa_reg, (R_SRI2C_SLAVE_ADDR <<
			VC_SMPS_SA1_SHIFT) | (R_SRI2C_SLAVE_ADDR <<
			VC_SMPS_SA0_SHIFT));

	voltage_write_reg(vc_smps_vol_ra_reg, (R_VDD2_SR_CONTROL <<
			VC_VOLRA1_SHIFT) | (R_VDD1_SR_CONTROL <<
			VC_VOLRA0_SHIFT));

	voltage_write_reg(vc_reg.cmdval0_reg,
			(vc_config.vdd1_on << VC_CMD_ON_SHIFT) |
			(vc_config.vdd1_onlp << VC_CMD_ONLP_SHIFT) |
			(vc_config.vdd1_ret << VC_CMD_RET_SHIFT) |
			(vc_config.vdd1_off << VC_CMD_OFF_SHIFT));

	voltage_write_reg(vc_reg.cmdval1_reg,
			(vc_config.vdd2_on << VC_CMD_ON_SHIFT) |
			(vc_config.vdd2_onlp << VC_CMD_ONLP_SHIFT) |
			(vc_config.vdd2_ret << VC_CMD_RET_SHIFT) |
			(vc_config.vdd2_off << VC_CMD_OFF_SHIFT));

	voltage_write_reg(vc_ch_conf_reg, VC_CMD1 | VC_RAV1);

	voltage_write_reg(vc_i2c_cfg_reg, VC_MCODE_SHIFT | VC_HSEN);

	/* Write setup times */
	voltage_write_reg(prm_clksetup_reg, vc_config.ret.clksetup);
	voltage_write_reg(prm_voltsetup1_reg,
		(vc_config.ret.voltsetup1_vdd2 << VC_SETUP_TIME2_SHIFT) |
		(vc_config.ret.voltsetup1_vdd1 << VC_SETUP_TIME1_SHIFT));
	voltage_write_reg(prm_voltoffset_reg, vc_config.ret.voltoffset);
	voltage_write_reg(prm_voltsetup2_reg, vc_config.ret.voltsetup2);
}

static void vp_latch_vsel(int vp_id, struct omap_volt_data *vdata)
{
	u32 vpconfig;
	unsigned long uvdc;
	char vsel;

	uvdc = omap_operation_v(vdata);

	vsel = omap_twl_uv_to_vsel(uvdc);
	vpconfig = voltage_read_reg(vp_reg[vp_id].vp_offs.vpconfig_reg);
	vpconfig &= ~(VP_INITVOLTAGE_MASK | VP_CONFIG_INITVDD);
	vpconfig |= vsel << VP_INITVOLTAGE_SHIFT;

	voltage_write_reg(vp_reg[vp_id].vp_offs.vpconfig_reg, vpconfig);

	/* Trigger initVDD value copy to voltage processor */
	voltage_write_reg(vp_reg[vp_id].vp_offs.vpconfig_reg,
			(vpconfig | VP_CONFIG_INITVDD));

	/* Clear initVDD copy trigger bit */
	voltage_write_reg(vp_reg[vp_id].vp_offs.vpconfig_reg, vpconfig);
}

static void __init vp_configure(int vp_id)
{
	u32 vpconfig;
	unsigned long uvdc = 0;
	struct omap_volt_data *vdata;

	vpconfig = vp_reg[vp_id].vp_erroroffset | vp_reg[vp_id].vp_errorgain |
			VP_CONFIG_TIMEOUTEN;

	voltage_write_reg(vp_reg[vp_id].vp_offs.vpconfig_reg, vpconfig);

	voltage_write_reg(vp_reg[vp_id].vp_offs.vstepmin_reg,
			(vp_reg[vp_id].vp_smpswaittimemin |
			vp_reg[vp_id].vp_stepmin));

	voltage_write_reg(vp_reg[vp_id].vp_offs.vstepmax_reg,
			(vp_reg[vp_id].vp_smpswaittimemax |
			vp_reg[vp_id].vp_stepmax));

	voltage_write_reg(vp_reg[vp_id].vp_offs.vlimitto_reg,
			(vp_reg[vp_id].vp_vddmax | vp_reg[vp_id].vp_vddmin |
			vp_reg[vp_id].vp_timeout));
	/* Should remove this once OPP framework is fixed */
	if (vp_id == VDD1)
		uvdc = get_curr_vdd1_voltage();
	else if (vp_id == VDD2)
		uvdc = get_curr_vdd2_voltage();
	if (!uvdc) {
		pr_err("%s: Voltage processor%d does not exist?\n",
			__func__, vp_id);
		return;
	}
	vdata = omap_get_volt_data(vp_id, uvdc);

	/* Set the init voltage */
	vp_latch_vsel(vp_id, vdata);

	vpconfig = voltage_read_reg(vp_reg[vp_id].vp_offs.vpconfig_reg);
	/* Force update of voltage */
	voltage_write_reg(vp_reg[vp_id].vp_offs.vpconfig_reg,
			(vpconfig | VP_FORCEUPDATE));
	/* Clear force bit */
	voltage_write_reg(vp_reg[vp_id].vp_offs.vpconfig_reg, vpconfig);
}

static void __init vp_data_configure(int vp_id)
{
	if (cpu_is_omap34xx()) {
		unsigned long curr_volt;
		struct omap_volt_data *volt_data;
		u32 sys_clk_speed, timeout_val;

		vp_reg[vp_id].vp_offs = omap3_vp_offs[vp_id];
		if (vp_id == VDD1) {
			u8 vlimitto_vddmin, vlimitto_vddmax;

			if (cpu_is_omap3630()) {
				vlimitto_vddmin = OMAP3630_VP1_VLIMITTO_VDDMIN;
				vlimitto_vddmax = OMAP3630_VP1_VLIMITTO_VDDMAX;
			vp_reg[vp_id].volt_data = omap36xx_vdd1_volt_data;
			vp_reg[vp_id].volt_data_count =
					ARRAY_SIZE(omap36xx_vdd1_volt_data);
			} else {
				vlimitto_vddmin = OMAP3430_VP1_VLIMITTO_VDDMIN;
				vlimitto_vddmax = OMAP3430_VP1_VLIMITTO_VDDMAX;
			vp_reg[vp_id].volt_data = omap34xx_vdd1_volt_data;
			vp_reg[vp_id].volt_data_count =
					ARRAY_SIZE(omap34xx_vdd1_volt_data);
			}
			curr_volt = get_curr_vdd1_voltage();
			vp_reg[vp_id].vp_vddmin = (vlimitto_vddmin <<
					OMAP3430_VDDMIN_SHIFT);
			vp_reg[vp_id].vp_vddmax = (vlimitto_vddmax <<
					OMAP3430_VDDMAX_SHIFT);
			vp_reg[vp_id].vp_tranxdone_status =
					OMAP3430_VP1_TRANXDONE_ST;
		} else if (vp_id == VDD2) {
			u8 vlimitto_vddmin, vlimitto_vddmax;

			if (cpu_is_omap3630()) {
				vlimitto_vddmin = OMAP3630_VP2_VLIMITTO_VDDMIN;
				vlimitto_vddmax = OMAP3630_VP2_VLIMITTO_VDDMAX;
			vp_reg[vp_id].volt_data = omap36xx_vdd2_volt_data;
			vp_reg[vp_id].volt_data_count =
					ARRAY_SIZE(omap36xx_vdd2_volt_data);
			} else {
				vlimitto_vddmin = OMAP3430_VP2_VLIMITTO_VDDMIN;
				vlimitto_vddmax = OMAP3430_VP2_VLIMITTO_VDDMAX;
			vp_reg[vp_id].volt_data = omap34xx_vdd2_volt_data;
			vp_reg[vp_id].volt_data_count =
					ARRAY_SIZE(omap34xx_vdd2_volt_data);
			}
			curr_volt = get_curr_vdd2_voltage();
			vp_reg[vp_id].vp_vddmin = (vlimitto_vddmin <<
					OMAP3430_VDDMIN_SHIFT);
			vp_reg[vp_id].vp_vddmax = (vlimitto_vddmax <<
					OMAP3430_VDDMAX_SHIFT);
			vp_reg[vp_id].vp_tranxdone_status =
					OMAP3430_VP2_TRANXDONE_ST;
		} else {
			pr_warning("%s: Voltage processor%d does not exist "
				   "in OMAP\n", __func__, vp_id);
			return;
		}

		volt_data = omap_get_volt_data(vp_id, curr_volt);
		if (IS_ERR(volt_data)) {
			pr_err("%s: Unable to get voltage table for VDD%d"
				" during vp configure [%ld]!\n", __func__,
				vp_id + 1, curr_volt);
			return;
		}

		vp_reg[vp_id].vp_erroroffset = (OMAP3_VP_CONFIG_ERROROFFSET <<
					OMAP3430_INITVOLTAGE_SHIFT);
		vp_reg[vp_id].vp_errorgain = (volt_data->vp_errorgain <<
					OMAP3430_ERRORGAIN_SHIFT);
		vp_reg[vp_id].vp_smpswaittimemin =
					(OMAP3_VP_VSTEPMIN_SMPSWAITTIMEMIN <<
					OMAP3430_SMPSWAITTIMEMIN_SHIFT);
		vp_reg[vp_id].vp_smpswaittimemax =
					(OMAP3_VP_VSTEPMAX_SMPSWAITTIMEMAX <<
					OMAP3430_SMPSWAITTIMEMAX_SHIFT);
		vp_reg[vp_id].vp_stepmin = (OMAP3_VP_VSTEPMIN_VSTEPMIN <<
					OMAP3430_VSTEPMIN_SHIFT);
		vp_reg[vp_id].vp_stepmax = (OMAP3_VP_VSTEPMAX_VSTEPMAX <<
					OMAP3430_VSTEPMAX_SHIFT);
		/*
		 * Use sys clk speed to convert the VP timeout in us to
		 * number of clock cycles
		 */
		if (IS_ERR(sys_ck)) {
			pr_warning("%s: Could not get the sys clk to calculate "
				   "timeout value for VP %d\n", __func__,
				   vp_id + 1);
			return;
		}
		sys_clk_speed = clk_get_rate(sys_ck);
		/* Divide to avoid overflow */
		sys_clk_speed /= 1000;
		timeout_val = (sys_clk_speed * OMAP3_VP_VLIMITTO_TIMEOUT_US) /
					1000;
		vp_reg[vp_id].vp_timeout = (timeout_val <<
					OMAP3430_TIMEOUT_SHIFT);
	}
	/* TODO Extend this for OMAP4 ?? Or need a separate file  */
}

/*
 * vc_bypass_scale_voltage - VC bypass method of voltage scaling
 */
static int vc_bypass_scale_voltage(u32 vdd, unsigned long target_volt,
				unsigned long current_volt,
				struct omap_volt_data *vdata_target,
				struct omap_volt_data *vdata_current)
{
	u32 vc_bypass_value;
	u32 reg_addr = 0;
	u32 loop_cnt = 0, retries_cnt = 0;
	u32 smps_steps = 0;
	u32 smps_delay = 0;
	u8 target_vsel, current_vsel;

	target_vsel = omap_twl_uv_to_vsel(target_volt);
	current_vsel = omap_twl_uv_to_vsel(current_volt);
	smps_steps = abs(target_vsel - current_vsel);

	if (vdd == VDD1) {
		u32 vc_cmdval0;

		vc_cmdval0 = voltage_read_reg(vc_reg.cmdval0_reg);
		vc_cmdval0 &= ~VC_CMD_ON_MASK;
		vc_cmdval0 |= (target_vsel << VC_CMD_ON_SHIFT);
		voltage_write_reg(vc_reg.cmdval0_reg, vc_cmdval0);
		reg_addr = R_VDD1_SR_CONTROL;

	} else if (vdd == VDD2) {
		u32 vc_cmdval1;

		vc_cmdval1 = voltage_read_reg(vc_reg.cmdval1_reg);
		vc_cmdval1 &= ~VC_CMD_ON_MASK;
		vc_cmdval1 |= (target_vsel << VC_CMD_ON_SHIFT);
		voltage_write_reg(vc_reg.cmdval1_reg, vc_cmdval1);
		reg_addr = R_VDD2_SR_CONTROL;
	} else {
		pr_warning("%s: Wrong VDD[%d] passed!\n", __func__, vdd + 1);
		return -EINVAL;
	}

	/* OMAP3430 has errorgain varying btw various opp's */
	if (cpu_is_omap34xx()) {
		u32 errorgain = voltage_read_reg(vp_reg[vdd].vp_offs.
					vpconfig_reg);

		vp_reg[vdd].vp_errorgain = vdata_target->vp_errorgain <<
			OMAP3430_ERRORGAIN_SHIFT;
		errorgain &= ~VP_ERRORGAIN_MASK;
		errorgain |= vp_reg[vdd].vp_errorgain;
		voltage_write_reg(vp_reg[vdd].vp_offs.vpconfig_reg,
				errorgain);
	}

	vc_bypass_value = (target_vsel << VC_DATA_SHIFT) |
			(reg_addr << VC_REGADDR_SHIFT) |
			(R_SRI2C_SLAVE_ADDR << VC_SLAVEADDR_SHIFT);

	voltage_write_reg(vc_reg.bypass_val_reg, vc_bypass_value);

	voltage_write_reg(vc_reg.bypass_val_reg,
			vc_bypass_value | VC_VALID);
	vc_bypass_value = voltage_read_reg(vc_reg.bypass_val_reg);

	while ((vc_bypass_value & VC_VALID) != 0x0) {
		loop_cnt++;
		if (retries_cnt > 10) {
			pr_warning("%s: Loop count exceeded in check SR I2C"
				"write during voltgae scaling\n", __func__);
			return -ETIMEDOUT;
		}
		if (loop_cnt > 50) {
			retries_cnt++;
			loop_cnt = 0;
			udelay(10);
		}
		vc_bypass_value = voltage_read_reg(vc_reg.bypass_val_reg);
	}

	/*
	 *  T2 SMPS slew rate (min) 4mV/uS, step size 12.5mV,
	 *  2us added as buffer.
	 */
	smps_delay = ((smps_steps * 125) / 40) + 2;
	udelay(smps_delay);
	return 0;
}

static void __init init_voltageprocessors(void)
{
	int i;

	if (cpu_is_omap34xx()) {
		vp_reg = omap3_vp_reg;
		no_scalable_vdd = OMAP3_NO_SCALABLE_VDD;
	} else {
		/* TODO: Add support for OMAP4 */
		pr_warning("%s: Voltage processor support not yet added\n",
				__func__);
		return;
	}
	for (i = 0; i < no_scalable_vdd; i++) {
		vp_data_configure(i);
		vp_configure(i);
	}
}

/**
 * vp_is_transdone() - is transfer done on vp?
 * @vdd: vdd to check
 */
int vp_is_transdone(u32 vdd)
{
	return (prm_read_mod_reg(OCP_MOD, PRM_IRQSTATUS_REG_OFFSET) &
			vp_reg[vdd].vp_tranxdone_status) ? 1 : 0;
}

/**
 * vp_clear_transdone() - clear transdone
 * @vdd: vdd to operate on
 */
void vp_clear_transdone(u32 vdd)
{
	prm_write_mod_reg(vp_reg[vdd].vp_tranxdone_status,
			OCP_MOD, PRM_IRQSTATUS_REG_OFFSET);
}

/**
 * vc_setup_on_voltage() - setup the ON voltage returning from RET/OFF
 * @vdd:	vdd id
 * @target_volt: voltage to set to
 */
void vc_setup_on_voltage(u32 vdd, unsigned long target_volt)
{
	u32 vc_cmdval;
	u8 vc_cmdreg;
	u8 target_vsel;

	target_vsel = omap_twl_uv_to_vsel(target_volt);
	vc_cmdreg = (vdd == VDD1) ? vc_reg.cmdval0_reg : vc_reg.cmdval1_reg;

	vc_cmdval = voltage_read_reg(vc_cmdreg);
	vc_cmdval &= ~VC_CMD_ON_MASK;
	vc_cmdval |= target_vsel << VC_CMD_ON_SHIFT;

	voltage_write_reg(vc_cmdreg, vc_cmdval);
}

/* Public functions */

/* VP force update method of voltage scaling */
static int vp_forceupdate_scale_voltage(u32 vdd, unsigned long target_volt,
					unsigned long current_volt,
					struct omap_volt_data *vdata_target,
					struct omap_volt_data *vdata_current)
{
	u32 smps_steps = 0, smps_delay = 0;
	u32 vpconfig;
	int timeout = 0;
	u8 target_vsel, current_vsel;

	if (!((vdd == VDD1) || (vdd == VDD2))) {
		pr_warning("%s: Wrong vdd id[%d] passed to vp forceupdate\n",
			   __func__, vdd);
		return -EINVAL;
	}

	target_vsel = omap_twl_uv_to_vsel(target_volt);
	current_vsel = omap_twl_uv_to_vsel(current_volt);
	smps_steps = abs(target_vsel - current_vsel);

	vc_setup_on_voltage(vdd, target_volt);

	/*
	 * Clear all pending TransactionDone interrupt/status. Typical latency
	 * is <3us
	 */
	while (timeout++ < VP_TRANXDONE_TIMEOUT) {
		vp_clear_transdone(vdd);
		if (!vp_is_transdone(vdd))
				break;
		udelay(1);
	}

	if (timeout >= VP_TRANXDONE_TIMEOUT) {
		pr_warning("%s: VP%d TRANXDONE timeout exceeded."
			   "Voltage change aborted\n", __func__, vdd);
		return -ETIMEDOUT;
	}

	/* OMAP3430 has errorgain varying btw higher and lower opp's */
	if (cpu_is_omap34xx())
		vp_reg[vdd].vp_errorgain = (vdata_target->vp_errorgain <<
				OMAP3430_ERRORGAIN_SHIFT);

	/* Configure for VP-Force Update */
	vpconfig = voltage_read_reg(vp_reg[vdd].vp_offs.vpconfig_reg);
	vpconfig &= ~(VP_CONFIG_INITVDD | VP_FORCEUPDATE |
			VP_INITVOLTAGE_MASK | VP_ERRORGAIN_MASK);
	vpconfig |= ((target_vsel << VP_INITVOLTAGE_SHIFT) |
			vp_reg[vdd].vp_errorgain);
	voltage_write_reg(vp_reg[vdd].vp_offs.vpconfig_reg, vpconfig);

	/* Trigger initVDD value copy to voltage processor */
	vpconfig |= VP_CONFIG_INITVDD;
	voltage_write_reg(vp_reg[vdd].vp_offs.vpconfig_reg, vpconfig);

	/* Force update of voltage */
	vpconfig |= VP_FORCEUPDATE;
	voltage_write_reg(vp_reg[vdd].vp_offs.vpconfig_reg, vpconfig);

	timeout = 0;
	/*
	 * Wait for TransactionDone. Typical latency is <200us.
	 * Depends on SMPSWAITTIMEMIN/MAX and voltage change
	 */
	omap_test_timeout((prm_read_mod_reg(OCP_MOD, PRM_IRQSTATUS_REG_OFFSET) &
			vp_reg[vdd].vp_tranxdone_status),
			VP_TRANXDONE_TIMEOUT, timeout);

	if (timeout >= VP_TRANXDONE_TIMEOUT)
		pr_err("%s: VP%d TRANXDONE timeout exceeded. TRANXDONE"
			" never got set after the voltage update!\n",
			__func__, vdd);

	/* Wait for voltage to settle with SW wait-loop */
	smps_delay = ((smps_steps * 125) / 40) + 2;
	udelay(smps_delay);

	/*
	 * Disable TransactionDone interrupt , clear all status, clear
	 * control registers
	 */
	timeout = 0;
	while (timeout++ < VP_TRANXDONE_TIMEOUT) {
		vp_clear_transdone(vdd);
		if (!vp_is_transdone(vdd))
				break;
		udelay(1);
	}
	if (timeout >= VP_TRANXDONE_TIMEOUT)
		pr_err("%s: VP%d TRANXDONE timeout exceeded while trying to"
			"clear the TRANXDONE status\n", __func__, vdd);

	vpconfig = voltage_read_reg(vp_reg[vdd].vp_offs.vpconfig_reg);
	/* Clear initVDD copy trigger bit */
	vpconfig &= ~VP_CONFIG_INITVDD;
	voltage_write_reg(vp_reg[vdd].vp_offs.vpconfig_reg, vpconfig);
	/* Clear force bit */
	vpconfig &= ~VP_FORCEUPDATE;
	voltage_write_reg(vp_reg[vdd].vp_offs.vpconfig_reg, vpconfig);

	return 0;
}

static void init_abb(void)
{
	/* calculate SR2_WTCNT_VALUE settling time */
	sr2_wtcnt_value = (ABB_MAX_SETTLING_TIME *
			(clk_get_rate(sys_ck) / 1000000) / 8);
}

/**
 * voltscale_adaptive_body_bias - controls ABB ldo during voltage scaling
 * @enable: enable/disable abb
 *
 * Adaptive Body-Bias is a technique in all OMAP silicon that uses the 45nm
 * process.  ABB can boost voltage in high OPPs for silicon with weak
 * characteristics (forward Body-Bias) as well as lower voltage in low OPPs
 * for silicon with strong characteristics (Reverse Body-Bias).
 *
 * Only Foward Body-Bias for operating at high OPPs is implemented below, per
 * recommendations from silicon team.
 * Reverse Body-Bias for saving power in active cases and sleep cases is not
 * yet implemented.
 * OMAP4 hardward also supports ABB ldo, but no recommendations have been made
 * to implement it yet.
 */
int voltscale_adaptive_body_bias(bool enable)
{
	u32 sr2en_enabled;
	int timeout;

	/* set ACTIVE_FBB_SEL for all 45nm silicon */
	prm_set_mod_reg_bits(OMAP3630_ACTIVE_FBB_SEL,
			OMAP3430_GR_MOD,
			OMAP3_PRM_LDO_ABB_CTRL_OFFSET);

	/* program settling time of 30us for ABB ldo transition */
	prm_rmw_mod_reg_bits(OMAP3630_SR2_WTCNT_VALUE_MASK,
			(sr2_wtcnt_value << OMAP3630_SR2_WTCNT_VALUE_SHIFT),
			OMAP3430_GR_MOD,
			OMAP3_PRM_LDO_ABB_CTRL_OFFSET);

	/* has SR2EN been enabled previously? */
	sr2en_enabled = (prm_read_mod_reg(OMAP3430_GR_MOD,
				OMAP3_PRM_LDO_ABB_CTRL_OFFSET) &
			OMAP3630_SR2EN);
	/* select fast, nominal or slow OPP for ABB ldo */
	if (enable) {
		/* program for fast opp - enable FBB */
		prm_rmw_mod_reg_bits(OMAP3630_OPP_SEL_MASK,
				(ABB_FAST_OPP << OMAP3630_OPP_SEL_SHIFT),
				OMAP3430_GR_MOD,
				OMAP3_PRM_LDO_ABB_SETUP_OFFSET);

		/* enable the ABB ldo if not done already */
		if (!sr2en_enabled)
			prm_set_mod_reg_bits(OMAP3630_SR2EN,
					OMAP3430_GR_MOD,
					OMAP3_PRM_LDO_ABB_CTRL_OFFSET);
	} else {
		/* program for nominal opp - bypass ABB ldo */
		prm_rmw_mod_reg_bits(OMAP3630_OPP_SEL_MASK,
				(ABB_NOMINAL_OPP << OMAP3630_OPP_SEL_SHIFT),
				OMAP3430_GR_MOD,
				OMAP3_PRM_LDO_ABB_SETUP_OFFSET);
	}

	/* clear ABB ldo interrupt status */
	prm_write_mod_reg(OMAP3630_ABB_LDO_TRANXDONE_ST,
			OCP_MOD,
			OMAP2_PRCM_IRQSTATUS_MPU_OFFSET);

	/* enable ABB LDO OPP change */
	prm_set_mod_reg_bits(OMAP3630_OPP_CHANGE,
			OMAP3430_GR_MOD,
			OMAP3_PRM_LDO_ABB_SETUP_OFFSET);

	timeout = 0;

	/* wait until OPP change completes */
	while ((timeout < ABB_MAX_SETTLING_TIME) &&
			(!(prm_read_mod_reg(OCP_MOD,
					    OMAP2_PRCM_IRQSTATUS_MPU_OFFSET) &
			   OMAP3630_ABB_LDO_TRANXDONE_ST))) {
		udelay(1);
		timeout++;
	}

	if (timeout == ABB_MAX_SETTLING_TIME)
		pr_err("%s: ABB: TRANXDONE timed out waiting for OPP change\n",
			__func__);

	timeout = 0;

	/* Clear all pending TRANXDONE interrupts/status */
	while (timeout < ABB_MAX_SETTLING_TIME) {
		prm_write_mod_reg(OMAP3630_ABB_LDO_TRANXDONE_ST,
				OCP_MOD,
				OMAP2_PRCM_IRQSTATUS_MPU_OFFSET);
		if (!(prm_read_mod_reg(OCP_MOD, OMAP2_PRCM_IRQSTATUS_MPU_OFFSET)
					& OMAP3630_ABB_LDO_TRANXDONE_ST))
			break;

		udelay(1);
		timeout++;
	}
	if (timeout == ABB_MAX_SETTLING_TIME)
		pr_err("%s: ABB: TRANXDONE timed out trying to clear status\n",
			__func__);

	return 0;
}

/**
 * get_curr_vdd1_voltage : Gets the current non-auto-compensated vdd1 voltage
 *
 * This is a temporary placeholder for this API. This should ideally belong
 * to Shared resource framework.
 */
unsigned long get_curr_vdd1_voltage(void)
{
	struct omap_opp *opp;
	unsigned long freq;

	if (IS_ERR(dpll1_clk))
		return 0;

	freq = dpll1_clk->rate;
	opp = opp_find_freq_ceil(OPP_MPU, &freq);
	if (IS_ERR(opp))
		return 0;

	return opp_get_voltage(opp);
}

/**
 * get_curr_vdd2_voltage : Gets the current non-auto-compensated vdd2 voltage
 *
 * This is a temporary placeholder for this API. This should ideally belong
 * to Shared resource framework.
 */
unsigned long get_curr_vdd2_voltage(void)
{
	struct omap_opp *opp;
	unsigned long freq;

	if (IS_ERR(l3_clk))
		return 0;

	freq = l3_clk->rate;
	opp = opp_find_freq_ceil(OPP_L3, &freq);
	if (IS_ERR(opp))
		return 0;

	return opp_get_voltage(opp);
}

/**
 * omap_voltageprocessor_enable : API to enable a particular VP
 * @vp_id : The id of the VP to be enable.
 *
 * This API enables a particular voltage processor. Needed by the smartreflex
 * class drivers.
 */
void omap_voltageprocessor_enable(int vp_id, struct omap_volt_data *vdata)
{
	u32 vpconfig;

	/* If VP is already enabled, do nothing. Return */
	if (voltage_read_reg(vp_reg[vp_id].vp_offs.vpconfig_reg) &
				VP_CONFIG_VPENABLE) {
		pr_err("%s: vp[%d] already enabled\n", __func__, vp_id);
		return;
	}
	/*
	 * This latching is required only if VC bypass method is used for
	 * voltage scaling during dvfs.
	 */
	if (!voltscale_vpforceupdate)
		vp_latch_vsel(vp_id, vdata);

	vpconfig = voltage_read_reg(vp_reg[vp_id].vp_offs.vpconfig_reg);
	/* Enable VP */
	voltage_write_reg(vp_reg[vp_id].vp_offs.vpconfig_reg,
				vpconfig | VP_CONFIG_VPENABLE);
}

/**
 * omap_voltageprocessor_disable : API to disable a particular VP
 * @vp_id : The id of the VP to be disable.
 *
 * This API disables a particular voltage processor. Needed by the smartreflex
 * class drivers.
 */
void omap_voltageprocessor_disable(int vp_id)
{
	u32 vpconfig;
	int timeout;

	/* If VP is already disabled, do nothing. Return */
	if (!(voltage_read_reg(vp_reg[vp_id].vp_offs.vpconfig_reg) &
				VP_CONFIG_VPENABLE))
		return;

	/* Disable VP */
	vpconfig = voltage_read_reg(vp_reg[vp_id].vp_offs.vpconfig_reg);
	vpconfig &= ~VP_CONFIG_VPENABLE;
	voltage_write_reg(vp_reg[vp_id].vp_offs.vpconfig_reg, vpconfig);

	/*
	 * Wait for VP idle Typical latency is <2us. Maximum latency is ~100us
	 */
	omap_test_timeout((voltage_read_reg
			(vp_reg[vp_id].vp_offs.status_reg)),
			VP_IDLE_TIMEOUT, timeout);

	if (timeout >= VP_IDLE_TIMEOUT)
		pr_err("%s: VP%d idle timedout\n", __func__, vp_id);
	return;
}

/**
 * omap_voltageprocessor_get_voltage() - get the current voltage measured in vp
 * @vp_id: voltage processor id
 *
 * returns the voltage configured at the voltage processor
 */
unsigned long omap_voltageprocessor_get_voltage(int vp_id)
{
	u8 vsel;
	vsel = voltage_read_reg(vp_reg[vp_id].vp_offs.voltage_reg);
	return omap_twl_vsel_to_uv(vsel);
}

/**
 * omap_voltage_scale : API to scale voltage of a particular voltage domain.
 * @vdd : the voltage domain whose voltage is to be scaled
 * @target_vsel : The target voltage of the voltage domain
 * @current_vsel : the current voltage of the voltage domain.
 *
 * This API should be called by the kernel to do the voltage scaling
 * for a particular voltage domain during dvfs or any other situation.
 */
int omap_voltage_scale(int vdd, struct omap_volt_data *vdata_target,
					struct omap_volt_data *vdata_current)
{
	int ret;

	u32 current_volt, target_volt;

	if (unlikely(IS_ERR(vdata_target) || IS_ERR(vdata_current))) {
			pr_err("%s: Yikes.. bad parameters!!\n", __func__);
			return -EINVAL;
	}
	target_volt = omap_operation_v(vdata_target);
	current_volt = omap_operation_v(vdata_current);

	/* Disable abb when going to voltage which does not need abb */
	if (!vdata_target->abb && vdata_current->abb)
		ret = voltscale_adaptive_body_bias(false);

	if (voltscale_vpforceupdate)
		ret = vp_forceupdate_scale_voltage(vdd, target_volt,
				current_volt, vdata_target, vdata_current);
	else
		ret = vc_bypass_scale_voltage(vdd, target_volt,
				current_volt, vdata_target, vdata_current);

	/* enable abb when going to voltage which needs abb */
	if (vdata_target->abb && !vdata_current->abb)
		ret = voltscale_adaptive_body_bias(true);

	return ret;
}

/**
 * omap_reset_voltage : Resets the voltage of a particular voltage domain
 * to that of the current OPP.
 * @vdd : the voltage domain whose voltage is to be reset.
 *
 * This API finds out the correct voltage the voltage domain is supposed
 * to be at and resets the voltage to that level. Should be used expecially
 * while disabling any voltage compensation modules.
 *
 * Returns the voltage data struct pointing to current reset voltage if
 * all goes well, else returns err pointer
 */
struct omap_volt_data *omap_reset_voltage(int vdd)
{
	unsigned long target_uvdc, current_uvdc;
	struct omap_volt_data *vdata_target, vdata_current;
	char vsel;

	/* Should remove this once OPP framework is fixed */
	if (vdd == VDD1) {
		target_uvdc = get_curr_vdd1_voltage();
		if (!target_uvdc)
			return ERR_PTR(-EINVAL);
	} else if (vdd == VDD2) {
		target_uvdc = get_curr_vdd2_voltage();
		if (!target_uvdc)
			return ERR_PTR(-EINVAL);
	} else {
		pr_err("%s: Wrong VDD passed in omap_reset_voltage %d\n",
				__func__, vdd + 1);
		return ERR_PTR(-EINVAL);
	}
	vsel = voltage_read_reg(vp_reg[vdd].vp_offs.voltage_reg);
	current_uvdc = (vsel * 12500) + 600000;

	vdata_target = omap_get_volt_data(vdd, target_uvdc);
	if (IS_ERR(vdata_target)) {
		pr_err("%s: oops.. target=%p[%ld] current=[%ld] - error?\n",
				__func__, vdata_target, target_uvdc,
				current_uvdc);
		return ERR_PTR(-EINVAL);
	}

	/*
	 * HACK - take the voltage from sr and pass that as the targetted
	 * voltage
	 */
	memcpy(&vdata_current, vdata_target, sizeof(vdata_current));
	vdata_current.u_volt_nominal = current_uvdc;

	omap_voltage_scale(vdd, vdata_target, &vdata_current);
	return vdata_target;
}

/**
 * omap_change_voltscale_method : API to change the voltage scaling method.
 * @voltscale_method : the method to be used for voltage scaling.
 *
 * This API can be used by the board files to change the method of voltage
 * scaling between vpforceupdate and vcbypass. The parameter values are
 * defined in voltage.h
 */
void omap_change_voltscale_method(int voltscale_method)
{
	switch (voltscale_method) {
	case VOLTSCALE_VPFORCEUPDATE:
		voltscale_vpforceupdate = true;
		return;
	case VOLTSCALE_VCBYPASS:
		voltscale_vpforceupdate = false;
		return;
	default:
		pr_warning("%s: Trying to change the method of voltage scaling"
				"to an unsupported one!\n", __func__);
	}
}

/**
 * omap3_pm_init_vc - polpulates vc_config with values specified in board file
 * @setup_vc - the structure with various vc parameters
 *
 * Updates vc_config with the voltage setup times and other parameters as
 * specified in setup_vc. vc_config is later used in init_voltagecontroller
 * to initialize the voltage controller registers. Board files should call
 * this function with the correct volatge settings corresponding
 * the particular PMIC and chip.
 */
void __init omap_voltage_init_vc(struct prm_setup_vc *setup_vc)
{
	if (cpu_is_omap3430())
		memcpy(&vc_config, &omap3430_vc_config,
				sizeof(struct prm_setup_vc));
	else if (cpu_is_omap3630())
		memcpy(&vc_config, &omap3630_vc_config,
				sizeof(struct prm_setup_vc));

	if (!setup_vc)
		return;

	/* CLK & VOLT SETUPTIME for RET & OFF */
	memcpy(&vc_config, setup_vc, 2 * sizeof(struct setuptime_vc));
}

void omap_voltage_vc_update(int core_next_state)
{
	u32 voltctrl = 0;

	/* update voltsetup time */
	if (core_next_state == PWRDM_POWER_OFF) {
		voltctrl = OMAP3430_AUTO_OFF;
		prm_write_mod_reg(vc_config.off.clksetup, OMAP3430_GR_MOD,
				OMAP3_PRM_CLKSETUP_OFFSET);
		prm_write_mod_reg((vc_config.off.voltsetup1_vdd2 <<
				OMAP3430_SETUP_TIME2_SHIFT) |
				(vc_config.off.voltsetup1_vdd1 <<
				OMAP3430_SETUP_TIME1_SHIFT),
				OMAP3430_GR_MOD, OMAP3_PRM_VOLTSETUP1_OFFSET);

		if (voltage_off_while_idle) {
			voltctrl |= OMAP3430_SEL_OFF;
			prm_write_mod_reg(vc_config.off.voltsetup2,
					OMAP3430_GR_MOD,
					OMAP3_PRM_VOLTSETUP2_OFFSET);
		}

	} else if (core_next_state == PWRDM_POWER_RET) {
		voltctrl = OMAP3430_AUTO_RET;
		prm_write_mod_reg(vc_config.ret.clksetup, OMAP3430_GR_MOD,
				OMAP3_PRM_CLKSETUP_OFFSET);
		prm_write_mod_reg((vc_config.ret.voltsetup1_vdd2 <<
				OMAP3430_SETUP_TIME2_SHIFT) |
				(vc_config.ret.voltsetup1_vdd1 <<
				OMAP3430_SETUP_TIME1_SHIFT),
				OMAP3430_GR_MOD, OMAP3_PRM_VOLTSETUP1_OFFSET);

		/* clear voltsetup2_reg if sys_off not enabled */
		prm_write_mod_reg(vc_config.ret.voltsetup2, OMAP3430_GR_MOD,
				OMAP3_PRM_VOLTSETUP2_OFFSET);
	}

	prm_write_mod_reg(voltctrl, OMAP3430_GR_MOD,
				OMAP3_PRM_VOLTCTRL_OFFSET);
}

/**
 * omap_get_voltage_table : API to get the voltage table associated with a
 *			    particular voltage domain.
 *
 * @vdd : the voltage domain id for which the voltage table is required
 * @volt_data : the voltage table for the particular vdd which is to be
 *		populated by this API
 * @volt_count : number of distinct voltages supported by this vdd which
 *		is to be populated by this API.
 *
 * This API populates the voltage table associated with a VDD and the count
 * of number of voltages supported into the passed parameter pointers.
 */
void omap_get_voltage_table(int vdd, struct omap_volt_data **volt_data,
						int *volt_count)
{
	*volt_data = vp_reg[vdd].volt_data;
	*volt_count = vp_reg[vdd].volt_data_count;
}

/**
 * omap_get_volt_data : API to get the voltage table entry for a particular
 *		     voltage
 * @vdd : the voltage domain id for whose voltage table has to be searched
 * @volt : the voltage to be searched in the voltage table
 *
 * This API searches through the voltage table for the required voltage
 * domain and tries to find a matching entry for the passed voltage volt.
 * If a matching entry is found volt_data is populated with that entry.
 * Returns -ENXIO if not voltage table exisits for the passed voltage
 * domain or if there is no matching entry. On success returns pointer
 * to the matching volt_data.
 */
struct omap_volt_data *omap_get_volt_data(int vdd, unsigned long volt)
{
	int i;

	if (!vp_reg[vdd].volt_data) {
		pr_notice("%s: voltage table does not exist for VDD %d\n",
				__func__, vdd + 1);
		return ERR_PTR(-ENXIO);
	}

	for (i = 0; i < vp_reg[vdd].volt_data_count; i++) {
		if (vp_reg[vdd].volt_data[i].u_volt_nominal == volt) {
			return &vp_reg[vdd].volt_data[i];
		}
	}
	pr_notice("%s: Unable to match the current voltage[%ld] with"
			"the voltage table for VDD %d\n", __func__,
			volt, vdd + 1);
	return ERR_PTR(-ENXIO);
}

/**
 * omap_voltage_init : Volatage init API which does VP and VC init.
 */
void __init omap_voltage_init(void)
{
	if (cpu_is_omap34xx())
		volt_mod = OMAP3430_GR_MOD;
	else
		return;

	dpll1_clk = clk_get(NULL, "dpll1_ck");
	l3_clk = clk_get(NULL, "l3_ick");
	sys_ck = clk_get(NULL, "sys_ck");

	init_voltagecontroller();
	init_voltageprocessors();
	init_abb();
}
