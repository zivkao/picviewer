"""Generate the application icon.

An aperture: unmistakably photographic, and its blade silhouette still reads at
16 pixels, which is the size that decides whether an icon works. Blades sweep
warm to cool, a nod to the colour management underneath.

    python assets/make_icon.py

Writes picviewer.ico (multi-size) and picviewer.png (512px) beside this file.
"""

from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent

# Matches the viewer canvas, so the icon and the app look related.
BACKGROUND = (32, 33, 36)
TILE_RADIUS = 0.22          # corner rounding as a fraction of the tile

BLADES = 6
OUTER = 0.405               # blade tip, as a fraction of the tile
INNER = 0.108               # aperture opening
OVERLAP = 1.34              # blade span in units of one blade's angle
SEAM = 0.011                # blade edge; thicker reads as a spoked wheel

# One hue, lit from the upper left. Six *different* hues read as a colour
# wheel no matter how the blades are shaped; varying only lightness lets the
# geometry say "aperture" instead.
PALETTES = {
    "amber": ((255, 214, 140), (176, 106, 38)),
    "blue": ((168, 208, 255), (54, 96, 168)),
}
PALETTE = "amber"
LIGHT_FROM = -2.3           # radians; where the highlight sits


def blade_colour(index: int, palette: str = None):
    """Lightness around the iris, as if lit from one side."""
    lit, shadow = PALETTES[palette or PALETTE]
    angle = index * 2 * math.pi / BLADES - math.pi / 2
    # 1.0 facing the light, 0.0 away from it.
    t = (math.cos(angle - LIGHT_FROM) + 1) / 2
    t = 0.18 + 0.82 * t
    return tuple(round(shadow[c] + (lit[c] - shadow[c]) * t) for c in range(3))


SEAM_COLOUR = (26, 27, 30)

SIZES = [16, 24, 32, 48, 64, 128, 256]
SUPERSAMPLE = 8             # draw large, downsample once, keep the edges smooth


def blade_polygon(index: int, size: int, arc_steps: int = 28):
    """One aperture blade: a wide outer arc drawn in to a single inner point.

    Real blades overlap and taper to a tip, which is what separates an iris
    from a pie chart. Each blade spans more than its share of the circle and is
    drawn over its neighbour, so the visible edge is the blade's own leading
    edge rather than a radial gap.
    """
    centre = size / 2
    step = 2 * math.pi / BLADES
    span = step * OVERLAP
    start = index * step - math.pi / 2

    outer_r = OUTER * size
    inner_r = INNER * size

    points = [
        (centre + outer_r * math.cos(start + span * i / arc_steps),
         centre + outer_r * math.sin(start + span * i / arc_steps))
        for i in range(arc_steps + 1)
    ]
    # The tip sits at the trailing end, so the blades all sweep the same way.
    points.append((centre + inner_r * math.cos(start + span),
                   centre + inner_r * math.sin(start + span)))
    points.append((centre + inner_r * math.cos(start),
                   centre + inner_r * math.sin(start)))
    return points


def rounded_tile(size: int) -> Image.Image:
    tile = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(tile)
    draw.rounded_rectangle(
        [0, 0, size - 1, size - 1],
        radius=int(TILE_RADIUS * size),
        fill=BACKGROUND + (255,),
    )
    return tile


def render(size: int, tile: bool = True) -> Image.Image:
    """Draw at SUPERSAMPLE resolution, then downsample once."""
    big = size * SUPERSAMPLE
    image = rounded_tile(big) if tile else Image.new("RGBA", (big, big), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)

    seam = max(1, round(SEAM * big))
    polygons = [blade_polygon(i, big) for i in range(BLADES)]

    for index, polygon in enumerate(polygons):
        draw.polygon(polygon, fill=blade_colour(index) + (255,))

    # Stroke afterwards: drawn during the loop, each seam is buried by the next
    # blade and the iris flattens into a pie chart.
    for polygon in polygons:
        draw.line(polygon + [polygon[0]], fill=SEAM_COLOUR + (255,),
                  width=seam, joint="curve")

    return image.resize((size, size), Image.Resampling.LANCZOS)


# The runtime copy lives inside the package so it is found relative to the
# module and travels with it into a PyInstaller build.
RUNTIME_ICON = HERE.parent / "picviewer" / "icon.ico"
DOC_IMAGE = HERE / "picviewer.png"


def main() -> None:
    frames = [render(s) for s in SIZES]
    frames[-1].save(RUNTIME_ICON, format="ICO",
                    sizes=[(s, s) for s in SIZES], append_images=frames[:-1])

    render(512).save(DOC_IMAGE)
    print("wrote {} ({} sizes)".format(RUNTIME_ICON.name, len(SIZES)))
    print("wrote {}".format(DOC_IMAGE.name))


if __name__ == "__main__":
    main()
