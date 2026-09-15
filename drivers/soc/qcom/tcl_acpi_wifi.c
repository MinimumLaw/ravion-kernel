// SPDX-License-Identifier: GPL-2.0-only
/* TCL SC7180 diagnostic ACPI wiring; no device-tree enumeration. */
#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/efi.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/platform_data/qcom-tcl-acpi.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/regulator/machine.h>
#include <linux/workqueue.h>
#include <dt-bindings/power/qcom-rpmpd.h>

static struct acpi_device *pep, *glnk, *amss, *qwln;
static struct platform_device *apcs, *cmd, *rmtfs, *rsc, *smp2p, *qmp;
static struct platform_device *cc, *pd, *rega, *regc, *mpss, *wlan;
static struct qcom_tcl_pas_data pas_data;
static unsigned int stage, attempts;
static bool ready;
module_param(ready, bool, 0444);
module_param(stage, uint, 0444);

static const struct software_node apcs_node;
static const struct property_entry apcs_props[] = {
	PROPERTY_ENTRY_U32("#mbox-cells", 1), { }
};
static const struct software_node apcs_node = {
	.name = "tcl-apss", .properties = apcs_props,
};
static const u32 smem_ids[] = { 435, 428 };
static const struct property_entry smp2p_props[] = {
	PROPERTY_ENTRY_U32_ARRAY("qcom,smem", smem_ids),
	PROPERTY_ENTRY_U32("qcom,local-pid", 0),
	PROPERTY_ENTRY_U32("qcom,remote-pid", 1),
	PROPERTY_ENTRY_REF("mboxes", &apcs_node, 14), { }
};
static const struct software_node smp2p_node = {
	.name = "tcl-modem-smp2p", .properties = smp2p_props,
};
static const struct property_entry in_props[] = {
	PROPERTY_ENTRY_STRING("qcom,entry-name", "slave-kernel"),
	PROPERTY_ENTRY_BOOL("interrupt-controller"),
	PROPERTY_ENTRY_U32("#interrupt-cells", 2), { }
};
static const struct software_node in_node = {
	.name = "slave-kernel", .parent = &smp2p_node, .properties = in_props,
};
static const struct property_entry out_props[] = {
	PROPERTY_ENTRY_STRING("qcom,entry-name", "master-kernel"),
	PROPERTY_ENTRY_U32("#qcom,smem-state-cells", 1), { }
};
static const struct software_node out_node = {
	.name = "master-kernel", .parent = &smp2p_node, .properties = out_props,
};
static const struct property_entry qmp_props[] = {
	PROPERTY_ENTRY_REF("mboxes", &apcs_node, 0), { }
};
static const struct software_node qmp_node = {
	.name = "tcl-aoss", .properties = qmp_props,
};
static const struct property_entry glink_props[] = {
	PROPERTY_ENTRY_STRING("label", "modem"),
	PROPERTY_ENTRY_U32("qcom,remote-pid", 1),
	PROPERTY_ENTRY_REF("mboxes", &apcs_node, 12), { }
};
static const struct software_node glink_node = {
	.name = "glink-edge", .properties = glink_props,
};
static const struct property_entry pas_props[] = {
	PROPERTY_ENTRY_STRING("firmware-name", "qcom/sc7180/tcl/qcmpss7180_nm.mbn"),
	PROPERTY_ENTRY_REF("qcom,qmp", &qmp_node),
	PROPERTY_ENTRY_REF("qcom,smem-states", &out_node, 0),
	PROPERTY_ENTRY_STRING("qcom,smem-state-names", "stop"),
	PROPERTY_ENTRY_BOOL("qcom,tcl-no-interconnect"), { }
};
static const struct software_node pas_node = {
	.name = "tcl-mpss", .properties = pas_props,
};
static const struct software_node * const nodes[] = {
	&apcs_node, &smp2p_node, &in_node, &out_node, &qmp_node,
	&glink_node, &pas_node, NULL,
};
static const u32 tcs_config[] = { 2, 2, 0, 3, 1, 3, 3, 1 };
static const struct property_entry rsc_props[] = {
	PROPERTY_ENTRY_U32("qcom,drv-id", 2),
	PROPERTY_ENTRY_U32("qcom,tcs-offset", 0xd00),
	PROPERTY_ENTRY_U32_ARRAY("qcom,tcs-config", tcs_config), { }
};
static const struct property_entry rmtfs_props[] = {
	PROPERTY_ENTRY_U32("qcom,client-id", 1), { }
};

/* Exact constraints from the currently working TCL DT. L11c keeps its voltage. */
#define TCL_REG(_id, _supply, _uv) \
static struct regulator_consumer_supply _id##_consumer = \
	REGULATOR_SUPPLY(_supply, "tcl-wcn3990"); \
static struct regulator_init_data _id##_init = { \
	.constraints = { .name = #_id, .min_uV = _uv, .max_uV = _uv, \
		.apply_uV = !!(_uv), .initial_mode = REGULATOR_MODE_NORMAL, \
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_MODE | \
				  ((_uv) ? REGULATOR_CHANGE_VOLTAGE : 0), \
		.valid_modes_mask = REGULATOR_MODE_NORMAL }, \
	.num_consumer_supplies = 1, .consumer_supplies = &_id##_consumer, \
}
TCL_REG(l9a, "vdd-0.8-cx-mx", 664000);
TCL_REG(l1c, "vdd-1.8-xo", 1800000);
TCL_REG(l2c, "vdd-1.3-rfa", 1304000);
TCL_REG(l10c, "vdd-3.3-ch0", 3304000);
TCL_REG(l11c, "vdd-3.3-ch1", 0);
static const struct qcom_tcl_regulator a_regs[] = { { "ldo9", &l9a_init, true } };
static const struct qcom_tcl_regulator c_regs[] = {
	{ "ldo1", &l1c_init, true }, { "ldo2", &l2c_init, true },
	{ "ldo10", &l10c_init, true }, { "ldo11", &l11c_init, true },
};
static const struct qcom_tcl_regulator_data a_data = { "a", a_regs, ARRAY_SIZE(a_regs) };
static const struct qcom_tcl_regulator_data c_data = { "c", c_regs, ARRAY_SIZE(c_regs) };

static int reserved_window(u64 base, u64 size)
{
	efi_memory_desc_t md;

	if (efi_mem_desc_lookup(base, &md) || md.type != EFI_RESERVED_TYPE ||
	    md.phys_addr > base || md.num_pages > (U64_MAX >> EFI_PAGE_SHIFT) ||
	    (md.num_pages << EFI_PAGE_SHIFT) < base + size - md.phys_addr)
		return -EINVAL;
	return 0;
}

static struct acpi_device *find_acpi(const char *path, const char *hid)
{
	struct acpi_device *adev;
	acpi_handle handle;

	if (ACPI_FAILURE(acpi_get_handle(NULL, (acpi_string)path, &handle)))
		return ERR_PTR(-ENODEV);
	adev = acpi_get_acpi_dev(handle);
	if (!adev)
		return ERR_PTR(-ENODEV);
	if (hid && strcmp(acpi_device_hid(adev), hid)) {
		acpi_dev_put(adev);
		return ERR_PTR(-ENODEV);
	}
	return adev;
}

static int firmware_irq(struct acpi_device *adev, unsigned int index,
			unsigned int gsi, const char *name, struct resource *res)
{
	struct irq_data *data;
	int ret;

	/* acpi_irq_get fills only start, end and flags. */
	memset(res, 0, sizeof(*res));
	ret = acpi_irq_get(adev->handle, index, res);
	if (ret)
		return ret;
	data = irq_get_irq_data(res->start);
	if (!data || irqd_to_hwirq(data) != gsi)
		return -EINVAL;
	res->name = name;
	return 0;
}

static int add_device(struct platform_device **slot, const char *name,
		      struct device *parent, struct fwnode_handle *fwnode,
		      const struct resource *res, unsigned int nres,
		      const struct property_entry *props, const void *data, size_t size)
{
	struct platform_device_info info = {
		.name = name, .id = PLATFORM_DEVID_NONE, .parent = parent,
		.fwnode = fwnode, .res = res, .num_res = nres,
		.properties = props, .data = data, .size_data = size,
	};

	*slot = platform_device_register_full(&info);
	if (IS_ERR(*slot))
		return PTR_ERR(*slot);
	pr_info("TCL ACPI WiFi: registered %s\n", name);
	return 0;
}

static bool bound(struct platform_device *pdev)
{
	bool yes;

	if (IS_ERR_OR_NULL(pdev))
		return false;
	device_lock(&pdev->dev);
	yes = device_is_bound(&pdev->dev);
	device_unlock(&pdev->dev);
	return yes;
}

static int basic_devices(void)
{
	struct resource apcs_res = DEFINE_RES_MEM(0x17c00000, 0x10000);
	struct resource cmd_res = DEFINE_RES_MEM(0x80820000, 0x20000);
	struct resource rmtfs_res = DEFINE_RES_MEM(0x80600000, 0x200000);
	int ret;

	ret = add_device(&cmd, "cmd-db", &pep->dev, NULL, &cmd_res, 1, NULL, NULL, 0);
	if (ret)
		return ret;
	ret = add_device(&apcs, "qcom-apss-shared", &pep->dev,
			 software_node_fwnode(&apcs_node), &apcs_res, 1, NULL, NULL, 0);
	if (ret)
		return ret;
	return add_device(&rmtfs, "qcom_rmtfs_mem", &amss->dev, NULL,
			  &rmtfs_res, 1, rmtfs_props, NULL, 0);
}

static int transport_devices(void)
{
	struct resource rsc_res[] = { DEFINE_RES_MEM_NAMED(0x18220000, 0x10000, "drv-2"), { } };
	struct resource qmp_res[] = { DEFINE_RES_MEM(0xc300000, 0x400), { } };
	struct resource smp_res = { };
	int ret;

	ret = firmware_irq(pep, 4, 37, "drv-2", &rsc_res[1]);
	if (ret)
		return ret;
	ret = firmware_irq(pep, 5, 421, "aoss", &qmp_res[1]);
	if (ret)
		return ret;
	ret = firmware_irq(glnk, 0, 483, "smp2p", &smp_res);
	if (ret)
		return ret;
	ret = add_device(&rsc, "rpmh", &pep->dev, NULL, rsc_res, 2, rsc_props, NULL, 0);
	if (ret)
		return ret;
	ret = add_device(&qmp, "qcom_aoss_qmp", &pep->dev,
			 software_node_fwnode(&qmp_node), qmp_res, 2, NULL, NULL, 0);
	if (ret)
		return ret;
	return add_device(&smp2p, "qcom_smp2p", &glnk->dev,
			  software_node_fwnode(&smp2p_node), &smp_res, 1, NULL, NULL, 0);
}

static int power_devices(void)
{
	struct clk_hw *xo;
	int ret;

	xo = clk_hw_register_fixed_rate(&pep->dev, "xo_board", NULL, 0, 38400000);
	if (IS_ERR(xo))
		return PTR_ERR(xo);
	ret = add_device(&cc, "qcom-sc7180-rpmhcc", &rsc->dev, NULL, NULL, 0, NULL, NULL, 0);
	if (ret)
		return ret;
	ret = add_device(&pd, "qcom-sc7180-rpmhpd", &rsc->dev, NULL, NULL, 0, NULL, NULL, 0);
	if (ret)
		return ret;
	ret = add_device(&rega, "tcl-pm6150-reg", &rsc->dev, NULL, NULL, 0,
			 NULL, &a_data, sizeof(a_data));
	if (ret)
		return ret;
	return add_device(&regc, "tcl-pm6150l-reg", &rsc->dev, NULL, NULL, 0,
			  NULL, &c_data, sizeof(c_data));
}

static int proxy_domain(const char *name, unsigned int index, struct device **target)
{
	struct genpd_onecell_data *data = platform_get_drvdata(pd);
	struct platform_device *consumer;
	int ret;

	if (!data || index >= data->num_domains || !data->domains[index])
		return -EINVAL;
	consumer = platform_device_register_simple(name, PLATFORM_DEVID_NONE, NULL, 0);
	if (IS_ERR(consumer))
		return PTR_ERR(consumer);
	ret = pm_genpd_add_device(data->domains[index], &consumer->dev);
	if (ret) {
		platform_device_unregister(consumer);
		return ret;
	}
	pm_runtime_enable(&consumer->dev);
	*target = &consumer->dev;
	return 0;
}

static int modem_device(void)
{
	static const unsigned int bits[] = { 0, 1, 2, 3, 7 };
	static const char * const names[] = { "fatal", "ready", "handover", "stop-ack", "shutdown-ack" };
	struct resource res[7] = { DEFINE_RES_MEM_NAMED(0x86000000, 0x2000000, "firmware") };
	struct resource edge_irq;
	struct irq_fwspec spec = { .fwnode = software_node_fwnode(&in_node), .param_count = 2 };
	struct clk *xo;
	int ret, i;

	ret = proxy_domain("tcl-mpss-cx", SC7180_CX, &pas_data.proxy_pds[0]);
	if (ret)
		return ret;
	ret = proxy_domain("tcl-mpss-mss", SC7180_MSS, &pas_data.proxy_pds[1]);
	if (ret)
		return ret;
	xo = clk_get_sys(dev_name(&cc->dev), "bi_tcxo");
	if (IS_ERR(xo))
		return PTR_ERR(xo);
	ret = clk_register_clkdev(xo, "xo", "tcl-sc7180-mpss");
	/* Keep the clock reference for the lifetime of this diagnostic boot. */
	if (ret)
		return ret;
	ret = firmware_irq(amss, 0, 298, "wdog", &res[1]);
	if (ret)
		return ret;
	for (i = 0; i < ARRAY_SIZE(bits); i++) {
		spec.param[0] = bits[i];
		spec.param[1] = IRQ_TYPE_EDGE_RISING;
		ret = irq_create_fwspec_mapping(&spec);
		if (ret <= 0)
			return ret ? ret : -ENODEV;
		res[i + 2] = (struct resource)DEFINE_RES_IRQ_NAMED(ret, names[i]);
	}
	ret = firmware_irq(glnk, 3, 481, "glink", &edge_irq);
	if (ret)
		return ret;
	pas_data.glink_node = software_node_fwnode(&glink_node);
	pas_data.glink_irq = edge_irq.start;
	return add_device(&mpss, "tcl-sc7180-mpss", &amss->dev,
			  software_node_fwnode(&pas_node), res, ARRAY_SIZE(res),
			  NULL, &pas_data, sizeof(pas_data));
}

static int wlan_device(void)
{
	struct resource res[14] = {};
	struct resource_entry *entry;
	LIST_HEAD(resources);
	unsigned int nmem = 0;
	int ret, i;

	/* Keep board properties in ACPI _DSD; IORT owns the software node. */
	if (!fwnode_property_read_bool(acpi_fwnode_handle(qwln),
				       "qcom,msa-fixed-perm")) {
		pr_err("TCL WLAN requires the early ACPI MSA fixed-permission property\n");
		return -EINVAL;
	}

	/* Verify all OEM memory descriptors before creating the DMA consumer. */
	ret = acpi_dev_get_resources(qwln, &resources, NULL, NULL);
	if (ret < 0)
		return ret;
	resource_list_for_each_entry(entry, &resources) {
		if (resource_type(entry->res) != IORESOURCE_MEM)
			continue;
		if (nmem == 0 && entry->res->start == 0x18800000 &&
		    resource_size(entry->res) == 0x800000)
			res[0] = (struct resource) {
				.start = entry->res->start,
				.end = entry->res->end,
				.flags = entry->res->flags,
			};
		else if (nmem == 1 && entry->res->start == 0xc250000 &&
			 resource_size(entry->res) == 0x10)
			;
		else if (nmem == 2 && entry->res->start == 0x93900000 &&
			 resource_size(entry->res) == 0x200000)
			res[1] = (struct resource) {
				.start = entry->res->start,
				.end = entry->res->end,
				.flags = entry->res->flags,
			};
		else {
			ret = -EINVAL;
			goto free_resources;
		}
		nmem++;
	}
	ret = nmem == 3 ? 0 : -EINVAL;
free_resources:
	acpi_dev_free_resource_list(&resources);
	if (ret)
		return ret;
	res[0].name = "membase";
	res[1].name = "msa";
	for (i = 0; i < 12; i++) {
		ret = firmware_irq(qwln, i, 446 + i, NULL, &res[i + 2]);
		if (ret)
			return ret;
	}
	return add_device(&wlan, "tcl-wcn3990", &amss->dev,
			  acpi_fwnode_handle(qwln), res, ARRAY_SIZE(res), NULL, NULL, 0);
}

static void wifi_work(struct work_struct *work);
static DECLARE_DELAYED_WORK(wifi_task, wifi_work);

static void wifi_work(struct work_struct *work)
{
	struct device *smem;
	bool smem_bound;
	int ret = 0;

	if (++attempts > 90) {
		pr_err("TCL ACPI WiFi: stage %u timed out; no modem start\n", stage);
		return;
	}
	switch (stage) {
	case 0:
		smem = bus_find_device_by_name(&platform_bus_type, NULL, "qcom-smem");
		if (!smem)
			goto retry;
		device_lock(smem);
		smem_bound = device_is_bound(smem);
		device_unlock(smem);
		put_device(smem);
		if (!smem_bound)
			goto retry;
		ret = basic_devices();
		break;
	case 1:
		if (!bound(cmd) || !bound(apcs) || !bound(rmtfs))
			goto retry;
		ret = transport_devices();
		break;
	case 2:
		if (!bound(rsc) || !bound(qmp) || !bound(smp2p))
			goto retry;
		ret = power_devices();
		break;
	case 3:
		if (!bound(cc) || !bound(pd) || !bound(rega) || !bound(regc))
			goto retry;
		ret = modem_device();
		if (!ret)
			ret = wlan_device();
		break;
	case 4:
		if (!bound(mpss) || !bound(wlan))
			goto retry;
		ready = true;
		pr_info("TCL ACPI WiFi: all drivers bound; userspace may start MPSS after RMTFS service check\n");
		return;
	}
	if (ret) {
		pr_err("TCL ACPI WiFi: stage %u failed: %d; no modem start\n", stage, ret);
		return;
	}
	pr_info("TCL ACPI WiFi: stage %u registered\n", stage);
	stage++;
	attempts = 0;
retry:
	schedule_delayed_work(&wifi_task, msecs_to_jiffies(2000));
}

static int __init tcl_acpi_wifi_init(void)
{
	struct acpi_table_header *table;
	bool match;
	int ret;

	if (acpi_disabled || !efi_enabled(EFI_BOOT) ||
	    ACPI_FAILURE(acpi_get_table(ACPI_SIG_IORT, 0, &table)))
		return -ENODEV;
	match = !memcmp(table->oem_id, "QCOM  ", ACPI_OEM_ID_SIZE) &&
		!memcmp(table->oem_table_id, "QCOMEDK2", ACPI_OEM_TABLE_ID_SIZE) &&
		table->oem_revision == 0x7180;
	acpi_put_table(table);
	if (!match || reserved_window(0x80820000, 0x20000) ||
	    reserved_window(0x80600000, 0x200000) ||
	    reserved_window(0x86000000, 0x2000000) ||
	    reserved_window(0x93900000, 0x200000))
		return -EINVAL;
	pep = find_acpi("\\_SB.PEP0", "QCOM0819");
	glnk = find_acpi("\\_SB.GLNK", "QCOM088D");
	amss = find_acpi("\\_SB.AMSS", "QCOM081E");
	qwln = find_acpi("\\_SB.AMSS.QWLN", NULL);
	if (IS_ERR(pep) || IS_ERR(glnk) || IS_ERR(amss) || IS_ERR(qwln)) {
		ret = -ENODEV;
		goto put_acpi;
	}
	ret = software_node_register_node_group(nodes);
	if (ret)
		goto put_acpi;
	/* No unload: registered providers and their references survive any stage failure. */
	schedule_delayed_work(&wifi_task, 0);
	return 0;
put_acpi:
	if (!IS_ERR(pep))
		acpi_dev_put(pep);
	if (!IS_ERR(glnk))
		acpi_dev_put(glnk);
	if (!IS_ERR(amss))
		acpi_dev_put(amss);
	if (!IS_ERR(qwln))
		acpi_dev_put(qwln);
	return ret;
}
module_init(tcl_acpi_wifi_init);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TCL SC7180 ACPI WiFi diagnostic wiring");
