"""Write a minimal but valid DNG so the RAW path can be exercised without a camera.

Layout mirrors a real DNG: IFD0 holds an uncompressed RGB preview and points at
a SubIFD holding the 16-bit RGGB CFA mosaic. That way both the embedded-preview
fast path and the full demosaic path get tested.
"""

import struct
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
WORK = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / ".testwork"

BYTE, ASCII, SHORT, LONG, RATIONAL, SRATIONAL = 1, 2, 3, 4, 5, 10
TYPE_SIZE = {BYTE: 1, ASCII: 1, SHORT: 2, LONG: 4, RATIONAL: 8, SRATIONAL: 8}

W, H = 800, 600
TW, TH = 200, 150


def scene(w, h):
    """A colourful test pattern with gradients, bars and a disc."""
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float64)
    r = 0.15 + 0.8 * (xx / w)
    g = 0.15 + 0.8 * (yy / h)
    b = 0.5 + 0.45 * np.sin(xx / w * 6.28)
    bars = ((xx // 40).astype(int) % 2) == 0
    r = np.where(bars, r, r * 0.35)
    disc = ((xx - w / 2) ** 2 + (yy - h / 2) ** 2) < (min(w, h) * 0.28) ** 2
    for ch in (r, g, b):
        ch[disc] = 0.95
    return np.clip(np.stack([r, g, b], axis=-1), 0, 1)


def bayer_rggb(rgb):
    """Sample an RGB scene down to a single-channel RGGB mosaic."""
    h, w, _ = rgb.shape
    cfa = np.zeros((h, w), dtype=np.float64)
    cfa[0::2, 0::2] = rgb[0::2, 0::2, 0]   # R
    cfa[0::2, 1::2] = rgb[0::2, 1::2, 1]   # G
    cfa[1::2, 0::2] = rgb[1::2, 0::2, 1]   # G
    cfa[1::2, 1::2] = rgb[1::2, 1::2, 2]   # B
    return (cfa * 65535).astype("<u2")


class IFD:
    """Collects tag entries and any values too large to sit inline."""

    def __init__(self):
        self.entries = []          # (tag, type, count, inline_bytes | None, payload)
        self.pending_offsets = {}  # tag -> patch position, filled in later

    def add(self, tag, typ, values):
        if isinstance(values, (bytes, str)):
            payload = values.encode("ascii") + b"\0" if isinstance(values, str) else values
            count = len(payload)
        elif typ in (RATIONAL, SRATIONAL):
            count = len(values)
            fmt = "<" + ("2l" if typ == SRATIONAL else "2L") * count
            payload = struct.pack(fmt, *[n for pair in values for n in pair])
        else:
            count = len(values)
            code = {BYTE: "B", SHORT: "H", LONG: "L"}[typ]
            payload = struct.pack("<" + code * count, *values)
        self.entries.append([tag, typ, count, payload])

    def add_offset_placeholder(self, tag, typ=LONG):
        """A LONG tag whose value is an offset we only learn after layout."""
        self.entries.append([tag, typ, 1, struct.pack("<L", 0)])

    def size(self):
        return 2 + 12 * len(self.entries) + 4

    def serialise(self, ifd_offset, next_ifd=0):
        """Return (ifd_bytes, overflow_bytes, {tag: patch_offset_in_file})."""
        self.entries.sort(key=lambda e: e[0])
        overflow_base = ifd_offset + self.size()
        overflow = b""
        out = struct.pack("<H", len(self.entries))
        patches = {}

        for i, (tag, typ, count, payload) in enumerate(self.entries):
            entry_value_pos = ifd_offset + 2 + 12 * i + 8
            if len(payload) <= 4:
                value_field = payload.ljust(4, b"\0")
            else:
                if len(overflow) % 2:
                    overflow += b"\0"
                value_field = struct.pack("<L", overflow_base + len(overflow))
                overflow += payload
            patches[tag] = entry_value_pos
            out += struct.pack("<HHL", tag, typ, count) + value_field

        out += struct.pack("<L", next_ifd)
        return out, overflow, patches


def build(path: Path):
    rgb = scene(W, H)
    cfa = bayer_rggb(rgb)
    cfa_bytes = cfa.tobytes()

    thumb = (scene(TW, TH) * 255).astype(np.uint8)
    thumb_bytes = thumb.tobytes()

    ifd0 = IFD()
    ifd0.add(254, LONG, [1])                       # NewSubfileType: reduced-res preview
    ifd0.add(256, LONG, [TW])
    ifd0.add(257, LONG, [TH])
    ifd0.add(258, SHORT, [8, 8, 8])                # BitsPerSample
    ifd0.add(259, SHORT, [1])                      # Compression: none
    ifd0.add(262, SHORT, [2])                      # Photometric: RGB
    ifd0.add_offset_placeholder(273)               # StripOffsets
    ifd0.add(277, SHORT, [3])                      # SamplesPerPixel
    ifd0.add(278, LONG, [TH])                      # RowsPerStrip
    ifd0.add(279, LONG, [len(thumb_bytes)])        # StripByteCounts
    ifd0.add(284, SHORT, [1])                      # PlanarConfiguration
    ifd0.add_offset_placeholder(330)               # SubIFDs -> the raw IFD
    ifd0.add(50706, BYTE, [1, 4, 0, 0])            # DNGVersion
    ifd0.add(50707, BYTE, [1, 1, 0, 0])            # DNGBackwardVersion
    ifd0.add(50708, ASCII, "PicViewer Synthetic")  # UniqueCameraModel
    ifd0.add(50721, SRATIONAL, [                   # ColorMatrix1 (sRGB-ish)
        (10000, 10000), (0, 10000), (0, 10000),
        (0, 10000), (10000, 10000), (0, 10000),
        (0, 10000), (0, 10000), (10000, 10000),
    ])
    ifd0.add(50728, RATIONAL, [(1, 1), (1, 1), (1, 1)])   # AsShotNeutral
    ifd0.add(50778, SHORT, [21])                          # CalibrationIlluminant1: D65

    raw_ifd = IFD()
    raw_ifd.add(254, LONG, [0])                    # NewSubfileType: full resolution
    raw_ifd.add(256, LONG, [W])
    raw_ifd.add(257, LONG, [H])
    raw_ifd.add(258, SHORT, [16])
    raw_ifd.add(259, SHORT, [1])
    raw_ifd.add(262, SHORT, [32803])               # Photometric: CFA
    raw_ifd.add_offset_placeholder(273)            # StripOffsets
    raw_ifd.add(277, SHORT, [1])
    raw_ifd.add(278, LONG, [H])
    raw_ifd.add(279, LONG, [len(cfa_bytes)])
    raw_ifd.add(284, SHORT, [1])
    raw_ifd.add(33421, SHORT, [2, 2])              # CFARepeatPatternDim
    raw_ifd.add(33422, BYTE, [0, 1, 1, 2])         # CFAPattern: RGGB
    raw_ifd.add(50714, SHORT, [0])                 # BlackLevel
    raw_ifd.add(50717, LONG, [65535])              # WhiteLevel

    # Lay the file out: header, IFD0 (+overflow), raw IFD (+overflow), pixel data.
    header_size = 8
    ifd0_off = header_size
    ifd0_bytes, ifd0_over, _ = ifd0.serialise(ifd0_off)
    raw_ifd_off = ifd0_off + len(ifd0_bytes) + len(ifd0_over)
    raw_bytes, raw_over, _ = raw_ifd.serialise(raw_ifd_off)

    thumb_off = raw_ifd_off + len(raw_bytes) + len(raw_over)
    cfa_off = thumb_off + len(thumb_bytes)

    # Re-serialise now that the offsets are known.
    ifd0.entries = [
        e if e[0] not in (273, 330)
        else [e[0], e[1], e[2], struct.pack("<L", thumb_off if e[0] == 273 else raw_ifd_off)]
        for e in ifd0.entries
    ]
    raw_ifd.entries = [
        e if e[0] != 273 else [e[0], e[1], e[2], struct.pack("<L", cfa_off)]
        for e in raw_ifd.entries
    ]
    ifd0_bytes, ifd0_over, _ = ifd0.serialise(ifd0_off)
    raw_bytes, raw_over, _ = raw_ifd.serialise(raw_ifd_off)

    blob = (
        struct.pack("<2sHL", b"II", 42, ifd0_off)
        + ifd0_bytes + ifd0_over
        + raw_bytes + raw_over
        + thumb_bytes
        + cfa_bytes
    )
    path.write_bytes(blob)
    return path


if __name__ == "__main__":
    out = WORK / "samples" / "synthetic.dng"
    out.parent.mkdir(parents=True, exist_ok=True)
    build(out)
    print("wrote {} ({:.1f} MB)".format(out, out.stat().st_size / 1e6))
