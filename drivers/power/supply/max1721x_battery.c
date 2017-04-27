/*
 * 1-wire client/driver for the Maxim MAX17211/MAX17215 Fuel Gauge IC
 *
 * Author: Alex A. Mihaylov <minimumlaw@rambler.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/param.h>
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/idr.h>

#include "../../w1/w1.h"
#include "../../w1/slaves/w1_max1721x.h"

#define DEF_DEV_NAME_MAX17211	"MAX17211"
#define DEF_DEV_NAME_MAX17215	"MAX17215"
#define DEF_DEV_NAME_UNKNOWN	"UNKNOWN"
#define DEF_MFG_NAME		"MAXIM"

struct max17211_device_info {
	struct device *dev;
	struct power_supply *bat;
	struct power_supply_desc bat_desc;
	struct device *w1_dev;
	/* battery design format */
	uint16_t rsense; /* in tenths uOhm */
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
	uint16_t reg;
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		/*
		 * POWER_SUPPLY_PROP_PRESENT will always readable via
		 * sysfs interface. Value return 0 if battery not
		 * present or unaccesable via W1.
		 */
		val->intval =
			w1_max1721x_reg_get(info->w1_dev, MAX172XX_REG_STATUS,
				&reg) ? 0 : !(reg & MAX172XX_BAT_PRESENT);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		ret = w1_max1721x_reg_get(info->w1_dev,
			MAX172XX_REG_REPSOC, &reg);
		val->intval = max172xx_percent_to_ps(reg);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = w1_max1721x_reg_get(info->w1_dev,
			MAX172XX_REG_BATT, &reg);
		val->intval = max172xx_voltage_to_ps(reg);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		ret = w1_max1721x_reg_get(info->w1_dev,
			MAX172XX_REG_DESIGNCAP, &reg);
		val->intval = max172xx_capacity_to_ps(reg);
		break;
	case POWER_SUPPLY_PROP_CHARGE_AVG:
		ret = w1_max1721x_reg_get(info->w1_dev,
			MAX172XX_REG_REPCAP, &reg);
		val->intval = max172xx_capacity_to_ps(reg);
		break;
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG:
		ret = w1_max1721x_reg_get(info->w1_dev,
			MAX172XX_REG_TTE, &reg);
		val->intval = max172xx_time_to_ps(reg);
		break;
	case POWER_SUPPLY_PROP_TIME_TO_FULL_AVG:
		ret = w1_max1721x_reg_get(info->w1_dev,
			MAX172XX_REG_TTF, &reg);
		val->intval = max172xx_time_to_ps(reg);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		ret = w1_max1721x_reg_get(info->w1_dev,
			MAX172XX_REG_TEMP, &reg);
		val->intval = max172xx_temperature_to_ps(reg);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = w1_max1721x_reg_get(info->w1_dev,
			MAX172XX_REG_CURRENT, &reg);
		val->intval = max172xx_current_to_voltage(reg) / info->rsense;
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		ret = w1_max1721x_reg_get(info->w1_dev,
			MAX172XX_REG_AVGCURRENT, &reg);
		val->intval = max172xx_current_to_voltage(reg) / info->rsense;
		break;
	/*
	 * Strings already received and inited by probe.
	 * We do dummy read for check battery still available.
	 */
	case POWER_SUPPLY_PROP_MODEL_NAME:
		ret = w1_max1721x_reg_get(info->w1_dev,
			MAX1721X_REG_DEV_STR, &reg);
		val->strval = info->DeviceName;
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		ret = w1_max1721x_reg_get(info->w1_dev,
			MAX1721X_REG_MFG_STR, &reg);
		val->strval = info->ManufacturerName;
		break;
	case POWER_SUPPLY_PROP_SERIAL_NUMBER:
		ret = w1_max1721x_reg_get(info->w1_dev,
			MAX1721X_REG_SER_HEX, &reg);
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

static int get_string(struct device *dev, uint16_t reg, uint8_t nr, char *str)
{
	uint16_t val;

	if (!str || !(reg == MAX1721X_REG_MFG_STR ||
			reg == MAX1721X_REG_DEV_STR))
		return -EFAULT;

	while (nr--) {
		if (w1_max1721x_reg_get(dev, reg++, &val))
			return -EFAULT;
		*str++ = val>>8 & 0x00FF;
		*str++ = val & 0x00FF;
	}
	return 0;
}

/* Maxim say: Serial number is a hex string up to 12 hex characters */
static int get_sn_string(struct device *dev, char *str)
{
	uint16_t val[3];

	if (!str)
		return -EFAULT;

	if (w1_max1721x_reg_get(dev, MAX1721X_REG_SER_HEX, &val[0]))
		return -EFAULT;
	if (w1_max1721x_reg_get(dev, MAX1721X_REG_SER_HEX + 1, &val[1]))
		return -EFAULT;
	if (w1_max1721x_reg_get(dev, MAX1721X_REG_SER_HEX + 2, &val[2]))
		return -EFAULT;

	snprintf(str, 13, "%04X%04X%04X", val[0], val[1], val[2]);
	return 0;
}

static int max1721x_battery_probe(struct platform_device *pdev)
{
	struct power_supply_config psy_cfg = {};
	struct max17211_device_info *info;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	platform_set_drvdata(pdev, info);

	info->dev = &pdev->dev;
	info->w1_dev = pdev->dev.parent;
	info->bat_desc.name = dev_name(&pdev->dev);
	info->bat_desc.type = POWER_SUPPLY_TYPE_BATTERY;
	info->bat_desc.properties = max1721x_battery_props;
	info->bat_desc.num_properties = ARRAY_SIZE(max1721x_battery_props);
	info->bat_desc.get_property = max1721x_battery_get_property;
	/*
	 * FixMe:
	 * Device without no_thermal = true not register (err -22)
	 * Len of platform device name "max17211-battery.X.auto"
	 * more than 20 chars limit in THERMAL_NAME_LENGTH from
	 * include/uapi/linux/thermal.h
	 */
	info->bat_desc.no_thermal = true;
	psy_cfg.drv_data = info;

	if (w1_max1721x_reg_get(info->w1_dev,
			MAX1721X_REG_NRSENSE, &info->rsense))
		return -ENODEV;

	if (!info->rsense) {
		dev_warn(info->dev, "RSenese not calibrated, set 10 mOhms!\n");
		info->rsense = 1000; /* in regs in 10^-5 */
	}
	dev_dbg(info->dev, "RSense: %d mOhms.\n", info->rsense / 100);

	if (get_string(info->w1_dev, MAX1721X_REG_MFG_STR,
			MAX1721X_REG_MFG_NUMB, info->ManufacturerName)) {
		dev_err(info->dev, "Can't read manufacturer. Hardware error.\n");
		return -ENODEV;
	}

	if (!info->ManufacturerName[0])
		strncpy(info->ManufacturerName, DEF_MFG_NAME,
			2 * MAX1721X_REG_MFG_NUMB);

	if (get_string(info->w1_dev, MAX1721X_REG_DEV_STR,
			MAX1721X_REG_DEV_NUMB, info->DeviceName)) {
		dev_err(info->dev, "Can't read device. Hardware error.\n");
		return -ENODEV;
	}
	if (!info->DeviceName[0]) {
		uint16_t dev_name;

		if (w1_max1721x_reg_get(info->w1_dev,
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

	if (get_sn_string(info->w1_dev, info->SerialNumber)) {
		dev_err(info->dev, "Can't read serial. Hardware error.\n");
		return -ENODEV;
	}

	info->bat = power_supply_register(&pdev->dev, &info->bat_desc,
						&psy_cfg);
	if (IS_ERR(info->bat)) {
		dev_err(info->dev, "failed to register battery\n");
		return PTR_ERR(info->bat);
	}

	return 0;
}

static int max1721x_battery_remove(struct platform_device *pdev)
{
	struct max17211_device_info *info = platform_get_drvdata(pdev);

	power_supply_unregister(info->bat);

	return 0;
}

static struct platform_driver max1721x_battery_driver = {
	.driver = {
		.name = "max1721x-battery",
	},
	.probe	  = max1721x_battery_probe,
	.remove   = max1721x_battery_remove,
};
module_platform_driver(max1721x_battery_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alex A. Mihaylov <minimumlaw@rambler.ru>");
MODULE_DESCRIPTION("Maxim MAX17211/MAX17215 Fuel Gauage IC driver");
MODULE_ALIAS("platform:max1721x-battery");
