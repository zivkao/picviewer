"""Entry point: python -m picviewer [path]"""

from __future__ import annotations

import argparse
import logging
import sys
from pathlib import Path

from PySide6.QtGui import QIcon
from PySide6.QtWidgets import QApplication

from . import __version__
from .applog import setup_logging
from .window import MainWindow

ICON = Path(__file__).with_name("icon.ico")

# Windows groups taskbar buttons by this id and takes the icon from whatever
# owns it. Left unset, a Python app inherits the interpreter's id, so the
# taskbar shows the Python logo no matter what icon the window carries.
APP_ID = "zivkao.picviewer"


def _claim_taskbar_identity() -> None:
    if sys.platform != "win32":
        return
    try:
        import ctypes

        ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID(APP_ID)
    except Exception:
        logging.getLogger("picviewer").debug("could not set app id", exc_info=True)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="picviewer", description="Image viewer")
    parser.add_argument("path", nargs="?", help="image file or folder to open")
    parser.add_argument("-v", "--verbose", action="store_true", help="debug logging")
    args = parser.parse_args(argv)

    log_path = setup_logging(args.verbose)
    log = logging.getLogger("picviewer")
    log.info("--- Pic Viewer %s starting, logging to %s ---", __version__, log_path)

    _claim_taskbar_identity()

    app = QApplication(sys.argv[:1])
    app.setApplicationName("Pic Viewer")
    if ICON.exists():
        app.setWindowIcon(QIcon(str(ICON)))
    else:
        log.warning("icon missing at %s; run assets/make_icon.py", ICON)

    window = MainWindow()
    window.show()

    if args.path:
        window.open_path(Path(args.path).expanduser())

    code = app.exec()
    log.info("--- exiting with code %s ---", code)
    return code


if __name__ == "__main__":
    raise SystemExit(main())
