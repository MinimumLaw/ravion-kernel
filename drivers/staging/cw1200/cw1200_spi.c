/*
 * Mac80211 SPI driver for ST-Ericsson CW1200 device
 *
 * Author: Alex A. Muhaylov <MinimumLaw@Rambler.Ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/spi/spi.h>
#include <linux/gpio.h>

#include "cw1200.h"
#include "sbus.h"
#include "cw1200_plat.h"

/* MACROS */
#define SDIO_TO_SPI_ADDR(addr) ((addr & 0x1f)>>2)
#define SET_WRITE 0x7FFF
#define SET_READ 0x8000

struct sbus_priv {
	struct spi_device	*spi;
	struct cw1200_common	*core;
	spinlock_t		 lock;
	sbus_irq_handler	irq_handler;
	void			*irq_priv;
	const struct cw1200_platform_data *pdata;
};

static int cw1200_spi_memcpy_fromio(struct sbus_priv *self,
		unsigned int address, void *rxdata, int len)
{
	struct spi_device *spi = self->spi;
	uint32_t addr = SDIO_TO_SPI_ADDR(address);
	int rval;
	uint16_t regaddr;

	struct spi_message	m;
	struct spi_transfer	t_addr = {
		.tx_buf		= &regaddr,
		.len		= sizeof(regaddr),
	};
	struct spi_transfer	t_msg = {
		.rx_buf		= rxdata,
		.len		= len,
	};

	regaddr = (addr)<<12;
	regaddr |= SET_READ;
	regaddr |= (len>>1);
	regaddr = cpu_to_le16(regaddr);

	spi_message_init(&m);

	spi_message_add_tail(&t_addr, &m);
	spi_message_add_tail(&t_msg, &m);
	rval = spi_sync(spi, &m);

	return rval;
}

static int cw1200_spi_memcpy_toio(struct sbus_priv *self,
		unsigned int address, const void *txdata, int len)
{
	struct spi_device *spi = self->spi;
	uint32_t addr = SDIO_TO_SPI_ADDR(address);
	int rval;
	uint16_t regaddr;
	struct spi_transfer	t_addr = {
		.tx_buf		= &regaddr,
		.len		= sizeof(regaddr),
	};
	struct spi_transfer	t_msg = {
		.tx_buf		= txdata,
		.len		= len,
	};
	struct spi_message	m;

	regaddr = (addr)<<12;
	regaddr &= SET_WRITE;
	regaddr |= (len>>1);
	regaddr = cpu_to_le16(regaddr);

	spi_message_init(&m);
	spi_message_add_tail(&t_addr, &m);
	spi_message_add_tail(&t_msg, &m);
	rval = spi_sync(spi, &m);

	return rval;
}

static irqreturn_t cw1200_spi_irq_handler(int irq, void *dev_id)
{
	struct sbus_priv *self = dev_id;

	BUG_ON(!self);
	if (self->irq_handler)
		self->irq_handler(self->irq_priv);
	return IRQ_HANDLED;
}


static int cw1200_spi_irq_subscribe(struct sbus_priv *self,
	sbus_irq_handler handler, void *priv)
{
	int ret;
	unsigned long flags;

	if (!handler)
		return -EINVAL;

	spin_lock_irqsave(&self->lock, flags);
	self->irq_priv = priv;
	self->irq_handler = handler;
	spin_unlock_irqrestore(&self->lock, flags);

	dev_dbg(&self->spi->dev, "SW IRQ subscribe\n");

	ret = request_any_context_irq(self->pdata->irq->start,
	cw1200_spi_irq_handler, IRQF_TRIGGER_RISING,
	self->pdata->irq->name, self);

	WARN_ON(ret < 0);

	return ret;
}

static int cw1200_spi_irq_unsubscribe(struct sbus_priv *self)
{
	int ret = 0;
	unsigned long flags;
	const struct resource *irq = self->pdata->irq;

	WARN_ON(!self->irq_handler);
	if (!self->irq_handler)
		return 0;

	dev_dbg(&self->spi->dev, "SW IRQ unsubscribe\n");
	free_irq(irq->start, self);

	spin_lock_irqsave(&self->lock, flags);
	self->irq_priv = NULL;
	self->irq_handler = NULL;
	spin_unlock_irqrestore(&self->lock, flags);

	return ret;
}

static int cw1200_spi_reset(struct sbus_priv *self)
{
	const struct resource *reset = self->pdata->reset;

	if (reset) {
		dev_err(&self->spi->dev, "Reset cw1200_spi module\n");
		gpio_direction_output(reset->start, 0); /* active low reset*/
		msleep(100); /* hold reset */
		gpio_direction_output(reset->start, 1); /* return to active mode */
		msleep(100); /* time to activate chip */
	};

	return 0;
}

static int cw1200_spi_pm(struct sbus_priv *self, bool  suspend)
{
	return irq_set_irq_wake(self->pdata->irq->start, suspend);
}

static struct sbus_ops cw1200_sbus_ops = {
	.sbus_memcpy_fromio	= cw1200_spi_memcpy_fromio,
	.sbus_memcpy_toio	= cw1200_spi_memcpy_toio,
	.irq_subscribe		= cw1200_spi_irq_subscribe,
	.irq_unsubscribe	= cw1200_spi_irq_unsubscribe,
	.reset			= cw1200_spi_reset,
	.power_mgmt		= cw1200_spi_pm,
};

static int __devinit cw1200_spi_probe(struct spi_device *card)
{
	struct sbus_priv *self;
	int	status;

	dev_info(&card->dev, "CW1200 spi probe (CS:%d, Mode:%d, Bits:%d)\n",
	card->chip_select, card->mode, card->bits_per_word);

	if (!card->max_speed_hz || card->max_speed_hz > 52000000) {
		dev_info(&card->dev, "Limit SPI port speed to 52MHz\n");
		card->max_speed_hz = 52000000;
	};

	if (card->bits_per_word != 16) {
		dev_info(&card->dev, "SPI for this driver require 16 mode support. "
		"Try to set 16 bit mode now!\n");
		card->bits_per_word = 16;
	};

	if (card->mode != SPI_MODE_3) {
		dev_info(&card->dev, "SPI for this driver require SPI mode 3 support. "
		"Try to set mode 3 now!\n");
		card->mode = SPI_MODE_3;
	};
	status = spi_setup(card);

	if (status) {
		dev_err(&card->dev, "spi_setup() failed with err=%d!\n",
			status);
		return status;
	};

	self = kzalloc(sizeof(*self), GFP_KERNEL);
	if (!self) {
		dev_err(&card->dev, "Can't allocate sbus_priv.\n");
		return -ENOMEM;
	};

	dev_set_drvdata(&card->dev, self);

	spin_lock_init(&self->lock);

	self->spi = card;
	self->pdata = card->dev.platform_data;

	if (!self->pdata) {
		dev_err(&card->dev, "Can't get platform specific data.\n");
		return -ENODEV;
	};

	if (!self->pdata->irq) {
		dev_err(&card->dev, "SPI mode requre IRQ resources "
			"on platform  data.\n");
		return -ENODEV;
	};

	if (self->pdata->clk_ctrl) {
		status = self->pdata->clk_ctrl(self->pdata, true);
		if (status)
			goto err_clkctrl;
	};

	if (self->pdata->power_ctrl) {
		status = self->pdata->power_ctrl(self->pdata, true);
		if (status) 
			goto err_pwrctrl;
	};

	if (self->pdata->reset) {
		/* Init reset pin to active (high) state */
		status = gpio_request_one(
			self->pdata->reset->start,
			GPIOF_OUT_INIT_HIGH,
			self->pdata->reset->name);
		if (status) {
			dev_err(&self->spi->dev, "Error request gpio %d:"
			" for cw1200 reset (code %d)\n",
			self->pdata->reset->start, status);
			goto err_rstctrl;
		};
		cw1200_spi_reset(self);
	};

	status = cw1200_core_probe(&cw1200_sbus_ops,
		self, &card->dev, &self->core);

	if (status) {
		dev_err(&card->dev, "Error connecting SPI bus to CW1200\n");
		goto err_wlnctrl;
	};

	return status;

err_wlnctrl:
	cw1200_spi_reset(self);
	if (self->pdata->reset)
		gpio_free(self->pdata->reset->start);
err_rstctrl:
	if (self->pdata->power_ctrl)
		self->pdata->power_ctrl(self->pdata, false);
err_pwrctrl:
	if (self->pdata->clk_ctrl)
		self->pdata->clk_ctrl(self->pdata, false);
err_clkctrl:
	if (self)
		kfree(self);
	return status;
}

static int __devexit cw1200_spi_remove(struct spi_device *card)
{
	struct sbus_priv *self = dev_get_drvdata(&card->dev);

	if (self) {
		if (self->core) {
			cw1200_core_release(self->core);
			self->core = NULL;
		};
		if (self->pdata->power_ctrl)
			self->pdata->power_ctrl(self->pdata, false);
		if (self->pdata->clk_ctrl)
			self->pdata->clk_ctrl(self->pdata, false);
		if (self->pdata->reset) {
			gpio_direction_input(self->pdata->reset->start);
			gpio_free(self->pdata->reset->start);
		}
		kfree(self);
	}

	return 0;
}

static struct spi_driver cw1200_spi_driver = {
	.driver = {
	.name	= "cw1200_spi",
	.bus	= &spi_bus_type,
	.owner	= THIS_MODULE,
	},
	.probe	= cw1200_spi_probe,
	.remove	= __devexit_p(cw1200_spi_remove),
};

static int __init cw1200_spi_init(void)
{
	int ret;

	ret = spi_register_driver(&cw1200_spi_driver);
	if (ret < 0)
		printk(KERN_ERR "Fail to register CW1200 spi driver: %d", ret);

	return ret;
}

static void __exit cw1200_spi_exit(void)
{
	spi_unregister_driver(&cw1200_spi_driver);
}

module_init(cw1200_spi_init);
module_exit(cw1200_spi_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alex A. Mihaylov <minimumlaw@rambler.ru>");
MODULE_ALIAS("spi:cw1200");
