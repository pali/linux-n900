/*
 * This file is part of the ROHM BH1770GLC / OSRAM SFH7770 sensor driver.
 * Chip is combined proximity and ambient light sensor.
 *
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
 *
 * Contact: Samu Onkalo <samu.p.onkalo@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/i2c/bhsfh.h>
#include <linux/regulator/consumer.h>
#include <linux/workqueue.h>
#include <linux/miscdevice.h>
#include <linux/delay.h>
#include "bhsfh.h"

#define BHSFH_ALS_CONTROL	0x80 /* ALS operation mode control */
#define BHSFH_PS_CONTROL	0x81 /* PS operation mode control */
#define BHSFH_I_LED		0x82 /* active LED and LED1, LED2 current */
#define BHSFH_I_LED3		0x83 /* LED3 current setting */
#define BHSFH_ALS_PS_MEAS	0x84 /* Forced mode trigger */
#define BHSFH_PS_MEAS_RATE	0x85 /* PS meas. rate at stand alone mode */
#define BHSFH_ALS_MEAS_RATE	0x86 /* ALS meas. rate at stand alone mode */
#define BHSFH_PART_ID		0x8a /* Part number and revision ID */
#define BHSFH_MANUFACT_ID	0x8b /* Manufacturerer ID */
#define BHSFH_ALS_DATA_0	0x8c /* ALS DATA low byte */
#define BHSFH_ALS_DATA_1	0x8d /* ALS DATA high byte */
#define BHSFH_ALS_PS_STATUS	0x8e /* Measurement data and int status */
#define BHSFH_PS_DATA_LED1	0x8f /* PS data from LED1 */
#define BHSFH_PS_DATA_LED2	0x90 /* PS data from LED2 */
#define BHSFH_PS_DATA_LED3	0x91 /* PS data from LED3 */
#define BHSFH_INTERRUPT		0x92 /* Interrupt setting */
#define BHSFH_PS_TH_LED1	0x93 /* PS interrupt threshold for LED1 */
#define BHSFH_PS_TH_LED2	0x94 /* PS interrupt threshold for LED2 */
#define BHSFH_PS_TH_LED3	0x95 /* PS interrupt threshold for LED3 */
#define BHSFH_ALS_TH_UP_0	0x96 /* ALS upper threshold low byte */
#define BHSFH_ALS_TH_UP_1	0x97 /* ALS upper threshold high byte */
#define BHSFH_ALS_TH_LOW_0	0x98 /* ALS lower threshold low byte */
#define BHSFH_ALS_TH_LOW_1	0x99 /* ALS lower threshold high byte */

/* MANUFACT_ID */
#define BHSFH_MANUFACT_ROHM	0x01
#define BHSFH_MANUFACT_OSRAM	0x03

/* PART_ID */
#define BHSFH_PART		0x90
#define BHSFH_PART_MASK	0xf0
#define BHSFH_REV_MASK		0x0f
#define BHSFH_REV_SHIFT	0
#define BHSFH_REV_0		0x00
#define BHSFH_REV_1		0x01

/* Operating modes for both */
#define BHSFH_STANDBY		0x00
#define BHSFH_FORCED		0x02
#define BHSFH_STANDALONE	0x03
#define BHSFH_SWRESET		(0x01 << 2)

#define BHSFH_PS_TRIG_MEAS	(1 << 0)
#define BHSFH_ALS_TRIG_MEAS	(1 << 1)

/* Interrupt control */
#define BHSFH_INT_OUTPUT_MODE	(1 << 3) /* 0 = latched */
#define BHSFH_INT_POLARITY	(1 << 2) /* 1 = active high */
#define BHSFH_INT_ALS_ENA	(1 << 1)
#define BHSFH_INT_PS_ENA	(1 << 0)

/* Interrupt status */
#define BHSFH_INT_LED1_DATA	(1 << 0)
#define BHSFH_INT_LED1_INT	(1 << 1)
#define BHSFH_INT_LED2_DATA	(1 << 2)
#define BHSFH_INT_LED2_INT	(1 << 3)
#define BHSFH_INT_LED3_DATA	(1 << 4)
#define BHSFH_INT_LED3_INT	(1 << 5)
#define BHSFH_INT_LEDS_INT	((1 << 1) | (1 << 3) | (1 << 5))
#define BHSFH_INT_ALS_DATA	(1 << 6)
#define BHSFH_INT_ALS_INT	(1 << 7)

#define BHSFH_DISABLE		0
#define BHSFH_ENABLE		1
#define BHSFH_PS_CHANNELS	1

 /* Following are milliseconds */
#define BHSFH_ALS_DEFAULT_RATE	200
#define BHSFH_PS_DEFAULT_RATE	20
#define BHSFH_PS_DEF_RATE_THRESH 200
#define BHSFH_STARTUP_DELAY	50
#define BHSFH_RESET_TIME	10

#define BHSFH_ALS_RANGE		65535
#define BHSFH_PS_RANGE		255
#define BHSFH_COEF_SCALER	1024
#define BHSFH_CALIB_SCALER	8192
#define BHSFH_ALS_NEUTRAL_CALIB_VALUE (1 * BHSFH_CALIB_SCALER)
#define BHSFH_ALS_DEF_THRES	1000
#define BHSFH_PS_DEF_THRES	70
#define BHSFH_PS_DEF_ABS_THRES	100
#define BHSFH_DEFAULT_PERSISTENCE 10
#define BHSFH_ALS_GA_SCALE	16384
#define BHSFH_ALS_CF_SCALE	2048 /* CF ChipFactor */
#define BHSFH_NEUTRAL_CF	BHSFH_ALS_CF_SCALE
#define BHSFH_ALS_CORR_SCALE    4096

#define PS_ABOVE_THRESHOLD	1
#define PS_BELOW_THRESHOLD	0

#define PS_IGNORE_ALS_LIMIT	200

static struct workqueue_struct *poll_wq; /* For OSRAM REV 0 */

static const char reg_vcc[] = "Vcc";
static const char reg_vleds[] = "Vleds";

/* Supported stand alone rates in ms from chip data sheet */
static s16 ps_rates[] = {10, 20, 30, 40, 70, 100, 200, 500, 1000, 2000};

/* Supported IR-led currents in mA */
static const u8 ps_curr_ma[] = {5, 10, 20, 50, 100, 150, 200};

/* Supported stand alone rates in ms from chip data sheet */
static s16 als_rates[] = {100, 200, 500, 1000, 2000};

/*
 * Following two functions converts raw values from HW to somehow normalized
 * values. Purpose is to compensate differences between different sensor
 * versions and variants so that result means about the same between
 * versions.
 */
static inline u8 bhsfh_psraw_to_adjusted(struct bhsfh_chip *chip, u8 psraw)
{
	u16 adjusted;
	adjusted = (u16)(((u32)(psraw + chip->ps_const) * chip->ps_coef) /
		BHSFH_COEF_SCALER);
	if (adjusted > BHSFH_PS_RANGE)
		adjusted = BHSFH_PS_RANGE;
	return adjusted;
}

static inline u8 bhsfh_psadjusted_to_raw(struct bhsfh_chip *chip, u8 ps)
{
	u16 raw;

	raw = ((u32)ps * BHSFH_COEF_SCALER) / chip->ps_coef;
	if (raw > chip->ps_const)
		raw = raw - chip->ps_const;
	else
		raw = 0;
	return raw;
}

static void bhsfh_reset(struct bhsfh_chip *chip)
{
	i2c_smbus_write_byte_data(chip->client, BHSFH_ALS_CONTROL,
				BHSFH_SWRESET);
	msleep(BHSFH_RESET_TIME);
}

static int bhsfh_interrupt_control(struct bhsfh_chip *chip,
				int als, int ps)
{
	/* Sorry, no ALS interrupts for broken chip */
	if (chip->broken_chip)
		als = 0;

	/* Set ALS interrupt mode, interrupt active low, latched */
	return i2c_smbus_write_byte_data(chip->client,
					BHSFH_INTERRUPT,
					(als << 1) | (ps << 0));
}

static inline int bhsfh_als_interrupt_control(struct bhsfh_chip *chip,
					int als)
{
	chip->int_mode_als = als;
	return bhsfh_interrupt_control(chip, als, chip->int_mode_ps);
}

static inline int bhsfh_ps_interrupt_control(struct bhsfh_chip *chip,
					int ps)
{
	chip->int_mode_ps = ps;
	return bhsfh_interrupt_control(chip, chip->int_mode_als, ps);
}

static int bhsfh_als_rate(struct bhsfh_chip *chip, int rate)
{
	int i;

	/* Check that requested rate is supported one */
	for (i = 0; i < ARRAY_SIZE(als_rates); i++)
		if (als_rates[i] == rate) {
			chip->als_rate = rate;
			break;
		}
	if (i == ARRAY_SIZE(als_rates))
		return -EINVAL;

	/* sysfs may call this when the chip is powered off */
	if (!chip->powered)
		return 0;

	/* Proper proximity response needs fastest als rate (100ms) */
	if (chip->ps_users)
		i = 0;

	return i2c_smbus_write_byte_data(chip->client,
					BHSFH_ALS_MEAS_RATE,
					i);
}

static int bhsfh_ps_rate(struct bhsfh_chip *chip, int mode)
{
	int rate;

	rate = (mode == PS_ABOVE_THRESHOLD) ?
		chip->ps_rate_threshold : chip->ps_rate;

	return i2c_smbus_write_byte_data(chip->client,
					BHSFH_PS_MEAS_RATE,
					rate);
}

/* InfraredLED is controlled by the chip during proximity scanning */
static int bhsfh_led_cfg(struct bhsfh_chip *chip, u8 ledcurr)
{
	if (ledcurr > chip->pdata->led_max_curr)
		return -EINVAL;

	chip->ps_led = ledcurr;

	/* sysfs may call this when the chip is powered off */
	if (!chip->powered)
		return 0;

	/* LED cfg, current for leds 1 and 2 */
	return i2c_smbus_write_byte_data(chip->client,
					BHSFH_I_LED,
					(BHSFH_LED1 << 6) |
					(BHSFH_LED_5mA << 3) |
					ledcurr);
}

/* For OSRAM REV 0 */
static int bhsfh_broken_chip_thresholds(struct bhsfh_chip *chip, u8 thres)
{
	u8 data[3];
	int i;

	data[0] = thres;
	data[1] = BHSFH_PS_RANGE;
	data[2] = BHSFH_PS_RANGE;
	i2c_smbus_write_i2c_block_data(chip->client,
				BHSFH_PS_TH_LED1,
				3,
				data);

	/*
	 * It seems that thresholds are not taken always in to use
	 * without readback.
	 */
	for (i = 0; i < 3; i++)
		data[i] = i2c_smbus_read_byte_data(chip->client,
						BHSFH_PS_TH_LED1 + i);
	return 0;
}

static int bhsfh_ps_set_threshold(struct bhsfh_chip *chip)
{
	u8 tmp = 0;

	/* sysfs may call this when the chip is powered off */
	if (!chip->powered)
		return 0;

	tmp = bhsfh_psadjusted_to_raw(chip, chip->ps_threshold);
	chip->ps_threshold_hw = tmp;

	if (chip->broken_chip)
		return bhsfh_broken_chip_thresholds(chip, tmp);
	return	i2c_smbus_write_byte_data(chip->client, BHSFH_PS_TH_LED1,
					tmp);
}

static inline u16 bhsfh_als_raw_to_adjusted(struct bhsfh_chip *chip, u16 raw)
{
	u32 lux;
	lux = ((u32)raw * chip->als_corr) / BHSFH_ALS_CORR_SCALE;
	return min(lux, (u32)BHSFH_ALS_RANGE);
}

static inline u16 bhsfh_als_adjusted_to_raw(struct bhsfh_chip *chip,
					u16 adjusted)
{
	return (u32)adjusted * BHSFH_ALS_CORR_SCALE / chip->als_corr;
}

static int bhsfh_als_update_thresholds(struct bhsfh_chip *chip,
					u16 threshold_hi, u16 threshold_lo)
{
	u8 data[4];
	int ret;

	/* sysfs may call this when the chip is powered off */
	if (!chip->powered)
		return 0;

	/*
	 * Compensate threshold values with the correction factors if not
	 * set to minimum or maximum.
	 */
	if ((threshold_hi != BHSFH_ALS_RANGE) && (threshold_hi != 0))
		threshold_hi = bhsfh_als_adjusted_to_raw(chip, threshold_hi);

	if ((threshold_lo != BHSFH_ALS_RANGE) && (threshold_lo != 0))
		threshold_lo = bhsfh_als_adjusted_to_raw(chip, threshold_lo);

	if ((chip->als_thres_hi_onchip == threshold_hi) &&
		(chip->als_thres_lo_onchip == threshold_lo))
		return 0;

	chip->als_thres_hi_onchip = threshold_hi;
	chip->als_thres_lo_onchip = threshold_lo;

	data[0] = threshold_hi;
	data[1] = threshold_hi >> 8;
	data[2] = threshold_lo;
	data[3] = threshold_lo >> 8;

	ret = i2c_smbus_write_i2c_block_data(chip->client,
					BHSFH_ALS_TH_UP_0,
					ARRAY_SIZE(data),
					data);
	return ret;
}

/*
 * Note! There is HW bug in i2c block read implementation.
 * This should be changed to block read if new HW version works ok
 * Or if ES samples require support, add runtime detection.
 */
static int bhsfh_als_get_result(struct bhsfh_chip *chip)
{
	u16 data;
	int ret;

	ret = i2c_smbus_read_byte_data(chip->client, BHSFH_ALS_DATA_0);
	if (ret < 0)
		return ret;

	data = ret & 0xff;
	ret = i2c_smbus_read_byte_data(chip->client, BHSFH_ALS_DATA_1);
	if (ret < 0)
		return ret;

	chip->als_data_raw = data | ((ret & 0xff) << 8);
	return 0;
}

static u32 bhsfh_set_corr_value(struct bhsfh_chip *chip)
{
	u32 tmp;
	/* Impact of glass attenuation correction */
	tmp = (BHSFH_ALS_CORR_SCALE * chip->als_ga) / BHSFH_ALS_GA_SCALE;
	/* Impact of chip factor correction */
	tmp = (tmp * chip->als_cf) / BHSFH_ALS_CF_SCALE;
	/* Impact Device specific calibration correction */
	tmp = (tmp * chip->als_calib) / BHSFH_CALIB_SCALER;
	return tmp;
}

static int bhsfh_als_read_result(struct bhsfh_chip *chip)
{

	int ret;

	chip->als_data = bhsfh_als_raw_to_adjusted(chip, chip->als_data_raw);
	chip->als_offset += sizeof(struct bhsfh_als);

	ret = bhsfh_als_update_thresholds(chip,
					chip->als_threshold_hi,
					chip->als_threshold_lo);
	return ret;
}

static int bhsfh_chip_on(struct bhsfh_chip *chip)
{
	int ret = 0;

	if (!chip->als_users && !chip->ps_users) {
		ret = regulator_bulk_enable(ARRAY_SIZE(chip->regs),
					chip->regs);
		if (ret < 0)
			return ret;
		chip->powered = true;
		msleep(BHSFH_STARTUP_DELAY);

		bhsfh_reset(chip);
		chip->als_data_raw = 0;

		/* Als is started always since proximity needs als results */
		ret = i2c_smbus_write_byte_data(chip->client,
					BHSFH_ALS_CONTROL, BHSFH_STANDALONE);
	}

	/* Assume reset defaults */
	chip->als_thres_hi_onchip = BHSFH_ALS_RANGE;
	chip->als_thres_lo_onchip = 0;

	if (ret < 0)
		regulator_bulk_disable(ARRAY_SIZE(chip->regs), chip->regs);

	return ret;
}

static void bhsfh_chip_off(struct bhsfh_chip *chip)
{
	if (!chip->als_users && !chip->ps_users) {
		bhsfh_interrupt_control(chip, BHSFH_DISABLE,
					BHSFH_DISABLE);
		/* Proximity is allready in standby here */
		i2c_smbus_write_byte_data(chip->client,
					BHSFH_ALS_CONTROL, BHSFH_STANDBY);
		chip->powered = false;
		regulator_bulk_disable(ARRAY_SIZE(chip->regs), chip->regs);
	}
}

int bhsfh_als_on(struct bhsfh_chip *chip)
{
	int ret =  bhsfh_chip_on(chip);
	if (!ret) {
		chip->als_users++;
		ret |= bhsfh_als_rate(chip, chip->als_rate);
		ret |= bhsfh_als_interrupt_control(chip, BHSFH_ENABLE);

		/* This causes interrupt after the next measurement cycle */
		bhsfh_als_update_thresholds(chip, BHSFH_ALS_DEF_THRES,
						BHSFH_ALS_DEF_THRES);
		/*
		 * This prevents user updates to threshold until the
		 * first interrupt has occured. Driver assumes that
		 * threshold is not changed between open and the first
		 * interrupt to always get a fresh result.
		 */
		chip->als_wait_interrupt = true;
	}
	return ret;
}

void bhsfh_als_off(struct bhsfh_chip *chip)
{
	if (!--chip->als_users)
		bhsfh_als_interrupt_control(chip, BHSFH_DISABLE);
	bhsfh_chip_off(chip);
}

int bhsfh_proximity_on(struct bhsfh_chip *chip)
{
	int ret;

	ret = bhsfh_chip_on(chip);
	if (!ret) {
		chip->ps_users++;
		chip->ps_force_update = true; /* Report in every case */

		bhsfh_als_rate(chip, chip->als_rate);
		bhsfh_ps_set_threshold(chip);
		bhsfh_led_cfg(chip, chip->ps_led);
		bhsfh_ps_rate(chip, PS_BELOW_THRESHOLD);
		bhsfh_ps_interrupt_control(chip, BHSFH_ENABLE);
		i2c_smbus_write_byte_data(chip->client,
					BHSFH_PS_CONTROL, BHSFH_STANDALONE);
	}
	return ret;
}

void bhsfh_proximity_off(struct bhsfh_chip *chip)
{
	if (!--chip->ps_users) {
		/* Restore normal als rate when PS goes off */
		bhsfh_als_rate(chip, chip->als_rate);
		bhsfh_ps_interrupt_control(chip, BHSFH_DISABLE);
		i2c_smbus_write_byte_data(chip->client,
					BHSFH_PS_CONTROL, BHSFH_STANDBY);
	}
	bhsfh_chip_off(chip);
}

static int bhsfh_ps_rates(struct bhsfh_chip *chip, int rate,
			int rate_threshold)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ps_rates); i++) {
		if (ps_rates[i] == rate) {
			chip->ps_rate = i;
			break;
		}
	}
	if (i == ARRAY_SIZE(ps_rates))
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(ps_rates); i++) {
		if (ps_rates[i] == rate_threshold) {
			chip->ps_rate_threshold = i;
			return 0;
		}
	}
	return -EINVAL;
}

static int bhsfh_ps_read_result(struct bhsfh_chip *chip)
{
	int ret;
	u16 als_data;
	bool above;
	u8 mode;

	als_data = chip->als_data_raw;

	ret = i2c_smbus_read_byte_data(chip->client, BHSFH_PS_DATA_LED1);
	if (ret < 0)
		goto out;

	if (ret > chip->ps_threshold_hw)
		above = true;
	else
		above = false;

	chip->ps_data_raw = ret;

	/*
	 * when ALS levels goes above limit, proximity result may be
	 * false proximity. Thus ignore the result. With real proximity
	 * there is a shadow causing low als levels.
	 */
	if (als_data > PS_IGNORE_ALS_LIMIT)
		ret = 0;

	chip->ps_data = bhsfh_psraw_to_adjusted(chip, ret);

	/* Strong proximity level or force mode requires immediate response */
	if ((chip->ps_data >= chip->ps_abs_thres) || (chip->ps_force_update))
		chip->ps_persistence_counter = chip->ps_persistence;

	chip->ps_force_update = false;

	if (likely(above)) {
		if (chip->ps_persistence_counter < chip->ps_persistence) {
			chip->ps_persistence_counter++;
			ret = -ENODATA;
		} else {
			mode = PS_ABOVE_THRESHOLD;
			ret = 0;
		}
	} else {
		chip->ps_persistence_counter = 0;
		mode = PS_BELOW_THRESHOLD;
		chip->ps_data = 0;
		ret = 0;
	}
	if (ret == 0) {
		bhsfh_ps_rate(chip, mode);
		chip->ps_offset += sizeof(struct bhsfh_ps);
		wake_up_interruptible(chip->ps_dev_wait);
	}
out:
	return ret;
}

/* For OSRAM REV 0 */
void bhsfh_handle_buggy_version(struct bhsfh_chip *chip)
{
	/*
	 * Schedule restore work to happen some time after the next
	 * measurement. Chip internal clock runs too slowly. That is why
	 * rate is multiplied with 1.5. And some safety constant
	 * (not really know for sure what is the internal clock)
	 */
	cancel_delayed_work_sync(&chip->work);
	if (chip->als_users || chip->ps_users) {
		int rate = ps_rates[chip->ps_rate_threshold];
		queue_delayed_work(poll_wq, &chip->work,
				msecs_to_jiffies(rate * 3 / 2 + 20));
	}

	/* Thresholds must be turned off to get interrupt event low */
	if (chip->ps_users)
		bhsfh_broken_chip_thresholds(chip, BHSFH_PS_RANGE);
}

static int bhsfh_detect(struct bhsfh_chip *chip)
{
	struct i2c_client *client = chip->client;
	s32 ret;
	u8 manu;
	u8 part;

	ret = i2c_smbus_read_byte_data(client, BHSFH_MANUFACT_ID);
	if (ret < 0)
		goto error;

	manu = (u8)ret;

	ret = i2c_smbus_read_byte_data(client, BHSFH_PART_ID);
	if (ret < 0)
		goto error;
	part = (u8)ret;
	chip->revision = (part & BHSFH_REV_MASK) >> BHSFH_REV_SHIFT;
	chip->ps_coef = BHSFH_COEF_SCALER;
	chip->ps_const = 0;
	chip->als_cf = BHSFH_NEUTRAL_CF;

	if ((manu == BHSFH_MANUFACT_ROHM) &&
	    ((part & BHSFH_PART_MASK)	== BHSFH_PART)) {
		snprintf(chip->chipname, sizeof(chip->chipname), "BH1770GLC");
		return 0;
	}

	if ((manu == BHSFH_MANUFACT_OSRAM) &&
	    ((part & BHSFH_PART_MASK)	== BHSFH_PART)) {
		snprintf(chip->chipname, sizeof(chip->chipname), "SFH7770");
		if ((part & BHSFH_REV_MASK) == BHSFH_REV_0)
			chip->broken_chip = 1;
		/* Values selected by comparing different versions */
		chip->ps_coef = 819; /* 0.8 * BHSFH_COEF_SCALER */
		chip->ps_const = 40;
		return 0;
	}

	ret = -ENODEV;
error:
	dev_dbg(&client->dev, "BHSFH or SFH7770 not found\n");

	return ret;
}

/*
 * This work is re-scheduled at every proximity interrupt.
 * If this work is running, it means that there hasn't been any
 * proximity interrupt in time. Situation is handled as no-proximity.
 * It would be nice to have low-threshold interrupt or interrupt
 * when measurement and hi-threshold are both 0. But neither of those exists.
 * This is a workaroud for missing HW feature.
 */

static void bhsfh_ps_work(struct work_struct *work)
{
	struct bhsfh_chip *chip =
		container_of(work, struct bhsfh_chip, ps_work.work);

	mutex_lock(&chip->mutex);
	bhsfh_ps_read_result(chip);
	mutex_unlock(&chip->mutex);
}

/* This is threaded irq handler */
static irqreturn_t bhsfh_irq(int irq, void *data)
{
	struct bhsfh_chip *chip = data;
	int status;
	int rate = 0;

	mutex_lock(&chip->mutex);
	status = i2c_smbus_read_byte_data(chip->client, BHSFH_ALS_PS_STATUS);

	/* Acknowledge interrupt by reading this register */
	i2c_smbus_read_byte_data(chip->client, BHSFH_INTERRUPT);

	/* Refresh als data only when new result is available */
	if (status & BHSFH_INT_ALS_DATA)
		bhsfh_als_get_result(chip);

	/* Disable interrupt logic */
	if (!chip->broken_chip)
		i2c_smbus_write_byte_data(chip->client, BHSFH_INTERRUPT,
					(0 << 1) | (0 << 0));

	if (!chip->broken_chip)
		if (chip->als_users && (status & BHSFH_INT_ALS_INT)) {
			chip->als_wait_interrupt = false;
			bhsfh_als_read_result(chip);
			wake_up_interruptible(chip->als_dev_wait);
		}

	if (chip->int_mode_ps)
		if (status & BHSFH_INT_LEDS_INT) {
			rate = ps_rates[chip->ps_rate_threshold];
			bhsfh_ps_read_result(chip);
		}

	/* Re-enable interrupt logic */
	if (!chip->broken_chip)
		i2c_smbus_write_byte_data(chip->client, BHSFH_INTERRUPT,
					(chip->int_mode_als << 1) |
					(chip->int_mode_ps << 0));
	mutex_unlock(&chip->mutex);

	/*
	 * Can't cancel work while keeping mutex since the work uses the
	 * same mutex.
	 */
	if (rate) {
		/* Broken chip is slow */
		if (chip->broken_chip)
			rate = rate * 3;
		/*
		 * Simulate missing no-proximity interrupt 50ms after the
		 * next expected interrupt time. 50ms because of timing
		 * inaccuracy.
		 */
		cancel_delayed_work_sync(&chip->ps_work);
		schedule_delayed_work(&chip->ps_work,
				msecs_to_jiffies(rate + 50));
	}

	if (chip->broken_chip)
		bhsfh_handle_buggy_version(chip);

	return IRQ_HANDLED;
}

/* For OSRAM REV 0 */
static void bhsfh_work(struct work_struct *work)
{
	struct bhsfh_chip *chip =
		container_of(work, struct bhsfh_chip, work.work);
	int status;
	u8 data[4];

	mutex_lock(&chip->mutex);
	/* For broken chips, handle ALS stuff here */
	status = i2c_smbus_read_byte_data(chip->client, BHSFH_ALS_PS_STATUS);
	bhsfh_als_get_result(chip);
	if (chip->als_users) {
		if (status & BHSFH_INT_ALS_INT) {
			bhsfh_als_read_result(chip);
			wake_up_interruptible(chip->als_dev_wait);
		}
		queue_delayed_work(poll_wq, &chip->work,
				msecs_to_jiffies(1000));
	}
	/*
	 * This is experimental solution:
	 * To acknowledge Proximity interrupt it seems that status register
	 * read must be started from previous registers.
	 * Before that operation, thresholds must be
	 * disabled over one measurement period.
	 * We really don't need this information, but we need to
	 * get chip somehow inactivate the interrupt request.
	 */
	i2c_smbus_read_i2c_block_data(chip->client,
				BHSFH_PS_DATA_LED3,
				3,
				data);

	/* Back to interrupt based method */
	if (chip->ps_users)
		bhsfh_broken_chip_thresholds(chip, chip->ps_threshold_hw);
	mutex_unlock(&chip->mutex);
}

static ssize_t bhsfh_get_ps_rate(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct bhsfh_chip *chip =  dev_get_drvdata(dev);
	return sprintf(buf, "%d %d\n", ps_rates[chip->ps_rate],
		ps_rates[chip->ps_rate_threshold]);
}

static ssize_t bhsfh_set_ps_rate(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct bhsfh_chip *chip =  dev_get_drvdata(dev);
	int rate = 0, rate_threshold = 0;
	int ret;

	ret = sscanf(buf, "%d %d", &rate, &rate_threshold);
	if (ret < 0)
		return ret;

	if (ret == 0)
		return count;

	/* Second value is optional */
	if (ret == 1)
		rate_threshold = ps_rates[chip->ps_rate_threshold];

	mutex_lock(&chip->mutex);
	ret = bhsfh_ps_rates(chip, rate, rate_threshold);
	mutex_unlock(&chip->mutex);
	if (ret < 0)
		return ret;
	return count;
}

static ssize_t bhsfh_get_ps_thres(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct bhsfh_chip *chip =  dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", chip->ps_threshold);
}

static ssize_t bhsfh_set_ps_thres(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct bhsfh_chip *chip =  dev_get_drvdata(dev);
	unsigned long value;
	int ret;

	if (strict_strtoul(buf, 0, &value))
		return -EINVAL;
	if (value > BHSFH_PS_RANGE)
		return -EINVAL;

	mutex_lock(&chip->mutex);
	chip->ps_threshold = value;
	ret = bhsfh_ps_set_threshold(chip);
	mutex_unlock(&chip->mutex);
	if (ret < 0)
		return ret;
	return count;
}

static ssize_t bhsfh_ps_led_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct bhsfh_chip *chip =  dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", ps_curr_ma[chip->ps_led]);
}

static ssize_t bhsfh_ps_led_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct bhsfh_chip *chip =  dev_get_drvdata(dev);
	unsigned long value;
	u8 led_curr;
	int ret;
	int j;

	if (strict_strtoul(buf, 0, &value))
		return -EINVAL;

	led_curr = BHSFH_LED_5mA;

	for (j = 0; j < ARRAY_SIZE(ps_curr_ma); j++)
		if (value == ps_curr_ma[j]) {
			led_curr = j;
			break;
		}
	if (j == ARRAY_SIZE(ps_curr_ma))
		return -EINVAL;

	mutex_lock(&chip->mutex);
	ret = bhsfh_led_cfg(chip, led_curr);
	mutex_unlock(&chip->mutex);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t bhsfh_ps_persistence_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct bhsfh_chip *chip = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%u\n", chip->ps_persistence);
}

static ssize_t bhsfh_ps_persistence_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t len)
{
	struct bhsfh_chip *chip = dev_get_drvdata(dev);
	unsigned long value;

	if (strict_strtoul(buf, 0, &value))
		return -EINVAL;

	if (value > 255)
		return -EINVAL;

	chip->ps_persistence = value;

	return len;
}

static ssize_t bhsfh_ps_abs_thres_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct bhsfh_chip *chip = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%u\n", chip->ps_abs_thres);
}

static ssize_t bhsfh_ps_abs_thres_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t len)
{
	struct bhsfh_chip *chip = dev_get_drvdata(dev);
	unsigned long value;

	if (strict_strtoul(buf, 0, &value))
		return -EINVAL;

	if (value > 255)
		return -EINVAL;

	chip->ps_abs_thres = value;

	return len;
}

static ssize_t bhsfh_chip_id_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct bhsfh_chip *chip =  dev_get_drvdata(dev);
	return sprintf(buf, "%s rev %d\n", chip->chipname, chip->revision);
}

static ssize_t bhsfh_als_calib_format_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", BHSFH_CALIB_SCALER);
}

static ssize_t bhsfh_als_calib_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct bhsfh_chip *chip = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%u\n", chip->als_calib);
}

static ssize_t bhsfh_als_calib_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len)
{
	struct bhsfh_chip *chip = dev_get_drvdata(dev);
	unsigned long value;
	u32 old_calib;
	u32 new_corr;

	if (strict_strtoul(buf, 0, &value))
		return -EINVAL;

	mutex_lock(&chip->mutex);
	old_calib = chip->als_calib;
	chip->als_calib = value;
	new_corr = bhsfh_set_corr_value(chip);
	if (new_corr == 0) {
		chip->als_calib = old_calib;
		mutex_unlock(&chip->mutex);
		return -EINVAL;
	}
	chip->als_corr = new_corr;
	mutex_unlock(&chip->mutex);

	return len;
}

static ssize_t bhsfh_get_als_rate(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct bhsfh_chip *chip =  dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", chip->als_rate);
}

static ssize_t bhsfh_set_als_rate(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct bhsfh_chip *chip =  dev_get_drvdata(dev);
	unsigned long rate;
	int ret;

	if (strict_strtoul(buf, 0, &rate))
		return -EINVAL;

	mutex_lock(&chip->mutex);
	ret = bhsfh_als_rate(chip, rate);
	mutex_unlock(&chip->mutex);

	if (ret < 0)
		return ret;

	return count;
}

static ssize_t bhsfh_get_als_thres_range(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct bhsfh_chip *chip =  dev_get_drvdata(dev);
	return sprintf(buf, "%d %d\n", chip->als_threshold_lo,
		chip->als_threshold_hi);
}

static ssize_t bhsfh_set_als_thres_range(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len)
{
	struct bhsfh_chip *chip =  dev_get_drvdata(dev);
	int thres_lo;
	int thres_hi;
	int ret;

	ret = sscanf(buf, "%d %d", &thres_lo, &thres_hi);
	if (ret < 0)
		return ret;

	if (ret != 2)
		return -EINVAL;

	if (thres_lo > thres_hi)
		return -EINVAL;

	if (thres_hi > BHSFH_ALS_RANGE)
		return -EINVAL;

	if (thres_lo < 0)
		return -EINVAL;

	mutex_lock(&chip->mutex);
	chip->als_threshold_lo = thres_lo;
	chip->als_threshold_hi = thres_hi;
	/*
	 * Don't update values in HW if we are still waiting for
	 * first interrupt to come after device handle open call.
	 */
	if (!chip->als_wait_interrupt)
		ret = bhsfh_als_update_thresholds(chip, thres_hi, thres_lo);
	mutex_unlock(&chip->mutex);

	if (ret < 0)
		return ret;
	return len;
}

static DEVICE_ATTR(ps_rate, S_IRUGO | S_IWUSR, bhsfh_get_ps_rate,
					       bhsfh_set_ps_rate);
static DEVICE_ATTR(ps_threshold, S_IRUGO | S_IWUSR, bhsfh_get_ps_thres,
						    bhsfh_set_ps_thres);
static DEVICE_ATTR(ps_led, S_IRUGO | S_IWUSR, bhsfh_ps_led_show,
					      bhsfh_ps_led_store);
static DEVICE_ATTR(ps_persistence, S_IRUGO | S_IWUSR,
					      bhsfh_ps_persistence_show,
					      bhsfh_ps_persistence_store);
static DEVICE_ATTR(ps_abs_thres, S_IRUGO | S_IWUSR,
					      bhsfh_ps_abs_thres_show,
					      bhsfh_ps_abs_thres_store);
static DEVICE_ATTR(chip_id, S_IRUGO, bhsfh_chip_id_show, NULL);

static DEVICE_ATTR(als_calib_format, S_IRUGO, bhsfh_als_calib_format_show,
					      NULL);
static DEVICE_ATTR(als_calib, S_IRUGO | S_IWUSR, bhsfh_als_calib_show,
						 bhsfh_als_calib_store);
static DEVICE_ATTR(als_rate, S_IRUGO | S_IWUSR, bhsfh_get_als_rate,
						bhsfh_set_als_rate);
static DEVICE_ATTR(als_thres_range, S_IRUGO | S_IWUSR,
					      bhsfh_get_als_thres_range,
					      bhsfh_set_als_thres_range);

static struct attribute *sysfs_attrs[] = {
	&dev_attr_als_calib_format.attr,
	&dev_attr_als_calib.attr,
	&dev_attr_als_rate.attr,
	&dev_attr_als_thres_range.attr,
	&dev_attr_ps_rate.attr,
	&dev_attr_ps_threshold.attr,
	&dev_attr_ps_led.attr,
	&dev_attr_ps_persistence.attr,
	&dev_attr_ps_abs_thres.attr,
	&dev_attr_chip_id.attr,
	NULL
};

static struct attribute_group bhsfh_attribute_group = {
	.attrs = sysfs_attrs
};

static int __devinit bhsfh_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct bhsfh_chip *chip;
	int err;

	chip = kzalloc(sizeof *chip, GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	i2c_set_clientdata(client, chip);
	chip->client  = client;

	mutex_init(&chip->mutex);
	INIT_DELAYED_WORK(&chip->ps_work, bhsfh_ps_work);

	/* For OSRAM REV 0 */
	INIT_DELAYED_WORK(&chip->work, bhsfh_work);
	poll_wq = create_singlethread_workqueue("bhsfh_buggy");
	if (!poll_wq) {
		dev_err(&client->dev, "Cannot get workqueue\n");
		err = -ENOMEM;
		goto fail_nowq;
	}

	/*
	 * Platform data contains led configuration and safety limits.
	 * Too strong current can damage HW permanently.
	 * Platform data filled with zeros causes minimum current.
	 */
	if (client->dev.platform_data == NULL) {
		dev_err(&client->dev, "platform data is mandatory\n");
		err = -EINVAL;
		goto fail1;
	}

	chip->pdata	= client->dev.platform_data;
	chip->als_calib = BHSFH_ALS_NEUTRAL_CALIB_VALUE;
	chip->als_rate = BHSFH_ALS_DEFAULT_RATE;
	chip->als_threshold_lo = BHSFH_ALS_DEF_THRES;
	chip->als_threshold_hi = BHSFH_ALS_DEF_THRES;

	if (chip->pdata->glass_attenuation == 0)
		chip->als_ga = BHFSH_NEUTRAL_GA;
	else
		chip->als_ga = chip->pdata->glass_attenuation;

	chip->ps_threshold = BHSFH_PS_DEF_THRES;
	chip->ps_led = chip->pdata->led_def_curr;
	chip->ps_abs_thres = BHSFH_PS_DEF_ABS_THRES;
	chip->ps_persistence = BHSFH_DEFAULT_PERSISTENCE;

	bhsfh_ps_rates(chip, BHSFH_PS_DEFAULT_RATE,
			BHSFH_PS_DEF_RATE_THRESH);

	chip->regs[0].supply = reg_vcc;
	chip->regs[1].supply = reg_vleds;

	err = regulator_bulk_get(&client->dev,
				 ARRAY_SIZE(chip->regs), chip->regs);
	if (err < 0) {
		dev_err(&client->dev, "Cannot get regulators\n");
		goto fail1;
	}

	err = regulator_bulk_enable(ARRAY_SIZE(chip->regs), chip->regs);
	if (err < 0) {
		dev_err(&client->dev, "Cannot enable regulators\n");
		goto fail2;
	}

	msleep(BHSFH_STARTUP_DELAY);

	err = bhsfh_detect(chip);
	if (!err)
		bhsfh_reset(chip);

	regulator_bulk_disable(ARRAY_SIZE(chip->regs), chip->regs);
	if (err < 0)
		goto fail2;

	chip->als_corr = bhsfh_set_corr_value(chip);
	if (chip->als_corr == 0) {
		dev_err(&client->dev, "Improper correction values\n");
		err = -EINVAL;
		goto fail2;
	}

	if (chip->pdata->setup_resources) {
		err = chip->pdata->setup_resources();
		if (err) {
			err = -EINVAL;
			goto fail2;
		}
	}

	err = bhsfh_als_init(chip);
	if (err < 0)
		goto fail3;

	err = bhsfh_ps_init(chip);
	if (err < 0)
		goto fail4;

	err = sysfs_create_group(&chip->client->dev.kobj,
				&bhsfh_attribute_group);
	if (err < 0) {
		dev_err(&chip->client->dev, "Sysfs registration failed\n");
		goto fail5;
	}

	err = request_threaded_irq(client->irq, NULL,
				bhsfh_irq,
				IRQF_TRIGGER_FALLING | IRQF_ONESHOT |
				(chip->broken_chip ? 0  : IRQF_TRIGGER_LOW),
				"bhsfh", chip);
	if (err) {
		dev_err(&client->dev, "could not get IRQ %d\n",
			client->irq);
		goto fail6;
	}
	return err;
fail6:
	sysfs_remove_group(&chip->client->dev.kobj,
			&bhsfh_attribute_group);
fail5:
	bhsfh_ps_destroy(chip);
fail4:
	bhsfh_als_destroy(chip);
fail3:
	if (chip->pdata->release_resources)
		chip->pdata->release_resources();
fail2:
	regulator_bulk_free(ARRAY_SIZE(chip->regs), chip->regs);
fail1:
	destroy_workqueue(poll_wq);
fail_nowq:
	kfree(chip);
	return err;
}

static int __devexit bhsfh_remove(struct i2c_client *client)
{
	struct bhsfh_chip *chip = i2c_get_clientdata(client);

	free_irq(client->irq, chip);

	sysfs_remove_group(&chip->client->dev.kobj,
			&bhsfh_attribute_group);

	if (chip->pdata->release_resources)
		chip->pdata->release_resources();

	cancel_delayed_work_sync(&chip->work);
	destroy_workqueue(poll_wq);

	bhsfh_als_destroy(chip);
	cancel_delayed_work_sync(&chip->ps_work);
	bhsfh_ps_destroy(chip);
	regulator_bulk_free(ARRAY_SIZE(chip->regs), chip->regs);
	kfree(chip);
	return 0;
}

#ifdef CONFIG_PM
static int bhsfh_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct bhsfh_chip *chip = i2c_get_clientdata(client);

	mutex_lock(&chip->mutex);
	if (chip->als_users) {
		i2c_smbus_write_byte_data(chip->client,
			BHSFH_ALS_CONTROL, BHSFH_STANDBY);
		i2c_smbus_write_byte_data(chip->client,
					BHSFH_PS_CONTROL, BHSFH_STANDBY);
	}
	mutex_unlock(&chip->mutex);
	return 0;
}

static int bhsfh_resume(struct i2c_client *client)
{
	struct bhsfh_chip *chip = i2c_get_clientdata(client);

	mutex_lock(&chip->mutex);
	if (chip->als_users)
		i2c_smbus_write_byte_data(chip->client,
					BHSFH_ALS_CONTROL, BHSFH_STANDALONE);
	if (chip->ps_users)
		i2c_smbus_write_byte_data(chip->client,
					BHSFH_PS_CONTROL, BHSFH_STANDALONE);
	mutex_unlock(&chip->mutex);
	return 0;
}

static void bhsfh_shutdown(struct i2c_client *client)
{
	struct bhsfh_chip *chip = i2c_get_clientdata(client);

	i2c_smbus_write_byte_data(chip->client,
				BHSFH_ALS_CONTROL, BHSFH_STANDBY);
	i2c_smbus_write_byte_data(chip->client,
				BHSFH_PS_CONTROL, BHSFH_STANDBY);
}

#else
#define bhsfh_suspend  NULL
#define bhsfh_shutdown NULL
#define bhsfh_resume   NULL
#endif

static const struct i2c_device_id bhsfh_id[] = {
	{"bh1770glc", 0 },
	{"sfh7770", 0 },
	{}
};

MODULE_DEVICE_TABLE(i2c, bhsfh_id);

static struct i2c_driver bhsfh_driver = {
	.driver	 = {
		.name	= "bhsfh",
		.owner	= THIS_MODULE,
	},
	.suspend  = bhsfh_suspend,
	.shutdown = bhsfh_shutdown,
	.resume	  = bhsfh_resume,
	.probe	  = bhsfh_probe,
	.remove	  = __devexit_p(bhsfh_remove),
	.id_table = bhsfh_id,
};

static int __init bhsfh_init(void)
{
	return i2c_add_driver(&bhsfh_driver);
}

static void __exit bhsfh_exit(void)
{
	i2c_del_driver(&bhsfh_driver);
}

MODULE_DESCRIPTION("BH1770GLC / SFH7770 combined ALS and proximity sensor");
MODULE_AUTHOR("Samu Onkalo, Nokia Corporation");
MODULE_LICENSE("GPL v2");

module_init(bhsfh_init);
module_exit(bhsfh_exit);
