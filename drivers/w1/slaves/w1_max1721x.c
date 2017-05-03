/*
 * 1-Wire implementation for Maxim Semiconductor
 * MAX17211/MAX17215 standalone fuel gauge chip
 *
 * Copyright (C) 2017 OAO Radioavionica
 * Author: Alex A. Mihaylov <minimumlaw@rambler.ru>
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
#include "w1_max1721x.h"

static int w1_max17211_add_device(struct w1_slave *sl)
{
	int ret;
	struct platform_device *pdev;

	pdev = platform_device_alloc("max1721x-battery", PLATFORM_DEVID_AUTO);
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
};

static struct w1_family w1_max17211_family = {
	.fid = W1_FAMILY_MAX17211,
	.fops = &w1_max17211_fops,
};
module_w1_family(w1_max17211_family);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alex A. Mihaylov <minimumlaw@rambler.ru>");
MODULE_DESCRIPTION("1-wire Driver for MAX17211/MAX17215 battery monitor");
MODULE_ALIAS("w1-family-" __stringify(W1_FAMILY_MAX17211));
