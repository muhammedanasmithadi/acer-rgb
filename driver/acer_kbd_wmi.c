/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * acer_kbd_wmi.c - WMI transport for the Acer keyboard backlight driver
 *
 * Derived from tuxedo-drivers clevo_wmi.c (Copyright (c) 2020-2021
 * TUXEDO Computers GmbH). Reduced to a single static transport: the
 * package-buffer call variant was only used by battery charge control
 * and is gone with it.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/wmi.h>

#include "acer_kbd.h"

static int acer_kbd_wmi_evaluate(u32 wmi_method_id, u32 wmi_arg,
				 union acpi_object **result)
{
	struct acpi_buffer in = { sizeof(wmi_arg), &wmi_arg };
	struct acpi_buffer out = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	acpi_status status;

	status = wmi_evaluate_method(ACER_KBD_WMI_METHOD_GUID, 0x00,
				     wmi_method_id, &in, &out);
	if (ACPI_FAILURE(status)) {
		AKB_ERROR("failed to evaluate WMI method 0x%02x\n", wmi_method_id);
		return -EIO;
	}

	obj = out.pointer;
	if (!obj) {
		AKB_ERROR("WMI method 0x%02x returned no object\n", wmi_method_id);
		return -EIO;
	}

	if (result)
		*result = obj;
	else
		ACPI_FREE(obj);

	return 0;
}

/* Fire-and-forget variant for set commands whose reply is irrelevant. */
int acer_kbd_evaluate(u8 cmd, u32 arg, u32 *result)
{
	int status;
	union acpi_object *obj = NULL;

	status = acer_kbd_evaluate2(cmd, arg, &obj);
	if (status)
		return status;

	if (obj->type == ACPI_TYPE_INTEGER) {
		if (result)
			*result = (u32)obj->integer.value;
	} else {
		AKB_ERROR("cmd 0x%02x returned non-integer, use acer_kbd_evaluate2\n",
			  cmd);
		status = -ENODATA;
	}
	ACPI_FREE(obj);

	return status;
}

int acer_kbd_evaluate2(u8 cmd, u32 arg, union acpi_object **result)
{
	if (!acer_kbd_transport_ready()) {
		AKB_ERROR("no active transport while attempting cmd %02x arg %08x\n",
			  cmd, arg);
		return -ENODEV;
	}
	return acer_kbd_wmi_evaluate(cmd, arg, result);
}

static int acer_kbd_wmi_probe(struct wmi_device *wdev, const void *context)
{
	int status;
	union acpi_object *obj = NULL;

	if (!wmi_has_guid(ACER_KBD_WMI_EVENT_GUID)) {
		AKB_DEBUG("probe: event GUID missing\n");
		return -ENODEV;
	}

	if (!wmi_has_guid(ACER_KBD_WMI_METHOD_GUID)) {
		AKB_DEBUG("probe: method GUID missing\n");
		return -ENODEV;
	}

	/* The GUIDs are shared across vendors, so sanity-check the
	 * firmware with a known general method. It must answer with a
	 * non-error integer. */
	status = acer_kbd_wmi_evaluate(ACER_KBD_CMD_GET_BIOS_FEATURES_1,
				       0, &obj);
	if (status < 0) {
		AKB_DEBUG("probe: GUIDs present but method call failed\n");
		return -ENODEV;
	}
	if (obj->type != ACPI_TYPE_INTEGER ||
	    (u32)obj->integer.value == 0xffffffff) {
		AKB_DEBUG("probe: GUIDs present but method returned unexpected value\n");
		ACPI_FREE(obj);
		return -ENODEV;
	}
	ACPI_FREE(obj);

	AKB_INFO("WMI interface initialized\n");

	return acer_kbd_add_wmi_transport();
}

static void acer_kbd_wmi_remove(struct wmi_device *wdev)
{
	acer_kbd_remove_wmi_transport();
}

static void acer_kbd_wmi_notify(struct wmi_device *wdev, union acpi_object *data)
{
	u32 event = 0;
	union acpi_object *obj = NULL;

	/* The notification itself carries no data; poll the event register. */
	if (!acer_kbd_wmi_evaluate(ACER_KBD_CMD_GET_EVENT, 0, &obj)) {
		if (obj->type == ACPI_TYPE_INTEGER)
			event = (u32)obj->integer.value;
		else
			AKB_ERROR("event poll returned non-integer\n");
		ACPI_FREE(obj);
	}
	if (event)
		acer_kbd_event(event);
}

static const struct wmi_device_id acer_kbd_wmi_ids[] = {
	/* One entry is enough; this driver handles all of its GUIDs. */
	{ .guid_string = ACER_KBD_WMI_EVENT_GUID },
	{ }
};

static struct wmi_driver acer_kbd_wmi_driver = {
	.driver = {
		.name = ACER_KBD_DRIVER_NAME "_wmi",
		.owner = THIS_MODULE,
	},
	.id_table = acer_kbd_wmi_ids,
	.probe = acer_kbd_wmi_probe,
	.remove = acer_kbd_wmi_remove,
	.notify = acer_kbd_wmi_notify,
};

int acer_kbd_wmi_register(void)
{
	return wmi_driver_register(&acer_kbd_wmi_driver);
}

void acer_kbd_wmi_unregister(void)
{
	wmi_driver_unregister(&acer_kbd_wmi_driver);
}

MODULE_DEVICE_TABLE(wmi, acer_kbd_wmi_ids);
MODULE_ALIAS("wmi:" ACER_KBD_WMI_EVENT_GUID);
MODULE_ALIAS("wmi:" ACER_KBD_WMI_METHOD_GUID);
