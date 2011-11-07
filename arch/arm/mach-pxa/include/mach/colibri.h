#ifndef _COLIBRI_H_
#define _COLIBRI_H_

#include <net/ax88796.h>
#include <mach/mfp.h>
#include <mach/irqs.h>


////////////////////////////
// Colibri PXA27x Specific
#if defined(CONFIG_MACH_COLIBRI) 
/*
 * Video
 */
#define        COLIBRI_USE_VGA_MODULATOR
//#undef       COLIBRI_USE_VGA_MODULATOR

/*
 * Backlight
 */
#define COLIBRI_BL_MAX_INTENSITY 0xFE
#define COLIBRI_BL_PERIOD_NS     3500

/* Touchscreen Controller Philips UCB1400 */
#define GPIO_27X_UCB1400        MFP_PIN_GPIO113
#define COLI27X_TOUCH_IRQ       IRQ_GPIO(GPIO_27X_UCB1400)

/* SecureDigital/Multimedia Card controller */
#define GPIO_NR_COLI27X_SD_DETECT_N     MFP_PIN_GPIO0

/* OHCI Host/Client detect pin */
#define COLI27X_USB_VBUS        MFP_PIN_GPIO89
#define COLI27X_USB_OC          MFP_PIN_GPIO88
#define COLI27X_USB_OC_IRQ      IRQ_GPIO(COLI27X_USB_OC)
#define COLI27X_UDC_DETECT      MFP_PIN_GPIO41
#define COLI27X_UDC_DETECT_IRQ  IRQ_GPIO(COLI27X_UDC_DETECT)

#define COLI320_UDC_DETECT      MFP_PIN_GPIO96
#define COLI320_UDC_DETECT_IRQ  IRQ_GPIO(COLI320_UDC_DETECT)

/* PCMCIA/CF device */
#define COLI27X_PCMCIA_PRDY     MFP_PIN_GPIO1
#define COLI27X_PCMCIA_PRST     MFP_PIN_GPIO53
#define COLI27X_PCMCIA_PBVD1    MFP_PIN_GPIO83
#define COLI27X_PCMCIA_PBVD2    MFP_PIN_GPIO82
#define COLI27X_PCMCIA_PCD      MFP_PIN_GPIO84
#define COLI27X_PCMCIA_nPPEN    MFP_PIN_GPIO107
#define COLI27X_PCMCIA_PRDY_IRQ IRQ_GPIO(COLI27X_PCMCIA_PRDY)
#define COLI27X_PCMCIA_PCD_IRQ  IRQ_GPIO(COLI27X_PCMCIA_PCD)

extern void coli27x_init_cf_bus( void );
extern void coli27x_exit_cf_bus( void );

#define COLIBRI_PXA270_FLASH_PHYS	(PXA_CS0_PHYS)  /* Flash region */
#define COLIBRI_PXA270_ETH_PHYS		(PXA_CS2_PHYS)  /* Ethernet */
#define COLIBRI_PXA270_ETH_IRQ_GPIO	114
#define COLIBRI_PXA270_ETH_IRQ		\
	gpio_to_irq(mfp_to_gpio(COLIBRI_PXA270_ETH_IRQ_GPIO))

#endif /* CONFIG_MACH_COLIBRI */

////////////////////////////
// Colibri PXA32x Specific
#if defined( CONFIG_MACH_COLIBRI320 )
/*
 * PCMCIA/CompactFlash interface
 */
#define COLI32X_PCMCIA_PCD      mfp_to_gpio(GPIO81_GPIO)
#define COLI32X_PCMCIA_PRDY     mfp_to_gpio(GPIO29_GPIO)
#define COLI32X_PCMCIA_PRST     mfp_to_gpio(GPIO77_GPIO)
#define COLI32X_PCMCIA_PBVD1    mfp_to_gpio(GPIO53_GPIO)
#define COLI32X_PCMCIA_PBVD2    mfp_to_gpio(GPIO79_GPIO)
#define COLI32X_PCMCIA_nPPEN    mfp_to_gpio(GPIO57_GPIO)
#define COLI32X_PCMCIA_PCD_IRQ  gpio_to_irq( COLI32X_PCMCIA_PCD ) 
#define COLI32X_PCMCIA_PRDY_IRQ gpio_to_irq( COLI32X_PCMCIA_PRDY ) 

/*
CPLD rgisters block       0x17800000 - 0x178FFFFF => F0000000 - F00FFFFF
Static Memory controller  0x4A000000 - 0x4A0FFFFF => F0100000 - F01FFFFF
*/

/*
 * CPLD map from 0x14000000 - 0x17FFFFFF : 0xEE000000 - 0xF1FFFFFF
 */
#define CPLD_EXT_nCS0_PHYS	(PXA3xx_CS3_PHYS)
#define CPLD_EXT_nCS0_SIZE	0x40000
#define CPLD_EXT_nCS0_VIRT	0xEE000000

#define CPLD_EXT_nCS1_PHYS	(CPLD_EXT_nCS0_PHYS + 0x02000000)
#define CPLD_EXT_nCS1_SIZE	0x20000
#define CPLD_EXT_nCS1_VIRT	(CPLD_EXT_nCS0_VIRT + CPLD_EXT_nCS0_SIZE)

#define CPLD_EXT_nCS2_PHYS	(CPLD_EXT_nCS1_PHYS + 0x01000000)
#define CPLD_EXT_nCS2_SIZE	0x10000
#define CPLD_EXT_nCS2_VIRT	(CPLD_EXT_nCS1_VIRT + CPLD_EXT_nCS1_SIZE)

#define CPLD_REGS_PHYS  	(CPLD_EXT_nCS2_PHYS + 0x00800000)
#define CPLD_REGS_SIZE		0x10000
#define CPLD_REGS_VIRT		(CPLD_EXT_nCS2_VIRT + CPLD_EXT_nCS2_SIZE)

/*
 * CPLD for CF-Interface and External CS
 */
#define CPLD_REGS_P2V(X)	((X) - CPLD_REGS_PHYS + CPLD_REGS_VIRT)
#define CPLD_REGS_V2P(X)	((X) - CPLD_REGS_VIRT + CPLD_REGS_PHYS)

#ifndef __ASSEMBLY__
#  define __CPLD_REG(x) \
                (*((volatile u16 *)CPLD_REGS_P2V(x)))
#else
#  define __CPLD_REG(x)  CPLD_REGS_P2V(x)
#endif


// CPLD Registers and constants
#define CPLD_EXT_nCS_CTRL_REG	__CPLD_REG( 0x17800000 )
#define CPLD_MEM_CTRL_REG	__CPLD_REG( 0x17800004 )

// CPLD_EXT_nCS_CTRL_REG
#define EXT_nCS2_EC_DIS         (1<<15)
#define EXT_nCS1_EC_DIS         (1<<14)
#define EXT_nCS0_EC_DIS         (1<<13)
#define EXT_nCS2_DIS            (1<<10)
#define EXT_nCS1_DIS            (1<<9)
#define EXT_nCS0_DIS            (1<<8)
#define EXT_nCS2_EC_EN          (1<<7)
#define EXT_nCS1_EC_EN          (1<<6)
#define EXT_nCS0_EC_EN          (1<<5)
#define EXT_nCS2_EN             (1<<2)
#define EXT_nCS1_EN             (1<<1)
#define EXT_nCS0_EN             (1<<0)

// CPLD_MEM_CTRL_REG
#define nOE_DIS                 (1<<10)
#define RDnWR_DIS               (1<<9)
#define CF_DIS                  (1<<8)
#define nOE_EN                  (1<<2)
#define RDnWR_EN                (1<<1)
#define CF_EN                   (1<<0)


/*
 * Static memory controller map from 0x4A000000 - 0x4A0FFFFF : 0xED000000 - 0xED0FFFFF
 */
#define SMC_PHYS		0x4A000000
#define SMC_SIZE		0x00100000
#define SMC_VIRT		0xED000000

#define SMC_REGS_P2V(X)	((X) - SMC_PHYS + SMC_VIRT)
#define SMC_REGS_V2P(X)	((X) - SMC_VIRT + SMC_PHYS)

#ifndef __ASSEMBLY__
#  define __SMC_REG(x) \
                (*((volatile u32 *)SMC_REGS_P2V(x)))
#else
#  define __SMC_REG(x)  SMC_REGS_P2V(x)
#endif

/*
 * Static Memory Controller
 */
#define SMC_MSC0            __SMC_REG(0x4A000008) /* CS0 and CS1 control */
#define SMC_MSC1            __SMC_REG(0x4A00000C) /* CS2 and CS3 control */
#define SMC_MEMCLKCFG       __SMC_REG(0x4A000068)
#define SMC_CSADRCFG0       __SMC_REG(0x4A000080)
#define SMC_CSADRCFG1       __SMC_REG(0x4A000084)
#define SMC_CSADRCFG2       __SMC_REG(0x4A000088)
#define SMC_CSADRCFG3       __SMC_REG(0x4A00008C)
#define SMC_CSADRCFG_P      __SMC_REG(0x4A000090)
#define SMC_CSMSADRCFG      __SMC_REG(0x4A0000A0)
#define SMC_MEMCLKCFG       __SMC_REG(0x4A000068)
#define SMC_SXCNFG          __SMC_REG(0x4A00001C)

/*
 * PCMCIA/CompactFlash
 */
#define SMC_MECR            __SMC_REG(0x4A000014) 
#define SMC_MCMEM           __SMC_REG(0x4A000028)
#define SMC_MCATT           __SMC_REG(0x4A000030)
#define SMC_MCIO            __SMC_REG(0x4A000038)

#define SMC_MECR_CIT        (1 << 1)        /* Card Is There: 0 -> no card, 1 -> card inserted */

extern void coli32x_init_cf_bus( void );
extern void coli32x_exit_cf_bus( void );
#endif /* CONFIG_MACH_COLIBRI320 */

/*
 * Colibri PXA3xx modules settings
 */
#if defined(CONFIG_MMC_PXA) || defined(CONFIG_MMC_PXA_MODULE)
extern void colibri_pxa3xx_init_mmc(mfp_cfg_t *pins, int len, int detect_pin);
#else
static inline void colibri_pxa3xx_init_mmc(mfp_cfg_t *pins, int len, int detect_pin) {}
#endif

#if defined(CONFIG_FB_PXA) || defined(CONFIG_FB_PXA_MODULE)
extern void colibri_pxa3xx_init_lcd(int bl_pin);
#else
static inline void colibri_pxa3xx_init_lcd(int bl_pin) {}
#endif

#if defined(CONFIG_AX88796)
extern void colibri_pxa3xx_init_eth(struct ax_plat_data *plat_data);
#endif

#if defined(CONFIG_MTD_NAND_PXA3xx) || defined(CONFIG_MTD_NAND_PXA3xx_MODULE)
extern void colibri_pxa3xx_init_nand(void);
#else
static inline void colibri_pxa3xx_init_nand(void) {}
#endif

/*
 * common settings for all modules
 */

/* physical memory regions */
#define COLIBRI_SDRAM_BASE	0xa0000000      /* SDRAM region */


#endif /* _COLIBRI_H_ */

