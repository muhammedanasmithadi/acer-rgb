#!/bin/sh
# Install kbd-rgbd system-wide: daemon, scripts, presets, systemd unit
# Requires root privileges

set -e

if [ "$(id -u)" -ne 0 ]; then
  echo "This script must be run as root. Use: sudo $0"
  exit 1
fi

# Preflight: cargo may not be on root's PATH under sudo, and a
# non-interactive sudo -u shell does not source dotfiles, so probe the
# invoking user's well-known cargo locations directly. Prefer real
# toolchain binaries over the rustup shim (the shim needs a default
# toolchain configured, which root does not have).
if ! command -v cargo > /dev/null 2>&1 && [ -n "${SUDO_USER:-}" ]; then
    SUDO_HOME=$(getent passwd "$SUDO_USER" | cut -d: -f6)
    for cand in "$SUDO_HOME"/.rustup/toolchains/*/bin \
                "$SUDO_HOME/.cargo/bin" \
                "$SUDO_HOME/.local/share/mise/shims"; do
        if [ -x "$cand/cargo" ]; then
            PATH="$cand:$PATH"
            export PATH
            break
        fi
    done
    if [ -d "$SUDO_HOME/.rustup" ]; then
        RUSTUP_HOME="$SUDO_HOME/.rustup"
        export RUSTUP_HOME
    fi
    unset SUDO_HOME
fi
for tool in cargo dkms depmod modprobe; do
    command -v "$tool" > /dev/null || {
        echo "missing required tool: $tool (hint: sudo dnf install dkms)" >&2
        exit 1
    }
done
if [ ! -d "/lib/modules/$(uname -r)/build" ]; then
    echo "kernel headers missing for $(uname -r): sudo dnf install kernel-devel" >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
echo "Installing from: $SCRIPT_DIR"

# Build the Rust binary
echo "  building kbd-rgbd..."
(cd "$SCRIPT_DIR" && cargo build --release)

# Install daemon binary
echo "  /usr/local/bin/kbd-rgbd"
install -m755 "$SCRIPT_DIR"/target/release/kbd-rgbd /usr/local/bin/kbd-rgbd

# Install control scripts
echo "  /usr/local/bin/kbd-*"
for script in kbd-brightness-up kbd-brightness-down kbd-preset-switch kbd-preset-list kbd-off; do
  install -m755 "$SCRIPT_DIR/scripts/$script" "/usr/local/bin/$script"
done

# Remove old kbdctl
rm -f /usr/local/bin/kbdctl /usr/local/bin/kbd-brightness /usr/local/bin/kbd-preset

# Copy presets
echo "  presets/  -> /etc/tailord/"
install -d -m755 /etc/tailord/keyboard /etc/tailord/profiles
cp -a "$SCRIPT_DIR/presets/keyboard/"*.json /etc/tailord/keyboard/
install -m644 "$SCRIPT_DIR/presets/profiles/"*.json /etc/tailord/profiles/

# Copy modprobe configs
echo "  modprobe configs"
install -d -m755 /etc/modules-load.d /etc/modprobe.d
install -m644 "$SCRIPT_DIR/packaging/modules-load.d/acer-kbd-backlight.conf" /etc/modules-load.d/acer-kbd-backlight.conf
install -m644 "$SCRIPT_DIR/packaging/modprobe.d/acer-kbd-backlight.conf" /etc/modprobe.d/acer-kbd-backlight.conf
# Remove superseded tuxedo configs, if any
rm -f /etc/modules-load.d/clevo-wmi.conf /etc/modules-load.d/tuxedo_keyboard.conf
rm -f /etc/modprobe.d/tuxedo-keyboard.conf

# Install the kernel driver via DKMS
echo "  acer_kbd_backlight DKMS driver"
for tool in dkms depmod modprobe; do
    command -v "$tool" > /dev/null || { echo "missing required tool: $tool" >&2; exit 1; }
done
DKMS_SRC=/usr/src/acer-kbd-backlight-1.0.0
rm -rf "$DKMS_SRC"
cp -r "$SCRIPT_DIR/driver" "$DKMS_SRC"
dkms remove acer-kbd-backlight/1.0.0 --all 2>/dev/null || true
dkms add "$DKMS_SRC"
dkms install acer-kbd-backlight/1.0.0
depmod -a
modprobe acer_kbd_backlight

# Install systemd unit
echo "  kbd-rgbd.service"
install -m644 "$SCRIPT_DIR/packaging/kbd-rgbd.service" /etc/systemd/system/kbd-rgbd.service

# Ensure active symlink exists (default to cycle if not set)
if [ ! -L /etc/tailord/active_profile.json ]; then
  ln -sf /etc/tailord/profiles/cycle.json /etc/tailord/active_profile.json
fi

# Remove old tailord service, enable new one
systemctl daemon-reload
systemctl disable --now tailord.service 2>/dev/null || true
rm -f /etc/systemd/system/tailord.service
systemctl enable --now kbd-rgbd.service 2>/dev/null || true

echo ""
echo "Installation complete."
echo "  - acer_kbd_backlight driver installed via DKMS and loaded"
echo "  - kbd-rgbd.service is enabled and started"
echo "  - kbd-brightness-up, kbd-brightness-down, kbd-preset-switch,"
echo "    kbd-preset-list, kbd-off available in /usr/local/bin/"
echo ""
echo "Verify:"
echo "  ls /sys/class/leds/ | grep kbd_backlight"
echo "  sudo ./scripts/validate-driver.sh"
