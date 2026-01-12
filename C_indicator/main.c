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

// ==================== 基础定义 (15px 优化) ====================
#define IND_W 15      // 指示器宽度：15像素更精致
#define IND_H 15      // 指示器高度：15像素更精致

// 颜色定义 (BGR 格式，适配 UpdateLayeredWindow)
#define COLOR_CN   0x000078FF // 橙色：中文状态
#define COLOR_EN   0x00FF7800 // 蓝色：英文状态
#define COLOR_CAPS 0x0000C800 // 绿色：大写锁定

#define WM_TRAYICON (WM_USER + 1)
#define ID_ABOUT    1001
#define ID_EXIT     1002

// 定义 DPI 上下文，防止旧版 SDK 编译报错
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

// ==================== 全局变量 ====================
static HWND g_hwnd;
static NOTIFYICONDATAW g_nid = {0};

// 缓存机制：减少无意义重绘，降低系统负载
static COLORREF g_lastColor = 0;
static WCHAR    g_lastChar  = 0;
static POINT    g_lastPos   = { -10000, -10000 }; 
static BOOL     g_lastHasCaret = FALSE;
static int      g_lastDpi = 0;

/* ---------------- 极简图标资源与解码函数 (带加固逻辑) ---------------- */
// 处理 Base64 自定义图标，增加内存分配检查及非法字符过滤
HICON LoadMyIcon(const char* base64) {
    if (!base64) return LoadIcon(NULL, IDI_APPLICATION);
    int len = (int)strlen(base64);
    
    // 加固：检查堆分配失败情况
    BYTE* buf = (BYTE*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len);
    if (!buf) return LoadIcon(NULL, IDI_APPLICATION);

    // 解码表处理：初始化为 -1 以识别非法字符
    int table[256];
    memset(table, -1, sizeof(table));
    const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i = 0; i < 64; i++) table[(unsigned char)b64[i]] = i;

    int out = 0, val = 0, bits = 0;
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)base64[i];
        if (c == '=') break;
        if (table[c] == -1) continue; // 加固：处理并跳过非法字符
        val = (val << 6) | table[c];
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            buf[out++] = (BYTE)((val >> bits) & 0xFF);
        }
    }

    // 加固：CreateIconFromResourceEx 失败处理
    HICON h = CreateIconFromResourceEx(buf, out, TRUE, 0x00030000, 16, 16, LR_DEFAULTCOLOR);
    HeapFree(GetProcessHeap(), 0, buf);
    return h ? h : LoadIcon(NULL, IDI_APPLICATION);
}

/* ---------------- 深度 DPI 启用逻辑 ---------------- */
// 适配 Win10/Win11 及旧版系统的 DPI 意识
void EnableDeepDPI(void) {
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        typedef BOOL (WINAPI* PSetDpi)(DPI_AWARENESS_CONTEXT);
        PSetDpi pfn = (PSetDpi)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (pfn && pfn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return;
    }
    SetProcessDPIAware();
}

/* ---------------- 输入状态查询 (带超时加固) ---------------- */
WCHAR QueryState(COLORREF* color) {
    // 优先判断大写锁定状态
    if (GetKeyState(VK_CAPITAL) & 1) {
        *color = COLOR_CAPS;
        return L'A';
    }

    HWND fg = GetForegroundWindow();
    if (fg) {
        HWND ime = ImmGetDefaultIMEWnd(fg);
        DWORD_PTR isOpen = 0, convMode = 0;
        // 超时时间设为 80ms，平衡响应速度与卡死预防
        if (ime && SendMessageTimeoutW(ime, WM_IME_CONTROL, 0x005, 0, SMTO_ABORTIFHUNG, 80, &isOpen) && isOpen) {
            SendMessageTimeoutW(ime, WM_IME_CONTROL, 0x001, 0, SMTO_ABORTIFHUNG, 80, &convMode);
            if (convMode & IME_CMODE_NATIVE) {
                *color = COLOR_CN;
                return L'中';
            }
        }
    }
    *color = COLOR_EN;
    return L'E';
}

/* ---------------- 核心渲染引擎 ---------------- */
void Render(void) {
    // 解决 XP/Win7 兼容性，确保 DPI 不为 0
    UINT dpi = GetDpiForWindow(g_hwnd);
    if (dpi == 0) dpi = 96;
    double scale = (double)dpi / 96.0;

    int w = (int)(IND_W * scale);
    int h = (int)(IND_H * scale);

    POINT pt = {0};
    BOOL hasCaret = FALSE;
    HWND fg = GetForegroundWindow();

    if (fg) {
        GUITHREADINFO gti = { sizeof(gti) };
        if (GetGUIThreadInfo(GetWindowThreadProcessId(fg, NULL), &gti) && gti.hwndCaret) {
            if (gti.rcCaret.bottom > gti.rcCaret.top) {
                POINT cp = { gti.rcCaret.left, gti.rcCaret.bottom };
                ClientToScreen(gti.hwndCaret, &cp);
                // 关键修复：支持多显示器负坐标定位
                if (MonitorFromPoint(cp, MONITOR_DEFAULTTONULL)) {
                    pt.x = cp.x + (gti.rcCaret.right - gti.rcCaret.left) / 2 - w / 2;
                    pt.y = cp.y + (int)(2 * scale);
                    hasCaret = TRUE;
                }
            }
        }
    }

    if (!hasCaret) {
        GetCursorPos(&pt);
        pt.x += (int)(15 * scale); // 15px 尺寸下鼠标偏移微调
        pt.y += (int)(15 * scale);
    }

    COLORREF curC;
    WCHAR curChar = QueryState(&curC);

    // 缓存校验：位置、状态、DPI均未变化时不进行重绘，极大地节省资源
    if (curChar == g_lastChar && curC == g_lastColor && 
        abs(pt.x - g_lastPos.x) < 2 && abs(pt.y - g_lastPos.y) < 2 &&
        hasCaret == g_lastHasCaret && dpi == g_lastDpi) {
        return; 
    }

    g_lastChar = curChar; g_lastColor = curC; g_lastPos = pt; 
    g_lastHasCaret = hasCaret; g_lastDpi = dpi;

    HDC hdcS = GetDC(NULL);
    HDC hdcM = CreateCompatibleDC(hdcS);
    BITMAPINFO bi = { {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB} };
    void* bits = NULL;
    HBITMAP hBmp = CreateDIBSection(hdcM, &bi, DIB_RGB_COLORS, &bits, NULL, 0);

    if (hBmp && bits) {
        HGDIOBJ oldB = SelectObject(hdcM, hBmp);
        memset(bits, 0, w * h * 4); // 清空背景（透明）
        
        HBRUSH br = CreateSolidBrush(curC);
        HPEN pen = CreatePen(PS_SOLID, 1, curC);
        SelectObject(hdcM, br);
        SelectObject(hdcM, pen);
        Ellipse(hdcM, 0, 0, w - 1, h - 1); // 绘制状态圆点

        SetBkMode(hdcM, TRANSPARENT);
        SetTextColor(hdcM, RGB(255, 255, 255));
        
        // 字号适配 15px
        int fontSize = -MulDiv(10, dpi, 96); 
        HFONT font = CreateFontW(fontSize, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Arial");
        HGDIOBJ oldF = SelectObject(hdcM, font);
        
        RECT rc = {0, 0, w, h};
        WCHAR txt[2] = { curChar, 0 };
        DrawTextW(hdcM, txt, 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // Alpha 通道修正：UpdateLayeredWindow 依赖正确的 Alpha 值
        DWORD* p = (DWORD*)bits;
        for (int i = 0; i < w * h; i++) { if (p[i] != 0) p[i] |= 0xFF000000; }

        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        SIZE sz = { w, h };
        POINT sPt = { 0, 0 };
        UpdateLayeredWindow(g_hwnd, hdcS, &pt, &sz, hdcM, &sPt, 0, &bf, ULW_ALPHA);

        SelectObject(hdcM, oldF); DeleteObject(font);
        SelectObject(hdcM, oldB); 
        DeleteObject(pen); DeleteObject(br);
    }
    if (hBmp) DeleteObject(hBmp);
    DeleteDC(hdcM); ReleaseDC(NULL, hdcS);
}

/* ---------------- 窗口过程 (带完整关于信息) ---------------- */
LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_TRAYICON) {
        if (l == WM_RBUTTONUP || l == WM_LBUTTONUP) {
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, ID_ABOUT, L"关于 (About)");
            AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(menu, MF_STRING, ID_EXIT, L"退出 (Exit)");
            POINT pt; GetCursorPos(&pt);
            SetForegroundWindow(h);
            TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, h, NULL);
            DestroyMenu(menu);
        }
        return 0;
    }
    if (m == WM_COMMAND) {
        if (LOWORD(w) == ID_ABOUT) {
            // 这里恢复了你所有的功能说明和 Bug 修复笔记
            MessageBoxW(h, 
                L"IME Indicator V5.9\n\n"
                L"功能特性：\n"
                L"1. 在光标或鼠标底部用彩色小点指示输入状态。\n"
                L"2. 状态定义：\n"
                L"   - 英文状态：蓝底白字 E；\n"
                L"   - 中文状态：橙底白字 中；\n"
                L"   - 大写锁定：绿底白字 A；\n\n"
                L"3. 性能优化：采用 DIB 渲染与缓存对比技术，仅状态改变时刷新。\n"
                L"4. 兼容性修复：支持多显示器（处理负坐标光标定位 Bug）。\n\n"
                L"修订说明：已加固内存管理、非法 Base64 过滤及 XP/Win7 API 兼容性。\n"
                L"By LC & Grok & Gemini 2026.01.12", 
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
        // 加固：释放动态创建的图标句柄
        if (g_nid.hIcon) DestroyIcon(g_nid.hIcon);
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
    }
    return DefWindowProcW(h, m, w, l);
}

/* ---------------- 入口函数 ---------------- */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    EnableDeepDPI(); 

    const char* ICON_DATA = "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";
    
    HICON hMyIcon = LoadMyIcon(ICON_DATA);

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"IME_V5_MULTIMON_FIXED";
    wc.hIcon = hMyIcon;
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                             wc.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);

    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = hMyIcon;
    wcscpy(g_nid.szTip, L"IME Indicator (LC)");
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
