"""ICC profile conversion, verified with the real profiles Windows ships."""

import os
import sys
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
ROOT = Path(__file__).resolve().parents[2]
WORK = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / ".testwork"
WORK.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(ROOT))

from PIL import Image
from PySide6.QtWidgets import QApplication
COLOR = Path(r"C:\Windows\System32\spool\drivers\color")
OUT = WORK / "samples"
OUT.mkdir(parents=True, exist_ok=True)
app = QApplication([])
from picviewer.decoders import get_registry
from picviewer.color import _is_srgb
from PIL import ImageCms
import io
r = get_registry()

# In-gamut mid-tones: these differ numerically between spaces without clipping.
SW = [("olive",(120,150,60)),("teal",(60,140,150)),("mauve",(160,110,150)),
      ("grey",(128,128,128)),("skin",(210,170,140))]

for prof in ("AdobeRGB1998.icc","ProPhoto.icm","sRGB Color Space Profile.icm"):
    pp = COLOR / prof
    icc = pp.read_bytes()
    im = Image.new("RGB",(len(SW)*20,20))
    for i,(_,rgb) in enumerate(SW):
        for x in range(i*20,(i+1)*20):
            for y in range(20): im.putpixel((x,y),rgb)
    f = OUT / ("icc2_"+pp.stem.replace(" ","_")+".jpg")
    im.save(f,"JPEG",quality=100,icc_profile=icc)
    src = ImageCms.ImageCmsProfile(io.BytesIO(icc))
    desc = ImageCms.getProfileDescription(src).strip()
    res = r.load_full(f); q = res.image
    print("\n=== {}  [{}] ===".format(prof, desc))
    print("  treated as sRGB (transform skipped):", _is_srgb(src))
    for i,(label,s0) in enumerate(SW):
        c = q.pixelColor(i*20+10,10); got=(c.red(),c.green(),c.blue())
        print("    {:<7} {!s:<16} -> {!s:<16} delta {}".format(
            label, s0, got, max(abs(a-b) for a,b in zip(s0,got))))
