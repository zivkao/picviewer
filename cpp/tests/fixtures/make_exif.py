"""Write exif_camera.jpg and exif_phone.heic: tiny images carrying camera EXIF.

    .venv\\Scripts\\python.exe cpp/tests/fixtures/make_exif.py

The info panel's tests need files whose EXIF is known exactly, and the real
camera files in image_example/ are untracked -- personal photographs, in a
public repository -- so a CI runner never has them. These are 64x48 and a few
kilobytes, with values chosen so each formatting rule shows:

  exif_camera.jpg   a full-frame body: the focal length *is* its 35 mm
                    equivalent, so none is shown; a +2/3 EV bias; f/5.6
  exif_phone.heic   a phone: 5.1 mm is 26 mm in 35 mm terms; no bias, so the
                    row is left out; HEIC carries EXIF behind a 4-byte offset,
                    which is the path through libheif rather than the file;
                    and a GPS position, Taipei 101, north and east
  exif_png.png      the camera record in a PNG eXIf chunk
  exif_webp.webp    the same in a WebP EXIF chunk
  exif_tiff.tif     the same in a TIFF's own directories, with a position in
                    Rio de Janeiro, south and west, 700 m up. Written by hand
                    below, big-endian, with every directory placed after
                    90 KB of pixels: past the 64 KiB libexif reads, which is
                    the layout libtiff itself writes and the case the
                    viewer's TIFF condensing exists for. (Pillow's TIFF
                    writer also drops the EXIF and GPS directories.)

Pillow writes the TIFF structure inside the other EXIF blocks little-endian;
the hand-written TIFF is the big-endian case.
"""

import pathlib
import struct

from PIL import Image
from PIL.ExifTags import IFD
from PIL.TiffImagePlugin import IFDRational
import pillow_heif

HERE = pathlib.Path(__file__).parent


def exif(make, model, lens, f_number, exposure, iso, focal, focal35, bias, taken):
    data = Image.Exif()
    data[0x010F] = make
    data[0x0110] = model
    sub = data.get_ifd(IFD.Exif)
    sub[0xA434] = lens
    sub[0x829D] = f_number
    sub[0x829A] = exposure
    sub[0x8827] = iso
    sub[0x920A] = focal
    if focal35:
        sub[0xA405] = focal35
    if bias is not None:
        sub[0x9204] = bias
    sub[0x9003] = taken
    return data


def gps(data, latitude, longitude, metres):
    def dms(value):
        value = abs(value)
        d = int(value)
        m = int((value - d) * 60)
        s = round(((value - d) * 60 - m) * 60 * 10000)
        return (IFDRational(d, 1), IFDRational(m, 1), IFDRational(s, 10000))
    g = data.get_ifd(IFD.GPSInfo)
    g[1] = "N" if latitude >= 0 else "S"
    g[2] = dms(latitude)
    g[3] = "E" if longitude >= 0 else "W"
    g[4] = dms(longitude)
    g[5] = 0
    g[6] = IFDRational(metres, 1)
    return data


def picture():
    # Four quadrants, so a viewer that mishandles the file is visibly wrong.
    image = Image.new("RGB", (64, 48))
    for (x, y), colour in {(0, 0): "red", (32, 0): "green",
                           (0, 24): "blue", (32, 24): "yellow"}.items():
        image.paste(colour, (x, y, x + 32, y + 24))
    return image


camera = exif("Canon", "Canon EOS R5", "RF24-105mm F4 L IS USM",
              IFDRational(56, 10), IFDRational(1, 250), 400, IFDRational(70), 70,
              IFDRational(2, 3), "2025:05:01 13:22:10")
picture().save(HERE / "exif_camera.jpg", quality=90, exif=camera)

phone = exif("Apple", "iPhone 13", "iPhone 13 back dual wide camera 5.1mm f/1.6",
             IFDRational(16, 10), IFDRational(1, 60), 125, IFDRational(51, 10), 26,
             None, "2025:06:02 08:05:09")
gps(phone, 25.033964, 121.564472, 12)
pillow_heif.register_heif_opener()
picture().save(HERE / "exif_phone.heic", quality=90, exif=phone.tobytes())

picture().save(HERE / "exif_png.png", exif=camera)
picture().save(HERE / "exif_webp.webp", lossless=True, exif=camera.tobytes())


def tiff_with_exif(path, image, ifd0, exif_ifd, gps_ifd):
    """An uncompressed, big-endian RGB TIFF: pixels first, directories after.

    Each entry is (tag, type, count, value bytes already big-endian). The
    image, EXIF and GPS directories follow the pixel data, then every value
    too long to sit in its entry.
    """
    width, height = image.size
    pixels = image.tobytes()
    def u16(v): return struct.pack(">H", v)
    def u32(v): return struct.pack(">I", v)
    def size(entries): return 2 + 12 * len(entries) + 4

    strip = 8
    base = [
        (256, 4, 1, u32(width)), (257, 4, 1, u32(height)),
        (258, 3, 3, u16(8) + u16(8) + u16(8)), (259, 3, 1, u16(1)),
        (262, 3, 1, u16(2)),
    ] + ifd0 + [
        (273, 4, 1, u32(strip)), (277, 3, 1, u16(3)), (278, 4, 1, u32(height)),
        (279, 4, 1, u32(len(pixels))),
    ]
    first_at = strip + len(pixels)
    first_count = len(base) + 2
    exif_at = first_at + size([None] * first_count)
    gps_at = exif_at + size(exif_ifd)
    overflow_at = gps_at + size(gps_ifd)
    first = sorted(base + [(34665, 4, 1, u32(exif_at)), (34853, 4, 1, u32(gps_at))])

    overflow = bytearray()
    def write(entries):
        out = bytearray(u16(len(entries)))
        for tag, kind, count, value in entries:
            out += u16(tag) + u16(kind) + u32(count)
            if len(value) <= 4:
                out += value + bytes(4 - len(value))
            else:
                out += u32(overflow_at + len(overflow))
                overflow.extend(value)
                if len(overflow) % 2:
                    overflow.append(0)
        return out + u32(0)

    header = bytes([0x4D, 0x4D, 0x00, 0x2A]) + u32(first_at)
    body = write(first) + write(exif_ifd) + write(gps_ifd)
    path.write_bytes(header + pixels + body + overflow)


def ascii(text): return text.encode() + bytes(1)
def rational(num, den): return struct.pack(">II", num, den)
def srational(num, den): return struct.pack(">ii", num, den)
def dms(value):
    value = abs(value)
    d = int(value)
    m = int((value - d) * 60)
    s = round(((value - d) * 60 - m) * 60 * 10000)
    return rational(d, 1) + rational(m, 1) + rational(s, 10000)


# 200x150 RGB is 90,000 bytes of pixels, so every directory starts past 64 KiB.
big = picture().resize((200, 150), Image.NEAREST)
tiff_with_exif(
    HERE / "exif_tiff.tif", big,
    ifd0=[(271, 2, 6, ascii("Canon")), (272, 2, 13, ascii("Canon EOS R5"))],
    exif_ifd=[
        (0x829A, 5, 1, rational(1, 250)), (0x829D, 5, 1, rational(56, 10)),
        (0x8827, 3, 1, struct.pack(">H", 400)), (0x9003, 2, 20, ascii("2025:05:01 13:22:10")),
        (0x9204, 10, 1, srational(2, 3)), (0x920A, 5, 1, rational(70, 1)),
        (0xA405, 3, 1, struct.pack(">H", 70)), (0xA434, 2, 23, ascii("RF24-105mm F4 L IS USM")),
    ],
    gps_ifd=[
        (1, 2, 2, ascii("S")), (2, 5, 3, dms(-22.951916)),
        (3, 2, 2, ascii("W")), (4, 5, 3, dms(-43.210487)),
        (5, 1, 1, bytes(1)), (6, 5, 1, rational(700, 1)),
    ],
)

for name in ("exif_camera.jpg", "exif_phone.heic", "exif_png.png", "exif_webp.webp",
             "exif_tiff.tif"):
    print(name, (HERE / name).stat().st_size, "bytes")
