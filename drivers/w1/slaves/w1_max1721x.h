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

/* Number of valid register addresses */
#define MAX1721X_MAX_REG_NR	(0x1EF)

/* Multiplers for convert regs to power_supply units */

#define MAX172XX_TEMP_MULTIPLER	10/256 	/* in tenths of deg. C */
#define MAX172XX_VOLT_MULTIPLER	1250	/* in uV */
#define MAX172XX_PERC_MULTIPLER	1/256	/* in percent from 0 to 100 */
#define MAX172XX_CAP_MULTIPLER	500	/* in uAh */
#define MAX172XX_TIME_MULTIPLER	5625/1000	/* in sec. */
/**********************************************************
 * Calculating current registers resolution:
 *
 * RSense stored in 10^-5 Ohm, so multiple fullscale to 10^5.
 * Current will be in uA becourse fullscale voltage in uV.
 * 16 bit current reg fullscale +/-51.2mV is 102400 uV.
 * So: 100000 * 102400 / 65535 = 156252
 **********************************************************/
#define MAX172XX_CURR_MULTIPLER	156252

/* MAX1721X/MAX17215 Output Registers for I2C and W1 chips */

#define MAX172XX_REG_STATUS	0x000
#define MAX172XX_BAT_PRESENT	(1<<4)
#define MAX172XX_REG_DEVNAME	0x021
#define MAX172X1_DEV		0x0001
#define MAX172X5_DEV		0x0005
#define MAX172XX_DEV_MASK	0x000F
#define MAX172XX_REG_TEMP	0x008	/* Temperature */
#define MAX172XX_REG_BATT	0x0DA	/* Battery voltage */
#define MAX172XX_REG_CURRENT	0x00A	/* Actual current */
#define MAX172XX_REG_AVGCURRENT	0x00B	/* Average current */
#define MAX172XX_REG_REPSOC	0x006	/* Percentage of charge */
#define MAX172XX_REG_DESIGNCAP	0x018	/* Design capacity */
#define MAX172XX_REG_REPCAP	0x005	/* Average capacity */
#define MAX172XX_REG_TTE	0x011	/* Time to empty */
#define MAX172XX_REG_TTF	0x020	/* Time to full */

/* Factory settings (nonvilatile registers) (W1 specific) */

#define MAX1721X_REG_NRSENSE	0x1CF	/* RSense in 10^-5 Ohm */
/* Strings */
#define MAX1721X_REG_MFG_STR	0x1CC
#define MAX1721X_REG_MFG_NUMB	3
#define MAX1721X_REG_DEV_STR	0x1DB
#define MAX1721X_REG_DEV_NUMB	5
/* HEX Strings */
#define MAX1721X_REG_SER_HEX	0x1D8

extern int w1_max1721x_reg_get(struct device *dev, uint16_t addr,
			uint16_t *val);
extern int w1_max1721x_reg_set(struct device *dev, uint16_t addr,
			uint16_t val);

#endif /* !__w1_max17211_h__ */
