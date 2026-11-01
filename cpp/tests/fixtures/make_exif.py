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
                    which is the path through libheif rather than the file

Pillow writes the TIFF structure inside the EXIF block little-endian. The
big-endian case is covered by a block built in unit_tests.cpp instead.
"""

import pathlib

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
pillow_heif.register_heif_opener()
picture().save(HERE / "exif_phone.heic", quality=90, exif=phone.tobytes())

for name in ("exif_camera.jpg", "exif_phone.heic"):
    print(name, (HERE / name).stat().st_size, "bytes")
