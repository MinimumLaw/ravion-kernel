/*
 * SoC audio driver for Toradex Colibri
 *
 * Copyright (C) 2010-2011 Noser Engineering
 *
 * 2010-11-19: Marcel Ziswiler <marcel.ziswiler@noser.com>
 *             initial version (note: WM9715L is fully WM9712 compatible)
 *
 * Copied from tosa.c:
 * Copyright 2005 Wolfson Microelectronics PLC.
 * Copyright 2005 Openedhand Ltd.
 *
 * Authors: Liam Girdwood <lrg@slimlogic.co.uk>
 *          Richard Purdie <richard@openedhand.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/gpio.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include <asm/mach-types.h>
#include <mach/audio.h>

#include "../codecs/wm9712.h"
#include "pxa2xx-pcm.h"
#include "pxa2xx-ac97.h"

static const struct snd_soc_dapm_widget colibri_dapm_widgets[] = {
	SND_SOC_DAPM_HP("HEADPHONE", NULL),
	SND_SOC_DAPM_LINE("LINEIN", NULL),
	SND_SOC_DAPM_MIC("MIC_IN", NULL),
};

/* Currently supported audio map */
static const struct snd_soc_dapm_route colibri_audio_map[] = {
	/* Colibri SODIMM pin 1 (MIC_IN)
	   Colibri Evaluation Board: Audio jack X26 bottom pink
	   Orchid: Audio jack X11 bottom pink MIC in */
	{ "MIC_IN", NULL, "MIC1" },

	/* Colibri SODIMM pin 5 & 7 (LINEIN_L/R)
	   Colibri Evaluation Board: Audio jack X26 top blue
	   Orchid: Audio jack X11 top blue line in
	   MECS Tellurium: Audio jack X11 pin 1 & 2 */
	{ "LINEIN", NULL, "LINEINL" },
	{ "LINEIN", NULL, "LINEINR" },

	/* Colibri SODIMM pin 15 & 17 (HEADPHONE_L/R)
	   Colibri Evaluation Board: Audio jack X26 middle green
	   Orchid: Audio jack X11 middle green line out
	   Protea: Audio jack X53 line out
	   MECS Tellurium: Audio jack X11 pin 4 & 5 (HEADPHONE_LF/RF) */
	{ "HEADPHONE", NULL, "LOUT2" },
	{ "HEADPHONE", NULL, "ROUT2" },
};

static int colibri_wm9712l_init(struct snd_soc_codec *codec)
{
	int err;

	/* add Colibri specific widgets */
	err = snd_soc_dapm_new_controls(codec, colibri_dapm_widgets,
					ARRAY_SIZE(colibri_dapm_widgets));
	if (err)
		return err;

	/* set up Colibri specific audio path audio_map */
	err = snd_soc_dapm_add_routes(codec, colibri_audio_map, ARRAY_SIZE(colibri_audio_map));
	if (err)
		return err;

	/* connected pins */
	snd_soc_dapm_enable_pin(codec, "MIC1");
	snd_soc_dapm_enable_pin(codec, "LINEINL");
	snd_soc_dapm_enable_pin(codec, "LINEINR");
	snd_soc_dapm_enable_pin(codec, "LOUT2");
	snd_soc_dapm_enable_pin(codec, "ROUT2");

	/* not connected pins */
	snd_soc_dapm_nc_pin(codec, "MIC2");
	snd_soc_dapm_nc_pin(codec, "PHONE");
	snd_soc_dapm_nc_pin(codec, "PCBEEP");
	snd_soc_dapm_nc_pin(codec, "MONOOUT");
	snd_soc_dapm_nc_pin(codec, "OUT3");
	snd_soc_dapm_nc_pin(codec, "HPOUTL");
	snd_soc_dapm_nc_pin(codec, "HPOUTR");

	err = snd_soc_dapm_sync(codec);
	if (err)
		return err;

	return 0;
}

static struct snd_soc_dai_link colibri_dai[] = {
	{
		.name = "AC97 HiFi",
		.stream_name = "AC97 HiFi",
		.cpu_dai = &pxa_ac97_dai[PXA2XX_DAI_AC97_HIFI],
		.codec_dai = &wm9712_dai[WM9712_DAI_AC97_HIFI],
		.init = colibri_wm9712l_init,
	},
	{
		.name = "AC97 Aux",
		.stream_name = "AC97 Aux",
		.cpu_dai = &pxa_ac97_dai[PXA2XX_DAI_AC97_AUX],
		.codec_dai = &wm9712_dai[WM9712_DAI_AC97_AUX],
	},
};

static struct snd_soc_card colibri = {
	.name = "Toradex Colibri",
	.platform = &pxa2xx_soc_platform,
	.dai_link = colibri_dai,
	.num_links = ARRAY_SIZE(colibri_dai),
};

static struct snd_soc_device colibri_snd_devdata = {
	.card = &colibri,
	.codec_dev = &soc_codec_dev_wm9712,
};

static struct platform_device *colibri_snd_device;

static int __init colibri_init(void)
{
	int ret;

	if (!(machine_is_colibri() || machine_is_colibri320()))
		return -ENODEV;

	colibri_snd_device = platform_device_alloc("soc-audio", -1);
	if (!colibri_snd_device)
		return -ENOMEM;

	platform_set_drvdata(colibri_snd_device, &colibri_snd_devdata);
	colibri_snd_devdata.dev = &colibri_snd_device->dev;
	ret = platform_device_add(colibri_snd_device);

	if (!ret)
		return 0;

/* Fail gracefully */
	platform_device_put(colibri_snd_device);

	return ret;
}

static void __exit colibri_exit(void)
{
	platform_device_unregister(colibri_snd_device);
}

module_init(colibri_init);
module_exit(colibri_exit);

/* Module information */
MODULE_AUTHOR("Marcel Ziswiler");
MODULE_DESCRIPTION("ALSA SoC WM9715L on Toradex Colibri");
MODULE_LICENSE("GPL");
