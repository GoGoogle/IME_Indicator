#pragma execution_character_set("utf-8") // 强制 MSVC 使用 UTF-8 处理字符串，解决乱码
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <imm.h>
#include <shellapi.h>

// --- GDI+ 类型声明 ---
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

// --- GDI+ 函数导入 ---
__declspec(dllimport) int __stdcall GdiplusStartup(ULONG_PTR*, const struct GdiplusStartupInput*, void*);
__declspec(dllimport) void __stdcall GdiplusShutdown(ULONG_PTR);
__declspec(dllimport) int __stdcall GdipCreateFromHDC(HDC, GpGraphics**);
__declspec(dllimport) int __stdcall GdipDeleteGraphics(GpGraphics*);
__declspec(dllimport) int __stdcall GdipSetSmoothingMode(GpGraphics*, SmoothingMode);
__declspec(dllimport) int __stdcall GdipCreateSolidFill(ARGB, GpSolidFill**);
__declspec(dllimport) int __stdcall GdipDeleteBrush(GpBrush*);
__declspec(dllimport) int __stdcall GdipFillEllipse(GpGraphics*, GpBrush*, REAL, REAL, REAL, REAL);

#define INDICATOR_SIZE 12
#define COLOR_CN       0xA0FF7800 
#define COLOR_EN       0x300078FF 
#define COLOR_CAPS     0xA000FF00 
#define ID_TRAY_EXIT   2001
#define ID_TRAY_RESTART 2002
#define WM_TRAYICON    (WM_USER + 100)

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "shell32.lib")

HWND g_hwnd;
ULONG_PTR g_token;
NOTIFYICONDATAW g_nid = {0};
UINT g_uShellRestart;

// 改进的状态获取：跨进程更稳定
void GetState(unsigned int* color) {
    HWND fore = GetForegroundWindow();
    BOOL cn = FALSE;
    if (fore) {
        HWND ime = ImmGetDefaultIMEWnd(fore);
        DWORD_PTR open = 0;
        if (SendMessageTimeoutW(ime, 0x283, 0x005, 0, SMTO_ABORTIFHUNG, 30, &open) && open) {
            DWORD_PTR mode = 0;
            SendMessageTimeoutW(ime, 0x283, 0x001, 0, SMTO_ABORTIFHUNG, 30, &mode);
            cn = (mode & 1);
        }
    }
    BOOL caps = (GetKeyState(VK_CAPITAL) & 1);
    *color = caps ? COLOR_CAPS : (cn ? COLOR_CN : COLOR_EN);
}

// 核心修复：跨进程获取光标位置
void GetTargetPos(POINT* pt) {
    HWND fore = GetForegroundWindow();
    DWORD tid = GetWindowThreadProcessId(fore, NULL);
    GUITHREADINFO gti = { sizeof(GUITHREADINFO) };
    
    // 关键点：传入当前前台窗口的线程 ID
    if (GetGUIThreadInfo(tid, &gti) && gti.hwndCaret) {
        POINT caretPt = { gti.rcCaret.left, gti.rcCaret.top };
        ClientToScreen(gti.hwndCaret, &caretPt);
        pt->x = caretPt.x + 2;
        pt->y = caretPt.y + (gti.rcCaret.bottom - gti.rcCaret.top) + 2;
        if (pt->x > 1 && pt->y > 1) return;
    }
    // 降级到鼠标
    GetCursorPos(pt);
    pt->x += 2;
    pt->y += 20;
}

void Render(POINT pt, unsigned int color) {
    HDC hdc = GetDC(NULL);
    HDC mdc = CreateCompatibleDC(hdc);
    BITMAPINFO bi = { {sizeof(BITMAPINFOHEADER), INDICATOR_SIZE, INDICATOR_SIZE, 1, 32, BI_RGB} };
    void* bits;
    HBITMAP bmp = CreateDIBSection(mdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HGDIOBJ old = SelectObject(mdc, bmp);

    GpGraphics* g; GpSolidFill* b;
    GdipCreateFromHDC(mdc, &g);
    GdipSetSmoothingMode(g, SmoothingModeAntiAlias);
    GdipCreateSolidFill((ARGB)color, &b);
    GdipFillEllipse(g, b, 0, 0, (REAL)INDICATOR_SIZE, (REAL)INDICATOR_SIZE);

    SIZE sz = { INDICATOR_SIZE, INDICATOR_SIZE };
    POINT src = { 0, 0 };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(g_hwnd, hdc, &pt, &sz, mdc, &src, 0, &bf, ULW_ALPHA);

    GdipDeleteBrush(b); GdipDeleteGraphics(g);
    SelectObject(mdc, old); DeleteObject(bmp);
    DeleteDC(mdc); ReleaseDC(NULL, hdc);
}

void AddTrayIcon(HWND hwnd) {
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcscpy(g_nid.szTip, L"IME Indicator (C)");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == g_uShellRestart) { AddTrayIcon(h); return 0; }
    switch (m) {
        case WM_TRAYICON:
            if (l == WM_RBUTTONUP) {
                POINT pt; GetCursorPos(&pt);
                HMENU hMenu = CreatePopupMenu();
                // 使用带 W 的 API 配合编译器指令解决乱码
                AppendMenuW(hMenu, MF_STRING, ID_TRAY_RESTART, L"重启程序 (Restart)");
                AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"退出 (Exit)");
                SetForegroundWindow(h);
                TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, h, NULL);
                DestroyMenu(hMenu);
            }
            break;
        case WM_COMMAND:
            if (LOWORD(w) == ID_TRAY_EXIT) DestroyWindow(h);
            if (LOWORD(w) == ID_TRAY_RESTART) {
                WCHAR path[MAX_PATH];
                GetModuleFileNameW(NULL, path, MAX_PATH);
                ShellExecuteW(NULL, L"open", path, NULL, NULL, SW_SHOW);
                DestroyWindow(h);
            }
            break;
        case WM_DESTROY:
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            PostQuitMessage(0);
            return 0;
    }
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
        POINT targetPt; unsigned int color;
        GetTargetPos(&targetPt);
        GetState(&color);
        Render(targetPt, color);
        Sleep(10); 
    }
cleanup:
    GdiplusShutdown(g_token);
    return 0;
}
