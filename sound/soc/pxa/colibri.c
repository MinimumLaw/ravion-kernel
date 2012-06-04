#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/gpio.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>

#include <asm/mach-types.h>
#include <asm/system_info.h>
#include <mach/audio.h>

#include "../codecs/wm9712.h"
#include "pxa2xx-ac97.h"

static struct snd_soc_dai_link wm9715g_dai[] = {
    {
	.name = "AC97 HiFi",
	.stream_name = "AC97 HiFi",
	.cpu_dai_name = "pxa2xx-ac97",
	.codec_dai_name =  "wm9712-hifi",
	.codec_name = "wm9712-codec",
	.platform_name = "pxa-pcm-audio",
    },
    {
	.name = "AC97 Aux",
	.stream_name = "AC97 Aux",
	.cpu_dai_name = "pxa2xx-ac97-aux",
	.codec_dai_name = "wm9712-aux",
	.codec_name = "wm9712-codec",
	.platform_name = "pxa-pcm-audio",
    },
};

static struct snd_soc_card wm9715g_card = {
        .name = "WM9715G",
        .dai_link = wm9715g_dai,
        .num_links = ARRAY_SIZE(wm9715g_dai),
};

static struct platform_device *colibri_snd_device;

static int __init colibri_asoc_init(void)
{
        int ret;

	if ( !( machine_is_colibri() || machine_is_colibri320() ) )
	    return -ENODEV;

	if ( system_rev < 0x20a ) {
	    printk(KERN_ERR "No ASoC driver for UCB1400. Use CONFIG_SND_PXA2XX_AC97 codec!\n");
	    return -ENODEV;
	};


        colibri_snd_device = platform_device_alloc("soc-audio", -1);
        if (!colibri_snd_device)
            return -ENOMEM;

        platform_set_drvdata(colibri_snd_device, &wm9715g_card);
        ret = platform_device_add(colibri_snd_device);

        if ( ret != 0 ) {
            platform_device_put(colibri_snd_device);
            printk(KERN_INFO "[!] cleanup device: platform_device_add() return %d\n", ret);
        };

        return ret;
}

static void __exit colibri_asoc_exit(void)
{
        platform_device_unregister(colibri_snd_device);
}


module_init(colibri_asoc_init);
module_exit(colibri_asoc_exit);

/* Module information */
MODULE_AUTHOR("Alex A. Mihaylov <MinimumLaw@Rambler.Ru>");
MODULE_DESCRIPTION("ALSA SoC driver for Toradex Colibri rev > 2.0a");
MODULE_LICENSE("GPL v2");
