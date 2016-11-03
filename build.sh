#!/bin/sh

export CROSS_COMPILE=armv7a-neon-linux-gnueabi-
export ARCH=arm
export ROOT_FS_PATH=/cimc/root/armv7a-neon/exports
export TFTP_FS_PATH=/cimc/exporttftp
export BR_OVERLAY_PATH=/cimc/build/__git__/buildroot/output/overlay
export DEF_TARGET="zImage modules imx6dl-colibri-mcp.dtb"
export DEF_ARGS="-j3 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} \
INSTALL_MOD_PATH=${ROOT_FS_PATH}"
export SUDO=sudo

if [ -z "$*" ]; then
    [ -f .config ] || make ${DEF_ARGS} ravion_imx6_defconfig
    make ${DEF_ARGS} ${DEF_TARGET}
    echo -n Cleanup old modules...
    ${SUDO} rm -rf ${ROOT_FS_PATH}/lib/modules/* && echo OK || echo FAIL
    ${SUDO} make ${DEF_ARGS} modules_install
    echo install kernel into rootfs and tftp
    ${SUDO} cp -f arch/arm/boot/zImage ${ROOT_FS_PATH}/boot/zImage
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-mcp.dtb ${ROOT_FS_PATH}/boot/mx6.dtb
    ${SUDO} cp -f arch/arm/boot/zImage ${TFTP_FS_PATH}/boot/zImage
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-mcp.dtb ${TFTP_FS_PATH}/boot/mx6.dtb
    if [ -d ${BR_OVERLAY_PATH} ]; then
	rm -rf \
	${BR_OVERLAY_PATH}/lib/modules/* \
	${BR_OVERLAY_PATH}/boot/zImage  \
	${BR_OVERLAY_PATH}/boot/mx6.dtb
	cp -rf ${ROOT_FS_PATH}/lib/modules/*	${BR_OVERLAY_PATH}/lib/modules/
	cp ${ROOT_FS_PATH}/boot/zImage		${BR_OVERLAY_PATH}/boot/
	cp ${ROOT_FS_PATH}/boot/mx6.dtb 	${BR_OVERLAY_PATH}/boot/
    fi
else
    make ${DEF_ARGS} $*
fi
