//! Keyframe animation engine: pure functions, no I/O.
//!
//! A `Key` list describes colors and the transition *away* from each
//! one; [`build`] expands it into per-frame output at 80 ms steps,
//! mirroring the timing of the original JSON preset engine so existing
//! animations look identical.

pub struct Frame {
    pub r: u8,
    pub g: u8,
    pub b: u8,
    pub duration_ms: u64,
}

pub struct Key {
    pub r: u8,
    pub g: u8,
    pub b: u8,
    /// Milliseconds spent transitioning from this key to the next.
    pub ms: u64,
    /// Snap instantly instead of interpolating.
    pub snap: bool,
}

const STEP_MS: u64 = 80;

pub fn lerp(a: u8, b: u8, t: f64) -> u8 {
    let v = a as f64 + (b as f64 - a as f64) * t;
    v.clamp(0.0, 255.0).round() as u8
}

/// Expand keys into frames, wrapping from the last key to the first.
pub fn build(keys: &[Key]) -> Vec<Frame> {
    let mut frames = Vec::new();
    if keys.is_empty() {
        return frames;
    }
    for (i, from) in keys.iter().enumerate() {
        let to = &keys[(i + 1) % keys.len()];
        if from.snap {
            frames.push(Frame {
                r: to.r,
                g: to.g,
                b: to.b,
                duration_ms: from.ms.max(1),
            });
            continue;
        }
        let steps = (from.ms.max(1) / STEP_MS).max(1);
        for s in 0..=steps {
            let t = s as f64 / steps as f64;
            frames.push(Frame {
                r: lerp(from.r, to.r, t),
                g: lerp(from.g, to.g, t),
                b: lerp(from.b, to.b, t),
                duration_ms: STEP_MS,
            });
        }
    }
    frames
}

fn linear(keys: &[(u8, u8, u8)], ms: u64) -> Vec<Key> {
    keys.iter()
        .map(|&(r, g, b)| Key {
            r,
            g,
            b,
            ms,
            snap: false,
        })
        .collect()
}

/// Built-in animations, ported 1:1 from the retired JSON presets.
pub fn builtin(name: &str) -> Option<Vec<Frame>> {
    let keys: Vec<Key> = match name {
        "rainbow" => linear(
            &[
                (255, 0, 0),
                (255, 165, 0),
                (255, 255, 0),
                (0, 255, 0),
                (0, 0, 255),
                (148, 0, 211),
            ],
            4000,
        ),
        "cycle" => linear(&[(255, 0, 0), (0, 255, 0), (0, 0, 255)], 6000),
        "ocean" => linear(
            &[
                (0, 50, 100),
                (0, 100, 200),
                (0, 180, 255),
                (0, 200, 180),
                (0, 80, 150),
            ],
            6000,
        ),
        "sunset" => linear(
            &[
                (255, 80, 0),
                (255, 150, 0),
                (255, 200, 100),
                (200, 50, 100),
                (150, 0, 80),
            ],
            5000,
        ),
        "strobe" => vec![
            Key {
                r: 255,
                g: 255,
                b: 255,
                ms: 100,
                snap: true,
            },
            Key {
                r: 0,
                g: 0,
                b: 0,
                ms: 100,
                snap: true,
            },
        ],
        _ => return None,
    };
    Some(build(&keys))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_lerp_bounds() {
        assert_eq!(lerp(0, 255, 0.0), 0);
        assert_eq!(lerp(0, 255, 1.0), 255);
    }

    #[test]
    fn test_lerp_mid() {
        assert_eq!(lerp(0, 255, 0.5), 128);
        assert_eq!(lerp(100, 200, 0.5), 150);
    }

    #[test]
    fn test_build_linear_step_count() {
        // 2 keys, 160 ms each at 80 ms steps => (2+1) frames per segment.
        let frames = build(&linear(&[(255, 0, 0), (0, 0, 255)], 160));
        assert_eq!(frames.len(), 6);
        assert_eq!((frames[0].r, frames[0].b), (255, 0));
        assert_eq!((frames[2].r, frames[2].b), (0, 255));
    }

    #[test]
    fn test_build_snap_and_wrap() {
        let frames = build(&[
            Key {
                r: 1,
                g: 2,
                b: 3,
                ms: 100,
                snap: true,
            },
            Key {
                r: 4,
                g: 5,
                b: 6,
                ms: 100,
                snap: false,
            },
        ]);
        // snap emits the *target*; linear wraps back to the first key.
        assert_eq!(frames.len(), 1 + 2);
        assert_eq!((frames[0].r, frames[0].g, frames[0].b), (4, 5, 6));
        assert_eq!((frames[2].r, frames[2].g, frames[2].b), (1, 2, 3));
    }

    #[test]
    fn test_build_empty() {
        assert!(build(&[]).is_empty());
    }

    #[test]
    fn test_builtin_names_resolve() {
        for name in ["rainbow", "cycle", "ocean", "sunset", "strobe"] {
            let frames = builtin(name).expect(name);
            assert!(!frames.is_empty(), "{name}");
        }
        assert!(builtin("nope").is_none());
    }
}
