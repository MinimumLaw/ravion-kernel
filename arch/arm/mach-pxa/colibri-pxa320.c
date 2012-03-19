/*
 *  arch/arm/mach-pxa/colibri-pxa320.c
 *
 *  Support for Toradex PXA320/310 based Colibri module
 *
 *  Daniel Mack <daniel@caiaq.de>
 *  Matthias Meier <matthias.j.meier@gmx.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/usb/gpio_vbus.h>
#include <linux/i2c.h>
#include <linux/i2c/pxa-i2c.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/lp3972.h>
#include <linux/regulator/max8660.h>

#include <asm/mach-types.h>
#include <asm/sizes.h>
#include <asm/mach/arch.h>
#include <asm/mach/irq.h>
#include <asm/mach/map.h>

#include <mach/pxa320.h>
#include <mach/colibri.h>
#include <mach/pxafb.h>
#include <mach/ohci.h>
#include <mach/audio.h>
#include <mach/pxa27x-udc.h>
#include <mach/udc.h>

#include "generic.h"
#include "devices.h"

#ifdef	CONFIG_MACH_COLIBRI_EVALBOARD
static mfp_cfg_t colibri_pxa320_evalboard_pin_config[] __initdata = {
	/* MMC */
	GPIO22_MMC1_CLK,
	GPIO23_MMC1_CMD,
	GPIO18_MMC1_DAT0,
	GPIO19_MMC1_DAT1,
	GPIO20_MMC1_DAT2,
	GPIO21_MMC1_DAT3,
	GPIO28_GPIO,	/* SD detect */

	/* UART 1 configuration (may be set by bootloader) */
	GPIO99_UART1_CTS,
	GPIO104_UART1_RTS,
	GPIO97_UART1_RXD,
	GPIO98_UART1_TXD,
	GPIO101_UART1_DTR,
	GPIO103_UART1_DSR,
	GPIO100_UART1_DCD,
	GPIO102_UART1_RI,

	/* UART 2 configuration */
	GPIO109_UART2_CTS,
	GPIO112_UART2_RTS,
	GPIO110_UART2_RXD,
	GPIO111_UART2_TXD,

	/* UART 3 configuration */
	GPIO30_UART3_RXD,
	GPIO31_UART3_TXD,

	/* UHC */
	GPIO2_2_USBH_PEN,
	GPIO3_2_USBH_PWR,

	/* I2C */
	GPIO32_I2C_SCL,
	GPIO33_I2C_SDA,

	/* PCMCIA */
	MFP_CFG(GPIO59, AF7),	/* SoDIMM75 CIF_MCLK  MUX GPIO77	*/
	MFP_CFG(GPIO61, AF7),	/* SoDIMM94 CIF_HSYNC MUX CPLD nPCE1	*/
	MFP_CFG(GPIO60, AF7),	/* SoDIMM96 CIF_PCLK  MUX CPLD nPCE2	*/
	MFP_CFG(GPIO62, AF7),	/* SoDIMM81 CIF_VSYNC MUX GPIO81	*/
	MFP_CFG(GPIO56, AF7),	/* SoDIMM59 CIF_DD7   MUX GPIO14 ???	*/
	MFP_CFG(GPIO27, AF7),	/* SoDIMM93 GPIO27    MUX CPLD RDnWR	*/
	MFP_CFG(GPIO50, AF7),	/* SoDIMM98 GPIO50    MUX CPLD nPREG	*/
	MFP_CFG(GPIO51, AF7),	/* SoDIMM101 GPIO51   MUX GPIO6_nPIOW	*/
	MFP_CFG(GPIO52, AF7),	/* SoDIMM103 GPIO52   MUX GPIO5_nPIOR	*/
	MFP_CFG(GPIO54, AF7),	/* SoDIMM97 GPIO54    MUX CPLD DF_CLE_nOE */
	MFP_CFG(GPIO93, AF7),	/* SoDIMM99 GPIO93    MUX CPLD DF_ALE_nWE */
	MFP_CFG(GPIO122, AF7),	/* SoDIMM100 GPIO122  MUX CPLD nPXCVREN	*/
	MFP_CFG(GPIO125, AF7),	/* SoDIMM85  GPIO125  MUX GPIO57 nPPEN	*/
	GPIO2_RDY,              /* PXA270 - DNU, PXA320 - GPIO (not CF) */
	GPIO4_nCS3,             /* CPLD and ext. CSs */
	GPIO5_NPIOR,
	GPIO6_NPIOW,
	GPIO7_NPIOS16,
	GPIO8_NPWAIT,
	GPIO29_GPIO | MFP_LPM_EDGE_FALL,	/* PRDY  (READY GPIO) */
	GPIO57_GPIO,				/* nPPEN (POWER GPIO) */
	GPIO81_GPIO | MFP_LPM_EDGE_BOTH,	/* PCD  (DETECT GPIO) */
	GPIO77_GPIO,				/* PRST  (RESET GPIO) */
	GPIO53_GPIO,				/* PBVD1 */
	GPIO79_GPIO,				/* PBVD2 */
};
#else
static mfp_cfg_t colibri_pxa320_evalboard_pin_config[] __initdata = {};
#endif

#define COLIBRI_ETH_IRQ_GPIO	mfp_to_gpio(GPIO36_GPIO)

#if defined(CONFIG_AX88796) || \
    defined(CONFIG_AX88796C) || \
    defined(CONFIG_AX88796_MODULE) || \
    defined(CONFIG_AX88796C_MODULE)

/*
 * Asix AX88796B/AX88796C platform specific data
 */
static struct ax_plat_data colibri_asix_platdata = {
	.flags		= 0, /* defined later */
	.wordlength	= 2,
};

static mfp_cfg_t colibri_pxa320_eth_pin_config[] __initdata = {
	GPIO3_nCS2,			/* AX88796 chip select */
	GPIO36_GPIO | MFP_PULL_HIGH	/* AX88796 IRQ */
};

/*
 * Asix AX88796B Ethernet on Colibri v1.x
 */
static struct resource ax88796b_resource[] = {
	[0] = {
		.start = PXA3xx_CS2_PHYS,
		.end   = PXA3xx_CS2_PHYS + (0x20 * 2) - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = PXA_GPIO_TO_IRQ(COLIBRI_ETH_IRQ_GPIO),
		.end   = PXA_GPIO_TO_IRQ(COLIBRI_ETH_IRQ_GPIO),
		.flags = IORESOURCE_IRQ | IRQF_TRIGGER_FALLING,
	}
};

static struct platform_device ax88796b_device = {
	.name		= "ax88796",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(ax88796b_resource),
	.resource	= ax88796b_resource,
	.dev		= {
		.platform_data = &colibri_asix_platdata
	}
};

/*
 * Asix AX88796C Ethernet on Colibri v2.x
 */

static struct resource ax88796c_resource[] = {
	[0] = {
		.start = PXA3xx_CS2_PHYS,
		.end   = PXA3xx_CS2_PHYS + 0xFFFF,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = gpio_to_irq(COLIBRI_ETH_IRQ_GPIO),
		.end   = gpio_to_irq(COLIBRI_ETH_IRQ_GPIO),
		.flags = IORESOURCE_IRQ | IRQF_TRIGGER_FALLING,
	}
};

static struct platform_device ax88796c_device = {
	.name		= "ax88796c",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(ax88796c_resource),
	.resource	= ax88796c_resource,
	.dev		= {
		.platform_data = &colibri_asix_platdata
	}
};

static void __init colibri_pxa320_init_eth(void)
{
	colibri_pxa3xx_init_eth(&colibri_asix_platdata);
	pxa3xx_mfp_config(ARRAY_AND_SIZE(colibri_pxa320_eth_pin_config));
	if ( system_rev < 0x20a )
	    platform_device_register(&ax88796b_device);
	else
	    platform_device_register(&ax88796c_device);
}
#else
static inline void __init colibri_pxa320_init_eth(void) {}
#endif /* CONFIG_AX88796 */

#if defined(CONFIG_USB_PXA27X)||defined(CONFIG_USB_PXA27X_MODULE)
static struct gpio_vbus_mach_info colibri_pxa320_gpio_vbus_info = {
	.gpio_vbus		= mfp_to_gpio(MFP_PIN_GPIO96),
	.gpio_pullup		= -1,
};

static struct platform_device colibri_pxa320_gpio_vbus = {
	.name	= "gpio-vbus",
	.id	= -1,
	.dev	= {
		.platform_data	= &colibri_pxa320_gpio_vbus_info,
	},
};

static void colibri_pxa320_udc_command(int cmd)
{
	if (cmd == PXA2XX_UDC_CMD_CONNECT)
		UP2OCR = UP2OCR_HXOE | UP2OCR_DPPUE;
	else if (cmd == PXA2XX_UDC_CMD_DISCONNECT)
		UP2OCR = UP2OCR_HXOE | UP2OCR_HXS
		    | UP2OCR_DPPDE | UP2OCR_DMPDE;
}

static struct pxa2xx_udc_mach_info colibri_pxa320_udc_info __initdata = {
	.udc_command		= colibri_pxa320_udc_command,
	.gpio_pullup		= -1,
};

static void __init colibri_pxa320_init_udc(void)
{
	pxa_set_udc_info(&colibri_pxa320_udc_info);
	platform_device_register(&colibri_pxa320_gpio_vbus);
}
#else
static inline void colibri_pxa320_init_udc(void) {}
#endif

#if defined(CONFIG_FB_PXA) || defined(CONFIG_FB_PXA_MODULE)
static mfp_cfg_t colibri_pxa320_lcd_pin_config[] __initdata = {
	GPIO6_2_LCD_LDD_0,
	GPIO7_2_LCD_LDD_1,
	GPIO8_2_LCD_LDD_2,
	GPIO9_2_LCD_LDD_3,
	GPIO10_2_LCD_LDD_4,
	GPIO11_2_LCD_LDD_5,
	GPIO12_2_LCD_LDD_6,
	GPIO13_2_LCD_LDD_7,
	GPIO63_LCD_LDD_8,
	GPIO64_LCD_LDD_9,
	GPIO65_LCD_LDD_10,
	GPIO66_LCD_LDD_11,
	GPIO67_LCD_LDD_12,
	GPIO68_LCD_LDD_13,
	GPIO69_LCD_LDD_14,
	GPIO70_LCD_LDD_15,
	GPIO71_LCD_LDD_16,
	GPIO72_LCD_LDD_17,
	GPIO73_LCD_CS_N,
	GPIO74_LCD_VSYNC,
	GPIO14_2_LCD_FCLK,
	GPIO15_2_LCD_LCLK,
	GPIO16_2_LCD_PCLK,
	GPIO17_2_LCD_BIAS,
};

static void __init colibri_pxa320_init_lcd(void)
{
	pxa3xx_mfp_config(ARRAY_AND_SIZE(colibri_pxa320_lcd_pin_config));
}
#else
static inline void colibri_pxa320_init_lcd(void) {}
#endif

#if	defined(CONFIG_AC97_BUS) || \
	defined(CONFIG_AC97_BUS_MODULE)
static mfp_cfg_t colibri_pxa320_ac97_pin_config[] __initdata = {
	GPIO34_AC97_SYSCLK,
	GPIO35_AC97_SDATA_IN_0,
	GPIO37_AC97_SDATA_OUT,
	GPIO38_AC97_SYNC,
	GPIO39_AC97_BITCLK,
	GPIO40_AC97_nACRESET
};

/* Rev > 2.0a use WM9715l codec with aSoC - 100% comp. with wm9712 */
static struct platform_device wm9715l_device = {
	.name	= "wm9712-codec",
	.id	= -1,
};

static inline void __init colibri_pxa320_init_ac97(void)
{
	pxa3xx_mfp_config(ARRAY_AND_SIZE(colibri_pxa320_ac97_pin_config));
	pxa_set_ac97_info(NULL);
	if ( system_rev >= 0x20a )
	    platform_device_register(&wm9715l_device);
}
#else
static inline void colibri_pxa320_init_ac97(void) {}
#endif

/*
 * Power I2C depend on I2C
 */
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)

/* MAX8661 regilator on module rev < 2.0a (stub only) */
static struct max8660_platform_data max8660_regulators = {
        .num_subdevs = 0, // only stub in this moment
};

static struct i2c_board_info __initdata max8660_pwr_i2c_devs[] = {
    {
        .type = "max8660",
        .addr = 0x34,
        .platform_data = &max8660_regulators,
    },
};

/* LP3972 regulator on module rev 2.0a and later (stub + gpio) */
static struct lp3972_platform_data lp3972_regulators = {
    .gpio[0] = LP3972_GPIO_INPUT, // on chip gpio1,2 named in kernel gpio[0,1]
#if defined (CONFIG_PCMCIA_PXA2XX) || defined(CONFIG_PCMCIA_PXA2XX_MODULE)
    .gpio[1] = LP3972_GPIO_OUTPUT_LOW, // on colibri v2 LP3972 GPIO2 used for multiplexe SoDIMM pin ...
#endif
    .num_regulators = 0, // only stub in this moment
};

static struct i2c_board_info __initdata lp3972_pwr_i2c_devs[] = {
    {
        .type = "lp3972",
        .addr = 0x34,
        .platform_data = &lp3972_regulators,
    },
};

static void __init colibri_pxa320_init_power_i2c(void)
{
    pxa3xx_set_i2c_power_info(NULL);
    if ( system_rev < 0x20a )
	i2c_register_board_info(1, ARRAY_AND_SIZE(max8660_pwr_i2c_devs));
    else
	i2c_register_board_info(1, ARRAY_AND_SIZE(lp3972_pwr_i2c_devs));
}
#else
static inline void colibri_pxa320_init_power_i2c(void) {}
#endif

void __init colibri_pxa320_init(void)
{
	colibri_pxa320_init_eth();
	colibri_pxa320_init_power_i2c();
	colibri_pxa3xx_init_nand();
	colibri_pxa320_init_lcd();
	colibri_pxa3xx_init_lcd(mfp_to_gpio(GPIO49_GPIO));
	colibri_pxa320_init_ac97();
	colibri_pxa320_init_udc();

	/* Evalboard init */
	pxa3xx_mfp_config(ARRAY_AND_SIZE(colibri_pxa320_evalboard_pin_config));
	colibri_evalboard_init();
}

static struct map_desc colibri_pxa320_io_desc[] __initdata = {
	{       // CPLD regs
		.virtual	= CPLD_REGS_VIRT,
		.pfn		= __phys_to_pfn(CPLD_REGS_PHYS),
		.length		= CPLD_REGS_LEN,
		.type		= MT_DEVICE
	}
};

void __init colibri_pxa320_map_io(void)
{
	pxa3xx_map_io();
	iotable_init(ARRAY_AND_SIZE(colibri_pxa320_io_desc));
}


MACHINE_START(COLIBRI320, "Toradex Colibri PXA320")
	.atag_offset	= 0x100,
	.init_machine	= colibri_pxa320_init,
	.map_io		= colibri_pxa320_map_io,
	.init_irq	= pxa3xx_init_irq,
	.handle_irq	= pxa3xx_handle_irq,
	.timer		= &pxa_timer,
	.restart	= pxa_restart,
MACHINE_END

