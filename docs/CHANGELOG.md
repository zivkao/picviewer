# Changelog

What was done, when, and what it turned up. Newest first.

`TASKS.md` says what is left; this says what has been done. Commit hashes link
each entry to the full reasoning in its commit message.

---

## 2026-11-02 — UCRT64, and four more makers

- **The toolchain moved from MSYS2's MINGW64 environment to UCRT64.** MINGW64
  is being retired: its repository had shrunk to 1,648 packages against
  UCRT64's 3,945, and the CI action flagged it deprecated. `.msys64` now holds
  UCRT64 only; scripts, `env.cmd`, CI and the docs all point there.
- The first UCRT64 build failed with `at_quick_exit` and `timespec_get` not
  declared. With both environments installed, Qt's CMake config found Vulkan
  headers under `mingw64/include`, and that directory's C headers -- written
  for the older runtime -- were then picked up ahead of UCRT64's. Removing the
  102 MINGW64 packages fixed it; nothing can mix the two now.
- Versions: gcc 16.2, Qt 6.12, LibRaw 0.22.3, libheif 1.23.7, libexif 0.6.26.
  All tests pass unchanged.
- Four more CC0 samples from raw.pixls.us, each checked against the site's
  SHA-256: Olympus E-M10 II ORF, Panasonic DC-GH7 RW2 (a 2024 body), Pentax
  K-r PEF, Leica M10-R DNG. All decode. `everyRealRawDecodes` now reads every
  camera file in `image_example/`, eight makers, requiring the preview, the
  RAW decoder, sensor-sized output and the camera's metadata.

## 2026-11-01 — paths outside the code page

- **Found while reading the decoders for the info panel, then confirmed on
  the installed build** by opening copies of the samples under other names.
  On this machine (code page 950, Big5):

  | Path | RAW (DNG) | HEIC |
  |---|---|---|
  | ASCII | decoded | decoded |
  | Traditional Chinese name | decoded | **failed** |
  | ASCII name in a folder called 照片 | decoded | **failed** |
  | Simplified Chinese or emoji name | **200x150 thumbnail, via Qt** | **failed** |

- Cause: both libraries were given `path.toLocal8Bit()`. libheif reads a
  `char*` path as UTF-8, so every non-ASCII character broke it. LibRaw's
  `char*` open goes through the ANSI code page, so only characters outside it
  did -- and then the Qt decoder, next in line, opened the same file and
  returned the thumbnail from its first TIFF directory, with nothing on
  screen to say so.
- Fix: LibRaw is opened through its UTF-16 overload; libheif is given the
  file's bytes, mapped through QFile, and never sees a path.
- `UnicodePathTests`: nine names in two formats, each required to decode
  through the right decoder at full size, and a RAW preview under an emoji
  name. All six that mattered failed before the fix, in exactly the pattern
  above. Two mutations put the old open calls back.
- Every sample, fixture and real file in the project had an ASCII name, which
  is why nothing caught this before.

## 2026-11-01 — info panel

- `I` opens a side panel with what the camera recorded -- body, lens, focal
  length (with the 35 mm equivalent when it differs), aperture, shutter, ISO,
  exposure bias, date taken -- and the file's own details: name, dimensions,
  size, format, decoder, colour profile, modified date. On the View menu and
  the toolbar; closed by default; hidden in fullscreen and put back after.
- RAW metadata from LibRaw, which the RAW decoder already has open. JPEG and
  HEIC/AVIF EXIF through libexif, newly added to the toolchain and to CI.
  exiv2 was the first choice and is no longer built for MINGW64.
- libexif "follows the specification" by default, rewriting and adding
  entries on load; that is turned off. The code first said it invented a
  date for undated photographs. Checked by turning it back on: the fields
  read here come out the same, an undated block included, so the comment now
  says only what is documented and no mutation pretends to guard it.
- Two committed fixtures, `exif_camera.jpg` and `exif_phone.heic`, about 1 KB
  each, from `cpp/tests/fixtures/make_exif.py`; a big-endian EXIF block is
  built by hand in the unit tests, since Pillow writes only little-endian.
  41 new unit tests, 9 new functional; the real CR2 is read where present.
- One new functional test failed at first: `I` pressed straight after
  `show()`, before the offscreen window was active, so the shortcut never
  fired. The test now waits for the window; the panel was fine.

## 2026-11-01 — CI

- `.github/workflows/cpp.yml`: on every push, a clean Windows runner installs
  the toolchain from MSYS2, builds, and runs the unit suite and the functional
  suite at 1x, 1.5x and 2x. About three minutes. The application log is
  uploaded when anything fails.
- **The first run failed, and was right to.** `theLogIsBeingWritten` checked
  only that the log file was not empty. The development machine's log holds
  every earlier session, so it always passed there; on the runner the file
  was empty. Inside `qExec`, QTest installs its own message handler and
  forwards nothing, so no line a test logs reaches the application's log.
  The test now looks for a marker `main()` logs before any suite starts,
  unique to the run. A 26th mutation, which stops the logger flushing each
  line, fails it.
- **Corrected:** the empty file was first put down to Windows not updating a
  file's directory-entry size while it is held open. Reading the file back
  showed the line was simply not there; the QTest handler was the cause.
- The runner flags MINGW64 as deprecated, and MSYS2's MINGW64 repository is
  shrinking: 1,648 packages against UCRT64's 3,945, exiv2 already gone. Moving
  the toolchain to UCRT64 is now in `TASKS.md`.

## 2026-11-01 — rotate and flip

- `R` / `L` turn the picture a quarter right or left, `H` / `V` mirror it.
  On the View menu, and ↺ ↻ on the toolbar. The status bar names the
  orientation ("rotated 90°", "flipped horizontally"), the log records each
  step, and `Ctrl+C` copies the image as shown. The file is never changed.
- The turn is applied to the picture, not to the view. The view's transform
  is the zoom, read as its `m11`, and a quarter turn makes `m11` zero; turning
  the view would have broken the zoom readout, fit, and display scaling at
  once.
- A new image starts upright. The full decode of a RAW replacing its preview
  keeps the turn, since it is the same picture. Zoomed in, the detail under
  the centre stays there through a turn.
- New `OrientationTests` suite (18 tests), reading colours off the rendered
  canvas from a four-quadrant image, at 1x, 1.5x and 2x. Turn-then-flip and
  flip-then-turn have a row each, because they are different pictures.
- One test failed at 2x on first run: a zoomed-in detail drifted 4 image
  pixels through a turn, against a tolerance of 6 pixels summed. That is
  scroll rounding -- one logical pixel is two image pixels at 2x, and centring
  rounds before and after the turn -- so the tolerance now scales with the
  display ratio. Losing the detail is hundreds of pixels.
- Nine mutations added to the harness for the likeliest ways to get this
  wrong -- turning the view instead of the picture, composing in the wrong
  order, clockwise the wrong way, keeping a turn into the next image, and so
  on. All 25 now applied and caught; baseline green before and after.
- `cpp/README.md` no longer claims the functional suites are unported; they
  were ported on 2026-10-24.
- The documented `.msys64\usr\bin\bash -lc "bash cpp/..."` commands failed
  with "No such file or directory" when repackaging. An MSYS2 login shell
  changes to `$HOME` unless `CHERE_INVOKING` is set, and this machine's
  Windows environment defines `HOME` elsewhere. The docs now say to set it,
  and `env.cmd` does.

## 2026-10-25 — display scaling, and RAW from three more makers

- **"100%" was not 1:1 on a scaled display.** A `QGraphicsView` transform is
  in logical pixels; at 200% scaling each image pixel became a 2x2 block of
  screen pixels while the status bar said 100%. Zoom is now measured in screen
  pixels, fit compares against the canvas's physical size, and a move to a
  screen with different scaling recomputes the transform.
- `functional_tests --scale N` runs the whole suite as on a display scaled by
  N. A new high-DPI suite renders a one-pixel checkerboard and reads the
  pixels back: with the fix reverted it reports "20 of 40 neighbouring screen
  pixels alternate". All 56 functional tests pass at 1x, 1.5x and 2x.
- Two of the new tests passed with the fix reverted. They asked
  `scaleFactor()` whether the zoom was 1:1, and its definition had been
  reverted too. Rewritten to measure the transform and ratio directly.
- The mutation harness now runs the functional suite a second time at 2x,
  since a display-scaling defect is a no-op at a ratio of 1. All 16 mutations
  applied and caught; the two scaling ones only by the 2x run.
- The harness then reported a green baseline as broken. It now reads three
  results, and its closing check still read two, leaving "0 0" in one name.
  The results above stand; only the closing check was wrong.
- Downloaded three CC0 samples from raw.pixls.us after confirming the licence
  in the site's own data, each checked against its published SHA-256: Nikon
  Z 6 NEF (lossless compressed), Sony A7 III ARW (compressed), Fujifilm X-T30
  RAF (X-Trans). All decode. NEF and ARW carry TIFF magic bytes like the CR2,
  so the decoder ranking is now confirmed on three makers' real files.
- Fujifilm X-Trans takes 5.5 s to demosaic, about four times a Bayer RAW, and
  Sony's embedded preview is only 1616x1080. Both recorded in `TASKS.md`.
- **Corrected:** X-Trans was first recorded at 7.7 s and a CR2 at 4.3 s. Both
  were measured with the other RAWs in the folder being prefetched alongside,
  competing for the CPU. The benchmark now opens each file from a folder of
  its own; alone they are 5.5 s and 1.3 s.
- The benchmark flagged a JPEG going from 1 ms to 2 ms as "+100%, WORSE". A
  change is now flagged only when it clears both a relative and an absolute
  threshold -- 15% and 5 ms for decodes, 30% and 100 ms for startup.
- The benchmark gave every real RAW the same id, so four makers' files
  overwrote each other and only the last survived. One id per format now.

## 2026-10-24 — C++ functional tests, development docs

- Ported the GUI-level suites to C++: drop routing, drag and drop, navigation,
  shortcuts, window chrome, unreadable-file handling. 48 tests driving a real
  `MainWindow` offscreen. Unlike the Python originals they assert rather than
  print.
- Restructured the build so `picviewer_core` includes the GUI layer, and the
  application and both test binaries link one library. The tests drive the real
  window, not a copy.
- Extended the mutation harness to both test layers, 14 reintroduced defects.
  Its first run caught three defects *in the tests themselves*:
  - the allocation-limit test checked a constant, not that the call used it;
  - the stale-result test passed with the check deleted, by luck of file order;
  - three mutations never applied, because a multi-line anchor cannot match
    inside bash double quotes — and the harness treated that as neutral.
  All three fixed; SKIP now fails the run. Final run: 14 of 14 applied and
  caught — the decoding and cache defects by the unit suite, the window and
  canvas defects only by the functional one.
- Added a committed fixture, a 9000x9000 PNG that is 249 KiB on disk and
  309 MiB decoded, replacing a test that depended on a personal photograph.
- Wrote `docs/DEVELOPMENT.md`, `docs/TASKS.md` and this file.
- Nearly committed a deliberately broken `cache.cpp`: `git add -A` was run
  while the mutation harness had it edited. Caught before commit; the hazard is
  documented.

## 2026-10-24 — measurements kept, a claim corrected — `1d43e04`

- Twenty-five diagnostic scripts were in a session scratch directory. The ones
  that earn their keep moved to `tools/` and `cpp/tools/`.
- `tools/benchmark.ps1` records startup, per-format decode time and installed
  size into `tools/baseline.json`, and reports later runs as deltas.
- **Corrected:** the C++ build does not start twice as fast. An earlier
  measurement compared a cold file cache with a warm one; measured properly
  the two builds start in the same time. The real difference is decoding —
  9x on a small JPEG, 1.7x on a 19 MB CR2.

## 2026-10-24 — C++ packaged; first real camera RAW — `2e68545`

- Packaged: 139 MB, installed per-user, side by side with the Python build.
  Verified by running the installed copy with `PATH` cut back to Windows.
- The packaged build had silently lost TIFF and WebP: plugins are loaded
  dynamically and their libraries were not in the dependency walk.
- 42 MB of AV1/HEVC encoders cannot be removed — `libheif` imports them at
  load time and will not load without them.
- **First real Canon CR2 decoded.** It opens `49 49 2A 00`, plain TIFF, which
  is the ambiguity the decoder ranking was built for — confirmed on a real file.
- A 101 MP JPEG failed: Qt refuses over 256 MiB of pixels by default.

## 2026-10-23 — diagnostics — `8fb5d1d`

- Crash handler for the C++ build, which had none.
- Environment recorded at startup: OS, Qt, LibRaw, libheif, screens, DPI.
- Failures log file size, what the file was taken for, and each decoder's reason.
- `picviewer -v` printed the version and exited, because `addVersionOption()`
  claims `-v`.

## 2026-10-23 — C++ port — `1e51e25`, `a2e563f`

- Isolated MSYS2 toolchain in `.msys64/`, the way `.venv` isolates Python.
- Full port of the decoder architecture, the cache, the loader and the window.
- The unit suite ran, passed and printed nothing: QTest's stdout logger is torn
  down after each `qExec`.
- `.ico` files showed their smallest frame instead of their largest.

## 2026-10-23 — Python build packaged — `93c2319`

- PyInstaller build and per-user installer.
- The first build did not start at all: relative imports in `__main__.py` fail
  when PyInstaller runs it as a top-level script.

## 2026-10-04 — launcher — `74aaeef`

- `run.bat` had never worked from outside the project folder.

## 2026-09-05 — the Python viewer — `a71d07c` … `95b97ee`

- Decoder registry over Pillow, pillow-heif and rawpy; two-stage RAW; ICC and
  EXIF; byte-budgeted cache; asynchronous loading.
- **The defining bug:** TIFF-based RAW files were decoded by Pillow, which
  succeeded by returning the embedded preview. Both decoders scored 100 and a
  tie went to registration order.
- Drag and drop did nothing: the `QGraphicsView` viewport consumed every drop.
- Unit tests added, then proven by reintroducing six defects.
- Application icon drawn.
