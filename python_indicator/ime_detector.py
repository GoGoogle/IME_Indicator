import ctypes
from win32_api import (
    user32, imm32, GUITHREADINFO, WM_IME_CONTROL, 
    IMC_GETOPENSTATUS, IMC_GETCONVERSIONMODE, IME_CMODE_NATIVE
)
from ctypes import byref, sizeof, wintypes

def get_focused_window():
    """获取当前焦点窗口"""
    fore_hwnd = user32.GetForegroundWindow()
    if not fore_hwnd:
        return 0
    
    thread_id = user32.GetWindowThreadProcessId(fore_hwnd, None)
    gui_info = GUITHREADINFO()
    gui_info.cbSize = sizeof(GUITHREADINFO)
    
    if user32.GetGUIThreadInfo(thread_id, byref(gui_info)):
        if gui_info.hwndFocus:
            return gui_info.hwndFocus
        if gui_info.hwndActive:
            return gui_info.hwndActive
    
    return fore_hwnd

def get_ime_window(hwnd):
    """获取 IME 默认窗口句柄"""
    return imm32.ImmGetDefaultIMEWnd(hwnd)

def send_message_timeout(hwnd, msg, wparam, lparam, timeout_ms=500):
    """带超时的消息发送"""
    result = wintypes.DWORD()
    ret = user32.SendMessageTimeoutW(
        hwnd, msg, wparam, lparam,
        0x2,  # SMTO_ABORTIFHUNG
        timeout_ms,
        byref(result)
    )
    if ret:
        return result.value
    return None

def is_chinese_mode():
    """检测是否为中文输入模式"""
    hwnd = get_focused_window()
    ime_hwnd = get_ime_window(hwnd)
    if not ime_hwnd:
        return False
    
    # 获取 IME 开启状态
    open_status = send_message_timeout(ime_hwnd, WM_IME_CONTROL, IMC_GETOPENSTATUS, 0)
    if not open_status:
        return False
        
    # 获取转换模式并检测是否包含 NATIVE 标志 (中文)
    conversion_mode = send_message_timeout(ime_hwnd, WM_IME_CONTROL, IMC_GETCONVERSIONMODE, 0)
    if conversion_mode is not None:
        return bool(conversion_mode & IME_CMODE_NATIVE)
    return False
