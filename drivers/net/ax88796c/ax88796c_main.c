/*
 * ASIX AX88796C based Fast Ethernet Devices
 * Copyright (C) 2009 ASIX Electronics Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "ax88796c.h"
#include "ax88796c_ioctl.h"

/* Naming constant declarations */

/* Local variables declarations */
static char version[] =
KERN_INFO AX88796C_ADP_NAME ":v" AX88796C_DRV_VERSION
" " __TIME__ " " __DATE__ "\n"
KERN_INFO "  http://www.asix.com.tw\n";
#if (AX88796C_8BIT_MODE)
static int bus_wide = 0;
#else
static int bus_wide = 1;
#endif

static int mem;
static int irq;
static int ps_level = AX_PS_D0;
static int msg_enable = (NETIF_MSG_DRV |
			 NETIF_MSG_PROBE |
			 NETIF_MSG_LINK |
			 NETIF_MSG_IFUP |
			 NETIF_MSG_RX_ERR |
			 NETIF_MSG_TX_ERR |
//			 NETIF_MSG_TX_QUEUED |
//			 NETIF_MSG_INTR |
//			 NETIF_MSG_RX_STATUS |
//			 NETIF_MSG_PKTDATA |
//			 NETIF_MSG_HW |
			 NETIF_MSG_WOL);

module_param (mem, int, 0);
module_param (irq, int, 0);
module_param (msg_enable, int, 0);
module_param (ps_level, int, 0);

MODULE_PARM_DESC(mem, "MEMORY base address(es), required");
MODULE_PARM_DESC(irq, "IRQ number(s)");
MODULE_PARM_DESC(msg_enable, "Message level");
MODULE_PARM_DESC(ps_level, "Power Saving Level (0:disable 1:level 1 2:level 2)");

MODULE_DESCRIPTION ("ASIX AX88796C Fast Ethernet driver");
MODULE_LICENSE ("GPL");

/*
 * ----------------------------------------------------------------------------
 * Function Name: ax88796c_dump_regs
 * Purpose: Dump all MAC registers
 * ----------------------------------------------------------------------------
 */
static void ax88796c_dump_regs (struct ax88796c_device *ax_local)
{
	void __iomem *ax_base = ax_local->membase;
	u8 i, j;

	printk ("       Page0   Page1   Page2   Page3   "
				"Page4   Page5   Page6   Page7\n");
	for (i = 0; i < 0x20; i += 2) {

		printk ("0x%02x   ", i);
		for (j = 0; j < 8; j++) {
			AX_SELECT_PAGE(j, ax_base + PG_PSR);
			printk ("0x%04x  ", AX_READ (ax_base + AX_SHIFT (i)));
		}
		printk ("\n");
	}
	printk ("\n");

}

/*
 * ----------------------------------------------------------------------------
 * Function Name: ax88796c_dump_tx_pkt
 * Purpose: Dump TX packets
 * ----------------------------------------------------------------------------
 */
static void ax88796c_dump_tx_pkt (struct sk_buff *skb)
{
	struct skb_data *entry = (struct skb_data *) skb->cb;
	u8 *buf = (u8 *)&entry->txhdr;
	int i, total = 0;

	printk ("Dump processed TX packet (len %d)\n",
			(sizeof (struct tx_header) +
			entry->dma_len +
			sizeof (struct tx_eop_header)));
	for (i = 0; i < sizeof (struct tx_header); i++) {
		printk ("%02x ", *(buf + i));
		total++;
	}

	for (i = 0; i < entry->dma_len; i++) {
		if ((total % 16) == 0)
			printk ("\n");
		printk ("%02x ", *(skb->data + i));
		total++;
	}

	buf = (u8 *)&entry->tx_eop;
	for (i = 0; i < sizeof (struct tx_eop_header); i++) {
		if ((total % 16) == 0)
			printk ("\n");
		printk ("%02x ", *(buf + i));
		total++;
	}

	printk ("\n");

}

/*
 * ----------------------------------------------------------------------------
 * Function Name: ax88796c_dump_rx_pkt
 * Purpose: Dump RX packets
 * ----------------------------------------------------------------------------
 */
static void ax88796c_dump_rx_pkt (struct sk_buff *skb)
{
	int i;
	for (i = 0; i < skb->len; i++) {
		if ((i % 16) == 0)
			printk ("\n");
		printk ("%02x ", *(skb->data + i));
	}
	printk ("\n");
}

/*
 * ----------------------------------------------------------------------------
 * Function Name: ax88796c_dump_phy_regs
 * Purpose: Dump PHY register from MR0 to MR5
 * ----------------------------------------------------------------------------
 */
static void ax88796c_dump_phy_regs (struct ax88796c_device *ax_local)
{
	int i;

	printk ("Dump PHY registers:\n");
	for (i = 0; i < 6; i++) {
		printk ("  MR%d = 0x%04x\n", i,
			ax88796c_mdio_read_phy (ax_local->ndev,
						ax_local->mii.phy_id, i));
	}
}

/*
 * ----------------------------------------------------------------------------
 * Function Name: ax88796c_reset
 * Purpose: Reset the whol chip
 * ----------------------------------------------------------------------------
 */
static int ax88796c_reset (struct ax88796c_device *ax_local)
{
	void __iomem *ax_base = ax_local->membase;
	unsigned long start;

	AX_WRITE (PSR_RESET, ax_base + PG_PSR);
	AX_WRITE (PSR_RESET_CLR, ax_base + PG_PSR);

	start = jiffies;
	while (!(AX_READ (ax_base + PG_PSR) & PSR_DEV_READY))
	{
		if (time_after(start, start + 2 * HZ / 100)) {		/* 20ms */
			printk (KERN_ERR 
				"%s: timeout waiting for reset completion\n",
				ax_local->ndev->name);
			return -1;
		}
	}

	return 0;
}

/*
 * ----------------------------------------------------------------------------
 * Function Name: ax88796c_reload_eeprom
 * Purpose: Load eeprom data from eeprom
 * ----------------------------------------------------------------------------
 */
static int ax88796c_reload_eeprom (struct ax88796c_device *ax_local)
{
	void __iomem *ax_base = ax_local->membase;
	unsigned long start;

	AX_SELECT_PAGE(PAGE3, ax_base + PG_PSR);
	AX_WRITE (EECR_RELOAD , ax_base + P3_EECR);

	start = jiffies;
	while (!(AX_READ (ax_base + PG_PSR) & PSR_DEV_READY)) {
		if (time_after(start, start + 2 * HZ / 100)) {		/* 20ms */
			printk (KERN_ERR 
				"%s: timeout waiting for reload eeprom\n",
				ax_local->ndev->name);
			return -1;
		}
	}

	AX_SELECT_PAGE(PAGE0, ax_base + PG_PSR);
	return 0;
}

/*
 * ----------------------------------------------------------------------------
 * Function Name: ax88796c_set_multicast
 * Purpose: Set receiving mode and multicast filter
 * ----------------------------------------------------------------------------
 */
static void ax88796c_set_multicast (struct net_device *ndev)
{
	struct ax88796c_device *ax_local = netdev_priv (ndev);
	void __iomem *ax_base = ax_local->membase;
	unsigned long flags;
	u16 rx_ctl = RXCR_AB;
	u8 power;
 #if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35)
	int mc_count = ndev->mc_count;
#else
	int mc_count = netdev_mc_count (ndev);
#endif

	spin_lock_irqsave (&ax_local->isr_lock, flags);

	power = ax88796c_check_power_state (ndev);

	AX_SELECT_PAGE(PAGE2, ax_base + PG_PSR);
	if (ndev->flags & IFF_PROMISC) {
		rx_ctl |= RXCR_PRO;
	} else if (ndev->flags & IFF_ALLMULTI
		   || mc_count > AX_MAX_MCAST) {
		rx_ctl |= RXCR_AMALL;
	} else if (mc_count == 0) {
		/* just broadcast and directed */
	} else {
		u32 crc_bits;
		int i;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35)
		struct dev_mc_list *mc_list = ndev->mc_list;

		memset(ax_local->multi_filter, 0, AX_MCAST_FILTER_SIZE);
		/* Build the multicast hash filter. */
		for (i = 0; i < ndev->mc_count; i++) {
			crc_bits = ether_crc (ETH_ALEN, mc_list->dmi_addr);
			ax_local->multi_filter[crc_bits >> 29] |=
						(1 << ((crc_bits >> 26) & 7));
			mc_list = mc_list->next;
		}
#else
		struct netdev_hw_addr *ha;
		netdev_for_each_mc_addr (ha, ndev) {
			crc_bits = ether_crc (ETH_ALEN, ha->addr);
			ax_local->multi_filter[crc_bits >> 29] |=
						(1 << ((crc_bits >> 26) & 7));
		}
#endif
		AX_SELECT_PAGE (PAGE3, ax_base + PG_PSR);
		for (i = 0; i < 4; i++) {
			AX_WRITE (((ax_local->multi_filter[i*2+1] << 8) |
					ax_local->multi_filter[i*2]),
					ax_base + P3_MFAR(i));

		}

	}

	AX_SELECT_PAGE(PAGE2, ax_base + PG_PSR);
	AX_WRITE (rx_ctl ,ax_base + P2_RXCR);
	
	AX_SELECT_PAGE(PAGE0, ax_base + PG_PSR);

	if (power)
		ax88796c_set_power_saving (ax_local->ndev, ax_local->ps_level);

	spin_unlock_irqrestore (&ax_local->isr_lock, flags);
}

/*
 * ----------------------------------------------------------------------------
  * Function Name: ax88796c_handle_tx_hdr
 * Purpose: TX headers processing
 * ----------------------------------------------------------------------------
 */
static void inline
ax88796c_handle_tx_hdr (struct tx_header *txhdr, u16 pkt_len, u16 seq_num,
			u8 soffset, u8 eoffset)
{
	u16 len_bar = (~pkt_len & TX_HDR_SOP_PKTLENBAR );

	/* Prepare SOP header */
	txhdr->sop.flags_pktlen = pkt_len;
	txhdr->sop.seqnum_pktlenbar = ((seq_num << 11) & TX_HDR_SOP_SEQNUM) |
					(~pkt_len & TX_HDR_SOP_PKTLENBAR);

	cpu_to_be16s (&txhdr->sop.flags_pktlen);
	cpu_to_be16s (&txhdr->sop.seqnum_pktlenbar);

#ifdef TX_MANUAL_DEQUEUE_CNT
	if(ax_local->seq_num % TX_MANUAL_DEQUEUE_CNT == 0)
		txhdr->sop.flags_pktlen |= TX_HDR_SOP_MDEQ;
#endif

	/* Prepare Segment header */
	txhdr->seg.flags_seqnum_seglen = TX_HDR_SEG_FS |
					TX_HDR_SEG_LS | pkt_len ;
	txhdr->seg.eo_so_seglenbar = ((u16)eoffset << TX_HDR_SEG_EOFBITS) |
					((u16)soffset << TX_HDR_SEG_SOFBITS) |
					len_bar;

	cpu_to_be16s (&txhdr->seg.flags_seqnum_seglen);
	cpu_to_be16s (&txhdr->seg.eo_so_seglenbar);
}

/*
 * ----------------------------------------------------------------------------
 * Function Name: ax88796c_process_tx_eop
 * Purpose: TX EOP header handling
 * ----------------------------------------------------------------------------
 */
static void inline
ax88796c_handle_tx_eop(struct tx_eop_header *tx_eop, u16 pkt_len, u16 seq_num)
{
	/* Prepare EOP header */
	tx_eop->seqnum_pktlen = ((seq_num << 11) & TX_HDR_EOP_SEQNUM) | pkt_len;
	
	tx_eop->seqnumbar_pktlenbar = ((~seq_num << 11) & TX_HDR_EOP_SEQNUMBAR)
					| (~pkt_len & TX_HDR_EOP_PKTLENBAR);

	cpu_to_be16s (&tx_eop->seqnum_pktlen);
	cpu_to_be16s (&tx_eop->seqnumbar_pktlenbar);
}

/*
 * ----------------------------------------------------------------------------
 * Function Name: ax88796c_check_free_pages
 * Purpose: Check free pages of TX buffer
 * ----------------------------------------------------------------------------
 */
static int
ax88796c_check_free_pages (struct ax88796c_device *ax_local, u8 need_pages)
{
	void __iomem *ax_base = ax_local->membase;
	u8 free_pages;
	u16 tmp;

	free_pages = AX_READ (ax_base + P0_TFBFCR) & TX_FREEBUF_MASK;
	if (free_pages < need_pages) 
	{
		/* schedule free page interrupt */
		tmp = AX_READ (ax_base + P0_TFBFCR) & TFBFCR_SCHE_FREE_PAGE;
		AX_WRITE (tmp | TFBFCR_TX_PAGE_SET |
				TFBFCR_SET_FREE_PAGE(need_pages),
				ax_base + P0_TFBFCR);
		return -ENOMEM;
	}

	return 0; 
}

/*
 * ----------------------------------------------------------------------------
 * Function Name: ax88796c_xmit
 * Purpose: Packet transmission handling
 * ----------------------------------------------------------------------------
 */
static int ax88796c_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct ax88796c_device *ax_local = netdev_priv (ndev);
	struct skb_data *entry = (struct skb_data *) skb->cb;
	unsigned long flags;
	u8 offset;

	entry->pages = ((skb->len + sizeof (struct tx_header) - 1) >>
						AX88796C_PAGE_SHIFT ) + 1;
	offset = (unsigned long)skb->data % 4;
	entry->len = skb->len;
	entry->dma_len = ((skb->len + offset + 3) & DWORD_ALIGNMENT);
	entry->offset = offset;
	ax88796c_handle_tx_hdr (&entry->txhdr, skb->len,
				ax_local->seq_num, offset,
				ax_local->burst_len);

	ax88796c_handle_tx_eop(&entry->tx_eop,
			       skb->len, ax_local->seq_num);

	/*Increase Sequence Number*/
	ax_local->seq_num = (ax_local->seq_num + 1) & 0x1F;

	if (netif_msg_pktdata (ax_local))
	{
		printk ("\n%s: TX packet, len %d\n",
					__FUNCTION__, skb->len);
		ax88796c_dump_tx_pkt (skb);
	}

	spin_lock_irqsave (&ax_local->tx_busy_q.lock, flags);

	if (skb_queue_empty (&ax_local->tx_busy_q) &&
	    skb_queue_empty (&ax_local->tx_q) &&
	    (ax88796c_check_free_pages (ax_local, entry->pages) == 0)) {
		ax_local->low_level_output(ndev, skb, entry);
	} else {
		skb_queue_tail(&ax_local->tx_q, skb);
		if (skb_queue_len (&ax_local->tx_q) >= TX_QUEUE_HIGH_WATER) {
			if (netif_msg_tx_queued (ax_local))
				printk ("%s: Too much TX packets in queue %d\n"
					, __FUNCTION__
					, skb_queue_len (&ax_local->tx_q));
			netif_stop_queue (ndev);
		}
	}

	spin_unlock_irqrestore (&ax_local->tx_busy_q.lock, flags);

	return NET_XMIT_SUCCESS;
}

#if (TX_MANUAL_DEQUEUE)
/*
 * ----------------------------------------------------------------------------
 * Function Name: ax88796c_handle_tx_manaul_dequeue
 * Purpose: Tx manual dequeue handleing
 * ----------------------------------------------------------------------------
 */
static void
ax88796c_handle_tx_manaul_dequeue (struct ax88796c_device *ax_local)
{
	void __iomem *ax_base = ax_local->membase;
	AX_WRITE (TFBFCR_MANU_ENTX | AX_READ (ax_base + P0_TFBFCR),
		  ax_base + P0_TFBFCR);
}
#endif /* TX_MANUAL_DEQUEUE */

/*
 * ----------------------------------------------------------------------------
 * Function Name: ax88796c_tx_pio_xmit
 * Purpose: Packet transmitted to AX88796C by PIO mode
 * ----------------------------------------------------------------------------
 */
static void  inline
ax88796c_tx_pio_xmit (struct ax88796c_device *ax_local, 
		      const unsigned char *buf, int byte_count)
{
	void __iomem *ax_base = ax_local->membase;
	unsigned int i, count;

	count = ((byte_count + 3) & DWORD_ALIGNMENT);
	for (i = 0; i < count; i += 2) {
		AX_WRITE (*((u16 *)(buf + i)), ax_base + DATA_PORT_ADDR );
	}
}

/*
 * ----------------------------------------------------------------------------
 * Function Name: ax88796c_tx_pio_start
 * Purpose: Packet transmission handling by using PIO
 * ----------------------------------------------------------------------------
 */
static int
ax88796c_tx_pio_start (struct net_device *ndev,
		struct sk_buff *skb, struct skb_data *entry)
{
	struct ax88796c_device *ax_local = netdev_priv (ndev);
	void __iomem *ax_base = ax_local->membase;

	/* Start tx transmission */
	AX_WRITE (TSNR_TXB_START | TSNR_PKT_CNT(1), ax_base + P0_TSNR);

	ax88796c_tx_pio_xmit (ax_local, (u8 *)&entry->txhdr,
				sizeof(entry->txhdr));
	ax88796c_tx_pio_xmit (ax_local, (skb->data - entry->offset),
				entry->dma_len);
	ax88796c_tx_pio_xmit (ax_local, (u8 *)&entry->tx_eop,
				sizeof(entry->tx_eop));

	/* If tx error interrupt is asserted */
	if ((ISR_TXERR & AX_READ (ax_base + P0_ISR)) != 0)
	{
		ax_local->stat.tx_dropped++;
		AX_WRITE ((TXNR_TXB_REINIT | AX_READ (ax_base+P0_TSNR)),
				ax_base+P0_TSNR);
		ax_local->seq_num = 0;	
	} else {
		ax_local->stat.tx_bytes += entry->len;
		ax_local->stat.tx_packets++;
	}

	dev_kfree_skb_any (skb);

	return NET_XMIT_SUCCESS;
}

#if (TX_DMA_MODE)
/*
 * ----------------------------------------------------------------------------
 * Function Name: ax88796c_tx_dma_start
 * Purpose: Packet transmission handling by using DMA
 * ----------------------------------------------------------------------------
 */
static int
ax88796c_tx_dma_start (struct net_device *ndev,
			struct sk_buff *skb, struct skb_data *entry)
{
	struct ax88796c_device *ax_local = netdev_priv (ndev);
	void __iomem *ax_base = ax_local->membase;

	__skb_queue_tail(&ax_local->tx_busy_q, skb);

	entry->ndev = ndev;
	entry->phy_addr = dma_map_single (NULL, (skb->data - entry->offset),
						entry->dma_len, DMA_TO_DEVICE);

	/* Start tx DMA */
	AX_WRITE ((TSNR_TXB_START | TSNR_PKT_CNT(1)), ax_base + P0_TSNR);

	ax88796c_tx_pio_xmit (ax_local, (u8 *)&entry->txhdr,
				sizeof(entry->txhdr));

	dma_start (entry->phy_addr, entry->dma_len / 2, 1);

	return NET_XMIT_SUCCESS;
}

/*
 * ----------------------------------------------------------------------------
 * Function Name: ax88796c_tx_dma_complete
 * Purpose: Tx DMA completion handling
 * ----------------------------------------------------------------------------
 */
static void ax88796c_tx_dma_complete(void *priv)
{
	struct net_device *ndev = (struct net_device *)priv;
	struct ax88796c_device *ax_local = netdev_priv (ndev);
	void __iomem *ax_base = ax_local->membase;
	struct sk_buff *skb;
	struct skb_data *entry;
	unsigned long flags;

	spin_lock_irqsave (&ax_local->tx_busy_q.lock, flags);

	skb = __skb_dequeue (&ax_local->tx_busy_q);
	if (!skb) {
		/* Should not happened */
		if (netif_msg_tx_err (ax_local))
			printk ("%s: No skb in tx_busy_q\n", __FUNCTION__);
		spin_unlock_irqrestore (&ax_local->tx_busy_q.lock, flags);
		return;
	}
	entry = (struct skb_data *) skb->cb;

	/* PIO write eop hdr */
	ax88796c_tx_pio_xmit (ax_local, (u8 *)&entry->tx_eop,
				sizeof(entry->tx_eop));

	/* If tx bridge is idle */
	if (((AX_READ ( ax_base + P0_TSNR ) & TXNR_TXB_IDLE) == 0) ||
	    ((ISR_TXERR & AX_READ (ax_base + P0_ISR) ) != 0)) {

		AX_WRITE (ISR_TXERR, ax_base + P0_ISR);

		ax_local->stat.tx_dropped++;

		if (netif_msg_tx_err (ax_local))
			printk ("%s: TX FIFO error, "
				"re-initialize the TX bridge\n",
				__FUNCTION__);

		/* Reinitial tx bridge */	
		AX_WRITE (TXNR_TXB_REINIT | AX_READ (ax_base+P0_TSNR),
				ax_base+P0_TSNR);
		ax_local->seq_num = 0;
	} else {
		ax_local->stat.tx_bytes += entry->len;
		ax_local->stat.tx_packets++;
	}

	/* Unmap DMA address */
	dma_unmap_single (NULL, entry->phy_addr,
				entry->dma_len, DMA_TO_DEVICE);
	dev_kfree_skb_any (skb);

	/* If there is any pending packet */ 	
	if (skb_queue_len (&ax_local->tx_q)) {
		struct skb_data *entry;
		skb = skb_peek(&ax_local->tx_q);

		entry = (struct skb_data *) skb->cb;
		if (ax88796c_check_free_pages (ax_local, entry->pages) == 0) {
			skb_unlink(skb, &ax_local->tx_q);
			ax_local->low_level_output(ndev, skb, entry);
		}
	}

	if (netif_queue_stopped (ndev) && 
	    skb_queue_len (&ax_local->tx_q) < TX_QUEUE_LOW_WATER) {
		netif_wake_queue (ndev);
	}

	spin_unlock_irqrestore (&ax_local->tx_busy_q.lock, flags);

	return;
}
#endif /* #if (TX_DMA_MODE) */

/*
 * ----------------------------------------------------------------------------
 * Function Name: ax_skb_return
 * Purpose: Send pkt to upper layer	
 * ----------------------------------------------------------------------------
 */
static void inline 
ax88796c_skb_return (struct ax88796c_device *ax_local,
		     struct sk_buff *skb, struct rx_header *rxhdr)
{
	int	status;

	do {
		if (!(ax_local->checksum & AX_RX_CHECKSUM)) {
			break;
		}

		/* checksum error bit is set */
		if ((rxhdr->flags & RX_HDR3_L3_ERR)
			|| (rxhdr->flags & RX_HDR3_L4_ERR)) {
			break;
		}

		if ((RX_HDR3_L3_PKT_TYPE(rxhdr->flags))
			|| (RX_HDR3_L4_PKT_TYPE(rxhdr->flags))) {
			skb->ip_summed = CHECKSUM_UNNECESSARY;
		}
	} while (0);

	skb->dev = ax_local->ndev;
	ax_local->stat.rx_packets++;
	ax_local->stat.rx_bytes += skb->len;

	skb->truesize = skb->len + sizeof(struct sk_buff);
	skb->protocol = eth_type_trans (skb, ax_local->ndev);
	
	status = netif_rx (skb);
	if (status != NET_RX_SUCCESS && netif_msg_rx_status (ax_local))
		printk ("netif_rx status %d\n", status);
}

/*
 * ----------------------------------------------------------------------------
 * Function Name: ax_rx_fixup
 * Purpose: Handle the received pkt buffer and send to kernel
 * ----------------------------------------------------------------------------
 */
static void
ax88696c_rx_fixup (struct ax88796c_device *ax_local,
		   struct sk_buff *rx_skb)
{
	u16 len;
	struct rx_header *rxhdr;

	/*
	 * Process first 6 bytes data as RX header
	 * The rx_skb->data point to the RX header
	 */
	rxhdr = (struct rx_header *) rx_skb->data;

	if (netif_msg_pktdata (ax_local)) {
		int len = be16_to_cpu (rxhdr->flags_len) & RX_HDR1_PKT_LEN;
		printk ("\n%s: Dump RX data, total len %d, packet len %d",
			__FUNCTION__, rx_skb->len, len);
		ax88796c_dump_rx_pkt (rx_skb);
	}

	/* Swap the RX header to the right order */
	be16_to_cpus (&rxhdr->flags_len);
	be16_to_cpus (&rxhdr->seq_lenbar);
	be16_to_cpus (&rxhdr->flags);

	/* Validate the RX header of this packet */
	if ((((short)rxhdr->flags_len) & RX_HDR1_PKT_LEN) !=
	    (~((short)rxhdr->seq_lenbar) & TX_HDR_SOP_PKTLEN)) {
		ax_local->stat.rx_frame_errors++;
		dev_kfree_skb_any (rx_skb);
		return;
	}

	/* Get packet length */
	len = rxhdr->flags_len & RX_HDR1_PKT_LEN;

	/* The packet lies after RX header */
	skb_pull (rx_skb, sizeof (*rxhdr));
	__pskb_trim (rx_skb, len);

	return ax88796c_skb_return (ax_local, rx_skb, rxhdr);

} /* End of ax_rx_fixup () */

/*
 * ----------------------------------------------------------------------------
 * Function Name: ax88796c_rx_pio 
 * Purpose: Packet reception handling by using PIO
 * ----------------------------------------------------------------------------
 */
static int ax88796c_rx_pio (struct net_device *ndev)
{
	struct ax88796c_device *ax_local = netdev_priv (ndev);
	void __iomem *ax_base = ax_local->membase;
	struct sk_buff *skb;
	u8 pkt_cnt;
	u16 w_count;
	u16 header;
	u16 len;
	int i;
	u8 loop_cnt = 20;

	while (--loop_cnt) {

		/* Read out the rx pkt counts and word counts */
		AX_WRITE (RTWCR_RX_LATCH | AX_READ (ax_base + P0_RTWCR),
				ax_base + P0_RTWCR);	
		pkt_cnt = AX_READ (ax_base + P0_RXBCR2) & RXBCR2_PKT_MASK;
		if (!pkt_cnt)
			break;

		/* Read out the rx header1 */
		header = AX_READ (ax_base + P0_RCPHR);
		len = header & RX_HDR1_PKT_LEN;

		if ((header & RX_HDR1_MII_ERR) || 
			(header & RX_HDR1_CRC_ERR)) {
			ax_local->stat.rx_crc_errors++;

			/* skip this packet */
			AX_WRITE (RXBCR1_RXB_DISCARD, ax_base + P0_RXBCR1);
			continue;
		}

		w_count = ((len + sizeof (struct rx_header) + 3)
						& DWORD_ALIGNMENT) >> 1;

		skb = dev_alloc_skb (w_count * 2);
		if (!skb) {
			if (netif_msg_rx_err (ax_local))
				printk ("%s: Couldn't allocate a sk_buff"
					" of size %d\n",
                                        ndev->name, w_count * 2);
			/* skip this packet */
			AX_WRITE (RXBCR1_RXB_DISCARD, ax_base + P0_RXBCR1);
			continue;
		}
		skb_put (skb, w_count * 2);

		/* Start rx PIO transmission */
		AX_WRITE (RXBCR1_RXB_START | w_count, ax_base + P0_RXBCR1);

		for (i = 0; i < skb->len; i += 2) {
			*((u16 *)(skb->data + i)) =
				AX_READ (ax_base + DATA_PORT_ADDR);
		}

		ax88696c_rx_fixup (ax_local, skb);
	}

	return 0;
}

#if (RX_DMA_MODE)
/*
 * ----------------------------------------------------------------------------
 * Function Name: ax88796c_rx
 * Purpose: Packet reception handling by using DMA
 * ----------------------------------------------------------------------------
 */
static int ax88796c_rx_dma (struct net_device *ndev)
{
 	struct ax88796c_device *ax_local = netdev_priv (ndev);
	void __iomem *ax_base = ax_local->membase;
	struct sk_buff *skb;
	u16 w_count, header, len;
	struct skb_data *entry;
	u16 pkt_cnt;

	AX_WRITE (RTWCR_RX_LATCH | AX_READ (ax_base + P0_RTWCR),
				ax_base + P0_RTWCR);	
	pkt_cnt = AX_READ (ax_base + P0_RXBCR2) & RXBCR2_PKT_MASK;
	if (!pkt_cnt) {
		return 0;
	}

	/* Read out the rx header1 */
	header = AX_READ (ax_base + P0_RCPHR);
	len = header & RX_HDR1_PKT_LEN;

	if ((header & RX_HDR1_MII_ERR) || 
		(header & RX_HDR1_CRC_ERR)) {
		ax_local->stat.rx_crc_errors++;

		/* skip this packet */
		AX_WRITE (RXBCR1_RXB_DISCARD, ax_base + P0_RXBCR1);
		return 0;
	}

	w_count = ((len + sizeof (struct rx_header) + 3)
					& DWORD_ALIGNMENT) >> 1;

	skb = dev_alloc_skb (w_count * 2);
	if (!skb) {
		if (netif_msg_rx_err (ax_local))
			printk ("%s: Couldn't allocate a sk_buff of size %d\n",
					ndev->name, w_count * 2);
		/* skip this packet */
		AX_WRITE (RXBCR1_RXB_DISCARD, ax_base + P0_RXBCR1);
		return 0;
	}

	skb_put (skb, w_count * 2);

	/* Enable RX bridge */
	AX_WRITE (RXBCR1_RXB_START | w_count, ax_base + P0_RXBCR1);

	entry = (struct skb_data *) skb->cb;
	entry->phy_addr = dma_map_single (NULL, skb->data, skb->len,
				DMA_FROM_DEVICE);
	__skb_queue_tail (&ax_local->rx_busy_q, skb);

	ax_local->rx_dmaing = 1;

	/* Start rx DMA transmission */
	dma_start (entry->phy_addr, w_count, 0);

	return 1;
}
#endif

#if (RX_DMA_MODE)
/*
 * ----------------------------------------------------------------------------
 * Function Name: ax88796c_rx_dma_complete
 * Purpose: Rx DMA completion handling
 * ----------------------------------------------------------------------------
 */
static void ax88796c_rx_dma_complete(void *dev_temp)
{
	struct net_device *ndev = (struct net_device* ) dev_temp;
	struct ax88796c_device *ax_local = netdev_priv (ndev);
	void __iomem *ax_base = ax_local->membase;
	struct sk_buff *skb;
	struct skb_data *entry;
	unsigned long flags;

	spin_lock_irqsave (&ax_local->isr_lock, flags);
	AX_WRITE (IMR_MASKALL, ax_base + P0_IMR);

	skb = __skb_dequeue (&ax_local->rx_busy_q);
	if (!skb) {
		if (netif_msg_rx_err (ax_local)) {
			/* Should not happened  */
			printk ("%s: No RX SKB in queue\n", __FUNCTION__);
		}
	}
	entry = (struct skb_data *) skb->cb;

	/* Unmap DMA address */
	dma_unmap_single(NULL, entry->phy_addr, skb->len, DMA_FROM_DEVICE);

	/* Check if rx bridge is idle */
	if ((AX_READ (ax_base + P0_RXBCR2) & RXBCR2_RXB_IDLE) == 0) {
		if (netif_msg_rx_err (ax_local) )
			printk("%s: Rx Bridge is not idle\n", ndev->name);
		AX_WRITE (RXBCR2_RXB_REINIT, ax_base + P0_RXBCR2);
		dev_kfree_skb_any (skb);
	} else {
		ax88696c_rx_fixup (ax_local, skb);
	}

	ax_local->rx_dmaing = 0;

	/* If the rx pkt in RX RAM */
	ax88796c_rx_dma (ndev);

	/* Enable RX interrupt  */
	if (ax_local->rx_dmaing) {
		AX_WRITE((IMR_DEFAULT | IMR_RXPKT), ax_base + P0_IMR);
	} else {
		AX_WRITE (ISR_RXPKT, ax_base + P0_ISR);
		AX_WRITE(IMR_DEFAULT, ax_base + P0_IMR);
	}

	spin_unlock_irqrestore (&ax_local->isr_lock, flags);
}
#endif

/*
 * ----------------------------------------------------------------------------
 * Function Name: ax88796c_watchdog
 * Purpose: Check media link status
 * ----------------------------------------------------------------------------
 */
static void ax88796c_watchdog (unsigned long arg)
{
	struct net_device *ndev = (struct net_device *)(arg);
    	struct ax88796c_device *ax_local = netdev_priv (ndev);
	void __iomem *ax_base = ax_local->membase;
	unsigned long time_to_chk = AX88796C_WATCHDOG_PERIOD;
	unsigned long flags;
	u16 phy_status;

	spin_lock_irqsave (&ax_local->isr_lock, flags);

	/* 
	 * Do a simple verification if AX88796C is in power saving mode.
	 * If AX88796C is in power saving mode, then we cannot
	 * switch to another page.
	 */
	if (ax_local->ps_level) {
		AX_SELECT_PAGE (PAGE1, ax_base + PG_PSR);
		if (AX_READ (ax_base + P0_BOR) == 0x1234) {
			if (netif_msg_timer (ax_local))
				printk ("%s: In power saving mode\n",
					ax_local->ndev->name);
			ax_local->w_state = chk_cable;
			goto out;
		}

		/*
		 * Be aware of AX88796C entering to power saving mode
		 * during watchdog operations.
		 */
		AX_SELECT_PAGE (PAGE0, ax_base + PG_PSR);
		ax88796c_set_power_saving (ndev, AX_PS_D0);
	}

	phy_status = AX_READ (ax_base + P0_PSCR);
	if (phy_status & PSCR_PHYLINK) {

		ax_local->w_state = ax_nop;
		time_to_chk = 0;

	} else if (!(phy_status & PSCR_PHYCOFF)) {
	/* The ethernet cable has been plugged */

		if (ax_local->w_state == chk_cable) {
			if (netif_msg_timer (ax_local))
				printk ("%s: Cable connected\n",
					ax_local->ndev->name);
			ax_local->w_state = chk_link;
			ax_local->w_ticks = 0;
		} else {
			if (netif_msg_timer (ax_local))
				printk ("%s: Check media status\n",
					ax_local->ndev->name);
			if (++ax_local->w_ticks == AX88796C_WATCHDOG_RESTART) {
				if (netif_msg_timer (ax_local))
					printk ("%s: Restart autoneg\n",
						ax_local->ndev->name);
				ax88796c_mdio_write_phy (ax_local->ndev,
					ax_local->mii.phy_id, MII_BMCR,
					BMCR_SPEED100 | BMCR_ANENABLE | BMCR_ANRESTART);
				ax_local->w_ticks = 0;
			}
		}
	} else {
		if (netif_msg_timer (ax_local))
			printk ("%s: Check cable status\n",
				ax_local->ndev->name);
		ax_local->w_state = chk_cable;
	}

	if (ax_local->ps_level)
		ax88796c_set_power_saving (ndev, ax_local->ps_level);
out:
	spin_unlock_irqrestore (&ax_local->isr_lock, flags);

	if (time_to_chk)
		mod_timer (&ax_local->watchdog, jiffies + time_to_chk);
}

/*
 * ----------------------------------------------------------------------------
 * Function Name: ax88796c_check_media
 * Purpose: Process media link status
 * ----------------------------------------------------------------------------
 */
static void ax88796c_check_media (struct ax88796c_device *ax_local)
{
	u16 bmsr, bmcr;

	if (netif_msg_hw (ax_local))
		ax88796c_dump_phy_regs (ax_local);

	bmsr = ax88796c_mdio_read_phy (ax_local->ndev,
			ax_local->mii.phy_id, MII_BMSR);

	if (!(bmsr & BMSR_LSTATUS) && netif_carrier_ok (ax_local->ndev)) {
		struct sk_buff *skb;
		unsigned long flags;
		
		netif_carrier_off (ax_local->ndev);
		if (netif_msg_link (ax_local))
			printk(KERN_INFO "%s: link down\n",
					ax_local->ndev->name);

		spin_lock_irqsave (&ax_local->tx_busy_q.lock, flags);

		/* If media link down, free all pending resource */
		while (!skb_queue_empty (&ax_local->tx_q)) {
			skb = skb_dequeue (&ax_local->tx_q);
			dev_kfree_skb_any (skb);
		}
		spin_unlock_irqrestore (&ax_local->tx_busy_q.lock, flags);

		ax_local->w_state = chk_cable;
		mod_timer (&ax_local->watchdog,
				jiffies + AX88796C_WATCHDOG_PERIOD);

	} else if((bmsr & BMSR_LSTATUS) && !netif_carrier_ok (ax_local->ndev)) {
		bmcr = ax88796c_mdio_read_phy (ax_local->ndev,
				ax_local->mii.phy_id, MII_BMCR);
		if (netif_msg_link (ax_local))
			printk(KERN_INFO "%s: link up, %sMbps, %s-duplex\n",
				ax_local->ndev->name,
				(bmcr & BMCR_SPEED100) ? "100" : "10",
				(bmcr & BMCR_FULLDPLX) ? "full" : "half");

		netif_carrier_on (ax_local->ndev);
	}

	return;
}

/* ----------------------------------------------------------------------------
 * Function Name: ax88796c_interrupt
 * Purpose: Interrupt handling
 * ----------------------------------------------------------------------------
 */
static irqreturn_t
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,28)
ax88796c_interrupt (int irq, void *dev_id)
#else
ax88796c_interrupt (int irq, void *dev_id, struct pt_regs * regs)
#endif
{
	struct net_device *ndev = dev_id;
    	struct ax88796c_device *ax_local = netdev_priv (ndev);
	void __iomem *ax_base = ax_local->membase;
	u16 interrupts;
	unsigned long flags;

	spin_lock_irqsave (&ax_local->isr_lock, flags);

	/* Mask all interrupts */
	AX_WRITE (IMR_MASKALL, ax_base + P0_IMR);

	/* Check and handle each interrupt event */
	interrupts = AX_READ (ax_base + P0_ISR);

	if (netif_msg_intr (ax_local)) {
		printk ("\n%s: Interrupt status 0x%x\n",
			__FUNCTION__ , interrupts);
	}

	if (interrupts) {

		/* RX interrupt */
		if (interrupts & ISR_RXPKT) {

			if (skb_queue_empty (&ax_local->rx_busy_q))
				ax_local->low_level_input (ndev);
		}

		/* Tx free page interrupt */
		if (interrupts & (ISR_TXPAGES)) {

			spin_lock (&ax_local->tx_busy_q.lock);

			/* If there is any pending packet */ 	
			if (!skb_queue_empty (&ax_local->tx_q) &&
			    skb_queue_empty (&ax_local->tx_busy_q)) {
				struct sk_buff *skb = skb_peek(&ax_local->tx_q);
				struct skb_data *entry =
						(struct skb_data *) skb->cb;

				if (ax88796c_check_free_pages (ax_local,
				    entry->pages) == 0) {
					__skb_unlink (skb, &ax_local->tx_q);
					ax_local->low_level_output (ndev,
						skb, entry);
				}
			}

			/* 
			 * In PIO mode, we only keep one packet in tx_q,
			 * if the packet has been sent correctly, then
			 * we can wake the transmit queue.
			 */
			if (netif_queue_stopped (ndev) && 
			    skb_queue_len (&ax_local->tx_q) < 
					   TX_QUEUE_LOW_WATER) {
				netif_wake_queue (ndev);
			}

			spin_unlock (&ax_local->tx_busy_q.lock);
		}

		if (interrupts & ISR_LINK) {
			ax88796c_check_media (ax_local);
		}

		/* Acknowledge all interrupts */
		AX_WRITE (interrupts, ax_base + P0_ISR);
	}

	/* If rx interrupt is asserted, do not unmake rx interrupt */
	if (ax_local->rx_dmaing) {
		AX_WRITE((IMR_DEFAULT | IMR_RXPKT), ax_base + P0_IMR);
	} else {
		AX_WRITE(IMR_DEFAULT, ax_base + P0_IMR);
	}

	spin_unlock_irqrestore (&ax_local->isr_lock, flags);

	return IRQ_HANDLED;
}

/*
 * ----------------------------------------------------------------------------
 * Function Name: ax88796c_get_stats
 * Purpose: Return statistics to the upper layer
 * ----------------------------------------------------------------------------
 */
static struct net_device_stats *ax88796c_get_stats (struct net_device *ndev)
{
	struct ax88796c_device *ax_local = netdev_priv (ndev);
	return &ax_local->stat;
}

/*
 * ----------------------------------------------------------------------------
 * Function Name: ax88796c_phy_init
 * Purpose: Initialize phy.
 * ----------------------------------------------------------------------------
 */
#ifndef ADVERTISE_PAUSE_CAP
#define ADVERTISE_PAUSE_CAP	0x400
#endif
static void ax88796c_phy_init (struct ax88796c_device *ax_local)
{
	u16 advertise = ADVERTISE_ALL | ADVERTISE_CSMA | ADVERTISE_PAUSE_CAP;
	unsigned long flags;
	void __iomem *ax_base = ax_local->membase;

	spin_lock_irqsave (&ax_local->isr_lock, flags);

	AX_SELECT_PAGE (PAGE2, ax_base + PG_PSR);

	/* Setup LED mode */
	AX_WRITE ((LCR_LED0_EN | LCR_LED0_DUPLEX | LCR_LED1_EN |
		   LCR_LED1_100MODE), ax_base + P2_LCR0);
	AX_WRITE ((AX_READ (ax_base + P2_LCR1) & LCR_LED2_MASK) |
		  LCR_LED2_EN | LCR_LED2_LINK, ax_base + P2_LCR1);

#if (AX88796C_8BIT_MODE)
	AX_WRITE(AX_READ(ax_base + P2_LCR1) | LCR_I_FULL_ACTIVE, 
						ax_base + P2_LCR1);
#endif

	AX_WRITE (POOLCR_PHYID(ax_local->mii.phy_id) | POOLCR_POLL_EN |
				POOLCR_POLL_FLOWCTRL | POOLCR_POLL_BMCR,
				ax_base + P2_POOLCR);

	AX_SELECT_PAGE(PAGE0, ax_base + PG_PSR);
	spin_unlock_irqrestore (&ax_local->isr_lock, flags);

	ax88796c_mdio_write (ax_local->ndev,
			ax_local->mii.phy_id, MII_ADVERTISE, advertise);

	ax88796c_mdio_write (ax_local->ndev, ax_local->mii.phy_id, MII_BMCR,
			     BMCR_SPEED100 | BMCR_ANENABLE | BMCR_ANRESTART);

	netif_carrier_off (ax_local->ndev);
	
	if (netif_msg_hw (ax_local))
		ax88796c_dump_phy_regs (ax_local);
}

/*
 * ----------------------------------------------------------------------------
 * Function Name: ax88796c_set_mac_addr
 * Purpose: Set up AX88796C MAC address
 * ----------------------------------------------------------------------------
 */
static void ax88796c_set_mac_addr (struct net_device *ndev)
{
	struct ax88796c_device *ax_local = netdev_priv (ndev);
	unsigned long flags;
	void __iomem *ax_base = ax_local->membase;

	spin_lock_irqsave (&ax_local->isr_lock, flags);
	/* Write deault MAC address into AX88796C */
	AX_SELECT_PAGE(PAGE3, ax_local->membase + PG_PSR);

	AX_WRITE(( (u16)(ndev->dev_addr[4] << MACASR_HIGH_BITS) |
			(u16)ndev->dev_addr[5] ),
			ax_local->membase + P3_MACASR0 );
	AX_WRITE(( (u16)(ndev->dev_addr[2] << MACASR_HIGH_BITS) |
			(u16)ndev->dev_addr[3] ),
			ax_local->membase + P3_MACASR1 );
	AX_WRITE(( (u16)(ndev->dev_addr[0] << MACASR_HIGH_BITS) |
			(u16)ndev->dev_addr[1] ),
			ax_local->membase + P3_MACASR2 );

	AX_SELECT_PAGE(PAGE0, ax_base + PG_PSR);
	spin_unlock_irqrestore (&ax_local->isr_lock, flags);
}

/*
 * ----------------------------------------------------------------------------
 * Function Name: ax88796c_set_mac_address
 * Purpose: Reset the whol chip
 * ----------------------------------------------------------------------------
 */
static int ax88796c_set_mac_address (struct net_device *ndev, void *p)
{
	struct ax88796c_device *ax_local = netdev_priv (ndev);
	struct sockaddr *addr = p;
	u8 power;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	power = ax88796c_check_power_state (ndev);

	memcpy(ndev->dev_addr, addr->sa_data, ndev->addr_len);

	ax88796c_set_mac_addr (ndev);

	if (power)
		ax88796c_set_power_saving (ndev, ax_local->ps_level);

	return 0;
}

/*
 * ----------------------------------------------------------------------------
 * Function Name: ax88796c_load_mac_addr
 * Purpose: Read MAC address from AX88796C
 * ----------------------------------------------------------------------------
 */
static int ax88796c_load_mac_addr (struct net_device *ndev)
{
	struct ax88796c_device *ax_local = netdev_priv (ndev);
	int i , j;	
	unsigned long flags;
	void __iomem *ax_base = ax_local->membase;

	spin_lock_irqsave (&ax_local->isr_lock, flags);
	/* Read the MAC address from AX88796C */
	AX_SELECT_PAGE(PAGE3, ax_local->membase + PG_PSR);
	for(i = 2, j = 0 ; i >= 0 ; i-- , j++ ){
		u16 temp;
		temp = AX_READ ( ax_local->membase + P3_MACASR(i));
		ndev->dev_addr[j*2+1]  = (u8)(temp & MACASR_LOWBYTE_MASK) ;
		ndev->dev_addr[j*2]  = (u8)(temp >> MACASR_HIGH_BITS);
	}
	AX_SELECT_PAGE(PAGE0, ax_base + PG_PSR);
	spin_unlock_irqrestore (&ax_local->isr_lock, flags);

	/* Support for no EEPROM */   
	if ((ndev->dev_addr[0] & 0x01) ||
		((ndev->dev_addr[0] == 0) && (ndev->dev_addr[1] == 0) &&
		 (ndev->dev_addr[2] == 0) && (ndev->dev_addr[3] == 0) &&
		 (ndev->dev_addr[4] == 0) && (ndev->dev_addr[5] == 0))) {

		/* no eeprom setup default mac address */
		ndev->dev_addr[0] = 0x00;
		ndev->dev_addr[1] = 0x12;
		ndev->dev_addr[2] = 0x34;
		ndev->dev_addr[3] = 0x56;
		ndev->dev_addr[4] = 0x78;
		ndev->dev_addr[5] = 0x9A;

		/* Mac on platform_data. Patch default with platform */
		if ((ax_local->plat->flags & AXFLG_MAC_FROMPLATFORM) &&
		    ax_local->plat->mac_addr)
			memcpy(ndev->dev_addr, ax_local->plat->mac_addr,
			    ETHER_ADDR_LEN);
	}

	if (netif_msg_probe (ax_local))
		printk (KERN_INFO "%s: MAC Address "
			"%2.2x-%2.2x-%2.2x-%2.2x-%2.2x-%2.2x\n",
			AX88796C_DRV_NAME,
			ndev->dev_addr[0], ndev->dev_addr[1],
			ndev->dev_addr[2], ndev->dev_addr[3],
			ndev->dev_addr[4], ndev->dev_addr[5]);
	return 0;
}

/*
 * ----------------------------------------------------------------------------
 * Function Name: ax88796c_init
 * Purpose: Initialize hardware and driver specific data.
 * ----------------------------------------------------------------------------
 */
static int ax88796c_init (struct ax88796c_device *ax_local)
{
	void __iomem *ax_base = ax_local->membase;

	/* Reset AX88796C */
	ax88796c_reset (ax_local);

	/*Reload EEPROM*/
	ax88796c_reload_eeprom (ax_local);

	/* AX88796C Page1 registers initialization */
	AX_SELECT_PAGE (PAGE1, ax_base + PG_PSR);

	/* Enable RX packet process */	
	AX_WRITE (RPPER_RXEN, ax_base + P1_RPPER);

	/* Disable stuffing */
	AX_WRITE (AX_READ (ax_base + P1_RXBSPCR) & ~RXBSPCR_STUF_ENABLE,
			ax_base + P1_RXBSPCR);

	ax88796c_set_mac_addr (ax_local->ndev);

	ax88796c_set_csums (ax_local->ndev);

	/* AX88796C Page0 registers initialization */
	AX_SELECT_PAGE (PAGE0, ax_base + PG_PSR);
	if (ax_local->plat_endian == PLAT_LITTLE_ENDIAN) {
		AX_WRITE ((FER_IPALM | FER_DCRC | FER_BSWAP | FER_RXEN |
				FER_TXEN | FER_INTLO | FER_IRQ_PULL),
				ax_base + P0_FER);
	} else {
		AX_WRITE ((FER_IPALM | FER_DCRC | FER_RXEN | FER_TXEN |
				FER_INTLO | FER_IRQ_PULL),
				ax_base + P0_FER);
	}

	ax88796c_set_power_saving (ax_local->ndev, ax_local->ps_level);

	ax88796c_phy_init (ax_local);

	return 0;
}

/*
 * ----------------------------------------------------------------------------
 * Function Name: ax88796c_free_skbuff
 * Purpose: Free allocated skb buffer
 * ----------------------------------------------------------------------------
 */
static void
ax88796c_free_skbuff (struct sk_buff_head *q)
{
	struct sk_buff *skb;

	/* Release skb_queue */
	while (q->qlen) {
		skb = skb_dequeue (q);
		dev_kfree_skb (skb);
	}
}

/*
 * ----------------------------------------------------------------------------
 * Function Name: ax88796c_
 * Purpose: Device open and initialization
 * ----------------------------------------------------------------------------
 */
static int 
ax88796c_open(struct net_device *ndev)
{
	struct ax88796c_device *ax_local = netdev_priv (ndev);
	void __iomem *ax_base = ax_local->membase;
	int ret = 0;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)
	unsigned long irq_flag = SA_SHIRQ | IORESOURCE_IRQ_LOWEDGE;
#else
	unsigned long irq_flag = IRQF_SHARED | IORESOURCE_IRQ_LOWEDGE;
#endif
	u8 power = ax88796c_check_power_state (ndev);

	/* Mask interrupt first */
	AX_WRITE (IMR_MASKALL, ax_base + P0_IMR);

	/* Initialize all the local variables */
	ax_local->seq_num = 0;

	ax88796c_init (ax_local);

	/* Request IRQ */
	ret = request_irq (ndev->irq, &ax88796c_interrupt,
				irq_flag, ndev->name, ndev);
	if (ret) {
		if (netif_msg_ifup (ax_local))
			printk (KERN_ERR
				"%s: unable to get IRQ %d (errno=%d)\n",
				ndev->name, ndev->irq, ret);
		return ret;
	}

	ret = ax88796c_plat_dma_init (
				ndev->base_addr,
				ax_local->tx_dma_complete,
				ax_local->rx_dma_complete,
				ndev);
	if (ret) {
		free_irq (ndev->irq, ndev);
		return ret;
	}

	if (netif_msg_hw (ax_local)) {
		printk ("Dump AX88796C registers (after initialaztion):\n");
		ax88796c_dump_regs (ax_local);
	}

	AX_SELECT_PAGE(PAGE0, ax_local->membase + PG_PSR);
	AX_WRITE (IMR_DEFAULT, ax_base + P0_IMR);

	netif_start_queue(ndev);

	if (power)
		ax88796c_set_power_saving (ax_local->ndev, ax_local->ps_level);

	init_timer (&ax_local->watchdog);
	ax_local->watchdog.function = &ax88796c_watchdog;
	ax_local->watchdog.expires = jiffies + AX88796C_WATCHDOG_PERIOD;
	ax_local->watchdog.data = (unsigned long) ndev;
	ax_local->w_state = chk_cable;
	ax_local->w_ticks = 0;

	add_timer (&ax_local->watchdog);

	return 0;
}

/*
 * ----------------------------------------------------------------------------
 * Function Name: ax88796c_close
 * Purpose: Device close
 * ----------------------------------------------------------------------------
 */
static int
ax88796c_close(struct net_device *ndev)
{
	struct ax88796c_device *ax_local = netdev_priv (ndev);
	unsigned long flags;

	(void) ax88796c_check_power_state (ndev);

	netif_stop_queue (ndev);

	del_timer_sync (&ax_local->watchdog);

	/*Release IRQ*/
 	spin_lock_irqsave (&ax_local->isr_lock, flags);
	free_irq (ndev->irq, ndev);
	spin_unlock_irqrestore (&ax_local->isr_lock, flags);

	ax88796c_plat_dma_release ();

	ax88796c_free_skbuff (&ax_local->tx_q);
	ax88796c_free_skbuff (&ax_local->tx_busy_q);
	ax88796c_free_skbuff (&ax_local->rx_busy_q);

	ax88796c_reset (ax_local);

	ax88796c_set_power_saving (ax_local->ndev, ax_local->ps_level);

	return 0;
}

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,28)
const struct net_device_ops ax88796c_netdev_ops = {
	.ndo_open		= ax88796c_open,
	.ndo_stop		= ax88796c_close,
	.ndo_start_xmit		= ax88796c_xmit,
	.ndo_get_stats		= ax88796c_get_stats,
	.ndo_set_multicast_list = ax88796c_set_multicast,
	.ndo_do_ioctl		= ax88796c_ioctl,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address 	= ax88796c_set_mac_address,
};
#endif

/* 
 * ----------------------------------------------------------------------------
 * Function Name: ax88796c_probe
 * Purpose: Device initialization.
 * ----------------------------------------------------------------------------
 */
static int __devinit 
ax88796c_probe (struct ax88796c_device *ax_local)
{
	struct net_device *ndev = ax_local->ndev;
	void __iomem *ax_base;
	int retval;
	u16 temp;

	ax_base = ax_local->membase;

	/* Wakeup AX88796C first */
	AX_WRITE (0xFF, ax_base + PG_HOST_WAKEUP);
	msleep (200);

	ax88796c_plat_init (bus_wide);

	/* Check endian */
	AX_SELECT_PAGE(PAGE0, ax_base + PG_PSR);
	temp = AX_READ (ax_base + P0_BOR);
	if (temp == 0x1234) {
		ax_local->plat_endian = PLAT_LITTLE_ENDIAN;
	} else {
		AX_WRITE (0xFFFF, ax_base + P0_BOR);
		ax_local->plat_endian = PLAT_BIG_ENDIAN;
	}

	/* Reset AX88796C */
	ax88796c_reset (ax_local);

	/*Reload EEPROM*/
	ax88796c_reload_eeprom (ax_local);

	if (netif_msg_hw (ax_local)) {
		printk ("Dump AX88796C registers:\n");
		ax88796c_dump_regs (ax_local);
	}

	if (netif_msg_probe (ax_local))
		printk (version);

	/* Set local arguments */
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,28)
	ndev->netdev_ops = &ax88796c_netdev_ops;
#else
	ndev->hard_start_xmit = ax88796c_xmit;
	ndev->get_stats	= ax88796c_get_stats;
	ndev->set_multicast_list = ax88796c_set_multicast;
	ndev->open = ax88796c_open;
	ndev->stop = ax88796c_close;
	ndev->do_ioctl = ax88796c_ioctl;
	ndev->set_mac_address 	= ax88796c_set_mac_address,
#endif
	ndev->ethtool_ops = &ax88796c_ethtool_ops;

	/* Initialize MII structure */
	ax_local->mii.dev = ndev;
	ax_local->mii.mdio_read = ax88796c_mdio_read;
	ax_local->mii.mdio_write = ax88796c_mdio_write;
	ax_local->mii.phy_id_mask = PHY_ID_MASK;
	ax_local->mii.reg_num_mask = REG_NUM_MASK;
	ax_local->mii.phy_id = PHY_ID;

	ax_local->checksum = AX_RX_CHECKSUM | AX_TX_CHECKSUM;

	/* Hook TX and RX function */
	ax_local->burst_len = DMA_BURST_LEN_2_WORD;	/* per access 16-bit */
	ax_local->low_level_output = ax88796c_tx_pio_start;
	ax_local->low_level_input = ax88796c_rx_pio;
#if (TX_DMA_MODE)
	ax_local->burst_len = DMA_BURST_LEN;		/* per access 32-bit */
	ax_local->low_level_output = ax88796c_tx_dma_start;
	ax_local->tx_dma_complete = ax88796c_tx_dma_complete;
#endif
#if (RX_DMA_MODE)
	ax_local->low_level_input = ax88796c_rx_dma;
	ax_local->rx_dma_complete = ax88796c_rx_dma_complete;
#endif

	ether_setup (ndev);

	/*Set Hardware check sum*/
	ndev->features |= NETIF_F_HW_CSUM;

	/* Initialize queue */
	skb_queue_head_init (&ax_local->tx_q);
	skb_queue_head_init (&ax_local->tx_busy_q);
	skb_queue_head_init (&ax_local->rx_busy_q);

	spin_lock_init (&ax_local->isr_lock);

	ax88796c_load_mac_addr (ndev);

	retval = register_netdev(ndev);
	if (retval == 0) {
		/* now, print out the card info, in a short format.. */
		if (netif_msg_probe (ax_local))
			printk("%s:  at %#lx IRQ %d\n",
				ndev->name,
				ndev->base_addr, ndev->irq);
	}

	return 0;
}


/*
 * ----------------------------------------------------------------------------
 * Function Name: ax88796c_drv_probe
 * Purpose: Driver get resource and probe
 * ----------------------------------------------------------------------------
 */
static int ax88796c_drv_probe(struct platform_device *pdev)
{
	struct ax88796c_device *ax_local;
	struct resource *res = NULL;
	void __iomem *addr;
	int ret;
	struct net_device *ndev;

	/* User can overwrite AX88796C's base address */
	if (!mem){

		res = platform_get_resource (pdev, IORESOURCE_MEM, 0);
		if (!res) {
			printk("%s: get no resource !\n", AX88796C_DRV_NAME);
			return -ENODEV;
		}

		mem = res->start;
	}

	/* Get the IRQ resource from kernel */
	if(!irq)
		irq = platform_get_irq(pdev, 0);

	/* Request the regions */
	if (!request_mem_region (mem, AX88796C_IO_EXTENT, "ax88796c")) {
		printk("%s: request_mem_region fail !", AX88796C_DRV_NAME);
		return -EBUSY;
	}

	addr = ioremap (mem, AX88796C_IO_EXTENT);
	if (!addr) {
		ret = -EBUSY;
		goto release_region;
	}

	ndev = alloc_etherdev (sizeof (struct ax88796c_device));
	if (!ndev) {
		printk("%s: could not allocate device.\n", AX88796C_DRV_NAME);
		ret = -ENOMEM;
		goto unmap_region;
	}

	SET_NETDEV_DEV(ndev, &pdev->dev);
	platform_set_drvdata (pdev, ndev);

	ndev->base_addr = mem;
	ndev->irq = irq;

	ax_local = netdev_priv (ndev);
	ax_local->membase = addr;
 	ax_local->ndev = ndev;
	ax_local->msg_enable =  msg_enable;

	/* setup platform data */
	ax_local->plat = pdev->dev.platform_data;

	if (ps_level == AX_PS_D1) {
		if (netif_msg_probe (ax_local))
			printk ("AX88796C: Enable power saving level 1\n");
	} else if (ps_level == AX_PS_D2) {
		if (netif_msg_probe (ax_local))
			printk ("AX88796C: Enable power saving level 2\n");
	} else {
		if (netif_msg_probe (ax_local))
			printk ("AX88796C: Power saving disabled\n");
		ps_level = 0;
	}
	ax_local->ps_level = ps_level;

	/* Enable Link Change and Magic Packet wakeup */
	ax_local->wol = WFCR_LINKCH | WFCR_MAGICP;

	ret = ax88796c_probe (ax_local);
	if (!ret)
		return 0;

	platform_set_drvdata (pdev, NULL);
	free_netdev (ndev);
unmap_region:
	iounmap (addr);
release_region:
	release_mem_region (mem, AX88796C_IO_EXTENT);

	printk("%s: not found (%d).\n", AX88796C_DRV_NAME, ret);
	return ret;
}

/* Power management handling functions */
/*
 * ----------------------------------------------------------------------------
 * Function Name: ax88796c_compute_crc16
 * Purpose: Caculate crc for wakeup pattern
 * ----------------------------------------------------------------------------
 */
static unsigned short ax88796c_compute_crc16 (unsigned char *Buffer, int Length)
{
	unsigned short i=0, j=0, k=0, crc=0;
	unsigned char c[16]={0}, txpd[8]={0}, first_xor=0;
	crc = 0xFFFF;
	for(j = 0; j < Length; j++) {
 
		for(i = 0; i < 16; i++) {
			c[i] = (crc >> i) & 0x0001;
		}
		for(i = 0; i < 8; i++) {
			txpd[i] = (Buffer[j] >> i) & 0x0001;
		}
		for(i = 0; i < 8; i++) {
			first_xor = c[15]^txpd[i];
			c[14] = c[14]^first_xor;
			c[1] = c[1]^first_xor;
			for(k = 15 ; k > 0; k--) {
				c[k] = c[k-1];
			}
			c[0] = first_xor;
		}
 
		crc = (c[15] << 15) | (c[14] << 14) | (c[13] << 13) |
		      (c[12] << 12) | (c[11] << 11) | (c[10] << 10) |
		      (c[9] <<  9) |  (c[8] <<  8) | (c[7] <<  7) |
		      (c[6] <<  6) |  (c[5] <<  5) | (c[4] <<  4) |
		      (c[3] <<  3) |  (c[2] <<  2) | (c[1] <<  1) | c[0];
	}

	return crc;
} /* End of ComputeCRC16() */

/*
 * ----------------------------------------------------------------------------
 * Function Name: ax88796c_suspend
 * Purpose: Device suspend handling function
 * ----------------------------------------------------------------------------
 */
static int
ax88796c_suspend(struct platform_device *p_dev, pm_message_t state)
{
	struct net_device *ndev = dev_get_drvdata(&(p_dev)->dev);
	struct ax88796c_device *ax_local = netdev_priv (ndev);
	void __iomem *ax_base = ax_local->membase;
	unsigned long flags;

	if (!ndev || !netif_running (ndev))
		return 0;

	netif_device_detach (ndev);
	netif_stop_queue (ndev);

	spin_lock_irqsave (&ax_local->isr_lock, flags);

	ax88796c_check_power_state (ndev);

	AX_WRITE (IMR_MASKALL, ax_base + P0_IMR);

	ax88796c_plat_dma_release ();

	ax88796c_free_skbuff (&ax_local->tx_q);
	ax88796c_free_skbuff (&ax_local->rx_busy_q);

	if (ax_local->wol) {

		AX_SELECT_PAGE(PAGE5, ax_base + PG_PSR);
		AX_WRITE (0, ax_base + P5_WFTR);

		if (ax_local->wol & WFCR_LINKCH) {	/* Link change */

			/* Disable wol power saving in link change mode */
			AX_SELECT_PAGE(PAGE0, ax_base + PG_PSR);
			AX_WRITE ((AX_READ (P0_PSCR + ax_base) & ~PSCR_WOLPS),
					P0_PSCR + ax_base);
			AX_SELECT_PAGE(PAGE5, ax_base + PG_PSR);

			if (netif_msg_wol (ax_local))
				printk ("Enable link change wakeup\n");
			AX_WRITE (WFTR_8192MS, ax_base + P5_WFTR);
		}
		if (ax_local->wol & WFCR_MAGICP) {	/* Magic packet */
			if (netif_msg_wol (ax_local))
				printk ("Enable magic packet wakeup\n");
		}
		if (ax_local->wol & WFCR_WAKEF) {	/* ARP wakeup */
			struct in_device *in_dev =
				(struct in_device *) ndev->ip_ptr;

			/* Get ip address from upper layer */
			if (in_dev != NULL) {
				struct in_ifaddr *ifa = in_dev->ifa_list;

				/* caculate crc and mask */
				if (ifa != NULL) {
					u8 packet[6];

					packet[0] = 0x08;
					packet[1] = 0x06;
					memcpy(&packet[2],
						&ifa->ifa_address, 4);
	
					/*
					 * The CRC will be caculated from
					 * 12. Last byte is the LSB of the
					 * ip address.
					 */
					AX_WRITE (0x000c, ax_base + P5_WF0BMR0);
					AX_WRITE (0xf000, ax_base + P5_WF0BMR1);
					AX_WRITE (((u16)packet[5] << 8 |
							(10 / 2)),
							ax_base + P5_WF0OBR);
					AX_WRITE (ax88796c_compute_crc16 (
							&packet[0], 6),
							ax_base + P5_WF0CR);
					AX_WRITE (WFCR03_F0_EN,
							ax_base + P5_WFCR03);
	
					if (netif_msg_wol (ax_local))
						printk ("Enable ARP wakeup\n");
				}
			}
		}

		AX_SELECT_PAGE(PAGE0, ax_base + PG_PSR);
		AX_WRITE (ax_local->wol | WFCR_WAKEUP | WFCR_PMEEN,
				ax_base + P0_WFCR);
	}

	spin_unlock_irqrestore (&ax_local->isr_lock, flags);

	return 0;
}

/*
 * ----------------------------------------------------------------------------
 * Function Name: ax88796c_resume
 * Purpose: Device resume handling function
 * ----------------------------------------------------------------------------
 */
static int
ax88796c_resume(struct platform_device *p_dev)
{
	struct net_device *ndev = dev_get_drvdata(&(p_dev)->dev);
	struct ax88796c_device *ax_local = netdev_priv (ndev);
	void __iomem *ax_base = ax_local->membase;
	int ret;
	u16 pme;

	/* Wakeup AX88796C first */
	AX_WRITE (0xFF, ax_base + PG_HOST_WAKEUP);
	msleep (200);

	pme = AX_READ (ax_base + P0_WFCR);
	if (pme & WFCR_LINKCHS) {
		if (netif_msg_wol (ax_local))
			printk ("Wakeuped from link change.\n");
	} else if (pme & WFCR_MAGICPS) {
		if (netif_msg_wol (ax_local))
			printk ("Wakeuped from magic packet.\n");
	} else if (pme & WFCR_WAKEFS) {
		if (netif_msg_wol (ax_local))
			printk ("Wakeuped from wakeup frame.\n");
	}

	netif_device_attach(ndev);

	/* Initialize all the local variables*/
	ax_local->seq_num = 0;

	ax88796c_init (ax_local);

	AX_SELECT_PAGE(PAGE0, ax_local->membase + PG_PSR);

	ret = ax88796c_plat_dma_init (ndev->base_addr,
				ax_local->tx_dma_complete,
				ax_local->rx_dma_complete,
				ndev);
	if (ret)
		return ret;

	netif_start_queue (ndev);

	AX_SELECT_PAGE(PAGE0, ax_local->membase + PG_PSR);
	AX_WRITE (IMR_DEFAULT, ax_base + P0_IMR);

	ax88796c_set_power_saving (ax_local->ndev, ax_local->ps_level);

	return 0;
}

/*
 * ----------------------------------------------------------------------------
 * Function Name: ax88796c_exit_module
 * Purpose: Driver Driver clean and exit
 * ----------------------------------------------------------------------------
 */
static int __devexit ax88796c_exit_module(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct ax88796c_device *ax_local = netdev_priv (ndev);
	void __iomem *ax_base = ax_local->membase;
	struct resource *res;

	platform_set_drvdata (pdev, NULL);
	unregister_netdev (ndev);
	iounmap (ax_base);

	if (mem) {
		release_mem_region (mem, AX88796C_IO_EXTENT);
	} else {
		res = platform_get_resource (pdev, IORESOURCE_MEM, 0);
		if (res)
			release_mem_region (res->start, AX88796C_IO_EXTENT);
	}
	free_netdev (ndev);

	return 1;
}

/*Fill platform driver information*/
static struct platform_driver ax88796c_driver = {
	.driver	= {
		.name    = "ax88796c",
		.owner	 = THIS_MODULE,
	},
	.probe   = ax88796c_drv_probe,
	.remove  = __devexit_p(ax88796c_exit_module),
	.suspend = ax88796c_suspend,
	.resume  = ax88796c_resume,
};

/*
 * ----------------------------------------------------------------------------
 * Function Name: ax88796c_init_mod
 * Purpose: Driver initialize
 * ----------------------------------------------------------------------------
 */
static int __init
ax88796c_init_mod(void)
{
	return platform_driver_register (&ax88796c_driver);
}

/*
 * ----------------------------------------------------------------------------
 * Function Name: ax88796c_exit_module
 * Purpose: Platform driver unregister 
 * ----------------------------------------------------------------------------
 */
static void __exit
ax88796c_cleanup(void)
{
	/*Unregister platform driver*/
	platform_driver_unregister (&ax88796c_driver);
}


module_init(ax88796c_init_mod);
module_exit(ax88796c_cleanup);

