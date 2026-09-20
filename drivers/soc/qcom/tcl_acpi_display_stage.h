/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef TCL_ACPI_DISPLAY_STAGE_H
#define TCL_ACPI_DISPLAY_STAGE_H

#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/platform_data/qcom-tcl-clkdev.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/pm_domain.h>

/* The board owner must keep suppliers alive until all staged children are
 * destroyed. It establishes supplier links before activate. Domains attached
 * with stage_domain are detached by destroy after quiescing the driver.
 * Domains attached externally remain the board owner's responsibility.
 * No hardware operation is done by create.
 */
#define TCL_DISPLAY_STAGE_BLOCK "tcl-display-waiting-for-suppliers"
#define TCL_DISPLAY_STAGE_CLOCKS 8
struct tcl_display_stage {
	struct platform_device *pdev;
	struct clk *clocks[TCL_DISPLAY_STAGE_CLOCKS];
	struct clk_lookup *lookups[TCL_DISPLAY_STAGE_CLOCKS];
	unsigned int num_clocks;
	bool domain_attached;
};

static inline int tcl_display_stage_create_full(struct tcl_display_stage *stage,
		const char *name, int id, struct device *parent,
		const struct resource *resources, unsigned int count,
		const struct property_entry *properties, const void *data, size_t size)
{
	struct platform_device *pdev;
	int ret;

	if (stage->pdev)
		return -EBUSY;
	pdev = platform_device_alloc(name, id);
	if (!pdev)
		return -ENOMEM;
	pdev->dev.parent = parent;
	ret = device_set_driver_override(&pdev->dev, TCL_DISPLAY_STAGE_BLOCK);
	if (ret)
		goto put;
	ret = platform_device_add_resources(pdev, resources, count);
	if (ret)
		goto put;
	ret = device_create_managed_software_node(&pdev->dev, properties, NULL);
	if (ret)
		goto put;
	ret = platform_device_add_data(pdev, data, size);
	if (ret)
		goto put;
	ret = platform_device_add(pdev);
	if (ret)
		goto put;
	stage->pdev = pdev;
	return 0;
put:
	platform_device_put(pdev);
	return ret;
}

static inline int tcl_display_stage_create(struct tcl_display_stage *stage,
		const char *name, struct device *parent,
		const struct resource *resources, unsigned int count,
		const struct property_entry *properties)
{
	return tcl_display_stage_create_full(stage, name, PLATFORM_DEVID_NONE,
			parent, resources, count, properties, NULL, 0);
}

static inline int tcl_display_stage_clock(struct tcl_display_stage *stage,
		const char *provider, const char *source, const char *consumer)
{
	struct clk_lookup *lookup;
	struct clk *clk;
	unsigned int i = stage->num_clocks;

	if (!stage->pdev || i == TCL_DISPLAY_STAGE_CLOCKS)
		return -EINVAL;
	if (!device_has_driver_override(&stage->pdev->dev))
		return -EBUSY;
	clk = clk_get_sys(provider, source);
	if (IS_ERR(clk))
		return PTR_ERR(clk);
	lookup = qcom_tcl_clkdev_lookup(__clk_get_hw(clk), consumer,
				       dev_name(&stage->pdev->dev));
	if (IS_ERR(lookup)) {
		clk_put(clk);
		return PTR_ERR(lookup);
	}
	stage->clocks[i] = clk;
	stage->lookups[i] = lookup;
	stage->num_clocks++;
	return 0;
}

static inline int tcl_display_stage_domain(struct tcl_display_stage *stage,
		struct generic_pm_domain *domain)
{
	int ret;

	if (!stage->pdev || !domain)
		return -EINVAL;
	if (stage->pdev->dev.pm_domain ||
	    !device_has_driver_override(&stage->pdev->dev))
		return -EBUSY;
	ret = pm_genpd_add_device(domain, &stage->pdev->dev);
	if (!ret)
		stage->domain_attached = true;
	return ret;
}

static inline int tcl_display_stage_activate(struct tcl_display_stage *stage)
{
	int ret;

	if (!stage->pdev)
		return -EINVAL;
	/* Normal matching must set pdev->id_entry; forcing a driver name skips it. */
	ret = device_set_driver_override(&stage->pdev->dev, "");
	if (ret)
		return ret;
	ret = device_attach(&stage->pdev->dev);
	return ret > 0 ? 0 : ret ?: -EPROBE_DEFER;
}

static inline int tcl_display_stage_quiesce(struct tcl_display_stage *stage)
{
	int ret;

	if (!stage->pdev)
		return 0;
	ret = device_set_driver_override(&stage->pdev->dev, TCL_DISPLAY_STAGE_BLOCK);
	if (ret)
		return ret;
	device_release_driver(&stage->pdev->dev);
	return 0;
}

static inline int tcl_display_stage_destroy(struct tcl_display_stage *stage)
{
	int ret;

	if (!stage->pdev)
		return 0;
	ret = tcl_display_stage_quiesce(stage);
	if (ret)
		return ret;
	if (stage->domain_attached) {
		ret = pm_genpd_remove_device(&stage->pdev->dev);
		if (ret)
			return ret;
		stage->domain_attached = false;
	}
	/* Do not free a device still attached by another owner. */
	if (stage->pdev->dev.pm_domain)
		return -EBUSY;
	while (stage->num_clocks) {
		unsigned int i = --stage->num_clocks;

		clkdev_drop(stage->lookups[i]);
		clk_put(stage->clocks[i]);
	}
	platform_device_unregister(stage->pdev);
	stage->pdev = NULL;
	return 0;
}
#endif
