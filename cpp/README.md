# Pic Viewer (C++)

A port of the Python viewer to C++ with Qt6, LibRaw and libheif.

## Toolchain

Isolated the way `.venv` is. `.msys64/` in the project root is a complete MSYS2
installation: gcc 16.2, CMake, Ninja, Qt6 6.12, LibRaw, libheif, lcms2. It
touches neither the system `PATH` nor the registry nor any MSYS2 installed
elsewhere, and deleting the folder removes it entirely.

```
cpp\env.cmd             a shell with the toolchain on PATH
cpp\env.cmd build       configure and build
cpp\env.cmd test        build and run the unit tests
```

To recreate it from nothing: download `msys2-base-x86_64-*.tar.xz` from
repo.msys2.org, extract it to `.msys64` in the project root, then

```
.msys64\usr\bin\bash -lc "pacman -Syu --noconfirm"
.msys64\usr\bin\bash -lc "pacman -S --noconfirm --needed \
    mingw-w64-x86_64-{gcc,cmake,ninja,pkgconf,qt6-base,qt6-tools,qt6-imageformats,libraw,libheif,lcms2}"
```

`qt6-imageformats` is not optional: `qt6-base` alone reads only JPEG, GIF and
ICO beyond the formats compiled into QtGui, so TIFF and WebP go missing
without it.

## What differs from the Python version

**Decoders.** Qt's own image plugins stand in for Pillow, libheif is used
directly rather than through a wrapper, and LibRaw replaces rawpy. The
registry, the scoring and the two-stage RAW path are the same design.

**Thinner colour handling.** Qt does natively what Pillow needed help with:
EXIF orientation is `QImageReader::setAutoTransform`, and the ICC conversion is
`QColorSpace` rather than `ImageCms` by hand.

**One behaviour the port had to add.** An `.ico` holds several sizes and Qt
starts on the first directory entry, usually the smallest -- a 16x16 icon out
of a file that also carries 256x256. Pillow picked the largest; the port now
does too, and a test holds it there.

## Tests

```
cpp\env.cmd test
```

98 assertions across four suites: container sniffing, decoder ranking, the
byte-budgeted cache, and format-level behaviour against the real sample files.
All run offscreen, so no display is needed.

Each suite writes its own report file rather than using QTest's default stdout
logger. QTest tears that logger down when a `qExec` returns, so with several
suites in one binary a plain run printed *nothing at all* -- the tests ran,
passed, and said so into a closed stream.

Reintroducing each of seven defects the Python version earned its fixes for
turns the suite red, which is the only thing that makes a green suite worth
anything:

| Defect put back | Caught |
|---|---|
| Qt decoder wins on a container match alone | 25 failures |
| RAW decoder ranks below Qt on TIFF | 2 failures |
| Cache evicts by entry count, not bytes | 2 failures |
| Cache forgets failures immediately | 2 failures |
| Sniffing ignores magic bytes | 21 failures |
| RW2 read as plain TIFF | 2 failures |
| ICO takes the first frame | 1 failure |

## Verified against the samples

Every file in `samples/` opened with the built executable, read back from the
app's own log:

```
qt   : 12 files      heif : 2 files       raw  : 3 files
rotated_exif.jpg  ->  400x600   EXIF applied
sample.ico        ->  256x256   largest frame
actually_png.jpg  ->  decoded as PNG despite the .jpg name
synthetic.dng     ->  preview 200x150 first, then 800x600 at 65ms
errors: none                              15 of 15 decoded
```

## Not done

The nine functional suites from the Python version are not ported -- drag and
drop routing, keyboard shortcuts, menu and toolbar state, fullscreen chrome and
the unreadable-file handling are implemented but only exercised by hand. There
is no packaging or installer for this build yet, and `picviewer.exe` currently
runs only from the toolchain shell, since it needs the Qt DLLs on `PATH`.
