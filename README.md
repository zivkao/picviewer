# Pic Viewer

A desktop image viewer that opens most of what a camera or a phone produces.

## Setup

```
py -3.13 -m venv .venv
.venv\Scripts\python.exe -m pip install -r requirements.txt
```

## Run

```
.venv\Scripts\python.exe -m picviewer            # empty, then Ctrl+O or drop a file
.venv\Scripts\python.exe -m picviewer photo.cr2  # opens the file, folder becomes the playlist
.venv\Scripts\python.exe -m picviewer D:\Photos  # opens a folder
```

Or double-click `run.bat`.

## Keys

| | |
|---|---|
| `Ctrl+O` | open |
| `←` `→` `Space` `PgUp` `PgDn` | previous / next |
| `Home` `End` | first / last |
| wheel, `+` `-` | zoom at the cursor |
| `0` / `1` / double-click | fit / actual size / toggle |
| drag | pan |
| `F11` | fullscreen |
| `Del` | move to Recycle Bin |
| `Esc` `Ctrl+Q` | leave fullscreen / quit |

Dropping works too: one file opens it with its folder as the playlist, several
files become the playlist themselves, and a folder opens everything in it.

Everything is also on the menu bar (File / Go / View / Help) and the toolbar, so
nothing is keyboard-only. Buttons grey out when they cannot act. `Ctrl+E` reveals
the current file in Explorer, `Ctrl+C` copies the image to the clipboard.

## Logs

`%LOCALAPPDATA%\PicViewer\logs\picviewer.log` (Help -> Open Log Folder), rotating
at 1 MB with 3 backups. Every open, decode, failure and crash lands there,
including which decoder handled each file:

```
INFO  picviewer.loader  decoded synthetic.dng via raw in 95ms -> 800x600 (1.4 MB)
INFO  picviewer.window  copied actually_png.jpg to clipboard
```

The file handler is not optional comfort: the app runs under `pythonw.exe`, which
has no console, so stderr goes nowhere and an unlogged failure leaves no trace at
all. `-v` adds debug detail and, when run through `python.exe`, console output.

## Tests

```
.venv\Scripts\python.exe testsun_all.py
```

Nine suites, all offscreen (`QT_QPA_PLATFORM=offscreen`), no display needed.
They generate their own fixtures into `.testwork/` -- sample files in every
writable format, a hand-built DNG so the RAW path can be exercised without a
camera, and deny-read files that reproduce an unreadable-file failure. Add `-v`
to see each suite's own output.

The suites lean on printed findings rather than assertions, because most of what
matters here is better read than asserted: which decoder won, how long a decode
took, what the status bar ended up saying.

## Formats

| Decoder | Formats |
|---|---|
| Pillow | jpg, png, bmp, gif, tiff, webp, ico, tga, pnm, jp2, psd, dds, and Pillow's long tail |
| pillow-heif | heic, heif, avif |
| rawpy / LibRaw | cr2, cr3, nef, arw, dng, orf, rw2, raf, pef, srw, x3f, iiq, 3fr, and the rest of LibRaw's list |

## How it works

**Dispatch** (`sniff.py`, `decoders/registry.py`) — files are identified by magic
bytes first and extension second, because extensions lie. Each decoder scores its
own confidence and the registry tries them highest-first, falling through on
failure.

The awkward case is TIFF: every TIFF-based RAW (NEF, ARW, CR2, DNG) has TIFF magic
bytes, and Pillow will decode such a file *successfully* — returning the small
embedded preview from IFD0 rather than the photo. Falling through on exceptions
does not help when nothing throws, so the extension breaks the tie: a TIFF
container not named `.tif` goes to the RAW decoder.

**Two-stage RAW** (`decoders/raw_decoder.py`) — demosaicing costs 1-3 seconds, so
the embedded JPEG preview is shown first (a few ms) and the full decode replaces
it in place, without disturbing zoom or pan.

**Drops** (`viewer.py`) — the canvas handles its own drag-and-drop rather than
leaving it to the window. A `QGraphicsView` switches on `acceptDrops` for its
viewport so it can offer drags to graphics items, and marks the event handled
whether or not anything took it — so a drop landing on the canvas, which is
where the cursor always is, never reaches the window underneath.

**Failures** (`decoders/registry.py`, `cache.py`) — readability is checked before
any decoder is tried, so an unreachable file says so instead of surfacing as
`heif: [Errno 13] Permission denied`, which blames the format for a filesystem
problem. Failures are reported on the canvas rather than in a dialog (a folder of
unreadable files would otherwise mean one modal per image) and are remembered by
cache key, so the prefetcher stops retrying them. The key carries mtime and size,
so a file that is later fixed is picked up on its own.

**Threading** (`loader.py`) — decoding runs on a `QThreadPool`; results carry a
generation number so anything the user has already navigated past is discarded.
Neighbours are prefetched at a lower priority.

**Cache** (`cache.py`) — LRU with a budget in *bytes*, not entries. A hundred
thumbnails and a hundred 100-megapixel TIFFs are the same count and three orders
of magnitude apart in memory.

**Colour** (`color.py`) — EXIF orientation is applied and embedded ICC profiles are
converted to sRGB. Without the first, phone photos are sideways; without the
second, Adobe RGB and Display P3 files look washed out.

## Not built yet

Thumbnail strip (the disk-backed thumbnail cache it needs is the real work),
animated GIF/WebP playback (frame 0 is shown), Windows file associations,
slideshow, EXIF detail panel, rotate/flip, copy to clipboard.
