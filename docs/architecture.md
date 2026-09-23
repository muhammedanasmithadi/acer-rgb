# kbd-rgbd architecture

## Overview

kbd-rgbd is a single-threaded Rust daemon that reads JSON color profiles,
pre-computes animation frames, and writes RGB values to a kernel LED class
device. Runtime control via a regular file at `/run/kbd-rgbd/cmd`.

Two Cargo deps: `serde` + `serde_json`. No D-Bus. No KDE.

## Data flow

```
acer_kbd_backlight (kernel module, driver/)
       ↓
/sys/class/leds/rgb:kbd_backlight/multi_intensity
       ↓
kbd-rgbd (Rust daemon — reads JSON, writes RGB, cmd file IPC)
       ↓
kbd-brightness-up  kbd-brightness-down  kbd-preset-switch  kbd-off
```

## JSON types

### Keyboard animation (`/etc/tailord/keyboard/<name>.json`)

Serde externally-tagged enum:

```json
{ "Single": { "r": 255, "g": 100, "b": 0 } }
```

```json
{
  "Multiple": [
    { "color": { "r": 255, "g": 0, "b": 0 },
      "transition": "Linear", "transition_time": 4000 },
    { "color": { "r": 0, "g": 255, "b": 0 },
      "transition": "Linear", "transition_time": 4000 }
  ]
}
```

```json
"None"
```

`Transition` is a typed enum (`Linear` / `None`). Absent field defaults to
Linear behavior.

### Profile selector (`/etc/tailord/profiles/<name>.json`)

```json
{ "leds": [{
    "device_name": "platform:acer_kbd_backlight",
    "function": "kbd_backlight",
    "profile": "<keyboard_profile_name>",
    "mode": "Rgb"
}]}
```

(`device_name`/`function`/`mode` are informational only; the daemon
keys off `profile`.)

`fans` and `performance_profile` removed from original tailord format.

### Active profile

Symlink: `/etc/tailord/active_profile.json → profiles/<name>.json`

Updated atomically via `.tmp` + `rename()`.

## Frame computation

| Profile type | Transition | Behavior |
|-------------|-----------|----------|
| `None` | — | Write `0 0 0`, sleep 1000ms |
| `Single` | — | Write fixed color, sleep 1000ms |
| `Multiple` | `Linear` | Interpolate `from→to` over N steps at 80ms each |
| `Multiple` | `None` | Snap to target color, sleep `transition_time` ms |

Interpolation: `lerp(a, b, t) = a + (b - a) * t`, per channel, clamped 0–255.
Uses `0..=steps` (inclusive) to ensure `t=1.0` is reached for all transitions,
including short ones where `steps=1`. Each segment produces `steps+1` frames;
the endpoint duplicate at each boundary (~80ms at 12.5FPS) is visually
negligible.

## Cmd file protocol

Regular file at `/run/kbd-rgbd/cmd` (world-writable, mode 666).

| Command | Effect |
|---------|--------|
| `stop` | Write `0 0 0` to sysfs, exit |
| `reload` | Re-read active profile |
| `profile <name>` | Switch to preset |
| `brightness_up` | +26 brightness (clamped 0–255) |
| `brightness_down` | -26 brightness (clamped 0–255) |

Daemon reads with `OpenOptions::new().read(true).write(true).create(true)`,
truncates with `set_len(0)` after each read.

## Shell scripts

5 single-purpose scripts, no argument parsing:

| Script | Action |
|--------|--------|
| `kbd-brightness-up` | Writes `brightness_up` to cmd file |
| `kbd-brightness-down` | Writes `brightness_down` to cmd file |
| `kbd-preset-switch` | Cycles to next profile (sorted, wraps) |
| `kbd-preset-list` | Lists profiles with `* (active)` marker |
| `kbd-off` | Writes `profile off` to cmd file (daemon stays alive) |

## Service lifecycle

```
systemctl start  → RuntimeDirectory created → daemon starts → loops forever
systemctl stop   → ExecStop (writes "stop" to cmd) → daemon writes 0 0 0 → exits
systemctl kill   → SIGTERM → daemon dies (LEDs stay at last color)
```

`RuntimeDirectory=kbd-rgbd` (systemd v240+) creates `/run/kbd-rgbd` before
`ExecStart`. Daemon creates the cmd file and sets 0666 permissions on startup
as a fallback. `ExecStop` runs before SIGTERM. `TimeoutStopSec=2`.

## Error handling

- **LED missing at start:** retry 5s, check `stop` during retry
- **Profile load fail:** log, keep last valid state; startup retries
  every 5s until a valid active profile resolves
- **Sysfs write fail:** retry 5× at 1s, reload profile on persistent failure
- **Selector hardening:** `profile` names from both the cmd file and
  selector JSONs are validated (`is_valid_profile_name`); the active
  symlink is confined to `/etc/tailord/profiles`; symlink swaps are
  staged through a `.tmp` link and errors abort the switch
- **Brightness read fail:** fall back to `AtomicU32` in-memory last-known value
- **Path traversal:** `is_valid_profile_name()` rejects non-alphanumeric chars
  (except `-`, `_`), max 64 chars

## Key differences from tailord

| Feature | tailord | kbd-rgbd |
|---------|---------|----------|
| Dependencies | ~100 crates | 2 (serde + serde_json) |
| IPC | D-Bus | Regular file |
| Brightness | PowerDevil D-Bus | Daemon writes sysfs directly |
| Profile switching | tailord D-Bus method | `profile <name>` cmd |
| Binary size | ~4MB+ | ~356KB stripped |
| Runtime deps | D-Bus, KDE, Python | libc only |
| Error type | — | `KbdError` with `From<io::Error>` + `From<serde_json::Error>` |
