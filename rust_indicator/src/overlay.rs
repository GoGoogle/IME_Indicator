//! GDI+ 悬浮窗渲染模块

use std::ptr::null_mut;
use windows::Win32::Foundation::{COLORREF, HWND, POINT, SIZE};
use windows::Win32::Graphics::Gdi::{
    CreateCompatibleDC, CreateDIBSection, DeleteDC, DeleteObject, GetDC, ReleaseDC, SelectObject,
    BITMAPINFO, BITMAPINFOHEADER, BI_RGB, DIB_RGB_COLORS,
};
use windows::Win32::Graphics::GdiPlus::{
    GdipCreateFromHDC, GdipCreateSolidFill, GdipDeleteBrush, GdipDeleteGraphics,
    GdipFillEllipse, GdipSetSmoothingMode, GdiplusShutdown, GdiplusStartup,
    GdiplusStartupInput, GpBrush, GpGraphics, GpSolidFill, SmoothingModeAntiAlias,
};
use windows::Win32::UI::WindowsAndMessaging::{
    CreateWindowExW, DefWindowProcW, DestroyWindow, DispatchMessageW, PeekMessageW,
    RegisterClassExW, SetWindowPos, ShowWindow, TranslateMessage, UpdateLayeredWindow,
    HWND_TOPMOST, MSG, PM_REMOVE, SWP_NOACTIVATE, SWP_NOMOVE, SWP_NOSIZE, SW_HIDE, SW_SHOW,
    ULW_ALPHA, WNDCLASSEXW, WS_EX_LAYERED, WS_EX_NOACTIVATE, WS_EX_TOOLWINDOW, WS_EX_TOPMOST,
    WS_EX_TRANSPARENT, WS_POPUP,
};

pub struct IndicatorOverlay {
    hwnd: HWND,
    size: i32,
    color_cn: u32,
    color_en: u32,
    // --- CAPS_INDICATOR_START ---
    color_caps: u32,
    // --- CAPS_INDICATOR_END ---
    offset_x: i32,
    offset_y: i32,
    gdi_token: usize,
}

impl IndicatorOverlay {
    pub fn new(
        name: &str,
        size: i32,
        color_cn: u32,
        color_en: u32,
        color_caps: u32, // --- CAPS_INDICATOR ---
        offset_x: i32,
        offset_y: i32,
    ) -> Self {
        let gdi_token = Self::init_gdiplus();
        let hwnd = Self::create_window(name, size);

        Self {
            hwnd,
            size,
            color_cn,
            color_en,
            color_caps, // --- CAPS_INDICATOR ---
            offset_x,
            offset_y,
            gdi_token,
        }
    }

    fn init_gdiplus() -> usize {
        let mut token = 0;
        let input = GdiplusStartupInput { GdiplusVersion: 1, ..Default::default() };
        unsafe { let _ = GdiplusStartup(&mut token, &input, None); }
        token
    }

    fn create_window(_name: &str, size: i32) -> HWND {
        unsafe {
            let instance = windows::Win32::System::LibraryLoader::GetModuleHandleW(None).unwrap();
            let window_class = windows::core::w!("IndicatorOverlayClass");
            let wc = WNDCLASSEXW {
                cbSize: std::mem::size_of::<WNDCLASSEXW>() as u32,
                lpfnWndProc: Some(DefWindowProcW),
                hInstance: instance.into(),
                lpszClassName: window_class,
                ..Default::default()
            };
            RegisterClassExW(&wc);

            CreateWindowExW(
                WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                window_class, None, WS_POPUP, 0, 0, size, size, None, None, instance, None,
            )
        }
    }

    pub fn update(&self, x: i32, y: i32, is_chinese: bool, is_caps: bool, caret_h: i32) {
        // --- CAPS_INDICATOR_START ---
        let color = if is_caps { self.color_caps } else if is_chinese { self.color_cn } else { self.color_en };
        // --- CAPS_INDICATOR_END ---

        unsafe {
            let screen_dc = GetDC(None);
            let mem_dc = CreateCompatibleDC(screen_dc);
            let bmi = BITMAPINFO {
                bmiHeader: BITMAPINFOHEADER {
                    biSize: std::mem::size_of::<BITMAPINFOHEADER>() as u32,
                    biWidth: self.size, biHeight: self.size, biPlanes: 1, biBitCount: 32, biCompression: BI_RGB.0, ..Default::default()
                }, ..Default::default()
            };

            let mut ppv_bits = null_mut();
            let h_bitmap = CreateDIBSection(mem_dc, &bmi, DIB_RGB_COLORS, &mut ppv_bits, None, 0).unwrap_or_default();
            let old_bitmap = SelectObject(mem_dc, h_bitmap);

            let mut graphics = null_mut();
            GdipCreateFromHDC(mem_dc, &mut graphics);
            GdipSetSmoothingMode(graphics, SmoothingModeAntiAlias);
            let mut brush = null_mut();
            GdipCreateSolidFill(color, &mut brush);
            GdipFillEllipse(graphics, brush as *mut GpBrush, 0.0, 0.0, self.size as f32, self.size as f32);
            GdipDeleteBrush(brush as *mut GpBrush);
            GdipDeleteGraphics(graphics);

            let dest_point = POINT { x: x + self.offset_x - self.size / 2, y: y + caret_h + self.offset_y - self.size / 2 };
            let src_point = POINT::default();
            let size = SIZE { cx: self.size, cy: self.size };
            let blend = windows::Win32::Graphics::Gdi::BLENDFUNCTION { blend_op: 0, blend_flags: 0, source_constant_alpha: 255, alpha_format: 1 };

            UpdateLayeredWindow(self.hwnd, screen_dc, Some(&dest_point), Some(&size), mem_dc, Some(&src_point), COLORREF(0), Some(&blend), ULW_ALPHA);

            SelectObject(mem_dc, old_bitmap);
            DeleteObject(h_bitmap);
            DeleteDC(mem_dc);
            ReleaseDC(None, screen_dc);

            SetWindowPos(self.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

            let mut msg = MSG::default();
            while PeekMessageW(&mut msg, self.hwnd, 0, 0, PM_REMOVE).into() {
                let _ = TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
    }

    pub fn show(&self) { unsafe { ShowWindow(self.hwnd, SW_SHOW); } }
    pub fn hide(&self) { unsafe { ShowWindow(self.hwnd, SW_HIDE); } }
    pub fn cleanup(&self) { unsafe { DestroyWindow(self.hwnd); GdiplusShutdown(self.gdi_token); } }
}
