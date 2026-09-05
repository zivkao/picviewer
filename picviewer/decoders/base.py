"""Decoder interface shared by every format backend."""

from __future__ import annotations

from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np
from PIL import Image
from PySide6.QtGui import QImage

# Confidence levels returned by Decoder.score(). Anything <= 0 means "cannot
# handle"; the registry tries candidates from highest to lowest and falls
# through to the next on failure.
SCORE_EXACT = 100      # magic bytes identify a format this decoder owns
SCORE_LIKELY = 60      # plausible from the container, e.g. TIFF-based RAW
SCORE_EXTENSION = 40   # only the file suffix suggests it
SCORE_FALLBACK = 10    # last resort, let the library try


@dataclass
class DecodedImage:
    """One decode result, possibly a stand-in for a slower full decode."""

    image: QImage
    is_preview: bool = False
    full_size: tuple[int, int] | None = None
    metadata: dict[str, str] = field(default_factory=dict)

    def nbytes(self) -> int:
        return max(self.image.sizeInBytes(), 1)


class Decoder(ABC):
    name: str = "decoder"

    @abstractmethod
    def score(self, path: Path, container: str) -> int:
        """How confident this decoder is that it can read *path*."""

    def load_preview(
        self, path: Path, container: str, max_size: tuple[int, int]
    ) -> DecodedImage | None:
        """A fast, possibly degraded decode. None if there is no fast path."""
        return None

    @abstractmethod
    def load_full(
        self, path: Path, container: str, max_size: tuple[int, int] | None = None
    ) -> DecodedImage:
        """The accurate decode, optionally downscaled to fit *max_size*."""


def pil_to_qimage(im: Image.Image) -> QImage:
    """Convert a PIL image to a QImage that owns its pixel buffer."""
    if im.mode == "RGBA":
        fmt, channels = QImage.Format.Format_RGBA8888, 4
    elif im.mode == "RGB":
        fmt, channels = QImage.Format.Format_RGB888, 3
    else:
        # Anything with alpha or a palette becomes RGBA so transparency
        # survives; everything else (L, CMYK, I;16, F) becomes RGB.
        needs_alpha = "A" in im.getbands() or im.mode in ("P", "PA")
        im = im.convert("RGBA" if needs_alpha else "RGB")
        fmt, channels = (
            (QImage.Format.Format_RGBA8888, 4)
            if needs_alpha
            else (QImage.Format.Format_RGB888, 3)
        )

    data = im.tobytes()
    qimg = QImage(data, im.width, im.height, im.width * channels, fmt)
    # QImage does not take ownership of `data`, so copy before it is collected.
    return qimg.copy()


def array_to_qimage(arr: np.ndarray) -> QImage:
    """Convert an HxWx3 uint8 array (rawpy output) to a QImage."""
    if arr.dtype != np.uint8:
        arr = (arr >> 8).astype(np.uint8) if arr.dtype == np.uint16 else arr.astype(np.uint8)
    arr = np.ascontiguousarray(arr[:, :, :3])
    h, w, _ = arr.shape
    qimg = QImage(arr.data, w, h, w * 3, QImage.Format.Format_RGB888)
    return qimg.copy()
