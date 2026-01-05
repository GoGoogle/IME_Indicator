//! IME 状态检测模块 - 处理中英文模式切换及大写锁定逻辑
use windows::Win32::Foundation::{HWND, WPARAM, LPARAM};
use windows::Win32::UI::Input::Ime::ImmGetDefaultIMEWnd;
use windows::Win32::UI::Input::KeyboardAndMouse::{GetKeyState, VK_CAPITAL};
use windows::Win32::UI::WindowsAndMessaging::{
    GetForegroundWindow, GetGUIThreadInfo, GetWindowThreadProcessId,
    SendMessageTimeoutW, GUITHREADINFO, SMTO_ABORTIFHUNG,
};

// Windows IME 消息常量定义
const WM_IME_CONTROL: u32 = 0x283;
const IMC_GETOPENSTATUS: usize = 0x5;       // 获取 IME 开启/关闭状态
const IMC_GETCONVERSIONMODE: usize = 0x1;    // 获取输入模式（中文/英文/标点）
const IME_CMODE_NATIVE: u32 = 0x0001;        // 原生模式位（通常代表中文）

/// 获取当前真正拥有输入焦点的窗口（处理复杂的多级窗口结构）
fn get_focused_window() -> HWND {
    unsafe {
        let fore_hwnd = GetForegroundWindow();
        if fore_hwnd.0.is_null() { return HWND::default(); }
        
        let thread_id = GetWindowThreadProcessId(fore_hwnd, None);
        let mut gui_info = GUITHREADINFO { 
            cbSize: std::mem::size_of::<GUITHREADINFO>() as u32, 
            ..Default::default() 
        };
        
        // 尝试通过 GUI 线程信息获取更精准的焦点子窗口
        if GetGUIThreadInfo(thread_id, &mut gui_info).is_ok() {
            if !gui_info.hwndFocus.0.is_null() { return gui_info.hwndFocus; }
            if !gui_info.hwndActive.0.is_null() { return gui_info.hwndActive; }
        }
        fore_hwnd
    }
}

/// 判断当前输入法是否处于“中文模式”
pub fn is_chinese_mode() -> bool {
    let hwnd = get_focused_window();
    let ime_hwnd = unsafe { ImmGetDefaultIMEWnd(hwnd) };
    if ime_hwnd.0.is_null() { return false; }
    
    let mut result: usize = 0;
    unsafe {
        // 步骤 1: 检测 IME 是否打开（如果关闭，说明是纯英文状态）
        let _ = SendMessageTimeoutW(
            ime_hwnd, WM_IME_CONTROL, WPARAM(IMC_GETOPENSTATUS), 
            LPARAM(0), SMTO_ABORTIFHUNG, 500, Some(&mut result)
        );
        if result == 0 { return false; }
        
        // 步骤 2: 检测是否处于 Native 模式（即中文输入状态）
        let _ = SendMessageTimeoutW(
            ime_hwnd, WM_IME_CONTROL, WPARAM(IMC_GETCONVERSIONMODE), 
            LPARAM(0), SMTO_ABORTIFHUNG, 500, Some(&mut result)
        );
        (result as u32 & IME_CMODE_NATIVE) != 0
    }
}

/// 判断大写锁定 (Caps Lock) 是否开启
pub fn is_caps_lock_on() -> bool {
    unsafe { 
        // GetKeyState 最低位 (1) 表示切换状态
        (GetKeyState(VK_CAPITAL.0 as i32) & 1) != 0 
    }
}
