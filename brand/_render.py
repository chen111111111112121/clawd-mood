"""把 brand/*.svg 栅格化成透明背景 PNG（多尺寸）。
用法: python brand/_render.py
注意: 字标(mood-wordmark)要画字母,需系统字体,**不要**用 QT_QPA_PLATFORM=offscreen
      (offscreen 平台字体库为空,字母会变成空方框)。默认 windows 平台即可。
"""
import os
from PySide6.QtGui import (
    QGuiApplication, QImage, QPainter, QColor, QFont, QFontMetrics,
)
from PySide6.QtSvg import QSvgRenderer
from PySide6.QtCore import Qt, QRectF

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "png")
os.makedirs(OUT, exist_ok=True)

# (svg, 高度列表, 是否方形)。方形按 size×size；非方形按 viewBox 宽高比定宽。
JOBS = [
    ("mood-logo.svg",     [16, 32, 48, 64, 128, 256, 512], True),
    ("mood-mascot.svg",   [256, 512, 1024], True),
]
WORDMARK_SIZES = [64, 128, 256, 512]  # 字标高度（宽度自适应）


def render_wordmark(height):
    """字标 m[face][face]d：QtSvg 不擅长 <text>，故用 QFont 画字母 + QSvg 画脸合成。"""
    H = height
    fs = round(H * 0.92)
    font = QFont("Segoe UI")
    font.setPixelSize(fs)
    font.setWeight(QFont.ExtraBold)
    fm = QFontMetrics(font)
    m_w, d_w = fm.horizontalAdvance("m"), fm.horizontalAdvance("d")
    face = round(H * 0.58)
    gap = round(H * 0.03)
    pad = round(H * 0.06)
    baseline = round(H * 0.80)
    face_top = baseline - face

    x_m = pad
    x_f1 = x_m + m_w + gap
    x_f2 = x_f1 + face + gap
    x_d = x_f2 + face + gap
    total_w = x_d + d_w + pad

    img = QImage(total_w, H, QImage.Format_ARGB32)
    img.fill(QColor(0, 0, 0, 0))
    p = QPainter(img)
    p.setRenderHint(QPainter.Antialiasing, True)
    p.setRenderHint(QPainter.TextAntialiasing, True)
    p.setFont(font)
    p.setPen(QColor("#43342A"))
    p.drawText(x_m, baseline, "m")
    p.drawText(x_d, baseline, "d")
    r = QSvgRenderer(os.path.join(HERE, "mood-logo.svg"))
    r.render(p, QRectF(x_f1, face_top, face, face))
    r.render(p, QRectF(x_f2, face_top, face, face))
    p.end()
    path = os.path.join(OUT, f"mood-wordmark-{total_w}x{H}.png")
    img.save(path, "PNG")
    return path


def render(svg_name, height, square):
    renderer = QSvgRenderer(os.path.join(HERE, svg_name))
    assert renderer.isValid(), f"invalid svg: {svg_name}"
    if square:
        w = h = height
    else:
        vb = renderer.viewBoxF()
        h = height
        w = round(height * vb.width() / vb.height())
    img = QImage(w, h, QImage.Format_ARGB32)
    img.fill(QColor(0, 0, 0, 0))  # 透明背景
    p = QPainter(img)
    p.setRenderHint(QPainter.Antialiasing, True)
    renderer.render(p)
    p.end()
    stem = os.path.splitext(svg_name)[0]
    tag = f"{height}" if square else f"{w}x{h}"
    path = os.path.join(OUT, f"{stem}-{tag}.png")
    img.save(path, "PNG")
    return path


def main():
    QGuiApplication([])
    n = 0
    for svg_name, sizes, square in JOBS:
        for s in sizes:
            print("wrote", render(svg_name, s, square))
            n += 1
    for s in WORDMARK_SIZES:
        print("wrote", render_wordmark(s))
        n += 1
    print(f"done: {n} PNGs -> {OUT}")


if __name__ == "__main__":
    main()
