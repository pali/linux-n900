/*
 * dfl61-twl4030.c -- SoC audio for TWL4030 on Nokia DFL61 class devices
 *
 * Author: Peter Ujfalusi <peter.ujfalusi@nokia.com>
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
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/jack.h>

#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <mach/gpio.h>
#include <plat/dfl61-audio.h>
#include <plat/mcbsp.h>

#include "omap-mcbsp.h"
#include "omap-pcm.h"
#include "../codecs/twl4030.h"

#define IHF_ENABLE_GPIO		(192 + 7)
#define PIEZO_ENABLE_GPIO	(98)
#define JACK_REPORT_MASK	(SND_JACK_MECHANICAL | SND_JACK_AVOUT | \
				 SND_JACK_HEADSET)

static enum dfl61audio_twl4030_config audio_config;
static unsigned int sys_freq;

static struct snd_soc_card snd_soc_machine_dfl61twl;
static struct snd_soc_jack dfl61_jack;

static struct dfl61audio_hsmic_event *hsmic_event;

void dfl61_jack_report(int status)
{
	if (dfl61_jack.card)
		snd_soc_jack_report(&dfl61_jack, status, JACK_REPORT_MASK);
	else
		pr_err("dfl61-twl4030: Cannot report jack status");
}
EXPORT_SYMBOL(dfl61_jack_report);

int dfl61_request_hsmicbias(bool enable)
{
	struct snd_soc_codec *codec = snd_soc_machine_dfl61twl.codec;

	if (!codec) {
		pr_err("dfl61-twl4030: Cannot set hsmicbias yet");
		return -ENODEV;
	}

	if (enable)
		snd_soc_dapm_force_enable_pin(codec, "Headset Mic Bias");
	else
		snd_soc_dapm_disable_pin(codec, "Headset Mic Bias");

	return snd_soc_dapm_sync(codec);
}
EXPORT_SYMBOL(dfl61_request_hsmicbias);

void dfl61_register_hsmic_event_cb(struct dfl61audio_hsmic_event *event)
{
	hsmic_event = event;
}
EXPORT_SYMBOL(dfl61_register_hsmic_event_cb);

static int dfl61twl_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->dai->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_codec *codec = socdev->card->codec;
	unsigned int	fmt;
	int ret;

	switch (params_channels(params)) {
	case 2: /* Stereo I2S mode */
		fmt =	SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBM_CFM;

		if (audio_config == AUDIO_CONFIG3)
			snd_soc_dapm_disable_pin(codec, "Digital2 Mic");
		break;
	case 4: /* Four channel TDM mode */
		fmt =	SND_SOC_DAIFMT_DSP_A |
			SND_SOC_DAIFMT_IB_NF |
			SND_SOC_DAIFMT_CBM_CFM;

		if (audio_config == AUDIO_CONFIG3)
			snd_soc_dapm_enable_pin(codec, "Digital2 Mic");
		break;
	default:
		return -EINVAL;
	}
	/* Set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, fmt);
	if (ret < 0) {
		pr_err("dfl61-twl4030: Can't set codec DAI configuration\n");
		return ret;
	}

	/* Set cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, fmt);
	if (ret < 0) {
		pr_err("dfl61-twl4030: Can't set cpu DAI configuration\n");
		return ret;
	}

	/* Set the codec system clock for DAC and ADC */
	ret = snd_soc_dai_set_sysclk(codec_dai, 0, sys_freq,
					    SND_SOC_CLOCK_IN);
	if (ret < 0) {
		pr_err("dfl61-twl4030: Can't set codec system clock\n");
		return ret;
	}

	return 0;
}

static int dfl61twl_spk_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *k, int event)
{
	if (SND_SOC_DAPM_EVENT_ON(event))
		gpio_set_value(IHF_ENABLE_GPIO, 1);
	else
		gpio_set_value(IHF_ENABLE_GPIO, 0);

	return 0;
}

static int dfl61twl_dac33_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *k, int event)
{
	if (SND_SOC_DAPM_EVENT_ON(event))
		dfl61dac33_interconnect_enable(1);
	else
		dfl61dac33_interconnect_enable(0);

	return 0;
}

static int dfl61twl_piezo_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *k, int event)
{
	if (SND_SOC_DAPM_EVENT_ON(event))
		gpio_set_value(PIEZO_ENABLE_GPIO, 1);
	else
		gpio_set_value(PIEZO_ENABLE_GPIO, 0);

	return 0;
}

static int dfl61twl_hsmic_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *k, int event)
{
	if (!hsmic_event || !hsmic_event->event)
		return 0;

	if (SND_SOC_DAPM_EVENT_ON(event))
		hsmic_event->event(hsmic_event->private, 1);
	else
		hsmic_event->event(hsmic_event->private, 0);

	return 0;
}

/* DAPM widgets and routing for audio config1 */
static const struct snd_soc_dapm_widget config1_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Ext Spk", dfl61twl_spk_event),
	SND_SOC_DAPM_SPK("Earpiece", NULL),
	SND_SOC_DAPM_SPK("Vibra", NULL),
	SND_SOC_DAPM_SPK("Piezo", dfl61twl_piezo_event),

	SND_SOC_DAPM_MIC("Digital Mic", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
};

static const struct snd_soc_dapm_route config1_audio_map[] = {
	{"Ext Spk", NULL, "PREDRIVER"},
	{"Earpiece", NULL, "EARPIECE"},
	{"Vibra", NULL, "VIBRA"},
	{"Piezo", NULL, "HSOL"},

	{"DIGIMIC0", NULL, "Mic Bias 1"},
	{"Mic Bias 1", NULL, "Digital Mic"},

	{"HSMIC", NULL, "Headset Mic Bias"},
	{"Headset Mic Bias", NULL, "Headset Mic"},
};

/* DAPM widgets and routing for audio config2 */
static const struct snd_soc_dapm_widget config2_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Ext Spk", dfl61twl_spk_event),
	SND_SOC_DAPM_SPK("Earpiece", NULL),
	SND_SOC_DAPM_SPK("Vibra", NULL),
	SND_SOC_DAPM_SPK("Piezo", dfl61twl_piezo_event),
	SND_SOC_DAPM_SPK("DAC33 interconnect", dfl61twl_dac33_event),

	SND_SOC_DAPM_MIC("Digital Mic", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),

	SND_SOC_DAPM_LINE("FMRX Left Line-in", NULL),
	SND_SOC_DAPM_LINE("FMRX Right Line-in", NULL),
};

static const struct snd_soc_dapm_route config2_audio_map[] = {
	{"Ext Spk", NULL, "PREDRIVER"},
	{"Earpiece", NULL, "EARPIECE"},
	{"Vibra", NULL, "VIBRA"},
	{"Piezo", NULL, "HSOL"},
	{"DAC33 interconnect", NULL, "PREDRIVEL"},

	{"DIGIMIC0", NULL, "Mic Bias 1"},
	{"Mic Bias 1", NULL, "Digital Mic"},

	{"HSMIC", NULL, "Headset Mic Bias"},
	{"Headset Mic Bias", NULL, "Headset Mic"},

	{"AUXL", NULL, "FMRX Left Line-in"},
	{"AUXR", NULL, "FMRX Right Line-in"},
};

/* DAPM widgets and routing for audio config3 */
static const struct snd_soc_dapm_widget config3_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Ext Spk", dfl61twl_spk_event),
	SND_SOC_DAPM_SPK("Earpiece", NULL),
	SND_SOC_DAPM_SPK("HAC", NULL),
	SND_SOC_DAPM_SPK("Vibra", NULL),
	SND_SOC_DAPM_SPK("Piezo", dfl61twl_piezo_event),
	SND_SOC_DAPM_SPK("DAC33 interconnect", dfl61twl_dac33_event),

	SND_SOC_DAPM_MIC("Digital Mic", NULL),
	SND_SOC_DAPM_MIC("Digital2 Mic", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", dfl61twl_hsmic_event),

	SND_SOC_DAPM_LINE("FMRX Left Line-in", NULL),
	SND_SOC_DAPM_LINE("FMRX Right Line-in", NULL),
};

static const struct snd_soc_dapm_route config3_audio_map[] = {
	{"Ext Spk", NULL, "PREDRIVER"},
	{"Earpiece", NULL, "EARPIECE"},
	{"HAC", NULL, "HFL"},
	{"Vibra", NULL, "VIBRA"},
	{"Piezo", NULL, "HSOL"},
	{"DAC33 interconnect", NULL, "PREDRIVEL"},

	{"DIGIMIC0", NULL, "Mic Bias 1"},
	{"Mic Bias 1", NULL, "Digital Mic"},

	{"DIGIMIC1", NULL, "Mic Bias 2"},
	{"Mic Bias 2", NULL, "Digital2 Mic"},

	{"HSMIC", NULL, "Headset Mic Bias"},
	{"Headset Mic Bias", NULL, "Headset Mic"},

	{"AUXL", NULL, "FMRX Left Line-in"},
	{"AUXR", NULL, "FMRX Right Line-in"},
};

/* DAPM widgets and routing for audio config4 */
static const struct snd_soc_dapm_widget config4_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Ext Spk", dfl61twl_spk_event),
	SND_SOC_DAPM_SPK("Earpiece", NULL),
	SND_SOC_DAPM_SPK("HAC", NULL),
	SND_SOC_DAPM_SPK("Vibra", NULL),
	SND_SOC_DAPM_SPK("DAC33 interconnect", dfl61twl_dac33_event),

	SND_SOC_DAPM_MIC("Digital Mic", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", dfl61twl_hsmic_event),

	SND_SOC_DAPM_LINE("FMRX Left Line-in", NULL),
	SND_SOC_DAPM_LINE("FMRX Right Line-in", NULL),
};

static const struct snd_soc_dapm_route config4_audio_map[] = {
	{"Ext Spk", NULL, "PREDRIVER"},
	{"Earpiece", NULL, "EARPIECE"},
	{"HAC", NULL, "HFL"},
	{"Vibra", NULL, "VIBRA"},
	{"DAC33 interconnect", NULL, "PREDRIVEL"},

	{"DIGIMIC0", NULL, "Mic Bias 1"},
	{"Mic Bias 1", NULL, "Digital Mic"},

	{"HSMIC", NULL, "Headset Mic Bias"},
	{"Headset Mic Bias", NULL, "Headset Mic"},

	{"AUXL", NULL, "FMRX Left Line-in"},
	{"AUXR", NULL, "FMRX Right Line-in"},
};

/* Pre DAC routings for the twl4030 codec */
static const char *twl4030_predacl1_texts[] = {
	"SDRL1", "SDRM1", "SDRL2", "SDRM2",
};
static const char *twl4030_predacr1_texts[] = {
	"SDRR1", "SDRM1", "SDRR2", "SDRM2"
};
static const char *twl4030_predacl2_texts[] = {"SDRL2", "SDRM2"};
static const char *twl4030_predacr2_texts[] = {"SDRR2", "SDRM2"};

static const struct soc_enum twl4030_predacl1_enum =
	SOC_ENUM_SINGLE(TWL4030_REG_RX_PATH_SEL, 2,
			ARRAY_SIZE(twl4030_predacl1_texts),
			twl4030_predacl1_texts);

static const struct soc_enum twl4030_predacr1_enum =
	SOC_ENUM_SINGLE(TWL4030_REG_RX_PATH_SEL, 0,
			ARRAY_SIZE(twl4030_predacr1_texts),
			twl4030_predacr1_texts);

static const struct soc_enum twl4030_predacl2_enum =
	SOC_ENUM_SINGLE(TWL4030_REG_RX_PATH_SEL, 5,
			ARRAY_SIZE(twl4030_predacl2_texts),
			twl4030_predacl2_texts);

static const struct soc_enum twl4030_predacr2_enum =
	SOC_ENUM_SINGLE(TWL4030_REG_RX_PATH_SEL, 4,
			ARRAY_SIZE(twl4030_predacr2_texts),
			twl4030_predacr2_texts);

static const struct snd_kcontrol_new dfl61twl_controls[] = {
	/* Mux controls before the DACs */
	SOC_ENUM("DACL1 Playback Mux", twl4030_predacl1_enum),
	SOC_ENUM("DACR1 Playback Mux", twl4030_predacr1_enum),
	SOC_ENUM("DACL2 Playback Mux", twl4030_predacl2_enum),
	SOC_ENUM("DACR2 Playback Mux", twl4030_predacr2_enum),
};

static int dfl61twl_twl4030_init(struct snd_soc_codec *codec)
{
	int err;

	/* Create jack for accessory reporting */
	err = snd_soc_jack_new(&snd_soc_machine_dfl61twl, "Jack",
				JACK_REPORT_MASK , &dfl61_jack);
	if (err < 0)
		return err;

	/* Add DFL61 specific controls */
	snd_soc_add_controls(codec, dfl61twl_controls,
				ARRAY_SIZE(dfl61twl_controls));

	/* Disable unused pins */
	snd_soc_dapm_nc_pin(codec, "MAINMIC");
	snd_soc_dapm_nc_pin(codec, "CARKITMIC");
	snd_soc_dapm_nc_pin(codec, "SUBMIC");

	snd_soc_dapm_nc_pin(codec, "HSOR");
	snd_soc_dapm_nc_pin(codec, "CARKITL");
	snd_soc_dapm_nc_pin(codec, "CARKITR");
	snd_soc_dapm_nc_pin(codec, "HFR");

	switch (audio_config) {
	case AUDIO_CONFIG1:
		/* Disable unused pins */
		snd_soc_dapm_nc_pin(codec, "AUXL");
		snd_soc_dapm_nc_pin(codec, "AUXR");
		snd_soc_dapm_nc_pin(codec, "PREDRIVEL");
		snd_soc_dapm_nc_pin(codec, "HFL");
		snd_soc_dapm_nc_pin(codec, "DIGIMIC1");
		/* Add config specific widgets and routes */
		snd_soc_dapm_new_controls(codec, config1_dapm_widgets,
					ARRAY_SIZE(config1_dapm_widgets));
		snd_soc_dapm_add_routes(codec, config1_audio_map,
					ARRAY_SIZE(config1_audio_map));
		break;
	case AUDIO_CONFIG2:
		/* Disable unused pins */
		snd_soc_dapm_nc_pin(codec, "HFL");
		snd_soc_dapm_nc_pin(codec, "DIGIMIC1");
		/* Add config specific widgets and routes */
		snd_soc_dapm_new_controls(codec, config2_dapm_widgets,
					ARRAY_SIZE(config2_dapm_widgets));
		snd_soc_dapm_add_routes(codec, config2_audio_map,
					ARRAY_SIZE(config2_audio_map));
		break;
	case AUDIO_CONFIG3:
		/* Add config specific widgets and routes */
		snd_soc_dapm_new_controls(codec, config3_dapm_widgets,
					ARRAY_SIZE(config3_dapm_widgets));
		snd_soc_dapm_add_routes(codec, config3_audio_map,
					ARRAY_SIZE(config3_audio_map));
		break;
	case AUDIO_CONFIG4:
		/* Disable unused pins */
		snd_soc_dapm_nc_pin(codec, "DIGIMIC1");
		snd_soc_dapm_nc_pin(codec, "HSOL");
		/* Add config specific widgets and routes */
		snd_soc_dapm_new_controls(codec, config4_dapm_widgets,
					ARRAY_SIZE(config4_dapm_widgets));
		snd_soc_dapm_add_routes(codec, config4_audio_map,
					ARRAY_SIZE(config4_audio_map));
		break;
	}

	/* Add McBSP3 */
	if (omap_mcbsp_st_add_controls(codec, 2))
		dev_dbg(codec->dev, "Unable to set Sidetone for McBSP3\n");

	/* Threshold + SMARTIDLE */
	omap_mcbsp_set_dma_op_mode(2, MCBSP_DMA_MODE_THRESHOLD);
	omap_mcbsp_set_sidle_mode(2, MCBSP_SIDLE_SMART_IDLE,
					MCBSP_CLKACT_IOFF_FON);
	return 0;
}

static struct snd_soc_ops dfl61twl_ops = {
	.hw_params = dfl61twl_hw_params,
};

/* Digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link dfl61twl_dai = {
	.name = "TWL4030",
	.stream_name = "TWL4030",
	.cpu_dai = &omap_mcbsp_dai[DFL61_TWL4030_ID],
	.codec_dai = &twl4030_dai[TWL4030_DAI_HIFI],
	.init = dfl61twl_twl4030_init,
	.ops = &dfl61twl_ops,
};

/* Audio machine (card) driver */
static struct snd_soc_card snd_soc_machine_dfl61twl = {
	.name = "dfl61-twl4030",
	.platform = &omap_soc_platform,
	.dai_link = &dfl61twl_dai,
	.num_links = 1,
};

/* twl4030 setup */
static struct twl4030_setup_data twl4030_setup;

/* Audio subsystem */
static struct snd_soc_device dfl61twl_snd_devdata = {
	.card = &snd_soc_machine_dfl61twl,
	.codec_dev = &soc_codec_dev_twl4030,
	.codec_data = &twl4030_setup,
};

static struct platform_device *dfl61twl_snd_device;

static int dfl61twl_probe(struct platform_device *pdev)
{
	int ret;
	struct dfl61audio_twl4030_platform_data *pdata =
							pdev->dev.platform_data;

	audio_config = pdata->audio_config;
	sys_freq = pdata->freq;

	dfl61twl_snd_device = platform_device_alloc("soc-audio",
							DFL61_TWL4030_ID);
	if (!dfl61twl_snd_device) {
		pr_err("dfl61-twl4030: Platform device allocation failed\n");
		return -ENOMEM;
	}

	twl4030_setup.sysclk = sys_freq / 1000;
	twl4030_setup.ramp_delay_value = 2;
	twl4030_setup.offset_cncl_path = TWL4030_OFFSET_CNCL_SEL_ARX2;
	twl4030_setup.check_defaults = 0;
	twl4030_setup.reset_registers = 0;
	twl4030_setup.digimic_delay = 0;

	platform_set_drvdata(dfl61twl_snd_device, &dfl61twl_snd_devdata);
	dfl61twl_snd_devdata.dev = &dfl61twl_snd_device->dev;
	*(unsigned int *)dfl61twl_dai.cpu_dai->private_data = 2; /* McBSP3 */

	ret = gpio_request(IHF_ENABLE_GPIO, "IHF_EN");
	if (ret < 0) {
		pr_err("dfl61-twl4030: Can not get gpio for IHF\n");
		goto err_gpio;
	}
	gpio_direction_output(IHF_ENABLE_GPIO, 0);

	if (audio_config != AUDIO_CONFIG4) {
		ret = gpio_request(PIEZO_ENABLE_GPIO, "PIEZO_EN");
		if (ret < 0) {
			pr_err("dfl61-twl4030: Can not get gpio for Piezo\n");
			gpio_free(IHF_ENABLE_GPIO);
			goto err_gpio;
		}
		gpio_direction_output(PIEZO_ENABLE_GPIO, 0);
	}

	ret = platform_device_add(dfl61twl_snd_device);
	if (ret) {
		pr_err("dfl61-twl4030: Unable to add platform device\n");
		goto err_pdevice;
	}

	pr_info("DFL61 TWL SoC init done (config %d)\n", audio_config + 1);
	return 0;

err_pdevice:
	gpio_free(IHF_ENABLE_GPIO);
	if (audio_config != AUDIO_CONFIG4)
		gpio_free(PIEZO_ENABLE_GPIO);
err_gpio:
	platform_device_put(dfl61twl_snd_device);

	return ret;
}

static int __exit dfl61twl_remove(struct platform_device *pdev)
{
	platform_device_unregister(dfl61twl_snd_device);
	gpio_free(IHF_ENABLE_GPIO);

	if (audio_config != AUDIO_CONFIG4)
		gpio_free(PIEZO_ENABLE_GPIO);

	return 0;
}

static struct platform_driver dfl61twl_driver = {
	.probe  = dfl61twl_probe,
	.remove = __exit_p(dfl61twl_remove),
	.driver = {
		.name = "dfl61audio-twl4030",
		.owner = THIS_MODULE,
	},
};

static int __init dfl61twl_module_init(void)
{
	return platform_driver_register(&dfl61twl_driver);
}
module_init(dfl61twl_module_init);

static void __exit dfl61twl_module_exit(void)
{
	platform_driver_unregister(&dfl61twl_driver);
}
module_exit(dfl61twl_module_exit);

MODULE_AUTHOR("Peter Ujfalusi <peter.ujfalusi@nokia.com>");
MODULE_DESCRIPTION("ALSA SoC for TWL4030 on Nokia DFL61 class devices");
MODULE_LICENSE("GPL");
