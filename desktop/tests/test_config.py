import json
import os
from clawd_mochi.core import config


def test_read_config_missing_returns_defaults(tmp_path):
    cfg = config.read_config(str(tmp_path))
    assert cfg["activeTool"] is None
    assert cfg["presence"] == "auto"
    assert [t["id"] for t in cfg["tools"]] == ["cc", "cursor"]


def test_read_config_parses_existing(tmp_path):
    (tmp_path / "agent.json").write_text(
        json.dumps({"activeTool": "cc", "presence": "meeting",
                    "tools": [{"id": "cc", "name": "Claude Code", "installed": True}]}),
        encoding="utf-8")
    cfg = config.read_config(str(tmp_path))
    assert cfg["activeTool"] == "cc"
    assert cfg["presence"] == "meeting"
    assert cfg["tools"][0]["id"] == "cc"


def test_read_config_invalid_presence_falls_back(tmp_path):
    (tmp_path / "agent.json").write_text(json.dumps({"presence": ""}), encoding="utf-8")
    assert config.read_config(str(tmp_path))["presence"] == "auto"


def test_write_active_tool_roundtrip(tmp_path):
    config.write_active_tool("cursor", str(tmp_path))
    assert config.read_config(str(tmp_path))["activeTool"] == "cursor"
    config.write_active_tool(None, str(tmp_path))
    assert config.read_config(str(tmp_path))["activeTool"] is None


def test_write_active_tool_preserves_presence(tmp_path):
    config.write_presence("rest", str(tmp_path))
    config.write_active_tool("cc", str(tmp_path))
    cfg = config.read_config(str(tmp_path))
    assert cfg["presence"] == "rest"
    assert cfg["activeTool"] == "cc"


def test_write_presence_validates(tmp_path):
    config.write_presence("bogus", str(tmp_path))
    assert config.read_config(str(tmp_path))["presence"] == "auto"
    config.write_presence("solder", str(tmp_path))
    assert config.read_config(str(tmp_path))["presence"] == "solder"


def test_read_state_missing(tmp_path):
    assert config.read_state(str(tmp_path)) == {"lastSeen": {}}


def test_read_state_parses(tmp_path):
    (tmp_path / "agent-state.json").write_text(
        json.dumps({"lastSeen": {"cc": 123}}), encoding="utf-8")
    assert config.read_state(str(tmp_path))["lastSeen"] == {"cc": 123}
