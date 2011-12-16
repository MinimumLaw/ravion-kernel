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

static struct snd_soc_dai_link colibri_dai[] = {
	{
		.name = "AC97 HiFi",
		.stream_name = "AC97 HiFi",
		.cpu_dai = &pxa_ac97_dai[PXA2XX_DAI_AC97_HIFI],
		.codec_dai = &wm9712_dai[WM9712_DAI_AC97_HIFI],
	},
	{
		.name = "AC97 Aux",
		.stream_name = "AC97 Aux",
		.cpu_dai = &pxa_ac97_dai[PXA2XX_DAI_AC97_AUX],
		.codec_dai = &wm9712_dai[WM9712_DAI_AC97_AUX],
	},
};

static struct snd_soc_card colibri = {
	.name = "WM9715G",
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

	if ( system_rev < 0x20a ) {
		printk("[I] For sound on module revision before 2.0a use CONFIG_SND_PXA2XX_AC97\n"
		"[I] and disable ASoC code or try modprobe snd-pxa2xx-ac97 on next reboot\n");
		return -ENODEV;
	}

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
