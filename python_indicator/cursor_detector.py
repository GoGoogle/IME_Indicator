import ctypes
from win32_api import user32, CURSORINFO, HCURSOR
from ctypes import byref, sizeof, cast, wintypes

class CursorDetector:
    """鼠标光标形状检测类"""
    def __init__(self, target_cursor_ids):
        self.target_cursor_ids = target_cursor_ids
        self.shared_cursor_handles = self._get_shared_cursor_handles()

    def _get_shared_cursor_handles(self):
        """获取目标光标在系统中的共享句柄"""
        handles = set()
        for cid in self.target_cursor_ids:
            h = user32.LoadCursorW(None, cast(cid, wintypes.LPCWSTR))
            if h:
                handles.add(h)
        return handles

    def is_target_cursor(self):
        """检测当前光标是否为目标形状之一"""
        ci = CURSORINFO()
        ci.cbSize = sizeof(CURSORINFO)
        if user32.GetCursorInfo(byref(ci)):
            return ci.hCursor in self.shared_cursor_handles
        return False
