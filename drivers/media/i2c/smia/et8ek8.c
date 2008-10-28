/*
 * drivers/media/video/et8ek8.c
 *
 * Copyright (C) 2008 Nokia Corporation
 *
 * Contact: Sakari Ailus <sakari.ailus@nokia.com>
 *          Tuukka Toivonen <tuukka.o.toivonen@nokia.com>
 *
 * Based on code from Toni Leinonen <toni.leinonen@offcode.fi>.
 *
 * This driver is based on the Micron MT9T012 camera imager driver
 * (C) Texas Instruments.
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
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/v4l2-mediabus.h>

#include <media/smiaregs.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>

#include <media/et8ek8.h>

#define ET8EK8_XCLK_HZ		9600000

#define CTRL_GAIN		0
#define CTRL_EXPOSURE		1
#define CTRL_TEST_PATTERN	2

#define CID_TO_CTRL(id)		((id)==V4L2_CID_GAIN ? CTRL_GAIN : \
				 (id)==V4L2_CID_EXPOSURE ? CTRL_EXPOSURE : \
				 (id)==V4L2_CID_TEST_PATTERN ? CTRL_TEST_PATTERN : \
				 -EINVAL)

enum et8ek8_versions {
	ET8EK8_REV_1 = 0x0001,
	ET8EK8_REV_2,
};

/*
 * This table describes what should be written to the sensor register
 * for each gain value. The gain(index in the table) is in terms of
 * 0.1EV, i.e. 10 indexes in the table give 2 time more gain [0] in
 * the *analog gain, [1] in the digital gain
 *
 * Analog gain [dB] = 20*log10(regvalue/32); 0x20..0x100
 */
static struct et8ek8_gain {
	u16 analog;
	u16 digital;
} const et8ek8_gain_table[] = {
	{ 32,    0},  /* x1 */
	{ 34,    0},
	{ 37,    0},
	{ 39,    0},
	{ 42,    0},
	{ 45,    0},
	{ 49,    0},
	{ 52,    0},
	{ 56,    0},
	{ 60,    0},
	{ 64,    0},  /* x2 */
	{ 69,    0},
	{ 74,    0},
	{ 79,    0},
	{ 84,    0},
	{ 91,    0},
	{ 97,    0},
	{104,    0},
	{111,    0},
	{119,    0},
	{128,    0},  /* x4 */
	{137,    0},
	{147,    0},
	{158,    0},
	{169,    0},
	{181,    0},
	{194,    0},
	{208,    0},
	{223,    0},
	{239,    0},
	{256,    0},  /* x8 */
	{256,   73},
	{256,  152},
	{256,  236},
	{256,  327},
	{256,  424},
	{256,  528},
	{256,  639},
	{256,  758},
	{256,  886},
	{256, 1023},  /* x16 */
};

/* Register definitions */
#define REG_REVISION_NUMBER_L	0x1200
#define REG_REVISION_NUMBER_H	0x1201

#define PRIV_MEM_START_REG	0x0008
#define PRIV_MEM_WIN_SIZE	8

#define ET8EK8_I2C_DELAY	3	/* msec delay b/w accesses */

#define USE_CRC			1

/*
 * Return time of one row in microseconds, .8 fixed point format.
 * If the sensor is not set to any mode, return zero.
 */
static int et8ek8_get_row_time(struct et8ek8_sensor *sensor)
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

/*
 * Convert exposure time `us' to rows. Modify `us' to make it to
 * correspond to the actual exposure time.
 */
static int et8ek8_exposure_us_to_rows(struct et8ek8_sensor *sensor, u32 *us)
{
	unsigned int rows;	/* Exposure value as written to HW (ie. rows) */
	unsigned int rt;	/* Row time in .8 fixed point */

	/* Assume that the maximum exposure time is at most ~8 s,
	 * and the maximum width (with blanking) ~8000 pixels.
	 * The formula here is in principle as simple as
	 *    rows = exptime / 1e6 / width * pixel_clock
	 * but to get accurate results while coping with value ranges,
	 * have to do some fixed point math.
	 */

	rt = et8ek8_get_row_time(sensor);
	rows = ((*us << 8) + (rt >> 1)) / rt;

	if (rows > sensor->current_reglist->mode.max_exp)
		rows = sensor->current_reglist->mode.max_exp;

	/* Set the exposure time to the rounded value */
	*us = (rt * rows + (1 << 7)) >> 8;

	return rows;
}

/*
 * Convert exposure time in rows to microseconds
 */
static int et8ek8_exposure_rows_to_us(struct et8ek8_sensor *sensor, int rows)
{
	return (et8ek8_get_row_time(sensor) * rows + (1 << 7)) >> 8;
}

/* Called to change the V4L2 gain control value. This function
 * rounds and clamps the given value and updates the V4L2 control value.
 * If power is on, also updates the sensor analog and digital gains.
 * gain is in 0.1 EV (exposure value) units.
 */
static int et8ek8_set_gain(struct et8ek8_sensor *sensor, s32 gain)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->subdev);
	struct et8ek8_gain new;
	int r;

	new = et8ek8_gain_table[gain];

	/* FIXME: optimise I2C writes! */
	r = smia_i2c_write_reg(client, SMIA_REG_8BIT,
				0x124a, new.analog >> 8);
	if (r)
		return r;
	r = smia_i2c_write_reg(client, SMIA_REG_8BIT,
				0x1249, new.analog & 0xff);
	if (r)
		return r;

	r = smia_i2c_write_reg(client, SMIA_REG_8BIT,
				0x124d, new.digital >> 8);
	if (r)
		return r;
	r = smia_i2c_write_reg(client, SMIA_REG_8BIT,
				0x124c, new.digital & 0xff);

	return r;
}

static int et8ek8_set_test_pattern(struct et8ek8_sensor *sensor, s32 mode)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->subdev);
	int cbh_mode, cbv_mode, tp_mode, din_sw, r1420, rval;

	/* Values for normal mode */
	cbh_mode = 0;
	cbv_mode = 0;
	tp_mode  = 0;
	din_sw   = 0x00;
	r1420    = 0xF0;

	if (mode != 0) {
		/* Test pattern mode */
		if (mode < 5) {
			cbh_mode = 1;
			cbv_mode = 1;
			tp_mode  = mode + 3;
		} else {
			cbh_mode = 0;
			cbv_mode = 0;
			tp_mode  = mode - 4 + 3;
		}
		din_sw   = 0x01;
		r1420    = 0xE0;
	}

	rval = smia_i2c_write_reg(client, SMIA_REG_8BIT, 0x111B, tp_mode << 4);
	if (rval)
		goto out;

	rval = smia_i2c_write_reg(client, SMIA_REG_8BIT, 0x1121, cbh_mode << 7);
	if (rval)
		goto out;

	rval = smia_i2c_write_reg(client, SMIA_REG_8BIT, 0x1124, cbv_mode << 7);
	if (rval)
		goto out;

	rval = smia_i2c_write_reg(client, SMIA_REG_8BIT, 0x112C, din_sw);
	if (rval)
		goto out;

	rval = smia_i2c_write_reg(client, SMIA_REG_8BIT, 0x1420, r1420);
	if (rval)
		goto out;

out:
	return rval;
}

/* -----------------------------------------------------------------------------
 * V4L2 controls
 */

static int et8ek8_get_ctrl(struct v4l2_ctrl *ctrl)
{
	struct et8ek8_sensor *sensor =
		container_of(ctrl->handler, struct et8ek8_sensor, ctrl_handler);
	const struct smia_mode *mode = &sensor->current_reglist->mode;

	switch (ctrl->id) {
	case V4L2_CID_MODE_FRAME_WIDTH:
		ctrl->cur.val = mode->width;
		break;
	case V4L2_CID_MODE_FRAME_HEIGHT:
		ctrl->cur.val = mode->height;
		break;
	case V4L2_CID_MODE_VISIBLE_WIDTH:
		ctrl->cur.val = mode->window_width;
		break;
	case V4L2_CID_MODE_VISIBLE_HEIGHT:
		ctrl->cur.val = mode->window_height;
		break;
	case V4L2_CID_MODE_PIXELCLOCK:
		ctrl->cur.val = mode->pixel_clock;
		break;
	case V4L2_CID_MODE_SENSITIVITY:
		ctrl->cur.val = mode->sensitivity;
		break;
	case V4L2_CID_MODE_OPSYSCLOCK:
		ctrl->cur.val = mode->opsys_clock;
		break;
	}

	return 0;
}

static int et8ek8_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct et8ek8_sensor *sensor =
		container_of(ctrl->handler, struct et8ek8_sensor, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->subdev);
	int uninitialized_var(rows);

	if (ctrl->id == V4L2_CID_EXPOSURE)
		rows = et8ek8_exposure_us_to_rows(sensor, (u32 *)&ctrl->val);

	switch (ctrl->id) {
	case V4L2_CID_GAIN:
		return et8ek8_set_gain(sensor, ctrl->val);

	case V4L2_CID_EXPOSURE:
		return smia_i2c_write_reg(client, SMIA_REG_16BIT, 0x1243,
					  swab16(rows));

	case V4L2_CID_TEST_PATTERN:
		return et8ek8_set_test_pattern(sensor, ctrl->val);

	default:
		return -EINVAL;
	}
}

static const struct v4l2_ctrl_ops et8ek8_ctrl_ops = {
	.g_volatile_ctrl = et8ek8_get_ctrl,
	.s_ctrl = et8ek8_set_ctrl,
};

static const char *et8ek8_test_pattern_menu[] = {
	"Normal",
	"Vertical colorbar",
	"Horizontal colorbar",
	"Scale",
	"Ramp",
	"Small vertical colorbar",
	"Small horizontal colorbar",
	"Small scale",
	"Small ramp",
};

static const struct v4l2_ctrl_config et8ek8_ctrls[] = {
	{
		.ops		= &et8ek8_ctrl_ops,
		.id		= V4L2_CID_TEST_PATTERN,
		.type		= V4L2_CTRL_TYPE_MENU,
		.name		= "Test pattern mode",
		.min		= 0,
		.max		= ARRAY_SIZE(et8ek8_test_pattern_menu) - 1,
		.step		= 0,
		.def		= 0,
		.flags		= 0,
		.qmenu		= et8ek8_test_pattern_menu,
	},
	{
		.id		= V4L2_CID_MODE_CLASS,
		.type		= V4L2_CTRL_TYPE_CTRL_CLASS,
		.name		= "SMIA-type sensor information",
		.min		= 0,
		.max		= 0,
		.step		= 1,
		.def		= 0,
		.flags		= V4L2_CTRL_FLAG_READ_ONLY
				| V4L2_CTRL_FLAG_WRITE_ONLY,
	},
	{
		.ops		= &et8ek8_ctrl_ops,
		.id		= V4L2_CID_MODE_FRAME_WIDTH,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Frame width",
		.min		= 0,
		.max		= 0,
		.step		= 1,
		.def		= 0,
		.flags		= V4L2_CTRL_FLAG_READ_ONLY
				  | V4L2_CTRL_FLAG_VOLATILE,
	},
	{
		.ops		= &et8ek8_ctrl_ops,
		.id		= V4L2_CID_MODE_FRAME_HEIGHT,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Frame height",
		.min		= 0,
		.max		= 0,
		.step		= 1,
		.def		= 0,
		.flags		= V4L2_CTRL_FLAG_READ_ONLY
				  | V4L2_CTRL_FLAG_VOLATILE,
	},
	{
		.ops		= &et8ek8_ctrl_ops,
		.id		= V4L2_CID_MODE_VISIBLE_WIDTH,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Visible width",
		.min		= 0,
		.max		= 0,
		.step		= 1,
		.def		= 0,
		.flags		= V4L2_CTRL_FLAG_READ_ONLY
				  | V4L2_CTRL_FLAG_VOLATILE,
	},
	{
		.ops		= &et8ek8_ctrl_ops,
		.id		= V4L2_CID_MODE_VISIBLE_HEIGHT,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Visible height",
		.min		= 0,
		.max		= 0,
		.step		= 1,
		.def		= 0,
		.flags		= V4L2_CTRL_FLAG_READ_ONLY
				  | V4L2_CTRL_FLAG_VOLATILE,
	},
	{
		.ops		= &et8ek8_ctrl_ops,
		.id		= V4L2_CID_MODE_PIXELCLOCK,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Pixel clock [Hz]",
		.min		= 0,
		.max		= 0,
		.step		= 1,
		.def		= 0,
		.flags		= V4L2_CTRL_FLAG_READ_ONLY
				  | V4L2_CTRL_FLAG_VOLATILE,
	},
	{
		.ops		= &et8ek8_ctrl_ops,
		.id		= V4L2_CID_MODE_SENSITIVITY,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Sensivity",
		.min		= 0,
		.max		= 0,
		.step		= 1,
		.def		= 0,
		.flags		= V4L2_CTRL_FLAG_READ_ONLY
				  | V4L2_CTRL_FLAG_VOLATILE,
	},
	{
		.ops		= &et8ek8_ctrl_ops,
		.id		= V4L2_CID_MODE_OPSYSCLOCK,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Output pixel clock [Hz]",
		.min		= 0,
		.max		= 0,
		.step		= 1,
		.def		= 0,
		.flags		= V4L2_CTRL_FLAG_READ_ONLY
				  | V4L2_CTRL_FLAG_VOLATILE,
	},
};

static int et8ek8_init_controls(struct et8ek8_sensor *sensor)
{
	unsigned int i;
	u32 min, max;

	v4l2_ctrl_handler_init(&sensor->ctrl_handler,
			       ARRAY_SIZE(et8ek8_ctrls) + 2);

	/* V4L2_CID_GAIN */
	v4l2_ctrl_new_std(&sensor->ctrl_handler, &et8ek8_ctrl_ops,
			  V4L2_CID_GAIN, 0, ARRAY_SIZE(et8ek8_gain_table) - 1,
			  1, 0);

	/* V4L2_CID_EXPOSURE */
	min = et8ek8_exposure_rows_to_us(sensor, 1);
	max = et8ek8_exposure_rows_to_us(sensor,
				sensor->current_reglist->mode.max_exp);
	sensor->exposure =
		v4l2_ctrl_new_std(&sensor->ctrl_handler, &et8ek8_ctrl_ops,
				  V4L2_CID_EXPOSURE, min, max, min, max);

	/* V4L2_CID_TEST_PATTERN and V4L2_CID_MODE_* */
	for (i = 0; i < ARRAY_SIZE(et8ek8_ctrls); ++i)
		v4l2_ctrl_new_custom(&sensor->ctrl_handler, &et8ek8_ctrls[i],
				     NULL);

	if (sensor->ctrl_handler.error)
		return sensor->ctrl_handler.error;

	sensor->subdev.ctrl_handler = &sensor->ctrl_handler;
	return 0;
}

static void et8ek8_update_controls(struct et8ek8_sensor *sensor)
{
	struct v4l2_ctrl *ctrl = sensor->exposure;
	u32 min, max;

	min = et8ek8_exposure_rows_to_us(sensor, 1);
	max = et8ek8_exposure_rows_to_us(sensor,
					 sensor->current_reglist->mode.max_exp);

	v4l2_ctrl_lock(ctrl);
	ctrl->minimum = min;
	ctrl->maximum = max;
	ctrl->step = min;
	ctrl->default_value = max;
	ctrl->val = max;
	ctrl->cur.val = max;
	v4l2_ctrl_unlock(ctrl);
}

static int et8ek8_configure(struct et8ek8_sensor *sensor)
{
	struct v4l2_subdev *subdev = &sensor->subdev;
	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	int rval;

	rval = smia_i2c_write_regs(client, sensor->current_reglist->regs);
	if (rval)
		goto fail;

	/* Controls set while the power to the sensor is turned off are saved
	 * but not applied to the hardware. Now that we're about to start
	 * streaming apply all the current values to the hardware.
	 */
	rval = v4l2_ctrl_handler_setup(&sensor->ctrl_handler);
	if (rval)
		goto fail;

	return 0;

fail:
	dev_err(&client->dev, "sensor configuration failed\n");
	return rval;
}

static int et8ek8_stream_on(struct et8ek8_sensor *sensor)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->subdev);

	return smia_i2c_write_reg(client, SMIA_REG_8BIT, 0x1252, 0xb0);
}

static int et8ek8_stream_off(struct et8ek8_sensor *sensor)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->subdev);

	return smia_i2c_write_reg(client, SMIA_REG_8BIT, 0x1252, 0x30);
}

static int et8ek8_s_stream(struct v4l2_subdev *subdev, int streaming)
{
	struct et8ek8_sensor *sensor = to_et8ek8_sensor(subdev);
	int ret;

	if (!streaming)
		return et8ek8_stream_off(sensor);

	ret = et8ek8_configure(sensor);
	if (ret < 0)
		return ret;

	return et8ek8_stream_on(sensor);
}

/* --------------------------------------------------------------------------
 * V4L2 subdev operations
 */

static int et8ek8_power_off(struct et8ek8_sensor *sensor)
{
	struct v4l2_subdev *subdev = &sensor->subdev;
	int rval;

	rval = sensor->platform_data->set_xshutdown(subdev, 0);
	udelay(1);
	rval |= sensor->platform_data->set_xclk(subdev, 0);
	rval |= regulator_disable(sensor->vana);
	return rval;
}

static int et8ek8_power_on(struct et8ek8_sensor *sensor)
{
	struct v4l2_subdev *subdev = &sensor->subdev;
	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	unsigned int hz = ET8EK8_XCLK_HZ;
	int val, rval;

	rval = regulator_enable(sensor->vana);
	if (rval) {
		dev_err(&client->dev, "failed to enable vana regulator\n");
		return rval;
	}

	if (sensor->current_reglist)
		hz = sensor->current_reglist->mode.ext_clock;

	rval = sensor->platform_data->set_xclk(subdev, hz);
	if (rval)
		goto out;

	udelay(10);			/* I wish this is a good value */

	rval = sensor->platform_data->set_xshutdown(subdev, 1);
	if (rval)
		goto out;

	msleep(5000*1000/hz+1);				/* Wait 5000 cycles */

	if (sensor->meta_reglist) {
		rval = smia_i2c_reglist_find_write(client,
						   sensor->meta_reglist,
						   SMIA_REGLIST_POWERON);
		if (rval)
			goto out;
	}

#ifdef USE_CRC
	rval = smia_i2c_read_reg(client,
				 SMIA_REG_8BIT, 0x1263, &val);
	if (rval)
		goto out;
#if USE_CRC
	val |= (1<<4);
#else
	val &= ~(1<<4);
#endif
	rval = smia_i2c_write_reg(client,
				  SMIA_REG_8BIT, 0x1263, val);
	if (rval)
		goto out;
#endif

out:
	if (rval)
		et8ek8_power_off(sensor);

	return rval;
}

/* --------------------------------------------------------------------------
 * V4L2 subdev video operations
 */

static int et8ek8_enum_mbus_code(struct v4l2_subdev *subdev,
				 struct v4l2_subdev_fh *fh,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct et8ek8_sensor *sensor = to_et8ek8_sensor(subdev);

	return smia_reglist_enum_mbus_code(sensor->meta_reglist, code);
}

static int et8ek8_enum_frame_size(struct v4l2_subdev *subdev,
				  struct v4l2_subdev_fh *fh,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	struct et8ek8_sensor *sensor = to_et8ek8_sensor(subdev);

	return smia_reglist_enum_frame_size(sensor->meta_reglist, fse);
}

static int et8ek8_enum_frame_ival(struct v4l2_subdev *subdev,
				  struct v4l2_subdev_fh *fh,
				  struct v4l2_subdev_frame_interval_enum *fie)
{
	struct et8ek8_sensor *sensor = to_et8ek8_sensor(subdev);

	return smia_reglist_enum_frame_ival(sensor->meta_reglist, fie);
}

static struct v4l2_mbus_framefmt *
__et8ek8_get_pad_format(struct et8ek8_sensor *sensor, struct v4l2_subdev_fh *fh,
			unsigned int pad, enum v4l2_subdev_format_whence which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(fh, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &sensor->format;
	default: 
		return NULL;
	}
}

static int et8ek8_get_pad_format(struct v4l2_subdev *subdev,
				 struct v4l2_subdev_fh *fh,
				 struct v4l2_subdev_format *fmt)
{
	struct et8ek8_sensor *sensor = to_et8ek8_sensor(subdev);
	struct v4l2_mbus_framefmt *format;
 
	format = __et8ek8_get_pad_format(sensor, fh, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	fmt->format = *format;
	return 0;
}

static int et8ek8_set_pad_format(struct v4l2_subdev *subdev,
				 struct v4l2_subdev_fh *fh,
				 struct v4l2_subdev_format *fmt)
{
	struct et8ek8_sensor *sensor = to_et8ek8_sensor(subdev);
	struct v4l2_mbus_framefmt *format;
        struct smia_reglist *reglist;
	int ret;

	format = __et8ek8_get_pad_format(sensor, fh, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	reglist = smia_reglist_find_mode_fmt(sensor->meta_reglist,
					     &fmt->format);
	smia_reglist_to_mbus(reglist, &fmt->format);
	*format = fmt->format;

	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		sensor->current_reglist = reglist;
		ret = sensor->platform_data->configure_interface(subdev,
						&sensor->current_reglist->mode);
		if (ret < 0)
			return ret;

		et8ek8_update_controls(sensor);
	}

	return 0;
}

static int et8ek8_get_frame_interval(struct v4l2_subdev *subdev,
				     struct v4l2_subdev_frame_interval *fi)
{
	struct et8ek8_sensor *sensor = to_et8ek8_sensor(subdev);

	memset(fi, 0, sizeof(*fi));
	fi->interval = sensor->current_reglist->mode.timeperframe;

	return 0;
}

static int et8ek8_set_frame_interval(struct v4l2_subdev *subdev,
				     struct v4l2_subdev_frame_interval *fi)
{
	struct et8ek8_sensor *sensor = to_et8ek8_sensor(subdev);
	struct smia_reglist *reglist;

	reglist = smia_reglist_find_mode_ival(sensor->meta_reglist,
					      sensor->current_reglist,
					      &fi->interval);

	if (!reglist)
		return -EINVAL;

	if (sensor->current_reglist->mode.ext_clock != reglist->mode.ext_clock)
		return -EINVAL;

	sensor->current_reglist = reglist;
	et8ek8_update_controls(sensor);

	return 0;
}

static int et8ek8_g_priv_mem(struct v4l2_subdev *subdev)
{
	struct et8ek8_sensor *sensor = to_et8ek8_sensor(subdev);
	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	unsigned int length = ET8EK8_PRIV_MEM_SIZE;
	unsigned int offset = 0;
	u8 *ptr  = sensor->priv_mem;
	int rval = 0;

	/* Read the EEPROM window-by-window, each window 8 bytes */
	do {
		u8 buffer[PRIV_MEM_WIN_SIZE];
		struct i2c_msg msg;
		int bytes, i;
		int ofs;

		/* Set the current window */
		rval = smia_i2c_write_reg(client,
					  SMIA_REG_8BIT,
					  0x0001,
					  0xe0 | (offset >> 3));
		if (rval < 0)
			goto out;

		/* Wait for status bit */
		for (i = 0; i < 1000; ++i) {
			u32 status;
			rval = smia_i2c_read_reg(client,
						 SMIA_REG_8BIT,
						 0x0003,
						 &status);
			if (rval < 0)
				goto out;
			if ((status & 0x08) == 0)
				break;
			msleep(1);
		};

		if (i == 1000) {
			rval = -EIO;
			goto out;
		}

		/* Read window, 8 bytes at once, and copy to user space */
		ofs = offset & 0x07;	/* Offset within this window */
		bytes = length + ofs > 8 ? 8-ofs : length;
		msg.addr = client->addr;
		msg.flags = 0;
		msg.len = 2;
		msg.buf = buffer;
		ofs += PRIV_MEM_START_REG;
		buffer[0] = (u8)(ofs >> 8);
		buffer[1] = (u8)(ofs & 0xFF);
		rval = i2c_transfer(client->adapter, &msg, 1);
		if (rval < 0)
			goto out;
		mdelay(ET8EK8_I2C_DELAY);
		msg.addr = client->addr;
		msg.len = bytes;
		msg.flags = I2C_M_RD;
		msg.buf = buffer;
		memset(buffer, 0, sizeof(buffer));
		rval = i2c_transfer(client->adapter, &msg, 1);
		if (rval < 0)
			goto out;
		rval = 0;
		memcpy(ptr, buffer, bytes);

		length -= bytes;
		offset += bytes;
		ptr    += bytes;
	} while (length > 0);

out:
	return rval;
}

static int et8ek8_dev_init(struct v4l2_subdev *subdev)
{
	struct et8ek8_sensor *sensor = to_et8ek8_sensor(subdev);
	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	char name[SMIA_MAX_LEN];
	int rval, rev_l, rev_h;

	sensor->vana = regulator_get(&client->dev, "VANA");
	if (IS_ERR(sensor->vana)) {
		dev_err(&client->dev, "could not get regulator for vana\n");
		return -ENODEV;
	}

	rval = et8ek8_power_on(sensor);
	if (rval) {
		rval = -ENODEV;
		goto out_regulator_put;
	}

	if (smia_i2c_read_reg(client, SMIA_REG_8BIT,
			      REG_REVISION_NUMBER_L, &rev_l) != 0
	    || smia_i2c_read_reg(client, SMIA_REG_8BIT,
				 REG_REVISION_NUMBER_H, &rev_h) != 0) {
		dev_err(&client->dev,
			"no et8ek8 sensor detected\n");
		rval = -ENODEV;
		goto out_poweroff;
	}
	sensor->version = (rev_h << 8) + rev_l;
	if (sensor->version != ET8EK8_REV_1
	    && sensor->version != ET8EK8_REV_2)
		dev_info(&client->dev,
			 "unknown version 0x%x detected, "
			 "continuing anyway\n", sensor->version);

	snprintf(name, sizeof(name), "%s-%4.4x.bin", ET8EK8_NAME,
		 sensor->version);
	if (request_firmware(&sensor->fw, name,
			     &client->dev)) {
		dev_err(&client->dev,
			"can't load firmware %s\n", name);
		rval = -ENODEV;
		goto out_poweroff;
	}
	sensor->meta_reglist =
		(struct smia_meta_reglist *)sensor->fw->data;
	rval = smia_reglist_import(sensor->meta_reglist);
	if (rval) {
		dev_err(&client->dev,
			"invalid register list %s, import failed\n",
			name);
		goto out_release;
	}

	sensor->current_reglist =
		smia_reglist_find_type(sensor->meta_reglist,
				       SMIA_REGLIST_MODE);
	if (!sensor->current_reglist) {
		dev_err(&client->dev,
			"invalid register list %s, no mode found\n",
			name);
		rval = -ENODEV;
		goto out_release;
	}

	smia_reglist_to_mbus(sensor->current_reglist, &sensor->format);

	rval = smia_i2c_reglist_find_write(client,
					   sensor->meta_reglist,
					   SMIA_REGLIST_POWERON);
	if (rval) {
		dev_err(&client->dev,
			"invalid register list %s, no POWERON mode found\n",
			name);
		goto out_release;
	}
	rval = et8ek8_stream_on(sensor);	/* Needed to be able to read EEPROM */
	if (rval)
		goto out_release;
	rval = et8ek8_g_priv_mem(subdev);
	if (rval)
		dev_warn(&client->dev,
			"can not read OTP (EEPROM) memory from sensor\n");
	rval = et8ek8_stream_off(sensor);
	if (rval)
		goto out_release;

	rval = et8ek8_power_off(sensor);
	if (rval)
		goto out_release;

	return 0;

out_release:
	release_firmware(sensor->fw);
out_poweroff:
	sensor->meta_reglist = NULL;
	sensor->fw = NULL;
	et8ek8_power_off(sensor);
out_regulator_put:
	regulator_put(sensor->vana);
	sensor->vana = NULL;

	return rval;
}

/* --------------------------------------------------------------------------
 * sysfs attributes
 */
static ssize_t
et8ek8_priv_mem_read(struct device *dev, struct device_attribute *attr,
		     char *buf)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(to_i2c_client(dev));
	struct et8ek8_sensor *sensor = to_et8ek8_sensor(subdev);

#if PAGE_SIZE < ET8EK8_PRIV_MEM_SIZE
#error PAGE_SIZE too small!
#endif

	memcpy(buf, sensor->priv_mem, ET8EK8_PRIV_MEM_SIZE);

	return ET8EK8_PRIV_MEM_SIZE;
}
static DEVICE_ATTR(priv_mem, S_IRUGO, et8ek8_priv_mem_read, NULL);

/* --------------------------------------------------------------------------
 * V4L2 subdev core operations
 */

static int
et8ek8_registered(struct v4l2_subdev *subdev)
{
	struct et8ek8_sensor *sensor = to_et8ek8_sensor(subdev);
	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	struct v4l2_mbus_framefmt *format;
	int rval;

	if (device_create_file(&client->dev, &dev_attr_priv_mem) != 0) {
		dev_err(&client->dev, "could not register sysfs entry\n");
		return -EBUSY;
	}

	rval = et8ek8_dev_init(subdev);
	if (rval)
		return rval;

	rval = et8ek8_init_controls(sensor);
	if (rval) {
		dev_err(&client->dev, "controls initialization failed\n");
		return rval;
	}

	format = __et8ek8_get_pad_format(sensor, NULL, 0,
					 V4L2_SUBDEV_FORMAT_ACTIVE);
	return 0;
}

static int __et8ek8_set_power(struct et8ek8_sensor *sensor, int on)
{
	return on ? et8ek8_power_on(sensor) : et8ek8_power_off(sensor);
}

static int et8ek8_set_power(struct v4l2_subdev *subdev, int on)
{
	struct et8ek8_sensor *sensor = to_et8ek8_sensor(subdev);
	int ret = 0;

	mutex_lock(&sensor->power_lock);

	/* If the power count is modified from 0 to != 0 or from != 0 to 0,
	 * update the power state.
	 */
	if (sensor->power_count == !on) {
		ret = __et8ek8_set_power(sensor, !!on);
		if (ret < 0)
			goto done;
	}

	/* Update the power count. */
	sensor->power_count += on ? 1 : -1;
	WARN_ON(sensor->power_count < 0);

done:
	mutex_unlock(&sensor->power_lock);
	return ret;
}

static int et8ek8_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct et8ek8_sensor *sensor = to_et8ek8_sensor(sd);
	struct v4l2_mbus_framefmt *format;
	struct smia_reglist *reglist;

	reglist = smia_reglist_find_type(sensor->meta_reglist,
					 SMIA_REGLIST_MODE);
	format = __et8ek8_get_pad_format(sensor, fh, 0, V4L2_SUBDEV_FORMAT_TRY);
	smia_reglist_to_mbus(reglist, format);

	return et8ek8_set_power(sd, 1);
}

static int et8ek8_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	return et8ek8_set_power(sd, 0);
}

static const struct v4l2_subdev_video_ops et8ek8_video_ops = {
	.s_stream = et8ek8_s_stream,
	.g_frame_interval = et8ek8_get_frame_interval,
	.s_frame_interval = et8ek8_set_frame_interval,
};

static const struct v4l2_subdev_core_ops et8ek8_core_ops = {
	.s_power = et8ek8_set_power,
};

static const struct v4l2_subdev_pad_ops et8ek8_pad_ops = {
	.enum_mbus_code = et8ek8_enum_mbus_code,
        .enum_frame_size = et8ek8_enum_frame_size,
        .enum_frame_interval = et8ek8_enum_frame_ival,
	.get_fmt = et8ek8_get_pad_format,
	.set_fmt = et8ek8_set_pad_format,
};

static const struct v4l2_subdev_ops et8ek8_ops = {
	.core = &et8ek8_core_ops,
	.video = &et8ek8_video_ops,
	.pad = &et8ek8_pad_ops,
};

static const struct v4l2_subdev_internal_ops et8ek8_internal_ops = {
	.registered = et8ek8_registered,
	.open = et8ek8_open,
	.close = et8ek8_close,
};

/* --------------------------------------------------------------------------
 * I2C driver
 */
#ifdef CONFIG_PM

static int et8ek8_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct et8ek8_sensor *sensor = to_et8ek8_sensor(subdev);

	if (!sensor->power_count)
		return 0;

	return __et8ek8_set_power(sensor, 0);
}

static int et8ek8_resume(struct i2c_client *client)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct et8ek8_sensor *sensor = to_et8ek8_sensor(subdev);

	if (!sensor->power_count)
		return 0;

	return __et8ek8_set_power(sensor, 1);
}

#else

#define et8ek8_suspend	NULL
#define et8ek8_resume	NULL

#endif /* CONFIG_PM */

static int et8ek8_probe(struct i2c_client *client,
			const struct i2c_device_id *devid)
{
	struct et8ek8_sensor *sensor;
	int ret;

	sensor = kzalloc(sizeof(*sensor), GFP_KERNEL);
	if (sensor == NULL)
		return -ENOMEM;

	sensor->platform_data = client->dev.platform_data;

	mutex_init(&sensor->power_lock);

	v4l2_i2c_subdev_init(&sensor->subdev, client, &et8ek8_ops);
	sensor->subdev.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	sensor->subdev.internal_ops = &et8ek8_internal_ops;

	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_init(&sensor->subdev.entity, 1, &sensor->pad, 0);
	if (ret < 0)
		kfree(sensor);

	return ret;
}

static int __exit et8ek8_remove(struct i2c_client *client)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct et8ek8_sensor *sensor = to_et8ek8_sensor(subdev);

	v4l2_device_unregister_subdev(&sensor->subdev);
	device_remove_file(&client->dev, &dev_attr_priv_mem);
	v4l2_ctrl_handler_free(&sensor->ctrl_handler);
	media_entity_cleanup(&sensor->subdev.entity);
	if (sensor->vana)
		regulator_put(sensor->vana);

	release_firmware(sensor->fw);
	kfree(sensor);
	return 0;
}

static const struct i2c_device_id et8ek8_id_table[] = {
	{ ET8EK8_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, et8ek8_id_table);

static struct i2c_driver et8ek8_i2c_driver = {
	.driver		= {
		.name	= ET8EK8_NAME,
	},
	.probe		= et8ek8_probe,
	.remove		= __exit_p(et8ek8_remove),
	.suspend	= et8ek8_suspend,
	.resume		= et8ek8_resume,
	.id_table	= et8ek8_id_table,
};

static int __init et8ek8_init(void)
{
	int rval;

	rval = i2c_add_driver(&et8ek8_i2c_driver);
	if (rval)
		printk(KERN_INFO "%s: failed registering " ET8EK8_NAME "\n",
		       __func__);

	return rval;
}

static void __exit et8ek8_exit(void)
{
	i2c_del_driver(&et8ek8_i2c_driver);
}

module_init(et8ek8_init);
module_exit(et8ek8_exit);

MODULE_AUTHOR("Sakari Ailus <sakari.ailus@nokia.com>");
MODULE_DESCRIPTION("Toshiba ET8EK8 camera sensor driver");
MODULE_LICENSE("GPL");
