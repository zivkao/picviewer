"""Logging setup.

The app normally runs under pythonw.exe, which has no console: anything written
to stderr disappears. So the file handler is not a nicety, it is the only record
that exists -- without it a decode that silently returns the wrong image leaves
no trace at all.
"""

from __future__ import annotations

import logging
import os
import sys
from logging.handlers import RotatingFileHandler
from pathlib import Path

MAX_BYTES = 1024 * 1024
BACKUP_COUNT = 3
FORMAT = "%(asctime)s %(levelname)-7s %(name)-22s %(message)s"
DATE_FORMAT = "%Y-%m-%d %H:%M:%S"


def log_dir() -> Path:
    """Per-user log location, outside the project so it survives a reinstall."""
    base = os.environ.get("LOCALAPPDATA") or os.path.expanduser("~/.local/share")
    return Path(base) / "PicViewer" / "logs"


def log_file() -> Path:
    return log_dir() / "picviewer.log"


def setup_logging(verbose: bool = False) -> Path:
    """Install the file handler (and a console one when there is a console)."""
    directory = log_dir()
    directory.mkdir(parents=True, exist_ok=True)
    path = log_file()

    root = logging.getLogger()
    root.setLevel(logging.DEBUG if verbose else logging.INFO)
    for handler in list(root.handlers):
        root.removeHandler(handler)

    formatter = logging.Formatter(FORMAT, DATE_FORMAT)

    file_handler = RotatingFileHandler(
        path, maxBytes=MAX_BYTES, backupCount=BACKUP_COUNT, encoding="utf-8"
    )
    file_handler.setFormatter(formatter)
    file_handler.setLevel(logging.DEBUG if verbose else logging.INFO)
    root.addHandler(file_handler)

    # pythonw.exe leaves sys.stderr as None, and writing to it would raise.
    if sys.stderr is not None:
        console = logging.StreamHandler(sys.stderr)
        console.setFormatter(formatter)
        console.setLevel(logging.DEBUG if verbose else logging.WARNING)
        root.addHandler(console)

    _install_exception_hook()
    return path


def _install_exception_hook() -> None:
    """Record crashes too -- otherwise a traceback under pythonw is invisible."""
    previous = sys.excepthook

    def hook(exc_type, exc_value, traceback):
        logging.getLogger("picviewer").critical(
            "unhandled exception", exc_info=(exc_type, exc_value, traceback)
        )
        previous(exc_type, exc_value, traceback)

    sys.excepthook = hook
