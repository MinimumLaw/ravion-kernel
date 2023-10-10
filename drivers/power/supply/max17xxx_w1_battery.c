/*
 * 1-Wire implementation for Maxim Semiconductor
 * MAX7211/MAX17215 standalone fuel gauge chip
 *
 * Copyright (C) 2021 Radioavionica Corporation
 * Author: Alex A. Mihaylov <minimumlaw@rambler.ru>
 *
 * Use consistent with the GNU GPL is permitted,
 * provided that this copyright notice is
 * preserved in its entirety in all copies and derived works.
 *
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/w1.h>
#include <linux/regmap.h>
#include <linux/power_supply.h>

#define W1_MAX172XX_FAMILY_ID		0x26

#define DEF_DEV_NAME_MAX172X1		"MAX172X1"
#define DEF_DEV_NAME_MAX172X5		"MAX172X5"
#define DEF_DEV_NAME_MAX173X0		"MAX173X1"
#define DEF_DEV_NAME_MAX173X1		"MAX173X1"
#define DEF_DEV_NAME_MAX173X2		"MAX173X15"
#define DEF_DEV_NAME_MAX173X3		"MAX173X15"
#define DEF_DEV_NAME_MAX17320		"MAX17320"
#define DEF_DEV_NAME_MAX17320_CHINA	"MAX17320 CHINA"
#define DEF_DEV_NAME_MAX17330		"MAX17330"
#define DEF_DEV_NAME_UNKNOWN		"UNKNOWN"
#define DEF_MFG_NAME			"MAXIM"
#define ADI_MFG_NAME			"ADI"

#define MAX172XX_REG_DEVNAME	0x021	/* DevName regiter */
#define MAX172XX_DEVID_MASK	0x000F	/* Device field in DevName register */
#define MAX172XX_DEVREV_MASK	0xFFF0	/* Revision field in DevName register */
#define MAX172X1_DEV_ID		0x01
#define MAX172X5_DEV_ID		0x05
#define MAX173X0_DEVNAME	0x4069
#define MAX173X1_DEVNAME	0x4065
#define MAX173X2_DEVNAME	0x4066
#define MAX173X3_DEVNAME	0x4067
#define MAX17320_DEVNAME	0x4209
#define MAX17320_CHINA_DEVNAME	0x420A
#define MAX17330_DEVNAME	0x4309 /* FixMe: correct value required!!! */

#define PSY_MAX_NAME_LEN	32

/* Number of valid register addresses in W1 mode */
#define MAX1721X_MAX_REG_NR	0x1EF

/* Factory settings (nonvolatile registers) (W1 specific) */
#define MAX1721X_REG_NRSENSE	0x1CF	/* RSense in 10^-5 Ohm */
/* Strings */
#define MAX1721X_REG_MFG_STR	0x1CC
#define MAX1721X_REG_MFG_NUMB	3
#define MAX1721X_REG_DEV_STR	0x1DB
#define MAX1721X_REG_DEV_NUMB	5
/* HEX Strings */
#define MAX1721X_REG_SER_HEX	0x1D8

/* MAX172XX Output Registers for W1 chips */
#define MAX172XX_REG_STATUS	0x000	/* status reg */
#define MAX172XX_BAT_PRESENT	(1<<4)	/* battery connected bit */
#define MAX172XX_REG_TEMP	0x008	/* Temperature */
#define MAX172XX_REG_BATT	0x0DA	/* Battery voltage */
#define MAX172XX_REG_CURRENT	0x00A	/* Actual current */
#define MAX172XX_REG_AVGCURRENT	0x00B	/* Average current */
#define MAX172XX_REG_REPSOC	0x006	/* Percentage of charge */
#define MAX172XX_REG_DESIGNCAP	0x018	/* Design capacity */
#define MAX172XX_REG_REPCAP	0x005	/* Average capacity */
#define MAX172XX_REG_TTE	0x011	/* Time to empty */
#define MAX172XX_REG_TTF	0x020	/* Time to full */

/* MAX173XX Output Registers for W1 chips */
#define MAX173XX_REG_TEMP	0x01b	/* Temperature */
#define MAX173XX_REG_CURRENT	0x01c	/* Actual current */
#define MAX173XX_REG_AVGCURRENT	0x01d	/* Average current */

/* MAX17xxx SBS Compatible area STATUS register */
#define MAX17XXX_REG_SBS_STATUS		0x116
#define MAX17XXX_REG_SBS_CURRENT	0x10A /* charge/discharge status */
#define SBS_STATUS_INITIALIZED		0x0080
#define SBS_STATUS_DISCHARGING		0x0040
#define SBS_STATUS_FULLY_CHARGED	0x0020
#define SBS_STATUS_FULLY_DISCHARGED	0x0010

struct chip_to_ps {
	/* function for convert raw regs to powersource class value */
	int (*to_time)(unsigned int reg);
	int (*to_percent)(unsigned int reg);
	int (*to_voltage)(unsigned int reg);
	int (*to_capacity)(unsigned int reg);
	int (*to_temperature)(unsigned int reg);
	int (*to_sense_voltage)(unsigned int reg);
};

struct max172xx_device_info {
	char name[PSY_MAX_NAME_LEN];
	struct power_supply *bat;
	struct power_supply_desc bat_desc;
	struct device *w1_dev;
	struct regmap *regmap;
	struct chip_to_ps* helper;
	/* battery design format */
	unsigned int rsense; /* in tenths uOhm */
	unsigned int TempRegister;
	unsigned int CurrentRegister;
	unsigned int AvgCurrentRegister;
	char DeviceName[16]; /* MAX17320 CHINA\0 */
	char ManufacturerName[7]; /* MAXIM_\0 */
	char SerialNumber[13]; /* see get_sn_str() later for comment */
};

/*
 * Convert regs value to power_supply units
 */
static int max172xx_time_to_ps(unsigned int reg)
{
	return reg * 5625 / 1000;	/* in sec. */
}

static int max173xx_time_to_ps(unsigned int reg)
{
	return reg * 5625 / 1000;	/* in sec. */
}

static int max172xx_percent_to_ps(unsigned int reg)
{
	return reg / 256;	/* in percent from 0 to 100 */
}

static int max173xx_percent_to_ps(unsigned int reg)
{
	return reg / 256;	/* in percent from 0 to 100 */
}

static int max172xx_voltage_to_ps(unsigned int reg)
{
	return reg * 1250;	/* in uV (1.250uV LSB)*/
}

static int max173xx_voltage_to_ps(unsigned int reg)
{
	return reg * 3125 / 10;	/* in uV (0.3125uV LSB) */
}

static int max172xx_capacity_to_ps(unsigned int reg)
{
	return reg * 500;	/* in uAh */
}

static int max173xx_capacity_to_ps(unsigned int reg)
{
	return reg * 500;	/* in uAh */
}

/*
 * Current and temperature is signed values, so unsigned regs
 * value must be converted to signed type
 */

static int max172xx_temperature_to_ps(unsigned int reg)
{
	int val = (int16_t)(reg);

	return val * 10 / 256; /* in tenths of deg. C */
}

static int max173xx_temperature_to_ps(unsigned int reg)
{
	int val = (int16_t)(reg);

	return val * 10 / 256; /* in tenths of deg. C */
}

/*
 * Calculating current registers resolution:
 *
 * RSense stored in 10^-5 Ohm, so measurement voltage must be
 * in 10^-11 Volts for get current in uA.
 * 16 bit current reg fullscale +/-51.2mV is 102400 uV.
 * So: 102400 / 65535 * 10^5 = 156252
 */
static int max172xx_to_sense_voltage(unsigned int reg)
{
	int val = (int16_t)(reg);

	return val * 156252;
}

static int max173xx_to_sense_voltage(unsigned int reg)
{
	int val = (int16_t)(reg);

	return val * 156252;
}


static struct chip_to_ps max172xx_helper = {
	.to_time	= max172xx_time_to_ps,
	.to_percent	= max172xx_percent_to_ps,
	.to_voltage	= max172xx_voltage_to_ps,
	.to_capacity	= max172xx_capacity_to_ps,
	.to_temperature	= max172xx_temperature_to_ps,
	.to_sense_voltage = max172xx_to_sense_voltage,
};

static struct chip_to_ps max173xx_helper = {
	.to_time	= max173xx_time_to_ps,
	.to_percent	= max173xx_percent_to_ps,
	.to_voltage	= max173xx_voltage_to_ps,
	.to_capacity	= max173xx_capacity_to_ps,
	.to_temperature	= max173xx_temperature_to_ps,
	.to_sense_voltage = max173xx_to_sense_voltage,
};

static inline struct max172xx_device_info *
to_device_info(struct power_supply *psy)
{
	return power_supply_get_drvdata(psy);
}

static int max1721x_battery_get_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	struct max172xx_device_info *info = to_device_info(psy);
	unsigned int reg = 0;
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		/*
		 * POWER_SUPPLY_PROP_PRESENT will always readable via
		 * sysfs interface. Value return 0 if battery not
		 * present or unaccessible via W1.
		 */
		val->intval =
			regmap_read(info->regmap, MAX172XX_REG_STATUS,
			&reg) ? 0 : !(reg & MAX172XX_BAT_PRESENT);
		break;
	case POWER_SUPPLY_PROP_STATUS:
		/*
		    POWER_SUPPLY_STATUS_UNKNOWN
		    POWER_SUPPLY_STATUS_CHARGING
		    POWER_SUPPLY_STATUS_DISCHARGING
		    POWER_SUPPLY_STATUS_NOT_CHARGING
		    POWER_SUPPLY_STATUS_FULL
		*/
		ret = regmap_read(info->regmap, MAX17XXX_REG_SBS_STATUS, &reg);
		if (ret)
			val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
		else {
			if (reg & SBS_STATUS_FULLY_CHARGED)
				val->intval = POWER_SUPPLY_STATUS_FULL;
			else if (reg & SBS_STATUS_DISCHARGING)
				val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
			else if (reg & SBS_STATUS_FULLY_DISCHARGED)
				val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
			else if (reg & SBS_STATUS_INITIALIZED) {
				unsigned int reg;
				int sbs_current;

				regmap_read(info->regmap, MAX17XXX_REG_SBS_CURRENT, &reg);
				sbs_current = max173xx_to_sense_voltage(reg);
				if (sbs_current > 0)
					val->intval = POWER_SUPPLY_STATUS_CHARGING;
				else
					val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
			} else
				val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
		};
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		ret = regmap_read(info->regmap, MAX172XX_REG_REPSOC, &reg);
		val->intval = info->helper->to_percent(reg);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = regmap_read(info->regmap, MAX172XX_REG_BATT, &reg);
		val->intval = info->helper->to_voltage(reg);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		ret = regmap_read(info->regmap, MAX172XX_REG_DESIGNCAP, &reg);
		val->intval = info->helper->to_capacity(reg);
		break;
	case POWER_SUPPLY_PROP_CHARGE_AVG:
		ret = regmap_read(info->regmap, MAX172XX_REG_REPCAP, &reg);
		val->intval = info->helper->to_capacity(reg);
		break;
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG:
		ret = regmap_read(info->regmap, MAX172XX_REG_TTE, &reg);
		val->intval = info->helper->to_time(reg);
		break;
	case POWER_SUPPLY_PROP_TIME_TO_FULL_AVG:
		ret = regmap_read(info->regmap, MAX172XX_REG_TTF, &reg);
		val->intval = info->helper->to_time(reg);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		ret = regmap_read(info->regmap, info->TempRegister, &reg);
		val->intval = info->helper->to_temperature(reg);
		break;
	/* We need signed current, so must cast info->rsense to signed type */
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = regmap_read(info->regmap, info->CurrentRegister, &reg);
		val->intval =
			info->helper->to_sense_voltage(reg) / (int)info->rsense;
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		ret = regmap_read(info->regmap, info->AvgCurrentRegister, &reg);
		val->intval =
			info->helper->to_sense_voltage(reg) / (int)info->rsense;
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
	POWER_SUPPLY_PROP_STATUS,
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
	POWER_SUPPLY_PROP_MODEL_NAME,		/* FixMe: chip name  */
	POWER_SUPPLY_PROP_MANUFACTURER,		/* FixMe: chip manufacturer */
	POWER_SUPPLY_PROP_SERIAL_NUMBER,

};

/* Maxim say: Serial number is a hex string up to 12 hex characters */
static int get_sn_string(struct max172xx_device_info *info, char *str)
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
	regmap_reg_range(0x100, 0x17F), /* SBS Compliant Memory */
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
	regmap_reg_range(0xE0, 0xFF),
	/* SBS Compliant Memory */
	regmap_reg_range(0x105, 0x105),
	regmap_reg_range(0x11d, 0x11F),
	regmap_reg_range(0x124, 0x134),
	regmap_reg_range(0x13A, 0x13A),
	regmap_reg_range(0x140, 0x14B),
	regmap_reg_range(0x150, 0x167),
	regmap_reg_range(0x169, 0x16C),
	regmap_reg_range(0x16E, 0x16F),
	regmap_reg_range(0x171, 0x17F),
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

static int devm_w1_max1721x_add_device(struct w1_slave *sl)
{
	struct power_supply_config psy_cfg = {};
	struct max172xx_device_info *info;
	unsigned int dev_name;

	info = devm_kzalloc(&sl->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	sl->family_data = (void *)info;
	info->w1_dev = &sl->dev;

	/*
	 * power_supply class battery name translated from W1 slave device
	 * unique ID (look like 26-0123456789AB) to "bat-0123456789AB\0"
	 * so, 26 (device family) correspond to max172xx devices.
	 * Device name still unique for any number of connected devices.
	 */
	snprintf(info->name, sizeof(info->name),
		"bat-%012X", (unsigned int)sl->reg_num.id);
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
	info->regmap = devm_regmap_init_w1(info->w1_dev,
					&max1721x_regmap_w1_config);
	if (IS_ERR(info->regmap)) {
		int err = PTR_ERR(info->regmap);

		dev_err(info->w1_dev, "Failed to allocate register map: %d\n",
			err);
		return err;
	}

	/* rsense init */
	info->rsense = 0;
	if (regmap_read(info->regmap, MAX1721X_REG_NRSENSE, &info->rsense)) {
		dev_err(info->w1_dev, "Can't read RSense. Hardware error.\n");
		return -ENODEV;
	}

	if (!info->rsense) {
		dev_warn(info->w1_dev, "RSense not calibrated, set 10 mOhms!\n");
		info->rsense = 1000; /* in regs in 10^-5 */
	}
	dev_info(info->w1_dev, "RSense: %d mOhms.\n", info->rsense / 100);

	/*
	 * Controller specific init
	 */
	if (regmap_read(info->regmap, MAX172XX_REG_DEVNAME, &dev_name)) {
		dev_err(info->w1_dev, "Can't read device name reg.\n");
		return -ENODEV;
	}

	/*
	 * Fill data for new (MAX173xx) series.
	 * Redefine for for old (MAX172xx) series later (if required)
	*/
	info->helper		= &max173xx_helper;
	info->TempRegister	= MAX173XX_REG_TEMP;
	info->CurrentRegister	= MAX173XX_REG_CURRENT;
	info->AvgCurrentRegister= MAX173XX_REG_AVGCURRENT;

	/* Ignore ManufacturerName (format uncknown) - set to "MAXIM" */
	strncpy(info->ManufacturerName, DEF_MFG_NAME,
		sizeof(info->ManufacturerName) - 1);

	switch (dev_name) {
	case MAX17330_DEVNAME:
		strncpy(info->DeviceName, DEF_DEV_NAME_MAX17330,
			sizeof(info->DeviceName) - 1);
		break;
	case MAX17320_CHINA_DEVNAME:
		/* Ignore ManufacturerName (format uncknown) - set to "ADI" */
		strncpy(info->ManufacturerName, ADI_MFG_NAME,
			sizeof(info->ManufacturerName) - 1);
		strncpy(info->DeviceName, DEF_DEV_NAME_MAX17320_CHINA,
			sizeof(info->DeviceName) - 1);
		break;
	case MAX17320_DEVNAME:
		strncpy(info->DeviceName, DEF_DEV_NAME_MAX17320,
			sizeof(info->DeviceName) - 1);
		break;
	case MAX173X0_DEVNAME:
		strncpy(info->DeviceName, DEF_DEV_NAME_MAX173X0,
			sizeof(info->DeviceName) - 1);
		break;
	case MAX173X1_DEVNAME:
		strncpy(info->DeviceName, DEF_DEV_NAME_MAX173X1,
			sizeof(info->DeviceName) - 1);
		break;
	case MAX173X2_DEVNAME:
		strncpy(info->DeviceName, DEF_DEV_NAME_MAX173X2,
			sizeof(info->DeviceName) -1);
		break;
	case MAX173X3_DEVNAME:
		strncpy(info->DeviceName, DEF_DEV_NAME_MAX173X3,
			sizeof(info->DeviceName) - 1);
		break;
	default: /* older (MAX172xx) series check */
	    info->helper = &max172xx_helper;
	    info->TempRegister		= MAX172XX_REG_TEMP;
	    info->CurrentRegister	= MAX172XX_REG_CURRENT;
	    info->AvgCurrentRegister	= MAX172XX_REG_AVGCURRENT;

	    switch (dev_name & MAX172XX_DEVID_MASK) { /* MAX172XX not specify revision field */
		case MAX172X1_DEV_ID:
			strncpy(info->DeviceName, DEF_DEV_NAME_MAX172X1,
				sizeof(info->DeviceName) - 1);
			break;
		case MAX172X5_DEV_ID:
			strncpy(info->DeviceName, DEF_DEV_NAME_MAX172X5,
				sizeof(info->DeviceName) - 1);
			break;
		default:
			dev_err(info->w1_dev, "Found ModelGauge m5 Fuel Gaige controller with unsupported chip 0x%04X", dev_name);
			return -ENOTSUPP;
	    };
	}

	if (get_sn_string(info, info->SerialNumber)) {
		dev_err(info->w1_dev, "Can't read serial. Hardware error.\n");
		return -ENODEV;
	}

	info->bat = devm_power_supply_register(&sl->dev, &info->bat_desc,
						&psy_cfg);
	if (IS_ERR(info->bat)) {
		dev_err(info->w1_dev, "failed to register battery\n");
		return PTR_ERR(info->bat);
	}
	return 0;
}

static const struct w1_family_ops w1_max172xx_fops = {
	.add_slave = devm_w1_max1721x_add_device,
};

static struct w1_family w1_max1721x_family = {
	.fid = W1_MAX172XX_FAMILY_ID,
	.fops = &w1_max172xx_fops,
};

module_w1_family(w1_max1721x_family);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alex A. Mihaylov <minimumlaw@rambler.ru>");
MODULE_DESCRIPTION("Maxim MAX172xx/MAX173xx Fuel Gauage IC driver");
MODULE_ALIAS("w1-family-" __stringify(W1_MAX172XX_FAMILY_ID));
