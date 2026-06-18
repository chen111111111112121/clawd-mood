# -*- mode: python ; coding: utf-8 -*-
# PyInstaller 打包配置:把上位机打成「拿到即烧」单包。
#   - 自带 Python + PySide6 + esptool + pyserial(目标机无需装任何环境)
#   - 随包固件镜像 clawd_mochi/firmware/*(bins + manifest.json)
#   - esptool 的 stub flasher 数据文件由 collect_all 收齐(否则刷写时报缺 stub)
#
# 构建(在 desktop/ 目录):
#   python -m pip install pyinstaller
#   pyinstaller clawd-mochi.spec
# 产物:dist/ClawdMochi/ClawdMochi.exe(onedir;改 onefile 见文末注释)
#
# 提示:发版前先跑 `python ../tools/bundle_firmware.py` 刷新 clawd_mochi/firmware。

from PyInstaller.utils.hooks import collect_all

datas = [("clawd_mochi/firmware", "clawd_mochi/firmware")]
binaries = []
hiddenimports = []

# esptool 需要其 stub flasher json + 子模块;pyserial 需要各平台串口后端;zeroconf 有动态导入。
for pkg in ("esptool", "serial", "zeroconf"):
    d, b, h = collect_all(pkg)
    datas += d
    binaries += b
    hiddenimports += h

a = Analysis(
    ["clawd_mochi/__main__.py"],
    pathex=[],
    binaries=binaries,
    datas=datas,
    hiddenimports=hiddenimports,
    hookspath=[],
    runtime_hooks=[],
    excludes=["tkinter", "pytest"],
    noarchive=False,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name="ClawdMochi",
    debug=False,
    strip=False,
    upx=False,
    console=False,            # GUI 应用,不弹控制台(刷写日志在界面里看)
    disable_windowed_traceback=False,
    icon=None,                # TODO: 正式图标(.ico)留待打磨期
)
coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    strip=False,
    upx=False,
    name="ClawdMochi",
)

# 如需单文件分发,删掉上面的 COLLECT,改用:
#   exe = EXE(pyz, a.scripts, a.binaries, a.datas, [], name="ClawdMochi",
#             console=False, onefile=True 等价写法见 PyInstaller 文档)
# onefile 启动稍慢(每次解包到临时目录),但只有一个 exe 便于分发。
