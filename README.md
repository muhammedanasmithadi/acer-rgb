# acer-rgb

Keyboard backlight enablement for Acer Aspire A715-79G (and similar)
using a self-owned kernel driver — no external driver packages required.

## What it does

- Loads the `acer_kbd_backlight` kernel module at boot (DKMS, `driver/`)
- Creates `/sys/class/leds/rgb:kbd_backlight/` LED class device
- Runs `kbd-rgbd` — a minimal Rust daemon that animates RGB colors via sysfs
- Provides 11 preset color profiles and shell scripts to control them
- No D-Bus, no KDE dependencies, no Python

## Architecture

```
┌─────────────────────────────────────────────────┐
│                  kbd-preset-switch               │
│                  kbd-preset-list                 │
│                  kbd-off                         │
│                  kbd-brightness-up/down          │
└───────┬─────────────────────────────┬────────────┘
        │ write /run/kbd-rgbd/cmd      │ write /run/kbd-rgbd/cmd
        ▼                              ▼
┌────────────────┐           ┌──────────────────┐
│   kbd-rgbd     │  reads    │  multi_intensity │
│  (Rust daemon) │◄─────────►│  brightness      │
│                │  writes   │  (LED class)     │
└───┬───┬───┬────┘           └──────────────────┘
    │   │   │
    │   │   └── /etc/tailord/keyboard/*.json
    │   │        (animation definitions)
    │   │
    │   └────── /etc/tailord/profiles/*.json
    │            (profile selectors)
    │
    └────────── /etc/tailord/active_profile.json
                 (symlink — atomically swapped)
```

## Quick start

```bash
# 1. Build and install system-wide
sudo ./scripts/install-system.sh

# 2. Validate the stack
sudo ./scripts/validate-driver.sh

# 3. Use it
kbd-preset-list
kbd-preset-switch
kbd-brightness-up
kbd-brightness-down
kbd-off
```

## Usage

| Command | Description |
|---------|-------------|
| `kbd-brightness-up` | Increase brightness by ~10% |
| `kbd-brightness-down` | Decrease brightness by ~10% |
| `kbd-preset-switch` | Cycle to next preset |
| `kbd-preset-list` | List all presets (active marked with `*`) |
| `kbd-off` | Turn off backlight (daemon stays alive) |

Brightness and animations are independent — brightness scales the LED
class output without affecting the daemon's RGB animation.

### Hyprland keybinds example

Add to `~/.config/hypr/hyprland.conf`:

```
bind = , XF86KbdBrightnessUp, exec, kbd-brightness-up
bind = , XF86KbdBrightnessDown, exec, kbd-brightness-down
bind = , XF86KbdLightOnOff, exec, kbd-off
bind = $mod+KB, KB, exec, kbd-preset-switch
```

## Presets

| Name | Description |
|------|-------------|
| `rainbow` | 6-color smooth rainbow, 4s per transition |
| `cycle` | Red → Green → Blue, 6s each |
| `default` | Red → Green → Blue, 6s each (active at install) |
| `warm-ambient` | Slow warm-tone fades, 15s each |
| `pastel` | Soft pastels, 5s each |
| `ocean` | Blue and teal tones, 6s each |
| `sunset` | Orange, red, purple warm tones, 5s each |
| `snap-cycle` | 6 colors instant snap, 500ms each |
| `strobe` | White/black 100ms strobe |
| `police` | Red/blue alternating, 300ms |
| `off` | Turns backlight off |

## Files

```
├── src/
│   ├── main.rs                 ← entrypoint: calls daemon::run()
│   ├── lib.rs                  ← crate root + re-exports
│   ├── error.rs                ← KbdError enum + From impls
│   ├── types.rs                ← JSON types + parsers + 8 tests
│   ├── animation.rs            ← lerp() + build_frames() + 2 tests
│   └── runtime.rs              ← daemon runtime + 3 tests
├── tests/
│   └── profile_loading.rs      ← 2 integration tests
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
├── presets/keyboard/           ← animation JSON definitions (11)
├── presets/profiles/           ← profile selectors (11)
├── packaging/
│   ├── kbd-rgbd.service        ← systemd service unit
│   ├── modules-load.d/         ← kernel module auto-load
│   └── modprobe.d/             ← module parameters
├── scripts/
│   ├── kbd-brightness-up       ← increase backlight
│   ├── kbd-brightness-down     ← decrease backlight
│   ├── kbd-preset-switch       ← cycle to next preset
│   ├── kbd-preset-list         ← list presets
│   ├── kbd-off                 ← turn off (daemon stays alive)
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

Write to `/run/kbd-rgbd/cmd` (newline-terminated):

| Command | Effect |
|---------|--------|
| `stop` | Write `0 0 0` to sysfs, exit |
| `reload` | Reload current profile from disk |
| `profile <name>` | Switch to profile (atomically updates symlink) |
| `brightness_up` | Increase brightness by ~10% (+26, clamped 0–255) |
| `brightness_down` | Decrease brightness by ~10% (-26, clamped 0–255) |

## Uninstall

```bash
sudo ./scripts/uninstall.sh
```

## License

GPL-2.0-or-later. The kernel driver in `driver/` is derived from
tuxedo-drivers (TUXEDO Computers GmbH, GPL-2.0+); attributions are kept
in the source headers. See `driver/README.md` for the full
provenance, safety case, and migration notes.
