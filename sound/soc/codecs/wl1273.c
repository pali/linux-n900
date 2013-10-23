/*
 * ALSA SoC WL1273 codec driver
 *
 * Author:      Matti Aaltonen, <matti.j.aaltonen@nokia.com>
 *
 * Copyright:   (C) 2010 Nokia Corporation
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

#undef DEBUG

#include <linux/mfd/wl1273-core.h>
#include <linux/module.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>

#include "wl1273.h"

static int wl1273_get_audio_route(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wl1273_priv *wl1273 = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.integer.value[0] = wl1273->mode;

	return 0;
}

static int wl1273_set_audio_route(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wl1273_priv *wl1273 = snd_soc_codec_get_drvdata(codec);

	if (wl1273->mode == ucontrol->value.integer.value[0])
		return 0;

	/* Do not allow changes while stream is running */
	if (codec->active)
		return -EPERM;

	wl1273->mode = ucontrol->value.integer.value[0];

	return 1;
}

static const char *wl1273_audio_route[] = { "Bt", "FmRx", "FmTx" };

static const struct soc_enum wl1273_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(wl1273_audio_route), wl1273_audio_route),
};

static int snd_wl1273_fm_ctune_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wl1273_priv *wl1273 = snd_soc_codec_get_drvdata(codec);

	dev_dbg(codec->dev, "%s: enter.\n", __func__);

	wl1273->ctune = wl1273_fm_get_tx_ctune(wl1273->core);
	ucontrol->value.integer.value[0] = wl1273->ctune;

	return 0;
}

static int snd_wl1273_fm_power_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wl1273_priv *wl1273 = snd_soc_codec_get_drvdata(codec);

	dev_dbg(codec->dev, "%s: enter.\n", __func__);

	wl1273->tx_power = 31 - wl1273_fm_get_tx_power(wl1273->core);
	ucontrol->value.integer.value[0] = wl1273->tx_power;

	return 0;
}

static int snd_wl1273_fm_power_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wl1273_priv *wl1273 = snd_soc_codec_get_drvdata(codec);
	int val, r = 0;

	dev_dbg(codec->dev, "%s: enter.\n", __func__);

	val = ucontrol->value.integer.value[0];
	if (val < 0 || val > 31)
		return -EDOM;

	if (wl1273->tx_power == val)
		return 0;

	wl1273->tx_power = val;
	r = wl1273_fm_set_tx_power(wl1273->core, 31 - val);
	if (r)
		return r;

	return 1;
}

static int snd_wl1273_fm_rds_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wl1273_priv *wl1273 = snd_soc_codec_get_drvdata(codec);

	dev_dbg(codec->dev, "%s: enter.\n", __func__);

	wl1273->rds_mode = wl1273_fm_get_rds(wl1273->core);
	ucontrol->value.integer.value[0] = wl1273->rds_mode;
	return 0;
}

static int snd_wl1273_fm_rds_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wl1273_priv *wl1273 = snd_soc_codec_get_drvdata(codec);
	unsigned int val;
	int r = 0;

	dev_dbg(codec->dev, "%s: enter.\n", __func__);

	val = ucontrol->value.integer.value[0];
	if (wl1273->rds_mode == val)
		return 0;

	wl1273->rds_mode = val;
	r = wl1273_fm_set_rds(wl1273->core, val);
	if (r)
		return r;

	return 1;
}

static const char *wl1273_rds_strings[] = { "Off", "On", "Reset" };

static const struct soc_enum wl1273_rds_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(wl1273_rds_strings), wl1273_rds_strings),
};

static int snd_wl1273_fm_mode_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wl1273_priv *wl1273 = snd_soc_codec_get_drvdata(codec);

	dev_dbg(codec->dev, "%s: enter.\n", __func__);

	wl1273->pwr_mode = wl1273_fm_get_mode(wl1273->core);
	ucontrol->value.integer.value[0] = wl1273->pwr_mode;

	return 0;
}

static int snd_wl1273_fm_mode_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wl1273_priv *wl1273 = snd_soc_codec_get_drvdata(codec);
	int val, r = 0;

	dev_dbg(codec->dev, "%s: enter.\n", __func__);

	val = ucontrol->value.integer.value[0];
	if (wl1273->pwr_mode == val)
		return 0;

	wl1273->pwr_mode = val;
	r = wl1273_fm_set_mode(wl1273->core, val);
	if (r < 0)
		return r;

	return 1;
}

static const char *wl1273_mode_strings[] = { "Rx", "Tx", "Off", "Suspend" };

static const struct soc_enum wl1273_mode_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(wl1273_mode_strings),
			    wl1273_mode_strings),
};

static int snd_wl1273_fm_audio_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wl1273_priv *wl1273 = snd_soc_codec_get_drvdata(codec);

	dev_dbg(codec->dev, "%s: enter.\n", __func__);

	wl1273->audio_mode = wl1273_fm_get_audio(wl1273->core);
	ucontrol->value.integer.value[0] = wl1273->audio_mode;

	return 0;
}

static int snd_wl1273_fm_audio_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wl1273_priv *wl1273 = snd_soc_codec_get_drvdata(codec);
	int val, r = 0;

	dev_dbg(codec->dev, "%s: enter.\n", __func__);

	val = ucontrol->value.integer.value[0];
	if (wl1273->audio_mode == val)
		return 0;

	wl1273->audio_mode = val;
	r = wl1273_fm_set_audio(wl1273->core, val);
	if (r < 0)
		return r;

	return 1;
}

static const char *wl1273_audio_strings[] = { "Digital", "Analog" };

static const struct soc_enum wl1273_audio_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(wl1273_audio_strings),
			    wl1273_audio_strings),
};

static int snd_wl1273_fm_region_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wl1273_priv *wl1273 = snd_soc_codec_get_drvdata(codec);

	dev_dbg(codec->dev, "%s: enter.\n", __func__);

	wl1273->region = wl1273_fm_get_region(wl1273->core);
	ucontrol->value.integer.value[0] = wl1273->region;

	return 0;
}

static int snd_wl1273_fm_region_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wl1273_priv *wl1273 = snd_soc_codec_get_drvdata(codec);
	int val, r;

	dev_dbg(codec->dev, "%s: enter.\n", __func__);

	val = ucontrol->value.integer.value[0];
	if (wl1273->region == val)
		return 0;

	wl1273->region = val;
	r = wl1273_fm_set_region(wl1273->core, val);
	if (r < 0)
		return r;

	return 1;
}

static const char *wl1273_region_strings[] = { "Japan", "USA-Europe" };

static const struct soc_enum wl1273_region_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(wl1273_region_strings),
			    wl1273_region_strings),
};

static int snd_wl1273_fm_preemphasis_get(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wl1273_priv *wl1273 = snd_soc_codec_get_drvdata(codec);

	dev_dbg(codec->dev, "%s: enter.\n", __func__);

	wl1273->preemphasis = wl1273_fm_get_preemphasis(wl1273->core);
	ucontrol->value.integer.value[0] = wl1273->preemphasis;

	return 0;
}

static int snd_wl1273_fm_preemphasis_put(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wl1273_priv *wl1273 = snd_soc_codec_get_drvdata(codec);
	int val, r = 0;

	dev_dbg(codec->dev, "%s: enter.\n", __func__);

	val = ucontrol->value.integer.value[0];
	if (wl1273->preemphasis == val)
		return 0;

	wl1273->preemphasis = val;
	r = wl1273_fm_set_preemphasis(wl1273->core,
				      ucontrol->value.integer.value[0]);
	if (r < 0)
		return 0;

	return 1;
}

static const char *wl1273_preemphasis_strings[] = { "Off", "50us", "75us" };

static const struct soc_enum wl1273_preemphasis_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(wl1273_preemphasis_strings),
			    wl1273_preemphasis_strings),
};

static int snd_wl1273_fm_spacing_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wl1273_priv *wl1273 = snd_soc_codec_get_drvdata(codec);
	int r;

	dev_dbg(codec->dev, "%s: enter.\n", __func__);

	r = wl1273_fm_tx_get_spacing(wl1273->core);
	if (r < 0)
		return r;

	wl1273->spacing = r;
	ucontrol->value.integer.value[0] = wl1273->spacing;

	return 0;
}

static int snd_wl1273_fm_spacing_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wl1273_priv *wl1273 = snd_soc_codec_get_drvdata(codec);
	int val, r;

	dev_dbg(codec->dev, "%s: enter.\n", __func__);

	val = ucontrol->value.integer.value[0];
	if (wl1273->spacing == val)
		return 0;

	wl1273->spacing = val;
	r = wl1273_fm_tx_set_spacing(wl1273->core,
				     ucontrol->value.integer.value[0]);
	if (r < 0)
		return r;

	return 1;
}

static const char *wl1273_spacing_strings[] = { "50kHz", "100kHz", "200kHz" };

static const struct soc_enum wl1273_spacing_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(wl1273_spacing_strings),
			    wl1273_spacing_strings),
};

static int snd_wl1273_fm_volume_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wl1273_priv *wl1273 = snd_soc_codec_get_drvdata(codec);

	dev_dbg(codec->dev, "%s: enter.\n", __func__);

	ucontrol->value.integer.value[0] = wl1273->core->volume;

	return 0;
}

static int snd_wl1273_fm_volume_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wl1273_priv *wl1273 = snd_soc_codec_get_drvdata(codec);
	int val, r;

	dev_dbg(codec->dev, "%s: enter.\n", __func__);

	val = ucontrol->value.integer.value[0];
	if (val < 0 || val > WL1273_MAX_VOLUME)
		return -EINVAL;

	r = wl1273_fm_set_volume(wl1273->core, val);
	if (r)
		return r;

	return 1;
}

static const struct snd_kcontrol_new wl1273_controls[] = {
	SOC_ENUM_EXT("Codec Mode", wl1273_enum[0],
		     wl1273_get_audio_route, wl1273_set_audio_route),
	SOC_SINGLE_EXT("Tx Ctune", 0, 0, 255, 0, snd_wl1273_fm_ctune_get, NULL),
	SOC_SINGLE_EXT("Tx Power", 0, 0, 31, 0, snd_wl1273_fm_power_get,
		       snd_wl1273_fm_power_put),
	SOC_ENUM_EXT("Rds Switch", wl1273_rds_enum[0],
		     snd_wl1273_fm_rds_get,  snd_wl1273_fm_rds_put),
	SOC_ENUM_EXT("Mode Switch", wl1273_mode_enum[0],
		     snd_wl1273_fm_mode_get,  snd_wl1273_fm_mode_put),
	SOC_ENUM_EXT("Audio Switch", wl1273_audio_enum[0],
		     snd_wl1273_fm_audio_get,  snd_wl1273_fm_audio_put),
	SOC_ENUM_EXT("Region Switch", wl1273_region_enum[0],
		     snd_wl1273_fm_region_get,  snd_wl1273_fm_region_put),
	SOC_ENUM_EXT("Pre-emphasis", wl1273_preemphasis_enum[0],
		     snd_wl1273_fm_preemphasis_get,
		     snd_wl1273_fm_preemphasis_put),
	SOC_ENUM_EXT("Channel Spacing", wl1273_spacing_enum[0],
		     snd_wl1273_fm_spacing_get,  snd_wl1273_fm_spacing_put),
	SOC_SINGLE_EXT("Master Capture Volume", 0, 0, WL1273_MAX_VOLUME, 0,
		       snd_wl1273_fm_volume_get, snd_wl1273_fm_volume_put),
};

static int wl1273_add_controls(struct snd_soc_codec *codec)
{
	return snd_soc_add_controls(codec, wl1273_controls,
				    ARRAY_SIZE(wl1273_controls));
}

static int wl1273_startup(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_pcm *pcm = socdev->card->dai_link->pcm;
	struct snd_soc_codec *codec = socdev->card->codec;
	struct wl1273_priv *wl1273 = snd_soc_codec_get_drvdata(codec);

	switch (wl1273->mode) {
	case WL1273_MODE_BT:
		pcm->info_flags &= ~SNDRV_PCM_INFO_HALF_DUPLEX;
		break;
	case WL1273_MODE_FM_RX:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			pr_err("Cannot play in RX mode.\n");
			return -EINVAL;
		}
		pcm->info_flags |= SNDRV_PCM_INFO_HALF_DUPLEX;
		break;
	case WL1273_MODE_FM_TX:
		if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
			pr_err("Cannot capture in TX mode.\n");
			return -EINVAL;
		}
		pcm->info_flags |= SNDRV_PCM_INFO_HALF_DUPLEX;
		break;
	default:
		return -EINVAL;
		break;
	}

	return 0;
}

static int wl1273_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_codec *codec = socdev->card->codec;
	struct wl1273_priv *wl1273 = snd_soc_codec_get_drvdata(codec);
	struct wl1273_core *core = wl1273->core;
	unsigned int rate, width, r;

	if (params_format(params) != SNDRV_PCM_FORMAT_S16_LE) {
		pr_err("Only SNDRV_PCM_FORMAT_S16_LE supported.\n");
		return -EINVAL;
	}

	rate = params_rate(params);
	width =  hw_param_interval(params, SNDRV_PCM_HW_PARAM_SAMPLE_BITS)->min;

	if (wl1273->mode == WL1273_MODE_BT) {
		if (rate != 8000) {
			pr_err("Rate %d not supported.\n", params_rate(params));
			return -EINVAL;
		}

		if (params_channels(params) != 1) {
			pr_err("Only mono supported.\n");
			return -EINVAL;
		}

		return 0;
	}

	if (wl1273->mode == WL1273_MODE_FM_TX &&
	    substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		pr_err("Only playback supported with TX.\n");
		return -EINVAL;
	}

	if (wl1273->mode == WL1273_MODE_FM_RX  &&
	    substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		pr_err("Only capture supported with RX.\n");
		return -EINVAL;
	}

	if (wl1273->mode != WL1273_MODE_FM_RX  &&
	    wl1273->mode != WL1273_MODE_FM_TX) {
		pr_err("Unexpected mode: %d.\n", wl1273->mode);
		return -EINVAL;
	}

	r = wl1273_fm_set_i2s_mode(core, rate, width);
	if (r)
		return r;

	r = wl1273_fm_set_channel_number(core, (params_channels(params)));
	if (r)
		return r;

	return 0;
}

static int wl1273_set_dai_fmt(struct snd_soc_dai *codec_dai,
			      unsigned int fmt)
{
	return 0;
}

static struct snd_soc_dai_ops wl1273_dai_ops = {
	.startup	= wl1273_startup,
	.hw_params	= wl1273_hw_params,
	.set_fmt	= wl1273_set_dai_fmt,
};

struct snd_soc_dai wl1273_dai = {
	.name = "WL1273 BT/FM codec",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE},
	.ops = &wl1273_dai_ops,
};
EXPORT_SYMBOL_GPL(wl1273_dai);

enum wl1273_mode wl1273_get_codec_mode(struct snd_soc_codec *codec)
{
	struct wl1273_priv *wl1273 = snd_soc_codec_get_drvdata(codec);
	return wl1273->mode;
}
EXPORT_SYMBOL_GPL(wl1273_get_codec_mode);

static int wl1273_soc_suspend(struct platform_device *pdev, pm_message_t state)
{
	return 0;
}

static int wl1273_soc_resume(struct platform_device *pdev)
{
	return 0;
}

static struct snd_soc_codec *wl1273_codec;

/*
 * initialize the driver
 * register the mixer and dsp interfaces with the kernel
 */
static int wl1273_soc_probe(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec;
	struct wl1273_priv *wl1273;
	int r = 0;

	dev_dbg(&pdev->dev, "%s.\n", __func__);

	codec = wl1273_codec;
	wl1273 = snd_soc_codec_get_drvdata(codec);
	socdev->card->codec = codec;

	codec->name = "wl1273";
	codec->owner = THIS_MODULE;
	codec->dai = &wl1273_dai;
	codec->num_dai = 1;

	/* register pcms */
	r = snd_soc_new_pcms(socdev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1);
	if (r < 0) {
		dev_err(&pdev->dev, "Wl1273: failed to create pcms.\n");
		goto err2;
	}

	r = wl1273_add_controls(codec);
	if (r < 0) {
		dev_err(&pdev->dev, "Wl1273: failed to add contols.\n");
		goto err1;
	}

	return 0;
err1:
	snd_soc_free_pcms(socdev);
err2:
	return r;
}

static int wl1273_soc_remove(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);

	snd_soc_free_pcms(socdev);

	return 0;
}

static int __devinit wl1273_codec_probe(struct platform_device *pdev)
{
	struct wl1273_core **pdata = pdev->dev.platform_data;
	struct snd_soc_codec *codec;
	struct wl1273_priv *wl1273;
	int r;

	dev_dbg(&pdev->dev, "%s.\n", __func__);

	if (!pdata) {
		dev_err(&pdev->dev, "Platform data is missing.\n");
		return -EINVAL;
	}

	wl1273 = kzalloc(sizeof(struct wl1273_priv), GFP_KERNEL);
	if (wl1273 == NULL) {
		dev_err(&pdev->dev, "Cannot allocate memory.\n");
		return -ENOMEM;
	}

	wl1273->mode = WL1273_MODE_BT;
	wl1273->ctune = -1;
	wl1273->tx_power = -1;
	wl1273->rds_mode = -1;
	wl1273->pwr_mode = -1;
	wl1273->audio_mode = -1;
	wl1273->region = -1;
	wl1273->preemphasis = -1;
	wl1273->spacing = -1;
	wl1273->core = *pdata;

	codec = &wl1273->codec;
	snd_soc_codec_set_drvdata(codec, wl1273);
	codec->dev = &pdev->dev;
	wl1273_dai.dev = &pdev->dev;

	mutex_init(&codec->mutex);
	INIT_LIST_HEAD(&codec->dapm_widgets);
	INIT_LIST_HEAD(&codec->dapm_paths);

	codec->name = "wl1273";
	codec->owner = THIS_MODULE;
	codec->dai = &wl1273_dai;
	codec->num_dai = 1;

	platform_set_drvdata(pdev, wl1273);
	wl1273_codec = codec;

	codec->bias_level = SND_SOC_BIAS_OFF;

	r = snd_soc_register_codec(codec);
	if (r != 0) {
		dev_err(codec->dev, "Failed to register codec: %d\n", r);
		goto err2;
	}

	r = snd_soc_register_dai(&wl1273_dai);
	if (r != 0) {
		dev_err(codec->dev, "Failed to register DAIs: %d\n", r);
		goto err1;
	}

	return 0;

err1:
	snd_soc_unregister_codec(codec);
err2:
	kfree(wl1273);
	return r;
}

static int __devexit wl1273_codec_remove(struct platform_device *pdev)
{
	struct wl1273_priv *wl1273 = platform_get_drvdata(pdev);

	dev_dbg(&pdev->dev, "%s\n", __func__);

	snd_soc_unregister_dai(&wl1273_dai);
	snd_soc_unregister_codec(&wl1273->codec);

	kfree(wl1273);
	wl1273_codec = NULL;

	return 0;
}

MODULE_ALIAS("platform:wl1273_codec_audio");

static struct platform_driver wl1273_codec_driver = {
	.probe		= wl1273_codec_probe,
	.remove		= __devexit_p(wl1273_codec_remove),
	.driver		= {
		.name	= "wl1273_codec_audio",
		.owner	= THIS_MODULE,
	},
};

static int __init wl1273_modinit(void)
{
	return platform_driver_register(&wl1273_codec_driver);
}
module_init(wl1273_modinit);

static void __exit wl1273_exit(void)
{
	platform_driver_unregister(&wl1273_codec_driver);
}
module_exit(wl1273_exit);

struct snd_soc_codec_device soc_codec_dev_wl1273 = {
	.probe = wl1273_soc_probe,
	.remove = wl1273_soc_remove,
	.suspend = wl1273_soc_suspend,
	.resume = wl1273_soc_resume};
EXPORT_SYMBOL_GPL(soc_codec_dev_wl1273);

MODULE_AUTHOR("Matti Aaltonen <matti.j.aaltonen@nokia.com>");
MODULE_DESCRIPTION("ASoC WL1273 codec driver");
MODULE_LICENSE("GPL");
