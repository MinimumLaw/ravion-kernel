// SPDX-License-Identifier: GPL-2.0-only
/* Diagnostic board glue: requires platform-resource SMEM/TCSR patches. */
#include <linux/acpi.h>
#include <linux/efi.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>

static struct platform_device *mutex_dev, *smem_dev;
static struct acpi_device *ipc;

static const struct resource mutex_res[] = {
	DEFINE_RES_MEM(0x01f40000, 0x20000),
};
static const struct resource smem_res[] = {
	DEFINE_RES_MEM(0x80900000, 0x200000),
};
static const struct property_entry smem_props[] = {
	PROPERTY_ENTRY_U32("qcom,hwlock-id", 3),
	{ }
};

static int __init tcl_acpi_smem_init(void)
{
	struct acpi_table_header *table;
	struct platform_device_info info = { };
	efi_memory_desc_t md;
	acpi_handle handle;
	int ret;
	bool match;

	if (acpi_disabled || !efi_enabled(EFI_BOOT))
		return -ENODEV;
	if (ACPI_FAILURE(acpi_get_table(ACPI_SIG_IORT, 0, &table)))
		return -ENODEV;
	match = !memcmp(table->oem_id, "QCOM  ", ACPI_OEM_ID_SIZE) &&
		!memcmp(table->oem_table_id, "QCOMEDK2", ACPI_OEM_TABLE_ID_SIZE) &&
		table->oem_revision == 0x7180;
	acpi_put_table(table);
	if (!match)
		return -ENODEV;

	/* Require the entire SMEM window inside one UEFI reserved descriptor. */
	ret = efi_mem_desc_lookup(0x80900000, &md);
	if (ret || md.type != EFI_RESERVED_TYPE ||
	    md.phys_addr > 0x80900000 || md.num_pages > (U64_MAX >> EFI_PAGE_SHIFT) ||
	    (md.num_pages << EFI_PAGE_SHIFT) < 0x80b00000ULL - md.phys_addr)
		return -EINVAL;
	if (ACPI_FAILURE(acpi_get_handle(NULL, "\\_SB.IPC0", &handle)))
		return -ENODEV;
	ipc = acpi_get_acpi_dev(handle);
	if (!ipc)
		return -ENODEV;
	if (strcmp(acpi_device_hid(ipc), "QCOM080E")) {
		ret = -ENODEV;
		goto put_ipc;
	}

	info.name = "qcom-tcsr-mutex";
	info.id = PLATFORM_DEVID_NONE;
	info.parent = &ipc->dev;
	info.res = mutex_res;
	info.num_res = ARRAY_SIZE(mutex_res);
	mutex_dev = platform_device_register_full(&info);
	if (IS_ERR(mutex_dev)) {
		ret = PTR_ERR(mutex_dev);
		goto put_ipc;
	}
	info.name = "qcom-smem";
	info.res = smem_res;
	info.num_res = ARRAY_SIZE(smem_res);
	info.properties = smem_props;
	smem_dev = platform_device_register_full(&info);
	if (IS_ERR(smem_dev)) {
		ret = PTR_ERR(smem_dev);
		platform_device_unregister(mutex_dev);
		goto put_ipc;
	}
	pr_info("TCL ACPI: registered SMEM/TCSR devices; inspect driver binding separately\n");
	return 0;
put_ipc:
	acpi_dev_put(ipc);
	return ret;
}
module_init(tcl_acpi_smem_init);

/* No unload: SMEM may have active remote-processor consumers. */
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TCL diagnostic ACPI SMEM/TCSR board glue");
