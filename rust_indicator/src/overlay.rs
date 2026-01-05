//! GDI+ 分层透明窗口渲染模块 - 负责绘制跟随光标的彩色圆点
use std::ptr::{null_mut, null};
use windows::core::{w, PCWSTR};
use windows::Win32::Foundation::{COLORREF, HWND, POINT, SIZE, LPARAM, LRESULT, WPARAM};
use windows::Win32::Graphics::Gdi::{
    CreateCompatibleDC, CreateDIBSection, DeleteDC, DeleteObject, GetDC, ReleaseDC, SelectObject,
    BITMAPINFO, BITMAPINFOHEADER, BI_RGB, DIB_RGB_COLORS,
    // 注意：以下三项在 windows-rs 0.58.0 中位于 Gdi 模块
    BLENDFUNCTION, AC_SRC_OVER, AC_SRC_ALPHA
};
use windows::Win32::Graphics::GdiPlus::*;
use windows::Win32::UI::WindowsAndMessaging::*;

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

/// 窗口过程函数：处理窗口消息。这里使用 extern "system" 确保符合 Windows ABI
extern "system" fn overlay_wndproc(hwnd: HWND, msg: u32, wparam: WPARAM, lparam: LPARAM) -> LRESULT {
    unsafe { DefWindowProcW(hwnd, msg, wparam, lparam) }
}

impl IndicatorOverlay {
    /// 初始化窗口并启动 GDI+
    pub fn new(_name: &str, size: i32, color_cn: u32, color_en: u32, color_caps: u32, offset_x: i32, offset_y: i32) -> Self {
        let mut token = 0;
        let input = GdiplusStartupInput { GdiplusVersion: 1, ..Default::default() };
        unsafe { let _ = GdiplusStartup(&mut token, &input, null_mut()); }

        let hwnd = unsafe {
            let h_inst = windows::Win32::System::LibraryLoader::GetModuleHandleW(None).unwrap();
            let cls_name = w!("IndicatorOverlayClass");
            let wc = WNDCLASSEXW {
                cbSize: std::mem::size_of::<WNDCLASSEXW>() as u32,
                lpfnWndProc: Some(overlay_wndproc),
                hInstance: h_inst.into(),
                lpszClassName: cls_name,
                ..Default::default()
            };
            RegisterClassExW(&wc);
            // 创建分层窗口 (WS_EX_LAYERED)，支持透明度，且不拦截鼠标点击 (WS_EX_TRANSPARENT)
            CreateWindowExW(
                WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                cls_name, PCWSTR(null()), WS_POPUP,
                0, 0, size, size, None, None, h_inst, None
            ).expect("Create Window Failed")
        };
        Self { hwnd, size, color_cn, color_en, color_caps, offset_x, offset_y, gdi_token: token }
    }

    /// 更新窗口位置并重新绘制颜色点
    pub fn update(&self, x: i32, y: i32, is_chinese: bool, is_caps: bool, caret_h: i32) {
        // 颜色优先级：大写锁定 > 中文模式 > 英文模式
        let color = if is_caps { self.color_caps } else if is_chinese { self.color_cn } else { self.color_en };
        
        unsafe {
            let screen_dc = GetDC(None);
            let mem_dc = CreateCompatibleDC(screen_dc);
            
            // 创建位图用于 GDI+ 绘制
            let bmi = BITMAPINFO {
                bmiHeader: BITMAPINFOHEADER {
                    biSize: std::mem::size_of::<BITMAPINFOHEADER>() as u32,
                    biWidth: self.size, biHeight: self.size, biPlanes: 1, biBitCount: 32, 
                    biCompression: BI_RGB.0, ..Default::default()
                }, ..Default::default()
            };
            let mut bits = null_mut();
            let h_bmp = CreateDIBSection(mem_dc, &bmi, DIB_RGB_COLORS, &mut bits, None, 0).unwrap();
            let old_bmp = SelectObject(mem_dc, h_bmp);
            
            // 使用 GDI+ 绘制抗锯齿圆点
            let mut g = null_mut();
            GdipCreateFromHDC(mem_dc, &mut g);
            GdipSetSmoothingMode(g, SmoothingModeAntiAlias);
            let mut b = null_mut();
            GdipCreateSolidFill(color, &mut b);
            GdipFillEllipse(g, b as *mut GpBrush, 0.0, 0.0, self.size as f32, self.size as f32);
            GdipDeleteBrush(b as *mut GpBrush);
            GdipDeleteGraphics(g);

            // 计算窗口屏幕位置
            let dest = POINT { x: x + self.offset_x - self.size/2, y: y + caret_h + self.offset_y - self.size/2 };
            let blend = BLENDFUNCTION { 
                BlendOp: AC_SRC_OVER as u8, 
                BlendFlags: 0, 
                SourceConstantAlpha: 255, 
                AlphaFormat: AC_SRC_ALPHA as u8 
            };
            
            // 核心调用：更新分层窗口的透明像素内容
            let _ = UpdateLayeredWindow(
                self.hwnd, screen_dc, Some(&dest), 
                Some(&SIZE { cx: self.size, cy: self.size }), 
                mem_dc, Some(&POINT::default()), COLORREF(0), 
                Some(&blend), ULW_ALPHA
            );
            
            // 清理 GDI 句柄
            SelectObject(mem_dc, old_bmp);
            let _ = DeleteObject(h_bmp);
            let _ = DeleteDC(mem_dc);
            ReleaseDC(None, screen_dc);
            
            // 确保窗口始终在最前端
            let _ = SetWindowPos(self.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
    
    pub fn show(&self) { unsafe { let _ = ShowWindow(self.hwnd, SW_SHOW); } }
    pub fn hide(&self) { unsafe { let _ = ShowWindow(self.hwnd, SW_HIDE); } }
    pub fn cleanup(&self) { unsafe { let _ = DestroyWindow(self.hwnd); GdiplusShutdown(self.gdi_token); } }
}
