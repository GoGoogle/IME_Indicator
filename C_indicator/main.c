#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <imm.h>
#include <ole2.h>
#include <oleauto.h>   /* ← 关键：SAFEARRAY */
#include <uiautomation.h>
#include <shellapi.h>

/* ---------------- GDI+ minimal ---------------- */
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

__declspec(dllimport) int __stdcall GdiplusStartup(ULONG_PTR*, const struct GdiplusStartupInput*, void*);
__declspec(dllimport) void __stdcall GdiplusShutdown(ULONG_PTR);
__declspec(dllimport) int __stdcall GdipCreateFromHDC(HDC, GpGraphics**);
__declspec(dllimport) int __stdcall GdipDeleteGraphics(GpGraphics*);
__declspec(dllimport) int __stdcall GdipSetSmoothingMode(GpGraphics*, SmoothingMode);
__declspec(dllimport) int __stdcall GdipCreateSolidFill(ARGB, GpSolidFill**);
__declspec(dllimport) int __stdcall GdipDeleteBrush(GpBrush*);
__declspec(dllimport) int __stdcall GdipFillEllipse(GpGraphics*, GpBrush*, REAL, REAL, REAL, REAL);

/* ---------------- config ---------------- */
#define INDICATOR_SIZE 12
#define COLOR_CN   0xA0FF7800
#define COLOR_EN   0x300078FF
#define COLOR_CAPS 0xA000FF00

#define WM_TRAYICON (WM_USER + 100)
#define ID_EXIT     1

#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"gdiplus.lib")
#pragma comment(lib,"imm32.lib")
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"oleaut32.lib")
#pragma comment(lib,"shell32.lib")

/* ---------------- globals ---------------- */
HWND g_hwnd;
ULONG_PTR g_gdiplus;
NOTIFYICONDATAW g_nid;
IUIAutomation* g_uia = NULL;

POINT g_lastPt = { -1,-1 };
UINT  g_lastColor = 0;

/* ---------------- IME state ---------------- */
UINT GetImeColor(void) {
    HWND fg = GetForegroundWindow();
    BOOL cn = FALSE;

    if (fg) {
        HWND ime = ImmGetDefaultIMEWnd(fg);
        DWORD_PTR open = 0, mode = 0;
        SendMessageTimeoutW(ime, 0x0283, 0x005, 0, SMTO_ABORTIFHUNG, 20, &open);
        if (open) {
            SendMessageTimeoutW(ime, 0x0283, 0x001, 0, SMTO_ABORTIFHUNG, 20, &mode);
            cn = (mode & 1);
        }
    }
    if (GetKeyState(VK_CAPITAL) & 1) return COLOR_CAPS;
    return cn ? COLOR_CN : COLOR_EN;
}

/* ---------------- caret via UIA TextPattern2 ---------------- */
BOOL GetCaretFromUIA(POINT* pt) {
    IUIAutomationElement* el = NULL;
    if (FAILED(g_uia->lpVtbl->GetFocusedElement(g_uia, &el)) || !el)
        return FALSE;

    IUIAutomationTextPattern2* tp2 = NULL;
    if (FAILED(el->lpVtbl->GetCurrentPattern(
        el, UIA_TextPattern2Id, (IUnknown**)&tp2)) || !tp2) {
        el->lpVtbl->Release(el);
        return FALSE;
    }

    IUIAutomationTextRange* range = NULL;
    if (FAILED(tp2->lpVtbl->GetCaretRange(tp2, NULL, &range)) || !range) {
        tp2->lpVtbl->Release(tp2);
        el->lpVtbl->Release(el);
        return FALSE;
    }

    SAFEARRAY* rects = NULL;
    if (SUCCEEDED(range->lpVtbl->GetBoundingRectangles(range, &rects)) &&
        rects && rects->rgsabound[0].cElements >= 4) {

        double* d = NULL;
        SafeArrayAccessData(rects, (void**)&d);
        pt->x = (LONG)d[0] + 2;
        pt->y = (LONG)(d[1] + d[3]) + 2;
        SafeArrayUnaccessData(rects);
        SafeArrayDestroy(rects);

        range->lpVtbl->Release(range);
        tp2->lpVtbl->Release(tp2);
        el->lpVtbl->Release(el);
        return TRUE;
    }

    if (rects) SafeArrayDestroy(rects);
    range->lpVtbl->Release(range);
    tp2->lpVtbl->Release(tp2);
    el->lpVtbl->Release(el);
    return FALSE;
}

/* ---------------- fallback caret ---------------- */
void GetCaretFallback(POINT* pt) {
    GUITHREADINFO gti = { sizeof(gti) };
    HWND fg = GetForegroundWindow();
    if (fg &&
        GetGUIThreadInfo(GetWindowThreadProcessId(fg, NULL), &gti) &&
        gti.hwndCaret) {
        POINT p = { gti.rcCaret.left, gti.rcCaret.bottom };
        ClientToScreen(gti.hwndCaret, &p);
        *pt = p;
        return;
    }
    GetCursorPos(pt);
    pt->y += 18;
}

/* ---------------- render ---------------- */
void RenderIndicator(POINT pt, UINT color) {
    if (pt.x == g_lastPt.x && pt.y == g_lastPt.y && color == g_lastColor)
        return;

    g_lastPt = pt;
    g_lastColor = color;

    HDC hdc = GetDC(NULL);
    HDC mdc = CreateCompatibleDC(hdc);

    BITMAPINFO bi = { 0 };
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = INDICATOR_SIZE;
    bi.bmiHeader.biHeight = INDICATOR_SIZE;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits;
    HBITMAP bmp = CreateDIBSection(mdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HGDIOBJ old = SelectObject(mdc, bmp);

    GpGraphics* g;
    GpSolidFill* b;
    GdipCreateFromHDC(mdc, &g);
    GdipSetSmoothingMode(g, SmoothingModeAntiAlias);
    GdipCreateSolidFill(color, &b);
    GdipFillEllipse(g, b, 0, 0, INDICATOR_SIZE, INDICATOR_SIZE);

    SIZE sz = { INDICATOR_SIZE, INDICATOR_SIZE };
    POINT src = { 0,0 };
    BLENDFUNCTION bf = { AC_SRC_OVER,0,255,AC_SRC_ALPHA };
    UpdateLayeredWindow(g_hwnd, hdc, &pt, &sz, mdc, &src, 0, &bf, ULW_ALPHA);

    GdipDeleteBrush(b);
    GdipDeleteGraphics(g);
    SelectObject(mdc, old);
    DeleteObject(bmp);
    DeleteDC(mdc);
    ReleaseDC(NULL, hdc);
}

/* ---------------- WinEvent hook ---------------- */
void CALLBACK WinEventProc(
    HWINEVENTHOOK h, DWORD ev, HWND hwnd,
    LONG idObj, LONG idChild,
    DWORD tid, DWORD time) {

    POINT pt;
    if (!GetCaretFromUIA(&pt))
        GetCaretFallback(&pt);

    RenderIndicator(pt, GetImeColor());
}

/* ---------------- window ---------------- */
LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_TRAYICON && l == WM_RBUTTONUP) {
        HMENU mnu = CreatePopupMenu();
        AppendMenuW(mnu, MF_STRING, ID_EXIT, L"Exit");
        POINT p; GetCursorPos(&p);
        SetForegroundWindow(h);
        TrackPopupMenu(mnu, TPM_RIGHTBUTTON, p.x, p.y, 0, h, NULL);
        DestroyMenu(mnu);
        return 0;
    }
    if (m == WM_COMMAND && LOWORD(w) == ID_EXIT) {
        DestroyWindow(h);
        return 0;
    }
    if (m == WM_DESTROY) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

/* ---------------- entry ---------------- */
int WINAPI WinMain(HINSTANCE hi, HINSTANCE, LPSTR, int) {
    CoInitialize(NULL);
    CoCreateInstance(&CLSID_CUIAutomation, NULL,
        CLSCTX_INPROC_SERVER, &IID_IUIAutomation, (void**)&g_uia);

    struct GdiplusStartupInput si = { 1,NULL,FALSE,FALSE };
    GdiplusStartup(&g_gdiplus, &si, NULL);

    WNDCLASSEXW wc = { sizeof(wc),0,WndProc,0,0,hi };
    wc.lpszClassName = L"IME_IND";
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT |
        WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        wc.lpszClassName, L"",
        WS_POPUP, 0, 0, 0, 0,
        NULL, NULL, hi, NULL);

    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    SetWinEventHook(EVENT_OBJECT_FOCUS, EVENT_OBJECT_LOCATIONCHANGE,
        NULL, WinEventProc, 0, 0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    ShowWindow(g_hwnd, SW_SHOW);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_uia) g_uia->lpVtbl->Release(g_uia);
    GdiplusShutdown(g_gdiplus);
    CoUninitialize();
    return 0;
}
