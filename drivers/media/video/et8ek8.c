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
#include <linux/version.h>
#include <linux/kernel.h>

#include <media/smiaregs.h>
#include <media/v4l2-int-device.h>

#include "et8ek8.h"

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

/* Called to change the V4L2 gain control value. This function
 * rounds and clamps the given value and updates the V4L2 control value.
 * If power is on, also updates the sensor analog and digital gains.
 * gain is in 0.1 EV (exposure value) units.
 */
static int et8ek8_set_gain(struct et8ek8_sensor *sensor, s32 gain)
{
	struct et8ek8_gain new;
	int r;

	sensor->controls[CTRL_GAIN].value = clamp(gain,
		sensor->controls[CTRL_GAIN].minimum,
		sensor->controls[CTRL_GAIN].maximum);

	if (sensor->power == V4L2_POWER_OFF)
		return 0;

	new = et8ek8_gain_table[sensor->controls[CTRL_GAIN].value];

	/* FIXME: optimise I2C writes! */
	r = smia_i2c_write_reg(sensor->i2c_client, SMIA_REG_8BIT,
				  0x124a, new.analog >> 8);
	if (r)
		return r;
	r = smia_i2c_write_reg(sensor->i2c_client, SMIA_REG_8BIT,
				  0x1249, new.analog & 0xff);
	if (r)
		return r;

	r = smia_i2c_write_reg(sensor->i2c_client, SMIA_REG_8BIT,
				  0x124d, new.digital >> 8);
	if (r)
		return r;
	r = smia_i2c_write_reg(sensor->i2c_client, SMIA_REG_8BIT,
				  0x124c, new.digital & 0xff);

	return r;
}

/* Called to change the V4L2 exposure control value. This function
 * rounds and clamps the given value and updates the V4L2 control value.
 * If power is on, also update the sensor exposure time.
 * exptime is in microseconds.
 */
static int et8ek8_set_exposure(struct et8ek8_sensor *sensor, s32 exptime)
{
	unsigned int clock;	/* Pixel clock in Hz>>10 fixed point */
	unsigned int rt;	/* Row time in .8 fixed point */
	unsigned int rows;	/* Exposure value as written to HW (ie. rows) */

	exptime = clamp(exptime, sensor->controls[CTRL_EXPOSURE].minimum,
				 sensor->controls[CTRL_EXPOSURE].maximum);

	/* Assume that the maximum exposure time is at most ~8 s,
	 * and the maximum width (with blanking) ~8000 pixels.
	 * The formula here is in principle as simple as
	 *    rows = exptime / 1e6 / width * pixel_clock
	 * but to get accurate results while coping with value ranges,
	 * have to do some fixed point math.
	 */
	clock = sensor->current_reglist->mode.pixel_clock;
	clock = (clock + (1 << 9)) >> 10;
	rt = sensor->current_reglist->mode.width * (1000000 >> 2);
	rt = (rt + (clock >> 1)) / clock;
	rows = ((exptime << 8) + (rt >> 1)) / rt;

	/* Set the V4L2 control for exposure time to the rounded value */
	sensor->controls[CTRL_EXPOSURE].value = (rt * rows + (1 << 7)) >> 8;

	if (sensor->power == V4L2_POWER_OFF)
		return 0;

	return smia_i2c_write_reg(sensor->i2c_client, SMIA_REG_16BIT, 0x1243,
				  swab16(rows));
}

static int et8ek8_set_test_pattern(struct et8ek8_sensor *sensor, s32 mode)
{
	int cbh_mode, cbv_mode, tp_mode, din_sw, r1420, rval;

	if (mode < 0 || mode > 8)
		return -EINVAL;

	sensor->controls[CTRL_TEST_PATTERN].value = mode;

	if (sensor->power == V4L2_POWER_OFF)
		return 0;

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

	rval = smia_i2c_write_reg(sensor->i2c_client, SMIA_REG_8BIT,
				  0x111B, tp_mode << 4);
	if (rval)
		goto out;

	rval = smia_i2c_write_reg(sensor->i2c_client, SMIA_REG_8BIT,
				  0x1121, cbh_mode << 7);
	if (rval)
		goto out;

	rval = smia_i2c_write_reg(sensor->i2c_client, SMIA_REG_8BIT,
				  0x1124, cbv_mode << 7);
	if (rval)
		goto out;

	rval = smia_i2c_write_reg(sensor->i2c_client, SMIA_REG_8BIT,
				  0x112C, din_sw);
	if (rval)
		goto out;

	rval = smia_i2c_write_reg(sensor->i2c_client, SMIA_REG_8BIT,
				  0x1420, r1420);
	if (rval)
		goto out;

out:
	return rval;

}

static int et8ek8_update_controls(struct v4l2_int_device *s)
{
	struct et8ek8_sensor *sensor = s->priv;
	int i;
	unsigned int rt;	/* Row time in us */
	unsigned int clock;	/* Pixel clock in Hz>>2 fixed point */

	if (sensor->current_reglist->mode.pixel_clock <= 0 ||
	    sensor->current_reglist->mode.width <= 0) {
		dev_err(&sensor->i2c_client->dev, "bad firmware\n");
		return -EIO;
	}

	clock = sensor->current_reglist->mode.pixel_clock;
	clock = (clock + (1 << 1)) >> 2;
	rt = sensor->current_reglist->mode.width * (1000000 >> 2);
	rt = (rt + (clock >> 1)) / clock;

	sensor->controls[CTRL_EXPOSURE].minimum = rt;
	sensor->controls[CTRL_EXPOSURE].maximum =
		sensor->current_reglist->mode.max_exp * rt;
	sensor->controls[CTRL_EXPOSURE].step = rt;
	sensor->controls[CTRL_EXPOSURE].default_value =
		sensor->controls[CTRL_EXPOSURE].maximum;
	if (sensor->controls[CTRL_EXPOSURE].value == 0)
		sensor->controls[CTRL_EXPOSURE].value =
			sensor->controls[CTRL_EXPOSURE].maximum;

	/* Adjust V4L2 control values and write them to the sensor */

	for (i=0; i<ARRAY_SIZE(sensor->controls); i++) {
		int rval = sensor->controls[i].set(sensor,
			sensor->controls[i].value);
		if (rval)
			return rval;
	}
	return 0;
}

static int et8ek8_configure(struct v4l2_int_device *s)
{
	struct et8ek8_sensor *sensor = s->priv;
	int rval;

	rval = et8ek8_update_controls(s);
	if (rval)
		goto fail;

	rval = smia_i2c_write_regs(sensor->i2c_client,
				   sensor->current_reglist->regs);
	if (rval)
		goto fail;

	rval = sensor->platform_data->configure_interface(
		s, &sensor->current_reglist->mode);
	if (rval)
		goto fail;

	return 0;

fail:
	dev_err(&sensor->i2c_client->dev, "sensor configuration failed\n");
	return rval;
}

static int et8ek8_stream_on(struct v4l2_int_device *s)
{
	struct et8ek8_sensor *sensor = s->priv;
	return smia_i2c_write_reg(sensor->i2c_client,
				  SMIA_REG_8BIT, 0x1252, 0xB0);
}

static int et8ek8_stream_off(struct v4l2_int_device *s)
{
	struct et8ek8_sensor *sensor = s->priv;
	return smia_i2c_write_reg(sensor->i2c_client,
				  SMIA_REG_8BIT, 0x1252, 0x30);
}

static int et8ek8_power_off(struct v4l2_int_device *s)
{
	struct et8ek8_sensor *sensor = s->priv;
	int rval;

	rval = sensor->platform_data->power_off(s);
	if (rval)
		return rval;
	udelay(1);
	rval = sensor->platform_data->set_xclk(s, 0);
	return rval;
}

static int et8ek8_power_on(struct v4l2_int_device *s)
{
	struct et8ek8_sensor *sensor = s->priv;
	unsigned int hz = ET8EK8_XCLK_HZ;
	int val, rval;

	if (sensor->current_reglist)
		hz = sensor->current_reglist->mode.ext_clock;

	rval = sensor->platform_data->set_xclk(s, hz);
	if (rval)
		goto out;

	udelay(10);			/* I wish this is a good value */

	rval = sensor->platform_data->power_on(s);
	if (rval)
		goto out;

	msleep(5000*1000/hz+1);				/* Wait 5000 cycles */

	if (sensor->meta_reglist) {
		rval = smia_i2c_reglist_find_write(sensor->i2c_client,
						   sensor->meta_reglist,
						   SMIA_REGLIST_POWERON);
		if (rval)
			goto out;
	}

	rval = et8ek8_stream_off(s);
	if (rval)
		goto out;

#ifdef USE_CRC
	rval = smia_i2c_read_reg(sensor->i2c_client,
				 SMIA_REG_8BIT, 0x1263, &val);
	if (rval)
		goto out;
#if USE_CRC
	val |= (1<<4);
#else
	val &= ~(1<<4);
#endif
	rval = smia_i2c_write_reg(sensor->i2c_client,
				  SMIA_REG_8BIT, 0x1263, val);
	if (rval)
		goto out;
#endif

out:
	if (rval)
		et8ek8_power_off(s);

	return rval;
}

static struct v4l2_queryctrl et8ek8_ctrls[] = {
	{
		.id		= V4L2_CID_GAIN,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Gain [0.1 EV]",
		.flags		= V4L2_CTRL_FLAG_SLIDER,
	},
	{
		.id		= V4L2_CID_EXPOSURE,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Exposure time [us]",
		.flags		= V4L2_CTRL_FLAG_SLIDER,
	},
	{
		.id		= V4L2_CID_TEST_PATTERN,
		.type		= V4L2_CTRL_TYPE_MENU,
		.name		= "Test pattern mode",
		.flags		= 0,
		.minimum	= 0,
		.maximum	= 8,
		.step		= 1,
		.default_value	= 0,
	},
};

static const __u32 et8ek8_mode_ctrls[] = {
	V4L2_CID_MODE_FRAME_WIDTH,
	V4L2_CID_MODE_FRAME_HEIGHT,
	V4L2_CID_MODE_VISIBLE_WIDTH,
	V4L2_CID_MODE_VISIBLE_HEIGHT,
	V4L2_CID_MODE_PIXELCLOCK,
	V4L2_CID_MODE_SENSITIVITY,
};

static int et8ek8_ioctl_queryctrl(struct v4l2_int_device *s,
				  struct v4l2_queryctrl *a)
{
	struct et8ek8_sensor *sensor = s->priv;
	int rval, ctrl;

	rval = smia_ctrl_query(et8ek8_ctrls, ARRAY_SIZE(et8ek8_ctrls), a);
	if (rval) {
		return smia_mode_query(et8ek8_mode_ctrls,
					ARRAY_SIZE(et8ek8_mode_ctrls), a);
	}

	ctrl = CID_TO_CTRL(a->id);
	if (ctrl < 0)
		return ctrl;

	a->minimum       = sensor->controls[ctrl].minimum;
	a->maximum       = sensor->controls[ctrl].maximum;
	a->step          = sensor->controls[ctrl].step;
	a->default_value = sensor->controls[ctrl].default_value;

	return 0;
}

static int et8ek8_ioctl_querymenu(struct v4l2_int_device *s,
				  struct v4l2_querymenu *qm)
{
	static const char *menu_name[] = {
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

	switch (qm->id) {
	case V4L2_CID_TEST_PATTERN:
		if (qm->index >= ARRAY_SIZE(menu_name))
			return -EINVAL;
		strcpy(qm->name, menu_name[qm->index]);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int et8ek8_ioctl_g_ctrl(struct v4l2_int_device *s,
			       struct v4l2_control *vc)
{
	struct et8ek8_sensor *sensor = s->priv;
	int ctrl;

	int rval = smia_mode_g_ctrl(et8ek8_mode_ctrls,
			ARRAY_SIZE(et8ek8_mode_ctrls),
			vc, &sensor->current_reglist->mode);
	if (rval == 0)
		return 0;

	ctrl = CID_TO_CTRL(vc->id);
	if (ctrl < 0)
		return ctrl;
	vc->value = sensor->controls[ctrl].value;
	return 0;
}

static int et8ek8_ioctl_s_ctrl(struct v4l2_int_device *s,
			       struct v4l2_control *vc)
{
	struct et8ek8_sensor *sensor = s->priv;
	int ctrl = CID_TO_CTRL(vc->id);
	if (ctrl < 0)
		return ctrl;
	return sensor->controls[ctrl].set(sensor, vc->value);
}

static int et8ek8_ioctl_enum_fmt_cap(struct v4l2_int_device *s,
				     struct v4l2_fmtdesc *fmt)
{
	struct et8ek8_sensor *sensor = s->priv;

	return smia_reglist_enum_fmt(sensor->meta_reglist, fmt);
}


static int et8ek8_ioctl_g_fmt_cap(struct v4l2_int_device *s,
				  struct v4l2_format *f)
{
	struct et8ek8_sensor *sensor = s->priv;
	struct v4l2_pix_format *pix = &f->fmt.pix;

	pix->width = sensor->current_reglist->mode.window_width;
	pix->height = sensor->current_reglist->mode.window_height;
	pix->pixelformat = sensor->current_reglist->mode.pixel_format;

	return 0;
}

static int et8ek8_ioctl_s_fmt_cap(struct v4l2_int_device *s,
				  struct v4l2_format *f)
{
	struct et8ek8_sensor *sensor = s->priv;
	struct smia_reglist *reglist;

	reglist = smia_reglist_find_mode_fmt(sensor->meta_reglist,
					     sensor->current_reglist, f);

	if (!reglist)
		return -EINVAL;

	if (sensor->power != V4L2_POWER_OFF &&
	    sensor->current_reglist->mode.ext_clock != reglist->mode.ext_clock)
		return -EINVAL;

	sensor->current_reglist = reglist;

	return et8ek8_update_controls(s);
}

static int et8ek8_ioctl_g_parm(struct v4l2_int_device *s,
			       struct v4l2_streamparm *a)
{
	struct et8ek8_sensor *sensor = s->priv;
	struct v4l2_captureparm *cparm = &a->parm.capture;

	if (a->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	memset(a, 0, sizeof(*a));
	a->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	cparm->capability = V4L2_CAP_TIMEPERFRAME;
	cparm->timeperframe = sensor->current_reglist->mode.timeperframe;

	return 0;
}

static int et8ek8_ioctl_s_parm(struct v4l2_int_device *s,
			       struct v4l2_streamparm *a)
{
	struct et8ek8_sensor *sensor = s->priv;
	struct smia_reglist *reglist;

	reglist = smia_reglist_find_mode_streamparm(sensor->meta_reglist,
						    sensor->current_reglist, a);

	if (!reglist)
		return -EINVAL;

	if (sensor->power != V4L2_POWER_OFF &&
	    sensor->current_reglist->mode.ext_clock != reglist->mode.ext_clock)
		return -EINVAL;

	sensor->current_reglist = reglist;

	return et8ek8_update_controls(s);
}

static int et8ek8_g_priv_mem(struct v4l2_int_device *s)
{
	struct et8ek8_sensor *sensor = s->priv;
	struct i2c_client *client = sensor->i2c_client;
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
		rval = smia_i2c_write_reg(sensor->i2c_client,
					  SMIA_REG_8BIT,
					  0x0001,
					  0xe0 | (offset >> 3));
		if (rval < 0)
			goto out;

		/* Wait for status bit */
		i = 1000;
		do {
			u32 status;
			rval = smia_i2c_read_reg(sensor->i2c_client,
						 SMIA_REG_8BIT,
						 0x0003,
						 &status);
			if (rval < 0)
				goto out;
			if ((status & 0x08) == 0)
				break;
			if (--i == 0) {
				rval = -EIO;
				goto out;
			}
			msleep(1);
		} while (1);

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

static int et8ek8_ioctl_dev_init(struct v4l2_int_device *s)
{
	struct et8ek8_sensor *sensor = s->priv;
	char name[FIRMWARE_NAME_MAX];
	int rval, rev_l, rev_h;

	rval = et8ek8_power_on(s);
	if (rval)
		return -ENODEV;

	if (smia_i2c_read_reg(sensor->i2c_client, SMIA_REG_8BIT,
			      REG_REVISION_NUMBER_L, &rev_l) != 0
	    || smia_i2c_read_reg(sensor->i2c_client, SMIA_REG_8BIT,
				 REG_REVISION_NUMBER_H, &rev_h) != 0) {
		dev_err(&sensor->i2c_client->dev,
			"no et8ek8 sensor detected\n");
		rval = -ENODEV;
		goto out_poweroff;
	}
	sensor->version = (rev_h << 8) + rev_l;
	if (sensor->version != ET8EK8_REV_1
	    && sensor->version != ET8EK8_REV_2)
		dev_info(&sensor->i2c_client->dev,
			 "unknown version 0x%x detected, "
			 "continuing anyway\n", sensor->version);

	snprintf(name, FIRMWARE_NAME_MAX, "%s-%4.4x.bin", ET8EK8_NAME,
		 sensor->version);
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

	rval = smia_i2c_reglist_find_write(sensor->i2c_client,
					   sensor->meta_reglist,
					   SMIA_REGLIST_POWERON);
	if (rval) {
		dev_err(&sensor->i2c_client->dev,
			"invalid register list %s, no POWERON mode found\n",
			name);
		goto out_release;
	}
	rval = et8ek8_stream_on(s);	/* Needed to be able to read EEPROM */
	if (rval)
		goto out_release;
	rval = et8ek8_g_priv_mem(s);
	if (rval)
		dev_warn(&sensor->i2c_client->dev,
			"can not read OTP (EEPROM) memory from sensor\n");
	rval = et8ek8_stream_off(s);
	if (rval)
		goto out_release;

	rval = et8ek8_power_off(s);
	if (rval)
		goto out_release;

	return 0;

out_release:
	release_firmware(sensor->fw);
out_poweroff:
	sensor->meta_reglist = NULL;
	sensor->fw = NULL;
	et8ek8_power_off(s);

	return rval;
}

static int et8ek8_ioctl_s_power(struct v4l2_int_device *s,
				enum v4l2_power new_state)
{
	struct et8ek8_sensor *sensor = s->priv;
	enum v4l2_power old_state = sensor->power;
	int rval = 0;

	/* If we are already in this mode, do nothing */
	if (old_state == new_state)
		return 0;

	/* Disable power if so requested (it was enabled) */
	if (new_state == V4L2_POWER_OFF) {
		rval = et8ek8_stream_off(s);
		if (rval)
			dev_err(&sensor->i2c_client->dev,
				"can not stop streaming\n");
		rval = et8ek8_power_off(s);
		goto out;
	}

	/* Either STANDBY or ON requested */

	/* Enable power and move to standby if it was off */
	if (old_state == V4L2_POWER_OFF) {
		rval = et8ek8_power_on(s);
		if (rval)
			goto out;
	}

	/* Now sensor is powered (standby or streaming) */

	if (new_state == V4L2_POWER_ON) {
		/* Standby -> streaming */
		sensor->power = V4L2_POWER_ON;
		rval = et8ek8_configure(s);
		if (rval) {
			et8ek8_stream_off(s);
			if (old_state == V4L2_POWER_OFF)
				et8ek8_power_off(s);
			goto out;
		}
		rval = et8ek8_stream_on(s);
	} else {
		/* Streaming -> standby */
		rval = et8ek8_stream_off(s);
	}

out:
	sensor->power = (rval == 0) ? new_state : old_state;
	return rval;
}

static int et8ek8_ioctl_g_priv(struct v4l2_int_device *s, void *priv)
{
	struct et8ek8_sensor *sensor = s->priv;

	return sensor->platform_data->g_priv(s, priv);
}

static int et8ek8_ioctl_enum_framesizes(struct v4l2_int_device *s,
					struct v4l2_frmsizeenum *frm)
{
	struct et8ek8_sensor *sensor = s->priv;

	return smia_reglist_enum_framesizes(sensor->meta_reglist, frm);
}

static int et8ek8_ioctl_enum_frameintervals(struct v4l2_int_device *s,
					    struct v4l2_frmivalenum *frm)
{
	struct et8ek8_sensor *sensor = s->priv;

	return smia_reglist_enum_frameintervals(sensor->meta_reglist, frm);
}

static ssize_t
et8ek8_priv_mem_read(struct device *dev, struct device_attribute *attr,
		     char *buf)
{
	struct et8ek8_sensor *sensor = dev_get_drvdata(dev);

#if PAGE_SIZE < ET8EK8_PRIV_MEM_SIZE
#error PAGE_SIZE too small!
#endif

	memcpy(buf, sensor->priv_mem, ET8EK8_PRIV_MEM_SIZE);

	return ET8EK8_PRIV_MEM_SIZE;
}
static DEVICE_ATTR(priv_mem, S_IRUGO, et8ek8_priv_mem_read, NULL);

static struct v4l2_int_ioctl_desc et8ek8_ioctl_desc[] = {
	{ vidioc_int_enum_fmt_cap_num,
	  (v4l2_int_ioctl_func *)et8ek8_ioctl_enum_fmt_cap },
	{ vidioc_int_try_fmt_cap_num,
	  (v4l2_int_ioctl_func *)et8ek8_ioctl_g_fmt_cap },
	{ vidioc_int_g_fmt_cap_num,
	  (v4l2_int_ioctl_func *)et8ek8_ioctl_g_fmt_cap },
	{ vidioc_int_s_fmt_cap_num,
	  (v4l2_int_ioctl_func *)et8ek8_ioctl_s_fmt_cap },
	{ vidioc_int_queryctrl_num,
	  (v4l2_int_ioctl_func *)et8ek8_ioctl_queryctrl },
	{ vidioc_int_querymenu_num,
	  (v4l2_int_ioctl_func *)et8ek8_ioctl_querymenu },
	{ vidioc_int_g_ctrl_num,
	  (v4l2_int_ioctl_func *)et8ek8_ioctl_g_ctrl },
	{ vidioc_int_s_ctrl_num,
	  (v4l2_int_ioctl_func *)et8ek8_ioctl_s_ctrl },
	{ vidioc_int_g_parm_num,
	  (v4l2_int_ioctl_func *)et8ek8_ioctl_g_parm },
	{ vidioc_int_s_parm_num,
	  (v4l2_int_ioctl_func *)et8ek8_ioctl_s_parm },
	{ vidioc_int_s_power_num,
	  (v4l2_int_ioctl_func *)et8ek8_ioctl_s_power },
	{ vidioc_int_g_priv_num,
	  (v4l2_int_ioctl_func *)et8ek8_ioctl_g_priv },
	{ vidioc_int_enum_framesizes_num,
	  (v4l2_int_ioctl_func *)et8ek8_ioctl_enum_framesizes },
	{ vidioc_int_enum_frameintervals_num,
	  (v4l2_int_ioctl_func *)et8ek8_ioctl_enum_frameintervals },
	{ vidioc_int_dev_init_num,
	  (v4l2_int_ioctl_func *)et8ek8_ioctl_dev_init },
};

static struct v4l2_int_slave et8ek8_slave = {
	.ioctls = et8ek8_ioctl_desc,
	.num_ioctls = ARRAY_SIZE(et8ek8_ioctl_desc),
};

static struct et8ek8_sensor et8ek8;

static struct v4l2_int_device et8ek8_int_device = {
	.module = THIS_MODULE,
	.name = ET8EK8_NAME,
	.priv = &et8ek8,
	.type = v4l2_int_type_slave,
	.u = {
		.slave = &et8ek8_slave,
	},
};

#ifdef CONFIG_PM

static int et8ek8_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct et8ek8_sensor *sensor = dev_get_drvdata(&client->dev);
	enum v4l2_power resume_state = sensor->power;
	int rval;

	rval = et8ek8_ioctl_s_power(sensor->v4l2_int_device, V4L2_POWER_OFF);
	if (rval == 0)
		sensor->power = resume_state;
	return rval;
}

static int et8ek8_resume(struct i2c_client *client)
{
	struct et8ek8_sensor *sensor = dev_get_drvdata(&client->dev);
	enum v4l2_power resume_state = sensor->power;

	sensor->power = V4L2_POWER_OFF;
	return et8ek8_ioctl_s_power(sensor->v4l2_int_device, resume_state);
}

#else

#define et8ek8_suspend	NULL
#define et8ek8_resume	NULL

#endif /* CONFIG_PM */

static int et8ek8_probe(struct i2c_client *client,
			const struct i2c_device_id *devid)
{
	struct et8ek8_sensor *sensor = &et8ek8;
	int rval;

	if (i2c_get_clientdata(client))
		return -EBUSY;

	sensor->platform_data = client->dev.platform_data;

	if (sensor->platform_data == NULL)
		return -ENODEV;

	if (device_create_file(&client->dev, &dev_attr_priv_mem) != 0) {
		dev_err(&client->dev, "could not register sysfs entry\n");
		return -EBUSY;
	}

	sensor->v4l2_int_device = &et8ek8_int_device;

	/* Gain is initialized here permanently */
	sensor->controls[CTRL_GAIN].minimum           = 0;
	sensor->controls[CTRL_GAIN].maximum = ARRAY_SIZE(et8ek8_gain_table) - 1;
	sensor->controls[CTRL_GAIN].step              = 1;
	sensor->controls[CTRL_GAIN].default_value     = 0;
	sensor->controls[CTRL_GAIN].value             = 0;
	sensor->controls[CTRL_GAIN].set               = et8ek8_set_gain;

	/* Exposure parameters may change at each mode change, just zero here */
	sensor->controls[CTRL_EXPOSURE].minimum       = 0;
	sensor->controls[CTRL_EXPOSURE].maximum       = 0;
	sensor->controls[CTRL_EXPOSURE].step          = 0;
	sensor->controls[CTRL_EXPOSURE].default_value = 0;
	sensor->controls[CTRL_EXPOSURE].value         = 0;
	sensor->controls[CTRL_EXPOSURE].set           = et8ek8_set_exposure;

	/* Test pattern mode control */
	sensor->controls[CTRL_TEST_PATTERN].minimum       = et8ek8_ctrls[CTRL_TEST_PATTERN].minimum;
	sensor->controls[CTRL_TEST_PATTERN].maximum       = et8ek8_ctrls[CTRL_TEST_PATTERN].maximum;
	sensor->controls[CTRL_TEST_PATTERN].step          = et8ek8_ctrls[CTRL_TEST_PATTERN].step;
	sensor->controls[CTRL_TEST_PATTERN].default_value = et8ek8_ctrls[CTRL_TEST_PATTERN].default_value;
	sensor->controls[CTRL_TEST_PATTERN].value         = 0;
	sensor->controls[CTRL_TEST_PATTERN].set           = et8ek8_set_test_pattern;

	sensor->i2c_client = client;
	i2c_set_clientdata(client, sensor);
	dev_set_drvdata(&client->dev, sensor);

	rval = v4l2_int_device_register(sensor->v4l2_int_device);
	if (rval) {
		device_remove_file(&client->dev, &dev_attr_priv_mem);
		i2c_set_clientdata(client, NULL);
		dev_set_drvdata(&client->dev, NULL);
	}

	return rval;
}

static int __exit et8ek8_remove(struct i2c_client *client)
{
	struct et8ek8_sensor *sensor = i2c_get_clientdata(client);

	if (!client->adapter)
		return -ENODEV;	/* our client isn't attached */

	v4l2_int_device_unregister(sensor->v4l2_int_device);
	dev_set_drvdata(&client->dev, NULL);
	i2c_set_clientdata(client, NULL);
	device_remove_file(&client->dev, &dev_attr_priv_mem);
	release_firmware(sensor->fw);

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
		printk(KERN_ALERT "%s: failed at i2c_add_driver\n", __func__);

	return rval;
}

static void __exit et8ek8_exit(void)
{
	i2c_del_driver(&et8ek8_i2c_driver);
}

/*
 * FIXME: Menelaus isn't ready (?) at module_init stage, so use
 * late_initcall for now.
 */
late_initcall(et8ek8_init);
module_exit(et8ek8_exit);

MODULE_AUTHOR("Sakari Ailus <sakari.ailus@nokia.com>");
MODULE_DESCRIPTION("Toshiba ET8EK8 camera sensor driver");
MODULE_LICENSE("GPL");
