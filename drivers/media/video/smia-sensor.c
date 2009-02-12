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
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/regulator/consumer.h>
#include <linux/v4l2-mediabus.h>

#include <media/media-entity.h>
#include <media/smiaregs.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>

#include "smia-sensor.h"

#define DEFAULT_XCLK		9600000		/* [Hz] */

#define SMIA_CTRL_GAIN		0
#define SMIA_CTRL_EXPOSURE	1
#define SMIA_NCTRLS		2

#define CID_TO_CTRL(id)		((id) == V4L2_CID_GAIN ? SMIA_CTRL_GAIN : \
				 (id) == V4L2_CID_EXPOSURE ? \
					 SMIA_CTRL_EXPOSURE : \
					 -EINVAL)

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
#define REG_ANALOG_GAIN_MIN	0x0084
#define REG_ANALOG_GAIN_MAX	0x0086
#define REG_ANALOG_GAIN_STEP	0x0088

/* Frame Format Description registers */
#define REG_FFMT_MTYPE		0x0040
#define REG_FFMT_MSTYPE		0x0041
#define REG_FFMT_DESC_ZERO	0x0042

struct smia_sensor_type {
	u8 manufacturer_id;
	u16 model_id;
	char *name;
};

/* Current values for V4L2 controls */
struct smia_control {
	s32 minimum;
	s32 maximum;
	s32 step;
	s32 default_value;
	s32 value;
	int (*set)(struct v4l2_subdev *, s32 value);
};

#define to_smia_sensor(sd)	container_of(sd, struct smia_sensor, subdev)

struct smia_sensor {
	struct v4l2_mbus_framefmt format;
	struct v4l2_subdev subdev;
	struct media_pad pad;

	/* Sensor information */
	char name[32];
	struct smia_sensor_type *type;
	u8  revision_number;
	u8  smia_version;
	u16 sof_rows;	/* Additional rows from the sensor @ Start-Of-Frame */

	/* V4L2 current control values */
	struct smia_control controls[SMIA_NCTRLS];

	struct smia_reglist *current_reglist;
	struct v4l2_fract timeperframe;

	struct smia_sensor_platform_data *platform_data;
	struct regulator *vana;

	const struct firmware *fw;
	struct smia_meta_reglist *meta_reglist;

	int power;
	int streaming;
};

static struct smia_sensor_type smia_sensors[] = {
	{ 0,	0,	"unknown" },
	{ 0x01,	0x022b,	"vs6555" },
	{ 0x0c,	0x208a,	"tcm8330md" },
};

static const __u32 smia_mode_ctrls[] = {
	V4L2_CID_MODE_FRAME_WIDTH,
	V4L2_CID_MODE_FRAME_HEIGHT,
	V4L2_CID_MODE_VISIBLE_WIDTH,
	V4L2_CID_MODE_VISIBLE_HEIGHT,
	V4L2_CID_MODE_PIXELCLOCK,
	V4L2_CID_MODE_SENSITIVITY,
};

static int smia_read_frame_fmt(struct v4l2_subdev *subdev)
{
	struct smia_sensor *sensor = to_smia_sensor(subdev);
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->subdev);
	int i, ncol_desc, nrow_desc;
	u32 val;
	int rval;

	sensor->sof_rows = 0;

	rval = smia_i2c_read_reg(client, SMIA_REG_8BIT, REG_FFMT_MTYPE,
				 &val);
	if (rval)
		return rval;

	/* We support only 2-byte Generic Frame Format Description */
	if (val != 0x01)
		return val;

	rval = smia_i2c_read_reg(client, SMIA_REG_8BIT, REG_FFMT_MSTYPE,
				 &val);
	if (rval)
		return rval;

	ncol_desc = (val & 0xF0) >> 4;
	nrow_desc = (val & 0x0F);

	for (i = ncol_desc; i < ncol_desc + nrow_desc; i++) {
		rval = smia_i2c_read_reg(client, SMIA_REG_16BIT,
					 REG_FFMT_DESC_ZERO + (i * 2),
					 &val);
		if (rval)
			return rval;

		if ((val & 0xF000) >> 12 == 5)
			continue;	/* Image Data */

		sensor->sof_rows += val & 0x0FFF;
	}

	return 0;
}

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
static int smia_set_gain(struct v4l2_subdev *subdev, s32 gain)
{
	struct smia_sensor *sensor = to_smia_sensor(subdev);
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->subdev);

	sensor->controls[SMIA_CTRL_GAIN].value = clamp(gain,
		sensor->controls[SMIA_CTRL_GAIN].minimum,
		sensor->controls[SMIA_CTRL_GAIN].maximum);

	if (!sensor->power)
		return 0;

	return smia_i2c_write_reg(client,
				  SMIA_REG_16BIT, REG_ANALOG_GAIN,
				  sensor->controls[SMIA_CTRL_GAIN].value);
}

/* Called to change the V4L2 exposure control value. This function
 * rounds and clamps the given value and updates the V4L2 control value.
 * If power is on, also update the sensor exposure time.
 * exptime is in microseconds.
 */
static int smia_set_exposure(struct v4l2_subdev *subdev, s32 exptime)
{
	struct smia_sensor *sensor = to_smia_sensor(subdev);
	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	int exposure_rows;

	exptime = clamp(exptime, sensor->controls[SMIA_CTRL_EXPOSURE].minimum,
				 sensor->controls[SMIA_CTRL_EXPOSURE].maximum);

	exposure_rows = smia_exposure_us_to_rows(sensor, &exptime);
	sensor->controls[SMIA_CTRL_EXPOSURE].value = exptime;

	if (!sensor->power)
		return 0;

	return smia_i2c_write_reg(client, SMIA_REG_16BIT,
				  REG_COARSE_EXPOSURE, exposure_rows);
}

static void smia_init_controls(struct v4l2_subdev *subdev)
{
	struct smia_sensor *sensor = to_smia_sensor(subdev);
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->subdev);

	smia_i2c_read_reg(client, SMIA_REG_16BIT, REG_ANALOG_GAIN_MIN,
			  (u32 *)&sensor->controls[SMIA_CTRL_GAIN].minimum);
	smia_i2c_read_reg(client, SMIA_REG_16BIT, REG_ANALOG_GAIN_MAX,
			  (u32 *)&sensor->controls[SMIA_CTRL_GAIN].maximum);
	smia_i2c_read_reg(client, SMIA_REG_16BIT, REG_ANALOG_GAIN_STEP,
			  (u32 *)&sensor->controls[SMIA_CTRL_GAIN].step);
	sensor->controls[SMIA_CTRL_GAIN].default_value =
				sensor->controls[SMIA_CTRL_GAIN].minimum;
	sensor->controls[SMIA_CTRL_GAIN].value             = 0;
	sensor->controls[SMIA_CTRL_GAIN].set               = smia_set_gain;

	/* Exposure parameters may change at each mode change, just zero here */
	sensor->controls[SMIA_CTRL_EXPOSURE].minimum       = 0;
	sensor->controls[SMIA_CTRL_EXPOSURE].maximum       = 0;
	sensor->controls[SMIA_CTRL_EXPOSURE].step          = 0;
	sensor->controls[SMIA_CTRL_EXPOSURE].default_value = 0;
	sensor->controls[SMIA_CTRL_EXPOSURE].value         = 0;
	sensor->controls[SMIA_CTRL_EXPOSURE].set           = smia_set_exposure;
}

static int smia_update_controls(struct v4l2_subdev *subdev)
{
	struct smia_sensor *sensor = to_smia_sensor(subdev);

	/* Adjust V4L2 control values due to sensor mode change */

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

	return 0;
}

static int smia_set_controls(struct v4l2_subdev *subdev)
{
	struct smia_sensor *sensor = to_smia_sensor(subdev);
	int i;

	/* Write the cntrols to the sensorr */

	for (i = 0; i < ARRAY_SIZE(sensor->controls); i++) {
		int rval = sensor->controls[i].set(subdev,
			sensor->controls[i].value);
		if (rval)
			return rval;
	}

	return 0;
}

/* Must be called with power already enabled on the sensor */
static int smia_configure(struct v4l2_subdev *subdev)
{
	struct smia_sensor *sensor = to_smia_sensor(subdev);
	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	int rval;

	rval = smia_i2c_write_regs(client,
				   sensor->current_reglist->regs);
	if (rval)
		goto fail;

	rval = smia_set_controls(subdev);
	if (rval)
		goto fail;

	return 0;

fail:
	dev_err(&client->dev, "sensor configuration failed\n");
	return rval;

}

static int smia_s_stream(struct v4l2_subdev *subdev, int streaming)
{
	struct smia_sensor *sensor = to_smia_sensor(subdev);
	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	int rval;

	if (sensor->streaming == streaming)
		return 0;

	if (streaming) {
		rval = smia_configure(subdev);
		if (!rval)
			rval = smia_i2c_write_reg(client, SMIA_REG_8BIT,
						  0x0100, 0x01);
	 } else {
		rval = smia_i2c_write_reg(client, SMIA_REG_8BIT, 0x0100, 0x00);
	 }

	if (rval == 0)
		sensor->streaming = streaming;

	return rval;
}

static int smia_power_off(struct v4l2_subdev *subdev)
{
	struct smia_sensor *sensor = to_smia_sensor(subdev);
	int rval;

	rval = sensor->platform_data->set_xclk(subdev, 0);
	rval |= sensor->platform_data->set_xshutdown(subdev, 0);
	rval |= regulator_disable(sensor->vana);

	return rval;
}

static int smia_power_on(struct v4l2_subdev *subdev)
{
	struct smia_sensor *sensor = to_smia_sensor(subdev);
	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	struct smia_reglist *reglist = NULL;
	int rval;
	unsigned int hz = DEFAULT_XCLK;

	if (sensor->meta_reglist) {
		reglist = smia_reglist_find_type(sensor->meta_reglist,
						 SMIA_REGLIST_POWERON);
		hz = reglist->mode.ext_clock;
	}

	rval = regulator_enable(sensor->vana);
	if (rval)
		goto out;

	rval = sensor->platform_data->set_xshutdown(subdev, 1);
	if (rval)
		goto out;

	sensor->platform_data->set_xclk(subdev, hz);

	/*
	 * At least 10 ms is required between xshutdown up and first
	 * i2c transaction. Clock must start at least 2400 cycles
	 * before first i2c transaction.
	 */
	msleep(10);

	if (reglist) {
		rval = smia_i2c_write_regs(client, reglist->regs);
		if (rval)
			goto out;
	}

out:
	if (rval)
		smia_power_off(subdev);

	return rval;
}

static struct v4l2_queryctrl smia_ctrls[] = {
	{
		.id		= V4L2_CID_GAIN,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Analog gain",
		.flags		= V4L2_CTRL_FLAG_SLIDER,
	},
	{
		.id		= V4L2_CID_EXPOSURE,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Exposure time [us]",
		.flags		= V4L2_CTRL_FLAG_SLIDER,
	},
};

/* --------------------------------------------------------------------------
 * V4L2 subdev video operations
 */
static int smia_enum_mbus_code(struct v4l2_subdev *subdev,
			       struct v4l2_subdev_fh *fh,
			       struct v4l2_subdev_mbus_code_enum *code)
{
	struct smia_sensor *sensor = to_smia_sensor(subdev);

	return smia_reglist_enum_mbus_code(sensor->meta_reglist, code);
}

static int smia_enum_frame_size(struct v4l2_subdev *subdev,
				struct v4l2_subdev_fh *fh,
				struct v4l2_subdev_frame_size_enum *fse)
{
	struct smia_sensor *sensor = to_smia_sensor(subdev);

	return smia_reglist_enum_frame_size(sensor->meta_reglist, fse);
}

static int smia_enum_frame_ival(struct v4l2_subdev *subdev,
				struct v4l2_subdev_fh *fh,
				struct v4l2_subdev_frame_interval_enum *fie)
{
	struct smia_sensor *sensor = to_smia_sensor(subdev);

	return smia_reglist_enum_frame_ival(sensor->meta_reglist, fie);
}

static struct v4l2_mbus_framefmt *
__smia_get_pad_format(struct smia_sensor *sensor, struct v4l2_subdev_fh *fh,
		      unsigned int pad, enum v4l2_subdev_format_whence which)
{
	if (pad != 0)
		return NULL;

	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(fh, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &sensor->format;
	default:
		return NULL;
	}
}

static int smia_get_pad_format(struct v4l2_subdev *subdev,
			       struct v4l2_subdev_fh *fh,
			       struct v4l2_subdev_format *fmt)
{
	struct smia_sensor *sensor = to_smia_sensor(subdev);
	struct v4l2_mbus_framefmt *format;

	format = __smia_get_pad_format(sensor, fh, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	fmt->format = *format;
	return 0;
}

static int smia_set_pad_format(struct v4l2_subdev *subdev,
			       struct v4l2_subdev_fh *fh,
			       struct v4l2_subdev_format *fmt)
{
	struct smia_sensor *sensor = to_smia_sensor(subdev);
	struct v4l2_mbus_framefmt *format;
	struct smia_reglist *reglist;

	format = __smia_get_pad_format(sensor, fh, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	reglist = smia_reglist_find_mode_fmt(sensor->meta_reglist,
					     &fmt->format);
	smia_reglist_to_mbus(reglist, &fmt->format);
	*format = fmt->format;

	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		sensor->current_reglist = reglist;
		sensor->platform_data->configure_interface(subdev,
			&sensor->current_reglist->mode);
	}

	return 0;
}

static int smia_get_frame_interval(struct v4l2_subdev *subdev,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct smia_sensor *sensor = to_smia_sensor(subdev);

	memset(fi, 0, sizeof(*fi));
	fi->interval = sensor->current_reglist->mode.timeperframe;

	return 0;
}

static int smia_set_frame_interval(struct v4l2_subdev *subdev,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct smia_sensor *sensor = to_smia_sensor(subdev);
	struct smia_reglist *reglist;

	reglist = smia_reglist_find_mode_ival(sensor->meta_reglist,
					      sensor->current_reglist,
					      &fi->interval);
	if (!reglist)
		return -EINVAL;

	sensor->current_reglist = reglist;
	sensor->platform_data->configure_interface(subdev,
		&sensor->current_reglist->mode);

	return smia_update_controls(subdev);
}

static int
smia_get_skip_top_lines(struct v4l2_subdev *subdev, u32 *lines)
{
	struct smia_sensor *sensor = to_smia_sensor(subdev);

	*lines = sensor->sof_rows;
	return 0;
}

/* --------------------------------------------------------------------------
 * V4L2 subdev core operations
 */

static int smia_dev_init(struct v4l2_subdev *subdev)
{
	struct smia_sensor *sensor = to_smia_sensor(subdev);
	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	struct v4l2_mbus_framefmt *format;
	char name[SMIA_MAX_LEN];
	u32 model_id, revision_number, manufacturer_id, smia_version;
	int i, rval;

	sensor->vana = regulator_get(&client->dev, "VANA");
	if (IS_ERR(sensor->vana)) {
		dev_err(&client->dev, "could not get regulator for vana\n");
		return -ENODEV;
	}

	rval = smia_power_on(subdev);
	if (rval) {
		rval = -ENODEV;
		goto out_regulator_release;
	}

	/* Read and check sensor identification registers */
	if (smia_i2c_read_reg(client, SMIA_REG_16BIT, REG_MODEL_ID, &model_id)
	    || smia_i2c_read_reg(client, SMIA_REG_8BIT,
				 REG_REVISION_NUMBER, &revision_number)
	    || smia_i2c_read_reg(client, SMIA_REG_8BIT,
				 REG_MANUFACTURER_ID, &manufacturer_id)
	    || smia_i2c_read_reg(client, SMIA_REG_8BIT,
				 REG_SMIA_VERSION, &smia_version)) {
		rval = -ENODEV;
		goto out_poweroff;
	}

	sensor->revision_number = revision_number;
	sensor->smia_version    = smia_version;

	if (smia_version != 10) {
		/* We support only SMIA version 1.0 at the moment */
		dev_err(&client->dev,
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

	/* This will be exported go the the v4l2_subdev description (through a
	 * string control) when we'll have one.
	 */
	if (i >= ARRAY_SIZE(smia_sensors))
		i = 0;			/* Unknown sensor */
	sensor->type = &smia_sensors[i];
	strlcpy(sensor->name, smia_sensors[i].name, sizeof(sensor->name));
	strlcpy(subdev->name, sensor->name, sizeof(subdev->name));


	/* Read sensor frame format */
	smia_read_frame_fmt(subdev);

	/* Initialize V4L2 controls */
	smia_init_controls(subdev);

	/* Import firmware */
	snprintf(name, sizeof(name), "%s-%02x-%04x-%02x.bin",
		 SMIA_SENSOR_NAME, sensor->type->manufacturer_id,
		 sensor->type->model_id, sensor->revision_number);

	if (request_firmware(&sensor->fw, name, &client->dev)) {
		dev_err(&client->dev, "can't load firmware %s\n", name);
		rval = -ENODEV;
		goto out_poweroff;
	}

	sensor->meta_reglist = (struct smia_meta_reglist *)sensor->fw->data;

	rval = smia_reglist_import(sensor->meta_reglist);
	if (rval) {
		dev_err(&client->dev,
			"invalid register list %s, import failed\n",
			name);
		goto out_release;
	}

	/* Select initial mode */
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

	sensor->platform_data->configure_interface(subdev,
		&sensor->current_reglist->mode);

	format = __smia_get_pad_format(sensor, NULL, 0,
				       V4L2_SUBDEV_FORMAT_ACTIVE);
	smia_reglist_to_mbus(sensor->current_reglist, format);

	sensor->streaming = 0;
	rval = smia_power_off(subdev);
	if (rval)
		goto out_release;

	return 0;

out_release:
	release_firmware(sensor->fw);
out_poweroff:
	sensor->meta_reglist = NULL;
	sensor->fw = NULL;
	smia_power_off(subdev);
out_regulator_release:
	regulator_put(sensor->vana);
	sensor->vana = NULL;

	return rval;
}

static int
smia_set_config(struct v4l2_subdev *subdev, int irq, void *platform_data)
{
	struct smia_sensor *sensor = to_smia_sensor(subdev);

	if (platform_data == NULL)
		return -ENODEV;

	sensor->platform_data = platform_data;

	return smia_dev_init(subdev);
}

static int smia_query_ctrl(struct v4l2_subdev *subdev, struct v4l2_queryctrl *a)
{
	struct smia_sensor *sensor = to_smia_sensor(subdev);
	int rval, ctrl;

	rval = smia_ctrl_query(smia_ctrls, ARRAY_SIZE(smia_ctrls), a);
	if (rval) {
		return smia_mode_query(smia_mode_ctrls,
					ARRAY_SIZE(smia_mode_ctrls), a);
	}

	ctrl = CID_TO_CTRL(a->id);
	if (ctrl < 0)
		return ctrl;

	a->minimum       = sensor->controls[ctrl].minimum;
	a->maximum       = sensor->controls[ctrl].maximum;
	a->step	         = sensor->controls[ctrl].step;
	a->default_value = sensor->controls[ctrl].default_value;

	return 0;
}

static int smia_get_ctrl(struct v4l2_subdev *subdev, struct v4l2_control *vc)
{
	struct smia_sensor *sensor = to_smia_sensor(subdev);
	int ctrl;

	int rval = smia_mode_g_ctrl(smia_mode_ctrls,
			ARRAY_SIZE(smia_mode_ctrls),
			vc, &sensor->current_reglist->mode);
	if (rval == 0)
		return 0;

	ctrl = CID_TO_CTRL(vc->id);
	if (ctrl < 0)
		return ctrl;
	vc->value = sensor->controls[ctrl].value;

	return 0;
}

static int smia_set_ctrl(struct v4l2_subdev *subdev, struct v4l2_control *vc)
{
	struct smia_sensor *sensor = to_smia_sensor(subdev);

	int ctrl = CID_TO_CTRL(vc->id);
	if (ctrl < 0)
		return ctrl;

	return sensor->controls[ctrl].set(subdev, vc->value);

}

static int smia_do_ext_ctrl(struct v4l2_subdev *subdev,
			      struct v4l2_ext_controls *c,
			      int (*do_ctrl)(struct v4l2_subdev *,
					     struct v4l2_control *))
{
	struct v4l2_control qctrl;
	int i, err = 0;

	if (!(c->ctrl_class == V4L2_CTRL_CLASS_CAMERA ||
	      c->ctrl_class == V4L2_CTRL_CLASS_USER))
		return -EINVAL;

	for (i = 0; i < c->count; i++) {
		qctrl.id = c->controls[i].id;
		qctrl.value = c->controls[i].value;
		err = do_ctrl(subdev, &qctrl);
		if (err) {
			c->error_idx = i;
			return err;
		}
		c->controls[i].value = qctrl.value;
	}

	return 0;
}

static int smia_get_ext_ctrls(struct v4l2_subdev *subdev,
				struct v4l2_ext_controls *c)
{
	return smia_do_ext_ctrl(subdev, c, smia_get_ctrl);
}

static int smia_set_ext_ctrls(struct v4l2_subdev *subdev,
				struct v4l2_ext_controls *c)
{
	return smia_do_ext_ctrl(subdev, c, smia_set_ctrl);
}

static int
smia_set_power(struct v4l2_subdev *subdev, int on)
{
	struct smia_sensor *sensor = to_smia_sensor(subdev);
	int rval = 0;

	/* If we are already in this mode, do nothing */
	if (sensor->power == on)
		return 0;

	if (on) {
		rval = smia_power_on(subdev);
	} else {
		rval = smia_power_off(subdev);
		if (rval == 0)
			sensor->streaming = 0;
	}

	if (rval == 0)
		sensor->power = on;

	return rval;
}

static const struct v4l2_subdev_video_ops smia_video_ops = {
	.s_stream = smia_s_stream,
	.g_frame_interval = smia_get_frame_interval,
	.s_frame_interval = smia_set_frame_interval,
};

static const struct v4l2_subdev_core_ops smia_core_ops = {
	.s_config = smia_set_config,
	.queryctrl = smia_query_ctrl,
	.g_ctrl = smia_get_ctrl,
	.s_ctrl = smia_set_ctrl,
	.g_ext_ctrls = smia_get_ext_ctrls,
	.s_ext_ctrls = smia_set_ext_ctrls,
	.s_power = smia_set_power,
};

static const struct v4l2_subdev_pad_ops smia_pad_ops = {
	.enum_mbus_code = smia_enum_mbus_code,
	.enum_frame_size = smia_enum_frame_size,
	.enum_frame_interval = smia_enum_frame_ival,
	.get_fmt = smia_get_pad_format,
	.set_fmt = smia_set_pad_format,
};

static const struct v4l2_subdev_sensor_ops smia_sensor_ops = {
	.g_skip_top_lines = smia_get_skip_top_lines,
};

static const struct v4l2_subdev_ops smia_ops = {
	.core = &smia_core_ops,
	.video = &smia_video_ops,
	.pad = &smia_pad_ops,
	.sensor = &smia_sensor_ops,
};

/* --------------------------------------------------------------------------
 * I2C driver
 */
#ifdef CONFIG_PM

static int smia_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct smia_sensor *sensor = to_smia_sensor(subdev);
	int ret, ss;

	if (!sensor->power)
		return 0;

	ss = sensor->streaming;
	if (sensor->streaming)
		smia_s_stream(subdev, 0);

	ret = smia_set_power(subdev, 0);
	if (ret < 0)
		return ret;

	sensor->power = 1;
	sensor->streaming = ss;
	return 0;
}

static int smia_resume(struct i2c_client *client)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct smia_sensor *sensor = to_smia_sensor(subdev);
	int rval;

	if (!sensor->power)
		return 0;

	sensor->power = 0;
	rval = smia_set_power(subdev, 1);
	if (rval)
		return rval;

	if (!sensor->streaming)
		return 0;

	sensor->streaming = 0;
	return smia_s_stream(subdev, 1);
}

#else

#define smia_suspend	NULL
#define smia_resume	NULL

#endif /* CONFIG_PM */

static int smia_probe(struct i2c_client *client,
			const struct i2c_device_id *devid)
{
	struct smia_sensor *sensor;
	int ret;

	sensor = kzalloc(sizeof(*sensor), GFP_KERNEL);
	if (sensor == NULL)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&sensor->subdev, client, &smia_ops);
	sensor->subdev.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	sensor->pad.flags = MEDIA_PAD_FL_OUTPUT;
	ret = media_entity_init(&sensor->subdev.entity, 1, &sensor->pad, 0);
	if (ret < 0)
		kfree(sensor);

	return ret;
}

static int __exit smia_remove(struct i2c_client *client)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct smia_sensor *sensor = to_smia_sensor(subdev);

	v4l2_device_unregister_subdev(&sensor->subdev);
	media_entity_cleanup(&sensor->subdev.entity);
	if (sensor->vana)
		regulator_put(sensor->vana);

	release_firmware(sensor->fw);
	kfree(sensor);
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
		printk(KERN_INFO "%s: failed registering " SMIA_SENSOR_NAME
			"\n", __func__);

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
module_init(smia_init);
module_exit(smia_exit);

MODULE_AUTHOR("Tuukka Toivonen <tuukka.o.toivonen@nokia.com>");
MODULE_DESCRIPTION("Generic SMIA-compatible camera sensor driver");
MODULE_LICENSE("GPL");
