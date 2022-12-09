// SPDX-License-Identifier: GPL-2.0+
/*
 * Pinctrl driver for HSPERIPH subsystem of MCom-03 SoC.
 * Copyright 2021-2022 RnD Center "ELVEES", JSC
 */

#include <linux/of.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/of_device.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include "../core.h"
#include "../pinctrl-utils.h"

/*
 * MCom-03 custom pinconf parameters
 */
#define PIN_CONFIG_MCOM03_PAD_ENABLE (PIN_CONFIG_END + 1)

const struct pinconf_generic_params mcom03_hsperiph_custom_params[] = {
	{ "elvees,pad-enable", PIN_CONFIG_MCOM03_PAD_ENABLE, 1 },
	{ "elvees,pad-disable", PIN_CONFIG_MCOM03_PAD_ENABLE, 0 },
};

#ifdef CONFIG_DEBUG_FS
static const struct
pin_config_item mcom03_hsperiph_conf_items[] = {
	PCONFDUMP(PIN_CONFIG_MCOM03_PAD_ENABLE, "enabled", NULL, false)
};
#endif

#define MCOM03_BIAS_BUS_HOLD		BIT(PIN_CONFIG_BIAS_BUS_HOLD)
#define MCOM03_BIAS_PULL_DOWN		BIT(PIN_CONFIG_BIAS_PULL_DOWN)
#define MCOM03_BIAS_PULL_UP		BIT(PIN_CONFIG_BIAS_PULL_UP)
#define MCOM03_DRIVE_OPEN_DRAIN		BIT(PIN_CONFIG_DRIVE_OPEN_DRAIN)
#define MCOM03_DRIVE_STRENGTH		BIT(PIN_CONFIG_DRIVE_STRENGTH)
#define MCOM03_INPUT_SCHMITT_ENABLE	BIT(PIN_CONFIG_INPUT_SCHMITT_ENABLE)
#define MCOM03_SLEW_RATE		BIT(PIN_CONFIG_SLEW_RATE)
#define MCOM03_POWER_SOURCE		BIT(PIN_CONFIG_POWER_SOURCE)

/*
 * MCom-03 config sets
 *
 * BIT(22): 22 is the first free index in range [0 - PIN_CONFIG_END] in
 * pin_config_param enumeration
 */
#define MCOM03_PAD_ENABLE BIT(22)
#define MCOM03_PULLS (MCOM03_BIAS_BUS_HOLD | MCOM03_BIAS_PULL_DOWN | \
		      MCOM03_BIAS_PULL_UP)
#define MCOM03_PCONF_SET1 (MCOM03_PULLS | MCOM03_DRIVE_OPEN_DRAIN |	\
			   MCOM03_DRIVE_STRENGTH |			\
			   MCOM03_INPUT_SCHMITT_ENABLE |		\
			   MCOM03_SLEW_RATE)
#define MCOM03_PCONF_SET2 (MCOM03_PULLS | MCOM03_SLEW_RATE |		\
			   MCOM03_DRIVE_STRENGTH | MCOM03_INPUT_SCHMITT_ENABLE)
#define MCOM03_PCONF_SET3 (MCOM03_PULLS | MCOM03_SLEW_RATE |		\
			   MCOM03_DRIVE_STRENGTH)
#define MCOM03_PCONF_SET4 (MCOM03_PULLS | MCOM03_INPUT_SCHMITT_ENABLE)
#define MCOM03_PCONF_SET5 (MCOM03_PAD_ENABLE | MCOM03_POWER_SOURCE | \
			  MCOM03_DRIVE_OPEN_DRAIN)

/* HSPERIPH common masks */
#define HS_PAD_EN_MASK		BIT(0)
#define HS_SCHMITT_MASK		BIT(15)
#define HS_CTL_MASK		GENMASK(10, 5)
#define HS_SLEW_RATE_MASK	GENMASK(4, 3)
#define HS_DRIVE_STRENGTH_MASK	GENMASK(5, 0)
#define HS_MISC_PAD_EN_MASK	BIT(8)
#define HS_POWER_SOURCE		BIT(1)

/* HSPERIPH common values */
#define HS_PAD_EN		BIT(0)
#define HS_SCHMITT_EN		BIT(15)
#define HS_CTL(arg)		((arg) << 5)
#define HS_MAX_SLEW_RATE	0x03
#define HS_MIN_SLEW_RATE	0
#define HS_SLEW_RATE(arg)	((arg) << 3)
#define HS_GET_SLEW_RATE(val)	(((val) >> 3) & GENMASK(1, 0))
#define HS_2mA			(HS_DRIVE_STRENGTH_MASK >> 5)
#define HS_4mA			(HS_DRIVE_STRENGTH_MASK >> 4)
#define HS_6mA			(HS_DRIVE_STRENGTH_MASK >> 3)
#define HS_8mA			(HS_DRIVE_STRENGTH_MASK >> 2)
#define HS_10mA			(HS_DRIVE_STRENGTH_MASK >> 1)
#define HS_12mA			HS_DRIVE_STRENGTH_MASK
#define HS_MISC_PAD_EN		BIT(8)
#define HS_OPEN_DRAIN		BIT(3)

/* Controller specific masks */
#define SDMMC_OPEN_DRAIN_MASK	BIT(16)
#define SDMMC_SOFT_CTL_MASK	BIT(17)

/* Controller specific values */
#define COMMON_HS_DRIVE(v)	((v) >> 5)
#define SDMMC_OPEN_DRAIN_EN	BIT(16)
#define SDMMC_SOFT_CTL_EN	BIT(17)
#define EMAC_V18		BIT(0)

/* NAND offsets */
/*
 * This offset is used by nfc_ctrl(as pinconf) and nfc_io(as pinmux) groups.
 */
#define NAND_CONF_SHARED_MUX	0x184
#define NAND_OFF_IO_SHARE_MAX	BIT(4)
#define MUX_TO_NAND		BIT(4)
#define MUX_TO_GNSS		0

enum mcom03_hsperiph_periph_ids {
	SDMMC0,
	SDMMC1,
	HSPERIPH_MISC,
	EMAC0,
	EMAC1,
	EMAC_GLOBAL,
	QSPI1,
	NAND,
};

struct mcom03_hsperiph_pinctrl {
	struct device		*dev;
	struct regmap		*hs_syscon;
	struct pinctrl_dev	*pctrl_dev;
};

/**
 * struct mcom03_hsperiph_pinconf - a pinconf wrapper for HSPERIPH subsystems
 * @offset: offset according HSPERIPH URB space
 * @periph_id: peripheral id in pinctrl driver
 * @pull_mask: mask to set pull-up/down/sus bits if needed
 * @pinconf_cap: a set of configs supported by the pinconf entity
 *
 * Every mcom03_hsperiph_pinconf entity is a register in HSPERIPH URB space
 * which can configure a group of pins or an individual pin with a certain
 * pinconf attribute.
 */
struct mcom03_hsperiph_pinconf {
	unsigned int		offset;
	const unsigned int	periph_id;
	const unsigned int	pull_mask;
	const unsigned int	pinconf_cap;
};

/**
 * struct mcom03_hsperiph_grp - a representation of group in HSPERIPH subsystem
 * @pins: array of pins
 * @name: group name
 * @npins: quantity of pins
 * @pinconf: pinconf wrapper
 */
struct mcom03_hsperiph_grp {
	/* pinctrl */
	const u32	*pins;
	const char	*name;
	unsigned int	npins;

	/* pinconf */
	struct mcom03_hsperiph_pinconf pinconf;
};

#define MCOM03_HSPERIPH_DEF_GRP(_name, ...)	\
	static const unsigned int _name##_pins[] = { __VA_ARGS__ }

#define MCOM03_HSPERIPH_GRP(_n, _off, _cap, _id, _pull_mask)	\
	{							\
		.name = #_n,					\
		.pins = _n##_pins,				\
		.npins = ARRAY_SIZE(_n##_pins),			\
		.pinconf = {					\
				.offset = _off,			\
				.periph_id = _id,		\
				.pinconf_cap = _cap,		\
				.pull_mask = _pull_mask,	\
			},					\
	}

/**
 * struct mcom03_hsperiph_pin - a representation of a pin in HSPERIPH subsystem
 * @pin: pin number in HSPERIPH pinctrl space
 * @pinconf: pinconf wrapper
 */
struct mcom03_hsperiph_pin {
	const unsigned int		pin;
	struct mcom03_hsperiph_pinconf	pinconf;
};

#define MCOM03_HSPERIPH_PIN(_p, _off, _cap, _id, _pull_mask)	\
	{							\
		.pin = _p,					\
		.pinconf = {					\
				.offset = _off,			\
				.periph_id = _id,		\
				.pinconf_cap = _cap,		\
				.pull_mask = _pull_mask,	\
			},					\
	}

/*
 * Some MCom-03 pins configs are implemented with in separate registers, so we
 * need to access them separately.
 *
 * example:
 * sdmmc0: sdmmc0_default {
 *	pin_cfg {
 *		pins = "SDMMC0_CMD";
 *		bias-pull-up;
 *	};
 * };
 */
enum special_pins {
	SDMMC0_CMD = 0,
	SDMMC0_CLK = 9,
	SDMMC1_CMD = 14,
	SDMMC1_CLK = 23,
	EMAC0_RGMII_MDIO = 29,
	EMAC0_RGMII_MDC = 30,
	EMAC0_RGMII_TXC = 31,
	EMAC0_RGMII_RXC = 32,
	EMAC1_RGMII_MDIO = 43,
	EMAC1_RGMII_MDC = 44,
	EMAC1_RGMII_TXC = 45,
	EMAC1_RGMII_RXC = 46,
	QSPI1_SCLK = 65,
	NFC_REN = 86,
	NFC_WEN = 87,
	NFC_DQS = 90,
};

static const struct pinctrl_pin_desc mcom03_hsperiph_pins[] = {
	/* SDMMC0 */
	PINCTRL_PIN(SDMMC0_CMD, "SDMMC0_CMD"),
	PINCTRL_PIN(1, "SDMMC0_DAT0"),
	PINCTRL_PIN(2, "SDMMC0_DAT1"),
	PINCTRL_PIN(3, "SDMMC0_DAT2"),
	PINCTRL_PIN(4, "SDMMC0_DAT3"),
	PINCTRL_PIN(5, "SDMMC0_DAT4"),
	PINCTRL_PIN(6, "SDMMC0_DAT5"),
	PINCTRL_PIN(7, "SDMMC0_DAT6"),
	PINCTRL_PIN(8, "SDMMC0_DAT7"),
	PINCTRL_PIN(SDMMC0_CLK, "SDMMC0_CLK"),
	PINCTRL_PIN(10, "SDMMC0_CDN"),
	PINCTRL_PIN(11, "SDMMC0_WP"),
	PINCTRL_PIN(12, "SDMMC0_18EN"),
	PINCTRL_PIN(13, "SDMMC0_PWR"),

	/* SDMMC1 */
	PINCTRL_PIN(SDMMC1_CMD, "SDMMC1_CMD"),
	PINCTRL_PIN(15, "SDMMC1_DAT0"),
	PINCTRL_PIN(16, "SDMMC1_DAT1"),
	PINCTRL_PIN(17, "SDMMC1_DAT2"),
	PINCTRL_PIN(18, "SDMMC1_DAT3"),
	PINCTRL_PIN(19, "SDMMC1_DAT4"),
	PINCTRL_PIN(20, "SDMMC1_DAT5"),
	PINCTRL_PIN(21, "SDMMC1_DAT6"),
	PINCTRL_PIN(22, "SDMMC1_DAT7"),
	PINCTRL_PIN(SDMMC1_CLK, "SDMMC1_CLK"),
	PINCTRL_PIN(24, "SDMMC1_CDN"),
	PINCTRL_PIN(25, "SDMMC1_WP"),
	PINCTRL_PIN(26, "SDMMC1_18EN"),
	PINCTRL_PIN(27, "SDMMC1_PWR"),

	/* EMAC0/1 */
	PINCTRL_PIN(28, "CLK125"),

	/* EMAC0 */
	PINCTRL_PIN(EMAC0_RGMII_MDIO, "EMAC0_RGMII_MDIO"),
	PINCTRL_PIN(EMAC0_RGMII_MDC, "EMAC0_RGMII_MDC"),
	PINCTRL_PIN(EMAC0_RGMII_TXC, "EMAC0_RGMII_TXC"),
	PINCTRL_PIN(EMAC0_RGMII_RXC, "EMAC0_RGMII_RXC"),
	PINCTRL_PIN(33, "EMAC0_RGMII_TXD0"),
	PINCTRL_PIN(34, "EMAC0_RGMII_TXD1"),
	PINCTRL_PIN(35, "EMAC0_RGMII_TXD2"),
	PINCTRL_PIN(36, "EMAC0_RGMII_TXD3"),
	PINCTRL_PIN(37, "EMAC0_RGMII_RXD0"),
	PINCTRL_PIN(38, "EMAC0_RGMII_RXD1"),
	PINCTRL_PIN(39, "EMAC0_RGMII_RXD2"),
	PINCTRL_PIN(40, "EMAC0_RGMII_RXD3"),
	PINCTRL_PIN(41, "EMAC0_RGMII_TXCTL"),
	PINCTRL_PIN(42, "EMAC0_RGMII_RXCTL"),

	/* EMAC1 */
	PINCTRL_PIN(EMAC1_RGMII_MDIO, "EMAC1_RGMII_MDIO"),
	PINCTRL_PIN(EMAC1_RGMII_MDC, "EMAC1_RGMII_MDC"),
	PINCTRL_PIN(EMAC1_RGMII_TXC, "EMAC1_RGMII_TXC"),
	PINCTRL_PIN(EMAC1_RGMII_RXC, "EMAC1_RGMII_RXC"),
	PINCTRL_PIN(47, "EMAC1_RGMII_TXD0"),
	PINCTRL_PIN(48, "EMAC1_RGMII_TXD1"),
	PINCTRL_PIN(49, "EMAC1_RGMII_TXD2"),
	PINCTRL_PIN(50, "EMAC1_RGMII_TXD3"),
	PINCTRL_PIN(51, "EMAC1_RGMII_RXD0"),
	PINCTRL_PIN(52, "EMAC1_RGMII_RXD1"),
	PINCTRL_PIN(53, "EMAC1_RGMII_RXD2"),
	PINCTRL_PIN(54, "EMAC1_RGMII_RXD3"),
	PINCTRL_PIN(55, "EMAC1_RGMII_TXCTL"),
	PINCTRL_PIN(56, "EMAC1_RGMII_RXCTL"),

	/* QSPI1 */
	PINCTRL_PIN(57, "QSPI1_SISI0"),
	PINCTRL_PIN(58, "QSPI1_SISI1"),
	PINCTRL_PIN(59, "QSPI1_SISI2"),
	PINCTRL_PIN(60, "QSPI1_SISI3"),
	PINCTRL_PIN(61, "QSPI1_SS0"),
	PINCTRL_PIN(62, "QSPI1_SS1"),
	PINCTRL_PIN(63, "QSPI1_SS2"),
	PINCTRL_PIN(64, "QSPI1_SS3"),
	PINCTRL_PIN(QSPI1_SCLK, "QSPI1_SCLK"),

	/* NAND */
	PINCTRL_PIN(66, "NFC_IO0"),
	PINCTRL_PIN(67, "NFC_IO1"),
	PINCTRL_PIN(68, "NFC_IO2"),
	PINCTRL_PIN(69, "NFC_IO3"),
	PINCTRL_PIN(70, "NFC_IO4"),
	PINCTRL_PIN(71, "NFC_IO5"),
	PINCTRL_PIN(72, "NFC_IO6"),
	PINCTRL_PIN(73, "NFC_IO7"),
	PINCTRL_PIN(74, "NFC_IO8"),
	PINCTRL_PIN(75, "NFC_IO9"),
	PINCTRL_PIN(76, "NFC_IO10"),
	PINCTRL_PIN(77, "NFC_IO11"),
	PINCTRL_PIN(78, "NFC_IO12"),
	PINCTRL_PIN(79, "NFC_IO13"),
	PINCTRL_PIN(80, "NFC_IO14"),
	PINCTRL_PIN(81, "NFC_IO15"),
	PINCTRL_PIN(82, "NFC_RBN0"),
	PINCTRL_PIN(83, "NFC_RBN1"),
	PINCTRL_PIN(84, "NFC_ALE"),
	PINCTRL_PIN(85, "NFC_CLE"),
	PINCTRL_PIN(NFC_REN, "NFC_REN"),
	PINCTRL_PIN(NFC_WEN, "NFC_WEN"),
	PINCTRL_PIN(88, "NFC_CEN0"),
	PINCTRL_PIN(89, "NFC_CEN1"),
	PINCTRL_PIN(NFC_DQS, "NFC_DQS"),
};

static struct mcom03_hsperiph_pin mcom03_hsperiph_special_pins[] = {
	MCOM03_HSPERIPH_PIN(SDMMC0_CMD, 0x34, MCOM03_PCONF_SET1, SDMMC0,
			    GENMASK(2, 0)),
	MCOM03_HSPERIPH_PIN(SDMMC0_CLK, 0x30, MCOM03_PCONF_SET1, SDMMC0,
			    GENMASK(2, 0)),
	MCOM03_HSPERIPH_PIN(SDMMC1_CMD, 0x70, MCOM03_PCONF_SET1, SDMMC1,
			    GENMASK(2, 0)),
	MCOM03_HSPERIPH_PIN(SDMMC1_CLK, 0x6C, MCOM03_PCONF_SET1, SDMMC1,
			    GENMASK(2, 0)),
	MCOM03_HSPERIPH_PIN(EMAC0_RGMII_MDIO, 0x158, MCOM03_PCONF_SET2, EMAC0,
			    GENMASK(2, 0)),
	MCOM03_HSPERIPH_PIN(EMAC0_RGMII_MDC, 0x15C, MCOM03_PCONF_SET3, EMAC0,
			    GENMASK(2, 0)),
	MCOM03_HSPERIPH_PIN(EMAC0_RGMII_TXC, 0x14C, MCOM03_PCONF_SET3, EMAC0,
			    GENMASK(2, 0)),
	MCOM03_HSPERIPH_PIN(EMAC0_RGMII_RXC, 0x154, MCOM03_PCONF_SET4, EMAC0,
			    GENMASK(2, 0)),
	MCOM03_HSPERIPH_PIN(EMAC1_RGMII_MDIO, 0x178, MCOM03_PCONF_SET2, EMAC1,
			    GENMASK(2, 0)),
	MCOM03_HSPERIPH_PIN(EMAC1_RGMII_MDC, 0x17C, MCOM03_PCONF_SET3, EMAC1,
			    GENMASK(2, 0)),
	MCOM03_HSPERIPH_PIN(EMAC1_RGMII_TXC, 0x16C, MCOM03_PCONF_SET3, EMAC1,
			    GENMASK(2, 0)),
	MCOM03_HSPERIPH_PIN(EMAC1_RGMII_RXC, 0x174,  MCOM03_PCONF_SET4, EMAC1,
			    GENMASK(2, 0)),
	MCOM03_HSPERIPH_PIN(QSPI1_SCLK, 0x28, MCOM03_PCONF_SET2, QSPI1,
			    GENMASK(2, 0)),
	MCOM03_HSPERIPH_PIN(NFC_REN, 0x190, MCOM03_PCONF_SET2, NAND,
			    GENMASK(2, 0)),
	MCOM03_HSPERIPH_PIN(NFC_WEN, 0x194, MCOM03_PCONF_SET2, NAND,
			    GENMASK(2, 0)),
	MCOM03_HSPERIPH_PIN(NFC_DQS, 0x19C, MCOM03_PCONF_SET2, NAND,
			    GENMASK(2, 0)),
};

/* Declaring MCom-03 HSPERIPH groups */
MCOM03_HSPERIPH_DEF_GRP(sdmmc0_ctrl, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
MCOM03_HSPERIPH_DEF_GRP(sdmmc1_ctrl, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23);
MCOM03_HSPERIPH_DEF_GRP(sdmmc0_data, 1, 2, 3, 4, 5, 6, 7, 8);
MCOM03_HSPERIPH_DEF_GRP(sdmmc1_data, 15, 16, 17, 18, 19, 20, 21, 22);
MCOM03_HSPERIPH_DEF_GRP(sdmmc_wp, 11, 25);
MCOM03_HSPERIPH_DEF_GRP(sdmmc_cdn, 10, 24);
/*
 * FIXME:
 * hsperiph_misc group is not fully described, so add USB{0,1}_EN_OCN pins
 * upon adding pinconf support for USB.
 */
MCOM03_HSPERIPH_DEF_GRP(hsperiph_misc, 10, 11, 12, 13, 24, 25, 26, 27);
MCOM03_HSPERIPH_DEF_GRP(sdmmc_18en_pwr, 12, 13, 26, 27);
MCOM03_HSPERIPH_DEF_GRP(emac_v18, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38,
			39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52,
			53, 54, 55, 56);
MCOM03_HSPERIPH_DEF_GRP(emac0_ctrl, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
			40, 41, 42);
MCOM03_HSPERIPH_DEF_GRP(emac0_tx, 31, 33, 34, 35, 36);
MCOM03_HSPERIPH_DEF_GRP(emac0_rx, 32, 37, 38, 39, 40);
MCOM03_HSPERIPH_DEF_GRP(emac1_ctrl, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53,
			54, 55, 56);
MCOM03_HSPERIPH_DEF_GRP(emac1_tx, 45, 47, 48, 49, 50);
MCOM03_HSPERIPH_DEF_GRP(emac1_rx, 46, 51, 52, 53, 54);
MCOM03_HSPERIPH_DEF_GRP(qspi1_ctrl, 57, 58, 59, 60, 61, 62, 63, 64, 65);
MCOM03_HSPERIPH_DEF_GRP(qspi1_ss, 57, 58, 59, 60);
MCOM03_HSPERIPH_DEF_GRP(qspi1_data, 61, 62, 63, 64);
MCOM03_HSPERIPH_DEF_GRP(nfc_ctrl, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76,
			77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90);
MCOM03_HSPERIPH_DEF_GRP(nfc_ale_cle, 84, 85);
MCOM03_HSPERIPH_DEF_GRP(nfc_cen, 88, 89);
MCOM03_HSPERIPH_DEF_GRP(nfc_io, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77,
			78, 79, 80, 81);
MCOM03_HSPERIPH_DEF_GRP(nfc_rbn, 82, 83);

static struct mcom03_hsperiph_grp mcom03_hsperiph_groups[] = {
	/* HSPERIPH:SD/MMC groups */
	MCOM03_HSPERIPH_GRP(sdmmc0_ctrl, 0x2C, MCOM03_PAD_ENABLE, SDMMC0, 0),
	MCOM03_HSPERIPH_GRP(sdmmc1_ctrl, 0x68, MCOM03_PAD_ENABLE, SDMMC1, 0),
	MCOM03_HSPERIPH_GRP(sdmmc0_data, 0x38, MCOM03_PCONF_SET1, SDMMC0,
			    GENMASK(2, 0)),
	MCOM03_HSPERIPH_GRP(sdmmc1_data, 0x74, MCOM03_PCONF_SET1, SDMMC1,
			    GENMASK(2, 0)),
	MCOM03_HSPERIPH_GRP(sdmmc_wp, 0x1A4, MCOM03_PULLS, HSPERIPH_MISC,
			    GENMASK(14, 12)),
	MCOM03_HSPERIPH_GRP(sdmmc_cdn, 0x1A4, MCOM03_PULLS, HSPERIPH_MISC,
			    GENMASK(11, 9)),
	MCOM03_HSPERIPH_GRP(hsperiph_misc, 0x1A4, MCOM03_PAD_ENABLE,
			    HSPERIPH_MISC, 0),
	MCOM03_HSPERIPH_GRP(sdmmc_18en_pwr, 0x1A4, MCOM03_DRIVE_STRENGTH,
			    HSPERIPH_MISC, 0),

	/* HSPERIPH:EMAC groups */
	MCOM03_HSPERIPH_GRP(emac_v18, 0x140, MCOM03_POWER_SOURCE,
			    EMAC_GLOBAL, 0),
	MCOM03_HSPERIPH_GRP(emac0_ctrl, 0x144, MCOM03_PAD_ENABLE |
			    MCOM03_DRIVE_OPEN_DRAIN, EMAC0, 0),
	MCOM03_HSPERIPH_GRP(emac0_tx, 0x148, MCOM03_PCONF_SET3, EMAC0,
			    GENMASK(2, 0)),
	MCOM03_HSPERIPH_GRP(emac0_rx, 0x150, MCOM03_PCONF_SET4, EMAC0,
			    GENMASK(2, 0)),
	MCOM03_HSPERIPH_GRP(emac1_ctrl, 0x164, MCOM03_PAD_ENABLE |
			    MCOM03_DRIVE_OPEN_DRAIN, EMAC1, 0),
	MCOM03_HSPERIPH_GRP(emac1_tx, 0x168, MCOM03_PCONF_SET3, EMAC1,
			    GENMASK(2, 0)),
	MCOM03_HSPERIPH_GRP(emac1_rx, 0x170, MCOM03_PCONF_SET4, EMAC1,
			    GENMASK(2, 0)),

	/* HSPERIPH: QSPI1 groups */
	MCOM03_HSPERIPH_GRP(qspi1_ctrl, 0x1C, MCOM03_PCONF_SET5, QSPI1, 0),
	MCOM03_HSPERIPH_GRP(qspi1_ss, 0x20, MCOM03_PCONF_SET2, QSPI1,
			    GENMASK(2, 0)),
	MCOM03_HSPERIPH_GRP(qspi1_data, 0x24, MCOM03_PCONF_SET2, QSPI1,
			    GENMASK(2, 0)),

	/* HSPERIPH: NAND groups */
	MCOM03_HSPERIPH_GRP(nfc_ctrl, NAND_CONF_SHARED_MUX, MCOM03_PCONF_SET5,
			    NAND, 0),
	MCOM03_HSPERIPH_GRP(nfc_ale_cle, 0x188, MCOM03_PCONF_SET2, NAND,
			    GENMASK(2, 0)),
	MCOM03_HSPERIPH_GRP(nfc_cen, 0x18C, MCOM03_PCONF_SET2, NAND,
			    GENMASK(2, 0)),
	MCOM03_HSPERIPH_GRP(nfc_io, 0x198, MCOM03_PCONF_SET2, NAND,
			    GENMASK(2, 0)),
	MCOM03_HSPERIPH_GRP(nfc_rbn, 0x1A0, MCOM03_PCONF_SET2, NAND,
			    GENMASK(2, 0)),
};

static const char *const mcom03_hsperiph_mux_groups[] = {
	"nfc_io",
};

static const char *const mcom03_hsperiph_pinmux_functions[] = {
	"NAND", "GNSS",
};

static bool mcom03_hsperiph_pinconf_param_supported(
				struct mcom03_hsperiph_pinconf *pinconf,
				enum pin_config_param param)
{
	/*
	 * 21 is the last occupied index in range [0 - PIN_CONFIG_END[ in
	 * pin_config_param enumeration
	 */
	return param < PIN_CONFIG_END ?
		pinconf->pinconf_cap & BIT(param) :
		pinconf->pinconf_cap & BIT((param - PIN_CONFIG_END) + 21);
}

static struct mcom03_hsperiph_pin *
mcom03_hsperiph_get_special_pin(unsigned int pin)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mcom03_hsperiph_special_pins); i++)
		if (mcom03_hsperiph_special_pins[i].pin == pin)
			return &mcom03_hsperiph_special_pins[i];

	return NULL;
}

static int
mcom03_hsperiph_pinconf_get_internal(struct mcom03_hsperiph_pinctrl *pctrl,
				     unsigned long *config,
				     struct mcom03_hsperiph_pinconf *pinconf)
{
	u8 pull_bit = 0;
	unsigned int val, arg = 1;
	enum pin_config_param param = pinconf_to_config_param(*config);
	unsigned int periph_id = pinconf->periph_id;
	unsigned int pull_mask = pinconf->pull_mask;

	if (!mcom03_hsperiph_pinconf_param_supported(pinconf, param))
		return -ENOTSUPP;

	regmap_read(pctrl->hs_syscon, pinconf->offset, &val);
	switch ((u32)param) {
	case PIN_CONFIG_MCOM03_PAD_ENABLE:
		if (periph_id == HSPERIPH_MISC) {
			if (!(val & HS_MISC_PAD_EN))
				return -EINVAL;
		} else {
			if (!(val & HS_PAD_EN))
				return -EINVAL;
		}
		break;
	case PIN_CONFIG_BIAS_BUS_HOLD:
	case PIN_CONFIG_BIAS_PULL_DOWN:
	case PIN_CONFIG_BIAS_PULL_UP:
		pull_bit = ffs(pull_mask) - 1;

		/*
		 * All periph_id have common way for indexing pulls, which is
		 * sus-pu-pd, except HSPERIPH_MISC which has indexing like
		 * sus-pd-pu.
		 */
		if (periph_id == HSPERIPH_MISC) {
			if (param == PIN_CONFIG_BIAS_PULL_DOWN)
				pull_bit++;
			else if (param == PIN_CONFIG_BIAS_PULL_UP)
				pull_bit += 2;
		} else {
			if (param == PIN_CONFIG_BIAS_PULL_UP)
				pull_bit++;
			else if (param == PIN_CONFIG_BIAS_PULL_DOWN)
				pull_bit += 2;
		}

		if (!(val & BIT(pull_bit)))
			return -EINVAL;
		break;
	case PIN_CONFIG_SLEW_RATE:
		arg = HS_GET_SLEW_RATE(val);
		break;
	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		if (!(val & HS_SCHMITT_EN))
			return -EINVAL;
		break;
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		if (periph_id == SDMMC0 || periph_id == SDMMC1) {
			if (!(val & SDMMC_OPEN_DRAIN_EN))
				return -EINVAL;
		} else
			if (!(val & HS_OPEN_DRAIN))
				return -EINVAL;
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		if (periph_id == SDMMC0 || periph_id == SDMMC1 ||
		    periph_id == EMAC0 || periph_id == EMAC1 ||
		    periph_id == QSPI1 || periph_id == NAND)
			val = COMMON_HS_DRIVE(val);

		val &= HS_DRIVE_STRENGTH_MASK;
		switch (val) {
		case HS_2mA:
			val = 2;
			break;
		case HS_4mA:
			val = 4;
			break;
		case HS_6mA:
			val = 6;
			break;
		case HS_8mA:
			val = 8;
			break;
		case HS_10mA:
			val = 10;
			break;
		case HS_12mA:
			val = 12;
			break;
		}

		arg = val;
		break;
	case PIN_CONFIG_POWER_SOURCE:
		switch (periph_id) {
		case EMAC_GLOBAL:
			arg = val & EMAC_V18 ? 1800 : 3300;
			break;
		default:
			arg = val & HS_POWER_SOURCE ? 1800 : 3300;
			break;
		}

		break;
	}

	*config = pinconf_to_config_packed(param, arg);
	return 0;
}

static int mcom03_hsperiph_pinconf_get(struct pinctrl_dev *pctldev,
				       unsigned int pin, unsigned long *config)
{
	struct mcom03_hsperiph_pinctrl *pctrl =
					pinctrl_dev_get_drvdata(pctldev);
	struct mcom03_hsperiph_pin *sp = mcom03_hsperiph_get_special_pin(pin);

	if (!sp)
		return -ENOTSUPP;

	return mcom03_hsperiph_pinconf_get_internal(pctrl, config,
						    &sp->pinconf);
}

static int mcom03_hsperiph_pinconf_group_get(struct pinctrl_dev *pctldev,
					     unsigned int selector,
					     unsigned long *config)
{
	struct mcom03_hsperiph_pinctrl *pctrl =
					pinctrl_dev_get_drvdata(pctldev);

	return mcom03_hsperiph_pinconf_get_internal(pctrl, config,
				&mcom03_hsperiph_groups[selector].pinconf);
}

static int
mcom03_hsperiph_pinconf_set_internal(struct mcom03_hsperiph_pinctrl *pctrl,
				      unsigned long config,
				      struct mcom03_hsperiph_pinconf *pinconf)
{
	struct regmap *regmap = pctrl->hs_syscon;
	u32 arg;
	u8 pull_bit = 0;
	enum pin_config_param param;
	unsigned int offset = pinconf->offset;
	unsigned int periph_id = pinconf->periph_id;
	unsigned int pull_mask = pinconf->pull_mask;

	param = pinconf_to_config_param(config);
	arg = pinconf_to_config_argument(config);
	if (!mcom03_hsperiph_pinconf_param_supported(pinconf, param))
		return -ENOTSUPP;

	switch ((u32)param) {
	case PIN_CONFIG_MCOM03_PAD_ENABLE:
		if (periph_id == HSPERIPH_MISC)
			regmap_update_bits(regmap, offset, HS_MISC_PAD_EN_MASK,
					   arg ? HS_MISC_PAD_EN : 0);
		else
			regmap_update_bits(regmap, offset, HS_PAD_EN_MASK,
					   arg ? HS_PAD_EN : 0);
		break;
	case PIN_CONFIG_BIAS_BUS_HOLD:
	case PIN_CONFIG_BIAS_PULL_DOWN:
	case PIN_CONFIG_BIAS_PULL_UP:
		pull_bit = ffs(pull_mask) - 1;

		/*
		 * All periph_id have common way for indexing pulls, which is
		 * sus-pu-pd, except HSPERIPH_MISC which has indexing like
		 * sus-pd-pu.
		 */
		if (periph_id == HSPERIPH_MISC) {
			if (param == PIN_CONFIG_BIAS_PULL_DOWN)
				pull_bit++;
			else if (param == PIN_CONFIG_BIAS_PULL_UP)
				pull_bit += 2;
		} else {
			if (param == PIN_CONFIG_BIAS_PULL_UP)
				pull_bit++;
			else if (param == PIN_CONFIG_BIAS_PULL_DOWN)
				pull_bit += 2;
		}

		regmap_update_bits(regmap, offset, pull_mask, BIT(pull_bit));
		break;
	case PIN_CONFIG_BIAS_DISABLE:
		regmap_update_bits(regmap, offset, pull_mask, 0);
		break;
	case PIN_CONFIG_SLEW_RATE:
		if (arg != HS_MIN_SLEW_RATE && arg != HS_MAX_SLEW_RATE)
			return -EINVAL;

		regmap_update_bits(regmap, offset, HS_SLEW_RATE_MASK,
				   HS_SLEW_RATE(arg));
		break;
	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		regmap_update_bits(regmap, offset, HS_SCHMITT_MASK,
				   arg != 0 ? HS_SCHMITT_EN : 0);
		break;
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		if (periph_id == SDMMC0 || periph_id == SDMMC1)
			regmap_update_bits(regmap, offset,
					   SDMMC_OPEN_DRAIN_MASK,
					   SDMMC_OPEN_DRAIN_EN);
		else
			regmap_update_bits(regmap, offset, HS_OPEN_DRAIN,
					   HS_OPEN_DRAIN);
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		switch (arg) {
		case 0:
			break;
		case 2:
			arg = HS_2mA;
			break;
		case 4:
			arg = HS_4mA;
			break;
		case 6:
			arg = HS_6mA;
			break;
		case 8:
			arg = HS_8mA;
			break;
		case 10:
			arg = HS_10mA;
			break;
		case 12:
			arg = HS_12mA;
			break;
		default:
			return -EINVAL;
		}

		if (periph_id == HSPERIPH_MISC)
			regmap_update_bits(regmap, offset,
					   HS_DRIVE_STRENGTH_MASK, arg);
		else if (periph_id == SDMMC0 || periph_id == SDMMC1) {
			/*
			 * Activating soft CTL for SDMMC pads, this means CTL
			 * will get it's value directly from HSPERIPH URB and
			 * not from SDMMC controllers.
			 */
			regmap_update_bits(regmap, offset, SDMMC_SOFT_CTL_MASK,
					   SDMMC_SOFT_CTL_EN);

			/* Installing CTL */
			regmap_update_bits(regmap, offset, HS_CTL_MASK,
					   HS_CTL(arg));
		} else
			regmap_update_bits(regmap, offset, HS_CTL_MASK,
					   HS_CTL(arg));
	break;
	case PIN_CONFIG_POWER_SOURCE:
		if (arg != 1800 && arg != 3300)
			return -EINVAL;

		switch (periph_id) {
		case EMAC_GLOBAL:
			regmap_update_bits(regmap, offset, EMAC_V18,
					   arg == 1800 ? EMAC_V18 : 0);
			break;
		default:
			regmap_update_bits(regmap, offset, HS_POWER_SOURCE,
					   arg == 1800 ? HS_POWER_SOURCE : 0);
			break;
		}

		break;
	}

	return 0;
}

static int mcom03_hsperiph_pinconf_set(struct pinctrl_dev *pctldev,
				       unsigned int pin, unsigned long *configs,
				       unsigned int num_configs)
{
	struct mcom03_hsperiph_pinctrl *pctrl =
					pinctrl_dev_get_drvdata(pctldev);
	struct mcom03_hsperiph_pin *sp = mcom03_hsperiph_get_special_pin(pin);
	int i, ret;

	if (!sp) {
		dev_err(pctrl->dev, "Pin access is prohibited for pin [%s].\n",
			mcom03_hsperiph_pins[pin].name);

		return -ENOTSUPP;
	}

	for (i = 0; i < num_configs; i++) {
		ret = mcom03_hsperiph_pinconf_set_internal(pctrl, configs[i],
							   &sp->pinconf);
		if (ret == -ENOTSUPP) {
			dev_err(pctrl->dev, "%s pin doesn't support property %u\n",
				mcom03_hsperiph_pins[sp->pin].name,
				pinconf_to_config_param(configs[i]));
			return ret;
		} else if (ret == -EINVAL) {
			dev_err(pctrl->dev, "%s pin doesn't support property %u with argument %u.\n",
				mcom03_hsperiph_pins[sp->pin].name,
				pinconf_to_config_param(configs[i]),
				pinconf_to_config_argument(configs[i]));
			return ret;
		}

		dev_dbg(pctrl->dev, "Setting %d param for pin %s\n",
			pinconf_to_config_param(configs[i]),
			mcom03_hsperiph_pins[sp->pin].name);
	}

	return 0;
}

static int mcom03_hsperiph_pinconf_group_set(struct pinctrl_dev *pctldev,
					     unsigned int selector,
					     unsigned long *configs,
					     unsigned int num_configs)
{
	struct mcom03_hsperiph_pinctrl *pctrl =
					pinctrl_dev_get_drvdata(pctldev);
	int i, ret;

	for (i = 0; i < num_configs; i++) {
		ret = mcom03_hsperiph_pinconf_set_internal(pctrl, configs[i],
				&mcom03_hsperiph_groups[selector].pinconf);
		if (ret == -ENOTSUPP) {
			dev_err(pctrl->dev, "[%s] group doesn't support property %u\n",
				mcom03_hsperiph_groups[selector].name,
				pinconf_to_config_param(configs[i]));
			return ret;
		} else if (ret == -EINVAL) {
			dev_err(pctrl->dev, "[%s] group doesn't support property %u with argument %u.\n",
				mcom03_hsperiph_groups[selector].name,
				pinconf_to_config_param(configs[i]),
				pinconf_to_config_argument(configs[i]));
			return ret;
		}
		dev_dbg(pctrl->dev, "Setting %d param for group %s\n",
			pinconf_to_config_param(configs[i]),
			mcom03_hsperiph_groups[selector].name);
	}

	return 0;
}

static const struct pinconf_ops mcom03_hsperiph_pinconf_ops = {
	.is_generic		= true,
	.pin_config_group_get	= mcom03_hsperiph_pinconf_group_get,
	.pin_config_group_set	= mcom03_hsperiph_pinconf_group_set,
	.pin_config_get		= mcom03_hsperiph_pinconf_get,
	.pin_config_set		= mcom03_hsperiph_pinconf_set,
};

static int mcom03_hsperiph_pinctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(mcom03_hsperiph_groups);
}

static const char *mcom03_hsperiph_pinctrl_get_group_name(
						struct pinctrl_dev *pctldev,
						unsigned int group)
{
	return mcom03_hsperiph_groups[group].name;
}

static int mcom03_hsperiph_pinctrl_get_group_pins(struct pinctrl_dev *pctldev,
					 unsigned int group,
					 const unsigned int **pins,
					 unsigned int *num_pins)
{
	*pins = mcom03_hsperiph_groups[group].pins;
	*num_pins = mcom03_hsperiph_groups[group].npins;

	return 0;
}

int mcom03_hsperiph_get_functions_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(mcom03_hsperiph_pinmux_functions);
}

const char *mcom03_hsperiph_get_function_name(struct pinctrl_dev *pctldev,
					      unsigned int selector)
{
	return mcom03_hsperiph_pinmux_functions[selector];
}

int mcom03_hsperiph_get_function_groups(struct pinctrl_dev *pctldev,
					unsigned int selector,
					const char * const **groups,
					unsigned int *num_groups)
{
	/* All HSPERIPH mux functions have the same group */
	*groups = mcom03_hsperiph_mux_groups;
	*num_groups = ARRAY_SIZE(mcom03_hsperiph_mux_groups);

	return 0;
}

int mcom03_hsperiph_mcom03_hsperiph_set_mux(struct pinctrl_dev *pctldev,
					    unsigned int func_selector,
					    unsigned int group_selector)
{
	struct mcom03_hsperiph_pinctrl *pctrl =
					pinctrl_dev_get_drvdata(pctldev);

	switch (func_selector) {
	case 0: // NAND
		regmap_update_bits(pctrl->hs_syscon, NAND_CONF_SHARED_MUX,
				   NAND_OFF_IO_SHARE_MAX, MUX_TO_NAND);
		break;
	case 1: // GNSS
		regmap_update_bits(pctrl->hs_syscon, NAND_CONF_SHARED_MUX,
				   NAND_OFF_IO_SHARE_MAX, MUX_TO_GNSS);
		break;
	}

	dev_dbg(pctldev->dev, "Set %s function to %s group\n",
		mcom03_hsperiph_pinmux_functions[func_selector],
		mcom03_hsperiph_groups[group_selector].name);

	return 0;
}

static const struct pinmux_ops mcom03_hsperiph_pinmux_ops = {
	.get_functions_count	= mcom03_hsperiph_get_functions_count,
	.get_function_name	= mcom03_hsperiph_get_function_name,
	.get_function_groups	= mcom03_hsperiph_get_function_groups,
	.set_mux		= mcom03_hsperiph_mcom03_hsperiph_set_mux,
};

static const struct pinctrl_ops mcom03_hsperiph_pinctrl_ops = {
	.dt_node_to_map		= pinconf_generic_dt_node_to_map_all,
	.dt_free_map		= pinctrl_utils_free_map,
	.get_groups_count	= mcom03_hsperiph_pinctrl_get_groups_count,
	.get_group_name		= mcom03_hsperiph_pinctrl_get_group_name,
	.get_group_pins		= mcom03_hsperiph_pinctrl_get_group_pins,
};

static struct pinctrl_desc mcom03_hsperiph_desc = {
	.name			= "mcom03-hsperiph-pinctrl",
	.owner			= THIS_MODULE,
	.pins			= mcom03_hsperiph_pins,
	.npins			= ARRAY_SIZE(mcom03_hsperiph_pins),
	.pmxops			= &mcom03_hsperiph_pinmux_ops,
	.confops		= &mcom03_hsperiph_pinconf_ops,
	.pctlops		= &mcom03_hsperiph_pinctrl_ops,
	.num_custom_params	= ARRAY_SIZE(mcom03_hsperiph_custom_params),
	.custom_params		= mcom03_hsperiph_custom_params,
#ifdef CONFIG_DEBUG_FS
	.custom_conf_items	= mcom03_hsperiph_conf_items,
#endif
};

static int mcom03_hsperiph_pinctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mcom03_hsperiph_pinctrl *pctrl;
	int ret;

	pctrl = devm_kzalloc(dev, sizeof(*pctrl), GFP_KERNEL);
	if (!pctrl)
		return -ENOMEM;

	pctrl->hs_syscon = syscon_regmap_lookup_by_phandle(dev->of_node,
							  "elvees,urb");
	if (IS_ERR(pctrl->hs_syscon)) {
		dev_err(&pdev->dev, "unable to get hsurb-syscon\n");
		return PTR_ERR(pctrl->hs_syscon);
	}

	pctrl->dev = dev;
	platform_set_drvdata(pdev, pctrl);

	ret = devm_pinctrl_register_and_init(dev, &mcom03_hsperiph_desc, pctrl,
					     &pctrl->pctrl_dev);
	if (ret) {
		dev_err(pctrl->dev, "Failed to register pinctrl (%d)\n", ret);
		return ret;
	}

	ret = pinctrl_enable(pctrl->pctrl_dev);
	if (ret) {
		dev_err(pctrl->dev, "Failed to enable pinctrl (%d)\n", ret);
		return ret;
	}

	dev_info(pctrl->dev, "%ld pins & %ld groups registered\n",
		 ARRAY_SIZE(mcom03_hsperiph_pins),
		 ARRAY_SIZE(mcom03_hsperiph_groups));
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id mcom03_hsperiph_pinctrl_match[] = {
	{ .compatible = "elvees,mcom03-hsperiph-pinctrl" },
	{ /* sentinel */ },
};
#endif

static struct platform_driver mcom03_hsperiph_pinctrl_driver = {
	.probe = mcom03_hsperiph_pinctrl_probe,
	.driver = {
		.name = "mcom03-hsperiph-pinctrl",
		.of_match_table = of_match_ptr(mcom03_hsperiph_pinctrl_match),
	},
};

static int __init mcom03_hsperiph_pinctrl_init(void)
{
	return platform_driver_register(&mcom03_hsperiph_pinctrl_driver);
}
arch_initcall(mcom03_hsperiph_pinctrl_init);
