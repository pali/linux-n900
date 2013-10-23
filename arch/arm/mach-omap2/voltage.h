/*
 * OMAP3 Voltage Management Routines
 *
 * Copyright (C) 2009 Texas Instruments, Inc.
 * Thara Gopinath <thara@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARCH_ARM_MACH_OMAP3_VOLTAGE_H
#define __ARCH_ARM_MACH_OMAP3_VOLTAGE_H

#include <linux/err.h>
#include "pm.h"

/* VoltageDomain instances */
#define VDD1	0
#define VDD2	1

struct setuptime_vc{
	u16 clksetup;
	u16 voltsetup1_vdd1;
	u16 voltsetup1_vdd2;
	u16 voltsetup2;
	u16 voltoffset;
};

struct prm_setup_vc {
/* CLK & VOLT SETUPTIME for RET */
	struct setuptime_vc ret;
/* CLK & VOLT SETUPTIME for OFF */
	struct setuptime_vc off;
/* PRM_VC_CMD_VAL_0 specific bits */
	u16 vdd1_on;
	u16 vdd1_onlp;
	u16 vdd1_ret;
	u16 vdd1_off;
/* PRM_VC_CMD_VAL_1 specific bits */
	u16 vdd2_on;
	u16 vdd2_onlp;
	u16 vdd2_ret;
	u16 vdd2_off;
};

#define VOLTSCALE_VPFORCEUPDATE		1
#define VOLTSCALE_VCBYPASS		2

#define PRM_IRQSTATUS_REG_OFFSET	OMAP3_PRM_IRQSTATUS_MPU_OFFSET
/* Generic VP definitions. Need to be redefined for OMAP4 */
#define VP_CONFIG_TIMEOUTEN	OMAP3430_TIMEOUTEN
#define VP_CONFIG_INITVDD	OMAP3430_INITVDD
#define VP_FORCEUPDATE		OMAP3430_FORCEUPDATE
#define VP_CONFIG_VPENABLE	OMAP3430_VPENABLE
#define VP_ERRORGAIN_MASK	OMAP3430_ERRORGAIN_MASK
#define VP_INITVOLTAGE_MASK	OMAP3430_INITVOLTAGE_MASK
#define VP_INITVOLTAGE_SHIFT	OMAP3430_INITVOLTAGE_SHIFT

/* Generic VC definitions. Need to be redefined for OMAP4 */
#define VC_SMPS_SA1_SHIFT	OMAP3430_SMPS_SA1_SHIFT
#define VC_SMPS_SA0_SHIFT	OMAP3430_SMPS_SA0_SHIFT
#define VC_VOLRA1_SHIFT		OMAP3430_VOLRA1_SHIFT
#define VC_VOLRA0_SHIFT		OMAP3430_VOLRA0_SHIFT
#define VC_CMD_ON_SHIFT		OMAP3430_VC_CMD_ON_SHIFT
#define VC_CMD_ONLP_SHIFT	OMAP3430_VC_CMD_ONLP_SHIFT
#define VC_CMD_RET_SHIFT	OMAP3430_VC_CMD_RET_SHIFT
#define VC_CMD_OFF_SHIFT	OMAP3430_VC_CMD_OFF_SHIFT
#define VC_SETUP_TIME2_SHIFT	OMAP3430_SETUP_TIME2_SHIFT
#define VC_SETUP_TIME1_SHIFT	OMAP3430_SETUP_TIME1_SHIFT
#define VC_DATA_SHIFT		OMAP3430_DATA_SHIFT
#define VC_REGADDR_SHIFT	OMAP3430_REGADDR_SHIFT
#define VC_SLAVEADDR_SHIFT	OMAP3430_SLAVEADDR_SHIFT
#define VC_CMD_ON_MASK		OMAP3430_VC_CMD_ON_MASK
#define VC_CMD1			OMAP3430_CMD1
#define VC_RAV1			OMAP3430_RAV1
#define VC_MCODE_SHIFT		OMAP3430_MCODE_SHIFT
#define VC_HSEN			OMAP3430_HSEN
#define VC_VALID		OMAP3430_VALID


/*
 * Omap 3430 VP registerspecific values. Maybe these need to come from
 * board file or PMIC data structure
 */
#define OMAP3_VP_CONFIG_ERROROFFSET		0x00
#define	OMAP3_VP_VSTEPMIN_SMPSWAITTIMEMIN	0x3C
#define OMAP3_VP_VSTEPMIN_VSTEPMIN		0x1
#define OMAP3_VP_VSTEPMAX_SMPSWAITTIMEMAX	0x3C
#define OMAP3_VP_VSTEPMAX_VSTEPMAX		0x04
#define OMAP3_VP_VLIMITTO_TIMEOUT_US		0x200

/*
 * Omap3430 specific VP register values. Maybe these need to come from
 * board file or PMIC data structure
 */
#define OMAP3430_VP1_VLIMITTO_VDDMIN		0x14
#define OMAP3430_VP1_VLIMITTO_VDDMAX		0x42
#define OMAP3430_VP2_VLIMITTO_VDDMAX		0x2C
#define OMAP3430_VP2_VLIMITTO_VDDMIN		0x18

/*
 * Omap3630 specific VP register values. Maybe these need to come from
 * board file or PMIC data structure
 */
#define OMAP3630_VP1_VLIMITTO_VDDMIN		0x14
#define OMAP3630_VP1_VLIMITTO_VDDMAX		0x44
#define OMAP3630_VP2_VLIMITTO_VDDMIN		0x12
#define OMAP3630_VP2_VLIMITTO_VDDMAX		0x42

/* TODO OMAP4 VP register values if the same file is used for OMAP4*/

/**
 * omap_volt_data - Omap voltage specific data.
 *
 * @u_volt_nominal	: The possible voltage value in uVolts
 * @u_volt_dyn_nominal	: The run time optimized nominal voltage for device.
 *			  this dynamic nominal is the nominal voltage
 *			  specialized for that device at that time.
 * @u_volt_dyn_margin	: margin to add on top of calib voltage for this opp
 * @u_volt_calib	: Calibrated voltage for this opp
 * @sr_nvalue		: Smartreflex N target value at voltage <voltage>
 * @sr_errminlimit	: Error min limit value for smartreflex. This value
 *			  differs at differnet opp and thus is linked
 *			  with voltage.
 * @vp_errorgain	: Error gain value for the voltage processor. This
 *			  field also differs according to the voltage/opp.
 */
struct omap_volt_data {
	unsigned long	u_volt_nominal;
	unsigned long	u_volt_dyn_nominal;
	unsigned long	u_volt_dyn_margin;
	unsigned long	u_volt_calib;
	u32		sr_nvalue;
	u8		sr_errminlimit;
	u8		vp_errorgain;
	bool		abb;
	u32		sr_val;
	u32		sr_error;
};

/* tell me what the current operation voltage I should use? */
static inline unsigned long omap_operation_v(struct omap_volt_data *vdata)
{
	return (vdata->u_volt_calib) ? vdata->u_volt_calib :
		(vdata->u_volt_dyn_nominal) ? vdata->u_volt_dyn_nominal :
			vdata->u_volt_nominal;
}

/* what is my dynamic nominal? */
static inline unsigned long omap_get_dyn_nominal(struct omap_volt_data *vdata)
{
	if (vdata->u_volt_calib) {
		unsigned long v = vdata->u_volt_calib +
			vdata->u_volt_dyn_margin;
		if (v > vdata->u_volt_nominal)
			return vdata->u_volt_nominal;
		return v;
	}
	return vdata->u_volt_nominal;
}

#ifdef CONFIG_PM
void omap_voltageprocessor_enable(int vp_id, struct omap_volt_data *vdata);
void omap_voltageprocessor_disable(int vp_id);
unsigned long omap_voltageprocessor_get_voltage(int vp_id);
void omap_voltage_init_vc(struct prm_setup_vc *setup_vc);
void omap_voltage_vc_update(int core_next_state);
void omap_voltage_init(void);
int omap_voltage_scale(int vdd, struct omap_volt_data *vdata_target,
					struct omap_volt_data *vdata_current);
struct omap_volt_data *omap_reset_voltage(int vdd);
void omap_change_voltscale_method(int voltscale_method);
void omap_get_voltage_table(int vdd, struct omap_volt_data **volt_data,
						int *volt_count);
struct omap_volt_data *omap_get_volt_data(int vdd, unsigned long volt);
unsigned long get_curr_vdd1_voltage(void);
unsigned long get_curr_vdd2_voltage(void);
int voltscale_adaptive_body_bias(bool enable);
void vc_setup_on_voltage(u32 vdd, unsigned long target_volt);
void vp_clear_transdone(u32 vdd);
int vp_is_transdone(u32 vdd);

#else

static inline void omap_voltageprocessor_enable(int vp_id,
		struct omap_volt_data *vdata) {}
static inline void omap_voltageprocessor_disable(int vp_id) {}
static inline unsigned long omap_voltageprocessor_get_voltage(int vp_id)
{
	return 0;
}
static inline void omap_voltage_init_vc(struct prm_setup_vc *setup_vc) {}
static inline void omap_voltage_vc_update(int core_next_state) {}
static inline void omap_voltage_init(void) {}
static inline int omap_voltage_scale(int vdd,
		struct omap_volt_data *vdata_target,
		struct omap_volt_data *vdata_current) { return -EINVAL; }
static inline struct omap_volt_data *omap_reset_voltage(int vdd)
{
	return ERR_PTR(-EINVAL);
}
static inline void omap_change_voltscale_method(int voltscale_method) {}
static inline void omap_get_voltage_table(int vdd,
		struct omap_volt_data **volt_data, int *volt_count) {}
static inline struct omap_volt_data *omap_get_volt_data(int vdd,
		unsigned long volt)
{
		return ERR_PTR(-ENXIO);
}
static inline unsigned long get_curr_vdd1_voltage(void) { return 0; }
static inline unsigned long get_curr_vdd2_voltage(void) { return 0; }
static inline int voltscale_adaptive_body_bias(bool enable) { return -EINVAL; }
static inline void vc_setup_on_voltage(u32 vdd, unsigned long target_volt) {}
static inline void vp_clear_transdone(u32 vdd) {}
static inline int vp_is_transdone(u32 vdd) { return -EINVAL; }

#endif	/* CONFIG_PM */
#endif
