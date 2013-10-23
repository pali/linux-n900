/*
 * dfl61-wl1273.c -- SoC audio for WL1273 on Nokia DFL61 class devices
 *
 * Author: Matti Aaltonen <matti.j.aaltonen@nokia.com>
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

#include <sound/soc.h>
#include <asm/mach-types.h>
#include <plat/dfl61-audio.h>
#include <plat/mcbsp.h>

#include "omap-mcbsp.h"
#include "omap-pcm.h"
#include "../codecs/wl1273.h"

static int dfl61wl1273_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_dai *codec_dai = rtd->dai->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
	struct snd_soc_codec *codec = socdev->card->codec;
	unsigned int fmt;
	int r;

	switch (wl1273_get_codec_mode(codec)) {
	case WL1273_MODE_FM_RX:
	case WL1273_MODE_FM_TX:
		fmt =	SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBM_CFM;

		break;
	case WL1273_MODE_BT:
		fmt =	SND_SOC_DAIFMT_DSP_A |
			SND_SOC_DAIFMT_IB_NF |
			SND_SOC_DAIFMT_CBM_CFM;

		break;
	default:
		return -EINVAL;
	}

	/* Set codec DAI configuration */
	r = snd_soc_dai_set_fmt(codec_dai, fmt);
	if (r < 0) {
		pr_err("Can't set codec DAI configuration: %d\n", r);
		return r;
	}

	/* Set cpu DAI configuration */
	r = snd_soc_dai_set_fmt(cpu_dai, fmt);
	if (r < 0) {
		pr_err("Can't set cpu DAI configuration: %d\n", r);
		return r;
	}
	return 0;
}

static struct snd_soc_ops dfl61wl1273_ops = {
	.startup = dfl61wl1273_startup,
};

/* Digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link dfl61wl1273_dai = {
	.name = "BT/FM PCM",
	.stream_name = "BT/FM Stream",
	.cpu_dai = &omap_mcbsp_dai[DFL61_WL1273_ID],
	.codec_dai = &wl1273_dai,
	.ops = &dfl61wl1273_ops,
};

/* Audio card driver */
static struct snd_soc_card snd_soc_card_dfl61wl1273 = {
	.name = "dfl61-wl1273",
	.platform = &omap_soc_platform,
	.dai_link = &dfl61wl1273_dai,
	.num_links = 1,
};

/* Audio subsystem */
static struct snd_soc_device dfl61wl1273_snd_devdata = {
	.card = &snd_soc_card_dfl61wl1273,
	.codec_dev = &soc_codec_dev_wl1273,
};

static struct platform_device *dfl61wl1273_snd_device;

static int __init dfl61wl1273_soc_init(void)
{
	int r;

	if (!machine_is_nokia_rx71() &&
	    !machine_is_nokia_rm680() && !machine_is_nokia_rm696())
		return -ENODEV;

	dfl61wl1273_snd_device = platform_device_alloc("soc-audio",
							DFL61_WL1273_ID);
	if (!dfl61wl1273_snd_device) {
		pr_err("Platform device allocation failed\n");
		return -ENOMEM;
	}

	platform_set_drvdata(dfl61wl1273_snd_device, &dfl61wl1273_snd_devdata);
	dfl61wl1273_snd_devdata.dev = &dfl61wl1273_snd_device->dev;
	*(unsigned int *)dfl61wl1273_dai.cpu_dai->private_data = 3; /* McBSP4 */

	r = platform_device_add(dfl61wl1273_snd_device);
	if (r)
		goto fail;

	/* Threshold + SMARTIDLE */
	omap_mcbsp_set_dma_op_mode(3, MCBSP_DMA_MODE_THRESHOLD);
	omap_mcbsp_set_sidle_mode(3, MCBSP_SIDLE_SMART_IDLE,
				  MCBSP_CLKACT_IOFF_FON);

	return 0;

fail:
	pr_err("Unable to add platform device\n");
	platform_device_put(dfl61wl1273_snd_device);

	return r;
}

static void __exit dfl61wl1273_soc_exit(void)
{
	platform_device_unregister(dfl61wl1273_snd_device);
}

module_init(dfl61wl1273_soc_init);
module_exit(dfl61wl1273_soc_exit);

MODULE_AUTHOR("Matti Aaltonen <matti.j.aaltonen@nokia.com>");
MODULE_DESCRIPTION("ALSA SoC for WL1273 on Nokia DFL61 class devices");
MODULE_LICENSE("GPL");
