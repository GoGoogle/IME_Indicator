#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <imm.h>
#include <shellapi.h>
#include <ole2.h>
#include <uiautomation.h>

// --- GDI+ 极简平面 API 声明 ---
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
#pragma comment(lib, "ole32.lib")

HWND g_hwnd;
ULONG_PTR g_token;
NOTIFYICONDATAW g_nid = {0};
IUIAutomation* g_pAutomation = NULL;

// --- 获取输入法状态颜色 ---
void GetState(unsigned int* color) {
    HWND fore = GetForegroundWindow();
    BOOL cn = FALSE;
    if (fore) {
        HWND ime = ImmGetDefaultIMEWnd(fore);
        DWORD_PTR open = 0;
        if (SendMessageTimeoutW(ime, 0x0283, 0x005, 0, SMTO_ABORTIFHUNG, 20, &open) && open) {
            DWORD_PTR mode = 0;
            SendMessageTimeoutW(ime, 0x0283, 0x001, 0, SMTO_ABORTIFHUNG, 20, &mode);
            cn = (mode & 1);
        }
    }
    *color = (GetKeyState(VK_CAPITAL) & 1) ? COLOR_CAPS : (cn ? COLOR_CN : COLOR_EN);
}

// --- 核心：多层探测光标位置 ---
void GetTargetPos(POINT* pt) {
    // 1. 尝试标准 Win32 光标 (记事本等)
    GUITHREADINFO gti = { sizeof(GUITHREADINFO) };
    if (GetGUIThreadInfo(GetWindowThreadProcessId(GetForegroundWindow(), NULL), &gti) && gti.hwndCaret) {
        POINT cp = { gti.rcCaret.left, gti.rcCaret.bottom };
        ClientToScreen(gti.hwndCaret, &cp);
        pt->x = cp.x + 2; pt->y = cp.y + 2;
        if (pt->x > 5) return;
    }

    // 2. 尝试 UI Automation (Chrome, VS Code 等现代应用)
    if (g_pAutomation) {
        IUIAutomationElement* pEl = NULL;
        if (SUCCEEDED(g_pAutomation->lpVtbl->GetFocusedElement(g_pAutomation, &pEl)) && pEl) {
            RECT rect;
            if (SUCCEEDED(pEl->lpVtbl->get_CurrentBoundingRectangle(pEl, &rect))) {
                // 聚焦在文字元素上，我们取其底部
                pt->x = rect.left; pt->y = rect.bottom;
                pEl->lpVtbl->Release(pEl);
                if (pt->x > 0) return;
            }
            pEl->lpVtbl->Release(pEl);
        }
    }

    // 3. 最终回退到鼠标
    GetCursorPos(pt);
    pt->x += 2; pt->y += 20;
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
    if (m == WM_TRAYICON && l == WM_RBUTTONUP) {
        POINT p; GetCursorPos(&p);
        HMENU hM = CreatePopupMenu();
        AppendMenuW(hM, MF_STRING, ID_TRAY_EXIT, L"\u9000\u51fa (Exit)");
        SetForegroundWindow(h);
        TrackPopupMenu(hM, TPM_RIGHTBUTTON, p.x, p.y, 0, h, NULL);
        DestroyMenu(hM);
    } else if (m == WM_COMMAND && LOWORD(w) == ID_TRAY_EXIT) {
        DestroyWindow(h);
    } else if (m == WM_DESTROY) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR lp, int ns) {
    CoInitialize(NULL);
    CoCreateInstance(&CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER, &IID_IUIAutomation, (void**)&g_pAutomation);

    struct GdiplusStartupInput si = {1, NULL, FALSE, FALSE};
    GdiplusStartup(&g_token, &si, NULL);

    WNDCLASSEXW wc = {sizeof(WNDCLASSEXW), 0, WndProc, 0, 0, hi, NULL, NULL, NULL, NULL, L"IME_IND", NULL};
    RegisterClassExW(&wc);
    g_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, 
                             L"IME_IND", L"", WS_POPUP, 0, 0, 0, 0, NULL, NULL, hi, NULL);

    g_nid.cbSize = sizeof(g_nid); g_nid.hWnd = g_hwnd; g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE; g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    ShowWindow(g_hwnd, SW_SHOW);
    MSG msg;
    while (1) {
        if (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) break;
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        POINT t; unsigned int c;
        GetTargetPos(&t);
        GetState(&c);
        Render(t, c);
        Sleep(15);
    }

    if (g_pAutomation) g_pAutomation->lpVtbl->Release(g_pAutomation);
    CoUninitialize();
    GdiplusShutdown(g_token);
    return 0;
}
