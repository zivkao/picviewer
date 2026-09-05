"""Container-format detection from magic bytes.

Extensions lie often enough that we sniff first and only fall back to the
suffix when the header is inconclusive. The result is a coarse *container*
label, not a decoder choice -- several RAW formats are TIFF containers, so
telling ``nef`` from ``tiff`` is left to the decoders' own scoring.
"""

from __future__ import annotations

from pathlib import Path

HEADER_BYTES = 32

# Container labels. Decoders match on these, so keep them stable.
JPEG = "jpeg"
PNG = "png"
GIF = "gif"
BMP = "bmp"
TIFF = "tiff"
WEBP = "webp"
HEIF = "heif"
AVIF = "avif"
PSD = "psd"
ICO = "ico"
JP2 = "jp2"
CR3 = "cr3"
RAF = "raf"
RW2 = "rw2"
ORF = "orf"
X3F = "x3f"
PNM = "pnm"
UNKNOWN = "unknown"

# ISO-BMFF brands, read from bytes 8..12. HEIC and AVIF share the container.
_FTYP_BRANDS = {
    b"heic": HEIF, b"heix": HEIF, b"heim": HEIF, b"heis": HEIF,
    b"hevc": HEIF, b"hevx": HEIF, b"mif1": HEIF, b"msf1": HEIF,
    b"avif": AVIF, b"avis": AVIF,
    b"crx ": CR3,
    b"jp2 ": JP2, b"jpx ": JP2,
}

# Extensions we accept when the header tells us nothing useful.
_EXT_FALLBACK = {
    ".jpg": JPEG, ".jpeg": JPEG, ".jpe": JPEG, ".jfif": JPEG,
    ".png": PNG, ".apng": PNG,
    ".gif": GIF, ".bmp": BMP, ".dib": BMP,
    ".tif": TIFF, ".tiff": TIFF,
    ".webp": WEBP,
    ".heic": HEIF, ".heif": HEIF, ".hif": HEIF,
    ".avif": AVIF,
    ".psd": PSD, ".ico": ICO,
    ".jp2": JP2, ".j2k": JP2, ".jpf": JP2, ".jpx": JP2,
    ".cr3": CR3, ".raf": RAF, ".rw2": RW2, ".orf": ORF, ".x3f": X3F,
    ".ppm": PNM, ".pgm": PNM, ".pbm": PNM, ".pnm": PNM, ".pfm": PNM,
}


def read_header(path: Path, size: int = HEADER_BYTES) -> bytes:
    try:
        with open(path, "rb") as fh:
            return fh.read(size)
    except OSError:
        return b""


def sniff(path: Path, header: bytes | None = None) -> str:
    """Return a coarse container label for *path*."""
    if header is None:
        header = read_header(path)

    if header:
        label = _from_header(header)
        if label != UNKNOWN:
            return label

    return _EXT_FALLBACK.get(path.suffix.lower(), UNKNOWN)


def _from_header(h: bytes) -> str:
    if h.startswith(b"\xff\xd8\xff"):
        return JPEG
    if h.startswith(b"\x89PNG\r\n\x1a\n"):
        return PNG
    if h.startswith((b"GIF87a", b"GIF89a")):
        return GIF
    if h.startswith(b"BM"):
        return BMP
    if h.startswith(b"8BPS"):
        return PSD
    if h.startswith(b"\x00\x00\x01\x00"):
        return ICO
    if h.startswith(b"\x00\x00\x00\x0cjP  "):
        return JP2
    if h.startswith(b"FUJIFILMCCD-RAW"):
        return RAF
    if h.startswith(b"FOVb"):
        return X3F
    if len(h) > 1 and h[0:1] == b"P" and h[1:2] in b"123456f":
        return PNM
    if h.startswith(b"RIFF") and h[8:12] == b"WEBP":
        return WEBP
    if h[4:8] == b"ftyp":
        return _FTYP_BRANDS.get(h[8:12].lower(), UNKNOWN)

    # Panasonic RW2 and Olympus ORF are TIFF-ish but use their own magic
    # numbers in place of the standard 42, so check them before plain TIFF.
    if h.startswith(b"IIU\x00"):
        return RW2
    if h.startswith((b"IIRO", b"IIRS", b"MMOR")):
        return ORF
    if h.startswith((b"II*\x00", b"MM\x00*", b"II+\x00", b"MM\x00+")):
        return TIFF

    return UNKNOWN
