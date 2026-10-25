# kbd-rgbd architecture

`kbd-rgbd` is a Rust daemon with no external crates. It renders one
keyboard backlight setting to
`/sys/class/leds/rgb:kbd_backlight/multi_intensity`.

It can render a built in animation, a static color, or off.

Brightness is out of scope. The Fn keys drive the LED brightness node
directly. The daemon only writes color.

## Data flow

```text
kbd-mode <name or hex or off>
        |
        v
kbd-rgbd writes color to multi_intensity
```

## Config

File: `/etc/acer-rgb.conf`

```ini
mode=cycle
```

Accepted values: `rainbow`, `cycle`, `ocean`, `sunset`, `strobe`,
`off`, or a hex color such as `ff0000`. Unknown values are logged and
ignored. Missing or invalid config falls back to `cycle`.

## Command protocol

File: `/run/kbd-rgbd/cmd`

| Command | Effect |
|---|---|
| `set <value>` | Validate, apply, and persist |
| `stop` | Write `0 0 0` and exit |

The daemon reads the file, then truncates it. Writers should hold an
flock across the write.

## Animations

Keyframes are expanded to 80 ms frames with linear interpolation.

| Name | Details |
|---|---|
| `rainbow` | Six colors, 4 seconds each |
| `cycle` | Red, green, blue, 6 seconds each |
| `ocean` | Blue and teal, 6 seconds each |
| `sunset` | Warm tones, 5 seconds each |
| `strobe` | White and black, 100 ms each |
| `off` | Black |
| `RRGGBB` | Static color |

## Ownership

- The driver reports Fn keys.
- The daemon owns color.
- The Fn layer owns brightness.

## Service lifecycle

- Start creates the runtime directory and renders the loop.
- Stop sends `stop`. The daemon writes `0 0 0` and exits.

## Error handling

- Bad config or command: log it and keep the current mode.
- Missing LED device: log and retry.
- Config write failure: log it. The mode still applies in memory.

## Accepted risks

- The command file is world writable so keybinds work without sudo.
- A stuck sysfs write causes the daemon to exit after about 5 minutes.
  systemd restarts it.

## History

Earlier versions used JSON presets and daemon side brightness. Those
were removed. The firmware cannot animate, so the renderer remains.
