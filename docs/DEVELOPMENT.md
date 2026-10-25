# Development

How to work on the C++ viewer, and the reasoning behind the parts that are not
obvious from the code.

The Python build under `picviewer/` still exists and still works, but active
development is on `cpp/`.

---

## Getting a build

The toolchain is isolated the way `.venv` is. `.msys64/` in the project root is
a complete MSYS2 installation — gcc 16.2, CMake, Ninja, Qt6 6.12, LibRaw,
libheif, lcms2. It touches neither the system `PATH` nor the registry nor any
MSYS2 installed elsewhere, and deleting the folder removes it entirely.

```
cpp\env.cmd             a shell with the toolchain on PATH
cpp\env.cmd build       configure and build
cpp\env.cmd test        build and run the unit tests
```

When a build fails in a way that makes no sense, check the toolchain before the
code:

```
.msys64\usr\bin\bash -lc "bash cpp/tools/verify_toolchain.sh"
.msys64\usr\bin\bash -lc "bash cpp/tools/build_probe.sh"
```

The first reports what the isolated root actually provides. The second compiles
one file against Qt, LibRaw, libheif and lcms2 and decodes one of each format,
which separates "the libraries are wrong" from "my build system is wrong"
before a long afternoon of CMake errors.

---

## Testing

Two layers, deliberately different in kind.

```
cpp\build\tests\unit_tests.exe                     100 tests, no display, about a second
cpp\build\tests\functional_tests.exe               56 tests, a real window, offscreen
cpp\build\tests\functional_tests.exe --scale 2     the same, as on a display at 200%
```

**Unit tests** cover pure logic: container sniffing, decoder ranking, the
byte-budgeted cache, and format behaviour against the sample files.

**Functional tests** drive a real `MainWindow` with real events — drag and drop
sent to actual widgets, keystrokes through `QTest::keyClick`, menus and toolbar
state read off the real objects. The Python versions of these printed
observations and left a runner to grep the output; these assert instead,
because almost everything they establish turned out to be pinnable after all.

`--scale` sets `QT_SCALE_FACTOR` before the application starts, so the whole
suite runs as it would on a scaled display, and the first high-DPI test checks
the ratio actually took effect. Run it at 1.5 and 2 as well as 1: display
scaling defects are invisible at a ratio of 1, so a suite run only there
cannot see them at all.

High-DPI assertions measure what reaches the screen — the transform times the
ratio, or the rendered pixels themselves — never `scaleFactor()`. That method
is the code's own account of the zoom, and a test that asks the code whether
it is right will agree with the code when it is wrong.

### Proving the tests are worth having

```
.msys64\usr\bin\bash -lc "bash cpp/tools/mutate.sh"
```

Reintroduces every defect this project has fixed and checks the suites go red
for it. A green suite says nothing until it can fail, and this has twice caught
tests that passed for the wrong reason.

Last run, 2026-10-25 — all 16 applied and caught. The suite runs three times
per mutation: unit, functional at 1x, functional at 2x display scaling.

| Defect put back | Unit | Functional 1x | Functional 2x |
|---|---|---|---|
| Qt decoder wins on a container match alone | 25 | 1 | 1 |
| RAW decoder ranks below Qt on TIFF | 2 | | |
| Sniffing ignores magic bytes | 21 | | |
| RW2 read as plain TIFF | 2 | | |
| ICO takes the first frame | 1 | | |
| Qt allocation limit left at its 256 MiB default | 1 | | |
| Cache evicts by entry count, not bytes | 2 | | |
| Cache forgets failures immediately | 2 | | |
| Drops handled on the window, not the canvas | | 4 | 4 |
| `dragMoveEvent` refuses, so drags stall | | 1 | 1 |
| Failures raised as modal dialogs again | | 2 | 2 |
| Fullscreen leaves the chrome on screen | | 1 | 1 |
| Actions never grey out | | 1 | 1 |
| Stale decodes allowed on screen | | 1 | 1 |
| 1:1 ignores display scaling | | | **7** |
| Fit compares logical size, not physical | | | **1** |

The split is the useful part. Decoding and cache defects fall to the unit
suite in about a second; window and canvas defects only to the functional
suite; and the two display-scaling defects *only* to the run at 2x — at a
ratio of 1 they change nothing, so a suite run only there would pass with both
of them in. Neither layer, nor either scale, covers what the others do.

Add a mutation whenever a defect is fixed. If the suites stay green with the
bug back in, the test that was supposed to guard it does not. A mutation whose
anchor is not found fails the run rather than printing SKIP and carrying on —
"nothing was tested" is not a neutral outcome.

**Do not stage or commit while it is running.** It edits files under `src/` in
place and restores each one after testing it, so for a few seconds at a time
the working tree holds code that is deliberately wrong. `git add -A` during a
run will stage exactly that. Wait for it to print its closing baseline.

---

## Measuring

```
powershell -ExecutionPolicy Bypass -File tools\benchmark.ps1
powershell -ExecutionPolicy Bypass -File tools\benchmark.ps1 -Update
```

Writes `tools/baseline.json` and reports later runs as deltas. Decode times come
from the application's own log rather than wall-clock timing around the process,
so they exclude startup and are the figure that moves when a decoder changes.

**Startup time is cache-sensitive and should not be read closely.** The same
build measures 700 ms or 1400 ms depending on what Windows has paged in. This
project once concluded the C++ build started twice as fast on exactly that
mistake. Treat a startup delta under about 30% as noise; the script itself only
flags a change that clears both a relative and an absolute threshold.

**Decode times are for one file alone.** The benchmark copies each into a
folder of its own before opening it, because the viewer prefetches a file's
neighbours in parallel. Opened among four other RAWs, a CR2 that decodes in
1.3 s alone measured 4.3 s, and X-Trans 7.7 s instead of 5.5 s — figures
that were briefly written down here as fact.

---

## Packaging

```
.msys64\usr\bin\bash -lc "bash cpp/packaging/deploy.sh"
.msys64\usr\bin\bash -lc "bash cpp/packaging/trim.sh"
powershell -ExecutionPolicy Bypass -File cpp\packaging\install.ps1 -DesktopShortcut
powershell -ExecutionPolicy Bypass -File tools\verify_package.ps1
```

`deploy.sh` gathers the runtime, `trim.sh` removes what a viewer cannot need
while re-verifying after each removal, `install.ps1` installs per-user, and
`verify_package.ps1` runs the *installed* copy with `PATH` cut back to Windows
itself. That last step is not optional: it is what catches a library that was
quietly resolving out of the build toolchain.

---

## Design decisions worth knowing

**Decoders are ranked, not chosen.** Each scores its own confidence for a file
and the registry tries them highest-first, falling through on failure. The
awkward case is TIFF: every TIFF-based RAW carries TIFF magic bytes, and Qt
decodes such a file *successfully* by returning the small embedded preview from
IFD0. Falling through on exceptions cannot help when nothing throws, so the
extension breaks the tie — a TIFF container not named `.tif` goes to the RAW
decoder. A real Canon CR2 opens `49 49 2A 00`, indistinguishable by magic bytes
from a scanned document, so this is not a hypothetical.

**RAW decodes in two stages.** Demosaicing a 19 MB CR2 costs over a second, so
the embedded preview is shown within milliseconds and the full decode replaces
it in place without disturbing zoom or pan.

**The cache budget is in bytes, not entries.** A hundred thumbnails and a
hundred 100-megapixel photographs are the same count and three orders of
magnitude apart in memory.

**Drops are handled on the canvas, not the window.** A `QGraphicsView` enables
`acceptDrops` on its viewport so it can offer drags to graphics items, and marks
the event handled either way. The canvas covers the window, so the cursor is
always over the viewport and a window-level handler never fires.

**Zoom is measured in screen pixels.** 100% means one image pixel per physical
screen pixel, as in any viewer used for inspecting detail. A `QGraphicsView`
transform works in logical pixels, and on a display scaled to 150% each of
those is 1.5 physical ones, so the transform for 1:1 is a scale of `1/ratio`,
not the identity — and fit decides whether an image fits by comparing it with
the canvas's physical size. If the window moves to a screen with different
scaling, the transform is recomputed so the chosen zoom survives.

**Failures are drawn on the canvas, not raised as dialogs.** Stepping through a
folder of unreadable files would otherwise mean dismissing one modal per image.

**Readability is checked before any decoder is blamed.** Otherwise an
unreachable file surfaces as `heif: permission denied`, which points at the
format when the problem is the filesystem.

---

## Defects, and what each one taught

Kept because the cause is usually more useful than the fix, and because several
of these are the kind of mistake that is easy to make twice.

| Defect | Cause | Guarded by |
|---|---|---|
| TIFF-based RAW decoded by the general-purpose backend, silently returning the embedded preview | Both decoders scored 100; a tie went to registration order. Falling through on failure cannot help when the wrong decoder *succeeds* | `tiffBasedRawGoesToTheRawDecoder`, `theAmbiguousCasesNeverTieAtTheTop` |
| A 101 MP JPEG refused to open | Qt declines over 256 MB of pixels by default, below this project's own cap, so the smaller limit silently decided | `qtsAllocationLimitCoversTheLoadersOwnCap` |
| `.ico` shown at 16x16 from a file carrying 256x256 | Qt starts on the first directory entry, which is usually the smallest | `anIcoShowsItsLargestFrameNotItsFirst` |
| TIFF and WebP missing from the packaged build | The dependency walk started at the executable, and plugins are loaded dynamically so they appear in no import table | `tools/verify_package.ps1` |
| Drops did nothing | The `QGraphicsView` viewport consumed them before the window saw them | `theViewportIsWhereADropActuallyLands` |
| `picviewer -v file.jpg` printed the version and exited | `addVersionOption()` claims `-v`, colliding with `-v` for verbose — so asking for a diagnostic log produced no log at all | an explicit `--version` option instead |
| A one-file playlist re-queued the file just decoded on every navigation | `(index + offset) % count` wraps back onto the current file | the current index is excluded from prefetch targets |
| Test suites printed nothing while passing | `QTest` tears down its stdout logger when a `qExec` returns; with several suites in one binary every result went into a closed stream | each suite writes its own report file |
| Three mutations reported "did not compile" and proved nothing | The MSYS2 python is a native Windows build and cannot resolve `/d/...` paths, so the edits never happened | `cygpath` in `mutate.sh` |
| A build failure and two failing tests were indistinguishable | `qExec` returns the number of failures, so exit code 2 meant both | sentinel 255 in `mutate.sh` |
| Verbose logging buried the application's own output | `-v` set the root logger to debug, and Pillow logs a line per plugin it imports | only the project's own categories follow `-v` |
| A regression test quietly became a skip | It depended on a file in the untracked `image_example/` that was later removed | a committed fixture, `cpp/tests/fixtures/large_9000x9000.png` — 249 KiB on disk, 309 MiB decoded |
| The allocation-limit fix could be undone without any test noticing | The replacement check asserted the *constant* was large enough, not that the call used it. A mutation hard-coding `setAllocationLimit(256)` survived | `anImagePastQtsDefaultLimitStillDecodes`, which decodes the fixture |
| The stale-result test passed with the generation check deleted | It navigated quickly and waited, and the last sample in the folder happens to be the slowest to decode — so it landed last whether or not stale results were discarded. The test was passing by luck of file order | `aResultFromAnEarlierGenerationIsDiscarded`, which delivers an old-generation result directly |
| "100%" was not 1:1 on a scaled display | The view's transform is in logical pixels. At 200% scaling each image pixel became a 2x2 block of screen pixels — enlarged and resampled while the status bar said 100% | `HighDpiTests`, which reads the rendered pixels back, and the 2x pass in `mutate.sh` |
| Two of the high-DPI tests passed with the fix reverted | They asked `scaleFactor()` whether the zoom was 1:1, and its definition was reverted along with the fix. The code's idea of 1:1 and its transform agreed with each other, and both disagreed with the screen | assertions on the transform times the ratio, never on `scaleFactor()` |
| A correct 640-pixel width measured as 642 at 1.5x | `mapFromScene` returns an integer polygon whose inclusive bounding rect is a pixel wider, then multiplied by 1.5. A measurement defect, not a rendering one — but it failed a correct build | `transform().mapRect`, which does not round |
| A green baseline reported as broken | The harness was changed to return three results and its closing check still read two; bash put "0 0" into the second name, which is not "0" | the closing check reads all three, the same way the opening one does |
| Three mutations reported SKIP and tested nothing | Their anchors spanned lines, and inside bash double quotes `\n` is a backslash and an n. The harness printed SKIP and carried on as though that were neutral | single-line anchors; SKIP now fails the run like a survivor does |

The pattern worth noticing: more than half were found by *running* the thing and
reading its output, not by reading the code. Several passed every test at the
time.

And a second pattern: a growing share of the rows are defects in the *tests*.
A test that skips, one that checks a constant instead of its use, one that
passes because of file order, two that asked the code under test whether it
was right, a measurement that rounded a correct answer into a failure, a
harness that treats "nothing happened" as neutral — each looks like coverage
and is not. Reverting the fix, or running the mutation harness, exposed every
one of them, which is the argument for doing that rather than trusting a green
suite.
