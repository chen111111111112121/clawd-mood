import sys

import pytest

from clawd_mochi.core import flash_runner


def test_run_flash_passes_argv_and_captures_stdout():
    seen = {}
    lines = []

    def fake_main(argv):
        seen["argv"] = argv
        print("Connecting....")
        print("Writing at 0x0... (100 %)")

    ok = flash_runner.run_flash(
        ["--chip", "esp32c3", "write_flash"],
        on_output=lines.append,
        esptool_main=fake_main,
    )
    assert ok is True
    assert seen["argv"] == ["--chip", "esp32c3", "write_flash"]
    assert any("100 %" in ln for ln in lines)


def test_run_flash_reports_done_success():
    done = {}

    def fake_main(argv):
        pass

    flash_runner.run_flash([], on_done=lambda ok, err: done.update(ok=ok, err=err),
                           esptool_main=fake_main)
    assert done == {"ok": True, "err": None}


def test_run_flash_handles_exception_as_failure():
    done = {}

    def fake_main(argv):
        raise RuntimeError("could not open port COM7")

    ok = flash_runner.run_flash([], on_done=lambda ok, err: done.update(ok=ok, err=err),
                                esptool_main=fake_main)
    assert ok is False
    assert "COM7" in done["err"]


def test_run_flash_nonzero_systemexit_is_failure():
    def fake_main(argv):
        raise SystemExit(2)

    assert flash_runner.run_flash([], esptool_main=fake_main) is False


def test_run_flash_zero_systemexit_is_success():
    def fake_main(argv):
        raise SystemExit(0)

    assert flash_runner.run_flash([], esptool_main=fake_main) is True


def test_run_flash_restores_stdout():
    saved = sys.stdout

    def fake_main(argv):
        print("noise")

    flash_runner.run_flash([], esptool_main=fake_main)
    assert sys.stdout is saved


def test_load_esptool_main_missing_raises():
    def bad_import(name, *a, **k):
        raise ImportError("no esptool")

    with pytest.raises(flash_runner.EsptoolMissing):
        flash_runner._load_esptool_main(importer=bad_import)
