// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020, Linaro Limited

#include <linux/err.h>
#include <linux/init.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/property.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/string.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <dt-bindings/sound/qcom,q6dsp-lpass-ports.h>
#include "q6dsp-lpass-clocks.h"

#define Q6DSP_MAX_CLK_ID			104
#define Q6DSP_LPASS_CLK_ROOT_DEFAULT		0


struct q6dsp_clk {
	struct device *dev;
	int q6dsp_clk_id;
	int attributes;
	int rate;
	uint32_t handle;
	struct clk_hw hw;
};

#define to_q6dsp_clk(_hw) container_of(_hw, struct q6dsp_clk, hw)

struct q6dsp_cc {
	struct device *dev;
	struct q6dsp_clk *clks[Q6DSP_MAX_CLK_ID];
	const struct q6dsp_clk_desc *desc;
};

static int clk_q6dsp_prepare(struct clk_hw *hw)
{
	struct q6dsp_clk *clk = to_q6dsp_clk(hw);
	struct q6dsp_cc *cc = dev_get_drvdata(clk->dev);

	return cc->desc->lpass_set_clk(clk->dev, clk->q6dsp_clk_id, clk->attributes,
				     Q6DSP_LPASS_CLK_ROOT_DEFAULT, clk->rate);
}

static void clk_q6dsp_unprepare(struct clk_hw *hw)
{
	struct q6dsp_clk *clk = to_q6dsp_clk(hw);
	struct q6dsp_cc *cc = dev_get_drvdata(clk->dev);

	cc->desc->lpass_set_clk(clk->dev, clk->q6dsp_clk_id, clk->attributes,
			      Q6DSP_LPASS_CLK_ROOT_DEFAULT, 0);
}

static int clk_q6dsp_set_rate(struct clk_hw *hw, unsigned long rate,
			      unsigned long parent_rate)
{
	struct q6dsp_clk *clk = to_q6dsp_clk(hw);

	clk->rate = rate;

	return 0;
}

static unsigned long clk_q6dsp_recalc_rate(struct clk_hw *hw,
					   unsigned long parent_rate)
{
	struct q6dsp_clk *clk = to_q6dsp_clk(hw);

	return clk->rate;
}

static int clk_q6dsp_determine_rate(struct clk_hw *hw,
				    struct clk_rate_request *req)
{
	return 0;
}

static const struct clk_ops clk_q6dsp_ops = {
	.prepare	= clk_q6dsp_prepare,
	.unprepare	= clk_q6dsp_unprepare,
	.set_rate	= clk_q6dsp_set_rate,
	.determine_rate = clk_q6dsp_determine_rate,
	.recalc_rate	= clk_q6dsp_recalc_rate,
};

static int clk_vote_q6dsp_block(struct clk_hw *hw)
{
	struct q6dsp_clk *clk = to_q6dsp_clk(hw);
	struct q6dsp_cc *cc = dev_get_drvdata(clk->dev);

	return cc->desc->lpass_vote_clk(clk->dev, clk->q6dsp_clk_id,
				  clk_hw_get_name(&clk->hw), &clk->handle);
}

static void clk_unvote_q6dsp_block(struct clk_hw *hw)
{
	struct q6dsp_clk *clk = to_q6dsp_clk(hw);
	struct q6dsp_cc *cc = dev_get_drvdata(clk->dev);

	cc->desc->lpass_unvote_clk(clk->dev, clk->q6dsp_clk_id, clk->handle);
}

static const struct clk_ops clk_vote_q6dsp_ops = {
	.prepare	= clk_vote_q6dsp_block,
	.unprepare	= clk_unvote_q6dsp_block,
};


static struct clk_hw *q6dsp_of_clk_hw_get(struct of_phandle_args *clkspec,
					  void *data)
{
	struct q6dsp_cc *cc = data;
	unsigned int idx = clkspec->args[0];
	unsigned int attr = clkspec->args[1];

	if (idx >= Q6DSP_MAX_CLK_ID || attr > LPASS_CLK_ATTRIBUTE_COUPLE_DIVISOR) {
		dev_err(cc->dev, "Invalid clk specifier (%d, %d)\n", idx, attr);
		return ERR_PTR(-EINVAL);
	}

	if (cc->clks[idx]) {
		cc->clks[idx]->attributes = attr;
		return &cc->clks[idx]->hw;
	}

	return ERR_PTR(-ENOENT);
}

int q6dsp_clock_dev_probe(struct platform_device *pdev)
{
	struct q6dsp_cc *cc;
	struct device *dev = &pdev->dev;
	const struct q6dsp_clk_init *q6dsp_clks;
	const struct q6dsp_clk_desc *desc;
	u32 attributes = LPASS_CLK_ATTRIBUTE_COUPLE_NO;
	int i, ret;

	cc = devm_kzalloc(dev, sizeof(*cc), GFP_KERNEL);
	if (!cc)
		return -ENOMEM;

	desc = device_get_match_data(dev);
	if (!desc && platform_get_device_id(pdev))
		desc = (const void *)platform_get_device_id(pdev)->driver_data;
	if (!desc)
		return -EINVAL;

	if (!dev->of_node && device_property_present(dev, "qcom,clock-attribute")) {
		ret = device_property_read_u32(dev, "qcom,clock-attribute", &attributes);
		if (ret)
			return ret;
		if (attributes < LPASS_CLK_ATTRIBUTE_COUPLE_NO ||
		    attributes > LPASS_CLK_ATTRIBUTE_COUPLE_DIVISOR)
			return -EINVAL;
	}

	cc->desc = desc;
	cc->dev = dev;
	q6dsp_clks = desc->clks;

	for (i = 0; i < desc->num_clks; i++) {
		unsigned int id = q6dsp_clks[i].clk_id;
		struct clk_init_data init = {
			.name =  q6dsp_clks[i].name,
		};
		struct q6dsp_clk *clk;

		if (id >= Q6DSP_MAX_CLK_ID || cc->clks[id])
			return -EINVAL;

		clk = devm_kzalloc(dev, sizeof(*clk), GFP_KERNEL);
		if (!clk)
			return -ENOMEM;

		clk->dev = dev;
		if (!dev->of_node)
			clk->attributes = attributes;
		clk->q6dsp_clk_id = q6dsp_clks[i].q6dsp_clk_id;
		clk->rate = q6dsp_clks[i].rate;
		clk->hw.init = &init;

		if (clk->rate)
			init.ops = &clk_q6dsp_ops;
		else
			init.ops = &clk_vote_q6dsp_ops;

		cc->clks[id] = clk;

		ret = devm_clk_hw_register(dev, &clk->hw);
		if (ret)
			return ret;
		/* clkdev stores connection IDs in a 16-byte field.  Long Q6AFE
		 * clock names cannot be looked up by this mechanism; software-node
		 * consumers use the explicit short aliases below instead.
		 */
		if (!dev->of_node && strlen(q6dsp_clks[i].name) < 16) {
			ret = devm_clk_hw_register_clkdev(dev, &clk->hw,
						 q6dsp_clks[i].name, dev_name(dev));
			if (ret)
				return ret;
		}
	}

	/* Software-node users cannot express a DT clock phandle.  Allow the
	 * provider node to declare explicit clkdev aliases as triples
	 * (provider-clock-name, consumer-device-name, consumer-clock-name).
	 * This keeps aliases attached to the APR/Q6AFE provider and avoids
	 * guessing a platform-device number from outside the APR bus. */
	if (!dev->of_node) {
		int n = device_property_read_string_array(dev, "qcom,clock-aliases",
								NULL, 0);
		const char *aliases[48];

		if (n < 0 && n != -EINVAL)
			return n;
		if (n % 3 || n > ARRAY_SIZE(aliases))
			return -EINVAL;
		if (n && device_property_read_string_array(dev, "qcom,clock-aliases",
								aliases, n) != n)
			return -EINVAL;
		for (i = 0; i < n; i += 3) {
			int j;
			struct q6dsp_clk *match = NULL;
			for (j = 0; j < desc->num_clks; j++)
				if (!strcmp(desc->clks[j].name, aliases[i])) {
					match = cc->clks[desc->clks[j].clk_id];
					break;
				}
			if (!match)
				return -ENOENT;
			ret = devm_clk_hw_register_clkdev(dev, &match->hw,
							  aliases[i + 2], aliases[i + 1]);
			if (ret)
				return ret;
		}
	}

	if (dev->of_node) {
		ret = devm_of_clk_add_hw_provider(dev, q6dsp_of_clk_hw_get, cc);
		if (ret)
			return ret;
	}

	dev_set_drvdata(dev, cc);

	return 0;
}
EXPORT_SYMBOL_GPL(q6dsp_clock_dev_probe);
