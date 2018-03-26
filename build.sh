#!/bin/sh

export CROSS_COMPILE=armv7a-hardfloat-linux-gnueabi-
export ARCH=arm
export ROOT_FS_PATH=/cimc/root/colibri-imx6
export BR_OVERLAY_PATH=/cimc/build/__git__/buildroot/output/overlay
export TFTP_FS_PATH=/cimc/exporttftp
export DEF_TARGET="zImage modules"
export DEF_TARGET="${DEF_TARGET} imx6dl-colibri-mcp.dtb imx6dl-colibri-mcp-maximal.dtb"
export DEF_TARGET="${DEF_TARGET} imx6dl-colibri-cimc-lite.dtb imx6dl-colibri-cimc.dtb imx6dl-colibri-router.dtb"
export DEF_TARGET="${DEF_TARGET} imx6dl-colibri-mtu.dtb imx6dl-colibri-pkk-m7.dtb imx6dl-colibri-pkk-m10.dtb"
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
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-mcp.dtb ${ROOT_FS_PATH}/boot/mx6-mcp.dtb
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-mcp-maximal.dtb ${ROOT_FS_PATH}/boot/mx6-eval.dtb
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-cimc-lite.dtb ${ROOT_FS_PATH}/boot/mx6-cimc-lite.dtb
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-cimc.dtb ${ROOT_FS_PATH}/boot/mx6-cimc.dtb
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-router.dtb ${ROOT_FS_PATH}/boot/mx6-router.dtb
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-mtu.dtb ${ROOT_FS_PATH}/boot/mx6-mtu.dtb
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-pkk-m7.dtb ${ROOT_FS_PATH}/boot/mx6-pkk-m7.dtb
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-pkk-m10.dtb ${ROOT_FS_PATH}/boot/mx6-pkk-m10.dtb
    ${SUDO} cp -f arch/arm/boot/zImage ${TFTP_FS_PATH}/boot/zImage
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-mcp.dtb ${TFTP_FS_PATH}/boot/mx6-mcp.dtb
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-mcp-maximal.dtb ${TFTP_FS_PATH}/boot/mx6-eval.dtb
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-cimc-lite.dtb ${TFTP_FS_PATH}/boot/mx6-cimc-lite.dtb
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-cimc.dtb ${TFTP_FS_PATH}/boot/mx6-cimc.dtb
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-router.dtb ${TFTP_FS_PATH}/boot/mx6-router.dtb
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-mtu.dtb ${TFTP_FS_PATH}/boot/mx6-mtu.dtb
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-pkk-m7.dtb ${TFTP_FS_PATH}/boot/mx6-pkk-m7.dtb
    ${SUDO} cp -f arch/arm/boot/dts/imx6dl-colibri-pkk-m10.dtb ${TFTP_FS_PATH}/boot/mx6-pkk-m10.dtb
    if [ -d ${BR_OVERLAY_PATH} ]; then
	rm -rf \
	${BR_OVERLAY_PATH}/lib/modules/* \
	${BR_OVERLAY_PATH}/boot/zImage  \
	${BR_OVERLAY_PATH}/boot/mx6.dtb \
	${BR_OVERLAY_PATH}/boot/mx6-cimc-lite.dtb
	cp -rf ${ROOT_FS_PATH}/lib/modules/*		${BR_OVERLAY_PATH}/lib/modules/
	cp ${ROOT_FS_PATH}/boot/zImage			${BR_OVERLAY_PATH}/boot/
	cp ${ROOT_FS_PATH}/boot/mx6.dtb 		${BR_OVERLAY_PATH}/boot/
	cp ${ROOT_FS_PATH}/boot/mx6-cimc-lite.dtb 	${BR_OVERLAY_PATH}/boot/
	cp ${ROOT_FS_PATH}/boot/mx6-cimc.dtb 		${BR_OVERLAY_PATH}/boot/
	cp ${ROOT_FS_PATH}/boot/mx6-mtu.dtb 		${BR_OVERLAY_PATH}/boot/
    fi
else
    make ${DEF_ARGS} $*
fi
