"""Main window: playlist, navigation, shortcuts, status."""

from __future__ import annotations

import re
from pathlib import Path

import logging
import subprocess

from PySide6.QtCore import Qt, QTimer
from PySide6.QtGui import QAction, QGuiApplication, QKeySequence
from PySide6.QtWidgets import (
    QFileDialog,
    QLabel,
    QMainWindow,
    QMessageBox,
    QStatusBar,
    QStyle,
    QToolBar,
    QToolButton,
)

from . import __version__
from .applog import log_dir
from .cache import ImageCache, cache_key
from .decoders import get_registry
from .loader import ImageLoader
from .viewer import ImageView, dropped_paths

log = logging.getLogger(__name__)

PREFETCH_RADIUS = 2


def natural_key(path: Path):
    """Sort IMG_2.jpg before IMG_10.jpg, the way a file manager would."""
    return [
        int(part) if part.isdigit() else part.lower()
        for part in re.split(r"(\d+)", path.name)
    ]


class MainWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("Pic Viewer")
        self.resize(1200, 800)
        self.setAcceptDrops(True)

        self.registry = get_registry()
        self.extensions = self.registry.supported_extensions()

        self.view = ImageView(self)
        self.setCentralWidget(self.view)

        self.loader = ImageLoader(ImageCache(), self)
        self.loader.ready.connect(self._on_ready)
        self.loader.failed.connect(self._on_failed)
        self.view.scale_changed.connect(self._on_scale_changed)
        self.view.files_dropped.connect(self.open_paths)

        self._playlist: list[Path] = []
        self._index = -1
        self._generation = 0
        self._showing_preview = False
        self._current_meta: dict[str, str] = {}

        self._build_status_bar()
        self._build_actions()
        self._build_menus()
        self._build_toolbar()
        self._show_placeholder()
        log.info(
            "window ready: %d extensions, heif=%s",
            len(self.extensions), self.registry.heif_available,
        )

    # -- chrome ------------------------------------------------------------

    def _build_status_bar(self) -> None:
        bar = QStatusBar(self)
        self._status_left = QLabel("")
        self._status_right = QLabel("")
        bar.addWidget(self._status_left, 1)
        bar.addPermanentWidget(self._status_right, 0)
        self.setStatusBar(bar)

    def _action(self, text, shortcuts, slot, icon=None, tip=None) -> QAction:
        act = QAction(text, self)
        if shortcuts:
            act.setShortcuts([QKeySequence(s) for s in shortcuts])
        if icon is not None:
            act.setIcon(self.style().standardIcon(icon))
        hint = tip or text
        if shortcuts:
            hint += "  (" + shortcuts[0] + ")"
        act.setToolTip(hint)
        act.setStatusTip(hint)
        act.triggered.connect(slot)
        self.addAction(act)
        return act

    def _build_actions(self) -> None:
        SP = QStyle.StandardPixmap
        self.act_open = self._action(
            "&Open File...", ["Ctrl+O"], self.open_dialog, SP.SP_FileIcon)
        self.act_open_folder = self._action(
            "Open &Folder...", ["Ctrl+Shift+O"], self.open_folder_dialog, SP.SP_DirOpenIcon)
        self.act_reveal = self._action(
            "Show in E&xplorer", ["Ctrl+E"], self.reveal_current)
        self.act_copy = self._action(
            "&Copy Image", ["Ctrl+C"], self.copy_current)
        self.act_delete = self._action(
            "&Delete", ["Delete"], self.delete_current, SP.SP_TrashIcon)
        self.act_quit = self._action(
            "E&xit", ["Ctrl+Q"], self.close)

        self.act_prev = self._action(
            "&Previous", ["Left", "Up", "Backspace", "PgUp"],
            lambda: self.step(-1), SP.SP_ArrowLeft, "Previous image")
        self.act_next = self._action(
            "&Next", ["Right", "Down", "Space", "PgDown"],
            lambda: self.step(1), SP.SP_ArrowRight, "Next image")
        self.act_first = self._action("&First", ["Home"], lambda: self.show_index(0))
        self.act_last = self._action(
            "&Last", ["End"], lambda: self.show_index(len(self._playlist) - 1))

        self.act_zoom_in = self._action("Zoom &In", ["+", "="], lambda: self.view.zoom_by(1.25))
        self.act_zoom_out = self._action("Zoom &Out", ["-"], lambda: self.view.zoom_by(1 / 1.25))
        self.act_fit = self._action("&Fit to Window", ["0"], self.view.fit)
        self.act_actual = self._action("&Actual Size", ["1"], self.view.zoom_to_actual)
        self.act_fullscreen = self._action(
            "F&ullscreen", ["F11"], self.toggle_fullscreen, SP.SP_TitleBarMaxButton)

        self.act_log = self._action("Open &Log Folder", [], self.open_log_folder)
        self.act_about = self._action("&About", [], self.show_about)

        # Esc is not on the Exit action: it must leave fullscreen when in it,
        # and quitting on a stray Esc is a nasty surprise otherwise.
        escape = QAction(self)
        escape.setShortcut(QKeySequence("Esc"))
        escape.triggered.connect(self.close_or_leave_fullscreen)
        self.addAction(escape)

    def _build_menus(self) -> None:
        bar = self.menuBar()

        file_menu = bar.addMenu("&File")
        file_menu.addAction(self.act_open)
        file_menu.addAction(self.act_open_folder)
        file_menu.addSeparator()
        file_menu.addAction(self.act_reveal)
        file_menu.addAction(self.act_copy)
        file_menu.addAction(self.act_delete)
        file_menu.addSeparator()
        file_menu.addAction(self.act_quit)

        go_menu = bar.addMenu("&Go")
        go_menu.addAction(self.act_prev)
        go_menu.addAction(self.act_next)
        go_menu.addSeparator()
        go_menu.addAction(self.act_first)
        go_menu.addAction(self.act_last)

        view_menu = bar.addMenu("&View")
        view_menu.addAction(self.act_zoom_in)
        view_menu.addAction(self.act_zoom_out)
        view_menu.addSeparator()
        view_menu.addAction(self.act_fit)
        view_menu.addAction(self.act_actual)
        view_menu.addSeparator()
        view_menu.addAction(self.act_fullscreen)

        help_menu = bar.addMenu("&Help")
        help_menu.addAction(self.act_log)
        help_menu.addAction(self.act_about)

    def _build_toolbar(self) -> None:
        bar = QToolBar("Main", self)
        bar.setObjectName("main_toolbar")
        bar.setMovable(False)
        bar.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonIconOnly)
        bar.addAction(self.act_open)
        bar.addAction(self.act_open_folder)
        bar.addSeparator()
        bar.addAction(self.act_prev)
        bar.addAction(self.act_next)
        bar.addSeparator()
        for act, label in (
            (self.act_fit, "Fit"),
            (self.act_actual, "1:1"),
            (self.act_zoom_out, "−"),
            (self.act_zoom_in, "+"),
        ):
            button = QToolButton(bar)
            button.setDefaultAction(act)
            # These four have no sensible standard icon, so they get short text
            # instead of the blank square an icon-only button would show.
            button.setText(label)
            button.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonTextOnly)
            bar.addWidget(button)
        bar.addSeparator()
        bar.addAction(self.act_fullscreen)
        bar.addAction(self.act_delete)
        self.addToolBar(bar)
        self._toolbar = bar

    # -- opening -----------------------------------------------------------

    def open_dialog(self) -> None:
        patterns = " ".join("*" + ext for ext in sorted(self.extensions))
        path, _ = QFileDialog.getOpenFileName(
            self, "Open image", "", "Images (" + patterns + ");;All files (*)"
        )
        if path:
            self.open_path(Path(path))

    def open_folder_dialog(self) -> None:
        folder = QFileDialog.getExistingDirectory(self, "Open folder")
        if folder:
            self.open_path(Path(folder))

    def open_path(self, path: Path) -> None:
        """Open a file (with its folder as the playlist), or a whole folder."""
        log.info("open %s", path)
        if path.is_dir():
            self._playlist = self._scan(path)
            self.show_index(0 if self._playlist else -1)
            return

        if not path.exists():
            self._error("Not found: " + str(path))
            return

        self._playlist = self._scan(path.parent)
        try:
            index = self._playlist.index(path)
        except ValueError:
            # An extension we do not recognise, opened explicitly. Honour it.
            self._playlist.insert(0, path)
            index = 0
        self.show_index(index)

    def open_paths(self, paths: list[Path]) -> None:
        """Open dropped items: one path behaves as open_path, many become the playlist."""
        paths = [Path(p) for p in paths]
        if not paths:
            return
        if len(paths) == 1:
            self.open_path(paths[0])
            return

        files = [p for p in paths if p.is_file()]
        for folder in (p for p in paths if p.is_dir()):
            files.extend(self._scan(folder))
        if not files:
            self._error("Nothing to open in that selection")
            return

        self._playlist = sorted(set(files), key=natural_key)
        self.show_index(0)

    def _scan(self, folder: Path) -> list[Path]:
        try:
            entries = [
                p
                for p in folder.iterdir()
                if p.is_file() and p.suffix.lower() in self.extensions
            ]
        except OSError:
            return []
        return sorted(entries, key=natural_key)

    # -- navigation --------------------------------------------------------

    def step(self, delta: int) -> None:
        if not self._playlist:
            return
        self.show_index((self._index + delta) % len(self._playlist))

    def show_index(self, index: int) -> None:
        if not self._playlist or not 0 <= index < len(self._playlist):
            self._show_placeholder()
            return

        self._index = index
        self._generation += 1
        self._showing_preview = False
        self._current_meta = {}

        path = self._playlist[index]
        self.setWindowTitle(path.name + " - Pic Viewer")
        self._status_left.setText(
            "[{}/{}]  {}".format(index + 1, len(self._playlist), path.name)
        )

        cached = self.loader.cache.get(cache_key(path))
        if cached is not None and not cached.is_preview:
            self._display(cached, keep_view=False)
        else:
            self._status_right.setText("Loading...")
            self.loader.load(path, self._generation)
        self._update_action_state()

        QTimer.singleShot(50, self._prefetch_neighbours)

    def _prefetch_neighbours(self) -> None:
        if not self._playlist:
            return
        count = len(self._playlist)
        targets = [
            self._playlist[(self._index + offset) % count]
            for offset in range(-PREFETCH_RADIUS, PREFETCH_RADIUS + 1)
            if offset != 0
        ]
        self.loader.prefetch(targets)

    # -- results -----------------------------------------------------------

    def _on_ready(self, path: Path, result, generation: int) -> None:
        if generation != self._generation:
            return  # the user moved on while this was decoding
        if result.is_preview and not self._showing_preview and self.view.has_image():
            return  # never replace a full decode with a preview
        self._display(result, keep_view=self._showing_preview)

    def _display(self, result, keep_view: bool) -> None:
        self.view.set_message("")
        self._showing_preview = result.is_preview
        self._current_meta = result.metadata
        self.view.set_image(result.image, keep_view=keep_view)
        self._update_status()

    def _on_failed(self, path: Path, message: str, generation: int) -> None:
        if generation != self._generation:
            return
        log.debug("showing failure for %s", path.name)
        self.view.clear()
        self.view.set_message("Could not open " + path.name + "\n\n" + message)
        self._status_right.setText("Failed")
        self._update_action_state()

    def _on_scale_changed(self, _scale: float) -> None:
        self._update_status()

    def _update_status(self) -> None:
        self._update_action_state()
        if not self.view.has_image():
            self._status_right.setText("")
            return

        meta = self._current_meta
        parts = [meta.get("size", ""), meta.get("format", "")]
        if meta.get("decoder"):
            parts.append("via " + meta["decoder"])
        if meta.get("icc"):
            parts.append("ICC")
        if meta.get("frames"):
            parts.append(meta["frames"] + " frames")
        parts.append("{:.0f}%".format(self.view.scale_factor * 100))
        if self._showing_preview:
            parts.append("preview...")
        self._status_right.setText("   ".join(p for p in parts if p))

    def _show_placeholder(self) -> None:
        self._index = -1
        self.view.clear()
        self.setWindowTitle("Pic Viewer")
        self.view.set_message("Drop an image here, or press Ctrl+O to open one")
        self._status_left.setText("Ctrl+O to open, or drop a file here")
        self._status_right.setText("")
        self._update_action_state()

    # -- commands ----------------------------------------------------------

    def toggle_fullscreen(self) -> None:
        entering = not self.isFullScreen()
        log.info("fullscreen %s", "on" if entering else "off")
        for chrome in (self.statusBar(), self.menuBar(), self._toolbar):
            chrome.setVisible(not entering)
        if entering:
            self.showFullScreen()
        else:
            self.showNormal()

    def reveal_current(self) -> None:
        """Open Explorer with the current file selected."""
        path = self.current_path()
        if path is None:
            return
        log.info("reveal %s", path)
        try:
            subprocess.Popen(["explorer", "/select,", str(path)])
        except OSError as exc:
            log.error("reveal failed: %s", exc)
            self._error("Could not open Explorer: " + str(exc))

    def copy_current(self) -> None:
        """Put the decoded image on the clipboard."""
        if not self.view.has_image():
            return
        QGuiApplication.clipboard().setPixmap(self.view._item.pixmap())
        name = self.current_path()
        log.info("copied %s to clipboard", name.name if name else "?")
        self.statusBar().showMessage("Copied to clipboard", 2000)

    def open_log_folder(self) -> None:
        directory = log_dir()
        directory.mkdir(parents=True, exist_ok=True)
        try:
            subprocess.Popen(["explorer", str(directory)])
        except OSError as exc:
            self._error("Log folder: " + str(directory) + "\n\n" + str(exc))

    def show_about(self) -> None:
        decoders = ", ".join(self.registry.decoder_names())
        QMessageBox.about(
            self,
            "About Pic Viewer",
            "Pic Viewer {}\n\n"
            "{} file extensions across decoders: {}\n"
            "HEIF/AVIF support: {}\n\n"
            "Log file:\n{}".format(
                __version__,
                len(self.extensions),
                decoders,
                "yes" if self.registry.heif_available else "no",
                log_dir() / "picviewer.log",
            ),
        )

    def current_path(self) -> Path | None:
        if not self._playlist or not 0 <= self._index < len(self._playlist):
            return None
        return self._playlist[self._index]

    def _update_action_state(self) -> None:
        """Grey out what cannot act, so the toolbar tells the truth."""
        has_file = self.current_path() is not None
        many = len(self._playlist) > 1
        for act in (self.act_delete, self.act_reveal):
            act.setEnabled(has_file)
        self.act_copy.setEnabled(self.view.has_image())
        for act in (self.act_prev, self.act_next, self.act_first, self.act_last):
            act.setEnabled(many)
        for act in (self.act_zoom_in, self.act_zoom_out, self.act_fit, self.act_actual):
            act.setEnabled(self.view.has_image())

    def close_or_leave_fullscreen(self) -> None:
        if self.isFullScreen():
            self.toggle_fullscreen()
        else:
            self.close()

    def delete_current(self) -> None:
        if not self._playlist or self._index < 0:
            return
        path = self._playlist[self._index]
        answer = QMessageBox.question(
            self,
            "Delete",
            "Move to Recycle Bin?\n\n" + path.name,
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No,
        )
        if answer != QMessageBox.StandardButton.Yes:
            return

        try:
            from send2trash import send2trash

            send2trash(str(path))
            log.info("deleted %s", path)
        except Exception as exc:
            self._error("Delete failed: " + str(exc))
            return

        self._playlist.pop(self._index)
        if not self._playlist:
            self._show_placeholder()
        else:
            self.show_index(min(self._index, len(self._playlist) - 1))

    def _error(self, message: str) -> None:
        QMessageBox.warning(self, "Pic Viewer", message)

    # -- drag and drop -----------------------------------------------------
    #
    # The canvas covers almost the whole window and handles its own drops (see
    # ImageView). These only catch drops on the remaining chrome, such as the
    # status bar.

    def dragEnterEvent(self, event) -> None:
        if dropped_paths(event):
            event.setDropAction(Qt.DropAction.CopyAction)
            event.accept()
        else:
            event.ignore()

    def dragMoveEvent(self, event) -> None:
        self.dragEnterEvent(event)

    def dropEvent(self, event) -> None:
        paths = dropped_paths(event)
        if not paths:
            event.ignore()
            return
        event.setDropAction(Qt.DropAction.CopyAction)
        event.accept()
        self.open_paths(paths)

    def closeEvent(self, event) -> None:
        self.loader.shutdown()
        super().closeEvent(event)
