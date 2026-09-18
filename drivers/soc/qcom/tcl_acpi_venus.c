// SPDX-License-Identifier: GPL-2.0-only
/* TCL B220G ACPI wiring for the SC7180 VIDEOCC and Venus codec. */
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/clk.h>
#include <linux/interconnect.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/platform_data/qcom-tcl-acpi.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>

#include <dt-bindings/clock/qcom,videocc-sc7180.h>
#include <dt-bindings/power/qcom,rpmhpd.h>

#include "tcl_acpi_display_stage.h"

static struct tcl_display_stage videocc, venus_pd, vcodec0_pd, cx_pd, venus;
static struct device *rpmhcc, *rpmhpd;
static int venus_irq;
static int result;
static bool ready;
module_param(result, int, 0444);
module_param(ready, bool, 0444);

static const struct property_entry video_props[] = {
	PROPERTY_ENTRY_BOOL("qcom,tcl-acpi-video"), { }
};

static const struct resource videocc_res[] = {
	DEFINE_RES_MEM(0x0ab00000, 0x10000),
};

static struct resource venus_res[] = {
	DEFINE_RES_MEM_NAMED(0x0aa00000, 0xff000, "venus"),
	DEFINE_RES_IRQ_NAMED(0, "venus"),
	DEFINE_RES_MEM_NAMED(0x85b00000, 0x500000, "firmware"),
};

static struct qcom_tcl_venus_data venus_data;

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

static int link_supplier(struct tcl_display_stage *consumer, struct device *provider)
{
	return device_link_add(&consumer->pdev->dev, provider, DL_FLAG_STATELESS) ? 0 : -ENOMEM;
}

static int firmware_check(void)
{
	acpi_handle handle;

	if (acpi_disabled || !dmi_match(DMI_SYS_VENDOR, "TCL Communication Ltd.") ||
	    !dmi_match(DMI_PRODUCT_NAME, "B220G") ||
	    ACPI_FAILURE(acpi_get_handle(NULL, "\\_SB.GPU0", &handle)))
		return -ENODEV;
	if (region_intersects(0x0aa00000, 0xff000, IORESOURCE_SYSTEM_RAM,
			      IORES_DESC_NONE) != REGION_DISJOINT ||
	    region_intersects(0x0ab00000, 0x10000, IORESOURCE_SYSTEM_RAM,
			      IORES_DESC_NONE) != REGION_DISJOINT ||
	    region_intersects(0x85b00000, 0x500000, IORESOURCE_SYSTEM_RAM,
			      IORES_DESC_NONE) != REGION_DISJOINT)
		return -EINVAL;
	venus_irq = acpi_register_gsi(NULL, 174, ACPI_LEVEL_SENSITIVE,
				      ACPI_ACTIVE_HIGH);
	if (venus_irq < 0)
		return venus_irq;
	venus_res[1].start = venus_irq;
	venus_res[1].end = venus_irq;
	return 0;
}

static int prepare_videocc(void)
{
	int ret;

	ret = tcl_display_stage_create(&videocc, "tcl-sc7180-videocc",
				       rpmhcc->parent, videocc_res,
				       ARRAY_SIZE(videocc_res), video_props);
	if (ret)
		return ret;
	ret = link_supplier(&videocc, rpmhcc);
	if (ret)
		return ret;
	ret = tcl_display_stage_clock(&videocc, dev_name(rpmhcc),
				      "bi_tcxo", "bi_tcxo");
	if (ret)
		return ret;
	return tcl_display_stage_activate(&videocc);
}

static int prepare_domain(struct tcl_display_stage *stage, const char *name,
			  struct generic_pm_domain *domain)
{
	int ret;

	ret = tcl_display_stage_create(stage, name, &videocc.pdev->dev,
				       NULL, 0, video_props);
	if (ret)
		return ret;
	ret = tcl_display_stage_domain(stage, domain);
	if (ret)
		return ret;
	pm_runtime_enable(&stage->pdev->dev);
	return 0;
}

static int prepare_domains(void)
{
	struct genpd_onecell_data *video = platform_get_drvdata(videocc.pdev);
	struct genpd_onecell_data *rpmh = dev_get_drvdata(rpmhpd);
	int ret;

	if (!video || !video->domains || video->num_domains != 2 ||
	    !video->domains[VENUS_GDSC] || !video->domains[VCODEC0_GDSC] ||
	    !rpmh || !rpmh->domains || rpmh->num_domains <= SC7180_CX ||
	    !rpmh->domains[SC7180_CX])
		return -EINVAL;
	ret = prepare_domain(&venus_pd, "tcl-venus-domain", video->domains[VENUS_GDSC]);
	if (ret)
		return ret;
	ret = prepare_domain(&vcodec0_pd, "tcl-vcodec0-domain",
			     video->domains[VCODEC0_GDSC]);
	if (ret)
		return ret;
	return prepare_domain(&cx_pd, "tcl-venus-cx-domain", rpmh->domains[SC7180_CX]);
}

static int prepare_venus(void)
{
	static const char * const consumers[] = {
		"core", "iface", "bus", "vcodec0_core", "vcodec0_bus",
	};
	static const char * const sources[] = {
		"video_cc_venus_ctl_core_clk", "video_cc_venus_ahb_clk",
		"video_cc_venus_ctl_axi_clk", "video_cc_vcodec0_core_clk",
		"video_cc_vcodec0_axi_clk",
	};
	unsigned int i;
	int ret;

	venus_data.venus_pd = &venus_pd.pdev->dev;
	venus_data.vcodec0_pd = &vcodec0_pd.pdev->dev;
	venus_data.cx_pd = &cx_pd.pdev->dev;
	ret = tcl_display_stage_create_full(&venus, "tcl-sc7180-venus",
					    PLATFORM_DEVID_NONE, rpmhcc->parent,
					    venus_res, ARRAY_SIZE(venus_res),
					    video_props, &venus_data,
					    sizeof(venus_data));
	if (ret)
		return ret;
	ret = link_supplier(&venus, &videocc.pdev->dev);
	if (ret)
		return ret;
	for (i = 0; i < ARRAY_SIZE(sources); i++) {
		ret = tcl_display_stage_clock(&venus, dev_name(&videocc.pdev->dev),
					      sources[i], consumers[i]);
		if (ret)
			return ret;
	}
	ret = acpi_dma_configure_id(&venus.pdev->dev, DEV_DMA_NON_COHERENT, NULL);
	if (ret)
		return ret;
	return tcl_display_stage_activate(&venus);
}

static int __init tcl_acpi_venus_init(void)
{
	int ret;

	ret = firmware_check();
	if (ret)
		return ret;
	rpmhcc = supplier("qcom-sc7180-rpmhcc");
	if (IS_ERR(rpmhcc))
		return PTR_ERR(rpmhcc);
	rpmhpd = supplier("qcom-sc7180-rpmhpd");
	if (IS_ERR(rpmhpd)) {
		ret = PTR_ERR(rpmhpd);
		goto put_cc;
	}
	result = prepare_videocc();
	if (!result)
		result = prepare_domains();
	if (!result)
		result = prepare_venus();
	ready = !result;
	if (result)
		pr_err("TCL ACPI Venus stopped: %d; staged resources retained\n", result);
	else
		pr_info("TCL ACPI Venus ready\n");
	return 0;
put_cc:
	put_device(rpmhcc);
	return ret;
}
module_init(tcl_acpi_venus_init);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TCL B220G ACPI VIDEOCC and Venus coordinator, no unload");
MODULE_SOFTDEP("pre: tcl_acpi_display venus_dec venus_enc");
