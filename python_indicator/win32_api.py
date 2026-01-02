import ctypes
from ctypes import wintypes, Structure, sizeof, POINTER, byref

# ============ Win32 常量 ============
WM_IME_CONTROL = 0x283
IMC_GETCONVERSIONMODE = 0x1
IMC_GETOPENSTATUS = 0x5

IME_CMODE_NATIVE = 0x0001
CURSOR_SHOWING = 0x00000001

# 光标 ID 常量
OCR_NORMAL      = 32512  # 标准箭头
OCR_IBEAM       = 32513  # I-Beam (文本选择)
OCR_WAIT        = 32514  # 等待 (沙漏/圆圈)
OCR_CROSS       = 32515  # 十字
OCR_UP          = 32516  # 向上箭头
OCR_SIZENWSE    = 32642  # 对角线调整 1 (左上-右下)
OCR_SIZENESW    = 32643  # 对角线调整 2 (右上-左下)
OCR_SIZEWE      = 32644  # 水平调整
OCR_SIZENS      = 32645  # 垂直调整
OCR_SIZEALL     = 32646  # 四向移动
OCR_NO          = 32648  # 禁止
OCR_HAND        = 32649  # 手型 (链接)
OCR_APPSTARTING = 32650  # 后台运行

OBJID_CARET = 0xFFFFFFF8

WS_EX_LAYERED = 0x00080000
WS_EX_TRANSPARENT = 0x00000020
WS_EX_TOPMOST = 0x00000008
WS_EX_NOACTIVATE = 0x08000000
WS_POPUP = 0x80000000

# UpdateLayeredWindow 常量
ULW_ALPHA = 0x00000002
AC_SRC_OVER = 0x00
AC_SRC_ALPHA = 0x01

SmoothingModeAntiAlias = 4

# ============ Win32 结构体 ============
HCURSOR = wintypes.HANDLE

class CURSORINFO(Structure):
    _fields_ = [
        ("cbSize", wintypes.DWORD),
        ("flags", wintypes.DWORD),
        ("hCursor", HCURSOR),
        ("ptScreenPos", wintypes.POINT),
    ]

class GUITHREADINFO(Structure):
    _fields_ = [
        ("cbSize", wintypes.DWORD),
        ("flags", wintypes.DWORD),
        ("hwndActive", wintypes.HWND),
        ("hwndFocus", wintypes.HWND),
        ("hwndCapture", wintypes.HWND),
        ("hwndMenuOwner", wintypes.HWND),
        ("hwndMoveSize", wintypes.HWND),
        ("hwndCaret", wintypes.HWND),
        ("rcCaret", wintypes.RECT),
    ]

class BLENDFUNCTION(Structure):
    _fields_ = [
        ("BlendOp", ctypes.c_byte),
        ("BlendFlags", ctypes.c_byte),
        ("SourceConstantAlpha", ctypes.c_byte),
        ("AlphaFormat", ctypes.c_byte),
    ]

class GdiplusStartupInput(Structure):
    _fields_ = [
        ("GdiplusVersion", ctypes.c_uint32),
        ("DebugEventCallback", ctypes.c_void_p),
        ("SuppressBackgroundThread", ctypes.c_int),
        ("SuppressExternalCodecs", ctypes.c_int),
    ]

class BITMAPINFOHEADER(Structure):
    _fields_ = [
        ("biSize", wintypes.DWORD), ("biWidth", wintypes.LONG), ("biHeight", wintypes.LONG),
        ("biPlanes", wintypes.WORD), ("biBitCount", wintypes.WORD), ("biCompression", wintypes.DWORD),
        ("biSizeImage", wintypes.DWORD), ("biXPelsPerMeter", wintypes.LONG), ("biYPelsPerMeter", wintypes.LONG),
        ("biClrUsed", wintypes.DWORD), ("biClrImportant", wintypes.DWORD),
    ]

class COMPOSITIONFORM(Structure):
    _fields_ = [
        ("dwStyle", wintypes.DWORD),
        ("ptCurrentPos", wintypes.POINT),
        ("rcArea", wintypes.RECT),
    ]

CFS_POINT = 0x0002

# ============ DLL 接口定义 ============
user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32
imm32 = ctypes.windll.imm32
gdiplus = ctypes.windll.gdiplus
oleacc = ctypes.windll.oleacc

# 为 64 位系统安全定义函数参数类型
user32.GetCursorInfo.argtypes = [POINTER(CURSORINFO)]
user32.GetCursorInfo.restype = wintypes.BOOL
user32.LoadCursorW.argtypes = [wintypes.HINSTANCE, wintypes.LPCWSTR]
user32.LoadCursorW.restype = HCURSOR
user32.GetGUIThreadInfo.argtypes = [wintypes.DWORD, POINTER(GUITHREADINFO)]
user32.GetGUIThreadInfo.restype = wintypes.BOOL
user32.GetForegroundWindow.restype = wintypes.HWND
user32.SendMessageTimeoutW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM, wintypes.UINT, wintypes.UINT, POINTER(wintypes.DWORD)]
user32.SendMessageTimeoutW.restype = ctypes.c_longlong
user32.ClientToScreen.argtypes = [wintypes.HWND, POINTER(wintypes.POINT)]
user32.ClientToScreen.restype = wintypes.BOOL
user32.GetCursorPos.argtypes = [POINTER(wintypes.POINT)]
user32.GetCursorPos.restype = wintypes.BOOL

user32.UpdateLayeredWindow.argtypes = [wintypes.HWND, wintypes.HDC, POINTER(wintypes.POINT), POINTER(wintypes.SIZE), wintypes.HDC, POINTER(wintypes.POINT), wintypes.COLORREF, POINTER(BLENDFUNCTION), wintypes.DWORD]
user32.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_uint]
user32.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
user32.DefWindowProcW.argtypes = [wintypes.HWND, ctypes.c_uint, wintypes.WPARAM, wintypes.LPARAM]
user32.DefWindowProcW.restype = ctypes.c_longlong
user32.GetDC.argtypes = [wintypes.HWND]
user32.GetDC.restype = wintypes.HDC
user32.ReleaseDC.argtypes = [wintypes.HWND, wintypes.HDC]
user32.ReleaseDC.restype = ctypes.c_int

imm32.ImmGetDefaultIMEWnd.argtypes = [wintypes.HWND]
imm32.ImmGetDefaultIMEWnd.restype = wintypes.HWND
imm32.ImmGetContext.argtypes = [wintypes.HWND]
imm32.ImmGetContext.restype = wintypes.HANDLE
imm32.ImmGetCompositionWindow.argtypes = [wintypes.HANDLE, POINTER(COMPOSITIONFORM)]
imm32.ImmGetCompositionWindow.restype = wintypes.BOOL
imm32.ImmReleaseContext.argtypes = [wintypes.HWND, wintypes.HANDLE]
imm32.ImmReleaseContext.restype = wintypes.BOOL

gdi32.CreateCompatibleDC.argtypes = [wintypes.HDC]
gdi32.CreateCompatibleDC.restype = wintypes.HDC
gdi32.CreateDIBSection.argtypes = [wintypes.HDC, POINTER(BITMAPINFOHEADER), wintypes.UINT, POINTER(ctypes.c_void_p), wintypes.HANDLE, wintypes.DWORD]
gdi32.CreateDIBSection.restype = wintypes.HBITMAP
gdi32.SelectObject.argtypes = [wintypes.HDC, wintypes.HGDIOBJ]
gdi32.SelectObject.restype = wintypes.HGDIOBJ
gdi32.DeleteObject.argtypes = [wintypes.HGDIOBJ]
gdi32.DeleteObject.restype = wintypes.BOOL
gdi32.DeleteDC.argtypes = [wintypes.HDC]
gdi32.DeleteDC.restype = wintypes.BOOL

gdiplus.GdipCreateFromHDC.argtypes = [wintypes.HDC, POINTER(ctypes.c_void_p)]
gdiplus.GdipSetSmoothingMode.argtypes = [ctypes.c_void_p, ctypes.c_int]
gdiplus.GdipCreateSolidFill.argtypes = [ctypes.c_uint32, POINTER(ctypes.c_void_p)]
gdiplus.GdipFillEllipse.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_float, ctypes.c_float, ctypes.c_float, ctypes.c_float]
gdiplus.GdipDeleteBrush.argtypes = [ctypes.c_void_p]
gdiplus.GdipDeleteGraphics.argtypes = [ctypes.c_void_p]

oleacc.AccessibleObjectFromWindow.argtypes = [wintypes.HWND, wintypes.DWORD, POINTER(wintypes.BYTE * 16), POINTER(ctypes.c_void_p)]
oleacc.AccessibleObjectFromWindow.restype = ctypes.HRESULT
