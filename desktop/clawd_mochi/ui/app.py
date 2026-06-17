import sys

from PySide6.QtGui import QIcon, QPixmap, QAction
from PySide6.QtWidgets import QApplication, QSystemTrayIcon, QMenu

from clawd_mochi.ui.main_window import MainWindow


def _placeholder_icon() -> QIcon:
    pm = QPixmap(32, 32)
    pm.fill()  # 纯色占位图标；正式图标在打包计划中替换
    return QIcon(pm)


def main():
    app = QApplication(sys.argv)
    app.setApplicationName("Clawd Mochi")
    app.setQuitOnLastWindowClosed(False)  # 关窗后驻留托盘

    win = MainWindow()
    win.show()

    tray = QSystemTrayIcon(_placeholder_icon(), app)
    tray.setToolTip("Clawd Mochi 控制台")
    menu = QMenu()
    act_show = QAction("打开控制台", app)
    act_show.triggered.connect(win.show)
    act_show.triggered.connect(win.raise_)
    act_quit = QAction("退出", app)
    act_quit.triggered.connect(app.quit)
    menu.addAction(act_show)
    menu.addSeparator()
    menu.addAction(act_quit)
    tray.setContextMenu(menu)
    tray.activated.connect(lambda reason: (win.show(), win.raise_()) if reason == QSystemTrayIcon.Trigger else None)
    tray.show()

    sys.exit(app.exec())


if __name__ == "__main__":
    main()
