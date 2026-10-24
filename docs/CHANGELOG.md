# Changelog

What was done, when, and what it turned up. Newest first.

`TASKS.md` says what is left; this says what has been done. Commit hashes link
each entry to the full reasoning in its commit message.

---

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
