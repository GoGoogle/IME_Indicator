//! 鼠标光标形状检测模块

use std::collections::HashSet;
use windows::Win32::Foundation::HINSTANCE;
use windows::Win32::UI::WindowsAndMessaging::{
    GetCursorInfo, LoadCursorW, CURSORINFO, CURSOR_SHOWING, HCURSOR,
};
use windows::core::PCWSTR;

/// 鼠标形状检测器
pub struct CursorDetector {
    /// 目标光标句柄集合
    target_cursor_handles: HashSet<isize>,
}

impl CursorDetector {
    /// 创建新的检测器
    pub fn new(target_cursor_ids: &[u32]) -> Self {
        let handles = Self::get_shared_cursor_handles(target_cursor_ids);
        Self {
            target_cursor_handles: handles,
        }
    }

    /// 获取目标光标在系统中的共享句柄
    fn get_shared_cursor_handles(cursor_ids: &[u32]) -> HashSet<isize> {
        let mut handles = HashSet::new();
        for &cid in cursor_ids {
            unsafe {
                let h = LoadCursorW(HINSTANCE::default(), PCWSTR(cid as *const u16));
                if let Ok(cursor) = h {
                    if !cursor.0.is_null() {
                        handles.insert(cursor.0 as isize);
                    }
                }
            }
        }
        handles
    }

    /// 检测当前光标是否为目标形状之一
    pub fn is_target_cursor(&self) -> bool {
        unsafe {
            let mut ci = CURSORINFO {
                cbSize: std::mem::size_of::<CURSORINFO>() as u32,
                flags: CURSOR_SHOWING,
                hCursor: HCURSOR::default(),
                ptScreenPos: Default::default(),
            };
            
            if GetCursorInfo(&mut ci).is_ok() {
                return self.target_cursor_handles.contains(&(ci.hCursor.0 as isize));
            }
        }
        false
    }
}
