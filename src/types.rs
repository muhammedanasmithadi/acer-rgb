use crate::error::{KbdError, Result};
use serde::Deserialize;

#[derive(Debug, Clone, Deserialize)]
pub struct Color {
    pub r: u8,
    pub g: u8,
    pub b: u8,
}

#[derive(Debug, Clone, Deserialize)]
pub enum Transition {
    Linear,
    None,
}

#[derive(Debug, Clone, Deserialize)]
pub struct ColorPoint {
    pub color: Color,
    pub transition: Option<Transition>,
    pub transition_time: Option<u64>,
}

#[derive(Debug, Clone, Deserialize)]
pub enum ColorProfile {
    Single(Color),
    Multiple(Vec<ColorPoint>),
    None,
}

#[derive(Debug, Deserialize)]
pub struct ProfileSelector {
    pub leds: Vec<LedProfile>,
}

#[derive(Debug, Deserialize)]
pub struct LedProfile {
    pub device_name: String,
    pub function: String,
    pub profile: String,
    pub mode: String,
}

#[derive(Debug, Clone)]
pub struct Frame {
    pub color: Color,
    pub duration_ms: u64,
}

pub fn parse_keyboard_json(json: &str) -> Result<ColorProfile> {
    serde_json::from_str(json).map_err(KbdError::Json)
}

pub fn parse_profile_selector(json: &str) -> Result<ProfileSelector> {
    serde_json::from_str(json).map_err(KbdError::Json)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::animation::build_frames;

    #[test]
    fn test_single_profile() {
        let json = r#"{"Single":{"r":255,"g":100,"b":0}}"#;
        let profile = parse_keyboard_json(json).unwrap();
        match &profile {
            ColorProfile::Single(c) => {
                assert_eq!(c.r, 255);
                assert_eq!(c.g, 100);
                assert_eq!(c.b, 0);
            }
            _ => panic!("expected Single"),
        }
        let frames = build_frames(&profile);
        assert_eq!(frames.len(), 1);
        assert_eq!(frames[0].color.r, 255);
        assert_eq!(frames[0].duration_ms, 1000);
    }

    #[test]
    fn test_none_profile() {
        let json = r#""None""#;
        let profile = parse_keyboard_json(json).unwrap();
        let frames = build_frames(&profile);
        assert_eq!(frames.len(), 1);
        assert_eq!(frames[0].color.r, 0);
        assert_eq!(frames[0].color.g, 0);
        assert_eq!(frames[0].color.b, 0);
    }

    #[test]
    fn test_multiple_profile_has_frames() {
        let json = r#"{"Multiple":[
            {"color":{"r":255,"g":0,"b":0},"transition":"Linear","transition_time":4000},
            {"color":{"r":0,"g":255,"b":0},"transition":"Linear","transition_time":4000}
        ]}"#;
        let profile = parse_keyboard_json(json).unwrap();
        let frames = build_frames(&profile);
        assert!(frames.len() >= 100);
    }

    #[test]
    fn test_selector_parse() {
        let json = r#"{"leds":[{"device_name":"platform:acer_kbd_backlight","function":"kbd_backlight","profile":"rainbow","mode":"Rgb"}]}"#;
        let sel = parse_profile_selector(json).unwrap();
        assert_eq!(sel.leds.len(), 1);
        assert_eq!(sel.leds[0].profile, "rainbow");
    }

    #[test]
    fn test_multiple_none_transition() {
        let json = r#"{"Multiple":[
            {"color":{"r":255,"g":0,"b":0},"transition":"None","transition_time":500},
            {"color":{"r":0,"g":255,"b":0},"transition":"None","transition_time":500}
        ]}"#;
        let profile = parse_keyboard_json(json).unwrap();
        let frames = build_frames(&profile);
        assert_eq!(frames.len(), 2);
        assert_eq!(frames[0].duration_ms, 500);
        assert_eq!(frames[1].duration_ms, 500);
    }

    #[test]
    fn test_transition_linear() {
        let json = r#"{"color":{"r":0,"g":0,"b":0},"transition":"Linear"}"#;
        let cp: ColorPoint = serde_json::from_str(json).unwrap();
        assert!(matches!(cp.transition, Some(Transition::Linear)));
    }

    #[test]
    fn test_transition_none() {
        let json = r#"{"color":{"r":0,"g":0,"b":0},"transition":"None"}"#;
        let cp: ColorPoint = serde_json::from_str(json).unwrap();
        assert!(matches!(cp.transition, Some(Transition::None)));
    }

    #[test]
    fn test_transition_absent_defaults_to_linear() {
        let json = r#"{"color":{"r":0,"g":0,"b":0}}"#;
        let cp: ColorPoint = serde_json::from_str(json).unwrap();
        assert!(cp.transition.is_none());
    }
}
