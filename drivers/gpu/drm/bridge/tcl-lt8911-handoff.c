// SPDX-License-Identifier: GPL-2.0-only
/* Diagnostic TCL B220G bridge: inherits firmware state, cannot cold-start.
 * ONLY register writes are bank selection (0xff), finishing at bank 0x81.
 * MSM still restarts DSI/PHY: successful probe does not prove visible output.
 */
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/property.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_connector.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_probe_helper.h>

struct tcl_handoff {
	struct drm_bridge bridge;
};

static const struct drm_display_mode tcl_mode = {
	.clock = 142520,
	.hdisplay = 1920, .hsync_start = 1978, .hsync_end = 2020,
	.htotal = 2080,
	.vdisplay = 1080, .vsync_start = 1083, .vsync_end = 1088,
	.vtotal = 1142,
	.flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

/* Gate against saved reads; no reset, PLL, GPIO or link-training writes. */
static int tcl_check_state(struct i2c_client *client)
{
	static const struct { u8 bank, reg, value; } expected[] = {
		{0x81, 0x00, 0x17}, {0x81, 0x01, 0x05}, {0x81, 0x02, 0xe0},
		{0xd0, 0x0d, 0x04}, {0xd0, 0x0e, 0x76},
		{0xd0, 0x0f, 0x04}, {0xd0, 0x10, 0x38},
		{0xd0, 0x11, 0x08}, {0xd0, 0x12, 0x20},
		{0xd0, 0x13, 0x07}, {0xd0, 0x14, 0x80},
		{0xd0, 0x15, 0x05}, {0xd0, 0x16, 0x2a},
		{0xd0, 0x17, 0x00}, {0xd0, 0x18, 0x03},
		{0xd0, 0x19, 0x00}, {0xd0, 0x1a, 0x3a},
		{0xa8, 0x05, 0x08}, {0xa8, 0x06, 0x20},
		{0xa8, 0x07, 0x00}, {0xa8, 0x08, 0x66},
		{0xa8, 0x09, 0x00}, {0xa8, 0x0a, 0x2a},
		{0xa8, 0x0b, 0x07}, {0xa8, 0x0c, 0x80},
		{0xa8, 0x0d, 0x04}, {0xa8, 0x0e, 0x76},
		{0xa8, 0x11, 0x00}, {0xa8, 0x12, 0x3b},
		{0xa8, 0x14, 0x05}, {0xa8, 0x15, 0x04},
		{0xa8, 0x16, 0x38}, {0xa8, 0x2d, 0x88},
	};
	int bank = -1, ret = 0, restore, value;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(expected); i++) {
		if (bank != expected[i].bank) {
			ret = i2c_smbus_write_byte_data(client, 0xff, expected[i].bank);
			if (ret < 0)
				break;
			bank = expected[i].bank;
		}
		value = i2c_smbus_read_byte_data(client, expected[i].reg);
		if (value < 0) {
			ret = value;
			break;
		}
		if (value != expected[i].value) {
			dev_err(&client->dev, "inherited state mismatch %02x:%02x=%02x, expected %02x\n",
				bank, expected[i].reg, value, expected[i].value);
			ret = -ENODEV;
			break;
		}
	}
	restore = i2c_smbus_write_byte_data(client, 0xff, 0x81);
	if (restore < 0)
		dev_err(&client->dev, "cannot select final bank 81: %d\n", restore);
	return ret < 0 ? ret : restore;
}

static int tcl_attach(struct drm_bridge *bridge, struct drm_encoder *encoder,
		      enum drm_bridge_attach_flags flags)
{
	return flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR ? 0 : -EINVAL;
}

static enum drm_mode_status tcl_mode_valid(struct drm_bridge *bridge,
		const struct drm_display_info *info, const struct drm_display_mode *mode)
{
	return drm_mode_match(mode, &tcl_mode,
		DRM_MODE_MATCH_TIMINGS | DRM_MODE_MATCH_CLOCK | DRM_MODE_MATCH_FLAGS)
		? MODE_OK : MODE_BAD;
}

static int tcl_get_modes(struct drm_bridge *bridge, struct drm_connector *connector)
{
	struct drm_display_mode *mode = drm_mode_duplicate(bridge->dev, &tcl_mode);

	if (!mode)
		return -ENOMEM;
	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);
	connector->display_info.bpc = 8;
	return 1;
}

static const struct drm_bridge_funcs tcl_bridge_funcs = {
	.attach = tcl_attach,
	.mode_valid = tcl_mode_valid,
	.get_modes = tcl_get_modes,
	.atomic_reset = drm_atomic_helper_bridge_reset,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
};

static int tcl_probe(struct i2c_client *client)
{
	const struct mipi_dsi_device_info info = { .type = "tcl-lt8911-handoff", .channel = 0 };
	struct device *dev = &client->dev;
	struct fwnode_handle *host_node, *ep, *bridge_node;
	struct mipi_dsi_host *host;
	struct mipi_dsi_device *dsi;
	struct tcl_handoff *ctx;
	int ret;

	if (client->addr != 0x29 ||
	    !i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;
	if (!dev->of_node &&
	    (!dmi_match(DMI_SYS_VENDOR, "TCL Communication Ltd.") ||
	     !dmi_match(DMI_PRODUCT_NAME, "B220G") ||
	     !device_property_read_bool(dev, "qcom,tcl-acpi-display")))
		return -ENODEV;
	ep = fwnode_graph_get_endpoint_by_id(dev_fwnode(dev), 0, 0, 0);
	if (!ep)
		return -EINVAL;
	host_node = fwnode_graph_get_remote_port_parent(ep);
	fwnode_handle_put(ep);
	if (!host_node)
		return -EINVAL;
	host = fwnode_find_mipi_dsi_host(host_node);
	fwnode_handle_put(host_node);
	if (!host)
		return -EPROBE_DEFER;
	ret = tcl_check_state(client);
	if (ret)
		return dev_err_probe(dev, ret, "firmware state gate failed\n");
	ctx = devm_drm_bridge_alloc(dev, struct tcl_handoff, bridge,
				    &tcl_bridge_funcs);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);
	ctx->bridge.of_node = dev->of_node;
	/* The ACPI client retains its primary node. The graph lives in its
	 * secondary software node, which must match the DSI remote endpoint.
	 */
	bridge_node = dev_fwnode(dev);
	if (!dev->of_node) {
		bridge_node = bridge_node ? bridge_node->secondary : NULL;
		if (!is_software_node(bridge_node))
			return -EINVAL;
	}
	ctx->bridge.fwnode = bridge_node;
	ctx->bridge.type = DRM_MODE_CONNECTOR_eDP;
	ctx->bridge.ops = DRM_BRIDGE_OP_MODES;
	ret = devm_drm_bridge_add(dev, &ctx->bridge);
	if (ret)
		return ret;
	dsi = devm_mipi_dsi_device_register_full(dev, host, &info);
	if (IS_ERR(dsi))
		return PTR_ERR(dsi);
	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	/* Non-burst sync event; continuous clock is an unverified test choice. */
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO;
	ret = devm_mipi_dsi_attach(dev, dsi);
	if (ret)
		return dev_err_probe(dev, ret, "DSI attach failed\n");
	dev_warn(dev, "experimental firmware handoff; no cold-start/suspend/link recovery\n");
	return 0;
}

static const struct of_device_id tcl_match[] = {
	{ .compatible = "tcl,b220g-lt8911exb-handoff-test" },
	{ }
};
MODULE_DEVICE_TABLE(of, tcl_match);

static const struct i2c_device_id tcl_ids[] = {
	{ "tcl-lt8911-handoff" }, { }
};
MODULE_DEVICE_TABLE(i2c, tcl_ids);

static const struct acpi_device_id tcl_acpi_ids[] = {
	{ "EDPBRIDGE" }, { }
};
MODULE_DEVICE_TABLE(acpi, tcl_acpi_ids);

static struct i2c_driver tcl_driver = {
	.id_table = tcl_ids,
	.driver = { .name = "tcl-lt8911-handoff", .of_match_table = tcl_match,
		    .acpi_match_table = tcl_acpi_ids },
	.probe = tcl_probe,
};
module_i2c_driver(tcl_driver);
MODULE_DESCRIPTION("TCL B220G LT8911EXB firmware handoff experiment");
MODULE_LICENSE("GPL");
