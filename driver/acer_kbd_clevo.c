/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * acer_kbd_clevo.c - keyboard command layer (Clevo WMI protocol)
 *
 * Derived from tuxedo-drivers clevo_keyboard.h (Copyright (c) 2018-2020
 * TUXEDO Computers GmbH). The Clevo WMI command protocol is what the
 * Acer firmware on ABBC0F6x implements for the backlight; only that
 * protocol subset is kept here. Removed: battery charge control,
 * Fn-lock support, the TUXEDO Control Center hotkey combo, the
 * performance-profile workaround for Clevo boards, Uniwill/ACPI
 * transports, and non-keyboard WMI commands.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/version.h>

#include "acer_kbd.h"

/* Firmware event codes are translated to input keys through this map.
 * Entries exist only to report keys; nothing here *drives* hardware. */
static struct key_entry acer_kbd_keymap[] = {
	/* Keyboard backlight */
	{ KE_KEY, ACER_KBD_EVENT_DECREASE, { KEY_KBDILLUMDOWN } },
	{ KE_KEY, ACER_KBD_EVENT_INCREASE, { KEY_KBDILLUMUP } },
	{ KE_KEY, ACER_KBD_EVENT_TOGGLE, { KEY_KBDILLUMTOGGLE } },
	{ KE_KEY, ACER_KBD_EVENT_CYCLE_MODE, { KEY_LIGHTS_TOGGLE } },
	/* Alternate set (e.g. 6-step white keyboards) */
	{ KE_KEY, ACER_KBD_EVENT_DECREASE2, { KEY_KBDILLUMDOWN } },
	{ KE_KEY, ACER_KBD_EVENT_INCREASE2, { KEY_KBDILLUMUP } },
	{ KE_KEY, ACER_KBD_EVENT_TOGGLE2, { KEY_KBDILLUMTOGGLE } },
	/* Touchpad toggle as KEY_F21, matching userspace expectations */
	{ KE_KEY, ACER_KBD_EVENT_TOUCHPAD_TOGGLE, { KEY_F21 } },
	{ KE_KEY, ACER_KBD_EVENT_TOUCHPAD_OFF, { KEY_F21 } },
	{ KE_KEY, ACER_KBD_EVENT_TOUCHPAD_ON, { KEY_F21 } },
	/* Rfkill, still needed by some devices */
	{ KE_KEY, ACER_KBD_EVENT_RFKILL1, { KEY_RFKILL } },
	{ KE_IGNORE, ACER_KBD_EVENT_RFKILL2, { KEY_RFKILL } },
	/* Volume events must be ignored to not interfere with firmware handling */
	{ KE_IGNORE, 0xfa, { KEY_UNKNOWN } },
	{ KE_IGNORE, 0xfb, { KEY_UNKNOWN } },
	{ KE_END, 0 }
};

static struct {
	u8 mode;
} kbd_state = {
	.mode = 0,
};

static const struct {
	u8 key;
	u32 value;
	const char *const name;
} kbd_backlight_modes[] = {
	{ 0, 0x00000000, "CUSTOM" },
	{ 1, 0x1002a000, "BREATHE" },
	{ 2, 0x33010000, "CYCLE" },
	{ 3, 0x80000000, "DANCE" },
	{ 4, 0xA0000000, "FLASH" },
	{ 5, 0x70000000, "RANDOM_COLOR" },
	{ 6, 0x90000000, "TEMPO" },
	{ 7, 0xB0000000, "WAVE" },
};

static void set_kbd_backlight_mode(u8 mode)
{
	if (mode >= ARRAY_SIZE(kbd_backlight_modes))
		return;

	AKB_DEBUG("set backlight mode %s\n", kbd_backlight_modes[mode].name);

	if (!acer_kbd_evaluate(ACER_KBD_CMD_SET_KB_RGB_LEDS,
			       kbd_backlight_modes[mode].value, NULL))
		kbd_state.mode = mode;
}

static ssize_t kbd_backlight_mode_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	return sysfs_emit(buf, "%d\n", kbd_state.mode);
}

static ssize_t kbd_backlight_mode_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	unsigned int mode;

	if (kstrtouint(buf, 0, &mode))
		return -EINVAL;

	mode = clamp_t(unsigned int, mode, 0, ARRAY_SIZE(kbd_backlight_modes) - 1);
	set_kbd_backlight_mode((u8)mode);

	return size;
}

static DEVICE_ATTR_RW(kbd_backlight_mode);

static int kbd_backlight_mode_validate(const char *value,
				       const struct kernel_param *param)
{
	int mode = 0;

	if (kstrtoint(value, 10, &mode) ||
	    mode < 0 || mode > (int)ARRAY_SIZE(kbd_backlight_modes) - 1)
		return -EINVAL;

	return param_set_int(value, param);
}

static const struct kernel_param_ops kbd_backlight_mode_ops = {
	.set = kbd_backlight_mode_validate,
	.get = param_get_int,
};

static u8 param_kbd_backlight_mode;
module_param_cb(kbd_backlight_mode, &kbd_backlight_mode_ops,
		&param_kbd_backlight_mode, 0444);
MODULE_PARM_DESC(kbd_backlight_mode,
		 "Keyboard backlight animation mode number (lists in sysfs)");

/* Firmware event dispatch. Only backlight keys are acted on; every
 * other event falls through to sparse-keymap reporting below. */
void acer_kbd_event(u32 event)
{
	AKB_DEBUG("firmware event %#04x\n", event);

	switch (event) {
	case ACER_KBD_EVENT_CYCLE_BRIGHTNESS:
		acer_kbd_leds_brightness_notify();
		break;
	default:
		break;
	}

	if (acer_kbd_current_driver && acer_kbd_current_driver->input_device &&
	    !acer_kbd_report_known_event(acer_kbd_current_driver->input_device,
					 event, 1, true))
		AKB_DEBUG("unknown key event %#04x\n", event);
}

/* ---- transport attach/detach (called from WMI probe/remove) ---- */

static struct acer_kbd_driver acer_kbd_driver;

static bool transport_active;
static DEFINE_MUTEX(transport_lock);

int acer_kbd_transport_ready(void)
{
	return transport_active;
}

int acer_kbd_add_wmi_transport(void)
{
	int ret = 0;

	mutex_lock(&transport_lock);
	if (transport_active)
		goto out;

	acer_kbd_evaluate(ACER_KBD_CMD_SET_EVENTS_ENABLED, 0, NULL);
	transport_active = true;

	if (acer_kbd_init_driver(&acer_kbd_driver)) {
		transport_active = false;
		ret = -ENODEV;
	}
out:
	mutex_unlock(&transport_lock);
	return ret;
}

void acer_kbd_remove_wmi_transport(void)
{
	mutex_lock(&transport_lock);
	if (transport_active) {
		acer_kbd_remove_driver();
		transport_active = false;
	}
	mutex_unlock(&transport_lock);
}

/* ---- platform driver ---- */

static bool mode_attr_created;

static int acer_kbd_probe(struct platform_device *dev)
{
	int ret;

	ret = acer_kbd_leds_init(dev);
	if (ret)
		return ret;

	/* Animation-mode sysfs only exists on 3-zone firmware. */
	if (acer_kbd_leds_get_type() == ACER_KBD_BACKLIGHT_3_ZONE_RGB) {
		ret = device_create_file(&dev->dev, &dev_attr_kbd_backlight_mode);
		if (ret)
			AKB_ERROR("kbd_backlight_mode sysfs creation failed\n");
		else
			mode_attr_created = true;
	}

	kbd_state.mode = param_kbd_backlight_mode;
	set_kbd_backlight_mode(kbd_state.mode);

	acer_kbd_evaluate(ACER_KBD_CMD_SET_EVENTS_ENABLED, 0, NULL);

	return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 11, 0)
static int acer_kbd_remove(struct platform_device *dev)
#else
static void acer_kbd_remove(struct platform_device *dev)
#endif
{
	if (mode_attr_created) {
		device_remove_file(&dev->dev, &dev_attr_kbd_backlight_mode);
		mode_attr_created = false;
	}
	acer_kbd_leds_remove(dev);
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 11, 0)
	return 0;
#endif
}

static int acer_kbd_suspend(struct platform_device *dev, pm_message_t state)
{
	acer_kbd_leds_suspend();
	return 0;
}

static int acer_kbd_resume(struct platform_device *dev)
{
	acer_kbd_evaluate(ACER_KBD_CMD_SET_EVENTS_ENABLED, 0, NULL);
	acer_kbd_leds_restore_state();
	acer_kbd_leds_resume();
	return 0;
}

static struct platform_driver acer_kbd_platform_driver = {
	.remove = acer_kbd_remove,
	.suspend = acer_kbd_suspend,
	.resume = acer_kbd_resume,
	.driver = {
		.name = ACER_KBD_DRIVER_NAME,
		.owner = THIS_MODULE,
	},
};

static struct acer_kbd_driver acer_kbd_driver = {
	.platform_driver = &acer_kbd_platform_driver,
	.probe = acer_kbd_probe,
	.key_map = acer_kbd_keymap,
};
