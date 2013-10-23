/*
 * linux/arch/arm/mach-omap3/smartreflex.c
 *
 * OMAP34XX SmartReflex Voltage Control
 *
 * Copyright (C) 2010 Texas Instruments, Inc.
 * Thara Gopinath <thara@ti.com>
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

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/kobject.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/debugfs.h>
#include <linux/delay.h>

#include <plat/omap_hwmod.h>
#include <plat/omap_device.h>
#include <plat/common.h>

#include "voltage.h"
#include "smartreflex.h"
#include "resource34xx_mutex.h"

#define SMARTREFLEX_NAME_LEN	16
#define SR_DISABLE_TIMEOUT	200

#define ACCURACY		100
#define NDELTA_3430		(3.0 * ACCURACY)
#define PDELTA_3430		(2.6 * ACCURACY)
#define NDELTA_3630		(3.3 * ACCURACY)
#define PDELTA_3630		(2.9 * ACCURACY)

struct omap_sr {
	int			srid;
	int			is_sr_enable;
	int			is_autocomp_active;
	int			sr_ip_type;
	u32			clk_length;
	u32			err_weight;
	u32			err_minlimit;
	u32			err_maxlimit;
	u32			accum_data;
	u32			senn_avgweight;
	u32			senp_avgweight;
	unsigned int		irq;
	void __iomem		*base;
	struct kobj_attribute	sysfs_attr;
	struct platform_device	*pdev;
	struct list_head	node;
};

/* sr_list contains all the instances of smartreflex module */
static LIST_HEAD(sr_list);
static struct omap_smartreflex_class_data *sr_class;
static struct omap_smartreflex_pmic_data *sr_pmic_data;

static inline void sr_write_reg(struct omap_sr *sr, unsigned offset, u32 value)
{
	__raw_writel(value, (sr->base + offset));
}

static inline void sr_modify_reg(struct omap_sr *sr, unsigned offset, u32 mask,
					u32 value)
{
	u32 reg_val;
	u32 errconfig_offs = 0;
	u32 errconfig_mask = 0;

	reg_val = __raw_readl(sr->base + offset);
	reg_val &= ~mask;
	/*
	 * Smartreflex error config register is special as it contains
	 * certain status bits which if written a 1 into means a clear
	 * of those bits. So in order to make sure no accidental write of
	 * 1 happens to those status bits, do a clear of them in the read
	 * value. Now if there is an actual reguest to write to these bits
	 * they will be set in the nex step.
	 */
	if (sr->sr_ip_type == SR_TYPE_V1) {
		errconfig_offs = ERRCONFIG_V1;
		errconfig_mask = ERRCONFIG_STATUS_V1_MASK;
	} else if (sr->sr_ip_type == SR_TYPE_V2) {
		errconfig_offs = ERRCONFIG_V2;
		errconfig_mask = ERRCONFIG_VPBOUNDINTST_V2;
	}
	if (offset == errconfig_offs)
		reg_val &= ~errconfig_mask;

	reg_val |= value;

	__raw_writel(reg_val, (sr->base + offset));
}

static inline u32 sr_read_reg(struct omap_sr *sr, unsigned offset)
{
	return __raw_readl(sr->base + offset);
}

static struct omap_sr *_sr_lookup(int srid)
{
	struct omap_sr *sr_info, *temp_sr_info;

	sr_info = NULL;
	list_for_each_entry(temp_sr_info, &sr_list, node) {
		if (srid == temp_sr_info->srid) {
			sr_info = temp_sr_info;
			break;
		}
	}
	return sr_info;
}

static inline u32 notifier_to_irqen_v1(u8 notify_flags)
{
	u32 val;
	val = (notify_flags & SR_NOTIFY_MCUACCUM) ?
		ERRCONFIG_MCUACCUMINTEN : 0;
	val |= (notify_flags & SR_NOTIFY_MCUVALID) ?
		ERRCONFIG_MCUVALIDINTEN : 0;
	val |= (notify_flags & SR_NOTIFY_MCUBOUND) ?
		ERRCONFIG_MCUBOUNDINTEN : 0;
	val |= (notify_flags & SR_NOTIFY_MCUDISACK) ?
		ERRCONFIG_MCUDISACKINTEN : 0;
	return val;
}

static inline u32 notifier_to_irqen_v2(u8 notify_flags)
{
	u32 val;
	val = (notify_flags & SR_NOTIFY_MCUACCUM) ?
		IRQENABLE_MCUACCUMINT : 0;
	val |= (notify_flags & SR_NOTIFY_MCUVALID) ?
		IRQENABLE_MCUVALIDINT : 0;
	val |= (notify_flags & SR_NOTIFY_MCUBOUND) ?
		IRQENABLE_MCUBOUNDSINT : 0;
	val |= (notify_flags & SR_NOTIFY_MCUDISACK) ?
		IRQENABLE_MCUDISABLEACKINT : 0;
	return val;
}

static inline u8 irqstat_to_notifier_v1(u32 status)
{
	u8 val;
	val = (status & ERRCONFIG_MCUACCUMINTST) ?
		SR_NOTIFY_MCUACCUM : 0;
	val |= (status & ERRCONFIG_MCUVALIDINTEN) ?
		SR_NOTIFY_MCUVALID : 0;
	val |= (status & ERRCONFIG_MCUBOUNDINTEN) ?
		SR_NOTIFY_MCUBOUND : 0;
	val |= (status & ERRCONFIG_MCUDISACKINTEN) ?
		SR_NOTIFY_MCUDISACK : 0;
	return val;
}

static inline u8 irqstat_to_notifier_v2(u32 status)
{
	u8 val;
	val = (status & IRQENABLE_MCUACCUMINT) ?
		SR_NOTIFY_MCUACCUM : 0;
	val |= (status & IRQENABLE_MCUVALIDINT) ?
		SR_NOTIFY_MCUVALID : 0;
	val |= (status & IRQENABLE_MCUBOUNDSINT) ?
		SR_NOTIFY_MCUBOUND : 0;
	val |= (status & IRQENABLE_MCUDISABLEACKINT) ?
		SR_NOTIFY_MCUDISACK : 0;
	return val;
}

static irqreturn_t sr_omap_isr(int irq, void *data)
{
	struct omap_sr *sr_info = (struct omap_sr *)data;
	u32 status = 0;
	u32 value = 0;

	if (sr_info->sr_ip_type == SR_TYPE_V1) {
		/* Status bits are one bit before enable bits in v1 */
		value = notifier_to_irqen_v1(sr_class->notify_flags) >> 1;

		/* Read the status bits */
		status = sr_read_reg(sr_info, ERRCONFIG_V1);
		status &= value;
		/* Clear them by writing back */
		sr_modify_reg(sr_info, ERRCONFIG_V1, value, status);

		value = irqstat_to_notifier_v1(status);

	} else if (sr_info->sr_ip_type == SR_TYPE_V2) {
		value = notifier_to_irqen_v2(sr_class->notify_flags);

		/* Read the status bits */
		status = sr_read_reg(sr_info, IRQSTATUS);
		status &= value;
		/* Clear them by writing back */
		sr_write_reg(sr_info, IRQSTATUS, status);

		value = irqstat_to_notifier_v2(status);
	}

	/* Attempt some resemblence of recovery! */
	if (!value) {
		pr_err("%s:[%d] Spurious interrupt! status = 0x%08x."
			"Disabling to prevent spamming!!\n", __func__,
			sr_info->srid, status);
		disable_irq_nosync(sr_info->irq);
	}

	/* Call the class driver notify function if registered*/
	if (sr_class->notify)
		sr_class->notify(sr_info->srid, value);

	return IRQ_HANDLED;
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
	default:
		pr_err("Invalid sysclk value: %d\n", sys_clk_speed);
		break;
	}
}

static void sr_set_regfields(struct omap_sr *sr)
{
	/*
	 * For time being these values are defined in smartreflex.h
	 * and populated during init. May be they can be moved to board
	 * file or pmic specific data structure. In that case these structure
	 * fields will have to be populated using the pdata or pmic structure.
	 */
	if (cpu_is_omap34xx()) {
		sr->err_weight = OMAP3430_SR_ERRWEIGHT;
		sr->err_maxlimit = OMAP3430_SR_ERRMAXLIMIT;
		sr->accum_data = OMAP3430_SR_ACCUMDATA;
		if (sr->srid == VDD1) {
			sr->senn_avgweight = OMAP3430_SR1_SENNAVGWEIGHT;
			sr->senp_avgweight = OMAP3430_SR1_SENPAVGWEIGHT;
		} else {
			sr->senn_avgweight = OMAP3430_SR2_SENNAVGWEIGHT;
			sr->senp_avgweight = OMAP3430_SR2_SENPAVGWEIGHT;
		}
	}
	/* TODO: 3630 and Omap4 specific bit field values */
}

static void sr_start_vddautocomp(struct omap_sr *sr)
{
	struct omap_volt_data *vdata;
	if (!sr_class || !(sr_class->enable) || !(sr_class->configure)) {
		pr_warning("smartreflex class driver not registered\n");
		return;
	}

	if (sr_class->class_init &&
	    sr_class->class_init(sr->srid, sr_class->class_priv_data)) {
		pr_err("%s: SR[%d]Class initialization failed\n", __func__,
			sr->srid);
		return;
	}
	sr->is_autocomp_active = 1;
	vdata = (sr->srid == VDD1) ?
		omap_get_volt_data(VDD1, get_curr_vdd1_voltage()) :
		omap_get_volt_data(VDD2, get_curr_vdd2_voltage());
	if (sr_class->enable(sr->srid, vdata))
		sr->is_autocomp_active = 0;
}

static void  sr_stop_vddautocomp(struct omap_sr *sr)
{
	if (!sr_class || !(sr_class->disable)) {
		pr_warning("smartreflex class driver not registered\n");
		return;
	}

	if (sr->is_autocomp_active == 1) {
		struct omap_volt_data *vdata;
		vdata = (sr->srid == VDD1) ?
			omap_get_volt_data(VDD1, get_curr_vdd1_voltage()) :
			omap_get_volt_data(VDD2, get_curr_vdd2_voltage());
		sr_class->disable(sr->srid, vdata, 1);
		if (sr_class->class_deinit &&
		    sr_class->class_deinit(sr->srid,
		      sr_class->class_priv_data)) {
			pr_err("%s: SR[%d]Class deinitialization failed\n",
					__func__, sr->srid);
		}
		sr->is_autocomp_active = 0;
	}
}

/*
 * This function handles the intializations which have to be done
 * only when both sr device and class driver regiter has
 * completed. This will be attempted to be called from both sr class
 * driver register and sr device intializtion API's. Only one call
 * will ultimately succeed.
 *
 * Currenly this function registers interrrupt handler for a particular SR
 * if smartreflex class driver is already registered and has
 * requested for interrupts and the SR interrupt line in present.
 */
static int sr_late_init(struct omap_sr *sr_info)
{
	char *name;
	struct omap_smartreflex_data *pdata = sr_info->pdev->dev.platform_data;
	int ret = 0;

	if (sr_class->notify && sr_class->notify_flags && sr_info->irq) {
		name = kzalloc(4, GFP_KERNEL);
		if (!name)
			pr_warning("%s: unable to allocate memory for name\n",
				__func__);
		else
			sprintf(name, "sr%d", sr_info->srid);
		ret = request_irq(sr_info->irq, sr_omap_isr,
				IRQF_DISABLED, name, (void *)sr_info);
		if (ret < 0) {
			iounmap(sr_info->base);
			pr_warning("%s: ERROR in registering interrupt "
				"handler for SR%d. Smartreflex will "
				"not function as desired %d ret\n",
				__func__, sr_info->srid, ret);
			kfree(name);
		} else {
			disable_irq(sr_info->irq);
		}
	}

	if (pdata->enable_on_init)
		sr_start_vddautocomp(sr_info);

	return ret;
}

static void sr_v1_disable(struct omap_sr *sr)
{
	int timeout = 0;

	/* Enable MCUDisableAcknowledge interrupt */
	sr_modify_reg(sr, ERRCONFIG_V1,
			ERRCONFIG_MCUDISACKINTEN, ERRCONFIG_MCUDISACKINTEN);

	/* SRCONFIG - disable SR */
	sr_modify_reg(sr, SRCONFIG, SRCONFIG_SRENABLE, 0x0);

	/* Disable all other SR interrupts and clear the status */
	sr_modify_reg(sr, ERRCONFIG_V1,
			(ERRCONFIG_MCUACCUMINTEN | ERRCONFIG_MCUVALIDINTEN |
			ERRCONFIG_MCUBOUNDINTEN | ERRCONFIG_VPBOUNDINTEN_V1),
			(ERRCONFIG_MCUACCUMINTST | ERRCONFIG_MCUVALIDINTST |
			ERRCONFIG_MCUBOUNDINTST |
			ERRCONFIG_VPBOUNDINTST_V1));

	/*
	 * Wait for SR to be disabled.
	 * wait until ERRCONFIG.MCUDISACKINTST = 1. Typical latency is 1us.
	 */
	omap_test_timeout((sr_read_reg(sr, ERRCONFIG_V1) &
			ERRCONFIG_MCUDISACKINTST), SR_DISABLE_TIMEOUT,
			timeout);

	if (timeout >= SR_DISABLE_TIMEOUT)
		pr_warning("SR%d disable timedout\n", sr->srid);

	/* Disable MCUDisableAcknowledge interrupt & clear pending interrupt */
	sr_modify_reg(sr, ERRCONFIG_V1, ERRCONFIG_MCUDISACKINTEN,
			ERRCONFIG_MCUDISACKINTST);
}

static void sr_v2_disable(struct omap_sr *sr)
{
	int timeout = 0;

	/* Enable MCUDisableAcknowledge interrupt */
	sr_write_reg(sr, IRQENABLE_SET, IRQENABLE_MCUDISABLEACKINT);

	/* SRCONFIG - disable SR */
	sr_modify_reg(sr, SRCONFIG, SRCONFIG_SRENABLE, 0x0);

	/* Disable all other SR interrupts and clear the status */
	sr_modify_reg(sr, ERRCONFIG_V2, ERRCONFIG_VPBOUNDINTEN_V2,
			ERRCONFIG_VPBOUNDINTST_V2);
	sr_write_reg(sr, IRQENABLE_CLR, (IRQENABLE_MCUACCUMINT |
			IRQENABLE_MCUVALIDINT |
			IRQENABLE_MCUBOUNDSINT));
	sr_write_reg(sr, IRQSTATUS, (IRQSTATUS_MCUACCUMINT |
			IRQSTATUS_MCVALIDINT |
			IRQSTATUS_MCBOUNDSINT));

	/*
	 * Wait for SR to be disabled.
	 * wait until IRQSTATUS.MCUDISACKINTST = 1. Typical latency is 1us.
	 */
	omap_test_timeout((sr_read_reg(sr, IRQSTATUS) &
			IRQSTATUS_MCUDISABLEACKINT), SR_DISABLE_TIMEOUT,
			timeout);

	if (timeout >= SR_DISABLE_TIMEOUT)
		pr_warning("SR%d disable timedout\n", sr->srid);

	/* Disable MCUDisableAcknowledge interrupt & clear pending interrupt */
	sr_write_reg(sr, IRQENABLE_CLR, IRQENABLE_MCUDISABLEACKINT);
	sr_write_reg(sr, IRQSTATUS, IRQSTATUS_MCUDISABLEACKINT);
}

/* Public Functions */

/**
 * is_sr_enabled() - Is sr enabled for this srid?
 * @srid: SRID
 *
 * returns -EINVAL for bad values, else returns status if
 * autocomp is enabled or not for this SRID
 */
bool is_sr_enabled(int srid)
{
	struct omap_sr *sr = _sr_lookup(srid);
	if (!sr) {
		pr_warning("omap_sr struct corresponding to SR%d not found\n",
								srid + 1);
		return false;
	}
	return (sr->is_autocomp_active) ? true : false;
}

/**
 * sr_configure_errgen : Configures the smrtreflex to perform AVS using the
 *			 error generator module.
 * @srid - The id of the sr module to be configured.
 *
 * This API is to be called from the smartreflex class driver to
 * configure the error generator module inside the smartreflex module.
 * SR settings if using the ERROR module inside Smartreflex.
 * SR CLASS 3 by default uses only the ERROR module where as
 * SR CLASS 2 can choose between ERROR module and MINMAXAVG
 * module.
 */
void sr_configure_errgen(int srid)
{
	u32 sr_config, sr_errconfig, errconfig_offs, vpboundint_en;
	u32 vpboundint_st, senp_en , senn_en;
	u8 senp_shift, senn_shift;
	struct omap_sr *sr = _sr_lookup(srid);
	struct omap_smartreflex_data *pdata;

	if (!sr) {
		pr_warning("omap_sr struct corresponding to SR%d not found\n",
								srid + 1);
		return;
	}

	pdata = sr->pdev->dev.platform_data;

	if (!sr->clk_length)
		sr_set_clk_length(sr);

	senp_en = pdata->senp_mod;
	senn_en = pdata->senn_mod;
	sr_config = (sr->clk_length << SRCONFIG_SRCLKLENGTH_SHIFT) |
		SRCONFIG_SENENABLE | SRCONFIG_ERRGEN_EN;
	if (sr->sr_ip_type == SR_TYPE_V1) {
		sr_config |= SRCONFIG_DELAYCTRL;
		senn_shift = SRCONFIG_SENNENABLE_V1_SHIFT;
		senp_shift = SRCONFIG_SENPENABLE_V1_SHIFT;
		errconfig_offs = ERRCONFIG_V1;
		vpboundint_en = ERRCONFIG_VPBOUNDINTEN_V1;
		vpboundint_st = ERRCONFIG_VPBOUNDINTST_V1;
	} else if (sr->sr_ip_type == SR_TYPE_V2) {
		senn_shift = SRCONFIG_SENNENABLE_V2_SHIFT;
		senp_shift = SRCONFIG_SENPENABLE_V2_SHIFT;
		errconfig_offs = ERRCONFIG_V2;
		vpboundint_en = ERRCONFIG_VPBOUNDINTEN_V2;
		vpboundint_st = ERRCONFIG_VPBOUNDINTST_V2;
	} else {
		pr_err("Trying to Configure smartreflex module without \
				specifying the ip\n");
		return;
	}
	sr_config |= ((senn_en << senn_shift) | (senp_en << senp_shift));
	sr_write_reg(sr, SRCONFIG, sr_config);
	sr_errconfig = (sr->err_weight << ERRCONFIG_ERRWEIGHT_SHIFT) |
		(sr->err_maxlimit << ERRCONFIG_ERRMAXLIMIT_SHIFT) |
		(sr->err_minlimit <<  ERRCONFIG_ERRMINLIMIT_SHIFT);
	sr_modify_reg(sr, errconfig_offs, (SR_ERRWEIGHT_MASK |
		SR_ERRMAXLIMIT_MASK | SR_ERRMINLIMIT_MASK),
		sr_errconfig);
	/* Enabling the interrupts if the ERROR module is used */
	sr_modify_reg(sr, errconfig_offs,
		vpboundint_en, (vpboundint_en | vpboundint_st));
}

/**
 * sr_configure_minmax : Configures the smrtreflex to perform AVS using the
 *			 minmaxavg module.
 * @srid - The id of the sr module to be configured.
 *
 * This API is to be called from the smartreflex class driver to
 * configure the minmaxavg module inside the smartreflex module.
 * SR settings if using the ERROR module inside Smartreflex.
 * SR CLASS 3 by default uses only the ERROR module where as
 * SR CLASS 2 can choose between ERROR module and MINMAXAVG
 * module.
 */
void sr_configure_minmax(int srid)
{
	u32 sr_config, sr_avgwt;
	u32 senp_en , senn_en;
	u8 senp_shift, senn_shift;
	struct omap_sr *sr = _sr_lookup(srid);
	struct omap_smartreflex_data *pdata;

	if (!sr) {
		pr_warning("omap_sr struct corresponding to SR%d not found\n",
								srid + 1);
		return;
	}

	pdata = sr->pdev->dev.platform_data;

	if (!sr->clk_length)
		sr_set_clk_length(sr);

	senp_en = pdata->senp_mod;
	senn_en = pdata->senn_mod;
	sr_config = (sr->clk_length << SRCONFIG_SRCLKLENGTH_SHIFT) |
		SRCONFIG_SENENABLE |
		(sr->accum_data << SRCONFIG_ACCUMDATA_SHIFT);
	if (sr->sr_ip_type == SR_TYPE_V1) {
		sr_config |= SRCONFIG_DELAYCTRL;
		senn_shift = SRCONFIG_SENNENABLE_V1_SHIFT;
		senp_shift = SRCONFIG_SENPENABLE_V1_SHIFT;
	} else if (sr->sr_ip_type == SR_TYPE_V2) {
		senn_shift = SRCONFIG_SENNENABLE_V2_SHIFT;
		senp_shift = SRCONFIG_SENPENABLE_V2_SHIFT;
	} else {
		pr_err("Trying to Configure smartreflex module without \
				specifying the ip\n");
		return;
	}
	sr_config |= ((senn_en << senn_shift) | (senp_en << senp_shift));
	sr_write_reg(sr, SRCONFIG, sr_config);
	sr_avgwt = (sr->senp_avgweight << AVGWEIGHT_SENPAVGWEIGHT_SHIFT) |
		(sr->senn_avgweight << AVGWEIGHT_SENNAVGWEIGHT_SHIFT);
	sr_write_reg(sr, AVGWEIGHT, sr_avgwt);
	/*
	 * Enabling the interrupts if MINMAXAVG module is used.
	 * TODO: check if all the interrupts are mandatory
	 */
	if (sr->sr_ip_type == SR_TYPE_V1) {
		sr_modify_reg(sr, ERRCONFIG_V1,
			(ERRCONFIG_MCUACCUMINTEN | ERRCONFIG_MCUVALIDINTEN |
			ERRCONFIG_MCUBOUNDINTEN),
			(ERRCONFIG_MCUACCUMINTEN | ERRCONFIG_MCUACCUMINTST |
			 ERRCONFIG_MCUVALIDINTEN | ERRCONFIG_MCUVALIDINTST |
			 ERRCONFIG_MCUBOUNDINTEN | ERRCONFIG_MCUBOUNDINTST));
	} else if (sr->sr_ip_type == SR_TYPE_V2) {
		sr_write_reg(sr, IRQSTATUS,
			IRQSTATUS_MCUACCUMINT | IRQSTATUS_MCVALIDINT |
			IRQSTATUS_MCBOUNDSINT | IRQSTATUS_MCUDISABLEACKINT);
		sr_write_reg(sr, IRQENABLE_SET,
			IRQENABLE_MCUACCUMINT | IRQENABLE_MCUVALIDINT |
			IRQENABLE_MCUBOUNDSINT | IRQENABLE_MCUDISABLEACKINT);
	}
}

/**
 * sr_enable : Enables the smartreflex module.
 * @srid - The id of the sr module to be enabled.
 * @volt - The voltage at which the Voltage domain associated with
 * the smartreflex module is operating at. This is required only to program
 * the correct Ntarget value.
 *
 * This API is to be called from the smartreflex class driver to
 * enable a smartreflex module. Returns 0 on success.Returns error value if the
 * target opp id passed is wrong or if ntarget value is wrong.
 */
int sr_enable(int srid, struct omap_volt_data *volt_data)
{
	u32 nvalue_reciprocal;
	struct omap_sr *sr = _sr_lookup(srid);

	if (!sr) {
		pr_warning("omap_sr struct corresponding to SR%d not found\n",
								srid + 1);
		return -EINVAL;
	}

	nvalue_reciprocal = volt_data->sr_nvalue;

	if (!nvalue_reciprocal) {
		pr_notice("NVALUE = 0 at voltage %ld for Smartreflex %d\n",
						volt_data->u_volt_nominal,
						sr->srid + 1);
		return -EINVAL;
	}

	/* errminlimit is opp dependent and hence linked to voltage */
	sr->err_minlimit = volt_data->sr_errminlimit;

	/* Enable the clocks */
	if (!sr->is_sr_enable) {
		struct omap_smartreflex_data *pdata =
				sr->pdev->dev.platform_data;
		int r = -EINVAL;
		if (pdata->device_enable)
			r = pdata->device_enable(sr->pdev);
		if (r) {
			pr_err("%s: Not able to turn on SR%d device during"
				"enable. So returning %d\n", __func__,
				sr->srid + 1, r);
			return r;
		}
		sr->is_sr_enable = 1;
	} else {
		pr_err("%s: sr[%d] sr is already enabled?\n", __func__, srid);
	}

	/* Check if SR is already enabled. If yes do nothing */
	if (sr_read_reg(sr, SRCONFIG) & SRCONFIG_SRENABLE)
		return 0;

	/* Configure SR */
	sr_class->configure(sr->srid);

	sr_write_reg(sr, NVALUERECIPROCAL, nvalue_reciprocal);
	/* SRCONFIG - enable SR */
	sr_modify_reg(sr, SRCONFIG, SRCONFIG_SRENABLE, SRCONFIG_SRENABLE);
	return 0;
}

/**
 * omap_sr_get_count() -  get the sr values for error and count
 * @srid:	srid
 * @error:	error value returned
 * @count:	count returned
 *
 * fails if the params are faulty or SR is disabled, else returns 0, if there is
 * valid data populated
 */
int omap_sr_get_count(int srid, unsigned int *error, unsigned int *count)
{
	struct omap_sr *sr = _sr_lookup(srid);
	unsigned int error_reg = 0;
	if (unlikely(!sr || !error || !count)) {
		pr_err("%s: invalid params!\n", __func__);
		return -EINVAL;
	}
	/*
	 * Refuse to cause a crash by accessing reg which dont have iclk
	 * enabled
	 */
	if (!sr->is_sr_enable) {
		pr_err("%s:%d Clock not enabled! please check sequence\n",
			__func__, srid);
		return -EINVAL;
	}
	switch (sr->sr_ip_type) {
	case SR_TYPE_V1:
		error_reg = SENERROR_V1;
		break;
	case SR_TYPE_V2:
		error_reg = SENERROR_V2;
		break;
	default:
		pr_err("%s: unknown revision??\n", __func__);
		return -EINVAL;
	}
	*count = sr_read_reg(sr, SENVAL);
	*error = sr_read_reg(sr, error_reg);
	return 0;
}

/**
 * sr_disable : Disables the smartreflex module.
 * @srid - The id of the sr module to be disabled.
 *
 * This API is to be called from the smartreflex class driver to
 * disable a smartreflex module.
 */
void sr_disable(int srid)
{
	struct omap_sr *sr = _sr_lookup(srid);
	struct omap_smartreflex_data *pdata;

	if (!sr) {
		pr_warning("omap_sr struct corresponding to SR%d not found\n",
								srid + 1);
		return;
	}

	/* Check if SR clocks are already disabled. If yes do nothing */
	if (!sr->is_sr_enable)
		return;

	/* Check if SR is already disabled. If yes just disable the clocks */
	if (!(sr_read_reg(sr, SRCONFIG) & SRCONFIG_SRENABLE))
		goto disable_clocks;

	if (sr->sr_ip_type == SR_TYPE_V1)
		sr_v1_disable(sr);
	else if (sr->sr_ip_type == SR_TYPE_V2)
		sr_v2_disable(sr);

disable_clocks:
	pdata = sr->pdev->dev.platform_data;
	if (pdata->device_idle) {
		pdata->device_idle(sr->pdev);
	} else {
		pr_warning("Unable to turn off SR%d clocks during SR disable",
				srid);
		return;
	}
	sr->is_sr_enable = 0;
}

/**
 * sr_notifier_control() - control the notifier mechanism
 * @srid:	srid to control the notifier
 * @enable:	true to enable notifiers and false to disable the same
 *
 * SR modules allow an MCU interrupt mechanism that vary based on the IP
 * revision, we allow the system to generate interrupt if the class driver
 * has capability to handle the same. it is upto the class driver to ensure
 * the proper sequencing and handling for a clean implementation. returns
 * 0 if all goes fine, else returns failure results
 */
int sr_notifier_control(int srid, bool enable)
{
	struct omap_sr *sr = _sr_lookup(srid);
	u32 value = 0;
	if (!sr) {
		pr_warning("omap_sr struct corresponding to SR%d not found\n",
								srid + 1);
		return -EINVAL;
	}
	if (!sr->is_autocomp_active)
		return -EINVAL;

	/* if I could never register an isr, why bother?? */
	if (!(sr_class && sr_class->notify && sr_class->notify_flags &&
				sr->irq)) {
		pr_err("%s: %d unable to setup irq without handling "
			"mechanism\n", __func__, srid);
		return -EINVAL;
	}

	switch (sr->sr_ip_type) {
	case SR_TYPE_V1:
		value = notifier_to_irqen_v1(sr_class->notify_flags);
		sr_modify_reg(sr, ERRCONFIG_V1, value,
				(enable) ? value : 0);
		break;
	case SR_TYPE_V2:
		value = notifier_to_irqen_v2(sr_class->notify_flags);
		sr_write_reg(sr, (enable) ? IRQENABLE_SET : IRQENABLE_CLR,
				value);
		break;
	default:
		pr_err("%s: unknown type of sr??\n", __func__);
		return -EINVAL;
	}

	if (enable)
		enable_irq(sr->irq);
	else
		disable_irq_nosync(sr->irq);

	return 0;
}

/**
 * omap_smartreflex_enable : API to enable SR clocks and to call into the
 * registered smartreflex class enable API.
 * @srid - The id of the sr module to be enabled.
 *
 * This API is to be called from the kernel in order to enable
 * a particular smartreflex module. This API will do the initial
 * configurations to turn on the smartreflex module and in turn call
 * into the registered smartreflex class enable API.
 */
void omap_smartreflex_enable(int srid, struct omap_volt_data *volt_data)
{
	struct omap_sr *sr = _sr_lookup(srid);

	if (!sr) {
		pr_warning("omap_sr struct corresponding to SR%d not found\n",
								srid + 1);
		return;
	}

	if (!sr->is_autocomp_active)
		return;

	if (!sr_class || !(sr_class->enable) || !(sr_class->configure)) {
		pr_warning("smartreflex class driver not registered\n");
		return;
	}

	sr_class->enable(srid, volt_data);
}

/**
 * omap_smartreflex_disable : API to disable SR clocks and to call into the
 * registered smartreflex class disable API.
 * @srid - The id of the sr module to be disabled.
 * @volt_data - voltage_data which is being disabled
 * @is_volt_reset - Whether the voltage needs to be reset after disabling
 *		    smartreflex module or not. This parameter is directly
 *		    passed on to the smartreflex class disable which takes the
 *		    appropriate action.
 *
 * This API is to be called from the kernel in order to disable
 * a particular smartreflex module. This API will in turn call
 * into the registered smartreflex class disable API.
 */
int omap_smartreflex_disable(int srid, struct omap_volt_data *volt_data,
		int is_volt_reset)
{
	struct omap_sr *sr = _sr_lookup(srid);

	if (!sr) {
		pr_warning("omap_sr struct corresponding to SR%d not found\n",
								srid + 1);
		return 0;
	}

	if (!sr->is_autocomp_active)
		return 0;

	/* If DVFS is performing a transition, then we must not proceed here. */
	if (is_volt_reset && mutex_is_locked(&dvfs_mutex))
		return -EBUSY;

	if (!sr_class || !(sr_class->disable)) {
		pr_warning("smartreflex class driver not registered\n");
		return 0;
	}

	return sr_class->disable(srid, volt_data, is_volt_reset);
}

/**
 * omap_sr_register_class : API to register a smartreflex class parameters.
 * @class_data - The structure containing various sr class specific data.
 *
 * This API is to be called by the smartreflex class driver to register itself
 * with the smartreflex driver during init.
 * Returns 0 if all went well, else returns error
 */
int omap_sr_register_class(struct omap_smartreflex_class_data *class_data)
{
	struct omap_sr *sr_info;

	if (!class_data) {
		pr_warning("Smartreflex class data passed is NULL\n");
		return -EINVAL;
	}

	if (sr_class) {
		pr_warning("Smartreflex class driver already registered\n");
		return -EBUSY;
	}

	sr_class = class_data;

	/*
	 * Call into late init to do intializations that require
	 * both sr driver and sr class driver to be initiallized.
	 */
	list_for_each_entry(sr_info, &sr_list, node)
		 sr_late_init(sr_info);
	return 0;
}

/**
 * omap_sr_register_pmic : API to register pmic specific info.
 * @pmic_data - The structure containing pmic specific data.
 *
 * This API is to be called from the PMIC specific code to register with
 * smartreflex driver pmic specific info. Currently the only info required
 * is the smartreflex init on the PMIC side.
 */
void omap_sr_register_pmic(struct omap_smartreflex_pmic_data *pmic_data)
{
	if (!pmic_data) {
		pr_warning("Trying to register NULL PMIC data structure with \
				smartreflex\n");
		return;
	}
	sr_pmic_data = pmic_data;
}

/* PM sysfs entries to enable disable smartreflex.*/
static ssize_t omap_sr_autocomp_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct omap_sr *sr_info;
	char *tbuf = buf;

	if (!attr || !attr->attr.name) {
		pr_err("%s: atttributes not correct?\n", __func__);
		return -EINVAL;
	}
	sr_info = _sr_lookup(attr->attr.name[6] - '1');

	if (!sr_info) {
		pr_err("%s: omap_sr struct corresponding to SR not"
			" found - %s\n", __func__, attr->attr.name);
		return -EINVAL;
	}
	tbuf += sprintf(tbuf, "%d\n", sr_info->is_autocomp_active);
	return tbuf - buf;
}

static ssize_t omap_sr_autocomp_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t n)
{
	struct omap_sr *sr_info;
	unsigned short val;

	if (!attr || !attr->attr.name) {
		pr_err("%s: atttributes not correct?\n", __func__);
		return -EINVAL;
	}
	sr_info = _sr_lookup(attr->attr.name[6] - '1');
	if (!sr_info) {
		pr_err("%s: omap_sr struct corresponding to SR not"
			" found - %s\n", __func__, attr->attr.name);
		return -EINVAL;
	}
	/* 1 or 0 please */
	if ((sscanf(buf, "%hu", &val) > 1) || (val > 1)) {
		pr_warning("%s: Invalid value '%s' = %d\n", __func__,
				buf, val);
		return -EINVAL;
	}
	/* Change SR value only if we have a delta */
	if (sr_info->is_autocomp_active ^ val) {
		mutex_lock(&dvfs_mutex);
		if (!val)
			sr_stop_vddautocomp(sr_info);
		else
			sr_start_vddautocomp(sr_info);
		mutex_unlock(&dvfs_mutex);
	}
	return n;
}

/**
 * sr_sysfsfs_create_entries_late() - create the Smart Reflex entries -
 * called as part of init sequence of SR uses the dentry registered early
 * @sr_info - SR information
 *
 * Returns 0 if all ok, else returns with error
 */
static __init int sr_sysfsfs_create_entries_late(struct omap_sr *sr_info)
{
	char name[] = "sr_vdd1_autocomp";
	name[6] += sr_info->srid;
	sr_info->sysfs_attr.attr.name = kzalloc(sizeof(name), GFP_KERNEL);
	if (!sr_info->sysfs_attr.attr.name) {
		pr_warning("%s: unable to allocate memory for %s\n", __func__,
				name);
		return -ENOMEM;
	}
	strcpy((char *)sr_info->sysfs_attr.attr.name, name);
	sr_info->sysfs_attr.attr.mode = 0644;
	sr_info->sysfs_attr.show = omap_sr_autocomp_show;
	sr_info->sysfs_attr.store = omap_sr_autocomp_store;
	if (sysfs_create_file(power_kobj, &sr_info->sysfs_attr.attr)) {
		pr_err("%s: sysfs creation of %s failed\n", __func__,
			sr_info->sysfs_attr.attr.name);
		kfree(sr_info->sysfs_attr.attr.name);
		sr_info->sysfs_attr.attr.name = NULL;
		return -EPERM;
	}
	return 0;
}

static int __devinit omap_smartreflex_probe(struct platform_device *pdev)
{
	struct omap_sr *sr_info = kzalloc(sizeof(struct omap_sr), GFP_KERNEL);
	struct omap_device *odev = to_omap_device(pdev);
	int ret = 0;
	struct resource *mem, *irq;

	if (!sr_info) {
		pr_warning("%s: no sr_info!\n", __func__);
		return -ENOMEM;
	}
	sr_info->pdev = pdev;
	sr_info->srid = pdev->id;
	sr_info->is_autocomp_active = 0;
	sr_info->clk_length = 0;
	sr_info->sr_ip_type = odev->hwmods[0]->class->rev;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		dev_err(&pdev->dev, "%s: no mem resource\n", __func__);
		ret = -ENODEV;
		goto err_free_devinfo;
	}
	sr_info->base = ioremap(mem->start, resource_size(mem));
	if (!sr_info->base) {
		dev_err(&pdev->dev, "%s: ioremap fail\n", __func__);
		ret = -ENOMEM;
		goto err_release_region;
	}

	irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (irq)
		sr_info->irq = irq->start;
	sr_set_clk_length(sr_info);
	sr_set_regfields(sr_info);

	list_add(&sr_info->node, &sr_list);

	/*
	 * Call into late init to do intializations that require
	 * both sr driver and sr class driver to be initiallized.
	 */
	if (sr_class)
		ret = sr_late_init(sr_info);
	if (ret) {
		pr_err("SmartReflex device[%d] failed to init=%d\n",
				sr_info->srid + 1, ret);
		goto err_unmap_region;
	} else {
		pr_info("SmartReflex device[%d] initialized\n",
				sr_info->srid + 1);
		/* Create the debug fs enteries */
		sr_sysfsfs_create_entries_late(sr_info);
	}

	return ret;

err_unmap_region:
	iounmap(sr_info->base);

err_release_region:
	release_mem_region(mem->start, resource_size(mem));

err_free_devinfo:
	kfree(sr_info);

	return ret;

}

static int __devexit omap_smartreflex_remove(struct platform_device *pdev)
{
	struct omap_sr *sr_info = _sr_lookup(pdev->id + 1);
	struct resource *mem;

	if (!sr_info) {
		pr_warning("omap_sr struct corresponding to SR%d not found\n",
							pdev->id + 1);
		return 0;
	}

	/* Disable Autocompensation if enabled before removing the module */
	if (sr_info->is_autocomp_active == 1)
		sr_stop_vddautocomp(sr_info);
	list_del(&sr_info->node);
	iounmap(sr_info->base);
	kfree(sr_info);

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(mem->start, resource_size(mem));


	return 0;
}

/**
 * recalc_with_margin() - helper to add margin to reciprocal and gain
 * @uv:		voltage in uVolts to add.
 * @soc_delta:	SoC specific delta base
 * @reci:	Reciprocal for the sensor
 * @gain:	Gain for the sensor
 *
 * The algorithm computes an adjustment required to meet the delta voltage
 * to be added to a given sensor's reciprocal and gain. It then does a
 * search for maximum gain for valid reciprocal value. This forms the
 * new reciprocal and gain which incorporates the additional voltage
 * requested.
 *
 * IMPORTANT: since it is not possible to ascertain the actual voltage from
 * ntarget value, the additional voltage will be accurate upto 1 additional
 * pmic step. The algorithm is optimized to adjust to higher end rather than
 * less than requested additional voltage as it could be unsafe to run at
 * voltage lower than requested level.
 *
 * Example: if the PMIC step size is 12.5 and requested margin in 25mV(2 PMIC
 * steps). the actual voltage achieved can be original V achieved + 25mV upto
 * original V + 37.5mV(3 steps) - depending on where V was achieved.
 */
static __init int recalc_with_margin(long uv, int soc_delta, unsigned int *reci,
		unsigned int *gain)
{
	int r = 0, g = 0;
	int nadj = 0;

	nadj = ((1 << (*gain + 8)) * ACCURACY) / (*reci) +
		soc_delta * uv / 1000;

	/* Linear search for the best reciprocal */
	for (g = 15; g >= 0; g--) {
		r = ((1 << (g + 8)) * ACCURACY) / nadj;
		if (r < 256) {
			*reci = r;
			*gain = g;
			return 0;
		}
	}
	/* Dont modify the input, just return error */
	return -EINVAL;
}

/**
 * sr_ntarget_add_margin() - Modify h/w ntarget to add a s/w margin
 * @vdata:	voltage data for the OPP to be modified with ntarget populated
 * @add_uv:	voltate to add to nTarget in uVolts
 *
 * Once the sr_device_init is complete and nTargets are populated, using this
 * function nTarget read from h/w efuse and stored in vdata is modified to add
 * a platform(board) specific additional voltage margin. Based on analysis,
 * we might need different margins to be added per vdata.
 */
int __init sr_ntarget_add_margin(struct omap_volt_data *vdata, ulong add_uv)
{
	u32 old_ntarget = vdata->sr_nvalue;
	u32 temp_senp_gain, temp_senp_reciprocal;
	u32 temp_senn_gain, temp_senn_reciprocal;
	int soc_p_delta, soc_n_delta;
	int r;

	temp_senp_gain = (old_ntarget & 0x00F00000) >> 20;
	temp_senn_gain = (old_ntarget & 0x000F0000) >> 16;
	temp_senp_reciprocal = (old_ntarget & 0x0000FF00) >> 8;
	temp_senn_reciprocal = old_ntarget & 0x000000FF;

	/* FIXME: OMAP4 breakage? */
	if (cpu_is_omap3630()) {
		soc_p_delta = PDELTA_3630;
		soc_n_delta = NDELTA_3630;
	} else {
		soc_p_delta = PDELTA_3430;
		soc_n_delta = NDELTA_3430;
	}

	r = recalc_with_margin(add_uv, soc_n_delta,
			&temp_senn_reciprocal, &temp_senn_gain);
	if (r) {
		pr_err("%s: unable to add %ld uV to ntarget 0x%08x for OPP "
			"voltage %ld on SENN\n",
			__func__, add_uv, old_ntarget, vdata->u_volt_nominal);
		return r;
	}
	r = recalc_with_margin(add_uv, soc_n_delta,
			&temp_senp_reciprocal, &temp_senp_gain);
	if (r) {
		pr_err("%s: unable to add %ld uV to ntarget 0x%08x for OPP "
			"voltage %ld on SENN\n",
			__func__, add_uv, old_ntarget, vdata->u_volt_nominal);
		return r;
	}

	/* Populate the new modified nTarget */
	vdata->sr_nvalue = (temp_senp_gain << 20) | (temp_senn_gain << 16) |
			(temp_senp_reciprocal << 8) | temp_senn_reciprocal;

	pr_debug("%s: nominal v=%ld, add %ld uV, ntarget=0x%08X -> %08X\n",
			__func__, vdata->u_volt_nominal, add_uv,
			old_ntarget, vdata->sr_nvalue);

	return 0;
}

static struct platform_driver smartreflex_driver = {
	.probe          = omap_smartreflex_probe,
	.remove         = omap_smartreflex_remove,
	.driver		= {
		.name	= "smartreflex",
	},
};

static int __init sr_init(void)
{
	int ret;

	if (sr_pmic_data && sr_pmic_data->sr_pmic_init)
		sr_pmic_data->sr_pmic_init();

	ret = platform_driver_probe(&smartreflex_driver,
				omap_smartreflex_probe);

	if (ret)
		pr_err("platform driver register failed for smartreflex");
	return 0;
}

static void __exit sr_exit(void)
{
	platform_driver_unregister(&smartreflex_driver);
}
late_initcall(sr_init);
module_exit(sr_exit);

MODULE_DESCRIPTION("OMAP SMARTREFLEX DRIVER");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRIVER_NAME);
MODULE_AUTHOR("Texas Instruments Inc");
