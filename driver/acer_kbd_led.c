/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * acer_kbd_led.c - keyboard backlight LED class devices
 *
 * Derived from tuxedo-drivers clevo_leds.h (Copyright (c) 2018-2020
 * TUXEDO Computers GmbH). Battery charge-control code, board-specific
 * quirks for non-Acer hardware, and dead commented-out code were
 * removed. The TODOs about not exposing set_brightness/set_color
 * externally are resolved by keeping them file-static: this is a
 * single-module driver with no external users.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/led-class-multicolor.h>
#include <linux/delay.h>

#include "acer_kbd.h"

#define ACER_KBD_BRIGHTNESS_MAX		0xff
#define ACER_KBD_BRIGHTNESS_DEFAULT	0x00

#define ACER_KBD_BRIGHTNESS_WHITE_MAX		0x02
#define ACER_KBD_BRIGHTNESS_WHITE_DEFAULT	0x00

#define ACER_KBD_BRIGHTNESS_WHITE_MAX_5		0x05
#define ACER_KBD_BRIGHTNESS_WHITE_MAX_5_DEFAULT	0x00

#define ACER_KBD_COLOR_DEFAULT	0xffffff

static enum acer_kbd_backlight_type acer_kbd_backlight_type =
	ACER_KBD_BACKLIGHT_NONE;
static bool leds_initialized;

static void color_scaling(enum acer_kbd_backlight_type *type,
			  u8 *red, u8 *green, u8 *blue)
{
	if (*type == ACER_KBD_BACKLIGHT_1_ZONE_RGB) {
		*red = (180 * *red) / 255;
		*blue = (200 * *blue) / 255;
	}
}

static int acer_kbd_set_white_brightness(u8 brightness)
{
	AKB_DEBUG("set white brightness %u\n", brightness);

	return acer_kbd_evaluate(ACER_KBD_CMD_SET_KB_WHITE_LEDS,
				 brightness, NULL);
}

static int acer_kbd_set_rgb_brightness(u8 brightness)
{
	AKB_DEBUG("set RGB brightness %u\n", brightness);

	return acer_kbd_evaluate(ACER_KBD_CMD_SET_KB_RGB_LEDS,
				 ACER_KBD_CMD_SET_KB_LEDS_SUB_RGB_BRIGHTNESS |
				 brightness,
				 NULL);
}

static int acer_kbd_set_rgb_color(u32 zone, u32 color)
{
	u32 cset = ((color & 0x0000FF) << 16) |
		   ((color & 0xFF0000) >> 8) |
		   ((color & 0x00FF00) >> 8);

	AKB_DEBUG("set color 0x%08x for zone 0x%08x\n", color, zone);

	return acer_kbd_evaluate(ACER_KBD_CMD_SET_KB_RGB_LEDS,
				 zone | cset, NULL);
}

/* Toggles the whole keyboard on/off around suspend. Issued on init,
 * suspend, and resume; the firmware on the Aspire A715-79G honors it. */
static int acer_kbd_set_keyboard_status(u8 state)
{
	u32 arg = 0xE0000000 | (state ? 0x07F001 : 0x003001);

	AKB_DEBUG("set keyboard enabled: %u\n", state);

	return acer_kbd_evaluate(ACER_KBD_CMD_SET_KB_RGB_LEDS, arg, NULL);
}

static void acer_kbd_led_brightness_set(struct led_classdev *led_cdev,
					 enum led_brightness brightness)
{
	if (acer_kbd_set_white_brightness(brightness)) {
		AKB_DEBUG("set white brightness failed\n");
		return;
	}
	led_cdev->brightness = brightness;
}

/* Firmware-side master brightness plus a color refresh, so writers of
 * multi_intensity observe the change immediately. */
static struct led_classdev_mc acer_kbd_mcled_cdevs[3];
static void acer_kbd_led_brightness_set_mc(struct led_classdev *led_cdev,
					    enum led_brightness brightness)
{
	struct led_classdev_mc *mcled_cdev = lcdev_to_mccdev(led_cdev);
	u32 zone, color;
	u8 red, green, blue;

	if (acer_kbd_set_rgb_brightness(brightness)) {
		AKB_DEBUG("set RGB brightness failed\n");
		return;
	}
	acer_kbd_mcled_cdevs[0].led_cdev.brightness = brightness;
	acer_kbd_mcled_cdevs[1].led_cdev.brightness = brightness;
	acer_kbd_mcled_cdevs[2].led_cdev.brightness = brightness;

	zone = mcled_cdev->subled_info[0].channel;

	red = mcled_cdev->subled_info[0].intensity;
	green = mcled_cdev->subled_info[1].intensity;
	blue = mcled_cdev->subled_info[2].intensity;

	color_scaling(&acer_kbd_backlight_type, &red, &green, &blue);
	color = (red << 16) + (green << 8) + blue;

	if (acer_kbd_set_rgb_color(zone, color))
		AKB_DEBUG("set RGB color failed\n");
}

static struct led_classdev acer_kbd_led_cdev = {
	.name = "white:" LED_FUNCTION_KBD_BACKLIGHT,
	.max_brightness = ACER_KBD_BRIGHTNESS_WHITE_MAX,
	.brightness_set = &acer_kbd_led_brightness_set,
	.brightness = ACER_KBD_BRIGHTNESS_WHITE_DEFAULT,
	.flags = LED_BRIGHT_HW_CHANGED,
};

static struct mc_subled acer_kbd_mcled_subleds[3][3] = {
	{
		{ .color_index = LED_COLOR_ID_RED,
		  .brightness = ACER_KBD_BRIGHTNESS_DEFAULT,
		  .intensity = 0xff,
		  .channel = ACER_KBD_CMD_SET_KB_LEDS_SUB_RGB_ZONE_0 },
		{ .color_index = LED_COLOR_ID_GREEN,
		  .brightness = ACER_KBD_BRIGHTNESS_DEFAULT,
		  .intensity = 0xff,
		  .channel = ACER_KBD_CMD_SET_KB_LEDS_SUB_RGB_ZONE_0 },
		{ .color_index = LED_COLOR_ID_BLUE,
		  .brightness = ACER_KBD_BRIGHTNESS_DEFAULT,
		  .intensity = 0xff,
		  .channel = ACER_KBD_CMD_SET_KB_LEDS_SUB_RGB_ZONE_0 },
	},
	{
		{ .color_index = LED_COLOR_ID_RED,
		  .brightness = ACER_KBD_BRIGHTNESS_DEFAULT,
		  .intensity = 0xff,
		  .channel = ACER_KBD_CMD_SET_KB_LEDS_SUB_RGB_ZONE_1 },
		{ .color_index = LED_COLOR_ID_GREEN,
		  .brightness = ACER_KBD_BRIGHTNESS_DEFAULT,
		  .intensity = 0xff,
		  .channel = ACER_KBD_CMD_SET_KB_LEDS_SUB_RGB_ZONE_1 },
		{ .color_index = LED_COLOR_ID_BLUE,
		  .brightness = ACER_KBD_BRIGHTNESS_DEFAULT,
		  .intensity = 0xff,
		  .channel = ACER_KBD_CMD_SET_KB_LEDS_SUB_RGB_ZONE_1 },
	},
	{
		{ .color_index = LED_COLOR_ID_RED,
		  .brightness = ACER_KBD_BRIGHTNESS_DEFAULT,
		  .intensity = 0xff,
		  .channel = ACER_KBD_CMD_SET_KB_LEDS_SUB_RGB_ZONE_2 },
		{ .color_index = LED_COLOR_ID_GREEN,
		  .brightness = ACER_KBD_BRIGHTNESS_DEFAULT,
		  .intensity = 0xff,
		  .channel = ACER_KBD_CMD_SET_KB_LEDS_SUB_RGB_ZONE_2 },
	},
};

static struct led_classdev_mc acer_kbd_mcled_cdevs[3] = {
	{
		.led_cdev.name = "rgb:" LED_FUNCTION_KBD_BACKLIGHT,
		.led_cdev.max_brightness = ACER_KBD_BRIGHTNESS_MAX,
		.led_cdev.brightness_set = &acer_kbd_led_brightness_set_mc,
		.led_cdev.brightness = ACER_KBD_BRIGHTNESS_DEFAULT,
		.num_colors = 3,
		.subled_info = acer_kbd_mcled_subleds[0],
	},
	{
		.led_cdev.name = "rgb:" LED_FUNCTION_KBD_BACKLIGHT,
		.led_cdev.max_brightness = ACER_KBD_BRIGHTNESS_MAX,
		.led_cdev.brightness_set = &acer_kbd_led_brightness_set_mc,
		.led_cdev.brightness = ACER_KBD_BRIGHTNESS_DEFAULT,
		.num_colors = 3,
		.subled_info = acer_kbd_mcled_subleds[1],
	},
	{
		.led_cdev.name = "rgb:" LED_FUNCTION_KBD_BACKLIGHT,
		.led_cdev.max_brightness = ACER_KBD_BRIGHTNESS_MAX,
		.led_cdev.brightness_set = &acer_kbd_led_brightness_set_mc,
		.led_cdev.brightness = ACER_KBD_BRIGHTNESS_DEFAULT,
		.num_colors = 3,
		.subled_info = acer_kbd_mcled_subleds[2],
	},
};

int acer_kbd_leds_init(struct platform_device *dev)
{
	int ret, i, status;
	union acpi_object *result;
	u32 features;

	/* Preferred detection: the SPECS buffer, byte 0x0f. */
	for (i = 0; i < 3; ++i) {
		status = acer_kbd_evaluate2(ACER_KBD_CMD_GET_SPECS, 0, &result);
		if (!status) {
			if (result->type == ACPI_TYPE_BUFFER) {
				AKB_DEBUG("GET_SPECS byte[0x0f]: 0x%02x\n",
					  result->buffer.pointer[0x0f]);
				acer_kbd_backlight_type = result->buffer.pointer[0x0f];
				if (acer_kbd_backlight_type) {
					status = acer_kbd_evaluate(
						ACER_KBD_CMD_GET_BIOS_FEATURES_2,
						0, &features);
					if (!status) {
						AKB_DEBUG("FEATURES_2: 0x%08x\n",
							  features);
						if (features &
						    ACER_KBD_CMD_GET_BIOS_FEATURES_2_SUB_WHITE_ONLY_KB_MAX_5) {
							acer_kbd_led_cdev.max_brightness =
								ACER_KBD_BRIGHTNESS_WHITE_MAX_5;
							acer_kbd_led_cdev.brightness =
								ACER_KBD_BRIGHTNESS_WHITE_MAX_5_DEFAULT;
						}
					}
					break;
				}
				AKB_DEBUG("backlight type 0x00 looks wrong, retrying\n");
				msleep(50);
			} else {
				AKB_ERROR("GET_SPECS returned wrong type, trying FEATURES_1\n");
				status = -EINVAL;
			}
			ACPI_FREE(result);
		} else {
			AKB_DEBUG("GET_SPECS failed, trying FEATURES_1\n");
		}
	}

	/* Fallback detection for older firmware. */
	if (status || acer_kbd_backlight_type == ACER_KBD_BACKLIGHT_NONE) {
		status = acer_kbd_evaluate(ACER_KBD_CMD_GET_BIOS_FEATURES_1,
					   0, &features);
		if (!status) {
			AKB_DEBUG("FEATURES_1: 0x%08x\n", features);
			if (features &
			    ACER_KBD_CMD_GET_BIOS_FEATURES_1_SUB_3_ZONE_RGB_KB) {
				acer_kbd_backlight_type =
					ACER_KBD_BACKLIGHT_3_ZONE_RGB;
			} else if (features &
				   ACER_KBD_CMD_GET_BIOS_FEATURES_1_SUB_WHITE_ONLY_KB) {
				acer_kbd_backlight_type =
					ACER_KBD_BACKLIGHT_FIXED_COLOR;

				status = acer_kbd_evaluate(
					ACER_KBD_CMD_GET_BIOS_FEATURES_2,
					0, &features);
				if (!status) {
					AKB_DEBUG("FEATURES_2: 0x%08x\n", features);
					if (features &
					    ACER_KBD_CMD_GET_BIOS_FEATURES_2_SUB_WHITE_ONLY_KB_MAX_5) {
						acer_kbd_led_cdev.max_brightness =
							ACER_KBD_BRIGHTNESS_WHITE_MAX_5;
						acer_kbd_led_cdev.brightness =
							ACER_KBD_BRIGHTNESS_WHITE_MAX_5_DEFAULT;
					}
				} else {
					AKB_DEBUG("FEATURES_2 failed\n");
				}
			}
		} else {
			AKB_DEBUG("FEATURES_1 failed\n");
		}
	}
	AKB_DEBUG("backlight type: 0x%02x\n", acer_kbd_backlight_type);

	if (acer_kbd_backlight_type == ACER_KBD_BACKLIGHT_FIXED_COLOR)
		acer_kbd_leds_set_brightness(acer_kbd_led_cdev.brightness);
	else
		acer_kbd_leds_set_color(ACER_KBD_COLOR_DEFAULT);

	if (acer_kbd_backlight_type == ACER_KBD_BACKLIGHT_FIXED_COLOR) {
		AKB_DEBUG("registering fixed-color LED\n");
		ret = led_classdev_register(&dev->dev, &acer_kbd_led_cdev);
		if (ret) {
			AKB_ERROR("fixed-color LED registration failed\n");
			return ret;
		}
	} else if (acer_kbd_backlight_type == ACER_KBD_BACKLIGHT_1_ZONE_RGB) {
		acer_kbd_set_keyboard_status(1);
		AKB_DEBUG("registering single-zone RGB LED\n");
		ret = devm_led_classdev_multicolor_register(
			&dev->dev, &acer_kbd_mcled_cdevs[0]);
		if (ret) {
			AKB_ERROR("single-zone RGB LED registration failed\n");
			return ret;
		}
	} else if (acer_kbd_backlight_type == ACER_KBD_BACKLIGHT_3_ZONE_RGB) {
		acer_kbd_set_keyboard_status(1);
		AKB_DEBUG("registering three-zone RGB LEDs\n");
		ret = devm_led_classdev_multicolor_register(
			&dev->dev, &acer_kbd_mcled_cdevs[0]);
		if (ret) {
			AKB_ERROR("zone 0 LED registration failed\n");
			return ret;
		}
		ret = devm_led_classdev_multicolor_register(
			&dev->dev, &acer_kbd_mcled_cdevs[1]);
		if (ret) {
			AKB_ERROR("zone 1 LED registration failed\n");
			devm_led_classdev_multicolor_unregister(
				&dev->dev, &acer_kbd_mcled_cdevs[0]);
			return ret;
		}
		ret = devm_led_classdev_multicolor_register(
			&dev->dev, &acer_kbd_mcled_cdevs[2]);
		if (ret) {
			AKB_ERROR("zone 2 LED registration failed\n");
			devm_led_classdev_multicolor_unregister(
				&dev->dev, &acer_kbd_mcled_cdevs[0]);
			devm_led_classdev_multicolor_unregister(
				&dev->dev, &acer_kbd_mcled_cdevs[1]);
			return ret;
		}
	}

	leds_initialized = true;
	return 0;
}

void acer_kbd_leds_remove(struct platform_device *dev)
{
	if (!leds_initialized)
		return;

	if (acer_kbd_backlight_type == ACER_KBD_BACKLIGHT_FIXED_COLOR)
		led_classdev_unregister(&acer_kbd_led_cdev);
	else if (acer_kbd_backlight_type == ACER_KBD_BACKLIGHT_1_ZONE_RGB)
		devm_led_classdev_multicolor_unregister(
			&dev->dev, &acer_kbd_mcled_cdevs[0]);
	else if (acer_kbd_backlight_type == ACER_KBD_BACKLIGHT_3_ZONE_RGB) {
		devm_led_classdev_multicolor_unregister(
			&dev->dev, &acer_kbd_mcled_cdevs[0]);
		devm_led_classdev_multicolor_unregister(
			&dev->dev, &acer_kbd_mcled_cdevs[1]);
		devm_led_classdev_multicolor_unregister(
			&dev->dev, &acer_kbd_mcled_cdevs[2]);
	}

	leds_initialized = false;
}

void acer_kbd_leds_suspend(void)
{
	switch (acer_kbd_backlight_type) {
	case ACER_KBD_BACKLIGHT_1_ZONE_RGB:
	case ACER_KBD_BACKLIGHT_3_ZONE_RGB:
		acer_kbd_set_keyboard_status(0);
		break;
	default:
		break;
	}
}

void acer_kbd_leds_resume(void)
{
	switch (acer_kbd_backlight_type) {
	case ACER_KBD_BACKLIGHT_1_ZONE_RGB:
	case ACER_KBD_BACKLIGHT_3_ZONE_RGB:
		acer_kbd_set_keyboard_status(1);
		break;
	default:
		break;
	}
}

void acer_kbd_leds_restore_state(void)
{
	if (acer_kbd_backlight_type == ACER_KBD_BACKLIGHT_FIXED_COLOR) {
		acer_kbd_led_cdev.brightness_set(&acer_kbd_led_cdev,
						 acer_kbd_led_cdev.brightness);
	} else if (acer_kbd_backlight_type == ACER_KBD_BACKLIGHT_1_ZONE_RGB) {
		acer_kbd_mcled_cdevs[0].led_cdev.brightness_set(
			&acer_kbd_mcled_cdevs[0].led_cdev,
			acer_kbd_mcled_cdevs[0].led_cdev.brightness);
	} else if (acer_kbd_backlight_type == ACER_KBD_BACKLIGHT_3_ZONE_RGB) {
		int i;

		for (i = 0; i < 3; ++i)
			acer_kbd_mcled_cdevs[i].led_cdev.brightness_set(
				&acer_kbd_mcled_cdevs[i].led_cdev,
				acer_kbd_mcled_cdevs[i].led_cdev.brightness);
	}
}

enum acer_kbd_backlight_type acer_kbd_leds_get_type(void)
{
	return acer_kbd_backlight_type;
}

void acer_kbd_leds_brightness_notify(void)
{
	u32 result;

	if (acer_kbd_backlight_type != ACER_KBD_BACKLIGHT_FIXED_COLOR)
		return;

	if (acer_kbd_evaluate(ACER_KBD_CMD_GET_KB_WHITE_LEDS, 0, &result))
		return;

	AKB_DEBUG("firmware brightness: %u\n", result);
	acer_kbd_led_cdev.brightness = result;
	led_classdev_notify_brightness_hw_changed(&acer_kbd_led_cdev, result);
}

void acer_kbd_leds_set_brightness(u8 brightness)
{
	if (acer_kbd_backlight_type == ACER_KBD_BACKLIGHT_FIXED_COLOR)
		acer_kbd_led_cdev.brightness_set(&acer_kbd_led_cdev, brightness);
	else if (acer_kbd_backlight_type == ACER_KBD_BACKLIGHT_1_ZONE_RGB)
		acer_kbd_mcled_cdevs[0].led_cdev.brightness_set(
			&acer_kbd_mcled_cdevs[0].led_cdev, brightness);
	else if (acer_kbd_backlight_type == ACER_KBD_BACKLIGHT_3_ZONE_RGB) {
		int i;

		for (i = 0; i < 3; ++i)
			acer_kbd_mcled_cdevs[i].led_cdev.brightness_set(
				&acer_kbd_mcled_cdevs[i].led_cdev, brightness);
	}
}

void acer_kbd_leds_set_color(u32 color)
{
	int i, j;

	if (acer_kbd_backlight_type != ACER_KBD_BACKLIGHT_1_ZONE_RGB &&
	    acer_kbd_backlight_type != ACER_KBD_BACKLIGHT_3_ZONE_RGB)
		return;

	for (i = 0; i < 3; ++i) {
		if (acer_kbd_backlight_type == ACER_KBD_BACKLIGHT_1_ZONE_RGB &&
		    i > 0)
			break;
		for (j = 0; j < 3; ++j)
			acer_kbd_mcled_cdevs[i].subled_info[j].intensity =
				(color >> (16 - 8 * j)) & 0xff;
		acer_kbd_mcled_cdevs[i].led_cdev.brightness_set(
			&acer_kbd_mcled_cdevs[i].led_cdev,
			acer_kbd_mcled_cdevs[i].led_cdev.brightness);
	}
}
