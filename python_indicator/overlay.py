import ctypes
from ctypes import wintypes, byref, sizeof
from win32_api import (
    user32, gdi32, gdiplus, BLENDFUNCTION, GdiplusStartupInput,
    BITMAPINFOHEADER, WS_EX_LAYERED, WS_EX_TRANSPARENT, WS_EX_TOPMOST,
    WS_EX_NOACTIVATE, WS_POPUP, ULW_ALPHA, AC_SRC_OVER, AC_SRC_ALPHA,
    SmoothingModeAntiAlias
)

def parse_color(color_val):
    """
    解析颜色值，支持：
    1. 元组 (R, G, B, A) 或 (R, G, B)
    2. HEX 字符串 "#RRGGBB" 或 "#AARRGGBB"
    返回 (R, G, B, A) 元组
    """
    if isinstance(color_val, (tuple, list)):
        if len(color_val) == 3:
            return (*color_val, 255)
        return tuple(color_val)
    
    if isinstance(color_val, str) and color_val.startswith('#'):
        hex_val = color_val.lstrip('#')
        if len(hex_val) == 6: # RRGGBB
            r = int(hex_val[0:2], 16)
            g = int(hex_val[2:4], 16)
            b = int(hex_val[4:6], 16)
            return (r, g, b, 255)
        elif len(hex_val) == 8: # RRGGBBAA (CSS 标准)
            r = int(hex_val[0:2], 16)
            g = int(hex_val[2:4], 16)
            b = int(hex_val[4:6], 16)
            a = int(hex_val[6:8], 16)
            return (r, g, b, a)
    return (128, 128, 128, 128)

class IndicatorOverlay:
    """通用状态指示悬浮窗 (GDI+ 渲染)"""
    def __init__(self, name, size=8, color_cn="#A0FF7800", color_en="#300078FF", offset_x=0, offset_y=0):
        self.name = name
        self.size = size
        self.color_cn = parse_color(color_cn)
        self.color_en = parse_color(color_en)
        self.offset_x = offset_x
        self.offset_y = offset_y
        self.hwnd = None
        self.gdi_token = ctypes.c_ulonglong()
        
        self._init_gdiplus()
        self.hwnd = self._create_window()

    def _init_gdiplus(self):
        gdi_input = GdiplusStartupInput(1, None, 0, 0)
        gdiplus.GdiplusStartup(byref(self.gdi_token), byref(gdi_input), None)

    def _create_window(self):
        WNDPROC = ctypes.WINFUNCTYPE(ctypes.c_longlong, wintypes.HWND, ctypes.c_uint, wintypes.WPARAM, wintypes.LPARAM)
        
        class WNDCLASSEX(ctypes.Structure):
            _fields_ = [
                ("cbSize", ctypes.c_uint), ("style", ctypes.c_uint), ("lpfnWndProc", WNDPROC),
                ("cbClsExtra", ctypes.c_int), ("cbWndExtra", ctypes.c_int),
                ("hInstance", wintypes.HINSTANCE), ("hIcon", wintypes.HANDLE),
                ("hCursor", wintypes.HANDLE), ("hbrBackground", wintypes.HBRUSH),
                ("lpszMenuName", wintypes.LPCWSTR), ("lpszClassName", wintypes.LPCWSTR),
                ("hIconSm", wintypes.HANDLE),
            ]

        def wnd_proc(hwnd, msg, wparam, lparam):
            if msg == 2: return 0
            return user32.DefWindowProcW(hwnd, msg, wparam, lparam)

        self._wnd_proc_ref = WNDPROC(wnd_proc)
        h_instance = ctypes.windll.kernel32.GetModuleHandleW(None)
        
        wxc = WNDCLASSEX()
        wxc.cbSize = sizeof(WNDCLASSEX)
        wxc.lpfnWndProc = self._wnd_proc_ref
        wxc.hInstance = h_instance
        wxc.lpszClassName = f"IMEIndicator_{self.name}"
        
        user32.RegisterClassExW(byref(wxc))

        hwnd = user32.CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
            wxc.lpszClassName, f"Indicator_{self.name}", WS_POPUP,
            0, 0, self.size, self.size, None, None, h_instance, None
        )
        return hwnd

    def update(self, x, y, is_chinese, caret_h=0):
        """同时更新渲染内容和屏幕位置"""
        color = self.color_cn if is_chinese else self.color_en
        
        screen_dc = user32.GetDC(0)
        mem_dc = gdi32.CreateCompatibleDC(screen_dc)
        
        bmi = BITMAPINFOHEADER()
        bmi.biSize = sizeof(BITMAPINFOHEADER)
        bmi.biWidth, bmi.biHeight = self.size, self.size
        bmi.biPlanes, bmi.biBitCount, bmi.biCompression = 1, 32, 0

        ppv_bits = ctypes.c_void_p()
        h_bitmap = gdi32.CreateDIBSection(mem_dc, byref(bmi), 0, byref(ppv_bits), None, 0)
        old_bitmap = gdi32.SelectObject(mem_dc, h_bitmap)

        graphics = ctypes.c_void_p()
        gdiplus.GdipCreateFromHDC(mem_dc, byref(graphics))
        gdiplus.GdipSetSmoothingMode(graphics, SmoothingModeAntiAlias)

        r, g, b, a = color
        argb = (a << 24) | (r << 16) | (g << 8) | b
        brush = ctypes.c_void_p()
        gdiplus.GdipCreateSolidFill(argb, byref(brush))
        gdiplus.GdipFillEllipse(graphics, brush, ctypes.c_float(0), ctypes.c_float(0), ctypes.c_float(self.size), ctypes.c_float(self.size))

        gdiplus.GdipDeleteBrush(brush)
        gdiplus.GdipDeleteGraphics(graphics)

        point_dest = wintypes.POINT(
            int(x + self.offset_x - self.size / 2),
            int(y + caret_h + self.offset_y - self.size / 2)
        )
        point_src = wintypes.POINT(0, 0)
        size_struct = wintypes.SIZE(self.size, self.size)
        blend = BLENDFUNCTION(AC_SRC_OVER, 0, 255, AC_SRC_ALPHA)

        user32.UpdateLayeredWindow(
            self.hwnd, screen_dc, byref(point_dest), byref(size_struct),
            mem_dc, byref(point_src), 0, byref(blend), ULW_ALPHA
        )

        gdi32.SelectObject(mem_dc, old_bitmap)
        gdi32.DeleteObject(h_bitmap)
        gdi32.DeleteDC(mem_dc)
        user32.ReleaseDC(0, screen_dc)
        
        user32.SetWindowPos(self.hwnd, -1, 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0010 | 0x0040)
        
        msg = wintypes.MSG()
        while user32.PeekMessageW(byref(msg), self.hwnd, 0, 0, 1):
            user32.TranslateMessage(byref(msg))
            user32.DispatchMessageW(byref(msg))

    def show(self):
        user32.ShowWindow(self.hwnd, 5)

    def hide(self):
        user32.ShowWindow(self.hwnd, 0)

    def cleanup(self):
        if self.hwnd: user32.DestroyWindow(self.hwnd)
        gdiplus.GdiplusShutdown(self.gdi_token)
