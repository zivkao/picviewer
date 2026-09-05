"""An unreadable file must explain itself, and must not spawn a modal per image."""

import os
import subprocess
import sys
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
ROOT = Path(__file__).resolve().parents[2]
WORK = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / ".testwork"
WORK.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(ROOT))

from PySide6.QtCore import QEventLoop, QTimer
from PySide6.QtWidgets import QApplication, QMessageBox

from picviewer.applog import setup_logging

setup_logging()

from picviewer.decoders import UnreadableFile, UnsupportedImage, get_registry
from picviewer.window import MainWindow

DENIED = WORK / "denied"

# Build the fixture here rather than expecting it to be set up outside: real
# image bytes plus a deny-read ACE, which reproduces the OneDrive placeholder
# failure (open succeeds for the directory, fails for the file).
FIXTURES = ("locked_a.heic", "locked_b.jpg", "locked_c.dng")
SOURCES = ("sample.heic", "sample.jpg", "synthetic.dng")
USER = os.environ.get("USERNAME", "")


def _icacls(path, *args):
    return subprocess.run(
        ["icacls", str(path), *args],
        capture_output=True, text=True,
    ).returncode


def build_fixture():
    DENIED.mkdir(parents=True, exist_ok=True)
    for name, source in zip(FIXTURES, SOURCES):
        target = DENIED / name
        src = ROOT / "samples" / source
        if not src.exists():
            print("  missing source sample:", src)
            continue
        _icacls(target, "/remove:d", USER)  # in case a previous run left it
        target.write_bytes(src.read_bytes())
        _icacls(target, "/deny", USER + ":(R)")


def clear_fixture():
    """Drop the deny ACEs so the files can be deleted again."""
    for name in FIXTURES:
        target = DENIED / name
        if target.exists():
            _icacls(target, "/remove:d", USER)


build_fixture()
app = QApplication([])
registry = get_registry()

print("=== registry: unreadable file ===")
for name in sorted(p.name for p in DENIED.iterdir()):
    path = DENIED / name
    try:
        registry.load_full(path)
        print("  {:<22} decoded (not denied after all)".format(name))
    except UnreadableFile as exc:
        first = str(exc).splitlines()[0]
        print("  {:<22} UnreadableFile: {}".format(name, first))
    except UnsupportedImage as exc:
        print("  {:<22} UnsupportedImage: {}".format(name, str(exc).splitlines()[0]))
    except Exception as exc:
        print("  {:<22} LEAKED {}: {}".format(name, type(exc).__name__, exc))

print("\n=== window: no modal dialogs while browsing ===")
modals = []
original = QMessageBox.warning


def spy(*args, **kwargs):
    modals.append(args[2] if len(args) > 2 else "?")
    return QMessageBox.StandardButton.Ok


QMessageBox.warning = staticmethod(spy)

win = MainWindow()
win.resize(900, 600)
win.show()


def pump(ms=150):
    loop = QEventLoop()
    QTimer.singleShot(ms, loop.quit)
    loop.exec()


win.open_path(DENIED)
pump(400)
print("  playlist:", len(win._playlist), "files")
for i in range(len(win._playlist)):
    win.show_index(i)
    pump(250)

print("  modal dialogs raised:", len(modals), "(should be 0)")
print("  canvas message shown :", repr(win.view._message[:60]))
print("  status bar           :", win._status_right.text())
print("  has image            :", win.view.has_image(), "(should be False)")

print("\n=== recovering: a readable file after failures ===")
win.open_path(Path(ROOT / "samples" / "sample.jpg"))
for _ in range(40):
    pump(50)
    if win.view.has_image() and not win._showing_preview:
        break
print("  has image:", win.view.has_image())
print("  message cleared:", repr(win.view._message))
print("  status:", win._status_right.text())

QMessageBox.warning = original
win.loader.shutdown()
clear_fixture()
