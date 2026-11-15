# Pic Viewer

<img src="assets/picviewer.png" width="96" align="right" alt="">

A desktop image viewer for Windows that opens most of what a camera or a
phone produces: JPEG, PNG, TIFF, WebP, GIF, HEIC and AVIF, and camera RAW
from Canon, Nikon, Sony, Fujifilm, Olympus, Panasonic, Pentax and Leica.
C++ with Qt6, LibRaw and libheif.

## Installing

Build it, then run the installer it produces. The toolchain is an isolated
MSYS2 in `.msys64/`; [cpp/README.md](cpp/README.md) says how to set it up.

```
cpp\env.cmd build
.msys64\usr\bin\bash -lc "bash cpp/packaging/deploy.sh"
.msys64\usr\bin\bash -lc "bash cpp/packaging/trim.sh"
.msys64\usr\bin\bash -lc "bash cpp/packaging/build_installer.sh"
cpp\dist\PicViewerCpp-<version>-setup.exe
```

It installs per-user into `%LOCALAPPDATA%\Programs\PicViewerCpp`, with no
administrator prompt, as "Pic Viewer" in the Start Menu and Settings > Apps.
It is registered for 24 formats, so Settings > Apps > Default apps >
Pic Viewer can make it the default viewer in one place; Windows does not let
a program make itself the default. Uninstall from Settings > Apps. Your
settings, logs and thumbnail cache stay in `%LOCALAPPDATA%\PicViewer`.

Double-clicking an image while the viewer is open opens it in the same
window.

## Keys

| | |
|---|---|
| `Ctrl+O` / `Ctrl+Shift+O` | open a file / a folder |
| `Ctrl+W` | close: back to the empty window |
| `←` `→` `Space` `PgUp` `PgDn` | previous / next; stops at either end |
| `Home` `End` | first / last |
| wheel, `+` `-` | zoom at the cursor |
| `0` / `1` / double-click | fit / actual size / toggle |
| drag | pan |
| `R` `L` | rotate right / left |
| `H` `V` | flip horizontal / vertical |
| `Ctrl+S` | save a turn into a JPEG, losslessly |
| `I` | info panel: camera, lens, exposure, date, position |
| `T` | thumbnail strip |
| `F5` | slideshow, fullscreen; `Esc` ends it |
| `P` | pause / resume an animated GIF or WebP |
| `F11` | fullscreen |
| `Ctrl+E` | show in Explorer |
| `Ctrl+C` | copy the image, the way it is shown |
| `Del` | move to the Recycle Bin |
| `Ctrl+,` | settings |
| `Esc` `Ctrl+Q` | leave fullscreen / quit |

Dropping works too: one file opens it with its folder as the playlist, several
files become the playlist themselves, and a folder opens everything in it.
Everything is also on the menu bar and the toolbar.

Rotating and flipping change only the view, and the next image starts
upright. `Ctrl+S` writes a turn into a JPEG's orientation tag after asking;
the picture itself is not re-compressed, so nothing is lost. RAW, HEIC and
PNG files are never changed.

## Formats

| Decoder | Formats |
|---|---|
| Qt's image plugins | jpg, png, gif, bmp, tiff, webp, ico, icns, tga, ppm/pgm/pbm, xbm, xpm, wbmp |
| libheif | heic, heif, avif |
| LibRaw | cr2, cr3, nef, arw, dng, orf, rw2, raf, pef, srw, x3f, rwl, iiq, 3fr and more |

Files are identified by their first bytes, not their extension. A RAW is
shown at once from the preview it carries, and replaced in place by the full
decode a second or so later. EXIF orientation is applied and embedded colour
profiles are converted to sRGB.

## Logs

`%LOCALAPPDATA%\PicViewer\logs\picviewer-cpp.log`, or Help > Open Log
Folder. It rotates at 1 MB and keeps three old files. Every run starts with
the versions of Qt, LibRaw and libheif and the screens it found, and every
file opened says which decoder took it and how long it took:

```
dispatch IMG_0893.CR2: 20187412 bytes, sniffed tiff, candidates raw(100) qt(60)
preview for IMG_0893.CR2 -> 2560x3840
decoded IMG_0893.CR2 via raw in 1247ms -> 3178x4770
```

`picviewer -v` adds the detail of how each file was dispatched, which is
what a bug report should carry.

## Documentation

- [cpp/README.md](cpp/README.md) -- the toolchain, the tests, packaging and
  what the installer registers.
- [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) -- how to build, test, measure
  and package; the design decisions; every defect found so far and its cause.
- [docs/TASKS.md](docs/TASKS.md) -- what is done, what is outstanding, and the
  decisions taken.
- [docs/CHANGELOG.md](docs/CHANGELOG.md) -- what was done and when.
- [progress.md](progress.md) -- a one-page summary, in Traditional Chinese.

## The Python build

The viewer was first written in Python, with PySide6, Pillow and rawpy. The
C++ build overtook it -- two to four times faster at decoding, a fifth
smaller installed -- and it was frozen, then archived on 2026-11-16. The tag
`python-final` holds it as it last was, with its own README and tests:

```
git checkout python-final
```

## License

MIT; see [LICENSE](LICENSE). That covers this project's own code. The
libraries an installed copy carries -- Qt, LibRaw, libheif and the codecs
beneath them -- keep their own licences: `THIRD-PARTY-NOTICES.txt`, beside
`picviewer.exe`, lists each one with its version, licence and licence text.
