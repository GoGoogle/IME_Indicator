#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <imm.h>
#include <shellapi.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "shell32.lib")

// ==================== 基础定义 ====================
#define IND_W          15      // 基础图标宽度
#define IND_H          15      // 基础图标高度

// 状态颜色 (BGR格式)
#define COLOR_CN       0x000078FF   // 中文 - 橙色
#define COLOR_EN       0x00FF7800   // 英文 - 蓝色
#define COLOR_CAPS     0x0000C800   // 大写锁定 - 绿色

#define WM_TRAYICON    (WM_USER + 1)
#define ID_ABOUT       1001
#define ID_EXIT        1002

// DPI相关兼容定义
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

// ==================== 全局变量 ====================
static HWND         g_hwnd;
static NOTIFYICONDATAW g_nid = {0};

// 渲染缓存：用于判断是否需要真正重绘
static COLORREF      g_lastColor = 0;
static WCHAR         g_lastChar  = 0;
static POINT         g_lastPos   = { -10000, -10000 }; 
static BOOL          g_lastHasCaret = FALSE;
static int           g_lastDpi = 0;

// ==================== Base64 解码 + 创建图标 ====================
HICON LoadMyIcon(const char* base64)
{
    int len = (int)strlen(base64);
    BYTE* buf = (BYTE*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len);
    if (!buf) return LoadIcon(NULL, IDI_APPLICATION);

    int decodeTable[256] = {0};
    const char* b64chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i = 0; i < 64; i++) decodeTable[(unsigned char)b64chars[i]] = i;

    int outIdx = 0, value = 0, bits = 0;
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)base64[i];
        if (c == '=') break;
        if (c <= 32) continue; // 跳过空白字符
        value = (value << 6) | decodeTable[c];
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            buf[outIdx++] = (BYTE)((value >> bits) & 0xFF);
        }
    }
    // 创建图标 (16x16)
    HICON hIcon = CreateIconFromResourceEx(buf, outIdx, TRUE, 0x00030000, 16, 16, LR_DEFAULTCOLOR);
    HeapFree(GetProcessHeap(), 0, buf);
    return hIcon ? hIcon : LoadIcon(NULL, IDI_APPLICATION);
}

// ==================== 启用高DPI支持 ====================
void EnableHighDPI(void)
{
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    if (hUser) {
        typedef BOOL(WINAPI* PSetDpi)(DPI_AWARENESS_CONTEXT);
        PSetDpi pfn = (PSetDpi)GetProcAddress(hUser, "SetProcessDpiAwarenessContext");
        if (pfn && pfn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return;
    }
    SetProcessDPIAware();
}

// ==================== 查询状态 (精准识别) ====================
WCHAR QueryInputState(COLORREF* outColor)
{
    if (GetKeyState(VK_CAPITAL) & 1) {
        *outColor = COLOR_CAPS;
        return L'A';
    }

    HWND fgWnd = GetForegroundWindow();
    if (!fgWnd) { *outColor = COLOR_EN; return L'E'; }

    HWND imeWnd = ImmGetDefaultIMEWnd(fgWnd);
    DWORD_PTR isOpen = 0, convMode = 0;

    // 增加超时控制，防止某些卡死的程序导致指示器假死
    if (imeWnd && SendMessageTimeoutW(imeWnd, WM_IME_CONTROL, 0x005, 0, SMTO_ABORTIFHUNG, 50, &isOpen) && isOpen) {
        SendMessageTimeoutW(imeWnd, WM_IME_CONTROL, 0x001, 0, SMTO_ABORTIFHUNG, 50, &convMode);
        if (convMode & IME_CMODE_NATIVE) {
            *outColor = COLOR_CN;
            return L'中';
        }
    }

    *outColor = COLOR_EN;
    return L'E';
}

// ==================== 核心渲染 (带资源持久化优化) ====================
void RenderIndicator(void)
{
    UINT dpi = GetDpiForWindow(g_hwnd);
    if (dpi == 0) dpi = 96;
    double scale = (double)dpi / 96.0;

    int width = (int)(IND_W * scale);
    int height = (int)(IND_H * scale);

    // 1. 获取位置
    POINT targetPt = {0};
    BOOL hasValidCaret = FALSE;
    HWND fg = GetForegroundWindow();

    if (fg) {
        GUITHREADINFO gti = { sizeof(gti) };
        if (GetGUIThreadInfo(GetWindowThreadProcessId(fg, NULL), &gti) && gti.hwndCaret) {
            if (gti.rcCaret.bottom > gti.rcCaret.top) {
                POINT caretScreen = { gti.rcCaret.left, gti.rcCaret.bottom };
                ClientToScreen(gti.hwndCaret, &caretScreen);
                if (MonitorFromPoint(caretScreen, MONITOR_DEFAULTTONULL)) {
                    targetPt.x = caretScreen.x + (gti.rcCaret.right - gti.rcCaret.left) / 2 - width / 2;
                    targetPt.y = caretScreen.y + (int)(2 * scale);
                    hasValidCaret = TRUE;
                }
            }
        }
    }

    if (!hasValidCaret) {
        GetCursorPos(&targetPt);
        targetPt.x += (int)(20 * scale);
        targetPt.y += (int)(20 * scale);
    }

    // 2. 状态检查
    COLORREF curColor;
    WCHAR curChar = QueryInputState(&curColor);

    // 3. 性能优化：位置或状态未显著变化时不重绘
    if (curChar == g_lastChar && curColor == g_lastColor && 
        abs(targetPt.x - g_lastPos.x) < 2 && abs(targetPt.y - g_lastPos.y) < 2 &&
        hasValidCaret == g_lastHasCaret && dpi == g_lastDpi) {
        return; 
    }

    g_lastChar = curChar; g_lastColor = curColor; g_lastPos = targetPt; 
    g_lastHasCaret = hasValidCaret; g_lastDpi = dpi;

    // 4. 执行绘制
    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    
    BITMAPINFO bmi = { {sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB} };
    void* bits = NULL;
    HBITMAP hBmp = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    HGDIOBJ oldBmp = SelectObject(hdcMem, hBmp);

    // 清空背景并画圆
    memset(bits, 0, width * height * 4);
    HBRUSH br = CreateSolidBrush(curColor);
    HPEN pen = CreatePen(PS_SOLID, 1, curColor);
    SelectObject(hdcMem, br);
    SelectObject(hdcMem, pen);
    Ellipse(hdcMem, 0, 0, width - 1, height - 1);

    // 绘制文字
    SetBkMode(hdcMem, TRANSPARENT);
    SetTextColor(hdcMem, RGB(255, 255, 255));
    // 修正字号计算：基础高度 16 在 96DPI 下
    int fontSize = -MulDiv(12, dpi, 96); 
    HFONT font = CreateFontW(fontSize, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 
                             0, 0, CLEARTYPE_QUALITY, 0, L"Arial");
    HGDIOBJ oldFont = SelectObject(hdcMem, font);
    
    RECT rc = {0, 0, width, height};
    WCHAR text[2] = { curChar, 0 };
    DrawTextW(hdcMem, text, 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // 修复 Alpha 通道：LayeredWindow 需要 alpha 值为 0xFF 才能显示
    DWORD* p = (DWORD*)bits;
    for (int i = 0; i < width * height; i++) { if (p[i] != 0) p[i] |= 0xFF000000; }

    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    SIZE winSize = { width, height };
    POINT srcPt = { 0, 0 };
    UpdateLayeredWindow(g_hwnd, hdcScreen, &targetPt, &winSize, hdcMem, &srcPt, 0, &bf, ULW_ALPHA);

    // 清理资源
    SelectObject(hdcMem, oldFont); DeleteObject(font);
    SelectObject(hdcMem, oldBmp);  DeleteObject(hBmp);
    DeleteObject(pen); DeleteObject(br);
    DeleteDC(hdcMem); ReleaseDC(NULL, hdcScreen);
}

// ==================== 窗口过程 ====================
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_TRAYICON && (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP)) {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, ID_ABOUT, L"关于 (About)");
        AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(menu, MF_STRING, ID_EXIT, L"退出 (Exit)");
        POINT pt; GetCursorPos(&pt);
        SetForegroundWindow(hWnd);
        TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
        DestroyMenu(menu);
        return 0;
    }
    if (msg == WM_COMMAND) {
        if (LOWORD(wParam) == ID_ABOUT) {
            MessageBoxW(hWnd, 
                L"输入指示器 (IME Indicator) V5.x\n\n"
                L"功能：根据 DPI 自动缩放大小，支持光标/鼠标跟随。\n"
                L"英文状态：蓝底白字 E\n"
                L"中文状态：橙底白字 中\n"
                L"大写锁定：绿底白字 A\n\n"
                L"2026.01.12 深度优化版 By LC & Grok & Gemini", 
                L"关于", MB_OK | MB_ICONINFORMATION);
        } else if (LOWORD(wParam) == ID_EXIT) DestroyWindow(hWnd);
        return 0;
    }
    if (msg == WM_TIMER) RenderIndicator();
    if (msg == WM_DESTROY) { 
        Shell_NotifyIconW(NIM_DELETE, &g_nid); 
        PostQuitMessage(0); 
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ==================== 主入口 ====================
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
    EnableHighDPI();
    
    // 图标 Base64 数据 (保持您的原始数据)
    const char* iconB64 = "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";

    HICON hIcon = LoadMyIcon(iconB64);

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"IME_V5_OPTIMIZED";
    wc.hIcon = hIcon;
    wc.hIconSm = hIcon;
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                             wc.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, NULL, NULL, hInst, NULL);

    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = hIcon;
    wcscpy(g_nid.szTip, L"输入指示器 (IME Indicator)");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    SetTimer(g_hwnd, 1, 15, NULL); // 约 60FPS 的检测频率

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
