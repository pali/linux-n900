/*
 * drivers/media/radio/radio-si4713.c
 *
 * Driver for I2C Silicon Labs Si4713 FM Radio Transmitter:
 *
 * Copyright (c) 2008 Instituto Nokia de Tecnologia - INdT
 * Author: Eduardo Valentin <eduardo.valentin@indt.org.br>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/* driver definitions */
#define DRIVER_AUTHOR "Eduardo Valentin <eduardo.valentin@indt.org.br>"
#define DRIVER_NAME SI4713_NAME
#define DRIVER_VERSION KERNEL_VERSION(0, 0, 1)
#define DRIVER_CARD "Silicon Labs Si4713 FM Radio Transmitter"
#define DRIVER_DESC "I2C driver for Si4713 FM Radio Transmitter"

/* frequency domain transformation (using times 10 to avoid floats) */
#define FREQDEV_UNIT	100000
#define FREQV4L2_MULTI	625
#define dev_to_v4l2(f)	((f * FREQDEV_UNIT) / FREQV4L2_MULTI)
#define v4l2_to_dev(f)	((f * FREQV4L2_MULTI) / FREQDEV_UNIT)

/* kernel includes */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/version.h>
#include <linux/interrupt.h>
#include <linux/sysfs.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/videodev2.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>

#include "radio-si4713.h"
#include "si4713.h"

/* module parameters */
static int radio_nr = -1;	/* radio device minor (-1 ==> auto assign) */

/*
 * Sysfs properties
 * Read and write functions
 */
#define property_write(prop, type, mask, check)				\
static ssize_t si4713_##prop##_write(struct device *dev,		\
					struct device_attribute *attr,	\
					const char *buf,		\
					size_t count)			\
{									\
	struct si4713_device *sdev = dev_get_drvdata(dev);		\
	type value;							\
	int rval;							\
									\
	if (!sdev)							\
		return -ENODEV;						\
									\
	sscanf(buf, mask, &value);					\
									\
	if (check)							\
		return -EDOM;						\
									\
	rval = si4713_set_##prop(sdev, value);				\
									\
	return rval < 0 ? rval : count;					\
}

#define property_read(prop, size, mask)					\
static ssize_t si4713_##prop##_read(struct device *dev,			\
					struct device_attribute *attr,	\
					char *buf)			\
{									\
	struct si4713_device *sdev = dev_get_drvdata(dev);		\
	size value;							\
									\
	if (!sdev)							\
		return -ENODEV;						\
									\
	value = si4713_get_##prop(sdev);				\
									\
	if (value >= 0)							\
		value = sprintf(buf, mask "\n", value);			\
									\
	return value;							\
}

#define DEFINE_SYSFS_PROPERTY(prop, signal, size, mask, check)		\
property_write(prop, signal size, mask, check)				\
property_read(prop, size, mask)						\
static DEVICE_ATTR(prop, S_IRUGO | S_IWUSR, si4713_##prop##_read,	\
					si4713_##prop##_write);
#define DEFINE_SYSFS_PROPERTY_RO(prop, signal, size, mask)		\
property_read(prop, size, mask)						\
static DEVICE_ATTR(prop, S_IRUGO, si4713_##prop##_read, NULL);


#define property_str_write(prop, size)					\
static ssize_t si4713_##prop##_write(struct device *dev,		\
					struct device_attribute *attr,	\
					const char *buf,		\
					size_t count)			\
{									\
	struct si4713_device *sdev = dev_get_drvdata(dev);		\
	int rval;							\
	u8 *in;								\
									\
	if (!sdev)							\
		return -ENODEV;						\
									\
	in = kzalloc(size + 1, GFP_KERNEL);				\
	if (!in)							\
		return -ENOMEM;						\
									\
	/* We don't want to miss the spaces */				\
	strncpy(in, buf, size);						\
	rval = si4713_set_##prop(sdev, in);				\
									\
	kfree(in);							\
									\
	return rval < 0 ? rval : count;					\
}

#define property_str_read(prop, size)					\
static ssize_t si4713_##prop##_read(struct device *dev,			\
					struct device_attribute *attr,	\
					char *buf)			\
{									\
	struct si4713_device *sdev = dev_get_drvdata(dev);		\
	int count;							\
	u8 *out;							\
									\
	if (!sdev)							\
		return -ENODEV;						\
									\
	out = kzalloc(size + 1, GFP_KERNEL);				\
	if (!out)							\
		return -ENOMEM;						\
									\
	si4713_get_##prop(sdev, out);					\
	count = sprintf(buf, "%s\n", out);				\
									\
	kfree(out);							\
									\
	return count;							\
}

#define DEFINE_SYSFS_PROPERTY_STR(prop, size)				\
property_str_write(prop, size)						\
property_str_read(prop, size)						\
static DEVICE_ATTR(prop, S_IRUGO | S_IWUSR, si4713_##prop##_read,	\
					si4713_##prop##_write);

/*
 * Power level property
 */
/* power_level (rw) 88 - 115 or 0 */
static ssize_t si4713_power_level_write(struct device *dev,
					struct device_attribute *attr,
					const char *buf,
					size_t count)
{
	struct si4713_device *sdev = dev_get_drvdata(dev);
	unsigned int p;
	int rval, pl;

	if (!sdev) {
		rval = -ENODEV;
		goto exit;
	}

	sscanf(buf, "%u", &p);

	pl = si4713_get_power_level(sdev);
	if (pl < 0) {
		rval = pl;
		goto exit;
	}

	rval = si4713_set_power_level(sdev, p);

exit:
	return rval < 0 ? rval : count;
}
property_read(power_level, unsigned int, "%u")
static DEVICE_ATTR(power_level, S_IRUGO | S_IWUSR, si4713_power_level_read,
					si4713_power_level_write);

DEFINE_SYSFS_PROPERTY(antenna_capacitor, unsigned, int, "%u",
			value > SI4713_MAX_ANTCAP)
/*
 * RDS properties
 */
/* rds_pi (rw) 0 - 0xFFFF */
DEFINE_SYSFS_PROPERTY(rds_pi, unsigned, int, "%x", 0)
/* rds_pty (rw) 0 - 0x1F */
DEFINE_SYSFS_PROPERTY(rds_pty, unsigned, int, "%u", value > MAX_RDS_PTY)
/* rds_enabled (rw) 0 - 1 */
DEFINE_SYSFS_PROPERTY(rds_enabled, unsigned, int, "%u", 0)
/* rds_ps_name (rw) strlen (8 - 96) */
DEFINE_SYSFS_PROPERTY_STR(rds_ps_name, MAX_RDS_PS_NAME)
/* rds_radio_text (rw) strlen (0 - 384) */
DEFINE_SYSFS_PROPERTY_STR(rds_radio_text, MAX_RDS_RADIO_TEXT)

/*
 * Limiter properties
 */
/* limiter_release_time (rw) 0 - 102390 */
DEFINE_SYSFS_PROPERTY(limiter_release_time, unsigned, long, "%lu",
			value > MAX_LIMITER_RELEASE_TIME)
/* limiter_deviation (rw) 0 - 90000 */
DEFINE_SYSFS_PROPERTY(limiter_deviation, unsigned, long, "%lu",
			value > MAX_LIMITER_DEVIATION)
/* limiter_enabled (rw) 0 - 1 */
DEFINE_SYSFS_PROPERTY(limiter_enabled, unsigned, int, "%u", 0)

/*
 * Pilot tone properties
 */
/* pilot_frequency (rw) 0 - 19000 */
DEFINE_SYSFS_PROPERTY(pilot_frequency, unsigned, int, "%u",
			value > MAX_PILOT_FREQUENCY)
/* pilot_deviation (rw) 0 - 90000 */
DEFINE_SYSFS_PROPERTY(pilot_deviation, unsigned, long, "%lu",
			value > MAX_PILOT_DEVIATION)
/* pilot_enabled (rw) 0 - 1 */
DEFINE_SYSFS_PROPERTY(pilot_enabled, unsigned, int, "%u", 0)

/*
 * Stereo properties
 */
/* stereo_enabled (rw) 0 - 1 */
DEFINE_SYSFS_PROPERTY(stereo_enabled, unsigned, int, "%u", 0)

/*
 * Audio Compression properties
 */
/* acomp_release_time (rw) 0 - 1000000 */
DEFINE_SYSFS_PROPERTY(acomp_release_time, unsigned, long, "%lu",
			value > MAX_ACOMP_RELEASE_TIME)
/* acomp_attack_time (rw) 0 - 5000 */
DEFINE_SYSFS_PROPERTY(acomp_attack_time, unsigned, int, "%u",
			value > MAX_ACOMP_ATTACK_TIME)
/* acomp_threshold (rw) -40 - 0 */
property_write(acomp_threshold, int, "%d",
		value > MAX_ACOMP_THRESHOLD ||
		value < MIN_ACOMP_THRESHOLD)

static ssize_t si4713_acomp_threshold_read(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct si4713_device *sdev = dev_get_drvdata(dev);
	int count;
	s8 thres;

	if (!sdev)
		return -ENODEV;

	count = si4713_get_acomp_threshold(sdev, &thres);

	if (count >= 0)
		count = sprintf(buf, "%d\n", thres);

	return count;
}
static DEVICE_ATTR(acomp_threshold, S_IRUGO | S_IWUSR,
					si4713_acomp_threshold_read,
					si4713_acomp_threshold_write);

/* acomp_gain (rw) 0 - 20 */
DEFINE_SYSFS_PROPERTY(acomp_gain, unsigned, int, "%u", value > MAX_ACOMP_GAIN)
/* acomp_enabled (rw) 0 - 1 */
DEFINE_SYSFS_PROPERTY(acomp_enabled, unsigned, int, "%u", 0)

/* Tune_measure (rw) */
DEFINE_SYSFS_PROPERTY(tune_measure, unsigned, int, "%u", 0)

/*
 * Region properties
 */
DEFINE_SYSFS_PROPERTY_RO(region_bottom_frequency, unsigned, int, "%u")
DEFINE_SYSFS_PROPERTY_RO(region_top_frequency, unsigned, int, "%u")
DEFINE_SYSFS_PROPERTY_RO(region_channel_spacing, unsigned, int, "%u")
DEFINE_SYSFS_PROPERTY(region_preemphasis, unsigned, int, "%u",
					((value != PREEMPHASIS_USA) &&
					(value != PREEMPHASIS_EU) &&
					(value != PREEMPHASIS_DISABLED)))
DEFINE_SYSFS_PROPERTY(region, unsigned, int, "%u", 0)

/*
 * Tone properties
 */
/* tone_frequency (rw) 0 - 19000 */
DEFINE_SYSFS_PROPERTY(tone_frequency, unsigned, int, "%u",
			value > MAX_TONE_FREQUENCY)
/* tone_deviation (rw) 0 - 90000 */
DEFINE_SYSFS_PROPERTY(tone_deviation, unsigned, long, "%lu",
			value > MAX_TONE_DEVIATION)
/* tone_on_time (rw) 0 - 65535 */
DEFINE_SYSFS_PROPERTY(tone_on_time, unsigned, int, "%u",
			value > MAX_TONE_ON_TIME)
/* tone_off_time (rw) 0 - 65535 */
DEFINE_SYSFS_PROPERTY(tone_off_time, unsigned, int, "%u",
			value > MAX_TONE_OFF_TIME)

static struct attribute *attrs[] = {
	&dev_attr_power_level.attr,
	&dev_attr_antenna_capacitor.attr,
	&dev_attr_rds_pi.attr,
	&dev_attr_rds_pty.attr,
	&dev_attr_rds_ps_name.attr,
	&dev_attr_rds_radio_text.attr,
	&dev_attr_rds_enabled.attr,
	&dev_attr_limiter_release_time.attr,
	&dev_attr_limiter_deviation.attr,
	&dev_attr_limiter_enabled.attr,
	&dev_attr_pilot_frequency.attr,
	&dev_attr_pilot_deviation.attr,
	&dev_attr_pilot_enabled.attr,
	&dev_attr_stereo_enabled.attr,
	&dev_attr_acomp_release_time.attr,
	&dev_attr_acomp_attack_time.attr,
	&dev_attr_acomp_threshold.attr,
	&dev_attr_acomp_gain.attr,
	&dev_attr_acomp_enabled.attr,
	&dev_attr_region_bottom_frequency.attr,
	&dev_attr_region_top_frequency.attr,
	&dev_attr_region_preemphasis.attr,
	&dev_attr_region_channel_spacing.attr,
	&dev_attr_region.attr,
	&dev_attr_tune_measure.attr,
	&dev_attr_tone_frequency.attr,
	&dev_attr_tone_deviation.attr,
	&dev_attr_tone_on_time.attr,
	&dev_attr_tone_off_time.attr,
	NULL,
};

static const struct attribute_group attr_group = {
	.attrs = attrs,
};

static irqreturn_t si4713_handler(int irq, void *dev)
{
	struct si4713_device *sdev = dev;

	dev_dbg(&sdev->client->dev, "IRQ called, signaling completion work\n");
	complete(&sdev->work);

	return IRQ_HANDLED;
}

/*
 * si4713_fops - file operations interface
 */
static const struct file_operations si4713_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.ioctl		= video_ioctl2,
	.compat_ioctl	= v4l_compat_ioctl32,
};

/*
 * Video4Linux Interface
 */
/*
 * si4713_v4l2_queryctrl - query control
 */
static struct v4l2_queryctrl si4713_v4l2_queryctrl[] = {
/* HINT: the disabled controls are only here to satify kradio and such apps */
	{
		.id		= V4L2_CID_AUDIO_VOLUME,
		.flags		= V4L2_CTRL_FLAG_DISABLED,
	},
	{
		.id		= V4L2_CID_AUDIO_BALANCE,
		.flags		= V4L2_CTRL_FLAG_DISABLED,
	},
	{
		.id		= V4L2_CID_AUDIO_BASS,
		.flags		= V4L2_CTRL_FLAG_DISABLED,
	},
	{
		.id		= V4L2_CID_AUDIO_TREBLE,
		.flags		= V4L2_CTRL_FLAG_DISABLED,
	},
	{
		.id		= V4L2_CID_AUDIO_MUTE,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Mute",
		.minimum	= 0,
		.maximum	= 1,
		.step		= 1,
		.default_value	= 1,
	},
	{
		.id		= V4L2_CID_AUDIO_LOUDNESS,
		.flags		= V4L2_CTRL_FLAG_DISABLED,
	},
};

/*
 * si4713_vidioc_querycap - query device capabilities
 */
static int si4713_vidioc_querycap(struct file *file, void *priv,
		struct v4l2_capability *capability)
{
	struct si4713_device *sdev = video_get_drvdata(video_devdata(file));

	strlcpy(capability->driver, DRIVER_NAME, sizeof(capability->driver));
	strlcpy(capability->card, DRIVER_CARD, sizeof(capability->card));
	snprintf(capability->bus_info, 32, "I2C: 0x%X", sdev->client->addr);
	capability->version = DRIVER_VERSION;
	capability->capabilities = V4L2_CAP_TUNER;

	return 0;
}

/*
 * si4713_vidioc_g_input - get input
 */
static int si4713_vidioc_g_input(struct file *filp, void *priv,
		unsigned int *i)
{
	*i = 0;

	return 0;
}

/*
 * si4713_vidioc_s_input - set input
 */
static int si4713_vidioc_s_input(struct file *filp, void *priv, unsigned int i)
{
	if (i)
		return -EINVAL;

	return 0;
}

/*
 * si4713_vidioc_queryctrl - enumerate control items
 */
static int si4713_vidioc_queryctrl(struct file *file, void *priv,
		struct v4l2_queryctrl *qc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(si4713_v4l2_queryctrl); i++) {
		if (qc->id && qc->id == si4713_v4l2_queryctrl[i].id) {
			memcpy(qc, &(si4713_v4l2_queryctrl[i]), sizeof(*qc));
			return 0;
		}
	}

	return -EINVAL;
}

/*
 * si4713_vidioc_g_ctrl - get the value of a control
 */
static int si4713_vidioc_g_ctrl(struct file *file, void *priv,
		struct v4l2_control *ctrl)
{
	struct si4713_device *sdev = video_get_drvdata(video_devdata(file));
	int rval = 0;

	if (!sdev)
		return -ENODEV;

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		rval = si4713_get_mute(sdev);
		if (rval >= 0)
			ctrl->value = rval;
		break;
	}

	return rval;
}

/*
 * si4713_vidioc_s_ctrl - set the value of a control
 */
static int si4713_vidioc_s_ctrl(struct file *file, void *priv,
		struct v4l2_control *ctrl)
{
	struct si4713_device *sdev = video_get_drvdata(video_devdata(file));
	int rval = 0;

	if (!sdev)
		return -ENODEV;

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		if (ctrl->value) {
				rval = si4713_set_mute(sdev, ctrl->value);
				if (rval < 0)
					goto exit;

				rval = si4713_set_power_state(sdev, POWER_DOWN);
		} else {
				rval = si4713_set_power_state(sdev, POWER_UP);
				if (rval < 0)
					goto exit;

				rval = si4713_setup(sdev);
				if (rval < 0)
					goto exit;

				rval = si4713_set_mute(sdev, ctrl->value);
			}
		break;
	}

exit:
	return rval;
}

/*
 * si4713_vidioc_g_audio - get audio attributes
 */
static int si4713_vidioc_g_audio(struct file *file, void *priv,
		struct v4l2_audio *audio)
{
	if (audio->index > 1)
		return -EINVAL;

	strncpy(audio->name, "Radio", 32);
	audio->capability = V4L2_AUDCAP_STEREO;

	return 0;
}

/*
 * si4713_vidioc_s_audio - set audio attributes
 */
static int si4713_vidioc_s_audio(struct file *file, void *priv,
		struct v4l2_audio *audio)
{
	if (audio->index != 0)
		return -EINVAL;

	return 0;
}

/*
 * si4713_vidioc_g_tuner - get tuner attributes
 */
static int si4713_vidioc_g_tuner(struct file *file, void *priv,
		struct v4l2_tuner *tuner)
{
	struct si4713_device *sdev = video_get_drvdata(video_devdata(file));

	if (!sdev)
		return -ENODEV;

	if (tuner->index > 0)
		return -EINVAL;

	strncpy(tuner->name, "FM Transmitter", 32);
	tuner->type = V4L2_TUNER_RADIO;
	tuner->rangelow =
		dev_to_v4l2(si4713_get_region_bottom_frequency(sdev) / 10);
	tuner->rangehigh =
		dev_to_v4l2(si4713_get_region_top_frequency(sdev) / 10);
	tuner->rxsubchans = V4L2_TUNER_SUB_STEREO;
	tuner->capability = V4L2_TUNER_CAP_STEREO | V4L2_TUNER_CAP_LOW;
	tuner->audmode = V4L2_TUNER_MODE_STEREO;

	/* automatic frequency control: -1: freq to low, 1 freq to high */
	tuner->afc = 0;

	return 0;
}

/*
 * si4713_vidioc_s_tuner - set tuner attributes
 */
static int si4713_vidioc_s_tuner(struct file *file, void *priv,
		struct v4l2_tuner *tuner)
{
	struct si4713_device *sdev = video_get_drvdata(video_devdata(file));

	if (!sdev)
		return -ENODEV;

	if (tuner->index > 0)
		return -EINVAL;

	return 0;
}

/*
 * si4713_vidioc_g_frequency - get tuner or modulator radio frequency
 */
static int si4713_vidioc_g_frequency(struct file *file, void *priv,
		struct v4l2_frequency *freq)
{
	struct si4713_device *sdev = video_get_drvdata(video_devdata(file));
	int rval = 0;
	int f;

	freq->type = V4L2_TUNER_RADIO;
	f = si4713_get_frequency(sdev);

	if (f < 0)
		rval = f;
	else
		freq->frequency = dev_to_v4l2(f);

	return rval;
}

/*
 * si4713_vidioc_s_frequency - set tuner or modulator radio frequency
 */
static int si4713_vidioc_s_frequency(struct file *file, void *priv,
		struct v4l2_frequency *freq)
{
	struct si4713_device *sdev = video_get_drvdata(video_devdata(file));
	int rval = 0;

	if (freq->type != V4L2_TUNER_RADIO) {
		rval = -EINVAL;
		goto exit;
	}

	rval = si4713_set_frequency(sdev, v4l2_to_dev(freq->frequency));

exit:
	return rval;
}

static struct v4l2_ioctl_ops si4713_ioctl_ops = {
	.vidioc_querycap	= si4713_vidioc_querycap,
	.vidioc_g_input		= si4713_vidioc_g_input,
	.vidioc_s_input		= si4713_vidioc_s_input,
	.vidioc_queryctrl	= si4713_vidioc_queryctrl,
	.vidioc_g_ctrl		= si4713_vidioc_g_ctrl,
	.vidioc_s_ctrl		= si4713_vidioc_s_ctrl,
	.vidioc_g_audio		= si4713_vidioc_g_audio,
	.vidioc_s_audio		= si4713_vidioc_s_audio,
	.vidioc_g_tuner		= si4713_vidioc_g_tuner,
	.vidioc_s_tuner		= si4713_vidioc_s_tuner,
	.vidioc_g_frequency	= si4713_vidioc_g_frequency,
	.vidioc_s_frequency	= si4713_vidioc_s_frequency,
};

/*
 * si4713_viddev_tamples - video device interface
 */
static struct video_device si4713_viddev_template = {
	.fops			= &si4713_fops,
	.name			= DRIVER_NAME,
	.vfl_type		= VID_TYPE_TUNER,
	.release		= video_device_release,
	.ioctl_ops		= &si4713_ioctl_ops,
};

/*
 * I2C driver interface
 */
/*
 * si4713_i2c_driver_probe - probe for the device
 */
static int si4713_i2c_driver_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	struct si4713_device *sdev;
	int rval, skip_release = 0;

	sdev = kzalloc(sizeof *sdev, GFP_KERNEL);
	if (!sdev) {
		dev_dbg(&client->dev, "Failed to alloc video device.\n");
		rval = -ENOMEM;
		goto exit;
	}

	sdev->videodev = video_device_alloc();
	if (!sdev->videodev) {
		dev_dbg(&client->dev, "Failed to alloc video device.\n");
		rval = -ENOMEM;
		goto free_sdev;
	}

	sdev->platform_data = client->dev.platform_data;
	if (!sdev->platform_data) {
		dev_dbg(&client->dev, "No platform data registered.\n");
		rval = -ENODEV;
		goto free_vdev;
	}

	sdev->client = client;
	i2c_set_clientdata(client, sdev);

	mutex_init(&sdev->mutex);
	init_completion(&sdev->work);

	if (client->irq) {
		rval = request_irq(client->irq,
			si4713_handler, IRQF_TRIGGER_FALLING | IRQF_DISABLED,
			client->name, sdev);
		if (rval < 0) {
			dev_err(&client->dev, "Could not request IRQ\n");
			goto free_vdev;
		}
		dev_dbg(&client->dev, "IRQ requested.\n");
	} else {
		dev_dbg(&client->dev, "IRQ not configure. Using timeouts.\n");
	}

	memcpy(sdev->videodev, &si4713_viddev_template,
			sizeof(si4713_viddev_template));
	video_set_drvdata(sdev->videodev, sdev);
	if (video_register_device(sdev->videodev, VFL_TYPE_RADIO, radio_nr)) {
		dev_dbg(&client->dev, "Could not register video device.\n");
		rval = -EIO;
		goto free_irq;
	}

	rval = sysfs_create_group(&sdev->client->dev.kobj, &attr_group);
	if (rval < 0) {
		dev_dbg(&client->dev, "Could not register sysfs interface.\n");
		goto free_registration;
	}

	rval = si4713_probe(sdev);
	if (rval < 0) {
		dev_dbg(&client->dev, "Failed to probe device information.\n");
		goto free_sysfs;
	}

	return 0;

free_sysfs:
	sysfs_remove_group(&sdev->client->dev.kobj, &attr_group);
free_registration:
	video_unregister_device(sdev->videodev);
	skip_release = 1;
free_irq:
	if (client->irq)
		free_irq(client->irq, sdev);
free_vdev:
	if (!skip_release)
		video_device_release(sdev->videodev);
free_sdev:
	kfree(sdev);
exit:
	return rval;
}

/*
 * si4713_i2c_driver_remove - remove the device
 */
static int __exit si4713_i2c_driver_remove(struct i2c_client *client)
{
	struct si4713_device *sdev = i2c_get_clientdata(client);
	struct video_device *vd;

	/* our client isn't attached */
	if (!client->adapter)
		return -ENODEV;

	if (sdev) {
		vd = sdev->videodev;

		sysfs_remove_group(&sdev->client->dev.kobj, &attr_group);

		if (vd)
			video_unregister_device(vd);

		if (sdev->power_state)
			si4713_set_power_state(sdev, POWER_DOWN);

		if (client->irq > 0)
			free_irq(client->irq, sdev);

		kfree(sdev);
	}
	i2c_set_clientdata(client, NULL);

	return 0;
}

/*
 * si4713_i2c_driver - i2c driver interface
 */
static const struct i2c_device_id si4713_id[] = {
	{ "si4713" , 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, si4713_id);

static struct i2c_driver si4713_i2c_driver = {
	.driver		= {
		.name	= DRIVER_NAME,
	},
	.probe		= si4713_i2c_driver_probe,
	.remove         = __exit_p(si4713_i2c_driver_remove),
	.id_table       = si4713_id,
};

/*
 * Module Interface
 */
/*
 * si4713_module_init - module init
 */
static int __init si4713_module_init(void)
{
	int rval;

	printk(KERN_INFO DRIVER_DESC "\n");

	rval = i2c_add_driver(&si4713_i2c_driver);
	if (rval < 0)
		printk(KERN_ERR DRIVER_NAME ": driver registration failed\n");

	return rval;
}

/*
 * si4713_module_exit - module exit
 */
static void __exit si4713_module_exit(void)
{
	i2c_del_driver(&si4713_i2c_driver);
}

module_init(si4713_module_init);
module_exit(si4713_module_exit);

module_param(radio_nr, int, 0);
MODULE_PARM_DESC(radio_nr,
		 "Minor number for radio device (-1 ==> auto assign)");

MODULE_LICENSE("GPL");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_VERSION("0.0.1");
