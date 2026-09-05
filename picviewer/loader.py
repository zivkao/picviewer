"""Asynchronous decoding: worker pool, two-stage delivery, neighbour prefetch."""

from __future__ import annotations

import logging
import time
from pathlib import Path

from PySide6.QtCore import QObject, QRunnable, QThreadPool, Signal

from .cache import ImageCache, cache_key
from .decoders import DecodedImage, get_registry

log = logging.getLogger(__name__)

# Beyond this we downscale on decode. A 200MP RGB888 image is already 600MB;
# letting the truly pathological files through would starve everything else.
MAX_DECODE_PIXELS = 200_000_000
PREVIEW_MAX_SIZE = (3840, 3840)

PRIORITY_FOREGROUND = 10
PRIORITY_PREFETCH = 0


class _Signals(QObject):
    ready = Signal(object, object, int)   # Path, DecodedImage, generation
    failed = Signal(object, str, int)     # Path, message, generation


class _LoadTask(QRunnable):
    """Decode one file, emitting a preview first when the format offers one."""

    def __init__(
        self,
        path: Path,
        generation: int,
        cache: ImageCache,
        signals: _Signals,
        cache_only: bool = False,
    ) -> None:
        super().__init__()
        self.path = path
        self.generation = generation
        self.cache = cache
        self.signals = signals
        self.cache_only = cache_only

    def run(self) -> None:
        key = cache_key(self.path)
        registry = get_registry()

        cached = self.cache.get(key)
        if cached is not None and not cached.is_preview:
            log.debug("cache hit %s", self.path.name)
            self._emit_ready(cached)
            return

        if not self.cache_only and cached is None:
            self._emit_preview(registry)

        started = time.perf_counter()
        try:
            result = registry.load_full(self.path, max_size=self._decode_cap())
        except Exception as exc:
            self.cache.mark_failed(key)
            # Only the first line: these messages are several lines of guidance
            # meant for the screen, and the full text in the log per attempt
            # buries everything else.
            summary = str(exc).splitlines()[0]
            if self.cache_only:
                log.debug("prefetch failed for %s: %s", self.path.name, summary)
            else:
                log.error("decode failed for %s: %s", self.path.name, summary)
                self.signals.failed.emit(self.path, str(exc), self.generation)
            return

        self.cache.put(key, result)
        log.info(
            "decoded %s via %s in %.0fms -> %dx%d (%.1f MB)%s",
            self.path.name,
            result.metadata.get("decoder", "?"),
            (time.perf_counter() - started) * 1000,
            result.image.width(),
            result.image.height(),
            result.nbytes() / 1e6,
            " [prefetch]" if self.cache_only else "",
        )
        self._emit_ready(result)

    def _emit_preview(self, registry) -> None:
        """Show something immediately for formats that decode slowly."""
        try:
            preview = registry.load_preview(self.path, PREVIEW_MAX_SIZE)
        except Exception:
            log.debug("preview failed for %s", self.path, exc_info=True)
            return
        if preview is not None:
            log.info(
                "preview for %s -> %dx%d",
                self.path.name, preview.image.width(), preview.image.height(),
            )
            self.signals.ready.emit(self.path, preview, self.generation)

    def _emit_ready(self, result: DecodedImage) -> None:
        if not self.cache_only:
            self.signals.ready.emit(self.path, result, self.generation)

    @staticmethod
    def _decode_cap() -> tuple[int, int]:
        side = int(MAX_DECODE_PIXELS**0.5)
        return (side, side)


class ImageLoader(QObject):
    """Front door for decoding. Results arrive on the GUI thread."""

    ready = Signal(object, object, int)
    failed = Signal(object, str, int)

    def __init__(self, cache: ImageCache | None = None, parent: QObject | None = None):
        super().__init__(parent)
        self.cache = cache or ImageCache()
        self._pool = QThreadPool(self)
        # Leave a core for the GUI thread; decoding is CPU-bound and will
        # happily saturate everything otherwise.
        self._pool.setMaxThreadCount(max(2, QThreadPool.globalInstance().maxThreadCount() - 1))

        self._signals = _Signals(self)
        self._signals.ready.connect(self.ready)
        self._signals.failed.connect(self.failed)

    def load(self, path: Path, generation: int) -> None:
        task = _LoadTask(path, generation, self.cache, self._signals)
        self._pool.start(task, PRIORITY_FOREGROUND)

    def prefetch(self, paths: list[Path]) -> None:
        """Warm the cache for images the user is about to reach."""
        for path in paths:
            key = cache_key(path)
            if self.cache.get(key) is not None or self.cache.has_failed(key):
                continue
            task = _LoadTask(path, -1, self.cache, self._signals, cache_only=True)
            self._pool.start(task, PRIORITY_PREFETCH)

    def shutdown(self) -> None:
        self._pool.clear()
        self._pool.waitForDone(3000)
