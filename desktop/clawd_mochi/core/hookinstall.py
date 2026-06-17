"""Hook 一键安装。移植 install-global.ps1，跨 OS。

Cursor ~/.cursor/hooks.json   : camelCase 事件，仅 stdin，命令不带事件名。
Claude ~/.claude/settings.json: PascalCase 事件，argv 传事件名，合并保留其它键。
Windows 命令用 cmd / powershell 包装；Unix 直接调用。
"""

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
