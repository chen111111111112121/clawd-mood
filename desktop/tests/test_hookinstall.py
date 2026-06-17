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


import os


def _make_hook_src(tmp_path):
    src = tmp_path / "src" / "clawd-hook.js"
    src.parent.mkdir(parents=True)
    src.write_text("// fake hook\n", encoding="utf-8")
    return str(src)


def test_find_node_prefers_which(monkeypatch):
    monkeypatch.setattr(hi.shutil, "which", lambda name: "/usr/bin/node")
    assert hi.find_node() == "/usr/bin/node"


def test_find_node_raises_when_absent(monkeypatch):
    monkeypatch.setattr(hi.shutil, "which", lambda name: None)
    monkeypatch.setattr(hi, "_WINDOWS_NODE_CANDIDATES", [])
    try:
        hi.find_node()
        assert False, "应抛异常"
    except hi.NodeNotFound:
        pass


def test_locate_hook_source_env(tmp_path, monkeypatch):
    src = _make_hook_src(tmp_path)
    monkeypatch.setenv("CLAWD_HOOK_SRC", src)
    assert hi.locate_hook_source() == src


def test_list_tools():
    assert set(hi.list_tools()) == {"cc", "cursor"}


def test_tool_present_when_config_dir_exists(tmp_path, monkeypatch):
    monkeypatch.setattr(hi.shutil, "which", lambda name: None)  # 排除 CLI 干扰
    home = tmp_path / "home"
    (home / ".cursor").mkdir(parents=True)
    assert hi.tool_present("cursor", home=str(home)) is True
    assert hi.tool_present("cc", home=str(home)) is False


def test_tool_present_via_cli(tmp_path, monkeypatch):
    monkeypatch.setattr(hi.shutil, "which", lambda name: "/usr/bin/claude" if name == "claude" else None)
    home = tmp_path / "home"
    home.mkdir()
    assert hi.tool_present("cc", home=str(home)) is True


def test_install_hook_for_refuses_when_absent(tmp_path, monkeypatch):
    monkeypatch.setattr(hi.shutil, "which", lambda name: None)
    src = _make_hook_src(tmp_path)
    home = tmp_path / "home"
    cfg = tmp_path / "cfg"
    r = hi.install_hook_for("cursor", "1.2.3.4", node="/usr/bin/node", hook_source=src,
                            home=str(home), config_directory=str(cfg), windows=False)
    assert r["ok"] is False
    assert r["reason"] == "not_present"
    assert not (home / ".cursor" / "hooks.json").exists()  # 未写配置


def test_install_hook_for_cursor_writes_global_and_config(tmp_path, monkeypatch):
    monkeypatch.setattr(hi.shutil, "which", lambda name: None)
    src = _make_hook_src(tmp_path)
    home = tmp_path / "home"
    cfg = tmp_path / "cfg"
    (home / ".cursor").mkdir(parents=True)  # 使 cursor "本机存在"
    r = hi.install_hook_for("cursor", "192.168.1.9", node="/usr/bin/node", hook_source=src,
                            home=str(home), config_directory=str(cfg), windows=False)
    assert r["ok"] is True and r["tool"] == "cursor"
    gh = cfg / "hook" / "clawd-hook.js"
    dev = cfg / "hook" / "device.json"
    assert gh.read_text(encoding="utf-8") == "// fake hook\n"
    assert json.loads(dev.read_text(encoding="utf-8"))["device_ip"] == "192.168.1.9"
    ch = json.loads((home / ".cursor" / "hooks.json").read_text(encoding="utf-8"))
    assert ch["version"] == 1
    assert "clawd-hook.js" in ch["hooks"]["stop"][0]["command"]


def test_install_hook_for_claude_merges_preserving(tmp_path, monkeypatch):
    monkeypatch.setattr(hi.shutil, "which", lambda name: None)
    src = _make_hook_src(tmp_path)
    home = tmp_path / "home"
    cfg = tmp_path / "cfg"
    claude = home / ".claude"
    claude.mkdir(parents=True)
    (claude / "settings.json").write_text(json.dumps({"model": "opus"}), encoding="utf-8")
    hi.install_hook_for("cc", "1.2.3.4", node="/usr/bin/node", hook_source=src,
                        home=str(home), config_directory=str(cfg), windows=False)
    s = json.loads((claude / "settings.json").read_text(encoding="utf-8"))
    assert s["model"] == "opus"            # 原键保留
    assert "Stop" in s["hooks"]            # Mochi hooks 合并


def test_install_hook_for_backs_up_existing(tmp_path, monkeypatch):
    monkeypatch.setattr(hi.shutil, "which", lambda name: None)
    src = _make_hook_src(tmp_path)
    home = tmp_path / "home"
    cfg = tmp_path / "cfg"
    cursor = home / ".cursor"
    cursor.mkdir(parents=True)
    (cursor / "hooks.json").write_text("OLD", encoding="utf-8")
    hi.install_hook_for("cursor", "1.2.3.4", node="/usr/bin/node", hook_source=src,
                        home=str(home), config_directory=str(cfg), windows=False)
    baks = [p for p in os.listdir(cursor) if p.startswith("hooks.json.bak.")]
    assert baks, "应生成备份"
    assert (cursor / baks[0]).read_text(encoding="utf-8") == "OLD"


def test_install_hook_for_force_bypasses_presence(tmp_path, monkeypatch):
    monkeypatch.setattr(hi.shutil, "which", lambda name: None)
    src = _make_hook_src(tmp_path)
    home = tmp_path / "home"
    cfg = tmp_path / "cfg"
    r = hi.install_hook_for("cursor", "1.2.3.4", node="/usr/bin/node", hook_source=src,
                            home=str(home), config_directory=str(cfg), windows=False, force=True)
    assert r["ok"] is True
    assert (home / ".cursor" / "hooks.json").exists()


def test_hook_installed(tmp_path):
    home = tmp_path / "home"
    (home / ".cursor").mkdir(parents=True)
    (home / ".cursor" / "hooks.json").write_text(
        json.dumps({"hooks": {"stop": [{"command": "node /x/clawd-hook.js"}]}}), encoding="utf-8")
    (home / ".claude").mkdir(parents=True)
    (home / ".claude" / "settings.json").write_text(json.dumps({"model": "opus"}), encoding="utf-8")
    assert hi.hook_installed("cursor", home=str(home)) is True
    assert hi.hook_installed("cc", home=str(home)) is False
