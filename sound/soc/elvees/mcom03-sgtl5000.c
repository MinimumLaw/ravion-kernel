// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2022 RnD Center "ELVEES", JSC
 */

#include <linux/iio/consumer.h>
#include <linux/iio/iio.h>
#include <linux/input-event-codes.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "../codecs/arizona.h"
#include "../codecs/wm8998.h"

#define MCLK1_FREQ 12288000

#define MODEM_TDM_TX_MASK 1
#define MODEM_TDM_RX_MASK 1
#define MODEM_TDM_SLOTS 1
#define MODEM_TDM_SLOT_WIDTH 128

#warning This code have STUB idea and MUST be rewriten for SGTL5000 codec!

struct sound_sgtl5000_priv {
	struct device *dev;
};

static const struct snd_soc_dapm_widget sound_sgtl5000_dapm_widgets[] = {
	SND_SOC_DAPM_LINE("Modem In", NULL),
	SND_SOC_DAPM_LINE("Modem Out", NULL),
};

static int sound_sgtl5000_modem_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *dai = rtd->codec_dai;
	struct snd_soc_component *component = dai->component;
	int ret;

	ret = snd_soc_component_set_sysclk(component, ARIZONA_CLK_SYSCLK,
					   ARIZONA_CLK_SRC_MCLK1, MCLK1_FREQ,
					   SND_SOC_CLOCK_IN);
	if (ret && ret != -ENOTSUPP) {
		dev_err(dai->dev, "Failed to set sysclk source\n");
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(dai, ARIZONA_CLK_SYSCLK, MCLK1_FREQ,
				     SND_SOC_CLOCK_IN);
	if (ret && ret != -ENOTSUPP) {
		dev_err(dai->dev, "Failed to set sysclk for DAI %d\n",
			dai->id);
		return ret;
	}

	ret = snd_soc_dai_set_tdm_slot(dai, MODEM_TDM_TX_MASK,
				       MODEM_TDM_RX_MASK,
				       MODEM_TDM_SLOTS,
				       MODEM_TDM_SLOT_WIDTH);
	if (ret && ret != -ENOTSUPP) {
		dev_err(dai->dev, "Failed to set TDM slots\n");
		return ret;
	}

	return 0;
}

static const struct snd_soc_pcm_stream sound_sgtl5000_modem_params = {
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.rate_min = 8000,
	.rate_max = 8000,
	.channels_min = 1,
	.channels_max = 1,
};

static const struct snd_soc_dapm_widget sound_sgtl5000_modem_widgets[] = {
	SND_SOC_DAPM_INPUT("Modem RX"),
	SND_SOC_DAPM_OUTPUT("Modem TX"),
};

static const struct snd_soc_dapm_route sound_sgtl5000_modem_routes[] = {
	{ "Modem Capture", NULL, "Modem RX" },
	{ "Modem TX", NULL, "Modem Playback" },
};

static const struct snd_soc_component_driver sound_sgtl5000_component = {
	.name			= "trustphone-wm8998",
	.dapm_widgets		= sound_sgtl5000_modem_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(sound_sgtl5000_modem_widgets),
	.dapm_routes		= sound_sgtl5000_modem_routes,
	.num_dapm_routes	= ARRAY_SIZE(sound_sgtl5000_modem_routes),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static struct snd_soc_dai_driver sound_sgtl5000_modem_dai[] = {
	{
		.name = "sgtl5000",
		.playback = {
			.stream_name = "Playback",
			.channels_min = 1,
			.channels_max = 1,
			.rate_min = 8000,
			.rate_max = 8000,
			.rates = SNDRV_PCM_RATE_8000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.capture = {
			.stream_name = "Capture",
			.channels_min = 1,
			.channels_max = 1,
			.rate_min = 8000,
			.rate_max = 8000,
			.rates = SNDRV_PCM_RATE_8000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
	},
};

static struct snd_soc_dai_link sound_sgtl5000_dai[] = {
	{
		.name = "sgtl5000",
		.stream_name = "HiFi",
		.cpu_dai_name = "sgtl5000",
		.codec_dai_name = "sgtl5000",
		.dai_fmt = SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_CBM_CFM |
			   SND_SOC_DAIFMT_IB_NF,
		.init = &sound_sgtl5000_modem_init,
		.params = &sound_sgtl5000_modem_params,
		.ignore_suspend = 1,
	},
};

static struct snd_soc_card sound_sgtl5000_card = {
	.name = "elvees-sgtl5000",
	.owner = THIS_MODULE,
	.dai_link = sound_sgtl5000_dai,
	.num_links = ARRAY_SIZE(sound_sgtl5000_dai),
	.dapm_widgets = sound_sgtl5000_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(sound_sgtl5000_dapm_widgets),
};

static int sound_sgtl5000_probe(struct platform_device *pdev)
{
	struct sound_sgtl5000_priv *priv;
	struct snd_soc_card *card = &sound_sgtl5000_card;
	struct device_node *codec;
	struct snd_soc_dai_link *dai_link;
	struct device *dev = &pdev->dev;
	int ret, i;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	snd_soc_card_set_drvdata(card, priv);
	card->dev = dev;

	ret = snd_soc_of_parse_card_name(card, "model");
	if (ret < 0) {
		dev_err(dev, "Failed to get card name\n");
		return ret;
	}

	ret = devm_snd_soc_register_component(dev,
					      &sound_sgtl5000_component,
					      sound_sgtl5000_modem_dai,
					      ARRAY_SIZE(sound_sgtl5000_modem_dai));
	if (ret < 0) {
		dev_err(dev, "Failed to register component: %d\n", ret);
		goto out;
	}

	ret = devm_snd_soc_register_card(dev, card);
	if (ret)
		dev_err(dev, "Failed to register card %d\n", ret);

out:
	of_node_put(codec);

	return ret;
}

static const struct of_device_id sound_sgtl5000_of_match[] = {
	{ .compatible = "mcom03,sound-sgtl5000" },
	{ },
};
MODULE_DEVICE_TABLE(of, sound_sgtl5000_of_match);

static struct platform_driver sound_sgtl5000_driver = {
	.driver = {
		.name		= "mcom03-sgtl5000",
		.pm		= &snd_soc_pm_ops,
		.of_match_table	= sound_sgtl5000_of_match,
	},
	.probe	= sound_sgtl5000_probe,
};
module_platform_driver(sound_sgtl5000_driver);

MODULE_DESCRIPTION("ELVEES SGTL5000 soundcard");
MODULE_LICENSE("GPL v2");
MODULE_SOFTDEP("pre: i2c");
