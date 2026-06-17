"""Hook 一键安装。移植 install-global.ps1，跨 OS。

Cursor ~/.cursor/hooks.json   : camelCase 事件，仅 stdin，命令不带事件名。
Claude ~/.claude/settings.json: PascalCase 事件，argv 传事件名，合并保留其它键。
Windows 命令用 cmd / powershell 包装；Unix 直接调用。
"""
import datetime as dt
import json
import os
import shutil

from clawd_mochi.core.paths import config_dir


class NodeNotFound(Exception):
    pass


_WINDOWS_NODE_CANDIDATES = [
    r"C:\Program Files\nodejs\node.exe",
    r"C:\Program Files (x86)\nodejs\node.exe",
    os.path.join(os.environ.get("LOCALAPPDATA", ""), "Programs", "node", "node.exe"),
]

CURSOR_EVENTS = [
    "sessionStart", "sessionEnd", "beforeSubmitPrompt",
    "preToolUse", "postToolUse", "postToolUseFailure",
    "subagentStart", "subagentStop", "preCompact", "stop",
]
CLAUDE_EVENTS = [
    "SessionStart", "SessionEnd", "UserPromptSubmit",
    "PreToolUse", "PostToolUse", "PostToolUseFailure",
    "SubagentStart", "SubagentStop", "PreCompact", "PostCompact",
    "Stop", "Notification",
]


def _cursor_command(node: str, hook: str, windows: bool) -> str:
    if windows:
        return f'cmd /d /s /c ""{node}" "{hook.replace(chr(92), "/")}""'
    return f'"{node}" "{hook}"'


def _claude_command(node: str, hook: str, event: str, windows: bool) -> str:
    if windows:
        return f'& "{node}" "{hook.replace(chr(92), "/")}" {event}'
    return f'"{node}" "{hook}" {event}'


def build_cursor_hooks(node: str, hook: str, windows: bool) -> dict:
    cmd = _cursor_command(node, hook, windows)
    entry = {"command": cmd, "timeout": 5}
    return {"version": 1, "hooks": {e: [dict(entry)] for e in CURSOR_EVENTS}}


def build_claude_hooks(node: str, hook: str, windows: bool) -> dict:
    out: dict = {}
    for e in CLAUDE_EVENTS:
        hook_obj = {"type": "command", "command": _claude_command(node, hook, e, windows),
                    "async": True, "timeout": 5}
        if windows:
            hook_obj = {"type": "command", "shell": "powershell",
                        "command": _claude_command(node, hook, e, windows),
                        "async": True, "timeout": 5}
        out[e] = [{"matcher": "", "hooks": [hook_obj]}]
    return out


def merge_claude_settings(existing: dict, mochi_hooks: dict) -> dict:
    settings = dict(existing or {})
    merged = dict(settings.get("hooks") or {})
    merged.update(mochi_hooks)
    settings["hooks"] = merged
    return settings


# —— 工具注册表：每款 AI 工具一条独立条目，新增工具只需在此加一行 ——
# config:        配置文件相对 home 的路径
# present_dirs:  本机"装有该工具"的判据目录（任一存在即视为存在）
# cli:           该工具的命令行名（在 PATH 中也视为存在）；无则 None
# style:         "cursor"=整文件覆盖；"claude"=合并进 settings.hooks
TOOLS = {
    "cc": {
        "name": "Claude Code",
        "config": (".claude", "settings.json"),
        "present_dirs": [(".claude",)],
        "cli": "claude",
        "style": "claude",
    },
    "cursor": {
        "name": "Cursor",
        "config": (".cursor", "hooks.json"),
        "present_dirs": [(".cursor",)],
        "cli": "cursor",
        "style": "cursor",
    },
}


def list_tools() -> list[str]:
    return list(TOOLS.keys())


def find_node(preferred: str | None = None) -> str:
    if preferred and os.path.exists(preferred):
        return os.path.abspath(preferred)
    found = shutil.which("node")
    if found:
        return found
    for p in _WINDOWS_NODE_CANDIDATES:
        if p and os.path.exists(p):
            return os.path.abspath(p)
    raise NodeNotFound("未找到 node，可在设置页手动指定路径或先安装 Node.js")


def locate_hook_source() -> str:
    """定位仓库内 clawd-hook.js。打包后由 PyInstaller 资源目录提供（后续计划）。"""
    env = os.environ.get("CLAWD_HOOK_SRC")
    if env and os.path.exists(env):
        return env
    here = os.path.dirname(os.path.abspath(__file__))
    # core -> clawd_mochi -> desktop -> 仓库根(clawd-mochi) -> hook/clawd-hook.js
    repo_hook = os.path.normpath(os.path.join(here, "..", "..", "..", "hook", "clawd-hook.js"))
    if os.path.exists(repo_hook):
        return repo_hook
    raise FileNotFoundError("未找到 clawd-hook.js，可设环境变量 CLAWD_HOOK_SRC 指定")


def tool_present(tool_id: str, home: str | None = None) -> bool:
    """本机是否装有这款 AI 工具（配置目录存在 或 CLI 在 PATH）。"""
    home = home or os.path.expanduser("~")
    spec = TOOLS[tool_id]
    for rel in spec["present_dirs"]:
        if os.path.isdir(os.path.join(home, *rel)):
            return True
    return bool(spec["cli"]) and shutil.which(spec["cli"]) is not None


def hook_installed(tool_id: str, home: str | None = None) -> bool:
    """这款工具的配置里是否已含我们的 hook（与 tool_present 是两回事）。"""
    home = home or os.path.expanduser("~")
    path = os.path.join(home, *TOOLS[tool_id]["config"])
    try:
        with open(path, encoding="utf-8") as f:
            return "clawd-hook" in f.read()
    except OSError:
        return False


def _write_text(path: str, text: str) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def _backup(path: str) -> str | None:
    if not os.path.exists(path):
        return None
    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    bak = f"{path}.bak.{stamp}"
    shutil.copyfile(path, bak)
    return bak


def _ensure_global_hook(device_ip: str, hook_source: str, config_directory: str) -> str:
    """把 hook 复制到 ~/.clawd-mood/hook/ 并写 device.json（幂等，多工具共用）。"""
    global_dir = os.path.join(config_directory, "hook")
    os.makedirs(global_dir, exist_ok=True)
    global_hook = os.path.join(global_dir, "clawd-hook.js")
    shutil.copyfile(hook_source, global_hook)
    _write_text(os.path.join(global_dir, "device.json"),
                json.dumps({"device_ip": device_ip}, ensure_ascii=False) + "\n")
    return global_hook


def install_hook_for(tool_id: str, device_ip: str, *, node: str | None = None,
                     hook_source: str | None = None, home: str | None = None,
                     config_directory: str | None = None, windows: bool | None = None,
                     force: bool = False) -> dict:
    """为单一工具独立安装 hook。force=True 跳过本机检测。"""
    if tool_id not in TOOLS:
        raise KeyError(tool_id)
    home = home or os.path.expanduser("~")
    config_directory = config_directory or config_dir()
    windows = os.name == "nt" if windows is None else windows

    if not force and not tool_present(tool_id, home=home):
        return {"ok": False, "tool": tool_id, "reason": "not_present"}

    node = node or find_node()
    hook_source = hook_source or locate_hook_source()
    global_hook = _ensure_global_hook(device_ip, hook_source, config_directory)

    spec = TOOLS[tool_id]
    config_path = os.path.join(home, *spec["config"])
    backup = _backup(config_path)

    if spec["style"] == "cursor":
        _write_text(config_path, json.dumps(
            build_cursor_hooks(node, global_hook, windows), ensure_ascii=False, indent=2) + "\n")
    else:  # claude：读现有 → 合并 → 写回
        existing = {}
        try:
            with open(config_path, encoding="utf-8") as f:
                existing = json.load(f)
        except (OSError, ValueError):
            existing = {}
        merged = merge_claude_settings(existing, build_claude_hooks(node, global_hook, windows))
        _write_text(config_path, json.dumps(merged, ensure_ascii=False, indent=2) + "\n")

    return {"ok": True, "tool": tool_id, "config": config_path, "node": node,
            "hook": global_hook, "device_ip": device_ip, "backup": backup}
