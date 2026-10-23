"""Logging setup.

The app normally runs under pythonw.exe, which has no console: anything written
to stderr disappears. So the file handler is not a nicety, it is the only record
that exists -- without it a decode that silently returns the wrong image leaves
no trace at all.
"""

from __future__ import annotations

import importlib
import logging
import os
import platform
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

    _quieten_third_parties(verbose)
    _install_exception_hook()
    return path


# Libraries that are chatty at DEBUG. Pillow alone logs a line per plugin it
# imports, which buries the application's own output in a verbose run -- and a
# debug log nobody can read is worth no more than no log at all.
NOISY = ("PIL", "PIL.Image", "PIL.PngImagePlugin", "PIL.TiffImagePlugin",
         "matplotlib", "urllib3", "asyncio")


def _quieten_third_parties(verbose: bool) -> None:
    for name in NOISY:
        logging.getLogger(name).setLevel(logging.WARNING)
    if verbose:
        # Only this application's own loggers follow -v down to DEBUG.
        logging.getLogger("picviewer").setLevel(logging.DEBUG)


def log_environment() -> None:
    """Record what this is running on and against.

    Decode bugs are routinely specific to a library version, and a report
    without these cannot be reproduced.
    """
    log = logging.getLogger("picviewer.env")
    log.info("os      : %s %s", platform.platform(), platform.machine())
    log.info("python  : %s", sys.version.split()[0])

    for name in ("PySide6", "PIL", "pillow_heif", "rawpy", "numpy"):
        try:
            module = importlib.import_module(name)
            version = getattr(module, "__version__", None) or getattr(
                module, "version", "?")
        except Exception as exc:
            version = f"unavailable ({exc})"
        log.info("%-8s: %s", name.lower(), version)


def log_display(app) -> None:
    """Screen geometry and scaling, once the application exists.

    Nothing in the viewer accounts for devicePixelRatio, so a scaled display
    is the first thing to suspect when "100%" does not look like 1:1.
    """
    log = logging.getLogger("picviewer.env")
    for screen in app.screens():
        size = screen.geometry()
        log.info(
            "screen  : %s %dx%d @ %.0f dpi, ratio %.2f",
            screen.name(), size.width(), size.height(),
            screen.logicalDotsPerInch(), screen.devicePixelRatio(),
        )


def _install_exception_hook() -> None:
    """Record crashes too -- otherwise a traceback under pythonw is invisible."""
    previous = sys.excepthook

    def hook(exc_type, exc_value, traceback):
        logging.getLogger("picviewer").critical(
            "unhandled exception", exc_info=(exc_type, exc_value, traceback)
        )
        previous(exc_type, exc_value, traceback)

    sys.excepthook = hook
