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
#define DEFAULT_EXPOSURE	33946		/* [us] */

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
static int smia_ioctl_enum_slaves(struct v4l2_int_device *s,
				  struct v4l2_slave_info *si);

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
	{ vidioc_int_enum_slaves_num,
	  (v4l2_int_ioctl_func *)smia_ioctl_enum_slaves },
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

static struct {
	u8 manufacturer_id;
	u16 model_id;
	char *name;
} smia_sensors[] = {
	{ 0x01, 0x022b, "vs6555" },
	{ 0x0c, 0x208a, "tcm8330md" },
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

/* Must be called with power already enabled on the sensor */
static int smia_configure(struct v4l2_int_device *s)
{
	struct smia_sensor *sensor = s->priv;
	int rval;

	rval = smia_i2c_reglist_find_write(sensor->i2c_client,
					   sensor->meta_reglist,
					   SMIA_REGLIST_POWERON);
	if (rval)
		goto fail;

	rval = smia_i2c_write_reg(sensor->i2c_client, SMIA_REG_8BIT,
				  REG_ANALOG_GAIN+1,
				  sensor->ctrl_gain << 4);
	if (rval)
		goto fail;
	rval = smia_i2c_write_reg(sensor->i2c_client,
		SMIA_REG_16BIT, REG_COARSE_EXPOSURE,
		smia_exposure_us_to_rows(sensor, &sensor->ctrl_exposure));
	if (rval)
		goto fail;

	rval = smia_i2c_write_regs(sensor->i2c_client,
				   sensor->current_reglist->regs);
	if (rval)
		goto fail;

	return rval;

fail:
	dev_err(&sensor->i2c_client->dev, "sensor configuration failed\n");
	return rval;

}

static int smia_setup_if(struct v4l2_int_device *s)
{
	struct smia_sensor *sensor = s->priv;
	int rval;
	unsigned int hz;

	if (sensor->current_reglist) {
		rval = sensor->platform_data->configure_interface(
			s,
			sensor->current_reglist->mode.window_width,
			sensor->current_reglist->mode.window_height);
		if (rval)
			return rval;
		hz = sensor->current_reglist->mode.ext_clock;
	} else {
		hz = DEFAULT_XCLK;
	}

	sensor->platform_data->set_xclk(s, hz);
	udelay(16*1000000/hz+1);		/* Wait 16 clocks */

	return 0;
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
	int rval;

	rval = sensor->platform_data->power_on(s);
	if (rval)
		goto out;

	rval = smia_setup_if(s);
	if (rval)
		goto out;

	/*
	 * At least 10 ms is required between xshutdown up and first
	 * i2c transaction. Clock must start at least 2400 cycles
	 * before first i2c transaction.
	 */
	msleep(10);

out:
	if (rval)
		smia_power_off(s);

	return rval;
}

static struct v4l2_queryctrl smia_ctrls[] = {
	{
		.id		= V4L2_CID_GAIN,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Analog gain",
		.minimum	= 0,
		.maximum	= 15,
		.step		= 1,
		.default_value	= 0,
		.flags		= V4L2_CTRL_FLAG_SLIDER,
	},
	{
		.id		= V4L2_CID_EXPOSURE,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Exposure time [us]",
		.minimum	= 0,
		.maximum	= DEFAULT_EXPOSURE,
		.step		= 1,
		.default_value	= DEFAULT_EXPOSURE,
		.flags		= V4L2_CTRL_FLAG_SLIDER,
	},
};

static int smia_ioctl_queryctrl(struct v4l2_int_device *s,
			   struct v4l2_queryctrl *a)
{
	struct smia_sensor *sensor = s->priv;
	int rval;

	rval = smia_ctrl_query(smia_ctrls, ARRAY_SIZE(smia_ctrls), a);
	if (rval) {
		return smia_mode_query(smia_mode_ctrls,
					ARRAY_SIZE(smia_mode_ctrls), a);
	}

	switch (a->id) {
	case V4L2_CID_EXPOSURE:
		if (sensor->current_reglist) {
			a->maximum = smia_exposure_rows_to_us(sensor,
				     sensor->current_reglist->mode.max_exp);
			a->step = smia_exposure_rows_to_us(sensor, 1);
			a->default_value = a->maximum;
		}
		break;
	}

	return 0;
}

static int smia_ioctl_g_ctrl(struct v4l2_int_device *s,
			struct v4l2_control *vc)
{
	struct smia_sensor *sensor = s->priv;

	int rval = smia_mode_g_ctrl(smia_mode_ctrls,
			ARRAY_SIZE(smia_mode_ctrls),
			vc, &sensor->current_reglist->mode);
	if (rval == 0)
		return 0;

	switch (vc->id) {
	case V4L2_CID_GAIN:
		vc->value = sensor->ctrl_gain;
		break;
	case V4L2_CID_EXPOSURE:
		vc->value = sensor->ctrl_exposure;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int smia_ioctl_s_ctrl(struct v4l2_int_device *s,
			struct v4l2_control *vc)
{
	struct smia_sensor *sensor = s->priv;
	int exposure_rows;
	int r = 0;

	switch (vc->id) {
	case V4L2_CID_GAIN:
		sensor->ctrl_gain = clamp(vc->value, 0, 15);
		if (sensor->power == V4L2_POWER_ON)
			r = smia_i2c_write_reg(sensor->i2c_client,
				SMIA_REG_8BIT, REG_ANALOG_GAIN+1,
				sensor->ctrl_gain << 4);
		break;
	case V4L2_CID_EXPOSURE:
		sensor->ctrl_exposure = vc->value;
		exposure_rows = smia_exposure_us_to_rows(sensor,
							&sensor->ctrl_exposure);
		if (sensor->power == V4L2_POWER_ON)
			r = smia_i2c_write_reg(sensor->i2c_client,
				SMIA_REG_16BIT, REG_COARSE_EXPOSURE,
				exposure_rows);
		break;
	default:
		return -EINVAL;
	}
	return r;
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

	if (reglist) {
		sensor->current_reglist = reglist;
		return 0;
	}

	return -EINVAL;
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

	if (reglist) {
		sensor->current_reglist = reglist;
		return 0;
	}

	return -EINVAL;
}

static int smia_dev_init(struct v4l2_int_device *s)
{
	struct smia_sensor *sensor = s->priv;
	char name[FIRMWARE_NAME_MAX];
	int model_id, revision_number, manufacturer_id, smia_version;
	int i, rval;

	/* Read and check sensor identification registers */
	if (smia_i2c_read_reg(sensor->i2c_client, SMIA_REG_16BIT,
			      REG_MODEL_ID, &model_id)
	    || smia_i2c_read_reg(sensor->i2c_client, SMIA_REG_8BIT,
				 REG_REVISION_NUMBER, &revision_number)
	    || smia_i2c_read_reg(sensor->i2c_client, SMIA_REG_8BIT,
				 REG_MANUFACTURER_ID, &manufacturer_id)
	    || smia_i2c_read_reg(sensor->i2c_client, SMIA_REG_8BIT,
				 REG_SMIA_VERSION, &smia_version)) {
		return -ENODEV;
	}

	sensor->model_id        = model_id;
	sensor->revision_number = revision_number;
	sensor->manufacturer_id = manufacturer_id;
	sensor->smia_version    = smia_version;

	if (sensor->smia_version != 10) {
		/* We support only SMIA version 1.0 at the moment */
		dev_err(&sensor->i2c_client->dev,
			"unknown sensor 0x%04x detected (smia ver %i.%i)\n",
			sensor->model_id,
			sensor->smia_version / 10, sensor->smia_version % 10);
		return -ENODEV;
	}

	/* Update identification string */
	for (i = 0; i < ARRAY_SIZE(smia_sensors); i++) {
		if (smia_sensors[i].manufacturer_id == sensor->manufacturer_id
		    && smia_sensors[i].model_id == sensor->model_id)
			break;
	}
	if (i < ARRAY_SIZE(smia_sensors))
		strncpy(s->name, smia_sensors[i].name, V4L2NAMESIZE);
	s->name[V4L2NAMESIZE-1] = 0;	/* Ensure NULL terminated string */

	/* Import firmware */
	snprintf(name, FIRMWARE_NAME_MAX, "%s-%02x-%04x-%02x.bin",
		 SMIA_SENSOR_NAME, sensor->manufacturer_id,
		 sensor->model_id, sensor->revision_number);

	if (request_firmware(&sensor->fw, name,
			     &sensor->i2c_client->dev)) {
		dev_err(&sensor->i2c_client->dev,
			"can't load firmware %s\n", name);
		return -ENODEV;
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

	sensor->dev_init_done = true;

	return 0;

out_release:
	release_firmware(sensor->fw);
	sensor->meta_reglist = NULL;
	sensor->fw = NULL;

	return rval;
}

static int smia_ioctl_s_power(struct v4l2_int_device *s, enum v4l2_power state)
{
	struct smia_sensor *sensor = s->priv;
	int rval = 0;

	if (state != V4L2_POWER_ON)
		state = V4L2_POWER_OFF;

	if (sensor->power == state)
		return 0;

	if (state == V4L2_POWER_ON) {
		rval = smia_power_on(s);
		if (rval)
			return rval;
		if (!sensor->dev_init_done) {
			rval = smia_dev_init(s);
			if (rval)
				goto out_poweroff;
		}
		rval = smia_configure(s);
		if (rval)
			goto out_poweroff;
	} else {
		/* V4L2_POWER_OFF */
		rval = smia_power_off(s);
		if (rval)
			return rval;
	}

	sensor->power = state;
	return 0;

out_poweroff:
	sensor->power = V4L2_POWER_OFF;
	smia_power_off(s);
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

static int smia_ioctl_enum_slaves(struct v4l2_int_device *s,
			     struct v4l2_slave_info *si)
{
	struct smia_sensor *sensor = s->priv;

	strlcpy(si->driver, SMIA_SENSOR_NAME, sizeof(si->driver));
	strlcpy(si->bus_info, "ccp2", sizeof(si->bus_info));
	snprintf(si->version, sizeof(si->version), "%02x",
		 sensor->revision_number);

	return 0;
}

#ifdef CONFIG_PM

static int smia_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct smia_sensor *sensor = i2c_get_clientdata(client);

	if (sensor->power == V4L2_POWER_OFF)
		return 0;

	return smia_power_off(sensor->v4l2_int_device);
}

static int smia_resume(struct i2c_client *client)
{
	struct smia_sensor *sensor = i2c_get_clientdata(client);
	enum v4l2_power resume_power;

	if (sensor->power == V4L2_POWER_OFF)
		return 0;

	resume_power = sensor->power;
	sensor->power = V4L2_POWER_OFF;

	return smia_ioctl_s_power(sensor->v4l2_int_device, resume_power);
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

	sensor->ctrl_gain     = 0;
	sensor->ctrl_exposure = DEFAULT_EXPOSURE;

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
