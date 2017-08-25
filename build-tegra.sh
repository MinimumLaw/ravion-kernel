#!/bin/sh

export CROSS_COMPILE=armv7a-neon-linux-gnueabi-
export ARCH=arm
export ROOT_FS_PATH=/cimc/root/colibri-imx6
export TFTP_FS_PATH=/cimc/exporttftp
export DEF_TARGET="zImage modules tegra30-colibri-mcp.dtb"
export DEF_ARGS="-j3 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} \
INSTALL_MOD_PATH=${ROOT_FS_PATH}"
export SUDO=sudo

if [ -z "$*" ]; then
    [ -f .config ] || make ${DEF_ARGS} ravion_tegra_defconfig
    make ${DEF_ARGS} ${DEF_TARGET}
    echo -n Cleanup old modules...
    ${SUDO} rm -rf ${ROOT_FS_PATH}/lib/modules/* && echo OK || echo FAIL
    ${SUDO} make ${DEF_ARGS} modules_install
    echo install kernel into rootfs and tftp
    ${SUDO} cp -f arch/arm/boot/zImage				${ROOT_FS_PATH}/boot/zImage
    ${SUDO} cp -f arch/arm/boot/dts/tegra30-colibri-mcp.dtb	${ROOT_FS_PATH}/boot/tegra30.dtb
    ${SUDO} cp -f arch/arm/boot/zImage				${TFTP_FS_PATH}/boot/zImage
    ${SUDO} cp -f arch/arm/boot/dts/tegra30-colibri-mcp.dtb	${TFTP_FS_PATH}/boot/tegra30.dtb
else
    make ${DEF_ARGS} $*
fi
