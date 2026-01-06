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

// 颜色定义
#define COLOR_CN   0x004B78FF // 橙红色 (BGR)
#define COLOR_EN   0x00FF7800 // 天蓝色 (BGR)
#define COLOR_CAPS 0x0000C800 // 绿色 (BGR)

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

/* ---------------- 渲染引擎 (核心修复) ---------------- */
void Render(void) {
    POINT pt;
    GUITHREADINFO gti = { sizeof(gti) };
    HWND fg = GetForegroundWindow();
    
    // 优先获取光标位置
    if (fg && GetGUIThreadInfo(GetWindowThreadProcessId(fg, NULL), &gti) && gti.hwndCaret) {
        pt.x = gti.rcCaret.left;
        pt.y = gti.rcCaret.bottom;
        ClientToScreen(gti.hwndCaret, &pt);
        pt.x += 4; pt.y += 4;
    } else {
        // 回退到鼠标
        GetCursorPos(&pt);
        pt.x += 10; pt.y += 18;
    }

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

    void* pBits;
    HBITMAP hBmp = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    HGDIOBJ hOldBmp = SelectObject(hdcMem, hBmp);

    // 绘制彩色背景小点（实心圆）
    HBRUSH hBr = CreateSolidBrush(clr);
    SelectObject(hdcMem, hBr);
    Ellipse(hdcMem, 0, 0, IND_W, IND_H);
    DeleteObject(hBr);

    // 绘制白色的字母 C/E/A
    SetBkMode(hdcMem, TRANSPARENT);
    SetTextColor(hdcMem, RGB(255, 255, 255));
    HFONT hFont = CreateFontW(-16, 0, 0, 0, FW_EXTRABOLD, 0, 0, 0, DEFAULT_CHARSET, 
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, 
                             DEFAULT_PITCH, L"Arial");
    HGDIOBJ hOldFont = SelectObject(hdcMem, hFont);
    RECT r = {0, 0, IND_W, IND_H};
    WCHAR s[2] = { ch, 0 };
    DrawTextW(hdcMem, s, 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // 必须处理 Alpha 通道，否则分层窗口看不见
    LPDWORD pdw = (LPDWORD)pBits;
    for (int i = 0; i < IND_W * IND_H; i++) {
        if (pdw[i] != 0) pdw[i] |= 0xFF000000; // 有颜色的地方设为不透明
    }

    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    POINT ptSrc = {0, 0};
    SIZE szWin = { IND_W, IND_H };
    UpdateLayeredWindow(g_hwnd, hdcScreen, &pt, &szWin, hdcMem, &ptSrc, 0, &bf, ULW_ALPHA);

    SelectObject(hdcMem, hOldFont);
    DeleteObject(hFont);
    SelectObject(hdcMem, hOldBmp);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
}

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
    }
    if (m == WM_TIMER) Render();
    if (m == WM_DESTROY) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    SetProcessDPIAware(); // 确保在高 DPI 下位置不偏移

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"IME_INDICATOR_V4";
    RegisterClassExW(&wc);

    // 关键属性：WS_EX_NOACTIVATE（不抢焦点）、WS_EX_TRANSPARENT（鼠标穿透）
    g_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                             wc.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);

    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcscpy(g_nid.szTip, L"输入法状态指示器");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    // 每 16ms 刷新一次（约 60 帧），保证平滑移动
    SetTimer(g_hwnd, 1, 16, NULL);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
