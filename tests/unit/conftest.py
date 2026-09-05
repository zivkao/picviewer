"""Make the package importable and keep Qt off-screen for the unit tests."""

import os
import sys
from pathlib import Path

import pytest

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))


@pytest.fixture(scope="session")
def qapp():
    """A QGuiApplication, for the few tests that need one.

    QImage works without it, which is why most of these tests need no fixture
    at all -- but QPixmap and QIcon do not, and creating one without an
    application instance takes the process down rather than raising.
    """
    from PySide6.QtGui import QGuiApplication

    existing = QGuiApplication.instance()
    if existing is not None:
        yield existing
        return

    app = QGuiApplication([])
    yield app
