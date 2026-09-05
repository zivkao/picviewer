"""Menus, toolbar, action enable-state, fullscreen chrome, and the log file."""

import os
import sys
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
ROOT = Path(__file__).resolve().parents[1]
WORK = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / ".testwork"
WORK.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(ROOT))

from PySide6.QtCore import QEventLoop, Qt, QTimer
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication, QToolBar

from picviewer.applog import log_file, setup_logging

log_path = setup_logging(verbose=False)

from picviewer.window import MainWindow

SAMPLES = ROOT / "samples"

app = QApplication([])
win = MainWindow()
win.resize(1000, 700)
win.show()


def pump(ms=120):
    loop = QEventLoop()
    QTimer.singleShot(ms, loop.quit)
    loop.exec()


print("=== menu bar ===")
for menu_action in win.menuBar().actions():
    menu = menu_action.menu()
    items = [a.text() for a in menu.actions() if a.text()]
    print("  {:<8} {}".format(menu_action.text(), items))

print("\n=== toolbar ===")
tb = win.findChild(QToolBar, "main_toolbar")
labels = []
for a in tb.actions():
    if a.isSeparator():
        labels.append("|")
    elif a.text():
        labels.append(a.text().replace("&", ""))
    else:
        labels.append("<widget>")
print("  ", " ".join(labels))
print("   visible:", tb.isVisible())

print("\n=== action state with nothing open ===")
for name in ("act_prev", "act_next", "act_delete", "act_copy", "act_fit", "act_reveal"):
    print("   {:<14} enabled={}".format(name, getattr(win, name).isEnabled()))

print("\n=== action state with a folder open ===")
win.open_path(SAMPLES)
for _ in range(50):
    pump(40)
    if win.view.has_image() and not win._showing_preview:
        break
for name in ("act_prev", "act_next", "act_delete", "act_copy", "act_fit", "act_reveal"):
    print("   {:<14} enabled={}".format(name, getattr(win, name).isEnabled()))
print("   status bar:", win._status_right.text())

print("\n=== toolbar buttons actually navigate ===")
win.show_index(3)
pump(150)
before = win._index
win.act_next.trigger()
pump(150)
mid = win._index
win.act_prev.trigger()
pump(150)
after = win._index
print("   index {} -> next {} -> prev {}".format(before, mid, after))

print("\n=== fullscreen hides chrome ===")
win.toggle_fullscreen()
pump(150)
print("   fullscreen: menubar={} toolbar={} statusbar={}".format(
    win.menuBar().isVisible(), tb.isVisible(), win.statusBar().isVisible()))
win.toggle_fullscreen()
pump(150)
print("   restored  : menubar={} toolbar={} statusbar={}".format(
    win.menuBar().isVisible(), tb.isVisible(), win.statusBar().isVisible()))

print("\n=== Esc leaves fullscreen instead of quitting ===")
win.toggle_fullscreen()
pump(150)
QTest.keyClick(win, Qt.Key.Key_Escape)
pump(150)
print("   still fullscreen after Esc:", win.isFullScreen(), "(should be False)")
print("   window still open:", win.isVisible(), "(should be True)")

print("\n=== clipboard copy ===")
win.show_index(0)
pump(300)
win.act_copy.trigger()
pump(80)
from PySide6.QtGui import QGuiApplication
cb = QGuiApplication.clipboard().pixmap()
print("   clipboard holds {}x{}".format(cb.width(), cb.height()))

win.loader.shutdown()
app.processEvents()

print("\n=== log file ===")
print("   path:", log_path)
print("   exists:", log_path.exists(), " size:", log_path.stat().st_size if log_path.exists() else 0, "bytes")
if log_path.exists():
    lines = log_path.read_text(encoding="utf-8").strip().splitlines()
    print("   {} lines, last 8:".format(len(lines)))
    for line in lines[-8:]:
        print("     ", line)
