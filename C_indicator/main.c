#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <imm.h>
#include <shellapi.h>

// --- 手动定义 GDI+ 必需的类型和结构体 (避开系统头文件冲突) ---
typedef float REAL;
typedef UINT32 ARGB;
typedef enum { SmoothingModeAntiAlias = 4 } SmoothingMode;
typedef void GpGraphics;
typedef void GpBrush;
typedef void GpSolidFill;

struct GdiplusStartupInput {
    UINT32 GdiplusVersion;
    void* DebugEventCallback;
    BOOL SuppressBackgroundThread;
    BOOL SuppressExternalCodecs;
};

// --- 手动声明 GDI+ 导出函数 (Flat API) ---
__declspec(dllimport) int __stdcall GdiplusStartup(ULONG_PTR*, const struct GdiplusStartupInput*, void*);
__declspec(dllimport) void __stdcall GdiplusShutdown(ULONG_PTR);
__declspec(dllimport) int __stdcall GdipCreateFromHDC(HDC, GpGraphics**);
__declspec(dllimport) int __stdcall GdipDeleteGraphics(GpGraphics*);
__declspec(dllimport) int __stdcall GdipSetSmoothingMode(GpGraphics*, SmoothingMode);
__declspec(dllimport) int __stdcall GdipCreateSolidFill(ARGB, GpSolidFill**);
__declspec(dllimport) int __stdcall GdipDeleteBrush(GpBrush*);
__declspec(dllimport) int __stdcall GdipFillEllipse(GpGraphics*, GpBrush*, REAL, REAL, REAL, REAL);

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
UINT g_uShellRestart;

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

void GetState(unsigned int* color) {
    HWND fore = GetForegroundWindow();
    BOOL cn = FALSE;
    if (fore) {
        HWND ime = ImmGetDefaultIMEWnd(fore);
        DWORD_PTR open = 0;
        if (SendMessageTimeoutW(ime, 0x283, 0x005, 0, SMTO_ABORTIFHUNG, 50, &open)) {
            if (open) {
                DWORD_PTR mode = 0;
                SendMessageTimeoutW(ime, 0x283, 0x001, 0, SMTO_ABORTIFHUNG, 50, &mode);
                cn = (mode & 1);
            }
        }
    }
    BOOL caps = (GetKeyState(VK_CAPITAL) & 1);
    *color = caps ? COLOR_CAPS : (cn ? COLOR_CN : COLOR_EN);
}

void Render(int x, int y, unsigned int color) {
    HDC hdc = GetDC(NULL);
    HDC mdc = CreateCompatibleDC(hdc);
    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = INDICATOR_SIZE;
    bi.bmiHeader.biHeight = INDICATOR_SIZE;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits;
    HBITMAP bmp = CreateDIBSection(mdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HGDIOBJ old = SelectObject(mdc, bmp);

    GpGraphics* g; GpSolidFill* b;
    GdipCreateFromHDC(mdc, &g);
    GdipSetSmoothingMode(g, SmoothingModeAntiAlias);
    GdipCreateSolidFill((ARGB)color, &b);
    GdipFillEllipse(g, b, 0, 0, (REAL)INDICATOR_SIZE, (REAL)INDICATOR_SIZE);

    POINT dst = { x + OFFSET_X, y + OFFSET_Y }, src = { 0, 0 };
    SIZE sz = { INDICATOR_SIZE, INDICATOR_SIZE };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(g_hwnd, hdc, &dst, &sz, mdc, &src, 0, &bf, ULW_ALPHA);

    GdipDeleteBrush(b); GdipDeleteGraphics(g);
    SelectObject(mdc, old); DeleteObject(bmp);
    DeleteDC(mdc); ReleaseDC(NULL, hdc);
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == g_uShellRestart) { AddTrayIcon(h); return 0; }
    if (m == WM_DESTROY) { Shell_NotifyIconW(NIM_DELETE, &g_nid); PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR lp, int ns) {
    struct GdiplusStartupInput si = {1, NULL, FALSE, FALSE};
    GdiplusStartup(&g_token, &si, NULL);
    
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
            if (msg.message == WM_QUIT) goto cleanup;
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        POINT p; GetCursorPos(&p); unsigned int c;
        GetState(&c); Render(p.x, p.y, c);
        Sleep(10);
    }
cleanup:
    GdiplusShutdown(g_token);
    return 0;
}
