# acer-kbd-backlight — self-owned keyboard backlight driver

Single-module kernel driver (`acer_kbd_backlight.ko`) for the keyboard
backlight on Acer laptops exposing the `ABBC0F6x` WMI GUIDs. Verified on
the Acer Aspire A715-79G (BIOS 1.07.01TACI). It replaces the
`clevo_wmi` + `tuxedo_keyboard` pair from `tuxedo-drivers`, which is no
longer required.

## Why a fork

Mainline `acer-wmi` binds only to GUIDs `676AA15E…`, `6AF4F258…`,
`67C3371D…`. This platform exposes `ABBC0F6B/6C/6D`, so `acer-wmi`
never loads. The TUXEDO `clevo_wmi` transport happens to speak the same
firmware protocol (verified live: probe passes, colors animate), but
pulling in all of `tuxedo-drivers` means:

- a DMI whitelist that rejects non-TUXEDO hardware (needed patching),
- battery charge-control code that performs a probe-time EC **write**
  to detect its feature — unsafe on foreign firmware,
- fan / webcam / touchpad / flightmode / performance-profile commands
  for hardware this machine does not have,
- Uniwill / Clevo-ACPI transports that can never bind here.

This fork keeps the keyboard-backlight path and deletes the rest.

## File map (upstream  tuxedo-drivers 4.22.3  →  here)

| Upstream | Here | Kept / changed |
|---|---|---|
| `clevo_wmi.c` (182) | `acer_kbd_wmi.c` | Transport only; package-buffer call dropped with charge control |
| `tuxedo_keyboard.c` (217) | `acer_kbd_core.c` | Init/gate/platform/input; Uniwill include, Fn-lock plumbing dropped |
| `tuxedo_keyboard_common.h` (113) | `acer_kbd.h` | Structs, color table (moved to `acer_kbd_clevo.c`), sparse helper inlined |
| `clevo_keyboard.h` (1139) | `acer_kbd_clevo.c` | Command layer, keymap, modes; flexicharger (~460 lines), Fn-lock, CC-combo, Clevo-only workarounds dropped |
| `clevo_leds.h` (554) | `acer_kbd_led.c` | LED devices, detection, suspend/resume; dead code, TODOs, quirks dropped |
| `clevo_interfaces.h` (103) | `acer_kbd.h` | GUIDs + keyboard-scoped commands only |
| `tuxedo_compatibility_check/` | — | Replaced by a built-in Acer allowlist + `force` param |
| `uniwill_*`, `clevo_acpi.c`, `ite_8291*`, `tuxedo_nb*`, `tuxedo_io`, … | — | Not needed; never loaded on this machine |

Net: ~2500 lines upstream → ~1300 here, one module, zero
inter-module symbols.

## Safety case

1. **No direct hardware I/O.** The only kernel interfaces used are
   `wmi_evaluate_method()` (firmware-mediated), the LED class,
   the input subsystem, and a platform device. Verified by grep:
   no `inb/outb`, no `ioremap`, no EC access.
2. **Keyboard-scoped WMI allowlist.** The driver issues exactly:
   `0x52` (probe/features), `0x01` (event poll), `0x46`
   (events enable), `0x0D` (specs), `0x7A` (features 2),
   `0x27` (white brightness), `0x67` (RGB set), `0x3D`
   (white brightness query). Fan (`0x68/0x69`), battery,
   and performance-profile writes are gone.
3. **Scoped DMI gate.** Loads only on allowlisted Acer systems
   (`force=1` overrides for testing). It cannot hijack genuine
   TUXEDO/Clevo hardware.
4. **License.** Upstream is GPL-2.0+; attribution headers are kept,
   so this fork complies.
5. **Secure Boot.** Currently disabled on this machine; DKMS builds
   load without signing. If Secure Boot is ever enabled, enroll a
   MOK and sign — same workflow as `acer-ec`.

## Userspace contract (unchanged from tuxedo_keyboard)

- LED device: `/sys/class/leds/rgb:kbd_backlight/` (`multi_intensity`,
  `brightness`, max 255). Name kept identical on purpose.
- Input device: `Acer Kbd Backlight` (Fn keys → `KEY_KBDILLUM_*`).
- Module params: `kbd_backlight_mode` (0–7, default from
  `/etc/modprobe.d/acer-kbd-backlight.conf`), `force` (bool).
- Mode sysfs `kbd_backlight_mode` appears only on 3-zone firmware.

## Build / install (maintainer)

```sh
# compile check against running kernel (no root needed)
cd driver && make

# install as DKMS (root): see ../scripts/install-system.sh
sudo cp -r driver /usr/src/acer-kbd-backlight-1.0.0
sudo dkms add acer-kbd-backlight/1.0.0
sudo dkms install acer-kbd-backlight/1.0.0
sudo modprobe acer_kbd_backlight
```

## Migrating off tuxedo-drivers (root, one time)

```sh
sudo dnf remove -y tuxedo-drivers
sudo rm -rf /usr/src/tuxedo-drivers-* /var/lib/dkms/tuxedo-drivers
sudo rm -f /etc/modules-load.d/clevo-wmi.conf \
            /etc/modules-load.d/tuxedo_keyboard.conf \
            /etc/modprobe.d/tuxedo-keyboard.conf \
            /etc/modprobe.d/disable-flexicharger.conf
# then run scripts/install-system.sh from this repo
```

## Validation checklist (after any driver change)

- [ ] `sudo modprobe acer_kbd_backlight` succeeds, no other
      `*tuxedo*`/`*clevo*` modules loaded
- [ ] `dmesg` shows `WMI interface initialized`
- [ ] `/sys/class/leds/rgb:kbd_backlight/` exists
- [ ] `kbd-rgbd` animates; `kbd-preset-switch`, `kbd-off`,
      `kbd-brightness-up/down` all work
- [ ] Fn brightness keys fire exactly once per press
      (`evtest` on the `Acer Kbd Backlight` device)
- [ ] Suspend/resume: backlight state restores, keyboard types
- [ ] Reboot: module autoloads, backlight works, no journal errors
- [ ] `scripts/validate-driver.sh` passes (automates most of the above)

## Maintenance notes

- `AUTOINSTALL=yes`: kernel updates rebuild automatically from
  `/usr/src/acer-kbd-backlight-1.0.0`. Re-copy on driver updates.
- Kernel API used (`wmi_driver`, `led_class_multicolor`,
  `sparse_keymap`, `platform_create_bundle`) is stable; minimum
  target is 6.11 (void `remove` callback). Re-check on major
  kernel releases by rebuilding.
- `force=1` exists for bringing up other Acer models; promote a
  model to the allowlist only after the full checklist passes on it.
