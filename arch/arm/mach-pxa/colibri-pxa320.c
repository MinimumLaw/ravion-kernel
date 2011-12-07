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
#include <linux/pda_power.h>
#include <linux/power_supply.h>

#include <asm/mach-types.h>
#include <asm/sizes.h>
#include <asm/mach/arch.h>
#include <asm/mach/irq.h>
#include <asm/mach/map.h>

#include <mach/pxa3xx-regs.h>
#include <mach/mfp-pxa320.h>
#include <mach/colibri.h>
#include <mach/pxafb.h>
#include <mach/ohci.h>
#include <mach/udc.h>
#include <mach/pxa27x-udc.h>
#include <mach/audio.h>
#include <plat/i2c.h>

#include <linux/spi/spi.h>
#include <mach/pxa2xx_spi.h>

#include <linux/regulator/max8660.h>
#include <linux/regulator/lp3972.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/consumer.h>

#include <linux/gpio_keys.h>
#include <linux/input.h>

#include "generic.h"
#include "devices.h"

#if defined(CONFIG_AX88796) || \
    defined(CONFIG_AX88796C) || \
    defined(CONFIG_AX88796_MODULE) || \
    defined(CONFIG_AX88796C_MODULE)
#define COLIBRI_ETH_IRQ_GPIO	mfp_to_gpio(GPIO36_GPIO)

/*
 * Asix AX88796 and AX88796C Ethernet common
 */
static u8 ax88796_mac_addr[] = { 0x00,0x14,0x2D,0x00,0x00,0x00 };
 
static struct ax_plat_data colibri_asix_platdata = {
	.flags		= 0, //AXFLG_MAC_FROMPLATFORM, //| AXFLG_MAC_FROMDEV,
	.wordlength	= 2,
	.mac_addr	= ax88796_mac_addr,
};

static mfp_cfg_t colibri_pxa320_eth_pin_config[] __initdata = {
	GPIO0_DRQ | MFP_PULL_HIGH | MFP_DS10X,
	GPIO3_nCS2 | MFP_DS10X,		/* AX88796 chip select */
	GPIO36_GPIO | MFP_PULL_HIGH	/* AX88796 IRQ */
};

/*
 * Asix AX88976 resource for colibri rev < 2.0a
 */
static struct resource colibri_ax88796_resource[] = {
	[0] = {
		.start = PXA3xx_CS2_PHYS,
		.end   = PXA3xx_CS2_PHYS + (0x20 * 2) - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = gpio_to_irq(COLIBRI_ETH_IRQ_GPIO),
		.end   = gpio_to_irq(COLIBRI_ETH_IRQ_GPIO),
		.flags = IORESOURCE_IRQ | IRQF_TRIGGER_FALLING,
	}
};

static struct platform_device ax88796_device = {
	.name		= "ax88796",
	.id		= 0,
	.num_resources 	= ARRAY_SIZE(colibri_ax88976_resource),
	.resource	= colibri_ax88796_resource,
	.dev		= {
		.platform_data = &colibri_asix_platdata
	}
};

/*
 * Asix AX88976C resource for colibri rev >= 2.0a
 */
static struct resource colibri_ax99796c_resource[] = {
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
       .name           = "ax88796c",
       .id             = 0,
       .num_resources  = ARRAY_SIZE(colibri_ax88796c_resource),
       .resource       = colibri_ax88796c_resource,
       .dev            = {
               .platform_data = &colibri_asix_platdata
       }
};

static void __init colibri_pxa320_init_eth(void)
{
	colibri_pxa3xx_init_eth(&colibri_asix_platdata);
	pxa3xx_mfp_config(ARRAY_AND_SIZE(colibri_pxa320_eth_pin_config));
	if ( system_rev < 0x20a )
	    platform_device_register(&ax88796_device);
	else
	    platform_device_register(&ax88796c_device);
}
#else
static inline void __init colibri_pxa320_init_eth(void) {}
#endif /* CONFIG_AX88796 */

/*
 * USB Host controller
 */
#if defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
static struct pxaohci_platform_data colibri_pxa320_ohci_info = {
	.port_mode	= PMM_GLOBAL_MODE,
	.flags		= ENABLE_PORT1 | ENABLE_PORT2 | NO_OC_PROTECTION, //POWER_CONTROL_LOW | POWER_SENSE_LOW,
};

void __init colibri_pxa320_init_ohci(void)
{
//	pxa3xx_mfp_config(ARRAY_AND_SIZE(colibri_pxa320_usb_pin_config));
	pxa_set_ohci_info(&colibri_pxa320_ohci_info);
	// port2 default in host mode
	UP2OCR = UP2OCR_HXOE | UP2OCR_HXS | UP2OCR_DPPDE | UP2OCR_DMPDE;
}
#else
static inline void colibri_pxa320_init_ohci(void) {}
#endif /* CONFIG_USB_OHCI_HCD || CONFIG_USB_OHCI_HCD_MODULE */

/*
 * USB Device Controller (gadgets support)
 */
#if defined(CONFIG_USB_GADGET_PXA27X) || defined(CONFIG_USB_GADGET_PXA27X_MODULE)

static mfp_cfg_t colibri_udc_pin_config[] __initdata = {
       GPIO96_GPIO | MFP_LPM_EDGE_BOTH,
};

static struct pxa2xx_udc_mach_info colibri_udc_info;

static irqreturn_t udc_vbus_irq(int irq, void *data) {
       if ( gpio_get_value(COLI320_UDC_DETECT) ) {
           printk(KERN_INFO "USB PORT2 now device.\n");
           UP2OCR = UP2OCR_HXOE | UP2OCR_DPPUE;
       } else {
           printk(KERN_INFO "USB PORT2 now host.\n");
           UP2OCR = UP2OCR_HXOE | UP2OCR_HXS | UP2OCR_DPPDE | UP2OCR_DMPDE;
       }
       return IRQ_HANDLED;
}

static void __init colibri_pxa320_init_udc(void) {
       pxa3xx_mfp_config(ARRAY_AND_SIZE(colibri_udc_pin_config));// configure GPIO
       if ( gpio_request(COLI320_UDC_DETECT,"USB_DETECT") ) {
           printk(KERN_ERR "UDC: GPIO VBus request error");
           return;
       };
       if ( request_irq(COLI320_UDC_DETECT_IRQ,udc_vbus_irq,
                             IRQF_SAMPLE_RANDOM | IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
                             "USB VBus IRQ",NULL ) ) {
           printk(KERN_ERR "UDC: USB VBus IRQ request error");
           return;
       };
       pxa_set_udc_info( &colibri_udc_info );                    // add USB Device interface
}
#else
static inline void colibri_pxa320_init_udc(void) {}
#endif


static mfp_cfg_t colibri_pxa320_mmc_pin_config[] __initdata = {
	GPIO28_GPIO | MFP_PULL_HIGH | MFP_DS10X, // SD detect
	GPIO22_MMC1_CLK,
	GPIO23_MMC1_CMD,
	GPIO18_MMC1_DAT0,
	GPIO19_MMC1_DAT1,
	GPIO20_MMC1_DAT2,
	GPIO21_MMC1_DAT3
};

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

static inline void __init colibri_pxa320_init_ac97(void)
{
	pxa3xx_mfp_config(ARRAY_AND_SIZE(colibri_pxa320_ac97_pin_config));
	pxa_set_ac97_info(NULL);
}
#else
static inline void colibri_pxa320_init_ac97(void) {}
#endif

/*
 * The following configuration is verified to work with the Toradex Orchid
 * carrier board
 */
static mfp_cfg_t colibri_pxa320_uart_pin_config[] __initdata = {
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
};

static void __init colibri_pxa320_init_uart(void)
{
	pxa3xx_mfp_config(ARRAY_AND_SIZE(colibri_pxa320_uart_pin_config));
}

#if 1
///////////////////////
// CPLD and Registers

static mfp_cfg_t colibri_pxa320_cpld_pin_config[] __initdata = {
	// GPIO1_2_GPIO, // Moved to SPI interface
	GPIO2_RDY | MFP_PULL_HIGH | MFP_DS10X,
	GPIO3_nCS2 | MFP_PULL_HIGH | MFP_DS10X,
	GPIO4_nCS3 | MFP_PULL_HIGH | MFP_DS10X,
	GPIO7_NPIOS16 | MFP_PULL_HIGH | MFP_DS10X,
	GPIO8_NPWAIT | MFP_PULL_HIGH | MFP_DS10X,
};

static void __init colibri_pxa320_init_cpld ( void )
{

    pxa3xx_mfp_config(ARRAY_AND_SIZE(colibri_pxa320_cpld_pin_config));
    // pxa3xx_mfp_config - is a very bad idea. I thinc vis function will
    // be rewriten in future version of Linux kernel. Now I directly
    // write constanst in critical MFPR registers
    pxa3xx_mfp_write(MFP_PIN_GPIO2, 0xc2c1 );
    pxa3xx_mfp_write(MFP_PIN_GPIO3, 0x03c1);
    pxa3xx_mfp_write(MFP_PIN_GPIO4, 0x0041);
    pxa3xx_mfp_write(MFP_PIN_GPIO7, 0x82c3);
    pxa3xx_mfp_write(MFP_PIN_GPIO8, 0x82c3);
    // Tweak from Toradex (set AF7 on GPIO 56,59,60,61,62 - disable them)
    pxa3xx_mfp_write(MFP_PIN_GPIO56,0x8387);
    pxa3xx_mfp_write(MFP_PIN_GPIO59,0x83c7);
    pxa3xx_mfp_write(MFP_PIN_GPIO60,0x8387);
    pxa3xx_mfp_write(MFP_PIN_GPIO61,0x8387);
    pxa3xx_mfp_write(MFP_PIN_GPIO62,0x8387);
    
    // Initialise SMC controller (Warning!!! This code plaftorm specific!)
    // Other PXA3xx based board require selfmade SMC memory mapping!!!
    SMC_CSADRCFG2 	= 0x0032C80B;
    SMC_CSADRCFG3 	= 0x0032C809;
    SMC_CSADRCFG_P	= 0x0038080C;
    
    SMC_MSC1		= 0x000902EC;
    SMC_MECR		= 0x00000002;
    SMC_SXCNFG		= 0x00880008;
    
    SMC_MCMEM		= 0x00028389;
    SMC_MCATT		= 0x00038809;
    SMC_MCIO		= 0x00028391;
    SMC_CSMSADRCFG	= 0x00000002;
}
#else
static inline void colibri_pxa320_init_cpld ( void ) {}
#endif

/*
 * CF Card module (PCMCIA, for Linux Kernel)
 */
#if defined(CONFIG_PCMCIA_PXA32X) || defined(CONFIG_PCMCIA_PXA32X_MODULE)

static mfp_cfg_t colibri_pxa320_cf_enable_config[] = {
        GPIO5_NPIOR | MFP_PULL_HIGH | MFP_DS10X,                           // nPIOR
        GPIO6_NPIOW | MFP_PULL_HIGH | MFP_DS10X,                           // nPIOW
};

void coli32x_init_cf_bus( void ) {
	pxa3xx_mfp_config(ARRAY_AND_SIZE(colibri_pxa320_cf_enable_config));
	// pxa3xx_mfp_config is very-very bad idea (wait for realy kernel support this function)
	pxa3xx_mfp_write(MFP_PIN_GPIO5, 0x9EC3);
	pxa3xx_mfp_write(MFP_PIN_GPIO6, 0x9EC3);	
	CPLD_MEM_CTRL_REG = CF_EN | nOE_EN | RDnWR_EN;
}
EXPORT_SYMBOL(coli32x_init_cf_bus);

static mfp_cfg_t colibri_pxa320_cf_disable_config[] = {
        GPIO5_GPIO,                            // nPIOR
        GPIO6_GPIO,                            // nPIOW
};

void coli32x_exit_cf_bus( void ) {
	pxa3xx_mfp_config(ARRAY_AND_SIZE(colibri_pxa320_cf_disable_config));
	CPLD_MEM_CTRL_REG = CF_DIS | nOE_DIS | RDnWR_DIS;
}
EXPORT_SYMBOL(coli32x_exit_cf_bus);

static mfp_cfg_t colibri_pxa320_cf_config[] __initdata = {
    GPIO29_GPIO | MFP_DS10X | MFP_LPM_EDGE_FALL,// PRDY
    GPIO53_GPIO | MFP_DS10X,			// PBVD1
    GPIO57_GPIO | MFP_DS10X,			// nPPEN
    GPIO77_GPIO | MFP_DS10X,			// PRST
    GPIO79_GPIO | MFP_DS10X,			// PBVD2
    GPIO81_GPIO | MFP_DS10X | MFP_LPM_EDGE_BOTH,// PCD

    // This pins conflicted with CF interface on SODIMM and MUST be switch to input
    GPIO27_GPIO, // SODIMM 93  - RDnWR
    GPIO50_GPIO, // SODIMM 98  - CPLD nPREG
    GPIO51_GPIO, // SODIMM 101 - GPIO6_nPIOW
    GPIO52_GPIO, // SODIMM 103 - GPIO5_nPIOR
    GPIO54_GPIO, // SODIMM 97  - DF_CLE_nOE
    GPIO93_GPIO, // SODIMM 99  - DF_ALE_nWE
    GPIO122_GPIO,// SODIMM 100 - CPLD nPXCVREN
    GPIO125_GPIO,// SODIMM 85  - GPIO57 nPPEN
};

static void __init colibri_pxa320_init_cf ( void )
{
    pxa3xx_mfp_config(ARRAY_AND_SIZE(colibri_pxa320_cf_config));
    // Switch required ping to input
    // theory, this action _MUST_ be done by  pxa3xx_mfp_config,
    // but in real world... I need write to correspondents MFPR.
    pxa3xx_mfp_write(MFP_PIN_GPIO27,  0x8280); // ??? SODIMM 93 !!!
    pxa3xx_mfp_write(MFP_PIN_GPIO50,  0x8280); // ??? SODIMM 98 !!!
    pxa3xx_mfp_write(MFP_PIN_GPIO51,  0x8280); // ??? SODIMM 101 !!!
    pxa3xx_mfp_write(MFP_PIN_GPIO52,  0x8280); // ??? SODIMM 103 !!!
    pxa3xx_mfp_write(MFP_PIN_GPIO54,  0x8280); // ??? SODIMM 97 !!!
    pxa3xx_mfp_write(MFP_PIN_GPIO93,  0x82C0); // ??? SODIMM 99 !!!
    pxa3xx_mfp_write(MFP_PIN_GPIO122, 0x8280); // ??? SODIMM 100 !!! 
    pxa3xx_mfp_write(MFP_PIN_GPIO125, 0x8280); // ??? SODIMM 85 !!! 
}
#else
static inline void colibri_pxa320_init_cf ( void ) {}
#endif // defined(CONFIG_PCMCIA_PXA2XX) || defined(CONFIG_PCMCIA_PXA2XX_MODULE)

/*
 * I2C Interface
 */
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
static mfp_cfg_t colibri320_i2c_pin_config[] __initdata = {
        GPIO32_I2C_SCL,
        GPIO33_I2C_SDA,
};

static struct i2c_board_info __initdata colibri320_i2c_devices[] = {
    {    // M41T0 - RTC chip on board with battery backup
	.type   = "m41t00", // MT41T00 supported by rtc-ds1307 module
	.addr   = 0x68,
    },
    { // DS2782 - LiOn Battery monitor (not platform specific)
	.type   = "ds2782", // not present on evaluation board,
	.addr   = 0x34,     // just for sample connection.
    },
    { // DS2482 - I2C to W1 interface converter
	.type	= "ds2482",
	.addr	= 0x18,
    },
};

/*
 * Voltage regulator chip on Power-I2C interface
 */
static struct max8660_platform_data max8661_pdata = {
        .num_subdevs = 0, // Only stub at this moment
};

static struct i2c_board_info max8661_power_i2c_devices[] __initdata = {
    { // MAX8661 - Core voltage regulator on colibri rev < 2.0a
	.type = "max8661",
	.addr = 0x34,
	.platform_data = &max8661_pdata,
    },
};

static struct lp3972_platform_data lp3972_pdata = {
    .gpio[0] = LP3972_GPIO_INPUT, // on chip gpio1,2 in kernel gpio[0,1]
    .gpio[1] = LP3972_GPIO_OUTPUT_LOW, // on colibri v2 LP3972 GPIO2 used for multiplexe SoDIMM pin ...
    .num_regulators = 0, // only stub in this moment

static struct i2c_board_info lp3972_power_i2c_devices[] __initdata = {
    { // LP3972 - Core voltage regulator on colibri rev >= 2.0a
	.type = "lp3972",
	.addr = 0x34,
	.platform_data = &lp3972_pdata,
    },
};

static inline void colibri_pxa320_init_i2c(void) {
        pxa3xx_mfp_config(ARRAY_AND_SIZE(colibri320_i2c_pin_config));
        // add I2C interface and i2c devices
        pxa_set_i2c_info( NULL );
        i2c_register_board_info(0, ARRAY_AND_SIZE(colibri320_i2c_devices) );

        pxa3xx_set_i2c_power_info( NULL ); // ...and POWER_I2C as second interface
        if ( system_rev < 0x20a )
	    i2c_register_board_info(1, ARRAY_AND_SIZE(max8661_power_i2c_devices) );
	else
	    i2c_register_board_info(1, ARRAY_AND_SIZE(lp3972_power_i2c_devices) );
}
#else
static inline void colibri_pxa320_init_i2c(void) {}
#endif

/*
 * SPI bus support
 */
#if defined (CONFIG_SPI) || defined (CONFIG_SPI_MODULE)
static mfp_cfg_t colibri_pxa320_ssp_pin_config[] __initdata = {
    // SPI interface main pins
    GPIO83_SSP1_SCLK,
    GPIO85_SSP1_TXD,
    GPIO86_SSP1_RXD,
    GPIO1_2_GPIO,// For internal Xilinx CPLD
#if defined(CONFIG_P54_SPI) || defined (CONFIG_P54_SPI_MODULE)
    GPIO84_GPIO, // For Sagrad SG901-1028
    GPIO94_GPIO | MFP_LPM_EDGE_RISE, // IRQ
    GPIO95_GPIO,                     // SLEEP
#endif
};

static struct pxa2xx_spi_master colibri_pxa320_spi_master = {
    .clock_enable = CKEN_SSP1,
#if defined(CONFIG_P54_SPI) || defined (CONFIG_P54_SPI_MODULE)
    .num_chipselect = 2,
#else
    .num_chipselect = 1,
#endif
    .enable_dma = 1,
};

static struct pxa2xx_spi_chip xilinx_spi_tuning = {
    .gpio_cs	= 1,
};

#if defined(CONFIG_P54_SPI) || defined (CONFIG_P54_SPI_MODULE)
static struct pxa2xx_spi_chip sagrad_spi_tuning = {
    .gpio_cs	= 84,
};
#endif

static struct spi_board_info colibri_pxa320_spi_chips[] = {
    { // Xilinx FPGA
	.modalias 	= "xilinx-fpga",
	.max_speed_hz 	= 13000000,
	.mode		= SPI_MODE_0,
	.bus_num	= 1,
	.chip_select	= 0,
	.controller_data = &xilinx_spi_tuning,
    },
#if defined(CONFIG_P54_SPI) || defined (CONFIG_P54_SPI_MODULE)
    { // Sagrad SG901-1028 or SG901-1039 - module p54spi, but... Joke from Nokia =)
	.modalias 	= "cx3110x",
	.max_speed_hz 	= 13000000,
	.mode		= SPI_MODE_0,
	.bus_num	= 1,
	.chip_select	= 1,
	.controller_data = &sagrad_spi_tuning,
    },
#endif
};

static inline void colibri_pxa320_init_spi ( void ) {
    // Configure MFP subsystem    
    pxa3xx_mfp_config( ARRAY_AND_SIZE( colibri_pxa320_ssp_pin_config ) );

    pxa2xx_set_spi_info(1,&colibri_pxa320_spi_master);
    spi_register_board_info( ARRAY_AND_SIZE( colibri_pxa320_spi_chips ) );
}
#else /* !defined (CONFIG_SPI) || !defined (CONFIG_SPI_MODULE) */
static inline void colibri_pxa320_init_spi ( void ) {}
#endif /* defined (CONFIG_SPI) || defined (CONFIG_SPI_MODULE) */

#define RAVION_DIET
/*
 * Power management support (power off by software request)
 */
#if defined (CONFIG_PM)
static void colibri_pxa320_power_off ( void ) {
    printk(KERN_INFO "Power off you computer may be physicaly disabled\n");
#if defined RAVION_DIET
    printk(KERN_INFO "Disable module power...");
    gpio_direction_output(MFP_PIN_GPIO11,0);
    printk(KERN_INFO "[DONE]\n");
//    while(1);
#endif
    arm_machine_restart('h', NULL);
}

static void colibri_pxa320_restart ( char mode, const char* cmd ) {
    printk(KERN_INFO "Machine ready for physical restart now\n");
    arm_machine_restart('h', cmd);
}

static inline void colibri_pxa320_init_pm ( void ) {
#ifdef RAVION_DIET
    int ret;
        
    ret = gpio_request(MFP_PIN_GPIO11,"PWR_OFF");
    if ( ret ) {
	printk(KERN_INFO "=>> Error reqesting power off gpio <<=\n");
    } else {
	printk(KERN_INFO "=>> Init power pin\n");
	gpio_direction_output(MFP_PIN_GPIO11, 1); // set to active power
    };	
#endif     
    pm_power_off        = colibri_pxa320_power_off;
    arm_pm_restart      = colibri_pxa320_restart;
}
#else
static inline void colibri_pxa320_init_pm ( void ) {}
#endif /* CONFIG_PM */

/*
 * GPIO Keyboard for KEY_POWER from power module
 */
#if defined (CONFIG_KEYBOARD_GPIO)
static struct gpio_keys_button gpio_keys_button[] = {
        [0] = {
                .desc   = "Power",
                .code   = KEY_POWER,
                .type   = EV_KEY,
                .active_low = 1,
                .debounce_interval = 100, // mSec
                .gpio   = 13,
                .wakeup = 1,
        },
/* Used for specific tasks
        [1] = {
                .desc   = "Ext. TNG",
                .code   = KEY_PROG1,
                .type   = EV_KEY,
                .active_low = 1,
                .debounce_interval = 100, // mSec
                .gpio   = 73, // CRYPT_REQ, SoDIMM200 pin 105
                .wakeup = 1,
        },
*/
};

static struct gpio_keys_platform_data colibri_pxa320_gpio_keys = {
        .buttons        = gpio_keys_button,
        .nbuttons       = 2,
};

static struct platform_device colibri_pxa320_gpio_keys_device = {
        .name           = "gpio-keys",
        .id             = -1,
        .dev            = {
                .platform_data  = &colibri_pxa320_gpio_keys,
        },
};

static struct platform_device *colibri_pxa320_keyboard_devices[] __initdata = {
        &colibri_pxa320_gpio_keys_device,
};

static inline void colibri_pxa320_init_keyboard_gpio( void ) {
    platform_add_devices( ARRAY_AND_SIZE(colibri_pxa320_keyboard_devices) );
}
#else
static inline void colibri_pxa320_init_keyboard_gpio( void ) {}
#endif /* CONFIG_KEYBOARD_GPIO */ 
#undef RAVION_DIET

void __init colibri_pxa320_init(void)
{
	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);

	colibri_pxa320_init_eth();
	colibri_pxa320_init_ohci();
	colibri_pxa320_init_udc();
	colibri_pxa3xx_init_nand();
	colibri_pxa320_init_lcd();
	colibri_pxa3xx_init_lcd(mfp_to_gpio(GPIO49_GPIO));
	colibri_pxa320_init_ac97();
	colibri_pxa3xx_init_mmc(ARRAY_AND_SIZE(colibri_pxa320_mmc_pin_config),
				mfp_to_gpio(MFP_PIN_GPIO28));
	colibri_pxa320_init_uart();
	colibri_pxa320_init_cpld();
	colibri_pxa320_init_cf();
	colibri_pxa320_init_i2c();
	colibri_pxa320_init_spi();
	colibri_pxa320_init_pm();
	colibri_pxa320_init_keyboard_gpio();
	colibri_pxa320_init_pda_power();
}

/*
 * IO Mapping for acces to CPLD
 */

static struct map_desc colibri_pxa320_io_desc[] __initdata = {
    { // EXT_nCS0
        .virtual        = CPLD_EXT_nCS0_VIRT,
        .pfn            = __phys_to_pfn (CPLD_EXT_nCS0_PHYS),
        .length         = CPLD_EXT_nCS0_SIZE,
        .type           = MT_DEVICE,
    },
    { // EXT_nCS1
        .virtual        = CPLD_EXT_nCS1_VIRT,
        .pfn            = __phys_to_pfn (CPLD_EXT_nCS1_PHYS),
        .length         = CPLD_EXT_nCS1_SIZE,
        .type           = MT_DEVICE,
    },
    { // EXT_nCS2
        .virtual        = CPLD_EXT_nCS2_VIRT,
        .pfn            = __phys_to_pfn (CPLD_EXT_nCS2_PHYS),
        .length         = CPLD_EXT_nCS2_SIZE,
        .type           = MT_DEVICE,
    },
    { // CPLD Register block
        .virtual        = CPLD_REGS_VIRT,
        .pfn            = __phys_to_pfn (CPLD_REGS_PHYS),
        .length         = CPLD_REGS_SIZE,
        .type           = MT_DEVICE,
    }, 
    { // Static memory controller
	.virtual	= SMC_VIRT,
	.pfn		= __phys_to_pfn (SMC_PHYS),
	.length		= SMC_SIZE,
	.type		= MT_DEVICE,
    },
};


static void __init colibri_pxa320_map_io( void )
{
    pxa_map_io();
    iotable_init(colibri_pxa320_io_desc, ARRAY_SIZE(colibri_pxa320_io_desc));
}

MACHINE_START(COLIBRI320, "Toradex Colibri PXA320")
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.boot_params	= COLIBRI_SDRAM_BASE + 0x100,
	.init_machine	= colibri_pxa320_init,
	.map_io		= colibri_pxa320_map_io,
	.init_irq	= pxa3xx_init_irq,
	.timer		= &pxa_timer,
MACHINE_END

