/*======================================================================

  Device driver for the PCMCIA control functionality of PXA32x
  microprocessors.

    The contents of this file may be used under the
    terms of the GNU Public License version 2 (the "GPL")

    (c) Alex Mihaylov (minimumlaw@rambler.ru), 2010

    derived from pxa2xx_base.c

    (c) Ian Molton (spyro@f2s.com) 2003
    (c) Stefan Eletzhofer (stefan.eletzhofer@inquant.de) 2003,4

    derived from sa11xx_base.c

     Portions created by John G. Dorsey are
     Copyright (C) 1999 John G. Dorsey.

  ======================================================================*/

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>

#include <mach/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <mach/pxa3xx-regs.h>
#include <asm/mach-types.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/ss.h>
#include <pcmcia/cistpl.h>

#include "soc_common.h"
#include "pxa32x_base.h"

// For MCMEM, MCIO, MCATTR and MECR regiser and SMC_MECR_CIT definition
#ifdef CONFIG_MACH_COLIBRI320
#include <mach/colibri.h>
#endif

/*
 * Personal Computer Memory Card International Association (PCMCIA) sockets
 */

#define PCMCIAPrtSp	0x04000000	/* PCMCIA Partition Space [byte]   */
#define PCMCIASp	(4*PCMCIAPrtSp)	/* PCMCIA Space [byte]             */
#define PCMCIAIOSp	PCMCIAPrtSp	/* PCMCIA I/O Space [byte]         */
#define PCMCIAAttrSp	PCMCIAPrtSp	/* PCMCIA Attribute Space [byte]   */
#define PCMCIAMemSp	PCMCIAPrtSp	/* PCMCIA Memory Space [byte]      */

#define PCMCIA0Sp	PCMCIASp	/* PCMCIA 0 Space [byte]           */
#define PCMCIA0IOSp	PCMCIAIOSp	/* PCMCIA 0 I/O Space [byte]       */
#define PCMCIA0AttrSp	PCMCIAAttrSp	/* PCMCIA 0 Attribute Space [byte] */
#define PCMCIA0MemSp	PCMCIAMemSp	/* PCMCIA 0 Memory Space [byte]    */

#define _PCMCIA0	(0x20000000)	/* PCMCIA [0]                      */
#define _PCMCIAIO0	(0x20000000)	/* PCMCIA I/O [0]                  */
#define _PCMCIAAttr0	(0x28000000)	/* PCMCIA Attribute [0]            */
#define _PCMCIAMem0	(0x2C000000)	/* PCMCIA Memory [0]               */

#define MCXX_SETUP_MASK     (0x7f)
#define MCXX_ASST_MASK      (0x1f)
#define MCXX_HOLD_MASK      (0x3f)
#define MCXX_SETUP_SHIFT    (0)
#define MCXX_ASST_SHIFT     (7)
#define MCXX_HOLD_SHIFT     (14)

static inline u_int pxa32x_mcxx_hold(u_int pcmcia_cycle_ns,
				     u_int mem_clk_10khz)
{
	u_int code = pcmcia_cycle_ns * mem_clk_10khz;
	return (code / 300000) + ((code % 300000) ? 1 : 0) - 1;
}

static inline u_int pxa32x_mcxx_asst(u_int pcmcia_cycle_ns,
				     u_int mem_clk_10khz)
{
	u_int code = pcmcia_cycle_ns * mem_clk_10khz;
	return (code / 300000) + ((code % 300000) ? 1 : 0) + 1;
}

static inline u_int pxa32x_mcxx_setup(u_int pcmcia_cycle_ns,
				      u_int mem_clk_10khz)
{
	u_int code = pcmcia_cycle_ns * mem_clk_10khz;
	return (code / 100000) + ((code % 100000) ? 1 : 0) - 1;
}

/* This function returns the (approximate) command assertion period, in
 * nanoseconds, for a given CPU clock frequency and MCXX_ASST value:
 */
static inline u_int pxa32x_pcmcia_cmd_time(u_int mem_clk_10khz,
					   u_int pcmcia_mcxx_asst)
{
	return (300000 * (pcmcia_mcxx_asst + 1) / mem_clk_10khz);
}

static int pxa32x_pcmcia_set_mcmem( int sock, int speed, int clock )
{
    if ( sock == 0 ) {
	SMC_MCMEM = ((pxa32x_mcxx_setup(speed, clock)
		& MCXX_SETUP_MASK) << MCXX_SETUP_SHIFT)
		| ((pxa32x_mcxx_asst(speed, clock)
		& MCXX_ASST_MASK) << MCXX_ASST_SHIFT)
		| ((pxa32x_mcxx_hold(speed, clock)
		& MCXX_HOLD_MASK) << MCXX_HOLD_SHIFT);

	return 0;
    } else {
	printk(KERN_ERR "PXA32x platform support only one socket!!!\n");
	return -ENODEV;
    };
}

static int pxa32x_pcmcia_set_mcio( int sock, int speed, int clock )
{
    if ( sock == 0 ) {
	SMC_MCIO = ((pxa32x_mcxx_setup(speed, clock)
		& MCXX_SETUP_MASK) << MCXX_SETUP_SHIFT)
		| ((pxa32x_mcxx_asst(speed, clock)
		& MCXX_ASST_MASK) << MCXX_ASST_SHIFT)
		| ((pxa32x_mcxx_hold(speed, clock)
		& MCXX_HOLD_MASK) << MCXX_HOLD_SHIFT);

	return 0;
    } else {
	printk(KERN_ERR "PXA32x platform support only one socket!!!\n");
	return -ENODEV;
    };
}

static int pxa32x_pcmcia_set_mcatt( int sock, int speed, int clock )
{
    if ( sock == 0 ) {
	SMC_MCATT = ((pxa32x_mcxx_setup(speed, clock)
		& MCXX_SETUP_MASK) << MCXX_SETUP_SHIFT)
		| ((pxa32x_mcxx_asst(speed, clock)
		& MCXX_ASST_MASK) << MCXX_ASST_SHIFT)
		| ((pxa32x_mcxx_hold(speed, clock)
		& MCXX_HOLD_MASK) << MCXX_HOLD_SHIFT);

	return 0;
    } else {
	printk(KERN_ERR "PXA32x platform support only one socket!!!\n");
	return -ENODEV;
    };
}

static int pxa32x_pcmcia_set_mcxx(struct soc_pcmcia_socket *skt, unsigned int clk)
{
	struct soc_pcmcia_timing timing;
	int sock = skt->nr;

	soc_common_pcmcia_get_timing(skt, &timing);

	pxa32x_pcmcia_set_mcmem(sock, timing.mem, clk);
	pxa32x_pcmcia_set_mcatt(sock, timing.attr, clk);
	pxa32x_pcmcia_set_mcio(sock, timing.io, clk);

	return 0;
}

static int pxa32x_pcmcia_set_timing(struct soc_pcmcia_socket *skt)
{
	unsigned int clk = get_memclk_frequency_10khz();
	return pxa32x_pcmcia_set_mcxx(skt, clk);
}

#ifdef CONFIG_CPU_FREQ

static int
pxa32x_pcmcia_frequency_change(struct soc_pcmcia_socket *skt,
			       unsigned long val,
			       struct cpufreq_freqs *freqs)
{
#warning "it's not clear if this is right since the core CPU (N) clock has no effect on the memory (L) clock"
	switch (val) {
	case CPUFREQ_PRECHANGE:
		if (freqs->new > freqs->old) {
			debug(skt, 2, "new frequency %u.%uMHz > %u.%uMHz, "
			       "pre-updating\n",
			       freqs->new / 1000, (freqs->new / 100) % 10,
			       freqs->old / 1000, (freqs->old / 100) % 10);
			pxa32x_pcmcia_set_mcxx(skt, freqs->new);
		}
		break;

	case CPUFREQ_POSTCHANGE:
		if (freqs->new < freqs->old) {
			debug(skt, 2, "new frequency %u.%uMHz < %u.%uMHz, "
			       "post-updating\n",
			       freqs->new / 1000, (freqs->new / 100) % 10,
			       freqs->old / 1000, (freqs->old / 100) % 10);
			pxa32x_pcmcia_set_mcxx(skt, freqs->new);
		}
		break;
	}
	return 0;
}
#endif

static void pxa32x_configure_sockets(struct device *dev)
{
	/*
	 * We have at least one socket, so set MECR:CIT
	 * (Card Is There)
	 */
	SMC_MECR |= SMC_MECR_CIT;
}

static const char *skt_names[] = {
	"PCMCIA socket 0",
};

#define SKT_DEV_INFO_SIZE(n) \
	(sizeof(struct skt_dev_info) + (n)*sizeof(struct soc_pcmcia_socket))

int pxa32x_drv_pcmcia_add_one(struct soc_pcmcia_socket *skt)
{
    if ( skt->nr == 0 ) { // PXA 32x have only one socket
	skt->res_skt.start = _PCMCIA0;
	skt->res_skt.end = _PCMCIA0 + PCMCIASp - 1;
	skt->res_skt.name = skt_names[skt->nr];
	skt->res_skt.flags = IORESOURCE_MEM;

	skt->res_io.start = _PCMCIAIO0;
	skt->res_io.end = _PCMCIAIO0 + PCMCIAIOSp - 1;
	skt->res_io.name = "io";
	skt->res_io.flags = IORESOURCE_MEM | IORESOURCE_BUSY;

	skt->res_mem.start = _PCMCIAMem0;
	skt->res_mem.end = _PCMCIAMem0 + PCMCIAMemSp - 1;
	skt->res_mem.name = "memory";
	skt->res_mem.flags = IORESOURCE_MEM;

	skt->res_attr.start = _PCMCIAAttr0;
	skt->res_attr.end = _PCMCIAAttr0 + PCMCIAAttrSp - 1;
	skt->res_attr.name = "attribute";
	skt->res_attr.flags = IORESOURCE_MEM;

	return soc_pcmcia_add_one(skt);
    } else {
	printk(KERN_ERR "Marvell PXA32x have only one PCMCIA socket!\n");
	return -ENODEV;
    }
}
EXPORT_SYMBOL(pxa32x_drv_pcmcia_add_one);

void pxa32x_drv_pcmcia_ops(struct pcmcia_low_level *ops)
{
	/* Provide our PXA32x specific timing routines. */
	ops->set_timing  = pxa32x_pcmcia_set_timing;
#ifdef CONFIG_CPU_FREQ
	ops->frequency_change = pxa32x_pcmcia_frequency_change;
#endif
}
EXPORT_SYMBOL(pxa32x_drv_pcmcia_ops);

static int pxa32x_drv_pcmcia_probe(struct platform_device *dev)
{
	int i, ret = 0;
	struct pcmcia_low_level *ops;
	struct skt_dev_info *sinfo;
	struct soc_pcmcia_socket *skt;

	ops = (struct pcmcia_low_level *)dev->dev.platform_data;
	if (!ops)
		return -ENODEV;

	pxa32x_drv_pcmcia_ops(ops);

	sinfo = kzalloc(SKT_DEV_INFO_SIZE(ops->nr), GFP_KERNEL);
	if (!sinfo)
		return -ENOMEM;

	sinfo->nskt = ops->nr;

	/* Initialize processor specific parameters */
	for (i = 0; i < ops->nr; i++) {
		skt = &sinfo->skt[i];

		skt->nr = ops->first + i;
		skt->ops = ops;
		skt->socket.owner = ops->owner;
		skt->socket.dev.parent = &dev->dev;
		skt->socket.pci_irq = NO_IRQ;

		ret = pxa32x_drv_pcmcia_add_one(skt);
		if (ret)
			break;
	}

	if (ret) {
		while (--i >= 0)
			soc_pcmcia_remove_one(&sinfo->skt[i]);
		kfree(sinfo);
	} else {
		pxa32x_configure_sockets(&dev->dev);
		dev_set_drvdata(&dev->dev, sinfo);
	}

	return ret;
}

static int pxa32x_drv_pcmcia_remove(struct platform_device *dev)
{
	struct skt_dev_info *sinfo = platform_get_drvdata(dev);
	int i;

	platform_set_drvdata(dev, NULL);

	for (i = 0; i < sinfo->nskt; i++)
		soc_pcmcia_remove_one(&sinfo->skt[i]);

	kfree(sinfo);
	return 0;
}

static int pxa32x_drv_pcmcia_resume(struct device *dev)
{
	pxa32x_configure_sockets(dev);
	return 0;
}

static const struct dev_pm_ops pxa32x_drv_pcmcia_pm_ops = {
	.resume		= pxa32x_drv_pcmcia_resume,
};

static struct platform_driver pxa32x_pcmcia_driver = {
	.probe		= pxa32x_drv_pcmcia_probe,
	.remove		= pxa32x_drv_pcmcia_remove,
	.driver		= {
		.name	= "pxa32x-pcmcia",
		.owner	= THIS_MODULE,
		.pm	= &pxa32x_drv_pcmcia_pm_ops,
	},
};

static int __init pxa32x_pcmcia_init(void)
{
	return platform_driver_register(&pxa32x_pcmcia_driver);
}

static void __exit pxa32x_pcmcia_exit(void)
{
	platform_driver_unregister(&pxa32x_pcmcia_driver);
}

fs_initcall(pxa32x_pcmcia_init);
module_exit(pxa32x_pcmcia_exit);

MODULE_AUTHOR("Alex Mihaylov <minimumlaw@rambler.ru>");
MODULE_DESCRIPTION("Linux PCMCIA Card Services: PXA32x core socket driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:pxa32x-pcmcia");
