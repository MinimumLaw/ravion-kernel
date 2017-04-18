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
#define MAX17211_REGS_NR		(0x200)
#define MAX17211_DATA_SIZE		(MAX17211_REGS_NR << 1)

extern int w1_max17211_read(struct device *dev, char *buf, int addr,
			  size_t count);
extern int w1_max17211_write(struct device *dev, char *buf, int addr,
			   size_t count);

#endif /* !__w1_max17211_h__ */
