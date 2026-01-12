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

// --- 基础定义（保持原样） ---
#define IND_W 18
#define IND_H 18

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

/* --- 极简图标资源与解码函数 --- */
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
        return L'中';
    }
    *clr = COLOR_EN;
    return L'E';
}

/* ---------------- 渲染核心 (引入 DPI 缩放支持) ---------------- */
void Render(void) {
    // --- 【新增：DPI 缩放计算】 ---
    // 自动获取当前显示器的 DPI (96 为 100% 缩放)
    UINT dpi = GetDpiForWindow(g_hwnd);
    if (dpi == 0) dpi = 96;
    double scale = (double)dpi / 96.0;

    // 计算缩放后的实际长宽和字体大小
    int curW = (int)(IND_W * scale);
    int curH = (int)(IND_H * scale);
    int fontSize = (int)(-12 * scale);

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
                
                // 【核心修复】检查该坐标是否位于任意有效显示器内
                HMONITOR hMon = MonitorFromPoint(caretPt, MONITOR_DEFAULTTONULL);
                
                // 只有当坐标确实落在某个显示器内时，才认为是有效光标
                if (hMon != NULL) {
                    int caretWidth = gti.rcCaret.right - gti.rcCaret.left;
                    // 使用缩放后的 curW 居中
                    pt.x = caretPt.x + (caretWidth / 2) - (curW / 2);
                    pt.y = caretPt.y + 2; 
                    bHasCaret = TRUE;
                }
            }
        }
    }

    // 回退到鼠标跟随
    if (!bHasCaret) {
        GetCursorPos(&pt);
        pt.x += (int)(25 * scale); // 偏移量同步缩放
        pt.y += (int)(25 * scale); 
    }

    COLORREF clr;
    WCHAR ch = QueryState(&clr);

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    
    // 使用动态宽高创建位图
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = curW;
    bmi.bmiHeader.biHeight = curH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = NULL;
    HBITMAP hBmp = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    HGDIOBJ hOldBmp = SelectObject(hdcMem, hBmp);

    // 适配动态宽高的内存清空
    memset(pBits, 0, curW * curH * 4);
    HBRUSH hBr = CreateSolidBrush(clr);
    HGDIOBJ hOldBr = SelectObject(hdcMem, hBr);
    HPEN hPen = CreatePen(PS_SOLID, 1, clr);
    HGDIOBJ hOldPen = SelectObject(hdcMem, hPen);
    
    // 绘制缩放后的圆
    Ellipse(hdcMem, 0, 0, curW - 1, curH - 1);

    SetBkMode(hdcMem, TRANSPARENT);
    SetTextColor(hdcMem, RGB(255, 255, 255));
    // 使用计算出的 fontSize
    HFONT hFont = CreateFontW(fontSize, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, 
                             DEFAULT_PITCH, L"Arial");
    HGDIOBJ hOldFont = SelectObject(hdcMem, hFont);
    RECT r = {0, 0, curW, curH};
    WCHAR s[2] = { ch, 0 };
    DrawTextW(hdcMem, s, 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    LPDWORD pdw = (LPDWORD)pBits;
    for (int i = 0; i < curW * curH; i++) {
        if (pdw[i] != 0) pdw[i] |= 0xFF000000; 
    }

    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    POINT ptSrc = {0, 0};
    SIZE szWin = { curW, curH };
    UpdateLayeredWindow(g_hwnd, hdcScreen, &pt, &szWin, hdcMem, &ptSrc, 0, &bf, ULW_ALPHA);

    SelectObject(hdcMem, hOldFont); DeleteObject(hFont);
    SelectObject(hdcMem, hOldPen); DeleteObject(hPen);
    SelectObject(hdcMem, hOldBr); DeleteObject(hBr);
    SelectObject(hdcMem, hOldBmp); DeleteObject(hBmp);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
}

/* ---------------- 窗口逻辑 ---------------- */
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
            // 100% 保持您要求的文字内容
            MessageBoxW(h, 
                L"输入指示器 (IME Indicator)功能如下：\n"
                L"在光标和鼠标底部用彩色带字母的小圆点指示中、英及大写状态\n\n"
                L"英文状态：蓝底白字 E；\n"
                L"中文状态：橙底白字 中；\n"
                L"大写锁定：绿底白字 A；\n\n"
                L"把中文状态的字母“C”更新成中文字符“中”，更加直观。By LC 2026.1.8", 
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

/* ---------------- WinMain ---------------- */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    EnableDeepDPI(); 

    HICON hMyIcon = LoadMyIcon(ICON_B64);

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"IME_V5_MULTIMON";
    wc.hIcon = hMyIcon;
    wc.hIconSm = hMyIcon;
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                             wc.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);

    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    
    // 使用图标替换 LoadIcon(NULL, IDI_APPLICATION)
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
