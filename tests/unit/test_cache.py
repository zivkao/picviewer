"""Byte-budgeted LRU and the failure memory."""

from pathlib import Path

import pytest
from PySide6.QtGui import QImage

from picviewer.cache import ImageCache, cache_key
from picviewer.decoders import DecodedImage


def make(width: int, height: int) -> DecodedImage:
    """A DecodedImage of a known size. QImage needs no QApplication."""
    image = QImage(width, height, QImage.Format.Format_RGB888)
    image.fill(0)
    return DecodedImage(image=image)


def test_nbytes_tracks_the_pixel_buffer_not_the_entry_count():
    small = make(10, 10)
    large = make(1000, 1000)
    assert large.nbytes() > small.nbytes() * 1000


def test_get_returns_what_was_put():
    cache = ImageCache(budget=10_000_000)
    item = make(100, 100)
    cache.put(("a",), item)
    assert cache.get(("a",)) is item


def test_missing_key_returns_none():
    assert ImageCache().get(("nothing",)) is None


def test_eviction_is_by_bytes_not_by_count():
    """A hundred thumbnails and a hundred huge images are the same count."""
    one = make(500, 500)
    cache = ImageCache(budget=one.nbytes() * 2 + 100)

    cache.put(("a",), make(500, 500))
    cache.put(("b",), make(500, 500))
    assert cache.get(("a",)) is not None
    assert cache.get(("b",)) is not None

    cache.put(("c",), make(500, 500))
    assert cache.get(("a",)) is None, "oldest should have been evicted"
    assert cache.get(("c",)) is not None


def test_many_tiny_entries_survive_where_few_large_ones_would_not():
    tiny = make(4, 4)
    cache = ImageCache(budget=tiny.nbytes() * 50)
    for i in range(40):
        cache.put((i,), make(4, 4))
    assert sum(cache.get((i,)) is not None for i in range(40)) == 40


def test_a_get_refreshes_recency():
    one = make(300, 300)
    cache = ImageCache(budget=one.nbytes() * 2 + 100)
    cache.put(("a",), make(300, 300))
    cache.put(("b",), make(300, 300))

    cache.get(("a",))                 # a is now the most recently used
    cache.put(("c",), make(300, 300))

    assert cache.get(("a",)) is not None
    assert cache.get(("b",)) is None, "b was least recently used"


def test_the_newest_entry_survives_even_when_it_alone_exceeds_budget():
    """Evicting it would mean never being able to show that image at all."""
    cache = ImageCache(budget=1000)
    big = make(800, 800)
    cache.put(("big",), big)
    assert cache.get(("big",)) is big
    assert cache.nbytes > cache.budget


def test_replacing_a_key_does_not_double_count_its_bytes():
    cache = ImageCache(budget=10_000_000)
    cache.put(("a",), make(200, 200))
    after_first = cache.nbytes
    cache.put(("a",), make(200, 200))
    assert cache.nbytes == after_first


def test_bytes_drop_back_to_zero_on_clear():
    cache = ImageCache()
    cache.put(("a",), make(100, 100))
    cache.clear()
    assert cache.nbytes == 0
    assert cache.get(("a",)) is None


# -- failure memory --------------------------------------------------------


def test_a_failure_is_remembered():
    cache = ImageCache()
    assert not cache.has_failed(("x",))
    cache.mark_failed(("x",))
    assert cache.has_failed(("x",))


def test_failures_do_not_grow_without_bound():
    from picviewer.cache import MAX_FAILED

    cache = ImageCache()
    for i in range(MAX_FAILED + 50):
        cache.mark_failed((i,))
    assert not cache.has_failed((0,)), "oldest failures should be dropped"
    assert cache.has_failed((MAX_FAILED + 49,))


def test_clear_forgets_failures_too():
    cache = ImageCache()
    cache.mark_failed(("x",))
    cache.clear()
    assert not cache.has_failed(("x",))


# -- cache keys ------------------------------------------------------------


def test_key_changes_when_the_file_changes(tmp_path: Path):
    """mtime and size are in the key, so an edited file is not served stale."""
    path = tmp_path / "f.bin"
    path.write_bytes(b"one")
    first = cache_key(path)

    path.write_bytes(b"two but longer")
    assert cache_key(path) != first


def test_key_is_stable_for_an_untouched_file(tmp_path: Path):
    path = tmp_path / "f.bin"
    path.write_bytes(b"data")
    assert cache_key(path) == cache_key(path)


def test_key_of_a_missing_file_does_not_raise():
    key = cache_key(Path("no/such/file.jpg"))
    assert isinstance(key, tuple)


def test_a_repaired_file_is_retried_because_its_key_moved(tmp_path: Path):
    """The failure memory must not condemn a file forever."""
    path = tmp_path / "f.bin"
    path.write_bytes(b"broken")
    cache = ImageCache()
    cache.mark_failed(cache_key(path))

    path.write_bytes(b"fixed and different")
    assert not cache.has_failed(cache_key(path))
