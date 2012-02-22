/*
 *  Board-specific setup code for the Ronetix AT91SAM9G45 module
 *
 *  Copyright (C) 2011	Alex A. Mihaylov AKA MinimumLaw
 *			<minimumlaw@rambler.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/fb.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/leds.h>
#include <linux/clk.h>
#include <linux/atmel-mci.h>
#include <linux/w1-gpio.h>

#include <mach/hardware.h>
#include <video/atmel_lcdc.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/irq.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/board.h>
#include <mach/gpio.h>
#include <mach/at91sam9_smc.h>
#include <mach/at91_shdwc.h>
#include <mach/cw1200_plat.h>

#include "sam9_smc.h"
#include "generic.h"


static void __init pm9g45_init_early(void)
{
	/* Initialize processor: 12.000 MHz crystal */
	at91_initialize(12000000);

	/* DGBU on ttyS0. (Rx & Tx only) */
	at91_register_uart(0, 0, 0);

	/* USART0 not connected on the -EK board */
	/* USART1 on ttyS2. (Rx, Tx, RTS, CTS) */
	/* at91_register_uart(AT91SAM9G45_ID_US1, 2, ATMEL_UART_CTS | ATMEL_UART_RTS); */

	/* set serial console to ttyS0 (ie, DBGU) */
	at91_set_serial_console(0);
}

/*
 * USB HS Host port (common to OHCI & EHCI)
 */
static struct at91_usbh_data __initdata pm9g45_usbh_hs_data = {
	.ports		= 2,
//	.vbus_pin	= {AT91_PIN_PD1, AT91_PIN_PD3}, // always powered
};


/*
 * USB HS Device port
 */
static struct usba_platform_data __initdata pm9g45_usba_udc_data = {
	.vbus_pin	= AT91_PIN_PD1, // BB9263 ver 1.1
};


/*
 * SPI devices.
 */
static struct spi_board_info pm9g45_spi_devices[] = {
	{	/* DataFlash chip */
		.modalias	= "mtd_dataflash",
		.chip_select	= 0,
		.max_speed_hz	= 15 * 1000 * 1000,
		.bus_num	= 0,
	},
};


/*
 * MCI (SD/MMC)
 */
static struct mci_platform_data __initdata mci0_data = {
	.slot[0] = {
		.bus_width	= 4,
		.detect_pin	= AT91_PIN_PD30,
		.wp_pin		= AT91_PIN_PD29,
	},
};
/*
static struct mci_platform_data __initdata mci1_data = {
	.slot[0] = {
		.bus_width	= 4,
		.detect_pin	= AT91_PIN_PD11,
		.wp_pin		= AT91_PIN_PD29,
	},
};
*/

/*
 * MACB Ethernet device
 */
static struct at91_eth_data __initdata pm9g45_macb_data = {
	.phy_irq_pin	= AT91_PIN_PD5,
	.is_rmii	= 1,
};


/*
 * NAND flash
 */
static struct mtd_partition __initdata pm9g45_nand_partition[] = {
	{
		.name	= "Partition 1",
		.offset	= 0,
		.size	= SZ_64M,
	},
	{
		.name	= "Partition 2",
		.offset	= MTDPART_OFS_NXTBLK,
		.size	= MTDPART_SIZ_FULL,
	},
};

/* det_pin is not connected */
static struct atmel_nand_data __initdata pm9g45_nand_data = {
	.ale		= 21,
	.cle		= 22,
	.rdy_pin	= AT91_PIN_PD3,
	.enable_pin	= AT91_PIN_PC14,
	.parts		= pm9g45_nand_partition,
	.num_parts	= ARRAY_SIZE(pm9g45_nand_partition),
#if defined(CONFIG_MTD_NAND_ATMEL_BUSWIDTH_16)
	.bus_width_16	= 1,
#else
	.bus_width_16	= 0,
#endif
};

static struct sam9_smc_config __initdata pm9g45_nand_smc_config = {
	.ncs_read_setup		= 0,
	.nrd_setup		= 2,
	.ncs_write_setup	= 0,
	.nwe_setup		= 2,

	.ncs_read_pulse		= 4,
	.nrd_pulse		= 4,
	.ncs_write_pulse	= 4,
	.nwe_pulse		= 4,

	.read_cycle		= 7,
	.write_cycle		= 7,

	.mode			= AT91_SMC_READMODE | AT91_SMC_WRITEMODE | AT91_SMC_EXNWMODE_DISABLE,
	.tdf_cycles		= 3,
};

static void __init pm9g45_add_device_nand(void)
{
	/* setup bus-width (8 or 16) */
	if (pm9g45_nand_data.bus_width_16)
		pm9g45_nand_smc_config.mode |= AT91_SMC_DBW_16;
	else
		pm9g45_nand_smc_config.mode |= AT91_SMC_DBW_8;

	/* configure chip-select 3 (NAND) */
	sam9_smc_configure(3, &pm9g45_nand_smc_config);

	at91_add_device_nand(&pm9g45_nand_data);
}


/*
 * LCD Controller
 */
#if defined(CONFIG_FB_ATMEL) || defined(CONFIG_FB_ATMEL_MODULE)
static struct fb_videomode at91_tft_vga_modes[] = {
	{
		.name           = "LG",
		.refresh	= 60,
		.xres		= 480,		.yres		= 272,
		.pixclock	= KHZ2PICOS(9000),

		.left_margin	= 1,		.right_margin	= 1,
		.upper_margin	= 40,		.lower_margin	= 1,
		.hsync_len	= 45,		.vsync_len	= 1,

		.sync		= 0,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
};

static struct fb_monspecs at91fb_default_monspecs = {
	.manufacturer	= "LG",
	.monitor        = "LB043WQ1",

	.modedb		= at91_tft_vga_modes,
	.modedb_len	= ARRAY_SIZE(at91_tft_vga_modes),
	.hfmin		= 15000,
	.hfmax		= 17640,
	.vfmin		= 57,
	.vfmax		= 67,
};

#define AT91SAM9G45_DEFAULT_LCDCON2	(ATMEL_LCDC_MEMOR_LITTLE \
					| ATMEL_LCDC_DISTYPE_TFT \
					| ATMEL_LCDC_CLKMOD_ALWAYSACTIVE)

/* Driver datas */
static struct atmel_lcdfb_info __initdata pm9g45_lcdc_data = {
	.lcdcon_is_backlight		= true,
	.default_bpp			= 32,
	.default_dmacon			= ATMEL_LCDC_DMAEN,
	.default_lcdcon2		= AT91SAM9G45_DEFAULT_LCDCON2,
	.default_monspecs		= &at91fb_default_monspecs,
	.guard_time			= 9,
	.lcd_wiring_mode		= ATMEL_LCDC_WIRING_RGB,
};

#else
static struct atmel_lcdfb_info __initdata pm9g45_lcdc_data;
#endif

/*
 * GPIO Buttons
 */
#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
static struct gpio_keys_button pm9g45_buttons[] = {
	{
		.code		= KEY_MUTE,
		.gpio		= AT91_PIN_PE7,
		.active_low	= 1,
		.desc		= "mute",
		.type		= EV_KEY,
		.wakeup		= 1,
	},
	{
		.code		= KEY_POWER,
		.gpio		= AT91_PIN_PE8,
		.active_low	= 1,
		.desc		= "power",
		.type		= EV_KEY,
		.wakeup		= 1,
	},
};

static struct gpio_keys_platform_data pm9g45_button_data = {
	.buttons	= pm9g45_buttons,
	.nbuttons	= ARRAY_SIZE(pm9g45_buttons),
};

static struct platform_device pm9g45_button_device = {
	.name		= "gpio-keys",
	.id		= -1,
	.num_resources	= 0,
	.dev		= {
		.platform_data	= &pm9g45_button_data,
	}
};

static void __init pm9g45_add_device_buttons(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(pm9g45_buttons); i++) {
		at91_set_GPIO_periph(pm9g45_buttons[i].gpio, 1);
		at91_set_deglitch(pm9g45_buttons[i].gpio, 1);
	}

	platform_device_register(&pm9g45_button_device);
}
#else
static void __init pm9g45_add_device_buttons(void) {}
#endif


/*
 * AC97
 * reset_pin is not connected: NRST
 */
static struct ac97c_platform_data pm9g45_ac97_data = {
};


/*
 * CompactFlash (CF True IDE mode - flag options)
 */
static struct at91_cf_data pm9g45_cf_data = {
        .chipselect = 4,
        .irq_pin    = AT91_PIN_PE1,
        .rst_pin    = AT91_PIN_PD14,
        .det_pin    = AT91_PIN_PD13,
//	.vcc_pin    = none, // always powered
//	.flags      = AT91_CF_TRUE_IDE, // set this, for use IDE (not CF)
};

/*
 * Maxim/Dalas OneWire host (w1-gpio)
 */
#if defined(CONFIG_W1_MASTER_GPIO) || defined(CONFIG_W1_MASTER_GPIO_MODULE)
static struct w1_gpio_platform_data w1_gpio_pdata = {
        .pin            = AT91_PIN_PA31,
        .is_open_drain  = 1,
};

static struct platform_device w1_device = {
        .name                   = "w1-gpio",
        .id                     = -1,
        .dev.platform_data      = &w1_gpio_pdata,
};

static void __init at91_add_device_w1(void)
{
        at91_set_GPIO_periph(w1_gpio_pdata.pin, 1);
        at91_set_multi_drive(w1_gpio_pdata.pin, 1);
        platform_device_register(&w1_device);
}
#else
static void __init at91_add_device_w1(void) {}
#endif

/******************************************************************************
 * ST/Ericson CW1200 wlan module
 ******************************************************************************/
#if defined(CONFIG_CW1200) || defined(CONFIG_CW1200_MODULE)
static struct cw1200_platform_data cw1200_pm9g45_platform_data = { 0 };

static struct platform_device cw1200_device = {
        .name = "cw1200_wlan",
        .dev = {
                .platform_data = &cw1200_pm9g45_platform_data,
                .init_name = "cw1200_wlan",
        },
};

//  cw1200_get_platform_data used by stage driver for ST/Ericson u5500 platform
const struct cw1200_platform_data *cw1200_get_platform_data(void)
{
        return &cw1200_pm9g45_platform_data;
}
EXPORT_SYMBOL_GPL(cw1200_get_platform_data);

static inline void pm9g45_cw1200_init(void) {
    cw1200_pm9g45_platform_data.mmc_id = "mmc0";

    platform_device_register(&cw1200_device);
}
#else
static inline void pm9g45_cw1200_init(void) {}
#endif


static void __init pm9g45_board_init(void)
{
	/* Serial */
	at91_add_device_serial();
	/* USB HS Host */
	at91_add_device_usbh_ohci(&pm9g45_usbh_hs_data);
	at91_add_device_usbh_ehci(&pm9g45_usbh_hs_data);
	/* USB HS Device */
	at91_add_device_usba(&pm9g45_usba_udc_data);
	/* SPI */
	at91_add_device_spi(pm9g45_spi_devices, ARRAY_SIZE(pm9g45_spi_devices));
	/* MMC */
	at91_add_device_mci(0, &mci0_data);
	/*at91_add_device_mci(1, &mci1_data);*/
	/* Ethernet */
	at91_add_device_eth(&pm9g45_macb_data);
	/* NAND */
	pm9g45_add_device_nand();
	/* I2C */
	at91_add_device_i2c(0, NULL, 0);
	/* LCD Controller */
	at91_add_device_lcdc(&pm9g45_lcdc_data);
	/* Push Buttons */
	pm9g45_add_device_buttons();
	/* AC97 */
	at91_add_device_ac97(&pm9g45_ac97_data);
	/* CF */
	at91_add_device_cf(&pm9g45_cf_data);
	/* W1 */
	at91_add_device_w1();
	/* CW1200 WiFi */
	pm9g45_cw1200_init();
}

MACHINE_START(PM9G45, "Ronetix PM9G45 cpu module")
//	.boot_params	= AT91_SDRAM_BASE + 0x100,
	.timer		= &at91sam926x_timer,
	.map_io		= at91_map_io,
	.init_irq	= at91_init_irq_default,
	.init_early	= pm9g45_init_early,
	.init_machine	= pm9g45_board_init,
MACHINE_END
