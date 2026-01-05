#![windows_subsystem = "windows"]
mod caret_detector; mod config; mod cursor_detector; mod ime_detector; mod overlay; mod tray;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::thread;
use std::time::{Duration, Instant};
use std::ptr::null_mut;
use windows::Win32::Foundation::POINT;
use windows::Win32::UI::WindowsAndMessaging::{GetCursorPos, LoadIconW, IDI_APPLICATION};
use caret_detector::CaretDetector;
use cursor_detector::CursorDetector;
use ime_detector::{is_chinese_mode, is_caps_lock_on};
use overlay::IndicatorOverlay;
use tray::TrayManager;

fn main() {
    set_dpi_awareness();
    let running = Arc::new(AtomicBool::new(true));
    let r_clone = running.clone();
    thread::spawn(move || run_detector_loop(r_clone));
    
    unsafe {
        let mut token = 0;
        let input = windows::Win32::Graphics::GdiPlus::GdiplusStartupInput { GdiplusVersion: 1, ..Default::default() };
        let _ = windows::Win32::Graphics::GdiPlus::GdiplusStartup(&mut token, &input, null_mut());
        
        if config::tray_enable() {
            let h_icon = LoadIconW(None, IDI_APPLICATION).unwrap();
            let mut tray = TrayManager::new(h_icon);
            // 这里调用托盘消息循环，保持程序运行
            use windows::Win32::UI::WindowsAndMessaging::{GetMessageW, TranslateMessage, DispatchMessageW, MSG};
            let mut msg = MSG::default();
            while running.load(Ordering::SeqCst) && GetMessageW(&mut msg, None, 0, 0).into() {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        } else {
            while running.load(Ordering::SeqCst) { thread::sleep(Duration::from_millis(100)); }
        }
    }
}

fn run_detector_loop(running: Arc<AtomicBool>) {
    let mut caret_det = CaretDetector::new();
    let cursor_det = CursorDetector::new(config::mouse_target_cursors());
    let caret_ov = if config::caret_enable() { Some(IndicatorOverlay::new("Caret", config::caret_size(), config::caret_color_cn(), config::caret_color_en(), config::caps_color(), config::caret_offset_x(), config::caret_offset_y())) } else { None };
    let mouse_ov = if config::mouse_enable() { Some(IndicatorOverlay::new("Mouse", config::mouse_size(), config::mouse_color_cn(), config::mouse_color_en(), config::caps_color(), config::mouse_offset_x(), config::mouse_offset_y())) } else { None };

    let mut last_check = Instant::now();
    let (mut is_cn, mut is_caps) = (false, false);
    let (mut caret_active, mut mouse_active) = (false, false);

    while running.load(Ordering::SeqCst) {
        if last_check.elapsed() >= Duration::from_millis(config::state_poll_interval_ms()) {
            is_cn = is_chinese_mode();
            is_caps = if config::caps_enable() { is_caps_lock_on() } else { false };
            if let Some(ref ov) = caret_ov {
                let should = is_cn || is_caps || config::caret_show_en();
                if should != caret_active { caret_active = should; if caret_active { ov.show(); } else { ov.hide(); } }
            }
            if let Some(ref ov) = mouse_ov {
                let should = cursor_det.is_target_cursor() && (is_cn || is_caps || config::mouse_show_en());
                if should != mouse_active { mouse_active = should; if mouse_active { ov.show(); } else { ov.hide(); } }
            }
            last_check = Instant::now();
        }
        if caret_active { if let Some(ref ov) = caret_ov { if let Some((x, y, h)) = caret_det.get_caret_pos() { ov.update(x, y, is_cn, is_caps, h); } } }
        if mouse_active { if let Some(ref ov) = mouse_ov { let mut pt = POINT::default(); unsafe { if GetCursorPos(&mut pt).is_ok() { ov.update(pt.x, pt.y, is_cn, is_caps, 0); } } } }
        thread::sleep(Duration::from_millis(config::track_poll_interval_ms()));
    }
}

fn set_dpi_awareness() {
    unsafe {
        let shcore = windows::Win32::System::LibraryLoader::LoadLibraryW(windows::core::w!("shcore.dll"));
        if let Ok(h) = shcore {
            if let Some(func) = windows::Win32::System::LibraryLoader::GetProcAddress(h, windows::core::s!("SetProcessDpiAwareness")) {
                let set_dpi: extern "system" fn(i32) -> i32 = std::mem::transmute(func);
                let _ = set_dpi(2);
            }
        }
    }
}
