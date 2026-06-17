from clawd_mochi.core import events

MIN = 60 * 1000
BASE = 1_700_000_000_000  # 任意基准；断言均为相对值


def at(m):
    return BASE + m * MIN


def test_empty_returns_zeros():
    r = events.aggregate_events([])
    assert r["tool"] is None
    assert r["activeMs"] == 0
    assert r["sessions"] == 0
    assert r["asks"] == 0
    assert r["longestFocusMs"] == 0
    assert r["naps"] == []
    assert r["segments"] == []


def test_counts_sessions_and_asks():
    r = events.aggregate_events([
        {"ts": at(0), "tool": "cc", "event": "SessionStart"},
        {"ts": at(1), "tool": "cc", "event": "UserPromptSubmit"},
        {"ts": at(2), "tool": "cursor", "event": "beforeSubmitPrompt"},
        {"ts": at(3), "tool": "cc", "event": "Stop"},
    ])
    assert r["sessions"] == 1
    assert r["asks"] == 2
    assert r["tool"] == "cc"  # 出现最多


def test_cursor_session_event_counts():
    r = events.aggregate_events([
        {"ts": at(0), "tool": "cursor", "event": "sessionStart"},
        {"ts": at(1), "tool": "cursor", "event": "beforeSubmitPrompt"},
    ])
    assert r["sessions"] == 1
    assert r["asks"] == 1


def test_gap_splits_segments():
    r = events.aggregate_events([
        {"ts": at(0), "tool": "cc", "event": "UserPromptSubmit"},
        {"ts": at(10), "tool": "cc", "event": "UserPromptSubmit"},
        {"ts": at(40), "tool": "cc", "event": "UserPromptSubmit"},
        {"ts": at(45), "tool": "cc", "event": "Stop"},
    ])
    assert len(r["segments"]) == 2
    assert r["activeMs"] == (10 + 5) * MIN
    assert r["longestFocusMs"] == 10 * MIN
    assert len(r["naps"]) == 1
    assert r["naps"][0]["ms"] == 30 * MIN
    assert r["firstTs"] == at(0)
    assert r["lastTs"] == at(45)


def test_exactly_20min_same_segment():
    r = events.aggregate_events([
        {"ts": at(0), "tool": "cc", "event": "Stop"},
        {"ts": at(20), "tool": "cc", "event": "Stop"},
    ])
    assert len(r["segments"]) == 1
    assert len(r["naps"]) == 0
    assert r["activeMs"] == 20 * MIN


def test_unsorted_input_sorted():
    r = events.aggregate_events([
        {"ts": at(5), "tool": "cc", "event": "Stop"},
        {"ts": at(0), "tool": "cc", "event": "UserPromptSubmit"},
    ])
    assert r["firstTs"] == at(0)
    assert r["activeMs"] == 5 * MIN


def test_read_events_missing_file(tmp_path):
    assert events.read_events("2026-06-17", str(tmp_path)) == []


def test_read_events_skips_bad_lines(tmp_path):
    (tmp_path / "events-2026-06-17.jsonl").write_text(
        '{"ts":1,"tool":"cc","event":"Stop"}\n'
        'not json\n'
        '\n'
        '{"ts":2,"tool":"cc","event":"Stop"}\n',
        encoding="utf-8")
    out = events.read_events("2026-06-17", str(tmp_path))
    assert [e["ts"] for e in out] == [1, 2]
