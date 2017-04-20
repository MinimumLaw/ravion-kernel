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
#include "../../w1/slaves/w1_max17211.h"

#define DEF_DEV_NAME	"MAX17211"
#define DEF_MFG_NAME	"MAXIM"

struct max17211_device_info {
	struct device *dev;
	struct power_supply *bat;
	struct power_supply_desc bat_desc;
	struct device *w1_dev;
	/* battery design format */
	uint16_t rsense; /* in tenths uOhm */
	char DeviceName[2*regDevNumb + 1];
	char ManufacturerName[2*regMfgNumb + 1];
	char SerialNumber[13]; /* see get_sn_str() later for comment */
};

static inline struct max17211_device_info *
to_device_info(struct power_supply *psy)
{
    return power_supply_get_drvdata(psy);
}

static int max17211_battery_get_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	struct max17211_device_info *info = to_device_info(psy);
	uint16_t reg;
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_TEMP:
		if(!w1_max17211_reg_get(info->w1_dev, regTemp, &reg))
			val->intval = (int16_t)reg * TEMP_MULTIPLER;
		else
			val->intval = INT_MIN;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		if(!w1_max17211_reg_get(info->w1_dev, regRepSOC, &reg))
			val->intval = reg * PERC_MULTIPLER;
		else
			val->intval = INT_MIN;
		break;
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		if(!w1_max17211_reg_get(info->w1_dev, regRepSOC, &reg)) {
			reg >>= 8; /* convert to percets */
			if(reg > 95)
			    val->intval = POWER_SUPPLY_CAPACITY_LEVEL_FULL;
			else if(reg > 75)
			    val->intval = POWER_SUPPLY_CAPACITY_LEVEL_HIGH;
			else if(reg > 25)
			    val->intval = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
			else if(reg > 5)
			    val->intval = POWER_SUPPLY_CAPACITY_LEVEL_LOW;
			else
			    val->intval = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
		} else
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_UNKNOWN;
		break;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		if(!w1_max17211_reg_get(info->w1_dev, regBatt, &reg))
			val->intval = reg * VOLT_MULTIPLER;
		else
			val->intval = INT_MIN;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		if(!w1_max17211_reg_get(info->w1_dev, regCurrent, &reg))
			val->intval = (int16_t)reg * CURR_MULTIPLER / info->rsense;
		else
			val->intval = INT_MIN;
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		if(!w1_max17211_reg_get(info->w1_dev, regAvgCurrent, &reg))
			val->intval = (int16_t)reg * CURR_MULTIPLER / info->rsense;
		else
			val->intval = INT_MIN;
		break;
	/* constant props. */
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	/* strings */
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = info->DeviceName;
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = info->ManufacturerName;
		break;
	case POWER_SUPPLY_PROP_SERIAL_NUMBER:
		val->strval = info->SerialNumber;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static enum power_supply_property max17211_battery_props[] = {
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	/* strings */
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_SERIAL_NUMBER,
};

static int get_string(struct device *dev, uint16_t reg, uint8_t nr, char* str)
{
	uint16_t val;
	if(!str || !(reg == regMfgStr || reg == regDevStr))
		 return -EFAULT;
	while(nr--) {
		if(w1_max17211_reg_get(dev, reg++, &val))
			return -EFAULT;
		*str++ = val>>8 & 0x00FF;
		*str++ = val & 0x00FF;
	}
	return 0;
}

/* Maxim say: Serial number is a hex string up to 12 hex characters */
static int get_sn_string(struct device *dev, char* str)
{
	uint16_t val[3];

	if(!str)
		 return -EFAULT;

	if(w1_max17211_reg_get(dev, regSerHex, &val[0]))
		return -EFAULT;
	if(w1_max17211_reg_get(dev, regSerHex + 1, &val[1]))
		return -EFAULT;
	if(w1_max17211_reg_get(dev, regSerHex + 2, &val[2]))
		return -EFAULT;

	snprintf(str,13,"%04X%04X%04X", val[0], val[1], val[2]);
	return 0;
}

static int max17211_battery_probe(struct platform_device *pdev)
{
	struct power_supply_config psy_cfg = {};
	struct max17211_device_info *dev_info;

	dev_info = devm_kzalloc(&pdev->dev, sizeof(*dev_info), GFP_KERNEL);
	if (!dev_info)
		return -ENOMEM;

	platform_set_drvdata(pdev, dev_info);

	dev_info->dev			= &pdev->dev;
	dev_info->w1_dev		= pdev->dev.parent;
	dev_info->bat_desc.name		= dev_name(&pdev->dev);
	dev_info->bat_desc.type		= POWER_SUPPLY_TYPE_BATTERY;
	dev_info->bat_desc.properties	= max17211_battery_props;
	dev_info->bat_desc.num_properties = ARRAY_SIZE(max17211_battery_props);
	dev_info->bat_desc.get_property	= max17211_battery_get_property;
	/* FixMe: 
	 * device without no_thermal = true not register (err -22) */
	dev_info->bat_desc.no_thermal	= true;
	psy_cfg.drv_data		= dev_info;

	if(w1_max17211_reg_get(dev_info->w1_dev, regnRSense, &dev_info->rsense))
		return -ENODEV;

	if(!dev_info->rsense) {
		dev_warn(dev_info->dev, "RSenese not calibrated, set 10 mOhms!\n");
		dev_info->rsense = 1000; /* in regs in 10uOhms */
	};
	dev_info(dev_info->dev,"RSense: %d uOhms.\n", 10*dev_info->rsense);

	if(get_string(dev_info->w1_dev, regMfgStr, regMfgNumb, dev_info->ManufacturerName)) {
		dev_err(dev_info->dev,"Can't read manufacturer. Hardware error.\n");
		return -ENODEV;
	};

	if(!dev_info->ManufacturerName[0])
		strncpy(dev_info->ManufacturerName, DEF_MFG_NAME, 2*regMfgNumb);

	if(get_string(dev_info->w1_dev, regDevStr, regDevNumb, dev_info->DeviceName)) {
		dev_err(dev_info->dev,"Can't read device. Hardware error.\n");
		return -ENODEV;
	};
	if(!dev_info->DeviceName[0])
		strncpy(dev_info->DeviceName, DEF_DEV_NAME, 2*regDevNumb);

	if(get_sn_string(dev_info->w1_dev, dev_info->SerialNumber)) {
		dev_err(dev_info->dev,"Can't read serial. Hardware error.\n");
		return -ENODEV;
	};

	dev_info->bat = power_supply_register(&pdev->dev, &dev_info->bat_desc,
						&psy_cfg);
	if (IS_ERR(dev_info->bat)) {
		dev_err(dev_info->dev, "failed to register battery\n");
		return PTR_ERR(dev_info->bat);
	}

	return 0;
}

static int max17211_battery_remove(struct platform_device *pdev)
{
	struct max17211_device_info *dev_info = platform_get_drvdata(pdev);

	power_supply_unregister(dev_info->bat);

	return 0;
}

static struct platform_driver max17211_battery_driver = {
	.driver = {
		.name = "max17211-battery",
	},
	.probe	  = max17211_battery_probe,
	.remove   = max17211_battery_remove,
};
module_platform_driver(max17211_battery_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alex A. Mihaylov <minimumlaw@rambler.ru>");
MODULE_DESCRIPTION("Maxim MAX17211/MAX17215 Fuel Gauage IC driver");
MODULE_ALIAS("platform:max17211-battery");
