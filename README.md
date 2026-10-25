# acer-rgb

Keyboard backlight control for the Acer Aspire A715-79G. It ships a
kernel driver and a Rust daemon.

## What it does

- Loads `acer_kbd_backlight` at boot through DKMS
- Creates `/sys/class/leds/rgb:kbd_backlight/`
- Runs `kbd-rgbd` to render animations or a static color
- Stores the mode in `/etc/acer-rgb.conf`
- Provides `kbd-mode` and `kbd-toggle`

The Fn keys own brightness. The daemon only changes color.

## Requirements

- Linux with systemd
- Kernel headers, compiler, and DKMS
- Rust toolchain

## Quick start

```sh
sudo ./scripts/install-system.sh
sudo ./scripts/validate-driver.sh
kbd-mode rainbow
```

## Usage

| Command | Description |
|---|---|
| `kbd-mode <name>` | Animate: `rainbow`, `cycle`, `ocean`, `sunset`, `strobe` |
| `kbd-mode next` | Rotate to the next animation |
| `kbd-mode RRGGBB` | Static color, for example `kbd-mode ff0000` |
| `kbd-mode off` | Turn off the backlight |
| `kbd-toggle` | Toggle on and off |
| `kbd-color RRGGBB` | Direct sysfs write. Stop the daemon first. |

The mode persists across reboots. Brightness stays where the Fn keys
put it.

## Animations

| Name | Description |
|---|---|
| `rainbow` | Six color smooth rainbow |
| `cycle` | Red, green, blue |
| `ocean` | Blue and teal tones |
| `sunset` | Orange, red, and purple tones |
| `strobe` | White and black strobe |
| `off` | Backlight off |
| `RRGGBB` | Any static color |

## Files

- `driver/` - kernel module
- `src/` - daemon and animation engine
- `scripts/` - install, uninstall, validation, and control scripts
- `packaging/` - systemd unit, module autoload, udev rule
- `docs/architecture.md` - design notes

## Uninstall

```sh
sudo ./scripts/uninstall.sh
```

## License

GPL-2.0-or-later. The kernel driver is derived from tuxedo-drivers.
See `driver/README.md` for provenance.
