/*
 * linux/drivers/pcmcia/pxa/pxa2xx_colibri.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * OAO Radioavionka., 2009
 * Alex A. Mihaylov <minimumlaw@rambler.ru>
 *
 * Based on:
 * Compulab Ltd., 2003, 2007, 2008
 * Mike Rapoport <mike@compulab.co.il>
 *
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/gpio.h>

#include <asm/mach-types.h>
#include <asm/system.h>
#include <mach/colibri.h>
//#include <mach/system.h>
#include <mach/gpio.h>

#include <mach/pxa3xx-regs.h>
#include <mach/mfp-pxa3xx.h>

#include "soc_common.h"

static struct pcmcia_irqs coli32x_irqs[] = {
	{ 0, COLI32X_PCMCIA_PCD_IRQ, "PCMCIA0 CD" },
};

static int coli32x_pcmcia_hw_init(struct soc_pcmcia_socket *skt)
{
    int ret;

    switch (skt->nr) {
    case 0:
	/* Ouput signals */
	ret = gpio_request(COLI32X_PCMCIA_nPPEN, "nPPEN");
	if (ret)
		return ret;
	gpio_direction_output(COLI32X_PCMCIA_nPPEN, 0); /* switch on */

	ret = gpio_request(COLI32X_PCMCIA_PRST, "PRST");
	if (ret)
		return ret;
	gpio_direction_output(COLI32X_PCMCIA_PRST, 1);

	/* Input signals */
	ret = gpio_request(COLI32X_PCMCIA_PRDY, "PRDY");
	if (ret)
		return ret;
	gpio_direction_input(COLI32X_PCMCIA_PRDY);

	ret = gpio_request(COLI32X_PCMCIA_PBVD1, "PBVD1");
	if (ret)
		return ret;
	gpio_direction_input(COLI32X_PCMCIA_PBVD1);

	ret = gpio_request(COLI32X_PCMCIA_PBVD2, "PBVD2");
	if (ret)
		return ret;
	gpio_direction_input(COLI32X_PCMCIA_PBVD2);

	// Reset PCMCIA Card
	gpio_direction_output(COLI32X_PCMCIA_PRST, 1);
	udelay(100);
	gpio_direction_output(COLI32X_PCMCIA_PRST, 0);

	skt->socket.pci_irq = COLI32X_PCMCIA_PRDY_IRQ;
	ret = soc_pcmcia_request_irqs(skt, coli32x_irqs, ARRAY_SIZE(coli32x_irqs));
	if (ret) {
    		printk(KERN_INFO "+++ %s - irq req failed +++\n", __FUNCTION__);
		gpio_free(COLI32X_PCMCIA_PRST);
		gpio_free(COLI32X_PCMCIA_nPPEN);
	};
	break;
    default:
	printk(KERN_INFO "Try hw_init NOT present PCCards socket!\n");
	ret = -ENODEV;
    };
    return ret;
}

static void coli32x_pcmcia_shutdown(struct soc_pcmcia_socket *skt)
{
    switch (skt->nr) {
    case 0:
	soc_pcmcia_free_irqs(skt, coli32x_irqs, ARRAY_SIZE(coli32x_irqs));
	gpio_free(COLI32X_PCMCIA_PRST);
	gpio_free(COLI32X_PCMCIA_nPPEN);
	gpio_free(COLI32X_PCMCIA_PRDY);
	gpio_free(COLI32X_PCMCIA_PBVD1);
	gpio_free(COLI32X_PCMCIA_PBVD2);
    default:
	printk(KERN_INFO "Try shutdown NOT present PCCards socket!\n");
    };
}


static void coli32x_pcmcia_socket_state(struct soc_pcmcia_socket *skt,
				       struct pcmcia_state *state)
{
    switch (skt->nr) {
    case 0:
	state->detect = (gpio_get_value(COLI32X_PCMCIA_PCD) == 0)	? 0 : 1;
	state->ready  = (gpio_get_value(COLI32X_PCMCIA_PRDY) == 0)	? 0 : 1;
	state->bvd1   = (gpio_get_value(COLI32X_PCMCIA_PBVD1) == 0)	? 0 : 1;
	state->bvd2   = (gpio_get_value(COLI32X_PCMCIA_PBVD2) == 0)	? 0 : 1;
	state->vs_3v  = 1;  /* only 3,3 v */
	state->vs_Xv  = 0;  /* no other voltage supported */
	state->wrprot = 0;  /* not available */
	break;
    default:
	printk(KERN_INFO "Try request NOT present PCCards socket!\n");
	state->detect = 0;
	state->ready  = 0;
	state->bvd1   = 0;
	state->bvd2   = 0;
	state->vs_3v  = 0;
	state->vs_Xv  = 0;
	state->wrprot = 0;
    };
}


static int coli32x_pcmcia_configure_socket(struct soc_pcmcia_socket *skt,
					  const socket_state_t *state)
{
    unsigned long flags;
    int ret;

    // Configure socket

    local_irq_save(flags);
    switch (skt->nr) {
    case 0:
        // configure Vcc and Vpp 
        if (state->Vcc == 0 && state->Vpp == 0)
        {
	    gpio_set_value(COLI32X_PCMCIA_nPPEN, 1);
	    printk(KERN_INFO "PCMCIA socket0 power disabled\n"); 
        }
        else if (state->Vcc == 33 && state->Vpp < 50)
	{
	    // PCMCIA socket0 power enabled
	    gpio_set_value(COLI32X_PCMCIA_nPPEN, 0);
	}
	else
	{
    	    printk(KERN_ERR "%s(): unsupported Vcc %u Vpp %u combination\n",
               __FUNCTION__, state->Vcc, state->Vpp);
    	    return -1;
	}

	// reset PCMCIA if requested
        if (state->flags & SS_RESET)
	{
	    // Reset card in slot0
	    gpio_set_value(COLI32X_PCMCIA_PRST, 1);
	}

	if (state->flags & SS_OUTPUT_ENA)
	{
	    // PCCards0 output enabled
	    coli32x_init_cf_bus();
	}

	ret = 0;
	break;
    default:
	ret = -1;
    };

    local_irq_restore(flags);
    udelay(200);
    return ret;
}

static void coli32x_pcmcia_socket_init(struct soc_pcmcia_socket *skt)
{
    switch( skt->nr ) {
    case 0:
	// PCCards socket0 init (bus off, power & reset)
	// Disable bus 
	coli32x_exit_cf_bus();
	// Power ON socket 
	gpio_set_value(COLI32X_PCMCIA_nPPEN, 0);
	// Reset socket 
        gpio_set_value(COLI32X_PCMCIA_PRST, 1);
        udelay(10);
        gpio_set_value(COLI32X_PCMCIA_PRST, 0);
	break;
    default:
	printk(KERN_INFO "Try init NOT present PCMCIA socket\n");
    };
}

static void coli32x_pcmcia_socket_suspend(struct soc_pcmcia_socket *skt)
{
    switch( skt->nr ) {
    case 0:
	/* PCCards socket0 suspend (power off) */
	gpio_set_value(COLI32X_PCMCIA_nPPEN, 1);
	break;
    default:
	printk(KERN_INFO "Try suspend NOT present PCMCIA socket\n");
    };
}


static struct pcmcia_low_level coli32x_pcmcia_ops __initdata = {
	.owner			= THIS_MODULE,
	.hw_init		= coli32x_pcmcia_hw_init,
	.hw_shutdown		= coli32x_pcmcia_shutdown,
	.socket_state		= coli32x_pcmcia_socket_state,
	.configure_socket	= coli32x_pcmcia_configure_socket,
	.socket_init		= coli32x_pcmcia_socket_init,
	.socket_suspend		= coli32x_pcmcia_socket_suspend,
	.nr			= 1,
	.first			= 0, 
};

static struct platform_device *coli32x_pcmcia_device;

int __init coli32x_pcmcia_init(void)
{
	int ret;

	coli32x_pcmcia_device = platform_device_alloc("pxa32x-pcmcia", -1);

	if (!coli32x_pcmcia_device)
		return -ENOMEM;

	ret = platform_device_add_data(coli32x_pcmcia_device, &coli32x_pcmcia_ops,
				       sizeof(coli32x_pcmcia_ops));

	if (ret == 0) {
		printk(KERN_INFO "Registering Colibri PXA320 PCMCIA interface.\n");
		ret = platform_device_add(coli32x_pcmcia_device);
	}

	if (ret) {
		printk(KERN_INFO "Put Colibri PXA320 PCMCIA interface to platform device list.\n");
		platform_device_put(coli32x_pcmcia_device);
        }

	return ret;
}

void __exit coli32x_pcmcia_exit(void)
{
	platform_device_unregister(coli32x_pcmcia_device);
}

static int __init colibri32x_pcmcia_init(void)
{
	int ret = -ENODEV;

	if ( cpu_is_pxa320() )
	    ret = coli32x_pcmcia_init();
	else
            printk(KERN_INFO "This cpu is NOT PXA320 - skip pxa320_colibri module.\n");

	return ret;
}

static void __exit colibri32x_pcmcia_exit(void)
{
	coli32x_pcmcia_exit();
}

module_init(colibri32x_pcmcia_init);
module_exit(colibri32x_pcmcia_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alex A. Mihaylov <minimumlaw@rambler.ru>");
MODULE_DESCRIPTION("Toradex Colibri PXA320 CF driver");
