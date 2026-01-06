#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <imm.h>
#include <shellapi.h>

#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"imm32.lib")
#pragma comment(lib,"shell32.lib")

#define IND_W 18
#define IND_H 18

#define COLOR_CN   RGB(255,120,0)
#define COLOR_EN   RGB(0,120,255)
#define COLOR_CAPS RGB(0,200,0)

#define WM_TRAYICON (WM_USER + 1)
#define ID_EXIT     1

static HWND g_hwnd;
static NOTIFYICONDATAW g_nid;

static POINT     g_lastPt   = { -1,-1 };
static WCHAR     g_lastCh   = 0;
static COLORREF  g_lastClr  = 0;

/* ---------------- DPI ---------------- */

void EnableDPI(void) {
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    if (u32) {
        typedef BOOL (WINAPI *P)(DPI_AWARENESS_CONTEXT);
        P fn = (P)GetProcAddress(u32, "SetProcessDpiAwarenessContext");
        if (fn) fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
}

/* ---------------- state ---------------- */

WCHAR QueryStateChar(COLORREF* clr) {
    if (GetKeyState(VK_CAPITAL) & 1) {
        *clr = COLOR_CAPS;
        return L'A';
    }

    HWND fg = GetForegroundWindow();
    HWND ime = fg ? ImmGetDefaultIMEWnd(fg) : NULL;

    DWORD_PTR open = 0, mode = 0;
    if (ime) {
        SendMessageTimeoutW(ime, 0x0283, 0x005, 0,
            SMTO_ABORTIFHUNG, 20, &open);
        if (open) {
            SendMessageTimeoutW(ime, 0x0283, 0x001, 0,
                SMTO_ABORTIFHUNG, 20, &mode);
        }
    }

    if (open && (mode & 1)) {
        *clr = COLOR_CN;
        return L'C';
    }

    *clr = COLOR_EN;
    return L'E';
}

/* ---------------- caret ---------------- */

BOOL QueryCaretScreenPos(POINT* pt) {
    GUITHREADINFO gti = { sizeof(gti) };
    HWND fg = GetForegroundWindow();
    if (!fg) return FALSE;

    if (!GetGUIThreadInfo(
        GetWindowThreadProcessId(fg, NULL), &gti))
        return FALSE;

    if (!gti.hwndCaret)
        return FALSE;

    POINT p = { gti.rcCaret.left, gti.rcCaret.bottom };
    ClientToScreen(gti.hwndCaret, &p);
    *pt = p;
    return TRUE;
}

/* ---------------- clamp ---------------- */

void ClampToMonitor(POINT* pt) {
    HMONITOR mon = MonitorFromPoint(*pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(mon, &mi);

    if (pt->x + IND_W > mi.rcWork.right)
        pt->x = mi.rcWork.right - IND_W;
    if (pt->y + IND_H > mi.rcWork.bottom)
        pt->y = mi.rcWork.bottom - IND_H;
    if (pt->x < mi.rcWork.left)
        pt->x = mi.rcWork.left;
    if (pt->y < mi.rcWork.top)
        pt->y = mi.rcWork.top;
}

/* ---------------- render ---------------- */

void Render(POINT pt, WCHAR ch, COLORREF clr) {
    if (pt.x == g_lastPt.x &&
        pt.y == g_lastPt.y &&
        ch   == g_lastCh &&
        clr  == g_lastClr)
        return;

    g_lastPt  = pt;
    g_lastCh  = ch;
    g_lastClr = clr;

    HDC hdc = GetDC(NULL);
    HDC mdc = CreateCompatibleDC(hdc);

    HBITMAP bmp = CreateCompatibleBitmap(hdc, IND_W, IND_H);
    HGDIOBJ old = SelectObject(mdc, bmp);

    HBRUSH bg = CreateSolidBrush(RGB(0,0,0));
    RECT r = {0,0,IND_W,IND_H};
    FillRect(mdc, &r, bg);
    DeleteObject(bg);

    SetBkMode(mdc, TRANSPARENT);
    SetTextColor(mdc, clr);

    HFONT font = CreateFontW(
        -12,0,0,0,FW_BOLD,0,0,0,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,
        DEFAULT_PITCH,L"Segoe UI");

    HGDIOBJ oldf = SelectObject(mdc, font);
    WCHAR s[2] = { ch, 0 };
    DrawTextW(mdc, s, 1, &r,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(mdc, oldf);
    DeleteObject(font);

    SIZE sz = { IND_W, IND_H };
    POINT src = {0,0};
    BLENDFUNCTION bf = { AC_SRC_OVER,0,255,AC_SRC_ALPHA };

    UpdateLayeredWindow(
        g_hwnd, hdc, &pt, &sz,
        mdc, &src, 0, &bf, ULW_ALPHA);

    SelectObject(mdc, old);
    DeleteObject(bmp);
    DeleteDC(mdc);
    ReleaseDC(NULL, hdc);
}

/* ---------------- WinEvent ---------------- */

void CALLBACK WinEventProc(
    HWINEVENTHOOK,
    DWORD,
    HWND,
    LONG,
    LONG,
    DWORD,
    DWORD) {

    POINT pt;
    if (!QueryCaretScreenPos(&pt)) {
        GetCursorPos(&pt);
        pt.y += 16;
    }

    ClampToMonitor(&pt);

    COLORREF clr;
    WCHAR ch = QueryStateChar(&clr);
    Render(pt, ch, clr);
}

/* ---------------- window ---------------- */

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_TRAYICON && l == WM_RBUTTONUP) {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, ID_EXIT, L"Exit");
        POINT p; GetCursorPos(&p);
        SetForegroundWindow(h);
        TrackPopupMenu(menu, TPM_RIGHTBUTTON,
            p.x, p.y, 0, h, NULL);
        DestroyMenu(menu);
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
    EnableDPI();

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.lpszClassName = L"IME_IND_C";
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST |
        WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
        wc.lpszClassName, L"",
        WS_POPUP, 0,0,0,0,
        NULL,NULL,hi,NULL);

    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    SetWinEventHook(
        EVENT_OBJECT_FOCUS,
        EVENT_OBJECT_LOCATIONCHANGE,
        NULL,
        WinEventProc,
        0,0,
        WINEVENT_OUTOFCONTEXT |
        WINEVENT_SKIPOWNPROCESS);

    MSG msg;
    while (GetMessageW(&msg,NULL,0,0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
