"""Synthesise real drag-and-drop events and find out who actually receives them."""

import os
import sys
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
ROOT = Path(__file__).resolve().parents[2]
WORK = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / ".testwork"
WORK.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(ROOT))

from PySide6.QtCore import QMimeData, QPoint, QPointF, Qt, QUrl
from PySide6.QtGui import QDragEnterEvent, QDropEvent
from PySide6.QtWidgets import QApplication

from picviewer.window import MainWindow

SAMPLES = ROOT / "samples"
target_file = SAMPLES / "sample.png"

app = QApplication([])
win = MainWindow()
win.resize(1000, 700)
win.show()


# Keep strong references: a QMimeData that Python collects arrives at the
# handler as a bare QObject.
_alive = []


def mime():
    m = QMimeData()
    m.setUrls([QUrl.fromLocalFile(str(target_file))])
    _alive.append(m)
    return m


def make_enter():
    return QDragEnterEvent(
        QPoint(500, 350), Qt.DropAction.CopyAction, mime(),
        Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier,
    )


def make_drop():
    return QDropEvent(
        QPointF(500, 350), Qt.DropAction.CopyAction, mime(),
        Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier,
    )


print("acceptDrops flags")
print("  MainWindow          :", win.acceptDrops())
print("  ImageView           :", win.view.acceptDrops())
print("  ImageView.viewport  :", win.view.viewport().acceptDrops())
print()

targets = [
    ("MainWindow", win),
    ("ImageView", win.view),
    ("ImageView.viewport", win.view.viewport()),
]

for label, widget in targets:
    win._playlist = []
    win._index = -1

    enter = make_enter()
    _alive.append(enter)
    QApplication.sendEvent(widget, enter)
    drop = make_drop()
    _alive.append(drop)
    QApplication.sendEvent(widget, drop)

    opened = len(win._playlist) > 0
    print("send to {:<20} enter.accepted={:<6} drop.accepted={:<6} -> opened={}".format(
        label, enter.isAccepted(), drop.isAccepted(), opened))

print()
print("Note: on the real desktop the OS delivers the drop to the widget under")
print("the cursor, which is the viewport -- the last row is the one that counts.")
