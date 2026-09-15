/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef ADRENO_TCL_ACPI_H
#define ADRENO_TCL_ACPI_H
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/platform_device.h>
#include <linux/property.h>

static inline bool tcl_acpi_gpu_device(struct device *dev)
{
#ifdef CONFIG_ACPI
	acpi_handle handle;

	return !acpi_disabled && !dev->of_node && dev->parent &&
		dmi_match(DMI_SYS_VENDOR, "TCL Communication Ltd.") &&
		dmi_match(DMI_PRODUCT_NAME, "B220G") &&
		device_property_read_bool(dev, "qcom,tcl-acpi-gpu") &&
		ACPI_SUCCESS(acpi_get_handle(NULL, "\\_SB.GPU0", &handle)) &&
		acpi_device_handle(acpi_companion_or_self(dev->parent)) == handle;
#else
	return false;
#endif
}

/* bus_find_device takes the reference consumed by the caller. */
static inline struct device *tcl_gpu_reference(struct device *dev, const char *name)
{
	struct fwnode_handle *fwnode;
	struct device *supplier;

	if (!tcl_acpi_gpu_device(dev))
		return ERR_PTR(-ENODEV);
	fwnode = fwnode_find_reference(dev_fwnode(dev), name, 0);
	if (IS_ERR(fwnode))
		return ERR_CAST(fwnode);
	supplier = bus_find_device_by_fwnode(&platform_bus_type, fwnode);
	fwnode_handle_put(fwnode);
	return supplier ?: ERR_PTR(-EPROBE_DEFER);
}
#endif
