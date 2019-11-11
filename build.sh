#!/bin/sh

if [ -z $CROSS_COMPILE ]; then
    export CROSS_COMPILE=arm-linux-gnueabihf-
fi
export ARCH=arm
export ROOT_FS_PATH=/cimc/root/colibri-imx6
export TFTP_FS_PATH=/cimc/exporttftp
export DEF_TARGET="zImage modules"
# Toradex Colibri
export DEF_TARGET="${DEF_TARGET} imx6dl-colibri-eval-v3.dtb imx6dl-colibri-test.dtb"
export DEF_TARGET="${DEF_TARGET} imx6dl-colibri-cimc-lite.dtb imx6dl-colibri-cimc.dtb imx6dl-colibri-router.dtb"
export DEF_TARGET="${DEF_TARGET} imx6dl-colibri-mtu.dtb imx6dl-colibri-pkk-m7.dtb imx6dl-colibri-pkk-m10.dtb"
export DEF_TARGET="${DEF_TARGET} imx6dl-colibri-stend-main.dtb imx6dl-colibri-stend-tablet.dtb"
export DEF_TARGET="${DEF_TARGET} imx6dl-colibri-stend-testbench.dtb"
# Ravion200 quad
export DEF_TARGET="${DEF_TARGET} imx6qp-ravion-test.dtb imx6qp-ravion-evaltest.dtb"
export DEF_TARGET="${DEF_TARGET} imx6qp-ravion-cimc-lite.dtb imx6qp-ravion-cimc.dtb  imx6qp-ravion-router.dtb"
export DEF_TARGET="${DEF_TARGET} imx6qp-ravion-mtu.dtb imx6qp-ravion-pkk-m7.dtb imx6qp-ravion-pkk-m10.dtb"
export DEF_TARGET="${DEF_TARGET} imx6qp-ravion-pkk-m7-i.dtb imx6qp-ravion-pkk-m10-i.dtb imx6qp-ravion-cimc-i.dtb"
export DEF_TARGET="${DEF_TARGET} imx6qp-ravion-stend-testbench.dtb imx6qp-ravion-eval-v3.dtb imx6dl-ravion-eval-v3.dtb"
# Ravion200 dual
export DEF_TARGET="${DEF_TARGET} imx6dl-ravion-cimc-lite.dtb imx6dl-ravion-cimc.dtb  imx6dl-ravion-router.dtb"
export DEF_TARGET="${DEF_TARGET} imx6dl-ravion-mtu.dtb imx6dl-ravion-pkk-m7.dtb imx6dl-ravion-pkk-m10.dtb"
export DEF_TARGET="${DEF_TARGET} imx6dl-ravion-pkk-m7-i.dtb imx6dl-ravion-pkk-m10-i.dtb imx6dl-ravion-cimc-i.dtb"
export DEF_TARGET="${DEF_TARGET} imx6dl-ravion-stend-testbench.dtb imx6dl-ravion-eval-v3.dtb"
export DEF_ARGS="-j `cat /proc/cpuinfo | grep processor | wc -l` ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} \
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
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-cimc-lite.dtb ${ROOT_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-cimc.dtb ${ROOT_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-test.dtb ${ROOT_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-eval-v3.dtb ${ROOT_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-mtu.dtb ${ROOT_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-pkk-m10.dtb ${ROOT_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-pkk-m7.dtb ${ROOT_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-router.dtb ${ROOT_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-stend-main.dtb ${ROOT_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-stend-tablet.dtb ${ROOT_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-stend-testbench.dtb ${ROOT_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6qp-ravion-cimc-i.dtb ${ROOT_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6qp-ravion-cimc-lite.dtb ${ROOT_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6qp-ravion-cimc.dtb ${ROOT_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6qp-ravion-mtu.dtb ${ROOT_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6qp-ravion-pkk-m10.dtb ${ROOT_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6qp-ravion-pkk-m10-i.dtb ${ROOT_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6qp-ravion-pkk-m7.dtb ${ROOT_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6qp-ravion-pkk-m7-i.dtb ${ROOT_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6qp-ravion-router.dtb ${ROOT_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6qp-ravion-evaltest.dtb ${ROOT_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6qp-ravion-test.dtb ${ROOT_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6qp-ravion-stend-testbench.dtb ${ROOT_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6qp-ravion-eval-v3.dtb ${ROOT_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-ravion-cimc-i.dtb ${ROOT_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-ravion-cimc-lite.dtb ${ROOT_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-ravion-cimc.dtb ${ROOT_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-ravion-mtu.dtb ${ROOT_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-ravion-pkk-m10.dtb ${ROOT_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-ravion-pkk-m10-i.dtb ${ROOT_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-ravion-pkk-m7.dtb ${ROOT_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-ravion-pkk-m7-i.dtb ${ROOT_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-ravion-router.dtb ${ROOT_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-ravion-stend-testbench.dtb ${ROOT_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-ravion-eval-v3.dtb ${ROOT_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/zImage ${TFTP_FS_PATH}/boot/zImage
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-cimc-lite.dtb ${TFTP_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-cimc.dtb ${TFTP_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-test.dtb ${TFTP_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-eval-v3.dtb ${TFTP_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-mtu.dtb ${TFTP_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-pkk-m10.dtb ${TFTP_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-pkk-m7.dtb ${TFTP_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-router.dtb ${TFTP_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-stend-main.dtb ${TFTP_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-stend-tablet.dtb ${TFTP_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-stend-testbench.dtb ${TFTP_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6qp-ravion-cimc-i.dtb ${TFTP_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6qp-ravion-cimc-lite.dtb ${TFTP_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6qp-ravion-cimc.dtb ${TFTP_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6qp-ravion-mtu.dtb ${TFTP_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6qp-ravion-pkk-m10.dtb ${TFTP_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6qp-ravion-pkk-m10-i.dtb ${TFTP_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6qp-ravion-pkk-m7.dtb ${TFTP_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6qp-ravion-pkk-m7-i.dtb ${TFTP_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6qp-ravion-router.dtb ${TFTP_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6qp-ravion-evaltest.dtb ${TFTP_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6qp-ravion-test.dtb ${TFTP_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6qp-ravion-stend-testbench.dtb ${TFTP_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6qp-ravion-eval-v3.dtb ${TFTP_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-ravion-cimc-i.dtb ${TFTP_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-ravion-cimc-lite.dtb ${TFTP_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-ravion-cimc.dtb ${TFTP_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-ravion-mtu.dtb ${TFTP_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-ravion-pkk-m10.dtb ${TFTP_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-ravion-pkk-m10-i.dtb ${TFTP_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-ravion-pkk-m7.dtb ${TFTP_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-ravion-pkk-m7-i.dtb ${TFTP_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-ravion-router.dtb ${TFTP_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-ravion-stend-testbench.dtb ${TFTP_FS_PATH}/boot/
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-ravion-eval-v3.dtb ${TFTP_FS_PATH}/boot/
else
    make ${DEF_ARGS} $*
fi
