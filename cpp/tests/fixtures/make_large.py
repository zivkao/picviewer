"""Write large_9000x9000.png: small on disk, over 256 MiB once decoded.

    python cpp/tests/fixtures/make_large.py

Qt refuses to decode any image whose pixels exceed its allocation limit, 256 MiB
by default, and this project raises that limit so ordinary large photographs
open. The test guarding that needs an image past the default -- and it has to
be a committed fixture, because the first test written for it depended on a
personal photograph that was later removed and it quietly became a skip.

A 9000x9000 RGB PNG decodes to RGB32 at 9000 * 9000 * 4 = 309 MiB. Filled with
one colour, every row is identical to the one before it, so deflate reduces
243 MB of scanlines to a few hundred kilobytes.

Written by hand with zlib rather than through an imaging library, because
building the image in memory first would cost the 309 MiB this avoids.
"""

import pathlib
import struct
import sys
import zlib

WIDTH = HEIGHT = 9000
COLOUR = bytes((40, 90, 160))

OUT = (pathlib.Path(sys.argv[1]) if len(sys.argv) > 1
       else pathlib.Path(__file__).with_name("large_9000x9000.png"))


def chunk(tag: bytes, data: bytes) -> bytes:
    body = tag + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))


def main() -> None:
    # 8-bit truecolour, no interlace.
    header = struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 2, 0, 0, 0)

    row = b"\x00" + COLOUR * WIDTH          # filter type 0, then the pixels
    compressor = zlib.compressobj(9)
    data = bytearray()
    for _ in range(HEIGHT):
        data += compressor.compress(row)
    data += compressor.flush()

    OUT.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", header)
        + chunk(b"IDAT", bytes(data))
        + chunk(b"IEND", b"")
    )
    decoded_mib = WIDTH * HEIGHT * 4 / 1024 / 1024
    print(f"wrote {OUT.name}: {OUT.stat().st_size / 1024:.0f} KiB on disk, "
          f"{decoded_mib:.0f} MiB decoded")


if __name__ == "__main__":
    main()
