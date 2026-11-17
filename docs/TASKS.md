# Tasks

Status of the viewer. The Python build it began as is archived at the tag
`python-final`. [progress.md](../progress.md) is the one-page summary of
this list, in Traditional Chinese; update both together.

Last reviewed: 2026-11-17

---

## Done

### Core
- [x] Decoder registry with confidence ranking and fall-through
- [x] Qt image plugins for the mainstream formats
- [x] libheif for HEIC and AVIF, used directly rather than through a wrapper
- [x] LibRaw for camera RAW, with the two-stage embedded-preview path, and a
      half-size stage between them when the embedded preview is small (Sony's
      1616x1080 bridged in 0.3 s, Panasonic's 1920x1440 in 0.5 s)
- [x] Container sniffing by magic bytes, extension only as a fallback
- [x] EXIF orientation and ICC to sRGB
- [x] Byte-budgeted LRU cache with failure memory
- [x] Asynchronous decoding, generation-stamped results, neighbour prefetch

### Interface
- [x] Zoom, pan, fit, actual size; nearest-neighbour past 400% and at 1:1
- [x] Zoom measured in screen pixels, so 1:1 is 1:1 on scaled displays too
- [x] Checkerboard behind images with alpha
- [x] Drag and drop: single file, several files, folders, with hover feedback
- [x] Menu bar, toolbar and shortcuts over one set of actions
- [x] Actions grey out when they cannot act
- [x] Stepping stops at the first and last image rather than wrapping round;
      Previous, Next, First and Last grey out at the end they cannot pass,
      and prefetch does not reach round the corner. The slideshow still loops
- [x] Close (Ctrl+W, Ctrl+F4): back to the empty window; the folder, the
      decoded images in memory and any queued decodes go with it, and a
      decode already running is not put on screen when it lands
- [x] Fullscreen hides the chrome; Escape leaves it rather than quitting,
      and a window maximized before goes back to maximized
- [x] Slideshow (F5): fullscreen, loops, each image's interval counted from
      when it lands so a slow RAW is not skipped; Escape, F5 or F11 ends it
- [x] Animated GIF and WebP play, frame by frame through QMovie rather than
      all decoded up front; P pauses; zoom and any turn hold across frames
- [x] Copy to clipboard, reveal in Explorer, delete to the Recycle Bin
- [x] Rotate and flip for viewing (R / L / H / V); the file is never
      rewritten, and copy hands over the image as shown
- [x] Save a turn into a JPEG (Ctrl+S, asks first): the EXIF orientation tag
      only, never the pixels; patched in place where it exists, otherwise a
      checked rewrite swapped in whole. Not offered for RAW, HEIC or PNG
- [x] Info panel (I): camera, lens, focal length, aperture, shutter, ISO,
      exposure bias, date taken, GPS position as a map link, and the file's
      own details. RAW via LibRaw; JPEG, PNG, WebP, TIFF and HEIC/AVIF EXIF
      via libexif, a TIFF's directories condensed first
- [x] Failures drawn on the canvas rather than raised as dialogs
- [x] Thumbnail strip (T): decoded only as rows come into sight, on two
      threads of its own, kept on disk keyed by path, time and size, and
      pruned to 200 MB least recently seen first
- [x] Settings (Ctrl+,): memory cache, how many images to decode ahead,
      slideshow interval; window size, position and open panels remembered.
      An INI file beside the logs, clamped on load since anyone can edit it
- [x] One window per session: a later launch hands its files to the first
      and exits, and the first comes forward, restored if minimised. A
      selection opened at once is gathered into one playlist

### Diagnostics
- [x] Rotating file log, written even with no console
- [x] Environment recorded at startup: OS, Qt, LibRaw, libheif, screens, DPI
- [x] Crash handler via an unhandled exception filter
- [x] Failures log the file size, what it was taken for, and each decoder's reason
- [x] Cache eviction logged, so memory pressure leaves a trace

### Verification
- [x] 210 unit tests over the pure logic; those reading the real camera
      files skip where those untracked files are absent, as on CI
- [x] 158 functional tests driving a real window offscreen, run at 1x, 1.5x
      and 2x display scaling
- [x] Mutation harness covering 98 reintroduced defects
- [x] CI: GitHub Actions builds from nothing and runs both layers at three
      display scales on every push
- [x] `tools/benchmark.ps1` with a committed baseline
- [x] `tools/verify_package.ps1` — the installed copy, with no toolchain on PATH

### Packaging
- [x] `deploy.sh` walks the dependency tree including plugins
- [x] `trim.sh` removes only what verifies clean afterwards
- [x] Per-user installer, clean uninstall; settings, logs and thumbnails
      kept in `%LOCALAPPDATA%\PicViewer`, and the uninstaller says so
- [x] Registered the way Windows offers it: first under Open with for all 24
      formats, and a page in Settings > Default apps; `assoc_probe` asks the
      shell, since the keys alone once passed while it was buried
- [x] Installed as "Pic Viewer", no longer "(C++)"; the old shortcuts are
      replaced
- [x] One-file installer, `PicViewerCpp-<version>-setup.exe`, 35 MB (NSIS);
      it copies the files and runs `install.ps1 -Register` for the rest, so
      the two cannot drift; checked end to end by `tools/verify_installer.ps1`
- [x] Verified on real RAW from eight makers -- Canon CR2, Nikon NEF
      (lossless compressed), Sony ARW (compressed), Fujifilm RAF (X-Trans),
      Olympus ORF, Panasonic RW2 (a 2024 GH7), Pentax PEF, Leica DNG -- and a
      real phone HEIC; `everyRealRawDecodes` reads each one present
- [x] Toolchain on UCRT64, MSYS2's current environment, with MINGW64
      removed from `.msys64` so the two cannot mix
- [x] The `.exe` carries the app icon and a version block (`picviewer.rc.in`);
      shortcuts and Settings > Apps showed Windows' generic icon before

---

## Outstanding

### Defects

None known. Every defect found so far, with its cause and what now guards
it, is in DEVELOPMENT.md, "Defects, and what each one taught"; CHANGELOG.md
says when each was fixed. A defect listed here is checked against the code
before it is worked on: two once listed had in fact been fixed already.


### Gaps in what is verified
- [ ] **Eight makers, one file each.** Canon, Nikon, Sony, Fujifilm,
      Olympus, Panasonic, Pentax and Leica all decode, but one body and one
      setting per maker; older bodies with other compression are unseen, as
      is Canon's CR3.
- [ ] **One machine.** Display scaling is now tested at 1x, 1.5x and 2x via
      `QT_SCALE_FACTOR`, but on one Windows 11 install and one GPU.
- [ ] **Moving between screens with different scaling is untested.** The
      canvas handles `DevicePixelRatioChange` by recomputing the transform so
      the chosen zoom survives, but no test changes the ratio of a running
      window -- that needs two real displays at different scaling.

### Performance
- [ ] **Fujifilm X-Trans takes 5.5 s to demosaic**, about four times a Bayer
      RAW of similar size (1.3-1.5 s for the CR2, NEF and ARW). X-Trans uses a
      6x6 colour filter rather than 2x2, and LibRaw's default quality runs the
      3-pass Markesteijn algorithm; the 1-pass variant should be roughly three
      times faster at a small cost in fine detail. The embedded preview shows
      meanwhile, so the screen is never empty.

### Other platforms
- [ ] **macOS.** Not started. It would be 1.1.0, a feature: the Windows
      build carries on unchanged. Qt, LibRaw, libheif, libexif and lcms2 are
      all in Homebrew, and the decoders, cache, canvas, panels and settings
      are portable already. What is not:
      - **Opening from Finder.** Finder does not start a second process for
        an image double-clicked while the viewer runs; it sends the running
        one a `QFileOpenEvent`, and the first launch gets one too. The named
        mutex and `WM_COPYDATA` in `instance.cpp` are Windows-only, and with
        nothing handling that event a double-click in Finder opens nothing.
        This is the one that matters.
      - `applog.cpp` includes `windows.h` and installs its crash handler
        with `SetUnhandledExceptionFilter`, unguarded, so it does not
        compile elsewhere. A signal handler instead.
      - `raw_decoder.cpp` opens through LibRaw's `wchar_t` overload, which
        exists only on Windows. On macOS the UTF-8 path is the right one.
      - Show in Explorer runs `explorer`; Finder's equivalent is `open -R`.
        The wording too: Explorer, Recycle Bin, and About's "beside an
        installed picviewer.exe".
      - Settings, logs and thumbnails would land in
        `~/Library/Preferences/PicViewer`, which works, where macOS keeps
        logs in `~/Library/Logs` and caches in `~/Library/Caches`. A choice
        to make, not a defect.
      - **Build.** `project()` names the RC language and `picviewer.rc.in`
        is Windows' resource format. macOS wants an app bundle, an
        `Info.plist` declaring the 24 formats so Finder lists it under Open
        With, and an `.icns` icon.
      - **Packaging and tools.** None of `deploy.sh`, `trim.sh`,
        `notices.sh` (which reads pacman's records), the NSIS installer or
        the PowerShell scripts carries over: `macdeployqt`, a `.dmg`, and
        notices built from Homebrew's records instead.
      - **Tests.** Some were written against Windows -- the `.exe`'s
        resources, at least; which others need changing shows only by
        running them there. CI gains a macOS job beside the Windows one.
      - **Signing.** For its author's own use, the ad-hoc signature Apple
        Silicon builds get anyway, and right-click > Open the first time.
        For anyone else, a paid Apple Developer account and notarisation.

      Before starting: it needs a real Mac. CI can show it builds and the
      suites pass; Finder, Open With and a Retina display can be checked
      only on one. And Preview already opens HEIC and most camera RAW, so
      the case for the port is what Pic Viewer does that Preview does not.

### Packaging
- [ ] 142 MB, of which 42 MB is AV1 and HEVC **encoders** that a viewer never
      calls. They cannot simply be deleted — libheif imports them at load time
      and will not load without them — so this needs libheif rebuilt without
      them. Decided against on 2026-11-02: a library to rebuild by hand on
      every update, and a slower CI, for 42 MB.
- [ ] ICU's data table is another 32 MB. Qt can be built against its own
      Unicode tables instead, which means rebuilding Qt.
- [ ] Unsigned, so SmartScreen will warn on another machine. Signing needs a
      code-signing certificate, which is bought, not built.

### Only if the installer is ever published
The viewer is built for its author's own use and no binary is distributed:
no GitHub Release, no CI artifact but logs. The licences of what the
package carries bind only someone who distributes it, so none of this is
needed now. Before publishing an installer, though:
- [ ] **The three GPL libraries.** x265 (22 MB) and x264 (2 MB) are encoders
      libheif links but a viewer never calls -- libheif takes 2 functions
      from one and 10 from the other, all for encoding -- and JBIG-KIT (60 KB)
      reads a fax compression inside TIFF. Shipped as they are, the whole
      installer goes out under the GPL, with the source of everything in it.
      Stub DLLs of the same names and exports, built here, would remove them
      without rebuilding libheif: 24 MB smaller, nothing GPL, and JBIG-in-TIFF
      no longer opening.
- [ ] **Sources of the LGPL libraries** (Qt, LibRaw, libheif, libde265,
      GLib, libexif and the rest): the MSYS2 source packages of the exact
      versions, published beside the installer. `THIRD-PARTY-NOTICES.txt`
      already names each package and version.
- [ ] **HEVC is patented.** Not a licence matter: libde265 decodes a format
      whose patents belong to others, as every HEVC decoder does. Free and
      open-source viewers ship one regardless; selling the viewer would need
      it dealt with.
- [ ] Signing, above.

---

## Decisions taken, so they are not relitigated

- **Qt6 over MSVC/vcpkg.** The isolated MSYS2 root gives prebuilt Qt, LibRaw
  and libheif without compiling Qt from source for hours.
- **One folder, not one file.** A single-file build unpacks itself to a temp
  directory on every launch, which is seconds of delay each time you
  double-click an image.
- **Both builds installed side by side, until 2026-11-15.** Separate
  directories, Start Menu entries and uninstall keys, so they could be
  compared. Not the "Open with" key, though: registry keys ignore case, so
  `Applications\PicViewer.exe` and `Applications\picviewer.exe` were one key,
  and either uninstaller removed it for both. The Python build was
  uninstalled from the development machine on 2026-11-15, by hand for that
  reason, and the C++ build took the plain name "Pic Viewer".
- **Functional tests assert rather than print.** The Python versions printed
  and left a runner to grep; almost everything turned out to be pinnable.
- **Personal photographs stay untracked.** `image_example/` holds real camera
  files and the repository is public.
- **MIT for this project's own code, 2026-11-16.** The repository was public
  with no licence, which reserves every right: anyone could read it and
  nobody could use it. MIT is the shortest licence that permits use, keeps
  the copyright with its author and disclaims warranty, and it is compatible
  with every licence among the libraries the package carries, GPL included.
  `LICENSE` is the one place the copyright line is written; the `.exe` and
  the installer read it from there.
- **Semantic Versioning, from 1.0.0 on 2026-11-16.** MAJOR for a change of
  architecture or one that does not carry on from the last version, MINOR
  for a feature, PATCH for a fix or a library update. The release steps are
  in DEVELOPMENT.md, "Versions and releases".
- **The Python build archived, 2026-11-16.** Frozen since the port overtook
  it; its tree is the tag `python-final`. Two of its scripts stayed, because
  committed files come from them -- see DEVELOPMENT.md, "Committed test
  images".
