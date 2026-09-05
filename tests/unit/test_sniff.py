"""Container detection from magic bytes.

No filesystem: sniff() accepts the header directly, so every case here is a
byte string and an expected label.
"""

from pathlib import Path

import pytest

from picviewer import sniff

# Real leading bytes for each container.
HEADERS = {
    sniff.JPEG: b"\xff\xd8\xff\xe0\x00\x10JFIF\x00\x01",
    sniff.PNG: b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR",
    sniff.GIF: b"GIF89a\x40\x01\xf0\x00\x00\x00\x00\x00",
    sniff.BMP: b"BM\x36\x00\x0c\x00\x00\x00\x00\x00\x36\x00",
    sniff.PSD: b"8BPS\x00\x01\x00\x00\x00\x00\x00\x00\x00\x03",
    sniff.ICO: b"\x00\x00\x01\x00\x01\x00\x20\x20\x00\x00\x01\x00",
    sniff.RAF: b"FUJIFILMCCD-RAW 0201FF12345678",
    sniff.X3F: b"FOVb\x00\x00\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00",
    sniff.RW2: b"IIU\x00\x18\x00\x00\x00\x88\xe7\x74\xd8",
    sniff.ORF: b"IIRO\x08\x00\x00\x00\x00\x00\x00\x00\x00\x00",
    sniff.TIFF: b"II*\x00\x08\x00\x00\x00\x10\x00\x00\x01",
    sniff.WEBP: b"RIFF\x24\x0c\x00\x00WEBPVP8 ",
    sniff.HEIF: b"\x00\x00\x00\x18ftypheic\x00\x00\x00\x00",
    sniff.AVIF: b"\x00\x00\x00\x1cftypavif\x00\x00\x00\x00",
    sniff.CR3: b"\x00\x00\x00\x18ftypcrx \x00\x00\x00\x00",
    sniff.JP2: b"\x00\x00\x00\x0cjP  \r\n\x87\n\x00\x00\x00\x14",
    sniff.PNM: b"P6\n640 480\n255\n\x00\x00\x00\x00\x00",
}


@pytest.mark.parametrize("expected,header", sorted(HEADERS.items()))
def test_header_identifies_container(expected, header):
    assert sniff.sniff(Path("whatever.bin"), header) == expected


def test_magic_bytes_beat_a_lying_extension():
    """A PNG named .jpg is a PNG. Extensions are a hint, not evidence."""
    assert sniff.sniff(Path("photo.jpg"), HEADERS[sniff.PNG]) == sniff.PNG
    assert sniff.sniff(Path("photo.png"), HEADERS[sniff.JPEG]) == sniff.JPEG


@pytest.mark.parametrize(
    "name,expected",
    [
        ("a.jpg", sniff.JPEG), ("a.JPEG", sniff.JPEG), ("a.jfif", sniff.JPEG),
        ("a.png", sniff.PNG), ("a.heic", sniff.HEIF), ("a.HEIF", sniff.HEIF),
        ("a.avif", sniff.AVIF), ("a.cr3", sniff.CR3), ("a.rw2", sniff.RW2),
        ("a.tif", sniff.TIFF), ("a.ppm", sniff.PNM), ("a.psd", sniff.PSD),
    ],
)
def test_extension_is_the_fallback_when_the_header_says_nothing(name, expected):
    assert sniff.sniff(Path(name), b"\x00" * 32) == expected


def test_unknown_when_neither_header_nor_extension_helps():
    assert sniff.sniff(Path("mystery.xyz"), b"\x00" * 32) == sniff.UNKNOWN


def test_empty_header_falls_back_to_the_extension():
    """An unreadable file yields no header; the suffix is all that is left."""
    assert sniff.sniff(Path("photo.heic"), b"") == sniff.HEIF


def test_truncated_header_does_not_raise():
    for size in range(0, 12):
        assert isinstance(sniff.sniff(Path("a.bin"), b"\xff\xd8\xff"[:size]), str)


def test_heif_brands_all_map_to_heif():
    for brand in (b"heic", b"heix", b"heim", b"heis", b"hevc", b"mif1", b"msf1"):
        header = b"\x00\x00\x00\x18ftyp" + brand + b"\x00" * 8
        assert sniff.sniff(Path("x.bin"), header) == sniff.HEIF


def test_avif_brands_are_not_confused_with_heif():
    for brand in (b"avif", b"avis"):
        header = b"\x00\x00\x00\x1cftyp" + brand + b"\x00" * 8
        assert sniff.sniff(Path("x.bin"), header) == sniff.AVIF


def test_rw2_and_orf_are_not_read_as_plain_tiff():
    """Both are TIFF-shaped but use their own magic in place of the 42."""
    assert sniff.sniff(Path("x.bin"), HEADERS[sniff.RW2]) == sniff.RW2
    assert sniff.sniff(Path("x.bin"), HEADERS[sniff.ORF]) == sniff.ORF


def test_big_endian_tiff_is_still_tiff():
    assert sniff.sniff(Path("x.bin"), b"MM\x00*\x00\x00\x00\x08" + b"\x00" * 8) == sniff.TIFF


def test_read_header_on_a_missing_file_returns_empty_not_an_exception():
    assert sniff.read_header(Path("does/not/exist.jpg")) == b""
