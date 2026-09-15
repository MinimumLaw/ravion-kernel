// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 */

#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/dmi.h>
#include <linux/property.h>
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,gpucc-sc7180.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "common.h"
#include "gdsc.h"

#define CX_GMU_CBCR_SLEEP_MASK		0xF
#define CX_GMU_CBCR_SLEEP_SHIFT		4
#define CX_GMU_CBCR_WAKE_MASK		0xF
#define CX_GMU_CBCR_WAKE_SHIFT		8

enum {
	P_BI_TCXO,
	P_GPLL0_OUT_MAIN,
	P_GPLL0_OUT_MAIN_DIV,
	P_GPU_CC_PLL1_OUT_MAIN,
};

static const struct pll_vco fabia_vco[] = {
	{ 249600000, 2000000000, 0 },
};

static struct clk_alpha_pll gpu_cc_pll1 = {
	.offset = 0x100,
	.vco_table = fabia_vco,
	.num_vco = ARRAY_SIZE(fabia_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_FABIA],
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "gpu_cc_pll1",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fabia_ops,
		},
	},
};

static const struct parent_map gpu_cc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_GPU_CC_PLL1_OUT_MAIN, 3 },
	{ P_GPLL0_OUT_MAIN, 5 },
	{ P_GPLL0_OUT_MAIN_DIV, 6 },
};

static const struct clk_parent_data gpu_cc_parent_data_0[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &gpu_cc_pll1.clkr.hw },
	{ .fw_name = "gcc_gpu_gpll0_clk_src" },
	{ .fw_name = "gcc_gpu_gpll0_div_clk_src" },
};

static const struct freq_tbl ftbl_gpu_cc_gmu_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(200000000, P_GPLL0_OUT_MAIN_DIV, 1.5, 0, 0),
	{ }
};

static struct clk_rcg2 gpu_cc_gmu_clk_src = {
	.cmd_rcgr = 0x1120,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gpu_cc_parent_map_0,
	.freq_tbl = ftbl_gpu_cc_gmu_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gpu_cc_gmu_clk_src",
		.parent_data = gpu_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(gpu_cc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_branch gpu_cc_crc_ahb_clk = {
	.halt_reg = 0x107c,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x107c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpu_cc_crc_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_cx_gmu_clk = {
	.halt_reg = 0x1098,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1098,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpu_cc_cx_gmu_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&gpu_cc_gmu_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_cx_snoc_dvm_clk = {
	.halt_reg = 0x108c,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x108c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpu_cc_cx_snoc_dvm_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_cxo_aon_clk = {
	.halt_reg = 0x1004,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x1004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpu_cc_cxo_aon_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpu_cc_cxo_clk = {
	.halt_reg = 0x109c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x109c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpu_cc_cxo_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct gdsc cx_gdsc = {
	.gdscr = 0x106c,
	.gds_hw_ctrl = 0x1540,
	.clk_dis_wait_val = 8,
	.pd = {
		.name = "cx_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = VOTABLE,
};

static struct gdsc gx_gdsc = {
	.gdscr = 0x100c,
	.clamp_io_ctrl = 0x1508,
	.pd = {
		.name = "gx_gdsc",
		.power_on = gdsc_gx_do_nothing_enable,
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = CLAMP_IO,
};

static struct gdsc *gpu_cc_sc7180_gdscs[] = {
	[CX_GDSC] = &cx_gdsc,
	[GX_GDSC] = &gx_gdsc,
};

static struct clk_regmap *gpu_cc_sc7180_clocks[] = {
	[GPU_CC_CXO_CLK] = &gpu_cc_cxo_clk.clkr,
	[GPU_CC_CRC_AHB_CLK] = &gpu_cc_crc_ahb_clk.clkr,
	[GPU_CC_CX_GMU_CLK] = &gpu_cc_cx_gmu_clk.clkr,
	[GPU_CC_CX_SNOC_DVM_CLK] = &gpu_cc_cx_snoc_dvm_clk.clkr,
	[GPU_CC_CXO_AON_CLK] = &gpu_cc_cxo_aon_clk.clkr,
	[GPU_CC_GMU_CLK_SRC] = &gpu_cc_gmu_clk_src.clkr,
	[GPU_CC_PLL1] = &gpu_cc_pll1.clkr,
};

static const struct regmap_config gpu_cc_sc7180_regmap_config = {
	.reg_bits =	32,
	.reg_stride =	4,
	.val_bits =	32,
	.max_register =	0x8008,
	.fast_io =	true,
};

static const struct qcom_cc_desc gpu_cc_sc7180_desc = {
	.config = &gpu_cc_sc7180_regmap_config,
	.clks = gpu_cc_sc7180_clocks,
	.num_clks = ARRAY_SIZE(gpu_cc_sc7180_clocks),
	.gdscs = gpu_cc_sc7180_gdscs,
	.num_gdscs = ARRAY_SIZE(gpu_cc_sc7180_gdscs),
};

static const struct of_device_id gpu_cc_sc7180_match_table[] = {
	{ .compatible = "qcom,sc7180-gpucc" },
	{ }
};
MODULE_DEVICE_TABLE(of, gpu_cc_sc7180_match_table);

static struct generic_pm_domain *tcl_gpu_domains[] = { &cx_gdsc.pd, &gx_gdsc.pd };
static struct genpd_onecell_data tcl_gpu_pd_data = {
	.domains = tcl_gpu_domains, .num_domains = ARRAY_SIZE(tcl_gpu_domains),
};

static int tcl_gpucc_prepare(struct platform_device *pdev)
{
#ifdef CONFIG_ACPI
	static const char * const names[] = {
		"bi_tcxo", "gcc_gpu_gpll0_clk_src", "gcc_gpu_gpll0_div_clk_src",
	};
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct clk *clk;
	acpi_handle handle;
	int i;

	if (acpi_disabled || dev->of_node || !dev->parent ||
	    !dmi_match(DMI_SYS_VENDOR, "TCL Communication Ltd.") ||
	    !dmi_match(DMI_PRODUCT_NAME, "B220G") ||
	    !device_property_read_bool(dev, "qcom,tcl-acpi-gpu") ||
	    ACPI_FAILURE(acpi_get_handle(NULL, "\\_SB.GPU0", &handle)) ||
	    acpi_device_handle(acpi_companion_or_self(dev->parent)) != handle ||
	    !acpi_match_acpi_device((const struct acpi_device_id[]) {
		{ "QCOM083A" }, { } }, acpi_companion_or_self(dev->parent)))
		return -ENODEV;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res || res->start != 0x5090000 || resource_size(res) != 0x9000)
		return -EINVAL;
	for (i = 0; i < ARRAY_SIZE(names); i++) {
		clk = devm_clk_get(dev, names[i]);
		if (IS_ERR(clk))
			return dev_err_probe(dev, PTR_ERR(clk), "missing %s\n", names[i]);
		if (clk_get_rate(clk) != (i == 0 ? 19200000 : i == 1 ? 600000000 : 300000000))
			return -EINVAL;
	}
	clk = devm_clk_get_enabled(dev, "iface");
	return PTR_ERR_OR_ZERO(clk);
#else
	return -ENODEV;
#endif
}

static int gpu_cc_sc7180_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	struct alpha_pll_config gpu_cc_pll_config = {};
	unsigned int value, mask;
	struct qcom_cc_desc platform_desc = gpu_cc_sc7180_desc;
	int ret;

	if (!pdev->dev.of_node) {
		ret = tcl_gpucc_prepare(pdev);
		if (ret)
			return ret;
		platform_desc.platform_clocks = true;
	}

	regmap = qcom_cc_map(pdev, &gpu_cc_sc7180_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	/* 360MHz Configuration */
	gpu_cc_pll_config.l = 0x12;
	gpu_cc_pll_config.alpha = 0xc000;
	gpu_cc_pll_config.config_ctl_val = 0x20485699;
	gpu_cc_pll_config.config_ctl_hi_val = 0x00002067;
	gpu_cc_pll_config.user_ctl_val = 0x00000001;
	gpu_cc_pll_config.user_ctl_hi_val = 0x00004805;
	gpu_cc_pll_config.test_ctl_hi_val = 0x40000000;

	clk_fabia_pll_configure(&gpu_cc_pll1, regmap, &gpu_cc_pll_config);

	/* Recommended WAKEUP/SLEEP settings for the gpu_cc_cx_gmu_clk */
	mask = CX_GMU_CBCR_WAKE_MASK << CX_GMU_CBCR_WAKE_SHIFT;
	mask |= CX_GMU_CBCR_SLEEP_MASK << CX_GMU_CBCR_SLEEP_SHIFT;
	value = 0xF << CX_GMU_CBCR_WAKE_SHIFT | 0xF << CX_GMU_CBCR_SLEEP_SHIFT;
	ret = regmap_update_bits(regmap, 0x1098, mask, value);
	if (ret)
		return ret;

	ret = qcom_cc_really_probe(&pdev->dev, &platform_desc, regmap);
	if (!ret && !pdev->dev.of_node)
		platform_set_drvdata(pdev, &tcl_gpu_pd_data);
	return ret;
}

static const struct platform_device_id tcl_gpucc_ids[] = {
	{ "tcl-sc7180-gpucc" }, { }
};
MODULE_DEVICE_TABLE(platform, tcl_gpucc_ids);

static struct platform_driver gpu_cc_sc7180_driver = {
	.id_table = tcl_gpucc_ids,
	.probe = gpu_cc_sc7180_probe,
	.driver = {
		.name = "sc7180-gpucc",
		.of_match_table = gpu_cc_sc7180_match_table,
	},
};

module_platform_driver(gpu_cc_sc7180_driver);

MODULE_DESCRIPTION("QTI GPU_CC SC7180 Driver");
MODULE_LICENSE("GPL v2");
