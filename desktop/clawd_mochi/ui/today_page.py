import re

from PySide6.QtCore import Qt
from PySide6.QtWidgets import QWidget, QVBoxLayout, QHBoxLayout, QLabel

from clawd_mochi.core import events, fmt
from clawd_mochi.core.paths import today_str
from clawd_mochi.ui import theme
from clawd_mochi.ui.widgets import make_card, MoodFace


def _time_html(ms: int) -> str:
    s = fmt.fmt_dur(ms)
    out = ""
    for num, unit in re.findall(r"(\d+)([hm])", s):
        out += (f"<span style='font-size:52px;font-weight:800;letter-spacing:-2px'>{num}</span>"
                f"<span style='font-size:23px;color:{theme.CORAL};font-weight:800'> {unit} </span>")
    return out or "<span style='font-size:52px;font-weight:800'>0</span>"


class TodayPage(QWidget):
    def __init__(self):
        super().__init__()
        root = QVBoxLayout(self)
        root.setContentsMargins(30, 24, 30, 28)
        root.setSpacing(14)

        head = QVBoxLayout()
        head.setSpacing(3)
        h1 = QLabel("今日陪伴")
        h1.setObjectName("H1")
        self._date = QLabel("")
        self._date.setObjectName("HeadSub")
        head.addWidget(h1)
        head.addWidget(self._date)
        root.addLayout(head)

        # —— hero 卡 ——
        self._hero = make_card()
        hl = QHBoxLayout(self._hero)
        hl.setContentsMargins(22, 18, 22, 18)
        left = QVBoxLayout()
        left.setSpacing(3)
        lead = QLabel("它陪你的时间")
        lead.setObjectName("HeroLead")
        self._time = QLabel()
        self._time.setObjectName("HeroTime")
        self._time.setTextFormat(Qt.RichText)
        self._meta = QLabel()
        self._meta.setObjectName("HeroMeta")
        self._meta.setTextFormat(Qt.RichText)
        left.addWidget(lead)
        left.addWidget(self._time)
        left.addWidget(self._meta)
        hl.addLayout(left)
        hl.addStretch(1)
        hl.addWidget(MoodFace(78), 0, Qt.AlignVCenter)
        root.addWidget(self._hero)

        # —— 三个 tile ——
        self._tiles_row = QHBoxLayout()
        self._tiles_row.setSpacing(14)
        self._tile_vals = []
        for label, sub in [("会话", "SessionStart 次数"),
                            ("提问", "你向它发起"),
                            ("最长连续专注", "不被 20min 空档打断")]:
            card = make_card()
            cl = QVBoxLayout(card)
            cl.setContentsMargins(18, 16, 18, 16)
            cl.setSpacing(6)
            num = QLabel("–")
            num.setObjectName("TileNum")
            lab = QLabel(label)
            lab.setObjectName("TileLabel")
            s = QLabel(sub)
            s.setObjectName("TileSub")
            cl.addWidget(num)
            cl.addWidget(lab)
            cl.addWidget(s)
            self._tiles_row.addWidget(card, 1)
            self._tile_vals.append(num)
        root.addLayout(self._tiles_row)

        # —— 叙事卡 ——
        self._story_card = make_card()
        sl = QVBoxLayout(self._story_card)
        sl.setContentsMargins(20, 16, 20, 16)
        self._story = QLabel()
        self._story.setObjectName("Story")
        self._story.setWordWrap(True)
        self._story.setTextFormat(Qt.RichText)
        sl.addWidget(self._story)
        root.addWidget(self._story_card)

        # —— 空状态 ——
        self._empty = QLabel("今天还没开始陪伴，去写点代码吧 🐾")
        self._empty.setObjectName("Empty")
        self._empty.setAlignment(Qt.AlignCenter)
        root.addWidget(self._empty)

        root.addStretch(1)
        self.refresh()

    def _set_content_visible(self, on: bool):
        self._hero.setVisible(on)
        self._story_card.setVisible(on)
        for i in range(self._tiles_row.count()):
            w = self._tiles_row.itemAt(i).widget()
            if w:
                w.setVisible(on)
        self._empty.setVisible(not on)

    def refresh(self):
        date = today_str()
        t = events.aggregate_events(events.read_events(date))
        if not t["firstTs"]:
            self._date.setText(date)
            self._set_content_visible(False)
            return
        self._set_content_visible(True)
        tool = t["tool"] or "AI"
        self._date.setText(f"{date} · 首次 {fmt.hhmm(t['firstTs'])} · 最近 {fmt.hhmm(t['lastTs'])}")
        self._time.setText(_time_html(t["activeMs"]))
        self._meta.setText(f"和 <b style='color:{theme.CORAL_DEEP}'>{tool}</b> 一起度过的一天")
        self._tile_vals[0].setText(str(t["sessions"]))
        self._tile_vals[1].setText(str(t["asks"]))
        self._tile_vals[2].setText(fmt.fmt_dur(t["longestFocusMs"]))

        story = (f"今天和 <b style='color:{theme.CORAL_DEEP}'>{tool}</b> 结对 "
                 f"<b style='color:{theme.CORAL_DEEP}'>{fmt.fmt_dur(t['activeMs'])}</b> · "
                 f"<b style='color:{theme.CORAL_DEEP}'>{t['sessions']}</b> 次会话 · "
                 f"<b style='color:{theme.CORAL_DEEP}'>{t['asks']}</b> 次提问")
        if t["longestFocusMs"]:
            story += (f" · 最长连续专注 <b style='color:{theme.CORAL_DEEP}'>"
                      f"{fmt.fmt_dur(t['longestFocusMs'])}</b>")
        naps = sorted(t["naps"], key=lambda n: n["ms"], reverse=True)
        if naps:
            big = naps[0]
            story += (f"<br>{fmt.hhmm(big['start'])}–{fmt.hhmm(big['end'])} "
                      f"你休息时，它睡了 <b style='color:{theme.CORAL_DEEP}'>{fmt.fmt_dur(big['ms'])}</b> 💤")
        self._story.setText(story)
