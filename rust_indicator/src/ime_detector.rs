use windows::Win32::Foundation::{HWND, WPARAM, LPARAM};
use windows::Win32::UI::Input::Ime::ImmGetDefaultIMEWnd;
use windows::Win32::UI::Input::KeyboardAndMouse::{GetKeyState, VK_CAPITAL};
use windows::Win32::UI::WindowsAndMessaging::{
    GetForegroundWindow, GetGUIThreadInfo, GetWindowThreadProcessId,
    SendMessageTimeoutW, GUITHREADINFO, SMTO_ABORTIFHUNG,
};

const WM_IME_CONTROL: u32 = 0x283;
const IMC_GETOPENSTATUS: usize = 0x5;
const IMC_GETCONVERSIONMODE: usize = 0x1;
const IME_CMODE_NATIVE: u32 = 0x0001;

fn get_focused_window() -> HWND {
    unsafe {
        let fore_hwnd = GetForegroundWindow();
        if fore_hwnd.0.is_null() { return HWND::default(); }
        let thread_id = GetWindowThreadProcessId(fore_hwnd, None);
        let mut gui_info = GUITHREADINFO { cbSize: std::mem::size_of::<GUITHREADINFO>() as u32, ..Default::default() };
        if GetGUIThreadInfo(thread_id, &mut gui_info).is_ok() {
            if !gui_info.hwndFocus.0.is_null() { return gui_info.hwndFocus; }
            if !gui_info.hwndActive.0.is_null() { return gui_info.hwndActive; }
        }
        fore_hwnd
    }
}

pub fn is_chinese_mode() -> bool {
    let hwnd = get_focused_window();
    let ime_hwnd = unsafe { ImmGetDefaultIMEWnd(hwnd) };
    if ime_hwnd.0.is_null() { return false; }
    
    let mut result: usize = 0;
    unsafe {
        let _ = SendMessageTimeoutW(ime_hwnd, WM_IME_CONTROL, WPARAM(IMC_GETOPENSTATUS), LPARAM(0), SMTO_ABORTIFHUNG, 500, Some(&mut result));
        if result == 0 { return false; }
        
        let _ = SendMessageTimeoutW(ime_hwnd, WM_IME_CONTROL, WPARAM(IMC_GETCONVERSIONMODE), LPARAM(0), SMTO_ABORTIFHUNG, 500, Some(&mut result));
        (result as u32 & IME_CMODE_NATIVE) != 0
    }
}

pub fn is_caps_lock_on() -> bool {
    unsafe { (GetKeyState(VK_CAPITAL.0 as i32) & 1) != 0 }
}
