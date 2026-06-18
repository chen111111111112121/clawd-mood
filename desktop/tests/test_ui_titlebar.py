import os
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

import pytest
from PySide6.QtWidgets import QApplication, QWidget


@pytest.fixture(scope="session")
def qapp():
    app = QApplication.instance() or QApplication([])
    yield app


def test_buttons_kind_and_actions(qapp):
    from clawd_mochi.ui.titlebar import TitleBar, WindowButton

    win = QWidget()
    win.show()
    tb = TitleBar(win)

    assert isinstance(tb.btn_min, WindowButton) and tb.btn_min.kind == "min"
    assert isinstance(tb.btn_close, WindowButton) and tb.btn_close.kind == "close"

    # 关闭按钮 = 收进托盘(hide)
    assert not win.isHidden()
    tb.btn_close.click()
    assert win.isHidden()

    # 最小化按钮调用 showMinimized
    win.show()
    called = {"min": False}
    win.showMinimized = lambda: called.__setitem__("min", True)
    tb.btn_min.click()
    assert called["min"] is True
