import json
from clawd_mochi.core import hookinstall as hi


def test_cursor_hooks_windows_shape():
    h = hi.build_cursor_hooks("C:\\node.exe", "C:\\Users\\me\\.clawd-mood\\hook\\clawd-hook.js", windows=True)
    assert h["version"] == 1
    assert set(h["hooks"].keys()) == set(hi.CURSOR_EVENTS)
    entry = h["hooks"]["sessionStart"][0]
    assert entry["timeout"] == 5
    assert entry["command"].startswith("cmd /d /s /c ")
    assert "node.exe" in entry["command"]
    # hook 路径用正斜杠
    assert "clawd-hook.js" in entry["command"]


def test_cursor_hooks_unix_shape():
    h = hi.build_cursor_hooks("/usr/bin/node", "/home/me/.clawd-mood/hook/clawd-hook.js", windows=False)
    entry = h["hooks"]["stop"][0]
    assert entry["command"] == '"/usr/bin/node" "/home/me/.clawd-mood/hook/clawd-hook.js"'


def test_claude_hooks_windows_shape():
    h = hi.build_claude_hooks("C:\\node.exe", "C:\\hook\\clawd-hook.js", windows=True)
    assert set(h.keys()) == set(hi.CLAUDE_EVENTS)
    block = h["Stop"][0]
    assert block["matcher"] == ""
    hook = block["hooks"][0]
    assert hook["type"] == "command"
    assert hook["shell"] == "powershell"
    assert hook["async"] is True
    assert hook["timeout"] == 5
    assert hook["command"].endswith(" Stop")        # 事件名作为 argv
    assert hook["command"].startswith("& ")


def test_claude_hooks_unix_no_shell_key():
    h = hi.build_claude_hooks("/usr/bin/node", "/home/me/hook/clawd-hook.js", windows=False)
    hook = h["UserPromptSubmit"][0]["hooks"][0]
    assert "shell" not in hook
    assert hook["command"] == '"/usr/bin/node" "/home/me/hook/clawd-hook.js" UserPromptSubmit'


def test_merge_claude_preserves_other_keys():
    existing = {"model": "opus", "hooks": {"Foo": [{"x": 1}]}}
    mochi = hi.build_claude_hooks("/n", "/h", windows=False)
    merged = hi.merge_claude_settings(existing, mochi)
    assert merged["model"] == "opus"          # 非 hooks 键保留
    assert merged["hooks"]["Foo"] == [{"x": 1}]  # 非 Mochi 事件保留
    assert "Stop" in merged["hooks"]          # Mochi 事件加入


def test_merge_claude_overwrites_same_event():
    existing = {"hooks": {"Stop": [{"old": True}]}}
    mochi = hi.build_claude_hooks("/n", "/h", windows=False)
    merged = hi.merge_claude_settings(existing, mochi)
    assert merged["hooks"]["Stop"] != [{"old": True}]  # 被 Mochi 覆盖


def test_merge_claude_empty_existing():
    mochi = hi.build_claude_hooks("/n", "/h", windows=False)
    merged = hi.merge_claude_settings({}, mochi)
    assert set(merged["hooks"].keys()) == set(hi.CLAUDE_EVENTS)
