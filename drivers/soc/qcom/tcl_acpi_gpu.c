// SPDX-License-Identifier: GPL-2.0-only
/* TCL B220G ACPI Adreno bring-up. Resources are retained for the boot. */
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/interconnect.h>
#include <linux/ioport.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/pm_opp.h>
#include <linux/pm_runtime.h>
#include <linux/irq.h>
#include <dt-bindings/clock/qcom,gpucc-sc7180.h>
#include <dt-bindings/power/qcom-rpmpd.h>
#include <dt-bindings/interconnect/qcom,icc.h>
#include "tcl_acpi_display_stage.h"

static struct tcl_display_stage cc, cx, gx, gmu, adreno;
static struct device *firmware, *gcc, *xo;
static struct icc_path *mem_path;
static struct clk *smmu_bus;
static int result;
static bool ready;
module_param(result, int, 0444);
module_param(ready, bool, 0444);
static const struct property_entry props[] = {
	PROPERTY_ENTRY_BOOL("qcom,tcl-acpi-gpu"), { }
};
static const struct resource cc_res[] = { DEFINE_RES_MEM(0x5090000, 0x9000) };
static struct resource gmu_res[] = {
	DEFINE_RES_MEM_NAMED(0x506a000, 0x31000, "gmu"),
	DEFINE_RES_MEM_NAMED(0xb290000, 0x10000, "gmu_pdc"),
	DEFINE_RES_MEM_NAMED(0xb490000, 0x10000, "gmu_pdc_seq"),
	DEFINE_RES_IRQ_NAMED(0, "hfi"), DEFINE_RES_IRQ_NAMED(0, "gmu"),
};
static struct resource gpu_res[] = {
	DEFINE_RES_MEM_NAMED(0x5000000, 0x40000, "kgsl_3d0_reg_memory"),
	DEFINE_RES_MEM_NAMED(0x509e000, 0x1000, "cx_mem"),
	DEFINE_RES_MEM_NAMED(0x5061000, 0x800, "cx_dbgc"),
	DEFINE_RES_MEM_NAMED(0x80840000, 0x2000, "zap-shader"),
	DEFINE_RES_IRQ_NAMED(0, "kgsl_3d0_irq"),
};

static struct device *supplier(const char *name)
{
	struct device *dev = bus_find_device_by_name(&platform_bus_type, NULL, name);

	if (!dev)
		return ERR_PTR(-EPROBE_DEFER);
	if (!dev->driver) {
		put_device(dev);
		return ERR_PTR(-EPROBE_DEFER);
	}
	return dev;
}

static int firmware_check(void)
{
	struct acpi_device *adev;
	struct resource_entry *entry;
	acpi_handle handle;
	LIST_HEAD(resources);
	bool gpu_window = false, cc_window = false, gmu_window = false;
	int ret, irq = 0;

	if (acpi_disabled || !dmi_match(DMI_SYS_VENDOR, "TCL Communication Ltd.") ||
	    !dmi_match(DMI_PRODUCT_NAME, "B220G") ||
	    ACPI_FAILURE(acpi_get_handle(NULL, "\\_SB.GPU0", &handle)))
		return -ENODEV;
	adev = acpi_get_acpi_dev(handle);
	if (!adev)
		return -ENODEV;
	firmware = &adev->dev;
	if (!acpi_match_acpi_device((const struct acpi_device_id[]) {
		{ "QCOM083A" }, { } }, adev))
		return -ENODEV;
	ret = acpi_dev_get_resources(adev, &resources, NULL, NULL);
	if (ret < 0)
		return ret;
	list_for_each_entry(entry, &resources, node) {
		struct resource *res = entry->res;

		if (res->flags & IORESOURCE_DISABLED)
			continue;
		if (resource_type(res) == IORESOURCE_MEM) {
			gpu_window |= res->start == 0x5000000 && resource_size(res) == 0x3f010;
			cc_window |= resource_contains(res, &cc_res[0]);
			gmu_window |= resource_contains(res, &gmu_res[0]);
		} else if (resource_type(res) == IORESOURCE_IRQ && res->start == res->end) {
			struct irq_data *data = irq_get_irq_data(res->start);

			if (data && data->hwirq == 332 &&
			    irqd_get_trigger_type(data) == IRQ_TYPE_LEVEL_HIGH)
				irq = res->start;
		}
	}
	acpi_dev_free_resource_list(&resources);
	if (!gpu_window || !cc_window || !gmu_window || irq <= 0)
		return -EINVAL;
	/* Full GPU/PDC windows and GMU interrupts supplement truncated OEM CRS
	 * using the SC7180 binding and the working board DT. Never map RAM.
	 */
	if (region_intersects(0x80840000, 0x2000, IORESOURCE_SYSTEM_RAM,
			      IORES_DESC_NONE) != REGION_DISJOINT)
		return -EINVAL;
	gpu_res[4].start = gpu_res[4].end = irq;
	ret = acpi_register_gsi(NULL, 336, ACPI_LEVEL_SENSITIVE, ACPI_ACTIVE_HIGH);
	if (ret < 0)
		return ret;
	gmu_res[3].start = gmu_res[3].end = ret;
	ret = acpi_register_gsi(NULL, 337, ACPI_LEVEL_SENSITIVE, ACPI_ACTIVE_HIGH);
	if (ret < 0)
		return ret;
	gmu_res[4].start = gmu_res[4].end = ret;
	return 0;
}

static int keep_supplier(struct device *dev, struct device *provider)
{
	return device_link_add(dev, provider, DL_FLAG_STATELESS) ? 0 : -ENOMEM;
}

static int prepare_clock_controller(void)
{
	static const char * const names[] = {
		"gcc_gpu_gpll0_clk_src", "gcc_gpu_gpll0_div_clk_src", "gcc_gpu_cfg_ahb_clk",
	};
	static const char * const connections[] = {
		"gcc_gpu_gpll0_clk_src", "gcc_gpu_gpll0_div_clk_src", "iface",
	};
	int i, ret;

	gcc = supplier("tcl-display-gcc");
	if (IS_ERR(gcc))
		return PTR_ERR(gcc);
	xo = supplier("qcom-sc7180-rpmhcc");
	if (IS_ERR(xo))
		return PTR_ERR(xo);
	ret = tcl_display_stage_create(&cc, "tcl-sc7180-gpucc", firmware,
			cc_res, ARRAY_SIZE(cc_res), props);
	if (ret)
		return ret;
	ret = keep_supplier(&cc.pdev->dev, gcc);
	if (ret)
		return ret;
	ret = keep_supplier(&cc.pdev->dev, xo);
	if (ret)
		return ret;
	ret = tcl_display_stage_clock(&cc, dev_name(xo), "bi_tcxo", "bi_tcxo");
	if (ret)
		return ret;
	for (i = 0; i < ARRAY_SIZE(names); i++) {
		ret = tcl_display_stage_clock(&cc, dev_name(gcc), names[i], connections[i]);
		if (ret)
			return ret;
	}
	return tcl_display_stage_activate(&cc);
}

static int match_adreno_smmu(struct device *dev, const void *unused)
{
	struct resource *res;

	if (!dev->driver || strcmp(dev->driver->name, "arm-smmu"))
		return 0;
	res = platform_get_resource(to_platform_device(dev), IORESOURCE_MEM, 0);
	return res && res->start == 0x5040000 && resource_size(res) == 0x10000;
}

static int attach_smmu_domain(struct generic_pm_domain *domain)
{
	struct device *smmu = bus_find_device(&platform_bus_type, NULL, NULL,
					    match_adreno_smmu);
	int ret;

	if (!smmu)
		return -EPROBE_DEFER;
	if (smmu->pm_domain || pm_runtime_enabled(smmu)) {
		put_device(smmu);
		return -EBUSY;
	}
	ret = pm_genpd_add_device(domain, smmu);
	if (ret) {
		put_device(smmu);
		return ret;
	}
	/* CX and the bus clocks are already held on by the coordinator. */
	ret = pm_runtime_set_active(smmu);
	if (ret)
		return ret; /* Retain the attached device, as with other staged failures. */
	pm_runtime_enable(smmu);
	/* Keep the IORT supplier active throughout this initial bring-up. */
	pm_runtime_get_noresume(smmu);
	return 0;
}

static int prepare_domains(void)
{
	struct genpd_onecell_data *pd = platform_get_drvdata(cc.pdev);
	int ret;

	if (!pd || pd->num_domains != 2 || !pd->domains ||
	    !pd->domains[CX_GDSC] || !pd->domains[GX_GDSC])
		return -EINVAL;
	ret = tcl_display_stage_create(&cx, "tcl-gpu-cx-domain", firmware, NULL, 0, props);
	if (ret)
		return ret;
	ret = tcl_display_stage_domain(&cx, pd->domains[CX_GDSC]);
	if (ret)
		return ret;
	ret = tcl_display_stage_create(&gx, "tcl-gpu-gx-domain", firmware, NULL, 0, props);
	if (ret)
		return ret;
	ret = tcl_display_stage_domain(&gx, pd->domains[GX_GDSC]);
	if (ret)
		return ret;
	pm_runtime_enable(&cx.pdev->dev);
	pm_runtime_enable(&gx.pdev->dev);
	/* First bring-up holds CX and SMMU bus. Dynamic idle follows validation. */
	ret = pm_runtime_resume_and_get(&cx.pdev->dev);
	if (ret < 0)
		return ret;
	smmu_bus = clk_get_sys(dev_name(gcc), "gcc_gpu_memnoc_gfx_clk");
	if (IS_ERR(smmu_bus))
		return PTR_ERR(smmu_bus);
	ret = clk_prepare_enable(smmu_bus);
	if (ret)
		return ret;
	return attach_smmu_domain(pd->domains[CX_GDSC]);
}

static int gmu_dma_probe(struct platform_device *pdev)
{
	struct iommu_fwspec *spec = dev_iommu_fwspec_get(&pdev->dev);

	if (!spec || spec->num_ids != 1 || spec->ids[0] != 5 ||
	    !device_iommu_mapped(&pdev->dev))
		return -EINVAL;
	return 0;
}
static const struct platform_device_id gmu_ids[] = { { "tcl-sc7180-gmu" }, { } };
static struct platform_driver gmu_driver = {
	.probe = gmu_dma_probe,
	.driver_managed_dma = true,
	.id_table = gmu_ids,
	.driver = { .name = "tcl-sc7180-gmu" },
};

static int prepare_gmu(void)
{
	static const char * const clocks[] = { "gmu", "cxo", "axi", "memnoc" };
	static const char * const sources[] = {
		"gpu_cc_cx_gmu_clk", "gpu_cc_cxo_clk",
		"gcc_ddrss_gpu_axi_clk", "gcc_gpu_memnoc_gfx_clk",
	};
	const struct property_entry properties[] = {
		PROPERTY_ENTRY_BOOL("qcom,tcl-acpi-gpu"),
		PROPERTY_ENTRY_STRING_ARRAY("clock-names", clocks),
		PROPERTY_ENTRY_REF("qcom,cx-domain", to_software_node(dev_fwnode(&cx.pdev->dev))),
		PROPERTY_ENTRY_REF("qcom,gx-domain", to_software_node(dev_fwnode(&gx.pdev->dev))),
		{ }
	};
	struct dev_pm_opp_data opp = { .freq = 200000000, .level = RPMH_REGULATOR_LEVEL_MIN_SVS };
	int ret, i;

	ret = tcl_display_stage_create(&gmu, "tcl-sc7180-gmu", firmware,
			gmu_res, ARRAY_SIZE(gmu_res), properties);
	if (ret)
		return ret;
	ret = keep_supplier(&gmu.pdev->dev, &cc.pdev->dev);
	if (ret)
		return ret;
	for (i = 0; i < ARRAY_SIZE(clocks); i++) {
		ret = tcl_display_stage_clock(&gmu, i < 2 ? dev_name(&cc.pdev->dev) : dev_name(gcc),
					      sources[i], clocks[i]);
		if (ret)
			return ret;
	}
	ret = dev_pm_opp_add_dynamic(&gmu.pdev->dev, &opp);
	if (ret)
		return ret;
	return tcl_display_stage_activate(&gmu);
}

static int prepare_adreno(void)
{
	const struct property_entry properties[] = {
		PROPERTY_ENTRY_BOOL("qcom,tcl-acpi-gpu"),
		PROPERTY_ENTRY_U32("qcom,chipid", 0x06010800),
		PROPERTY_ENTRY_REF("qcom,gmu", to_software_node(dev_fwnode(&gmu.pdev->dev))),
		PROPERTY_ENTRY_STRING("qcom,zap-firmware", "qcom/sc7180/tcl/qcdxkmsuc7180.mbn"),
		{ }
	};
	struct dev_pm_opp_data opp = { .freq = 180000000, .level = RPMH_REGULATOR_LEVEL_MIN_SVS };
	int ret;

	ret = tcl_display_stage_create(&adreno, "tcl-sc7180-adreno", firmware,
			gpu_res, ARRAY_SIZE(gpu_res), properties);
	if (ret)
		return ret;
	ret = keep_supplier(&adreno.pdev->dev, &gmu.pdev->dev);
	if (ret)
		return ret;
	/* All fuse bins support 180MHz; bandwidth is the corresponding DT vote. */
	ret = dev_pm_opp_add_dynamic(&adreno.pdev->dev, &opp);
	if (ret)
		return ret;
	mem_path = icc_get(&adreno.pdev->dev, "qxm_gpu@tcl-sc7180-gnoc", "ebi@tcl-sc7180-mc");
	if (IS_ERR_OR_NULL(mem_path))
		return mem_path ? PTR_ERR(mem_path) : -ENODEV;
	icc_set_tag(mem_path, QCOM_ICC_TAG_ALWAYS);
	ret = icc_set_bw(mem_path, 0, 1804000);
	if (ret)
		return ret;
	return tcl_display_stage_activate(&adreno);
}

static int __init tcl_gpu_init(void)
{
	int ret;

	result = firmware_check();
	if (result)
		goto out;
	result = platform_driver_register(&gmu_driver);
	if (result)
		goto out;
	result = prepare_clock_controller();
	if (!result)
		result = prepare_domains();
	if (!result)
		result = prepare_gmu();
	if (!result)
		result = prepare_adreno();
out:
	ready = !result;
	if (result) {
		/* Prevent asynchronous retries after partial initialization. */
		if (gmu.pdev && !gmu.pdev->dev.driver) {
			ret = device_set_driver_override(&gmu.pdev->dev, TCL_DISPLAY_STAGE_BLOCK);
			if (ret)
				pr_err("TCL GMU retry block failed: %d\n", ret);
		}
		if (adreno.pdev && !adreno.pdev->dev.driver) {
			ret = device_set_driver_override(&adreno.pdev->dev, TCL_DISPLAY_STAGE_BLOCK);
			if (ret)
				pr_err("TCL GPU retry block failed: %d\n", ret);
		}
	}
	pr_info("TCL ACPI GPU preparation result=%d ready=%d; resources retained\n", result, ready);
	return 0;
}
module_init(tcl_gpu_init);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TCL B220G ACPI Adreno coordinator, boot lifetime, 180MHz initial OPP");
