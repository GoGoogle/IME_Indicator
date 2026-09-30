#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS

#include <windows.h>
#include <imm.h>
#include <shellapi.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <strsafe.h>

/* Ensure core COM types are available before including UIAutomation headers.
   WIN32_LEAN_AND_MEAN can exclude these from windows.h, so include them explicitly. */
#include <unknwn.h>
#include <ole2.h>

#include <uiautomation.h>

#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"imm32.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"uiautomationcore.lib")

// ==================== 基础定义 ====================
#define IND_W 15
#define IND_H 15

#define COLOR_CN   0x000078FF // 橙色
#define COLOR_EN   0x00FF7800 // 蓝色
#define COLOR_CAPS 0x0000C800 // 绿色

#define WM_TRAYICON (WM_USER + 1)
#define ID_ABOUT    1001
#define ID_EXIT     1002
#define ID_ONLY_WHEN_TYPING 1003
#define TIMER_RENDER 1
#define TIMER_RESUME_REFRESH 2

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

#ifndef PBT_APMRESUMEAUTOMATIC
#define PBT_APMRESUMEAUTOMATIC 0x0012
#endif

// ==================== 全局变量 ====================
static HWND g_hwnd;
static NOTIFYICONDATAW g_nid = {0};
static UINT g_TaskbarRestartMsg = 0; // 用于处理 explorer.exe 重启
static WCHAR g_logPath[MAX_PATH] = {0};
static HWND g_lastForeground = NULL;
static HWND g_lastInputWindow = NULL;
static const WCHAR* g_imeSource = L"unknown";
static IUIAutomation* g_uia = NULL;
static BOOL g_uiaInitialized = FALSE;
static HWND g_uiaCachedWindow = NULL;
static BOOL g_uiaCachedAcceptsText = FALSE;
static ULONGLONG g_uiaNextCheck = 0;

// 缓存机制状态
static COLORREF g_lastColor = 0;
static WCHAR    g_lastChar  = 0;
static POINT    g_lastPos   = { -10000, -10000 }; 
static BOOL     g_lastHasCaret = FALSE;
static int      g_lastDpi = 0;
// TRUE 时，只在系统可取得编辑插入点（光标）时显示指示器。
static BOOL     g_onlyWhenTyping = FALSE;

/* ---------------- 诊断日志 ---------------- */
void LogEvent(const WCHAR* format, ...) {
    if (!g_logPath[0]) return;

    WCHAR message[1024];
    WCHAR line[1200];
    va_list args;
    SYSTEMTIME now;
    va_start(args, format);
    StringCchVPrintfW(message, _countof(message), format, args);
    va_end(args);

    GetLocalTime(&now);
    StringCchPrintfW(line, _countof(line),
                     L"%04u-%02u-%02u %02u:%02u:%02u.%03u %s\r\n",
                     now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
                     now.wSecond, now.wMilliseconds, message);

    int bytes = WideCharToMultiByte(CP_UTF8, 0, line, -1, NULL, 0, NULL, NULL);
    if (bytes <= 1) return;
    char utf8[2400];
    if (!WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, (int)_countof(utf8), NULL, NULL)) return;

    HANDLE file = CreateFileW(g_logPath, FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(file, utf8, (DWORD)(bytes - 1), &written, NULL);
        CloseHandle(file);
    }
}

void InitializeLog(void) {
    WCHAR localAppData[MAX_PATH];
    DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData,
                                           _countof(localAppData));
    if (length && length < _countof(localAppData)) {
        WCHAR directory[MAX_PATH];
        StringCchPrintfW(directory, _countof(directory), L"%s\\IME Indicator", localAppData);
        CreateDirectoryW(directory, NULL);
        StringCchPrintfW(g_logPath, _countof(g_logPath), L"%s\\IME_Indicator.log", directory);
    }
    LogEvent(L"START version=V7.3 onlyWhenTyping=%d", g_onlyWhenTyping);
}

void LogFocusChange(HWND fg, HWND inputWindow) {
    if (fg == g_lastForeground && inputWindow == g_lastInputWindow) return;

    WCHAR fgClass[128] = L"";
    WCHAR inputClass[128] = L"";
    DWORD fgPid = 0, inputPid = 0;
    if (fg) {
        GetClassNameW(fg, fgClass, _countof(fgClass));
        GetWindowThreadProcessId(fg, &fgPid);
    }
    if (inputWindow) {
        GetClassNameW(inputWindow, inputClass, _countof(inputClass));
        GetWindowThreadProcessId(inputWindow, &inputPid);
    }
    LogEvent(L"FOCUS fg=%p pid=%lu class=%s input=%p pid=%lu class=%s",
             fg, fgPid, fgClass, inputWindow, inputPid, inputClass);
    g_lastForeground = fg;
    g_lastInputWindow = inputWindow;
}

/* ---------------- 强制重置渲染缓存 ---------------- */
void InvalidateRenderCache(void) {
    g_lastChar  = 0;
    g_lastColor = 0;
    g_lastPos.x = -10000;
    g_lastPos.y = -10000;
    g_lastDpi   = 0;
}

/* ---------------- 极简图标资源与解码函数 ---------------- */
HICON LoadMyIcon(const char* base64) {
    if (!base64) return LoadIcon(NULL, IDI_APPLICATION);
    int len = (int)strlen(base64);
    
    BYTE* buf = (BYTE*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len);
    if (!buf) return LoadIcon(NULL, IDI_APPLICATION);

    int table[256];
    memset(table, -1, sizeof(table));
    const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i = 0; i < 64; i++) table[(unsigned char)b64[i]] = i;

    int out = 0, val = 0, bits = 0;
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)base64[i];
        if (c == '=') break;
        // 增加严格的边界安全检查，防止非法字符导致数组越界
        if (c >= 256 || table[c] == -1) continue; 
        
        val = (val << 6) | table[c];
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            buf[out++] = (BYTE)((val >> bits) & 0xFF);
        }
    }

    HICON h = CreateIconFromResourceEx(buf, out, TRUE, 0x00030000, 16, 16, LR_DEFAULTCOLOR);
    HeapFree(GetProcessHeap(), 0, buf);
    return h ? h : LoadIcon(NULL, IDI_APPLICATION);
}

/* ---------------- 深度 DPI 启用逻辑 ---------------- */
void EnableDeepDPI(void) {
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        typedef BOOL (WINAPI* PSetDpi)(DPI_AWARENESS_CONTEXT);
        PSetDpi pfn = (PSetDpi)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (pfn && pfn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return;
    }
    SetProcessDPIAware();
}

/* 返回前台线程真正接收键盘输入的子窗口，而不是仅返回顶层窗口。 */
HWND GetInputWindow(HWND fg) {
    if (!fg) return NULL;

    GUITHREADINFO gti = { sizeof(gti) };
    DWORD tid = GetWindowThreadProcessId(fg, NULL);
    if (tid && GetGUIThreadInfo(tid, &gti) && gti.hwndFocus) {
        return gti.hwndFocus;
    }
    return fg;
}

/* 根据锚点所在显示器取得 DPI，避免多屏（尤其不同缩放比例）下错位。 */
UINT GetDpiForPoint(POINT pt, HWND fallbackWindow) {
    typedef HRESULT (WINAPI *PGetDpiForMonitor)(HMONITOR, int, UINT*, UINT*);
    HMONITOR monitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    UINT dpiX = 96, dpiY = 96;

    if (shcore && monitor) {
        PGetDpiForMonitor getDpiForMonitor =
            (PGetDpiForMonitor)GetProcAddress(shcore, "GetDpiForMonitor");
        if (getDpiForMonitor && SUCCEEDED(getDpiForMonitor(monitor, 0, &dpiX, &dpiY))) {
            FreeLibrary(shcore);
            return dpiX;
        }
    }
    if (shcore) FreeLibrary(shcore);

    dpiX = fallbackWindow ? GetDpiForWindow(fallbackWindow) : 96;
    return dpiX ? dpiX : 96;
}

/* 运行时获取 API，兼容使用旧版 Windows SDK 的编译环境。 */
void ConvertPointToPhysical(HWND hwnd, POINT* pt) {
    typedef BOOL (WINAPI *PLogicalToPhysicalPointForPerMonitorDPI)(HWND, LPPOINT);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    PLogicalToPhysicalPointForPerMonitorDPI convert = user32
        ? (PLogicalToPhysicalPointForPerMonitorDPI)GetProcAddress(
              user32, "LogicalToPhysicalPointForPerMonitorDPI")
        : NULL;
    if (convert) convert(hwnd, pt);
}

/*
 * 原生 Win32 编辑框会提供插入点；部分现代框架不会，因此再用
 * WM_GETDLGCODE 判断当前焦点窗口是否声明接收文本/编辑键。
 */
BOOL WindowAcceptsText(HWND hwnd) {
    DWORD_PTR dlgCode = 0;
    if (hwnd && SendMessageTimeoutW(hwnd, WM_GETDLGCODE, 0, 0,
                                    SMTO_ABORTIFHUNG | SMTO_BLOCK, 80, &dlgCode) &&
        (dlgCode & (DLGC_WANTCHARS | DLGC_WANTALLKEYS | DLGC_HASSETSEL))) {
        return TRUE;
    }

    /*
     * Chromium/Electron 等窗口通常不实现 WM_GETDLGCODE，但会为获得焦点的
     * 渲染窗口关联 HIMC。此后备项只用于“是否隐藏”的判定，不改变 IME 状态。
     */
    if (hwnd) {
        HIMC himc = ImmGetContext(hwnd);
        if (himc) {
            ImmReleaseContext(hwnd, himc);
            return TRUE;
        }
    }
    return FALSE;
}

/* ---------------- 输入状态查询 ---------------- */
WCHAR QueryState(HWND inputWindow, COLORREF* color) {
    if (GetKeyState(VK_CAPITAL) & 1) {
        g_imeSource = L"caps-lock";
        *color = COLOR_CAPS;
        return L'A';
    }

    if (inputWindow) {
        HIMC himc = ImmGetContext(inputWindow);
        if (himc) {
            BOOL isOpen = ImmGetOpenStatus(himc);
            DWORD convMode = 0, sentenceMode = 0;
            ImmGetConversionStatus(himc, &convMode, &sentenceMode);
            ImmReleaseContext(inputWindow, himc);
            if (isOpen && (convMode & IME_CMODE_NATIVE)) {
                g_imeSource = L"input-himc";
                *color = COLOR_CN;
                return L'中';
            }
        }

        // 某些 TSF/浏览器类窗口不直接暴露 HIMC，保留原 IME 窗口查询作后备。
        HWND ime = ImmGetDefaultIMEWnd(inputWindow);
        DWORD_PTR isOpen = 0, convMode = 0;
        if (ime && SendMessageTimeoutW(ime, WM_IME_CONTROL, 0x005, 0, SMTO_ABORTIFHUNG, 80, &isOpen) && isOpen) {
            SendMessageTimeoutW(ime, WM_IME_CONTROL, 0x001, 0, SMTO_ABORTIFHUNG, 80, &convMode);
            if (convMode & IME_CMODE_NATIVE) {
                g_imeSource = L"input-ime-window";
                *color = COLOR_CN;
                return L'中';
            }
        }
    }

    /*
     * 有些 Chromium/Electron 窗口的焦点落在内部渲染子窗口，但 IME 状态仍
     * 绑定于顶层窗口；两者不同且子窗口未给出中文状态时再查询一次顶层窗口。
     */
    HWND fg = GetForegroundWindow();
    if (fg && fg != inputWindow) {
        HIMC himc = ImmGetContext(fg);
        if (himc) {
            BOOL isOpen = ImmGetOpenStatus(himc);
            DWORD convMode = 0, sentenceMode = 0;
            ImmGetConversionStatus(himc, &convMode, &sentenceMode);
            ImmReleaseContext(fg, himc);
            if (isOpen && (convMode & IME_CMODE_NATIVE)) {
                g_imeSource = L"foreground-himc";
                *color = COLOR_CN;
                return L'中';
            }
        }

        HWND ime = ImmGetDefaultIMEWnd(fg);
        DWORD_PTR isOpen = 0, convMode = 0;
        if (ime && SendMessageTimeoutW(ime, WM_IME_CONTROL, 0x005, 0,
                                       SMTO_ABORTIFHUNG, 80, &isOpen) && isOpen) {
            SendMessageTimeoutW(ime, WM_IME_CONTROL, 0x001, 0,
                                SMTO_ABORTIFHUNG, 80, &convMode);
            if (convMode & IME_CMODE_NATIVE) {
                g_imeSource = L"foreground-ime-window";
                *color = COLOR_CN;
                return L'中';
            }
        }
    }
    g_imeSource = L"english-fallback";
    *color = COLOR_EN;
    return L'E';
}

/* ---------------- 核心渲染引擎 ---------------- */
void Render(void) {
    POINT anchor = {0};
    POINT pt = {0};
    BOOL hasCaret = FALSE;
    HWND fg = GetForegroundWindow();
    HWND inputWindow = GetInputWindow(fg);
    LogFocusChange(fg, inputWindow);

    // 定位光标或鼠标位置
    if (fg) {
        GUITHREADINFO gti = { sizeof(gti) };
        if (GetGUIThreadInfo(GetWindowThreadProcessId(fg, NULL), &gti) && gti.hwndCaret) {
            if (gti.rcCaret.bottom > gti.rcCaret.top) {
                POINT cp = { gti.rcCaret.left, gti.rcCaret.bottom };
                ClientToScreen(gti.hwndCaret, &cp);
                // GetGUIThreadInfo 返回的坐标可能按目标进程的 DPI 虚拟化。
                // 统一转成物理像素，避免 DPI-unaware 应用位于副屏时发生位移。
                ConvertPointToPhysical(gti.hwndCaret, &cp);
                if (MonitorFromPoint(cp, MONITOR_DEFAULTTONULL)) {
                    anchor.x = cp.x + (gti.rcCaret.right - gti.rcCaret.left) / 2;
                    anchor.y = cp.y;
                    hasCaret = TRUE;
                }
            }
        }
    }

    if (!hasCaret) GetCursorPos(&anchor);

    /*
     * 没有原生 caret 的现代界面（例如 Chromium/Electron）仍可能接收文本。
     * 此时以 WM_GETDLGCODE 为后备判定，避免“仅在可输入文字时显示”误隐藏。
     */
    if (g_onlyWhenTyping && !hasCaret && !WindowAcceptsText(inputWindow)) {
        if (IsWindowVisible(g_hwnd)) {
            LogEvent(L"HIDE reason=no-text-input fg=%p input=%p", fg, inputWindow);
            ShowWindow(g_hwnd, SW_HIDE);
            InvalidateRenderCache();
        }
        return;
    }

    // 必须以锚点所在显示器的 DPI 计算尺寸；g_hwnd 可能还停留在另一块屏幕。
    UINT dpi = GetDpiForPoint(anchor, inputWindow ? inputWindow : fg);
    double scale = (double)dpi / 96.0;
    int w = (int)(IND_W * scale);
    int h = (int)(IND_H * scale);

    if (hasCaret) {
        pt.x = anchor.x - w / 2;
        pt.y = anchor.y + (int)(2 * scale);
    } else {
        pt.x = anchor.x + (int)(15 * scale);
        pt.y = anchor.y + (int)(15 * scale);
    }

    // 先前可能因没有插入点而隐藏；恢复后必须重新显示。
    if (!IsWindowVisible(g_hwnd)) {
        ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    }

    COLORREF curC;
    WCHAR curChar = QueryState(inputWindow ? inputWindow : fg, &curC);

    // 缓存校验：避免无意义重绘，降低系统负载
    if (curChar == g_lastChar && curC == g_lastColor && 
        abs(pt.x - g_lastPos.x) < 2 && abs(pt.y - g_lastPos.y) < 2 &&
        hasCaret == g_lastHasCaret && dpi == g_lastDpi) {
        return; 
    }

    g_lastChar = curChar; g_lastColor = curC; g_lastPos = pt; 
    g_lastHasCaret = hasCaret; g_lastDpi = dpi;
    LogEvent(L"RENDER state=%c source=%s caret=%d anchor=(%ld,%ld) pos=(%ld,%ld) dpi=%u",
             curChar, g_imeSource, hasCaret, anchor.x, anchor.y, pt.x, pt.y, dpi);

    HDC hdcS = GetDC(NULL);
    HDC hdcM = CreateCompatibleDC(hdcS);
    BITMAPINFO bi = { {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB} };
    void* bits = NULL;
    HBITMAP hBmp = CreateDIBSection(hdcM, &bi, DIB_RGB_COLORS, &bits, NULL, 0);

    if (hBmp && bits) {
        // 保存 DC 状态，确保干净的环境
        int dcState = SaveDC(hdcM);
        
        SelectObject(hdcM, hBmp);
        memset(bits, 0, w * h * 4); // 清空背景（透明）
        
        HBRUSH br = CreateSolidBrush(curC);
        HPEN pen = CreatePen(PS_SOLID, 1, curC);
        SelectObject(hdcM, br);
        SelectObject(hdcM, pen);
        Ellipse(hdcM, 0, 0, w - 1, h - 1); 

        SetBkMode(hdcM, TRANSPARENT);
        SetTextColor(hdcM, RGB(255, 255, 255));
        
        int fontSize = -MulDiv(10, dpi, 96); 
        HFONT font = CreateFontW(fontSize, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Arial");
        SelectObject(hdcM, font);
        
        RECT rc = {0, 0, w, h};
        WCHAR txt[2] = { curChar, 0 };
        DrawTextW(hdcM, txt, 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // Alpha 通道修正
        DWORD* p = (DWORD*)bits;
        for (int i = 0; i < w * h; i++) { if (p[i] != 0) p[i] |= 0xFF000000; }

        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        SIZE sz = { w, h };
        POINT sPt = { 0, 0 };
        if (!UpdateLayeredWindow(g_hwnd, hdcS, &pt, &sz, hdcM, &sPt, 0, &bf, ULW_ALPHA)) {
            LogEvent(L"ERROR UpdateLayeredWindow code=%lu", GetLastError());
        }

        // 统一恢复上下文资源
        RestoreDC(hdcM, dcState);
        DeleteObject(font);
        DeleteObject(pen); 
        DeleteObject(br);
    }
    if (hBmp) DeleteObject(hBmp);
    DeleteDC(hdcM); 
    ReleaseDC(NULL, hdcS);
}

/* ---------------- 窗口过程 ---------------- */
LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    // 1. 拦截资源管理器重启消息，自动恢复托盘图标
    if (m == g_TaskbarRestartMsg && g_TaskbarRestartMsg != 0) {
        Shell_NotifyIconW(NIM_ADD, &g_nid);
        LogEvent(L"TASKBAR recreated");
        InvalidateRenderCache();
        return 0;
    }

    switch (m) {
        // 2. 监听电源状态广播：处理睡眠/休眠唤醒
        case WM_POWERBROADCAST:
            // PBT_APMRESUMEAUTOMATIC 表示系统从休眠/睡眠中自动恢复
            if (w == PBT_APMRESUMEAUTOMATIC || w == 0x0007 /*PBT_APMRESUMESUSPEND*/) {
                LogEvent(L"POWER resume event=%lu", (DWORD)w);
                Shell_NotifyIconW(NIM_MODIFY, &g_nid); 
                InvalidateRenderCache();
                // 唤醒后分层窗口可能仍存在但不再合成；重新显示并延迟重绘一次。
                ShowWindow(h, SW_SHOWNOACTIVATE);
                SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
                Render();
                SetTimer(h, TIMER_RESUME_REFRESH, 500, NULL);
            }
            break;

        // 3. 监听显示器分辨率/多屏插拔变化
        case WM_DISPLAYCHANGE:
            LogEvent(L"DISPLAYCHANGE width=%u height=%u bpp=%u", LOWORD(l), HIWORD(l), (UINT)w);
            InvalidateRenderCache();
            break;

        case WM_TRAYICON:
            if (l == WM_RBUTTONUP || l == WM_LBUTTONUP) {
                HMENU menu = CreatePopupMenu();
                AppendMenuW(menu,
                            MF_STRING | (g_onlyWhenTyping ? MF_CHECKED : MF_UNCHECKED),
                            ID_ONLY_WHEN_TYPING,
                            L"仅在可输入文字时显示");
                AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(menu, MF_STRING, ID_ABOUT, L"关于 (About)");
                AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(menu, MF_STRING, ID_EXIT, L"退出 (Exit)");
                POINT pt; GetCursorPos(&pt);
                SetForegroundWindow(h);
                TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, h, NULL);
                DestroyMenu(menu);
            }
            break;

        case WM_COMMAND:
            if (LOWORD(w) == ID_ONLY_WHEN_TYPING) {
                g_onlyWhenTyping = !g_onlyWhenTyping;
                LogEvent(L"SETTING onlyWhenTyping=%d", g_onlyWhenTyping);
                InvalidateRenderCache();
                Render();
            }
            else if (LOWORD(w) == ID_ABOUT) {
                MessageBoxW(h, 
                    L"IME Indicator V7.3 Final\n\n"
                    L"功能特性：\n"
                    L"1. 在光标或鼠标底部用彩色小点指示输入状态。\n"
                    L"2. 状态定义：蓝底(英), 橙底(中), 绿底(大写锁定)。\n"
                    L"3. 托盘菜单可启用「仅在可输入文字时显示」；启用后，"
                    L"没有文本插入光标时将隐藏指示器。\n\n"
                    L"核心修复：\n"
                    L"1. 睡眠或休眠唤醒后自动恢复窗口显示、置顶与重绘，"
                    L"避免指示状态消失。\n"
                    L"2. 按光标所在显示器的 DPI 定位，改善多屏、不同缩放比例下的偏差。\n"
                    L"3. 优先读取实际焦点子窗口的输入状态，并兼容没有原生光标的现代桌面应用。\n"
                    L"4. 受 Windows 权限隔离限制，如需跟踪管理员窗口，请以【管理员身份】运行。\n"
                    L"5. 诊断日志：%LOCALAPPDATA%\\IME Indicator\\IME_Indicator.log。\n\n"
                    L"By LC & Grok & Gemini & ChatGPT 2026.09.30", 
                    L"关于 IME Indicator", 
                    MB_OK | MB_ICONINFORMATION);
            }
            else if (LOWORD(w) == ID_EXIT) {
                DestroyWindow(h);
            }
            break;

        case WM_TIMER:
            if (w == TIMER_RESUME_REFRESH) {
                KillTimer(h, TIMER_RESUME_REFRESH);
                InvalidateRenderCache();
                Render();
            }
            else if (w == TIMER_RENDER) {
                Render();
            }
            break;

        case WM_DESTROY:
            LogEvent(L"STOP");
            if (g_nid.hIcon) DestroyIcon(g_nid.hIcon);
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            PostQuitMessage(0);
            break;
    }
    
    return DefWindowProcW(h, m, w, l);
}

/* ---------------- 入口函数 ---------------- */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    InitializeLog();
    EnableDeepDPI(); 

    const char* ICON_DATA = "iVBORw0KGgoAAAANSUhEUgAAADAAAAAwCAIAAADYYG7QAAAAIGNIUk0AAHomAACAhAAA+gAAAIDoAAB1MAAA6mAAADqYAAAXcJy6UTwAAAAGYktHRAD/AP8A/6C9p5MAAAAHdElNRQfqAQYOODhWA81HAAACP3pUWHRSYX[...]";
    
    HICON hMyIcon = LoadMyIcon(ICON_DATA);
    
    // 注册特定消息以便在 explorer 重启时重建托盘
    g_TaskbarRestartMsg = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"IME_V5_MULTIMON_FIXED";
    wc.hIcon = hMyIcon;
    
    // 错误处理：检查注册是否成功
    if (!RegisterClassExW(&wc)) {
        if (hMyIcon) DestroyIcon(hMyIcon);
        return 0; 
    }

    g_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                             wc.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);

    // 错误处理：拦截窗口创建失败
    if (!g_hwnd) {
        if (hMyIcon) DestroyIcon(hMyIcon);
        return 0;
    }

    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = hMyIcon;
    wcscpy(g_nid.szTip, L"IME Indicator (LC)");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    
    // 优化：修改为 50ms (避免 15ms 的高频 CPU 占用，50ms 视觉上已足够跟手)
    SetTimer(g_hwnd, TIMER_RENDER, 50, NULL); 

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
