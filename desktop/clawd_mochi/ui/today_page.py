from PySide6.QtWidgets import QWidget, QVBoxLayout, QLabel


class TodayPage(QWidget):
    def __init__(self):
        super().__init__()
        layout = QVBoxLayout(self)
        self._label = QLabel("今日陪伴（占位）")
        self._label.setWordWrap(True)
        layout.addWidget(self._label)
        layout.addStretch(1)

    def refresh(self):
        pass
