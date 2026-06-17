"""agent.json / agent-state.json 读写。

约定（与 Node 控制台一致）：
  agent.json       面板独占写 / hook 只读：{ activeTool, tools, presence }
  agent-state.json hook 独占写 / 面板只读：{ lastSeen }
"""
import json
import os

from clawd_mochi.core.paths import config_dir

DEFAULT_TOOLS = [
    {"id": "cc", "name": "Claude Code", "installed": True},
    {"id": "cursor", "name": "Cursor", "installed": True},
]
PRESENCE_VALUES = ["auto", "meeting", "toilet", "solder", "rest"]


def read_config(directory: str | None = None) -> dict:
    directory = directory or config_dir()
    try:
        with open(os.path.join(directory, "agent.json"), encoding="utf-8") as f:
            cfg = json.load(f)
    except (OSError, ValueError):
        return {"activeTool": None, "tools": DEFAULT_TOOLS, "presence": "auto"}
    active = cfg.get("activeTool")
    tools = cfg.get("tools")
    presence = cfg.get("presence")
    return {
        "activeTool": active if isinstance(active, str) and active else None,
        "tools": tools if isinstance(tools, list) and tools else DEFAULT_TOOLS,
        "presence": presence if isinstance(presence, str) and presence else "auto",
    }


def _atomic_write_json(path: str, obj: dict) -> None:
    """temp+rename 原子写；Windows 偶发 rename 失败 → 退化直接覆盖（单写者无竞争）。"""
    data = json.dumps(obj, ensure_ascii=False, indent=2)
    tmp = f"{path}.{os.getpid()}.tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write(data)
    try:
        os.replace(tmp, path)
    except OSError:
        try:
            with open(path, "w", encoding="utf-8") as f:
                f.write(data)
        except OSError:
            pass
        try:
            os.unlink(tmp)
        except OSError:
            pass


def write_active_tool(tool: str | None, directory: str | None = None) -> dict:
    directory = directory or config_dir()
    cfg = read_config(directory)
    cfg["activeTool"] = tool if isinstance(tool, str) and tool else None
    os.makedirs(directory, exist_ok=True)
    _atomic_write_json(os.path.join(directory, "agent.json"), cfg)
    return cfg


def write_presence(presence: str, directory: str | None = None) -> dict:
    directory = directory or config_dir()
    cfg = read_config(directory)
    cfg["presence"] = presence if presence in PRESENCE_VALUES else "auto"
    os.makedirs(directory, exist_ok=True)
    _atomic_write_json(os.path.join(directory, "agent.json"), cfg)
    return cfg


def read_state(directory: str | None = None) -> dict:
    directory = directory or config_dir()
    try:
        with open(os.path.join(directory, "agent-state.json"), encoding="utf-8") as f:
            st = json.load(f)
        return {"lastSeen": (st or {}).get("lastSeen") or {}}
    except (OSError, ValueError):
        return {"lastSeen": {}}
