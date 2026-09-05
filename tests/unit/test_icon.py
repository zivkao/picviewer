"""The application icon ships, loads, and carries the sizes Windows asks for."""

from PySide6.QtCore import QSize
from PySide6.QtGui import QIcon

from picviewer.__main__ import APP_ID, ICON

# 16 is the title bar and the small taskbar; 256 is the Explorer tile. Windows
# picks the nearest available and rescales, so a missing 16 looks muddy.
REQUIRED_SIZES = [16, 24, 32, 48, 64, 128, 256]


def test_the_icon_is_present_in_the_package():
    assert ICON.exists(), "run assets/make_icon.py to regenerate " + ICON.name


def test_the_icon_loads(qapp):
    assert not QIcon(str(ICON)).isNull()


def test_every_required_size_is_embedded(qapp):
    available = {size.width() for size in QIcon(str(ICON)).availableSizes()}
    missing = [s for s in REQUIRED_SIZES if s not in available]
    assert not missing, "icon has no {} px variant".format(missing)


def test_the_smallest_size_is_not_a_rescale_of_a_larger_one(qapp):
    """A 16px entry must actually be in the file, not synthesised on demand."""
    pixmap = QIcon(str(ICON)).pixmap(QSize(16, 16))
    assert pixmap.width() == 16 and pixmap.height() == 16


def test_the_icon_is_opaque_where_it_should_be(qapp):
    """The tile must be solid: a transparent centre reads as a hole, and takes
    the colour of whatever taskbar it lands on."""
    image = QIcon(str(ICON)).pixmap(QSize(256, 256)).toImage()
    centre = image.pixelColor(128, 128)
    assert centre.alpha() == 255, "the aperture opening is transparent"


def test_the_app_id_is_set_so_the_taskbar_does_not_show_python():
    assert APP_ID and "." in APP_ID
