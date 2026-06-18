import os
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

import pytest
from PySide6.QtWidgets import QApplication


@pytest.fixture(scope="session")
def qapp():
    app = QApplication.instance() or QApplication([])
    yield app


def test_mainwindow_has_titlebar_and_close_hides(qapp, monkeypatch):
    # 避免 __init__ 里的真实网络探测
    from clawd_mochi.core import device
    monkeypatch.setattr(device, "device_test",
                        lambda: {"ok": False, "target": "—"})

    from clawd_mochi.ui.main_window import MainWindow
    from clawd_mochi.ui.titlebar import TitleBar

    win = MainWindow()
    win.show()
    assert isinstance(win._title_bar, TitleBar)
    assert win.minimumWidth() >= 900

    assert not win.isHidden()
    win._title_bar.btn_close.click()
    assert win.isHidden()
