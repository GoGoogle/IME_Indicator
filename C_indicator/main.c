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

#define IND_W 24
#define IND_H 24

// 颜色定义 (BGR 格式)
#define COLOR_CN   0x000078FF // 橙色
#define COLOR_EN   0x00FF7800 // 蓝色
#define COLOR_CAPS 0x0000C800 // 绿色

#define WM_TRAYICON (WM_USER + 1)
#define ID_EXIT     1

static HWND g_hwnd;
static NOTIFYICONDATAW g_nid;

/* ---------------- 状态查询 ---------------- */
WCHAR QueryState(COLORREF* clr) {
    if (GetKeyState(VK_CAPITAL) & 1) {
        *clr = COLOR_CAPS;
        return L'A';
    }
    HWND fg = GetForegroundWindow();
    HWND ime = fg ? ImmGetDefaultIMEWnd(fg) : NULL;
    DWORD_PTR open = 0, mode = 0;
    if (ime) {
        if (SendMessageTimeoutW(ime, 0x0283, 0x005, 0, SMTO_ABORTIFHUNG, 15, &open) && open) {
            SendMessageTimeoutW(ime, 0x0283, 0x001, 0, SMTO_ABORTIFHUNG, 15, &mode);
        }
    }
    if (open && (mode & 1)) {
        *clr = COLOR_CN;
        return L'C';
    }
    *clr = COLOR_EN;
    return L'E';
}

/* ---------------- 坐标微调逻辑 ---------------- */
void GetTargetPoint(POINT* pt) {
    GUITHREADINFO gti = { sizeof(gti) };
    HWND fg = GetForegroundWindow();
    
    // 1. 尝试获取光标焦点 (Caret)
    if (fg && GetGUIThreadInfo(GetWindowThreadProcessId(fg, NULL), &gti) && gti.hwndCaret) {
        // 获取光标矩形的中点位置
        RECT rc = gti.rcCaret;
        pt->x = rc.left + (rc.right - rc.left) / 2;
        pt->y = rc.bottom + 8; // 在光标底部下方 8 像素处
        
        ClientToScreen(gti.hwndCaret, pt);
        
        // 简单有效性检查，防止坐标跳转到屏幕原点
        if (pt->x > 0 && pt->y > 0) return;
    }

    // 2. 回退到鼠标模式 (加大偏移量以防遮挡)
    GetCursorPos(pt);
    pt->x += 18; // 向右偏移，避开鼠标主体
    pt->y += 22; // 向下偏移，避开鼠标尖端
}

/* ---------------- 渲染核心 ---------------- */
void Render(void) {
    POINT pt;
    GetTargetPoint(&pt);

    COLORREF clr;
    WCHAR ch = QueryState(&clr);

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = IND_W;
    bmi.bmiHeader.biHeight = IND_H;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = NULL;
    HBITMAP hBmp = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    HGDIOBJ hOldBmp = SelectObject(hdcMem, hBmp);

    // 清空背景并设置 Alpha
    memset(pBits, 0, IND_W * IND_H * 4);

    // 绘制彩色实心圆
    HBRUSH hBr = CreateSolidBrush(clr);
    HGDIOBJ hOldBr = SelectObject(hdcMem, hBr);
    HPEN hPen = CreatePen(PS_SOLID, 1, clr);
    HGDIOBJ hOldPen = SelectObject(hdcMem, hPen);
    Ellipse(hdcMem, 0, 0, IND_W - 1, IND_H - 1);

    // 绘制文字
    SetBkMode(hdcMem, TRANSPARENT);
    SetTextColor(hdcMem, RGB(255, 255, 255));
    HFONT hFont = CreateFontW(-15, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, 
                             DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ hOldFont = SelectObject(hdcMem, hFont);
    RECT r = {0, 0, IND_W, IND_H};
    WCHAR s[2] = { ch, 0 };
    DrawTextW(hdcMem, s, 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // 强制补全 Alpha 通道，使圆点可见
    LPDWORD pdw = (LPDWORD)pBits;
    for (int i = 0; i < IND_W * IND_H; i++) {
        if (pdw[i] != 0) pdw[i] |= 0xFF000000; 
    }

    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    POINT ptSrc = {0, 0};
    SIZE szWin = { IND_W, IND_H };
    UpdateLayeredWindow(g_hwnd, hdcScreen, &pt, &szWin, hdcMem, &ptSrc, 0, &bf, ULW_ALPHA);

    SelectObject(hdcMem, hOldFont); DeleteObject(hFont);
    SelectObject(hdcMem, hOldPen); DeleteObject(hPen);
    SelectObject(hdcMem, hOldBr); DeleteObject(hBr);
    SelectObject(hdcMem, hOldBmp); DeleteObject(hBmp);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
}

/* ---------------- 窗口消息处理 ---------------- */
LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_TRAYICON && (l == WM_RBUTTONUP || l == WM_LBUTTONUP)) {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, ID_EXIT, L"退出 (Exit)");
        POINT p; GetCursorPos(&p);
        SetForegroundWindow(h);
        TrackPopupMenu(menu, TPM_RIGHTBUTTON, p.x, p.y, 0, h, NULL);
        DestroyMenu(menu);
        return 0;
    }
    if (m == WM_COMMAND && LOWORD(w) == ID_EXIT) DestroyWindow(h);
    if (m == WM_TIMER) Render();
    if (m == WM_DESTROY) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    SetProcessDPIAware(); 

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"IME_INDICATOR_V6";
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                             wc.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);

    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcscpy(g_nid.szTip, L"输入法指示器");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    SetTimer(g_hwnd, 1, 15, NULL); // 15ms 约等于 66FPS，足够丝滑

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
