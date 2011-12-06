#ifndef _AX796C_PLAT_H_
#define _AX796C_PLAT_H_

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/crc32.h>
#include <linux/mii.h>
#include <linux/if_vlan.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/irq.h>
#include <asm/dma.h>
#include <linux/dma-mapping.h>

/* Platform independent parameter settings */
#ifdef CONFIG_ARCH_S3C2410
/* S3C2410 platform */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
#include <asm/arch/regs-mem.h>
#include <asm/arch/regs-irq.h>
#include <asm/arch/regs-gpio.h>
#else
#include <mach/regs-gpio.h>
#include <mach/regs-mem.h>
#include <plat/regs-dma.h>
#endif
#endif

#ifndef TRUE
#define TRUE				1
#endif

#ifndef FALSE
#define FALSE				0
#endif

/* 
 * Configuration options
 */
/* DMA mode only effected on SMDK2440 platform. */
#define TX_DMA_MODE			FALSE
#define RX_DMA_MODE			FALSE
#define AX88796B_PIN_COMPATIBLE		TRUE
#define AX88796C_8BIT_MODE		FALSE
#define DMA_BURST_LEN			DMA_BURST_LEN_4_WORD
#define REG_SHIFT			0x00

#if (AX88796B_PIN_COMPATIBLE)
#define DATA_PORT_ADDR			0x800
#else
#define DATA_PORT_ADDR			0x4000
#endif

/* Exported DMA operations */
void ax88796c_plat_init (int bus_type);
int ax88796c_plat_dma_init (unsigned long base_addr,
				void (*tx_dma_complete)(void *data),
				void (*rx_dma_complete)(void *data),
				void *priv);
void ax88796c_plat_dma_release (void);
void dma_start(dma_addr_t dst, int len, u8 tx);

#endif	/* _AX796C_PLAT_H_ */

