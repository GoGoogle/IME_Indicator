#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <imm.h>
#include <shellapi.h>
#include <ole2.h>
#include <uiautomation.h>
#include <msctf.h>

/* ================= GDI+ MINIMAL ================= */

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

__declspec(dllimport) int  __stdcall GdiplusStartup(ULONG_PTR*, const struct GdiplusStartupInput*, void*);
__declspec(dllimport) void __stdcall GdiplusShutdown(ULONG_PTR);
__declspec(dllimport) int  __stdcall GdipCreateFromHDC(HDC, GpGraphics**);
__declspec(dllimport) int  __stdcall GdipDeleteGraphics(GpGraphics*);
__declspec(dllimport) int  __stdcall GdipSetSmoothingMode(GpGraphics*, SmoothingMode);
__declspec(dllimport) int  __stdcall GdipCreateSolidFill(ARGB, GpSolidFill**);
__declspec(dllimport) int  __stdcall GdipDeleteBrush(GpBrush*);
__declspec(dllimport) int  __stdcall GdipFillEllipse(GpGraphics*, GpBrush*, REAL, REAL, REAL, REAL);

/* ================= CONFIG ================= */

#define INDICATOR_SIZE 12

#define COLOR_CN    0xA0FF7800
#define COLOR_EN    0x300078FF
#define COLOR_CAPS  0xA000FF00

#define ID_TRAY_EXIT  5001
#define WM_TRAYICON   (WM_USER + 101)
#define WM_INTERNAL_REDRAW (WM_USER + 200)

/* ================= GLOBALS ================= */

HWND g_hwnd;
ULONG_PTR g_gdiplus;
NOTIFYICONDATAW g_nid;

IUIAutomation* g_uia = NULL;
IUIAutomationFocusChangedEventHandler* g_focusHandler = NULL;

ITfThreadMgr* g_tsfMgr = NULL;
DWORD g_tsfCookie = TF_INVALID_COOKIE;

POINT g_lastPt = { -9999, -9999 };
unsigned int g_lastColor = 0;
volatile BOOL g_needRedraw = TRUE;

/* ================= IME STATE ================= */

void GetState(unsigned int* color)
{
    HWND fg = GetForegroundWindow();
    BOOL cn = FALSE;

    if (fg) {
        HWND ime = ImmGetDefaultIMEWnd(fg);
        if (ime) {
            DWORD_PTR open = 0;
            if (SendMessageTimeoutW(
                    ime, 0x0283, 0x005, 0,
                    SMTO_ABORTIFHUNG, 20, &open) && open) {

                DWORD_PTR mode = 0;
                SendMessageTimeoutW(
                    ime, 0x0283, 0x001, 0,
                    SMTO_ABORTIFHUNG, 20, &mode);
                cn = (mode & 1);
            }
        }
    }

    if (GetKeyState(VK_CAPITAL) & 1)
        *color = COLOR_CAPS;
    else
        *color = cn ? COLOR_CN : COLOR_EN;
}

/* ================= UIA CARET ================= */

BOOL GetCaretPos_UIA(POINT* pt)
{
    if (!g_uia) return FALSE;

    IUIAutomationElement* el = NULL;
    if (FAILED(g_uia->lpVtbl->GetFocusedElement(g_uia, &el)) || !el)
        return FALSE;

    IUIAutomationTextPattern2* tp2 = NULL;
    HRESULT hr = el->lpVtbl->GetCurrentPattern(
        el, UIA_TextPattern2Id, (IUnknown**)&tp2);

    if (FAILED(hr) || !tp2) {
        el->lpVtbl->Release(el);
        return FALSE;
    }

    IUIAutomationTextRange* range = NULL;
    BOOL active = FALSE;
    hr = tp2->lpVtbl->GetCaretRange(tp2, &active, &range);

    if (FAILED(hr) || !range) {
        tp2->lpVtbl->Release(tp2);
        el->lpVtbl->Release(el);
        return FALSE;
    }

    SAFEARRAY* rects = NULL;
    range->lpVtbl->GetBoundingRectangles(range, &rects);

    if (rects && rects->rgsabound[0].cElements >= 4) {
        double* p;
        SafeArrayAccessData(rects, (void**)&p);
        pt->x = (LONG)p[0] + 2;
        pt->y = (LONG)(p[1] + p[3]) + 2;
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

/* ================= CARET FALLBACK ================= */

void GetTargetPos(POINT* pt)
{
    if (GetCaretPos_UIA(pt))
        return;

    HWND fg = GetForegroundWindow();
    GUITHREADINFO gti = { sizeof(GUITHREADINFO) };
    if (fg &&
        GetGUIThreadInfo(GetWindowThreadProcessId(fg, NULL), &gti) &&
        gti.hwndCaret &&
        !IsRectEmpty(&gti.rcCaret)) {

        POINT cp = { gti.rcCaret.left, gti.rcCaret.bottom };
        ClientToScreen(gti.hwndCaret, &cp);
        pt->x = cp.x + 2;
        pt->y = cp.y + 2;
        return;
    }

    GetCursorPos(pt);
    pt->x += 2;
    pt->y += 20;
}

/* ================= RENDER ================= */

void Render(const POINT* pt, unsigned int color)
{
    HDC hdc = GetDC(NULL);
    HDC mdc = CreateCompatibleDC(hdc);

    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = INDICATOR_SIZE;
    bi.bmiHeader.biHeight = INDICATOR_SIZE;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = NULL;
    HBITMAP bmp = CreateDIBSection(mdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HGDIOBJ old = SelectObject(mdc, bmp);

    GpGraphics* g = NULL;
    GpSolidFill* b = NULL;
    GdipCreateFromHDC(mdc, &g);
    GdipSetSmoothingMode(g, SmoothingModeAntiAlias);
    GdipCreateSolidFill((ARGB)color, &b);
    GdipFillEllipse(g, b, 0, 0,
                    (REAL)INDICATOR_SIZE,
                    (REAL)INDICATOR_SIZE);

    SIZE sz = { INDICATOR_SIZE, INDICATOR_SIZE };
    POINT src = { 0, 0 };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

    UpdateLayeredWindow(
        g_hwnd, hdc, (POINT*)pt, &sz,
        mdc, &src, 0, &bf, ULW_ALPHA);

    GdipDeleteBrush(b);
    GdipDeleteGraphics(g);
    SelectObject(mdc, old);
    DeleteObject(bmp);
    DeleteDC(mdc);
    ReleaseDC(NULL, hdc);
}

/* ================= UIA FOCUS EVENT ================= */

typedef struct {
    IUIAutomationFocusChangedEventHandler iface;
    LONG ref;
} FocusHandler;

HRESULT STDMETHODCALLTYPE FH_QueryInterface(
    IUIAutomationFocusChangedEventHandler* self,
    REFIID riid, void** ppv)
{
    if (IsEqualIID(riid, &IID_IUnknown) ||
        IsEqualIID(riid, &IID_IUIAutomationFocusChangedEventHandler)) {
        *ppv = self;
        ((FocusHandler*)self)->ref++;
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE FH_AddRef(IUIAutomationFocusChangedEventHandler* self)
{
    return InterlockedIncrement(&((FocusHandler*)self)->ref);
}

ULONG STDMETHODCALLTYPE FH_Release(IUIAutomationFocusChangedEventHandler* self)
{
    LONG r = InterlockedDecrement(&((FocusHandler*)self)->ref);
    if (!r) HeapFree(GetProcessHeap(), 0, self);
    return r;
}

HRESULT STDMETHODCALLTYPE FH_HandleFocusChangedEvent(
    IUIAutomationFocusChangedEventHandler* self,
    IUIAutomationElement* sender)
{
    PostMessageW(g_hwnd, WM_INTERNAL_REDRAW, 0, 0);
    return S_OK;
}

IUIAutomationFocusChangedEventHandlerVtbl g_focusVtbl = {
    FH_QueryInterface,
    FH_AddRef,
    FH_Release,
    FH_HandleFocusChangedEvent
};

/* ================= TSF SINK ================= */

typedef struct {
    ITfUIElementSink iface;
    LONG ref;
} TsfSink;

HRESULT STDMETHODCALLTYPE TS_QueryInterface(ITfUIElementSink* self, REFIID riid, void** ppv)
{
    if (IsEqualIID(riid, &IID_IUnknown) ||
        IsEqualIID(riid, &IID_ITfUIElementSink)) {
        *ppv = self;
        ((TsfSink*)self)->ref++;
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE TS_AddRef(ITfUIElementSink* self)
{
    return InterlockedIncrement(&((TsfSink*)self)->ref);
}

ULONG STDMETHODCALLTYPE TS_Release(ITfUIElementSink* self)
{
    LONG r = InterlockedDecrement(&((TsfSink*)self)->ref);
    if (!r) HeapFree(GetProcessHeap(), 0, self);
    return r;
}

HRESULT STDMETHODCALLTYPE TS_BeginUIElement(ITfUIElementSink* self, DWORD id, BOOL* show)
{
    PostMessageW(g_hwnd, WM_INTERNAL_REDRAW, 0, 0);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE TS_UpdateUIElement(ITfUIElementSink* self, DWORD id)
{
    PostMessageW(g_hwnd, WM_INTERNAL_REDRAW, 0, 0);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE TS_EndUIElement(ITfUIElementSink* self, DWORD id)
{
    PostMessageW(g_hwnd, WM_INTERNAL_REDRAW, 0, 0);
    return S_OK;
}

ITfUIElementSinkVtbl g_tsfVtbl = {
    TS_QueryInterface,
    TS_AddRef,
    TS_Release,
    TS_BeginUIElement,
    TS_UpdateUIElement,
    TS_EndUIElement
};

/* ================= WINDOW ================= */

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_INTERNAL_REDRAW) {
        g_needRedraw = TRUE;
        return 0;
    }

    if (m == WM_TRAYICON && l == WM_RBUTTONUP) {
        POINT p; GetCursorPos(&p);
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"退出 (Exit)");
        SetForegroundWindow(h);
        TrackPopupMenu(menu, TPM_RIGHTBUTTON, p.x, p.y, 0, h, NULL);
        DestroyMenu(menu);
        return 0;
    }

    if (m == WM_COMMAND && LOWORD(w) == ID_TRAY_EXIT) {
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

/* ================= ENTRY ================= */

int WINAPI WinMain(HINSTANCE hi, HINSTANCE, LPSTR, int)
{
    CoInitialize(NULL);

    CoCreateInstance(&CLSID_CUIAutomation, NULL,
        CLSCTX_INPROC_SERVER,
        &IID_IUIAutomation,
        (void**)&g_uia);

    if (g_uia) {
        FocusHandler* fh = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(FocusHandler));
        fh->iface.lpVtbl = &g_focusVtbl;
        fh->ref = 1;
        g_focusHandler = (IUIAutomationFocusChangedEventHandler*)fh;
        g_uia->lpVtbl->AddFocusChangedEventHandler(g_uia, NULL, g_focusHandler);
    }

    CoCreateInstance(&CLSID_TF_ThreadMgr, NULL,
        CLSCTX_INPROC_SERVER,
        &IID_ITfThreadMgr,
        (void**)&g_tsfMgr);

    if (g_tsfMgr) {
        TfClientId cid;
        g_tsfMgr->lpVtbl->Activate(g_tsfMgr, &cid);
        TsfSink* sink = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(TsfSink));
        sink->iface.lpVtbl = &g_tsfVtbl;
        sink->ref = 1;
        ITfSource* src;
        if (SUCCEEDED(g_tsfMgr->lpVtbl->QueryInterface(g_tsfMgr, &IID_ITfSource, (void**)&src))) {
            src->lpVtbl->AdviseSink(src, &IID_ITfUIElementSink, &sink->iface, &g_tsfCookie);
            src->lpVtbl->Release(src);
        }
    }

    struct GdiplusStartupInput si = { 1, NULL, FALSE, FALSE };
    GdiplusStartup(&g_gdiplus, &si, NULL);

    WNDCLASSEXW wc = { sizeof(wc), 0, WndProc, 0, 0, hi, NULL, NULL, NULL, NULL, L"IME_IND", NULL };
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT |
        WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        L"IME_IND", L"", WS_POPUP,
        0, 0, INDICATOR_SIZE, INDICATOR_SIZE,
        NULL, NULL, hi, NULL);

    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    lstrcpyW(g_nid.szTip, L"IME 状态指示器");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    ShowWindow(g_hwnd, SW_SHOW);

    MSG msg;
    while (1) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto exit;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (g_needRedraw) {
            POINT pt;
            unsigned int color;
            GetTargetPos(&pt);
            GetState(&color);

            if (pt.x != g_lastPt.x ||
                pt.y != g_lastPt.y ||
                color != g_lastColor) {

                Render(&pt, color);
                g_lastPt = pt;
                g_lastColor = color;
            }
            g_needRedraw = FALSE;
        }
        Sleep(50);
    }

exit:
    if (g_uia && g_focusHandler)
        g_uia->lpVtbl->RemoveFocusChangedEventHandler(g_uia, g_focusHandler);

    if (g_tsfMgr && g_tsfCookie != TF_INVALID_COOKIE) {
        ITfSource* src;
        if (SUCCEEDED(g_tsfMgr->lpVtbl->QueryInterface(g_tsfMgr, &IID_ITfSource, (void**)&src))) {
            src->lpVtbl->UnadviseSink(src, g_tsfCookie);
            src->lpVtbl->Release(src);
        }
    }

    if (g_tsfMgr) g_tsfMgr->lpVtbl->Release(g_tsfMgr);
    if (g_uia) g_uia->lpVtbl->Release(g_uia);

    CoUninitialize();
    GdiplusShutdown(g_gdiplus);
    return 0;
}
