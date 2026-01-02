from win32_api import OCR_IBEAM, OCR_NORMAL

# ============ 通用设置 ============
# 状态检测间隔 (秒)
STATE_POLL_INTERVAL = 0.1  # 100ms
# 位置追踪间隔 (秒)
TRACK_POLL_INTERVAL = 0.01 # 10ms

# ============ 1. 文本光标提示 (Caret Indicator) ============
CARET_ENABLE = True           # 是否启用光标提示
CARET_COLOR_CN = "#FF7800A0"  # 中文颜色 (橙色, 透明度 A0)
CARET_COLOR_EN = "#0078FF30"  # 英文颜色 (蓝色, 透明度 30)
CARET_SIZE = 8                # 提示器大小
CARET_OFFSET_X = 0            # 提示器 X 偏移
CARET_OFFSET_Y = 0            # 提示器 Y 偏移 (为 0 时紧贴光标底部)
CARET_SHOW_EN = True          # 英文状态下是否也显示

# ============ 2. 鼠标跟随提示 (Mouse Indicator) ============
MOUSE_ENABLE = False           # 是否启用鼠标提示
MOUSE_COLOR_CN = "#FF7800C8"  # 中文颜色
MOUSE_COLOR_EN = "#0078FF30"  # 英文颜色
MOUSE_SIZE = 8                # 提示器大小
MOUSE_OFFSET_X = 2            # 提示器 X 偏移
MOUSE_OFFSET_Y = 18           # 提示器 Y 偏移
MOUSE_SHOW_EN = False         # 英文状态下是否也显示

# 仅在以下鼠标形状时显示 (OCR_IBEAM: I型光标, OCR_NORMAL: 标准箭头)
MOUSE_TARGET_CURSORS = [OCR_IBEAM, OCR_NORMAL]
