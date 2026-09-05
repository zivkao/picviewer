"""Small pure helpers: playlist ordering, sRGB detection, EXIF-aware sizing."""

import io
from pathlib import Path

import pytest
from PIL import Image, ImageCms

from picviewer.color import _is_srgb, apply_orientation, to_srgb
from picviewer.decoders.pillow_decoder import _oriented_size
from picviewer.window import natural_key

# -- playlist ordering -----------------------------------------------------


def sort_names(names):
    return [p.name for p in sorted((Path(n) for n in names), key=natural_key)]


def test_numbers_sort_numerically_not_as_text():
    assert sort_names(["IMG_10.jpg", "IMG_2.jpg", "IMG_1.jpg"]) == [
        "IMG_1.jpg", "IMG_2.jpg", "IMG_10.jpg"
    ]


def test_ordering_ignores_case():
    assert sort_names(["b.jpg", "A.jpg"]) == ["A.jpg", "b.jpg"]


def test_long_runs_of_digits_do_not_overflow_into_text_ordering():
    assert sort_names(["a9.jpg", "a10.jpg", "a100.jpg", "a99.jpg"]) == [
        "a9.jpg", "a10.jpg", "a99.jpg", "a100.jpg"
    ]


def test_multiple_number_groups_compare_left_to_right():
    assert sort_names(["s1e10.jpg", "s1e2.jpg", "s2e1.jpg"]) == [
        "s1e2.jpg", "s1e10.jpg", "s2e1.jpg"
    ]


def test_names_without_digits_still_sort():
    assert sort_names(["cat.jpg", "apple.jpg", "bee.jpg"]) == [
        "apple.jpg", "bee.jpg", "cat.jpg"
    ]


def test_leading_zeros_do_not_create_a_separate_ordering():
    assert sort_names(["a007.jpg", "a7.jpg", "a8.jpg"])[-1] == "a8.jpg"


# -- sRGB detection --------------------------------------------------------

COLOR_DIR = Path(r"C:\Windows\System32\spool\drivers\color")


def load_profile(filename: str):
    path = COLOR_DIR / filename
    if not path.exists():
        pytest.skip("system profile not present: " + filename)
    return ImageCms.ImageCmsProfile(io.BytesIO(path.read_bytes()))


def test_the_windows_srgb_profile_is_recognised():
    """It is described as 'sRGB IEC61966-2.1', not plain 'sRGB'.

    Matching only the exact name missed it, so every sRGB JPEG -- most of them --
    paid for a full-image colour transform that changed nothing.
    """
    assert _is_srgb(load_profile("sRGB Color Space Profile.icm"))


@pytest.mark.parametrize("filename", ["AdobeRGB1998.icc", "ProPhoto.icm", "WideGamutRGB.icc"])
def test_wide_gamut_profiles_are_not_mistaken_for_srgb(filename):
    assert not _is_srgb(load_profile(filename))


def test_an_image_with_no_profile_is_returned_untouched():
    image = Image.new("RGB", (4, 4), (10, 20, 30))
    assert to_srgb(image) is image


def test_a_corrupt_profile_does_not_lose_the_image():
    image = Image.new("RGB", (4, 4), (10, 20, 30))
    image.info["icc_profile"] = b"this is not a profile"
    assert to_srgb(image).size == (4, 4)


# -- EXIF orientation ------------------------------------------------------


def tagged(orientation: int, size=(60, 40)) -> Image.Image:
    image = Image.new("RGB", size, (128, 64, 32))
    exif = image.getexif()
    exif[0x0112] = orientation
    return Image.open(io.BytesIO(_encode(image, exif)))


def _encode(image: Image.Image, exif) -> bytes:
    buffer = io.BytesIO()
    image.save(buffer, "JPEG", exif=exif)
    return buffer.getvalue()


@pytest.mark.parametrize("orientation", [5, 6, 7, 8])
def test_transposing_orientations_swap_the_reported_size(orientation):
    assert _oriented_size(tagged(orientation)) == (40, 60)


@pytest.mark.parametrize("orientation", [1, 2, 3, 4])
def test_non_transposing_orientations_keep_the_size(orientation):
    assert _oriented_size(tagged(orientation)) == (60, 40)


def test_an_image_with_no_exif_reports_its_plain_size():
    assert _oriented_size(Image.new("RGB", (60, 40))) == (60, 40)


@pytest.mark.parametrize("orientation", [5, 6, 7, 8])
def test_the_pixels_are_actually_rotated_to_match(orientation):
    """The reported size must not disagree with what apply_orientation produces."""
    image = tagged(orientation)
    reported = _oriented_size(image)
    assert apply_orientation(image).size == reported
