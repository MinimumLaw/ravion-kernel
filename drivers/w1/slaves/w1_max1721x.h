/*
 * 1-Wire implementation for the max17211 chip
 *
 * Copyright © 2017, Alex A. Mihaylov <minimumlaw@rambler.ru>
 *
 * Use consistent with the GNU GPL is permitted,
 * provided that this copyright notice is
 * preserved in its entirety in all copies and derived works.
 *
 */

#ifndef __w1_max17211_h__
#define __w1_max17211_h__

/* Known commands to the MAX1721X chip */
#define W1_MAX1721X_READ_DATA		0x69
#define W1_MAX1721X_WRITE_DATA		0x6C

/* Factory settings (nonvilatile registers) (W1 specific) */

#define MAX1721X_REG_NRSENSE	0x1CF	/* RSense in 10^-5 Ohm */
/* Strings */
#define MAX1721X_REG_MFG_STR	0x1CC
#define MAX1721X_REG_MFG_NUMB	3
#define MAX1721X_REG_DEV_STR	0x1DB
#define MAX1721X_REG_DEV_NUMB	5
/* HEX Strings */
#define MAX1721X_REG_SER_HEX	0x1D8

/* Number of valid register addresses */
#define MAX1721X_MAX_REG_NR	(0x1EF)

/* MAX1721X/MAX17215 Output Registers for I2C and W1 chips */

#define MAX172XX_REG_STATUS	0x000	/* status reg */
#define MAX172XX_BAT_PRESENT	(1<<4)	/* battery connected bit */
#define MAX172XX_REG_DEVNAME	0x021	/* chip config */
#define MAX172XX_DEV_MASK	0x000F	/* chip type mask */
#define MAX172X1_DEV		0x0001
#define MAX172X5_DEV		0x0005
#define MAX172XX_REG_TEMP	0x008	/* Temperature */
#define MAX172XX_REG_BATT	0x0DA	/* Battery voltage */
#define MAX172XX_REG_CURRENT	0x00A	/* Actual current */
#define MAX172XX_REG_AVGCURRENT	0x00B	/* Average current */
#define MAX172XX_REG_REPSOC	0x006	/* Percentage of charge */
#define MAX172XX_REG_DESIGNCAP	0x018	/* Design capacity */
#define MAX172XX_REG_REPCAP	0x005	/* Average capacity */
#define MAX172XX_REG_TTE	0x011	/* Time to empty */
#define MAX172XX_REG_TTF	0x020	/* Time to full */

/* Convert regs value to power_supply units */

static inline int max172xx_time_to_ps(uint16_t reg)
{
	return reg * 5625 / 1000;	/* in sec. */
}

static inline int max172xx_percent_to_ps(uint16_t reg)
{
	return reg / 256;	/* in percent from 0 to 100 */
}

static inline int max172xx_voltage_to_ps(uint16_t reg)
{
	return reg * 1250;	/* in uV */
}

static inline int max172xx_capacity_to_ps(uint16_t reg)
{
	return reg * 500;	/* in uAh */
}

/*
 * Current and temperature is signed values, so unsigned regs
 * value must be converted to signed type
 */

static inline int max172xx_temperature_to_ps(uint16_t reg)
{
	return (int16_t)reg * 10 / 256;	/* in tenths of deg. C */
}

/*
 * Calculating current registers resolution:
 *
 * RSense stored in 10^-5 Ohm, so mesaurment voltage must be
 * in 10^-11 Volts for get current in uA.
 * 16 bit current reg fullscale +/-51.2mV is 102400 uV.
 * So: 102400 / 65535 * 10^5 = 156252
 */
static inline int max172xx_current_to_voltage(uint16_t reg)
{
	return (int16_t)reg * 156252;
}

extern int w1_max1721x_reg_get(struct device *dev, uint16_t addr,
			uint16_t *val);
extern int w1_max1721x_reg_set(struct device *dev, uint16_t addr,
			uint16_t val);

#endif /* !__w1_max17211_h__ */
