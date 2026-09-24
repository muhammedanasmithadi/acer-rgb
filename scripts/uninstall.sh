#!/bin/sh
# Remove kbd-rgbd system-wide installation
# Requires root privileges

set -eu

if [ "$(id -u)" -ne 0 ]; then
  echo "This script must be run as root. Use: sudo $0"
  exit 1
fi

echo "Stopping and disabling services..."
systemctl disable --now kbd-rgbd.service 2>/dev/null || true

echo "Removing systemd units..."
rm -f /etc/systemd/system/kbd-rgbd.service
rm -f /etc/systemd/system/tailord.service

echo "Removing modprobe config..."
rm -f /etc/modules-load.d/acer-kbd-backlight.conf
rm -f /etc/modprobe.d/acer-kbd-backlight.conf
rm -f /etc/modules-load.d/clevo-wmi.conf
rm -f /etc/modules-load.d/tuxedo_keyboard.conf
rm -f /etc/modprobe.d/tuxedo-keyboard.conf

echo "Removing DKMS driver..."
modprobe -r acer_kbd_backlight 2>/dev/null || true
dkms remove acer-kbd-backlight/1.0.0 --all 2>/dev/null || true
rm -rf /usr/src/acer-kbd-backlight-1.0.0
depmod -a 2>/dev/null || true

echo "Removing config..."
rm -f /etc/acer-rgb.conf
rm -rf /etc/tailord
rm -rf /run/kbd-rgbd

echo "Removing old tailord binary..."
rm -f /usr/bin/tailord

echo "Removing daemon..."
rm -f /usr/local/bin/kbd-rgbd

echo "Removing control scripts..."
for script in kbd-mode kbd-color kbd-brightness-up kbd-brightness-down \
              kbd-preset-switch kbd-preset-list kbd-off kbdctl \
              kbd-brightness kbd-preset; do
  rm -f "/usr/local/bin/$script"
done

systemctl daemon-reload

echo ""
echo "Uninstallation complete."
