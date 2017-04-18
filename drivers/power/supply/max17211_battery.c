/*
 * 1-wire client/driver for the Maxim MAX17211/MAX17215 Fuel Gauge IC
 *
 * Author: Alex A. Mihaylov <minimumlaw@rambler.ru>
 *
 * Based on ds2781_battery drivers
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

struct max17211_device_info {
	struct device *dev;
	struct power_supply *bat;
	struct power_supply_desc bat_desc;
	struct device *w1_dev;
};

static const char model[] = "MAX17211";
static const char manufacturer[] = "Maxim/Dallas";

static inline struct max17211_device_info *
to_max17211_device_info(struct power_supply *psy)
{
	return power_supply_get_drvdata(psy);
}

static inline struct power_supply *to_power_supply(struct device *dev)
{
	return dev_get_drvdata(dev);
}

static int max17211_battery_get_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	int ret = 0;
	struct max17211_device_info *dev_info = to_max17211_device_info(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = 1000;
		break;

	case POWER_SUPPLY_PROP_TEMP:
		ret = 2300;
		break;

	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = model;
		break;

	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = manufacturer;
		break;

	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = 100000;
		break;

	case POWER_SUPPLY_PROP_CURRENT_AVG:
		ret = 200000;
		break;

	case POWER_SUPPLY_PROP_STATUS:
		ret = 0;
		break;

	case POWER_SUPPLY_PROP_CAPACITY:
		ret = 48000;
		break;

	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		ret = 350000; //max17211_get_accumulated_current(dev_info, &val->intval);
		break;

	case POWER_SUPPLY_PROP_CHARGE_NOW:
		ret = 1;
		break;

	default:
		ret = -EINVAL;
	}

	return ret;
}

static enum power_supply_property max17211_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_CHARGE_NOW,
};

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

	psy_cfg.drv_data		= dev_info;

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

