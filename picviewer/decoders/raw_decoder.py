"""RAW decoder built on rawpy/LibRaw, with a fast embedded-preview path.

Demosaicing a 24MP RAW costs 1-3 seconds, which would make every page turn
feel broken. Cameras embed a full-size JPEG preview in the file, so we show
that within ~50ms and let the real decode replace it when it lands.
"""

from __future__ import annotations

import io
import logging
from pathlib import Path

import rawpy
from PIL import Image

from .. import sniff
from ..color import normalize
from .base import (
    SCORE_EXACT,
    SCORE_LIKELY,
    DecodedImage,
    Decoder,
    array_to_qimage,
    pil_to_qimage,
)

log = logging.getLogger(__name__)

# LibRaw covers far more than this, but these are the ones worth claiming
# with high confidence. Anything else TIFF-shaped still gets a shot below.
EXTENSIONS = {
    ".cr2", ".cr3", ".crw",           # Canon
    ".nef", ".nrw",                   # Nikon
    ".arw", ".srf", ".sr2",           # Sony
    ".orf",                           # Olympus
    ".rw2",                           # Panasonic
    ".raf",                           # Fujifilm
    ".pef", ".dng",                   # Pentax / Adobe
    ".srw",                           # Samsung
    ".x3f",                           # Sigma
    ".erf", ".kdc", ".dcr",           # Epson / Kodak
    ".mrw",                           # Minolta
    ".rwl",                           # Leica
    ".iiq",                           # Phase One
    ".3fr", ".fff",                   # Hasselblad
    ".mef", ".mos",                   # Mamiya / Leaf
    ".ari", ".raw", ".rwz",
}

CONTAINERS = {sniff.CR3, sniff.RAF, sniff.RW2, sniff.ORF, sniff.X3F}

# Extensions that mean "this really is an ordinary TIFF, not a RAW".
TIFF_EXTENSIONS = {".tif", ".tiff"}


class RawDecoder(Decoder):
    name = "raw"

    def score(self, path: Path, container: str) -> int:
        ext = path.suffix.lower()
        if container in CONTAINERS or ext in EXTENSIONS:
            return SCORE_EXACT
        # NEF, ARW, DNG, CR2 and friends are all TIFF containers, so magic
        # bytes alone cannot separate them from a photo saved as TIFF. A TIFF
        # container not named .tif is overwhelmingly a RAW file, so claim it;
        # a genuine .tif still goes to Pillow first and falls through to here.
        if container == sniff.TIFF:
            return SCORE_EXACT if ext not in TIFF_EXTENSIONS else SCORE_LIKELY
        return 0

    def load_preview(
        self, path: Path, container: str, max_size: tuple[int, int]
    ) -> DecodedImage | None:
        try:
            with rawpy.imread(str(path)) as raw:
                full_size = (raw.sizes.width, raw.sizes.height)
                thumb = raw.extract_thumb()
        except (rawpy.LibRawNoThumbnailError, rawpy.LibRawUnsupportedThumbnailError):
            return None
        except rawpy.LibRawError:
            log.debug("thumbnail extraction failed for %s", path, exc_info=True)
            return None

        if thumb.format == rawpy.ThumbFormat.JPEG:
            with Image.open(io.BytesIO(thumb.data)) as im:
                im.load()
                im = normalize(im)
                if im.width > max_size[0] or im.height > max_size[1]:
                    im.thumbnail(max_size, Image.Resampling.LANCZOS)
                qimg = pil_to_qimage(im)
        else:
            qimg = array_to_qimage(thumb.data)

        return DecodedImage(
            image=qimg,
            is_preview=True,
            full_size=full_size,
            metadata={"format": "RAW", "size": f"{full_size[0]} x {full_size[1]}"},
        )

    def load_full(
        self, path: Path, container: str, max_size: tuple[int, int] | None = None
    ) -> DecodedImage:
        with rawpy.imread(str(path)) as raw:
            full_size = (raw.sizes.width, raw.sizes.height)
            # Half-size demosaicing is roughly 4x faster. Only take it when the
            # result still exceeds what we are asked to display.
            half = bool(
                max_size
                and full_size[0] >= max_size[0] * 2
                and full_size[1] >= max_size[1] * 2
            )
            rgb = raw.postprocess(
                use_camera_wb=True,
                output_color=rawpy.ColorSpace.sRGB,
                output_bps=8,
                half_size=half,
                no_auto_bright=False,
            )
            meta = self._metadata(raw, full_size, half)

        qimg = array_to_qimage(rgb)
        if max_size and (qimg.width() > max_size[0] or qimg.height() > max_size[1]):
            from PySide6.QtCore import Qt

            qimg = qimg.scaled(
                max_size[0],
                max_size[1],
                Qt.AspectRatioMode.KeepAspectRatio,
                Qt.TransformationMode.SmoothTransformation,
            )

        return DecodedImage(
            image=qimg, is_preview=False, full_size=full_size, metadata=meta
        )

    def _metadata(self, raw, full_size: tuple[int, int], half: bool) -> dict[str, str]:
        meta = {
            "format": "RAW",
            "size": f"{full_size[0]} x {full_size[1]}",
            "demosaic": "half-size" if half else "full",
        }
        try:
            meta["colors"] = str(raw.num_colors)
            meta["pattern"] = raw.color_desc.decode("ascii", "replace")
        except Exception:
            pass
        return meta
