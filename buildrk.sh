#!/bin/sh

export ARCH=arm64
# Name board(s) DTB file(s)
export DTBS="${DTBS} rockchip/rk3399pro-firefly.dtb"

if [ -z ${DEFCONFIG} ]; then
    # defconfig name
    export DEFCONFIG=rk3399pro_defconfig
fi
if [ -z ${CROSS_COMPILE} ]; then
    export CROSS_COMPILE=aarch64-linux-gnu-
fi
if [ -z ${ROOT_FS_PATH} ]; then
    export ROOT_FS_PATH=/cimc/root/aarch64-root
fi;
if [ -z ${TFTP_FS_PATH} ]; then
    export TFTP_FS_PATH=/cimc/exporttftp
fi

export DEF_TARGET="Image modules ${DTBS}"
export DEF_ARGS="-j `cat /proc/cpuinfo | grep processor | wc -l` ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} \
INSTALL_MOD_PATH=${ROOT_FS_PATH}"
export SUDO=sudo

if [ -z "$*" ]; then
    [ -f .config ] || make ${DEF_ARGS} ${DEFCONFIG}
    make ${DEF_ARGS} ${DEF_TARGET} || exit $?
    echo -n Cleanup old modules...
    ${SUDO} rm -rf ${ROOT_FS_PATH}/lib/modules/* && echo OK || echo FAIL
    ${SUDO} make ${DEF_ARGS} modules_install
    echo install kernel into rootfs and tftp
    ${SUDO} cp -f arch/${ARCH}/boot/Image ${ROOT_FS_PATH}/boot/Image
    ${SUDO} cp -f arch/${ARCH}/boot/Image ${TFTP_FS_PATH}/boot/Image
    for i in ${DTBS}; do
	${SUDO} cp -f arch/${ARCH}/boot/dts/${i} ${ROOT_FS_PATH}/boot/
	${SUDO} cp -f arch/${ARCH}/boot/dts/${i} ${TFTP_FS_PATH}/boot/
    done
else
    make ${DEF_ARGS} $*
fi
