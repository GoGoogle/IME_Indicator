#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <imm.h>
#include <shellapi.h>

// --- GDI+ 平面接口声明 ---
typedef float REAL;
typedef UINT32 ARGB;
typedef enum { SmoothingModeAntiAlias = 4 } SmoothingMode;
typedef void GpGraphics;
typedef void GpBrush;
typedef void GpSolidFill;
struct GdiplusStartupInput { UINT32 GdiplusVersion; void* DebugEventCallback; BOOL SuppressBackgroundThread; BOOL SuppressExternalCodecs; };

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
#define ID_TRAY_EXIT   5001
#define WM_TRAYICON    (WM_USER + 101)

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "shell32.lib")

HWND g_hwnd;
ULONG_PTR g_token;
NOTIFYICONDATAW g_nid = {0};

// --- 精准获取状态 ---
void GetState(unsigned int* color) {
    HWND fore = GetForegroundWindow();
    BOOL cn = FALSE;
    if (fore) {
        HWND ime = ImmGetDefaultIMEWnd(fore);
        if (ime) {
            DWORD_PTR open = 0;
            if (SendMessageTimeoutW(ime, 0x0283, 0x005, 0, SMTO_ABORTIFHUNG, 30, &open) && open) {
                DWORD_PTR mode = 0;
                SendMessageTimeoutW(ime, 0x0283, 0x001, 0, SMTO_ABORTIFHUNG, 30, &mode);
                cn = (mode & 1);
            }
        }
    }
    *color = (GetKeyState(VK_CAPITAL) & 1) ? COLOR_CAPS : (cn ? COLOR_CN : COLOR_EN);
}

// --- 核心：光标焦点捕捉 ---
void GetTargetPos(POINT* pt) {
    HWND fore = GetForegroundWindow();
    DWORD foreTid = GetWindowThreadProcessId(fore, NULL);
    DWORD currTid = GetCurrentThreadId();
    
    GUITHREADINFO gti = { sizeof(GUITHREADINFO) };
    BOOL found = FALSE;

    if (AttachThreadInput(currTid, foreTid, TRUE)) {
        if (GetGUIThreadInfo(foreTid, &gti) && gti.hwndCaret) {
            POINT cp = { gti.rcCaret.left, gti.rcCaret.bottom };
            ClientToScreen(gti.hwndCaret, &cp);
            pt->x = cp.x + 4;
            pt->y = cp.y + 4;
            found = TRUE;
        }
        AttachThreadInput(currTid, foreTid, FALSE);
    }

    if (!found) {
        GetCursorPos(pt);
        pt->x += 2;
        pt->y += 20;
    }
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

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_TRAYICON:
            if (l == WM_RBUTTONUP || l == WM_LBUTTONUP) {
                POINT pt; GetCursorPos(&pt);
                HMENU hMenu = CreatePopupMenu();
                // \u9000\u51fa = 退出
                AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"\u9000\u51fa (Exit)");
                SetForegroundWindow(h); // 必须调用，否则菜单不消失或不响应
                TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, h, NULL);
                DestroyMenu(hMenu);
            }
            break;
        case WM_COMMAND:
            if (LOWORD(w) == ID_TRAY_EXIT) {
                Shell_NotifyIconW(NIM_DELETE, &g_nid);
                PostQuitMessage(0);
            }
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProcW(h, m, w, l);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR lp, int ns) {
    struct GdiplusStartupInput si = {1, NULL, FALSE, FALSE};
    GdiplusStartup(&g_token, &si, NULL);

    WNDCLASSEXW wc = {sizeof(WNDCLASSEXW), 0, WndProc, 0, 0, hi, NULL, NULL, NULL, NULL, L"IME_IND_CLASS", NULL};
    RegisterClassExW(&wc);
    
    g_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, 
                             wc.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, NULL, NULL, hi, NULL);
    
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcscpy(g_nid.szTip, L"IME Indicator (C)");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    ShowWindow(g_hwnd, SW_SHOW);

    MSG msg;
    while (1) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto cleanup;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        POINT target; unsigned int col;
        GetTargetPos(&target);
        GetState(&col);
        Render(target, col);
        Sleep(10);
    }

cleanup:
    GdiplusShutdown(g_token);
    return 0;
}
