#!/bin/sh

export ARCH=arm64

if [ -z ${DEFCONFIG} ]; then
    # defconfig name
    export DEFCONFIG=tcl_acpi_standard_defconfig
fi
if [ -z ${CROSS_COMPILE} ]; then
    export CROSS_COMPILE=aarch64-linux-gnu-
fi
if [ -z ${ROOT_FS_PATH} ]; then
    export ROOT_FS_PATH=/cimc/root/tcl-b220g
fi;

export DEF_TARGET="Image modules"
export DEF_ARGS="-j `cat /proc/cpuinfo | grep processor | wc -l` ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} \
INSTALL_MOD_PATH=${ROOT_FS_PATH} INSTALL_PATH=${ROOT_FS_PATH}/boot"
export SUDO=sudo

if [ -z "$*" ]; then
    [ -f .config ] || make ${DEF_ARGS} ${DEFCONFIG}
    make ${DEF_ARGS} ${DEF_TARGET} || exit $?
    echo -n Cleanup old modules...
    ${SUDO} rm -rf ${ROOT_FS_PATH}/lib/modules/* && echo OK || echo FAIL
    ${SUDO} make ${DEF_ARGS} modules_install install
else
    make ${DEF_ARGS} $*
fi
