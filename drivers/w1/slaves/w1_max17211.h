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

/* Known commands to the MAX17211 chip */
#define W1_MAX17211_READ_DATA		0x69
#define W1_MAX17211_WRITE_DATA		0x6C

/* Number of valid register addresses */
#define MAX17211_MAX_REG_NR	(0x1EF)

/* MAX17211/MAX17215 Output Registers */

#define REG_TEMP	0x008	/* Temperature */
#define TEMP_MULTIPLER	10/256 	/* in tenths of deg. C */
#define REG_BATT	0x0DA	/* Battery voltage */
#define VOLT_MULTIPLER	1250	/* in uV */
#define REG_CURRENT	0x00A	/* Actual current */
#define REG_AVGCURRENT	0x00B	/* Average current */
/**********************************************************
 * Calculating current registers resolution:
 *
 * RSense stored in 10^-5 Ohm, so multiple fullscale to 10^5.
 * Current will be in uA becourse fullscale voltage in uV.
 * 16 bit current reg fullscale +/-51.2mV is 102400 uV.
 * So: 100000 * 102400 / 65535 = 156252
 **********************************************************/
#define CURR_MULTIPLER	156252
#define REG_NRSENSE	0x1CF	/* Factory RSense in 10^-5 Ohm */
#define REG_REPSOC	0x006	/* percentage of charge */
#define PERC_MULTIPLER	1/256	/* in percent from 0 to 100 */
#define REG_DESIGNCAP	0x018	/* All capacity in 2mAh resolution */
#define REG_REPCAP	0x005
#define CAP_MULTIPLER	500	/* Convert to uAh */
#define REG_TTE		0x011	/* Time to full/empty */
#define REG_TTF		0x020	/* 102.4 hour fullscale or 5.625sec/bit */
#define TIME_MULTIPLER	5625/1000
/* Strings */
#define REG_MFG_STR	0x1CC
#define REG_MFG_NUMB	3
#define REG_DEV_STR	0x1DB
#define REG_DEV_NUMB	5
/* HEX Strings */
#define REG_SER_HEX	0x1D8

extern int w1_max17211_reg_get(struct device *dev, uint16_t addr,
			uint16_t *val);
extern int w1_max17211_reg_set(struct device *dev, uint16_t addr,
			uint16_t val);

#endif /* !__w1_max17211_h__ */
