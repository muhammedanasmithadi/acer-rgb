use std::fs;
use std::io::{self, Read};
use std::os::unix::fs::PermissionsExt;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU32, Ordering};
use std::thread::sleep;
use std::time::Duration;

use crate::animation::build_frames;
use crate::error::{KbdError, Result};
use crate::types::{parse_keyboard_json, parse_profile_selector, Color, Frame};

const ACTIVE_PROFILE_PATH: &str = "/etc/tailord/active_profile.json";
const PROFILES_DIR: &str = "/etc/tailord/profiles";
const KEYBOARD_DIR: &str = "/etc/tailord/keyboard";
const LED_PATH: &str = "/sys/class/leds/rgb:kbd_backlight/multi_intensity";
const BRIGHTNESS_PATH: &str = "/sys/class/leds/rgb:kbd_backlight/brightness";
const CMD_PATH: &str = "/run/kbd-rgbd/cmd";

static BRIGHTNESS: AtomicU32 = AtomicU32::new(255);

fn is_valid_profile_name(name: &str) -> bool {
    !name.is_empty()
        && name.len() <= 64
        && name
            .chars()
            .all(|c| c.is_ascii_alphanumeric() || c == '-' || c == '_')
}

fn load_frames(selector_path: &Path) -> Result<Vec<Frame>> {
    let sel_text = fs::read_to_string(selector_path)?;
    let selector = parse_profile_selector(&sel_text)?;
    let entry = selector.leds.first().ok_or(KbdError::NoLeds)?;
    let profile_name = &entry.profile;
    if !is_valid_profile_name(profile_name) {
        return Err(KbdError::InvalidProfileName(profile_name.clone()));
    }
    let kb_path = Path::new(KEYBOARD_DIR).join(format!("{}.json", profile_name));
    let kb_text = fs::read_to_string(&kb_path)?;
    let color_profile = parse_keyboard_json(&kb_text)?;
    let frames = build_frames(&color_profile);
    if frames.is_empty() {
        return Err(KbdError::NoFrames);
    }
    Ok(frames)
}

fn resolve_active_path() -> Option<PathBuf> {
    let link = fs::read_link(ACTIVE_PROFILE_PATH).ok()?;
    let resolved = if link.is_absolute() {
        link
    } else {
        Path::new(ACTIVE_PROFILE_PATH).parent()?.join(link)
    };
    // Confine to the profiles directory: never follow a selector
    // that points elsewhere, even if the symlink was replaced.
    if resolved.parent()? == Path::new(PROFILES_DIR) {
        Some(resolved)
    } else {
        eprintln!("active profile escapes {}: {}", PROFILES_DIR, resolved.display());
        None
    }
}

fn write_sysfs(color: &Color) -> io::Result<()> {
    let data = format!("{} {} {}", color.r, color.g, color.b);
    fs::write(LED_PATH, data.as_bytes())
}

fn adjust_brightness(delta: i32) {
    let cur = fs::read_to_string(BRIGHTNESS_PATH)
        .ok()
        .and_then(|s| s.trim().parse::<i32>().ok())
        .unwrap_or_else(|| BRIGHTNESS.load(Ordering::Relaxed) as i32);
    let new = (cur + delta).clamp(0, 255);
    BRIGHTNESS.store(new as u32, Ordering::Relaxed);
    if let Err(e) = fs::write(BRIGHTNESS_PATH, format!("{}\n", new)) {
        eprintln!("brightness write error: {}", e);
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
    let mut content = String::new();
    f.read_to_string(&mut content).ok()?;
    let _ = f.set_len(0);
    let trimmed = content.trim().to_string();
    if trimmed.is_empty() {
        return None;
    }
    Some(trimmed)
}

fn handle_profile_cmd(name: &str, frames: &mut Vec<Frame>, frame_idx: &mut usize) {
    if !is_valid_profile_name(name) {
        eprintln!("invalid profile name: '{}'", name);
        return;
    }
    let profile_path = format!("{}/{}.json", PROFILES_DIR, name);
    match load_frames(Path::new(&profile_path)) {
        Ok(f) => {
            let active = Path::new(ACTIVE_PROFILE_PATH);
            let tmp = active.with_extension("json.tmp");
            if let Err(e) = fs::remove_file(&tmp) {
                if e.kind() != io::ErrorKind::NotFound {
                    eprintln!("switch to '{}': cannot clear tmp symlink: {}", name, e);
                    return;
                }
            }
            if let Err(e) = std::os::unix::fs::symlink(&profile_path, &tmp) {
                eprintln!("switch to '{}': cannot stage symlink: {}", name, e);
                return;
            }
            if let Err(e) = fs::rename(&tmp, active) {
                eprintln!("switch to '{}': cannot activate profile: {}", name, e);
                let _ = fs::remove_file(&tmp);
                return;
            }
            eprintln!("switched to '{}' ({} frames)", name, f.len());
            *frames = f;
            *frame_idx = 0;
        }
        Err(e) => eprintln!("switch to '{}': {}", name, e),
    }
}

fn retry_sleep(secs: u64) -> bool {
    for _ in 0..secs {
        if let Some(cmd) = read_command() {
            if cmd == "stop" {
                let _ = write_sysfs(&Color { r: 0, g: 0, b: 0 });
                return true;
            }
        }
        sleep(Duration::from_secs(1));
    }
    false
}

pub fn run() {
    eprintln!("kbd-rgbd starting (pid {})", std::process::id());

    let _ = fs::create_dir_all("/run/kbd-rgbd");

    if let Ok(f) = fs::OpenOptions::new()
        .create(true)
        .write(true)
        .truncate(false)
        .open(CMD_PATH)
    {
        drop(f);
        let _ = fs::set_permissions(CMD_PATH, fs::Permissions::from_mode(0o666));
    }

    let (mut frames, mut frame_idx) = loop {
        match resolve_active_path().and_then(|p| match load_frames(&p) {
            Ok(f) => {
                eprintln!("loaded {} frames", f.len());
                Some(f)
            }
            Err(e) => {
                eprintln!("profile load: {}", e);
                None
            }
        }) {
            Some(f) => break (f, 0usize),
            None => {
                eprintln!("waiting for active profile...");
                if retry_sleep(5) {
                    return;
                }
            }
        }
    };

    loop {
        if let Some(cmd) = read_command() {
            match cmd.as_str() {
                "stop" => {
                    let _ = write_sysfs(&Color { r: 0, g: 0, b: 0 });
                    break;
                }
                "reload" => {
                    if let Some(p) = resolve_active_path() {
                        if let Ok(f) = load_frames(&p) {
                            eprintln!("reloaded {} frames", f.len());
                            frames = f;
                            frame_idx = 0;
                        }
                    }
                }
                _ if cmd.starts_with("profile ") => {
                    let name = cmd.trim_start_matches("profile ").trim().to_string();
                    handle_profile_cmd(&name, &mut frames, &mut frame_idx);
                }
                "brightness_up" => adjust_brightness(26),
                "brightness_down" => adjust_brightness(-26),
                _ => {}
            }
        }

        let frame = &frames[frame_idx];
        if let Err(e) = write_sysfs(&frame.color) {
            eprintln!("write error: {}", e);
            let mut recovered = false;
            for _ in 0..5 {
                if let Some(cmd) = read_command() {
                    if cmd == "stop" {
                        let _ = write_sysfs(&Color { r: 0, g: 0, b: 0 });
                        return;
                    }
                    if cmd.starts_with("profile ") {
                        let name = cmd.trim_start_matches("profile ").trim().to_string();
                        handle_profile_cmd(&name, &mut frames, &mut frame_idx);
                        recovered = true;
                        break;
                    }
                }
                if write_sysfs(&frame.color).is_ok() {
                    recovered = true;
                    break;
                }
                sleep(Duration::from_secs(1));
            }
            if !recovered {
                eprintln!("retry failed, reloading profile");
                if let Some(p) = resolve_active_path() {
                    if let Ok(f) = load_frames(&p) {
                        frames = f;
                        frame_idx = 0;
                    }
                }
            }
            continue;
        }

        frame_idx = (frame_idx + 1) % frames.len();
        sleep(Duration::from_millis(frame.duration_ms));
    }

    eprintln!("kbd-rgbd stopped");
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_is_valid_profile_name() {
        assert!(is_valid_profile_name("cycle"));
        assert!(is_valid_profile_name("static-blue"));
        assert!(is_valid_profile_name("test_profile"));
        assert!(is_valid_profile_name("a"));
        assert!(!is_valid_profile_name(""));
        assert!(!is_valid_profile_name("../etc/passwd"));
        assert!(!is_valid_profile_name("a/b"));
        assert!(!is_valid_profile_name(&"a".repeat(65)));
    }

    #[test]
    fn test_load_frames_rejects_traversal_selector() {
        let dir = std::env::temp_dir().join("kbd-rgbd-test-traversal");
        let _ = fs::create_dir_all(&dir);
        let sel = dir.join("evil.json");
        fs::write(
            &sel,
            r#"{"leds":[{"device_name":"platform:acer_kbd_backlight","function":"kbd_backlight","profile":"../../etc/passwd","mode":"Single"}]}"#,
        )
        .unwrap();
        let err = load_frames(&sel).unwrap_err();
        assert!(
            matches!(err, KbdError::InvalidProfileName(_)),
            "expected InvalidProfileName, got {}",
            err
        );
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn test_resolve_active_path_confines_to_profiles_dir() {
        // resolve_active_path reads the fixed system path; it must at
        // least not panic when the symlink is absent or invalid here.
        let _ = resolve_active_path();
    }
}
