/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * acer_kbd_core.c - module init, hardware gate, platform/input devices
 *
 * Derived from tuxedo-drivers tuxedo_keyboard.c (Copyright (c) 2018-2020
 * TUXEDO Computers GmbH). The TUXEDO DMI whitelist module is replaced
 * by a built-in Acer allowlist below; Fn-lock plumbing is gone (the
 * feature is ACPI-only and unavailable over WMI).
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/mutex.h>

#include "acer_kbd.h"

MODULE_AUTHOR("Acer Kbd Backlight Project");
MODULE_DESCRIPTION("Keyboard backlight driver for Acer laptops (ABBC0F6x WMI)");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0.0");

/* Hardware gate: load only on allowlisted Acer systems. `force=1`
 * overrides the gate for testing on other models with the same WMI
 * GUIDs. Scoped narrowly on purpose so this driver can never hijack
 * genuine TUXEDO/Clevo hardware. */
static const struct dmi_system_id acer_kbd_dmi_table[] __initconst = {
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Aspire A715-79G"),
		},
	},
	{ }
};

static bool force;
module_param(force, bool, 0444);
MODULE_PARM_DESC(force, "Skip the Acer DMI allowlist check");

struct platform_device *acer_kbd_platform_device;
struct acer_kbd_driver *acer_kbd_current_driver;
static struct input_dev *acer_kbd_input_device;
static DEFINE_MUTEX(acer_kbd_init_lock);

static int acer_kbd_input_init(const struct key_entry *key_map)
{
	int err;

	acer_kbd_input_device = input_allocate_device();
	if (!acer_kbd_input_device) {
		AKB_ERROR("input device allocation failed\n");
		return -ENOMEM;
	}

	acer_kbd_input_device->name = ACER_KBD_INPUT_NAME;
	acer_kbd_input_device->phys = ACER_KBD_DRIVER_NAME "/input0";
	acer_kbd_input_device->id.bustype = BUS_HOST;
	acer_kbd_input_device->dev.parent = &acer_kbd_platform_device->dev;

	if (key_map) {
		err = sparse_keymap_setup(acer_kbd_input_device, key_map, NULL);
		if (err) {
			AKB_ERROR("sparse keymap setup failed\n");
			goto err_free;
		}
	}

	err = input_register_device(acer_kbd_input_device);
	if (err) {
		AKB_ERROR("input device registration failed\n");
		goto err_free;
	}

	return 0;

err_free:
	input_free_device(acer_kbd_input_device);
	acer_kbd_input_device = NULL;
	return err;
}

/* Returns 0 when the driver bundle is up (or was already up),
 * negative errno on failure. */
int acer_kbd_init_driver(struct acer_kbd_driver *driver)
{
	struct platform_device *pdev;

	AKB_DEBUG("init driver\n");

	mutex_lock(&acer_kbd_init_lock);

	if (acer_kbd_platform_device) {
		AKB_DEBUG("platform device already initialized\n");
		goto success;
	}

	pdev = platform_create_bundle(driver->platform_driver,
				      driver->probe, NULL, 0, NULL, 0);
	acer_kbd_platform_device = pdev;
	if (!pdev) {
		AKB_ERROR("platform bundle creation failed\n");
		mutex_unlock(&acer_kbd_init_lock);
		return -ENODEV;
	}

	if (driver->key_map) {
		if (acer_kbd_input_init(driver->key_map))
			driver->input_device = NULL;
		else
			driver->input_device = acer_kbd_input_device;
	}

	acer_kbd_current_driver = driver;
success:
	mutex_unlock(&acer_kbd_init_lock);
	return 0;
}

void acer_kbd_remove_driver(void)
{
	mutex_lock(&acer_kbd_init_lock);

	if (acer_kbd_input_device) {
		input_unregister_device(acer_kbd_input_device);
		acer_kbd_input_device = NULL;
	}
	if (acer_kbd_platform_device) {
		platform_device_unregister(acer_kbd_platform_device);
		acer_kbd_platform_device = NULL;
	}
	if (acer_kbd_current_driver) {
		platform_driver_unregister(
			acer_kbd_current_driver->platform_driver);
		acer_kbd_current_driver = NULL;
	}

	mutex_unlock(&acer_kbd_init_lock);
}

static int __init acer_kbd_init(void)
{
	AKB_INFO("loading (force=%d)\n", force);

	if (!force && !dmi_check_system(acer_kbd_dmi_table)) {
		AKB_ERROR("unsupported system, refusing to load (use force=1 to override)\n");
		return -ENODEV;
	}

	return acer_kbd_wmi_register();
}

static void __exit acer_kbd_exit(void)
{
	acer_kbd_wmi_unregister();
	acer_kbd_remove_driver();
	AKB_INFO("unloaded\n");
}

module_init(acer_kbd_init);
module_exit(acer_kbd_exit);
