"""Generate sample files in every writable format and push them through the pipeline."""

import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORK = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / ".testwork"
WORK.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(ROOT))

from PIL import Image, ImageDraw
import pillow_heif

pillow_heif.register_heif_opener()
try:
    pillow_heif.register_avif_opener()
except AttributeError:
    pass

OUT = WORK / "samples"
OUT.mkdir(parents=True, exist_ok=True)


def make(mode="RGB", size=(640, 480)):
    im = Image.new(mode, size, (30, 90, 160) if mode == "RGB" else (30, 90, 160, 128))
    d = ImageDraw.Draw(im)
    for i in range(0, size[0], 40):
        d.line([(i, 0), (i, size[1])], fill=(240, 200, 60), width=3)
    d.ellipse([120, 90, 520, 390], outline=(255, 255, 255), width=6)
    return im


targets = [
    ("sample.jpg", "JPEG", "RGB", {"quality": 92}),
    ("sample.png", "PNG", "RGBA", {}),
    ("sample.bmp", "BMP", "RGB", {}),
    ("sample.gif", "GIF", "GIF_P", {}),
    ("sample.tif", "TIFF", "RGB", {}),
    ("sample.webp", "WEBP", "RGBA", {}),
    ("sample.tga", "TGA", "RGB", {}),
    ("sample.ppm", "PPM", "RGB", {}),
    ("sample.ico", "ICO", "RGBA", {}),
    ("sample.heic", "HEIF", "RGB", {}),
    ("sample.avif", "AVIF", "RGB", {}),
]

created = []
for name, fmt, mode, kwargs in targets:
    path = OUT / name
    try:
        img = make("RGB") if mode == "GIF_P" else make(mode)
        if mode == "GIF_P":
            img = img.convert("P", palette=Image.Palette.ADAPTIVE)
        if fmt == "ICO":
            img = img.resize((256, 256))
        img.save(path, fmt, **kwargs)
        created.append(path)
    except Exception as exc:
        print("  SKIP write {:<14} {}".format(name, exc))

# A JPEG with EXIF orientation 6 (rotate 90) to prove exif_transpose runs.
try:
    im = make("RGB", (600, 400))
    exif = im.getexif()
    exif[274] = 6
    p = OUT / "rotated_exif.jpg"
    im.save(p, "JPEG", exif=exif)
    created.append(p)
except Exception as exc:
    print("  SKIP write rotated_exif.jpg", exc)

# A file whose extension lies: PNG data named .jpg.
liar = OUT / "actually_png.jpg"
make("RGB").save(liar, "PNG")
created.append(liar)

print("\ncreated {} sample files in {}\n".format(len(created), OUT))

from picviewer import sniff
from picviewer.decoders import get_registry

registry = get_registry()
print("heif available:", registry.heif_available)
print("supported extensions:", len(registry.supported_extensions()))
print()

fmt = "{:<20} {:<10} {:<12} {:<14} {}"
print(fmt.format("FILE", "SNIFFED", "DECODER", "RESULT", "TIME"))
print("-" * 78)

failures = 0
for path in sorted(created):
    container = sniff.sniff(path)
    cands = registry.candidates(path)
    decoder = cands[0].name if cands else "-"
    t0 = time.perf_counter()
    try:
        result = registry.load_full(path)
        img = result.image
        outcome = "{}x{} {}".format(img.width(), img.height(), "A" if img.hasAlphaChannel() else "")
    except Exception as exc:
        outcome = "FAIL: {}".format(str(exc)[:40])
        failures += 1
    dt = (time.perf_counter() - t0) * 1000
    print(fmt.format(path.name, container, decoder, outcome.strip(), "{:.0f}ms".format(dt)))

print("\n{} failures out of {}".format(failures, len(created)))

# The liar and the rotated file are the two that prove the interesting bits.
r = registry.load_full(OUT / "rotated_exif.jpg")
print("\nrotated_exif.jpg -> {}x{} (source was 600x400; 400x600 means EXIF applied)".format(
    r.image.width(), r.image.height()))
print("actually_png.jpg sniffed as:", sniff.sniff(liar), "(png means magic bytes beat the extension)")
