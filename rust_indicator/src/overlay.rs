//! GDI+ 悬浮窗渲染模块
use std::ptr::{null_mut, null};
use windows::core::{w, PCWSTR};
use windows::Win32::Foundation::{COLORREF, HWND, POINT, SIZE, LPARAM, LRESULT, WPARAM};
use windows::Win32::Graphics::Gdi::{
    CreateCompatibleDC, CreateDIBSection, DeleteDC, DeleteObject, GetDC, ReleaseDC, SelectObject,
    BITMAPINFO, BITMAPINFOHEADER, BI_RGB, DIB_RGB_COLORS,
};
use windows::Win32::Graphics::GdiPlus::{
    GdipCreateFromHDC, GdipCreateSolidFill, GdipDeleteBrush, GdipDeleteGraphics,
    GdipFillEllipse, GdipSetSmoothingMode, GdiplusShutdown, GdiplusStartup,
    GdiplusStartupInput, GpBrush, SmoothingModeAntiAlias,
};
use windows::Win32::UI::WindowsAndMessaging::{
    CreateWindowExW, DefWindowProcW, DestroyWindow, DispatchMessageW, PeekMessageW,
    RegisterClassExW, SetWindowPos, ShowWindow, TranslateMessage, UpdateLayeredWindow,
    HWND_TOPMOST, MSG, PM_REMOVE, SWP_NOACTIVATE, SWP_NOMOVE, SWP_NOSIZE, SW_HIDE, SW_SHOW,
    ULW_ALPHA, WNDCLASSEXW, WS_EX_LAYERED, WS_EX_NOACTIVATE, WS_EX_TOOLWINDOW, WS_EX_TOPMOST,
    WS_EX_TRANSPARENT, WS_POPUP, BLENDFUNCTION, AC_SRC_OVER, AC_SRC_ALPHA
};

pub struct IndicatorOverlay {
    hwnd: HWND,
    size: i32,
    color_cn: u32,
    color_en: u32,
    color_caps: u32,
    offset_x: i32,
    offset_y: i32,
    gdi_token: usize,
}

// 核心修复：手动定义一个符合 "system" 签名的 WNDPROC，防止编译器的类型推导错误
extern "system" fn overlay_wndproc(hwnd: HWND, msg: u32, wparam: WPARAM, lparam: LPARAM) -> LRESULT {
    unsafe { DefWindowProcW(hwnd, msg, wparam, lparam) }
}

impl IndicatorOverlay {
    pub fn new(_name: &str, size: i32, color_cn: u32, color_en: u32, color_caps: u32, offset_x: i32, offset_y: i32) -> Self {
        let mut token = 0;
        let input = GdiplusStartupInput { GdiplusVersion: 1, ..Default::default() };
        unsafe { let _ = GdiplusStartup(&mut token, &input, null_mut()); }

        let hwnd = unsafe {
            let h_inst = windows::Win32::System::LibraryLoader::GetModuleHandleW(None).unwrap();
            let cls_name = w!("IndicatorOverlayClass");
            let wc = WNDCLASSEXW {
                cbSize: std::mem::size_of::<WNDCLASSEXW>() as u32,
                lpfnWndProc: Some(overlay_wndproc), // 使用上面定义的 extern "system" 函数
                hInstance: h_inst.into(),
                lpszClassName: cls_name,
                ..Default::default()
            };
            RegisterClassExW(&wc);
            CreateWindowExW(
                WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                cls_name, PCWSTR(null()), WS_POPUP,
                0, 0, size, size, None, None, h_inst, None
            ).expect("Create Window Failed")
        };

        Self { hwnd, size, color_cn, color_en, color_caps, offset_x, offset_y, gdi_token: token }
    }

    pub fn update(&self, x: i32, y: i32, is_chinese: bool, is_caps: bool, caret_h: i32) {
        let color = if is_caps { self.color_caps } else if is_chinese { self.color_cn } else { self.color_en };
        unsafe {
            let screen_dc = GetDC(None);
            let mem_dc = CreateCompatibleDC(screen_dc);
            let bmi = BITMAPINFO {
                bmiHeader: BITMAPINFOHEADER {
                    biSize: std::mem::size_of::<BITMAPINFOHEADER>() as u32,
                    biWidth: self.size, biHeight: self.size, biPlanes: 1, biBitCount: 32, biCompression: BI_RGB.0, ..Default::default()
                }, ..Default::default()
            };
            let mut bits = null_mut();
            let h_bmp = CreateDIBSection(mem_dc, &bmi, DIB_RGB_COLORS, &mut bits, None, 0).unwrap();
            let old_bmp = SelectObject(mem_dc, h_bmp);
            
            let mut g = null_mut();
            GdipCreateFromHDC(mem_dc, &mut g);
            GdipSetSmoothingMode(g, SmoothingModeAntiAlias);
            let mut b = null_mut();
            GdipCreateSolidFill(color, &mut b);
            GdipFillEllipse(g, b as *mut GpBrush, 0.0, 0.0, self.size as f32, self.size as f32);
            GdipDeleteBrush(b as *mut GpBrush);
            GdipDeleteGraphics(g);

            let dest = POINT { x: x + self.offset_x - self.size/2, y: y + caret_h + self.offset_y - self.size/2 };
            let blend = BLENDFUNCTION { 
                BlendOp: AC_SRC_OVER as u8, 
                BlendFlags: 0, 
                SourceConstantAlpha: 255, 
                AlphaFormat: AC_SRC_ALPHA as u8 
            };
            
            // 修复：UpdateLayeredWindow 在 0.58.0 中需要 &T 类型
            let _ = UpdateLayeredWindow(
                self.hwnd, 
                screen_dc, 
                Some(&dest), 
                Some(&SIZE { cx: self.size, cy: self.size }), 
                mem_dc, 
                Some(&POINT::default()), 
                COLORREF(0), 
                Some(&blend), 
                ULW_ALPHA
            );
            
            SelectObject(mem_dc, old_bmp);
            let _ = DeleteObject(h_bmp);
            let _ = DeleteDC(mem_dc);
            ReleaseDC(None, screen_dc);
            
            // 修复拼写：HW_TOPMOST -> HWND_TOPMOST
            let _ = SetWindowPos(self.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            
            let mut msg = MSG::default();
            while PeekMessageW(&mut msg, self.hwnd, 0, 0, PM_REMOVE).into() {
                let _ = TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
    }
    pub fn show(&self) { unsafe { let _ = ShowWindow(self.hwnd, SW_SHOW); } }
    pub fn hide(&self) { unsafe { let _ = ShowWindow(self.hwnd, SW_HIDE); } }
    pub fn cleanup(&self) { unsafe { let _ = DestroyWindow(self.hwnd); GdiplusShutdown(self.gdi_token); } }
}
