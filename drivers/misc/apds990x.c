/*
 * This file is part of the APDS990x sensor driver.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
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
#include <linux/regulator/consumer.h>
#include <linux/miscdevice.h>
#include <linux/poll.h>
#include <linux/delay.h>
#include <linux/apds990x.h>

/* Register map */
#define APDS990X_ENABLE	 0x00 /* Enable of states and interrupts */
#define APDS990X_ATIME	 0x01 /* ALS ADC time  */
#define APDS990X_PTIME	 0x02 /* Proximity ADC time  */
#define APDS990X_WTIME	 0x03 /* Wait time  */
#define APDS990X_AILTL	 0x04 /* ALS interrupt low threshold low byte */
#define APDS990X_AILTH	 0x05 /* ALS interrupt low threshold hi byte */
#define APDS990X_AIHTL	 0x06 /* ALS interrupt hi threshold low byte */
#define APDS990X_AIHTH	 0x07 /* ALS interrupt hi threshold hi byte */
#define APDS990X_PILTL	 0x08 /* Proximity interrupt low threshold low byte */
#define APDS990X_PILTH	 0x09 /* Proximity interrupt low threshold hi byte */
#define APDS990X_PIHTL	 0x0a /* Proximity interrupt hi threshold low byte */
#define APDS990X_PIHTH	 0x0b /* Proximity interrupt hi threshold hi byte */
#define APDS990X_PERS	 0x0c /* Interrupt persistence filters */
#define APDS990X_CONFIG	 0x0d /* Configuration */
#define APDS990X_PPCOUNT 0x0e /* Proximity pulse count */
#define APDS990X_CONTROL 0x0f /* Gain control register */
#define APDS990X_REV	 0x11 /* Revision Number */
#define APDS990X_ID	 0x12 /* Device ID */
#define APDS990X_STATUS	 0x13 /* Device status */
#define APDS990X_CDATAL	 0x14 /* Clear ADC low data register */
#define APDS990X_CDATAH	 0x15 /* Clear ADC high data register */
#define APDS990X_IRDATAL 0x16 /* IR ADC low data register */
#define APDS990X_IRDATAH 0x17 /* IR ADC high data register */
#define APDS990X_PDATAL	 0x18 /* Proximity ADC low data register */
#define APDS990X_PDATAH	 0x19 /* Proximity ADC high data register */

/* Control */
#define APDS990X_MAX_AGAIN	3

/* Enable register */
#define APDS990X_EN_PIEN	(0x1 << 5)
#define APDS990X_EN_AIEN	(0x1 << 4)
#define APDS990X_EN_WEN		(0x1 << 3)
#define APDS990X_EN_PEN		(0x1 << 2)
#define APDS990X_EN_AEN		(0x1 << 1)
#define APDS990X_EN_PON		(0x1 << 0)
#define APDS990X_EN_DISABLE_ALL 0

/* Status register */
#define APDS990X_ST_PINT	(0x1 << 5)
#define APDS990X_ST_AINT	(0x1 << 4)

/* I2C access types */
#define APDS990x_CMD_TYPE_MASK	(0x03 << 5)
#define APDS990x_CMD_TYPE_RB	(0x00 << 5) /* Repeated byte */
#define APDS990x_CMD_TYPE_INC	(0x01 << 5) /* Auto increment */
#define APDS990x_CMD_TYPE_SPE	(0x03 << 5) /* Special function */

#define APDS990x_ADDR_SHIFT	0
#define APDS990x_CMD		0x80

/* Interrupt ack commands */
#define APDS990X_INT_ACK_ALS	0x6
#define APDS990X_INT_ACK_PS	0x5
#define APDS990X_INT_ACK_BOTH	0x7

/* ptime */
#define APDS990X_PTIME_DEFAULT	0xff /* Recommended conversion time 2.7ms*/

/* wtime */
#define APDS990X_WTIME_DEFAULT	0xee /* ~50ms wait time */

#define APDS990X_TIME_TO_ADC	1024 /* One timetick as ADC count value */

/* ppcount */
#define APDS990X_PPCOUNT_DEFAULT 5

/* Persistence */
#define APDS990X_APERS_SHIFT	0
#define APDS990X_PPERS_SHIFT	4

/* Supported ID:s */
#define APDS990X_ID_0		0
#define APDS990X_ID_4		4
#define APDS990X_ID_29		0x29

/* pgain and pdiode settings */
#define APDS_PGAIN_1X	       0x0
#define APDS_PDIODE_IR	       0x2

/* Reverse chip factors for threshold calculation */
struct reverse_factors {
	u32 afactor;
	int cf1;
	int irf1;
	int cf2;
	int irf2;
};

struct apds990x_chip {
	struct apds990x_platform_data	*pdata;
	struct i2c_client		*client;
	struct miscdevice		miscdev;
	struct mutex			mutex; /* avoid parallel access */
	struct regulator_bulk_data	regs[2];

	wait_queue_head_t		misc_wait;

	bool	ps_en;
	bool	ps_onoff;	/* Proximity on off behaviour enabled */
	bool	manual;		/* Automatic or manual control mode */
	bool	als_wait_fresh_res;
	int	users;

	/* Chip parameters */
	struct	apds990x_chip_factors	cf;
	struct	reverse_factors		rcf;
	u16	atime;		/* als integration time */
	u16	atime_tot;	/* als reporting period */
	u16	fsm_time;	/* state machine loop round time */
	u16	a_max_result;	/* Max possible ADC value with current atime */
	u8	again_meas;	/* Gain used in last measurement */
	u8	again_next;	/* Next calculated gain */
	u8	pgain;
	u8	pdiode;
	u8	pdrive;
	u8	als_persistence;
	u8	ps_persistence;

	loff_t	offset;

	u32	als_lux_raw;
	u16	als_clear;
	u16	als_ir;
	u16	als_calib;
	u32	als_thres_hi;
	u32	als_thres_lo;

	u32	ps_thres;
	u16	ps_data;
	u16	ps_calib;

	u8	status;

	char	chipname[10];
	u8	revision;
};

#define APDS_RESULT_ALS			0x1
#define APDS_RESULT_PS			0x2

#define APDS_CALIB_SCALER		8192
#define APDS_ALS_NEUTRAL_CALIB_VALUE	(1 * APDS_CALIB_SCALER)
#define APDS_PS_NEUTRAL_CALIB_VALUE	(1 * APDS_CALIB_SCALER)

#define APDS_PS_DEF_THRES		600
#define APDS_PS_HYSTERESIS		50
#define APDS_ALS_DEF_THRES_HI		60001
#define APDS_ALS_DEF_THRES_LO		60000
#define APDS_DEFAULT_PS_PERS		1
#define APDS_DEFAULT_ALS_PERS		3

#define APDS_STARTUP_DELAY		10 /* ms */

#define APDS_RANGE			65535 /* Same range for both */
#define APDS_ALS_GAIN_LO_LIMIT		100
#define APDS_ALS_GAIN_LO_LIMIT_STRICT	25

#define TIMESTEP			87 /* 2.7ms is about 87 / 32 */
#define TIME_STEP_SCALER		32

#define APDS_ALS_AVERAGING_TIME 50 /* tolerates 50/60Hz ripple */

static const u8 again[]	= {1, 8, 16, 120}; /* ALS gain steps */
static const u8 ir_currents[]	= {100, 50, 25, 12}; /* IRled currents in mA */
static const u16 arates[]	= {100, 200, 500, 1000};
static const u8 avail_apersis[] = {0, 1, 2, 3, 5, 10, 15, 20};

/* Regulators */
static const char reg_vcc[] = "Vdd";
static const char reg_vled[] = "Vled";

static int apds990x_read_byte(struct apds990x_chip *chip, u8 reg, u8 *data)
{
	struct i2c_client *client = chip->client;
	s32 ret;

	reg &= ~APDS990x_CMD_TYPE_MASK;
	reg |= APDS990x_CMD | APDS990x_CMD_TYPE_RB;

	ret = i2c_smbus_read_byte_data(client, reg);
	*data = ret;
	return (int)ret;
}

static int apds990x_read_word(struct apds990x_chip *chip, u8 reg, u16 *data)
{
	struct i2c_client *client = chip->client;
	s32 ret;

	reg &= ~APDS990x_CMD_TYPE_MASK;
	reg |= APDS990x_CMD | APDS990x_CMD_TYPE_INC;

	ret = i2c_smbus_read_word_data(client, reg);
	*data = ret;
	return (int)ret;
}

static int apds990x_write_byte(struct apds990x_chip *chip, u8 reg, u8 data)
{
	struct i2c_client *client = chip->client;
	s32 ret;

	reg &= ~APDS990x_CMD_TYPE_MASK;
	reg |= APDS990x_CMD | APDS990x_CMD_TYPE_RB;


	ret = i2c_smbus_write_byte_data(client, reg, data);
	return (int)ret;
}

static int apds990x_write_word(struct apds990x_chip *chip, u8 reg, u16 data)
{
	struct i2c_client *client = chip->client;
	s32 ret;

	reg &= ~APDS990x_CMD_TYPE_MASK;
	reg |= APDS990x_CMD | APDS990x_CMD_TYPE_INC;

	ret = i2c_smbus_write_word_data(client, reg, data);
	return (int)ret;
}

static int apds990x_mode(struct apds990x_chip *chip)
{
	u8 reg = APDS990X_EN_DISABLE_ALL;

	/* ALS is mandatory, proximity optional */
	if (chip->users) {
		reg |= APDS990X_EN_AIEN | APDS990X_EN_PON | APDS990X_EN_AEN |
			APDS990X_EN_WEN;

		if (chip->ps_en)
			reg |= APDS990X_EN_PIEN | APDS990X_EN_PEN;
	}
	return apds990x_write_byte(chip, APDS990X_ENABLE, reg);
}

static u16 apds990x_lux_to_threshold(struct apds990x_chip *chip, u32 lux)
{
	u32 thres;
	u32 cpl;
	u32 ir;

	/*
	 * Reported LUX value is a combination of the IR and CLEAR channel
	 * values. However, interrupt threshold is only for clear channel.
	 * This function approximates needed HW threshold value for a given
	 * LUX value in the current lightning type.
	 * IR level compared to visible light varies heavily depending on the
	 * source of the light
	 *
	 * Calculate threshold value for the next measurement period.
	 * Math: threshold = lux * cpl where
	 * cpl = atime * again / (glass_attenuation * device_factor)
	 * (count-per-lux)
	 *
	 * First remove calibration. Division by four is to avoid overflow
	 */
	lux = lux * (APDS_CALIB_SCALER / 4) / (chip->als_calib / 4);

	/* Multiplication by 64 is to increase accuracy */
	cpl = ((u32)chip->atime * (u32)again[chip->again_next] *
		APDS_PARAM_SCALE * 64) / (chip->cf.ga * chip->cf.df);

	thres = lux * cpl / (APDS990X_ALS_OUTPUT_SCALE * 64);
	/*
	 * Convert IR light from the latest result to match with
	 * new gain step. This helps to adapt with the current
	 * source of light.
	 */
	ir = (u32)chip->als_ir * (u32)again[chip->again_next] /
		(u32)again[chip->again_meas];

	/*
	 * Compensate count with IR light impact
	 * IAC1 > IAC2 (see apds990x_get_lux for formulas)
	 */
	if (chip->als_clear *  APDS_PARAM_SCALE >=
		chip->rcf.afactor * chip->als_ir)
		thres = (chip->rcf.cf1 * thres + chip->rcf.irf1 * ir) /
			APDS_PARAM_SCALE;
	else
		thres = (chip->rcf.cf2 * thres + chip->rcf.irf2 * ir) /
			APDS_PARAM_SCALE;

	if (thres >= chip->a_max_result)
		thres = chip->a_max_result - 1;
	return thres;
}

static inline int apds990x_set_atime(struct apds990x_chip *chip, u32 time_ms)
{
	u8 reg_value;

	chip->atime = time_ms;
	/* Formula is specified in the data sheet */
	reg_value = 256 - ((time_ms * TIME_STEP_SCALER) / TIMESTEP);
	/* Calculate max ADC value for given integration time */
	chip->a_max_result = (u16)(256 - reg_value) * APDS990X_TIME_TO_ADC;
	return apds990x_write_byte(chip, APDS990X_ATIME, reg_value);
}

static int apds990x_refresh_params(struct apds990x_chip *chip)
{
	/* If the chip is not in use, don't try to access it */
	if (chip->users == 0)
		return 0;

	apds990x_write_byte(chip, APDS990X_CONTROL,
			(chip->pdrive << 6) |
			(chip->pdiode << 4) |
			(chip->pgain << 2) |
			(chip->again_next << 0));
	return 0;
}

static int apds990x_refresh_pthres(struct apds990x_chip *chip, int data)
{
	int ret;
	int lo;
	int hi;
	/* If the chip is not in use, don't try to access it */
	if (chip->users == 0)
		return 0;

	if (data < chip->ps_thres) {
		lo = 0;
		hi = chip->ps_thres;
	} else {
		lo = chip->ps_thres - APDS_PS_HYSTERESIS;
		if (chip->ps_onoff)
			hi = APDS_RANGE;
		else
			hi = chip->ps_thres;
	}

	ret = apds990x_write_word(chip, APDS990X_PILTL, lo);
	ret |= apds990x_write_word(chip, APDS990X_PIHTL, hi);
	return ret;
}

static int apds990x_refresh_athres(struct apds990x_chip *chip)
{
	int ret;
	/* If the chip is not in use, don't try to access it */
	if (chip->users == 0)
		return 0;

	ret = apds990x_write_word(chip, APDS990X_AILTL,
			apds990x_lux_to_threshold(chip, chip->als_thres_lo));
	ret |= apds990x_write_word(chip, APDS990X_AIHTL,
			apds990x_lux_to_threshold(chip, chip->als_thres_hi));

	return ret;
}

static void apds990x_force_a_refresh(struct apds990x_chip *chip)
{
	/* This will force ALS interrupt after the next measurement. */
	apds990x_write_word(chip, APDS990X_AILTL, APDS_ALS_DEF_THRES_LO);
	apds990x_write_word(chip, APDS990X_AIHTL, APDS_ALS_DEF_THRES_HI);
}

static void apds990x_force_p_refresh(struct apds990x_chip *chip)
{
	/* This will force proximity interrupt after the next measurement. */
	apds990x_write_word(chip, APDS990X_PILTL, APDS_PS_DEF_THRES - 1);
	apds990x_write_word(chip, APDS990X_PIHTL, APDS_PS_DEF_THRES);
}

static int apds990x_calc_again(struct apds990x_chip *chip)
{
	int curr_again = chip->again_meas;
	int next_again = chip->again_meas;
	int ret = 0;

	if (chip->manual)
		return 0;

	/* Calculate suitable als gain */
	if (chip->als_clear == chip->a_max_result)
		next_again -= 2; /* ALS saturated. Decrease gain by 2 steps */
	else if (chip->als_clear > chip->a_max_result / 2)
		next_again--;
	else if (chip->als_clear < APDS_ALS_GAIN_LO_LIMIT_STRICT)
		next_again += 2; /* Too dark. Increase gain by 2 steps */
	else if (chip->als_clear < APDS_ALS_GAIN_LO_LIMIT)
		next_again++;

	/* Limit gain to available range */
	if (next_again < 0)
		next_again = 0;
	else if (next_again > APDS990X_MAX_AGAIN)
		next_again = APDS990X_MAX_AGAIN;

	/* Let's check can we trust the measured result */
	if ((next_again < curr_again) &&
		(chip->als_clear == chip->a_max_result)) {
		/*
		 * Next gain is smaller and current measurement saturated.
		 * Result can be totally garbage due to saturation
		 */
		ret = -ERANGE;
	} else if ((next_again > curr_again) &&
		(chip->als_clear < APDS_ALS_GAIN_LO_LIMIT_STRICT)) {
		/*
		 * Next gain is larger and current measurement underflows.
		 * Result can be totally garbage due to underflow
		 */
		ret = -ERANGE;
	}

	chip->again_next = next_again;
	apds990x_refresh_params(chip);
	if (ret < 0)
		apds990x_force_a_refresh(chip); /* Bad result -> remeasure */
	else
		apds990x_refresh_athres(chip);
	return ret;
}

static int apds990x_get_lux(struct apds990x_chip *chip, int clear, int ir)
{
	int iac, iac1, iac2; /* IR adjusted counts */
	u32 lpc; /* Lux per count */

	/* Formulas:
	 * iac1 = CF1 * CLEAR_CH - IRF1 * IR_CH
	 * iac2 = CF2 * CLEAR_CH - IRF2 * IR_CH
	 */
	iac1 = (chip->cf.cf1 * clear - chip->cf.irf1 * ir) / APDS_PARAM_SCALE;
	iac2 = (chip->cf.cf2 * clear - chip->cf.irf2 * ir) / APDS_PARAM_SCALE;

	iac = max(iac1, iac2);
	iac = max(iac, 0);

	lpc = APDS990X_ALS_OUTPUT_SCALE * (chip->cf.df * chip->cf.ga) /
		(u32)(again[chip->again_meas] * (u32)chip->atime);

	return (iac * lpc) / APDS_PARAM_SCALE;
}

static int apds990x_ack_int(struct apds990x_chip *chip, u8 mode)
{
	struct i2c_client *client = chip->client;
	s32 ret;
	u8 reg = APDS990x_CMD | APDS990x_CMD_TYPE_SPE;

	switch (mode & (APDS990X_ST_AINT | APDS990X_ST_PINT)) {
	case APDS990X_ST_AINT:
		reg |= APDS990X_INT_ACK_ALS;
		break;
	case APDS990X_ST_PINT:
		reg |= APDS990X_INT_ACK_PS;
		break;
	default:
		reg |= APDS990X_INT_ACK_BOTH;
		break;
	}

	ret = i2c_smbus_read_byte_data(client, reg);
	return (int)ret;
}

static irqreturn_t apds990x_irq(int irq, void *data)
{
	struct apds990x_chip *chip = data;
	u8 status;

	apds990x_read_byte(chip, APDS990X_STATUS, &status);
	apds990x_ack_int(chip, status);

	mutex_lock(&chip->mutex);
	if (chip->users) {
		/*
		 * These should not happen at the same interrupt unless
		 * interrupt prosessing is highly delayed.
		 */
		if (status & APDS990X_ST_AINT) {
			apds990x_read_word(chip, APDS990X_CDATAL,
					&chip->als_clear);
			apds990x_read_word(chip, APDS990X_IRDATAL,
					&chip->als_ir);
			/* Store used gain for calculations */
			chip->again_meas = chip->again_next;

			chip->als_lux_raw = apds990x_get_lux(chip,
							chip->als_clear,
							chip->als_ir);

			if (apds990x_calc_again(chip) == 0) {
				/* It takes long enough until overflow */
				chip->offset +=
					sizeof(struct apds990x_data_full);
				chip->status |= APDS_RESULT_ALS;
				chip->als_wait_fresh_res = false;
			}
		}

		if (status & APDS990X_ST_PINT)
			if (chip->ps_en) {
				u16 clr_ch;

				apds990x_read_word(chip, APDS990X_CDATAL,
						&clr_ch);
				/*
				 * If ALS channel is saturated at min gain,
				 * proximity gives false posivite values.
				 * Just ignore them.
				 */
				if ((chip->again_meas == 0) &&
					(clr_ch == chip->a_max_result)) {
					chip->ps_data = 0;
				} else {
					apds990x_read_word(chip,
							APDS990X_PDATAL,
							&chip->ps_data);
				}
				apds990x_refresh_pthres(chip, chip->ps_data);
				if (chip->ps_data < chip->ps_thres)
					chip->ps_data = 0;
				chip->offset +=
					sizeof(struct apds990x_data_full);
				chip->status |= APDS_RESULT_PS;
			}
		wake_up_interruptible(&chip->misc_wait);
	}
	mutex_unlock(&chip->mutex);
	return IRQ_HANDLED;
}

static int apds990x_configure(struct apds990x_chip *chip)
{
	/* It is recommended to use disabled mode during these operations */
	apds990x_write_byte(chip, APDS990X_ENABLE, APDS990X_EN_DISABLE_ALL);

	/* conversion and wait times for different state machince states */
	apds990x_write_byte(chip, APDS990X_PTIME, APDS990X_PTIME_DEFAULT);
	apds990x_write_byte(chip, APDS990X_WTIME, APDS990X_WTIME_DEFAULT);
	apds990x_set_atime(chip, APDS_ALS_AVERAGING_TIME);

	apds990x_write_byte(chip, APDS990X_CONFIG, 0);

	/*
	 * Total time for one FSM round without proximity.
	 * One bit in wait is about 2.7 ms = 27 / 10
	 */
	chip->fsm_time = APDS_ALS_AVERAGING_TIME +
		(256 - APDS990X_WTIME_DEFAULT) * 27 / 10;

	/* Persistence levels */
	apds990x_write_byte(chip, APDS990X_PERS,
			(chip->als_persistence << APDS990X_APERS_SHIFT) |
			(chip->ps_persistence << APDS990X_PPERS_SHIFT));

	apds990x_write_byte(chip, APDS990X_PPCOUNT, APDS990X_PPCOUNT_DEFAULT);

	/* Start with relatively small gain */
	chip->again_meas = 1;
	chip->again_next = 1;
	apds990x_write_byte(chip, APDS990X_CONTROL,
			(chip->pdrive << 6) |
			(chip->pdiode << 4) |
			(chip->pgain << 2) |
			(chip->again_next << 0));
	return 0;
}

static int apds990x_detect(struct apds990x_chip *chip)
{
	struct i2c_client *client = chip->client;
	int ret;
	u8 id;

	ret = apds990x_read_byte(chip, APDS990X_ID, &id);
	if (ret < 0) {
		dev_err(&client->dev, "ID read failed\n");
		return ret;
	}

	ret = apds990x_read_byte(chip, APDS990X_REV, &chip->revision);
	if (ret < 0) {
		dev_err(&client->dev, "REV read failed\n");
		return ret;
	}

	switch (id) {
	case APDS990X_ID_0:
	case APDS990X_ID_29:
		snprintf(chip->chipname, sizeof(chip->chipname), "APDS-990x");
		break;
	case APDS990X_ID_4:
		snprintf(chip->chipname, sizeof(chip->chipname), "QPDS-T900");
		break;
	default:
		ret = -ENODEV;
		break;
	}
	return ret;
}

static unsigned int apds990x_poll(struct file *file, poll_table *wait)
{
	struct apds990x_chip *chip = container_of(file->private_data,
						struct apds990x_chip,
						miscdev);
	poll_wait(file, &chip->misc_wait, wait);
	if (file->f_pos < chip->offset)
		return POLLIN | POLLRDNORM;
	return 0;
}

static ssize_t apds990x_read(struct file *file, char __user *buf,
			size_t count, loff_t *offset)
{
	struct apds990x_chip *chip = container_of(file->private_data,
						struct apds990x_chip,
						miscdev);
	struct apds990x_data_full result;
	int data_amount;
	u32 tmp;

	if (count < sizeof(struct apds990x_data))
		return -EINVAL;

	if (*offset >= chip->offset) {
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(chip->misc_wait,
						(*offset < chip->offset)))
			return -ERESTARTSYS;
	}
	mutex_lock(&chip->mutex);

	/* Ambient light result */
	result.data.lux_raw = chip->als_lux_raw;
	tmp = (result.data.lux_raw * chip->als_calib) / APDS_CALIB_SCALER;
	if (tmp > (APDS_RANGE * APDS990X_ALS_OUTPUT_SCALE))
		tmp = APDS_RANGE * APDS990X_ALS_OUTPUT_SCALE;
	result.data.lux = tmp;

	result.data.status = 0;
	if ((chip->als_clear == chip->a_max_result) ||
		(chip->als_ir == chip->a_max_result))
		result.data.status = APDS990X_ALS_SATURATED;

	/* Proximity result */
	if (chip->ps_en) {
		result.data.ps_raw = chip->ps_data;
		tmp = ((u32)result.data.ps_raw * chip->ps_calib) /
			APDS_CALIB_SCALER;
		if (tmp > APDS_RANGE)
			tmp = APDS_RANGE;
		result.data.ps = tmp;
		result.data.status |= APDS990X_PS_ENABLED;
	} else {
		result.data.ps = 0;
		result.data.ps_raw = 0;
	}

	if (count == sizeof(result)) {
		result.als_clear = chip->als_clear;
		result.als_ir = chip->als_ir;
		result.als_gain = chip->again_meas;
		result.als_atime = chip->atime;
		result.ps_gain = chip->pgain;
		data_amount = sizeof(result);
	} else {
		data_amount = sizeof(struct apds990x_data);
	}

	if (chip->status & APDS_RESULT_ALS)
		result.data.status |= APDS990X_ALS_UPDATED;
	if (chip->status & APDS_RESULT_PS)
		result.data.status |= APDS990X_PS_UPDATED;

	*offset = chip->offset;

	mutex_unlock(&chip->mutex);

	return copy_to_user(buf, &result, data_amount) ? -EFAULT : data_amount;
}

static int apds990x_open(struct inode *inode, struct file *file)
{
	struct apds990x_chip *chip = container_of(file->private_data,
						struct apds990x_chip,
						miscdev);
	int err;

	mutex_lock(&chip->mutex);
	if (!chip->users) {
		err = regulator_bulk_enable(ARRAY_SIZE(chip->regs),
					chip->regs);
		if (err < 0)
			goto release_lock;

		msleep(APDS_STARTUP_DELAY);

		/* Refresh all configs in case of regulators were off */
		apds990x_configure(chip);
		chip->status = 0;
	}
	chip->users++;
	chip->als_wait_fresh_res = true;
	err = apds990x_mode(chip);
	apds990x_force_a_refresh(chip);
	apds990x_force_p_refresh(chip);
	file->f_pos = chip->offset;

release_lock:
	mutex_unlock(&chip->mutex);
	return err;
}

static int apds990x_close(struct inode *inode, struct file *file)
{
	struct apds990x_chip *chip = container_of(file->private_data,
						struct apds990x_chip,
						miscdev);
	mutex_lock(&chip->mutex);
	if (!--chip->users) {
		apds990x_mode(chip);
		regulator_bulk_disable(ARRAY_SIZE(chip->regs), chip->regs);
	}
	mutex_unlock(&chip->mutex);
	return 0;
}

static const struct file_operations apds990x_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= apds990x_read,
	.poll		= apds990x_poll,
	.open		= apds990x_open,
	.release	= apds990x_close,
};

static ssize_t apds990x_als_calib_format_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", APDS_CALIB_SCALER);
}

static DEVICE_ATTR(als_calib_format, S_IRUGO, apds990x_als_calib_format_show,
					      NULL);

static ssize_t apds990x_als_calib_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct apds990x_chip *chip = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%u\n", chip->als_calib);
}

static ssize_t apds990x_als_calib_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len)
{
	struct apds990x_chip *chip = dev_get_drvdata(dev);
	unsigned long value;

	if (strict_strtoul(buf, 0, &value))
		return -EINVAL;

	if (chip->als_calib > APDS_RANGE)
		return -EINVAL;

	chip->als_calib = value;

	return len;
}

static DEVICE_ATTR(als_calib, S_IRUGO | S_IWUSR, apds990x_als_calib_show,
		apds990x_als_calib_store);

static ssize_t apds990x_store_gain(struct apds990x_chip *chip,
				const char *buf, size_t len,
				const u8 *gaintable,
				u8 gaintablesize, u8 *target)
{
	unsigned long value;
	int i;

	if (strict_strtoul(buf, 0, &value))
		return -EINVAL;

	for (i = 0; i < gaintablesize; i++)
		if (value == gaintable[i]) {
			mutex_lock(&chip->mutex);
			*target = i;
			apds990x_refresh_params(chip);
			apds990x_refresh_athres(chip);
			mutex_unlock(&chip->mutex);
			break;
		}
	if (i == gaintablesize)
		return -EINVAL;
	return len;
}

static ssize_t apds990x_again_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct apds990x_chip *chip =  dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", again[chip->again_meas]);
}

static ssize_t apds990x_again_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len)
{
	struct apds990x_chip *chip =  dev_get_drvdata(dev);
	return apds990x_store_gain(chip, buf, len,
				again, ARRAY_SIZE(again), &chip->again_next);
}

static DEVICE_ATTR(again, S_IRUGO | S_IWUSR, apds990x_again_show,
		apds990x_again_store);

static ssize_t apds990x_atime_avail(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	int i;
	int pos = 0;
	for (i = 0; i < ARRAY_SIZE(arates); i++)
		pos += sprintf(buf + pos, "%d ", arates[i]);
	sprintf(buf + pos - 1, "\n");
	return pos;
}

static ssize_t apds990x_atime_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct apds990x_chip *chip =  dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", chip->atime_tot);
}

static ssize_t apds990x_atime_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len)
{
	struct apds990x_chip *chip =  dev_get_drvdata(dev);
	unsigned long value;
	int ret = len;
	int i;

	if (strict_strtoul(buf, 0, &value))
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(arates); i++)
		if (value == arates[i])
			break;

	if (i == ARRAY_SIZE(arates))
		return -EINVAL;

	mutex_lock(&chip->mutex);

	chip->atime_tot = value;

	/* Convert to available HW values */
	value /= chip->fsm_time;
	for (i = 0; i < sizeof(avail_apersis) - 1; i++)
		if (value >= avail_apersis[i])
			break;
	chip->als_persistence = value;

	/* Persistence levels */
	apds990x_write_byte(chip, APDS990X_PERS,
			(chip->als_persistence << APDS990X_APERS_SHIFT) |
			(chip->ps_persistence << APDS990X_PPERS_SHIFT));

	mutex_unlock(&chip->mutex);

	return ret;
}

static DEVICE_ATTR(atime_avail, S_IRUGO, apds990x_atime_avail, NULL);

static DEVICE_ATTR(atime, S_IRUGO | S_IWUSR,
		apds990x_atime_show,
		apds990x_atime_store);

static ssize_t apds990x_get_avail_strengths(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	int i;
	int pos = 0;
	for (i = ARRAY_SIZE(ir_currents); i > 0; i--)
		pos += sprintf(buf + pos, "%d ", ir_currents[i - 1]);
	sprintf(buf + pos - 1, "\n");
	return pos;
}

static DEVICE_ATTR(prox_avail_strength, S_IRUGO,
		apds990x_get_avail_strengths, NULL);

static ssize_t apds990x_get_strength(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct apds990x_chip *chip =  dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", ir_currents[chip->pdrive]);
}

static ssize_t apds990x_set_strength(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct apds990x_chip *chip =  dev_get_drvdata(dev);
	unsigned long value;
	int i;

	if (strict_strtoul(buf, 0, &value))
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(ir_currents); i++)
		if (value == ir_currents[i])
			break;

	if (i < ARRAY_SIZE(ir_currents)) {
		mutex_lock(&chip->mutex);
		chip->pdrive = i;
		apds990x_refresh_params(chip);
		mutex_unlock(&chip->mutex);
	} else {
		return -EINVAL;
	}
	return count;
}

static DEVICE_ATTR(prox_strength, S_IRUGO | S_IWUSR,
		apds990x_get_strength, apds990x_set_strength);

static ssize_t apds990x_prox_enable_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct apds990x_chip *chip =  dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", chip->ps_en);
}

static ssize_t apds990x_prox_enable_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len)
{
	struct apds990x_chip *chip =  dev_get_drvdata(dev);
	unsigned long value;

	if (strict_strtoul(buf, 0, &value))
		return -EINVAL;

	mutex_lock(&chip->mutex);
	chip->ps_en = !!value;
	chip->status &= ~APDS_RESULT_PS;
	if (chip->users) {
		apds990x_mode(chip);
		apds990x_force_p_refresh(chip);
	}
	mutex_unlock(&chip->mutex);
	return len;
}

static DEVICE_ATTR(prox_enable, S_IRUGO | S_IWUSR,
		apds990x_prox_enable_show,
		apds990x_prox_enable_store);

static ssize_t apds990x_prox_onoff_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct apds990x_chip *chip =  dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", chip->ps_onoff);
}

static ssize_t apds990x_prox_onoff_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len)
{
	struct apds990x_chip *chip =  dev_get_drvdata(dev);
	unsigned long value;

	if (strict_strtoul(buf, 0, &value))
		return -EINVAL;

	chip->ps_onoff = !!value;
	return len;
}

static DEVICE_ATTR(prox_onoff_mode, S_IRUGO | S_IWUSR,
		apds990x_prox_onoff_show,
		apds990x_prox_onoff_store);

static ssize_t apds990x_als_threshold_range_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct apds990x_chip *chip =  dev_get_drvdata(dev);
	return sprintf(buf, "%d %d\n", chip->als_thres_lo, chip->als_thres_hi);
}

static ssize_t apds990x_als_threshold_range_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len)
{
	struct apds990x_chip *chip =  dev_get_drvdata(dev);
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

	if (thres_hi > APDS_RANGE * APDS990X_ALS_OUTPUT_SCALE)
		return -EINVAL;

	if (thres_lo < 0)
		return -EINVAL;

	mutex_lock(&chip->mutex);
	chip->als_thres_lo = thres_lo;
	chip->als_thres_hi = thres_hi;
	if (!chip->als_wait_fresh_res)
		apds990x_refresh_athres(chip);
	mutex_unlock(&chip->mutex);

	return len;
}

static DEVICE_ATTR(als_threshold_range, S_IRUGO | S_IWUSR,
		apds990x_als_threshold_range_show,
		apds990x_als_threshold_range_store);

static ssize_t apds990x_prox_threshold_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct apds990x_chip *chip =  dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", chip->ps_thres);
}

static ssize_t apds990x_prox_threshold_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len)
{
	struct apds990x_chip *chip =  dev_get_drvdata(dev);
	unsigned long value;

	if (strict_strtoul(buf, 0, &value))
		return -EINVAL;

	if ((value > APDS_RANGE) || (value == 0) ||
		(value < APDS_PS_HYSTERESIS))
		return -EINVAL;

	chip->ps_thres = value;

	apds990x_force_p_refresh(chip);
	return len;
}

static DEVICE_ATTR(prox_threshold, S_IRUGO | S_IWUSR,
		apds990x_prox_threshold_show,
		apds990x_prox_threshold_store);

static ssize_t apds990x_manualmode_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct apds990x_chip *chip =  dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", chip->manual);
}

static struct attribute_group apds990x_attribute_group[];

static ssize_t apds990x_manualmode_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len)
{
	struct apds990x_chip *chip =  dev_get_drvdata(dev);
	unsigned long value;

	if (strict_strtoul(buf, 0, &value))
		return -EINVAL;

	mutex_lock(&chip->mutex);
	if (chip->manual != !!value) {
		if (!!value) {
			int ret;
			ret = sysfs_create_group(&dev->kobj,
						&apds990x_attribute_group[1]);
			if (ret < 0) {
				mutex_unlock(&chip->mutex);
				return ret;
			}
		} else {
			sysfs_remove_group(&dev->kobj,
					&apds990x_attribute_group[1]);
		}
	}

	chip->manual = !!value;
	mutex_unlock(&chip->mutex);
	return len;
}

static DEVICE_ATTR(manualmode, S_IRUGO | S_IWUSR,
		apds990x_manualmode_show,
		apds990x_manualmode_store);

static ssize_t apds990x_chip_id_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct apds990x_chip *chip =  dev_get_drvdata(dev);
	return sprintf(buf, "%s %d\n", chip->chipname, chip->revision);
}

static DEVICE_ATTR(chip_id, S_IRUGO, apds990x_chip_id_show, NULL);

static struct attribute *sysfs_attrs_ctrl[] = {
	&dev_attr_als_calib.attr,
	&dev_attr_als_calib_format.attr,
	&dev_attr_atime.attr,
	&dev_attr_atime_avail.attr,
	&dev_attr_chip_id.attr,
	&dev_attr_prox_enable.attr,
	&dev_attr_prox_onoff_mode.attr,
	&dev_attr_manualmode.attr,
	&dev_attr_prox_avail_strength.attr,
	&dev_attr_prox_strength.attr,
	&dev_attr_als_threshold_range.attr,
	&dev_attr_prox_threshold.attr,
	NULL
};

static struct attribute *sysfs_attrs_manual[] = {
	&dev_attr_again.attr,
	NULL,
};

static struct attribute_group apds990x_attribute_group[] = {
	{.attrs = sysfs_attrs_ctrl },
	{.attrs = sysfs_attrs_manual },
};

static int __devinit apds990x_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct apds990x_chip *chip;
	int err;

	chip = kzalloc(sizeof *chip, GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	init_waitqueue_head(&chip->misc_wait);

	i2c_set_clientdata(client, chip);
	chip->client  = client;

	mutex_init(&chip->mutex);
	chip->pdata	= client->dev.platform_data;

	if (chip->pdata == NULL) {
		dev_err(&client->dev, "platform data is mandatory\n");
		err = -EINVAL;
		goto fail1;
	}

	if (chip->pdata->cf.ga == 0) {
		/* set uncovered sensor default parameters */
		chip->cf.ga = 1966; /* 0.48 * APDS_PARAM_SCALE */
		chip->cf.cf1 = 4096; /* 1.00 * APDS_PARAM_SCALE */
		chip->cf.irf1 = 9134; /* 2.23 * APDS_PARAM_SCALE */
		chip->cf.cf2 = 2867; /* 0.70 * APDS_PARAM_SCALE */
		chip->cf.irf2 = 5816; /* 1.42 * APDS_PARAM_SCALE */
		chip->cf.df = 52;
	} else {
		chip->cf = chip->pdata->cf;
	}

	/* precalculate inverse chip factors for threshold control */
	chip->rcf.afactor =
		(chip->cf.irf1 - chip->cf.irf2) * APDS_PARAM_SCALE /
		(chip->cf.cf1 - chip->cf.cf2);
	chip->rcf.cf1 = APDS_PARAM_SCALE * APDS_PARAM_SCALE /
		chip->cf.cf1;
	chip->rcf.irf1 = chip->cf.irf1 * APDS_PARAM_SCALE /
		chip->cf.cf1;
	chip->rcf.cf2 = APDS_PARAM_SCALE * APDS_PARAM_SCALE /
		chip->cf.cf2;
	chip->rcf.irf2 = chip->cf.irf2 * APDS_PARAM_SCALE /
		chip->cf.cf2;

	/* Set something to start with */
	chip->als_thres_hi = APDS_ALS_DEF_THRES_HI;
	chip->als_thres_lo = APDS_ALS_DEF_THRES_LO;
	chip->als_calib = APDS_ALS_NEUTRAL_CALIB_VALUE;
	chip->als_persistence = APDS_DEFAULT_ALS_PERS;
	chip->atime_tot = 200;

	chip->ps_thres = APDS_PS_DEF_THRES;
	chip->pdrive = chip->pdata->pdrive;
	chip->pdiode = APDS_PDIODE_IR;
	chip->pgain = APDS_PGAIN_1X;
	chip->ps_calib = APDS_PS_NEUTRAL_CALIB_VALUE;
	chip->ps_persistence = APDS_DEFAULT_PS_PERS;
	chip->ps_onoff = true;

	chip->regs[0].supply = reg_vcc;
	chip->regs[1].supply = reg_vled;

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

	err = apds990x_detect(chip);
	apds990x_configure(chip);
	regulator_bulk_disable(ARRAY_SIZE(chip->regs), chip->regs);
	if (err < 0)
		goto fail2;

	if (chip->pdata->setup_resources) {
		err = chip->pdata->setup_resources();
		if (err) {
			err = -EINVAL;
			goto fail2;
		}
	}

	err = sysfs_create_group(&chip->client->dev.kobj,
				&apds990x_attribute_group[0]);
	if (err < 0) {
		dev_err(&chip->client->dev, "Sysfs registration failed\n");
		goto fail3;
	}

	chip->miscdev.minor  = MISC_DYNAMIC_MINOR;
	chip->miscdev.name   = "apds990x0";
	chip->miscdev.fops   = &apds990x_fops;
	chip->miscdev.parent = &chip->client->dev;
	err = misc_register(&chip->miscdev);
	if (err < 0) {
		dev_err(&chip->client->dev, "Device registration failed\n");
		goto fail4;
	}

	err = request_threaded_irq(client->irq, NULL,
				apds990x_irq,
				IRQF_TRIGGER_FALLING | IRQF_TRIGGER_LOW |
				IRQF_ONESHOT,
				"apds990x", chip);
	if (err) {
		dev_err(&client->dev, "could not get IRQ %d\n",
			client->irq);
		goto fail5;
	}
	return err;
fail5:
	misc_deregister(&chip->miscdev);
fail4:
	sysfs_remove_group(&chip->client->dev.kobj,
			&apds990x_attribute_group[0]);
fail3:
	if (chip->pdata && chip->pdata->release_resources)
		chip->pdata->release_resources();
fail2:
	regulator_bulk_free(ARRAY_SIZE(chip->regs), chip->regs);
fail1:
	kfree(chip);
	return err;
}

static int __devexit apds990x_remove(struct i2c_client *client)
{
	struct apds990x_chip *chip = i2c_get_clientdata(client);

	free_irq(client->irq, chip);
	sysfs_remove_group(&chip->client->dev.kobj,
			&apds990x_attribute_group[0]);
	if (chip->manual)
		sysfs_remove_group(&chip->client->dev.kobj,
				&apds990x_attribute_group[1]);
	if (chip->pdata && chip->pdata->release_resources)
		chip->pdata->release_resources();

	misc_deregister(&chip->miscdev);

	regulator_bulk_free(ARRAY_SIZE(chip->regs), chip->regs);

	kfree(chip);
	return 0;
}

#ifdef CONFIG_PM
static int apds990x_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct apds990x_chip *chip = i2c_get_clientdata(client);

	mutex_lock(&chip->mutex);
	apds990x_write_byte(chip, APDS990X_ENABLE, 0);
	mutex_unlock(&chip->mutex);
	return 0;
}

static int apds990x_resume(struct i2c_client *client)
{
	struct apds990x_chip *chip = i2c_get_clientdata(client);

	mutex_lock(&chip->mutex);
	apds990x_mode(chip);
	mutex_unlock(&chip->mutex);
	return 0;
}

static void apds990x_shutdown(struct i2c_client *client)
{
	struct apds990x_chip *chip = i2c_get_clientdata(client);
	apds990x_write_byte(chip, APDS990X_ENABLE, 0);
}
#else
#define apds990x_suspend  NULL
#define apds990x_resume   NULL
#define apds990x_shutdown NULL
#endif

static const struct i2c_device_id apds990x_id[] = {
	{"apds990x", 0 },
	{"qpdst900", 0 },
	{}
};

MODULE_DEVICE_TABLE(i2c, apds990x_id);

static struct i2c_driver apds990x_driver = {
	.driver	 = {
		.name	= "apds990x",
		.owner	= THIS_MODULE,
	},
	.suspend  = apds990x_suspend,
	.shutdown = apds990x_shutdown,
	.resume	  = apds990x_resume,
	.probe	  = apds990x_probe,
	.remove	  = __devexit_p(apds990x_remove),
	.id_table = apds990x_id,
};

static int __init apds990x_init(void)
{
	return i2c_add_driver(&apds990x_driver);
}

static void __exit apds990x_exit(void)
{
	i2c_del_driver(&apds990x_driver);
}

MODULE_DESCRIPTION("APDS990X combined ALS and proximity sensor");
MODULE_AUTHOR("Samu Onkalo, Nokia Corporation");
MODULE_LICENSE("GPL v2");

module_init(apds990x_init);
module_exit(apds990x_exit);
