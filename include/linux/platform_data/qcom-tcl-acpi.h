/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _QCOM_TCL_ACPI_PLATFORM_DATA_H
#define _QCOM_TCL_ACPI_PLATFORM_DATA_H

#include <linux/types.h>

struct device;
struct fwnode_handle;
struct regulator_init_data;

/* Board-owned resources must outlive the PAS consumer. */
struct qcom_tcl_pas_data {
	struct device *proxy_pds[2];
	struct fwnode_handle *glink_node;
	int glink_irq;
};

struct qcom_tcl_regulator {
	const char *name;
	struct regulator_init_data *init_data;
	bool firmware_supply;
};

struct qcom_tcl_regulator_data {
	const char *pmic_id;
	const struct qcom_tcl_regulator *regulators;
	unsigned int count;
};

#endif
