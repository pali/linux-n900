/**
 * drivers/media/video/ad5807.h
 *
 * Copyright (C) 2008-2009 Nokia Corporation
 * Copyright (C) 2007 Texas Instruments
 *
 * Contact:
 *
 * Based on ad5820.c by
 *	    Tuukka Toivonen <tuukka.o.toivonen@nokia.com>
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

#ifndef __AD5807_H_
#define __AD5807_H_

#ifdef __KERNEL__

#include <linux/i2c.h>
#include <linux/completion.h>
#include <linux/regulator/consumer.h>

#include <media/v4l2-subdev.h>

#define AD5807_NAME     "ad5807"
#define AD5807_I2C_ADDR (0x1C >> 1)

/**
 * Register definitions
 */
#define AD5807_REG_CONTROL		0x01
#define AD5807_CONTROL_INTERRUPT_BIT	0
#define AD5807_CONTROL_RESET_BIT	1

#define AD5807_REG_CONFIG		0x04
#define AD5807_CONFIG_TRIG_MASK		0x01
#define AD5807_CONFIG_TRIG_STROBE	1
#define AD5807_CONFIG_TRIG_I2C		0

#define AD5807_REG_PWM_PERIOD		0x05
#define AD5807_REG_PWM_TAH		0x06
#define AD5807_REG_PWM_TAL		0x07
#define AD5807_REG_PWM_TBH		0x08
#define AD5807_REG_PWM_TBL		0x09

#define AD5807_REG_DRIVE_CONFIG		0x0C
#define AD5807_DRIVE_CONFIG_CM_DRV_MASK	0x10 /* Continuous or Multi drive */
#define AD5807_DRIVE_CONFIG_CM_DRV_ON	0x10
#define AD5807_DRIVE_CONFIG_CM_DRV_OFF	0x00

#define AD5807_REG_PULSEOUT_MSB		0x0D
#define AD5807_REG_PULSEOUT_LSB		0x0E

#define AD5807_REG_SHUTTER_CONF		0x12
#define AD5807_SHUTTER_CONF_HIZ		0xAA
#define AD5807_SHUTTER_CONF_SHUT_CLOSE	0xA4 /* Shutter */
#define AD5807_SHUTTER_CONF_SHUT_OPEN	0xA1
#define AD5807_SHUTTER_CONF_F48_CLOSE	0x92 /* IRIS F4.8 */
#define AD5807_SHUTTER_CONF_F48_OPEN	0x86
#define AD5807_SHUTTER_CONF_F32_CLOSE	0x4A /* IRIS F3.2 */
#define AD5807_SHUTTER_CONF_F32_OPEN	0x1A

#define AD5807_REG_SHUTTER		0x13
#define AD5807_SHUTTER_ACTIVATE		0x04
#define AD5807_SHUTTER_DEACTIVATE	0x00
#define AD5807_SHUTTER_DELAY_ENABLE	0x02
#define AD5807_SHUTTER_DELAY_DISABLE	0x00
#define AD5807_SHUTTER_SHONTIME_STROBE	0x01
#define AD5807_SHUTTER_SHONTIME_REG	0x00 /* as defined by TIMING register */

#define AD5807_REG_ACTIVE		0x14
#define AD5807_ACTIVE_CMD_MOVE		0x01 /* Move Lens */
#define AD5807_ACTIVE_CMD_MEASPOS	0x04
#define AD5807_ACTIVE_DIR_BIT		1

#define AD5807_REG_POS_MEAS_MSB		0x15
#define AD5807_REG_POS_MEAS_LSB		0x16

#define AD5807_REG_STATUS		0x1B
#define AD5807_STATUS_BUSY_BIT		2

#define AD5807_REG_VERSION		0x1C

/*
 * Duration of the SIN output current pulse is controlled by
 * SHONTIME bits: register 0x03, if 0x13[0] == 0, else by Strobe pulse width
 * For 0x03 == 28
 * Drive Time: (5mS+3mS*(SHONTIME bit4~7))*19.2Mhz/EXT_CLK
 *             (5mS+3mS*2)*19.2Mhz/9.6Mhz
 *             22ms
 */
#define AD5807_MECHA_DRIVE_TIME_PRG_MS		(22)
#define AD5807_MECHA_DRIVE_TIME_ADD_WAIT_MS	(10)
#define AD5807_MECHA_DRIVE_TIME_MS	(AD5807_MECHA_DRIVE_TIME_PRG_MS + \
					AD5807_MECHA_DRIVE_TIME_ADD_WAIT_MS)

enum ad5807_iris_type {
	AD5807_IRIS_F48 = 0,
	AD5807_IRIS_F32,
};

struct ad5807_platform_data {
	int (*set_xclk)(struct v4l2_subdev *sd, u32 hz);
	int (*set_xshutdown)(struct v4l2_subdev *sd, u8 set);
};

/* dir 'a': 1 : Far -> Infinity -> Macro -> Near */
/* dir 'b': 0 : Far <- Infinity <- Macro <- Near */
#define DIRB_TOWARDS_FAR	0	/* Dir 'b' */
#define DIRA_TOWARDS_NEAR	1	/* Dir 'a' */

struct ad5807_parms {
	/* Read from Module's OTP */
#define AD5807_OTP_START	((0x9 * 8) + 5)

	/* PWM pulse numbers for movement in dir a */
	u16 plsn_far2inf;
	u16 plsn_inf2macro;
	u16 plsn_macro2near;

	/* PWM pulse numbers for movement in dir b */
	u16 plsn_near2macro;
	u16 plsn_macro2inf;
	u16 plsn_inf2far;

	u32 otp_wc; 	/* whatever! */

	/* PWM pattern generator duty cycles */
	u8 period;
	u8 tah;
	u8 tal;
	u8 tbh;
	u8 tbl;

	/* PWM pulse number for 1 um movement */
	u16 plsnum_dira;
	u16 plsnum_dirb;

	/* Position sensor ADC values */
	u16 p_near;
	u16 p_10cm;	/* Macro?? */
	u16 p_infinity;
	u16 p_far;

	/* Calculated in the calibration loop */
	/* micro meters per ADC digit: .8 fixed point */
	u32 umpd[2];	/* index: DIRB_TOWARDS_FAR, DIRA_TOWARDS_NEAR */
} __attribute__((packed));

enum ad5807_opda_mode {
	/*
	 * Controls the Output Drive *Activation* mode
	 * for SHUTTER/IRIS and LENS MOVE operations
	 */
	AD5807_OPDA_MODE_I2C = 0,	/* Drive initiated by I2C cmd */
	AD5807_OPDA_MODE_STROBE,	/* Drive initiated by Strobe input */
};

enum ad5807_opd_mode {
	/*
	 * Controls the Output Drive mode for LENS MOVE operation
	 * For SHUTTER/IRIS operation SS/MS/CD are apparently meaningless
	 */
	AD5807_OPD_MODE_SS = 0,	/* Single Shot Drive: I2C and Strobe */
	AD5807_OPD_MODE_MS,	/* Multi Shot Drive:  Strobe ONLY */
	AD5807_OPD_MODE_CD,	/* Continuous Drive:  I2C ONLY */
};

struct ad5807_device {
	struct v4l2_subdev subdev;
	struct ad5807_platform_data *platform_data;
	struct regulator *vana;

	struct v4l2_ctrl_handler ctrls;

	int power_state;
	int power_count;

	enum ad5807_opda_mode opda_mode;
	enum ad5807_opd_mode opd_mode;

	struct ad5807_parms parms;

	int position_sensor;		/* 1 => position sensor present */
	u8 move_lens_sync;

#define AD5807_WORK_CMD_MECHA_DRIVE_STOP	0x01
#define AD5807_WORK_CMD_POWER_OFF		0x02
	struct delayed_work ad5807_work;
	struct mutex mutex;
	u32 work_cmd;
	u32 work_counter;
	struct completion md_completion;

	bool dev_init_done;

	/* [9:0]: current 10 bit ADC value as read by MEASPOS cmd */
	u16 current_pos;
};

#define to_ad5807_device(sd)	container_of(sd, struct ad5807_device, subdev)

#endif /* __KERNEL__ */

#define AD5807_MOVE_LENS_UM_MAX 1000


/* Private IOCTLs */
#define VIDIOC_AD5807_PARAMS_CFG \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 1, struct ad5807_parms)

enum v4l2_ad5807_cids {
	V4L2_CID_AD5807_LENS_CALIBRATE = V4L2_CID_CAMERA_CLASS_BASE + 100,
	V4L2_CID_AD5807_SHUTTER,
	V4L2_CID_AD5807_IRIS_F48,
	V4L2_CID_AD5807_IRIS_F32,

	V4L2_CID_AD5807_MOVE_LENS_UM,
	V4L2_CID_AD5807_MOVE_LENS_PWM_PULSES,
	V4L2_CID_AD5807_MOVE_LENS_BLOCKING,

	V4L2_CID_AD5807_OPDA_MODE,
#define AD5807_MODE_OPDA_I2C		0x0
#define AD5807_MODE_OPDA_STROBE		0x1

	V4L2_CID_AD5807_OPD_MODE,
#define AD5807_MODE_OPD_SS		0x0
#define AD5807_MODE_OPD_MS		0x1
#define AD5807_MODE_OPD_CD		0x2
};

#endif /* __AD5807_H_ */
