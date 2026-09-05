"""Offscreen integration test: build the window, walk the playlist, check state."""

import os
import sys
import time
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
ROOT = Path(__file__).resolve().parents[1]
WORK = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / ".testwork"
WORK.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(ROOT))

from PySide6.QtCore import QEventLoop, QTimer
from PySide6.QtWidgets import QApplication

from picviewer.window import MainWindow

SAMPLES = WORK / "samples"

app = QApplication([])
win = MainWindow()
win.resize(1000, 700)
win.show()


def pump(ms):
    loop = QEventLoop()
    QTimer.singleShot(ms, loop.quit)
    loop.exec()


def wait_for_image(timeout_ms=8000):
    """Spin the event loop until a full (non-preview) decode lands."""
    deadline = time.monotonic() + timeout_ms / 1000
    while time.monotonic() < deadline:
        pump(30)
        if win.view.has_image() and not win._showing_preview:
            return True
    return False


win.open_path(SAMPLES)
print("playlist: {} files".format(len(win._playlist)))
print()

print("{:<20} {:<12} {:<10} {:<9} {}".format("FILE", "SHOWN", "SCALE", "PREVIEW", "STATUS"))
print("-" * 76)

failures = []
for i in range(len(win._playlist)):
    win.show_index(i)
    ok = wait_for_image()
    pm = win.view._item.pixmap()
    name = win._playlist[i].name
    shown = "{}x{}".format(pm.width(), pm.height()) if not pm.isNull() else "-"
    print("{:<20} {:<12} {:<10} {:<9} {}".format(
        name, shown, "{:.0f}%".format(win.view.scale_factor * 100),
        str(win._showing_preview), win._status_right.text()[:26]))
    if not ok:
        failures.append(name)

print()
print("cache: {:.1f} MB across the walk".format(win.loader.cache.nbytes / 1e6))

# Zoom behaviour on the last image.
print("\n=== zoom ===")
win.show_index(0)
wait_for_image()
win.view.fit()
print("  fit        -> {:.0f}%".format(win.view.scale_factor * 100))
win.view.zoom_to_actual()
print("  actual     -> {:.0f}%".format(win.view.scale_factor * 100))
win.view.zoom_by(1.25 ** 4)
print("  +4 steps   -> {:.0f}%  (crisp mode: {})".format(
    win.view.scale_factor * 100,
    win.view._item.transformationMode().name))
for _ in range(40):
    win.view.zoom_by(1 / 1.25)
print("  zoomed out -> {:.2f}%  (clamped at {:.0f}%)".format(
    win.view.scale_factor * 100, 2.0))

# Rapid navigation: every intermediate decode must be discarded.
print("\n=== stale-result discard ===")
gen_before = win._generation
for i in range(len(win._playlist)):
    win.show_index(i)
pump(50)
print("  {} instant navigations -> generation {} -> {}".format(
    len(win._playlist), gen_before, win._generation))
wait_for_image()
print("  settled on: {}  showing {}x{}".format(
    win._playlist[win._index].name,
    win.view._item.pixmap().width(), win.view._item.pixmap().height()))

print("\nfailures:", failures if failures else "none")
win.loader.shutdown()
