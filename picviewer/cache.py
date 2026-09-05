"""Byte-budgeted LRU cache for decoded images.

Counting entries instead of bytes is the classic way to blow up an image
viewer: a hundred thumbnails and a hundred 100-megapixel TIFFs are the same
number, three orders of magnitude apart in memory.
"""

from __future__ import annotations

import threading
from collections import OrderedDict
from pathlib import Path

from .decoders import DecodedImage

DEFAULT_BUDGET = 1024 * 1024 * 1024  # 1 GiB of decoded pixels
MAX_FAILED = 2048                    # remembered failures, oldest dropped first


def cache_key(path: Path) -> tuple[str, int, int]:
    """Identify a file by path plus mtime and size, so edits invalidate it."""
    try:
        st = path.stat()
        return (str(path), st.st_mtime_ns, st.st_size)
    except OSError:
        return (str(path), 0, 0)


class ImageCache:
    def __init__(self, budget: int = DEFAULT_BUDGET) -> None:
        self.budget = budget
        self._items: OrderedDict[tuple, DecodedImage] = OrderedDict()
        self._bytes = 0
        self._lock = threading.Lock()
        # Files that could not be decoded. Without this, a folder of unreadable
        # files (cloud placeholders, say) is retried on every navigation, since
        # nothing ever lands in the cache to stop the prefetcher.
        self._failed: OrderedDict[tuple, None] = OrderedDict()

    @property
    def nbytes(self) -> int:
        return self._bytes

    def get(self, key: tuple) -> DecodedImage | None:
        with self._lock:
            item = self._items.get(key)
            if item is not None:
                self._items.move_to_end(key)
            return item

    def put(self, key: tuple, item: DecodedImage) -> None:
        with self._lock:
            existing = self._items.pop(key, None)
            if existing is not None:
                # A full decode supersedes the preview it replaced.
                self._bytes -= existing.nbytes()

            self._items[key] = item
            self._bytes += item.nbytes()
            self._evict()

    def _evict(self) -> None:
        # Always keep the most recent entry, even if it alone exceeds budget --
        # evicting it would mean never being able to show that image at all.
        while self._bytes > self.budget and len(self._items) > 1:
            _, dropped = self._items.popitem(last=False)
            self._bytes -= dropped.nbytes()

    def mark_failed(self, key: tuple) -> None:
        with self._lock:
            self._failed[key] = None
            while len(self._failed) > MAX_FAILED:
                self._failed.popitem(last=False)

    def has_failed(self, key: tuple) -> bool:
        # The key carries mtime and size, so a file that is fixed or replaced
        # gets a new key and is tried again on its own.
        with self._lock:
            return key in self._failed

    def clear(self) -> None:
        with self._lock:
            self._items.clear()
            self._failed.clear()
            self._bytes = 0
