/*
 * 1-wire client/driver for the Maxim Semicondactor
 * MAX17211/MAX17215 Standalone Fuel Gauge IC
 *
 * Copyright (C) 2017 OAO Radioavionica
 * Author: Alex A. Mihaylov <minimumlaw@rambler.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/regmap.h>
#include <linux/power_supply.h>

#include "../../w1/w1.h"
#include "linux/power/max172xx-battery.h"

#define DEF_DEV_NAME_MAX17211	"MAX17211"
#define DEF_DEV_NAME_MAX17215	"MAX17215"
#define DEF_DEV_NAME_UNKNOWN	"UNKNOWN"
#define DEF_MFG_NAME		"MAXIM"

#define PSY_MAX_NAME_LEN	32

struct max17211_device_info {
	char name[PSY_MAX_NAME_LEN];
	struct power_supply *bat;
	struct power_supply_desc bat_desc;
	struct device *w1_dev;
	struct regmap *regmap;
	/* battery design format */
	unsigned int rsense; /* in tenths uOhm */
	char DeviceName[2 * MAX1721X_REG_DEV_NUMB + 1];
	char ManufacturerName[2 * MAX1721X_REG_MFG_NUMB + 1];
	char SerialNumber[13]; /* see get_sn_str() later for comment */
};

static inline struct max17211_device_info *
to_device_info(struct power_supply *psy)
{
	return power_supply_get_drvdata(psy);
}

static int max1721x_battery_get_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	struct max17211_device_info *info = to_device_info(psy);
	unsigned int reg = 0;
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		/*
		 * POWER_SUPPLY_PROP_PRESENT will always readable via
		 * sysfs interface. Value return 0 if battery not
		 * present or unaccesable via W1.
		 */
		val->intval =
			regmap_read(info->regmap, MAX172XX_REG_STATUS,
			&reg) ? 0 : !(reg & MAX172XX_BAT_PRESENT);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		ret = regmap_read(info->regmap, MAX172XX_REG_REPSOC, &reg);
		val->intval = max172xx_percent_to_ps(reg);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = regmap_read(info->regmap, MAX172XX_REG_BATT, &reg);
		val->intval = max172xx_voltage_to_ps(reg);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		ret = regmap_read(info->regmap, MAX172XX_REG_DESIGNCAP, &reg);
		val->intval = max172xx_capacity_to_ps(reg);
		break;
	case POWER_SUPPLY_PROP_CHARGE_AVG:
		ret = regmap_read(info->regmap, MAX172XX_REG_REPCAP, &reg);
		val->intval = max172xx_capacity_to_ps(reg);
		break;
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG:
		ret = regmap_read(info->regmap, MAX172XX_REG_TTE, &reg);
		val->intval = max172xx_time_to_ps(reg);
		break;
	case POWER_SUPPLY_PROP_TIME_TO_FULL_AVG:
		ret = regmap_read(info->regmap, MAX172XX_REG_TTF, &reg);
		val->intval = max172xx_time_to_ps(reg);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		ret = regmap_read(info->regmap, MAX172XX_REG_TEMP, &reg);
		val->intval = max172xx_temperature_to_ps(reg);
		break;
	/* We need signed current, so must cast info->rsense to signed type */
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = regmap_read(info->regmap, MAX172XX_REG_CURRENT, &reg);
		val->intval =
			max172xx_current_to_voltage(reg) / (int)info->rsense;
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		ret = regmap_read(info->regmap, MAX172XX_REG_AVGCURRENT, &reg);
		val->intval =
			max172xx_current_to_voltage(reg) / (int)info->rsense;
		break;
	/*
	 * Strings already received and inited by probe.
	 * We do dummy read for check battery still available.
	 */
	case POWER_SUPPLY_PROP_MODEL_NAME:
		ret = regmap_read(info->regmap, MAX1721X_REG_DEV_STR, &reg);
		val->strval = info->DeviceName;
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		ret = regmap_read(info->regmap, MAX1721X_REG_MFG_STR, &reg);
		val->strval = info->ManufacturerName;
		break;
	case POWER_SUPPLY_PROP_SERIAL_NUMBER:
		ret = regmap_read(info->regmap, MAX1721X_REG_SER_HEX, &reg);
		val->strval = info->SerialNumber;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static enum power_supply_property max1721x_battery_props[] = {
	/* int */
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_AVG,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	POWER_SUPPLY_PROP_TIME_TO_FULL_AVG,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	/* strings */
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_SERIAL_NUMBER,
};

static int get_string(struct max17211_device_info *info,
			uint16_t reg, uint8_t nr, char *str)
{
	unsigned int val;

	if (!str || !(reg == MAX1721X_REG_MFG_STR ||
			reg == MAX1721X_REG_DEV_STR))
		return -EFAULT;

	while (nr--) {
		if (regmap_read(info->regmap, reg++, &val))
			return -EFAULT;
		*str++ = val>>8 & 0x00FF;
		*str++ = val & 0x00FF;
	}
	return 0;
}

/* Maxim say: Serial number is a hex string up to 12 hex characters */
static int get_sn_string(struct max17211_device_info *info, char *str)
{
	unsigned int val[3];

	if (!str)
		return -EFAULT;

	if (regmap_read(info->regmap, MAX1721X_REG_SER_HEX, &val[0]))
		return -EFAULT;
	if (regmap_read(info->regmap, MAX1721X_REG_SER_HEX + 1, &val[1]))
		return -EFAULT;
	if (regmap_read(info->regmap, MAX1721X_REG_SER_HEX + 2, &val[2]))
		return -EFAULT;

	snprintf(str, 13, "%04X%04X%04X", val[0], val[1], val[2]);
	return 0;
}

/*
 * MAX1721x registers description for w1-regmap
 */
static const struct regmap_range max1721x_allow_range[] = {
	regmap_reg_range(0, 0xDF),	/* volatile data */
	regmap_reg_range(0x180, 0x1DF),	/* non-volatile memory */
	regmap_reg_range(0x1E0, 0x1EF),	/* non-volatile history (unused) */
};

static const struct regmap_range max1721x_deny_range[] = {
	/* volatile data unused registers */
	regmap_reg_range(0x24, 0x26),
	regmap_reg_range(0x30, 0x31),
	regmap_reg_range(0x33, 0x34),
	regmap_reg_range(0x37, 0x37),
	regmap_reg_range(0x3B, 0x3C),
	regmap_reg_range(0x40, 0x41),
	regmap_reg_range(0x43, 0x44),
	regmap_reg_range(0x47, 0x49),
	regmap_reg_range(0x4B, 0x4C),
	regmap_reg_range(0x4E, 0xAF),
	regmap_reg_range(0xB1, 0xB3),
	regmap_reg_range(0xB5, 0xB7),
	regmap_reg_range(0xBF, 0xD0),
	regmap_reg_range(0xDB, 0xDB),
	/* hole between volatile and non-volatile registers */
	regmap_reg_range(0xE0, 0x17F),
};

static const struct regmap_access_table max1721x_regs = {
	.yes_ranges	= max1721x_allow_range,
	.n_yes_ranges	= ARRAY_SIZE(max1721x_allow_range),
	.no_ranges	= max1721x_deny_range,
	.n_no_ranges	= ARRAY_SIZE(max1721x_deny_range),
};

/*
 * Model Gauge M5 Algorithm output register
 * Volatile data (must not be cached)
 */
static const struct regmap_range max1721x_volatile_allow[] = {
	regmap_reg_range(0, 0xDF),
};

static const struct regmap_access_table max1721x_volatile_regs = {
	.yes_ranges	= max1721x_volatile_allow,
	.n_yes_ranges	= ARRAY_SIZE(max1721x_volatile_allow),
};

/*
 * W1-regmap config
 */
static const struct regmap_config max1721x_regmap_w1_config = {
	.reg_bits = 16,
	.val_bits = 16,
	.rd_table = &max1721x_regs,
	.volatile_table = &max1721x_volatile_regs,
	.max_register = MAX1721X_MAX_REG_NR,
};

static int w1_max1721x_add_device(struct w1_slave *sl)
{
	struct power_supply_config psy_cfg = {};
	struct max17211_device_info *info;

	info = devm_kzalloc(&sl->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	sl->family_data = (void *)info;
	info->w1_dev = &sl->dev;

	/*
	 * power_supply class battery name translated from W1 slave device
	 * unical ID (look like 26-0123456789AB) to "max1721x-0123456789AB\0"
	 * so, 26 (device family) correcpondent to max1721x devices.
	 * Device name still unical for any numbers connected devices.
	 */
	snprintf(info->name, sizeof(info->name),
		"max1721x-%012X", (unsigned int)sl->reg_num.id);
	info->bat_desc.name = info->name;

	/*
	 * FixMe: battery device name exceed max len for thermal_zone device
	 * name and translation to thermal_zone must be disabled.
	 */
	info->bat_desc.no_thermal = true;
	info->bat_desc.type = POWER_SUPPLY_TYPE_BATTERY;
	info->bat_desc.properties = max1721x_battery_props;
	info->bat_desc.num_properties = ARRAY_SIZE(max1721x_battery_props);
	info->bat_desc.get_property = max1721x_battery_get_property;
	psy_cfg.drv_data = info;

	/* regmap init */
	info->regmap = regmap_init_w1(info->w1_dev,
					&max1721x_regmap_w1_config);
	if (IS_ERR(info->regmap)) {
		int err = PTR_ERR(info->regmap);

		dev_err(info->w1_dev, "Failed to allocate register map: %d\n",
			err);
		return err;
	}

	/* rsense init */
	info->rsense = 0;
	if (regmap_read(info->regmap, MAX1721X_REG_NRSENSE, &info->rsense))

	if (!info->rsense) {
		dev_warn(info->w1_dev, "RSenese not calibrated, set 10 mOhms!\n");
		info->rsense = 1000; /* in regs in 10^-5 */
	}
	dev_info(info->w1_dev, "RSense: %d mOhms.\n", info->rsense / 100);

	if (get_string(info, MAX1721X_REG_MFG_STR,
			MAX1721X_REG_MFG_NUMB, info->ManufacturerName)) {
		dev_err(info->w1_dev, "Can't read manufacturer. Hardware error.\n");
		return -ENODEV;
	}

	if (!info->ManufacturerName[0])
		strncpy(info->ManufacturerName, DEF_MFG_NAME,
			2 * MAX1721X_REG_MFG_NUMB);

	if (get_string(info, MAX1721X_REG_DEV_STR,
			MAX1721X_REG_DEV_NUMB, info->DeviceName)) {
		dev_err(info->w1_dev, "Can't read device. Hardware error.\n");
		return -ENODEV;
	}
	if (!info->DeviceName[0]) {
		unsigned int dev_name;

		if (regmap_read(info->regmap,
				MAX172XX_REG_DEVNAME, &dev_name)) {
			dev_err(info->w1_dev, "Can't read device name reg.\n");
			return -ENODEV;
		}

		switch (dev_name & MAX172XX_DEV_MASK) {
		case MAX172X1_DEV:
			strncpy(info->DeviceName, DEF_DEV_NAME_MAX17211,
				2 * MAX1721X_REG_DEV_NUMB);
			break;
		case MAX172X5_DEV:
			strncpy(info->DeviceName, DEF_DEV_NAME_MAX17215,
				2 * MAX1721X_REG_DEV_NUMB);
			break;
		default:
			strncpy(info->DeviceName, DEF_DEV_NAME_UNKNOWN,
				2 * MAX1721X_REG_DEV_NUMB);
		}
	}

	if (get_sn_string(info, info->SerialNumber)) {
		dev_err(info->w1_dev, "Can't read serial. Hardware error.\n");
		return -ENODEV;
	}

	info->bat = power_supply_register(&sl->dev, &info->bat_desc,
						&psy_cfg);
	if (IS_ERR(info->bat)) {
		dev_err(info->w1_dev, "failed to register battery\n");
		return PTR_ERR(info->bat);
	}

	return 0;
}

static void w1_max1721x_remove_device(struct w1_slave *sl)
{
	struct max17211_device_info *info =
		(struct max17211_device_info *)sl->family_data;

	power_supply_unregister(info->bat);
	regmap_exit(info->regmap);
	kfree(info);
}

static struct w1_family_ops w1_max1721x_fops = {
	.add_slave = w1_max1721x_add_device,
	.remove_slave = w1_max1721x_remove_device,
};

static struct w1_family w1_max1721x_family = {
	.fid = W1_MAX1721X_FAMILY_ID,
	.fops = &w1_max1721x_fops,
};

module_w1_family(w1_max1721x_family);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alex A. Mihaylov <minimumlaw@rambler.ru>");
MODULE_DESCRIPTION("Maxim MAX17211/MAX17215 Fuel Gauage IC driver");
MODULE_ALIAS("w1-family-" __stringify(W1_MAX1721X_FAMILY_ID));
