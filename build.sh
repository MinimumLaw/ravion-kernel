#!/bin/sh

export ARCH=arm
# UTSVU board (I.MX511)
export DTBS="${DTBS} nxp/imx/imx51-ravion-utsvu.dtb"
# Ravion200 quad (Ravion v2)
export DTBS="${DTBS} nxp/imx/imx6qp-ravion-kitsbimx6.dtb	nxp/imx/imx6qp-ravion-cimc.dtb"
export DTBS="${DTBS} nxp/imx/imx6qp-ravion-mtu.dtb		nxp/imx/imx6qp-ravion-router.dtb"
export DTBS="${DTBS} nxp/imx/imx6qp-ravion-pkk-m7.dtb		nxp/imx/imx6qp-ravion-pkk-m10.dtb"
export DTBS="${DTBS} nxp/imx/imx6qp-ravion-cimc215.dtb		nxp/imx/imx6qp-ravion-bm.dtb"
export DTBS="${DTBS} nxp/imx/imx6qp-ravion-cimc-no-video.dtb	nxp/imx/imx6qp-ravion-avikon.dtb"
export DTBS="${DTBS} nxp/imx/imx6qp-ravion-cimc-light.dtb	nxp/imx/imx6qp-ravion-pou.dtb"

if [ -z ${DEFCONFIG} ]; then
    export DEFCONFIG=ravion_imx6_defconfig
fi
if [ -z ${CROSS_COMPILE} ]; then
    export CROSS_COMPILE=arm-linux-gnueabihf-
fi
if [ -z ${ROOT_FS_PATH} ]; then
    export ROOT_FS_PATH=/cimc/root/colibri-imx6
fi;
if [ -z ${TFTP_FS_PATH} ]; then
    export TFTP_FS_PATH=/cimc/exporttftp
fi

export DEF_TARGET="zImage modules ${DTBS}"
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
    ${SUDO} cp -f arch/${ARCH}/boot/zImage ${ROOT_FS_PATH}/boot/zImage
    ${SUDO} cp -f arch/${ARCH}/boot/zImage ${TFTP_FS_PATH}/boot/zImage
    for i in ${DTBS}; do
	${SUDO} cp -f arch/${ARCH}/boot/dts/${i} ${ROOT_FS_PATH}/boot/
	${SUDO} cp -f arch/${ARCH}/boot/dts/${i} ${TFTP_FS_PATH}/boot/
    done
else
    make ${DEF_ARGS} $*
fi
