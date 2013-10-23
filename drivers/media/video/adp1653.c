/*
 * drivers/media/video/adp1653.c
 *
 * Copyright (C) 2008 Nokia Corporation
 *
 * Contact: Sakari Ailus <sakari.ailus@nokia.com>
 *          Tuukka Toivonen <tuukka.o.toivonen@nokia.com>
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
 * NOTES:
 * - Torch and Indicator lights are enabled by just increasing
 *   intensity from zero
 * - Increasing Flash light intensity does nothing until it is
 *   strobed (strobe control set to 1)
 * - Strobing flash disables Torch light (sets intensity to zero).
 *   This might be changed later.
 *
 * TODO:
 * - fault interrupt handling
 * - faster strobe (use i/o pin instead of i2c)
 *   - should ensure that the pin is in some sane state even if not used
 * - strobe control could return whether flash is still on (measure time)
 * - power doesn't need to be ON if all lights are off
 *
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/version.h>
#include <media/adp1653.h>

#define TIMEOUT_US_TO_CODE(t)	((820000 + 27300 - (t))/54600)
#define TIMEOUT_CODE_TO_US(c)	(820000 - (c) * 54600)

/* Write values into ADP1653 registers. Do nothing if power is off. */
static int adp1653_update_hw(struct v4l2_int_device *s)
{
	struct adp1653_flash *flash = s->priv;
	int rval;
	u8 out_sel = 0;
	u8 config = 0;

	if (flash->power != V4L2_POWER_ON)
		return 0;

	out_sel |= flash->indicator_intensity << ADP1653_REG_OUT_SEL_ILED_SHIFT;
	/* Set torch intensity to zero--prevents false triggering of SC Fault */
	rval = i2c_smbus_write_byte_data(flash->i2c_client,
					 ADP1653_REG_OUT_SEL, out_sel);
	if (rval < 0)
		return rval;

	if (flash->torch_intensity > 0) {
		/* Torch mode, light immediately on, duration indefinite */
		out_sel |= flash->torch_intensity
			   << ADP1653_REG_OUT_SEL_HPLED_SHIFT;
	} else {
		/* Flash mode, light on with strobe, duration from timer */
		out_sel |= flash->flash_intensity
			   << ADP1653_REG_OUT_SEL_HPLED_SHIFT;
		config |= ADP1653_REG_CONFIG_TMR_CFG;
		config |= TIMEOUT_US_TO_CODE(flash->flash_timeout)
			  << ADP1653_REG_CONFIG_TMR_SET_SHIFT;
	}

	rval = i2c_smbus_write_byte_data(flash->i2c_client,
					 ADP1653_REG_OUT_SEL, out_sel);
	if (rval < 0)
		return rval;

	rval = i2c_smbus_write_byte_data(flash->i2c_client,
					 ADP1653_REG_CONFIG, config);
	if (rval < 0)
		return rval;

	return 0;
}

static int adp1653_strobe(struct v4l2_int_device *s)
{
	struct adp1653_flash *flash = s->priv;
	int rval;

	if (flash->torch_intensity > 0) {
		/* Disabling torch enables flash in update_hw() */
		flash->torch_intensity = 0;
		rval = adp1653_update_hw(s);
		if (rval)
			return rval;
	}

	if (flash->platform_data->strobe) {
		/* Hardware-specific strobe using I/O pin */
		return flash->platform_data->strobe(s);
	} else {
		/* Software strobe using i2c */
		rval = i2c_smbus_write_byte_data(flash->i2c_client,
			ADP1653_REG_SW_STROBE, ADP1653_REG_SW_STROBE_SW_STROBE);
		if (rval)
			return rval;
		rval = i2c_smbus_write_byte_data(flash->i2c_client,
			ADP1653_REG_SW_STROBE, 0);
		return rval;
	}
}

static int adp1653_get_fault(struct v4l2_int_device *s)
{
	struct adp1653_flash *flash = s->priv;

	return i2c_smbus_read_byte_data(flash->i2c_client, ADP1653_REG_FAULT);
}

#define CTRL_CAMERA_FLASH_STROBE		0
#define CTRL_CAMERA_FLASH_TIMEOUT		1
#define CTRL_CAMERA_FLASH_INTENSITY		2
#define CTRL_CAMERA_FLASH_TORCH_INTENSITY	3
#define CTRL_CAMERA_FLASH_INDICATOR_INTENSITY	4
#define CTRL_CAMERA_FLASH_FAULT_SCP		5
#define CTRL_CAMERA_FLASH_FAULT_OT		6
#define CTRL_CAMERA_FLASH_FAULT_TMR		7
#define CTRL_CAMERA_FLASH_FAULT_OV		8

static struct v4l2_queryctrl adp1653_ctrls[] = {
	{
		.id		= V4L2_CID_FLASH_STROBE,
		.type		= V4L2_CTRL_TYPE_BUTTON,
		.name		= "Flash strobe",
		.minimum	= 0,
		.maximum	= 0,
		.step		= 0,
		.default_value	= 0,
		.flags		= V4L2_CTRL_FLAG_UPDATE,
	},

	{
		.id		= V4L2_CID_FLASH_TIMEOUT,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Flash timeout [us]",
		.minimum	= 1000,
		.step		= 54600,
		.flags		= V4L2_CTRL_FLAG_SLIDER,
	},
	{
		.id		= V4L2_CID_FLASH_INTENSITY,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Flash intensity",
		.minimum	= ADP1653_TORCH_INTENSITY_MAX + 1,
		.step		= 1,
		.default_value	= ADP1653_TORCH_INTENSITY_MAX + 1,
		.flags		= V4L2_CTRL_FLAG_SLIDER,
	},
	{
		.id		= V4L2_CID_TORCH_INTENSITY,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Torch intensity",
		.minimum	= 0,
		.step		= 1,
		.default_value	= 0,
		.flags		= V4L2_CTRL_FLAG_SLIDER,
	},
	{
		.id		= V4L2_CID_INDICATOR_INTENSITY,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Indicator intensity",
		.minimum	= 0,
		.step		= 1,
		.default_value	= 0,
		.flags		= V4L2_CTRL_FLAG_SLIDER,
	},

	/* Faults */
	{
		.id		= V4L2_CID_FLASH_ADP1653_FAULT_SCP,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Short-circuit fault",
		.minimum	= 0,
		.maximum	= 1,
		.step		= 1,
		.default_value	= 0,
		.flags		= V4L2_CTRL_FLAG_READ_ONLY,
	},
	{
		.id		= V4L2_CID_FLASH_ADP1653_FAULT_OT,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Overtemperature fault",
		.minimum	= 0,
		.maximum	= 1,
		.step		= 1,
		.default_value	= 0,
		.flags		= V4L2_CTRL_FLAG_READ_ONLY,
	},
	{
		.id		= V4L2_CID_FLASH_ADP1653_FAULT_TMR,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Timeout fault",
		.minimum	= 0,
		.maximum	= 1,
		.step		= 1,
		.default_value	= 0,
		.flags		= V4L2_CTRL_FLAG_READ_ONLY,
	},
	{
		.id		= V4L2_CID_FLASH_ADP1653_FAULT_OV,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Overvoltage fault",
		.minimum	= 0,
		.maximum	= 1,
		.step		= 1,
		.default_value	= 0,
		.flags		= V4L2_CTRL_FLAG_READ_ONLY,
	}
};

static int adp1653_ioctl_queryctrl(struct v4l2_int_device *s,
			   struct v4l2_queryctrl *a)
{
	return smia_ctrl_query(adp1653_ctrls, ARRAY_SIZE(adp1653_ctrls), a);
}

static int adp1653_ioctl_g_ctrl(struct v4l2_int_device *s,
			struct v4l2_control *vc)
{
	struct adp1653_flash *flash = s->priv;

	switch (vc->id) {
	case V4L2_CID_FLASH_TIMEOUT:
		vc->value = flash->flash_timeout;
		break;
	case V4L2_CID_FLASH_INTENSITY:
		vc->value = flash->flash_intensity;
		break;
	case V4L2_CID_TORCH_INTENSITY:
		vc->value = flash->torch_intensity;
		break;
	case V4L2_CID_INDICATOR_INTENSITY:
		vc->value = flash->indicator_intensity;
		break;

	case V4L2_CID_FLASH_ADP1653_FAULT_SCP:
		vc->value = (adp1653_get_fault(s)
			    & ADP1653_REG_FAULT_FLT_SCP) != 0;
		break;
	case V4L2_CID_FLASH_ADP1653_FAULT_OT:
		vc->value = (adp1653_get_fault(s)
			    & ADP1653_REG_FAULT_FLT_OT) != 0;
		break;
	case V4L2_CID_FLASH_ADP1653_FAULT_TMR:
		vc->value = (adp1653_get_fault(s)
			    & ADP1653_REG_FAULT_FLT_TMR) != 0;
		break;
	case V4L2_CID_FLASH_ADP1653_FAULT_OV:
		vc->value = (adp1653_get_fault(s)
			    & ADP1653_REG_FAULT_FLT_OV) != 0;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int adp1653_ioctl_s_ctrl(struct v4l2_int_device *s,
				struct v4l2_control *vc)
{
	struct adp1653_flash *flash = s->priv;
	int ctrl;
	int *value;

	switch (vc->id) {
	case V4L2_CID_FLASH_STROBE:
		return adp1653_strobe(s);

	case V4L2_CID_FLASH_TIMEOUT:
		ctrl = CTRL_CAMERA_FLASH_TIMEOUT;
		value = &flash->flash_timeout;
		break;
	case V4L2_CID_FLASH_INTENSITY:
		ctrl = CTRL_CAMERA_FLASH_INTENSITY;
		value = &flash->flash_intensity;
		break;
	case V4L2_CID_TORCH_INTENSITY:
		ctrl = CTRL_CAMERA_FLASH_TORCH_INTENSITY;
		value = &flash->torch_intensity;
		break;
	case V4L2_CID_INDICATOR_INTENSITY:
		ctrl = CTRL_CAMERA_FLASH_INDICATOR_INTENSITY;
		value = &flash->indicator_intensity;
		break;

	default:
		return -EINVAL;
	}

	if (vc->value < adp1653_ctrls[ctrl].minimum)
		vc->value = adp1653_ctrls[ctrl].minimum;
	if (vc->value > adp1653_ctrls[ctrl].maximum)
		vc->value = adp1653_ctrls[ctrl].maximum;
	vc->value = (vc->value - adp1653_ctrls[ctrl].minimum
		    + (adp1653_ctrls[ctrl].step >> 1))
		    / adp1653_ctrls[ctrl].step;
	vc->value = vc->value * adp1653_ctrls[ctrl].step
		    + adp1653_ctrls[ctrl].minimum;
	*value = vc->value;

	return adp1653_update_hw(s);
}

static int adp1653_init_device(struct v4l2_int_device *s)
{
	struct adp1653_flash *flash = s->priv;
	int rval;

	/* Clear FAULT register by writing zero to OUT_SEL */
	rval = i2c_smbus_write_byte_data(flash->i2c_client,
					 ADP1653_REG_OUT_SEL, 0);
	if (rval < 0) {
		dev_err(&flash->i2c_client->dev,
			"failed writing fault register\n");
		return -ENODEV;
	}

	/* Read FAULT register */
	rval = i2c_smbus_read_byte_data(flash->i2c_client, ADP1653_REG_FAULT);
	if (rval < 0) {
		dev_err(&flash->i2c_client->dev,
			"failed reading fault register\n");
		return -ENODEV;
	}

	if ((rval & 0x0f) != 0) {
		dev_err(&flash->i2c_client->dev, "device fault\n");
		return -ENODEV;
	}

	rval = adp1653_update_hw(s);
	if (rval) {
		dev_err(&flash->i2c_client->dev,
			"adp1653_update_hw failed at adp1653_init_device\n");
		return -ENODEV;
	}

	return 0;
}

static int adp1653_ioctl_dev_init(struct v4l2_int_device *s)
{
	struct adp1653_flash *flash = s->priv;

	adp1653_ctrls[CTRL_CAMERA_FLASH_TIMEOUT].default_value =
	adp1653_ctrls[CTRL_CAMERA_FLASH_TIMEOUT].maximum =
			flash->platform_data->max_flash_timeout;
	adp1653_ctrls[CTRL_CAMERA_FLASH_INTENSITY].maximum =
			flash->platform_data->max_flash_intensity;
	adp1653_ctrls[CTRL_CAMERA_FLASH_TORCH_INTENSITY].maximum =
			flash->platform_data->max_torch_intensity;
	adp1653_ctrls[CTRL_CAMERA_FLASH_INDICATOR_INTENSITY].maximum =
			flash->platform_data->max_indicator_intensity;

	flash->flash_timeout = adp1653_ctrls
			[CTRL_CAMERA_FLASH_TIMEOUT].default_value;
	flash->flash_intensity = adp1653_ctrls
			[CTRL_CAMERA_FLASH_INTENSITY].default_value;
	flash->torch_intensity = adp1653_ctrls
			[CTRL_CAMERA_FLASH_TORCH_INTENSITY].default_value;
	flash->indicator_intensity = adp1653_ctrls
			[CTRL_CAMERA_FLASH_INDICATOR_INTENSITY].default_value;
	return 0;
}

static int adp1653_ioctl_s_power(struct v4l2_int_device *s,
				 enum v4l2_power state)
{
	struct adp1653_flash *flash = s->priv;
	int rval = 0;

	if (state == V4L2_POWER_STANDBY)
		state = V4L2_POWER_ON;
	if (state == flash->power)
		return 0;

	switch (state) {
	case V4L2_POWER_STANDBY:
	case V4L2_POWER_ON:
		rval = flash->platform_data->power_on(s);
		if (rval)
			return rval;
		flash->power = V4L2_POWER_ON;

		rval = adp1653_init_device(s);
		if (rval)
			goto fail;

		break;

	case V4L2_POWER_OFF:
		rval = flash->platform_data->power_off(s);
		flash->power = V4L2_POWER_OFF;
		break;
	}
	return 0;

fail:
	flash->platform_data->power_off(s);
	flash->power = V4L2_POWER_OFF;
	return rval;
}

static int adp1653_ioctl_g_priv(struct v4l2_int_device *s, void *priv)
{
	struct adp1653_flash *flash = s->priv;

	return flash->platform_data->g_priv(s, priv);
}

static struct v4l2_int_ioctl_desc adp1653_ioctl_desc[] = {
	{ vidioc_int_queryctrl_num,
	  (v4l2_int_ioctl_func *)adp1653_ioctl_queryctrl },
	{ vidioc_int_g_ctrl_num,
	  (v4l2_int_ioctl_func *)adp1653_ioctl_g_ctrl },
	{ vidioc_int_s_ctrl_num,
	  (v4l2_int_ioctl_func *)adp1653_ioctl_s_ctrl },
	{ vidioc_int_s_power_num,
	  (v4l2_int_ioctl_func *)adp1653_ioctl_s_power },
	{ vidioc_int_g_priv_num,
	  (v4l2_int_ioctl_func *)adp1653_ioctl_g_priv },
	{ vidioc_int_dev_init_num,
	  (v4l2_int_ioctl_func *)adp1653_ioctl_dev_init },
};

static struct v4l2_int_slave adp1653_slave = {
	.ioctls = adp1653_ioctl_desc,
	.num_ioctls = ARRAY_SIZE(adp1653_ioctl_desc),
};

static struct adp1653_flash adp1653;

static struct v4l2_int_device adp1653_int_device = {
	.module = THIS_MODULE,
	.name = ADP1653_NAME,
	.priv = &adp1653,
	.type = v4l2_int_type_slave,
	.u = {
		.slave = &adp1653_slave,
	},
};

#ifdef CONFIG_PM

static int adp1653_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct adp1653_flash *flash = i2c_get_clientdata(client);

	if (flash->power == V4L2_POWER_OFF)
		return 0;

	return flash->platform_data->power_off(flash->v4l2_int_device);
}

static int adp1653_resume(struct i2c_client *client)
{
	struct adp1653_flash *flash = i2c_get_clientdata(client);
	enum v4l2_power resume_power;

	if (flash->power == V4L2_POWER_OFF)
		return 0;

	resume_power = flash->power;
	flash->power = V4L2_POWER_OFF;

	return adp1653_ioctl_s_power(flash->v4l2_int_device, resume_power);
}

#else

#define adp1653_suspend	NULL
#define adp1653_resume	NULL

#endif /* CONFIG_PM */

static int adp1653_probe(struct i2c_client *client,
			 const struct i2c_device_id *devid)
{
	struct adp1653_flash *flash = &adp1653;
	int rval;

	if (i2c_get_clientdata(client))
		return -EBUSY;

	flash->platform_data = client->dev.platform_data;

	if (flash->platform_data == NULL)
		return -ENODEV;

	flash->v4l2_int_device = &adp1653_int_device;

	flash->i2c_client = client;
	i2c_set_clientdata(client, flash);

	rval = v4l2_int_device_register(flash->v4l2_int_device);
	if (rval)
		i2c_set_clientdata(client, NULL);

	return rval;
}

static int __exit adp1653_remove(struct i2c_client *client)
{
	struct adp1653_flash *flash = i2c_get_clientdata(client);

	if (!client->adapter)
		return -ENODEV;	/* our client isn't attached */

	v4l2_int_device_unregister(flash->v4l2_int_device);
	i2c_set_clientdata(client, NULL);

	return 0;
}

static const struct i2c_device_id adp1653_id_table[] = {
	{ ADP1653_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adp1653_id_table);

static struct i2c_driver adp1653_i2c_driver = {
	.driver		= {
		.name	= ADP1653_NAME,
	},
	.probe		= adp1653_probe,
	.remove		= __exit_p(adp1653_remove),
	.suspend	= adp1653_suspend,
	.resume		= adp1653_resume,
	.id_table	= adp1653_id_table,
};

static int __init adp1653_init(void)
{
	int rval;

	rval = i2c_add_driver(&adp1653_i2c_driver);
	if (rval)
		printk(KERN_ALERT "%s: failed at i2c_add_driver\n", __func__);

	return rval;
}

static void __exit adp1653_exit(void)
{
	i2c_del_driver(&adp1653_i2c_driver);
}

/*
 * FIXME: Menelaus isn't ready (?) at module_init stage, so use
 * late_initcall for now.
 */
late_initcall(adp1653_init);
module_exit(adp1653_exit);

MODULE_AUTHOR("Sakari Ailus <sakari.ailus@nokia.com>");
MODULE_DESCRIPTION("Analog Devices ADP1653 LED flash driver");
MODULE_LICENSE("GPL");
