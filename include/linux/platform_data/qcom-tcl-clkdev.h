/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _QCOM_TCL_CLKDEV_H
#define _QCOM_TCL_CLKDEV_H

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/device.h>
#include <linux/overflow.h>
#include <linux/slab.h>
#include <linux/string.h>

/* clkdev_create() has fixed-size ID buffers. Own both complete strings in
 * the same allocation as the lookup, so the normal clkdev_drop() frees all.
 */
static inline struct clk_lookup *
qcom_tcl_clkdev_lookup(struct clk_hw *hw, const char *con_id, const char *dev_id)
{
	struct clk_lookup *cl;
	struct clk *check;
	size_t con_size, dev_size;
	char *names;

	if (IS_ERR_OR_NULL(hw) || !con_id || !dev_id)
		return ERR_PTR(-EINVAL);
	con_size = size_add(strlen(con_id), 1);
	dev_size = size_add(strlen(dev_id), 1);
	cl = kzalloc(size_add(sizeof(*cl), size_add(con_size, dev_size)), GFP_KERNEL);
	if (!cl)
		return ERR_PTR(-ENOMEM);
	names = (char *)(cl + 1);
	cl->con_id = memcpy(names, con_id, con_size);
	cl->dev_id = memcpy(names + con_size, dev_id, dev_size);
	cl->clk_hw = hw;
	clkdev_add(cl);
	/* Verify the exact consumer-visible lookup, not just allocation success. */
	check = clk_get_sys(dev_id, con_id);
	if (IS_ERR(check)) {
		clkdev_drop(cl);
		return ERR_CAST(check);
	}
	if (__clk_get_hw(check) != hw) {
		clk_put(check);
		clkdev_drop(cl);
		return ERR_PTR(-EEXIST);
	}
	clk_put(check);
	return cl;
}

static inline void qcom_tcl_clkdev_drop(void *data)
{
	clkdev_drop(data);
}

static inline int
qcom_tcl_devm_clkdev(struct device *dev, struct clk_hw *hw,
		     const char *con_id, const char *dev_id)
{
	struct clk_lookup *cl = qcom_tcl_clkdev_lookup(hw, con_id, dev_id);

	if (IS_ERR(cl))
		return PTR_ERR(cl);
	return devm_add_action_or_reset(dev, qcom_tcl_clkdev_drop, cl);
}
#endif
