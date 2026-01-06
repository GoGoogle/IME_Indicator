#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <imm.h>
#include <shellapi.h>
#include <ole2.h>
#include <uiautomation.h>

#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"imm32.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"ole32.lib")

#define IND_W 24
#define IND_H 24
#define COLOR_CN   0x000078FF 
#define COLOR_EN   0x00FF7800 
#define COLOR_CAPS 0x0000C800 
#define ID_EXIT     1
#define WM_TRAYICON (WM_USER + 1)

static HWND g_hwnd;
static NOTIFYICONDATAW g_nid;
static IUIAutomation* g_pAutomation = NULL;

/* ---------------- 状态查询 ---------------- */
WCHAR QueryState(COLORREF* clr) {
    if (GetKeyState(VK_CAPITAL) & 1) { *clr = COLOR_CAPS; return L'A'; }
    HWND fg = GetForegroundWindow();
    HWND ime = fg ? ImmGetDefaultIMEWnd(fg) : NULL;
    DWORD_PTR open = 0, mode = 0;
    if (ime) {
        if (SendMessageTimeoutW(ime, 0x0283, 0x005, 0, SMTO_ABORTIFHUNG, 15, &open) && open) {
            SendMessageTimeoutW(ime, 0x0283, 0x001, 0, SMTO_ABORTIFHUNG, 15, &mode);
        }
    }
    if (open && (mode & 1)) { *clr = COLOR_CN; return L'C'; }
    *clr = COLOR_EN; return L'E';
}

/* ---------------- 深度光标探测 (含 UIA) ---------------- */
void GetTargetPoint(POINT* pt) {
    GUITHREADINFO gti = { sizeof(gti) };
    HWND fg = GetForegroundWindow();
    
    // 1. 尝试标准 Win32 光标 (记事本等)
    if (fg && GetGUIThreadInfo(GetWindowThreadProcessId(fg, NULL), &gti) && gti.hwndCaret) {
        RECT rc = gti.rcCaret;
        pt->x = rc.left; pt->y = rc.bottom + 8;
        ClientToScreen(gti.hwndCaret, pt);
        if (pt->x > 0) return;
    }

    // 2. 针对 Chrome/Edge 等 (使用 UI Automation)
    if (g_pAutomation) {
        IUIAutomationElement* pEl = NULL;
        if (SUCCEEDED(g_pAutomation->lpVtbl->GetFocusedElement(g_pAutomation, &pEl)) && pEl) {
            RECT rect;
            if (SUCCEEDED(pEl->lpVtbl->get_CurrentBoundingRectangle(pEl, &rect))) {
                // 如果是输入框，取其底部位置
                pt->x = rect.left; 
                pt->y = rect.bottom + 2; 
                pEl->lpVtbl->Release(pEl);
                if (pt->x > 0) return;
            }
            pEl->lpVtbl->Release(pEl);
        }
    }

    // 3. 最终回退到鼠标
    GetCursorPos(pt);
    pt->x += 18; pt->y += 22;
}

/* ---------------- 渲染逻辑 (保持上一版成功的 Alpha 处理) ---------------- */
void Render(void) {
    POINT pt; GetTargetPoint(&pt);
    COLORREF clr; WCHAR ch = QueryState(&clr);

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = IND_W; bmi.bmiHeader.biHeight = IND_H;
    bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32; bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = NULL;
    HBITMAP hBmp = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    HGDIOBJ hOldBmp = SelectObject(hdcMem, hBmp);
    memset(pBits, 0, IND_W * IND_H * 4);

    HBRUSH hBr = CreateSolidBrush(clr);
    HGDIOBJ hOldBr = SelectObject(hdcMem, hBr);
    HPEN hPen = CreatePen(PS_SOLID, 1, clr);
    HGDIOBJ hOldPen = SelectObject(hdcMem, hPen);
    Ellipse(hdcMem, 0, 0, IND_W - 1, IND_H - 1);

    SetBkMode(hdcMem, TRANSPARENT);
    SetTextColor(hdcMem, RGB(255, 255, 255));
    HFONT hFont = CreateFontW(-15, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ hOldFont = SelectObject(hdcMem, hFont);
    RECT r = {0, 0, IND_W, IND_H}; WCHAR s[2] = { ch, 0 };
    DrawTextW(hdcMem, s, 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    LPDWORD pdw = (LPDWORD)pBits;
    for (int i = 0; i < IND_W * IND_H; i++) { if (pdw[i] != 0) pdw[i] |= 0xFF000000; }

    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    POINT ptSrc = {0, 0}; SIZE szWin = { IND_W, IND_H };
    UpdateLayeredWindow(g_hwnd, hdcScreen, &pt, &szWin, hdcMem, &ptSrc, 0, &bf, ULW_ALPHA);

    SelectObject(hdcMem, hOldFont); DeleteObject(hFont);
    SelectObject(hdcMem, hOldPen); DeleteObject(hPen);
    SelectObject(hdcMem, hOldBr); DeleteObject(hBr);
    SelectObject(hdcMem, hOldBmp); DeleteObject(hBmp);
    DeleteDC(hdcMem); ReleaseDC(NULL, hdcScreen);
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_TRAYICON && (l == WM_RBUTTONUP || l == WM_LBUTTONUP)) {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, ID_EXIT, L"退出 (Exit)");
        POINT p; GetCursorPos(&p); SetForegroundWindow(h);
        TrackPopupMenu(menu, TPM_RIGHTBUTTON, p.x, p.y, 0, h, NULL);
        DestroyMenu(menu); return 0;
    }
    if (m == WM_COMMAND && LOWORD(w) == ID_EXIT) DestroyWindow(h);
    if (m == WM_TIMER) Render();
    if (m == WM_DESTROY) { Shell_NotifyIconW(NIM_DELETE, &g_nid); PostQuitMessage(0); }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR lp, int ns) {
    CoInitialize(NULL); // 初始化 COM
    CoCreateInstance(&CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER, &IID_IUIAutomation, (void**)&g_pAutomation);

    SetProcessDPIAware(); 
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc; wc.hInstance = hi; wc.lpszClassName = L"IME_IND_V7";
    RegisterClassExW(&wc);
    g_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, NULL, NULL, hi, NULL);
    
    g_nid.cbSize = sizeof(g_nid); g_nid.hWnd = g_hwnd; g_nid.uID = 1; g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP; g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION); wcscpy(g_nid.szTip, L"IME Indicator");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    SetTimer(g_hwnd, 1, 15, NULL);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    
    if (g_pAutomation) g_pAutomation->lpVtbl->Release(g_pAutomation);
    CoUninitialize();
    return 0;
}
