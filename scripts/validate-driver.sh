#!/bin/sh
# Validate the acer_kbd_backlight driver + kbd-rgbd stack.
# Requires root privileges (reads sysfs, queries systemd/dkms).
# Exits nonzero on the first failed check.
#
# Usage: sudo ./scripts/validate-driver.sh

set -eu

if [ "$(id -u)" -ne 0 ]; then
    echo "This script must be run as root. Use: sudo $0"
    exit 1
fi

pass=0
check() {
    # check <description> <command...>
    desc="$1"
    shift
    if "$@" > /dev/null 2>&1; then
        echo "PASS: $desc"
        pass=$((pass + 1))
    else
        echo "FAIL: $desc"
        exit 1
    fi
}

check_value() {
    # check_value <description> <expected> <actual>
    if [ "$2" = "$3" ]; then
        echo "PASS: $1 ($3)"
        pass=$((pass + 1))
    else
        echo "FAIL: $1 (expected '$2', got '$3')"
        exit 1
    fi
}

echo "== kernel module =="
lsmod | grep -q "^acer_kbd_backlight " && echo "PASS: acer_kbd_backlight loaded" && pass=$((pass + 1)) \
    || { echo "FAIL: acer_kbd_backlight not loaded"; exit 1; }
if lsmod | grep -Eq "^(tuxedo_keyboard|clevo_wmi|uniwill_wmi|clevo_acpi|tuxedo_io|tuxedo_nb|ite_829)[[:space:]]"; then
    echo "FAIL: stale tuxedo/clevo modules still loaded"
    exit 1
fi
echo "PASS: no stale tuxedo/clevo modules loaded"
pass=$((pass + 1))

echo "== LED device =="
LED=/sys/class/leds/rgb:kbd_backlight
check "LED device exists" test -d "$LED"
check "multi_intensity writable" test -w "$LED/multi_intensity"
MAX=$(cat "$LED/max_brightness")
check_value "max_brightness is 255" "255" "$MAX"

echo "== daemon =="
check "kbd-rgbd active" systemctl is-active --quiet kbd-rgbd.service
check "cmd file exists" test -f /run/kbd-rgbd/cmd
if journalctl -u kbd-rgbd --no-pager --since "2 min ago" 2>/dev/null | grep -qi "error"; then
    echo "FAIL: kbd-rgbd logged errors in the last 2 minutes"
    exit 1
fi
echo "PASS: no kbd-rgbd errors in the last 2 minutes"
pass=$((pass + 1))

echo "== live backlight write =="
# Stop the animator first: it rewrites multi_intensity every frame,
# which would race the roundtrip below. Restart is trap-guarded so a
# failure cannot leave the daemon stopped.
systemctl stop kbd-rgbd.service
trap 'systemctl start kbd-rgbd.service > /dev/null 2>&1 || true' EXIT
printf '255 255 255' > "$LED/multi_intensity"
sleep 1
AFTER_INTENSITY=$(cat "$LED/multi_intensity")
printf '%s' "$AFTER_INTENSITY" | grep -q "255 255 255" \
    && echo "PASS: sysfs write/read roundtrip ($AFTER_INTENSITY)" && pass=$((pass + 1)) \
    || { echo "FAIL: sysfs roundtrip mismatch ($AFTER_INTENSITY)"; exit 1; }

echo "== DKMS registration =="
check "dkms knows acer-kbd-backlight" dkms status -m acer-kbd-backlight -v 1.0.0

echo ""
echo "All $pass checks passed."
echo "Manual checks still required:"
echo "  - Fn brightness keys fire exactly once per press (evtest)"
echo "  - suspend/resume restores backlight and keyboard still types"
echo "  - reboot: module autoloads, backlight works"
