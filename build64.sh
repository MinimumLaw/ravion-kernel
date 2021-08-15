#!/bin/sh

export ARCH=arm64
# Name board(s) DTB file(s)
export DTBS="${DTBS} freescale/imx8mn-evk.dtb"

if [ -z ${DEFCONFIG} ]; then
    # defconfig name
    export DEFCONFIG=ravion_defconfig
fi
if [ -z ${CROSS_COMPILE} ]; then
    export CROSS_COMPILE=aarch64-linux-gnu-
fi
if [ -z ${ROOT_FS_PATH} ]; then
    export ROOT_FS_PATH=/cimc/root/colibri-imx6
fi;
if [ -z ${TFTP_FS_PATH} ]; then
    export TFTP_FS_PATH=/cimc/exporttftp
fi

export DEF_TARGET="kernel modules ${DTBS}"
export DEF_ARGS="-j `cat /proc/cpuinfo | grep processor | wc -l` ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} \
INSTALL_MOD_PATH=${ROOT_FS_PATH}"
export SUDO=sudo

if [ -z "$*" ]; then
    [ -f .config ] || make ${DEF_ARGS} ${DEFCONFIG}
    make ${DEF_ARGS} ${DEF_TARGET}
    echo -n Cleanup old modules...
    ${SUDO} rm -rf ${ROOT_FS_PATH}/lib/modules/* && echo OK || echo FAIL
    ${SUDO} make ${DEF_ARGS} modules_install
    echo install kernel into rootfs and tftp
    ${SUDO} cp -f arch/arm/boot/zImage ${ROOT_FS_PATH}/boot/zImage
    ${SUDO} cp -f arch/arm/boot/zImage ${TFTP_FS_PATH}/boot/zImage
    for i in ${DTBS}; do
	${SUDO} cp -f arch/arm/boot/dts/${i} ${ROOT_FS_PATH}/boot/
	${SUDO} cp -f arch/arm/boot/dts/${i} ${TFTP_FS_PATH}/boot/
    done
else
    make ${DEF_ARGS} $*
fi
