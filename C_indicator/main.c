#define UNICODE
#define _UNICODE
#include <windows.h>
#include <gdiplus.h>
#include <imm.h>

// --- 配置区 ---
#define INDICATOR_SIZE 12
#define COLOR_CN       0xA0FF7800 
#define COLOR_EN       0x300078FF 
#define COLOR_CAPS     0xA000FF00 
#define OFFSET_X       2
#define OFFSET_Y       20

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "shell32.lib")

HWND g_hwnd;
ULONG_PTR g_token;
NOTIFYICONDATAW g_nid = {0};
UINT g_uShellRestart; // 任务栏重启消息 ID

// 托盘图标添加函数（抽离出来以便重用）
void AddTrayIcon(HWND hwnd) {
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_USER + 100;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcscpy(g_nid.szTip, L"IME Indicator (C)");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void GetState(ARGB* color) {
    HWND fore = GetForegroundWindow();
    BOOL cn = FALSE;
    if (fore) {
        HWND ime = ImmGetDefaultIMEWnd(fore);
        LRESULT open = 0;
        if (SendMessageTimeoutW(ime, 0x283, 0x005, 0, SMTO_ABORTIFHUNG, 50, (PDWORD_PTR)&open)) {
            if (open) {
                LRESULT mode = SendMessageTimeoutW(ime, 0x283, 0x001, 0, SMTO_ABORTIFHUNG, 50, (PDWORD_PTR)&cn);
                cn = (cn & 1);
            }
        }
    }
    BOOL caps = (GetKeyState(VK_CAPITAL) & 1);
    *color = caps ? COLOR_CAPS : (cn ? COLOR_CN : COLOR_EN);
}

void Render(int x, int y, ARGB color) {
    HDC hdc = GetDC(NULL);
    if (!hdc) return; // 防御检查
    HDC mdc = CreateCompatibleDC(hdc);
    
    BITMAPINFO bi = { {sizeof(BITMAPINFOHEADER), INDICATOR_SIZE, INDICATOR_SIZE, 1, 32, BI_RGB} };
    void* bits;
    HBITMAP bmp = CreateDIBSection(mdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp) {
        HGDIOBJ old = SelectObject(mdc, bmp);
        GpGraphics* g; GpSolidFill* b;
        GdipCreateFromHDC(mdc, &g);
        GdipSetSmoothingMode(g, SmoothingModeAntiAlias);
        GdipCreateSolidFill(color, &b);
        GdipFillEllipse(g, b, 0, 0, (float)INDICATOR_SIZE, (float)INDICATOR_SIZE);

        POINT dst = { x + OFFSET_X, y + OFFSET_Y }, src = { 0, 0 };
        SIZE sz = { INDICATOR_SIZE, INDICATOR_SIZE };
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        UpdateLayeredWindow(g_hwnd, hdc, &dst, &sz, mdc, &src, 0, &bf, ULW_ALPHA);

        GdipDeleteBrush(b); GdipDeleteGraphics(g);
        SelectObject(mdc, old);
        DeleteObject(bmp);
    }
    DeleteDC(mdc); ReleaseDC(NULL, hdc);
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    // 关键：如果 Windows 资源管理器重启（唤醒后常见），重新添加图标
    if (m == g_uShellRestart) {
        AddTrayIcon(h);
        return 0;
    }
    switch (m) {
        case WM_DESTROY:
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            PostQuitMessage(0);
            return 0;
        case WM_POWERBROADCAST:
            // 收到电源状态变化消息（如从睡眠中恢复 PBT_APMRESUMEAUTOMATIC）
            if (w == 0x0012) { 
                InvalidateRect(g_hwnd, NULL, TRUE); // 强制重绘
            }
            break;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR lp, int ns) {
    GdiplusStartupInput si = {1}; GdiplusStartup(&g_token, &si, NULL);
    
    // 注册任务栏重启消息
    g_uShellRestart = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wc = {sizeof(WNDCLASSEXW), 0, WndProc, 0, 0, hi, NULL, NULL, NULL, NULL, L"IC", NULL};
    RegisterClassExW(&wc);
    g_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, 
                             L"IC", L"", WS_POPUP, 0, 0, 0, 0, NULL, NULL, hi, NULL);
    ShowWindow(g_hwnd, SW_SHOW);

    AddTrayIcon(g_hwnd);

    MSG msg;
    while (1) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return 0;
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        POINT p; GetCursorPos(&p); ARGB c;
        GetState(&c); Render(p.x, p.y, c);
        Sleep(10);
    }
    GdiplusShutdown(g_token);
    return 0;
}
