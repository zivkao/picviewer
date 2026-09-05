"""Pillow-backed decoder: the workhorse for mainstream formats."""

from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageFile

from .. import sniff
from ..color import normalize
from .base import (
    SCORE_EXACT,
    SCORE_EXTENSION,
    SCORE_LIKELY,
    SCORE_FALLBACK,
    DecodedImage,
    Decoder,
    pil_to_qimage,
)

# Better a partially decoded image than a hard failure on a truncated file.
ImageFile.LOAD_TRUNCATED_IMAGES = True

# Pillow has no fixed ceiling that suits both "block decompression bombs" and
# "open my 300MP panorama", so raise it well past any real camera output.
Image.MAX_IMAGE_PIXELS = 1_000_000_000

CONTAINERS = {
    sniff.JPEG, sniff.PNG, sniff.GIF, sniff.BMP,
    sniff.TIFF, sniff.WEBP, sniff.PSD, sniff.ICO, sniff.JP2,
}

EXTENSIONS = {
    ".jpg", ".jpeg", ".jpe", ".jfif", ".png", ".apng", ".gif", ".bmp", ".dib",
    ".tif", ".tiff", ".webp", ".psd", ".ico", ".icns", ".jp2", ".j2k", ".jpf",
    ".jpx", ".tga", ".ppm", ".pgm", ".pbm", ".pnm", ".pcx", ".sgi", ".xbm",
    ".dds", ".im", ".msp", ".blp", ".ftc", ".ftu",
}


class PillowDecoder(Decoder):
    name = "pillow"
    containers = CONTAINERS
    extensions = EXTENSIONS

    def score(self, path: Path, container: str) -> int:
        ext = path.suffix.lower()
        if container in self.containers:
            # A container match alone is not enough to win. Every TIFF-based
            # RAW (NEF, ARW, CR2, DNG...) sniffs as plain TIFF, and Pillow will
            # happily decode such a file -- returning the small embedded
            # preview from IFD0 instead of the actual photo, without ever
            # raising. Requiring the extension to agree keeps those on the
            # RAW decoder, which ranks itself higher for them.
            return SCORE_EXACT if ext in self.extensions else SCORE_LIKELY
        if ext in self.extensions:
            return SCORE_EXTENSION
        # Pillow recognises a long tail of formats we do not enumerate, so let
        # it try last rather than refusing outright.
        return SCORE_FALLBACK if container == sniff.UNKNOWN else 0

    def load_full(
        self, path: Path, container: str, max_size: tuple[int, int] | None = None
    ) -> DecodedImage:
        with Image.open(path) as im:
            full_size = _oriented_size(im)
            if max_size:
                # For JPEG (and JPEG2000) this makes libjpeg decode at 1/2,
                # 1/4 or 1/8 scale outright -- several times faster than
                # decoding full size and resampling afterwards. It is a no-op
                # for every other format.
                im.draft("RGB", max_size)

            im.load()
            meta = self._metadata(im, full_size)
            im = normalize(im)

            if max_size and (im.width > max_size[0] or im.height > max_size[1]):
                im.thumbnail(max_size, Image.Resampling.LANCZOS)

            qimg = pil_to_qimage(im)

        return DecodedImage(
            image=qimg,
            is_preview=False,
            full_size=full_size,
            metadata=meta,
        )

    def _metadata(self, im: Image.Image, full_size: tuple[int, int]) -> dict[str, str]:
        meta = {
            "format": im.format or "?",
            "mode": im.mode,
            "size": f"{full_size[0]} x {full_size[1]}",
        }
        frames = getattr(im, "n_frames", 1)
        if frames > 1:
            meta["frames"] = str(frames)
        if im.info.get("icc_profile"):
            meta["icc"] = "embedded"
        return meta


EXIF_ORIENTATION = 0x0112
# Orientations 5-8 transpose the image, so width and height swap.
_SWAPPING_ORIENTATIONS = {5, 6, 7, 8}


def _oriented_size(im: Image.Image) -> tuple[int, int]:
    """Source dimensions as they will appear once EXIF rotation is applied."""
    try:
        orientation = im.getexif().get(EXIF_ORIENTATION, 1)
    except Exception:
        orientation = 1
    w, h = im.size
    return (h, w) if orientation in _SWAPPING_ORIENTATIONS else (w, h)


class HeifDecoder(PillowDecoder):
    """HEIC/HEIF/AVIF via pillow-heif, which plugs into Pillow's opener table.

    Kept as its own decoder so scoring stays explicit and so a missing
    pillow-heif degrades to "cannot open HEIC" instead of breaking everything.
    """

    name = "heif"
    containers = {sniff.HEIF, sniff.AVIF}
    extensions = {".heic", ".heif", ".hif", ".avif"}

    def score(self, path: Path, container: str) -> int:
        if container in self.containers:
            return SCORE_EXACT
        if path.suffix.lower() in self.extensions:
            return SCORE_EXTENSION
        return 0


def register_heif() -> bool:
    """Install the HEIF/AVIF openers into Pillow. False if unavailable."""
    try:
        import pillow_heif
    except ImportError:
        return False
    pillow_heif.register_heif_opener()
    try:
        pillow_heif.register_avif_opener()
    except AttributeError:
        pass  # older pillow-heif folds AVIF into the HEIF opener
    return True
