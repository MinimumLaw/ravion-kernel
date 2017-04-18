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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/idr.h>
#include <linux/gfp.h>

#include "../w1.h"
#include "../w1_int.h"
#include "../w1_family.h"
#include "w1_max17211.h"

static int w1_max17211_io(struct device *dev, char *buf, int addr, size_t count,
			int io)
{
	struct w1_slave *sl = container_of(dev, struct w1_slave, dev);

	if (!dev)
		return 0;

	mutex_lock(&sl->master->bus_mutex);

	if (addr > MAX17211_DATA_SIZE || addr < 0) {
		count = 0;
		goto out;
	}
	if (addr + count > MAX17211_DATA_SIZE)
		count = MAX17211_DATA_SIZE - addr;

	if (!w1_reset_select_slave(sl)) {
		if (!io) {
			int readen = 0;

			dev_info(dev,"Slave read from:%d, count:%d!\r\n", addr, count);
			w1_write_8(sl->master, W1_MAX17211_READ_DATA);
			w1_write_8(sl->master, addr & 0x00FF);
			w1_write_8(sl->master, addr>>8 & 0x00FF);
			while(count) {
				u8 curr = count > 128 ? 128 : curr;
				u8 done;
				done = w1_read_block(sl->master, buf + readen, curr);
				readen += done;
				count -= done;
			};
			dev_info(dev,"Slave read count:%d!\r\n", readen);
			count = readen;
		} else {
			dev_info(dev,"Slave write to:%d, count:%d!\r\n", addr, count);
			w1_write_8(sl->master, W1_MAX17211_WRITE_DATA);
			w1_write_8(sl->master, addr & 0x00FF);
			w1_write_8(sl->master, addr>>8 & 0x00FF);
			w1_write_block(sl->master, buf, count);
			/* XXX w1_write_block returns void, not n_written */
		}
	} else {
		dev_info(dev,"Slave select failed!\r\n");
	}

out:
	mutex_unlock(&sl->master->bus_mutex);

	return count;
}

int w1_max17211_read(struct device *dev, char *buf, int addr, size_t count)
{
	return w1_max17211_io(dev, buf, addr, count, 0);
}

int w1_max17211_write(struct device *dev, char *buf, int addr, size_t count)
{
	return w1_max17211_io(dev, buf, addr, count, 1);
}

/* sysfs - simple fuel gauge registers dump interface */

static ssize_t dump_read(struct file *filp, struct kobject *kobj,
			     struct bin_attribute *bin_attr, char *buf,
			     loff_t off, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	return w1_max17211_read(dev, buf, off, count);
}

static BIN_ATTR_RO(dump, MAX17211_DATA_SIZE);

static struct bin_attribute *w1_max17211_bin_attrs[] = {
	&bin_attr_dump,
	NULL,
};

static const struct attribute_group w1_max17211_group = {
	.bin_attrs = w1_max17211_bin_attrs,
};

static const struct attribute_group *w1_max17211_groups[] = {
	&w1_max17211_group,
	NULL,
};

static int w1_max17211_add_device(struct w1_slave *sl)
{
	int ret;
	struct platform_device *pdev;

	pdev = platform_device_alloc("max17211-battery", PLATFORM_DEVID_AUTO);
	if (!pdev)
		return -ENOMEM;
	pdev->dev.parent = &sl->dev;

	ret = platform_device_add(pdev);
	if (ret)
		goto pdev_add_failed;

	dev_set_drvdata(&sl->dev, pdev);

	return 0;

pdev_add_failed:
	platform_device_put(pdev);

	return ret;
}

static void w1_max17211_remove_device(struct w1_slave *sl)
{
	struct platform_device *pdev = dev_get_drvdata(&sl->dev);

	platform_device_unregister(pdev);
}

static struct w1_family_ops w1_max17211_fops = {
	.add_slave    = w1_max17211_add_device,
	.remove_slave = w1_max17211_remove_device,
	.groups       = w1_max17211_groups,
};

static struct w1_family w1_max17211_family = {
	.fid = W1_FAMILY_MAX17211,
	.fops = &w1_max17211_fops,
};
module_w1_family(w1_max17211_family);

EXPORT_SYMBOL(w1_max17211_read);
EXPORT_SYMBOL(w1_max17211_write);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alex A. Mihaylov <minimumlaw@rambler.ru>");
MODULE_DESCRIPTION("1-wire Driver for MAX17211/MAX17215 battery monitor");
MODULE_ALIAS("w1-family-" __stringify(W1_FAMILY_MAX17211));
