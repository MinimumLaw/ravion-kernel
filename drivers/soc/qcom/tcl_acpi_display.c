// SPDX-License-Identifier: GPL-2.0-only
/* Diagnostic, boot-lifetime ACPI display clock wiring for TCL B220G. */
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/pm_runtime.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/interconnect.h>
#include <linux/resource.h>
#include <linux/platform_data/qcom-tcl-acpi.h>
#include <linux/regulator/machine.h>
#include <dt-bindings/power/qcom,rpmhpd.h>
#include <dt-bindings/interconnect/qcom,icc.h>

#include "tcl_acpi_display_stage.h"

/* Like the diagnostic Wi-Fi owner, this coordinator has no unload support.
 * Do not manually unbind its suppliers. DSI/DPU activation requires scanout=1.
 * A coherent kernel containing the clock-core changes is required.
 */
static struct tcl_display_stage gcc, dispcc, phy_reg, phy, mdss, dpu, dsi_reg, refgen, dsi;
static unsigned int dsi_irq;
static struct device *bridge_controller;
static struct i2c_adapter *bridge_adapter;
static struct i2c_client *bridge_client;
static unsigned int dpu_irq;
static int mdss_irq;
static struct tcl_display_stage bcm, noc[5];
static struct icc_path *display_mem, *display_cfg;
static struct device *rpmhcc, *rpmhpd, *gpu;
static struct module *phy_owner;
static int result;
static bool ready;
static bool scanout;
module_param(scanout, bool, 0444);
MODULE_PARM_DESC(scanout, "Activate DSI, validated firmware bridge and DPU after preparation");
module_param(result, int, 0444);
module_param(ready, bool, 0444);

static const struct property_entry props[] = {
	PROPERTY_ENTRY_BOOL("qcom,tcl-acpi-display"), { }
};
static const struct resource gcc_res[] = {
	DEFINE_RES_MEM(0x100000, 0x1f0000),
};
static const struct resource dispcc_res[] = {
	DEFINE_RES_MEM(0xaf00000, 0x20000),
};

static const struct resource mdss_mem =
	DEFINE_RES_MEM_NAMED(0xae00000, 0x1000, "mdss");

static const struct resource dsi_mem =
	DEFINE_RES_MEM_NAMED(0xae94000, 0x400, "dsi_ctrl");

static const struct resource dpu_mem[] = {
	DEFINE_RES_MEM_NAMED(0xae01000, 0x8f000, "mdp"),
	DEFINE_RES_MEM_NAMED(0xaeb0000, 0x3000, "vbif"),
};

static const struct resource phy_res[] = {
	DEFINE_RES_MEM_NAMED(0xae94400, 0x200, "dsi_phy"),
	DEFINE_RES_MEM_NAMED(0xae94600, 0x280, "dsi_phy_lane"),
	DEFINE_RES_MEM_NAMED(0xae94a00, 0x1e0, "dsi_pll"),
};

/* Supply mapping/constraint used by the successful TCL DT display test.
 * The upstream input rail remains firmware-managed, as in that DT.
 */
static struct regulator_consumer_supply phy_supply =
	REGULATOR_SUPPLY("vdds", "tcl-sc7180-dsi-phy");
static struct regulator_init_data phy_reg_init = {
	.constraints = {
		.name = "tcl-display-l4a", .min_uV = 880000, .max_uV = 880000,
		.apply_uV = true, .initial_mode = REGULATOR_MODE_NORMAL,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_MODE |
				  REGULATOR_CHANGE_VOLTAGE,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = 1, .consumer_supplies = &phy_supply,
};
static const struct qcom_tcl_regulator phy_regs[] = {
	{ "ldo4", &phy_reg_init, true },
};
static const struct qcom_tcl_regulator_data phy_reg_data = {
	"a", phy_regs, ARRAY_SIZE(phy_regs),
};

static struct regulator_consumer_supply dsi_supply =
	REGULATOR_SUPPLY("vdda", "tcl-sc7180-dsi");
static struct regulator_init_data dsi_reg_init = {
	.constraints = {
		.name = "tcl-display-l3c", .min_uV = 1200000, .max_uV = 1200000,
		.apply_uV = true, .initial_mode = REGULATOR_MODE_NORMAL,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE |
				  REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = 1, .consumer_supplies = &dsi_supply,
};
static const struct qcom_tcl_regulator dsi_regs[] = {
	{ "ldo3", &dsi_reg_init, true },
};
static const struct qcom_tcl_regulator_data dsi_reg_data = {
	"c", dsi_regs, ARRAY_SIZE(dsi_regs),
};
static struct regulator_consumer_supply refgen_supply =
	REGULATOR_SUPPLY("refgen", "tcl-sc7180-dsi");
static struct regulator_init_data refgen_init = {
	.constraints = {
		.name = "tcl-display-refgen", .valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies = 1, .consumer_supplies = &refgen_supply,
};
static const struct resource refgen_res[] = {
	DEFINE_RES_MEM(0xff1000, 0x60),
};

/* Existing RPMh suppliers must be built in and bound before loading us. */
static struct device *get_supplier(const char *name)
{
	struct device *dev = bus_find_device_by_name(&platform_bus_type, NULL, name);
	bool valid;

	if (!dev)
		return ERR_PTR(-EPROBE_DEFER);
	device_lock(dev);
	valid = device_is_bound(dev) && dev->driver && !dev->driver->owner;
	device_unlock(dev);
	if (!valid) {
		put_device(dev);
		return ERR_PTR(-EPROBE_DEFER);
	}
	return dev;
}

static int check_firmware(void)
{
	static const struct acpi_device_id ids[] = { { "QCOM083A" }, { } };
	struct acpi_device *adev;
	struct resource_entry *entry;
	acpi_handle handle;
	LIST_HEAD(resources);
	bool contains = false, contains_mdss = false, contains_dsi = false;
	unsigned int phy_windows = 0, dpu_windows = 0;
	unsigned int i;
	int ret;

	if (acpi_disabled ||
	    !dmi_match(DMI_SYS_VENDOR, "TCL Communication Ltd.") ||
	    !dmi_match(DMI_PRODUCT_NAME, "B220G") ||
	    ACPI_FAILURE(acpi_get_handle(NULL, "\\_SB.GPU0", &handle)))
		return -ENODEV;
	adev = acpi_get_acpi_dev(handle);
	if (!adev)
		return -ENODEV;
	gpu = &adev->dev;
	if (!acpi_match_acpi_device(ids, adev)) {
		ret = -ENODEV;
		goto put;
	}
	ret = acpi_dev_get_resources(adev, &resources, NULL, NULL);
	if (ret < 0)
		goto put;
	list_for_each_entry(entry, &resources, node) {
		if (resource_type(entry->res) == IORESOURCE_IRQ &&
		    !(entry->res->flags & IORESOURCE_DISABLED) &&
		    entry->res->start == entry->res->end) {
			struct irq_data *data = irq_get_irq_data(entry->res->start);

			/* _CRS is already translated to a Linux IRQ by ACPI. */
			if (data && data->hwirq == 115 &&
			    irqd_get_trigger_type(data) == IRQ_TYPE_LEVEL_HIGH)
				mdss_irq = entry->res->start;
		}
		if (resource_type(entry->res) != IORESOURCE_MEM ||
		    entry->res->flags & IORESOURCE_DISABLED)
			continue;
		for (i = 0; i < ARRAY_SIZE(dpu_mem); i++)
			if (resource_contains(entry->res, &dpu_mem[i]))
				dpu_windows |= BIT(i);
		if (resource_contains(entry->res, &dsi_mem))
			contains_dsi = true;
		if (resource_contains(entry->res, &mdss_mem))
			contains_mdss = true;
		if (resource_contains(entry->res, &dispcc_res[0]))
			contains = true;
		for (i = 0; i < ARRAY_SIZE(phy_res); i++)
			if (resource_contains(entry->res, &phy_res[i]))
				phy_windows |= BIT(i);
	}
	acpi_dev_free_resource_list(&resources);
	if (contains && contains_mdss && contains_dsi && mdss_irq > 0 &&
	    dpu_windows == GENMASK(ARRAY_SIZE(dpu_mem) - 1, 0) &&
	    phy_windows == GENMASK(ARRAY_SIZE(phy_res) - 1, 0))
		return 0;
	ret = -EINVAL;
put:
	put_device(gpu);
	gpu = NULL;
	return ret;
}

static int link_supplier(struct tcl_display_stage *consumer, struct device *supplier)
{
	/* Boot-lifetime references: teardown is explicitly not supported yet. */
	return device_link_add(&consumer->pdev->dev, supplier,
			       DL_FLAG_STATELESS) ? 0 : -ENOMEM;
}

/* Endpoint providers are registered first, then the upstream fabrics.
 * The topology is the existing SC7180 table, including system_noc.
 */
static int prepare_interconnects(void)
{
	static const char * const names[] = {
		"tcl-sc7180-mc", "tcl-sc7180-cnoc", "tcl-sc7180-snoc",
		"tcl-sc7180-gnoc", "tcl-sc7180-mnoc",
	};
	unsigned int i;
	int ret;

	ret = tcl_display_stage_create(&bcm, "tcl-bcm-voter", rpmhcc->parent,
			NULL, 0, props);
	if (ret)
		return ret;
	ret = link_supplier(&bcm, rpmhcc->parent);
	if (ret)
		return ret;
	ret = tcl_display_stage_activate(&bcm);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(names); i++) {
		const struct property_entry noc_props[] = {
			PROPERTY_ENTRY_BOOL("qcom,tcl-acpi-display"),
			PROPERTY_ENTRY_REF("qcom,bcm-voters",
				to_software_node(dev_fwnode(&bcm.pdev->dev))),
			{ }
		};

		ret = tcl_display_stage_create(&noc[i], names[i], rpmhcc->parent,
				NULL, 0, noc_props);
		if (ret)
			return ret;
		ret = link_supplier(&noc[i], &bcm.pdev->dev);
		if (ret)
			return ret;
		ret = tcl_display_stage_activate(&noc[i]);
		if (ret)
			return ret;
	}

	/* Linux7.2 dynamic ICC names are <node>@<provider-device>. */
	display_mem = icc_get(gpu, "qxm_mdp0@tcl-sc7180-mnoc", "ebi@tcl-sc7180-mc");
	if (IS_ERR_OR_NULL(display_mem))
		return display_mem ? PTR_ERR(display_mem) : -ENODEV;
	display_cfg = icc_get(gpu, "acm_apps0@tcl-sc7180-gnoc",
			"qhs_display_cfg@tcl-sc7180-cnoc");
	if (IS_ERR_OR_NULL(display_cfg))
		return display_cfg ? PTR_ERR(display_cfg) : -ENODEV;
	icc_set_tag(display_mem, QCOM_ICC_TAG_ALWAYS);
	icc_set_tag(display_cfg, QCOM_ICC_TAG_ALWAYS);
	/* Keep path references as part of the boot-lifetime coordinator.
	 * No scanout bandwidth is guessed here. Normal provider registration
	 * can send bootstrap BCM votes; this is not a read-only operation.
	 */
	pr_info("TCL display ICC paths resolved; bootstrap bandwidth floor retained\n");
	return 0;
}

static int prepare_gcc(struct generic_pm_domain *cx)
{
	int ret;

	ret = tcl_display_stage_create(&gcc, "tcl-display-gcc", gpu,
			gcc_res, ARRAY_SIZE(gcc_res), props);
	if (ret)
		return ret;
	ret = link_supplier(&gcc, rpmhcc);
	if (ret)
		return ret;
	ret = link_supplier(&gcc, rpmhpd);
	if (ret)
		return ret;
	ret = tcl_display_stage_clock(&gcc, dev_name(rpmhcc), "bi_tcxo", "bi_tcxo");
	if (ret)
		return ret;
	ret = tcl_display_stage_domain(&gcc, cx);
	if (ret)
		return ret;
	return tcl_display_stage_activate(&gcc);
}

static int prepare_dispcc(void)
{
	int ret;

	ret = tcl_display_stage_create(&dispcc, "tcl-sc7180-dispcc", gpu,
			dispcc_res, ARRAY_SIZE(dispcc_res), props);
	if (ret)
		return ret;
	ret = link_supplier(&dispcc, rpmhcc);
	if (ret)
		return ret;
	ret = link_supplier(&dispcc, &gcc.pdev->dev);
	if (ret)
		return ret;
	ret = tcl_display_stage_clock(&dispcc, dev_name(rpmhcc), "bi_tcxo", "bi_tcxo");
	if (ret)
		return ret;
	ret = tcl_display_stage_clock(&dispcc, dev_name(&gcc.pdev->dev),
			"gcc_disp_gpll0_clk_src", "gcc_disp_gpll0_clk_src");
	if (ret)
		return ret;
	ret = tcl_display_stage_clock(&dispcc, dev_name(&gcc.pdev->dev),
			"gcc_disp_ahb_clk", "iface");
	if (ret)
		return ret;
	ret = tcl_display_stage_activate(&dispcc);
	if (ret)
		return ret;
	if (!platform_get_drvdata(dispcc.pdev))
		return -ENODEV;
	return 0;
}

static int prepare_phy_regulator(void)
{
	int ret;

	/* .1 is independent of the existing Wi-Fi PM6150 instance. */
	ret = tcl_display_stage_create_full(&phy_reg, "tcl-pm6150-reg", 1,
			rpmhcc->parent, NULL, 0, props,
			&phy_reg_data, sizeof(phy_reg_data));
	if (ret)
		return ret;
	ret = link_supplier(&phy_reg, rpmhcc->parent);
	if (ret)
		return ret;
	return tcl_display_stage_activate(&phy_reg);
}

static int prepare_phy(void)
{
	struct device *dev;
	int ret;

	ret = tcl_display_stage_create(&phy, "tcl-sc7180-dsi-phy", gpu,
			phy_res, ARRAY_SIZE(phy_res), props);
	if (ret)
		return ret;
	ret = link_supplier(&phy, &phy_reg.pdev->dev);
	if (ret)
		return ret;
	ret = link_supplier(&phy, &dispcc.pdev->dev);
	if (ret)
		return ret;
	ret = link_supplier(&phy, rpmhcc);
	if (ret)
		return ret;
	ret = tcl_display_stage_clock(&phy, dev_name(&dispcc.pdev->dev),
			"disp_cc_mdss_ahb_clk", "iface");
	if (ret)
		return ret;
	ret = tcl_display_stage_clock(&phy, dev_name(rpmhcc), "bi_tcxo", "ref");
	if (ret)
		return ret;
	/* The DT parent MDSS supplies this domain implicitly; ACPI GPU0 does not. */
	ret = tcl_display_stage_domain(&phy, platform_get_drvdata(dispcc.pdev));
	if (ret)
		return ret;
	ret = tcl_display_stage_activate(&phy);
	if (ret)
		return ret;

	/* DISPCC's late PLL parents must outlive all downstream clocks. */
	dev = &phy.pdev->dev;
	device_lock(dev);
	if (!dev->driver || !try_module_get(dev->driver->owner)) {
		ret = -ENODEV;
	} else {
		phy_owner = dev->driver->owner;
		ret = 0;
	}
	device_unlock(dev);
	return ret;
}


static int prepare_mdss(void)
{
	static const char * const clock_names[] = { "iface", "ahb", "core" };
	static const char * const mem_route[] = {
		"qxm_mdp0@tcl-sc7180-mnoc", "ebi@tcl-sc7180-mc",
	};
	static const char * const cfg_route[] = {
		"acm_apps0@tcl-sc7180-gnoc", "qhs_display_cfg@tcl-sc7180-cnoc",
	};
	const struct property_entry mdss_props[] = {
		PROPERTY_ENTRY_BOOL("qcom,tcl-acpi-display"),
		PROPERTY_ENTRY_STRING_ARRAY("clock-names", clock_names),
		PROPERTY_ENTRY_STRING_ARRAY("qcom,icc-mdp0-mem", mem_route),
		PROPERTY_ENTRY_STRING_ARRAY("qcom,icc-cpu-cfg", cfg_route),
		PROPERTY_ENTRY_U32("qcom,icc-tag", QCOM_ICC_TAG_ALWAYS),
		{ }
	};
	struct resource resources[] = {
		mdss_mem, DEFINE_RES_IRQ_NAMED(mdss_irq, "mdss"),
	};
	unsigned int i;
	int ret;

	ret = tcl_display_stage_create(&mdss, "tcl-sc7180-mdss", gpu,
			resources, ARRAY_SIZE(resources), mdss_props);
	if (ret)
		return ret;
	ret = link_supplier(&mdss, &gcc.pdev->dev);
	if (ret)
		return ret;
	ret = link_supplier(&mdss, &dispcc.pdev->dev);
	if (ret)
		return ret;
	for (i = 0; i < ARRAY_SIZE(noc); i++) {
		ret = link_supplier(&mdss, &noc[i].pdev->dev);
		if (ret)
			return ret;
	}
	ret = tcl_display_stage_clock(&mdss, dev_name(&gcc.pdev->dev),
			"gcc_disp_ahb_clk", "iface");
	if (ret)
		return ret;
	ret = tcl_display_stage_clock(&mdss, dev_name(&dispcc.pdev->dev),
			"disp_cc_mdss_ahb_clk", "ahb");
	if (ret)
		return ret;
	ret = tcl_display_stage_clock(&mdss, dev_name(&dispcc.pdev->dev),
			"disp_cc_mdss_mdp_clk", "core");
	if (ret)
		return ret;
	ret = tcl_display_stage_domain(&mdss, platform_get_drvdata(dispcc.pdev));
	if (ret)
		return ret;
	return tcl_display_stage_activate(&mdss);
}


/* Keep DPU probe blocked until the DSI component graph is complete. */
static int prepare_dpu(struct generic_pm_domain *cx)
{
	static const char * const clock_names[] = {
		"bus", "iface", "rot", "lut", "core", "vsync",
	};
	static const char * const sources[] = {
		"gcc_disp_hf_axi_clk", "disp_cc_mdss_ahb_clk",
		"disp_cc_mdss_rot_clk", "disp_cc_mdss_mdp_lut_clk",
		"disp_cc_mdss_mdp_clk", "disp_cc_mdss_vsync_clk",
	};
	const struct property_entry dpu_props[] = {
		PROPERTY_ENTRY_BOOL("qcom,tcl-acpi-display"),
		PROPERTY_ENTRY_STRING_ARRAY("clock-names", clock_names),
		{ }
	};
	struct resource resources[] = {
		dpu_mem[0], dpu_mem[1],
		DEFINE_RES_IRQ_NAMED(0, "dpu"),
	};
	struct irq_domain *domain;
	unsigned int i;
	int ret;

	domain = irq_find_matching_fwnode(dev_fwnode(&mdss.pdev->dev), DOMAIN_BUS_ANY);
	if (!domain)
		return -EPROBE_DEFER;
	dpu_irq = irq_create_mapping(domain, 0);
	if (!dpu_irq)
		return -ENOMEM;
	resources[2].start = resources[2].end = dpu_irq;
	ret = tcl_display_stage_create(&dpu, "tcl-sc7180-dpu", &mdss.pdev->dev,
			resources, ARRAY_SIZE(resources), dpu_props);
	if (ret) {
		irq_dispose_mapping(dpu_irq);
		dpu_irq = 0;
		return ret;
	}
	/* From here on retain IRQ and device with the other diagnostic stages. */
	ret = link_supplier(&dpu, rpmhpd);
	if (ret)
		return ret;
	ret = link_supplier(&dpu, &gcc.pdev->dev);
	if (ret)
		return ret;
	ret = link_supplier(&dpu, &dispcc.pdev->dev);
	if (ret)
		return ret;
	for (i = 0; i < ARRAY_SIZE(noc); i++) {
		ret = link_supplier(&dpu, &noc[i].pdev->dev);
		if (ret)
			return ret;
	}
	for (i = 0; i < ARRAY_SIZE(clock_names); i++) {
		ret = tcl_display_stage_clock(&dpu,
			dev_name(i ? &dispcc.pdev->dev : &gcc.pdev->dev),
			sources[i], clock_names[i]);
		if (ret)
			return ret;
	}
	ret = tcl_display_stage_domain(&dpu, cx);
	if (ret)
		return ret;
	pr_info("TCL DPU resources ready; probe held for DSI component graph\n");
	return 0;
}

static int prepare_dsi_supplies(void)
{
	int ret;

	ret = tcl_display_stage_create_full(&dsi_reg, "tcl-pm6150l-reg", 2,
			rpmhcc->parent, NULL, 0, props, &dsi_reg_data, sizeof(dsi_reg_data));
	if (ret)
		return ret;
	ret = link_supplier(&dsi_reg, rpmhcc->parent);
	if (ret)
		return ret;
	ret = tcl_display_stage_activate(&dsi_reg);
	if (ret)
		return ret;
	/* Supplemental SoC resource from upstream SC7180, not GPU0._CRS. */
	ret = tcl_display_stage_create_full(&refgen, "tcl-sc7180-refgen",
			PLATFORM_DEVID_NONE, gpu, refgen_res, ARRAY_SIZE(refgen_res), props,
			&refgen_init, sizeof(refgen_init));
	if (ret)
		return ret;
	return tcl_display_stage_activate(&refgen);
}


/* Static storage is intentional: software node references outlive init. */
static struct software_node graph_nodes[9];
static const struct property_entry port0_props[] = { PROPERTY_ENTRY_U32("reg", 0), { } };
static const struct property_entry port1_props[] = { PROPERTY_ENTRY_U32("reg", 1), { } };
static const u32 dsi_lanes[] = { 0, 1, 2, 3 };
static const struct property_entry dpu_ep_props[] = {
	PROPERTY_ENTRY_U32("reg", 0), PROPERTY_ENTRY_REF("remote-endpoint", &graph_nodes[3]), { }
};
static const struct property_entry dsi_in_props[] = {
	PROPERTY_ENTRY_U32("reg", 0), PROPERTY_ENTRY_REF("remote-endpoint", &graph_nodes[1]), { }
};
static const struct property_entry dsi_out_props[] = {
	PROPERTY_ENTRY_U32("reg", 0), PROPERTY_ENTRY_U32_ARRAY("data-lanes", dsi_lanes),
	PROPERTY_ENTRY_REF("remote-endpoint", &graph_nodes[8]), { }
};
static const struct property_entry bridge_ep_props[] = {
	PROPERTY_ENTRY_U32("reg", 0), PROPERTY_ENTRY_U32_ARRAY("data-lanes", dsi_lanes),
	PROPERTY_ENTRY_REF("remote-endpoint", &graph_nodes[5]), { }
};
static const struct software_node * const graph_group[] = {
	&graph_nodes[0], &graph_nodes[1], &graph_nodes[2], &graph_nodes[3],
	&graph_nodes[4], &graph_nodes[5], &graph_nodes[6], &graph_nodes[7],
	&graph_nodes[8], NULL,
};

static int prepare_display_graph(void)
{
	const struct software_node *dpu_node = to_software_node(dev_fwnode(&dpu.pdev->dev));
	const struct software_node *dsi_node = to_software_node(dev_fwnode(&dsi.pdev->dev));

	if (!dpu_node || !dsi_node)
		return -EINVAL;
	graph_nodes[0] = (struct software_node){ "port@1", dpu_node, port1_props };
	graph_nodes[1] = (struct software_node){ "endpoint@0", &graph_nodes[0], dpu_ep_props };
	graph_nodes[2] = (struct software_node){ "port@0", dsi_node, port0_props };
	graph_nodes[3] = (struct software_node){ "endpoint@0", &graph_nodes[2], dsi_in_props };
	graph_nodes[4] = (struct software_node){ "port@1", dsi_node, port1_props };
	graph_nodes[5] = (struct software_node){ "endpoint@0", &graph_nodes[4], dsi_out_props };
	graph_nodes[6] = (struct software_node){ "tcl-lt8911-handoff", NULL, props };
	graph_nodes[7] = (struct software_node){ "port@0", &graph_nodes[6], port0_props };
	graph_nodes[8] = (struct software_node){ "endpoint@0", &graph_nodes[7], bridge_ep_props };
	return software_node_register_node_group(graph_group);
}

static int prepare_dsi(struct generic_pm_domain *cx)
{
	static const char * const names[] = { "byte", "byte_intf", "pixel", "core", "iface", "bus" };
	static const char * const sources[] = {
		"disp_cc_mdss_byte0_clk", "disp_cc_mdss_byte0_intf_clk", "disp_cc_mdss_pclk0_clk",
		"disp_cc_mdss_esc0_clk", "disp_cc_mdss_ahb_clk", "gcc_disp_hf_axi_clk",
	};
	const struct property_entry dsi_props[] = {
		PROPERTY_ENTRY_BOOL("qcom,tcl-acpi-display"),
		PROPERTY_ENTRY_REF("phys", to_software_node(dev_fwnode(&phy.pdev->dev))),
		PROPERTY_ENTRY_STRING_ARRAY("clock-names", names), { }
	};
	struct resource resources[] = { dsi_mem, DEFINE_RES_IRQ_NAMED(0, "dsi") };
	struct irq_domain *domain;
	struct device *suppliers[] = { rpmhpd, &dispcc.pdev->dev, &gcc.pdev->dev,
		&phy.pdev->dev, &dsi_reg.pdev->dev, &refgen.pdev->dev };
	unsigned int i;
	int ret;

	domain = irq_find_matching_fwnode(dev_fwnode(&mdss.pdev->dev), DOMAIN_BUS_ANY);
	if (!domain)
		return -EPROBE_DEFER;
	dsi_irq = irq_create_mapping(domain, 4);
	if (!dsi_irq)
		return -ENOMEM;
	resources[1].start = resources[1].end = dsi_irq;
	ret = tcl_display_stage_create(&dsi, "tcl-sc7180-dsi", &mdss.pdev->dev,
			resources, ARRAY_SIZE(resources), dsi_props);
	if (ret) {
		irq_dispose_mapping(dsi_irq);
		dsi_irq = 0;
		return ret;
	}
	for (i = 0; i < ARRAY_SIZE(suppliers); i++) {
		ret = link_supplier(&dsi, suppliers[i]);
		if (ret)
			return ret;
	}
	for (i = 0; i < ARRAY_SIZE(names); i++) {
		ret = tcl_display_stage_clock(&dsi,
			dev_name(i == 5 ? &gcc.pdev->dev : &dispcc.pdev->dev), sources[i], names[i]);
		if (ret)
			return ret;
	}
	ret = tcl_display_stage_domain(&dsi, cx);
	if (ret)
		return ret;
	return prepare_display_graph();
}


static int prepare_bridge_client(void)
{
	static const struct property_entry bus_props[] = {
		PROPERTY_ENTRY_BOOL("qcom,tcl-acpi-display"),
		PROPERTY_ENTRY_U32("clock-frequency", 100000), { }
	};
	struct acpi_device *adev;
	acpi_handle handle;
	int ret;

	if (ACPI_FAILURE(acpi_get_handle(NULL, "\\_SB.IC11", &handle)))
		return -ENODEV;
	adev = acpi_get_acpi_dev(handle);
	if (!adev)
		return -ENODEV;
	bridge_controller = bus_find_device_by_fwnode(&platform_bus_type,
			acpi_fwnode_handle(adev));
	acpi_dev_put(adev);
	if (!bridge_controller)
		return -EPROBE_DEFER;

	device_lock(bridge_controller);
	if (device_is_bound(bridge_controller)) {
		ret = device_property_read_bool(bridge_controller,
			"qcom,tcl-acpi-display") ? 0 : -EBUSY;
	} else if (!device_property_read_bool(bridge_controller, "qcom,tcl-acpi-display")) {
		ret = device_create_managed_software_node(bridge_controller, bus_props, NULL);
	} else {
		ret = 0;
	}
	device_unlock(bridge_controller);
	if (ret)
		return ret;
	ret = device_attach(bridge_controller);
	if (ret <= 0)
		return ret < 0 ? ret : -EPROBE_DEFER;
	bridge_adapter = i2c_get_adapter_by_fwnode(acpi_fwnode_handle(ACPI_COMPANION(bridge_controller)));
	if (!bridge_adapter)
		return -EPROBE_DEFER;
	if (bridge_adapter->dev.parent != bridge_controller)
		return -EINVAL;
	/* IC11 registration enumerates EDP1 from its ACPI I2cSerialBus resource.
	 * Reuse that client; a second client at 0x29 would return -EBUSY.
	 */
	if (ACPI_FAILURE(acpi_get_handle(NULL, "\\_SB.EDP1", &handle)))
		return -ENODEV;
	adev = acpi_get_acpi_dev(handle);
	if (!adev)
		return -ENODEV;
	bridge_client = i2c_find_device_by_fwnode(acpi_fwnode_handle(adev));
	acpi_dev_put(adev);
	if (!bridge_client)
		return -EPROBE_DEFER;
	if (bridge_client->adapter != bridge_adapter ||
	    bridge_client->addr != 0x29 || (bridge_client->flags & I2C_CLIENT_TEN))
		return -EINVAL;
	device_lock(&bridge_client->dev);
	if (device_is_bound(&bridge_client->dev))
		ret = -EBUSY;
	else
		ret = device_add_software_node(&bridge_client->dev, &graph_nodes[6]);
	device_unlock(&bridge_client->dev);
	if (ret)
		return ret;
	/* The host remains blocked; the bridge will defer until it registers. */
	if (!device_link_add(&bridge_client->dev, &dsi.pdev->dev, DL_FLAG_STATELESS))
		return -ENOMEM;
	return 0;
}


/* SC7180 DPU 6.2 offsets, matching catalog/dpu_6_2_sc7180.h and
 * dpu_hw_intf.c. Stop firmware DMA while its original SMMU and DSI clock
 * configuration are still intact. DPU's default DMA domain is attached
 * before its probe, so doing this in dpu_kms_hw_init would be too late.
 */
static int quiesce_firmware_scanout(void)
{
	const unsigned int intf_base = 0x6a800;
	const unsigned int timing_enable = 0x000, intf_status = 0x26c;
	void __iomem *base;
	u32 revision, enable, status;
	int ret;

	/* Run before the DPU platform device exists.  Platform creation can
	 * attach the firmware-owned scanout stream to Linux's new IOMMU domain,
	 * so checking the DPU device here would make the promised ordering
	 * impossible.
	 */
	if (!mdss.pdev)
		return -ENODEV;
	ret = pm_runtime_resume_and_get(&mdss.pdev->dev);
	if (ret < 0)
		return ret;
	base = ioremap(dpu_mem[0].start, resource_size(&dpu_mem[0]));
	if (!base) {
		ret = -ENOMEM;
		goto put;
	}
	revision = readl(base);
	if (revision != 0x60020000) {
		dev_err(gpu, "unexpected DPU revision %#x before handoff\n", revision);
		ret = -ENODEV;
		goto unmap;
	}
	enable = readl(base + intf_base + timing_enable);
	status = readl(base + intf_base + intf_status);
	dev_info(gpu, "firmware DSI timing enable=%#x status=%#x\n", enable, status);
	/* This register contains only the timing-enable bit. Reject unknown HW. */
	if (enable & ~BIT(0)) {
		ret = -EINVAL;
		goto unmap;
	}
	writel(0, base + intf_base + timing_enable);
	/* Read STATUS, not the shadow ENABLE register: disable latches at VSYNC.
	 * Allow six frames at the validated 60 Hz panel mode. Never attach a new
	 * IOMMU domain after a failed stop, nor conceal the failure with a reset.
	 */
	ret = readl_poll_timeout(base + intf_base + intf_status, status,
				!(status & BIT(0)), 1000, 100000);
	if (ret)
		dev_err(gpu, "firmware DSI stop timed out: status=%#x; aborting handoff\n",
			status);
	else
		dev_info(gpu, "firmware DSI scanout stopped before clock/IOMMU handoff\n");
unmap:
	iounmap(base);
put:
	pm_runtime_put_sync(&mdss.pdev->dev);
	return ret;
}

static int prepare_clock_defaults(void)
{
	static const char * const names[] = {
		"disp_cc_mdss_ahb_clk", "disp_cc_mdss_vsync_clk", "disp_cc_mdss_rot_clk",
		"disp_cc_mdss_mdp_clk", "disp_cc_mdss_byte0_clk_src", "disp_cc_mdss_pclk0_clk_src",
		"dsi0_phy_pll_out_byteclk", "dsi0_phy_pll_out_dsiclk",
		"disp_cc_mdss_byte0_clk", "disp_cc_mdss_byte0_intf_clk",
		"disp_cc_mdss_pclk0_clk",
	};
	struct clk *clks[ARRAY_SIZE(names)];
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(names); i++) {
		clks[i] = clk_get_sys(dev_name(i == 6 || i == 7 ?
				&phy.pdev->dev : &dispcc.pdev->dev), names[i]);
		if (IS_ERR(clks[i])) {
			ret = PTR_ERR(clks[i]);
			dev_err(gpu, "failed to obtain display clock %s: %d\n",
				names[i], ret);
			goto put;
		}
	}
	/* Explicit bootstrap CX vote, retained for this diagnostic boot. */
	ret = dev_pm_domain_set_performance_state(&gcc.pdev->dev, 256);
	if (ret)
		goto put;
	/* PHY has the MDSS domain attached and a safe iface-only resume. */
	ret = pm_runtime_resume_and_get(&phy.pdev->dev);
	if (ret < 0)
		goto put;
	/* Firmware leaves the DSI branches physically enabled, but CCF has no
	 * matching enable counts.  Adopt each branch with a balanced enable and
	 * disable so the first shared-VCO rate change does not attempt to update
	 * a live pixel RCG behind CCF's back.
	 */
	for (unsigned int j = 8; j < ARRAY_SIZE(names); j++) {
		ret = clk_prepare_enable(clks[j]);
		if (ret) {
			dev_err(gpu, "failed to adopt firmware clock %s: %d\n",
				names[j], ret);
			goto suspend;
		}
		clk_disable_unprepare(clks[j]);
	}
	for (unsigned int j = 0; j < 4; j++) {
		unsigned long rate = j == 3 ? 200000000 : 19200000;

		ret = clk_set_rate(clks[j], rate);
		if (ret)
			goto suspend;
		if (clk_get_rate(clks[j]) != rate) {
			ret = -EINVAL;
			goto suspend;
		}
	}
	for (unsigned int j = 0; j < 2; j++) {
		ret = clk_set_parent(clks[4 + j], clks[6 + j]);
		if (ret)
			goto suspend;
		if (!clk_is_match(clk_get_parent(clks[4 + j]), clks[6 + j])) {
			ret = -EINVAL;
			goto suspend;
		}
	}
suspend:
	pm_runtime_put_sync(&phy.pdev->dev);
put:
	while (i)
		clk_put(clks[--i]);
	return ret;
}

static int activate_display(void)
{
	int ret;

	ret = prepare_clock_defaults();
	if (ret)
		return dev_err_probe(gpu, ret, "TCL initial display clocks failed\n");
	ret = tcl_display_stage_activate(&dsi);
	if (ret)
		return dev_err_probe(gpu, ret, "TCL DSI host activation failed\n");
	ret = device_attach(&bridge_client->dev);
	if (ret <= 0)
		return dev_err_probe(gpu, ret < 0 ? ret : -EPROBE_DEFER,
				     "TCL firmware bridge not ready\n");
	ret = tcl_display_stage_activate(&dpu);
	if (ret)
		return dev_err_probe(gpu, ret, "TCL DPU activation failed\n");
	pr_info("TCL DSI, firmware bridge and DPU bound; verify connector and scanout\n");
	return 0;
}

static int block_pending_probes(void)
{
	struct tcl_display_stage *stages[] = {
		&dpu, &dsi, &refgen, &dsi_reg, &mdss, &phy, &phy_reg, &dispcc, &gcc,
		&noc[4], &noc[3], &noc[2], &noc[1], &noc[0], &bcm,
	};
	int first = 0, ret;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(stages); i++) {
		if (!stages[i]->pdev)
			continue;
		ret = device_set_driver_override(&stages[i]->pdev->dev,
				TCL_DISPLAY_STAGE_BLOCK);
		if (ret && !first)
			first = ret;
	}
	return first;
}

static int __init tcl_display_init(void)
{
	struct genpd_onecell_data *domains;
	int ret;

	ret = check_firmware();
	if (ret) {
		pr_err("TCL display firmware validation failed: %d\n", ret);
		return ret;
	}
	rpmhcc = get_supplier("qcom-sc7180-rpmhcc");
	if (IS_ERR(rpmhcc)) {
		ret = PTR_ERR(rpmhcc);
		goto put_gpu;
	}
	rpmhpd = get_supplier("qcom-sc7180-rpmhpd");
	if (IS_ERR(rpmhpd)) {
		ret = PTR_ERR(rpmhpd);
		goto put_cc;
	}
	if (!rpmhcc->parent || rpmhcc->parent != rpmhpd->parent) {
		ret = -ENODEV;
		goto put_pd;
	}
	domains = dev_get_drvdata(rpmhpd);
	if (!domains || !domains->domains || domains->num_domains <= SC7180_CX ||
	    !domains->domains[SC7180_CX]) {
		ret = -ENODEV;
		goto put_pd;
	}

	result = prepare_interconnects();
	if (!result)
		result = prepare_gcc(domains->domains[SC7180_CX]);
	if (!result)
		result = prepare_dispcc();
	if (!result)
		result = prepare_phy_regulator();
	if (!result)
		result = prepare_phy();
	if (!result)
		result = prepare_mdss();
	/* Stop firmware DMA before prepare_dpu() creates the device and the
	 * platform/IORT path attaches SID 0x800 to Linux's SMMU domain.
	 */
	if (!result && scanout)
		result = quiesce_firmware_scanout();
	if (!result)
		result = prepare_dpu(domains->domains[SC7180_CX]);
	if (!result)
		result = prepare_dsi_supplies();
	if (!result)
		result = prepare_dsi(domains->domains[SC7180_CX]);
	if (!result)
		result = prepare_bridge_client();
	if (!result && scanout)
		result = activate_display();
	ready = !result;
	if (result) {
		/* Keep ownership after any partial setup, including deferred probe.
		 * Block further probe attempts, retaining bound providers and
		 * their clock objects. Never free an attached domain or
		 * clkdev alias while a partially registered device can use it.
		 */
		ret = block_pending_probes();
		pr_err("TCL display clocks stopped: result=%d block=%d; resources retained\n",
		       result, ret);
	} else {
		pr_info("TCL display preparation ready; scanout requested=%d\n", scanout);
	}
	return 0;
put_pd:
	put_device(rpmhpd);
put_cc:
	put_device(rpmhcc);
put_gpu:
	put_device(gpu);
	return ret;
}
module_init(tcl_display_init);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TCL B220G diagnostic ACPI display clock coordinator, no unload");
MODULE_SOFTDEP("pre: msm tcl_acpi_wifi tcl-lt8911-handoff");
