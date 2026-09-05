"""EXIF orientation and ICC -> sRGB conversion.

Skipping either of these is what makes a home-grown viewer look wrong:
phone photos come out sideways, and Adobe RGB / Display P3 files come out
desaturated because their wide-gamut numbers get painted as sRGB.
"""

from __future__ import annotations

import io
import logging

from PIL import Image, ImageCms, ImageOps

log = logging.getLogger(__name__)

_SRGB = ImageCms.createProfile("sRGB")


def apply_orientation(im: Image.Image) -> Image.Image:
    """Rotate/flip according to the EXIF orientation tag, then drop the tag."""
    try:
        return ImageOps.exif_transpose(im)
    except Exception:  # malformed EXIF should never cost us the image
        log.debug("exif_transpose failed", exc_info=True)
        return im


def to_srgb(im: Image.Image) -> Image.Image:
    """Convert to sRGB if the image carries a different ICC profile."""
    raw_profile = im.info.get("icc_profile")
    if not raw_profile:
        return im

    try:
        src = ImageCms.ImageCmsProfile(io.BytesIO(raw_profile))
        if _is_srgb(src):
            return im
        # profileToProfile needs a mode it can transform; L and P are not it.
        working = im if im.mode in ("RGB", "RGBA", "CMYK") else im.convert("RGB")
        converted = ImageCms.profileToProfile(
            working, src, _SRGB, outputMode="RGBA" if "A" in working.getbands() else "RGB"
        )
        if converted is not None:
            converted.info.pop("icc_profile", None)
            return converted
    except Exception:
        log.debug("ICC conversion failed for embedded profile", exc_info=True)
    return im


def _is_srgb(profile) -> bool:
    """Recognise the many spellings of sRGB so we can skip a no-op transform.

    Most JPEGs on the internet carry an sRGB profile under one of several
    descriptions ("sRGB IEC61966-2.1", "sRGB Color Space Profile", ...).
    Comparing against a single canonical name misses them, and every miss
    costs a full-image colour transform that changes nothing.
    """
    try:
        description = ImageCms.getProfileDescription(profile).strip().lower()
    except Exception:
        return False
    return description.startswith("srgb") or "iec61966-2.1" in description


def normalize(im: Image.Image) -> Image.Image:
    """Everything a decoded PIL image needs before it is shown."""
    return to_srgb(apply_orientation(im))
