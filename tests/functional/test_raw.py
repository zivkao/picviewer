"""Exercise the RAW path: sniffing, decoder ranking, preview, full demosaic."""

import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORK = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / ".testwork"
WORK.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(ROOT))

SAMPLES = WORK / "samples"
dng = SAMPLES / "synthetic.dng"

import rawpy

print("=== raw layer (rawpy/LibRaw) ===")
with rawpy.imread(str(dng)) as raw:
    print("  sizes      :", raw.sizes.width, "x", raw.sizes.height)
    print("  colors     :", raw.num_colors, raw.color_desc)
    try:
        t = raw.extract_thumb()
        print("  thumbnail  :", t.format)
    except Exception as exc:
        print("  thumbnail  : none ->", type(exc).__name__)

from picviewer import sniff
from picviewer.decoders import get_registry

registry = get_registry()

print("\n=== dispatch ===")
print("  sniffed as :", sniff.sniff(dng))
print("  candidates :", [d.name for d in registry.candidates(dng)])

print("\n=== preview stage ===")
t0 = time.perf_counter()
prev = registry.load_preview(dng, (3840, 3840))
dt = (time.perf_counter() - t0) * 1000
if prev is None:
    print("  no preview available")
else:
    print("  {}x{}  is_preview={}  {:.0f}ms".format(
        prev.image.width(), prev.image.height(), prev.is_preview, dt))

print("\n=== full demosaic ===")
t0 = time.perf_counter()
full = registry.load_full(dng)
dt = (time.perf_counter() - t0) * 1000
print("  {}x{}  is_preview={}  {:.0f}ms".format(
    full.image.width(), full.image.height(), full.is_preview, dt))
print("  metadata   :", full.metadata)

print("\n=== half-size path (max_size forces it) ===")
t0 = time.perf_counter()
small = registry.load_full(dng, max_size=(320, 320))
dt = (time.perf_counter() - t0) * 1000
print("  {}x{}  {:.0f}ms  demosaic={}".format(
    small.image.width(), small.image.height(), dt, small.metadata.get("demosaic")))

print("\n=== pixel sanity (centre disc should be near-white) ===")
img = full.image
c = img.pixelColor(img.width() // 2, img.height() // 2)
print("  centre RGB :", c.red(), c.green(), c.blue())
corner = img.pixelColor(5, 5)
print("  corner RGB :", corner.red(), corner.green(), corner.blue())

print("\n=== cache eviction (byte budget) ===")
from picviewer.cache import ImageCache, cache_key
cache = ImageCache(budget=full.nbytes() + 10)   # room for one image only
cache.put(("a",), full)
cache.put(("b",), full)
print("  after 2 puts of {:.1f} MB each into a {:.1f} MB budget:".format(
    full.nbytes() / 1e6, cache.budget / 1e6))
print("  held: {:.1f} MB, 'a' evicted =".format(cache.nbytes / 1e6), cache.get(("a",)) is None)

print("\n=== unsupported file ===")
bogus = SAMPLES / "bogus.xyz"
bogus.write_bytes(b"not an image at all, just some bytes" * 10)
from picviewer.decoders import UnsupportedImage
try:
    registry.load_full(bogus)
    print("  ERROR: should have raised")
except UnsupportedImage as exc:
    print("  raised UnsupportedImage as expected")
except Exception as exc:
    print("  raised", type(exc).__name__, "->", exc)
