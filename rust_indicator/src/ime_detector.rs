//! IME 状态检测模块 - 检测中英文输入模式及大写锁定状态
use windows::Win32::Foundation::{HWND, LPARAM, WPARAM};
use windows::Win32::UI::Input::Ime::ImmGetDefaultIMEWnd;
use windows::Win32::UI::Input::KeyboardAndMouse::{GetKeyState, VK_CAPITAL};
use windows::Win32::UI::WindowsAndMessaging::{
    GetForegroundWindow, GetGUIThreadInfo, GetWindowThreadProcessId, SendMessageTimeoutW,
    GUITHREADINFO, SMTO_ABORTIFHUNG,
};

/// IME 控制消息常量
const WM_IME_CONTROL: u32 = 0x283;
const IMC_GETOPENSTATUS: usize = 0x5;
const IMC_GETCONVERSIONMODE: usize = 0x1;
const IME_CMODE_NATIVE: u32 = 0x0001;

/// 获取当前真正具有输入焦点的窗口句柄
fn get_focused_window() -> HWND {
    unsafe {
        let fore_hwnd = GetForegroundWindow();
        if fore_hwnd.0.is_null() {
            return HWND::default();
        }

        // 获取前台窗口所属的线程 ID
        let thread_id = GetWindowThreadProcessId(fore_hwnd, None);
        let mut gui_info = GUITHREADINFO {
            cbSize: std::mem::size_of::<GUITHREADINFO>() as u32,
            ..Default::default()
        };

        // 通过 GUI 线程信息获取精准的焦点窗口（处理多文档界面或复杂窗口）
        if GetGUIThreadInfo(thread_id, &mut gui_info).is_ok() {
            if !gui_info.hwndFocus.0.is_null() {
                return gui_info.hwndFocus;
            }
            if !gui_info.hwndActive.0.is_null() {
                return gui_info.hwndActive;
            }
        }
        fore_hwnd
    }
}

/// 检测当前是否为中文输入模式
pub fn is_chinese_mode() -> bool {
    let hwnd = get_focused_window();
    // 获取窗口对应的默认 IME 消息处理窗口
    let ime_hwnd = unsafe { ImmGetDefaultIMEWnd(hwnd) };
    
    if ime_hwnd.0.is_null() {
        return false;
    }

    unsafe {
        let mut result: usize = 0;

        // 1. 首先检测 IME 是否处于开启状态 (Open Status)
        let _ = SendMessageTimeoutW(
            ime_hwnd,
            WM_IME_CONTROL,
            WPARAM(IMC_GETOPENSTATUS),
            LPARAM(0),
            SMTO_ABORTIFHUNG,
            500,
            Some(&mut result),
        );
        
        // 如果 IME 关闭（纯英文状态），直接返回 false
        if result == 0 {
            return false;
        }

        // 2. 如果 IME 开启，进一步检测转换模式 (Conversion Mode)
        // 检测是否处于原生字符转换模式（Native Mode，通常代表中文模式）
        let _ = SendMessageTimeoutW(
            ime_hwnd,
            WM_IME_CONTROL,
            WPARAM(IMC_GETCONVERSIONMODE),
            LPARAM(0),
            SMTO_ABORTIFHUNG,
            500,
            Some(&mut result),
        );

        // IME_CMODE_NATIVE 位为 1 表示中文/原生模式
        (result as u32 & IME_CMODE_NATIVE) != 0
    }
}

/// 检测大写锁定
