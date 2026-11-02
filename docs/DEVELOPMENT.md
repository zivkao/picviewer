# Development

How to work on the C++ viewer, and the reasoning behind the parts that are not
obvious from the code.

The Python build under `picviewer/` still exists and still works, but active
development is on `cpp/`.

---

## Getting a build

The toolchain is isolated the way `.venv` is. `.msys64/` in the project root is
a complete MSYS2 installation in its UCRT64 environment — gcc 16.2, CMake,
Ninja, Qt6 6.12, LibRaw, libheif, libexif, lcms2. It touches neither the
system `PATH` nor the registry nor any MSYS2 installed elsewhere, and deleting
the folder removes it entirely. Only UCRT64 is installed in it: with MINGW64
packages present as well, CMake found Vulkan headers under `mingw64/include`
and the build mixed the two C runtimes' headers.

```
cpp\env.cmd             a shell with the toolchain on PATH
cpp\env.cmd build       configure and build
cpp\env.cmd test        build and run the unit tests
```

The `.msys64\usr\bin\bash -lc "bash cpp/..."` lines in this file and in
`cpp/README.md` take paths relative to the project root, and an MSYS2 login
shell changes to `$HOME` first unless `CHERE_INVOKING` is set. Where Windows
itself defines `HOME`, as it does on the development machine, that is nowhere
near the project, and every one of them fails with "No such file or
directory". Set it once per console, from the project root:

```
set CHERE_INVOKING=1                 cmd
$env:CHERE_INVOKING = 1              PowerShell
```

`env.cmd` sets it too. Start these from cmd or PowerShell rather than Git
Bash: launched from another MSYS runtime, MSYS2's bash loses `USERNAME`, and
the failures suite needs it.

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
cpp\build\tests\unit_tests.exe                     205 tests, no display, about a second
cpp\build\tests\functional_tests.exe               124 tests, a real window, offscreen
cpp\build\tests\functional_tests.exe --scale 2     the same, as on a display at 200%
```

**Unit tests** cover pure logic: container sniffing, decoder ranking, the
byte-budgeted cache, and format behaviour against the sample files.

**CI.** `.github/workflows/cpp.yml` runs the unit suite and the functional
suite at 1x, 1.5x and 2x on every push and pull request, on a clean Windows
runner that installs the toolchain from MSYS2 at the time of the run. That is
deliberately not the `.msys64` snapshot: a failure there with a green local
run means either the change depends on something only this machine has, or an
upstream package moved, and the job prints the package versions so the two
can be told apart. The application log is uploaded as an artifact when
anything fails. Its first run found a test that had only ever passed because
of leftovers on the development machine; see the defect table.

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

The orientation tests follow the same rule. Their image has four coloured
quadrants, and each test reads which colour landed top-left and top-right off
the rendered canvas, so a turn the wrong way, or a turn and a flip composed in
the wrong order, shows up as the wrong colour rather than as a matrix the code
agrees with.

### Proving the tests are worth having

```
.msys64\usr\bin\bash -lc "bash cpp/tools/mutate.sh"
```

Reintroduces every defect this project has fixed and checks the suites go red
for it. A green suite says nothing until it can fail, and this has twice caught
tests that passed for the wrong reason.

Last run, 2026-11-02 — all 73 applied and caught, and the baseline green
again after them. The suite runs three times per mutation: unit, functional at
1x, functional at 2x display scaling. The rows below are the harness's own
labels, copied from that run's output.

| Defect put back | Unit | Functional 1x | Functional 2x |
|---|---|---|---|
| Qt decoder: container match alone wins | 33 | 1 | 1 |
| RAW decoder ranks below Qt on TIFF | 2 | | |
| Sniff ignores magic bytes, trusts the suffix | 21 | | |
| RW2 read as plain TIFF | 2 | | |
| ICO takes the first frame, not the largest | 1 | | |
| Qt allocation limit left at its default | 1 | | |
| Cache evicts by entry count, not bytes | 3 | | |
| Cache forgets failures immediately | 2 | | |
| Drops handled on the window, not the canvas | | 4 | 4 |
| `dragMoveEvent` refuses, so drags stall | | 1 | 1 |
| Failures raised as modal dialogs again | | 2 | 2 |
| Fullscreen leaves the chrome on screen | | 1 | 1 |
| Actions never grey out | | 1 | 1 |
| 1:1 ignores display scaling | | | **8** |
| Fit compares logical size, not physical | | | **1** |
| Log lines buffered, not flushed | | 1 | 1 |
| RAW opened by its ANSI path again | 3 | | |
| HEIF opened by its ANSI path again | 3 | | |
| Stale decodes allowed on screen | | 1 | 1 |
| A turn rotates the view, not the image | | 19 | 19 |
| Turn and flip composed in the wrong order | | 2 | 2 |
| Clockwise turns counter-clockwise | | 6 | 6 |
| The next image inherits the turn | | 5 | 5 |
| The full RAW decode undoes the turn | | 2 | 2 |
| Fit measures the image unturned | | 3 | 3 |
| Zoomed in, a turn loses the detail | | 1 | 1 |
| Copy hands over the unturned image | | 1 | 1 |
| Status bar keeps quiet about a turn | | 8 | 8 |
| JPEG EXIF looked for under the wrong name | 1 | 2 | 2 |
| HEIF EXIF read without skipping its offset | 5 | 2 | 2 |
| Fast shutter speeds shown as decimals | 6 | 3 | 3 |
| The make repeated in the camera name | 3 | 4 | 4 |
| The panel keeps the last image's details | | 8 | 8 |
| Fullscreen leaves the panel up | | 1 | 1 |
| RAW: no half-size stage after a small preview | 1 | | |
| RAW: the half-size stage passed off as the final decode | 1 | | |
| Thumbs: clicking one does nothing | | 1 | 1 |
| Thumbs: the strip does not follow the image | | 1 | 1 |
| Thumbs: every row decoded, in sight or not | | 1 | 1 |
| Thumbs: fullscreen leaves the strip up | | 1 | 1 |
| Thumbs: the disk cache is never read | 2 | 1 | 1 |
| Thumbs: an edited file keeps its old thumbnail | 1 | | |
| Thumbs: pruning takes the newest first | 1 | | |
| Save: file and view turns composed the wrong way round | | 1 | 1 |
| Save: the view keeps its turn on the saved file | | 4 | 4 |
| Save: offered for formats that cannot take it | | 1 | 1 |
| Save: the tag patched one byte off | 1 | 2 | 2 |
| Save: new EXIF put ahead of JFIF's APP0 | 1 | | |
| Panel: PNG EXIF never looked for | 1 | 1 | 1 |
| Panel: WebP EXIF never looked for | 1 | 1 | 1 |
| Panel: a TIFF handed to libexif whole | 1 | 1 | 1 |
| Panel: condensing a TIFF drops its GPS | 2 | 1 | 1 |
| Panel: south and west not negated | 4 | 1 | 1 |
| Panel: GPS minutes and seconds ignored | 6 | 3 | 3 |
| Panel: 0,0 taken for a place | 1 | | |
| Panel: the location is not a link | | 1 | 1 |
| Animation: never starts | | 4 | 4 |
| Animation: keeps playing after moving on | | 1 | 1 |
| Animation: each frame resets zoom and turn | | 1 | 1 |
| Animation: P does nothing | | 1 | 1 |
| Animation: pause is live on a still image | | 1 | 1 |
| Slideshow: never advances | | 1 | 1 |
| Slideshow: leaving fullscreen leaves it running | | 2 | 2 |
| Slideshow: the slot starts one with a single image | | 1 | 1 |
| Slideshow: the action is live with a single image | | 1 | 1 |
| Slideshow: stopping leaves a fullscreen the user chose | | 1 | 1 |
| Slideshow: fullscreen forgets the window was maximized | | 1 | 1 |
| Settings: window layout not saved on close | | 2 | 2 |
| Settings: closing in fullscreen saves it hidden | | 1 | 1 |
| Settings: cache size setting ignored | | 1 | 1 |
| Settings: prefetch radius stuck at two | | 1 | 1 |
| Settings: hand-edited values not clamped | 1 | | |
| Settings: a lower cache budget waits for the next insert | 1 | | |

The split is the useful part. Decoding and cache defects fall to the unit
suite in about a second; window and canvas defects only to the functional
suite; and the two display-scaling defects *only* to the run at 2x — at a
ratio of 1 they change nothing, so a suite run only there would pass with both
of them in. Neither layer, nor either scale, covers what the others do.

The rows from the orientation work on are different in kind. They were written
with each feature -- orientation, the info panel, the half-size RAW stage,
thumbnails, saving, animation, the slideshow, settings -- as the likeliest
ways to get it wrong, so each of its tests was seen to fail before it was
trusted to pass.
"1:1 ignores display scaling" now fails eight tests rather than seven because
one of the orientation tests checks that a turn at 1:1 stays 1:1 on screen.

Add a mutation whenever a defect is fixed. If the suites stay green with the
bug back in, the test that was supposed to guard it does not. A mutation whose
anchor is not found fails the run rather than printing SKIP and carrying on —
"nothing was tested" is not a neutral outcome.

`ONLY="regex"` in the environment runs just the mutations whose label
matches, for the few added with a change; the record above is always from a
full run.

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

**RAW decodes in two stages, or three.** Demosaicing a 19 MB CR2 costs over a
second, so the embedded preview is shown within milliseconds and the full
decode replaces it in place without disturbing zoom or pan. Some cameras embed
a small preview -- Sony 1616x1080, Panasonic 1920x1440 -- that is soft on a
large screen for that second, so when the preview's long side is under 2000
pixels and the sensor is at least twice it, a half-size decode comes between:
it skips the demosaic interpolation and costs about a quarter of the full one
(0.3 s for the Sony, against 1.1 s). The full decode runs after it on the same
thread, so the final image arrives that much later; a sharp one arrives three
times sooner.

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

**Rotate and flip turn the picture, not the view, and never the file.** The
turn is a transform on the pixmap item, about its centre; the view's own
transform stays a pure zoom. That is not a matter of taste: `scaleFactor()`
reads the zoom as the transform's `m11`, and a quarter turn makes `m11` zero —
rotate the view and the status bar reads 0%, fit divides by nothing, and every
display-scaling sum above breaks with it. The scene rect follows the turned
item, so fit measures a portrait turned from a landscape as a portrait.
Steps compose in the order pressed (turn-then-flip and flip-then-turn are
different pictures). A new image starts upright; the full decode of a RAW
replacing its preview keeps the turn, since it is the same picture sharper.
Nothing is written back on a keypress: a viewer that rewrites a photograph
uninvited risks the one thing a viewer must not. Copy hands over the image
as shown.

**A turn is saved only on request, only to a JPEG, and only through its
orientation tag.** `Ctrl+S` asks first. Re-encoding a JPEG to rotate it loses
quality every time; the EXIF orientation tag exists so nothing has to be
re-encoded, and every viewer honours it. The tag is written as the file's own
turn composed with the view's -- the order matters as soon as a mirror is
involved. Where the tag exists its two bytes are overwritten in place, so
nothing else about the file changes, not even its creation time. Where it
does not, the new file is built in memory, checked (the tag reads back, it
decodes the right way round, the camera's record is unchanged) and swapped in
with `QSaveFile`, so a failure part-way leaves the original whole. A new EXIF
block goes after JFIF's APP0, which must stay first. RAW and HEIC are not
offered: neither has a safe equivalent.

**The camera's record comes from two libraries, and libexif is the second
choice.** RAW metadata is read by LibRaw, which the RAW decoder has open
anyway and which reaches the maker notes where lens names often live. JPEG
and HEIC/AVIF EXIF goes through libexif: found by walking a JPEG's segment
headers, or handed over by libheif, which stores EXIF as a metadata item
behind a 4-byte offset. exiv2 would have read every format, RAW included, but
MSYS2 did not build it for MINGW64, where this started; on UCRT64 it is
available, but libexif does what is needed and is small. Two
details: libexif's default of "following the specification" rewrites and adds
entries on load, so it is turned off -- the panel shows what the camera wrote.
PNG and WebP carry the block in a chunk of their own. A TIFF is harder: its
EXIF lives in the file's own directories, which libtiff writes after the
pixels, and libexif reads no more than the first 64 KiB of a block -- a test
hands it a whole file to show it finds nothing. So the entries the panel
uses are copied into a compact block first. A GPS position is shown as a link
to OpenStreetMap that opens only if clicked; nothing is looked up. It is read in the decoder, on
the worker thread, so it is cached with the image and costs the GUI nothing.

**Settings are an INI file, and the tests never see the real one.**
`picviewer-cpp.ini` beside the logs, not the registry: one folder holds
everything the application writes, and deleting it resets everything. Values
are clamped on load, since it is a text file anyone can edit. Window geometry
and dock layout are saved on close -- after leaving fullscreen, which hides
the chrome and the panels and must not be what the next session starts with.
Both test binaries point `settings::setFilePath` at a temporary file first.

**Thumbnails are decoded only when seen, and kept on disk.** The strip is a
`QListView` over a model whose decoration is requested only for rows the view
paints, and uniform item sizes are what make that true: without them the view
asks every row for its size, which means its picture, and a folder of sixty
decodes sixty -- a mutation shows it. Requests run newest first on two
threads of their own, so scrolling fills in what is now in sight and the
image being viewed is never held up. A RAW's thumbnail comes from its
embedded preview. Thumbnails are cached in `%LOCALAPPDATA%\PicViewer\thumbnails`
under a hash of path, modified time and size, so an edited file gets a new
one; a hit touches its file, and each start prunes the folder to 200 MB least
recently seen first. In memory they are bounded by bytes, not count.

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
| The log test passed on stale content, and the log was not written during tests at all | It checked only that the log file was not empty, and the development machine's log held every earlier session. Inside `qExec`, QTest installs its own message handler, which keeps messages for the report and forwards none, so nothing a test logs reaches the file. The first CI run, on a machine with no earlier session, found the file empty | `main()` logs a marker unique to the run before any suite starts, and the test reads the file back for it; a mutation that stops flushing each line now fails it |
| A HEIC in a folder called 照片 would not open; a RAW named in Simplified Chinese opened at 200x150 | Paths went to LibRaw and libheif as `toLocal8Bit()`. libheif reads its `char*` as UTF-8, so any non-ASCII character failed; LibRaw's goes through the ANSI code page, so characters outside it failed -- and then the Qt decoder succeeded on the same file by returning the thumbnail in its first TIFF directory. Every sample and test file had an ASCII name, so nothing had ever exercised it | LibRaw opened through its UTF-16 overload, libheif given the mapped bytes; `UnicodePathTests`, nine names in two formats, which on a CI runner's code page 1252 are all outside it |
| Every packaging run, benchmark and package check emptied the user's log | `trim.sh`, `benchmark.ps1`, `sweep.ps1` and `verify_package.ps1` each cleared the application log before a run so that what they read back was theirs -- the user's own log, their history for any bug report. Caught when the session's command classifier refused a diagnostic script that did the same | each notes the log's length first and reads only what is appended (`tools/logtail.ps1`) |
| `trim.sh` called a working package broken | It stopped waiting at the first `decoded` line of any file. Once a RAW gained a half-size stage, a neighbour's prefetch finished first and the RAW was killed mid-decode | it waits for the file under test's own line |
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
