/*
 *  linux/arch/arm/mach-pxa/colibri-pxa270.c
 *
 *  Support for Toradex PXA270 based Colibri module
 *  Daniel Mack <daniel@caiaq.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/sysdev.h>
#include <linux/interrupt.h>
#include <linux/bitops.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/gpio.h>
#include <linux/pm.h>
#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/sizes.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/mach/flash.h>

#include <mach/pxa27x.h>
#include <mach/colibri.h>

#include <mach/ohci.h>
#include <mach/pxa27x-udc.h>
#include <mach/udc.h>
#include <mach/audio.h>
#include <mach/mmc.h>
#include <plat/i2c.h>
#include <mach/pxafb.h>

#include <linux/regulator/max8660.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/consumer.h>

#include <linux/spi/spi.h>
#include <mach/pxa2xx_spi.h>

#include "generic.h"
#include "devices.h"

/*
 * GPIO configuration
 */
static mfp_cfg_t colibri_pxa270_pin_config[] __initdata = {
        /* Full-feature UART */
        GPIO10_FFUART_DCD,
        GPIO27_FFUART_RTS,
        GPIO33_FFUART_DSR,
        GPIO34_FFUART_RXD,
        GPIO38_FFUART_RI,
        GPIO39_FFUART_TXD,
        GPIO40_FFUART_DTR,
        GPIO100_FFUART_CTS,

        /* Bluetooth UART */
        GPIO42_BTUART_RXD,
        GPIO43_BTUART_TXD,
        GPIO45_BTUART_RTS,

        /* Standart UART */
        GPIO46_STUART_RXD,
        GPIO47_STUART_TXD,

        /* System bus */
        GPIO18_RDY,
        // External CSs>// nEXT_CS0 - allways +3,3V
        GPIO15_nCS_1,   // nCAN_CS
        GPIO79_nCS_3,   // nEXT_CS1
        GPIO80_nCS_4,   // nEXT_CS2

        /* Touchscreen */
        GPIO113_GPIO,   /* Touchscreen IRQ */

	GPIO78_nCS_2,	/* Ethernet CS */
	GPIO114_GPIO,	/* Ethernet IRQ */
};

/*
 * NOR flash
 */
static struct mtd_partition colibri_partitions[] = {
        {
                .name =         "Bootloader",
                .offset =       0x00000000,
                .size =         0x00040000,
                .mask_flags =   MTD_WRITEABLE  /* force read-only    */
        }, {
                .name =         "Enverooment",
                .offset =       0x00040000,
                .size =         0x00040000,
                .mask_flags =   MTD_WRITEABLE  /* force read-only    */
        }, {
                .name =         "Kernel",
                .offset =       0x00080000,
                .size =         0x00600000,
                .mask_flags =   0
        }, {
                .name =         "Rootfs",
                .offset =       0x00680000,
                .size =         MTDPART_SIZ_FULL,
                .mask_flags =   0
        }
};

static struct physmap_flash_data colibri_flash_data[] = {
        {
                .width          = 4,                    /* bankwidth in bytes */
                .parts          = colibri_partitions,
                .nr_parts       = ARRAY_SIZE(colibri_partitions)
        }
};

static struct resource colibri_pxa270_flash_resource = {
        .start  = PXA_CS0_PHYS,
        .end    = PXA_CS0_PHYS + SZ_32M - 1,
        .flags  = IORESOURCE_MEM,
};

static struct platform_device colibri_pxa270_flash_device = {
        .name   = "physmap-flash",
        .id     = 0,
        .dev    = {
                .platform_data = colibri_flash_data,
        },
        .resource = &colibri_pxa270_flash_resource,
        .num_resources = 1,
};

/*
 * DM9000 Ethernet
 */
#if defined(CONFIG_DM9000) || defined (CONFIG_DM9000_MODULE)
static struct resource dm9000_resources[] = {
	[0] = {
		.start	= COLIBRI_PXA270_ETH_PHYS,
		.end	= COLIBRI_PXA270_ETH_PHYS + 3,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= COLIBRI_PXA270_ETH_PHYS + 4,
		.end	= COLIBRI_PXA270_ETH_PHYS + 4 + 500,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start	= COLIBRI_PXA270_ETH_IRQ,
		.end	= COLIBRI_PXA270_ETH_IRQ,
		.flags	= IORESOURCE_IRQ | IRQF_TRIGGER_RISING,
	},
};

static struct platform_device dm9000_device = {
	.name		= "dm9000",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(dm9000_resources),
	.resource	= dm9000_resources,
};
#endif /* CONFIG_DM9000 */

/*
 * USB OHCI Host Controller
 */
#if defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
static mfp_cfg_t colibri_usb_pin_config[] __initdata = {
        GPIO88_GPIO, // O - usb hosts powered, 1 - unpowered
        GPIO89_GPIO | MFP_LPM_EDGE_BOTH, // Overcurrent detection
};
static struct pxaohci_platform_data colibri_ohci_info = {
        .port_mode      = PMM_NPS_MODE,
        .flags          = ENABLE_PORT_ALL | NO_OC_PROTECTION,
};
static irqreturn_t usb_overcurrent_irq(int irq, void *data)
{
        if ( gpio_get_value(COLI27X_USB_OC) ) {
                printk(KERN_ERR "Power up USB hosts.\n");
                gpio_direction_output(COLI27X_USB_VBUS,0);
        } else {
                printk(KERN_ERR "USB down USB hosts, becource overcurrent detected!\n");
                gpio_direction_output(COLI27X_USB_VBUS,1);
        }
        return IRQ_HANDLED;
}
static void __init colibri_init_ohci(void)
{
        pxa2xx_mfp_config(ARRAY_AND_SIZE(colibri_usb_pin_config));      /* configure GPIO               */
        if ( gpio_request(COLI27X_USB_OC,"USB POWER OK") ) {
            printk(KERN_ERR "USB: GPIO power detection request error");
            return;
        }
        if ( gpio_request(COLI27X_USB_VBUS,"USB POWER DISABLED") ) {
            printk(KERN_ERR "UDC: GPIO power manager request error");
            return;
        }
        if ( request_irq(COLI27X_USB_OC_IRQ,usb_overcurrent_irq,
                              IRQF_SAMPLE_RANDOM | IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
                              "USB Overcurrent IRQ",NULL ) ) {
            printk(KERN_ERR "UDC: USB Overcurrent IRQ request error");
            return;
        };
        gpio_direction_output(COLI27X_USB_VBUS,0);                      /* Power UP USB Hosts           */
        pxa_set_ohci_info(&colibri_ohci_info);                          /* add USB Host interface       */
        UP2OCR = UP2OCR_HXS | UP2OCR_HXOE | UP2OCR_DPPDE | UP2OCR_DMPDE;/* add PORT2 Host support       */}
#else
static inline void colibri_init_ohci(void) {}
#endif /* CONFIG_USB_OHCI_HCD || CONFIG_USB_OHCI_HCD_MODULE */

/*
 * USB Device Controller (gadgets support)
 */
#if defined(CONFIG_USB_GADGET_PXA27X) || defined(CONFIG_USB_GADGET_PXA27X_MODULE)

static mfp_cfg_t colibri_udc_pin_config[] __initdata = {
       GPIO41_GPIO | MFP_LPM_EDGE_BOTH,
};

static struct pxa2xx_udc_mach_info colibri_udc_info;

static irqreturn_t udc_vbus_irq(int irq, void *data) {
       if ( gpio_get_value(COLI27X_UDC_DETECT) ) {
           printk(KERN_INFO "USB PORT2 now device.\n");
           UP2OCR = UP2OCR_HXOE | UP2OCR_DPPUE;
       } else {
           printk(KERN_INFO "USB PORT2 now host.\n");
           UP2OCR = UP2OCR_HXOE | UP2OCR_HXS | UP2OCR_DPPDE | UP2OCR_DMPDE;
       } 
       return IRQ_HANDLED;
}

static void __init colibri_init_udc(void) {
       pxa2xx_mfp_config(ARRAY_AND_SIZE(colibri_udc_pin_config));// configure GPIO
       if ( gpio_request(COLI27X_UDC_DETECT,"USB_VBus") ) {
           printk(KERN_ERR "UDC: GPIO VBus request error");
           return;
       };
       if ( request_irq(COLI27X_UDC_DETECT_IRQ,udc_vbus_irq,
                             IRQF_SAMPLE_RANDOM | IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
                             "USB VBus IRQ",NULL ) ) {
           printk(KERN_ERR "UDC: USB VBus IRQ request error");
           return;
       };
       pxa_set_udc_info( &colibri_udc_info );                    // add USB Device interface
}
#else    
static inline void colibri_init_udc(void) {}
#endif  

/*
 * AC97 Audio
 */
#if defined(CONFIG_SND_PXA2XX_AC97) || defined(CONFIG_SND_PXA2XX_AC97_MODULE)
static mfp_cfg_t colibri_ac97_pin_config[] __initdata = {
        GPIO28_AC97_BITCLK,
        GPIO29_AC97_SDATA_IN_0,
        GPIO30_AC97_SDATA_OUT,
        GPIO31_AC97_SYNC,
        GPIO95_AC97_nRESET,
        GPIO98_AC97_SYSCLK,
}; 

static pxa2xx_audio_ops_t colibri_ac97_pdata = {
        .reset_gpio     = 95,
}; 

static inline void colibri_init_sound(void) {
        pxa2xx_mfp_config(ARRAY_AND_SIZE(colibri_ac97_pin_config)); /* GPIO     */
        pxa_set_ac97_info( &colibri_ac97_pdata );              /* add AC-97 Audio codec        */
}  
#else
static inline void colibri_init_sound(void) {}
#endif //defined(CONFIG_SND_PXA2XX_AC97) || defined(CONFIG_SND_PXA2XX_AC97_MODULE)

/*
 * SecureDigital / Multimedia Card controller
 */
#if defined(CONFIG_MMC_PXA) || defined(CONFIG_MMC_PXA_MODULE)

static mfp_cfg_t colibri_mmc_pin_config[] __initdata = {
        GPIO0_GPIO, // GPIO0 - Detect Both (Fall & Rise) IRQ - card detect
        GPIO32_MMC_CLK,
        GPIO92_MMC_DAT_0,
        GPIO109_MMC_DAT_1,
        GPIO110_MMC_DAT_2,
        GPIO111_MMC_DAT_3,
        GPIO112_MMC_CMD,
};

static int colibri_mci_init(struct device *dev, irq_handler_t colibri_detect_int,
                                void *data)
{
        int err = 0;
        /* Setup an interrupt for detecting card insert/remove events */
        err = gpio_request(GPIO_NR_COLI27X_SD_DETECT_N, "SD IRQ");
        if (err)
                goto err;
        err = gpio_direction_input(GPIO_NR_COLI27X_SD_DETECT_N);
        if (err)
                goto err2;
        err = request_irq(gpio_to_irq(GPIO_NR_COLI27X_SD_DETECT_N),
                        colibri_detect_int,
                        IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
                        "SD/MMC card detect", data);
        if (err) {
                printk(KERN_ERR "%s: cannot request SD/MMC card detect IRQ\n",
                                __func__);
                goto err2;
        }

        printk(KERN_DEBUG "%s: irq registered\n", __func__);

        return 0;

err2:
        gpio_free(GPIO_NR_COLI27X_SD_DETECT_N);
err:
        return err;
}

static void colibri_mci_exit(struct device *dev, void *data)
{
        free_irq(gpio_to_irq(GPIO_NR_COLI27X_SD_DETECT_N), data);
        gpio_free(GPIO_NR_COLI27X_SD_DETECT_N);
}

static struct pxamci_platform_data colibri_mci_platform_data = {
        .ocr_mask       = MMC_VDD_32_33 | MMC_VDD_33_34,
        .detect_delay_ms= 200,
        .init           = colibri_mci_init,
        .exit           = colibri_mci_exit,
        .gpio_card_detect       = -1,
        .gpio_card_ro           = -1,
        .gpio_power             = -1,
};

static void __init colibri_init_mmc(void)
{
        pxa2xx_mfp_config(ARRAY_AND_SIZE(colibri_mmc_pin_config));
        pxa_set_mci_info(&colibri_mci_platform_data);
}
#else
static inline void colibri_init_mmc(void) {}
#endif

/*
 * PCCard module (PCMCIA, if your know about this)
 */
#if defined(CONFIG_PCMCIA_PXA2XX) || defined(CONFIG_PCMCIA_PXA2XX_MODULE)
// Set AFTER card detected in slot for working with cards
static mfp_cfg_t colibri_pccards_bus_config[] = {
        GPIO48_nPOE,                            // nPOE
        GPIO49_nPWE,                            // nPWE
        GPIO50_nPIOR,                           // nPIOR
        GPIO51_nPIOW,                           // nPIOW
        GPIO54_nPCE_2,                          // nPCE2
        GPIO55_nPREG,                           // nPREG
        GPIO56_nPWAIT,                          // nPWAIT
        GPIO57_nIOIS16,                         // nIOIS16
        GPIO85_nPCE_1,                          // nPCE1
        GPIO104_PSKTSEL,                        // PSKTSEL
};
// Set after cards removed - disable PCMCIA bus
static mfp_cfg_t colibri_pccards_gpio_config[] = {
        GPIO48_GPIO,                            // nPOE
// Thank to:
//       Per Larsson, BitSim AB 
// GPIO49_nPWE may be used as GPIO only, if no VLIO devices present.
// See (Note 4 on Colibri PXA270 datasheet. Page 19). If DM9000
// supported in kernel _NOT_ switch them back to GPIO
#if defined(CONFIG_DM9000) || defined (CONFIG_DM9000_MODULE)
	GPIO49_nPWE,				// nPWE used by VLIO (ethernet controller)
#else                                           
        GPIO49_GPIO,                            // No VLIO used - switch back to GPIO
#endif        
        GPIO50_GPIO,                            // nPIOR
        GPIO51_GPIO,                            // nPIOW
        GPIO54_GPIO,                            // nPCE2
        GPIO55_GPIO,                            // nPREG
        GPIO56_GPIO,                            // nPWAIT
        GPIO57_GPIO,                            // nIOIS16
        GPIO85_GPIO,                            // nPCE1
        GPIO104_GPIO,                           // PSKTSEL
};

// Set on intialization phase for detect and configure cards
static mfp_cfg_t colibri_pccards_altf_config[] = {
        GPIO1_GPIO | MFP_LPM_EDGE_FALL,         // PRDY - slot ready (falling IRQ)
        GPIO53_GPIO,                            // PRST - reset card (init in module)
        GPIO82_GPIO,                            // PBVD2
        GPIO83_GPIO,                            // PBVD1
        GPIO84_GPIO | MFP_LPM_EDGE_BOTH,        // PCD - change detected???
        GPIO107_GPIO,                           // nPPEN - power enable for PCCards/CF (init in module) */
};

void coli27x_init_cf_bus( void ) {
    pxa2xx_mfp_config(ARRAY_AND_SIZE(colibri_pccards_bus_config));
}
EXPORT_SYMBOL(coli27x_init_cf_bus);

void coli27x_exit_cf_bus( void ) {
    pxa2xx_mfp_config(ARRAY_AND_SIZE(colibri_pccards_gpio_config));
}
EXPORT_SYMBOL(coli27x_exit_cf_bus);

static inline void colibri_init_pccards(void) {
    pxa2xx_mfp_config(ARRAY_AND_SIZE(colibri_pccards_altf_config)); /* GPIO     */
}
#else
static inline void colibri_init_pccards(void) {}
#endif

/*
 * I2C Interface
 */
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
static mfp_cfg_t colibri_i2c_pin_config[] __initdata = {
        GPIO117_I2C_SCL,
        GPIO118_I2C_SDA,
};

/*
 * User specific I2C devices
 */
static struct i2c_board_info __initdata colibri_i2c_devices[] = {
#if 1
    {    // M41T0 - RTC chip on board with battery backup
        .type   = "m41t00", // MT41T00 supported by rtc-ds1307 module
        .addr   = 0x68,
    },
#else     
    { // DS2782 - LiOn Battery monitor (not platform specific)
        .type   = "ds2782",
        .addr   = 0x68, // realy 0x68, but... conflicted with RTC
    },
#endif    
};
  
/*
 * Voltage regulator chip on Power-I2C interface
 */
static struct i2c_board_info colibri_power_i2c_devices[] __initdata = {
  // Here Must be code for Ti TPS65020 (may be Ti TPS65023 supported?)
};

static inline void colibri_init_i2c(void) {
        pxa2xx_mfp_config(ARRAY_AND_SIZE(colibri_i2c_pin_config)); /* GPIO      */
        // add I2C interface and i2c devices
        pxa_set_i2c_info( NULL );
        i2c_register_board_info(0, ARRAY_AND_SIZE(colibri_i2c_devices) );

        pxa27x_set_i2c_power_info( NULL ); // ...and POWER_I2C as second interface
        i2c_register_board_info(1, ARRAY_AND_SIZE(colibri_power_i2c_devices) ); // for POWER i2c devices
}
#else
static inline void colibri_init_i2c(void) {}
#endif

/*
 * PXA FrameBuffer Video System 
*/
#if defined(CONFIG_FB_PXA) || defined (CONFIG_FB_PXA_MODULE)
static mfp_cfg_t colibri_video_pin_config[] __initdata = {
        GPIO58_LCD_LDD_0,
        GPIO59_LCD_LDD_1,
        GPIO60_LCD_LDD_2,
        GPIO61_LCD_LDD_3,
        GPIO62_LCD_LDD_4,
        GPIO63_LCD_LDD_5,
        GPIO64_LCD_LDD_6,
        GPIO65_LCD_LDD_7,
        GPIO66_LCD_LDD_8,
        GPIO67_LCD_LDD_9,
        GPIO68_LCD_LDD_10,
        GPIO69_LCD_LDD_11,
        GPIO70_LCD_LDD_12,
        GPIO71_LCD_LDD_13,
        GPIO72_LCD_LDD_14,
        GPIO73_LCD_LDD_15,
        GPIO86_LCD_LDD_16,
        GPIO87_LCD_LDD_17,
        GPIO74_LCD_FCLK,
        GPIO75_LCD_LCLK,
        GPIO76_LCD_PCLK,
        GPIO77_LCD_BIAS,
//      GPIO14_LCD_VSYNC,                       // IRQ for vertical Sync - not used!!! Pin in-use in ps/2 controller
        GPIO19_LCD_CS,
#ifdef COLIBRI_USE_VGA_MODULATOR
        GPIO81_GPIO,                            // VGA modulator CS for evaluation board
#endif /* COLIBRI_USE_VGA_MODULATOR */
};
#if defined(COLIBRI_USE_VGA_MODULATOR)
/*
 * Use monitor-compatable video modes
 */
static struct pxafb_mode_info vga_compatable_modes[] = {
        [0] = {
                 // VGA 640x480 @ 60Hz VGA
                .xres           = 640,
                .yres           = 480,
                .bpp            = 24,
                .depth          = 18,
                .pixclock       = 25170,
                .left_margin    = 16,
                .right_margin   = 48,
                .upper_margin   = 11,
                .lower_margin   = 31,
                .hsync_len      = 96,
                .vsync_len      = 2,
                .sync           = 0, // H-, V-
        },
        [1] = {
                 // VGA 640x480 @ 72Hz VGA
                .xres           = 640,
                .yres           = 480,
                .bpp            = 24,
                .depth          = 18,
                .pixclock       = 31500,
                .left_margin    = 24,
                .right_margin   = 128,
                .upper_margin   = 9,
                .lower_margin   = 28,
                .hsync_len      = 40,
                .vsync_len      = 3,
                .sync           = 0, // H-, V-
        },
        [2] = {
                 // VGA 640x480 @ 75Hz VESA
                .xres           = 640,
                .yres           = 480,
                .bpp            = 24,
                .depth          = 18,
                .pixclock       = 31500,
                .left_margin    = 16,
                .right_margin   = 48,
                .upper_margin   = 11,
                .lower_margin   = 32,
                .hsync_len      = 96,
                .vsync_len      = 2,
                .sync           = 0, // H-, V-
        },
        [3] = {
                 // VGA 640x480 @ 85Hz VESA
                .xres           = 640,
                .yres           = 480,
                .bpp            = 24,
                .depth          = 18,
                .pixclock       = 36000,
                .left_margin    = 32,
                .right_margin   = 112,
                .upper_margin   = 1,
                .lower_margin   = 25,
                .hsync_len      = 112,
                .vsync_len      = 25,
                .sync           = 0, // H-, V-
        },
};

static struct pxafb_mach_info colibri_vid_mode_info = {
        .modes                  = vga_compatable_modes,
        .num_modes              = 4,
        .lcd_conn               = LCD_COLOR_TFT_18BPP,// | LCD_PCLK_EDGE_FALL,
};
#else /* !COLIBRI_USE_VGA_MODULATOR */
/*
 * Sharp LS037V7DWO1 
 */
static struct pxafb_mode_info sharp_ls037_modes[] = {
        [0] = {
                .pixclock       = 39700,
                .xres           = 480,
                .yres           = 640,
                .bpp            = 24,
                .depth          = 18,
                .hsync_len      = 8,
                .left_margin    = 81,
                .right_margin   = 81,
                .vsync_len      = 1,
                .upper_margin   = 2,
                .lower_margin   = 7,
                .sync           = 0,
        },
        [1] = {
                .pixclock       = 158000,
                .xres           = 240,
                .yres           = 320,
                .bpp            = 24,
                .depth          = 18,
                .hsync_len      = 4,
                .left_margin    = 39,
                .right_margin   = 39,
                .vsync_len      = 1,
                .upper_margin   = 2,
                .lower_margin   = 3,
                .sync           = 0,
        },
};

static struct pxafb_mach_info colibri_vid_mode_info = {
        .modes                  = sharp_ls037_modes,
        .num_modes              = 2,
        .lcd_conn               = LCD_COLOR_TFT_18BPP, // | LCD_PCLK_EDGE_FALL,
};
#endif /* COLIBRI_USE_VGA_MODULATOR */

static inline void colibri_init_fbdev( void ) {
        pxa2xx_mfp_config(ARRAY_AND_SIZE(colibri_video_pin_config));    /* configure GPIO               */
#ifdef COLIBRI_USE_VGA_MODULATOR
        /* Enable CS on VGA modulator */
        if ( gpio_request( 81, "VGA Modulator" ) )
            printk(KERN_INFO "Error requesting VGA modulator GPIO pin\n");
        else
            gpio_direction_output( 81, 1 );
#endif /* COLIBRI_USE_VGA_MODULATOR */
        set_pxa_fb_info(&colibri_vid_mode_info);   /* add FB device to platform    */
}
#else /* undefined(CONFIG_FB_PXA) || undefined (CONFIG_FB_PXA_MODULE) */
static inline void colibri_init_fbdev(void) {}
#endif /* defined(CONFIG_FB_PXA) || defined (CONFIG_FB_PXA_MODULE) */

static struct platform_device *colibri_pxa270_devices[] __initdata = {
	&colibri_pxa270_flash_device,
#if defined(CONFIG_DM9000)
	&dm9000_device,
#endif
};

/*
 * SPI bus support
 */
#if defined (CONFIG_SPI) || defined (CONFIG_SPI_MODULE)
static mfp_cfg_t colibri_ssp_pin_config[] __initdata = {
    GPIO23_SSP1_SCLK,
    GPIO24_SSP1_SFRM,
    GPIO25_SSP1_TXD,
    GPIO26_SSP1_RXD,
};
 
static struct pxa2xx_spi_master colibri_spi_master = {
    .clock_enable = CKEN_SSP1,
    .num_chipselect = 1,
    .enable_dma = 1,
};

struct platform_device colibri_spi_ssp1 = {
        .name          = "pxa2xx-spi",
        .id            = 1,
        .dev           = {
                .platform_data = &colibri_spi_master,
        }
};

static struct platform_device *colibri_spi_devices[] __initdata = {
        &colibri_spi_ssp1,
};
 
#if defined(CONFIG_P54_SPI) || defined (CONFIG_P54_SPI_MODULE)
static struct pxa2xx_spi_chip pxa270_spi_tuning = {
    .tx_threshold       = 8,
    .rx_threshold       = 128,
    .dma_burst_size     = 8,
    .timeout            = 235,
    //enable_loopback   = 0,
    //gpio_cs           = ,
    //void (*cs_control)(u32 command);
};

static struct spi_board_info colibri_sagrad_wifi_module[] __initdata = {
    { // Sagrad SG901-1028 or SG901-1039
        .modalias       = "p54spi",
        .max_speed_hz   = 13000000,
        .bus_num        = 1,
        .chip_select    = 0,
        .controller_data = &pxa270_spi_tuning,
    },
};
#endif /* defined(CONFIG_P54_SPI) || defined (CONFIG_P54_SPI_MODULE) */

static inline void colibri_init_spi ( void ) {
    pxa2xx_mfp_config(ARRAY_AND_SIZE(colibri_ssp_pin_config));

    platform_add_devices( ARRAY_AND_SIZE(colibri_spi_devices) );
#if defined(CONFIG_P54_SPI) || defined (CONFIG_P54_SPI_MODULE)
    spi_register_board_info(ARRAY_AND_SIZE(colibri_sagrad_wifi_module));
#endif /* defined(CONFIG_P54_SPI) || defined (CONFIG_P54_SPI_MODULE) */
}
#else /* !defined (CONFIG_SPI) || !defined (CONFIG_SPI_MODULE) */
static inline void colibri_init_spi ( void ) {}
#endif /* defined (CONFIG_SPI) || defined (CONFIG_SPI_MODULE) */


#if defined (CONFIG_PM)
static void colibri_pxa270_power_off ( void ) {
    printk(KERN_INFO "Power off you computer may be physicaly disabled\n");
    /* place power off code here */
    arm_machine_restart('h', NULL);
}

static void colibri_pxa270_restart ( char mode, const char* cmd ) {
    printk(KERN_INFO "Machine ready for physical restart now\n");
    arm_machine_restart('h', cmd);
}

static inline void colibri_init_pm ( void ) {
    pm_power_off	= colibri_pxa270_power_off;
    arm_pm_restart	= colibri_pxa270_restart;
}
#else
static inline void colibri_init_pm ( void ) {} 
#endif /* CONFIG_PM */


static void __init colibri_pxa270_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(colibri_pxa270_pin_config));
	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);
	platform_add_devices(ARRAY_AND_SIZE(colibri_pxa270_devices));

        colibri_init_i2c();                     /* add i2c interface            */
        colibri_init_ohci();                    /* add USB OHCI Host controller */
        colibri_init_udc();                     /* USB Device (gadget) interface*/
        colibri_init_sound();                   /* add AC97 sound support       */
        colibri_init_pccards();                 /* add PCCARDS interface        */
        colibri_init_mmc();                     /* add SD/MMC host controller   */
        colibri_init_fbdev();                   /* add Video/TFT output         */
        colibri_init_spi();			/* add SPI bus support 		*/
        colibri_init_pm();			/* add PowerManager support     */
}

MACHINE_START(COLIBRI, "Toradex Colibri PXA270")
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.boot_params	= COLIBRI_SDRAM_BASE + 0x100,
	.init_machine	= colibri_pxa270_init,
	.map_io		= pxa_map_io,
	.init_irq	= pxa27x_init_irq,
	.timer		= &pxa_timer,
MACHINE_END

