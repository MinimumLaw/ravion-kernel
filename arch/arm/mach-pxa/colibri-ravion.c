/*
 *  linux/arch/arm/mach-pxa/colibri-evalboard.c
 *
 *  Support for Toradex Colibri Evaluation Carrier Board
 *  Daniel Mack <daniel@caiaq.de>
 *  Marek Vasut <marek.vasut@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <asm/mach/arch.h>
#include <linux/i2c.h>
#include <linux/i2c/pxa-i2c.h>

#include <mach/pxa320.h>
#include <mach/colibri.h>
#include <linux/platform_data/mmc-pxamci.h>
#include <linux/platform_data/usb-ohci-pxa27x.h>
#include <mach/pxa27x-udc.h>

#include <linux/spi/spi.h>
#include <linux/spi/pxa2xx_spi.h>

#include <linux/input.h>
#include <linux/gpio_keys.h>

#include <linux/pda_power.h>
#include <linux/power_supply.h>

#include "generic.h"
#include "devices.h"

#if defined( CONFIG_MACH_COLIBRI_EVALBOARD )
#error Only one carrier board may be suported (EVALBOARD or RAVIONBOARD)
#endif

/******************************************************************************
 * GPIO configuration
 ******************************************************************************/
static mfp_cfg_t colibri_pxa320_ravionboard_pin_config[] __initdata = {
        /* MMC */
        GPIO22_MMC1_CLK | MFP_DS13X,
        GPIO23_MMC1_CMD | MFP_DS13X,
        GPIO18_MMC1_DAT0| MFP_DS13X,
        GPIO19_MMC1_DAT1| MFP_DS13X,
        GPIO20_MMC1_DAT2| MFP_DS13X,
        GPIO21_MMC1_DAT3| MFP_DS13X,
        GPIO28_GPIO	| MFP_DS08X,    /* SD detect */

        /* UART 1 configuration (may be set by bootloader) */
        GPIO97_UART1_RXD,
        GPIO98_UART1_TXD,

        /* UART 2 configuration */
        GPIO110_UART2_RXD,
        GPIO111_UART2_TXD,

        /* UART 3 configuration */
        GPIO30_UART3_RXD,
        GPIO31_UART3_TXD,

        /* UHC
        GPIO2_2_USBH_PEN,
        GPIO3_2_USBH_PWR, */

        /* I2C */
        GPIO32_I2C_SCL,
        GPIO33_I2C_SDA,

        /* PCMCIA */
        MFP_CFG(GPIO59, AF7),   /* SoDIMM75 CIF_MCLK  MUX GPIO77        */
        MFP_CFG(GPIO61, AF7),   /* SoDIMM94 CIF_HSYNC MUX CPLD nPCE1    */
        MFP_CFG(GPIO60, AF7),   /* SoDIMM96 CIF_PCLK  MUX CPLD nPCE2    */
        MFP_CFG(GPIO62, AF7),   /* SoDIMM81 CIF_VSYNC MUX GPIO81        */
        MFP_CFG(GPIO56, AF7),   /* SoDIMM59 CIF_DD7   MUX GPIO14 ???    */
        MFP_CFG(GPIO27, AF7),   /* SoDIMM93 GPIO27    MUX CPLD RDnWR    */
        MFP_CFG(GPIO50, AF7),   /* SoDIMM98 GPIO50    MUX CPLD nPREG    */
        MFP_CFG(GPIO51, AF7),   /* SoDIMM101 GPIO51   MUX GPIO6_nPIOW   */
        MFP_CFG(GPIO52, AF7),   /* SoDIMM103 GPIO52   MUX GPIO5_nPIOR   */
        MFP_CFG(GPIO54, AF7),   /* SoDIMM97 GPIO54    MUX CPLD DF_CLE_nOE */
        MFP_CFG(GPIO93, AF7),   /* SoDIMM99 GPIO93    MUX CPLD DF_ALE_nWE */
        MFP_CFG(GPIO122, AF7),  /* SoDIMM100 GPIO122  MUX CPLD nPXCVREN */
        MFP_CFG(GPIO125, AF7),  /* SoDIMM85  GPIO125  MUX GPIO57 nPPEN  */
        GPIO4_nCS3,             /* CPLD and ext. CSs */
        GPIO5_NPIOR,
        GPIO6_NPIOW,
        GPIO7_NPIOS16,
        GPIO8_NPWAIT,
        GPIO29_GPIO | MFP_LPM_EDGE_FALL,        /* PRDY  (READY GPIO) */
        GPIO57_GPIO,                            /* nPPEN (POWER GPIO) */
        GPIO81_GPIO | MFP_LPM_EDGE_BOTH,        /* PCD  (DETECT GPIO) */
        GPIO77_GPIO,                            /* PRST  (RESET GPIO) */
        GPIO53_GPIO,                            /* PBVD1 */
        GPIO79_GPIO,                            /* PBVD2 */

};

/******************************************************************************
 * SD/MMC card controller
 ******************************************************************************/
#if defined(CONFIG_MMC_PXA) || defined(CONFIG_MMC_PXA_MODULE)
static struct pxamci_platform_data colibri_mci_platform_data = {
	.ocr_mask		= MMC_VDD_32_33 | MMC_VDD_33_34,
	.gpio_power		= -1,
	.gpio_card_ro		= -1,
	.detect_delay_ms	= 200,
};

static void __init colibri_mmc_init(void)
{
	colibri_mci_platform_data.gpio_card_detect =
	    GPIO28_COLIBRI_PXA320_SD_DETECT;
	pxa_set_mci_info(&colibri_mci_platform_data);
}
#else
static inline void colibri_mmc_init(void) {}
#endif

/******************************************************************************
 * USB Host
 ******************************************************************************/
#if defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
static int colibri_ohci_init(struct device *dev)
{
	UP2OCR = UP2OCR_HXS | UP2OCR_HXOE | UP2OCR_DPPDE | UP2OCR_DMPDE;
	return 0;
}

static struct pxaohci_platform_data colibri_ohci_info = {
//	.port_mode	= PMM_GLOBAL_MODE,
	.port_mode	=  PMM_NPS_MODE,
	.flags		= ENABLE_PORT1 | ENABLE_PORT2,
//			  | POWER_CONTROL_LOW | POWER_SENSE_LOW,
	.init		= colibri_ohci_init,
};

static void __init colibri_uhc_init(void)
{
    int ret;

    ret = gpio_request(mfp_to_gpio(MFP_PIN_GPIO2_2),"USB_PWR");
    if ( ret ) {
        printk(KERN_INFO "=>> Error reqesting usb host power gpio <<=\n");
    } else {
        gpio_direction_output(mfp_to_gpio(MFP_PIN_GPIO2_2), 0); // set to active power
    };

//    ret = gpio_request(MFP_PIN_GPIO3_2,"USB_OC");

    pxa_set_ohci_info(&colibri_ohci_info);
}
#else
static inline void colibri_uhc_init(void) {}
#endif

/******************************************************************************
 * Xilinx CPLD ans Sagrad Wi-Fi modules SPI interface
 ******************************************************************************/
static mfp_cfg_t colibri_pxa320_ssp_pin_config[] __initdata = {
    // SPI interface main pins
    GPIO83_SSP1_SCLK,
    GPIO85_SSP1_TXD,
    GPIO86_SSP1_RXD,
    GPIO1_2_GPIO,// SPI_CS For internal Xilinx CPLD
    GPIO84_GPIO | MFP_DS08X, // SPI CS For Sagrad SG901-1028
    GPIO94_GPIO | MFP_DS08X | MFP_LPM_EDGE_RISE, // IRQ
    GPIO95_GPIO | MFP_DS08X,                     // SLEEP
};


static struct pxa2xx_spi_chip xilinx_spi_tuning = {
    .gpio_cs   = 1,
};

static struct pxa2xx_spi_chip sg1028_spi_tuning = {
    .gpio_cs   = 84,
};

static struct spi_board_info colibri_pxa320_spi_chips[] = {
    { // Xilinx FPGA
       .modalias	= "xilinx-fpga",
       .max_speed_hz	= 13000000,
       .mode		= SPI_MODE_0,
       .bus_num		= 1,
       .chip_select	= 0,
       .controller_data = &xilinx_spi_tuning,
    },
    { // Sagrad SG901-1028 or SG901-1039 - module p54spi, but... Joke from Nokia =)
        .modalias       = "p54spi",
        .max_speed_hz   = 13000000,
        .mode           = SPI_MODE_0,
        .bus_num        = 1,
        .chip_select    = 1,
        .controller_data = &sg1028_spi_tuning,
    },
};

static struct pxa2xx_spi_master colibri_pxa320_spi_master = {
    .clock_enable = CKEN_SSP1,
    .num_chipselect = ARRAY_SIZE(colibri_pxa320_spi_chips),
    .enable_dma = 1,
};

static inline void colibri_spi_init(void)
{
    // Configure MFP subsystem
    pxa3xx_mfp_config( ARRAY_AND_SIZE( colibri_pxa320_ssp_pin_config ) );
    // Add SPI master interface
    pxa2xx_set_spi_info(1,&colibri_pxa320_spi_master);
    // Add devices on SPI bus
    spi_register_board_info( ARRAY_AND_SIZE( colibri_pxa320_spi_chips ) );
}

/******************************************************************************
 * I2C RTC
 ******************************************************************************/
#if defined(CONFIG_I2C) || defined (CONFIG_I2C_MODULE)
static struct i2c_board_info __initdata colibri_i2c_devs[] = {
#if defined(CONFIG_RTC_DRV_DS1307) || defined(CONFIG_RTC_DRV_DS1307_MODULE)
    {
	I2C_BOARD_INFO("m41t00", 0x68),
    },
#endif
    {
	// Battery controller (old edition, with I2C)
	I2C_BOARD_INFO("ds2782", 0x34),
    },
    {
	// I2C to W1 bridge for new edition battery
	I2C_BOARD_INFO("ds2482", 0x30),
    },
};

static void __init colibri_i2c_init(void)
{
    pxa_set_i2c_info(NULL);
    i2c_register_board_info(0, ARRAY_AND_SIZE(colibri_i2c_devs));
}
#else
static inline void colibri_i2c_init(void) {}
#endif

/******************************************************************************
 * Power management support (power off by software request)
 ******************************************************************************/
#if defined (CONFIG_PM)
static void colibri_pxa320_power_off ( void ) {
    printk(KERN_INFO "Disable module power...");
    gpio_direction_output(MFP_PIN_GPIO11,0);
    printk(KERN_INFO "[DONE]\n");
    pxa_restart('h',NULL);
}

static inline void colibri_pm_init ( void ) {
    int ret;

    ret = gpio_request(MFP_PIN_GPIO11,"PWR_OFF");
    if ( ret ) {
        printk(KERN_INFO "=>> Error reqesting power off gpio <<=\n");
    } else {
        gpio_direction_output(MFP_PIN_GPIO11, 1); // set to active power
    };
    pm_power_off        = colibri_pxa320_power_off;
}
#else
static inline void colibri_pm_init ( void ) {}
#endif

/******************************************************************************
 * GPIO Keyboard for KEY_POWER from power module
 ******************************************************************************/
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
};

static struct gpio_keys_platform_data colibri_pxa320_gpio_keys = {
    .buttons        = gpio_keys_button,
    .nbuttons       = 1,
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

static inline void colibri_kbd_init( void ) {
    platform_add_devices( ARRAY_AND_SIZE(colibri_pxa320_keyboard_devices) );
}
#else
static inline void colibri_kbd_init( void ) {}
#endif

/******************************************************************************
 * PDA Power class
 ******************************************************************************/
#if 1
static int colibri_pxa320_power_supply_init(struct device *dev)
{
    return 0;
}

static void colibri_pxa320_power_supply_exit(struct device *dev)
{
}

static char *colibri_pxa320_supplicants[] = {
    "ds2782-battery.0"
    "ds2760-battery.0"
    "ds2760-battery.1"
    "ds2760-battery.2"
    "ds2760-battery.3"
};

static struct pda_power_pdata colibri_pxa320_power_supply_info = {
    .init            = colibri_pxa320_power_supply_init,
    .exit            = colibri_pxa320_power_supply_exit,
    .supplied_to     = colibri_pxa320_supplicants,
    .num_supplicants = ARRAY_SIZE(colibri_pxa320_supplicants),
};

static struct platform_device colibri_pxa320_power_supply = {
    .name = "pda-power",
    .id   = -1,
    .dev  = {
        .platform_data = &colibri_pxa320_power_supply_info,
    },
};

static inline void colibri_pwr_init(void) {
    platform_device_register(&colibri_pxa320_power_supply);
}
#else
static inline void colibri_pwr_init(void) {}
#endif

/*
 * TiWi_R2 on sdio interface
 */
#if defined(CONFIG_WLCORE_SDIO) || defined(CONFIG_WLCORE_SDIO_MODULE)
#include <linux/wl12xx.h>

/*
    MFP_PULL_LOW | \
    MFP_LPM_PULL_LOW | \
*/
#define IRQ_GPIO_CFG_FLAG (\
    MFP_DS08X | \
    MFP_LPM_EDGE_RISE )

static mfp_cfg_t tiwi_pin_config[] __initdata = {
    GPIO73_GPIO | IRQ_GPIO_CFG_FLAG,
    GPIO9_GPIO  | IRQ_GPIO_CFG_FLAG,
};

#define TIWI_IRQ_GPIO	mfp_to_gpio(GPIO73_GPIO) /* GPIO9_GPIO */

static struct wl12xx_platform_data tiwi_mmc_pdata = {
    .irq = PXA_GPIO_TO_IRQ(TIWI_IRQ_GPIO),
    .board_ref_clock = WL12XX_REFCLOCK_38_XTAL,
    .board_tcxo_clock = WL12XX_TCXOCLOCK_26,
    .platform_quirks = WL12XX_PLATFORM_QUIRK_EDGE_IRQ, /* no level irq */
};

static inline void colibri_wl1271_sdio_init(void) {
    pxa3xx_mfp_config( ARRAY_AND_SIZE( tiwi_pin_config ) );
    wl12xx_set_platform_data(&tiwi_mmc_pdata);
};
#else
static inline void colibri_wl1271_sdio_init(void) {};
#endif

void __init colibri_evalboard_init(void)
{
    printk(KERN_INFO "===> Add carrier board devices for RadioAvionica board begin <===\n");

    pxa3xx_mfp_config(ARRAY_AND_SIZE(colibri_pxa320_ravionboard_pin_config));
    printk(KERN_INFO "GPIO configured for OAO Radioavionica Carrier Board\n");

    printk(KERN_INFO "Serial device ttyS0 (FFUART)\n"); pxa_set_ffuart_info(NULL);
    printk(KERN_INFO "Serial device ttyS1 (BTUART)\n"); pxa_set_btuart_info(NULL);
    printk(KERN_INFO "Serial device ttyS2 (STUART)\n"); pxa_set_stuart_info(NULL);

    printk(KERN_INFO "SD/MMC card controller\n"); colibri_mmc_init();
    printk(KERN_INFO "USB Open Host Controller Interface\n"); colibri_uhc_init();
    printk(KERN_INFO "SPI Intrface (WiFi and CPLD)\n"); colibri_spi_init();
    printk(KERN_INFO "I2C with OW bridge, Battery\n"); colibri_i2c_init();
    printk(KERN_INFO "PM power off functiona\n"); colibri_pm_init();
    printk(KERN_INFO "KEYBOARD power key from POU\n"); colibri_kbd_init();
    printk(KERN_INFO "POWER monitoring infrastructure\n"); colibri_pwr_init();
    printk(KERN_INFO "TiWi_R2 in sdio interface\n"); colibri_wl1271_sdio_init();

    printk(KERN_INFO "===>  Add carrier board devices for RadioAvionica board end  <===\n");
}
