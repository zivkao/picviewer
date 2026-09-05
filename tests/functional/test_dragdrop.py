"""Drag-and-drop behaviour: routing, multi-file, folders, rejection, hover paint."""

import os
import sys
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
ROOT = Path(__file__).resolve().parents[2]
WORK = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / ".testwork"
WORK.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(ROOT))

from PySide6.QtCore import QEventLoop, QMimeData, QPoint, QPointF, Qt, QTimer, QUrl
from PySide6.QtGui import QDragEnterEvent, QDragLeaveEvent, QDropEvent
from PySide6.QtWidgets import QApplication

from picviewer.window import MainWindow

SAMPLES = ROOT / "samples"

app = QApplication([])
win = MainWindow()
win.resize(1000, 700)
win.show()
viewport = win.view.viewport()

_alive = []


def urls_mime(paths):
    m = QMimeData()
    m.setUrls([QUrl.fromLocalFile(str(p)) for p in paths])
    _alive.append(m)
    return m


def text_mime(text):
    m = QMimeData()
    m.setText(text)
    _alive.append(m)
    return m


def drag_over(mime):
    ev = QDragEnterEvent(
        QPoint(500, 350), Qt.DropAction.CopyAction, mime,
        Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier,
    )
    _alive.append(ev)
    QApplication.sendEvent(viewport, ev)
    return ev


def drop(mime):
    ev = QDropEvent(
        QPointF(500, 350), Qt.DropAction.CopyAction, mime,
        Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier,
    )
    _alive.append(ev)
    QApplication.sendEvent(viewport, ev)
    return ev


def reset():
    win._playlist = []
    win._index = -1


def pump(ms=200):
    loop = QEventLoop()
    QTimer.singleShot(ms, loop.quit)
    loop.exec()


print("=== single file ===")
reset()
m = urls_mime([SAMPLES / "sample.png"])
drag_over(m)
print("  hover highlight active:", win.view._drag_hover)
drop(m)
pump()
print("  playlist {} entries, showing '{}'".format(
    len(win._playlist), win._playlist[win._index].name if win._playlist else "-"))
print("  hover cleared after drop:", not win.view._drag_hover)
print("  (folder became the playlist, as with Ctrl+O)")

print("\n=== three files ===")
reset()
picks = [SAMPLES / n for n in ("sample.jpg", "sample.webp", "synthetic.dng")]
m = urls_mime(picks)
drag_over(m)
drop(m)
pump()
print("  playlist:", [p.name for p in win._playlist])
print("  only the dropped files:", len(win._playlist) == 3)

print("\n=== folder ===")
reset()
m = urls_mime([SAMPLES])
drag_over(m)
drop(m)
pump()
print("  playlist {} entries from the folder".format(len(win._playlist)))

print("\n=== mixed: a folder plus a file ===")
reset()
m = urls_mime([SAMPLES, SAMPLES / "sample.png"])
drag_over(m)
drop(m)
pump()
print("  playlist {} entries, deduplicated: {}".format(
    len(win._playlist), len(win._playlist) == len(set(win._playlist))))

print("\n=== plain text (not a file) ===")
reset()
m = text_mime("just some text")
ev = drag_over(m)
print("  dragEnter accepted:", ev.isAccepted(), "(should be False)")
print("  hover highlight active:", win.view._drag_hover, "(should be False)")
ev2 = drop(m)
print("  drop accepted:", ev2.isAccepted(), "(should be False)")
print("  playlist untouched:", len(win._playlist) == 0)

print("\n=== drag leaves without dropping ===")
reset()
m = urls_mime([SAMPLES / "sample.png"])
drag_over(m)
before = win.view._drag_hover
lv = QDragLeaveEvent()
_alive.append(lv)
QApplication.sendEvent(viewport, lv)
print("  hover during drag: {} -> after leave: {}".format(before, win.view._drag_hover))

print("\n=== rendering still works with the paintEvent override ===")
reset()
win.open_path(SAMPLES / "sample.png")
for _ in range(60):
    pump(30)
    if win.view.has_image() and not win._showing_preview:
        break
pm = win.view._item.pixmap()
print("  pixmap on canvas: {}x{}".format(pm.width(), pm.height()))
win.view._drag_hover = True
win.view.repaint()
win.view._drag_hover = False
win.view.repaint()
print("  repaint with and without the drop overlay: no crash")

win.loader.shutdown()
