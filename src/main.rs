//! kbd-rgbd: minimal keyboard backlight animator.
//!
//! Reads one setting and renders it to
//! `/sys/class/leds/rgb:kbd_backlight/multi_intensity` in a loop:
//!
//! - `/etc/acer-rgb.conf` with `mode=<name>` selects a built-in
//!   animation (`rainbow`, `cycle`, `ocean`, `sunset`, `strobe`),
//!   `mode=off` turns the LEDs off, `mode=rrggbb` shows a static color.
//! - `/run/kbd-rgbd/cmd` accepts two commands: `set <mode|hex|off>`
//!   (applies immediately and persists to the config) and `stop`
//!   (turns LEDs off and exits, used by the systemd unit).
//!
//! Brightness is deliberately untouched: the Fn keys drive the LED
//! `brightness` node directly via brightnessctl, which is the single
//! source of truth for the level.

mod anim;

use anim::{build, builtin, Frame, Key};

use std::fs;
use std::io::{self, Read};
use std::os::raw::c_int;
use std::os::unix::fs::PermissionsExt;
use std::os::unix::io::AsRawFd;
use std::thread::sleep;
use std::time::Duration;

const CONFIG_PATH: &str = "/etc/acer-rgb.conf";
const CMD_PATH: &str = "/run/kbd-rgbd/cmd";
const LED_PATH: &str = "/sys/class/leds/rgb:kbd_backlight/multi_intensity";
const DEFAULT_MODE: &str = "cycle";
/// Give up after this many consecutive failed writes (x 5 s each).
const MAX_CONSECUTIVE_FAILURES: u32 = 60;

extern "C" {
    fn flock(fd: c_int, operation: c_int) -> c_int;
}

const LOCK_EX: c_int = 2;
const LOCK_UN: c_int = 8;

/// RAII exclusive `flock(2)` on a raw fd. Links libc directly, so no
/// new crate is needed for file locking.
struct FileLock {
    fd: c_int,
}

impl FileLock {
    /// Acquire an exclusive lock, retrying on EINTR.
    ///
    /// # Safety
    /// `fd` must be a valid open file descriptor; constants match Linux.
    fn exclusive(fd: c_int) -> io::Result<Self> {
        loop {
            // SAFETY: fd is open (caller-held `File` outlives the guard).
            let rc = unsafe { flock(fd, LOCK_EX) };
            if rc == 0 {
                return Ok(Self { fd });
            }
            let err = io::Error::last_os_error();
            if err.kind() != io::ErrorKind::Interrupted {
                return Err(err);
            }
        }
    }
}

impl Drop for FileLock {
    fn drop(&mut self) {
        // SAFETY: same fd as acquired; errors on unlock are not actionable.
        unsafe {
            flock(self.fd, LOCK_UN);
        }
    }
}

fn parse_hex(s: &str) -> Option<(u8, u8, u8)> {
    if s.len() != 6 || !s.chars().all(|c| c.is_ascii_hexdigit()) {
        return None;
    }
    let r = u8::from_str_radix(&s[0..2], 16).ok()?;
    let g = u8::from_str_radix(&s[2..4], 16).ok()?;
    let b = u8::from_str_radix(&s[4..6], 16).ok()?;
    Some((r, g, b))
}

/// Resolve a mode argument (`off`, hex color, or animation name) to frames.
fn frames_for(arg: &str) -> Option<Vec<Frame>> {
    if arg == "off" {
        return Some(build(&[Key {
            r: 0,
            g: 0,
            b: 0,
            ms: 1000,
            snap: true,
        }]));
    }
    if let Some((r, g, b)) = parse_hex(arg) {
        return Some(vec![Frame {
            r,
            g,
            b,
            duration_ms: 1000,
        }]);
    }
    builtin(arg)
}

fn load_config() -> Option<Vec<Frame>> {
    let text = fs::read_to_string(CONFIG_PATH).ok()?;
    for line in text.lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        if let Some(value) = line.strip_prefix("mode=") {
            return frames_for(value.trim());
        }
    }
    None
}

fn persist_config(arg: &str) {
    if let Err(e) = fs::write(CONFIG_PATH, format!("mode={arg}\n")) {
        eprintln!("cannot persist config: {e}");
    }
}

fn read_command() -> Option<String> {
    let mut f = fs::OpenOptions::new()
        .read(true)
        .write(true)
        .create(true)
        .truncate(false)
        .open(CMD_PATH)
        .ok()?;
    // Serialize against writers (they hold the same lock): a command
    // landing between our read and truncate is no longer lost.
    let _guard = FileLock::exclusive(f.as_raw_fd()).ok()?;
    let mut content = String::new();
    f.read_to_string(&mut content).ok()?;
    let _ = f.set_len(0);
    let trimmed = content.trim().to_string();
    if trimmed.is_empty() {
        return None;
    }
    Some(trimmed)
}

fn write_sysfs(frame: &Frame) -> io::Result<()> {
    fs::write(LED_PATH, format!("{} {} {}\n", frame.r, frame.g, frame.b))
}

fn run() {
    eprintln!("kbd-rgbd starting (pid {})", std::process::id());

    let _ = fs::create_dir_all("/run/kbd-rgbd");
    if fs::OpenOptions::new()
        .create(true)
        .write(true)
        .truncate(false)
        .open(CMD_PATH)
        .is_ok()
    {
        let _ = fs::set_permissions(CMD_PATH, fs::Permissions::from_mode(0o666));
    }

    let mut frames = load_config().unwrap_or_else(|| {
        eprintln!("no valid config, defaulting to {DEFAULT_MODE}");
        builtin(DEFAULT_MODE).expect("built-in default must resolve")
    });
    let mut idx = 0usize;
    let mut failures = 0u32;

    loop {
        if let Some(cmd) = read_command() {
            if cmd == "stop" {
                let _ = write_sysfs(&Frame {
                    r: 0,
                    g: 0,
                    b: 0,
                    duration_ms: 0,
                });
                break;
            }
            if let Some(arg) = cmd.strip_prefix("set ") {
                let arg = arg.trim();
                match frames_for(arg) {
                    Some(f) => {
                        eprintln!("mode -> {arg} ({} frames)", f.len());
                        persist_config(arg);
                        frames = f;
                        idx = 0;
                    }
                    None => eprintln!("invalid mode: '{arg}'"),
                }
            }
        }

        let frame = &frames[idx];
        if let Err(e) = write_sysfs(frame) {
            failures += 1;
            eprintln!("write error: {e} ({failures}/{MAX_CONSECUTIVE_FAILURES})");
            if failures >= MAX_CONSECUTIVE_FAILURES {
                // LED gone for ~5 minutes: exit so systemd restarts us
                // instead of logging forever. Clean `stop` above still
                // exits 0 and does not trip the restart.
                eprintln!("LED device missing, exiting for restart");
                std::process::exit(1);
            }
            sleep(Duration::from_secs(5));
            continue;
        }
        failures = 0;

        idx = (idx + 1) % frames.len();
        sleep(Duration::from_millis(frame.duration_ms));
    }

    eprintln!("kbd-rgbd stopped");
}

fn main() {
    run();
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_file_lock_excludes_second_taker() {
        let dir = std::env::temp_dir().join("kbd-rgbd-test-lock");
        let _ = fs::create_dir_all(&dir);
        let path = dir.join("lock");
        let f = fs::OpenOptions::new()
            .read(true)
            .write(true)
            .create(true)
            .truncate(false)
            .open(&path)
            .unwrap();
        let _guard = FileLock::exclusive(f.as_raw_fd()).unwrap();
        // A second open must observe the lock (nonblocking take fails).
        let f2 = fs::OpenOptions::new()
            .read(true)
            .write(true)
            .truncate(false)
            .open(&path)
            .unwrap();
        const LOCK_NB: c_int = 4;
        let denied = unsafe { flock(f2.as_raw_fd(), LOCK_EX | LOCK_NB) };
        assert_eq!(denied, -1);
        drop(_guard);
        let acquired = unsafe { flock(f2.as_raw_fd(), LOCK_EX | LOCK_NB) };
        assert_eq!(acquired, 0);
        unsafe {
            flock(f2.as_raw_fd(), LOCK_UN);
        }
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn test_parse_hex() {
        assert_eq!(parse_hex("ff0000"), Some((255, 0, 0)));
        assert_eq!(parse_hex("00FF80"), Some((0, 255, 128)));
        assert_eq!(parse_hex(""), None);
        assert_eq!(parse_hex("fff"), None);
        assert_eq!(parse_hex("gggggg"), None);
        assert_eq!(parse_hex("ff00000"), None);
    }

    #[test]
    fn test_frames_for() {
        assert_eq!(frames_for("off").unwrap().len(), 1);
        let f = frames_for("00ff00").unwrap();
        assert_eq!((f[0].r, f[0].g, f[0].b), (0, 255, 0));
        assert!(frames_for("cycle").unwrap().len() > 10);
        assert!(frames_for("nope").is_none());
        for name in ["rainbow", "cycle", "ocean", "sunset", "strobe"] {
            assert!(frames_for(name).is_some());
        }
    }
}
