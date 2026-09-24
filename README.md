# acer-rgb

Keyboard backlight enablement for Acer Aspire A715-79G (and similar)
using a self-owned kernel driver — no external driver packages required.

## What it does

- Loads the `acer_kbd_backlight` kernel module at boot (DKMS, `driver/`)
- Creates `/sys/class/leds/rgb:kbd_backlight/` LED class device
- Runs `kbd-rgbd` — a dependency-free Rust daemon that renders one of
  5 built-in animations (or a static color) via sysfs
- One config file (`/etc/acer-rgb.conf`), one control script (`kbd-mode`)
- No D-Bus, no KDE dependencies, no Python, no JSON, no presets

Brightness is owned by the Fn keys, which drive the LED `brightness`
node directly via brightnessctl — the daemon never touches it.

## Architecture

```
kbd-mode <rainbow|cycle|ocean|sunset|strobe|off|RRGGBB>
        │ write `set …` to /run/kbd-rgbd/cmd (persists to /etc/acer-rgb.conf)
        ▼
┌──────────────┐   writes   ┌──────────────────┐
│   kbd-rgbd   │ ─────────► │  multi_intensity │
│ (Rust daemon)│            │  (LED class)     │
└──────────────┘            └──────────────────┘
```

## Quick start

```bash
# 1. Build and install system-wide
sudo ./scripts/install-system.sh

# 2. Validate the stack
sudo ./scripts/validate-driver.sh

# 3. Use it
kbd-mode rainbow
kbd-mode ff0000
kbd-mode off
```

## Usage

| Command | Description |
|---------|-------------|
| `kbd-mode <name>` | Animate: `rainbow`, `cycle`, `ocean`, `sunset`, `strobe` |
| `kbd-mode RRGGBB` | Static color, e.g. `kbd-mode ff0000` |
| `kbd-mode off` | Turn off backlight (daemon stays alive) |
| `kbd-color RRGGBB` | Low-level direct sysfs write (stop the daemon first) |

The mode persists in `/etc/acer-rgb.conf` and survives reboots.
Brightness stays where the Fn keys put it — animations only drive color.

### Hyprland keybinds example

The Fn brightness keys are handled outside this repo (brightnessctl
directly on the LED device, e.g. `~/.config/hypr/scripts/kbd-brightness.sh`).
For mode switching, bind `kbd-mode`:

```
bind = $mod+KB, KB, exec, kbd-mode cycle
```

## Animations

| Name | Description |
|------|-------------|
| `rainbow` | 6-color smooth rainbow, 4s per transition |
| `cycle` | Red → Green → Blue, 6s each (default) |
| `ocean` | Blue and teal tones, 6s each |
| `sunset` | Orange, red, purple warm tones, 5s each |
| `strobe` | White/black 100ms strobe |
| `off` | Turns backlight off |
| `RRGGBB` | Any static color, e.g. `ff0000` |

## Files

```
├── src/
│   ├── main.rs                 ← daemon: config, cmd file, sysfs loop + 2 tests
│   └── anim.rs                 ← keyframe engine + 5 built-ins + 6 tests
├── Cargo.toml                  ← zero dependencies
├── .github/workflows/ci.yml     ← CI: fmt, clippy, test, shellcheck, build
├── Cargo.toml
├── driver/                     ← acer_kbd_backlight kernel module (DKMS)
│   ├── acer_kbd.h              ← GUIDs, commands, shared structs
│   ├── acer_kbd_core.c         ← init, Acer DMI gate, platform/input
│   ├── acer_kbd_wmi.c          ← WMI transport + probe
│   ├── acer_kbd_clevo.c        ← backlight modes, keymap, events
│   ├── acer_kbd_led.c          ← LED devices, detection, suspend/resume
│   ├── Kbuild + Makefile       ← kbuild files
│   ├── dkms.conf               ← DKMS package definition
│   └── README.md               ← driver design + safety case
├── packaging/
│   ├── kbd-rgbd.service        ← systemd service unit
│   ├── modules-load.d/         ← kernel module auto-load
│   └── modprobe.d/             ← module parameters
├── scripts/
│   ├── kbd-mode                ← set mode via daemon (applies + persists)
│   ├── kbd-color               ← direct sysfs static color (daemon stopped)
│   ├── install-system.sh       ← system installation (incl. DKMS driver)
│   ├── uninstall.sh            ← system removal (incl. DKMS driver)
│   └── validate-driver.sh      ← driver/stack health checks (needs root)
└── docs/
    └── architecture.md         ← configuration reference
```

## Requirements

- Linux with systemd v240+ (for `RuntimeDirectory=` support)
- Kernel headers + compiler + DKMS (`sudo dnf install kernel-devel dkms gcc make`)
- Rust toolchain (for building `kbd-rgbd`)
- DKMS `AUTOINSTALL` rebuilds the driver on kernel updates

## Daemon commands

Write to `/run/kbd-rgbd/cmd` (newline-terminated, world-writable by design
so user keybinds work without sudo):

| Command | Effect |
|---------|--------|
| `set <mode\|hex\|off>` | Apply immediately and persist to `/etc/acer-rgb.conf` |
| `stop` | Write `0 0 0` to sysfs, exit (used by the systemd unit) |

## Uninstall

```bash
sudo ./scripts/uninstall.sh
```

## License

GPL-2.0-or-later. The kernel driver in `driver/` is derived from
tuxedo-drivers (TUXEDO Computers GmbH, GPL-2.0+); attributions are kept
in the source headers. See `driver/README.md` for the full
provenance, safety case, and migration notes.
