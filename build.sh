#!/bin/bash

export ARCH=arm
export CROSS_COMPILE=armv7a-neon-linux-gnueabi-
# export CROSS_COMPILE=armv7a-hardfloat-linux-gnueabi-
# export CROSS_COMPILE=arm-fsl-linux-gnueabi-
export LOADADDR=0x90800000
export STARTADDR=0x90800040

if [ -z ${1} ]; then
    if [ -f .config ]; then
	make UIMAGE_LOADADDR=${LOADADDR} UIMAGE_ENTRYADDR=${STARTADDR} -j5 imx51-utsvu.dtb uImage
    else
	make clean distclean
	make utsvu_defconfig
	make UIMAGE_LOADADDR=${LOADADDR} UIMAGE_ENTRYADDR=${STARTADDR} -j5 imx51-utsvu.dtb uImage
    fi
    if [ -f arch/arm/boot/uImage ]; then
	echo Update TFTP kernel
	cp -f arch/arm/boot/uImage /cimc/exporttftp/
	md5sum arch/arm/boot/uImage /cimc/exporttftp/uImage
    fi
    if [ -f arch/arm/boot/dts/imx51-utsvu.dtb ]; then
	echo Update TFTP device tree
	cp -f arch/arm/boot/dts/imx51-utsvu.dtb /cimc/exporttftp/
	md5sum arch/arm/boot/dts/imx51-utsvu.dtb /cimc/exporttftp/imx51-utsvu.dtb
    fi
else
    make $*
fi

