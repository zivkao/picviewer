"""The image canvas: zoom, pan, fit."""

from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import QPointF, QRectF, Qt, Signal
from PySide6.QtGui import (
    QBrush,
    QColor,
    QFont,
    QImage,
    QPainter,
    QPen,
    QPixmap,
    QWheelEvent,
)
from PySide6.QtWidgets import QGraphicsPixmapItem, QGraphicsScene, QGraphicsView

MIN_SCALE = 0.02
MAX_SCALE = 40.0
ZOOM_STEP = 1.25
# Past this magnification, smoothing just blurs the pixels the user zoomed in
# to inspect, so switch to nearest-neighbour.
CRISP_THRESHOLD = 4.0

BACKGROUND = QColor(32, 33, 36)
CHECKER_LIGHT = QColor(74, 76, 80)
CHECKER_DARK = QColor(58, 60, 64)
CHECKER_SIZE = 12

MESSAGE_TEXT = QColor(168, 172, 180)
DROP_ACCENT = QColor(120, 175, 255)
DROP_VEIL = QColor(120, 175, 255, 28)


class ImageView(QGraphicsView):
    """A pan/zoom canvas for a single image."""

    scale_changed = Signal(float)
    files_dropped = Signal(list)

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self._scene = QGraphicsScene(self)
        self._item = QGraphicsPixmapItem()
        self._item.setTransformationMode(Qt.TransformationMode.SmoothTransformation)
        self._scene.addItem(self._item)
        self.setScene(self._scene)

        self.setRenderHints(
            QPainter.RenderHint.SmoothPixmapTransform | QPainter.RenderHint.Antialiasing
        )
        self.setTransformationAnchor(QGraphicsView.ViewportAnchor.AnchorUnderMouse)
        self.setResizeAnchor(QGraphicsView.ViewportAnchor.AnchorViewCenter)
        self.setDragMode(QGraphicsView.DragMode.ScrollHandDrag)
        self.setFrameShape(QGraphicsView.Shape.NoFrame)
        self.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.setBackgroundBrush(QBrush(BACKGROUND))

        self._fit_mode = True
        self._has_alpha = False
        self._checker = _make_checker()

        # Drops must be handled here rather than on the main window. A
        # QGraphicsView turns on acceptDrops for its viewport so it can offer
        # drags to graphics items, and the viewport marks the event handled
        # either way -- so a drop on the canvas never reaches the window.
        self.setAcceptDrops(True)
        self._drag_hover = False
        self._message = ""

    # -- content -----------------------------------------------------------

    def set_image(self, image: QImage, keep_view: bool = False) -> None:
        """Show *image*. With keep_view, preserve the current zoom and pan.

        keep_view matters when a full-quality decode replaces the preview that
        is already on screen -- the picture should sharpen in place, not jump
        back to fit.
        """
        self._has_alpha = image.hasAlphaChannel()
        self._item.setPixmap(QPixmap.fromImage(image))
        self._scene.setSceneRect(QRectF(image.rect()))

        if keep_view and not self._fit_mode:
            self._apply_transformation_mode()
        else:
            self.fit()

    def clear(self) -> None:
        self._item.setPixmap(QPixmap())
        self._scene.setSceneRect(QRectF())
        self.viewport().update()

    def set_message(self, text: str) -> None:
        """Show text on the empty canvas.

        Failures are reported here rather than in a dialog: stepping through a
        folder whose files are all unreadable would otherwise mean dismissing
        one modal per image.
        """
        self._message = text or ""
        self.viewport().update()

    def has_image(self) -> bool:
        return not self._item.pixmap().isNull()

    # -- zoom --------------------------------------------------------------

    @property
    def scale_factor(self) -> float:
        return self.transform().m11()

    def fit(self) -> None:
        """Shrink oversized images to the viewport; leave small ones at 1:1."""
        if not self.has_image():
            return
        self._fit_mode = True
        self.resetTransform()

        rect = self._scene.sceneRect()
        view = self.viewport().rect()
        if rect.width() > view.width() or rect.height() > view.height():
            self.fitInView(rect, Qt.AspectRatioMode.KeepAspectRatio)

        self.centerOn(rect.center())
        self._after_zoom()

    def zoom_to_actual(self) -> None:
        if not self.has_image():
            return
        self._fit_mode = False
        self.resetTransform()
        self._after_zoom()

    def toggle_fit(self) -> None:
        if self._fit_mode and abs(self.scale_factor - 1.0) > 1e-3:
            self.zoom_to_actual()
        else:
            self.fit()

    def zoom_by(self, factor: float) -> None:
        if not self.has_image():
            return
        target = self.scale_factor * factor
        clamped = max(MIN_SCALE, min(MAX_SCALE, target))
        if abs(clamped - self.scale_factor) < 1e-6:
            return
        self._fit_mode = False
        self.scale(clamped / self.scale_factor, clamped / self.scale_factor)
        self._after_zoom()

    def _after_zoom(self) -> None:
        self._apply_transformation_mode()
        self.scale_changed.emit(self.scale_factor)

    def _apply_transformation_mode(self) -> None:
        crisp = self.scale_factor >= CRISP_THRESHOLD
        self._item.setTransformationMode(
            Qt.TransformationMode.FastTransformation
            if crisp
            else Qt.TransformationMode.SmoothTransformation
        )

    # -- events ------------------------------------------------------------

    def wheelEvent(self, event: QWheelEvent) -> None:
        delta = event.angleDelta().y()
        if delta:
            self.zoom_by(ZOOM_STEP if delta > 0 else 1 / ZOOM_STEP)
            event.accept()
        else:
            super().wheelEvent(event)

    def mouseDoubleClickEvent(self, event) -> None:
        self.toggle_fit()
        event.accept()

    def resizeEvent(self, event) -> None:
        super().resizeEvent(event)
        if self._fit_mode:
            self.fit()

    # -- drag and drop -----------------------------------------------------

    def dragEnterEvent(self, event) -> None:
        if dropped_paths(event):
            self._set_hover(True)
            event.setDropAction(Qt.DropAction.CopyAction)
            event.accept()
        else:
            event.ignore()

    def dragMoveEvent(self, event) -> None:
        # Without this the drag is refused mid-flight and the cursor shows the
        # "no entry" sign even though dragEnterEvent accepted.
        if self._drag_hover:
            event.setDropAction(Qt.DropAction.CopyAction)
            event.accept()
        else:
            event.ignore()

    def dragLeaveEvent(self, event) -> None:
        self._set_hover(False)
        event.accept()

    def dropEvent(self, event) -> None:
        self._set_hover(False)
        paths = dropped_paths(event)
        if not paths:
            event.ignore()
            return
        event.setDropAction(Qt.DropAction.CopyAction)
        event.accept()
        self.files_dropped.emit(paths)

    def _set_hover(self, hovering: bool) -> None:
        if self._drag_hover != hovering:
            self._drag_hover = hovering
            self.viewport().update()

    # -- painting ----------------------------------------------------------

    def paintEvent(self, event) -> None:
        super().paintEvent(event)

        if self._message and not self.has_image():
            painter = QPainter(self.viewport())
            painter.setPen(MESSAGE_TEXT)
            font = QFont(painter.font())
            font.setPointSize(max(10, font.pointSize() + 1))
            painter.setFont(font)
            painter.drawText(
                self.viewport().rect().adjusted(40, 40, -40, -40),
                Qt.AlignmentFlag.AlignCenter | Qt.TextFlag.TextWordWrap,
                self._message,
            )
            painter.end()

        if not self._drag_hover:
            return

        painter = QPainter(self.viewport())
        rect = self.viewport().rect().adjusted(8, 8, -9, -9)
        painter.fillRect(self.viewport().rect(), DROP_VEIL)
        pen = QPen(DROP_ACCENT, 2, Qt.PenStyle.DashLine)
        painter.setPen(pen)
        painter.drawRoundedRect(rect, 10, 10)

        font = QFont(painter.font())
        font.setPointSize(max(11, font.pointSize() + 3))
        painter.setFont(font)
        painter.drawText(rect, Qt.AlignmentFlag.AlignCenter, "Drop to open")
        painter.end()

    def drawBackground(self, painter: QPainter, rect: QRectF) -> None:
        painter.fillRect(rect, BACKGROUND)
        if self._has_alpha and self.has_image():
            # Checkerboard only under the image, so transparent regions read as
            # transparent rather than as dark grey.
            painter.save()
            painter.setBrushOrigin(QPointF(0, 0))
            painter.fillRect(
                rect.intersected(self._scene.sceneRect()), QBrush(self._checker)
            )
            painter.restore()


def dropped_paths(event) -> list[Path]:
    """Local filesystem paths carried by a drag, ignoring remote URLs."""
    mime = event.mimeData()
    if mime is None or not mime.hasUrls():
        return []
    paths = []
    for url in mime.urls():
        local = url.toLocalFile()
        if local:
            paths.append(Path(local))
    return paths


def _make_checker() -> QPixmap:
    pm = QPixmap(CHECKER_SIZE * 2, CHECKER_SIZE * 2)
    pm.fill(CHECKER_DARK)
    painter = QPainter(pm)
    painter.fillRect(0, 0, CHECKER_SIZE, CHECKER_SIZE, CHECKER_LIGHT)
    painter.fillRect(
        CHECKER_SIZE, CHECKER_SIZE, CHECKER_SIZE, CHECKER_SIZE, CHECKER_LIGHT
    )
    painter.end()
    return pm
