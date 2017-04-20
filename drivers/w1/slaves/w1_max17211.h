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

/* ModelGauge m5 Algorithm Output Registers */

#define regTemp		(0x008) /* Temperature */
#define	TEMP_MULTIPLER	10/256  /* in tenths of deg. C */
#define regBatt		(0x0DA) /* Battery voltage */
#define VOLT_MULTIPLER	1250	/* in uV */
#define regCurrent	(0x00A) /* Actual current */
#define regAvgCurrent	(0x00B) /* Average current */
#define CURR_FULL_SCALE	5120	/* 51,2mV in 10uV */
#define regnRSense	(0x1CF) /* Factory RSense in 10uOhm */
#define regRepSOC	(0x006) /* percentage of charge */
#define PERC_MULTIPLER	1/256	/* in percent from 0 to 100 */

#define regMfgStr	(0x1CC)
#define regMfgNumb	3
#define regDevStr	(0x1DB)
#define	regDevNumb	5
#define regSerStr	(0x1D8)
#define regSerNumb	3

extern int w1_max17211_reg_get(struct device *dev, uint16_t addr,
			uint16_t *val);
extern int w1_max17211_reg_set(struct device *dev, uint16_t addr,
			uint16_t val);

#endif /* !__w1_max17211_h__ */
