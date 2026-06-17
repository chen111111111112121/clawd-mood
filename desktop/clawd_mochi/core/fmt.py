"""时长/时刻格式化。移植 panel.html 的 fmtDur / hhmm。"""
import datetime as dt


def fmt_dur(ms: float) -> str:
    m = round(ms / 60000)
    h, mm = divmod(m, 60)
    return f"{h}h{mm}m" if h else f"{mm}m"


def hhmm(ts_ms: float) -> str:
    d = dt.datetime.fromtimestamp(ts_ms / 1000)
    return f"{d.hour:02d}:{d.minute:02d}"
