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

## Packaging

```
.msys64\usr\bin\bash -lc "bash cpp/packaging/deploy.sh"
.msys64\usr\bin\bash -lc "bash cpp/packaging/trim.sh"
powershell -ExecutionPolicy Bypass -File cpp\packaging\install.ps1 -DesktopShortcut
```

139 MB in 69 files, installed per-user into
`%LOCALAPPDATA%\Programs\PicViewerCpp`. Deliberately a separate install from
the Python build -- different directory, Start Menu name and uninstall key --
so the two sit side by side while they are being compared.

`deploy.sh` walks the dependency tree rather than listing libraries by hand,
and seeds that walk with the *plugins* as well as the executable. Plugins are
loaded dynamically, so nothing in `picviewer.exe`'s import table mentions them:
walking from the executable alone left `libtiff` and `libwebp` behind and the
packaged build silently lost TIFF and WebP, which the log made visible as a
shorter `qt reads:` line than the development build's.

`trim.sh` removes what a viewer cannot need, re-running every decoder after
each removal. That verification is not ceremony. The four codec libraries look
obviously droppable -- 42 MB of AV1 and HEVC *encoders* in a program that only
decodes -- and removing any one of them stops **all** decoding, because
`libheif` imports them at load time and fails to load without them. Only the
networking pieces came out.

So the size is 139 MB against the Python build's 177 MB: a fifth smaller, not
the several-fold saving the bare executable's 0.5 MB suggests. ICU's data table
(32 MB) and those encoders (42 MB) are over half of it, and shrinking either
means rebuilding Qt or libheif from source.

## Measured against the Python build

| | Python | C++ |
|---|---|---|
| Launch to a window | 1433 ms | **698 ms** |
| Real Canon CR2, 19 MB | 2376 ms | **1293 ms** |
| Real HEIC, 12 MP | 981 ms | **352 ms** |
| 101 MP JPEG | 4505 ms | **1886 ms** |
| Installed size | 177 MB | **139 MB** |

One cost rather than a saving: Qt decodes to RGB32 where Pillow gave RGB888, so
the same 101 MP photograph occupies 407 MB here against 305 MB there.

## Verified on real files

The first camera RAW and phone HEIC this project has ever seen, decoded by the
*installed* copy with the toolchain taken off `PATH` entirely -- 17 of 17 files,
every decoder exercised:

```
dispatch IMG_0893.CR2: 20187412 bytes, sniffed tiff, candidates raw(100) qt(60)
preview for IMG_0893.CR2 -> 2560x3840
decoded IMG_0893.CR2 via raw in 1247ms -> 3178x4770
```

A real CR2 opens `49 49 2A 00` -- plain little-endian TIFF, indistinguishable by
magic bytes from a scanned document. That is the ambiguity the scoring
tie-break exists for, and `raw(100) qt(60)` is it working on a real file:
without it the general-purpose decoder wins, succeeds, and shows the embedded
preview as though it were the photograph.

## Not done

The nine functional suites from the Python version are not ported -- drag and
drop routing, keyboard shortcuts, menu and toolbar state, fullscreen chrome and
the unreadable-file handling are implemented but only exercised by hand.

A real RAW has now been decoded, but only one, from one camera. Nikon, Sony and
Fujifilm files remain unseen, and each carries its own compression and colour
filter layout.
