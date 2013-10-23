/*
 * drivers/media/video/ad5820.c
 *
 * AD5820 DAC driver for camera voice coil focus.
 *
 * Copyright (C) 2008 Nokia Corporation
 * Copyright (C) 2007 Texas Instruments
 *
 * Contact: Tuukka Toivonen <tuukka.o.toivonen@nokia.com>
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
#include <linux/bitops.h>
#include <linux/kernel.h>

#include <mach/io.h>
#include <mach/gpio.h>

#include <media/ad5820.h>

#include <media/smiaregs.h>

#define BIT_POWER_DOWN		(1<<15)

#define CODE_TO_RAMP_US(s)	((s) == 0 ? 0 : (1 << ((s) - 1)) * 50)
#define RAMP_US_TO_CODE(c)	fls(((c) + ((c)>>1)) / 50)

static struct ad5820_device ad5820;

#define CTRL_FOCUS_ABSOLUTE		0
#define CTRL_FOCUS_RAMP_TIME		1
#define CTRL_FOCUS_RAMP_MODE		2

static struct v4l2_queryctrl ad5820_ctrls[] = {
	/* Minimum current is 0 mA, maximum is 100 mA. Thus,
	 * 1 code is equivalent to 100/1023 = 0.0978 mA.
	 * Nevertheless, we do not use [mA] for focus position,
	 * because it is meaningless for user. Meaningful would
	 * be to use focus distance or even its inverse, but
	 * since the driver doesn't have sufficiently knowledge
	 * to do the conversion, we will just use abstract codes here.
	 * In any case, smaller value = focus position farther from camera.
	 * The default zero value means focus at infinity,
	 * and also least current consumption.
	 */
	{
		.id		= V4L2_CID_FOCUS_ABSOLUTE,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Focus, Absolute",
		.minimum	= 0,
		.maximum	= 1023,
		.step		= 1,
		.default_value	= 0,
		.flags		= 0,
	},
	{
		.id		= V4L2_CID_FOCUS_AD5820_RAMP_TIME,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Focus ramping time [us]",
		.minimum	= 0,
		.maximum	= 3200,
		.step		= 50,
		.default_value	= 0,
		.flags		= 0,
	},
	{
		.id		= V4L2_CID_FOCUS_AD5820_RAMP_MODE,
		.type		= V4L2_CTRL_TYPE_MENU,
		.name		= "Focus ramping mode",
		.minimum	= 0,
		.maximum	= 1,
		.step		= 1,
		.default_value	= 0,
		.flags		= 0,
	},
};

/**
 * @brief I2C write using i2c_transfer().
 * @param lens - the driver data structure
 * @param data - register value to be written
 * @returns nonnegative on success, negative if failed
 */
static int ad5820_write(struct v4l2_int_device *s, u16 data)
{
	struct ad5820_device *coil = s->priv;
	struct i2c_client *client = coil->i2c_client;
	struct i2c_msg msg;
	int r;

	if (!client->adapter)
		return -ENODEV;

	data = cpu_to_be16(data);
	msg.addr  = client->addr;
	msg.flags = 0;
	msg.len   = 2;
	msg.buf   = (u8 *)&data;

	r = i2c_transfer(client->adapter, &msg, 1);
	if (r >= 0)
		return 0;

	dev_err(&coil->i2c_client->dev, "write failed, error %d\n", r);

	return r;
}

/**
 * @brief I2C read using i2c_transfer().
 * @param lens - the driver data structure
 * @returns unsigned 16-bit register value on success, negative if failed
 */
static int ad5820_read(struct v4l2_int_device *s)
{
	struct ad5820_device *coil = s->priv;
	struct i2c_client *client = coil->i2c_client;
	struct i2c_msg msg;
	int r;
	u16 data = 0;

	if (!client->adapter)
		return -ENODEV;

	msg.addr  = client->addr;
	msg.flags = I2C_M_RD;
	msg.len   = 2;
	msg.buf   = (u8 *)&data;

	r = i2c_transfer(client->adapter, &msg, 1);
	if (r >= 0)
		return be16_to_cpu(data);

	dev_err(&coil->i2c_client->dev, "read failed, error %d\n", r);

	return r;
}

/* Calculate status word and write it to the device based on current
 * values of V4L2 controls. It is assumed that the stored V4L2 control
 * values are properly limited and rounded. */
static int ad5820_update_hw(struct v4l2_int_device *s)
{
	struct ad5820_device *coil = s->priv;
	u16 status;

	if (coil->power == V4L2_POWER_OFF)
		return 0;

	status = RAMP_US_TO_CODE(coil->focus_ramp_time);
	status |= coil->focus_ramp_mode << 3;
	status |= coil->focus_absolute << 4;

	if (coil->power == V4L2_POWER_STANDBY)
		status |= BIT_POWER_DOWN;

	return ad5820_write(s, status);
}

static int ad5820_ioctl_queryctrl(struct v4l2_int_device *s,
				  struct v4l2_queryctrl *qc)
{
	return smia_ctrl_query(ad5820_ctrls, ARRAY_SIZE(ad5820_ctrls), qc);
}

static int ad5820_ioctl_querymenu(struct v4l2_int_device *s,
				  struct v4l2_querymenu *qm)
{
	switch (qm->id) {
	case V4L2_CID_FOCUS_AD5820_RAMP_MODE:
		if (qm->index & ~1)
			return -EINVAL;
		strcpy(qm->name, qm->index == 0 ? "Linear ramp" : "64/16 ramp");
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int ad5820_ioctl_g_ctrl(struct v4l2_int_device *s,
			       struct v4l2_control *vc)
{
	struct ad5820_device *coil = s->priv;

	switch (vc->id) {
	case V4L2_CID_FOCUS_ABSOLUTE:
		vc->value = coil->focus_absolute;
		break;
	case V4L2_CID_FOCUS_AD5820_RAMP_TIME:
		vc->value = coil->focus_ramp_time;
		break;
	case V4L2_CID_FOCUS_AD5820_RAMP_MODE:
		vc->value = coil->focus_ramp_mode;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int ad5820_ioctl_s_ctrl(struct v4l2_int_device *s,
			struct v4l2_control *vc)
{
	struct ad5820_device *coil = s->priv;
	u32 code;
	int r = 0;

	switch (vc->id) {
	case V4L2_CID_FOCUS_ABSOLUTE:
		coil->focus_absolute = clamp(vc->value,
				ad5820_ctrls[CTRL_FOCUS_ABSOLUTE].minimum,
				ad5820_ctrls[CTRL_FOCUS_ABSOLUTE].maximum);
		r = ad5820_update_hw(s);
		break;

	case V4L2_CID_FOCUS_AD5820_RAMP_TIME:
		code = clamp(vc->value,
				ad5820_ctrls[CTRL_FOCUS_RAMP_TIME].minimum,
				ad5820_ctrls[CTRL_FOCUS_RAMP_TIME].maximum);
		code = RAMP_US_TO_CODE(code);
		coil->focus_ramp_time = CODE_TO_RAMP_US(code);
		break;

	case V4L2_CID_FOCUS_AD5820_RAMP_MODE:
		coil->focus_ramp_mode = clamp(vc->value,
				ad5820_ctrls[CTRL_FOCUS_RAMP_MODE].minimum,
				ad5820_ctrls[CTRL_FOCUS_RAMP_MODE].maximum);
		break;

	default:
		return -EINVAL;
	}

	return r;
}

static int ad5820_ioctl_dev_init(struct v4l2_int_device *s)
{
	/* Detect that the chip is there */

	struct ad5820_device *coil = s->priv;
	static const int CHECK_VALUE = 0x3FF0;
	u16 status = BIT_POWER_DOWN | CHECK_VALUE;
	int rval;

	rval = coil->platform_data->s_power(s, V4L2_POWER_ON);
	if (rval)
		goto not_detected;
	rval = ad5820_write(s, status);
	if (rval)
		goto not_detected;
	rval = ad5820_read(s);
	if (rval != status)
		goto not_detected;

	coil->platform_data->s_power(s, V4L2_POWER_OFF);
	return 0;

not_detected:
	dev_err(&coil->i2c_client->dev, "not detected\n");
	return -ENODEV;
}

static int ad5820_ioctl_s_power(struct v4l2_int_device *s,
				enum v4l2_power new_state)
{
	struct ad5820_device *coil = s->priv;
	enum v4l2_power orig_state = coil->power;
	int rval;

	if (new_state == V4L2_POWER_STANDBY)
		new_state = V4L2_POWER_ON;

	if (orig_state == new_state)
		return 0;
	if (orig_state == V4L2_POWER_OFF) {
		/* Requested STANDBY or ON -- enable power */
		rval = coil->platform_data->s_power(s, V4L2_POWER_ON);
		if (rval)
			return rval;
	}
	coil->power = new_state;
	if (new_state == V4L2_POWER_OFF) {
		/* Requested OFF -- before disabling power, set to standby */
		coil->power = V4L2_POWER_STANDBY;
	}
	/*
	 * Here power is on. If OFF is requested, the chip
	 * is first set into STANDBY mode. This is necessary
	 * because sensor driver might keep power enabled even
	 * if lens driver requests it off.
	 */
	rval = ad5820_update_hw(s);
	if (rval)
		goto fail;
	coil->power = new_state;
	if (new_state == V4L2_POWER_OFF) {
		/* Requested OFF -- disable power */
		rval = coil->platform_data->s_power(s, V4L2_POWER_OFF);
		if (rval)
			goto fail;
	}

	return 0;

fail:
	/* Try to restore original state and return error code */
	coil->platform_data->s_power(s, orig_state == V4L2_POWER_OFF ?
				     V4L2_POWER_OFF : V4L2_POWER_ON);
	coil->power = orig_state;
	ad5820_update_hw(s);
	return rval;
}

static int ad5820_ioctl_g_priv(struct v4l2_int_device *s, void *priv)
{
	struct ad5820_device *coil = s->priv;

	return coil->platform_data->g_priv(s, priv);
}

static struct v4l2_int_ioctl_desc ad5820_ioctl_desc[] = {
	{ vidioc_int_queryctrl_num,
	  (v4l2_int_ioctl_func *)ad5820_ioctl_queryctrl },
	{ vidioc_int_querymenu_num,
	  (v4l2_int_ioctl_func *)ad5820_ioctl_querymenu },
	{ vidioc_int_g_ctrl_num,
	  (v4l2_int_ioctl_func *)ad5820_ioctl_g_ctrl },
	{ vidioc_int_s_ctrl_num,
	  (v4l2_int_ioctl_func *)ad5820_ioctl_s_ctrl },
	{ vidioc_int_s_power_num,
	  (v4l2_int_ioctl_func *)ad5820_ioctl_s_power },
	{ vidioc_int_g_priv_num,
	  (v4l2_int_ioctl_func *)ad5820_ioctl_g_priv },
	{ vidioc_int_dev_init_num,
	  (v4l2_int_ioctl_func *)ad5820_ioctl_dev_init },
};

static struct v4l2_int_slave ad5820_slave = {
	.ioctls = ad5820_ioctl_desc,
	.num_ioctls = ARRAY_SIZE(ad5820_ioctl_desc),
};

static struct v4l2_int_device ad5820_int_device = {
	.module = THIS_MODULE,
	.name = AD5820_NAME,
	.priv = &ad5820,
	.type = v4l2_int_type_slave,
	.u = {
		.slave = &ad5820_slave,
	},
};

#ifdef CONFIG_PM

static int ad5820_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct ad5820_device *coil = i2c_get_clientdata(client);

	if (coil->power == V4L2_POWER_OFF)
		return 0;

	return coil->platform_data->s_power(coil->v4l2_int_device, V4L2_POWER_OFF);
}

static int ad5820_resume(struct i2c_client *client)
{
	struct ad5820_device *coil = i2c_get_clientdata(client);
	enum v4l2_power resume_power;

	if (coil->power == V4L2_POWER_OFF)
		return 0;

	resume_power = coil->power;
	coil->power = V4L2_POWER_OFF;

	return ad5820_ioctl_s_power(coil->v4l2_int_device, resume_power);
}

#else

#define ad5820_suspend	NULL
#define ad5820_resume	NULL

#endif /* CONFIG_PM */

static int ad5820_probe(struct i2c_client *client,
			const struct i2c_device_id *devid)
{
	struct ad5820_device *coil = &ad5820;
	int rval;

	if (i2c_get_clientdata(client))
		return -EBUSY;

	coil->platform_data = client->dev.platform_data;

	if (coil->platform_data == NULL)
		return -ENODEV;

	coil->focus_absolute  =
			ad5820_ctrls[CTRL_FOCUS_ABSOLUTE].default_value;
	coil->focus_ramp_time =
			ad5820_ctrls[CTRL_FOCUS_RAMP_TIME].default_value;
	coil->focus_ramp_mode =
			ad5820_ctrls[CTRL_FOCUS_RAMP_MODE].default_value;

	coil->v4l2_int_device = &ad5820_int_device;

	coil->i2c_client = client;
	i2c_set_clientdata(client, coil);

	rval = v4l2_int_device_register(coil->v4l2_int_device);
	if (rval)
		i2c_set_clientdata(client, NULL);

	return rval;
}

static int __exit ad5820_remove(struct i2c_client *client)
{
	struct ad5820_device *coil = i2c_get_clientdata(client);

	if (!client->adapter)
		return -ENODEV;	/* our client isn't attached */

	v4l2_int_device_unregister(coil->v4l2_int_device);
	i2c_set_clientdata(client, NULL);

	return 0;
}

static const struct i2c_device_id ad5820_id_table[] = {
	{ AD5820_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ad5820_id_table);

static struct i2c_driver ad5820_i2c_driver = {
	.driver		= {
		.name	= AD5820_NAME,
	},
	.probe		= ad5820_probe,
	.remove		= __exit_p(ad5820_remove),
	.suspend	= ad5820_suspend,
	.resume		= ad5820_resume,
	.id_table	= ad5820_id_table,
};

static int __init ad5820_init(void)
{
	int rval;

	rval = i2c_add_driver(&ad5820_i2c_driver);
	if (rval)
		printk(KERN_INFO "%s: failed registering " AD5820_NAME "\n",
		       __func__);

	return rval;
}

static void __exit ad5820_exit(void)
{
	i2c_del_driver(&ad5820_i2c_driver);
}


late_initcall(ad5820_init);
module_exit(ad5820_exit);

MODULE_AUTHOR("Tuukka Toivonen <tuukka.o.toivonen@nokia.com>");
MODULE_DESCRIPTION("AD5820 camera lens driver");
MODULE_LICENSE("GPL");
