"""Decoder ranking.

This is where the worst bug so far lived: a TIFF-based RAW scored 100 from both
Pillow and the RAW decoder, the tie was broken by registration order, Pillow won,
and it decoded the file *successfully* -- returning the small embedded preview
from IFD0 instead of the photograph, never raising, so the fall-through never
fired. Pure ranking logic, so it belongs here rather than behind a sample file.
"""

from pathlib import Path

import pytest

from picviewer import sniff
from picviewer.decoders import get_registry
from picviewer.decoders.pillow_decoder import HeifDecoder, PillowDecoder
from picviewer.decoders.raw_decoder import RawDecoder

registry = get_registry()
pillow = PillowDecoder()
raw = RawDecoder()
heif = HeifDecoder()


def top(name: str, container: str) -> str:
    """Name of the decoder that would be tried first."""
    order = registry.candidates(Path(name), container)
    assert order, "no decoder claimed {}{}".format(name, _why_nothing_claimed(container))
    return order[0].name


def _why_nothing_claimed(container: str) -> str:
    """Name the likely cause rather than leaving a bare 'nothing claimed it'.

    A partial install (pip stopping early on Windows long paths, say) leaves
    pillow-heif absent, the HEIF decoder unregistered, and HEIC silently
    unsupported. Saying so beats reporting an empty candidate list.
    """
    if container in (sniff.HEIF, sniff.AVIF) and not registry.heif_available:
        return " -- pillow-heif is not installed, so HEIC/AVIF are unsupported"
    return ""


def test_heif_support_is_actually_installed():
    """pillow-heif is a hard requirement, not an optional extra."""
    assert registry.heif_available, (
        "pillow-heif failed to import; reinstall with "
        "'pip install -r requirements.txt' and check for a partial install"
    )


# Every TIFF-based RAW: same magic bytes as an ordinary TIFF, told apart only by
# the extension. None of these had coverage before.
TIFF_BASED_RAW = [
    "photo.dng", "DSC_0001.nef", "DSC00001.arw", "IMG_0001.cr2",
    "P1000001.rw2", "_DSC0001.pef", "SAM_0001.srw", "IMG_0001.orf",
    "L1000001.rwl", "DSCF0001.raf", "0001.3fr", "0001.iiq",
    "0001.mef", "0001.erf", "0001.kdc", "0001.dcr", "0001.nrw",
    "0001.sr2", "0001.srf", "0001.mos", "0001.fff",
]


@pytest.mark.parametrize("name", TIFF_BASED_RAW)
def test_tiff_based_raw_goes_to_the_raw_decoder(name):
    assert top(name, sniff.TIFF) == "raw"


@pytest.mark.parametrize("name", ["scan.tif", "scan.tiff", "SCAN.TIF"])
def test_a_genuine_tiff_still_goes_to_pillow(name):
    assert top(name, sniff.TIFF) == "pillow"


def test_a_tiff_container_with_an_unknown_extension_prefers_raw():
    """Almost nothing but a RAW ships TIFF magic under a foreign suffix."""
    assert top("mystery.xyz", sniff.TIFF) == "raw"


@pytest.mark.parametrize("name", TIFF_BASED_RAW + ["scan.tif", "mystery.xyz"])
def test_the_ambiguous_cases_never_tie_at_the_top(name):
    """A tie would be resolved by registration order, which is how the bug got in."""
    order = registry.candidates(Path(name), sniff.TIFF)
    assert len(order) >= 2, "expected both pillow and raw as candidates"
    best = order[0].score(Path(name), sniff.TIFF)
    runner_up = order[1].score(Path(name), sniff.TIFF)
    assert best > runner_up, "{} ties at {}".format(name, best)


@pytest.mark.parametrize(
    "name,container,expected",
    [
        ("a.jpg", sniff.JPEG, "pillow"),
        ("a.png", sniff.PNG, "pillow"),
        ("a.gif", sniff.GIF, "pillow"),
        ("a.bmp", sniff.BMP, "pillow"),
        ("a.webp", sniff.WEBP, "pillow"),
        ("a.ico", sniff.ICO, "pillow"),
        ("a.psd", sniff.PSD, "pillow"),
        ("a.heic", sniff.HEIF, "heif"),
        ("a.avif", sniff.AVIF, "heif"),
        ("a.cr3", sniff.CR3, "raw"),
        ("a.rw2", sniff.RW2, "raw"),
        ("a.raf", sniff.RAF, "raw"),
        ("a.orf", sniff.ORF, "raw"),
        ("a.x3f", sniff.X3F, "raw"),
    ],
)
def test_unambiguous_formats_reach_their_decoder(name, container, expected):
    assert top(name, container) == expected


def test_a_lying_extension_does_not_redirect_a_clear_container():
    """PNG bytes named .heic are still PNG bytes, so Pillow takes them."""
    assert top("photo.heic", sniff.PNG) == "pillow"


def test_raw_declines_formats_it_cannot_read():
    for container in (sniff.JPEG, sniff.PNG, sniff.GIF, sniff.WEBP, sniff.HEIF):
        assert raw.score(Path("a.bin"), container) == 0


def test_heif_declines_everything_that_is_not_heif_or_avif():
    for container in (sniff.JPEG, sniff.PNG, sniff.TIFF, sniff.CR3):
        assert heif.score(Path("a.bin"), container) == 0


def test_pillow_offers_itself_last_for_an_unrecognised_file():
    """Pillow reads a long tail we do not enumerate, so let it try rather than refuse."""
    score = pillow.score(Path("mystery.xyz"), sniff.UNKNOWN)
    assert 0 < score < pillow.score(Path("a.jpg"), sniff.JPEG)


def test_nothing_claims_a_file_that_is_neither_known_nor_unknown():
    assert registry.candidates(Path("a.exe"), sniff.CR3) == [
        d for d in registry.candidates(Path("a.exe"), sniff.CR3)
    ]
    assert all(d.name == "raw" for d in registry.candidates(Path("a.exe"), sniff.CR3))


def test_candidates_are_sorted_by_descending_confidence():
    order = registry.candidates(Path("photo.dng"), sniff.TIFF)
    scores = [d.score(Path("photo.dng"), sniff.TIFF) for d in order]
    assert scores == sorted(scores, reverse=True)


def test_case_of_the_extension_does_not_matter():
    assert top("PHOTO.DNG", sniff.TIFF) == top("photo.dng", sniff.TIFF)
    assert top("PHOTO.TIF", sniff.TIFF) == top("photo.tif", sniff.TIFF)
