#ifndef _COLIBRI_H_
#define _COLIBRI_H_

#include <asm/system_info.h>

#include <net/ax88796.h>
#include <mach/mfp.h>

/*
 * base board glue for PXA270 module
 */

enum {
	COLIBRI_EVALBOARD = 0,
	COLIBRI_PXA270_INCOME,
};

#if defined(CONFIG_MACH_COLIBRI_EVALBOARD) || defined(CONFIG_MACH_COLIBRI_RAVIONBOARD)
extern void colibri_evalboard_init(void);
#else
static inline void colibri_evalboard_init(void) {}
#endif

#if defined(CONFIG_MACH_COLIBRI_PXA270_INCOME)
extern void colibri_pxa270_income_boardinit(void);
#else
static inline void colibri_pxa270_income_boardinit(void) {}
#endif

/*
 * common settings for all modules
 */

#if defined(CONFIG_MMC_PXA) || defined(CONFIG_MMC_PXA_MODULE)
extern void colibri_pxa3xx_init_mmc(mfp_cfg_t *pins, int len, int detect_pin);
#else
static inline void colibri_pxa3xx_init_mmc(mfp_cfg_t *pins,
				int len, int detect_pin) {}
#endif

#if defined(CONFIG_FB_PXA) || defined(CONFIG_FB_PXA_MODULE)
extern void colibri_pxa3xx_init_lcd(int bl_pin);
#else
static inline void colibri_pxa3xx_init_lcd(int bl_pin) {}
#endif

#if defined(CONFIG_AX88796) || defined(CONFIG_AX88796C)
extern void colibri_pxa3xx_init_eth(struct ax_plat_data *plat_data);
#endif

#if defined(CONFIG_MTD_NAND_PXA3xx) || defined(CONFIG_MTD_NAND_PXA3xx_MODULE)
extern void colibri_pxa3xx_init_nand(void);
#else
static inline void colibri_pxa3xx_init_nand(void) {}
#endif

/* physical memory regions */
#define COLIBRI_SDRAM_BASE	0xa0000000      /* SDRAM region */

/* GPIO definitions for Colibri PXA270 */
#define GPIO114_COLIBRI_PXA270_ETH_IRQ	114
#define GPIO0_COLIBRI_PXA270_SD_DETECT	0
#define GPIO113_COLIBRI_PXA270_TS_IRQ	113

/* GPIO definitions for Colibri PXA300/310 */
#define GPIO13_COLIBRI_PXA300_SD_DETECT	13

/* GPIO definitions for Colibri PXA320 */
#define GPIO28_COLIBRI_PXA320_SD_DETECT	28


/*****************************************************************************
 * Add virtual memory region for Colibri PXA320 CPLD registers
 *
 * Phus start Phys end    Virt start Virt end   Device/Function name
 * 0x40000000-0x42000000->0xf2000000-0xf4000000 PXA Regs/pxa_map_io()
 * 0x4a000000-0x4a200000->0xf6000000-0xf6200000 SMEMC/pxa3xx_map.io()
 * 0x17800000-0x17a00000->0xf6200000-0xf6400000 CPLD/colibri_pxa320_map_io()
 ****************************************************************************/
#define CPLD_REGS_PHYS		0x17800000
#define CPLD_REGS_VIRT		0xf6200000
#define CPLD_REGS_LEN		0x00200000

#define CPLD_CS_CTRL		(CPLD_REGS_VIRT)
#define CPLD_EXT_nCS2_EC_DIS	(1<<15)
#define CPLD_EXT_nCS1_EC_DIS	(1<<14)
#define CPLD_EXT_nCS0_EC_DIS	(1<<13)
#define CPLD_EXT_nCS2_DIS	(1<<10)
#define CPLD_EXT_nCS1_DIS	(1<<9)
#define CPLD_EXT_nCS0_DIS	(1<<8)
#define CPLD_EXT_nCS2_EC_EN	(1<<7)
#define CPLD_EXT_nCS1_EC_EN	(1<<6)
#define CPLD_EXT_nCS0_EC_EN	(1<<5)
#define CPLD_EXT_nCS2_EN	(1<<2)
#define CPLD_EXT_nCS1_EN	(1<<1)
#define CPLD_EXT_nCS0_EN	(1<<0)

#define CPLD_MEM_CTRL           (CPLD_REGS_VIRT + 4)
#define CPLD_MEM_nOE_DIS	(1<<10)
#define CPLD_MEM_RDnWR_DIS	(1<<9)
#define CPLD_MEM_CF_DIS		(1<<8)
#define CPLD_MEM_nOE_EN		(1<<2)
#define CPLD_MEM_RDnWR_EN	(1<<1)
#define CPLD_MEM_CF_EN		(1<<0)

#endif /* _COLIBRI_H_ */
