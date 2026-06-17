import datetime as dt
from clawd_mochi.core import fmt

MIN = 60 * 1000


def test_fmt_dur_minutes_only():
    assert fmt.fmt_dur(5 * MIN) == "5m"


def test_fmt_dur_hours_and_minutes():
    assert fmt.fmt_dur((2 * 60 + 15) * MIN) == "2h15m"


def test_fmt_dur_rounds_to_nearest_minute():
    assert fmt.fmt_dur(90 * 1000) == "2m"  # 90s → 1.5min → round 2


def test_fmt_dur_zero():
    assert fmt.fmt_dur(0) == "0m"


def test_hhmm_local_time():
    ts = int(dt.datetime(2026, 6, 17, 9, 5).timestamp() * 1000)
    assert fmt.hhmm(ts) == "09:05"
