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

#define IND_W 20
#define IND_H 20

#define COLOR_CN   RGB(255,120,0)
#define COLOR_EN   RGB(0,120,255)
#define COLOR_CAPS RGB(0,200,0)

#define WM_TRAYICON (WM_USER + 1)
#define ID_EXIT     1

static HWND g_hwnd;
static NOTIFYICONDATAW g_nid;

static POINT     g_lastPt  = { -1,-1 };
static WCHAR     g_lastCh  = 0;
static COLORREF  g_lastClr = 0;

/* ---------------- DPI 修复 ---------------- */
void EnableDPI(void) {
    SetProcessDPIAware(); // 简化 DPI 适配，确保跨屏坐标正确
}

/* ---------------- 状态查询 ---------------- */
WCHAR QueryStateChar(COLORREF* clr) {
    if (GetKeyState(VK_CAPITAL) & 1) {
        *clr = COLOR_CAPS;
        return L'A';
    }
    HWND fg = GetForegroundWindow();
    HWND ime = fg ? ImmGetDefaultIMEWnd(fg) : NULL;
    DWORD_PTR open = 0, mode = 0;
    if (ime) {
        SendMessageTimeoutW(ime, 0x0283, 0x005, 0, SMTO_ABORTIFHUNG, 20, &open);
        if (open) {
            SendMessageTimeoutW(ime, 0x0283, 0x001, 0, SMTO_ABORTIFHUNG, 20, &mode);
        }
    }
    if (open && (mode & 1)) {
        *clr = COLOR_CN;
        return L'C';
    }
    *clr = COLOR_EN;
    return L'E';
}

/* ---------------- 光标定位 ---------------- */
BOOL QueryCaretScreenPos(POINT* pt) {
    GUITHREADINFO gti = { sizeof(gti) };
    HWND fg = GetForegroundWindow();
    if (!fg) return FALSE;
    if (!GetGUIThreadInfo(GetWindowThreadProcessId(fg, NULL), &gti)) return FALSE;
    if (!gti.hwndCaret) return FALSE;

    POINT p = { gti.rcCaret.left, gti.rcCaret.bottom };
    ClientToScreen(gti.hwndCaret, &p);
    *pt = p;
    return TRUE;
}

/* ---------------- 屏幕边缘限制 ---------------- */
void ClampToMonitor(POINT* pt) {
    HMONITOR mon = MonitorFromPoint(*pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(mon, &mi);
    if (pt->x + IND_W > mi.rcWork.right) pt->x = mi.rcWork.right - IND_W;
    if (pt->y + IND_H > mi.rcWork.bottom) pt->y = mi.rcWork.bottom - IND_H;
    if (pt->x < mi.rcWork.left) pt->x = mi.rcWork.left;
    if (pt->y < mi.rcWork.top) pt->y = mi.rcWork.top;
}

/* ---------------- 核心渲染 (修复 Alpha 通道) ---------------- */
void Render(POINT pt, WCHAR ch, COLORREF clr) {
    // 性能优化：只有在状态或位置变化时才重绘
    if (pt.x == g_lastPt.x && pt.y == g_lastPt.y && ch == g_lastCh && clr == g_lastClr) return;
    g_lastPt = pt; g_lastCh = ch; g_lastClr = clr;

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    
    // 创建包含 Alpha 通道的位图
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = IND_W;
    bmi.bmiHeader.biHeight = IND_H;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits;
    HBITMAP hBmp = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    HGDIOBJ hOldBmp = SelectObject(hdcMem, hBmp);

    // 绘制背景圆角矩形
    RECT r = {0, 0, IND_W, IND_H};
    HBRUSH hBr = CreateSolidBrush(clr);
    FillRect(hdcMem, &r, hBr);
    DeleteObject(hBr);

    // 绘制文字
    SetBkMode(hdcMem, TRANSPARENT);
    SetTextColor(hdcMem, RGB(255, 255, 255)); // 文字统一用白色，背景显色
    HFONT hFont = CreateFontW(-14, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, 
                             DEFAULT_PITCH, L"Arial");
    HGDIOBJ hOldFont = SelectObject(hdcMem, hFont);
    WCHAR s[2] = { ch, 0 };
    DrawTextW(hdcMem, s, 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // 更新分层窗口
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    POINT ptSrc = {0, 0};
    SIZE szWin = { IND_W, IND_H };
    
    // 注意：这里需要对所有像素设置 Alpha=255，否则 UpdateLayeredWindow 会变透明
    LPDWORD pdw = (LPDWORD)pBits;
    for (int i = 0; i < IND_W * IND_H; i++) pdw[i] |= 0xFF000000;

    UpdateLayeredWindow(g_hwnd, hdcScreen, &pt, &szWin, hdcMem, &ptSrc, 0, &bf, ULW_ALPHA);

    SelectObject(hdcMem, hOldFont);
    DeleteObject(hFont);
    SelectObject(hdcMem, hOldBmp);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
}

/* ---------------- 修复：WinEventProc 参数 ---------------- */
void CALLBACK WinEventProc(HWINEVENTHOOK hHook, DWORD event, HWND hwnd, LONG idObj, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime) {
    POINT pt;
    if (!QueryCaretScreenPos(&pt)) {
        GetCursorPos(&pt);
        pt.y += 20;
    }
    ClampToMonitor(&pt);
    COLORREF clr;
    WCHAR ch = QueryStateChar(&clr);
    Render(pt, ch, clr);
}

/* ---------------- 窗口消息处理 ---------------- */
LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_TRAYICON && (l == WM_RBUTTONUP || l == WM_LBUTTONUP)) {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, ID_EXIT, L"退出程序 (Exit)");
        POINT p; GetCursorPos(&p);
        SetForegroundWindow(h);
        TrackPopupMenu(menu, TPM_RIGHTBUTTON, p.x, p.y, 0, h, NULL);
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

/* ---------------- 修复：WinMain 参数 ---------------- */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    EnableDPI();

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"IME_IND_V3";
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                             wc.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);

    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcscpy(g_nid.szTip, L"IME Indicator");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    SetWinEventHook(EVENT_OBJECT_FOCUS, EVENT_OBJECT_LOCATIONCHANGE, NULL, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);

    // 增加定时器，确保鼠标跟随更平滑
    SetTimer(g_hwnd, 1, 16, NULL);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (msg.message == WM_TIMER) {
            WinEventProc(NULL, 0, NULL, 0, 0, 0, 0); // 借用 Event 函数刷新
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
