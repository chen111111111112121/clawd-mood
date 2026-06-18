"""进程内跑 esptool 刷写固件(不依赖外部 python 命令,打包后 exe 自带)。

设计:`esptool.main(argv)` 在调用方的工作线程里跑;stdout/stderr 重定向到逐行回调供 UI
解析进度。esptool 入口可注入(esptool_main),便于单测;缺失时抛 EsptoolMissing。
本模块不依赖 Qt。
"""
import sys


class EsptoolMissing(Exception):
    """打包/环境异常:找不到 esptool(正常 exe 不应触发)。"""


def _load_esptool_main(importer=__import__):
    try:
        mod = importer("esptool")
    except ImportError as e:
        raise EsptoolMissing(str(e))
    return mod.main


class _LineWriter:
    """把写入按行切开喂给回调;同时不吞掉数据(供进度解析)。"""
    def __init__(self, on_line):
        self._on_line = on_line
        self._buf = ""

    def write(self, s):
        if not self._on_line:
            return len(s) if s else 0
        self._buf += s
        while True:
            i = self._buf.find("\n")
            if i < 0:
                # esptool 进度用 \r 刷新同一行,也当作一行切出
                j = self._buf.find("\r")
                if j < 0:
                    break
                line, self._buf = self._buf[:j], self._buf[j + 1:]
            else:
                line, self._buf = self._buf[:i], self._buf[i + 1:]
            if line:
                self._on_line(line)
        return len(s) if s else 0

    def flush(self):
        if self._on_line and self._buf:
            self._on_line(self._buf)
            self._buf = ""


def run_flash(argv, on_output=None, on_done=None, esptool_main=None) -> bool:
    """跑一次刷写。返回是否成功;若给了 on_done 则回调 (ok, err)。"""
    main = esptool_main or _load_esptool_main()
    saved_out, saved_err = sys.stdout, sys.stderr
    writer = _LineWriter(on_output)
    ok, err = False, None
    sys.stdout = sys.stderr = writer
    try:
        main(list(argv))
        ok, err = True, None
    except SystemExit as e:
        ok = e.code in (0, None)
        err = None if ok else f"esptool 退出码 {e.code}"
    except Exception as e:  # noqa: BLE001 — 任何刷写异常都转成失败回报,不崩 UI
        ok, err = False, str(e)
    finally:
        writer.flush()
        sys.stdout, sys.stderr = saved_out, saved_err
    if on_done:
        on_done(ok, err)
    return ok
