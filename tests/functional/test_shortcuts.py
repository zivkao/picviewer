"""Do the keyboard shortcuts actually fire? Send real key events and watch state."""

import os
import sys
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
ROOT = Path(__file__).resolve().parents[2]
WORK = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / ".testwork"
WORK.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(ROOT))

from PySide6.QtCore import QEventLoop, Qt, QTimer
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication

from picviewer.window import MainWindow

SAMPLES = ROOT / "samples"

app = QApplication([])
win = MainWindow()
win.resize(1000, 700)
win.show()
win.open_path(SAMPLES)


def pump(ms=120):
    loop = QEventLoop()
    QTimer.singleShot(ms, loop.quit)
    loop.exec()


pump(400)
print("playlist: {} files".format(len(win._playlist)))
print("view has focus:", win.view.hasFocus(), " focus widget:",
      type(app.focusWidget()).__name__ if app.focusWidget() else None)
print()

NAV = [
    ("Right", Qt.Key.Key_Right), ("Down", Qt.Key.Key_Down),
    ("Space", Qt.Key.Key_Space), ("PgDown", Qt.Key.Key_PageDown),
    ("Left", Qt.Key.Key_Left), ("Up", Qt.Key.Key_Up),
    ("Backspace", Qt.Key.Key_Backspace), ("PgUp", Qt.Key.Key_PageUp),
    ("Home", Qt.Key.Key_Home), ("End", Qt.Key.Key_End),
]

print("{:<12} {:<10} {:<10} {}".format("KEY", "INDEX", "AFTER", "RESULT"))
print("-" * 52)
broken = []
for label, key in NAV:
    win.show_index(5)
    pump(60)
    before = win._index
    QTest.keyClick(win, key)
    pump(60)
    after = win._index
    moved = after != before
    expected_move = label not in ()
    status = "moved" if moved else "NO EFFECT"
    if not moved:
        broken.append(label)
    print("{:<12} {:<10} {:<10} {}".format(label, before, after, status))

print()
ZOOM = [("+", Qt.Key.Key_Plus), ("-", Qt.Key.Key_Minus),
        ("0 (fit)", Qt.Key.Key_0), ("1 (actual)", Qt.Key.Key_1)]
win.show_index(0)
pump(200)
print("{:<12} {:<12} {}".format("KEY", "SCALE", "RESULT"))
print("-" * 42)
for label, key in ZOOM:
    win.view.zoom_to_actual()
    before = win.view.scale_factor
    QTest.keyClick(win, key)
    pump(60)
    after = win.view.scale_factor
    changed = abs(after - before) > 1e-6
    print("{:<12} {:<12} {}".format(
        label, "{:.0f}% -> {:.0f}%".format(before * 100, after * 100),
        "changed" if changed else "no change"))

print()
print("=== discoverable UI ===")
mb = win.menuBar()
print("  menu bar actions :", [a.text() for a in mb.actions()] or "NONE")
print("  toolbars         :", [t.windowTitle() for t in win.findChildren(type(win).__mro__[0])] if False else
      [t.windowTitle() for t in win.findChildren(__import__("PySide6.QtWidgets", fromlist=["QToolBar"]).QToolBar)] or "NONE")
print("  window actions   :", len(win.actions()), "(keyboard only, not visible anywhere)")

print()
print("=== logging ===")
import logging
root = logging.getLogger()
print("  root handlers    :", root.handlers or "NONE")
print("  log files on disk:", list(ROOT.glob("*.log")) or "NONE")

print()
print("shortcuts with no effect:", broken if broken else "none")
win.loader.shutdown()
