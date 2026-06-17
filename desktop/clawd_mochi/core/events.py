"""今日陪伴事件聚合。移植 agent/clawd-agent.js 的 readEvents / aggregateEvents。

事件日志由 Hook 按天 append：~/.clawd-mood/events-YYYY-MM-DD.jsonl，每行 {ts, tool, event}。
"""
import json
import os

from clawd_mochi.core.paths import config_dir

IDLE_GAP_MS = 20 * 60 * 1000
SESSION_EVENTS = {"SessionStart", "sessionStart"}
ASK_EVENTS = {"UserPromptSubmit", "beforeSubmitPrompt"}


def read_events(date: str, directory: str | None = None) -> list[dict]:
    directory = directory or config_dir()
    try:
        with open(os.path.join(directory, f"events-{date}.jsonl"), encoding="utf-8") as f:
            raw = f.read()
    except OSError:
        return []
    out = []
    for line in raw.split("\n"):
        t = line.strip()
        if not t:
            continue
        try:
            out.append(json.loads(t))
        except ValueError:
            pass  # 跳过坏行
    return out


def aggregate_events(evts: list[dict] | None, gap: int = IDLE_GAP_MS) -> dict:
    evs = sorted(
        (e for e in (evts or []) if e and isinstance(e.get("ts"), (int, float))),
        key=lambda e: e["ts"],
    )
    out = {
        "tool": None, "activeMs": 0, "sessions": 0, "asks": 0,
        "longestFocusMs": 0, "firstTs": None, "lastTs": None,
        "naps": [], "segments": [],
    }
    if not evs:
        return out
    out["firstTs"] = evs[0]["ts"]
    out["lastTs"] = evs[-1]["ts"]

    tool_count: dict[str, int] = {}
    for e in evs:
        if e.get("event") in SESSION_EVENTS:
            out["sessions"] += 1
        if e.get("event") in ASK_EVENTS:
            out["asks"] += 1
        tool = e.get("tool")
        if tool:
            tool_count[tool] = tool_count.get(tool, 0) + 1
    if tool_count:
        # 计数最多者；并列取最先出现（dict 保持插入序，max 返回首个最大值）
        out["tool"] = max(tool_count, key=lambda k: tool_count[k])

    seg_start = evs[0]["ts"]
    prev = evs[0]["ts"]
    for e in evs[1:]:
        if e["ts"] - prev > gap:
            out["segments"].append({"start": seg_start, "end": prev, "ms": prev - seg_start})
            out["naps"].append({"start": prev, "end": e["ts"], "ms": e["ts"] - prev})
            seg_start = e["ts"]
        prev = e["ts"]
    out["segments"].append({"start": seg_start, "end": prev, "ms": prev - seg_start})
    out["activeMs"] = sum(s["ms"] for s in out["segments"])
    out["longestFocusMs"] = max(s["ms"] for s in out["segments"])
    return out
