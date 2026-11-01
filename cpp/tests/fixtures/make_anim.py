"""Write anim.gif and anim.webp: three frames, red, green, blue, looping.

    .venv\\Scripts\\python.exe cpp/tests/fixtures/make_anim.py

The playback tests read the canvas's colour over time, so each frame is one
solid colour and the whole cycle is short: 80 ms a frame, a quarter of a
second around. 48x32, a few hundred bytes each.
"""

import pathlib

from PIL import Image

HERE = pathlib.Path(__file__).parent

frames = [Image.new("RGB", (48, 32), colour) for colour in ("red", "lime", "blue")]

for name, extra in (("anim.gif", {}), ("anim.webp", {"lossless": True})):
    frames[0].save(HERE / name, save_all=True, append_images=frames[1:],
                   duration=80, loop=0, **extra)
    print(name, (HERE / name).stat().st_size, "bytes")
