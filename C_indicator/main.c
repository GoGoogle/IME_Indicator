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
#define ID_ABOUT    1001
#define ID_EXIT     1002

// 定义 DPI 上下文，防止旧版 SDK 编译报错
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

static HWND g_hwnd;
static NOTIFYICONDATAW g_nid;

/* --- 极简图标资源与解码函数 (独立注入，不改动下方任何原有代码) --- */
const char* ICON_B64 = "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";

HICON LoadMyIcon(const char* s) {
    int len = (int)strlen(s);
    BYTE* b = (BYTE*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len);
    int tbl[256] = {0}; const char* p = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for(int i=0; i<64; i++) tbl[(int)p[i]] = i;
    int n=0, v=0, bits=0;
    for(int i=0; i<len; i++) {
        if(s[i]=='=') break;
        v = (v << 6) | tbl[(int)s[i]]; bits += 6;
        if(bits >= 8) { bits -= 8; b[n++] = (v >> bits) & 0xFF; }
    }
    HICON h = CreateIconFromResourceEx(b, n, TRUE, 0x00030000, 16, 16, LR_DEFAULTCOLOR);
    HeapFree(GetProcessHeap(), 0, b);
    return h;
}

/* ---------------- 深度 DPI 启用逻辑 ---------------- */
void EnableDeepDPI(void) {
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        typedef BOOL (WINAPI * SetProcessDpiAwarenessContextProc)(DPI_AWARENESS_CONTEXT);
        SetProcessDpiAwarenessContextProc setDpiAwareness = 
            (SetProcessDpiAwarenessContextProc)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        
        if (setDpiAwareness) {
            setDpiAwareness(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            return;
        }
    }
    SetProcessDPIAware();
}

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
        if (SendMessageTimeoutW(ime, 0x0283, 0x005, 0, SMTO_ABORTIFHUNG, 10, &open) && open) {
            SendMessageTimeoutW(ime, 0x0283, 0x001, 0, SMTO_ABORTIFHUNG, 10, &mode);
        }
    }
    if (open && (mode & 1)) {
        *clr = COLOR_CN;
        return L'C';
    }
    *clr = COLOR_EN;
    return L'E';
}

/* ---------------- 渲染核心 (多屏坐标修复版) ---------------- */
void Render(void) {
    POINT pt = {0};
    GUITHREADINFO gti = { sizeof(gti) };
    HWND fg = GetForegroundWindow();
    BOOL bHasCaret = FALSE;
    
    if (fg) {
        DWORD tid = GetWindowThreadProcessId(fg, NULL);
        if (GetGUIThreadInfo(tid, &gti)) {
            // 必须有光标句柄且高度有效 (排除隐形光标)
            if (gti.hwndCaret && (gti.rcCaret.bottom - gti.rcCaret.top > 0)) {
                POINT caretPt = { gti.rcCaret.left, gti.rcCaret.bottom };
                ClientToScreen(gti.hwndCaret, &caretPt);
                
                // 【核心修复】这里删除了 caretPt.x > 0 的判断
                // 改为检查该坐标是否位于任意有效显示器内
                HMONITOR hMon = MonitorFromPoint(caretPt, MONITOR_DEFAULTTONULL);
                
                // 只有当坐标确实落在某个显示器内时，才认为是有效光标
                if (hMon != NULL) {
                    int caretWidth = gti.rcCaret.right - gti.rcCaret.left;
                    pt.x = caretPt.x + (caretWidth / 2) - (IND_W / 2);
                    pt.y = caretPt.y + 2; 
                    bHasCaret = TRUE;
                }
            }
        }
    }

    // 回退到鼠标跟随
    if (!bHasCaret) {
        GetCursorPos(&pt);
        pt.x += 25; 
        pt.y += 25; 
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
    HFONT hFont = CreateFontW(-16, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, 
                             DEFAULT_PITCH, L"Arial");
    HGDIOBJ hOldFont = SelectObject(hdcMem, hFont);
    RECT r = {0, 0, IND_W, IND_H};
    WCHAR s[2] = { ch, 0 };
    DrawTextW(hdcMem, s, 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

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

/* ---------------- 窗口逻辑 (100% 原始代码，包含 MessageBox 内容) ---------------- */
LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_TRAYICON && (l == WM_RBUTTONUP || l == WM_LBUTTONUP)) {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, ID_ABOUT, L"关于 (About)");
        AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(menu, MF_STRING, ID_EXIT, L"退出 (Exit)");
        
        POINT p; GetCursorPos(&p);
        SetForegroundWindow(h);
        TrackPopupMenu(menu, TPM_RIGHTBUTTON, p.x, p.y, 0, h, NULL);
        DestroyMenu(menu);
        return 0;
    }
    
    if (m == WM_COMMAND) {
        if (LOWORD(w) == ID_ABOUT) {
            MessageBoxW(h, 
                L"输入指示器 (IME Indicator)功能如下：\n"
                L"在光标和鼠标底部用彩色带字母的小圆点指示中、英及大写状态\n\n"
                L"英文状态：蓝底白字 E；\n"
                L"中文状态：橙底白字 C；\n"
                L"大写锁定：绿底白字 A；\n\n"
                L"修复了多显示器(负坐标)光标失效的Bug。By LC 2026.1.6", 
                L"关于 IME Indicator", 
                MB_OK | MB_ICONINFORMATION);
        }
        else if (LOWORD(w) == ID_EXIT) {
            DestroyWindow(h);
        }
        return 0;
    }
    if (m == WM_TIMER) Render();
    if (m == WM_DESTROY) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
    }
    return DefWindowProcW(h, m, w, l);
}

/* ---------------- WinMain (仅替换注册和初始化时的图标句柄) ---------------- */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    EnableDeepDPI(); 

    // 仅在此处加载图标，不改动下方其他原有代码
    HICON hMyIcon = LoadMyIcon(ICON_B64);

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"IME_V5_MULTIMON";
    wc.hIcon = hMyIcon;   // 原始代码是 NULL
    wc.hIconSm = hMyIcon; // 原始代码是 NULL
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                             wc.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);

    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    // 使用新图标替换 LoadIcon(NULL, IDI_APPLICATION)
    g_nid.hIcon = hMyIcon;
    wcscpy(g_nid.szTip, L"IME Indicator");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    SetTimer(g_hwnd, 1, 15, NULL); 

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
