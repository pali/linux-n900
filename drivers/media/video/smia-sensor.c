/*
 * drivers/media/video/smia-sensor.c
 *
 * Copyright (C) 2008 Nokia Corporation
 *
 * Contact: Tuukka Toivonen <tuukka.o.toivonen@nokia.com>
 *
 * Based on code from Toni Leinonen <toni.leinonen@offcode.fi>
 *                and Sakari Ailus <sakari.ailus@nokia.com>.
 *
 * This driver is based on the Micron MT9T012 camera imager driver
 * (C) Texas Instruments and Toshiba ET8EK8 driver (C) Nokia.
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
 *
 */

#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/version.h>

#include <media/smiaregs.h>
#include <media/v4l2-int-device.h>

#include "smia-sensor.h"

#define DEFAULT_XCLK		9600000		/* [Hz] */

#define SMIA_CTRL_GAIN		0
#define SMIA_CTRL_EXPOSURE	1
#define SMIA_NCTRLS		2

#define CID_TO_CTRL(id)		((id) == V4L2_CID_GAIN ? SMIA_CTRL_GAIN : \
				 (id) == V4L2_CID_EXPOSURE ? \
					 SMIA_CTRL_EXPOSURE : \
					 -EINVAL)

#define VS6555_RESET_SHIFT_HACK	1

/* Register definitions */

/* Status registers */
#define REG_MODEL_ID		0x0000
#define REG_REVISION_NUMBER	0x0002
#define REG_MANUFACTURER_ID	0x0003
#define REG_SMIA_VERSION	0x0004

/* Exposure time and gain registers */
#define REG_FINE_EXPOSURE	0x0200
#define REG_COARSE_EXPOSURE	0x0202
#define REG_ANALOG_GAIN		0x0204

struct smia_sensor;

struct smia_sensor_type {
	u8 manufacturer_id;
	u16 model_id;
	char *name;
	int ev_table_size;
	u16 *ev_table;
};

/* Current values for V4L2 controls */
struct smia_control {
	s32 minimum;
	s32 maximum;
	s32 step;
	s32 default_value;
	s32 value;
	int (*set)(struct smia_sensor *, s32 value);
};

struct smia_sensor {
	struct i2c_client *i2c_client;
	struct i2c_driver driver;

	/* Sensor information */
	struct smia_sensor_type *type;
	u8  revision_number;
	u8  smia_version;

	/* V4L2 current control values */
	struct smia_control controls[SMIA_NCTRLS];

	struct smia_reglist *current_reglist;
	struct v4l2_int_device *v4l2_int_device;
	struct v4l2_fract timeperframe;

	struct smia_sensor_platform_data *platform_data;

	const struct firmware *fw;
	struct smia_meta_reglist *meta_reglist;

	enum v4l2_power power;
};

static int smia_ioctl_queryctrl(struct v4l2_int_device *s,
				struct v4l2_queryctrl *a);
static int smia_ioctl_g_ctrl(struct v4l2_int_device *s,
			     struct v4l2_control *vc);
static int smia_ioctl_s_ctrl(struct v4l2_int_device *s,
			     struct v4l2_control *vc);
static int smia_ioctl_enum_fmt_cap(struct v4l2_int_device *s,
				   struct v4l2_fmtdesc *fmt);
static int smia_ioctl_g_fmt_cap(struct v4l2_int_device *s,
				struct v4l2_format *f);
static int smia_ioctl_s_fmt_cap(struct v4l2_int_device *s,
				struct v4l2_format *f);
static int smia_ioctl_g_parm(struct v4l2_int_device *s,
			     struct v4l2_streamparm *a);
static int smia_ioctl_s_parm(struct v4l2_int_device *s,
			     struct v4l2_streamparm *a);
static int smia_ioctl_s_power(struct v4l2_int_device *s, enum v4l2_power state);
static int smia_ioctl_g_priv(struct v4l2_int_device *s, void *priv);
static int smia_ioctl_enum_framesizes(struct v4l2_int_device *s,
				      struct v4l2_frmsizeenum *frm);
static int smia_ioctl_enum_frameintervals(struct v4l2_int_device *s,
					  struct v4l2_frmivalenum *frm);
static int smia_ioctl_dev_init(struct v4l2_int_device *s);

/* SMIA-model gain is stored in precalculated tables here. In the model,
 * reg  = (c0-gain*c1) / (gain*m1-m0)
 * gain = 2^ev
 * The constants c0, m0, c1 and m1 depend on sensor.
 */

/* Analog gain table for VS6555.
 * m0   = 0
 * c0   = 256
 * m1   = -1  (erroneously -16 in silicon)
 * c1   = 256
 * step = 16
 */
static u16 smia_gain_vs6555[] = {
/*	reg	   EV    gain     */
	  0,	/* 0.0   1.00000  */
	 16,	/* 0.1   1.07177  */
	 32,	/* 0.2   1.14870  */
	 48,	/* 0.3   1.23114  */
	 64,	/* 0.4   1.31951  */
	 80,	/* 0.5   1.41421  */
	 80,	/* 0.6   1.51572  */
	 96,	/* 0.7   1.62450  */
	112,	/* 0.8   1.74110  */
	112,	/* 0.9   1.86607  */
	128,	/* 1.0   2.00000  */
	144,	/* 1.1   2.14355  */
	144,	/* 1.2   2.29740  */
	160,	/* 1.3   2.46229  */
	160,	/* 1.4   2.63902  */
	160,	/* 1.5   2.82843  */
	176,	/* 1.6   3.03143  */
	176,	/* 1.7   3.24901  */
	176,	/* 1.8   3.48220  */
	192,	/* 1.9   3.73213  */
	192,	/* 2.0   4.00000  */
	192,	/* 2.1   4.28709  */
	208,	/* 2.2   4.59479  */
	208,	/* 2.3   4.92458  */
	208,	/* 2.4   5.27803  */
	208,	/* 2.5   5.65685  */
	208,	/* 2.6   6.06287  */
	224,	/* 2.7   6.49802  */
	224,	/* 2.8   6.96440  */
	224,	/* 2.9   7.46426  */
	224,	/* 3.0   8.00000  */
	224,	/* 3.1   8.57419  */
	224,	/* 3.2   9.18959  */
	224,	/* 3.3   9.84916  */
	224,	/* 3.4  10.55606  */
	240,	/* 3.5  11.31371  */
	240,	/* 3.6  12.12573  */
	240,	/* 3.7  12.99604  */
	240,	/* 3.8  13.92881  */
	240,	/* 3.9  14.92853  */
	240,	/* 4.0  16.00000  */
};

/* Analog gain table for TCM8330MD.
 * m0   = 1
 * c0   = 0
 * m1   = 0
 * c1   = 36 (MMS uses 29)
 * step = 1
 */
static u16 smia_gain_tcm8330md[] = {
/*	reg	   EV      gain     */
	 36,	/* 0.0     1.00000  */
	 39,	/* 0.1     1.07177  */
	 41,	/* 0.2     1.14870  */
	 44,	/* 0.3     1.23114  */
	 48,	/* 0.4     1.31951  */
	 51,	/* 0.5     1.41421  */
	 55,	/* 0.6     1.51572  */
	 58,	/* 0.7     1.62450  */
	 63,	/* 0.8     1.74110  */
	 67,	/* 0.9     1.86607  */
	 72,	/* 1.0     2.00000  */
	 77,	/* 1.1     2.14355  */
	 83,	/* 1.2     2.29740  */
	 89,	/* 1.3     2.46229  */
	 95,	/* 1.4     2.63902  */
	102,	/* 1.5     2.82843  */
	109,	/* 1.6     3.03143  */
	117,	/* 1.7     3.24901  */
	125,	/* 1.8     3.48220  */
	134,	/* 1.9     3.73213  */
	144,	/* 2.0     4.00000  */
	154,	/* 2.1     4.28709  */
	165,	/* 2.2     4.59479  */
	177,	/* 2.3     4.92458  */
	190,	/* 2.4     5.27803  */
	204,	/* 2.5     5.65685  */
	218,	/* 2.6     6.06287  */
	234,	/* 2.7     6.49802  */
	251,	/* 2.8     6.96440  */
	269,	/* 2.9     7.46426  */
	288,	/* 3.0     8.00000  */
};

static struct v4l2_int_ioctl_desc smia_ioctl_desc[] = {
	{ vidioc_int_enum_fmt_cap_num,
	  (v4l2_int_ioctl_func *)smia_ioctl_enum_fmt_cap },
	{ vidioc_int_try_fmt_cap_num,
	  (v4l2_int_ioctl_func *)smia_ioctl_g_fmt_cap },
	{ vidioc_int_g_fmt_cap_num,
	  (v4l2_int_ioctl_func *)smia_ioctl_g_fmt_cap },
	{ vidioc_int_s_fmt_cap_num,
	  (v4l2_int_ioctl_func *)smia_ioctl_s_fmt_cap },
	{ vidioc_int_queryctrl_num,
	  (v4l2_int_ioctl_func *)smia_ioctl_queryctrl },
	{ vidioc_int_g_ctrl_num,
	  (v4l2_int_ioctl_func *)smia_ioctl_g_ctrl },
	{ vidioc_int_s_ctrl_num,
	  (v4l2_int_ioctl_func *)smia_ioctl_s_ctrl },
	{ vidioc_int_g_parm_num,
	  (v4l2_int_ioctl_func *)smia_ioctl_g_parm },
	{ vidioc_int_s_parm_num,
	  (v4l2_int_ioctl_func *)smia_ioctl_s_parm },
	{ vidioc_int_s_power_num,
	  (v4l2_int_ioctl_func *)smia_ioctl_s_power },
	{ vidioc_int_g_priv_num,
	  (v4l2_int_ioctl_func *)smia_ioctl_g_priv },
	{ vidioc_int_enum_framesizes_num,
	  (v4l2_int_ioctl_func *)smia_ioctl_enum_framesizes },
	{ vidioc_int_enum_frameintervals_num,
	  (v4l2_int_ioctl_func *)smia_ioctl_enum_frameintervals },
	{ vidioc_int_dev_init_num,
	  (v4l2_int_ioctl_func *)smia_ioctl_dev_init },
};

static struct v4l2_int_slave smia_slave = {
	.ioctls = smia_ioctl_desc,
	.num_ioctls = ARRAY_SIZE(smia_ioctl_desc),
};

static struct smia_sensor smia;

static struct v4l2_int_device smia_int_device = {
	.module = THIS_MODULE,
	.name = SMIA_SENSOR_NAME,
	.priv = &smia,
	.type = v4l2_int_type_slave,
	.u = {
		.slave = &smia_slave,
	},
};

static struct smia_sensor_type smia_sensors[] = {
	{ 0, 0, "unknown", 0, NULL },
	{
		0x01, 0x022b, "vs6555",
		ARRAY_SIZE(smia_gain_vs6555), smia_gain_vs6555
	},
	{
		0x0c, 0x208a, "tcm8330md",
		ARRAY_SIZE(smia_gain_tcm8330md), smia_gain_tcm8330md
	},
};

static const __u32 smia_mode_ctrls[] = {
	V4L2_CID_MODE_FRAME_WIDTH,
	V4L2_CID_MODE_FRAME_HEIGHT,
	V4L2_CID_MODE_VISIBLE_WIDTH,
	V4L2_CID_MODE_VISIBLE_HEIGHT,
	V4L2_CID_MODE_PIXELCLOCK,
	V4L2_CID_MODE_SENSITIVITY,
};

/* Return time of one row in microseconds, .8 fixed point format.
 * If the sensor is not set to any mode, return zero. */
static int smia_get_row_time(struct smia_sensor *sensor)
{
	unsigned int clock;	/* Pixel clock in Hz>>10 fixed point */
	unsigned int rt;	/* Row time in .8 fixed point */

	if (!sensor->current_reglist)
		return 0;

	clock = sensor->current_reglist->mode.pixel_clock;
	clock = (clock + (1 << 9)) >> 10;
	rt = sensor->current_reglist->mode.width * (1000000 >> 2);
	rt = (rt + (clock >> 1)) / clock;

	return rt;
}

/* Convert exposure time `us' to rows. Modify `us' to make it to
 * correspond to the actual exposure time.
 */
static int smia_exposure_us_to_rows(struct smia_sensor *sensor, s32 *us)
{
	unsigned int rows;	/* Exposure value as written to HW (ie. rows) */
	unsigned int rt;	/* Row time in .8 fixed point */

	if (*us < 0)
		*us = 0;

	/* Assume that the maximum exposure time is at most ~8 s,
	 * and the maximum width (with blanking) ~8000 pixels.
	 * The formula here is in principle as simple as
	 *    rows = exptime / 1e6 / width * pixel_clock
	 * but to get accurate results while coping with value ranges,
	 * have to do some fixed point math.
	 */

	rt = smia_get_row_time(sensor);
	rows = ((*us << 8) + (rt >> 1)) / rt;

	if (rows > sensor->current_reglist->mode.max_exp)
		rows = sensor->current_reglist->mode.max_exp;

	/* Set the exposure time to the rounded value */
	*us = (rt * rows + (1 << 7)) >> 8;

	return rows;
}

/* Convert exposure time in rows to microseconds */
static int smia_exposure_rows_to_us(struct smia_sensor *sensor, int rows)
{
	return (smia_get_row_time(sensor) * rows + (1 << 7)) >> 8;
}

/* Called to change the V4L2 gain control value. This function
 * rounds and clamps the given value and updates the V4L2 control value.
 * If power is on, also updates the sensor analog gain.
 */
static int smia_set_gain(struct smia_sensor *sensor, s32 gain)
{
	gain = clamp(gain,
		sensor->controls[SMIA_CTRL_GAIN].minimum,
		sensor->controls[SMIA_CTRL_GAIN].maximum);
	sensor->controls[SMIA_CTRL_GAIN].value = gain;

	if (sensor->power == V4L2_POWER_OFF)
		return 0;

	return smia_i2c_write_reg(sensor->i2c_client,
				  SMIA_REG_16BIT, REG_ANALOG_GAIN,
				  sensor->type->ev_table[gain]);
}

/* Called to change the V4L2 exposure control value. This function
 * rounds and clamps the given value and updates the V4L2 control value.
 * If power is on, also update the sensor exposure time.
 * exptime is in microseconds.
 */
static int smia_set_exposure(struct smia_sensor *sensor, s32 exptime)
{
	int exposure_rows;

	exptime = clamp(exptime, sensor->controls[SMIA_CTRL_EXPOSURE].minimum,
				 sensor->controls[SMIA_CTRL_EXPOSURE].maximum);

	exposure_rows = smia_exposure_us_to_rows(sensor, &exptime);
	sensor->controls[SMIA_CTRL_EXPOSURE].value = exptime;

	if (sensor->power == V4L2_POWER_OFF)
		return 0;

	return smia_i2c_write_reg(sensor->i2c_client,
			SMIA_REG_16BIT, REG_COARSE_EXPOSURE, exposure_rows);
}

static int smia_stream_on(struct v4l2_int_device *s)
{
	struct smia_sensor *sensor = s->priv;
	return smia_i2c_write_reg(sensor->i2c_client,
				  SMIA_REG_8BIT, 0x0100, 0x01);
}

static int smia_stream_off(struct v4l2_int_device *s)
{
	struct smia_sensor *sensor = s->priv;
	return smia_i2c_write_reg(sensor->i2c_client,
				  SMIA_REG_8BIT, 0x0100, 0x00);
}

static int smia_update_controls(struct v4l2_int_device *s)
{
	struct smia_sensor *sensor = s->priv;
	int i;

	sensor->controls[SMIA_CTRL_EXPOSURE].minimum = 0;
	sensor->controls[SMIA_CTRL_EXPOSURE].maximum =
		smia_exposure_rows_to_us(sensor,
					 sensor->current_reglist->mode.max_exp);
	sensor->controls[SMIA_CTRL_EXPOSURE].step =
		smia_exposure_rows_to_us(sensor, 1);
	sensor->controls[SMIA_CTRL_EXPOSURE].default_value =
		sensor->controls[SMIA_CTRL_EXPOSURE].maximum;
	if (sensor->controls[SMIA_CTRL_EXPOSURE].value == 0)
		sensor->controls[SMIA_CTRL_EXPOSURE].value =
			sensor->controls[SMIA_CTRL_EXPOSURE].maximum;

	/* Adjust V4L2 control values and write them to the sensor */

	for (i = 0; i < ARRAY_SIZE(sensor->controls); i++) {
		int rval;
		if (!sensor->controls[i].set)
			continue;
		rval = sensor->controls[i].set(sensor,
			sensor->controls[i].value);
		if (rval)
			return rval;
	}
	return 0;
}

/* Must be called with power already enabled on the sensor */
static int smia_configure(struct v4l2_int_device *s)
{
	struct smia_sensor *sensor = s->priv;
	int rval;

	rval = smia_i2c_write_regs(sensor->i2c_client,
				   sensor->current_reglist->regs);
	if (rval)
		goto fail;

	/*
	 * FIXME: remove stream_off from here as soon as camera-firmware
	 * is modified to not enable streaming automatically.
	 */
	rval = smia_stream_off(s);
	if (rval)
		goto fail;

	rval = smia_update_controls(s);
	if (rval)
		goto fail;

	rval = sensor->platform_data->configure_interface(
		s,
		sensor->current_reglist->mode.window_width,
		sensor->current_reglist->mode.window_height);
	if (rval)
		goto fail;

	return 0;

fail:
	dev_err(&sensor->i2c_client->dev, "sensor configuration failed\n");
	return rval;

}

static int smia_power_off(struct v4l2_int_device *s)
{
	struct smia_sensor *sensor = s->priv;
	int rval;

	rval = sensor->platform_data->set_xclk(s, 0);
	if (rval)
		return rval;

	return sensor->platform_data->power_off(s);
}

static int smia_power_on(struct v4l2_int_device *s)
{
	struct smia_sensor *sensor = s->priv;
	struct smia_reglist *reglist = NULL;
	int rval;
	unsigned int hz = DEFAULT_XCLK;

	if (sensor->meta_reglist) {
		reglist = smia_reglist_find_type(sensor->meta_reglist,
						 SMIA_REGLIST_POWERON);
		hz = reglist->mode.ext_clock;
	}

	rval = sensor->platform_data->power_on(s);
	if (rval)
		goto out;

	sensor->platform_data->set_xclk(s, hz);

	/*
	 * At least 10 ms is required between xshutdown up and first
	 * i2c transaction. Clock must start at least 2400 cycles
	 * before first i2c transaction.
	 */
	msleep(10);

	if (reglist) {
		rval = smia_i2c_write_regs(sensor->i2c_client,
					   reglist->regs);
		if (rval)
			goto out;
	}

out:
	if (rval)
		smia_power_off(s);

	return rval;
}

static struct v4l2_queryctrl smia_ctrls[] = {
	{
		.id		= V4L2_CID_GAIN,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Analog gain [0.1 EV]",
		.flags		= V4L2_CTRL_FLAG_SLIDER,
	},
	{
		.id		= V4L2_CID_EXPOSURE,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Exposure time [us]",
		.flags		= V4L2_CTRL_FLAG_SLIDER,
	},
};

static int smia_ioctl_queryctrl(struct v4l2_int_device *s,
			   struct v4l2_queryctrl *a)
{
	struct smia_sensor *sensor = s->priv;
	int rval, ctrl;

	rval = smia_ctrl_query(smia_ctrls, ARRAY_SIZE(smia_ctrls), a);
	if (rval) {
		return smia_mode_query(smia_mode_ctrls,
					ARRAY_SIZE(smia_mode_ctrls), a);
	}

	ctrl = CID_TO_CTRL(a->id);
	if (ctrl < 0)
		return ctrl;
	if (!sensor->controls[ctrl].set)
		return -EINVAL;

	a->minimum       = sensor->controls[ctrl].minimum;
	a->maximum       = sensor->controls[ctrl].maximum;
	a->step          = sensor->controls[ctrl].step;
	a->default_value = sensor->controls[ctrl].default_value;

	return 0;
}

static int smia_ioctl_g_ctrl(struct v4l2_int_device *s,
			struct v4l2_control *vc)
{
	struct smia_sensor *sensor = s->priv;
	int ctrl;

	int rval = smia_mode_g_ctrl(smia_mode_ctrls,
			ARRAY_SIZE(smia_mode_ctrls),
			vc, &sensor->current_reglist->mode);
	if (rval == 0)
		return 0;

	ctrl = CID_TO_CTRL(vc->id);
	if (ctrl < 0)
		return ctrl;
	if (!sensor->controls[ctrl].set)
		return -EINVAL;
	vc->value = sensor->controls[ctrl].value;

	return 0;
}

static int smia_ioctl_s_ctrl(struct v4l2_int_device *s,
			struct v4l2_control *vc)
{
	struct smia_sensor *sensor = s->priv;

	int ctrl = CID_TO_CTRL(vc->id);
	if (ctrl < 0)
		return ctrl;
	if (!sensor->controls[ctrl].set)
		return -EINVAL;
	return sensor->controls[ctrl].set(sensor, vc->value);
}

static int smia_ioctl_enum_fmt_cap(struct v4l2_int_device *s,
			      struct v4l2_fmtdesc *fmt)
{
	struct smia_sensor *sensor = s->priv;
	return smia_reglist_enum_fmt(sensor->meta_reglist, fmt);
}

static int smia_ioctl_g_fmt_cap(struct v4l2_int_device *s,
			   struct v4l2_format *f)
{
	struct smia_sensor *sensor = s->priv;
	struct v4l2_pix_format *pix = &f->fmt.pix;

	pix->width = sensor->current_reglist->mode.window_width;
	pix->height = sensor->current_reglist->mode.window_height;
	pix->pixelformat = sensor->current_reglist->mode.pixel_format;

	return 0;
}

static int smia_ioctl_s_fmt_cap(struct v4l2_int_device *s,
			   struct v4l2_format *f)
{
	struct smia_sensor *sensor = s->priv;
	struct smia_reglist *reglist;

	reglist = smia_reglist_find_mode_fmt(sensor->meta_reglist,
					     sensor->current_reglist, f);
	if (!reglist)
		return -EINVAL;
	sensor->current_reglist = reglist;
	return smia_update_controls(s);
}

static int smia_ioctl_g_parm(struct v4l2_int_device *s,
			struct v4l2_streamparm *a)
{
	struct smia_sensor *sensor = s->priv;
	struct v4l2_captureparm *cparm = &a->parm.capture;

	if (a->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	memset(a, 0, sizeof(*a));
	a->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	cparm->capability = V4L2_CAP_TIMEPERFRAME;
	cparm->timeperframe = sensor->current_reglist->mode.timeperframe;

	return 0;
}

static int smia_ioctl_s_parm(struct v4l2_int_device *s,
			struct v4l2_streamparm *a)
{
	struct smia_sensor *sensor = s->priv;
	struct smia_reglist *reglist;

	reglist = smia_reglist_find_mode_streamparm(sensor->meta_reglist,
						    sensor->current_reglist, a);

	if (!reglist)
		return -EINVAL;
	sensor->current_reglist = reglist;
	return smia_update_controls(s);
}

static int smia_ioctl_dev_init(struct v4l2_int_device *s)
{
	struct smia_sensor *sensor = s->priv;
	char name[FIRMWARE_NAME_MAX];
	int model_id, revision_number, manufacturer_id, smia_version;
	int i, rval;

	rval = smia_power_on(s);
	if (rval)
		return -ENODEV;

	/* Read and check sensor identification registers */
	if (smia_i2c_read_reg(sensor->i2c_client, SMIA_REG_16BIT,
			      REG_MODEL_ID, &model_id)
	    || smia_i2c_read_reg(sensor->i2c_client, SMIA_REG_8BIT,
				 REG_REVISION_NUMBER, &revision_number)
	    || smia_i2c_read_reg(sensor->i2c_client, SMIA_REG_8BIT,
				 REG_MANUFACTURER_ID, &manufacturer_id)
	    || smia_i2c_read_reg(sensor->i2c_client, SMIA_REG_8BIT,
				 REG_SMIA_VERSION, &smia_version)) {
		rval = -ENODEV;
		goto out_poweroff;
	}

	sensor->revision_number = revision_number;
	sensor->smia_version    = smia_version;

	if (smia_version != 10) {
		/* We support only SMIA version 1.0 at the moment */
		dev_err(&sensor->i2c_client->dev,
			"unknown sensor 0x%04x detected (smia ver %i.%i)\n",
			model_id, smia_version / 10, smia_version % 10);
		rval = -ENODEV;
		goto out_poweroff;
	}

	/* Detect which sensor we have */
	for (i = 1; i < ARRAY_SIZE(smia_sensors); i++) {
		if (smia_sensors[i].manufacturer_id == manufacturer_id
		    && smia_sensors[i].model_id == model_id)
			break;
	}
	if (i >= ARRAY_SIZE(smia_sensors))
		i = 0;			/* Unknown sensor */
	sensor->type = &smia_sensors[i];

	/* Initialize V4L2 controls */

	/* Gain is initialized here permanently */
	sensor->controls[SMIA_CTRL_GAIN].minimum           = 0;
	sensor->controls[SMIA_CTRL_GAIN].maximum           =
				sensor->type->ev_table_size - 1;
	sensor->controls[SMIA_CTRL_GAIN].step              = 1;
	sensor->controls[SMIA_CTRL_GAIN].default_value     = 0;
	sensor->controls[SMIA_CTRL_GAIN].value             = 0;
	sensor->controls[SMIA_CTRL_GAIN].set               =
				sensor->type->ev_table ? smia_set_gain : NULL;

	/* Exposure parameters may change at each mode change, just zero here */
	sensor->controls[SMIA_CTRL_EXPOSURE].minimum       = 0;
	sensor->controls[SMIA_CTRL_EXPOSURE].maximum       = 0;
	sensor->controls[SMIA_CTRL_EXPOSURE].step          = 0;
	sensor->controls[SMIA_CTRL_EXPOSURE].default_value = 0;
	sensor->controls[SMIA_CTRL_EXPOSURE].value         = 0;
	sensor->controls[SMIA_CTRL_EXPOSURE].set           = smia_set_exposure;

	/* Update identification string */
	strncpy(s->name, sensor->type->name, V4L2NAMESIZE);
	s->name[V4L2NAMESIZE-1] = 0;	/* Ensure NULL terminated string */

	/* Import firmware */
	snprintf(name, FIRMWARE_NAME_MAX, "%s-%02x-%04x-%02x.bin",
		 SMIA_SENSOR_NAME, sensor->type->manufacturer_id,
		 sensor->type->model_id, sensor->revision_number);

	if (request_firmware(&sensor->fw, name,
			     &sensor->i2c_client->dev)) {
		dev_err(&sensor->i2c_client->dev,
			"can't load firmware %s\n", name);
		rval = -ENODEV;
		goto out_poweroff;
	}

	sensor->meta_reglist =
		(struct smia_meta_reglist *)sensor->fw->data;

	rval = smia_reglist_import(sensor->meta_reglist);
	if (rval) {
		dev_err(&sensor->i2c_client->dev,
			"invalid register list %s, import failed\n",
			name);
		goto out_release;
	}

	/* Select initial mode */
	sensor->current_reglist =
		smia_reglist_find_type(sensor->meta_reglist,
				       SMIA_REGLIST_MODE);
	if (!sensor->current_reglist) {
		dev_err(&sensor->i2c_client->dev,
			"invalid register list %s, no mode found\n",
			name);
		rval = -ENODEV;
		goto out_release;
	}

	rval = smia_power_off(s);
	if (rval)
		goto out_release;

	return 0;

out_release:
	release_firmware(sensor->fw);
out_poweroff:
	sensor->meta_reglist = NULL;
	sensor->fw = NULL;
	smia_power_off(s);

	return rval;
}

#if VS6555_RESET_SHIFT_HACK
/*
 * Check if certain undocumented registers have values we expect.
 * If not, reset sensor and recheck.
 * This should be called when streaming is already enabled.
 */
static int smia_vs6555_reset_shift_hack(struct v4l2_int_device *s)
{
	struct smia_sensor *sensor = s->priv;
	int count = 10;
	int r381c = 0;
	int r381d = 0;
	int r381e = 0;
	int r381f = 0;
	int rval;

	do {
		rval = smia_i2c_read_reg(sensor->i2c_client,
					 SMIA_REG_8BIT, 0x381c, &r381c);
		if (rval)
			return rval;
		rval = smia_i2c_read_reg(sensor->i2c_client,
					 SMIA_REG_8BIT, 0x381d, &r381d);
		if (rval)
			return rval;
		rval = smia_i2c_read_reg(sensor->i2c_client,
					 SMIA_REG_8BIT, 0x381e, &r381e);
		if (rval)
			return rval;
		rval = smia_i2c_read_reg(sensor->i2c_client,
					 SMIA_REG_8BIT, 0x381f, &r381f);
		if (rval)
			return rval;

		if (r381d != 0 && r381f != 0 &&
		    r381c == 0 && r381e == 0)
			return 0;

		dev_dbg(&sensor->i2c_client->dev, "VS6555 HW misconfigured--"
			"trying to reset (%02X%02X%02X%02X)\n",
			r381c, r381d, r381e, r381f);

		smia_stream_off(s);
		smia_power_off(s);
		msleep(2);
		rval = smia_power_on(s);
		if (rval)
			return rval;
		rval = smia_configure(s);
		if (rval)
			return rval;
		rval = smia_stream_on(s);
		if (rval)
			return rval;
	} while (--count > 0);

	dev_warn(&sensor->i2c_client->dev,
		"VS6555 reset failed--expect bad image\n");

	return 0;	/* Return zero nevertheless -- at least we tried */
}
#endif

static int smia_ioctl_s_power(struct v4l2_int_device *s,
				enum v4l2_power new_state)
{
	struct smia_sensor *sensor = s->priv;
	enum v4l2_power old_state = sensor->power;
	int rval = 0;

	/*
	 * Map STANDBY to OFF mode: there is no reason to keep the sensor
	 * powered if not streaming.
	 */
	if (new_state == V4L2_POWER_STANDBY)
		new_state = V4L2_POWER_OFF;

	/* If we are already in this mode, do nothing */
	if (old_state == new_state)
		return 0;

	/* Disable power if so requested (it was enabled) */
	if (new_state == V4L2_POWER_OFF) {
		rval = smia_stream_off(s);
		if (rval)
			dev_err(&sensor->i2c_client->dev,
				"can not stop streaming\n");
		rval = smia_power_off(s);
		goto out;
	}

	/* Either STANDBY or ON requested */

	/* Enable power and move to standby if it was off */
	if (old_state == V4L2_POWER_OFF) {
		rval = smia_power_on(s);
		if (rval)
			goto out;
	}

	/* Now sensor is powered (standby or streaming) */

	if (new_state == V4L2_POWER_ON) {
		/* Standby -> streaming */
		sensor->power = V4L2_POWER_ON;
		rval = smia_configure(s);
		if (rval) {
			smia_stream_off(s);
			if (old_state == V4L2_POWER_OFF)
				smia_power_off(s);
			goto out;
		}
		rval = smia_stream_on(s);
#if VS6555_RESET_SHIFT_HACK
		if (rval == 0 && sensor->type->manufacturer_id == 0x01)
			rval = smia_vs6555_reset_shift_hack(s);
#endif
	} else {
		/* Streaming -> standby */
		rval = smia_stream_off(s);
	}

out:
	sensor->power = (rval == 0) ? new_state : old_state;
	return rval;
}

static int smia_ioctl_g_priv(struct v4l2_int_device *s, void *priv)
{
	struct smia_sensor *sensor = s->priv;

	return sensor->platform_data->g_priv(s, priv);
}

static int smia_ioctl_enum_framesizes(struct v4l2_int_device *s,
				 struct v4l2_frmsizeenum *frm)
{
	struct smia_sensor *sensor = s->priv;

	return smia_reglist_enum_framesizes(sensor->meta_reglist, frm);
}

static int smia_ioctl_enum_frameintervals(struct v4l2_int_device *s,
				     struct v4l2_frmivalenum *frm)
{
	struct smia_sensor *sensor = s->priv;

	return smia_reglist_enum_frameintervals(sensor->meta_reglist, frm);
}

#ifdef CONFIG_PM

static int smia_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct smia_sensor *sensor = dev_get_drvdata(&client->dev);
	enum v4l2_power resume_state = sensor->power;
	int rval;

	rval = smia_ioctl_s_power(sensor->v4l2_int_device, V4L2_POWER_OFF);
	if (rval == 0)
		sensor->power = resume_state;
	return rval;
}

static int smia_resume(struct i2c_client *client)
{
	struct smia_sensor *sensor = dev_get_drvdata(&client->dev);
	enum v4l2_power resume_state = sensor->power;

	sensor->power = V4L2_POWER_OFF;
	return smia_ioctl_s_power(sensor->v4l2_int_device, resume_state);
}

#else

#define smia_suspend	NULL
#define smia_resume	NULL

#endif /* CONFIG_PM */

static int smia_probe(struct i2c_client *client,
			const struct i2c_device_id *devid)
{
	struct smia_sensor *sensor = &smia;
	int rval;

	if (i2c_get_clientdata(client))
		return -EBUSY;

	sensor->platform_data = client->dev.platform_data;

	if (sensor->platform_data == NULL)
		return -ENODEV;

	sensor->v4l2_int_device = &smia_int_device;

	sensor->i2c_client = client;
	i2c_set_clientdata(client, sensor);

	rval = v4l2_int_device_register(sensor->v4l2_int_device);
	if (rval)
		i2c_set_clientdata(client, NULL);

	return rval;
}

static int __exit smia_remove(struct i2c_client *client)
{
	struct smia_sensor *sensor = i2c_get_clientdata(client);

	if (!client->adapter)
		return -ENODEV;	/* our client isn't attached */

	v4l2_int_device_unregister(sensor->v4l2_int_device);
	i2c_set_clientdata(client, NULL);

	return 0;
}

static const struct i2c_device_id smia_id_table[] = {
	{ SMIA_SENSOR_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, smia_id_table);

static struct i2c_driver smia_i2c_driver = {
	.driver		= {
		.name	= SMIA_SENSOR_NAME,
	},
	.probe		= smia_probe,
	.remove		= __exit_p(smia_remove),
	.suspend	= smia_suspend,
	.resume		= smia_resume,
	.id_table	= smia_id_table,
};

static int __init smia_init(void)
{
	int rval;

	rval = i2c_add_driver(&smia_i2c_driver);
	if (rval)
		printk(KERN_ALERT "%s: failed at i2c_add_driver\n", __func__);

	return rval;
}

static void __exit smia_exit(void)
{
	i2c_del_driver(&smia_i2c_driver);
}

/*
 * FIXME: Menelaus isn't ready (?) at module_init stage, so use
 * late_initcall for now.
 */
late_initcall(smia_init);
module_exit(smia_exit);

MODULE_AUTHOR("Tuukka Toivonen <tuukka.o.toivonen@nokia.com>");
MODULE_DESCRIPTION("Generic SMIA-compatible camera sensor driver");
MODULE_LICENSE("GPL");
