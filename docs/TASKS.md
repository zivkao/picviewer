# Tasks

Status of the C++ viewer. The Python build is feature-frozen; see the root
README for what it is.

Last reviewed: 2026-10-24

---

## Done

### Core
- [x] Decoder registry with confidence ranking and fall-through
- [x] Qt image plugins for the mainstream formats
- [x] libheif for HEIC and AVIF, used directly rather than through a wrapper
- [x] LibRaw for camera RAW, with the two-stage embedded-preview path
- [x] Container sniffing by magic bytes, extension only as a fallback
- [x] EXIF orientation and ICC to sRGB
- [x] Byte-budgeted LRU cache with failure memory
- [x] Asynchronous decoding, generation-stamped results, neighbour prefetch

### Interface
- [x] Zoom, pan, fit, actual size; nearest-neighbour past 400%
- [x] Checkerboard behind images with alpha
- [x] Drag and drop: single file, several files, folders, with hover feedback
- [x] Menu bar, toolbar and shortcuts over one set of actions
- [x] Actions grey out when they cannot act
- [x] Fullscreen hides the chrome; Escape leaves it rather than quitting
- [x] Copy to clipboard, reveal in Explorer, delete to the Recycle Bin
- [x] Failures drawn on the canvas rather than raised as dialogs

### Diagnostics
- [x] Rotating file log, written even with no console
- [x] Environment recorded at startup: OS, Qt, LibRaw, libheif, screens, DPI
- [x] Crash handler via an unhandled exception filter
- [x] Failures log the file size, what it was taken for, and each decoder's reason
- [x] Cache eviction logged, so memory pressure leaves a trace

### Verification
- [x] 100 unit tests over the pure logic, none skipped
- [x] 48 functional tests driving a real window offscreen
- [x] Mutation harness covering 14 reintroduced defects
- [x] `tools/benchmark.ps1` with a committed baseline
- [x] `tools/verify_package.ps1` — the installed copy, with no toolchain on PATH

### Packaging
- [x] `deploy.sh` walks the dependency tree including plugins
- [x] `trim.sh` removes only what verifies clean afterwards
- [x] Per-user installer, side by side with the Python build, clean uninstall
- [x] Verified on a real Canon CR2 and a real phone HEIC

---

## Outstanding

### Defects

None known in the C++ source. Two were listed here on first writing and both
were already fixed — checked rather than assumed, which is the only reason to
keep a list like this at all:

- `-v` colliding with `--version` is fixed in `main.cpp`, and was never a
  problem in the Python build, which uses argparse and no Qt parser.
- A one-file playlist re-queueing itself is fixed in both.

The installed *Python* package predates its prefetch fix, so it still does the
redundant work until it is rebuilt. That is a stale install, not a defect.

### Gaps in what is verified
- [ ] **One camera, one phone.** A Canon CR2 and one HEIC. Nikon, Sony and
      Fujifilm RAW each carry their own compression and colour filter layout,
      and none has been seen.
- [ ] **One machine, one display configuration.** Everything has run on one
      Windows 11 install at 100% scaling.
- [ ] **High-DPI is untested and probably wrong.** Nothing accounts for
      `devicePixelRatio`, so on a scaled display "100%" almost certainly does
      not mean one image pixel per screen pixel. The log records the ratio so
      a report will at least say which display it was.
- [ ] **No CI.** Everything runs on one machine when somebody remembers.

### Features
- [ ] Thumbnail strip. The disk-backed thumbnail cache it needs is the real
      work; the decoder layer already takes a `maxSize` for it.
- [ ] Animated GIF and WebP. Frame 0 is shown.
- [ ] Rotate and flip.
- [ ] Slideshow.
- [ ] EXIF detail panel.
- [ ] Any settings at all. The cache budget, zoom limits and prefetch radius
      are constants in the source.

### Packaging
- [ ] 139 MB, of which 42 MB is AV1 and HEVC **encoders** that a viewer never
      calls. They cannot simply be deleted — libheif imports them at load time
      and will not load without them — so this needs libheif rebuilt without
      them, or a build that loads codecs as plugins.
- [ ] ICU's data table is another 32 MB. Qt can be built against its own
      Unicode tables instead, which means rebuilding Qt.
- [ ] No single-file installer. The current one is a PowerShell script; a
      `.exe` would need Inno Setup or similar.
- [ ] Unsigned, so SmartScreen will warn on another machine.

---

## Decisions taken, so they are not relitigated

- **Qt6 over MSVC/vcpkg.** The isolated MSYS2 root gives prebuilt Qt, LibRaw
  and libheif without compiling Qt from source for hours.
- **One folder, not one file.** A single-file build unpacks itself to a temp
  directory on every launch, which is seconds of delay each time you
  double-click an image.
- **Both builds installed side by side.** Separate directories, Start Menu
  entries and uninstall keys, so they can be compared rather than one
  replacing the other.
- **Functional tests assert rather than print.** The Python versions printed
  and left a runner to grep; almost everything turned out to be pinnable.
- **Personal photographs stay untracked.** `image_example/` holds real camera
  files and the repository is public.
