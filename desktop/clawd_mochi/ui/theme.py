"""暖调设计系统：调色板 + 全局 QSS + 阴影助手。

视觉真值对齐 web/mockups/desktop-app.html（沿用 panel.html 的 :root 配色）。
"""
from PySide6.QtWidgets import QGraphicsDropShadowEffect
from PySide6.QtGui import QColor

# —— 调色板（与 panel.html :root 一致）——
BG1 = "#FBEFE0"
BG2 = "#F1DEC6"
SHELL = "#FFFDFA"
SIDE = "#FBF2E7"
LINE = "#EEE0CE"
LINE2 = "#F3E8D9"
INK = "#43342A"
MUTED = "#9C8A78"
FAINT = "#BCAB97"
CORAL = "#D97757"
CORAL_DEEP = "#C4632F"
CORAL_SOFT = "#FBEEE2"
OK = "#3FAE7A"
OKBG = "#E3F3EA"
BAD = "#E0705F"
BADBG = "#F7E2DD"

# —— 全局样式表 ——
QSS = f"""
* {{ font-family: "Segoe UI", "Microsoft YaHei UI", system-ui; color: {INK}; }}

#Root {{
    background: qlineargradient(x1:0, y1:0, x2:0.6, y2:1, stop:0 {BG1}, stop:1 {BG2});
}}

/* —— 侧栏 —— */
#Side {{ background: {SIDE}; border-right: 1px solid {LINE}; }}
#NavLabel {{ color: {FAINT}; font-size: 10px; font-weight: 700; letter-spacing: 2px; }}

QPushButton#Nav {{
    text-align: left; padding: 9px 12px; border: none; border-radius: 11px;
    background: transparent; color: #6E5D4E; font-size: 14px; font-weight: 600;
}}
QPushButton#Nav:hover {{ background: #F5E8D8; }}
QPushButton#Nav:checked {{ background: {CORAL_SOFT}; color: {CORAL_DEEP}; }}

/* —— 设备状态脚 —— */
#DeviceFoot {{ background: {SHELL}; border: 1px solid {LINE2}; border-radius: 13px; }}
#Dot {{ border-radius: 5px; background: #C9B8A4; }}
#Dot[s="ok"] {{ background: {OK}; }}
#Dot[s="bad"] {{ background: {BAD}; }}
#DeviceStatus {{ font-size: 12px; font-weight: 600; color: {INK}; }}
#DeviceIp {{ font-size: 11px; color: {FAINT}; }}
QPushButton#FootBtn {{
    background: {CORAL_SOFT}; border: 1px solid {LINE}; border-radius: 9px;
    color: {CORAL_DEEP}; font-size: 12px; font-weight: 700; padding: 6px;
}}
QPushButton#FootBtn:hover {{ background: #F7E2CE; }}

/* —— 标题 —— */
#H1 {{ font-size: 23px; font-weight: 800; color: {INK}; }}
#HeadSub {{ font-size: 12px; color: {FAINT}; }}

/* —— 卡片 —— */
#Card {{ background: {SHELL}; border: 1px solid {LINE}; border-radius: 16px; }}

/* hero */
#HeroLead {{ color: {CORAL_DEEP}; font-size: 12px; font-weight: 700; }}
#HeroTime {{ color: {INK}; }}
#HeroMeta {{ color: {FAINT}; font-size: 12px; }}
#TileNum {{ color: {INK}; font-size: 30px; font-weight: 800; }}
#TileLabel {{ color: {MUTED}; font-size: 12px; font-weight: 600; }}
#TileSub {{ color: {FAINT}; font-size: 11px; }}
#Story {{ color: {MUTED}; font-size: 13px; }}
#Empty {{ color: {MUTED}; font-size: 15px; }}

/* —— 可选列表行 —— */
#Row {{ background: {SHELL}; border: 1px solid {LINE}; border-radius: 14px; }}
#Row[selected="true"] {{ border: 1px solid {CORAL}; background: {CORAL_SOFT}; }}
#Row[hoverable="true"]:hover {{ border: 1px solid #E6CFB4; }}
#IconBox {{ background: #FBF1E5; border: 1px solid {LINE2}; border-radius: 11px; }}
#Row[selected="true"] #IconBox {{ background: #FFFFFF; border: 1px solid #F0D2BB; }}
#RowName {{ font-size: 14px; font-weight: 700; color: {INK}; }}
#RowMeta {{ font-size: 11px; color: {FAINT}; }}

#Pill {{ font-size: 11px; font-weight: 700; border-radius: 11px; padding: 3px 10px;
        background: {OKBG}; color: #2E7D55; }}
#Pill[live="false"] {{ background: #F1E6D6; color: {FAINT}; }}

/* —— 输入框 —— */
QLineEdit#Field {{
    background: #FFFFFF; border: 1px solid {LINE}; border-radius: 11px;
    padding: 9px 13px; font-size: 13px; color: {INK};
}}
QLineEdit#Field:focus {{ border: 1px solid {CORAL}; }}
#FieldLabel {{ font-size: 13px; font-weight: 600; color: #6E5D4E; }}

/* —— 主按钮 —— */
QPushButton#Primary {{
    background: qlineargradient(x1:0, y1:0, x2:0.5, y2:1, stop:0 #E0875F, stop:1 {CORAL_DEEP});
    color: #FFFFFF; border: none; border-radius: 11px; padding: 9px 16px;
    font-size: 13px; font-weight: 700;
}}
QPushButton#Primary:hover {{ background: qlineargradient(x1:0, y1:0, x2:0.5, y2:1, stop:0 #E8956D, stop:1 #D06D38); }}
QPushButton#Primary:disabled {{ background: #EDE2D2; color: {FAINT}; }}

/* —— toast —— */
#Toast {{ font-size: 13px; font-weight: 600; border-radius: 12px; padding: 11px 14px; }}
#Toast[kind="ok"] {{ background: {OKBG}; color: #2E7D55; border: 1px solid #CDE9D8; }}
#Toast[kind="bad"] {{ background: {BADBG}; color: #9C3A2C; border: 1px solid #EFC9C0; }}
#Note {{ font-size: 12px; color: {MUTED}; }}

/* —— 固件升级页 —— */
#VTile {{ background: {SHELL}; border: 1px solid {LINE2}; border-radius: 13px; }}
#VKey {{ font-size: 11px; font-weight: 700; letter-spacing: 1px; color: {FAINT}; }}
#VVal {{ font-size: 23px; font-weight: 800; color: {INK}; }}
#VVal[accent="true"] {{ color: {CORAL_DEEP}; }}
#VMeta {{ font-size: 11px; color: {MUTED}; }}
#VArrow {{ font-size: 20px; color: {FAINT}; }}
#PortLabel {{ font-size: 12px; font-weight: 700; color: {MUTED}; }}
QComboBox#Port {{
    background: {SHELL}; border: 1px solid {LINE}; border-radius: 9px;
    padding: 7px 11px; font-size: 13px; color: {INK};
}}
QComboBox#Port:focus {{ border: 1px solid {CORAL}; }}
QComboBox#Port::drop-down {{ border: none; width: 22px; }}
QPushButton#Mini {{
    background: {CORAL_SOFT}; border: 1px solid {LINE}; border-radius: 9px;
    padding: 7px 12px; font-size: 12px; font-weight: 700; color: {CORAL_DEEP};
}}
QPushButton#Mini:hover {{ background: #F7E2CE; }}
#Hint {{ font-size: 12px; color: {FAINT}; }}
QProgressBar#Prog {{
    background: #F1E4D2; border: 1px solid {LINE2}; border-radius: 7px;
    height: 12px; text-align: center; color: transparent;
}}
QProgressBar#Prog::chunk {{
    border-radius: 6px;
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #E8915F, stop:1 {CORAL_DEEP});
}}
#ProgMeta {{ font-size: 11px; font-weight: 600; color: {MUTED}; }}
QTextEdit#Log {{
    background: #2C231C; color: #E9D9C6; border: none; border-radius: 11px;
    padding: 8px 10px; font-family: "Cascadia Code", Consolas, monospace; font-size: 11px;
}}
#Banner {{ font-size: 13px; font-weight: 600; border-radius: 11px; padding: 10px 14px; }}
#Banner[kind="ok"] {{ background: {OKBG}; color: #2E7D55; }}
#Banner[kind="bad"] {{ background: {BADBG}; color: #9C3A2C; }}
#Banner[kind="warn"] {{ background: #F8EED6; color: #8A5E16; }}
"""


def card_shadow(widget) -> None:
    """给卡片加柔和投影（QSS 无 box-shadow，用图形效果代替）。"""
    eff = QGraphicsDropShadowEffect(widget)
    eff.setBlurRadius(22)
    eff.setXOffset(0)
    eff.setYOffset(4)
    eff.setColor(QColor(150, 95, 55, 28))
    widget.setGraphicsEffect(eff)


def repolish(widget) -> None:
    """动态属性变化后重新应用 QSS。"""
    widget.style().unpolish(widget)
    widget.style().polish(widget)
    widget.update()
