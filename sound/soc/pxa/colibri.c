#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>

#include <asm/mach-types.h>
#include <mach/audio.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/ac97_codec.h>

#include "pxa2xx-ac97.h"
#include "../codecs/wm9712.h"

static struct snd_soc_ops colibri_ops;

static struct snd_soc_dai_link colibri_dai[] = {
	{
		.name = "AC97",
		.stream_name = "AC97 HiFi",
		.cpu_dai_name = "pxa2xx-ac97",
		.codec_dai_name = "wm9712-hifi",
		.codec_name = "wm9712-codec",
		.platform_name = "pxa-pcm-audio",
		.ops = &colibri_ops,
	},
	{
		.name = "AC97 Aux",
		.stream_name = "AC97 Aux",
		.cpu_dai_name = "pxa2xx-ac97-aux",
		.codec_dai_name ="wm9712-aux",
		.codec_name = "wm9712-codec",
		.platform_name = "pxa-pcm-audio",
		.ops = &colibri_ops,
	},
};

static struct snd_soc_card colibri = {
	.name = "WM9715G",
	.owner = THIS_MODULE,
	.dai_link = colibri_dai,
	.num_links = ARRAY_SIZE(colibri_dai),
};

static int colibri_wm9715_probe(struct platform_device *pdev)
{
	int rc;

	colibri.dev = &pdev->dev;
	rc =  snd_soc_register_card(&colibri);
	if (rc)
		dev_warn(&pdev->dev, "snd_soc_register_card() failed with %d in %s()!\n",
		    rc, __FUNCTION__);
	return rc;
}

static int colibri_wm9715_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);
	return 0;
}

static struct platform_driver colibri_wm9715_driver = {
	.probe		= colibri_wm9715_probe,
	.remove		= colibri_wm9715_remove,
	.driver		= {
		.name		= "colibri-wm9715",
		.owner		= THIS_MODULE,
	},
};

module_platform_driver(colibri_wm9715_driver);

/* Module information */
MODULE_AUTHOR("Alex A. Mihaylov (minimumlaw@rambler.ru)");
MODULE_DESCRIPTION("ALSA SoC WM9715 Toradex Colibri rev2.0a and more");
MODULE_LICENSE("GPL");
