# kbd-rgbd architecture

## Overview

kbd-rgbd is a dependency-free Rust daemon that renders one keyboard
backlight setting — a built-in animation, a static color, or off — to
the kernel LED class device
`/sys/class/leds/rgb:kbd_backlight/multi_intensity`.

Zero crates. No D-Bus. No JSON. No presets on disk: the 5 animations
are compiled in (`src/anim.rs`).

Brightness is out of scope by design. The Fn keys drive the LED
`brightness` node directly via brightnessctl, which is the single
source of truth for the level; the daemon only ever writes color.

## Data flow

```
kbd-mode <name|hex|off>          kbd-color RRGGBB (daemon stopped)
        │ write `set …`                   │ write sysfs directly
        ▼                                 ▼
┌──────────────┐   writes   ┌──────────────────┐
│   kbd-rgbd   │ ─────────► │  multi_intensity │
└──────────────┘            │  (LED class)     │
```

## Config

Single file: `/etc/acer-rgb.conf`.

```ini
# animation, static color, or off
mode=cycle
```

Accepted values: `rainbow`, `cycle`, `ocean`, `sunset`, `strobe`,
`off`, or a hex color (`ff0000`). Unknown values are logged and
ignored; a missing or invalid config falls back to `cycle`.

## Cmd file protocol

Regular file at `/run/kbd-rgbd/cmd` (world-writable, mode 666, so user
keybinds work without sudo).

| Command | Effect |
|---------|--------|
| `set <value>` | Validate, apply immediately, persist to `/etc/acer-rgb.conf` |
| `stop` | Write `0 0 0` to sysfs, exit |

The daemon reads the whole file, then truncates it. Invalid values are
logged and ignored; the previous mode keeps rendering.

## Animations

Keyframe lists expanded to 80 ms frames with linear interpolation
(`src/anim.rs::build`), wrapping end-to-start:

| Name | Keys |
|------|------|
| `rainbow` | 6 colors, 4 s each |
| `cycle` | red → green → blue, 6 s each |
| `ocean` | 5 blue/teal keys, 6 s each |
| `sunset` | 5 warm keys, 5 s each |
| `strobe` | white/black snap, 100 ms each |
| `off` | single black frame |
| `RRGGBB` | single static frame, 1 s cadence |

## Shell scripts

Two single-purpose scripts:

| Script | Action |
|--------|--------|
| `kbd-mode` | Sends `set <value>` to the daemon; `next` rotates animations |
| `kbd-toggle` | Toggles on/off via brightnessctl, preserving the level |
| `kbd-color` | Direct sysfs static color; for daemon-free use and debugging |

Ownership rule: the driver only *reports* Fn keys, the daemon owns
color, the Fn layer owns brightness. The mode key (`0x83`) therefore
does not change color in the kernel — it emits `KEY_LIGHTS_TOGGLE`,
and Hyprland binds it to `kbd-mode next`.

## Service lifecycle

```
systemctl start → RuntimeDirectory created → daemon starts → renders loop
systemctl stop  → ExecStop (writes "stop" to cmd) → daemon writes 0 0 0 → exits
```

`TimeoutStopSec=2`. Like its predecessor, a SIGKILL leaves the LEDs at
their last color; the next start re-renders from config.

## Error handling

- **Bad config/cmd value:** log, keep rendering the current mode.
- **LED missing (driver not loaded):** log once per 5 s, keep retrying —
  this is the normal state during early boot before DKMS autoload.
- **Config not persistable:** log, mode still applies in memory.

## History

v0.1.x drove animations from JSON presets under `/etc/tailord` with a
6-command protocol and daemon-side brightness (serde + serde_json,
~1000 lines with tests). The firmware cannot animate on its own, so a
renderer is still needed — but presets, selectors, symlinks, and the
brightness duplication were removed (see git history). Net: ~350 lines,
zero dependencies.
