from PySide6.QtCore import Qt
from PySide6.QtWidgets import QWidget, QVBoxLayout, QLabel

from clawd_mochi.core import events, fmt
from clawd_mochi.core.paths import today_str


class TodayPage(QWidget):
    def __init__(self):
        super().__init__()
        layout = QVBoxLayout(self)
        layout.setContentsMargins(28, 24, 28, 24)
        self._title = QLabel("<h1>今日陪伴</h1>")
        self._body = QLabel()
        self._body.setWordWrap(True)
        self._body.setTextFormat(Qt.RichText)
        self._body.setAlignment(Qt.AlignTop)
        layout.addWidget(self._title)
        layout.addWidget(self._body, 1)
        self.refresh()

    def refresh(self):
        date = today_str()
        t = events.aggregate_events(events.read_events(date))
        if not t["firstTs"]:
            self._body.setText("<p style='color:#9C8A78'>今天还没开始陪伴，去写点代码吧 🐾</p>")
            return
        tool = t["tool"] or "AI"
        story = (f"今天和 <b>{tool}</b> 结对 <b>{fmt.fmt_dur(t['activeMs'])}</b> · "
                 f"<b>{t['sessions']}</b> 次会话 · <b>{t['asks']}</b> 次提问")
        if t["longestFocusMs"]:
            story += f" · 最长连续专注 <b>{fmt.fmt_dur(t['longestFocusMs'])}</b>"
        naps = sorted(t["naps"], key=lambda n: n["ms"], reverse=True)
        if naps:
            big = naps[0]
            story += (f"<br>{fmt.hhmm(big['start'])}–{fmt.hhmm(big['end'])} "
                      f"你休息时，它睡了 <b>{fmt.fmt_dur(big['ms'])}</b> 💤")
        html = (
            f"<p style='font-size:13px;color:#9C8A78'>首次 {fmt.hhmm(t['firstTs'])} · "
            f"最近 {fmt.hhmm(t['lastTs'])}</p>"
            f"<p style='font-size:40px;font-weight:800;margin:6px 0'>"
            f"{fmt.fmt_dur(t['activeMs'])}</p>"
            f"<p style='font-size:13px;color:#C4632F'>它陪你的时间</p>"
            f"<hr><p style='line-height:1.7;color:#6E5D4E'>{story}</p>"
            f"<p style='color:#9C8A78'>会话 {t['sessions']} · 提问 {t['asks']} · "
            f"最长连续专注 {fmt.fmt_dur(t['longestFocusMs'])}"
            f"（不被 20 分钟以上空档打断的最长一段）</p>"
        )
        self._body.setText(html)
