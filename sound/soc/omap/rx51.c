/*
 * n810.c  --  SoC audio for Nokia RX51
 *
 * Copyright (C) 2008 Nokia Corporation
 *
 * Contact: Jarkko Nikula <jarkko.nikula@nokia.com>
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

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/i2c/tpa6130a2.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/jack.h>
#include <sound/tlv.h>

#include <linux/i2c/twl4030.h>
#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <linux/gpio.h>
#include <mach/mcbsp.h>

#include "omap-mcbsp.h"
#include "omap-pcm.h"
#include "../codecs/tlv320aic3x.h"
#include "aic34b_dummy.h"

#define RX51_CODEC_RESET_GPIO		60
#define RX51_TVOUT_SEL_GPIO		40
#define RX51_ECI_SWITCH_1_GPIO		178
#define RX51_ECI_SWITCH_2_GPIO		182
/* REVISIT: TWL4030 GPIO base in RX51. Now statically defined to 192 */
#define RX51_SPEAKER_AMP_TWL_GPIO	(192 + 7)

enum {
	RX51_JACK_DISABLED,
	RX51_JACK_HP,		/* headphone: stereo output, no mic */
	RX51_JACK_HS,		/* headset: stereo output with mic */
	RX51_JACK_MIC,		/* mic input only */
	RX51_JACK_ECI,		/* ECI headset */
	RX51_JACK_TVOUT,	/* stereo output with tv-out */
};

static int hp_lim = 63;
module_param(hp_lim, int, 0);

static int rx51_new_hw_audio;
static int rx51_spk_func;
static int rx51_jack_func;
static int rx51_fmtx_func;
static int rx51_dmic_func;
static int rx51_ear_func;
static struct snd_jack *rx51_jack;

static DEFINE_MUTEX(eci_mutex);
static int rx51_eci_mode = 1;
static int rx51_dapm_jack_bias;
static int tpa6130_volume = -1;
static int tpa6130_enable;
static int aic34b_volume;

static void rx51_set_eci_switches(int mode)
{
	switch (mode) {
	case 0: /* Bias off */
	case 1: /* Bias according to rx51_dapm_jack_bias */
	case 4: /* Bias on */
		/* Codec connected to mic/bias line */
		gpio_set_value(RX51_ECI_SWITCH_1_GPIO, 0);
		gpio_set_value(RX51_ECI_SWITCH_2_GPIO, 1);
		break;
	case 2:
		/* ECI INT#2 detect connected to mic/bias line */
		gpio_set_value(RX51_ECI_SWITCH_1_GPIO, 0);
		gpio_set_value(RX51_ECI_SWITCH_2_GPIO, 0);
		break;
	case 3:
		/* ECI RX/TX connected to mic/bias line */
		gpio_set_value(RX51_ECI_SWITCH_1_GPIO, 1);
		gpio_set_value(RX51_ECI_SWITCH_2_GPIO, 0);
		break;
	}
}

static void rx51_set_jack_bias(void)
{
	int enable_bias = 0;

	mutex_lock(&eci_mutex);
	if ((rx51_eci_mode == 1 && rx51_dapm_jack_bias) || rx51_eci_mode == 4)
		enable_bias = 1;
	else if (rx51_eci_mode == 1 && rx51_jack_func == RX51_JACK_ECI)
		enable_bias = 1;
	mutex_unlock(&eci_mutex);
	if (enable_bias)
		aic34b_set_mic_bias(2); /* 2.5 V */
	else
		aic34b_set_mic_bias(0);
}

static void rx51_set_jack_bias_handler(struct work_struct *unused)
{
	rx51_set_jack_bias();
}
DECLARE_WORK(rx51_jack_bias_work, rx51_set_jack_bias_handler);

static void rx51_ext_control(struct snd_soc_codec *codec)
{
	int hp = 0, mic = 0, tvout = 0;

	switch (rx51_jack_func) {
	case RX51_JACK_ECI:
	case RX51_JACK_HS:
		mic = 1;
	case RX51_JACK_HP:
		hp = 1;
		break;
	case RX51_JACK_MIC:
		mic = 1;
		break;
	case RX51_JACK_TVOUT:
		hp = 1;
		tvout = 1;
		break;
	}

	gpio_set_value(RX51_TVOUT_SEL_GPIO, tvout);

	if (rx51_spk_func)
		snd_soc_dapm_enable_pin(codec, "Ext Spk");
	else
		snd_soc_dapm_disable_pin(codec, "Ext Spk");
	if (hp)
		snd_soc_dapm_enable_pin(codec, "Headphone Jack");
	else
		snd_soc_dapm_disable_pin(codec, "Headphone Jack");
	if (mic)
		snd_soc_dapm_enable_pin(codec, "Mic Jack");
	else
		snd_soc_dapm_disable_pin(codec, "Mic Jack");
	if (rx51_fmtx_func)
		snd_soc_dapm_enable_pin(codec, "FM Transmitter");
	else
		snd_soc_dapm_disable_pin(codec, "FM Transmitter");
	if (rx51_dmic_func)
		snd_soc_dapm_enable_pin(codec, "DMic");
	else
		snd_soc_dapm_disable_pin(codec, "DMic");
	if (rx51_ear_func)
		snd_soc_dapm_enable_pin(codec, "Earphone");
	else
		snd_soc_dapm_disable_pin(codec, "Earphone");

	snd_soc_dapm_sync(codec);
}

int rx51_set_eci_mode(int mode)
{
	if (mode < 0 || mode > 4)
		return -EINVAL;

	mutex_lock(&eci_mutex);
	if (rx51_eci_mode == mode) {
		mutex_unlock(&eci_mutex);
		return 0;
	}

	rx51_eci_mode = mode;
	rx51_set_eci_switches(rx51_eci_mode);
	mutex_unlock(&eci_mutex);

	rx51_set_jack_bias();

	return 0;
}
EXPORT_SYMBOL(rx51_set_eci_mode);

static ssize_t eci_mode_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	return sprintf(buf, "%d\n", rx51_eci_mode);
}

static ssize_t eci_mode_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	int mode, retval;
	if (sscanf(buf, "%d", &mode) != 1)
		return -EINVAL;
	retval = rx51_set_eci_mode(mode);

	return (retval < 0) ? retval : count;
}

static DEVICE_ATTR(eci_mode, S_IRUGO | S_IWUSR,
		   eci_mode_show, eci_mode_store);

void rx51_jack_report(int status)
{
	snd_jack_report(rx51_jack, status);
}
EXPORT_SYMBOL(rx51_jack_report);

static int rx51_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->socdev->codec;

	snd_pcm_hw_constraint_minmax(runtime,
				     SNDRV_PCM_HW_PARAM_CHANNELS, 2, 2);

	rx51_ext_control(codec);

	return 0;
}

static void rx51_shutdown(struct snd_pcm_substream *substream)
{
}

static int pre_events;

static int rx51_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->dai->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
	int err;

	if (rx51_new_hw_audio) {
		if (!pre_events) {
			pre_events = 1;
			err = twl4030_enable_regulator(RES_VMMC2);
			if (err < 0)
				return err;
		}
	}

	/* Set codec DAI configuration */
	err = snd_soc_dai_set_fmt(codec_dai,
				  SND_SOC_DAIFMT_DSP_A |
				  SND_SOC_DAIFMT_IB_NF |
				  SND_SOC_DAIFMT_CBM_CFM);
	if (err < 0)
		return err;

	/* Set cpu DAI configuration */
	err = snd_soc_dai_set_fmt(cpu_dai,
				  SND_SOC_DAIFMT_DSP_A |
				  SND_SOC_DAIFMT_IB_NF |
				  SND_SOC_DAIFMT_CBM_CFM);
	if (err < 0)
		return err;

	/* Set the codec system clock for DAC and ADC */
	return snd_soc_dai_set_sysclk(codec_dai, 0, 19200000,
				      SND_SOC_CLOCK_IN);
}

static int rx51_bt_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;

	/* Set cpu DAI configuration */
	return cpu_dai->dai_ops.set_fmt(cpu_dai,
					SND_SOC_DAIFMT_DSP_A |
					SND_SOC_DAIFMT_IB_NF |
					SND_SOC_DAIFMT_CBM_CFM);
}

static struct snd_soc_ops rx51_bt_ops = {
	.hw_params = rx51_bt_hw_params,
};

static struct snd_soc_ops rx51_ops = {
	.startup = rx51_startup,
	.hw_params = rx51_hw_params,
	.shutdown = rx51_shutdown,
};

static int rx51_get_spk(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = rx51_spk_func;

	return 0;
}

static int rx51_set_spk(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec =  snd_kcontrol_chip(kcontrol);

	if (rx51_spk_func == ucontrol->value.integer.value[0])
		return 0;

	rx51_spk_func = ucontrol->value.integer.value[0];
	rx51_ext_control(codec);

	return 1;
}

static int rx51_spk_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *k, int event)
{
	if (SND_SOC_DAPM_EVENT_ON(event))
		gpio_set_value(RX51_SPEAKER_AMP_TWL_GPIO, 1);
	else
		gpio_set_value(RX51_SPEAKER_AMP_TWL_GPIO, 0);

	return 0;
}

static int rx51_get_jack(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = rx51_jack_func;

	return 0;
}

static int rx51_set_jack(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec =  snd_kcontrol_chip(kcontrol);

	if (rx51_jack_func == ucontrol->value.integer.value[0])
		return 0;

	rx51_jack_func = ucontrol->value.integer.value[0];

	mutex_lock(&eci_mutex);
	if (rx51_jack_func == RX51_JACK_ECI) {
		/* Set ECI switches according to ECI mode */
		rx51_set_eci_switches(rx51_eci_mode);
		schedule_work(&rx51_jack_bias_work);
	} else {
		/*
		 * Let the codec always be connected to mic/bias line when
		 * jack is in non-ECI function
		 */
		rx51_set_eci_switches(1);
		schedule_work(&rx51_jack_bias_work);
	}
	mutex_unlock(&eci_mutex);

	rx51_ext_control(codec);

	return 1;
}

static int rx51_jack_hp_event(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *k, int event)
{
	/*
	 * Note: HP amp and fmtx must not be enabled at the same
	 * time. We keep a shadow copy of the desired tpa_enable value but
	 * keep the hpamp really disabled whenever fmtx is enabled. If
	 * hpamp is requested on but fmtx is enabled, hpamp is kept
	 * disabled and enabled later from rx51_set_fmtx function when
	 * user disables fmtx.
	 */
	if (SND_SOC_DAPM_EVENT_ON(event)) {
		if (!rx51_fmtx_func)
			tpa6130a2_set_enabled(1);
		tpa6130_enable = 1;
	} else {
		tpa6130a2_set_enabled(0);
		tpa6130_enable = 0;
	}

	return 0;
}

static int rx51_jack_mic_event(struct snd_soc_dapm_widget *w,
			       struct snd_kcontrol *k, int event)
{
	if (SND_SOC_DAPM_EVENT_ON(event))
		rx51_dapm_jack_bias = 1;
	else
		rx51_dapm_jack_bias = 0;
	schedule_work(&rx51_jack_bias_work);

	return 0;
}

static int rx51_get_fmtx(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = rx51_fmtx_func;

	return 0;
}

static int rx51_set_fmtx(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec =  snd_kcontrol_chip(kcontrol);

	if (rx51_fmtx_func == ucontrol->value.integer.value[0])
		return 0;

	rx51_fmtx_func = ucontrol->value.integer.value[0];
	rx51_ext_control(codec);

	/* fmtx and tpa must not be enabled at the same time */
	if (rx51_fmtx_func && tpa6130_enable)
		tpa6130a2_set_enabled(0);
	if (!rx51_fmtx_func && tpa6130_enable)
		tpa6130a2_set_enabled(1);

	return 1;
}

static int rx51_get_input(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = rx51_dmic_func;

	return 0;
}

static int rx51_set_input(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec =  snd_kcontrol_chip(kcontrol);

	if (rx51_dmic_func == ucontrol->value.integer.value[0])
		return 0;

	rx51_dmic_func = ucontrol->value.integer.value[0];
	rx51_ext_control(codec);

	return 1;
}

static int rx51_get_ear(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = rx51_ear_func;

	return 0;
}

static int rx51_set_ear(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec =  snd_kcontrol_chip(kcontrol);

	if (rx51_ear_func == ucontrol->value.integer.value[0])
		return 0;

	rx51_ear_func = ucontrol->value.integer.value[0];
	rx51_ext_control(codec);

	return 1;
}

static int rx51_ear_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *k, int event)
{
	if (SND_SOC_DAPM_EVENT_ON(event))
		aic34b_ear_enable(1);
	else
		aic34b_ear_enable(0);

	return 0;
}

static int rx51_pre_spk_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *k, int event)
{
	if (!rx51_new_hw_audio)
		return 0;

	if (SND_SOC_DAPM_EVENT_ON(event))
		return twl4030_enable_regulator(RES_VMMC2);

	return 0;
}

static int rx51_post_spk_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *k, int event)
{
	if (!rx51_new_hw_audio)
		return 0;

	if (!SND_SOC_DAPM_EVENT_ON(event))
		return twl4030_disable_regulator(RES_VMMC2);

	return 0;
}

static int rx51_pre_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *k, int event)
{
	if (!rx51_new_hw_audio)
		return 0;

	if (SND_SOC_DAPM_EVENT_ON(event)) {
		if (!pre_events) {
			pre_events = 1;
			return twl4030_enable_regulator(RES_VMMC2);
		}
	}

	return 0;
}

static int rx51_post_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *k, int event)
{
	if (!rx51_new_hw_audio)
		return 0;

	if (!SND_SOC_DAPM_EVENT_ON(event)) {
		if (pre_events && !w->codec->active) {
			pre_events = 0;
			return twl4030_disable_regulator(RES_VMMC2);
		}
	}

	return 0;
}

enum {
       RX51_EXT_API_TPA6130,
       RX51_EXT_API_AIC34B,
};
#define SOC_RX51_EXT_SINGLE_TLV(xname, ext_api, max, tlv_array) \
{ \
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	.name = xname, \
	.access = SNDRV_CTL_ELEM_ACCESS_TLV_READ | \
		  SNDRV_CTL_ELEM_ACCESS_READWRITE, \
	.tlv.p = (tlv_array), \
	.info = rx51_ext_info_volsw, \
	.get = rx51_ext_get_volsw, \
	.put = rx51_ext_put_volsw, \
	.private_value = (ext_api) << 26 | (max) << 16, \
}

static int rx51_ext_info_volsw(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_info *uinfo)
{
	int ext_api = (kcontrol->private_value >> 26) & 0x0f;
	int max = (kcontrol->private_value >> 16) & 0xff;

	if (ext_api == RX51_EXT_API_TPA6130)
		if (hp_lim != max && hp_lim >= 2 && hp_lim <= 63) {
			kcontrol->private_value &= ~(0xff << 16);
			kcontrol->private_value |= (hp_lim << 16);
			max = hp_lim;
		}

	if (max == 1)
		uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	else
		uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;

	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = max;

	return 0;
}

static int rx51_ext_get_volsw(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	int ext_api = (kcontrol->private_value >> 26) & 0x0f;

	switch (ext_api) {
	case RX51_EXT_API_TPA6130:
		if (tpa6130_volume < 0)
			tpa6130_volume = tpa6130a2_get_volume();
		ucontrol->value.integer.value[0] = tpa6130_volume;
		break;
	case RX51_EXT_API_AIC34B:
		ucontrol->value.integer.value[0] = aic34b_volume;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rx51_ext_put_volsw(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	int ext_api = (kcontrol->private_value >> 26) & 0x0f;
	int change = 0;

	switch (ext_api) {
	case RX51_EXT_API_TPA6130:
		change = (tpa6130_volume != ucontrol->value.integer.value[0]);
		tpa6130_volume = ucontrol->value.integer.value[0];
		tpa6130a2_set_volume(tpa6130_volume);
		break;
	case RX51_EXT_API_AIC34B:
		change = (aic34b_volume != ucontrol->value.integer.value[0]);
		aic34b_volume = ucontrol->value.integer.value[0];
		aic34b_set_volume(aic34b_volume);
		break;
	default:
		return -EINVAL;
	}

	return change;
}

#define SOC_RX51_SINGLE_JACK_BIAS(xname) \
{ \
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	.name = xname, \
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE | \
		  SNDRV_CTL_ELEM_ACCESS_VOLATILE, \
	.info = rx51_info_jack_bias, \
	.get = rx51_get_jack_bias, \
	.put = rx51_put_jack_bias, \
}

static int rx51_info_jack_bias(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;

	return 0;
}

static int rx51_get_jack_bias(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = (aic34b_get_mic_bias() != 0);

	return 0;
}

static int rx51_put_jack_bias(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	int change, new_value;

	new_value = ucontrol->value.integer.value[0];
	change = (new_value != aic34b_get_mic_bias());

	if (change) {
		switch (rx51_jack_func) {
		case RX51_JACK_ECI:
		case RX51_JACK_HS:
		case RX51_JACK_MIC:
			aic34b_set_mic_bias(new_value * 2); /* 2.5 V */
			break;
		default:
			change = 0;
		}
	}

	return change;
}

static const struct snd_soc_dapm_widget aic34_dapm_widgets[] = {
	SND_SOC_DAPM_POST("Post event", rx51_post_event),
	SND_SOC_DAPM_SPK("Post spk", rx51_post_spk_event),
	SND_SOC_DAPM_SPK("Ext Spk", rx51_spk_event),
	SND_SOC_DAPM_SPK("Headphone Jack", rx51_jack_hp_event),
	SND_SOC_DAPM_MIC("Mic Jack", rx51_jack_mic_event),
	SND_SOC_DAPM_OUTPUT("FM Transmitter"),
	SND_SOC_DAPM_MIC("DMic", NULL),
	SND_SOC_DAPM_SPK("Earphone", rx51_ear_event),
	SND_SOC_DAPM_SPK("Pre spk", rx51_pre_spk_event),
	SND_SOC_DAPM_PRE("Pre event", rx51_pre_event),
};

static const struct snd_soc_dapm_route audio_map[] = {
	{"Post spk", NULL, "LLOUT"},
	{"Post spk", NULL, "RLOUT"},

	{"Ext Spk", NULL, "HPLOUT"},
	{"Ext Spk", NULL, "HPROUT"},

	{"Headphone Jack", NULL, "LLOUT"},
	{"Headphone Jack", NULL, "RLOUT"},
	{"LINE1L", NULL, "Mic Jack"},

	{"FM Transmitter", NULL, "LLOUT"},
	{"FM Transmitter", NULL, "RLOUT"},

	{"Earphone", NULL, "MONO_LOUT"},

	{"DMic Rate 64", NULL, "Mic Bias 2V"},
	{"Mic Bias 2V", NULL, "DMic"},

	{"Pre spk", NULL, "LLOUT"},
	{"Pre spk", NULL, "RLOUT"},
};

static const char *spk_function[] = {"Off", "On"};
static const char *jack_function[] = {"Off", "Headphone", "Headset",
				      "Mic", "ECI Headset", "TV-OUT"};
static const char *fmtx_function[] = {"Off", "On"};
static const char *input_function[] = {"ADC", "Digital Mic"};
static const char *ear_function[] = {"Off", "On"};

static const struct soc_enum rx51_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(spk_function), spk_function),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(jack_function), jack_function),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(fmtx_function), fmtx_function),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(input_function), input_function),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(ear_function), ear_function),
};

/*
 * TPA6130 volume. From -59.5 to 4 dB with increasing step size when going
 * down in gain. Justify scale so that it is quite correct from -20 dB and
 * up. This setting shows -30 dB at minimum, -12.95 dB at 49 % (actual
 * is -10.3 dB) and 4.65 dB at maximum (actual is 4 dB).
 */
static const unsigned int tpa6130_tlv[] = {
	TLV_DB_RANGE_HEAD(10),
	0, 1, TLV_DB_SCALE_ITEM(-5950, 600, 0),
	2, 3, TLV_DB_SCALE_ITEM(-5000, 250, 0),
	4, 5, TLV_DB_SCALE_ITEM(-4550, 160, 0),
	6, 7, TLV_DB_SCALE_ITEM(-4140, 190, 0),
	8, 9, TLV_DB_SCALE_ITEM(-3650, 120, 0),
	10, 11, TLV_DB_SCALE_ITEM(-3330, 160, 0),
	12, 13, TLV_DB_SCALE_ITEM(-3040, 180, 0),
	14, 20, TLV_DB_SCALE_ITEM(-2710, 110, 0),
	21, 37, TLV_DB_SCALE_ITEM(-1960, 74, 0),
	38, 63, TLV_DB_SCALE_ITEM(-720, 45, 0),
};

/*
 * TLV320AIC3x output stage volumes. From -78.3 to 0 dB. Muted below -78.3 dB.
 * Step size is approximately 0.5 dB over most of the scale but increasing
 * near the very low levels.
 * Define dB scale so that it is mostly correct for range about -55 to 0 dB
 * but having increasing dB difference below that (and where it doesn't count
 * so much). This setting shows -50 dB (actual is -50.3 dB) for register
 * value 100 and -58.5 dB (actual is -78.3 dB) for register value 117.
 */
static DECLARE_TLV_DB_SCALE(aic3x_output_stage_tlv, -5900, 50, 1);

static const struct snd_kcontrol_new aic34_rx51_controls[] = {
	SOC_ENUM_EXT("Speaker Function", rx51_enum[0],
		     rx51_get_spk, rx51_set_spk),
	SOC_ENUM_EXT("Jack Function", rx51_enum[1],
		     rx51_get_jack, rx51_set_jack),
	SOC_ENUM_EXT("FMTX Function", rx51_enum[2],
		     rx51_get_fmtx, rx51_set_fmtx),
	SOC_ENUM_EXT("Input Select",  rx51_enum[3],
		     rx51_get_input, rx51_set_input),
	SOC_ENUM_EXT("Earphone Function",  rx51_enum[4],
		     rx51_get_ear, rx51_set_ear),
	SOC_RX51_EXT_SINGLE_TLV("Headphone Playback Volume",
				RX51_EXT_API_TPA6130, 63,
				tpa6130_tlv),
	SOC_RX51_EXT_SINGLE_TLV("Earphone Playback Volume",
				RX51_EXT_API_AIC34B, 118,
				aic3x_output_stage_tlv),
	SOC_RX51_SINGLE_JACK_BIAS("Jack Bias Switch"),
};

static int rx51_aic34_init(struct snd_soc_codec *codec)
{
	int i, err;

	/* set up NC codec pins */
	snd_soc_dapm_nc_pin(codec, "MIC3L");
	snd_soc_dapm_nc_pin(codec, "MIC3R");
	snd_soc_dapm_nc_pin(codec, "LINE1R");

	/* Create jack for accessory reporting */
	err = snd_jack_new(codec->card, "Jack", SND_JACK_MECHANICAL |
			SND_JACK_HEADSET | SND_JACK_AVOUT, &rx51_jack);
	if (err < 0)
		return err;

	/* Add RX51 specific controls */
	for (i = 0; i < ARRAY_SIZE(aic34_rx51_controls); i++) {
		err = snd_ctl_add(codec->card,
			snd_soc_cnew(&aic34_rx51_controls[i], codec, NULL));
		if (err < 0)
			return err;
	}

	/* Add RX51 specific widgets */
	snd_soc_dapm_new_controls(codec, aic34_dapm_widgets,
				  ARRAY_SIZE(aic34_dapm_widgets));

	/* Set up RX51 specific audio path audio_map */
	snd_soc_dapm_add_routes(codec, audio_map, ARRAY_SIZE(audio_map));

	snd_soc_dapm_enable_pin(codec, "Earphone");

	snd_soc_dapm_sync(codec);

	return 0;
}

/* Since all codec control is done by Bluetooth hardware
   only some constrains need to be set for it */
struct snd_soc_dai btcodec_dai = {
	.name = "Bluetooth codec",
	.playback = {
		.stream_name = "BT Playback",
		.channels_min = 1,
		.channels_max = 1,
		.rates = SNDRV_PCM_RATE_8000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,},
	.capture = {
		.stream_name = "BT Capture",
		.channels_min = 1,
		.channels_max = 1,
		.rates = SNDRV_PCM_RATE_8000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,},
};

/* Digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link rx51_dai[] = {
	{
		.name = "TLV320AIC34",
		.stream_name = "AIC34",
		.cpu_dai = &omap_mcbsp_dai[0],
		.codec_dai = &aic3x_dai,
		.init = rx51_aic34_init,
		.ops = &rx51_ops,
	}, {
		.name = "Bluetooth PCM",
		.stream_name = "Bluetooth",
		.cpu_dai = &omap_mcbsp_dai[1],
		.codec_dai = &btcodec_dai,
		.ops = &rx51_bt_ops,
	}
};

/* Audio machine driver */
static struct snd_soc_machine snd_soc_machine_rx51 = {
	.name = "RX51",
	.dai_link = rx51_dai,
	.num_links = ARRAY_SIZE(rx51_dai),
};

/* Audio private data */
static struct aic3x_setup_data rx51_aic34_setup = {
	.i2c_bus = 2,
	.i2c_address = 0x18,
	.gpio_func[0] = AIC3X_GPIO1_FUNC_DISABLED,
	.gpio_func[1] = AIC3X_GPIO2_FUNC_DIGITAL_MIC_INPUT,
};

/* Audio subsystem */
static struct snd_soc_device rx51_snd_devdata = {
	.machine = &snd_soc_machine_rx51,
	.platform = &omap_soc_platform,
	.codec_dev = &soc_codec_dev_aic3x,
	.codec_data = &rx51_aic34_setup,
};

static struct platform_device *rx51_snd_device;

#define REMAP_OFFSET		2
#define DEDICATED_OFFSET	3
#define VMMC2_DEV_GRP		0x2B
#define VMMC2_285V		0x0a

static int __init rx51_soc_init(void)
{
	int err;
	struct device *dev;

	if (!machine_is_nokia_rx51())
		return -ENODEV;

	if ((system_rev >= 0x08 && system_rev <= 0x13) || /* Macros */
						system_rev >= 0x1901) {
		rx51_new_hw_audio = 1;
		err = twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
					VMMC2_285V,
					VMMC2_DEV_GRP + DEDICATED_OFFSET);
		err |= twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER, 0xee,
					VMMC2_DEV_GRP + REMAP_OFFSET);
		if (err) {
			printk(KERN_ERR "%s rx51 audio failed!\n", __func__);
			return -ENODEV;
		}
	}

	if (gpio_request(RX51_CODEC_RESET_GPIO, NULL) < 0)
		BUG();
	if (gpio_request(RX51_TVOUT_SEL_GPIO, "tvout_sel") < 0)
		BUG();
	if (gpio_request(RX51_ECI_SWITCH_1_GPIO, "ECI switch 1") < 0)
		BUG();
	if (gpio_request(RX51_ECI_SWITCH_2_GPIO, "ECI switch 2") < 0)
		BUG();
	gpio_direction_output(RX51_CODEC_RESET_GPIO, 0);
	gpio_direction_output(RX51_TVOUT_SEL_GPIO, 0);
	gpio_direction_output(RX51_ECI_SWITCH_1_GPIO, 0);
	gpio_direction_output(RX51_ECI_SWITCH_2_GPIO, 1);

	gpio_set_value(RX51_CODEC_RESET_GPIO, 0);
	udelay(1);
	gpio_set_value(RX51_CODEC_RESET_GPIO, 1);
	msleep(1);

	if (gpio_request(RX51_SPEAKER_AMP_TWL_GPIO, NULL) < 0)
		BUG();
	gpio_direction_output(RX51_SPEAKER_AMP_TWL_GPIO, 0);

	rx51_snd_device = platform_device_alloc("soc-audio", -1);
	if (!rx51_snd_device)
		return -ENOMEM;

	platform_set_drvdata(rx51_snd_device, &rx51_snd_devdata);
	rx51_snd_devdata.dev = &rx51_snd_device->dev;
	err = platform_device_add(rx51_snd_device);
	if (err)
		goto err1;

	dev = &rx51_snd_device->dev;

	*(unsigned int *)rx51_dai[0].cpu_dai->private_data = 1;
	*(unsigned int *)rx51_dai[1].cpu_dai->private_data = 2;

	err = device_create_file(dev, &dev_attr_eci_mode);
	if (err)
		goto err2;

	return err;
err2:
	platform_device_del(rx51_snd_device);
err1:
	platform_device_put(rx51_snd_device);

	return err;

}

static void __exit rx51_soc_exit(void)
{
	platform_device_unregister(rx51_snd_device);
}

module_init(rx51_soc_init);
module_exit(rx51_soc_exit);

MODULE_AUTHOR("Jarkko Nikula <jarkko.nikula@nokia.com>");
MODULE_DESCRIPTION("ALSA SoC Nokia RX51");
MODULE_LICENSE("GPL");
