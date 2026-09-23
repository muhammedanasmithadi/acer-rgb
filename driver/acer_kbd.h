/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * acer_kbd.h - shared definitions for the Acer keyboard backlight driver
 *
 * Derived from tuxedo-drivers (Copyright (c) 2018-2021 TUXEDO Computers
 * GmbH), stripped down to the keyboard-backlight path and reworked for
 * Acer laptops exposing the ABBC0F6x WMI GUIDs (verified on the Acer
 * Aspire A715-79G). Everything not required to drive the keyboard
 * backlight LED and its Fn hotkeys was removed:
 *
 *   - battery charge control ("flexicharger") including its probe-time
 *     EC *write* used for feature detection
 *   - Fn-lock support (ACPI-only, unavailable over WMI)
 *   - TUXEDO Control Center hotkey combo (Meta+Alt+F6)
 *   - performance-profile workarounds for Clevo boards
 *   - Uniwill / Clevo-ACPI transports (WMI only)
 *   - non-keyboard WMI commands (fan, webcam, touchpad, flightmode)
 *
 * All hardware access in this driver is firmware-mediated through
 * wmi_evaluate_method(); there is no direct port I/O, MMIO, or EC RAM
 * access anywhere in this codebase.
 */

#ifndef ACER_KBD_H
#define ACER_KBD_H

#include <linux/types.h>
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/input/sparse-keymap.h>

/* WMI GUIDs exposed by the Acer firmware (ABBC0F6x family). Only the
 * event (notification source) and method (command) GUIDs are used.
 * ABBC0F6C exists on the platform but carries no keyboard-backlight
 * function and is intentionally left alone. */
#define ACER_KBD_WMI_EVENT_GUID		"ABBC0F6B-8EA1-11D1-00A0-C90629100000"
#define ACER_KBD_WMI_METHOD_GUID	"ABBC0F6D-8EA1-11D1-00A0-C90629100000"

/* ---- WMI commands, keyboard-backlight scope only ---- */
/* Get commands (no parameter) */
#define ACER_KBD_CMD_GET_EVENT			0x01
#define ACER_KBD_CMD_GET_SPECS			0x0D /* buffer; byte 0x0f holds backlight type */
#define ACER_KBD_CMD_GET_BIOS_FEATURES_1	0x52 /* probe + white/3-zone detection */
#define ACER_KBD_CMD_GET_BIOS_FEATURES_1_SUB_WHITE_ONLY_KB	0x40000000
#define ACER_KBD_CMD_GET_BIOS_FEATURES_1_SUB_3_ZONE_RGB_KB	0x00400000
#define ACER_KBD_CMD_GET_BIOS_FEATURES_2	0x7A
#define ACER_KBD_CMD_GET_BIOS_FEATURES_2_SUB_WHITE_ONLY_KB_MAX_5	0x4000
#define ACER_KBD_CMD_GET_KB_WHITE_LEDS		0x3D

/* Set commands (take a parameter) */
#define ACER_KBD_CMD_SET_EVENTS_ENABLED		0x46
#define ACER_KBD_CMD_SET_KB_WHITE_LEDS		0x27
#define ACER_KBD_CMD_SET_KB_RGB_LEDS		0x67
#define ACER_KBD_CMD_SET_KB_LEDS_SUB_RGB_ZONE_0	0xF0000000
#define ACER_KBD_CMD_SET_KB_LEDS_SUB_RGB_ZONE_1	0xF1000000
#define ACER_KBD_CMD_SET_KB_LEDS_SUB_RGB_ZONE_2	0xF2000000
#define ACER_KBD_CMD_SET_KB_LEDS_SUB_RGB_BRIGHTNESS	0xF4000000

/* ---- Firmware event codes reported through the event GUID ---- */
#define ACER_KBD_EVENT_DECREASE			0x81
#define ACER_KBD_EVENT_INCREASE			0x82
#define ACER_KBD_EVENT_CYCLE_MODE		0x83
#define ACER_KBD_EVENT_CYCLE_BRIGHTNESS		0x8A
#define ACER_KBD_EVENT_TOGGLE			0x9F
/* Alternate set (e.g. 6-step white keyboards) */
#define ACER_KBD_EVENT_DECREASE2		0x20
#define ACER_KBD_EVENT_INCREASE2		0x21
#define ACER_KBD_EVENT_TOGGLE2			0x3f
/* Touchpad / rfkill / volume passthrough (reported only, never driven) */
#define ACER_KBD_EVENT_TOUCHPAD_TOGGLE		0x5D
#define ACER_KBD_EVENT_TOUCHPAD_OFF		0xFC
#define ACER_KBD_EVENT_TOUCHPAD_ON		0xFD
#define ACER_KBD_EVENT_RFKILL1			0x85
#define ACER_KBD_EVENT_RFKILL2			0x86 /* older duplicate, ignored */

/* Driver / device identities. The LED name is deliberately identical to
 * the one tuxedo_keyboard exposed ("rgb:kbd_backlight") so that all
 * existing userspace (kbd-rgbd, brightnessctl scripts, Hyprland binds)
 * keeps working without changes. */
#define ACER_KBD_DRIVER_NAME	"acer_kbd_backlight"
#define ACER_KBD_INPUT_NAME	"Acer Kbd Backlight"

/* Platform driver bundle owned by the core. */
struct acer_kbd_driver {
	struct platform_driver *platform_driver;
	int (*probe)(struct platform_device *);
	struct key_entry *key_map;
	struct input_dev *input_device;
};

extern struct platform_device *acer_kbd_platform_device;
extern struct acer_kbd_driver *acer_kbd_current_driver;

/* Report a firmware event through the sparse keymap, ignoring unknown
 * codes instead of emitting KEY_UNKNOWN (upstream tuxedo behavior). */
static inline bool acer_kbd_report_known_event(struct input_dev *dev,
					       unsigned int code,
					       unsigned int value,
					       bool autorelease)
{
	const struct key_entry *ke = sparse_keymap_entry_from_scancode(dev, code);

	if (ke) {
		sparse_keymap_report_entry(dev, ke, value, autorelease);
		return true;
	}

	return false;
}

/* Logging helpers (pr_fmt is defined per .c file). */
#define AKB_INFO(fmt, ...)	pr_info(fmt, ##__VA_ARGS__)
#define AKB_ERROR(fmt, ...)	pr_err(fmt, ##__VA_ARGS__)
#define AKB_DEBUG(fmt, ...)	pr_debug("[%s:%u] " fmt, __func__, __LINE__, ##__VA_ARGS__)

/* ---- transport (acer_kbd_wmi.c) ---- */
int acer_kbd_evaluate(u8 cmd, u32 arg, u32 *result);
int acer_kbd_evaluate2(u8 cmd, u32 arg, union acpi_object **result);
int acer_kbd_transport_ready(void);
int acer_kbd_wmi_register(void);
void acer_kbd_wmi_unregister(void);

/* ---- core (acer_kbd_core.c) ---- */
int acer_kbd_init_driver(struct acer_kbd_driver *driver);
void acer_kbd_remove_driver(void);

/* ---- clevo command layer (acer_kbd_clevo.c) ---- */
int acer_kbd_add_wmi_transport(void);
void acer_kbd_remove_wmi_transport(void);
void acer_kbd_event(u32 event);

/* ---- LED layer (acer_kbd_led.c) ---- */
enum acer_kbd_backlight_type {
	ACER_KBD_BACKLIGHT_NONE = 0x00,
	ACER_KBD_BACKLIGHT_FIXED_COLOR = 0x01,
	ACER_KBD_BACKLIGHT_3_ZONE_RGB = 0x02,
	ACER_KBD_BACKLIGHT_1_ZONE_RGB = 0x06,
};

int acer_kbd_leds_init(struct platform_device *dev);
void acer_kbd_leds_remove(struct platform_device *dev);
void acer_kbd_leds_suspend(void);
void acer_kbd_leds_resume(void);
void acer_kbd_leds_restore_state(void);
enum acer_kbd_backlight_type acer_kbd_leds_get_type(void);
void acer_kbd_leds_set_brightness(u8 brightness);
void acer_kbd_leds_set_color(u32 color);
void acer_kbd_leds_brightness_notify(void);

/* Standard color table shared by the color-cycle key handler. */
struct acer_kbd_color {
	u32 code;
	const char *name;
};

struct acer_kbd_color_list {
	unsigned int size;
	struct acer_kbd_color colors[];
};

extern struct acer_kbd_color_list acer_kbd_color_list;

#endif /* ACER_KBD_H */
