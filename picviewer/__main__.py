"""Entry point: python -m picviewer [path]"""

from __future__ import annotations

import argparse
import logging
import sys
from pathlib import Path

from PySide6.QtWidgets import QApplication

from . import __version__
from .applog import setup_logging
from .window import MainWindow


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="picviewer", description="Image viewer")
    parser.add_argument("path", nargs="?", help="image file or folder to open")
    parser.add_argument("-v", "--verbose", action="store_true", help="debug logging")
    args = parser.parse_args(argv)

    log_path = setup_logging(args.verbose)
    log = logging.getLogger("picviewer")
    log.info("--- Pic Viewer %s starting, logging to %s ---", __version__, log_path)

    app = QApplication(sys.argv[:1])
    app.setApplicationName("Pic Viewer")

    window = MainWindow()
    window.show()

    if args.path:
        window.open_path(Path(args.path).expanduser())

    code = app.exec()
    log.info("--- exiting with code %s ---", code)
    return code


if __name__ == "__main__":
    raise SystemExit(main())
