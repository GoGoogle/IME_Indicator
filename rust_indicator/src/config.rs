use std::collections::HashMap;
use std::fs;
use std::path::PathBuf;
use std::sync::OnceLock;

pub struct Config {
    pub poll_state_interval_ms: u64,
    pub poll_track_interval_ms: u64,
    pub tray_enable: bool,
    pub caret_enable: bool,
    pub caret_color_cn: u32,
    pub caret_color_en: u32,
    pub caret_size: i32,
    pub caret_offset_x: i32,
    pub caret_offset_y: i32,
    pub caret_show_en: bool,
    pub mouse_enable: bool,
    pub mouse_color_cn: u32,
    pub mouse_color_en: u32,
    pub mouse_size: i32,
    pub mouse_offset_x: i32,
    pub mouse_offset_y: i32,
    pub mouse_show_en: bool,
    pub mouse_target_cursors: Vec<u32>,
    pub caps_enable: bool,
    pub caps_color: u32,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            poll_state_interval_ms: 100,
            poll_track_interval_ms: 10,
            tray_enable: true,
            caret_enable: true,
            caret_color_cn: parse_color("#FF7800A0"),
            caret_color_en: parse_color("#0078FF30"),
            caret_size: 8,
            caret_offset_x: 0,
            caret_offset_y: 0,
            caret_show_en: true,
            mouse_enable: true,
            mouse_color_cn: parse_color("#FF7800A0"),
            mouse_color_en: parse_color("#0078FF30"),
            mouse_size: 8,
            mouse_offset_x: 2,
            mouse_offset_y: 18,
            mouse_show_en: true,
            mouse_target_cursors: vec![32513, 32512],
            caps_enable: true,
            caps_color: parse_color("#00FF00A0"),
        }
    }
}

pub fn parse_color(s: &str) -> u32 {
    let clean = s.trim().trim_matches('"').trim_start_matches('#');
    if clean.len() >= 6 {
        let r = u32::from_str_radix(&clean[0..2], 16).unwrap_or(0);
        let g = u32::from_str_radix(&clean[2..4], 16).unwrap_or(0);
        let b = u32::from_str_radix(&clean[4..6], 16).unwrap_or(0);
        let a = if clean.len() == 8 { u32::from_str_radix(&clean[6..8], 16).unwrap_or(0xA0) } else { 0xA0 };
        (a << 24) | (r << 16) | (g << 8) | b
    } else { 0xA0FF7800 }
}

fn load_config() -> Config {
    let mut config = Config::default();
    let path = get_config_path();
    if !path.exists() { let _ = fs::write(&path, generate_toml_template()); return config; }
    if let Ok(content) = fs::read_to_string(&path) {
        let mut sections = HashMap::new();
        let mut cur_sec = String::new();
        for line in content.lines() {
            let line = line.trim();
            if line.is_empty() || line.starts_with('#') { continue; }
            if line.starts_with('[') && line.ends_with(']') { cur_sec = line[1..line.len()-1].to_lowercase(); }
            else if let Some((k, v)) = line.split_once('=') {
                let key = k.trim().to_lowercase();
                let val = v.split(" #").next().unwrap().trim().to_string();
                sections.entry(cur_sec.clone()).or_insert_with(HashMap::new).insert(key, val);
            }
        }
        let get = |sec: &str, key: &str| sections.get(sec)?.get(key);
        if let Some(v) = get("poll", "state_interval_ms") { if let Ok(n) = v.parse() { config.poll_state_interval_ms = n; } }
        if let Some(v) = get("poll", "track_interval_ms") { if let Ok(n) = v.parse() { config.poll_track_interval_ms = n; } }
        if let Some(v) = get("tray", "enable") { config.tray_enable = v == "true"; }
        if let Some(v) = get("caret", "enable") { config.caret_enable = v == "true"; }
        if let Some(v) = get("caret", "color_cn") { config.caret_color_cn = parse_color(v); }
        if let Some(v) = get("caret", "color_en") { config.caret_color_en = parse_color(v); }
        if let Some(v) = get("caret", "size") { if let Ok(n) = v.parse() { config.caret_size = n; } }
        if let Some(v) = get("caret", "offset_x") { if let Ok(n) = v.parse() { config.caret_offset_x = n; } }
        if let Some(v) = get("caret", "offset_y") { if let Ok(n) = v.parse() { config.caret_offset_y = n; } }
        if let Some(v) = get("caret", "show_en") { config.caret_show_en = v == "true"; }
        if let Some(v) = get("mouse", "enable") { config.mouse_enable = v == "true"; }
        if let Some(v) = get("mouse", "color_cn") { config.mouse_color_cn = parse_color(v); }
        if let Some(v) = get("mouse", "color_en") { config.mouse_color_en = parse_color(v); }
        if let Some(v) = get("mouse", "size") { if let Ok(n) = v.parse() { config.mouse_size = n; } }
        if let Some(v) = get("mouse", "offset_x") { if let Ok(n) = v.parse() { config.mouse_offset_x = n; } }
        if let Some(v) = get("mouse", "offset_y") { if let Ok(n) = v.parse() { config.mouse_offset_y = n; } }
        if let Some(v) = get("mouse", "show_en") { config.mouse_show_en = v == "true"; }
        if let Some(v) = get("mouse", "target_cursors") {
            config.mouse_target_cursors = v.trim_matches(|c| c == '[' || c == ']')
                .split(',').filter_map(|s| s.trim().parse().ok()).collect();
        }
        if let Some(v) = get("caps", "enable") { config.caps_enable = v == "true"; }
        if let Some(v) = get("caps", "color") { config.caps_color = parse_color(v); }
    }
    config
}

pub fn get_config_path() -> PathBuf { std::env::current_exe().unwrap().parent().unwrap().join("config.toml") }

fn generate_toml_template() -> String {
    r##"[poll]
state_interval_ms = 100
track_interval_ms = 10
[tray]
enable = true
[caret]
enable = true
color_cn = "#FF7800A0"
color_en = "#0078FF30"
size = 8
offset_x = 0
offset_y = 0
show_en = true
[mouse]
enable = true
color_cn = "#FF7800A0"
color_en = "#0078FF30"
size = 8
offset_x = 2
offset_y = 18
show_en = true
target_cursors = [32513, 32512]
[caps]
enable = true
color = "#00FF00A0"
"##.to_string()
}

static CONFIG: OnceLock<Config> = OnceLock::new();
pub fn get() -> &'static Config { CONFIG.get_or_init(load_config) }
pub fn state_poll_interval_ms() -> u64 { get().poll_state_interval_ms }
pub fn track_poll_interval_ms() -> u64 { get().poll_track_interval_ms }
pub fn tray_enable() -> bool { get().tray_enable }
pub fn caret_enable() -> bool { get().caret_enable }
pub fn caret_color_cn() -> u32 { get().caret_color_cn }
pub fn caret_color_en() -> u32 { get().caret_color_en }
pub fn caret_size() -> i32 { get().caret_size }
pub fn caret_offset_x() -> i32 { get().caret_offset_x }
pub fn caret_offset_y() -> i32 { get().caret_offset_y }
pub fn caret_show_en() -> bool { get().caret_show_en }
pub fn mouse_enable() -> bool { get().mouse_enable }
pub fn mouse_color_cn() -> u32 { get().mouse_color_cn }
pub fn mouse_color_en() -> u32 { get().mouse_color_en }
pub fn mouse_size() -> i32 { get().mouse_size }
pub fn mouse_offset_x() -> i32 { get().mouse_offset_x }
pub fn mouse_offset_y() -> i32 { get().mouse_offset_y }
pub fn mouse_show_en() -> bool { get().mouse_show_en }
pub fn mouse_target_cursors() -> &'static [u32] { &get().mouse_target_cursors }
pub fn caps_enable() -> bool { get().caps_enable }
pub fn caps_color() -> u32 { get().caps_color }
