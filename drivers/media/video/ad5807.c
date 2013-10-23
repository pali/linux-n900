/**
 * drivers/media/video/ad5807.c
 *
 * AD5807 piezo actuator driver in ET8EN2 camera module.
 *
 * Copyright (C) 2008 Nokia Corporation
 * Copyright (C) 2007 Texas Instruments
 *
 * Contact: Vimarsh Zutshi <vimarsh.zutshi@nokia.com>
 *
 * Based on ad5820.c by
 *          Tuukka Toivonen <tuukka.o.toivonen@nokia.com>
 *          Sakari Ailus <sakari.ailus@nokia.com>
 *
 * Based on af_d88.c by Texas Instruments.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/mutex.h>

#include <linux/videodev2.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/ad5807.h>

#define AD5807_EXTCLK 	(9.6 * 1000 * 1000)

/**
 * Register initializations for ES2.0 samples @ 9.6MHz EXTCLK
 * Array index is the register index.
 * This puts the actuator in a "Single Shot Drive initiated by I2C write"
 *
 * For ES2.1, PWM_PERIOD, PWM_TAL, PWM_TBH are different (0x80, 0x76, 0x67)
 * but since we are overwriting these values from Module's OTP,
 * we should get the correct values. !!VERIFY!!
 */
static unsigned char ad5807_regs_defaults[] = {
	0,	/* Dummy */
	0x00,	/* 0x01: */
	0x18,	/* 0x02: MCLK: 0x18: Use EXTCLK 9.6MHz of Swordfish, div 1 */
	0x28,
	0x64,
	0xA2,	/* 0x05: PWM_PERIOD */
	0x0A,	/* 0x06: PWM_TAH */
	0x98,	/* 0x07: PWM_TAL */
	0x89,	/* 0x08: PWM_TBH */
	0x0F,	/* 0x09: PWM_TBL */
	0x00,	/* 0x0A: */
	0x00,
	0x02,
	0x00,	/* 0x0D: PULSEOUT_MSB */
	0x00,	/* 0x0E: PULSEOUT_LSB */
	0x0C,	/* 0x0F: */
	0x00,
	0x21,
	0xAA,
	0x00,
	0x00,
	0x00,	/* 0x15: */
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,	/* 0x1A: */
	0x00,
	0x00,	/* 0x1C: */
};

/**
 * Tunning parameter initializations for ES2.0.
 */
static struct ad5807_parms ad5807_params_defaults = {
	.plsn_far2inf 		= 2000,
	.plsn_inf2macro 	= 4000,
	.plsn_macro2near 	= 800,
	.plsn_near2macro 	= 982,
	.plsn_macro2inf 	= 4900,
	.plsn_inf2far 		= 2450,
	.period 		= 162,
	.tah 			= 10,
	.tal 			= 152,
	.tbh 			= 137,
	.tbl 			= 15,
	.plsnum_dira 		= 22,
	.plsnum_dirb 		= 27,
	.p_near 		= 580,
	.p_10cm 		= 568,
	.p_infinity 		= 124,
	.p_far 			= 120,
};

#define AD5807_NUM_REGS	ARRAY_SIZE(ad5807_regs_defaults)

#define CTRL_FOCUS_ABSOLUTE	0
#define CTRL_FOCUS_RELATIVE	1

static int ad5807_write(struct ad5807_device *coil, u8 index, u8 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&coil->subdev);
	struct i2c_msg msg;
	u8 data[2] = {0, 0};
	int rval;

	if (!client->adapter)
		return -ENODEV;

	msg.addr  = client->addr;
	msg.flags = 0;
	msg.len   = 2;
	msg.buf   = data;

	data[0] = index;
	data[1] = val;

	rval = i2c_transfer(client->adapter, &msg, 1);
	if (rval == 1)
		return 0;

	dev_err(&client->dev, "write failed, error %d\n", rval);

	return rval;
}

static int ad5807_read(struct ad5807_device *coil, u8 index, u8 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&coil->subdev);
	struct i2c_msg msg[2];
	int rval;

	if (!client->adapter)
		return -ENODEV;

	msg[0].addr  = client->addr;
	msg[0].flags = 0;
	msg[0].len   = 1;
	msg[0].buf   = (u8 *)&index;

	msg[1].addr  = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len   = 1;
	msg[1].buf   = val;
	*val = 0;

	rval = i2c_transfer(client->adapter, msg, 2);
	if (rval == 2)
		return 0;

	dev_err(&client->dev, "read failed, error %d\n", rval);

	return rval;
}

static inline int ad5807_is_busy(struct ad5807_device *coil)
{
	u8 status = 0;

	ad5807_read(coil, AD5807_REG_STATUS, &status);

	return (status >> AD5807_STATUS_BUSY_BIT) & 0x1;
}

static inline int ad5807_reset(struct ad5807_device *coil)
{
	return ad5807_write(coil, AD5807_REG_CONTROL,
			    1 << AD5807_CONTROL_RESET_BIT);
}

static inline int ad5807_interrupt(struct ad5807_device *coil)
{
	return ad5807_write(coil, AD5807_REG_CONTROL,
			    1 << AD5807_CONTROL_INTERRUPT_BIT);
}

static inline int ad5807_clear_mecha_drive(struct ad5807_device *coil)
{
	coil->work_cmd &= ~AD5807_WORK_CMD_MECHA_DRIVE_STOP;
	return ad5807_write(coil, AD5807_REG_SHUTTER_CONF,
			    AD5807_SHUTTER_CONF_HIZ);
}

#define IS_MECHA_DRIVE_ONGOING (coil->work_cmd & \
				AD5807_WORK_CMD_MECHA_DRIVE_STOP)

/**
 * ad5807_wait_on_mecha_drive:
 *    Mechanical drive to open/close Shutter or Iris membranes is
 *    mutually exclusive and once a mechanical drive is started it has
 *    to be run for some minimum time (AD5807_MECHA_DRIVE_TIME_MS)
 */
static void ad5807_wait_on_mecha_drive(struct ad5807_device *coil)
{
	mutex_lock(&coil->mutex);
	if (IS_MECHA_DRIVE_ONGOING) {
		if (coil->opda_mode == AD5807_OPDA_MODE_I2C) {
			INIT_COMPLETION(coil->md_completion);
			mutex_unlock(&coil->mutex);
			wait_for_completion(&coil->md_completion);
			mutex_lock(&coil->mutex);
		} else {	/* AD5807_OPDA_MODE_STROBE */
			ad5807_clear_mecha_drive(coil);
		}
	}
	mutex_unlock(&coil->mutex);
}

/**
 * ad5807_opda_control:
 *   Controls Output Drive *Activation* mode for SHUTTER/IRIS and
 *   LENS MOVE operations.
 *   This essentially controls how the Shutter/Iris and Lens Move
 *   commands will be executed. When OPDA == STROBE, the cmds are
 *   executed when the external strobe signal toggles. The strobe
 *   signal for AD5807 is driven by the Swordfish sensor module.
 *   When OPDA == I2C, the cmds are executed just after the I2C write
 *   to the cmd register (0x14)
 */
static int ad5807_opda_control(struct ad5807_device *coil, u8 opda)
{
	int rval = 0;
	u8 cmd;

	rval = ad5807_read(coil, AD5807_REG_CONFIG, &cmd);
	if (rval)
		return rval;

	cmd &= ~AD5807_CONFIG_TRIG_MASK;

	switch (opda) {
	case AD5807_OPDA_MODE_I2C:
		cmd |= AD5807_CONFIG_TRIG_I2C;
		break;
	case AD5807_OPDA_MODE_STROBE:
		cmd |= AD5807_CONFIG_TRIG_STROBE;
		break;
	default:
		return -EINVAL;
	}

	/**
	 * Check and wait on any ongoing mechanical drive
	 */
	ad5807_wait_on_mecha_drive(coil);

	/**
	 * Clear any pending ACTIVE command
	 */
	rval = ad5807_interrupt(coil);
	if (rval)
		return rval;

	if (ad5807_is_busy(coil))	/* This should not be so */
		return -EBUSY;

	/* Change OPDA */
	rval = ad5807_write(coil, AD5807_REG_CONFIG, cmd);
	if (rval)
		return rval;

	coil->opda_mode = opda;
	return rval;
}

/**
 * ad5807_opd_control:
 *   Controls the Output Drive mode for LENS MOVE operation.
 *   This essentially controls the output waveform patterns from the
 *   Pattern Generator. Output Waveform can be generated in a Single Shot,
 *   or can be Continuous,  or can be output in bursts where each burst
 *   is initiated by a strobe pulse.
 *   ONCE the device is put in a continuous or multi shot drive mode, the
 *   only way to take it out of the mode is either a Reset/Interrupt cmd
 *   or toggling XSHUTDOWN.
 */
static int ad5807_opd_control(struct ad5807_device *coil, u8 opd)
{
	int rval = 0;
	u8 cmd;

	rval = ad5807_read(coil, AD5807_REG_DRIVE_CONFIG, &cmd);
	if (rval)
		return rval;

	cmd &= ~AD5807_DRIVE_CONFIG_CM_DRV_MASK;

	switch (opd) {
	case AD5807_OPD_MODE_SS:
		cmd |= AD5807_DRIVE_CONFIG_CM_DRV_OFF;
		break;
	case AD5807_OPD_MODE_MS:
		if (coil->opda_mode != AD5807_OPDA_MODE_STROBE)
			return -EINVAL;

		cmd |= AD5807_DRIVE_CONFIG_CM_DRV_ON;
		break;
	case AD5807_OPD_MODE_CD:
		if (coil->opda_mode != AD5807_OPDA_MODE_I2C)
			return -EINVAL;

		cmd |= AD5807_DRIVE_CONFIG_CM_DRV_ON;
		break;
	default:
		return -EINVAL;
	}

	/**
	 * Check and wait on any ongoing mechanical drive
	 */
	ad5807_wait_on_mecha_drive(coil);

	/**
	 * Clear any pending ACTIVE command
	 */
	rval = ad5807_interrupt(coil);
	if (rval)
		return rval;

	if (ad5807_is_busy(coil))	/* This should not be so */
		return -EBUSY;

	/* Change OPD */
	rval = ad5807_write(coil, AD5807_REG_DRIVE_CONFIG, cmd);
	if (rval)
		return rval;

	coil->opd_mode = opd;
	return rval;
}

/**
 * ad5807_work:
 *   When the Shutter/Iris Open/Close command is executed the SIN output
 *   current pulse needs to be driven for some minimum time. After the
 *   time is over the SIN output is restored to it high impedance state.
 *   ADDITIONALLY, if a power off command is received from upper layers
 *   during that time, it is delayed and then executed here.
 */
static void ad5807_work(struct work_struct *work)
{
	int rval;
	struct ad5807_device *coil = container_of(work, struct ad5807_device,
						  ad5807_work.work);

	mutex_lock(&coil->mutex);

	if (coil->work_cmd & AD5807_WORK_CMD_MECHA_DRIVE_STOP) {
		rval = ad5807_clear_mecha_drive(coil);
		if (rval)
			coil->work_cmd |= (0x1 << 31);

		coil->work_counter = 0;
		if (!completion_done(&coil->md_completion))
			complete(&coil->md_completion);
	}

	if (coil->work_cmd & AD5807_WORK_CMD_POWER_OFF) {
		coil->platform_data->set_xshutdown(&coil->subdev, 0);
		coil->platform_data->set_xclk(&coil->subdev, 0);
		coil->power_state = 0;
		coil->work_cmd &= ~AD5807_WORK_CMD_POWER_OFF;
	}

	mutex_unlock(&coil->mutex);
}

/**
 * ad5807_shutter_control:
 *   open == 1 => Open Shutter
 *   open == 0 => Close Shutter
 */
static int ad5807_shutter_control(struct ad5807_device *coil, u8 open)
{
	int rval;
	u8 cmd1, cmd2;

	/**
	 * Check and wait on any ongoing mechanical drive
	 */
	ad5807_wait_on_mecha_drive(coil);

	if (ad5807_is_busy(coil))
		return -EBUSY;

	if (coil->work_cmd & (0x1 << 31)) {
		rval = ad5807_write(coil, AD5807_REG_SHUTTER_CONF,
				    AD5807_SHUTTER_CONF_HIZ);
		if (rval)
			return rval;
	}

	cmd1 = (open) ? AD5807_SHUTTER_CONF_SHUT_OPEN :
			AD5807_SHUTTER_CONF_SHUT_CLOSE;

	cmd2 = AD5807_SHUTTER_ACTIVATE |
	       AD5807_SHUTTER_DELAY_DISABLE |
	       AD5807_SHUTTER_SHONTIME_REG;

	rval = ad5807_write(coil, AD5807_REG_SHUTTER_CONF, cmd1);
	if (rval)
		return rval;

	rval = ad5807_write(coil, AD5807_REG_SHUTTER, cmd2);
	if (rval)
		return rval;

	coil->work_cmd |= AD5807_WORK_CMD_MECHA_DRIVE_STOP;
	if (coil->opda_mode == AD5807_OPDA_MODE_I2C)
		schedule_delayed_work(&coil->ad5807_work,
			      msecs_to_jiffies(AD5807_MECHA_DRIVE_TIME_MS));

	return rval;
}

/**
 * ad5807_iris_control:
 *   iris: AD5807_IRIS_F48 or AD5807_IRIS_F32
 *   open == 1 => Open IRIS membrane (Remove Aperture)
 *   open == 0 => Close IRIS membrane (Apply Aperture)
 */
static int ad5807_iris_control(struct ad5807_device *coil,
				enum ad5807_iris_type iris, u8 open)
{
	int rval;
	u8 cmd1, cmd2;

	/**
	 * Check and wait on any ongoing mechanical drive
	 */
	ad5807_wait_on_mecha_drive(coil);

	if (ad5807_is_busy(coil))
		return -EBUSY;

	if (coil->work_cmd & (0x1 << 31)) {
		rval = ad5807_write(coil, AD5807_REG_SHUTTER_CONF,
				    AD5807_SHUTTER_CONF_HIZ);
		if (rval)
			return rval;
	}

	if (iris == AD5807_IRIS_F48)
		cmd1 = (open) ? AD5807_SHUTTER_CONF_F48_OPEN :
				AD5807_SHUTTER_CONF_F48_CLOSE;
	else if (iris == AD5807_IRIS_F32)
		cmd1 = (open) ? AD5807_SHUTTER_CONF_F32_OPEN :
				AD5807_SHUTTER_CONF_F32_CLOSE;
	else
		return -EINVAL;

	cmd2 = AD5807_SHUTTER_ACTIVATE |
	       AD5807_SHUTTER_DELAY_DISABLE |
	       AD5807_SHUTTER_SHONTIME_REG;

	rval = ad5807_write(coil, AD5807_REG_SHUTTER_CONF, cmd1);
	if (rval)
		return rval;

	rval = ad5807_write(coil, AD5807_REG_SHUTTER, cmd2);
	if (rval)
		return rval;

	coil->work_cmd |= AD5807_WORK_CMD_MECHA_DRIVE_STOP;
	if (coil->opda_mode == AD5807_OPDA_MODE_I2C)
		schedule_delayed_work(&coil->ad5807_work,
			      msecs_to_jiffies(AD5807_MECHA_DRIVE_TIME_MS));

	return rval;
}

/**
 * ad5807_measure_pos:
 *   Measures the Lens Position using the Position Measurement
 *   circuitry in the Swordfish Modules (ES2.0)
 * NOTE: it is not known if future silicon version will have this circuitry.
 */
static int ad5807_measure_pos(struct ad5807_device *coil, u16 *pos)
{
	int i, rval;
	u8 p_lo, p_hi;

	if (ad5807_is_busy(coil))
		return -EBUSY;

	/* Measure position command*/
	rval = ad5807_write(coil, AD5807_REG_ACTIVE, AD5807_ACTIVE_CMD_MEASPOS);
	if (rval)
		return rval;

	i = 100;
	do {	/* Typical Loops: 4 */
		if (--i == 0)
			goto out;
	} while (ad5807_is_busy(coil));

	/* Read position */
	rval = ad5807_read(coil, AD5807_REG_POS_MEAS_MSB, &p_hi);
	if (rval)
		return rval;

	rval = ad5807_read(coil, AD5807_REG_POS_MEAS_LSB, &p_lo);
	if (rval)
		return rval;

	*pos = (p_hi << 8) | p_lo;
	return 0;
out:
	return -EBUSY;
}

/**
 * ad5807_move_lens_pwm:
 *   This is where the lens move is *actually* programmed.
 *   Moves lens by programming 'pwm_pulses' in the Pattern Generator
 *   Direction of movement is 'dir' as sent along with MOVE cmd.
 *   Start of the Lens movement is decided by the OPDA mode
 */
static int ad5807_move_lens_pwm(struct ad5807_device *coil,
				u16 pwm_pulses, u8 dir, u8 sync)
{
	int rval;

	/**
	 * Check and wait on any ongoing mechanical drive
	 */
	ad5807_wait_on_mecha_drive(coil);

	if (ad5807_is_busy(coil))
		return -EBUSY;

	/**
	 * Write the pwm_pulses
	 */
	rval = ad5807_write(coil, AD5807_REG_PULSEOUT_MSB, pwm_pulses >> 8);
	if (rval)
		return rval;

	rval = ad5807_write(coil, AD5807_REG_PULSEOUT_LSB, pwm_pulses & 0xFF);
	if (rval)
		return rval;

	/**
	 * Write Move lens command
	 *  - if OPDA == STROBE, the lens will move on rising edge of strobe
	 *  - if OPDA == I2C, the lens will start moving immediately
	 */
	rval = ad5807_write(coil, AD5807_REG_ACTIVE,
			    AD5807_ACTIVE_CMD_MOVE |
			    ((dir & 0x1) << AD5807_ACTIVE_DIR_BIT));
	if (rval || coil->opda_mode == AD5807_OPDA_MODE_STROBE)
		return rval;

	/**
	 * We can block only if opda_mode == AD5807_OPDA_MODE_I2C
	 */
	if (sync) {
		/* Typical Loops: dir a: ~150 from infinity to macro */
		/*                dir b: ~133 from macro to infinity */
		int i = 1000;
		do {
			if (--i == 0) {
				rval = -EBUSY;
				goto out;
			}
		} while (ad5807_is_busy(coil));
	}
out:
	return rval;
}

/**
 * ad5807_move_lens_um:
 *   Moves lens a distance of 'um' micro meters in direction 'dir'
 *   ad5807_move_lens_pwm is used to program the chip
 */
static int ad5807_move_lens_um(struct ad5807_device *coil, u16 um, u8 dir,
				u8 sync)
{
	struct i2c_client *client = v4l2_get_subdevdata(&coil->subdev);
	u16 pwm_pulses;
	int rval;

	if (um > AD5807_MOVE_LENS_UM_MAX)
		um = AD5807_MOVE_LENS_UM_MAX;

	pwm_pulses = (um * ((dir) ? coil->parms.plsnum_dira : \
				    coil->parms.plsnum_dirb));

	rval = ad5807_move_lens_pwm(coil, pwm_pulses, dir, sync);
	if (rval) {
		dev_err(&client->dev, "lens move error\n");
		return rval;
	}

	return 0;
}

/**
 * ad5807_set_position:
 *   Moves lens to a 'new_pos' with reference to 'current_pos' as measured
 *   from the Position Sensor in the Swordfish module. 'new_pos' and
 *   'current_pos' are values read from the 10bit ADC on the Position Sensor.
 *
 * NOTE: requires Position Sensor to be present in the Swordfish module.
 */
static int ad5807_set_position(struct ad5807_device *coil, u16 new_pos)
{
	struct i2c_client *client = v4l2_get_subdevdata(&coil->subdev);
	u16 step, prev_pos, pwm_pulses;
	int rval;
	u32 um;
	u8 dir;

	prev_pos = coil->current_pos;
	new_pos = clamp(new_pos,
			coil->parms.p_far,		/* Low Value */
			coil->parms.p_near);		/* High Value */

	if (new_pos < prev_pos) {
		dir = DIRB_TOWARDS_FAR;
		step = prev_pos - new_pos;
		if (step < 2)
			step = 2;
		um = step * coil->parms.umpd[DIRB_TOWARDS_FAR];
	} else if (new_pos > prev_pos) {
		dir = DIRA_TOWARDS_NEAR;
		step = new_pos - prev_pos;
		if (step < 2)
			step = 2;
		um = step * coil->parms.umpd[DIRA_TOWARDS_NEAR];
	} else
		return 0;

	/* coil->parms.umpd is in .8 fixed point */
	pwm_pulses = (um * ((dir) ? coil->parms.plsnum_dira : \
				    coil->parms.plsnum_dirb)) >> 8;

	rval = ad5807_move_lens_pwm(coil, pwm_pulses, dir,
				    coil->move_lens_sync);
	if (rval) {
		dev_err(&client->dev, "lens move error\n");
		return rval;
	}

	/* Sync with the actual lens position is done @ g_ctrl and power off */
	coil->current_pos = new_pos;
	return 0;
}

/**
 * ad5807_config_params:
 *   Parsing as per Swordfish ES2.0/ES2.1 OTP MEM map.
 *   Unfortunately, the p_infinity and p_10cm read from OTP do not
 *   seem to correspond to the sharpest focus points for the lens.
 *   But till an autofocus algo is running in the user space,
 *   they are crudely accurate.
 */
static int ad5807_config_params(struct ad5807_device *coil,
				struct ad5807_parms *params)
{
	struct v4l2_ctrl *ctrl;
	u16 pos;

	coil->parms.plsnum_dira = params->plsnum_dira;
	coil->parms.plsnum_dirb = params->plsnum_dirb;
	coil->parms.p_near	= params->p_near;
	coil->parms.p_10cm	= params->p_10cm;
	coil->parms.p_infinity	= params->p_infinity;
	coil->parms.p_far	= params->p_far;

	coil->parms.period	= params->period;
	coil->parms.tah		= params->tah;
	coil->parms.tal		= params->tal;
	coil->parms.tbh		= params->tbh;
	coil->parms.tbl		= params->tbl;

	ad5807_regs_defaults[AD5807_REG_PWM_PERIOD] = coil->parms.period;
	ad5807_regs_defaults[AD5807_REG_PWM_TAH] = coil->parms.tah;
	ad5807_regs_defaults[AD5807_REG_PWM_TAL] = coil->parms.tal;
	ad5807_regs_defaults[AD5807_REG_PWM_TBH] = coil->parms.tbh;
	ad5807_regs_defaults[AD5807_REG_PWM_TBL] = coil->parms.tbl;

	coil->parms.plsn_far2inf    = params->plsn_far2inf;
	coil->parms.plsn_inf2macro  = params->plsn_inf2macro;
	coil->parms.plsn_macro2near = params->plsn_macro2near;
	coil->parms.plsn_near2macro = params->plsn_near2macro;
	coil->parms.plsn_macro2inf  = params->plsn_macro2inf;
	coil->parms.plsn_inf2far    = params->plsn_inf2far;

	ctrl = v4l2_ctrl_find(&coil->ctrls, CTRL_FOCUS_ABSOLUTE);
	ctrl->minimum = coil->parms.p_far;
	ctrl->maximum = coil->parms.p_near;
	ctrl->default_value = coil->parms.p_infinity;

	pos = coil->parms.p_near - coil->parms.p_far;

	ctrl = v4l2_ctrl_find(&coil->ctrls, CTRL_FOCUS_RELATIVE);
	ctrl->minimum = -pos;
	ctrl->maximum = pos;

	return 0;
}

/**
 * Lens Calibration stuff:
 *   tries build a relation between the number of micro meters lens
 *   has to be moved to change the ADC value in the position
 *   measurement circuitry by One digit.
 *   This relation is inversely used to move the lens when movement
 *   is specified in terms of the ADC values
 * NOTE: Position Sensor circuitry required
 */

#define AD5807_CALIB_LENGTH		500
#define AD5807_CALIB_STEP		10
#define AD5807_CALIB_MIN_POS_DIFF	2
static int ad5807_run_loop(struct ad5807_device *coil, u8 dir, u32 *umpd)
{
	int i, rval;
	u16 pos, pos_p;
	u32 adc_t, n;

	rval = ad5807_measure_pos(coil, &pos_p);
	if (rval)
		goto fail;

	/* Run 10 um steps for AD5807_CALIB_LENGTH um in dir */
	pos = adc_t = n = 0;
	for (i = 0; i < AD5807_CALIB_LENGTH; i += AD5807_CALIB_STEP) {
		int d;
		rval = ad5807_move_lens_um(coil, AD5807_CALIB_STEP, dir, 1);
		if (rval)
			goto fail;
		rval = ad5807_measure_pos(coil, &pos);
		if (rval)
			goto fail;
		d = abs(pos - pos_p);
		if (d <= AD5807_CALIB_MIN_POS_DIFF) {
			pos_p = pos;
			continue;
		}
		pos_p = pos;
		adc_t += d;
		n++;
	}

	/* .8 fixed point */
	*umpd = (AD5807_CALIB_STEP << 16)/((adc_t << 8)/n);
	return 0;

fail:
	return rval;
}

static int ad5807_calibrate_um_per_adc_digit(struct ad5807_device *coil)
{
	u16 step, dir, rval;
	u32 umpd;

	rval = ad5807_measure_pos(coil, &coil->current_pos);
	if (rval)
		return rval;

	/* Drive towards the nearest End first */
	if ((coil->parms.p_near - coil->current_pos) < \
	    (coil->current_pos - coil->parms.p_far)) {
		step = coil->parms.p_near - coil->current_pos;
		dir = DIRA_TOWARDS_NEAR;
	} else {
		step = coil->current_pos - coil->parms.p_far;
		dir = DIRB_TOWARDS_FAR;
	}
	/* rough approx 2um/digit */
	ad5807_move_lens_um(coil, step * 2, dir, 1);

	/* Run loop in 1 - dir */
	rval = ad5807_run_loop(coil, 1 - dir, &umpd);
	if (rval)
		goto out;
	coil->parms.umpd[1 - dir] = umpd;

	/* Run loop in dir */
	rval = ad5807_run_loop(coil, dir, &umpd);
	if (rval)
		goto out;
	coil->parms.umpd[dir] = umpd;

	ad5807_move_lens_um(coil, AD5807_CALIB_LENGTH/2, 1 - dir, 1);
	return 0;
out:
	return rval;
}

static int ad5807_calibrate(struct ad5807_device *coil)
{
	int rval = 0;
	u16 pos = 0;

	rval = ad5807_calibrate_um_per_adc_digit(coil);
	if (rval)
		return rval;

	ad5807_measure_pos(coil, &pos);
	coil->current_pos = pos;

	return 0;
}

static int ad5807_init_regs(struct ad5807_device *coil)
{
	struct i2c_client *client = v4l2_get_subdevdata(&coil->subdev);
	int i, rval;

	for (i = 1; i < AD5807_NUM_REGS; i++) {
		rval = ad5807_write(coil, i, ad5807_regs_defaults[i]);
		if (rval) {
			dev_err(&client->dev,
				"failed writing init registers\n");
			return rval;
		}
	}

	coil->opda_mode = AD5807_OPDA_MODE_I2C;
	coil->opd_mode = AD5807_OPD_MODE_SS;

	return 0;
}

/* -----------------------------------------------------------------------------
 * Power management
 */

static int ad5807_power_on(struct ad5807_device *coil)
{
	unsigned char v;
	int rval;

	rval = regulator_enable(coil->vana);
	if (rval)
		return rval;

	rval = coil->platform_data->set_xshutdown(&coil->subdev, 1);
	if (rval)
		return rval;

	rval = coil->platform_data->set_xclk(&coil->subdev, AD5807_EXTCLK);
	if (rval)
		return rval;

	/* Ignore i2c timeout on first read */
	/* omap i2c driver bug */
	ad5807_read(coil, AD5807_REG_VERSION, &v);

	return ad5807_init_regs(coil);
}

static void ad5807_power_off(struct ad5807_device *coil)
{
	coil->platform_data->set_xshutdown(&coil->subdev, 0);
	coil->platform_data->set_xclk(&coil->subdev, 0);
	regulator_disable(coil->vana);
}

static int __ad5807_set_power(struct ad5807_device *coil, int on)
{
	u16 pos = 0;
	int ret = 0;

	if (on) {
		if (!coil->power_state) {
			/* If the device wasn't powered on, just power it on
			 * and bail out.
			 */
			ret = ad5807_power_on(coil);
			if (!ret)
				coil->power_state = on;
		} else {
			/* If it was already powered on, a delayed power off
			 * might be in progress. Clear the power off command to
			 * avoid surprises.
			 */
			coil->work_cmd &= ~AD5807_WORK_CMD_POWER_OFF;
		}
	} else if (coil->power_state) {
		/* Try to re-sync with _actual_ lens position. */
		ad5807_measure_pos(coil, &pos);
		if (pos)
			coil->position_sensor = pos;

		if (coil->opda_mode == AD5807_OPDA_MODE_I2C &&
		    IS_MECHA_DRIVE_ONGOING) {
			/* Do a delayed power off, handled by ad5807_work. */
			coil->work_cmd |= AD5807_WORK_CMD_POWER_OFF;
		} else {
			ad5807_power_off(coil);
			coil->power_state = 0;
			coil->work_cmd = 0;
		}
	}

	return ret;
}

/* -----------------------------------------------------------------------------
 * V4L2 controls
 */

static int ad5807_get_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ad5807_device *coil =
		container_of(ctrl->handler, struct ad5807_device, ctrls);
	int rval;
	u16 pos = 0;

	switch (ctrl->id) {
	case V4L2_CID_FOCUS_ABSOLUTE:
		if (!coil->power_state)
			break;

		rval = ad5807_measure_pos(coil, &pos);
		if (rval)
			return rval;

		ctrl->cur.val = coil->current_pos = pos;
		break;
	}

	return 0;
}

static int ad5807_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ad5807_device *coil =
		container_of(ctrl->handler, struct ad5807_device, ctrls);
	int rval = 0;
	u8 dir;

	switch (ctrl->id) {
	case V4L2_CID_FOCUS_ABSOLUTE:
		if (coil->current_pos == ctrl->val)
			return 0;

		if (!coil->power_state)
			return 0;

		rval = ad5807_set_position(coil, ctrl->val);
		break;
	case V4L2_CID_FOCUS_RELATIVE:
	{
		/**
		 * As per V4L2 specs, +ive values move towards near
		 * and -ive values move towards far, which suits us fine.
		 */
		int new_pos = coil->current_pos + ctrl->val;

		if (!coil->power_state)
			return 0;

		if (new_pos < 0)
			new_pos = 0;

		rval = ad5807_set_position(coil, new_pos);
		break;
	}
	case V4L2_CID_AD5807_MOVE_LENS_UM:
		if (ctrl->val > 0)
			dir = DIRA_TOWARDS_NEAR;
		else
			dir = DIRB_TOWARDS_FAR;

		return ad5807_move_lens_um(coil, abs(ctrl->val), dir,
			coil->move_lens_sync);
	case V4L2_CID_AD5807_MOVE_LENS_PWM_PULSES:
		if (ctrl->val > 0)
			dir = DIRA_TOWARDS_NEAR;
		else
			dir = DIRB_TOWARDS_FAR;

		return ad5807_move_lens_pwm(coil, abs(ctrl->val),
			dir, coil->move_lens_sync);
	case V4L2_CID_AD5807_MOVE_LENS_BLOCKING:
		coil->move_lens_sync = ctrl->val;
		break;
	case V4L2_CID_AD5807_LENS_CALIBRATE:
		return ad5807_calibrate(coil);
	case V4L2_CID_AD5807_SHUTTER:
		return ad5807_shutter_control(coil, ctrl->val);
	case V4L2_CID_AD5807_IRIS_F48:
		return ad5807_iris_control(coil, AD5807_IRIS_F48, ctrl->val);
	case V4L2_CID_AD5807_IRIS_F32:
		return ad5807_iris_control(coil, AD5807_IRIS_F32, ctrl->val);
	case V4L2_CID_AD5807_OPDA_MODE:
		return ad5807_opda_control(coil, ctrl->val);
	case V4L2_CID_AD5807_OPD_MODE:
		return ad5807_opd_control(coil, ctrl->val);
	default:
		return -EINVAL;
	}

	return rval;
}

static const struct v4l2_ctrl_ops ad5807_ctrl_ops = {
	.g_volatile_ctrl = ad5807_get_ctrl,
	.s_ctrl = ad5807_set_ctrl,
};

static const char * const ad5807_opda_mode_menu[] = {
	"Drive by I2C cmd",
	"Drive by Strobe input",
};

static const char * const ad5807_opd_mode_menu[] = {
	"Single Shot Drive: I2C and Strobe",
	"Multi Shot Drive:  Strobe ONLY",
	"Continuous Drive:  I2C ONLY",
};

static const struct v4l2_ctrl_config ad5807_ctrls[] = {
	{
		.ops		= &ad5807_ctrl_ops,
		.id		= V4L2_CID_FOCUS_ABSOLUTE,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Absolute Focus",
		.min		= 0,
		.max		= 1023,
		.step		= 1,
		.def		= 0,
		.is_volatile	= 1,
	},
	{
		.ops		= &ad5807_ctrl_ops,
		.id		= V4L2_CID_FOCUS_RELATIVE,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Relative Focus",
		.min		= -1023,
		.max		= 1023,
		.step		= 1,
		.def		= 0,
	},
	{
		.ops		= &ad5807_ctrl_ops,
		.id		= V4L2_CID_AD5807_LENS_CALIBRATE,
		.type		= V4L2_CTRL_TYPE_BUTTON,
		.name		= "Lens Calibration",
		.min		= 0,
		.max		= 1,
		.step		= 1,
		.def		= 0,
	},
	{
		.ops		= &ad5807_ctrl_ops,
		.id		= V4L2_CID_AD5807_SHUTTER,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Lens Shutter",
		.min		= 0,
		.max		= 1,
		.step		= 1,
		.def		= 0,
	},
	{
		.ops		= &ad5807_ctrl_ops,
		.id		= V4L2_CID_AD5807_IRIS_F48,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Lens Iris F4.8",
		.min		= 0,
		.max		= 1,
		.step		= 1,
		.def		= 0,
	},
	{
		.ops		= &ad5807_ctrl_ops,
		.id		= V4L2_CID_AD5807_IRIS_F32,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Lens Iris F3.2",
		.min		= 0,
		.max		= 1,
		.step		= 1,
		.def		= 0,
	},
	{
		.ops		= &ad5807_ctrl_ops,
		.id		= V4L2_CID_AD5807_MOVE_LENS_UM,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Move Lens (micro meters)",
		.min		= 1,
		.max		= 1000,
		.step		= 1,
		.def		= 1,
	},
	{
		.ops		= &ad5807_ctrl_ops,
		.id		= V4L2_CID_AD5807_MOVE_LENS_PWM_PULSES,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Move Lens (pwm pulses)",
		.min		= 1,
		.max		= 65536,
		.step		= 1,
		.def		= 1,
	},
	{
		.ops		= &ad5807_ctrl_ops,
		.id		= V4L2_CID_AD5807_MOVE_LENS_BLOCKING,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Lens Move Blocking",
		.min		= 0,
		.max		= 1,
		.step		= 1,
		.def		= 0,
	},
	{
		.ops		= &ad5807_ctrl_ops,
		.id		= V4L2_CID_AD5807_OPDA_MODE,
		.type		= V4L2_CTRL_TYPE_MENU,
		.name		= "Lens OPDA Mode",
		.min		= AD5807_OPDA_MODE_I2C,
		.max		= AD5807_OPDA_MODE_STROBE,
		.step		= 0,
		.def		= AD5807_OPDA_MODE_I2C,
		.qmenu		= ad5807_opda_mode_menu,
	},
	{
		.ops		= &ad5807_ctrl_ops,
		.id		= V4L2_CID_AD5807_OPD_MODE,
		.type		= V4L2_CTRL_TYPE_MENU,
		.name		= "Lens OPD Mode",
		.min		= AD5807_OPD_MODE_SS,
		.max		= AD5807_OPD_MODE_CD,
		.step		= 0,
		.def		= AD5807_OPD_MODE_SS,
		.qmenu		= ad5807_opd_mode_menu,
	},
};

/* -----------------------------------------------------------------------------
 * V4L2 subdev operations
 */

static int ad5807_set_power(struct v4l2_subdev *subdev, int on)
{
	struct ad5807_device *coil = to_ad5807_device(subdev);
	int ret = 0;

	mutex_lock(&coil->mutex);

	/* If the power count is modified from 0 to != 0 or from != 0 to 0,
	 * update the power state.
	 */
	if (coil->power_count == !on) {
		ret = __ad5807_set_power(coil, on);
		if (ret < 0)
			goto done;
	}

	/* Update the power count. */
	coil->power_count += on ? 1 : -1;
	WARN_ON(coil->power_count < 0);

done:
	mutex_unlock(&coil->mutex);
	return ret;
}

static int ad5807_registered(struct v4l2_subdev *subdev)
{
	struct ad5807_device *coil = to_ad5807_device(subdev);
	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	int rval = 0;
	u16 pos = 0;

	coil->vana = regulator_get(&client->dev, "VANA");
	if (IS_ERR(coil->vana)) {
		dev_err(&client->dev, "could not get regulator for vana\n");
		return -ENODEV;
	}

	rval = ad5807_power_on(coil);
	if (rval)
		goto out_err;

	rval = ad5807_measure_pos(coil, &pos);
	if (rval) {
		dev_err(&client->dev,
			"unable to measure lens position\n");
		coil->position_sensor = 0;
		goto out_err;	/* TODO */
	}
	coil->current_pos = pos;
	coil->position_sensor = 1;

	INIT_DELAYED_WORK(&coil->ad5807_work, ad5807_work);
	init_completion(&coil->md_completion);

	/*
	 * FIXME:
	 * These values are base on experimentation on ES2.0.
	 * If we run the calibration loop here, it takes approx 1.5 secs
	 * As of now User Space can reques a calibration using
	 * V4L2_CID_AD5807_LENS_CALIBRATE private control
	 */
	coil->parms.umpd[DIRB_TOWARDS_FAR] = 0x01c0;
	coil->parms.umpd[DIRA_TOWARDS_NEAR] = 0x01b0;

	coil->work_cmd = 0;
	coil->move_lens_sync = 0;

	ad5807_config_params(coil, &ad5807_params_defaults);
	coil->dev_init_done = true;
	ad5807_power_off(coil);
	return 0;

out_err:
	ad5807_power_off(coil);
	regulator_put(coil->vana);
	coil->vana = NULL;
	return rval;
}


/*
 * ad5807_ioctl - AD5807 module private ioctl's
 * @sd: AD5807 V4L2 subdevice
 * @cmd: ioctl command
 * @arg: ioctl argument
 *
 * Return 0 on success or a negative error code otherwise.
 */
static long ad5807_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct ad5807_device *coil = to_ad5807_device(sd);
	int ret;

	switch (cmd) {
	case VIDIOC_AD5807_PARAMS_CFG:
		ret = ad5807_config_params(coil, arg);
		break;

	default:
		return -ENOIOCTLCMD;
	}

	return ret;
}

static int ad5807_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	return ad5807_set_power(sd, 1);
}

static int ad5807_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	return ad5807_set_power(sd, 0);
}

static const struct v4l2_subdev_core_ops ad5807_core_ops = {
	.s_power = ad5807_set_power,
	.ioctl = ad5807_ioctl,
};

static const struct v4l2_subdev_ops ad5807_ops = {
	.core = &ad5807_core_ops,
};

static const struct v4l2_subdev_internal_ops ad5807_internal_ops = {
	.registered = ad5807_registered,
	.open = ad5807_open,
	.close = ad5807_close,
};

/* -----------------------------------------------------------------------------
 * I2C driver
 */
static int ad5807_probe(struct i2c_client *client,
			const struct i2c_device_id *devid)
{
	struct ad5807_device *coil;
	unsigned int i;
	int ret;

	if (client->dev.platform_data == NULL)
		return -ENODEV;

	coil = kzalloc(sizeof(*coil), GFP_KERNEL);
	if (coil == NULL)
		return -ENOMEM;

	coil->platform_data = client->dev.platform_data;
	mutex_init(&coil->mutex);

	v4l2_i2c_subdev_init(&coil->subdev, client, &ad5807_ops);
	coil->subdev.internal_ops = &ad5807_internal_ops;
	coil->subdev.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	v4l2_ctrl_handler_init(&coil->ctrls, ARRAY_SIZE(ad5807_ctrls));
	for (i = 0; i < ARRAY_SIZE(ad5807_ctrls); ++i)
		v4l2_ctrl_new_custom(&coil->ctrls, &ad5807_ctrls[i], NULL);
	coil->subdev.ctrl_handler = &coil->ctrls;

	ret = media_entity_init(&coil->subdev.entity, 0, NULL, 0);
	if (ret < 0)
		kfree(coil);

	return 0;
}

static int __exit ad5807_remove(struct i2c_client *client)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct ad5807_device *coil = to_ad5807_device(subdev);

	if (work_pending(&coil->ad5807_work.work))
		cancel_delayed_work_sync(&coil->ad5807_work);
	if (coil->power_state) {
		coil->platform_data->set_xshutdown(&coil->subdev, 0);
		coil->platform_data->set_xclk(&coil->subdev, 0);
		coil->power_state = 0;
	}

	v4l2_device_unregister_subdev(subdev);
	v4l2_ctrl_handler_free(&coil->ctrls);
	media_entity_cleanup(&coil->subdev.entity);
	if (coil->vana)
		regulator_put(coil->vana);

	kfree(coil);

	return 0;
}

#ifdef CONFIG_PM

/**
 * FIXME:
 *    If we suspend in OPDA_MODE_STROBE, we will resume in OPDA_MODE_I2C.
 *    Strobe mode is linked to et8en2 and we _could_ be missing the
 *    strobe trigger in suspend there. So for now, not restoring the
 *    OPDA Mode here.
 */

static int ad5807_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct ad5807_device *coil = to_ad5807_device(subdev);

	if (coil->power_count == 0)
		return 0;

	/**
	 * Check and wait on any ongoing mechanical drive
	 */
	ad5807_wait_on_mecha_drive(coil);

	ad5807_power_off(coil);

	return 0;
}

static int ad5807_resume(struct i2c_client *client)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct ad5807_device *coil = to_ad5807_device(subdev);

	if (coil->power_count == 0)
		return 0;

	return __ad5807_set_power(coil, 1);
}

#else

#define ad5807_suspend	NULL
#define ad5807_resume	NULL

#endif /* CONFIG_PM */

static const struct i2c_device_id ad5807_id_table[] = {
	{ AD5807_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ad5807_id_table);

static struct i2c_driver ad5807_i2c_driver = {
	.driver		= {
		.name	= AD5807_NAME,
	},
	.probe		= ad5807_probe,
	.remove		= __exit_p(ad5807_remove),
	.suspend	= ad5807_suspend,
	.resume		= ad5807_resume,
	.id_table	= ad5807_id_table,
};

static int __init ad5807_init(void)
{
	int rval;

	rval = i2c_add_driver(&ad5807_i2c_driver);
	if (rval)
		printk(KERN_INFO "%s: failed registering " AD5807_NAME "\n",
		       __func__);

	return rval;
}

static void __exit ad5807_exit(void)
{
	i2c_del_driver(&ad5807_i2c_driver);
}


module_init(ad5807_init);
module_exit(ad5807_exit);

MODULE_AUTHOR("Vimarsh Zutshi <vimarsh.zutshi@nokia.com>");
MODULE_DESCRIPTION("AD5807 actuator driver");
MODULE_LICENSE("GPL");
