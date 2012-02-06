cw1200_core-objs := \
		fwio.o \
		txrx.o \
		main.o \
		queue.o \
		hwio.o \
		bh.o \
		wsm.o \
		sta.o \
		ap.o \
		scan.o
cw1200_core-$(CONFIG_CW1200_DEBUGFS)	+= debug.o
cw1200_core-$(CONFIG_PM)		+= pm.o

obj-$(CONFIG_CW1200) += cw1200_core.o
obj-$(CONFIG_CW1200_SDIO) += cw1200_sdio.o
obj-$(CONFIG_CW1200_SPI)  += cw1200_spi.o
